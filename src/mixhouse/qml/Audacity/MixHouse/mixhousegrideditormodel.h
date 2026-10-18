/*
 * GlideMix
 */
#pragma once

#include <memory>
#include <vector>

#include <QObject>
#include <QQmlParserStatus>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration>

#include "async/asyncable.h"
#include "modularity/ioc.h"

#include "mixhouse/imixhousegrideditor.h"
#include "mixhouse/imixhouselibrary.h"
#include "mixhouse/imixhouselibraryconfiguration.h"
#include "mixhouse/imixhouserekordboxphrases.h"
#include "playback/imetronome.h"
#include "projectscene/iprojectsceneconfiguration.h"

namespace mixtape {
class Deck;
struct DeckAudio;
}

namespace au::mixhouse {
/// The Beatgrid editor's state: one library track on a deck of its own, its
/// constant beat grid and its phrases, with rekordbox's GRID EDIT and PHRASE
/// EDIT tools, a CUE button, loops, beat jumps, slip and quantize.
///
/// Grid edits are written to the library at once (detector "manual", so a
/// later rekordbox pass leaves them alone); phrase edits replace rekordbox's
/// phrases for the track. Everything is undoable while the track is open.
class MixHouseGridEditorModel : public QObject, public QQmlParserStatus, public muse::Contextable,
    public muse::async::Asyncable
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY trackChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY trackChanged)
    Q_PROPERTY(QString title READ title NOTIFY trackChanged)
    Q_PROPERTY(QString artist READ artist NOTIFY trackChanged)
    Q_PROPERTY(QString keyText READ keyText NOTIFY trackChanged)
    Q_PROPERTY(double durationSec READ durationSec NOTIFY trackChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY trackChanged)

    Q_PROPERTY(double bpm READ bpm NOTIFY gridChanged)
    Q_PROPERTY(double firstBeatSec READ firstBeatSec NOTIFY gridChanged)
    Q_PROPERTY(double downbeatSec READ downbeatSec NOTIFY gridChanged)
    Q_PROPERTY(bool gridLocked READ gridLocked NOTIFY gridChanged)
    Q_PROPERTY(QString gridSource READ gridSource NOTIFY gridChanged)

    Q_PROPERTY(double positionSec READ positionSec NOTIFY positionChanged)
    Q_PROPERTY(QString barBeatText READ barBeatText NOTIFY positionChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double cueSec READ cueSec NOTIFY cueChanged)

    Q_PROPERTY(bool quantize READ quantize WRITE setQuantize NOTIFY optionsChanged)
    Q_PROPERTY(bool slip READ slip WRITE setSlip NOTIFY optionsChanged)
    Q_PROPERTY(bool metronome READ metronome WRITE setMetronome NOTIFY optionsChanged)
    //! The app's metronome level (the timeline's metronome uses it too), 0..1.
    Q_PROPERTY(double metronomeLevel READ metronomeLevel WRITE setMetronomeLevel NOTIFY optionsChanged)
    Q_PROPERTY(bool loopActive READ loopActive NOTIFY loopChanged)
    Q_PROPERTY(int loopBeats READ loopBeats WRITE setLoopBeats NOTIFY loopChanged)
    Q_PROPERTY(double loopStartSec READ loopStartSec NOTIFY loopChanged)
    Q_PROPERTY(double loopEndSec READ loopEndSec NOTIFY loopChanged)

    Q_PROPERTY(double pxPerSec READ pxPerSec WRITE setPxPerSec NOTIFY zoomChanged)

    Q_PROPERTY(QVariantList phrases READ phrasesVariant NOTIFY phrasesChanged)
    Q_PROPERTY(int selectedPhrase READ selectedPhrase WRITE setSelectedPhrase NOTIFY phrasesChanged)
    Q_PROPERTY(bool phrasesEdited READ phrasesEdited NOTIFY phrasesChanged)
    Q_PROPERTY(QStringList phraseLabels READ phraseLabels CONSTANT)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)

    muse::ContextInject<IMixHouseLibrary> library { this };
    muse::ContextInject<IMixHouseGridEditor> gridEditor { this };
    muse::ContextInject<IMixHouseRekordboxPhrases> rekordboxPhrases { this };
    muse::GlobalInject<IMixHouseLibraryConfiguration> libraryConfiguration;
    muse::GlobalInject<au::playback::IMetronome> appMetronome;
    muse::GlobalInject<au::projectscene::IProjectSceneConfiguration> projectSceneConfiguration;

public:
    explicit MixHouseGridEditorModel(QObject* parent = nullptr);
    ~MixHouseGridEditorModel() override;

    void classBegin() override {}
    void componentComplete() override;

    //! Waveform summary for the views: one entry per kBinFrames of the track.
    struct Waveform {
        double binSec = 0.0;
        std::vector<float> all;
        std::vector<float> low;
        std::vector<float> mid;
        std::vector<float> high;
    };
    std::shared_ptr<const Waveform> waveform() const { return m_waveform; }
    //! rekordbox's RGB waveform colour for band levels 0..1: lows red, mids green,
    //! highs blue, pushed to full brightness so it stays vivid.
    static QColor bandColor(float low, float mid, float high);
    std::shared_ptr<const mixtape::DeckAudio> audio() const { return m_audio; }
    const std::vector<MixHousePhrase>& phraseList() const { return m_phrases; }

    bool loaded() const { return m_audio != nullptr; }
    bool loading() const { return m_loading; }
    QString title() const { return m_title; }
    QString artist() const { return m_artist; }
    QString keyText() const { return m_keyText; }
    double durationSec() const;
    QString errorText() const { return m_errorText; }

    double bpm() const { return m_grid.bpm; }
    double firstBeatSec() const { return m_grid.firstBeatSec; }
    double downbeatSec() const { return m_grid.downbeatSec; }
    bool gridLocked() const { return m_locked; }
    QString gridSource() const { return m_gridSource; }
    double beatSec() const { return m_grid.bpm > 0.0 ? 60.0 / m_grid.bpm : 0.5; }

    double positionSec() const { return m_position; }
    //! Where the deck is right now (between timer ticks too), for smooth drawing.
    double livePosition() const;
    QString barBeatText() const;
    bool playing() const { return m_playing; }
    double cueSec() const { return m_cue; }

    bool quantize() const;
    void setQuantize(bool on);
    bool slip() const { return m_slip; }
    void setSlip(bool on);
    bool metronome() const { return m_metronome; }
    void setMetronome(bool on);
    double metronomeLevel() const;
    void setMetronomeLevel(double level);
    //! The toolbar metronome's menu (level, sound, tune), same items and ids.
    Q_INVOKABLE QVariantList metronomeMenu() const;
    Q_INVOKABLE void handleMetronomeMenu(const QString& itemId);
    bool loopActive() const { return m_loopActive; }
    int loopBeats() const { return m_loopBeats; }
    void setLoopBeats(int beats);
    double loopStartSec() const { return m_loopStart; }
    double loopEndSec() const { return m_loopEnd; }

    double pxPerSec() const { return m_pxPerSec; }
    void setPxPerSec(double pxPerSec);

    QVariantList phrasesVariant() const;
    int selectedPhrase() const { return m_selectedPhrase; }
    void setSelectedPhrase(int index);
    bool phrasesEdited() const { return m_phrasesEdited; }
    QStringList phraseLabels() const;

    bool canUndo() const { return !m_undo.empty(); }
    bool canRedo() const { return !m_redo.empty(); }
    QString statusText() const { return m_status; }

    // Transport
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void cuePressed();
    Q_INVOKABLE void cueReleased();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void seekFraction(double fraction);
    Q_INVOKABLE void scratchBegin();
    Q_INVOKABLE void scratchTo(double seconds);
    Q_INVOKABLE void scratchEnd();
    Q_INVOKABLE void toggleLoop();
    Q_INVOKABLE void halveLoop();
    Q_INVOKABLE void doubleLoop();
    Q_INVOKABLE void jump(int direction);   // by loopBeats beats
    Q_INVOKABLE void jumpToPhrase(int direction);
    Q_INVOKABLE void zoomBy(double factor);

    // GRID EDIT
    Q_INVOKABLE void shiftGrid(double milliseconds);
    Q_INVOKABLE void setBarOneHere();
    Q_INVOKABLE void setBeatHere();
    Q_INVOKABLE void adjustBpm(double delta);
    Q_INVOKABLE void setBpmValue(double bpm);
    Q_INVOKABLE void scaleBpm(double factor);
    Q_INVOKABLE void tap();
    Q_INVOKABLE void toggleGridLock();
    Q_INVOKABLE void resetGridToRekordbox();

    // PHRASE EDIT
    Q_INVOKABLE void selectPhraseAt(double seconds);
    Q_INVOKABLE void cutPhrase();
    Q_INVOKABLE void setPhraseLabel(const QString& label);
    Q_INVOKABLE void deletePhrase();
    Q_INVOKABLE void toggleFill();
    Q_INVOKABLE void resetPhrases();
    //! Any text, e.g. "Vocal drop" instead of "Chorus 1"; the colour stays.
    Q_INVOKABLE void setPhraseName(const QString& name);
    Q_INVOKABLE void setPhraseColor(const QColor& color);
    //! The clip colour palette (Logic's 96, as the clip colour picker): [{index, color}].
    Q_INVOKABLE QVariantList colorPalette() const;
    Q_INVOKABLE void setPhraseColorIndex(int index);
    //! The colour the big waveform's body takes (the library track's colour, or the clip pink).
    QColor bodyColor() const;

    // Dragging in the ribbon (the waveform view): one undo step per drag.
    void beginPhraseDrag();
    //! `edge` 0 = start, 1 = end; the neighbour's edge follows. On bars with Quantize.
    void dragPhraseEdge(int index, int edge, double seconds);
    //! Moves the whole phrase by `delta` seconds from where the drag began.
    void dragPhrase(int index, double delta);
    void endPhraseDrag();
    //! Dragging a loop edge: `edge` 0 = start, 1 = end. On beats with Quantize.
    void dragLoopEdge(int edge, double seconds);

    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Grid helpers for the views
    double nearestBeat(double seconds) const;
    double nearestBar(double seconds) const;
    double barAt(double seconds) const;   // fractional bar number, bar 1 = the downbeat

signals:
    void trackChanged();
    void gridChanged();
    void positionChanged();
    void playingChanged();
    void cueChanged();
    void optionsChanged();
    void loopChanged();
    void zoomChanged();
    void phrasesChanged();
    void historyChanged();
    void statusChanged();
    void waveformChanged();

private:
    struct Grid {
        double bpm = 0.0;
        double firstBeatSec = 0.0;
        double downbeatSec = 0.0;
    };
    struct Snapshot {
        Grid grid;
        std::vector<MixHousePhrase> phrases;
        bool phrasesEdited = false;
    };

    void open(int trackId);
    void onLoaded(int trackId, std::shared_ptr<mixtape::DeckAudio> audio, std::shared_ptr<Waveform> waveform,
                  const QString& error);
    void tick();
    void closeDeck();
    void setStatus(const QString& text);

    double quantized(double seconds) const;
    //! Grid with bpm changed so the beat nearest the playhead stays put.
    Grid withBpm(double bpm) const;
    void applyGrid(const Grid& grid, const QString& what);
    void applyPhrases(std::vector<MixHousePhrase> phrases, const QString& what);
    std::vector<MixHousePhrase> unEditedPhrases() const;
    void pushUndo();
    void restore(const Snapshot& snapshot);
    int phraseIndexAt(double seconds) const;
    void updateDeckGrid();
    //! The app's metronome click, resampled to the track's rate, for the deck.
    void updateDeckClicks();

    int m_trackId = -1;
    QString m_filePath;
    QString m_title;
    QString m_artist;
    QString m_keyText;
    QColor m_trackColor;
    QString m_errorText;
    bool m_loading = false;
    int m_loadGeneration = 0;

    std::shared_ptr<mixtape::DeckAudio> m_audio;
    std::shared_ptr<Waveform> m_waveform;
    std::unique_ptr<mixtape::Deck> m_deck;
    QTimer m_timer;

    Grid m_grid;
    bool m_locked = false;
    QString m_gridSource;
    std::vector<MixHousePhrase> m_phrases;
    bool m_phrasesEdited = false;
    int m_selectedPhrase = -1;

    double m_position = 0.0;
    bool m_playing = false;
    double m_cue = 0.0;
    bool m_cuePreview = false;
    bool m_scratching = false;
    bool m_scratchWasPlaying = false;

    bool m_slip = false;
    bool m_metronome = false;
    bool m_loopActive = false;
    int m_loopBeats = 4;
    double m_loopStart = 0.0;
    double m_loopEnd = 0.0;
    double m_pxPerSec = 200.0;

    std::vector<double> m_taps;
    std::vector<MixHousePhrase> m_dragOriginal;
    bool m_phraseDragging = false;
    std::vector<Snapshot> m_undo;
    std::vector<Snapshot> m_redo;
    QString m_status;
    std::shared_ptr<bool> m_alive;
};
}
