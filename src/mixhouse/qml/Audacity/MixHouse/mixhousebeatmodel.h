/*
 * GlideMix Samples B8: the Beat window's model. A channel-rack style step
 * sequencer (the workflow DJs know from FL Studio, our own code and look):
 * up to 8 rows of the user's own one-shots, 16 steps per bar, 1/2/4 bars,
 * swing. It previews itself as a loop and is written onto a song's sample
 * line as ONE rendered loop (mixhousebeat.h).
 */
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration>
#include <memory>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "project/iprojectconfiguration.h"
#include "trackedit/itrackeditinteraction.h"
#include "trackedit/iprojecthistory.h"

#include "mixhouse/imixhousetempomap.h"
#include "mixhouse/imixhousesamples.h"
#include "mixhouse/imixhouseactivity.h"
#include "mixhouse/mixhousebeat.h"
#include "mixhouse/imixhousebeatgrids.h"
#include "mixhouse/imixhouselibrary.h"

namespace au::mixhouse {
class MixHousePreviewPlayer;

class MixHouseBeatModel : public QObject, public muse::Injectable
{
    Q_OBJECT
    QML_ELEMENT

    //! [{name, path, mute, solo, gain, steps: [0..3]}]
    Q_PROPERTY(QVariantList rows READ rows NOTIFY patternChanged)
    Q_PROPERTY(int bars READ bars WRITE setBars NOTIFY patternChanged)
    //! The grid: 16 (1/16), 32 (x2) or 24 (1/16 triplets).
    Q_PROPERTY(int stepsPerBar READ stepsPerBar WRITE setStepsPerBar NOTIFY patternChanged)
    Q_PROPERTY(bool hasCopiedSteps READ hasCopiedSteps NOTIFY patternChanged)
    Q_PROPERTY(QStringList savedPatterns READ savedPatterns NOTIFY patternChanged)
    Q_PROPERTY(double swing READ swing WRITE setSwing NOTIFY patternChanged)
    //! The swing as the MPC percentage (50 straight .. 75).
    Q_PROPERTY(double swingPercent READ swingPercent WRITE setSwingPercent NOTIFY patternChanged)
    Q_PROPERTY(double bpm READ bpm NOTIFY bpmChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    //! The step the preview is on, -1 when stopped (the running light).
    Q_PROPERTY(int playStep READ playStep NOTIFY playStepChanged)
    //! The Beat starter's genre last used ("" = none).
    Q_PROPERTY(QString genre READ genre NOTIFY patternChanged)
    //! [{id, title, bpm}] for the genre menu.
    Q_PROPERTY(QVariantList genres READ genres CONSTANT)
    //! Tempo: follow the mix under the playhead, or the beat's own BPM.
    Q_PROPERTY(bool followMix READ followMix WRITE setFollowMix NOTIFY bpmChanged)
    Q_PROPERTY(double ownBpm READ ownBpm WRITE setOwnBpm NOTIFY bpmChanged)
    //! The sample line the beat goes onto (from the window's query).
    Q_PROPERTY(int sampleLine READ sampleLine WRITE setSampleLine NOTIFY sampleLineChanged)
    //! The placed beat being edited (its clip on the sample line), -1 = a new beat.
    Q_PROPERTY(int clipId READ clipId WRITE setClipId NOTIFY clipIdChanged)

    muse::Inject<context::IGlobalContext> globalContext = { this };
    muse::Inject<IMixHouseTempoMap> tempoMap = { this };
    muse::Inject<trackedit::ITrackeditInteraction> trackeditInteraction = { this };
    muse::Inject<trackedit::IProjectHistory> projectHistory = { this };
    muse::Inject<IMixHouseActivity> activity = { this };
    muse::GlobalInject<project::IProjectConfiguration> projectConfiguration;
    muse::GlobalInject<IMixHouseSamples> samples;
    muse::Inject<IMixHouseBeatGrids> beatGrids = { this };
    muse::Inject<IMixHouseLibrary> library = { this };

public:
    explicit MixHouseBeatModel(QObject* parent = nullptr);
    ~MixHouseBeatModel() override;

    QVariantList rows() const;
    int bars() const { return s_pattern.bars; }
    void setBars(int bars);
    int stepsPerBar() const { return s_pattern.stepsPerBar; }
    void setStepsPerBar(int spb);
    bool hasCopiedSteps() const { return s_hasClipboard; }
    QStringList savedPatterns() const;
    int totalSteps() const;
    QString patternsFolder() const;
    static BeatRow s_clipboard;
    static bool s_hasClipboard;
    double swing() const { return s_pattern.swing; }
    void setSwing(double swing);
    double swingPercent() const { return beatSwingPercent(s_pattern.swing); }
    void setSwingPercent(double percent) { setSwing((percent - 50.0) / 25.0); }
    double bpm() const;
    bool playing() const { return m_playing; }
    int playStep() const { return m_playStep; }
    int sampleLine() const { return m_sampleLine; }
    void setSampleLine(int id);
    int clipId() const { return m_clipId; }
    void setClipId(int id);

    //! Vibseek's "Send to Sampler": a clip's right-click adds its sound as a
    //! new row of the beat (the open Beat window shows it at once). False
    //! when all rows are taken.
    static bool addSampleRow(const QString& path);

    //! A one-shot dropped on a row (a file path or file:// URL); row = rows.length adds one.
    Q_INVOKABLE void setRowSample(int row, const QString& pathOrUrl);
    Q_INVOKABLE void removeRow(int row);
    Q_INVOKABLE void setStep(int row, int step, int level);   ///< 0 off .. 3 accent
    //! Shift-click: normal (78 %) -> accent (100 %) -> soft (50 %) -> normal.
    Q_INVOKABLE void cycleLevel(int row, int step);
    //! The Graph editor: what = "velocity" (0..1), "pitch" (semitones), "stepPan" (-1..1),
    //! "shift" (0..1 of a step), "repeat" (1..4), "gate" (length 0.05..1),
    //! "reverse" (0/1), "drive", "filter" (1 open), "reverb" (send) 0..1.
    Q_INVOKABLE void setStepValue(int row, int step, const QString& what, double value);
    Q_INVOKABLE void resetStepValue(int row, int step, const QString& what);
    Q_INVOKABLE void toggleMute(int row);
    Q_INVOKABLE void toggleSolo(int row);
    Q_INVOKABLE void setGain(int row, double gain);
    Q_INVOKABLE void setPan(int row, double pan);
    Q_INVOKABLE void toggleCutSelf(int row);
    Q_INVOKABLE void setSwingMix(int row, double mix);
    //! Choke group 0 (none) .. 4: a hit stops the other rows of the group.
    Q_INVOKABLE void setChoke(int row, int group);
    //! The quick fills: q4, q8, q16, offbeat, clear, rnd.
    Q_INVOKABLE void quickFill(int row, const QString& kind);
    //! Serato's Fill in: `division` hits a bar (1, 2, 4, 8, 16, 32), offbeat = between them.
    Q_INVOKABLE void fillIn(int row, int division, bool offbeat);
    //! Per row, like a sampler pad: Attack / Release in seconds, Key shift in semitones.
    Q_INVOKABLE void setAttack(int row, double seconds);
    Q_INVOKABLE void setRelease(int row, double seconds);
    Q_INVOKABLE void setKeyShift(int row, int semitones);
    //! A row's steps to another row (or the same row in another beat).
    Q_INVOKABLE void copySteps(int row);
    Q_INVOKABLE void pasteSteps(int row);
    //! Your own patterns (Serato's "User"): Samples/Beats/Patterns/<name>.glidebeat.
    Q_INVOKABLE bool savePattern(const QString& name);
    //! "" = any saved pattern.
    Q_INVOKABLE void loadPattern(const QString& name);
    //! Advanced fill (FL's tool, our look): `hits` spread over `length` steps
    //! (euclidean = as evenly as possible, else every length/hits steps),
    //! turned by `rotate`; a length shorter than the pattern makes the row repeat
    //! on its own cycle (polymeter).
    Q_INVOKABLE void advancedFill(int row, int hits, int length, int rotate, bool euclidean);
    Q_INVOKABLE void cloneRow(int row);
    //! Clicking a row's name: hear its one-shot once.
    Q_INVOKABLE void previewRow(int row);
    //! The row's tools (its name button's menu).
    Q_INVOKABLE void fill(int row, int every);                 ///< every 1, 2, 4 or 8 steps
    Q_INVOKABLE void evenOut(int row, int hits);               ///< hits spread evenly over the bar
    Q_INVOKABLE void shift(int row, int by);                   ///< steps to the right (negative: left)
    Q_INVOKABLE void reverse(int row);
    Q_INVOKABLE void invert(int row);
    Q_INVOKABLE void randomize(int row);
    Q_INVOKABLE void clearRow(int row);
    Q_INVOKABLE void clearAll();

    //! Beat starter (FL's Loop Starter idea, from the user's OWN samples): a
    //! genre fills the rack with one-shots of the right kinds and a groove.
    //! genre: an id from genres, or "random" (any genre's kit and tempo, its own groove).
    Q_INVOKABLE void startBeat(const QString& genre);
    //! The dice: another sample of the same kind (kick for a kick...).
    Q_INVOKABLE void diceRow(int row);
    //! New sounds for every row that isn't locked.
    Q_INVOKABLE void diceAll();
    Q_INVOKABLE void toggleLock(int row);
    //! A new groove for the row, in the genre's style.
    Q_INVOKABLE void generateSteps(int row);
    //! KEY: tunes the row to the key of the song above the sample line
    //! (the sample's key from its name).
    Q_INVOKABLE void tuneToSong(int row);

    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void stop();
    //! Renders the loop and puts it on the sample line at the bar under the playhead.
    Q_INVOKABLE bool putOnSampleLine();

signals:
    void patternChanged();
    void bpmChanged();
    void playingChanged();
    void playStepChanged();
    void sampleLineChanged();
    void clipIdChanged();

private:
    BeatRow* row(int index);
    void changed();
    bool renderPreview();
    QString beatsFolder() const;
    QString genre() const { return s_genre; }
    QVariantList genres() const;
    static QString genreTitle(const QString& id);
    bool followMix() const { return s_followMix; }
    void setFollowMix(bool follow);
    double ownBpm() const { return s_ownBpm; }
    void setOwnBpm(double bpm);
    static bool s_followMix;
    static double s_ownBpm;
    QString pickSample(const QString& role, const QString& avoid) const;
    void applyTemplate(BeatRow& row, bool vary) const;
    static QString s_genre;

    //! The pattern stays while GlideMix runs: closing and reopening the window keeps the beat.
    static BeatPattern s_pattern;
    static std::vector<MixHouseBeatModel*> s_live;
    int m_sampleLine = -1;
    int m_clipId = -1;
    bool m_playing = false;
    int m_playStep = -1;
    double m_renderedBpm = 0.0;
    std::unique_ptr<MixHousePreviewPlayer> m_player;
    std::unique_ptr<MixHousePreviewPlayer> m_shotPlayer;
    QTimer m_poll;
    QTimer m_rerender;
    QTimer m_queuePoll;
    QString m_previewPath;
};
}
