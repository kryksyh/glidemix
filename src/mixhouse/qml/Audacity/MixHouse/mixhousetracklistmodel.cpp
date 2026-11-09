/*
 * MixHouse DJ 2
 *
 * QML-facing list of the tracks in Mixxx's library. Mostly presentation, but
 * loadTrackToProject() is real: it hands the track's actual file to Audacity's
 * own import pipeline (project/iaudacityproject.h), the same one File > Import
 * Audio uses. Live DJ-style preview/cueing still needs the engine bridge --
 * this is "add it to the project as a real track", not playback.
 */
#include "mixhousetracklistmodel.h"
#include "mixhouse/internal/mixhousedjapps.h"
#include "mixhouse/internal/mixhousepreviewplayer.h"
#include "mixhouse/internal/mixhouseprojectaligner.h"
#include "mixhouse/imixhousekeymatch.h"
#include "mixhouse/mixhousesearchquery.h"
#include "global/settings.h"

#include <limits>
#include "mixhouse/mixhousematch.h"
#include "mixhouse/imixhouseexternallibrary.h"
#include <QObject>
#include <QProcess>
#include <QFile>
#include <QDateTime>
#include <QElapsedTimer>
#include <QtGlobal>
#if defined(Q_OS_MACOS)
#include <ApplicationServices/ApplicationServices.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#endif
#include <QDrag>
#include <QThread>
#include <QWindow>
#include <QMouseEvent>
#include <QCursor>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTextStream>
#include <QRegularExpression>
#include <QDesktopServices>
#include <QPointer>
#include <thread>
#include "mixhouse/mixhousefiletags.h"
#include "mixhouse/mixhouserekordboxxmlwriter.h"
#include "mixhouse/mixhousephrasecues.h"
#include <QStorageInfo>
#include <QSettings>
#include <QDate>
#include <QTimer>
#include <functional>
#include <QDirIterator>
#include <QFileInfo>
#include <QPainter>
#include <QFontMetrics>
#include <QGuiApplication>

#include <algorithm>

#include "project/iaudacityproject.h"
#include "translation.h"
#include "log.h"

using namespace au::mixhouse;

namespace {
//! GlideMix: the library and the Related tab share the Match/Related rules.
std::vector<au::mixhouse::MixHouseTrackListModel*> s_liveModels;
}

MixHouseTrackListModel::MixHouseTrackListModel(QObject* parent)
    : QAbstractListModel(parent), muse::Injectable(muse::iocCtxForQmlObject(this))
{
    // No IoC resolution here -- see load()'s own comment on why that
    // crashed this app on every startup. Batch analysis used to need its
    // own QTimer wired up here (a real per-tick queue this model owned
    // directly); it now just delegates to MixHouseLibrary's own real
    // background worker pool (startBackgroundAnalysis), which owns its own
    // threads -- nothing left for this constructor to wire.
    s_liveModels.push_back(this);
    loadRules();
    // GlideMix Match and Related: on/off and widths are remembered.
    m_matchOn = QSettings().value(QStringLiteral("GlideMix/matchOn"), false).toBool();
    m_matchWidth = std::clamp(QSettings().value(QStringLiteral("GlideMix/matchKeyWidth"), 2).toInt(), 0, 3);
    m_relatedOn = QSettings().value(QStringLiteral("GlideMix/relatedOn"), false).toBool();
    m_unanalyzedCountTimer.setSingleShot(true);
    m_unanalyzedCountTimer.setInterval(1000);
    QObject::connect(&m_unanalyzedCountTimer, &QTimer::timeout, this, [this]() {
        const int count = library()->unanalyzedTrackCount();
        if (count != m_unanalyzedCount) {
            m_unanalyzedCount = count;
            emit sidebarCountsChanged();
        }
    });
    m_countsTimer.setSingleShot(true);
    m_countsTimer.setInterval(3000);
    QObject::connect(&m_countsTimer, &QTimer::timeout, this, [this]() {
        // GlideMix: counting reads the whole library (~0.4 s for 37,000
        // songs), so while analysis keeps changing it, at most every 30 s.
        constexpr qint64 minGap = 30000;
        if (m_lastRecount.isValid() && m_lastRecount.elapsed() < minGap) {
            m_countsTimer.start(static_cast<int>(minGap - m_lastRecount.elapsed()));
            return;
        }
        m_countsTimer.setInterval(3000);
        recountSidebar();
    });
}

MixHouseTrackListModel::~MixHouseTrackListModel()
{
    s_liveModels.erase(std::remove(s_liveModels.begin(), s_liveModels.end(), this), s_liveModels.end());
    if (m_previewPlayer) {
        m_previewPlayer->stop();
    }
}

double MixHouseTrackListModel::previewVolume() const
{
    return QSettings().value(QStringLiteral("GlideMix/previewVolume"), 0.7).toDouble();
}

void MixHouseTrackListModel::setPreviewVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    QSettings().setValue(QStringLiteral("GlideMix/previewVolume"), volume);
    if (m_previewPlayer) {
        m_previewPlayer->setVolume(volume);
    }
    emit previewStateChanged();
}

void MixHouseTrackListModel::ensurePreviewPlayer()
{
    if (m_previewPlayer) {
        return;
    }
    m_previewPlayer = std::make_unique<MixHousePreviewPlayer>();
    m_previewPlayer->setVolume(previewVolume());

    // MixHouse DJ's OverviewDelegate: "Seeks are coalesced rather than issued
    // per click" -- restarted on each request, so a burst of clicks costs
    // one seek. Same 90ms.
    m_previewSeekTimer = new QTimer(this);
    m_previewSeekTimer->setSingleShot(true);
    m_previewSeekTimer->setInterval(90);
    connect(m_previewSeekTimer, &QTimer::timeout, this, [this]() {
        if (m_pendingPreviewSeek >= 0.0) {
            double rate = 1.0;
            const double at = syncedPreviewFraction(m_previewTrack, m_pendingPreviewSeek, rate);
            m_previewPlayer->setRate(rate);
            m_previewPlayer->playFrom(at);
            m_pendingPreviewSeek = -1.0;
            updatePreviewPosition();
        }
    });

    // Same delegate: the playhead refreshes at 10Hz ("plenty for a playhead
    // in a table cell"), and only while previewing.
    m_previewPollTimer = new QTimer(this);
    m_previewPollTimer->setInterval(100);
    connect(m_previewPollTimer, &QTimer::timeout, this, [this]() { updatePreviewPosition(); });
}

void MixHouseTrackListModel::updatePreviewPosition()
{
    if (!m_previewPlayer) {
        return;
    }
    const bool playing = m_previewPlayer->isPlaying();
    const double position = m_previewPlayer->position();
    if (position != m_previewPosition) {
        m_previewPosition = position;
        emit previewPositionChanged();
    }
    if (playing != m_previewPlaying) {
        m_previewPlaying = playing;   // false here = reached the end of the track
        emit previewStateChanged();
    }
    if (!playing) {
        m_previewPollTimer->stop();
    }
}

void MixHouseTrackListModel::previewTrackAt(int row, double fraction)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) {
        return;
    }
    const MixHouseTrack& track = m_tracks[static_cast<size_t>(row)];
    ensurePreviewPlayer();
    fraction = std::clamp(fraction, 0.0, 1.0);

    if (track.id == m_previewTrackId && m_previewPlaying) {
        // Already playing this track: jump to the clicked spot, debounced.
        m_pendingPreviewSeek = fraction;
        m_previewSeekTimer->start();
        return;
    }

    // A different track, or this one while stopped. The fork loaded into
    // mixxx's preview deck and then waited 140ms for the load to reset the
    // position before seeking; AVAudioFile opens synchronously, so playback
    // starts at the clicked spot straight away instead.
    if (track.id != m_previewTrackId) {
        const std::string device = muse::settings()->value(
            muse::Settings::Key("au3audio", "AudioIO/PlaybackDevice")).toString();
        if (!m_previewPlayer->load(track.filePath.toStdString(), device)) {
            LOGW() << "GlideMix preview: cannot load " << track.filePath;
            stopPreview();
            return;
        }
        m_previewTrackId = track.id;
        m_previewTrack = track;
    }
    m_pendingPreviewSeek = -1.0;
    m_previewSeekTimer->stop();
    double rate = 1.0;
    fraction = syncedPreviewFraction(track, fraction, rate);
    m_previewPlayer->setRate(rate);
    m_previewPlayer->playFrom(fraction);
    m_previewPlaying = m_previewPlayer->isPlaying();
    m_previewPosition = m_previewPlayer->position();
    emit previewStateChanged();
    emit previewPositionChanged();
    if (m_previewPlaying) {
        m_previewPollTimer->start();
    }
}

double MixHouseTrackListModel::syncedPreviewFraction(const MixHouseTrack& track, double fraction, double& rate) const
{
    // GlideMix (Naveen G, MixMeister): while the mix plays, a click on a
    // library waveform starts the song on the same beat of the bar as the
    // timeline, at the timeline's tempo, so you hear at once if the two fit.
    rate = 1.0;
    const auto state = globalContext() ? globalContext()->playbackState() : nullptr;
    if (!state || state->playbackStatus() != playback::PlaybackStatus::Running || !tempoMap()
        || !track.beats.valid || track.beats.beatLengthSec <= 0.0 || track.durationSec <= 0.0) {
        return fraction;
    }
    const double now = state->playbackPosition().to_double();
    const double timelineBpm = tempoMap()->bpmAt(now);
    const double ratio = timelineBpm / (60.0 / track.beats.beatLengthSec);
    if (!(ratio >= 0.8 && ratio <= 1.25)) {
        return fraction;   // too far apart to play together
    }
    const double beat = tempoMap()->secToBeat(now);
    const double inBar = beat - 4.0 * std::floor(beat / 4.0);
    const double beatLength = track.beats.beatLengthSec;
    const double downbeat = track.downbeatSec >= 0.0 ? track.downbeatSec : track.beats.firstBeatSec;
    const double clicked = std::clamp(fraction, 0.0, 1.0) * track.durationSec;
    double start = downbeat + (4.0 * std::round((clicked - downbeat) / (4.0 * beatLength)) + inBar) * beatLength;
    while (start < 0.0) {
        start += 4.0 * beatLength;
    }
    if (start >= track.durationSec) {
        return fraction;
    }
    rate = ratio;
    LOGI() << "GlideMix preview in time with the mix: " << timelineBpm << " BPM (x" << ratio << "), bar beat "
           << inBar << ", from " << start << " s";
    return start / track.durationSec;
}

void MixHouseTrackListModel::previewNextPhrase(int row)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) {
        return;
    }
    const MixHouseTrack& t = m_tracks[static_cast<size_t>(row)];
    if (t.durationSec <= 0.0) {
        return;
    }
    // The phrases the Beatgrid editor shows: edited ones, else rekordbox's,
    // else the GlideMix Phrase Detector's.
    std::vector<MixHousePhrase> phrases;
    if (t.id >= 0) {
        if (const auto edited = library()->trackPhraseEdits(t.id)) {
            phrases = *edited;
        }
    }
    if (phrases.empty() && rekordboxAnalysis()) {
        phrases = rekordboxAnalysis()->phrases(t.filePath, t.bpm);
    }
    if (phrases.empty() && t.id >= 0) {
        phrases = library()->detectedPhrases(t.id);
    }
    const bool playingThis = t.id == m_previewTrackId && m_previewPlaying;
    if (!playingThis) {
        previewTrackAt(row, 0.0);   // not playing yet: from the start
        return;
    }
    // Skip ahead as rekordbox does (it jumps 30 s): to the next phrase when
    // it starts within 16 bars, else 16 bars on (30 s without a tempo).
    const double now = m_previewPosition * t.durationSec;
    const double step = t.bpm > 0.0 ? 16.0 * 4.0 * 60.0 / t.bpm : 30.0;
    double target = now + step;
    for (const MixHousePhrase& p : phrases) {
        if (p.startSec > now + 0.5) {
            target = std::min(target, p.startSec);
            break;
        }
    }
    if (target < t.durationSec - 1.0) {
        previewTrackAt(row, target / t.durationSec);
    }
}

void MixHouseTrackListModel::stopPreview()
{
    if (!m_previewPlayer) {
        return;
    }
    m_pendingPreviewSeek = -1.0;
    m_previewSeekTimer->stop();
    m_previewPollTimer->stop();
    m_previewPlayer->stop();
    m_previewPosition = m_previewPlayer->position();
    const bool changed = m_previewPlaying;
    m_previewPlaying = false;
    emit previewPositionChanged();
    if (changed) {
        emit previewStateChanged();
    }
}

void MixHouseTrackListModel::load()
{
    m_overviewCueCache.clear();   // phrases may have changed (analysis, edits)
    // Subscribed here, NOT in the constructor: resolving an injected
    // dependency (library()) that early crashed this app on every single
    // startup -- confirmed directly from real .ips crash reports, all three
    // pointing at this exact constructor calling into IoC resolution before
    // this object's muse::Injectable context was actually ready for it. By
    // the time QML calls load() (Component.onCompleted, after this object is
    // fully constructed and parented), resolving library() is the same safe
    // operation every other method here already does. Guarded to subscribe
    // only once no matter how many times load() itself gets called.
    if (!m_ruleDataReady) {
        m_ruleDataReady = true;
        prepareRuleData();
    }
    if ((m_matchOn || m_relatedOn) && !m_referenceSubscribed) {
        subscribeReference();   // injected services are ready here, not in the constructor
        m_referenceTimer.start();
    }
    if (!m_subscribedToExternal && rekordboxXml()) {
        m_subscribedToExternal = true;
        // GlideMix: the DJ apps load in the background; show them once read.
        {
            auto* poll = new QTimer(this);
            poll->setInterval(1000);
            m_hiddenDjApps = hiddenDjApps();
            checkDjAppPlaces();
            connect(poll, &QTimer::timeout, this, [this]() {
                // Preferences > Library switched an app on or off: read them again.
                const QStringList hidden = hiddenDjApps();
                if (hidden != m_hiddenDjApps) {
                    m_hiddenDjApps = hidden;
                    reloadDjApps();
                    emit loaded();
                }
                if (++m_djAppPlacesTick % 3 == 0) {   // drives: every 3 s is plenty
                    checkDjAppPlaces();
                }
                if (djAppsGeneration() != m_djAppsGeneration) {
                    m_djAppsGeneration = djAppsGeneration();
                    emit loaded();   // the sidebar re-reads its nodes
                }
            });
            poll->start();
        }
        if (projectHistory()) {
            projectHistory()->historyChanged().onReceive(this, [this](trackedit::HistoryEvent) { updateProjectFiles(); });
        }
        if (globalContext()) {
            globalContext()->currentTrackeditProjectChanged().onNotify(this, [this]() {
                updateProjectFiles();
            });
        }
        updateProjectFiles();   // a project may be open already
        // A rekordbox XML read finishes in the background: refresh the
        // sidebar (and the table, when it shows that XML).
        rekordboxXml()->changed().onNotify(this, [this]() {
            // Reading (again): follow its progress for the sidebar row.
            if (rekordboxXml()->loading() && !m_rekordboxProgressTimer.isActive()) {
                m_rekordboxProgressTimer.setInterval(250);
                QObject::connect(&m_rekordboxProgressTimer, &QTimer::timeout, this, [this]() {
                    const int progress = rekordboxLoadProgress();
                    if (progress != m_rekordboxProgress) {
                        m_rekordboxProgress = progress;
                        emit rekordboxLoadProgressChanged();
                    }
                    if (progress < 0) {
                        m_rekordboxProgressTimer.stop();
                    }
                }, Qt::UniqueConnection);
                m_rekordboxProgressTimer.start();
            }
            m_rekordboxProgress = rekordboxLoadProgress();
            emit rekordboxLoadProgressChanged();
            load();
            emit loaded();   // the sidebar row: Loading, then the count and the playlists
            // A file just chosen (here or in File > Add to Library) that
            // isn't a rekordbox collection: say so once.
            if (!rekordboxXml()->loading() && rekordboxXml()->path() != m_rekordboxCheckedPath) {
                m_rekordboxCheckedPath = rekordboxXml()->path();
                const QString error = rekordboxXml()->loadError();
                if (m_rekordboxImportShown && !m_rekordboxImportDone) {
                    if (!error.isEmpty()) {
                        closeRekordboxImport();   // the warning below says why
                    } else {
                        m_rekordboxImportDone = true;
                        const int songs = rekordboxXml()->trackCount();
                        m_rekordboxImportStatus = muse::qtrc("mixhouse",
                            "%1 songs read. They are under Rekordbox XML in the sidebar. To add them to your "
                            "Music Collection, use Copy Rekordbox XML into Music Collection…").arg(QLocale().toString(songs));
                        emit rekordboxImportChanged();
                    }
                }
                if (!error.isEmpty()) {
                    interactive()->warning(muse::trc("mixhouse", "Rekordbox XML"),
                                           muse::qtrc("mixhouse", "This file couldn't be read as a rekordbox collection: %1\n\n"
                                                                  "In rekordbox: File > Export Collection in xml format.")
                                           .arg(error).toStdString());
                }
            }
        });
    }
    if (!m_subscribedToLibraryChanged) {
        m_subscribedToLibraryChanged = true;
        library()->libraryChanged().onNotify(this, [this]() {
            m_customTagCache.clear();
            // Bulk changes (a scan, an import, grids): read the index again.
            // Single-song edits are picked up by takeChangedTrackIds().
            m_indexLoaded = false;
            load();
            m_countsStale = true;
            // Not restarted while waiting: analysis changes the library every
            // second, and a restarting timer never fired.
            if (!m_countsTimer.isActive()) {
                m_countsTimer.start();
            }
        });
    }
    // Same reasoning, same guard pattern -- see this model's own
    // libraryRowHeight/greyOutPlayedTracks/searchDebounceMs Q_PROPERTY
    // comments for why this exists at all (a real bug testing caught: these
    // used to be CONSTANT, so a Preferences change never reached an
    // already-open panel).
    if (!m_subscribedToSettingsChanged) {
        m_subscribedToSettingsChanged = true;
        m_keyNotation = static_cast<int>(libraryConfiguration()->keyNotation());
        libraryConfiguration()->settingsChanged().onNotify(this, [this]() {
            // The Key column's text is written when the tracks are read, so a
            // new notation needs them read again.
            const int notation = static_cast<int>(libraryConfiguration()->keyNotation());
            if (notation != m_keyNotation) {
                m_keyNotation = notation;
                emit keyNotationChanged();
                load();
            }
            emit libraryRowHeightChanged();
            emit greyOutPlayedTracksChanged();
            emit keyBadgesChanged();
            emit searchDebounceMsChanged();
            emit libraryFontChanged();
            emit bpmPrecisionChanged();
            emit waveformOverviewTypeChanged();
            emit showWaveformMinuteMarkersChanged();
        });
    }
    // Same reasoning, same guard pattern. Both channels are re-published by
    // MixHouseLibrary from its own real Qt signal/slot handlers -- already
    // back on the main thread by the time they reach here (see
    // MixHouseLibraryScanner's own header comment for why that hop uses
    // plain Qt signals rather than muse::async::Channel directly), so
    // ordinary same-thread onReceive() usage here is exactly as safe as
    // every other muse::async subscription in this file.
    if (!m_subscribedToScan) {
        m_subscribedToScan = true;
        library()->scanProgress().onReceive(this, [this](int scanned, int added) {
            m_scanScanned = scanned;
            m_scanAdded = added;
            emit scanProgressChanged();
        });
        library()->revealTrackRequested().onReceive(this, [this](int trackId, IMixHouseLibrary::RevealMode mode) {
            emit revealTrackRequested(trackId, static_cast<int>(mode));
        });
        library()->scanFinished().onReceive(this, [this](const IMixHouseLibrary::ScanResult& result) {
            emit scanningChanged();
            if (!result.error.isEmpty()) {
                interactive()->error(muse::trc("mixhouse", "Couldn't scan music directories"),
                                      result.error.toStdString());
                return;
            }
            if (result.watch) {
                // Watch folders: a line beside Analyze, not a dialog.
                if (result.added > 0) {
                    const QString text = muse::qtrc("mixhouse", "%n new song(s) added from your music folders.",
                                                    nullptr, result.added);
                    setAnalysisMessage(text);
                    QTimer::singleShot(8000, this, [this, text]() {
                        if (m_analysisMessage == text) {
                            setAnalysisMessage(QString());
                        }
                    });
                }
                return;
            }
            if (libraryConfiguration()->showScanSummaryDialog() && result.scanned > 0) {
                const std::string title = muse::trc("mixhouse", "Library scan finished");
                std::string text = muse::qtrc("mixhouse",
                    "Scanned %1 file(s): %2 added to the library, %3 already known.", nullptr, result.scanned)
                    .arg(result.scanned).arg(result.added).arg(result.skipped).toStdString();
        if (result.unreadable > 0) {
            text += "\n" + muse::qtrc("mixhouse", "%n file(s) could not be read as audio (damaged or not audio) and were not added.",
                                        nullptr, result.unreadable).toStdString();
        }
                interactive()->info(title, text);
            }
        });
    }
    // Same reasoning, same guard pattern -- see IMixHouseLibrary::
    // startBackgroundAnalysis's own comment. analysisFinished's own summary
    // text is exactly what analyzeTracksWithConfirm's old blocking for-loop
    // used to show synchronously right after finishing; now shown here
    // instead since the actual work is async, and shared by BOTH the
    // toolbar's plain Analyze button (previously silent on completion) and
    // the right-click confirm-dialog path -- one real completion notice
    // for any batch, same as scanFinished's own dialog above.
    // The rekordbox index is built on a background thread. Rows painted before
    // it finishes cached our own fallback overview, so drop the cache and tell
    // the overview column to fetch again.
    // GlideMix: songs' HD waveforms finishing in the background.
    if (!m_subscribedToHdWaves && hdWaveforms()) {
        m_subscribedToHdWaves = true;
        hdWaveforms()->ready().onNotify(this, [this]() {
            ++m_hdWaveRevision;
            emit hdWaveRevisionChanged();
        });
    }
    if (!m_subscribedToRekordbox && rekordboxAnalysis()) {
        m_subscribedToRekordbox = true;
        rekordboxAnalysis()->ready().onNotify(this, [this]() {
            m_overviewCache.clear();
            m_overviewCueCache.clear();
            emit overviewDataChanged();
        });
    }
    if (!m_subscribedToAnalysis) {
        m_subscribedToAnalysis = true;
        library()->analysisProgress().onReceive(this, [this](int processed, int total) {
            m_analyzeProcessed = processed;
            m_analyzeTotal = total;
            emit analyzeProgressChanged();
            // GlideMix: the Analyze row counts down as songs finish (it stood
            // still for 30 s and more), at most once a second.
            if (!m_unanalyzedCountTimer.isActive()) {
                m_unanalyzedCountTimer.start();
            }
        });
        library()->trackAnalysisProgress().onReceive(this, [this](int trackId, int percent) {
            if (percent == -3) {
                // Only the energy changed (rated for the ENERGY lane).
                IMixHouseLibrary::TrackQuery one;
                one.trackId = trackId;
                const MixHouseTrackList fresh = library()->tracks(one);
                if (!fresh.empty()) {
                    updateIndexRow(fresh.front());
                }
                for (MixHouseTrack& t : m_tracks) {
                    if (t.id == trackId && !fresh.empty()) {
                        t.energy = fresh.front().energy;
                        emitAnalysisProgressChanged(trackId, true);
                        break;
                    }
                }
                return;
            }
            if (percent < 0) {
                if (!m_analysisProgressByTrack.remove(trackId)) {
                    return;
                }
                m_overviewCache.remove(trackId);   // the fresh waveform was just written
                m_overviewCueCache.remove(trackId);
                if (percent == -1 && m_indexLoaded) {
                    // GlideMix fast library: the in-memory row too, shown or not.
                    IMixHouseLibrary::TrackQuery one;
                    one.trackId = trackId;
                    const MixHouseTrackList fresh = library()->tracks(one);
                    if (!fresh.empty()) {
                        updateIndexRow(fresh.front());
                    }
                }
                if (percent == -1) {
                    // Succeeded: re-read this one row now (BPM, key, grid,
                    // downbeat, "Analyzed") instead of leaving "--" until the
                    // batch-end load() -- we saw BPM "not being written"
                    // during a long batch, though it already was in the DB.
                    for (MixHouseTrack& t : m_tracks) {
                        if (t.id != trackId) {
                            continue;
                        }
                        IMixHouseLibrary::TrackQuery one;
                        one.trackId = trackId;
                        one.hiddenOnly = m_query.hiddenOnly;
                        const MixHouseTrackList fresh = library()->tracks(one);
                        if (!fresh.empty()) {
                            const int playlistPosition = t.playlistPosition;
                            t = fresh.front();
                            t.playlistPosition = playlistPosition;
                        } else {
                            t.analyzed = true;
                        }
                        break;
                    }
                }
            } else {
                // A batch this model didn't start (new songs from watch
                // folders): the Analyze button turns into Stop.
                if (m_analysisProgressByTrack.isEmpty()) {
                    emit analyzingChanged();
                }
                m_analysisProgressByTrack.insert(trackId, percent);
            }
            emitAnalysisProgressChanged(trackId, percent < 0);
        });
        library()->analysisFinished().onReceive(this, [this](const IMixHouseLibrary::AnalysisSummary& summary) {
            // Stopped batches leave never-started tracks behind at 0% --
            // clear them before load()'s reset so nothing reads stale.
            m_analysisProgressByTrack.clear();
            // External rows show the analysis of their library copy.
            for (auto it = m_overviewCache.begin(); it != m_overviewCache.end();) {
                it = isExternalLibraryTrack(it.key()) ? m_overviewCache.erase(it) : std::next(it);
            }
            emit analyzingChanged();
            load();
            if (summary.total <= 0) {
                return;
            }
            // GlideMix: a few seconds of status text beside the Analyze button,
            // not a dialog -- the modal box opened while the list reloaded
            // and showed up as an empty window (and interrupted the work).
            QString text = summary.stopped
                ? muse::qtrc("mixhouse", "Analysis stopped: %n track(s) analyzed.", nullptr, summary.succeeded)
                : muse::qtrc("mixhouse", "Analysis finished: %n track(s) analyzed.", nullptr, summary.succeeded);
            if (summary.failed > 0) {
                text += QStringLiteral(" ") + muse::qtrc("mixhouse", "%n could not be read.", nullptr, summary.failed);
            }
            setAnalysisMessage(text);
            QTimer::singleShot(8000, this, [this, text]() {
                if (m_analysisMessage == text) {
                    setAnalysisMessage(QString());
                }
            });
        });
    }

    QElapsedTimer loadTimer;
    loadTimer.start();
    MixHouseTrackList fresh;
    if (!m_emptyPlaceholder) {
        // Capped: the panel shows a page of this, and an unbounded query on a large
        // collection would stall the UI thread. Sorting happens in SQL so the cap
        // takes the right rows, not an arbitrary slice.
        // GlideMix (Marc Mont): 2000 at a time; scrolling near the end loads
        // the next 2000 (loadMore). A different list or search starts again.
        const QString view = m_externalSource + QLatin1Char('|') + QString::number(m_externalPlaylist) + QLatin1Char('|')
                             + m_query.filter + QLatin1Char('|') + m_query.keyFilters.join(QLatin1Char(','))
                             + QStringLiteral("|%1|%2|%3%4%5%6|%7%8").arg(m_query.playlistId).arg(m_query.crateId)
                             .arg(m_query.missingOnly).arg(m_query.unanalyzedOnly).arg(m_query.playedOnly)
                             .arg(m_query.hiddenOnly).arg(static_cast<int>(m_query.sort)).arg(m_query.ascending);
        if (view != m_pageView) {
            m_pageView = view;
            m_pageLimit = kPageSize;
        }
        m_query.limit = m_pageLimit;
        if (m_relatedView) {
            fresh = MixHouseTrackList(m_relatedTracks.begin(), m_relatedTracks.end());
            if (m_relatedSortByColumn) {
                IMixHouseLibrary::TrackQuery q = m_query;
                q.limit = 0;
                sortAndLimitTracks(fresh, q, false);
            }
            m_pageLimit = std::numeric_limits<int>::max();
        } else if (m_externalSource == u"folder") {
            fresh = browseFolderTracks();
        } else if (m_externalSource == u"smart") {
            fresh = smartPlaylistTracks(m_externalPlaylist);
        } else if (m_externalSource == u"duplicates") {
            fresh = duplicateTracks();
        } else if (!m_externalSource.isEmpty()) {
            const IMixHouseExternalLibrary* external = externalLibrary(m_externalSource);
            // GlideMix fast library: every song of the other apps' lists, no
            // 2000 pages (rekordbox XML and the DJ apps are in memory already;
            // Lexicon reads its own database per view).
            IMixHouseLibrary::TrackQuery q = m_query;
            q.limit = 0;   // every song (Lexicon too: measured before keeping it)
            m_pageLimit = std::numeric_limits<int>::max();
            QElapsedTimer externalTook;
            externalTook.start();
            fresh = external ? external->tracks(m_externalPlaylist, q) : MixHouseTrackList();
            LOGI() << "GlideMix fast library: " << m_externalSource << " view of " << fresh.size() << " songs in "
                   << externalTook.elapsed() << " ms";
            applyOwnOverrides(fresh);
        } else if (!m_query.hiddenOnly) {
            fresh = indexedTracks();   // GlideMix (fast library): every row, from memory
            m_pageLimit = std::numeric_limits<int>::max();   // nothing more to load
        } else {
            fresh = library()->tracks(m_query);
        }
    }
    // GlideMix: the same tracks in the same order (a song loaded and marked
    // played, an analysis finishing): update the rows in place. A reset
    // destroyed every row, and a double-click that landed then was lost --
    // "track 6 is not created" (Chris, Mac).
    const bool sameRows = !m_tracks.empty() && fresh.size() == m_tracks.size()
                          && std::equal(fresh.begin(), fresh.end(), m_tracks.begin(),
                                        [](const MixHouseTrack& a, const MixHouseTrack& b) { return a.id == b.id; });
    if (sameRows) {
        m_tracks = std::move(fresh);
        // Every column: index(row) alone is column 0, and the table's other
        // columns (Rating, Color...) kept their old value until a view change.
        emit dataChanged(index(0, 0), index(static_cast<int>(m_tracks.size()) - 1, std::max(0, m_uiColumnCount - 1)));
        return;
    }
    // More of the same list (loadMore): add the new rows, so the view stays put.
    const bool grows = !m_tracks.empty() && fresh.size() > m_tracks.size()
                       && std::equal(m_tracks.begin(), m_tracks.end(), fresh.begin(),
                                     [](const MixHouseTrack& a, const MixHouseTrack& b) { return a.id == b.id; });
    if (grows) {
        beginInsertRows(QModelIndex(), static_cast<int>(m_tracks.size()), static_cast<int>(fresh.size()) - 1);
        m_tracks = std::move(fresh);
        endInsertRows();
        emit loaded();
        return;
    }
    beginResetModel();
    m_tracks = std::move(fresh);
    endResetModel();
    emit loaded();
    if (loadTimer.elapsed() >= 50) {
        LOGI() << "GlideMix library view: " << m_tracks.size() << " rows loaded in " << loadTimer.elapsed() << " ms";
    }
}

bool MixHouseTrackListModel::canLoadMore() const
{
    return static_cast<int>(m_tracks.size()) >= m_pageLimit;
}

void MixHouseTrackListModel::loadMore()
{
    if (!canLoadMore()) {
        return;
    }
    m_pageLimit += kPageSize;
    load();
    LOGI() << "GlideMix library: list now shows " << m_tracks.size() << " songs (page " << m_pageLimit << ")";
}

void MixHouseTrackListModel::sortBy(int sortKey)
{
    // GlideMix (Chris): three clicks -- ascending, descending, then back to
    // the view's own order (the library's default, a playlist's or folder's
    // order, Related's closest-first).
    const auto key = static_cast<IMixHouseLibrary::SortKey>(sortKey);
    const auto [defKey, defAscending] = viewDefaultSort();
    const bool atDefault = m_relatedView ? !m_relatedSortByColumn
                                         : (m_query.sort == defKey && m_query.ascending == defAscending);
    if (m_query.sort == key && !atDefault && !m_query.ascending) {
        // Third click: the view's own order.
        m_query.sort = defKey;
        m_query.ascending = defAscending;
        m_relatedSortByColumn = false;
        load();
        return;
    }
    if (m_query.sort == key && !(m_relatedView && atDefault)) {
        m_query.ascending = !m_query.ascending;   // same column: flip
    } else {
        m_query.sort = key;
        m_query.ascending = true;                 // new column: start ascending
    }
    m_relatedSortByColumn = m_relatedView;
    load();
}

namespace {
IMixHouseLibrary::SortKey sortKeyForSmartField(const QString& field);
}

std::pair<IMixHouseLibrary::SortKey, bool> MixHouseTrackListModel::viewDefaultSort() const
{
    using K = IMixHouseLibrary::SortKey;
    if (m_relatedView) {
        return { K::Position, true };   // not a column: closest songs first
    }
    if (m_externalSource == u"smart") {
        for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
            if (p.id == m_externalPlaylist) {
                return { sortKeyForSmartField(p.sort), p.ascending };
            }
        }
    }
    // A folder, a playlist (ours or another app's): the order it comes in.
    if (m_externalSource == u"folder" || m_query.playlistId >= 0
        || (!m_externalSource.isEmpty() && m_externalPlaylist >= 0)) {
        return { K::Position, true };
    }
    if (m_query.missingOnly || m_query.hiddenOnly) {
        return { K::Artist, true };
    }
    if (m_query.playedOnly) {
        return { K::LastPlayedAt, false };
    }
    return { K::DateAdded, false };
}

void MixHouseTrackListModel::setSort(int sortKey, bool ascending)
{
    const auto key = static_cast<IMixHouseLibrary::SortKey>(sortKey);
    if (m_query.sort == key && m_query.ascending == ascending) {
        return;
    }
    m_query.sort = key;
    m_query.ascending = ascending;
    m_relatedSortByColumn = m_relatedView;
    load();
}

void MixHouseTrackListModel::setSearch(const QString& text)
{
    if (m_query.filter == text) {
        return;
    }
    m_query.filter = text;
    load();
}

void MixHouseTrackListModel::setPlaylist(int playlistId)
{
    // Guard on the WHOLE view identity, not just playlistId: a crate,
    // Missing, or placeholder view also has playlistId == -1, so clicking
    // "All Tracks" (setPlaylist(-1)) while viewing any of those must still
    // go through and clear them, not early-return as a no-op.
    if (m_query.playlistId == playlistId && m_query.crateId < 0 && !m_query.missingOnly
        && !m_query.hiddenOnly && !m_query.unanalyzedOnly && !m_query.playedOnly
        && !m_emptyPlaceholder && m_externalSource.isEmpty()) {
        return;
    }
    const bool wasPlaylist = m_query.playlistId >= 0;
    const bool isPlaylist = playlistId >= 0;
    m_query.playlistId = playlistId;
    // A view is either the flat library, one playlist, one crate, Missing,
    // or a placeholder -- entering a playlist (or returning to the flat
    // "All Tracks" view) always leaves whichever of the others was active.
    m_query.crateId = -1;
    m_query.missingOnly = false;
    m_query.hiddenOnly = false;
    m_query.unanalyzedOnly = false;
    m_query.playedOnly = false;
    m_emptyPlaceholder = false;
    leaveExternal();
    // Real mixxx: each feature restores/clears its OWN remembered search on
    // activation (library.cpp:318-330) -- this port keeps one shared filter
    // string instead of one per feature, so the simplest way to stop a
    // search typed in one view from silently keeping to filter whatever's
    // clicked into next (with the search box text no longer matching what's
    // actually active) is to just clear it on every view switch.
    m_query.filter.clear();
    // Real mixxx: a playlist is an ORDERED list and shows in that order by
    // default; the flat Tracks view has no such ordering and defaults to
    // Date Added. Only flip the sort automatically on the transition INTO
    // or OUT OF a playlist view -- if the user already picked a different
    // sort while inside one, clicking a different playlist shouldn't reset
    // that choice.
    if (isPlaylist && !wasPlaylist) {
        m_query.sort = IMixHouseLibrary::SortKey::Position;
        m_query.ascending = true;
    } else if (wasPlaylist && !isPlaylist) {
        m_query.sort = IMixHouseLibrary::SortKey::DateAdded;
        m_query.ascending = false;
    }
    load();
}

void MixHouseTrackListModel::setCrate(int crateId)
{
    // Same reasoning as setPlaylist()'s guard: a playlist, Missing, or
    // placeholder view also has crateId == -1, so this must not no-op
    // while leaving any of those.
    if (m_query.crateId == crateId && m_query.playlistId < 0 && !m_query.missingOnly
        && !m_query.hiddenOnly && !m_query.unanalyzedOnly && !m_query.playedOnly
        && !m_emptyPlaceholder && m_externalSource.isEmpty()) {
        return;
    }
    const bool wasPlaylist = m_query.playlistId >= 0;
    m_query.crateId = crateId;
    // A view is either the flat library, one playlist, one crate, Missing,
    // or a placeholder -- entering a crate (or leaving one) always clears
    // whichever of the others was active. Unlike setPlaylist(), there's no
    // Position sort to flip to/from: crate_tracks has no ordering column
    // (real mixxx crates are unordered sets), so a crate view just keeps
    // whatever sort was already active. Only undo the playlist's own
    // Position sort when coming FROM one -- it means nothing outside a
    // playlist.
    m_query.playlistId = -1;
    m_query.missingOnly = false;
    m_query.hiddenOnly = false;
    m_query.unanalyzedOnly = false;
    m_query.playedOnly = false;
    m_emptyPlaceholder = false;
    leaveExternal();
    m_query.filter.clear();   // see setPlaylist()'s own comment
    if (wasPlaylist) {
        m_query.sort = IMixHouseLibrary::SortKey::DateAdded;
        m_query.ascending = false;
    }
    load();
}

void MixHouseTrackListModel::setMissing()
{
    // The sidebar's "Missing Tracks" is a single fixed node (refId -1, not
    // one-of-many like a playlist/crate id), so unlike setPlaylist/setCrate
    // this only ever means "enter it" -- there's no id to compare against.
    if (m_query.missingOnly && m_query.playlistId < 0 && m_query.crateId < 0
        && !m_query.hiddenOnly && !m_query.unanalyzedOnly && !m_query.playedOnly
        && !m_emptyPlaceholder && m_externalSource.isEmpty()) {
        return;
    }
    m_query.playlistId = -1;
    m_query.crateId = -1;
    m_query.missingOnly = true;
    m_query.hiddenOnly = false;
    m_query.unanalyzedOnly = false;
    m_query.playedOnly = false;
    m_emptyPlaceholder = false;
    leaveExternal();
    m_query.filter.clear();   // see setPlaylist()'s own comment
    // Real mixxx's MissingTableModel::setTableModel() default-sorts by
    // Artist ascending (missingtablemodel.cpp) -- unlike the flat Tracks
    // view's Date Added default.
    m_query.sort = IMixHouseLibrary::SortKey::Artist;
    m_query.ascending = true;
    load();
}

void MixHouseTrackListModel::setHidden()
{
    if (m_query.hiddenOnly && m_query.playlistId < 0 && m_query.crateId < 0
        && !m_query.missingOnly && !m_emptyPlaceholder && m_externalSource.isEmpty()) {
        return;
    }
    m_query.playlistId = -1;
    m_query.crateId = -1;
    m_query.missingOnly = false;
    m_query.hiddenOnly = true;
    m_emptyPlaceholder = false;
    leaveExternal();
    m_query.filter.clear();
    m_query.sort = IMixHouseLibrary::SortKey::Artist;
    m_query.ascending = true;
    load();
}

int MixHouseTrackListModel::verifyMissingTracks()
{
    // Real mixxx's LibraryScanner runs this as a side effect of every scan
    // (TrackDAO::verifyRemainingTracks); this port has no such background
    // scanner, so the sidebar's Missing node runs the same disk check
    // directly, on demand. verifyTrackLocations() itself calls load() (via
    // libraryChanged) when anything actually changed.
    return library()->verifyTrackLocations();
}

void MixHouseTrackListModel::leaveExternal()
{
    if (m_externalSource.isEmpty()) {
        return;
    }
    m_externalSource.clear();
    m_externalPlaylist = -1;
    // External playlists sort by their own order; the library views don't
    // know that position.
    if (m_query.sort == IMixHouseLibrary::SortKey::Position && m_query.playlistId < 0) {
        m_query.sort = IMixHouseLibrary::SortKey::DateAdded;
        m_query.ascending = false;
    }
}

void MixHouseTrackListModel::applyOwnOverrides(MixHouseTrackList& tracks) const
{
    // Ratings, colors and tags given in GlideMix to Lexicon / rekordbox songs
    // live in its own library (their databases are read-only here).
    const auto own = library()->ratingsAndColorsByLocation();
    const auto edits = library()->tagEditsByLocation();
    for (MixHouseTrack& t : tracks) {
        const auto e = edits.constFind(t.filePath);
        if (e != edits.constEnd()) {
            applyTagFields(t, *e);
        }
        const auto it = own.constFind(t.filePath);
        if (it != own.constEnd()) {
            if (it->first > 0) {
                t.rating = it->first;
            }
            if (it->second >= 0) {
                t.color = it->second;
            }
        }
    }
}

const IMixHouseExternalLibrary* MixHouseTrackListModel::externalLibrary(const QString& source) const
{
    if (source == u"lexicon") {
        return lexicon().get();
    }
    if (source == u"rekordbox") {
        return rekordboxXml().get();
    }
    // GlideMix: Engine DJ, Traktor, Serato, VirtualDJ, djay (mixhousedjapps.h).
    return djAppLibrary(source);
}

void MixHouseTrackListModel::readDjApp(const QString& source)
{
    readDjAppNow(source);
}

QStringList MixHouseTrackListModel::externalSources()
{
    return QStringList { QStringLiteral("lexicon"), QStringLiteral("rekordbox") } + djAppSources();
}

void MixHouseTrackListModel::setRekordboxXmlPath(const QString& path)
{
    if (rekordboxXml() && !path.isEmpty()) {
        m_rekordboxCheckedPath.clear();   // report on this file even when it is the same one again
        // GlideMix: a small window shows the reading and what came of it
        // (Chris: "Import ... looks like it does nothing").
        m_rekordboxImportShown = true;
        m_rekordboxImportDone = false;
        m_rekordboxImportStatus = muse::qtrc("mixhouse", "Reading %1…").arg(QFileInfo(path).fileName());
        emit rekordboxImportChanged();
        rekordboxXml()->setPath(path);
    }
}

void MixHouseTrackListModel::closeRekordboxImport()
{
    m_rekordboxImportShown = false;
    m_rekordboxImportDone = false;
    emit rekordboxImportChanged();
}

int MixHouseTrackListModel::rekordboxLoadProgress() const
{
    return rekordboxXml() ? rekordboxXml()->loadProgress() : -1;
}

void MixHouseTrackListModel::setExternalPlaylist(const QString& source, int playlistId)
{
    // GlideMix: another DJ app's tracks (Lexicon DJ's database, a
    // rekordbox XML export) in the same table, read-only.
    if (m_externalSource == source && m_externalPlaylist == playlistId) {
        return;
    }
    m_externalSource = source;
    m_externalPlaylist = playlistId;
    m_query.playlistId = -1;
    m_query.crateId = -1;
    m_query.missingOnly = false;
    m_query.hiddenOnly = false;
    m_query.unanalyzedOnly = false;
    m_query.playedOnly = false;
    m_emptyPlaceholder = false;
    m_query.filter.clear();
    if (playlistId >= 0) {
        m_query.sort = IMixHouseLibrary::SortKey::Position;
        m_query.ascending = true;
    } else if (m_query.sort == IMixHouseLibrary::SortKey::Position) {
        m_query.sort = IMixHouseLibrary::SortKey::DateAdded;
        m_query.ascending = false;
    }
    load();
}

namespace {
MixHouseSmartPlaylist smartFromVariant(const QVariantMap& m)
{
    MixHouseSmartPlaylist p;
    p.id = m.value("id", -1).toInt();
    p.name = m.value("name").toString().trimmed();
    p.matchAll = m.value("match").toString() != u"any";
    p.source = m.value("source", QStringLiteral("collection")).toString();
    for (const QVariant& gv : m.value("groups").toList()) {
        std::vector<MixHouseSmartRule> group;
        for (const QVariant& rv : gv.toList()) {
            const QVariantMap r = rv.toMap();
            group.push_back({ r.value("field").toString(), r.value("op").toString(), r.value("value").toString(),
                              r.value("value2").toString() });
        }
        if (!group.empty()) {
            p.groups.push_back(std::move(group));
        }
    }
    p.sort = m.value("sort", QStringLiteral("dateAdded")).toString();
    p.ascending = m.value("ascending", false).toBool();
    p.limit = std::max(0, m.value("limit", 0).toInt());
    return p;
}

QVariantMap smartToVariant(const MixHouseSmartPlaylist& p)
{
    QVariantList groups;
    for (const auto& group : p.groups) {
        QVariantList rules;
        for (const MixHouseSmartRule& r : group) {
            rules << QVariantMap { { "field", r.field }, { "op", r.op }, { "value", r.value }, { "value2", r.value2 } };
        }
        groups << QVariant(rules);
    }
    return QVariantMap { { "id", p.id }, { "name", p.name }, { "match", p.matchAll ? "all" : "any" },
                         { "source", p.source }, { "groups", groups }, { "sort", p.sort },
                         { "ascending", p.ascending }, { "limit", p.limit } };
}

IMixHouseLibrary::SortKey sortKeyForSmartField(const QString& field)
{
    using K = IMixHouseLibrary::SortKey;
    static const QHash<QString, K> keys = {
        { "title", K::Title }, { "artist", K::Artist }, { "album", K::Album }, { "albumArtist", K::AlbumArtist },
        { "genre", K::Genre }, { "composer", K::Composer }, { "grouping", K::Grouping }, { "comment", K::Comment },
        { "bpm", K::Bpm }, { "energy", K::Energy }, { "key", K::Key }, { "rating", K::Rating }, { "year", K::Year },
        { "duration", K::Duration }, { "dateAdded", K::DateAdded }, { "lastPlayed", K::LastPlayedAt },
        { "timesPlayed", K::TimesPlayed }, { "bitrate", K::Bitrate }, { "fileType", K::FileType },
    };
    return keys.value(field, K::DateAdded);
}
}

void MixHouseTrackListModel::setSmartPlaylist(int id)
{
    m_smartCounts.remove(id);   // just edited, maybe: count it again
    m_externalSource = QStringLiteral("smart");
    m_externalPlaylist = id;
    m_query.playlistId = -1;
    m_query.crateId = -1;
    m_query.missingOnly = false;
    m_query.hiddenOnly = false;
    m_query.unanalyzedOnly = false;
    m_query.playedOnly = false;
    m_emptyPlaceholder = false;
    m_query.filter.clear();
    // The table starts in the playlist's own order ("the newest 50").
    for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
        if (p.id == id) {
            m_query.sort = sortKeyForSmartField(p.sort);
            m_query.ascending = p.ascending;
        }
    }
    load();
}

void MixHouseTrackListModel::setDuplicates()
{
    m_externalSource = QStringLiteral("duplicates");
    m_externalPlaylist = -1;
    m_query.playlistId = -1;
    m_query.crateId = -1;
    m_query.missingOnly = false;
    m_query.hiddenOnly = false;
    m_query.unanalyzedOnly = false;
    m_query.playedOnly = false;
    m_emptyPlaceholder = false;
    m_query.filter.clear();
    load();
}

namespace {
//! Artist or title as a duplicate key: case, punctuation and spacing don't count.
QString duplicateKeyPart(const QString& text)
{
    QString out;
    out.reserve(text.size());
    bool space = false;
    for (const QChar c : text.toLower()) {
        if (c.isLetterOrNumber()) {
            out += c;
            space = false;
        } else if (!space && !out.isEmpty()) {
            out += u' ';
            space = true;
        }
    }
    return out.trimmed();
}

//! Which copy to keep: lossless first, then the higher bitrate, then an
//! analysed one, then the one rated higher.
double copyQuality(const MixHouseTrack& t)
{
    static const QStringList lossless = { QStringLiteral("wav"), QStringLiteral("aif"), QStringLiteral("aiff"),
                                          QStringLiteral("flac"), QStringLiteral("alac") };
    const QString type = t.fileType.isEmpty() ? QFileInfo(t.filePath).suffix().toLower() : t.fileType.toLower();
    return (lossless.contains(type) ? 1e6 : 0.0) + t.bitrate * 10.0 + (t.analyzed ? 5.0 : 0.0) + t.rating;
}
}

MixHouseTrackList MixHouseTrackListModel::duplicateTracks()
{
    IMixHouseLibrary::TrackQuery q = m_query;
    q.limit = 0;
    q.playlistId = -1;
    q.crateId = -1;
    QSet<int> extras;
    int groupCount = 0;
    MixHouseTrackList out = duplicateTracks(q, &extras, &groupCount);
    m_duplicateExtras = extras;
    m_duplicateGroups = groupCount;
    return out;
}

MixHouseTrackList MixHouseTrackListModel::duplicateTracks(const IMixHouseLibrary::TrackQuery& q, QSet<int>* extras,
                                                          int* groupCount) const
{
    MixHouseTrackList all = library()->tracks(q);

    QHash<QString, std::vector<size_t> > byKey;
    for (size_t i = 0; i < all.size(); ++i) {
        const MixHouseTrack& t = all[i];
        const QString title = duplicateKeyPart(t.title.isEmpty() ? QFileInfo(t.filePath).completeBaseName() : t.title);
        if (title.isEmpty()) {
            continue;
        }
        byKey[duplicateKeyPart(t.artist) + u'|' + title].push_back(i);
    }

    std::vector<std::vector<size_t> > groups;
    for (auto it = byKey.begin(); it != byKey.end(); ++it) {
        std::vector<size_t>& idx = it.value();
        if (idx.size() < 2) {
            continue;
        }
        // Same name, different length (radio edit vs extended) is not a copy.
        std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return all[a].durationSec < all[b].durationSec; });
        std::vector<size_t> run { idx.front() };
        const auto flush = [&]() {
            if (run.size() >= 2) {
                groups.push_back(run);
            }
        };
        for (size_t k = 1; k < idx.size(); ++k) {
            const double a = all[run.back()].durationSec;
            const double b = all[idx[k]].durationSec;
            if (a > 0.0 && b > 0.0 && b - a > 3.0) {
                flush();
                run.clear();
            }
            run.push_back(idx[k]);
        }
        flush();
    }
    for (std::vector<size_t>& g : groups) {
        std::stable_sort(g.begin(), g.end(), [&](size_t a, size_t b) { return copyQuality(all[a]) > copyQuality(all[b]); });
    }
    std::sort(groups.begin(), groups.end(), [&](const std::vector<size_t>& a, const std::vector<size_t>& b) {
        const MixHouseTrack& x = all[a.front()];
        const MixHouseTrack& y = all[b.front()];
        const int byArtist = QString::localeAwareCompare(x.artist, y.artist);
        return byArtist != 0 ? byArtist < 0 : QString::localeAwareCompare(x.title, y.title) < 0;
    });

    MixHouseTrackList out;
    for (const std::vector<size_t>& g : groups) {
        for (size_t k = 0; k < g.size(); ++k) {
            if (k > 0 && extras) {
                extras->insert(all[g[k]].id);
            }
            out.push_back(all[g[k]]);
        }
    }
    if (groupCount) {
        *groupCount = static_cast<int>(groups.size());
    }
    return out;
}

namespace {
// The generator's names: "Genre · House", "Key · 8A", "Energy · 7".
const QString kGeneratedSeparator = QStringLiteral(" · ");
QStringList generatedPrefixes()
{
    return { QStringLiteral("Genre") + kGeneratedSeparator, QStringLiteral("Key") + kGeneratedSeparator,
             QStringLiteral("Energy") + kGeneratedSeparator };
}
}

void MixHouseTrackListModel::generateSmartPlaylists(const QString& field, const QString& source)
{
    IMixHouseLibrary::TrackQuery base;
    const MixHouseTrackList tracks = smartCandidates(source, base);
    QSet<QString> existing;
    for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
        existing.insert(p.name);
    }

    // value -> how many songs; ordered for the sidebar.
    std::vector<std::pair<QString, int> > values;
    MixHouseSmartRule rule;
    rule.field = field;
    QString prefix;
    if (field == u"genre") {
        // Lexicon's genres are often lists ("Dubstep, EDM"): count each one.
        QHash<QString, int> counts;
        QHash<QString, QString> spelling;
        static const QRegularExpression separators(QStringLiteral("[,;/]"));
        for (const MixHouseTrack& t : tracks) {
            QSet<QString> seen;
            for (QString g : t.genre.split(separators, Qt::SkipEmptyParts)) {
                g = g.trimmed();
                const QString key = g.toLower();
                if (g.isEmpty() || seen.contains(key)) {
                    continue;
                }
                seen.insert(key);
                ++counts[key];
                spelling.insert(key, g);
            }
        }
        for (auto it = counts.cbegin(); it != counts.cend(); ++it) {
            if (it.value() >= 10) {
                values.push_back({ spelling.value(it.key()), it.value() });
            }
        }
        // The 50 biggest, alphabetically.
        std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (values.size() > 50) {
            values.resize(50);
        }
        std::sort(values.begin(), values.end(), [](const auto& a, const auto& b) {
            return QString::localeAwareCompare(a.first, b.first) < 0;
        });
        rule.op = QStringLiteral("contains");
        prefix = QStringLiteral("Genre");
    } else if (field == u"key") {
        QHash<QString, int> counts;
        for (const MixHouseTrack& t : tracks) {
            if (t.keyId >= 1 && t.keyId <= 24) {
                ++counts[IMixHouseKeyMatch::camelot(t.keyId)];
            }
        }
        for (int n = 1; n <= 12; ++n) {
            for (const QChar mode : { u'A', u'B' }) {
                const QString key = QString::number(n) + mode;
                if (counts.value(key) > 0) {
                    values.push_back({ key, counts.value(key) });
                }
            }
        }
        rule.op = QStringLiteral("is");
        prefix = QStringLiteral("Key");
    } else if (field == u"energy") {
        int counts[11] = {};
        for (const MixHouseTrack& t : tracks) {
            const int e = static_cast<int>(std::lround(t.energy));
            if (e >= 1 && e <= 10) {
                ++counts[e];
            }
        }
        for (int e = 1; e <= 10; ++e) {
            if (counts[e] > 0) {
                values.push_back({ QString::number(e), counts[e] });
            }
        }
        rule.op = QStringLiteral("is");
        prefix = QStringLiteral("Energy");
    } else {
        return;
    }

    int made = 0;
    for (const auto& [value, count] : values) {
        MixHouseSmartPlaylist p;
        p.name = prefix + kGeneratedSeparator + value;
        if (existing.contains(p.name)) {
            continue;
        }
        p.source = source;
        rule.value = value;
        p.groups = { { rule } };
        if (library()->saveSmartPlaylist(p) >= 0) {
            ++made;
        }
    }
    emit loaded();   // the sidebar re-reads its nodes
    const std::string text = made > 0
        ? muse::qtrc("mixhouse", "Made %n smart playlist(s). They are under Smart Playlists and update by themselves.",
                     nullptr, made).toStdString()
        : muse::trc("mixhouse", "No new smart playlists: there was nothing to make, or they exist already.");
    interactive()->info(muse::trc("mixhouse", "Generate Smart Playlists"), text);
}

void MixHouseTrackListModel::deleteGeneratedSmartPlaylistsWithConfirm()
{
    std::vector<int> ids;
    for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
        for (const QString& prefix : generatedPrefixes()) {
            if (p.name.startsWith(prefix)) {
                ids.push_back(p.id);
                break;
            }
        }
    }
    if (ids.empty()) {
        interactive()->info(muse::trc("mixhouse", "Delete Generated Smart Playlists"),
                            muse::trc("mixhouse", "There are no generated smart playlists."));
        return;
    }
    const auto result = interactive()->questionSync(
        muse::trc("mixhouse", "Delete Generated Smart Playlists"),
        muse::qtrc("mixhouse", "Delete the %n generated smart playlist(s) (Genre ·, Key ·, Energy ·)? "
                               "Their tracks stay in your library.", nullptr, static_cast<int>(ids.size())).toStdString(),
        { muse::IInteractive::Button::Cancel, muse::IInteractive::Button::Ok });
    if (result.standardButton() != muse::IInteractive::Button::Ok) {
        return;
    }
    const bool viewingOne = m_externalSource == u"smart"
                            && std::find(ids.begin(), ids.end(), m_externalPlaylist) != ids.end();
    for (const int id : ids) {
        library()->deleteSmartPlaylist(id);
    }
    if (viewingOne) {
        setPlaylist(-1);
    }
    emit loaded();
}

QStringList MixHouseTrackListModel::sidebarNodesContaining(int row) const
{
    QStringList nodes;
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) {
        return nodes;
    }
    const MixHouseTrack& t = m_tracks[static_cast<size_t>(row)];
    const bool external = isExternalTrack(t);
    // A Lexicon / rekordbox song is in our playlists as its library copy.
    const int libraryId = external ? libraryIdForFile(t.filePath) : t.id;
    for (const int id : library()->playlistsContainingTrack(libraryId)) {
        nodes << QStringLiteral("pl-") + QString::number(id);
    }
    // Smart playlists: their rules, for this one song. Ones with a limit
    // ("the newest 50") depend on the others, so they are left out.
    const bool fromLexicon = lexicon() && lexicon()->ownsTrack(t.id);
    const bool fromRekordbox = rekordboxXml() && rekordboxXml()->ownsTrack(t.id);
    for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
        if (p.limit > 0) {
            continue;
        }
        const bool sourceFits = p.source == u"all" || (p.source == u"collection" && !external)
                                || (p.source == u"lexicon" && fromLexicon) || (p.source == u"rekordbox" && fromRekordbox);
        if (!sourceFits) {
            continue;
        }
        MixHouseSmartContext context;
        for (const auto& group : p.groups) {
            for (const MixHouseSmartRule& r : group) {
                if (r.field == u"inPlaylist" && !context.playlistFiles.contains(r.value)) {
                    context.playlistFiles.insert(r.value, smartPlaylistFiles(r.value));
                }
            }
        }
        if (smartPlaylistMatches(p, t, context)) {
            nodes << QStringLiteral("sp-") + QString::number(p.id);
        }
    }
    return nodes;
}

QVariantList MixHouseTrackListModel::duplicateExtraRows() const
{
    QVariantList rows;
    for (size_t r = 0; r < m_tracks.size(); ++r) {
        if (m_duplicateExtras.contains(m_tracks[r].id)) {
            rows << static_cast<int>(r);
        }
    }
    return rows;
}

MixHouseTrackList MixHouseTrackListModel::smartCandidates(const QString& source, const IMixHouseLibrary::TrackQuery& base) const
{
    IMixHouseLibrary::TrackQuery q = base;
    q.playlistId = -1;
    q.crateId = -1;
    q.missingOnly = false;
    q.hiddenOnly = false;
    q.unanalyzedOnly = false;
    q.playedOnly = false;
    q.limit = 0;
    MixHouseTrackList out;
    QSet<QString> seen;
    const auto add = [&](MixHouseTrackList list) {
        for (MixHouseTrack& t : list) {
            if (t.filePath.isEmpty() || !seen.contains(t.filePath)) {
                seen.insert(t.filePath);
                out.push_back(std::move(t));
            }
        }
    };
    if (source == u"collection" || source == u"all") {
        add(library()->tracks(q));
    }
    for (const QString& ext : externalSources()) {
        if (source != ext && source != u"all") {
            continue;
        }
        const IMixHouseExternalLibrary* external = externalLibrary(ext);
        if (external && external->available()) {
            MixHouseTrackList list = external->tracks(-1, q);
            applyOwnOverrides(list);
            add(std::move(list));
        }
    }
    return out;
}

QSet<QString> MixHouseTrackListModel::smartPlaylistFiles(const QString& ref) const
{
    // "collection:5", "lexicon:12", "rekordbox:3"
    QSet<QString> files;
    const qsizetype colon = ref.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        return files;
    }
    const QString source = ref.left(colon);
    const int id = ref.mid(colon + 1).toInt();
    IMixHouseLibrary::TrackQuery q;
    q.limit = 0;
    MixHouseTrackList list;
    if (source == u"collection") {
        q.playlistId = id;
        list = library()->tracks(q);
    } else if (const IMixHouseExternalLibrary* external = externalLibrary(source); external && external->available()) {
        list = external->tracks(id, q);
    }
    for (const MixHouseTrack& t : list) {
        files.insert(t.filePath);
    }
    return files;
}

MixHouseSmartContext MixHouseTrackListModel::smartContextFor(const MixHouseSmartPlaylist& playlist) const
{
    MixHouseSmartContext context;
    for (const auto& group : playlist.groups) {
        for (const MixHouseSmartRule& r : group) {
            if (r.field == u"inPlaylist" && !context.playlistFiles.contains(r.value)) {
                context.playlistFiles.insert(r.value, smartPlaylistFiles(r.value));
            } else if (r.field == u"search" && !context.searchFiles.contains(r.value)) {
                // The search box's own reading of the text, over the playlist's source.
                IMixHouseLibrary::TrackQuery q;
                q.filter = r.value;
                q.limit = 0;
                QSet<QString> files;
                for (const MixHouseTrack& t : smartCandidates(playlist.source, q)) {
                    files.insert(t.filePath);
                }
                context.searchFiles.insert(r.value, files);
            }
        }
    }
    return context;
}

int MixHouseTrackListModel::smartPlaylistCount(const MixHouseSmartPlaylist& playlist) const
{
    // The whole playlist: not the search box, not the list's 2,000-row page.
    IMixHouseLibrary::TrackQuery all;
    all.limit = 0;
    return static_cast<int>(applySmartPlaylist(playlist, smartCandidates(playlist.source, all),
                                               smartContextFor(playlist)).size());
}

MixHouseTrackList MixHouseTrackListModel::smartPlaylistTracks(int id) const
{
    MixHouseSmartPlaylist playlist;
    bool found = false;
    for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
        if (p.id == id) {
            playlist = p;
            found = true;
        }
    }
    if (!found) {
        return {};
    }
    const MixHouseSmartContext context = smartContextFor(playlist);
    // The search box and key wheel narrow the candidates; the rules pick.
    MixHouseTrackList tracks = applySmartPlaylist(playlist, smartCandidates(playlist.source, m_query), context);
    IMixHouseLibrary::TrackQuery sortOnly;
    sortOnly.sort = m_query.sort;
    sortOnly.ascending = m_query.ascending;
    sortOnly.limit = m_query.limit;
    sortAndLimitTracks(tracks, sortOnly, false);
    return tracks;
}

QVariantList MixHouseTrackListModel::smartFieldList() const
{
    QVariantList out;
    for (const MixHouseSmartField& f : smartFields()) {
        out << QVariantMap { { "value", f.id }, { "text", muse::qtrc("mixhouse", f.title.toUtf8().constData()) },
                             { "type", f.type } };
    }
    return out;
}

QVariantList MixHouseTrackListModel::smartOperatorList(const QString& fieldType) const
{
    QVariantList out;
    for (const auto& [id, title] : smartOperators(fieldType)) {
        out << QVariantMap { { "value", id }, { "text", muse::qtrc("mixhouse", title.toUtf8().constData()) } };
    }
    return out;
}

QVariantMap MixHouseTrackListModel::smartPlaylistDefinition(int id) const
{
    for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
        if (p.id == id) {
            return smartToVariant(p);
        }
    }
    MixHouseSmartPlaylist fresh;   // a new one: one empty rule to start from
    fresh.name = muse::qtrc("mixhouse", "New Smart Playlist");
    fresh.groups.push_back({ MixHouseSmartRule { QStringLiteral("genre"), QStringLiteral("contains"), {}, {} } });
    return smartToVariant(fresh);
}

int MixHouseTrackListModel::saveSmartPlaylist(const QVariantMap& definition)
{
    MixHouseSmartPlaylist p = smartFromVariant(definition);
    if (p.name.isEmpty()) {
        p.name = muse::qtrc("mixhouse", "Smart Playlist");
    }
    const int id = library()->saveSmartPlaylist(p);
    if (id >= 0 && m_externalSource == u"smart" && m_externalPlaylist == id) {
        const_cast<MixHouseTrackListModel*>(this)->load();
    }
    return id;
}

void MixHouseTrackListModel::deleteSmartPlaylistWithConfirm(int id, const QString& name)
{
    const auto result = interactive()->questionSync(
        muse::trc("mixhouse", "Delete smart playlist"),
        muse::qtrc("mixhouse", "Delete the smart playlist \"%1\"? Its tracks stay in your library.").arg(name).toStdString(),
        { muse::IInteractive::Button::Cancel, muse::IInteractive::Button::Ok });
    if (result.standardButton() != muse::IInteractive::Button::Ok) {
        return;
    }
    if (library()->deleteSmartPlaylist(id) && m_externalSource == u"smart" && m_externalPlaylist == id) {
        setPlaylist(-1);
    }
}

int MixHouseTrackListModel::smartPlaylistPreviewCount(const QVariantMap& definition) const
{
    const MixHouseSmartPlaylist p = smartFromVariant(definition);
    const MixHouseSmartContext context = smartContextFor(p);
    return static_cast<int>(applySmartPlaylist(p, smartCandidates(p.source, IMixHouseLibrary::TrackQuery()), context).size());
}

QVariantList MixHouseTrackListModel::smartPlaylistChoices(const QString& source) const
{
    QVariantList out;
    if (source == u"collection" || source == u"all") {
        for (const MixHouseNamedItem& p : library()->playlists()) {
            if (!p.isFolder) {
                out << QVariantMap { { "value", QStringLiteral("collection:%1").arg(p.id) }, { "text", p.name } };
            }
        }
    }
    for (const QString& ext : externalSources()) {
        if (source != ext && source != u"all") {
            continue;
        }
        const IMixHouseExternalLibrary* external = externalLibrary(ext);
        if (!external || !external->available()) {
            continue;
        }
        const QString prefix = ext == u"lexicon" ? QStringLiteral("Lexicon: ")
                               : ext == u"rekordbox" ? QStringLiteral("rekordbox: ") : djAppName(ext) + QStringLiteral(": ");
        for (const IMixHouseExternalLibrary::Node& n : external->nodes()) {
            if (n.type != IMixHouseExternalLibrary::NodeType::Folder) {
                out << QVariantMap { { "value", QStringLiteral("%1:%2").arg(ext).arg(n.id) },
                                     { "text", source == u"all" ? prefix + n.name : n.name } };
            }
        }
    }
    return out;
}

int MixHouseTrackListModel::duplicateSmartPlaylistAsPlaylist(int id)
{
    QString name;
    for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
        if (p.id == id) {
            name = p.name;
        }
    }
    const MixHouseTrackList tracks = smartPlaylistTracks(id);
    if (name.isEmpty()) {
        return -1;
    }
    const int playlistId = library()->createPlaylist(name, -1);
    if (playlistId < 0) {
        return -1;
    }
    // Lexicon / rekordbox songs join the Music Collection first, in one go.
    QStringList missing;
    for (const MixHouseTrack& t : tracks) {
        if (isExternalTrack(t) && !t.filePath.isEmpty() && libraryIdForFile(t.filePath) < 0) {
            missing << t.filePath;
        }
    }
    if (!missing.isEmpty()) {
        library()->addTracksFromFiles(missing);
    }
    for (const MixHouseTrack& t : tracks) {
        const int libraryId = isExternalTrack(t) ? libraryIdForFile(t.filePath) : t.id;
        if (libraryId >= 0) {
            library()->addTrackToPlaylist(libraryId, playlistId);
        }
    }
    return playlistId;
}

MixHouseTrackList MixHouseTrackListModel::playlistTracksForExport(const QString& kind, int id) const
{
    IMixHouseLibrary::TrackQuery q;
    q.limit = 0;
    q.sort = IMixHouseLibrary::SortKey::Position;
    q.ascending = true;
    if (kind == u"playlist") {
        q.playlistId = id;
        return library()->tracks(q);
    }
    if (kind == u"crate") {
        q.crateId = id;
        q.sort = IMixHouseLibrary::SortKey::Artist;
        return library()->tracks(q);
    }
    if (kind == u"smart-playlist") {
        MixHouseSmartPlaylist playlist;
        for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
            if (p.id == id) {
                playlist = p;
            }
        }
        const MixHouseSmartContext context = smartContextFor(playlist);
        MixHouseTrackList tracks = applySmartPlaylist(playlist, smartCandidates(playlist.source, IMixHouseLibrary::TrackQuery()), context);
        IMixHouseLibrary::TrackQuery order;
        order.sort = sortKeyForSmartField(playlist.sort);
        order.ascending = playlist.ascending;
        sortAndLimitTracks(tracks, order, false);
        return tracks;
    }
    const QString source = kind.section(QLatin1Char('-'), 0, 0);   // "lexicon", "rekordbox"
    if (const IMixHouseExternalLibrary* external = externalLibrary(source); external && external->available()) {
        MixHouseTrackList tracks = external->tracks(id, q);
        applyOwnOverrides(tracks);
        return tracks;
    }
    return {};
}

void MixHouseTrackListModel::exportPlaylistToFolder(const QString& kind, int id, const QString& name, const QString& destination)
{
    const MixHouseTrackList tracks = playlistTracksForExport(kind, id);
    if (tracks.empty() || destination.isEmpty()) {
        setAnalysisMessage(muse::qtrc("mixhouse", "Nothing to export: the playlist is empty."));
        return;
    }
    // A folder of its own, named after the playlist (never over an old one).
    static const QRegularExpression kUnsafe(QStringLiteral("[\\\\/:*?\"<>|]"));
    QString base = name.trimmed();
    base.replace(kUnsafe, QStringLiteral("-"));
    if (base.isEmpty()) {
        base = QStringLiteral("Playlist");
    }
    QString folder = QDir(destination).filePath(base);
    for (int n = 2; QFileInfo::exists(folder); ++n) {
        folder = QDir(destination).filePath(QStringLiteral("%1 (%2)").arg(base).arg(n));
    }
    if (!QDir().mkpath(folder)) {
        interactive()->error(muse::trc("mixhouse", "Export to Folder"),
                             muse::qtrc("mixhouse", "Could not create the folder %1.").arg(folder).toStdString());
        return;
    }
    struct Item {
        QString source;
        QString line;
    };
    std::vector<Item> items;
    const int digits = tracks.size() >= 100 ? 3 : 2;
    for (const MixHouseTrack& t : tracks) {
        QString line = t.artist.isEmpty() ? t.title : t.artist + QStringLiteral(" - ") + t.title;
        QStringList details;
        if (t.bpm > 0) {
            details << QString::number(t.bpm, 'f', t.bpm == std::floor(t.bpm) ? 0 : 1) + QStringLiteral(" BPM");
        }
        if (!t.key.isEmpty()) {
            details << t.key;
        }
        if (t.durationSec > 0) {
            const int s = static_cast<int>(t.durationSec);
            details << QString::asprintf("%d:%02d", s / 60, s % 60);
        }
        if (!details.isEmpty()) {
            line += QStringLiteral("  (") + details.join(QStringLiteral(", ")) + QStringLiteral(")");
        }
        items.push_back({ t.filePath, line });
    }
    setAnalysisMessage(muse::qtrc("mixhouse", "Exporting %1 songs…").arg(tracks.size()));
    QPointer<MixHouseTrackListModel> self(this);
    std::thread([self, items, folder, base, digits]() {
        QStringList txt { base, QString() };
        QStringList m3u { QStringLiteral("#EXTM3U") };
        int copied = 0;
        int number = 0;
        QStringList missing;
        for (const Item& item : items) {
            ++number;
            const QString prefix = QStringLiteral("%1").arg(number, digits, 10, QLatin1Char('0'));
            const QFileInfo src(item.source);
            const QString fileName = prefix + QStringLiteral(" - ") + src.fileName();
            if (!src.exists() || !QFile::copy(item.source, QDir(folder).filePath(fileName))) {
                missing << item.line;
                txt << prefix + QStringLiteral(". ") + item.line + QStringLiteral("   [file missing]");
                continue;
            }
            ++copied;
            txt << prefix + QStringLiteral(". ") + item.line;
            m3u << fileName;
            if (copied % 5 == 0) {
                QMetaObject::invokeMethod(qApp, [self, copied, total = items.size()]() {
                    if (self) {
                        self->setAnalysisMessage(muse::qtrc("mixhouse", "Exporting… %1 of %2").arg(copied).arg(total));
                    }
                }, Qt::QueuedConnection);
            }
        }
        txt << QString() << QStringLiteral("Exported with GlideMix");
        const auto write = [&](const QString& path, const QStringList& lines) {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                QTextStream(&f) << lines.join(QLatin1Char('\n')) << "\n";
            }
        };
        write(QDir(folder).filePath(base + QStringLiteral(".txt")), txt);
        write(QDir(folder).filePath(base + QStringLiteral(".m3u8")), m3u);
        QMetaObject::invokeMethod(qApp, [self, folder, copied, missing]() {
            if (!self) {
                return;
            }
            QString text = muse::qtrc("mixhouse", "Exported %1 songs to %2").arg(copied).arg(QDir::toNativeSeparators(folder));
            if (!missing.isEmpty()) {
                text += QStringLiteral(" ") + muse::qtrc("mixhouse", "(%1 files not found)").arg(missing.size());
            }
            self->setAnalysisMessage(text);
            QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
            QTimer::singleShot(10000, self, [self, text]() {
                if (self && self->m_analysisMessage == text) {
                    self->setAnalysisMessage(QString());
                }
            });
        }, Qt::QueuedConnection);
    }).detach();
}

void MixHouseTrackListModel::exportPlaylistM3u(const QString& kind, int id, const QString& name)
{
    const MixHouseTrackList tracks = playlistTracksForExport(kind, id);
    if (tracks.empty()) {
        setAnalysisMessage(muse::qtrc("mixhouse", "Nothing to export: the playlist is empty."));
        return;
    }
    static const QRegularExpression kUnsafe(QStringLiteral("[\\\\/:*?\"<>|]"));
    QString base = name.trimmed();
    base.replace(kUnsafe, QStringLiteral("-"));
    if (base.isEmpty()) {
        base = QStringLiteral("Playlist");
    }
    const QString path = interactive()->selectSavingFileSync(
        muse::trc("mixhouse", "Export as M3U8 Playlist"),
        muse::io::path_t(QDir(musicFolder()).filePath(base + QStringLiteral(".m3u8"))),
        { muse::trc("mixhouse", "M3U8 playlist") + " (*.m3u8)" }).toQString();
    if (path.isEmpty()) {
        return;
    }
    QStringList lines { QStringLiteral("#EXTM3U") };
    for (const MixHouseTrack& t : tracks) {
        const QString who = t.artist.isEmpty() ? t.title : t.artist + QStringLiteral(" - ") + t.title;
        lines << QStringLiteral("#EXTINF:%1,%2").arg(static_cast<int>(std::lround(std::max(0.0, t.durationSec)))).arg(who);
        lines << QDir::toNativeSeparators(t.filePath);
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        interactive()->error(muse::trc("mixhouse", "Export as M3U8 Playlist"),
                             muse::qtrc("mixhouse", "Could not write %1.").arg(path).toStdString());
        return;
    }
    file.write(lines.join(QLatin1Char('\n')).toUtf8());
    file.write("\n");
    setAnalysisMessage(muse::qtrc("mixhouse", "Exported %1 songs to %2").arg(tracks.size()).arg(QFileInfo(path).fileName()));
}

void MixHouseTrackListModel::exportRekordboxXml()
{
    const QString path = interactive()->selectSavingFileSync(
        muse::trc("mixhouse", "Export rekordbox XML"),
        muse::io::path_t(QDir(musicFolder()).filePath(QStringLiteral("GlideMix rekordbox.xml"))),
        { muse::trc("mixhouse", "rekordbox XML") + " (*.xml)" }).toQString();
    if (path.isEmpty()) {
        return;
    }
    MixHouseXmlExport data;
    data.productVersion = QCoreApplication::applicationVersion();
    QHash<QString, int> index;   // file path -> position in data.tracks
    const auto addTrack = [&](const MixHouseTrack& t) {
        if (!t.filePath.isEmpty() && !index.contains(t.filePath)) {
            index.insert(t.filePath, static_cast<int>(data.tracks.size()));
            data.tracks.push_back(t);
        }
    };

    IMixHouseLibrary::TrackQuery all;
    all.limit = 0;
    for (const MixHouseTrack& t : library()->tracks(all)) {
        addTrack(t);
    }

    // The playlist tree, folders included.
    const MixHouseNamedList pls = library()->playlists();
    std::function<std::vector<MixHouseXmlPlaylist>(int)> children = [&](int parentId) {
        std::vector<MixHouseXmlPlaylist> out;
        for (const MixHouseNamedItem& p : pls) {
            if (p.parentId != parentId) {
                continue;
            }
            MixHouseXmlPlaylist node;
            node.name = p.name;
            node.folder = p.isFolder;
            if (p.isFolder) {
                node.children = children(p.id);
            } else {
                for (const MixHouseTrack& t : playlistTracksForExport(QStringLiteral("playlist"), p.id)) {
                    addTrack(t);
                    node.files << t.filePath;
                }
            }
            out.push_back(std::move(node));
        }
        return out;
    };
    data.playlists = children(-1);

    // Smart playlists, as they are right now (rekordbox's XML has no rules).
    const std::vector<MixHouseSmartPlaylist> smart = library()->smartPlaylists();
    if (!smart.empty()) {
        MixHouseXmlPlaylist folder;
        folder.name = muse::qtrc("mixhouse", "GlideMix Smart Playlists");
        folder.folder = true;
        for (const MixHouseSmartPlaylist& p : smart) {
            MixHouseXmlPlaylist node;
            node.name = p.name;
            for (const MixHouseTrack& t : playlistTracksForExport(QStringLiteral("smart-playlist"), p.id)) {
                addTrack(t);
                node.files << t.filePath;
            }
            folder.children.push_back(std::move(node));
        }
        data.playlists.push_back(std::move(folder));
    }

    // Lexicon / rekordbox songs given a rating, color or tags in GlideMix:
    // their own playlist, so they are easy to find and import.
    QSet<QString> changed;
    const auto rated = library()->ratingsAndColorsByLocation();
    for (auto it = rated.cbegin(); it != rated.cend(); ++it) {
        changed.insert(it.key());
    }
    const auto edits = library()->tagEditsByLocation();
    for (auto it = edits.cbegin(); it != edits.cend(); ++it) {
        changed.insert(it.key());
    }
    MixHouseXmlPlaylist changedList;
    changedList.name = muse::qtrc("mixhouse", "Changed in GlideMix");
    for (const QString& source : externalSources()) {
        const IMixHouseExternalLibrary* external = externalLibrary(source);
        if (changed.isEmpty() || !external || !external->available()) {
            continue;
        }
        MixHouseTrackList list = external->tracks(-1, all);
        applyOwnOverrides(list);
        for (MixHouseTrack& t : list) {
            if (changed.contains(t.filePath) && !changedList.files.contains(t.filePath)) {
                // The GlideMix copy wins where the song is in both.
                const auto own = index.constFind(t.filePath);
                if (own == index.constEnd()) {
                    addTrack(t);
                }
                changedList.files << t.filePath;
            }
        }
    }
    if (!changedList.files.isEmpty()) {
        data.playlists.push_back(std::move(changedList));
    }

    // Cue points: Lexicon / rekordbox songs have them. A song without any
    // gets hot cues where its sections start (Intro, Chorus, Outro...).
    int generated = 0;
    data.cues = [this, &generated](const MixHouseTrack& t) {
        bool fromPhrases = false;
        std::vector<MixHouseCue> cues = cuesFor(t, &fromPhrases);
        generated += fromPhrases && !cues.empty() ? 1 : 0;
        return cues;
    };

    const QString error = writeRekordboxXml(path, data);
    if (!error.isEmpty()) {
        interactive()->error(muse::trc("mixhouse", "Export rekordbox XML"), error.toStdString());
        return;
    }
    LOGI() << "rekordbox XML: " << data.tracks.size() << " tracks written to " << path << ", phrase cues for "
           << generated;
    std::string text = muse::qtrc("mixhouse", "%1 tracks and their playlists are in %2.")
                       .arg(data.tracks.size()).arg(QDir::toNativeSeparators(path)).toStdString();
    if (generated > 0) {
        text += "\n\n" + muse::qtrc("mixhouse", "%n song(s) without cue points got hot cues where their phrases start "
                                                 "(Intro, Chorus, Outro...), and memory cues at the same places.",
                                      nullptr, generated).toStdString();
    }
    text += "\n\n" + muse::qtrc("mixhouse", "rekordbox: Preferences > Advanced > Database > rekordbox xml, choose this file, "
                                           "then import the tracks or playlists from \"rekordbox xml\" in the tree.\n"
                                           "Lexicon: Import > rekordbox XML.").toStdString();
    interactive()->info(muse::trc("mixhouse", "rekordbox XML saved"), text);
}

void MixHouseTrackListModel::setAnalyzeView()
{
    setPlaylist(-1);
    m_query.unanalyzedOnly = true;
    load();
}

void MixHouseTrackListModel::setHistoryView()
{
    setPlaylist(-1);
    m_query.playedOnly = true;
    m_query.sort = IMixHouseLibrary::SortKey::LastPlayedAt;
    m_query.ascending = false;
    load();
}

void MixHouseTrackListModel::setRecordingsView()
{
    // The export folder: where finished mixes are saved.
    const QString folder = exportConfiguration() ? exportConfiguration()->directoryPath().toQString() : QString();
    setBrowseFolder(folder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation) : folder);
}

void MixHouseTrackListModel::setBrowseFolder(const QString& path)
{
    if (m_externalSource == u"folder" && m_browseFolder == path) {
        return;
    }
    m_externalSource = QStringLiteral("folder");
    m_externalPlaylist = -1;
    m_browseFolder = path;
    m_query.playlistId = -1;
    m_query.crateId = -1;
    m_query.missingOnly = false;
    m_query.hiddenOnly = false;
    m_query.unanalyzedOnly = false;
    m_query.playedOnly = false;
    m_emptyPlaceholder = false;
    m_query.filter.clear();
    load();
}

static bool isDroppedAudio(const QString& path);

MixHouseTrackList MixHouseTrackListModel::browseFolderTracks() const
{
    // Ids for files not in the library: below rekordbox XML's range
    // (-1,000,000,000 - index), so they count as external (read-only).
    constexpr int kBrowseIdBase = -2000000000;
    constexpr int kMaxFiles = 2000;
    MixHouseTrackList out;
    const QFileInfoList files = QDir(m_browseFolder).entryInfoList(QDir::Files | QDir::Readable,
                                                                   QDir::Name | QDir::IgnoreCase);
    const QString filter = m_query.filter.trimmed();
    for (const QFileInfo& info : files) {
        if (static_cast<int>(out.size()) >= kMaxFiles) {
            break;
        }
        if (!isDroppedAudio(info.fileName())) {
            continue;
        }
        IMixHouseLibrary::TrackQuery query;
        query.location = info.absoluteFilePath();
        query.limit = 1;
        MixHouseTrackList known = library()->tracks(query);
        MixHouseTrack t;
        if (!known.empty()) {
            t = known.front();
        } else {
            const MixHouseFileTags tags = readFileTags(info.absoluteFilePath());
            t.id = kBrowseIdBase - static_cast<int>(out.size());
            t.title = tags.title;
            t.artist = tags.artist;
            t.album = tags.album;
            t.genre = tags.genre;
            t.year = tags.year;
            t.filePath = info.absoluteFilePath();
            t.fileType = info.suffix().toLower();
            t.durationSec = -1.0;   // not known without decoding: shown as a dash
        }
        if (!filter.isEmpty() && !t.title.contains(filter, Qt::CaseInsensitive)
            && !t.artist.contains(filter, Qt::CaseInsensitive) && !t.album.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        out.push_back(t);
    }
    return out;
}

static QStringList customQuickLinks()
{
    return QSettings().value(QStringLiteral("GlideMix/quickLinks")).toStringList();
}

// GlideMix: the usual folders can be removed from Quick Links too; + brings one back.
static const QString kHiddenQuickLinks = QStringLiteral("GlideMix/hiddenQuickLinks");

void MixHouseTrackListModel::addQuickLink(const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    QStringList hidden = QSettings().value(kHiddenQuickLinks).toStringList();
    if (hidden.removeAll(clean) > 0) {
        QSettings().setValue(kHiddenQuickLinks, hidden);
        emit loaded();
    }
    QStringList links = customQuickLinks();
    if (clean.isEmpty() || links.contains(clean)) {
        return;
    }
    links << clean;
    QSettings().setValue(QStringLiteral("GlideMix/quickLinks"), links);
    emit loaded();   // the sidebar re-reads its nodes
}

void MixHouseTrackListModel::removeQuickLink(const QString& path)
{
    const QString clean = QDir::cleanPath(path);
    QStringList links = customQuickLinks();
    if (links.removeAll(clean) > 0) {
        QSettings().setValue(QStringLiteral("GlideMix/quickLinks"), links);
    }
    QStringList hidden = QSettings().value(kHiddenQuickLinks).toStringList();
    if (!hidden.contains(clean)) {
        hidden << clean;
        QSettings().setValue(kHiddenQuickLinks, hidden);
    }
    emit loaded();
}

QList<int> MixHouseTrackListModel::resolveOrImportTrackIds(const QVariantList& rows)
{
    QList<int> ids;
    QStringList toImport;
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row < 0 || row >= static_cast<int>(m_tracks.size())) {
            continue;
        }
        const MixHouseTrack& t = m_tracks[static_cast<size_t>(row)];
        if (!isExternalTrack(t)) {
            ids.append(t.id);
        } else if (const int known = libraryIdForFile(t.filePath); known >= 0) {
            ids.append(known);
        } else if (!t.filePath.isEmpty() && QFileInfo::exists(t.filePath)) {
            toImport << t.filePath;
        }
    }
    if (!toImport.isEmpty()) {
        library()->addTracksFromFiles(toImport);
        for (const QString& path : toImport) {
            IMixHouseLibrary::TrackQuery query;
            query.location = path;
            query.limit = 1;
            const MixHouseTrackList found = library()->tracks(query);
            if (!found.empty()) {
                ids.append(found.front().id);
            }
        }
    }
    return ids;
}

bool MixHouseTrackListModel::importRows(const QVariantList& rows)
{
    bool any = false;
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < static_cast<int>(m_tracks.size()) && isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
            any = true;
        }
    }
    if (!any) {
        return false;
    }
    resolveOrImportTrackIds(rows);
    load();
    return true;
}

void MixHouseTrackListModel::setEmptyPlaceholder()
{
    if (m_emptyPlaceholder) {
        return;
    }
    leaveExternal();
    m_query.playlistId = -1;
    m_query.crateId = -1;
    m_query.missingOnly = false;
    m_query.hiddenOnly = false;
    m_query.unanalyzedOnly = false;
    m_query.playedOnly = false;
    m_emptyPlaceholder = true;
    m_query.filter.clear();   // see setPlaylist()'s own comment
    load();
}

void MixHouseTrackListModel::setKeyFilters(const QStringList& requested)
{
    // Kept in Lancelot whatever the notation shown: the wheel's selection is
    // Lancelot, and the external libraries match on it.
    QStringList keys;
    for (const QString& key : requested) {
        const int keyId = keyIdFromText(key);
        keys << (keyId > 0 ? IMixHouseKeyMatch::camelot(keyId) : key);
    }
    if (m_query.keyFilters == keys) {
        return;
    }
    m_query.keyFilters = keys;
    load();
}

void MixHouseTrackListModel::importTagsFromFiles(const QVariantList& rows, bool allMatching)
{
    if (m_importingTags) {
        return;
    }
    const QList<int> ids = allMatching && m_externalSource.isEmpty() ? library()->matchingTracks(m_query).ids : resolveTrackIds(rows);
    const QHash<int, QString> locations = library()->trackLocations(ids);
    if (locations.isEmpty()) {
        return;
    }
    m_importingTags = true;
    setAnalysisMessage(muse::qtrc("mixhouse", "Reading the tags of %n song(s)…", nullptr, locations.size()));

    using Read = std::vector<std::pair<int, MixHouseFileTags> >;
    auto read = std::make_shared<Read>();
    read->reserve(locations.size());
    QThread* thread = QThread::create([locations, read]() {
        for (auto it = locations.constBegin(); it != locations.constEnd(); ++it) {
            const MixHouseFileTags tags = readFileTags(it.value());
            // A moved or unreadable file has no tags to give.
            if (tags.fromTags || !tags.artist.isEmpty() || !tags.album.isEmpty() || !tags.genre.isEmpty()
                || !tags.year.isEmpty() || tags.replayGain > 0.0 || tags.replayGainPeak >= 0.0) {
                read->emplace_back(it.key(), tags);
            }
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, [this, read]() {
        m_importingTags = false;
        const int changed = library()->applyFileTags(*read);
        const int count = static_cast<int>(read->size());
        const QString text = changed < 0 ? muse::qtrc("mixhouse", "The tags could not be saved to the library.")
                             : changed == 0 ? muse::qtrc("mixhouse", "Tags read from %n song(s): already up to date.", nullptr, count)
                             : muse::qtrc("mixhouse", "Tags read from %n song(s): %1 updated.", nullptr, count).arg(changed);
        setAnalysisMessage(text);
        QTimer::singleShot(8000, this, [this, text]() {
            if (m_analysisMessage == text) {
                setAnalysisMessage(QString());
            }
        });
        load();
    });
    thread->start();
}

int MixHouseTrackListModel::rowOfTrack(int trackId) const
{
    for (size_t i = 0; i < m_tracks.size(); ++i) {
        if (m_tracks[i].id == trackId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

QVariantMap MixHouseTrackListModel::trackInfo(int trackId) const
{
    QVariantMap out;
    IMixHouseLibrary::TrackQuery query;
    query.trackId = trackId;
    query.limit = 1;
    const MixHouseTrackList found = library()->tracks(query);
    if (!found.empty()) {
        out["title"] = found.front().title;
        out["album"] = found.front().album;
        out["artist"] = found.front().artist;
    }
    return out;
}

QString MixHouseTrackListModel::lancelotKey(const QString& key) const
{
    const int keyId = keyIdFromText(key);
    return keyId > 0 ? IMixHouseKeyMatch::camelot(keyId) : key;
}

static QVariantList toVariantList(const MixHouseNamedList& items)
{
    QVariantList out;
    for (const MixHouseNamedItem& it : items) {
        QVariantMap m;
        m["id"] = it.id;
        m["name"] = it.name;
        m["count"] = it.count;
        out.append(m);
    }
    return out;
}

namespace {
QVariantMap node(const QString& id, const QString& name, int level,
                 const QString& kind, int refId, int count, bool expandable,
                 bool locked = false)
{
    QVariantMap m;
    m["nodeId"] = id;
    m["name"] = name;
    m["level"] = level;
    m["kind"] = kind;          // what clicking it should show
    m["refId"] = refId;        // playlist/crate id, or -1
    m["count"] = count;
    m["expandable"] = expandable;
    m["locked"] = locked;      // Playlists.locked / crates.locked -- see MixHouseNamedItem
    return m;
}

/// Playlist folders/nesting -- see MixHouseNamedItem::parentId's own comment
/// for why this is original work (real mixxx has no folder concept for its
/// own Playlists table at all). Recursively renders `all`'s real parent_id
/// tree, one level of the real hierarchy at a time -- only ever descending
/// into a folder that's actually expanded, same lazy-render rule the
/// Playlists/Crates/History container nodes above already use for their own
/// (until now always flat) children. A folder's own displayed count is the
/// number of real children it directly holds (its own PlaylistTracks count
/// is always 0 -- nothing here ever adds a track straight to a folder row),
/// which is what real rekordbox itself shows next to a folder too.
//! A folder in Computer > Quick Links / Devices, and its subfolders when open.
void appendFolderNode(QVariantList& out, const QString& path, const QString& name, int level, bool removable,
                      const QStringList& expandedIds)
{
    if (path.isEmpty() || !QFileInfo(path).isDir()) {
        return;
    }
    const QString id = QStringLiteral("dir:") + path;
    const QFileInfoList subfolders = QDir(path).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable,
                                                              QDir::Name | QDir::IgnoreCase);
    QVariantMap n = node(id, name.isEmpty() ? path : name, level, QStringLiteral("folder-browse"), -1, 0,
                         !subfolders.isEmpty());
    n["path"] = path;
    n["removable"] = removable;
    out << n;
    if (expandedIds.contains(id) && level < 12) {
        for (const QFileInfo& sub : subfolders) {
            if (!sub.fileName().startsWith(u'.') && !sub.isBundle()) {
                appendFolderNode(out, sub.absoluteFilePath(), sub.fileName(), level + 1, false, expandedIds);
            }
        }
    }
}

void appendPlaylistTree(QVariantList& out, const MixHouseNamedList& all, int parentId, int level,
                        const QStringList& expandedIds)
{
    for (const MixHouseNamedItem& p : all) {
        if (p.parentId != parentId) {
            continue;
        }
        const QString nodeId = QStringLiteral("pl-") + QString::number(p.id);
        int displayCount = p.count;
        if (p.isFolder) {
            displayCount = 0;
            for (const MixHouseNamedItem& child : all) {
                if (child.parentId == p.id) {
                    ++displayCount;
                }
            }
        }
        // A folder never itself represents a track view -- "none" is the
        // same inert kind the Playlists/Crates/History CONTAINER nodes
        // below already use: expandable, but MixHouseLibraryPanel.qml's own
        // click dispatch doesn't treat "none" as a real view switch, only
        // ever as an expand/collapse toggle.
        QVariantMap n = node(nodeId, p.name, level, p.isFolder ? QStringLiteral("none") : QStringLiteral("playlist"),
                             p.id, displayCount, p.isFolder, p.locked);
        n["parentId"] = p.parentId;   // where a drag-and-drop move puts its neighbours
        out << n;
        if (p.isFolder && expandedIds.contains(nodeId)) {
            appendPlaylistTree(out, all, p.id, level + 1, expandedIds);
        }
    }
}
/// Another DJ app's folder/playlist tree under its sidebar node, the same
/// lazy one-level-at-a-time rendering as appendPlaylistTree.
void appendExternalTree(QVariantList& out, const std::vector<IMixHouseExternalLibrary::Node>& all,
                        const QString& idPrefix, const QString& playlistKind, int parentId, int level,
                        const QStringList& expandedIds)
{
    for (const IMixHouseExternalLibrary::Node& n : all) {
        if (n.parentId != parentId) {
            continue;
        }
        const QString nodeId = idPrefix + QString::number(n.id);
        const bool folder = n.type == IMixHouseExternalLibrary::NodeType::Folder;
        // Smart lists are the other app's own rule queries -- not evaluated here.
        const QString kind = folder ? QStringLiteral("none")
                             : n.type == IMixHouseExternalLibrary::NodeType::SmartList ? QStringLiteral("placeholder")
                             : playlistKind;
        out << node(nodeId, n.name, level, kind, n.id, n.trackCount, folder, true);
        if (folder && expandedIds.contains(nodeId)) {
            appendExternalTree(out, all, idPrefix, playlistKind, n.id, level + 1, expandedIds);
        }
    }
}
}  // namespace

void MixHouseTrackListModel::recountSidebar()
{
    if (!library()) {
        return;
    }
    QElapsedTimer took;
    took.start();
    m_lastRecount.start();
    m_countsStale = false;
    m_smartCounts.clear();
    for (const MixHouseSmartPlaylist& p : library()->smartPlaylists()) {
        m_smartCounts.insert(p.id, smartPlaylistCount(p));
    }
    IMixHouseLibrary::TrackQuery q;
    q.limit = 0;
    m_duplicateCount = static_cast<int>(duplicateTracks(q, nullptr, nullptr).size());
    m_unanalyzedCount = library()->unanalyzedTrackCount();
    LOGI() << "GlideMix: sidebar counts in " << took.elapsed() << " ms";
    emit sidebarCountsChanged();
}

QVariantList MixHouseTrackListModel::sidebarNodes(const QStringList& expandedIds) const
{
    if (m_countsStale && !m_countsTimer.isActive()) {
        m_countsTimer.start();
    }
    // Mixxx's sidebar is a FIXED hierarchy of features. Listing the Playlists
    // table directly is what produced 123 date-named rows at the top level --
    // those are session logs (PLHT_SET_LOG), and they belong under History.
    QVariantList out;
    auto isOpen = [&](const QString& id) { return expandedIds.contains(id); };

    // rename to match real rekordbox's own top-level naming
    // ("Collection" there, with "All"/"All Tracks" as the actual clickable
    // item under it -- see the real screenshots he sent). nodeId/kind stay
    // "tracks"/"all" unchanged -- this only touches what's displayed.
    out << node("tracks", QObject::tr("Music Collection"), 0, "all", -1, library()->trackCount(), true);
    if (isOpen("tracks")) {
        out << node("missing", QObject::tr("Missing Tracks"), 1, "missing", -1,
                    library()->missingTrackCount(), false);
        out << node("duplicates", QObject::tr("Duplicates"), 1, "duplicates", -1, m_duplicateCount, false);
        // GlideMix: no Hidden Tracks node. Nothing hides a track here
        // (Remove from Library deletes it), so it was always empty.
    }

    // Auto DJ exposes its Crates child in the same way as Mixxx's sidebar.
    // GlideMix: no Auto DJ and no Crates in the sidebar -- as in rekordbox,
    // playlists and folders organise the collection.
    const MixHouseNamedList pls = library()->playlists();
    // Real Playlists count at the root header: top-level items only (real
    // rekordbox's own Playlists root header counts the same way -- items
    // nested inside a folder aren't "in" the root, they're in the folder),
    // not every row in the whole table regardless of nesting depth.
    int topLevelPlaylistCount = 0;
    for (const MixHouseNamedItem& p : pls) {
        if (p.parentId < 0) {
            ++topLevelPlaylistCount;
        }
    }
    out << node("playlists", QObject::tr("Playlists"), 0, "none", -1, topLevelPlaylistCount, true);
    if (isOpen("playlists")) {
        appendPlaylistTree(out, pls, -1, 1, expandedIds);
    }

    // GlideMix: smart playlists (rules), right below the playlists.
    const std::vector<MixHouseSmartPlaylist> smart = library()->smartPlaylists();
    out << node("smart", QObject::tr("Smart Playlists"), 0, "none", -1, static_cast<int>(smart.size()), true);
    if (isOpen("smart")) {
        for (const MixHouseSmartPlaylist& p : smart) {
            // A new or edited playlist: counted on its own now (one playlist is quick).
            if (!m_smartCounts.contains(p.id) && !m_countsStale) {
                m_smartCounts.insert(p.id, smartPlaylistCount(p));
            }
            out << node("sp-" + QString::number(p.id), p.name, 1, "smart-playlist", p.id,
                        m_smartCounts.value(p.id, -1), false);
        }
    }

    out << node("computer", QObject::tr("Computer"), 0, "placeholder", -1, 0, true);
    if (isOpen("computer")) {
        // GlideMix: real folders -- Quick Links (the usual ones plus the
        // user's own, added with +) and Devices (the mounted drives); a
        // click lists a folder's music without importing it.
        out << node("computer-quick-links", QObject::tr("Quick Links"), 1, "none", -1, 0, true);
        if (isOpen("computer-quick-links")) {
            const QStringList standard = {
                QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
                QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
                QStandardPaths::writableLocation(QStandardPaths::DesktopLocation),
                QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            };
            const QStringList hidden = QSettings().value(kHiddenQuickLinks).toStringList();
            for (const QString& path : standard) {
                if (!hidden.contains(QDir::cleanPath(path))) {
                    appendFolderNode(out, path, QDir(path).dirName(), 2, true, expandedIds);
                }
            }
            for (const QString& path : QSettings().value(QStringLiteral("GlideMix/quickLinks")).toStringList()) {
                if (!standard.contains(path)) {
                    appendFolderNode(out, path, QDir(path).dirName(), 2, true, expandedIds);
                }
            }
        }
        out << node("computer-devices", QObject::tr("Devices"), 1, "none", -1, 0, true);
        // GlideMix: not on Windows -- it lists only /Volumes, and asking every
        // drive letter there blocks the UI on a dead network drive.
#ifndef Q_OS_WIN
        if (isOpen("computer-devices")) {
            for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
                const QString root = volume.rootPath();
                if (!volume.isValid() || !volume.isReady() || (root != u"/" && !root.startsWith(u"/Volumes/"))) {
                    continue;   // system and snapshot mounts
                }
                appendFolderNode(out, root, volume.displayName(), 2, false, expandedIds);
            }
        }
#endif
    }

    out << node("recordings", QObject::tr("Recordings"), 0, "recordings-view", -1, 0, false);

    const MixHouseNamedList hist = library()->historySessions();
    out << node("history", QObject::tr("History"), 0, "history-view", -1,
                static_cast<int>(hist.size()), true);
    if (isOpen("history")) {
        for (const MixHouseNamedItem& h : hist) {
            out << node("h-" + QString::number(h.id), h.name, 1, "playlist", h.id, h.count, false);
        }
    }

    out << node("analyze", QObject::tr("Analyze"), 0, "analyze-view", -1, m_unanalyzedCount, false);
    // GlideMix: a rekordbox XML export and Lexicon DJ's own library --
    // each one's tracks under the parent, its folders and playlists as
    // children (read-only).
    const bool rekordboxReady = rekordboxXml() && rekordboxXml()->available();
    const bool rekordboxLoading = rekordboxXml() && rekordboxXml()->loading();
    const QStringList hiddenApps = hiddenDjApps();   // Preferences > Library
    // Nothing chosen yet: a click asks for the file (rekordbox: File >
    // Export Collection in xml format). While it reads, the row shows the
    // progress instead of the count (MixHouseLibraryPanel.qml).
    const bool rekordboxShown = !hiddenApps.contains(QStringLiteral("rekordbox"));
    if (rekordboxShown) {
        out << node("rekordbox", QObject::tr("Rekordbox XML"), 0,
                    rekordboxReady ? "rekordbox-all" : rekordboxLoading ? "none" : "rekordbox-choose", -1,
                    rekordboxReady && !rekordboxLoading ? rekordboxXml()->trackCount() : 0, rekordboxReady);
    }
    if (rekordboxShown && rekordboxReady && isOpen("rekordbox")) {
        appendExternalTree(out, rekordboxXml()->nodes(), QStringLiteral("rb-"), QStringLiteral("rekordbox-playlist"), -1, 1,
                           expandedIds);
    }
    if (lexicon() && lexicon()->available()) {
        out << node("lexicon", QObject::tr("Lexicon DJ"), 0, "lexicon-all", -1, lexicon()->trackCount(), true);
        if (isOpen("lexicon")) {
            appendExternalTree(out, lexicon()->nodes(), QStringLiteral("lx-"), QStringLiteral("lexicon-playlist"), -1, 1,
                               expandedIds);
        }
    }
    // GlideMix: the other DJ apps on this computer, read-only, like Lexicon.
    for (const QString& source : djAppSources()) {
        if (hiddenApps.contains(source)) {
            continue;
        }
        if (djAppWaitsForClick(source)) {
            out << node(source, djAppName(source), 0, source + QStringLiteral("-read"), -1, 0, false);
            continue;
        }
        const IMixHouseExternalLibrary* app = djAppLibrary(source);
        if (!app || !app->available()) {
            continue;
        }
        const std::vector<IMixHouseExternalLibrary::Node> nodes = app->nodes();
        out << node(source, djAppName(source), 0, source + QStringLiteral("-all"), -1, app->trackCount(), !nodes.empty());
        if (!nodes.empty() && isOpen(source)) {
            appendExternalTree(out, nodes, source + QStringLiteral(":"), source + QStringLiteral("-playlist"), -1, 1,
                               expandedIds);
        }
    }
    return out;
}
QStringList MixHouseTrackListModel::availableKeys() const { return library()->keys(); }

bool MixHouseTrackListModel::openInGridEditor(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size()) || !gridEditor()) {
        return false;
    }
    const MixHouseTrack& t = m_tracks[static_cast<size_t>(row)];
    if (isExternalTrack(t)) {
        return false;   // Lexicon / rekordbox XML rows: edit their grids in that app
    }
    gridEditor()->openTrack(t.id);
    return true;
}

bool MixHouseTrackListModel::loadTrackToProject(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) {
        return false;
    }
    return loadTrack(m_tracks[static_cast<size_t>(row)]);
}

void MixHouseTrackListModel::loadTracksToProject(const QVariantList& rows)
{
    // The tracks, not the rows: the list can re-sort while they load (History).
    std::vector<int> sorted;
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < static_cast<int>(m_tracks.size())) {
            sorted.push_back(row);
        }
    }
    std::sort(sorted.begin(), sorted.end());
    const bool idle = m_loadQueue.empty();
    if (idle) {
        const auto project = globalContext()->currentTrackeditProject();
        m_loadFirstLane = project ? project->trackIdList().size() : 0;
        m_loadCount = 0;
    }
    for (const int row : sorted) {
        m_loadQueue.push_back(m_tracks[static_cast<size_t>(row)]);
    }
    m_loadCount += sorted.size();
    if (idle) {
        loadNextQueuedTrack();
    }
}

void MixHouseTrackListModel::loadNextQueuedTrack()
{
    if (m_loadQueue.empty()) {
        // The view ends on the first songs of the range, readable: all of
        // them at once would be slivers.
        const auto project = globalContext()->currentTrackeditProject();
        if (project && trackAligner() && m_loadCount > 1) {
            constexpr size_t kSongsInView = 4;
            const auto lanes = project->trackIdList();
            const size_t last = std::min(lanes.size(), m_loadFirstLane + kSongsInView);
            double start = std::numeric_limits<double>::max();
            double end = 0.0;
            for (size_t i = m_loadFirstLane; i < last; ++i) {
                for (const trackedit::Clip& c : project->clipList(lanes[i])) {
                    start = std::min(start, c.startTime);
                    end = std::max(end, c.endTime);
                }
            }
            if (end > start && m_loadFirstLane < lanes.size()) {
                const trackedit::TrackId first = lanes[m_loadFirstLane];
                selectionController()->setSelectedTracks({ first }, true);
                trackNavigationController()->setFocusedTrack(first, false);
                // After the scroll to the last song's lane, which comes later.
                QTimer::singleShot(100, this, [this, start, end, first]() {
                    trackAligner()->requestViewFit(start, end, first);
                });
            }
        }
        return;
    }
    bool cancelled = false;
    loadTrack(m_loadQueue.front(), &cancelled);
    m_loadQueue.pop_front();
    if (cancelled) {
        m_loadQueue.clear();   // Cancel means the range; a missing file skips one song
    }
    // One event loop turn between songs: the lanes and the Project tab catch up.
    QTimer::singleShot(0, this, [this]() { loadNextQueuedTrack(); });
}

bool MixHouseTrackListModel::loadTrack(const MixHouseTrack& track, bool* cancelled) const
{
    const MixHouseTrack t = withBeatGrid(track);
    if (t.filePath.isEmpty()) {
        LOGW() << "loadTrackToProject: no file path for track " << t.title;
        return false;
    }

    const au::project::IAudacityProjectPtr project = globalContext()->currentProject();
    if (!project) {
        LOGW() << "loadTrackToProject: no open project to load " << t.title << " into";
        return false;
    }

    const auto trackeditProject = project->trackeditProject();
    const std::vector<trackedit::TrackId> tracksBeforeImport = trackeditProject
        ? trackeditProject->trackIdList() : std::vector<trackedit::TrackId>();
    // Same test Audacity's importer uses for "first file into an empty
    // project" (Au3Importer::isProjectEmpty) -- decides whether this track
    // sets the project tempo or gets stretched to it.
    const bool projectWasEmpty = !trackeditProject || !trackeditProject->hasAudioContent().val;

    //! The library knows this track's tempo and the aligner applies it below,
    //! so the import's own detection -- a guess plus a dialog whose answer
    //! would be overwritten -- is skipped.
    const muse::Ret ret = project->import(muse::io::path_t(t.filePath.toStdString()), false, t.bpm <= 0.0);
    if (!ret) {
        if (cancelled) {
            *cancelled = ret.code() == static_cast<int>(muse::Ret::Code::Cancel);
        }
        LOGE() << "loadTrackToProject: import failed for " << t.filePath << ": " << ret.toString();
        return false;
    }
    if (!isExternalTrack(t)) {
        library()->markTrackPlayed(t.id);   // History
    }

    // The stock import UI ends with the imported lane selected/focused.  The
    // MixHouse table calls IAudacityProject::import() directly, so that UI
    // side effect does not happen and the effects panel can remain focused on
    // an older lane.  Reproduce the same state transition here so an effect
    // added immediately after loading is attached to the track the user just
    // loaded (and remains visible there).
    if (trackeditProject) {
        const std::vector<trackedit::TrackId> tracksAfterImport = trackeditProject->trackIdList();
        trackedit::TrackIdList importedTracks;
        for (const trackedit::TrackId trackId : tracksAfterImport) {
            if (std::find(tracksBeforeImport.begin(), tracksBeforeImport.end(), trackId)
                == tracksBeforeImport.end()) {
                importedTracks.push_back(trackId);
            }
        }
        if (!importedTracks.empty() && !projectWasEmpty && trackAligner()) {
            // GlideMix: a double-clicked song joins the end of the mix, its
            // IN on the last song's OUT, the way a drop there would place it
            // -- it used to land at 0:00 under everything else.
            trackedit::ClipKeyList newClips;
            double mixEnd = 0.0;
            if (const auto prj = globalContext()->currentTrackeditProject()) {
                for (const trackedit::TrackId id : tracksAfterImport) {
                    const bool isNew = std::find(importedTracks.begin(), importedTracks.end(), id) != importedTracks.end();
                    for (const trackedit::Clip& c : prj->clipList(id)) {
                        if (isNew) {
                            newClips.push_back(c.key);
                        } else {
                            mixEnd = std::max(mixEnd, c.endTime);
                        }
                    }
                }
                for (const trackedit::ClipKey& clip : newClips) {
                    trackeditInteraction()->changeClipStartTime(clip, prj->clip(clip).startTime + mixEnd, true);
                }
            }
            // The widest pull a drop has (64 bars): the new song's intro and
            // the last song's outro both lie between the two anchors.
            trackAligner()->alignDroppedFiles({ muse::io::path_t(t.filePath.toStdString()) }, { newClips }, false, 1e-6);
            // A song split at a grid jump is more than one clip now.
            if (const auto prj = globalContext()->currentTrackeditProject()) {
                newClips.clear();
                for (const trackedit::TrackId id : importedTracks) {
                    for (const trackedit::Clip& c : prj->clipList(id)) {
                        newClips.push_back(c.key);
                    }
                }
            }
            // Show it the way a drop does: scrolled to, not zoomed.
            if (const auto prj = globalContext()->currentTrackeditProject(); prj && !newClips.empty()) {
                const trackedit::Clip placed = prj->clip(newClips.front());
                const double focusStart = placed.startTime;
                trackAligner()->requestViewFocus(focusStart, placed.endTime, importedTracks.front());
            }
            selectionController()->setSelectedTracks(importedTracks, true);
            selectionController()->setSelectedClips(newClips, true);
            trackNavigationController()->setFocusedTrack(importedTracks.front(), false);
            LOGI() << "loadTrackToProject: appended to the mix at " << mixEnd << " s: " << t.title;
        } else if (!importedTracks.empty()) {
            MixHouseAlignRequest align;
            align.importedTracks = importedTracks;
            align.bpm = t.bpm;
            align.firstBeatSec = t.beats.firstBeatSec;
            align.downbeatSec = t.downbeatSec;
            align.downbeatConfidence = t.downbeatConfidence;
            align.projectWasEmpty = projectWasEmpty;
            align.lockToBars = !libraryConfiguration() || libraryConfiguration()->syncEnabled();
            align.snapBars = 1;
            const MixHouseAlignResult aligned = alignImportedTrackToBars(globalContext(), trackeditInteraction(), align);
            if (!aligned.aligned) {
                LOGI() << "loadTrackToProject: not aligned to bars (" << aligned.reason << "): " << t.title;
            } else {
                if (tempoMap() && !projectWasEmpty) {
                    tempoMap()->trackPlaced(aligned.stretchRatio);
                }
                // The import's undo step was recorded before the alignment
                // (as for a drop, MixHouseTrackAligner::alignDroppedFiles):
                // redo after an undo brought the song back unaligned.
                if (projectHistory()) {
                    projectHistory()->modifyState();
                }
            }
            selectionController()->setSelectedTracks(importedTracks, true);
            // GlideMix: the new song alone is selected -- a clip left selected
            // from before moved along with it on the next drag.
            if (const auto prj = globalContext()->currentTrackeditProject()) {
                trackedit::ClipKeyList clips;
                for (const trackedit::TrackId id : importedTracks) {
                    for (const trackedit::Clip& c : prj->clipList(id)) {
                        clips.push_back(c.key);
                    }
                }
                selectionController()->setSelectedClips(clips, true);
            }
            trackNavigationController()->setFocusedTrack(importedTracks.front(), false);
            LOGI() << "loadTrackToProject: selected/focused imported track " << importedTracks.front();
        } else {
            LOGW() << "loadTrackToProject: import succeeded but no new track was found for " << t.filePath;
        }
    }
    LOGI() << "loadTrackToProject: imported " << t.artist << " - " << t.title;
    return true;
}

//! The physical left button right now, not Qt's queued view of it.
static bool leftMouseButtonHeld()
{
#if defined(Q_OS_MACOS)
    return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft);
#elif defined(Q_OS_WIN)
    const int key = GetSystemMetrics(SM_SWAPBUTTON) ? VK_RBUTTON : VK_LBUTTON;
    return (GetAsyncKeyState(key) & 0x8000) != 0;
#else
    return true;
#endif
}

void MixHouseTrackListModel::startExternalDrag(const QVariantList& rows)
{
    std::vector<MixHouseTrack> tracks;
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < static_cast<int>(m_tracks.size())) {
            tracks.push_back(m_tracks[static_cast<size_t>(row)]);
        }
    }
    startExternalDragTracks(tracks);
}

void MixHouseTrackListModel::startExternalDragTracks(const std::vector<MixHouseTrack>& tracks)
{
    // Real mixxx: DragAndDropHelper::dragTrackLocations() (src/util/dnd.cpp)
    // builds this same QList<QUrl> from getSelectedRows(), one
    // QUrl::fromLocalFile() per real track_locations.location -- then hands
    // it to dragUrls(), which is exactly the QMimeData::setUrls() +
    // QDrag::exec(Qt::CopyAction) below. There's no separate "this is an
    // internal drag" MIME type on either side: Qt tells drop targets
    // in-app apart from external ones via QDropEvent::source() (null when
    // the drag came from another application), not via anything this
    // method needs to set up.
    QList<QUrl> urls;
    QStringList titles;
    QStringList missing;
    urls.reserve(static_cast<qsizetype>(tracks.size()));
    titles.reserve(static_cast<qsizetype>(tracks.size()));
    for (const MixHouseTrack& t : tracks) {
        // Never drag out a row with no real file (a Missing Tracks entry,
        // or -- in principle -- a stale index from a reload mid-drag).
        if (t.filePath.isEmpty()) {
            continue;
        }
        // GlideMix: a file moved or deleted since it was added. Dropped on the
        // timeline it used to fail without a word.
        if (!QFileInfo::exists(t.filePath)) {
            missing << (t.artist.isEmpty() ? t.title : (t.artist + QStringLiteral(" - ") + t.title));
            continue;
        }
        urls.append(QUrl::fromLocalFile(t.filePath));
        titles.append(t.artist.isEmpty() ? t.title : (t.artist + QStringLiteral(" - ") + t.title));
    }
    if (!missing.isEmpty()) {
        library()->verifyTrackLocations();   // they now show under Missing Tracks
        interactive()->warning(muse::trc("mixhouse", "Track file not found"),
                               muse::qtrc("mixhouse", "The file of %1 is no longer where the library last found it "
                                                      "(moved, renamed or deleted). It is listed under Missing Tracks.")
                               .arg(missing.join(QStringLiteral(", "))).toStdString());
    }
    if (urls.isEmpty()) {
        return;
    }

    QMimeData* mimeData = new QMimeData();
    mimeData->setUrls(urls);
    // Real mixxx's own sourceIdentifier (dragUrls()'s 3rd argument) --
    // "library" here, a deck group name (e.g. "[Channel1]") when the drag
    // instead starts from a deck's waveform. Nothing in this port reads it
    // back yet (no in-app drop targets exist), but setting it now keeps
    // this call a faithful match to the real one, and costs nothing.
    mimeData->setText(QStringLiteral("library"));

    // QDrag(this): `this` only matters for QDropEvent::source() checks an
    // in-app drop target could make later (real mixxx passes its own
    // WTrackTableView* the same way) -- irrelevant to a drop in Finder or
    // another application, which is the case this method exists for.
    QDrag* drag = new QDrag(this);
    drag->setMimeData(mimeData);
    drag->setPixmap(dragPixmap(titles));
    // Blocking, like real mixxx's own call: QDrag::exec() runs its own
    // event loop until the drag is dropped or cancelled. Qt hands mouse
    // tracking to the OS's native drag session for the duration, so this
    // does not re-enter QML's own mouse handling.
    // GlideMix: only while the button is really still held. When GlideMix was
    // busy (the previous song importing), the drag could start after the user
    // had let go: the OS drag then waited for a release that had already
    // happened, and the song stuck to the mouse until the next click.
    if (leftMouseButtonHeld()) {
        drag->exec(Qt::CopyAction);
    } else {
        // The real release already reached the row (Qt handled it before
        // this deferred call), so there is no grab left to release.
        delete drag;
        LOGI() << "Library drag not started: the mouse button was already released";
        return;
    }

    // GlideMix: the OS drag swallowed the mouse release, so the row that
    // started the drag still holds the mouse grab. The next press then ended
    // up finishing a stale drag instead of starting a new one: every other
    // song dragged from the library was lost ("track 6 is not created",
    // Chris, Mac). Hand the window the release it missed.
    if (QWindow* window = QGuiApplication::topLevelAt(QCursor::pos()) ? QGuiApplication::topLevelAt(QCursor::pos())
                                                                        : QGuiApplication::focusWindow()) {
        const QPointF local = window->mapFromGlobal(QCursor::pos());
        QMouseEvent release(QEvent::MouseButtonRelease, local, local, QCursor::pos(), Qt::LeftButton,
                            Qt::NoButton, QGuiApplication::keyboardModifiers());
        QCoreApplication::sendEvent(window, &release);
    }
}

QString MixHouseTrackListModel::musicFolder() const
{
    const QString music = QStandardPaths::writableLocation(QStandardPaths::MusicLocation);
    return QDir(music).exists() ? music : QDir::homePath();
}

QString MixHouseTrackListModel::rekordboxXmlFolder() const
{
    // Without a dir the Windows dialog opens in the process's working
    // folder (System32 when started by a task, the install folder from
    // the Start menu).
    if (rekordboxXml() && !rekordboxXml()->path().isEmpty()) {
        const QString dir = QFileInfo(rekordboxXml()->path()).absolutePath();
        if (QDir(dir).exists()) {
            return dir;
        }
    }
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).exists() ? docs : QDir::homePath();
}

QPixmap MixHouseTrackListModel::dragPixmap(const QStringList& titles)
{
    const QString label = titles.size() == 1
                           ? titles.first()
                           : muse::qtrc("mixhouse", "%1 tracks").arg(titles.size());
    const QFont font = QGuiApplication::font();
    const QFontMetrics metrics(font);
    const QString elided = metrics.elidedText(label, Qt::ElideRight, 240);
    const QRect textRect = metrics.boundingRect(elided);

    QPixmap pixmap(textRect.width() + 16, textRect.height() + 10);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(32, 32, 32, 230));
    painter.drawRoundedRect(pixmap.rect(), 4, 4);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, elided);
    painter.end();

    return pixmap;
}

int MixHouseTrackListModel::libraryRowHeight() const { return libraryConfiguration()->libraryRowHeight(); }
bool MixHouseTrackListModel::greyOutPlayedTracks() const { return libraryConfiguration()->greyOutPlayedTracks(); }
int MixHouseTrackListModel::searchDebounceMs() const { return libraryConfiguration()->searchDebounceMs(); }
QString MixHouseTrackListModel::libraryFontFamily() const { return libraryConfiguration()->libraryFontFamily(); }
int MixHouseTrackListModel::libraryFontPointSize() const { return libraryConfiguration()->libraryFontPointSize(); }

void MixHouseTrackListModel::setLibraryFontPointSize(int pointSize)
{
    if (libraryConfiguration() && pointSize != libraryConfiguration()->libraryFontPointSize()) {
        libraryConfiguration()->setLibraryFont(libraryConfiguration()->libraryFontFamily(), pointSize);
    }
}

void MixHouseTrackListModel::setLibraryRowHeight(int px)
{
    if (libraryConfiguration() && px != libraryConfiguration()->libraryRowHeight()) {
        libraryConfiguration()->setLibraryRowHeight(px);
    }
}

int MixHouseTrackListModel::themeFontPointSize() const
{
    return QGuiApplication::font().pointSize() > 0 ? QGuiApplication::font().pointSize() : 12;
}
int MixHouseTrackListModel::bpmPrecision() const { return libraryConfiguration()->bpmPrecision(); }
int MixHouseTrackListModel::keyNotation() const { return static_cast<int>(libraryConfiguration()->keyNotation()); }
bool MixHouseTrackListModel::keyBadges() const { return libraryConfiguration() && libraryConfiguration()->keyBadges(); }
int MixHouseTrackListModel::keyStyle() const { return libraryConfiguration() ? libraryConfiguration()->keyStyle() : 2; }
int MixHouseTrackListModel::waveformOverviewType() const
{
    return static_cast<int>(libraryConfiguration()->waveformOverviewType());
}
bool MixHouseTrackListModel::showWaveformMinuteMarkers() const
{
    return libraryConfiguration()->showWaveformMinuteMarkers();
}

void MixHouseTrackListModel::handleTrackDoubleClicked(int row)
{
    switch (libraryConfiguration()->trackDoubleClickAction()) {
    case MixHouseTrackDoubleClickAction::LoadToProject:
        loadTrackToProject(row);
        break;
    case MixHouseTrackDoubleClickAction::AddToAutoDjBottom:
        addTrackToAutoDj(row, AutoDjBottom);
        break;
    case MixHouseTrackDoubleClickAction::AddToAutoDjTop:
        addTrackToAutoDj(row, AutoDjTop);
        break;
    case MixHouseTrackDoubleClickAction::Ignore:
        break;
    }
}

QVariantList MixHouseTrackListModel::playlistsForMenu() const { return toVariantList(library()->playlists()); }
QVariantList MixHouseTrackListModel::cratesForMenu() const { return toVariantList(library()->crates()); }

bool MixHouseTrackListModel::addTrackToPlaylist(int row, int playlistId)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->addTrackToPlaylist(m_tracks[static_cast<size_t>(row)].id, playlistId);
    if (ok) {
        load();   // sidebar counts and the current view may both depend on this
    }
    return ok;
}

bool MixHouseTrackListModel::addTrackToCrate(int row, int crateId)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->addTrackToCrate(m_tracks[static_cast<size_t>(row)].id, crateId);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::setTrackColor(int row, int color)
{
    return setTracksColor(QVariantList { row }, color);
}

bool MixHouseTrackListModel::setTrackHidden(int row, bool hidden)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->setTrackHidden(m_tracks[static_cast<size_t>(row)].id, hidden);
    if (ok) {
        load();   // hiding must remove the row from the current (Tracks) view now
    }
    return ok;
}

int MixHouseTrackListModel::createPlaylistWithTrack(const QString& name, int row, int parentId)
{
    const int playlistId = library()->createPlaylist(name, parentId);
    if (playlistId >= 0) {
        // row < 0 is the real, expected case when creating from the sidebar
        // (Playlists right-click), not just a from-a-track shortcut -- still
        // needs a reload so the new playlist actually shows up there.
        if (row >= 0) {
            addTrackToPlaylist(row, playlistId);   // also calls load()
        } else {
            load();
        }
    }
    return playlistId;
}

int MixHouseTrackListModel::createPlaylistFolder(const QString& name, int parentId)
{
    // See MixHouseNamedItem::parentId's own comment for why this whole
    // feature is original work, not a port.
    const int folderId = library()->createPlaylistFolder(name, parentId);
    if (folderId >= 0) {
        load();
    }
    return folderId;
}

bool MixHouseTrackListModel::movePlaylistToFolder(int playlistId, int newParentId)
{
    return library()->movePlaylistToFolder(playlistId, newParentId);
}

bool MixHouseTrackListModel::movePlaylist(int playlistId, int parentId, int beforeId)
{
    return library() && library()->movePlaylist(playlistId, parentId, beforeId);
}

bool MixHouseTrackListModel::movePlaylistOrder(int playlistId, bool moveUp)
{
    return library()->movePlaylistOrder(playlistId, moveUp);
}

int MixHouseTrackListModel::createCrateWithTrack(const QString& name, int row)
{
    const int crateId = library()->createCrate(name);
    if (crateId >= 0) {
        if (row >= 0) {
            addTrackToCrate(row, crateId);   // also calls load()
        } else {
            load();
        }
    }
    return crateId;
}

bool MixHouseTrackListModel::renamePlaylist(int playlistId, const QString& name)
{
    const bool ok = library()->renamePlaylist(playlistId, name);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::renameCrate(int crateId, const QString& name)
{
    const bool ok = library()->renameCrate(crateId, name);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::setPlaylistLocked(int playlistId, bool locked)
{
    const bool ok = library()->setPlaylistLocked(playlistId, locked);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::setCrateLocked(int crateId, bool locked)
{
    const bool ok = library()->setCrateLocked(crateId, locked);
    if (ok) {
        load();
    }
    return ok;
}

void MixHouseTrackListModel::deletePlaylistWithConfirm(int playlistId, const QString& name, bool isFolder)
{
    // testing caught this directly testing folders: the dialog said "Delete
    // playlist ..." even when the real target was a folder. Wording only --
    // the delete itself already cascades correctly either way (a plain
    // playlist's own "subtree" is just itself).
    const std::string title = isFolder
        ? muse::qtrc("mixhouse", "Delete folder “%1”?").arg(name).toStdString()
        : muse::qtrc("mixhouse", "Delete playlist “%1”?").arg(name).toStdString();
    const std::string question = isFolder
        ? muse::trc("mixhouse",
              "The folder and everything inside it (playlists and sub-folders) will be removed. "
              "Tracks are not deleted from the library.")
        : muse::trc("mixhouse",
              "The playlist will be removed. Tracks in it are not deleted from the library.");
    const int deleteBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(title, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(deleteBtn, muse::trc("mixhouse", "Delete"), true) },
        deleteBtn);
    promise.onResolve(this, [this, playlistId](const muse::IInteractive::Result& res) {
        if (!res.isButton(muse::IInteractive::Button::Cancel) && library()->deletePlaylist(playlistId)) {
            // Real bug we hit directly: deleting the playlist currently
            // being VIEWED left m_query.playlistId pointing at an id that no
            // longer exists anywhere -- tracks()'s own JOIN against
            // PlaylistTracks then correctly (if unhelpfully) matches
            // nothing, so the table silently stayed empty forever
            // afterward, for every column/sort, while the header still
            // showed the real GLOBAL track count (a different, unrelated
            // query) -- looking exactly like a real crash even though nothing
            // actually failed. setPlaylist(-1) is the same reset every other
            // "leave a collection view" path already uses.
            if (m_query.playlistId == playlistId) {
                setPlaylist(-1);
            } else {
                load();
            }
        }
    });
}

void MixHouseTrackListModel::deleteCrateWithConfirm(int crateId, const QString& name)
{
    const std::string title = muse::qtrc("mixhouse", "Delete crate “%1”?").arg(name).toStdString();
    const std::string question = muse::trc("mixhouse",
        "The crate will be removed. Tracks in it are not deleted from the library.");
    const int deleteBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(title, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(deleteBtn, muse::trc("mixhouse", "Delete"), true) },
        deleteBtn);
    promise.onResolve(this, [this, crateId](const muse::IInteractive::Result& res) {
        if (!res.isButton(muse::IInteractive::Button::Cancel) && library()->deleteCrate(crateId)) {
            // Same real bug/fix as deletePlaylistWithConfirm's own comment,
            // for the crate-scoped view.
            if (m_query.crateId == crateId) {
                setPlaylist(-1);
            } else {
                load();
            }
        }
    });
}

void MixHouseTrackListModel::deletePlaylistsWithConfirm(const QVariantList& playlistIds)
{
    QList<int> ids;
    ids.reserve(playlistIds.size());
    for (const QVariant& v : playlistIds) {
        ids.append(v.toInt());
    }
    if (ids.isEmpty()) {
        return;
    }
    const int count = static_cast<int>(ids.size());

    const std::string title = muse::qtrc("mixhouse", "Delete %1 playlists?").arg(count).toStdString();
    const std::string question = muse::trc("mixhouse",
        "The playlists will be removed. Tracks in them are not deleted from the library.");
    const int deleteBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(title, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(deleteBtn, muse::trc("mixhouse", "Delete"), true) },
        deleteBtn);
    promise.onResolve(this, [this, ids](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        bool any = false;
        for (int id : ids) {
            if (library()->deletePlaylist(id)) {
                any = true;
            }
        }
        if (any) {
            // Same real bug/fix as deletePlaylistWithConfirm's own comment
            // -- a batch delete that happens to include whichever playlist
            // is currently being viewed hits it exactly the same way.
            if (ids.contains(m_query.playlistId)) {
                setPlaylist(-1);
            } else {
                load();
            }
        }
    });
}

void MixHouseTrackListModel::deleteCratesWithConfirm(const QVariantList& crateIds)
{
    QList<int> ids;
    ids.reserve(crateIds.size());
    for (const QVariant& v : crateIds) {
        ids.append(v.toInt());
    }
    if (ids.isEmpty()) {
        return;
    }
    const int count = static_cast<int>(ids.size());

    const std::string title = muse::qtrc("mixhouse", "Delete %1 crates?").arg(count).toStdString();
    const std::string question = muse::trc("mixhouse",
        "The crates will be removed. Tracks in them are not deleted from the library.");
    const int deleteBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(title, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(deleteBtn, muse::trc("mixhouse", "Delete"), true) },
        deleteBtn);
    promise.onResolve(this, [this, ids](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        bool any = false;
        for (int id : ids) {
            if (library()->deleteCrate(id)) {
                any = true;
            }
        }
        if (any) {
            // Same real bug/fix as deleteCrateWithConfirm's own comment.
            if (ids.contains(m_query.crateId)) {
                setPlaylist(-1);
            } else {
                load();
            }
        }
    });
}

void MixHouseTrackListModel::revealInFileBrowser(const QString& filePath) const
{
    if (filePath.isEmpty()) {
        return;
    }
    // `open -R` reveals AND selects the file in Finder -- the real "show me
    // where this lives" gesture. Qt.openUrlExternally("file://" + path) on a
    // FILE (not a directory) instead launches its default player, which is
    // not the same thing at all.
#ifdef Q_OS_WIN
    // Explorer's equivalent: "/select," then the native path as its own
    // argument (QProcess quotes it when it has spaces; Explorer accepts that).
    QProcess::startDetached(QStringLiteral("explorer.exe"), { QStringLiteral("/select,"), QDir::toNativeSeparators(filePath) });
#else
    QProcess::startDetached(QStringLiteral("/usr/bin/open"), { QStringLiteral("-R"), filePath });
#endif
}

bool MixHouseTrackListModel::addTrackToAutoDj(int row, int loc)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    MixHouseAutoDjLoc realLoc = MixHouseAutoDjLoc::Bottom;
    switch (static_cast<AutoDjLoc>(loc)) {
    case AutoDjTop:     realLoc = MixHouseAutoDjLoc::Top;     break;
    case AutoDjBottom:  realLoc = MixHouseAutoDjLoc::Bottom;  break;
    case AutoDjReplace: realLoc = MixHouseAutoDjLoc::Replace; break;
    }
    const bool ok = library()->addTrackToAutoDj(m_tracks[static_cast<size_t>(row)].id, realLoc);
    if (ok) {
        load();   // the Auto DJ sidebar row's count depends on this
    }
    return ok;
}

bool MixHouseTrackListModel::setTrackRating(int row, int rating)
{
    return setTracksRating(QVariantList { row }, rating);
}

bool MixHouseTrackListModel::setTrackComment(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->setTrackComment(m_tracks[static_cast<size_t>(row)].id, text);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::setTrackGrouping(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->setTrackGrouping(m_tracks[static_cast<size_t>(row)].id, text);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::setTrackComposer(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->setTrackComposer(m_tracks[static_cast<size_t>(row)].id, text);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::setTrackAlbumArtist(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->setTrackAlbumArtist(m_tracks[static_cast<size_t>(row)].id, text);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::setTrackNumber(int row, const QString& text)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->setTrackNumber(m_tracks[static_cast<size_t>(row)].id, text);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::scaleTrackBpm(int row, double factor)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->scaleTrackBpm(m_tracks[static_cast<size_t>(row)].id, factor);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::setTrackBpmLocked(int row, bool locked)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    const bool ok = library()->setTrackBpmLocked(m_tracks[static_cast<size_t>(row)].id, locked);
    if (ok) {
        load();   // the Adjust BPM submenu itself depends on the new lock state
    }
    return ok;
}

bool MixHouseTrackListModel::clearTrackField(int row, int field)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return false;
    }
    MixHouseClearField realField;
    switch (static_cast<ClearField>(field)) {
    case ClearBeats:       realField = MixHouseClearField::Beats;      break;
    case ClearPlayCount:   realField = MixHouseClearField::PlayCount;  break;
    case ClearRatingField: realField = MixHouseClearField::Rating;     break;
    case ClearComment:     realField = MixHouseClearField::Comment;    break;
    case ClearMainCue:     realField = MixHouseClearField::MainCue;    break;
    case ClearHotCues:     realField = MixHouseClearField::HotCues;    break;
    case ClearIntroCue:    realField = MixHouseClearField::IntroCue;   break;
    case ClearOutroCue:    realField = MixHouseClearField::OutroCue;   break;
    case ClearLoops:       realField = MixHouseClearField::Loops;      break;
    case ClearKey:         realField = MixHouseClearField::Key;        break;
    case ClearReplayGain:  realField = MixHouseClearField::ReplayGain; break;
    case ClearWaveform:    realField = MixHouseClearField::Waveform;   break;
    case ClearAll: default: realField = MixHouseClearField::All;      break;
    }
    const int trackId = m_tracks[static_cast<size_t>(row)].id;
    const bool ok = library()->clearTrackField(trackId, realField);
    if (ok && (realField == MixHouseClearField::Waveform || realField == MixHouseClearField::All)) {
        // The decoded overview is cached by track id for the process's
        // lifetime -- without this the cleared waveform kept drawing.
        m_overviewCache.remove(trackId);
    }
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::addTracksToPlaylist(const QVariantList& rows, int playlistId)
{
    const QList<int> ids = resolveOrImportTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        if (library()->addTrackToPlaylist(id, playlistId)) {
            any = true;
        }
    }
    if (any) {
        load();
    }
    return any;
}

bool MixHouseTrackListModel::addTracksToCrate(const QVariantList& rows, int crateId)
{
    const QList<int> ids = resolveOrImportTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        if (library()->addTrackToCrate(id, crateId)) {
            any = true;
        }
    }
    if (any) {
        load();
    }
    return any;
}

bool MixHouseTrackListModel::setTracksColor(const QVariantList& rows, int color)
{
    // Lexicon / rekordbox songs: stored on the same file in the Music Collection.
    const QList<int> ids = resolveOrImportTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        if (library()->setTrackColor(id, color)) {
            any = true;
        }
    }
    if (any) {
        load();
    }
    return any;
}

bool MixHouseTrackListModel::addTracksToAutoDj(const QVariantList& rows, int loc)
{
    MixHouseAutoDjLoc realLoc = MixHouseAutoDjLoc::Bottom;
    switch (static_cast<AutoDjLoc>(loc)) {
    case AutoDjTop:     realLoc = MixHouseAutoDjLoc::Top;     break;
    case AutoDjBottom:  realLoc = MixHouseAutoDjLoc::Bottom;  break;
    case AutoDjReplace: realLoc = MixHouseAutoDjLoc::Replace; break;
    }
    const QList<int> ids = resolveTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        if (library()->addTrackToAutoDj(id, realLoc)) {
            any = true;
        }
        // Replace only makes sense once -- every id after the first would
        // otherwise wipe the queue again and leave only the LAST track in
        // it. Real mixxx's own AddToAutoDJTrackPointerOperation only clears
        // once for the whole batch the same way (wtrackmenu.cpp:2818-2857).
        if (realLoc == MixHouseAutoDjLoc::Replace) {
            realLoc = MixHouseAutoDjLoc::Bottom;
        }
    }
    if (any) {
        load();
    }
    return any;
}

void MixHouseTrackListModel::addTracksToAutoDjReplaceWithConfirm(const QVariantList& rows)
{
    // Resolve ids now, before the dialog -- same rule as every other
    // confirm-dialog action here.
    const QList<int> ids = resolveTrackIds(rows);
    if (ids.isEmpty()) {
        return;
    }
    const std::string dlgTitle = muse::trc("mixhouse", "Replace Auto DJ Queue?");
    const std::string question = muse::qtrc("mixhouse",
        "This replaces everything currently in the Auto DJ queue with %1 track(s). "
        "The current queue cannot be recovered afterward.")
        .arg(ids.size()).toStdString();
    const int replaceBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(replaceBtn, muse::trc("mixhouse", "Replace"), true) },
        replaceBtn);
    promise.onResolve(this, [this, ids](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        MixHouseAutoDjLoc loc = MixHouseAutoDjLoc::Replace;
        bool any = false;
        for (int id : ids) {
            if (library()->addTrackToAutoDj(id, loc)) {
                any = true;
            }
            loc = MixHouseAutoDjLoc::Bottom;   // see addTracksToAutoDj()'s own comment
        }
        if (any) {
            load();
        }
    });
}

void MixHouseTrackListModel::applyTagFields(MixHouseTrack& t, const QVariantMap& f)
{
    auto take = [&f](const char* key, QString& field) {
        const QString k = QString::fromLatin1(key);
        if (f.contains(k)) {
            field = f.value(k).toString();
        }
    };
    take("title", t.title);
    take("artist", t.artist);
    take("album", t.album);
    take("albumArtist", t.albumArtist);
    take("genre", t.genre);
    take("year", t.year);
    take("trackNumber", t.trackNumber);
    take("composer", t.composer);
    take("grouping", t.grouping);
    take("comment", t.comment);
}

QStringList MixHouseTrackListModel::customTagNames() const
{
    return library() ? library()->customTagNames() : QStringList();
}

QString MixHouseTrackListModel::customTag(int trackId, const QString& name) const
{
    if (!library() || name.isEmpty()) {
        return {};
    }
    auto it = m_customTagCache.find(name);
    if (it == m_customTagCache.end()) {
        it = m_customTagCache.insert(name, library()->customTagValues(name));
    }
    return it->value(trackId);
}

int MixHouseTrackListModel::trackIdAt(int row) const
{
    return row >= 0 && row < static_cast<int>(m_tracks.size()) ? m_tracks[static_cast<size_t>(row)].id : -1;
}

QVariantMap MixHouseTrackListModel::tagsForRow(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) {
        return m;
    }
    const MixHouseTrack& t = m_tracks[static_cast<size_t>(row)];
    m[QStringLiteral("title")] = t.title;
    m[QStringLiteral("artist")] = t.artist;
    m[QStringLiteral("album")] = t.album;
    m[QStringLiteral("albumArtist")] = t.albumArtist;
    m[QStringLiteral("genre")] = t.genre;
    m[QStringLiteral("year")] = t.year;
    m[QStringLiteral("trackNumber")] = t.trackNumber;
    m[QStringLiteral("composer")] = t.composer;
    m[QStringLiteral("grouping")] = t.grouping;
    m[QStringLiteral("comment")] = t.comment;
    m[QStringLiteral("key")] = t.keyId > 0 ? IMixHouseKeyMatch::camelot(t.keyId) : QString();
    m[QStringLiteral("energy")] = t.energy > 0.0 ? QString::number(std::lround(t.energy)) : QString();
    m[QStringLiteral("rating")] = QString::number(t.rating);
    return m;
}

bool MixHouseTrackListModel::editTags(const QVariantList& rows, const QVariantMap& fieldsIn, bool writeFile)
{
    // GlideMix: Key (normalised, written to the file's key tag), Energy and
    // Rating (kept in the library) next to the text tags.
    QVariantMap fields = fieldsIn;
    const QVariant energy = fields.take(QStringLiteral("energy"));
    const QVariant rating = fields.take(QStringLiteral("rating"));
    if (fields.contains(QStringLiteral("key"))) {
        const QString typed = fields.value(QStringLiteral("key")).toString().trimmed();
        const int keyId = djKeyIdFromText(typed);
        if (!typed.isEmpty() && keyId == 0) {
            fields.remove(QStringLiteral("key"));   // not a key: left as it was
        } else {
            fields.insert(QStringLiteral("key"), keyId > 0 ? IMixHouseKeyMatch::traditional(keyId) : QString());
        }
    }
    if (energy.isValid() || rating.isValid()) {
        const QList<int> ids = resolveOrImportTrackIds(rows);
        for (int id : ids) {
            if (energy.isValid() && energy.toDouble() >= 1.0 && energy.toDouble() <= 10.0) {
                library()->setTrackEnergy(id, std::round(energy.toDouble()));
            }
            if (rating.isValid() && rating.toInt() >= 0 && rating.toInt() <= 5) {
                library()->setTrackRating(id, rating.toInt());
            }
        }
        if (fields.isEmpty()) {
            load();
            return true;
        }
    }
    if (fields.isEmpty()) {
        return false;
    }
    // Lexicon / rekordbox rows: the same file in the Music Collection, and the
    // edit kept to lay over their rows (their databases stay read-only).
    bool anyExternal = false;
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < static_cast<int>(m_tracks.size()) && isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
            anyExternal = true;
        }
    }
    const QList<int> ids = resolveOrImportTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        if (library()->setTrackTags(id, fields, writeFile, anyExternal)) {
            any = true;
        }
    }
    if (any) {
        load();
    }
    return any;
}

bool MixHouseTrackListModel::setTracksRating(const QVariantList& rows, int rating)
{
    // Lexicon / rekordbox songs: stored on the same file in the Music Collection.
    const QList<int> ids = resolveOrImportTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        if (library()->setTrackRating(id, rating)) {
            any = true;
        }
    }
    if (any) {
        load();
    }
    return any;
}

bool MixHouseTrackListModel::scaleTracksBpm(const QVariantList& rows, double factor)
{
    const QList<int> ids = resolveTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        // Per-track: scaleTrackBpm() itself silently skips (returns false,
        // no write) a BPM-locked track -- exactly real mixxx's own
        // per-track behavior when scaling a mixed lock-state selection.
        if (library()->scaleTrackBpm(id, factor)) {
            any = true;
        }
    }
    if (any) {
        load();
    }
    return any;
}

bool MixHouseTrackListModel::setTracksBpmLocked(const QVariantList& rows, bool locked)
{
    const QList<int> ids = resolveTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        if (library()->setTrackBpmLocked(id, locked)) {
            any = true;
        }
    }
    if (any) {
        load();
    }
    return any;
}

bool MixHouseTrackListModel::clearTracksField(const QVariantList& rows, int field)
{
    MixHouseClearField realField;
    switch (static_cast<ClearField>(field)) {
    case ClearBeats:       realField = MixHouseClearField::Beats;      break;
    case ClearPlayCount:   realField = MixHouseClearField::PlayCount;  break;
    case ClearRatingField: realField = MixHouseClearField::Rating;     break;
    case ClearComment:     realField = MixHouseClearField::Comment;    break;
    case ClearMainCue:     realField = MixHouseClearField::MainCue;    break;
    case ClearHotCues:     realField = MixHouseClearField::HotCues;    break;
    case ClearIntroCue:    realField = MixHouseClearField::IntroCue;   break;
    case ClearOutroCue:    realField = MixHouseClearField::OutroCue;   break;
    case ClearLoops:       realField = MixHouseClearField::Loops;      break;
    case ClearKey:         realField = MixHouseClearField::Key;        break;
    case ClearReplayGain:  realField = MixHouseClearField::ReplayGain; break;
    case ClearWaveform:    realField = MixHouseClearField::Waveform;   break;
    case ClearAll: default: realField = MixHouseClearField::All;      break;
    }
    const QList<int> ids = resolveTrackIds(rows);
    bool any = false;
    for (int id : ids) {
        // Per-track: clearTrackField(Beats/All) itself silently skips a
        // BPM-locked track now -- see that method's own comment.
        if (library()->clearTrackField(id, realField)) {
            any = true;
            if (realField == MixHouseClearField::Waveform || realField == MixHouseClearField::All) {
                m_overviewCache.remove(id);   // see clearTrackField(row, field)
            }
        }
    }
    if (any) {
        load();
    }
    return any;
}

bool MixHouseTrackListModel::isAnyTrackBpmLocked(const QVariantList& rows) const
{
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < static_cast<int>(m_tracks.size())
            && m_tracks[static_cast<size_t>(row)].bpmLocked) {
            return true;
        }
    }
    return false;
}

void MixHouseTrackListModel::purgeTrackWithConfirm(int row, const QString& title)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return;
    }
    const int trackId = m_tracks[static_cast<size_t>(row)].id;
    const std::string dlgTitle = muse::qtrc("mixhouse", "Purge “%1” from library?").arg(title).toStdString();
    const std::string question = muse::trc("mixhouse",
        "The track's metadata, ratings, cues and analysis will be removed from the library "
        "database. The audio file itself is not touched.");
    const int purgeBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(purgeBtn, muse::trc("mixhouse", "Purge"), true) },
        purgeBtn);
    promise.onResolve(this, [this, trackId](const muse::IInteractive::Result& res) {
        if (!res.isButton(muse::IInteractive::Button::Cancel) && library()->purgeTrack(trackId)) {
            load();
        }
    });
}

void MixHouseTrackListModel::moveTrackFileToTrashWithConfirm(int row, const QString& title)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())
        || isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        return;
    }
    const MixHouseTrack& t = m_tracks[static_cast<size_t>(row)];
    const int trackId = t.id;
    const QString filePath = t.filePath;
    // Real mixxx (Qt >= 5.15): "Move these files to the trash bin?" -- the OS
    // Trash, recoverable, not a permanent unlink.
    const std::string dlgTitle = muse::trc("mixhouse", "Move Track File to Trash?");
    const std::string question = muse::qtrc("mixhouse",
        "“%1” will be moved to the Trash and purged from the library database.")
        .arg(title).toStdString();
    const int moveBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(moveBtn, muse::trc("mixhouse", "Move to Trash"), true) },
        moveBtn);
    promise.onResolve(this, [this, trackId, filePath](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        // Purge only follows a file that was actually moved -- matches real
        // mixxx's RemoveTrackFilesFromDiskTrackPointerOperation, which only
        // queues a track for purgeTracks() once its file is confirmed gone.
        if (!filePath.isEmpty() && QFile::moveToTrash(filePath)) {
            library()->purgeTrack(trackId);
            load();
        } else {
            LOGW() << "moveTrackFileToTrashWithConfirm: QFile::moveToTrash failed for " << filePath;
        }
    });
}

std::vector<MixHouseCue> MixHouseTrackListModel::cuesFor(const MixHouseTrack& t, bool* generated) const
{
    for (const QString& source : externalSources()) {
        const IMixHouseExternalLibrary* external = externalLibrary(source);
        if (external && external->ownsTrack(t.id)) {
            std::vector<MixHouseCue> own = external->cues(t.id);
            if (!own.empty()) {
                return own;
            }
            break;
        }
    }
    if (generated) {
        *generated = true;
    }
    const MixHouseTrack withGrid = withBeatGrid(t);
    const bool grid = withGrid.beats.valid && withGrid.beats.beatLengthSec > 0.0;
    return phraseCues(phrasesForExport(t), grid ? withGrid.beats.firstBeatSec : 0.0, grid ? withGrid.beats.beatLengthSec : 0.0);
}

MixHouseTrack MixHouseTrackListModel::withBeatGrid(const MixHouseTrack& t) const
{
    if (t.beats.valid || t.beatsBlob.isEmpty() || t.sampleRate > 0 || isExternalTrack(t)) {
        return t;
    }
    IMixHouseLibrary::TrackQuery one;
    one.trackId = t.id;
    one.hiddenOnly = m_query.hiddenOnly;
    const MixHouseTrackList fresh = library()->tracks(one);
    if (fresh.empty()) {
        return t;
    }
    MixHouseTrack out = fresh.front();
    out.playlistPosition = t.playlistPosition;
    return out;
}

QVariantList MixHouseTrackListModel::overviewCues(int row)
{
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) {
        return {};
    }
    const MixHouseTrack& t = m_tracks[static_cast<size_t>(row)];
    const auto cached = m_overviewCueCache.constFind(t.id);
    if (cached != m_overviewCueCache.constEnd()) {
        return *cached;
    }
    QVariantList out;
    if (t.durationSec > 0.0) {
        for (const MixHouseCue& c : cuesFor(t)) {
            if (c.startSec < 0.0 || c.startSec > t.durationSec) {
                continue;
            }
            const bool hot = c.hotCueIndex >= 0;
            QVariantMap m;
            m["pos"] = c.startSec / t.durationSec;
            m["hot"] = hot;
            m["letter"] = hot ? QString(QChar(u'A' + std::min(c.hotCueIndex, 25))) : QString();
            m["color"] = hot && c.color.isValid() ? c.color.name() : QStringLiteral("#e8303a");
            out.push_back(m);
        }
    }
    m_overviewCueCache.insert(t.id, out);
    return out;
}

std::vector<MixHousePhrase> MixHouseTrackListModel::phrasesForExport(const MixHouseTrack& track) const
{
    // The phrase ribbon's own order: edited in the Beatgrid editor, then
    // rekordbox's, then GlideMix's analysis.
    const int libraryId = isExternalTrack(track) ? libraryIdForFile(track.filePath) : track.id;
    if (libraryId >= 0) {
        if (auto edited = library()->trackPhraseEdits(libraryId)) {
            return *edited;
        }
    }
    if (rekordboxAnalysis() && !track.filePath.isEmpty()) {
        std::vector<MixHousePhrase> phrases = rekordboxAnalysis()->phrases(track.filePath, track.bpm);
        if (!phrases.empty()) {
            return phrases;
        }
    }
    return libraryId >= 0 ? library()->detectedPhrases(libraryId) : std::vector<MixHousePhrase>();
}

int MixHouseTrackListModel::libraryIdForFile(const QString& filePath) const
{
    if (filePath.isEmpty() || !library()) {
        return -1;
    }
    IMixHouseLibrary::TrackQuery query;
    query.location = filePath;
    query.limit = 1;
    const MixHouseTrackList found = library()->tracks(query);
    return found.empty() ? -1 : found.front().id;
}

void MixHouseTrackListModel::collectAnalysisTargets(const QVariantList& rows, QList<int>& ids,
                                                    QStringList& externalFiles, bool allMatching) const
{
    // GlideMix: Select All means every song in the view, not the 2000 rows
    // loaded so far (Chris, 36,674 songs: "it takes only 2000").
    if (allMatching) {
        if (m_externalSource.isEmpty()) {
            ids = library()->matchingTracks(m_query).ids;
            return;
        }
        if (const IMixHouseExternalLibrary* external = externalLibrary(m_externalSource)) {
            IMixHouseLibrary::TrackQuery all = m_query;
            all.limit = 0;
            for (const MixHouseTrack& t : external->tracks(m_externalPlaylist, all)) {
                if (t.id > 0) {
                    ids << t.id;
                } else if (!t.filePath.isEmpty()) {
                    externalFiles << t.filePath;
                }
            }
            return;
        }
    }
    ids = resolveTrackIds(rows);
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < static_cast<int>(m_tracks.size()) && isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
            externalFiles << m_tracks[static_cast<size_t>(row)].filePath;
        }
    }
}

QList<int> MixHouseTrackListModel::analysisTrackIds(QList<int> ids, const QStringList& externalFiles)
{
    QStringList toAdd;
    for (const QString& file : externalFiles) {
        if (libraryIdForFile(file) < 0) {
            toAdd << file;
        }
    }
    if (!toAdd.isEmpty()) {
        library()->addTracksFromFiles(toAdd);
    }
    for (const QString& file : externalFiles) {
        const int id = libraryIdForFile(file);
        if (id >= 0 && !ids.contains(id)) {
            ids << id;
        }
    }
    return ids;
}

QList<int> MixHouseTrackListModel::resolveTrackIds(const QVariantList& rows) const
{
    QList<int> ids;
    ids.reserve(rows.size());
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        // Lexicon tracks (negative ids) aren't in this library: no writes.
        if (row >= 0 && row < static_cast<int>(m_tracks.size()) && !isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
            ids.append(m_tracks[static_cast<size_t>(row)].id);
        }
    }
    return ids;
}

/// Resolves to the real PlaylistTracks.position of each row -- see
/// removeTracksFromPlaylist()'s own comment for why position, not track id,
/// is the only way to remove ONE occurrence of a duplicated track. Only
/// meaningful when currently viewing a playlist (m_tracks[row].
/// playlistPosition is -1 otherwise, and silently skipped).
QList<int> MixHouseTrackListModel::resolvePlaylistPositions(const QVariantList& rows) const
{
    QList<int> positions;
    positions.reserve(rows.size());
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < static_cast<int>(m_tracks.size())) {
            const int pos = m_tracks[static_cast<size_t>(row)].playlistPosition;
            if (pos >= 0) {
                positions.append(pos);
            }
        }
    }
    return positions;
}

bool MixHouseTrackListModel::removeTracksFromCurrentCollection(const QVariantList& rows)
{
    if (m_query.crateId >= 0) {
        // crate_tracks has no position/duplicates -- track id is enough.
        const QList<int> trackIds = resolveTrackIds(rows);
        if (trackIds.isEmpty()) {
            return false;
        }
        const bool ok = library()->removeTracksFromCrate(m_query.crateId, trackIds);
        if (ok) {
            load();
        }
        return ok;
    }
    if (m_query.playlistId < 0) {
        return false;   // not viewing a playlist/crate -- nothing to remove from
    }
    const QList<int> positions = resolvePlaylistPositions(rows);
    if (positions.isEmpty()) {
        return false;
    }
    const bool ok = library()->removeTracksFromPlaylist(m_query.playlistId, positions);
    if (ok) {
        load();
    }
    return ok;
}

bool MixHouseTrackListModel::removeAllMatchingFromCurrentCollection()
{
    // No page cap here (unlike resolveTrackIds/resolvePlaylistPositions,
    // which only ever see the loaded m_tracks) -- matchingTracks() re-runs
    // the current view's own filter/join against the database directly.
    const IMixHouseLibrary::MatchingTracks matching = library()->matchingTracks(m_query);
    if (m_query.crateId >= 0) {
        if (matching.ids.isEmpty()) {
            return false;
        }
        const bool ok = library()->removeTracksFromCrate(m_query.crateId, matching.ids);
        if (ok) {
            load();
        }
        return ok;
    }
    if (m_query.playlistId < 0) {
        return false;
    }
    if (matching.playlistPositions.isEmpty()) {
        return false;
    }
    const bool ok = library()->removeTracksFromPlaylist(m_query.playlistId, matching.playlistPositions);
    if (ok) {
        load();
    }
    return ok;
}

void MixHouseTrackListModel::removeAllMatchingFromLibraryWithConfirm()
{
    // See this method's own header comment for the real bug this exists to
    // fix. Resolved directly from the database via the current query, NOT
    // from m_tracks/rows -- so this is correct regardless of how many
    // matches there are, whether that's 3 or 33,688.
    const QList<int> trackIds = library()->matchingTracks(m_query).ids;
    if (trackIds.isEmpty()) {
        return;
    }
    const int count = static_cast<int>(trackIds.size());

    const std::string dlgTitle = muse::trc("mixhouse", "Remove from Library?");
    const std::string question = muse::qtrc("mixhouse",
        "%1 track(s) will be removed from the library for good (playlists, crates, cues and "
        "analysis data go with them). The audio files themselves are not touched.")
        .arg(count).toStdString();
    const int removeBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(removeBtn, muse::trc("mixhouse", "Remove"), true) },
        removeBtn);
    promise.onResolve(this, [this, trackIds](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        library()->purgeTracks(trackIds);
        load();
    });
}

void MixHouseTrackListModel::removeTracksFromLibraryWithConfirm(const QVariantList& rows)
{
    // Resolve ids now, before the dialog -- a reload while it's open must not
    // renumber rows out from under this list (same rule as every other
    // confirm-dialog action here). This purges for real (see the header doc
    // comment for why it no longer soft-hides) so, unlike the old Hide it
    // replaces, it needs a real confirmation.
    const QList<int> trackIds = resolveTrackIds(rows);
    if (trackIds.isEmpty()) {
        return;
    }
    const int count = static_cast<int>(trackIds.size());

    const std::string dlgTitle = muse::trc("mixhouse", "Remove from Library?");
    const std::string question = muse::qtrc("mixhouse",
        "%1 track(s) will be removed from the library for good (playlists, crates, cues and "
        "analysis data go with them). The audio files themselves are not touched.")
        .arg(count).toStdString();
    const int removeBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(removeBtn, muse::trc("mixhouse", "Remove"), true) },
        removeBtn);
    promise.onResolve(this, [this, trackIds](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        library()->purgeTracks(trackIds);
        load();
    });
}

void MixHouseTrackListModel::removeFromCollectionAndLibraryWithConfirm(const QVariantList& rows)
{
    // Resolve ids AND snapshot which playlist this was about now, before any
    // dialog -- a reload while the dialog is open must not change which
    // collection this action was asked to remove from.
    const QList<int> trackIds = resolveTrackIds(rows);
    if (trackIds.isEmpty()) {
        return;
    }
    const int playlistId = m_query.playlistId;
    const int count = static_cast<int>(trackIds.size());

    const std::string dlgTitle = muse::trc("mixhouse", "Remove from Collection and Library?");
    const std::string question = playlistId >= 0
        ? muse::qtrc("mixhouse",
              "%1 track(s) will be removed from this playlist and removed from the library for "
              "good. The audio files themselves are not touched.").arg(count).toStdString()
        : muse::qtrc("mixhouse",
              "%1 track(s) will be removed from the library for good. The audio files themselves "
              "are not touched.").arg(count).toStdString();
    const int removeBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(removeBtn, muse::trc("mixhouse", "Remove"), true) },
        removeBtn);
    promise.onResolve(this, [this, trackIds, playlistId](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        if (playlistId >= 0) {
            library()->removeTracksFromPlaylist(playlistId, trackIds);
        }
        library()->purgeTracks(trackIds);
        load();
    });
}

void MixHouseTrackListModel::moveTracksToTrashWithConfirm(const QVariantList& rows)
{
    // Snapshot id+filePath pairs now, before the dialog -- a reload while
    // it's open must not renumber rows out from under this list (same rule
    // as moveTrackFileToTrashWithConfirm's single-track version above).
    QList<int> trackIds;
    QStringList filePaths;
    for (const QVariant& v : rows) {
        const int row = v.toInt();
        if (row >= 0 && row < static_cast<int>(m_tracks.size())) {
            const MixHouseTrack& t = m_tracks[static_cast<size_t>(row)];
            trackIds.append(t.id);
            filePaths.append(t.filePath);
        }
    }
    if (trackIds.isEmpty()) {
        return;
    }
    const int count = static_cast<int>(trackIds.size());

    // Real mixxx (Qt >= 5.15): "Move these files to the trash bin?" -- the
    // OS Trash, recoverable, not a permanent unlink.
    const std::string dlgTitle = muse::trc("mixhouse", "Delete track files from disk?");
    const std::string question = muse::qtrc("mixhouse",
        "%1 track file(s) will be moved to the Trash and purged from the library database.", nullptr, count)
        .arg(count).toStdString();
    const int moveBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(moveBtn, muse::trc("mixhouse", "Delete from Disk"), true) },
        moveBtn);
    promise.onResolve(this, [this, trackIds, filePaths](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        // Purge only follows files actually moved -- matches real mixxx's
        // RemoveTrackFilesFromDiskTrackPointerOperation, which only queues a
        // track for purgeTracks() once its file is confirmed gone.
        QList<int> purgeIds;
        for (int i = 0; i < trackIds.size(); ++i) {
            if (!filePaths[i].isEmpty() && QFile::moveToTrash(filePaths[i])) {
                purgeIds.append(trackIds[i]);
            } else {
                LOGW() << "moveTracksToTrashWithConfirm: QFile::moveToTrash failed for " << filePaths[i];
            }
        }
        if (!purgeIds.isEmpty()) {
            library()->purgeTracks(purgeIds);
            load();
        }
    });
}

void MixHouseTrackListModel::analyzeTrackWithConfirm(int row, const QString& title)
{
    if (row >= 0 && row < static_cast<int>(m_tracks.size()) && isExternalTrack(m_tracks[static_cast<size_t>(row)])) {
        analyzeTracksWithConfirm({ row });
        return;
    }
    if (row < 0 || row >= static_cast<int>(m_tracks.size())) {
        return;
    }
    const int trackId = m_tracks[static_cast<size_t>(row)].id;

    const std::string dlgTitle = muse::qtrc("mixhouse", "Analyze “%1”?").arg(title).toStdString();
    const std::string question = muse::trc("mixhouse",
        "Detects this track's BPM and beatgrid (Queen Mary DSP), "
        "musical key (OpenKeyScan) and waveform from its audio. Existing values will be "
        "overwritten.");
    const int analyzeBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(analyzeBtn, muse::trc("mixhouse", "Analyze"), true) },
        analyzeBtn);
    promise.onResolve(this, [this, trackId](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        // Overview cache keyed by id would otherwise still show a
        // now-stale-key/bpm-inconsistent row until a real reload -- clear it
        // for this track so a later Overview repaint re-reads whatever the
        // next analysis actually produces (bpm/key here, not the waveform
        // itself, but keeping this one cache invalidation rule in one place
        // avoids a second, easy-to-forget copy of the same "clear on
        // reanalyze" logic once that's wired too).
        m_overviewCache.remove(trackId);

        const MixHouseAnalyzeResult result = library()->analyzeTrack(trackId);
        if (!result.valid) {
            interactive()->error(muse::trc("mixhouse", "Analysis failed"), result.error.toStdString());
            return;
        }
        load();
        const QString keyText = result.key.isEmpty() ? QStringLiteral("—") : result.key;
        std::string text;
        if (result.bpmSkippedLocked) {
            // Real mixxx's analyzer refuses to touch bpm/beats on a locked
            // track at all -- key is still real/updated, bpm/beatgrid is
            // exactly what it was before.
            text = muse::qtrc("mixhouse", "BPM is locked, left unchanged. Key: %1")
                       .arg(keyText).toStdString();
        } else if (result.bpm > 0.0) {
            text = muse::qtrc("mixhouse", "BPM: %1, Key: %2")
                       .arg(QString::number(result.bpm, 'f', 1), keyText).toStdString();
        } else {
            text = muse::qtrc("mixhouse", "No beat detected. Key: %1").arg(keyText).toStdString();
        }
        interactive()->info(muse::trc("mixhouse", "Analysis finished"), text);
    });
}

void MixHouseTrackListModel::analyzeTracksWithConfirm(const QVariantList& rows, bool allMatching)
{
    // Resolve ids now, before the dialog -- same rule as every other
    // confirm-dialog batch action in this file.
    // Another DJ app's tracks are analyzed as Music Collection tracks (their
    // files get added there).
    QList<int> trackIds;
    QStringList externalFiles;
    collectAnalysisTargets(rows, trackIds, externalFiles, allMatching);
    if (trackIds.isEmpty() && externalFiles.isEmpty()) {
        return;
    }
    const int count = static_cast<int>(trackIds.size() + externalFiles.size());

    const std::string dlgTitle = muse::qtrc("mixhouse", "Analyze %1 track(s)?", nullptr, count).arg(count).toStdString();
    const std::string question = muse::trc("mixhouse",
        "Detects each track's BPM and beatgrid (Queen Mary DSP), "
        "musical key (OpenKeyScan) and waveform from its audio. Existing values will be "
        "overwritten. Analysis runs in the background, so you can keep working -- each "
        "track's progress shows in the library list.");
    const int analyzeBtn = int(muse::IInteractive::Button::Ok);
    auto promise = interactive()->question(dlgTitle, question,
        { interactive()->buttonData(muse::IInteractive::Button::Cancel),
          muse::IInteractive::ButtonData(analyzeBtn, muse::trc("mixhouse", "Analyze"), true) },
        analyzeBtn);
    promise.onResolve(this, [this, trackIds, externalFiles](const muse::IInteractive::Result& res) {
        if (res.isButton(muse::IInteractive::Button::Cancel)) {
            return;
        }
        startBatchAnalysisForIds(analysisTrackIds(trackIds, externalFiles));
    });
}

QVariantList MixHouseTrackListModel::rowsForAnalysisScope(bool newOnly) const
{
    QVariantList rows;
    // Real mixxx: analysislibrarytablemodel.cpp's own recentFilter() --
    // datetime_added > datetime('now', '-7 days'), ported exactly (a real
    // 7-day window, not an arbitrary round number).
    const QDateTime cutoff = QDateTime::currentDateTimeUtc().addDays(-7);
    for (int i = 0; i < static_cast<int>(m_tracks.size()); ++i) {
        if (newOnly) {
            const QDateTime added = QDateTime::fromString(
                m_tracks[static_cast<size_t>(i)].dateAdded, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
            if (!added.isValid() || added < cutoff) {
                continue;
            }
        }
        rows.append(i);
    }
    return rows;
}

bool MixHouseTrackListModel::analyzing() const { return library()->isAnalyzing(); }
int MixHouseTrackListModel::analyzeProcessed() const { return m_analyzeProcessed; }
int MixHouseTrackListModel::analyzeTotal() const { return m_analyzeTotal; }

void MixHouseTrackListModel::startBatchAnalysis(const QVariantList& rows, bool allMatching)
{
    QList<int> ids;
    QStringList externalFiles;
    collectAnalysisTargets(rows, ids, externalFiles, allMatching);
    startBatchAnalysisForIds(analysisTrackIds(ids, externalFiles));
}

void MixHouseTrackListModel::startBatchAnalysisForIds(const QList<int>& requestedIds)
{
    if (analyzing() || requestedIds.isEmpty()) {
        return;   // already running -- the QML button is Stop while this is true, but guard anyway
    }
    // GlideMix: songs never analysed go first, then the ones being redone --
    // with the list newest-first, Select All re-analysed 5,532 done songs
    // before the first new one and the Analyze count stood still (Chris).
    QList<int> trackIds;
    trackIds.reserve(requestedIds.size());
    QList<int> redo;
    for (int id : requestedIds) {
        const auto it = m_index.find(id);
        if (it != m_index.end() && it->second.analyzed) {
            redo << id;
        } else {
            trackIds << id;
        }
    }
    trackIds += redo;
    for (int trackId : trackIds) {
        m_overviewCache.remove(trackId);   // see analyzeTrackWithConfirm's own comment
    }
    library()->startBackgroundAnalysis(trackIds);
    if (analyzing()) {
        m_analysisProgressByTrack.clear();
        for (int trackId : trackIds) {
            m_analysisProgressByTrack.insert(trackId, 0);
            emitAnalysisProgressChanged(trackId);
        }
    }
    emit analyzingChanged();   // library()->isAnalyzing() already flips true synchronously inside the call above
}

void MixHouseTrackListModel::emitAnalysisProgressChanged(int trackId, bool allRoles)
{
    // Linear scan over the loaded page (<= 2000 rows, see load()) instead of
    // a trackId->row index: throttled progress is at most ~17 updates/s per
    // worker, and a scan can never go stale across load()'s model resets.
    for (size_t row = 0; row < m_tracks.size(); ++row) {
        if (m_tracks[row].id == trackId) {
            const int r = static_cast<int>(row);
            const QModelIndex first = index(r, 0);
            const QModelIndex last = index(r, std::max(0, m_uiColumnCount - 1));
            if (allRoles) {
                emit dataChanged(first, last);   // finished: BPM/key/grid changed too
            } else {
                emit dataChanged(first, last, { AnalysisProgressRole, AnalyzedRole });
            }
            return;
        }
    }
}

void MixHouseTrackListModel::stopBatchAnalysis()
{
    library()->stopBackgroundAnalysis();
    // Deliberately no load()/analyzingChanged() here -- any track(s)
    // already in flight on a worker keep running (no mid-decode
    // cancellation hook, see stopBackgroundAnalysis's own comment) until
    // analysisFinished fires from load()'s own subscription, which is what
    // actually flips analyzing back to false and refreshes the view.
}

void MixHouseTrackListModel::addFolderToLibrary(const QString& folder)
{
    if (folder.isEmpty()) {
        return;   // the native picker was cancelled -- not an error
    }
    // Real bug this now avoids, confirmed against the reference library: this
    // used to call library()->addTracksFromFolder() directly, a blocking
    // call on this (the UI) thread -- fine for a small folder, but the same
    // freeze risk as the startup rescan for a large one. See
    // IMixHouseLibrary::startBackgroundScan's own comment. The scan-
    // finished summary dialog/error is now handled once, generically, by
    // load()'s own scanFinished subscription -- shared with the startup
    // rescan below instead of duplicated per call site.
    if (mixHouseFolderTooBroadToScan(folder)) {
        interactive()->warning(muse::trc("mixhouse", "Choose your music folder"),
                               muse::qtrc("mixhouse", "%1 is much more than a music folder -- scanning it would go through "
                                                      "everything on it. Choose the folder your music is in.")
                               .arg(QDir::cleanPath(folder)).toStdString());
        return;
    }
    library()->startBackgroundScan({ folder });
    emit scanningChanged();
}

//! The same file types the library's scanner takes.
static const QStringList& droppedAudioSuffixes()
{
    static const QStringList kAudio = { QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"),
                                        QStringLiteral("flac"), QStringLiteral("ogg"), QStringLiteral("opus"),
                                        QStringLiteral("wav"), QStringLiteral("aiff"), QStringLiteral("aif"),
                                        QStringLiteral("wv") };
    return kAudio;
}

static bool isDroppedAudio(const QString& path)
{
    return droppedAudioSuffixes().contains(QFileInfo(path).suffix().toLower());
}

//! Whether `dir` holds any audio file, at any depth.
static bool folderHasAudio(const QString& dir)
{
    QDirIterator it(dir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (isDroppedAudio(it.next())) {
            return true;
        }
    }
    return false;
}

void MixHouseTrackListModel::addDroppedUrls(const QVariantList& urls, int playlistId, int crateId)
{
    const auto isAudio = isDroppedAudio;
    if (playlistId == kDropCurrentList || crateId == kDropCurrentList) {
        const bool ownList = m_externalSource.isEmpty() && !m_emptyPlaceholder;
        playlistId = ownList ? m_query.playlistId : -1;
        crateId = ownList ? m_query.crateId : -1;
    }

    QStringList folders, files;
    for (const QVariant& v : urls) {
        const QString path = v.toUrl().toLocalFile();
        if (path.isEmpty()) {
            continue;
        }
        if (QFileInfo(path).isDir()) {
            if (mixHouseFolderTooBroadToScan(path)) {
                interactive()->warning(muse::trc("mixhouse", "Choose your music folder"),
                                       muse::qtrc("mixhouse", "%1 is much more than a music folder -- scanning it would "
                                                              "go through everything on it. Choose the folder your music is in.")
                                       .arg(QDir::cleanPath(path)).toStdString());
                return;
            }
            folders << path;
        } else if (isAudio(path)) {
            files << path;
        }
    }
    if (folders.isEmpty() && files.isEmpty()) {
        return;
    }

    const bool intoList = playlistId >= 0 || crateId >= 0;
    if (!intoList && !folders.isEmpty()) {
        // Into the library only: folders the way Add Folder does (in the
        // background, with its summary), loose files straight away.
        if (!files.isEmpty()) {
            library()->addTracksFromFiles(files);
        }
        library()->startBackgroundScan(folders);
        emit scanningChanged();
        load();
        return;
    }

    // Into a list (or loose files only): every file is added now, so it can
    // be listed, in the order dropped and then by folder.
    for (const QString& folder : folders) {
        QStringList inFolder;
        QDirIterator it(folder, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            if (isAudio(path)) {
                inFolder << path;
            }
        }
        inFolder.sort(Qt::CaseInsensitive);
        files << inFolder;
    }
    const IMixHouseLibrary::ScanResult result = library()->addTracksFromFiles(files);
    if (!result.error.isEmpty()) {
        interactive()->error(muse::trc("mixhouse", "Couldn't add files to library"), result.error.toStdString());
        return;
    }
    int listed = 0;
    if (intoList) {
        for (const QString& path : files) {
            IMixHouseLibrary::TrackQuery query;
            query.location = path;
            query.limit = 1;
            const MixHouseTrackList found = library()->tracks(query);
            if (found.empty()) {
                continue;
            }
            const bool ok = playlistId >= 0 ? library()->addTrackToPlaylist(found.front().id, playlistId)
                                            : library()->addTrackToCrate(found.front().id, crateId);
            listed += ok ? 1 : 0;
        }
    }
    load();
    if (m_dropTotals) {   // part of a dropped folder tree: one summary at the end
        m_dropTotals->scanned += result.scanned;
        m_dropTotals->added += result.added;
        m_dropTotals->skipped += result.skipped;
        m_dropTotals->unreadable += result.unreadable;
        m_dropTotals->listed += listed;
        return;
    }
    if (libraryConfiguration()->showScanSummaryDialog()) {
        std::string text = muse::qtrc("mixhouse", "Scanned %1 file(s): %2 added to the library, %3 already known.", nullptr, result.scanned)
                           .arg(result.scanned).arg(result.added).arg(result.skipped).toStdString();
        if (result.unreadable > 0) {
            text += "\n" + muse::qtrc("mixhouse", "%n file(s) could not be read as audio (damaged or not audio) and were not added.",
                                        nullptr, result.unreadable).toStdString();
        }
        if (intoList) {
            text += "\n" + muse::qtrc("mixhouse", "%n track(s) added to the list.", nullptr, listed).toStdString();
        }
        interactive()->info(muse::trc("mixhouse", "Library scan finished"), text);
    }
}

void MixHouseTrackListModel::addDroppedUrlsAsNewLists(const QVariantList& urls, bool crates, int parentId)
{
    // A name already taken gets a number, as Finder numbers copies.
    const auto create = [&](const QString& baseName, int parent, bool folder) {
        int id = -1;
        for (int n = 1; id < 0 && n < 100; ++n) {
            const QString name = n == 1 ? baseName : QStringLiteral("%1 %2").arg(baseName).arg(n);
            id = crates ? library()->createCrate(name)
                 : folder ? library()->createPlaylistFolder(name, parent) : library()->createPlaylist(name, parent);
        }
        return id;
    };
    m_dropTotals = DropTotals {};
    const auto addList = [&](const QString& name, int parent, const QVariantList& items) {
        const int id = create(name, parent, false);
        if (id >= 0) {
            ++m_dropTotals->lists;
            addDroppedUrls(items, crates ? -1 : id, crates ? id : -1);
        }
    };
    // GlideMix: a Finder folder with music in its subfolders becomes the same
    // tree here: a playlist folder for it, a playlist for each folder that
    // holds tracks (its own loose tracks in a playlist of its name). Crates
    // do not nest: one crate per folder, with everything under it.
    std::function<void(const QString&, int, int)> addFolder = [&](const QString& dir, int parent, int depth) {
        const QString name = QDir(dir).dirName();
        QStringList subfolders;
        if (!crates && depth < 12) {
            for (const QFileInfo& sub : QDir(dir).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase)) {
                if (folderHasAudio(sub.absoluteFilePath())) {
                    subfolders << sub.absoluteFilePath();
                }
            }
        }
        if (subfolders.isEmpty()) {
            addList(name, parent, { QUrl::fromLocalFile(dir) });
            return;
        }
        const int folderId = create(name, parent, true);
        if (folderId < 0) {
            return;
        }
        QVariantList loose;
        for (const QFileInfo& f : QDir(dir).entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase)) {
            if (isDroppedAudio(f.absoluteFilePath())) {
                loose << QUrl::fromLocalFile(f.absoluteFilePath());
            }
        }
        if (!loose.isEmpty()) {
            addList(name, folderId, loose);
        }
        for (const QString& sub : subfolders) {
            addFolder(sub, folderId, depth + 1);
        }
    };

    // Loose files are grouped by the folder they are in.
    std::vector<std::pair<QString, QVariantList> > lists;
    const auto listFor = [&](const QString& name) -> QVariantList& {
        for (auto& [n, items] : lists) {
            if (n == name) {
                return items;
            }
        }
        lists.push_back({ name, {} });
        return lists.back().second;
    };
    for (const QVariant& v : urls) {
        const QString path = v.toUrl().toLocalFile();
        if (path.isEmpty()) {
            continue;
        }
        // Finder's folder URLs end in "/", which leaves fileName() empty.
        const QFileInfo info(QDir::cleanPath(path));
        if (info.isDir()) {
            if (mixHouseFolderTooBroadToScan(info.absoluteFilePath())) {
                interactive()->warning(muse::trc("mixhouse", "Choose your music folder"),
                                       muse::qtrc("mixhouse", "%1 is much more than a music folder -- scanning it would "
                                                              "go through everything on it. Choose the folder your music is in.")
                                       .arg(info.absoluteFilePath()).toStdString());
                m_dropTotals.reset();
                return;
            }
            if (folderHasAudio(info.absoluteFilePath())) {
                addFolder(info.absoluteFilePath(), parentId, 0);
            }
        } else if (isDroppedAudio(path)) {
            listFor(info.absoluteDir().dirName()) << v;
        }
    }
    for (const auto& [name, items] : lists) {
        addList(name, parentId, items);
    }
    const DropTotals totals = *m_dropTotals;
    m_dropTotals.reset();
    if (totals.lists > 0 && libraryConfiguration()->showScanSummaryDialog()) {
        std::string text = muse::qtrc("mixhouse", "Scanned %1 file(s): %2 added to the library, %3 already known.", nullptr, totals.scanned)
                           .arg(totals.scanned).arg(totals.added).arg(totals.skipped).toStdString();
        if (totals.unreadable > 0) {
            text += "\n" + muse::qtrc("mixhouse", "%n file(s) could not be read as audio (damaged or not audio) and were not added.",
                                        nullptr, totals.unreadable).toStdString();
        }
        text += "\n" + muse::qtrc("mixhouse", "%n list(s) made, with %1 songs.", nullptr, totals.lists)
                .arg(totals.listed).toStdString();
        interactive()->info(muse::trc("mixhouse", "Library scan finished"), text);
    }
}

void MixHouseTrackListModel::importFromRekordboxXml(const QString& path)
{
    if (path.isEmpty()) {
        return;   // the native picker was cancelled -- not an error
    }
    // A real rekordbox collection XML can be tens of thousands of tracks
    // (a real one: 70k+) -- parsing that much XML plus the
    // analysis-enrichment pass's own per-track lookups is real, sometimes-
    // slow work, and library()->importFromRekordboxXml() runs it as one
    // single blocking call with no way to report progress mid-call. the brief
    // caught this directly: he ran a real import and had no visible sign
    // anything was happening until the finished dialog appeared. This
    // can't show a LIVE percentage without either threading the import
    // (this app's own established rule: no SQL off the main thread, see
    // MixHouseLibraryPanel.qml's cover-art comment for exactly why) or
    // pumping the event loop mid-call (a real reentrancy risk: an event
    // pumped mid-import could trigger another action reaching the same
    // SQLite connection). What this DOES give, honestly: a real, visible
    // "something is happening" dialog for the whole duration, using the
    // same muse::IInteractive::showProgress() this app's own Home-screen
    // audio import already shows for its own long operations -- not
    // invented for this.
    muse::Progress progress;
    interactive()->showProgress(muse::trc("mixhouse", "Importing from Rekordbox…"), progress);
    progress.start();

    const MixHouseImportResult result = library()->importFromRekordboxXml(path);

    progress.finish(muse::ProgressResult::make_ok({}));

    if (!result.error.isEmpty()) {
        interactive()->error(muse::trc("mixhouse", "Couldn't import Rekordbox XML"), result.error.toStdString());
        return;
    }
    load();   // real mixxx's own scan-finished handling refreshes the view too
    const std::string title = muse::trc("mixhouse", "Rekordbox import finished");
    const std::string text = muse::qtrc("mixhouse",
        "%1 playlist(s) created, %2 track(s) matched, %3 got real bpm/beatgrid/key from rekordbox's own analysis.")
        .arg(result.playlistsCreated).arg(result.tracksMatched + result.tracksAdded).arg(result.analysisImported)
        .toStdString();
    interactive()->info(title, text);
}

QStringList MixHouseTrackListModel::musicDirectories() const
{
    return libraryConfiguration()->musicDirectories();
}

void MixHouseTrackListModel::addMusicDirectory(const QString& path)
{
    if (path.isEmpty()) {
        return;   // the native picker was cancelled -- not an error
    }
    // Real mixxx: clicking "Add" in the Music Directories list immediately
    // scans the new folder, it doesn't just remember it for later -- same
    // real INSERT-only scan (addTracksFromFolder) the File menu's own "Add
    // Folder to Library" and this same class's addFolderToLibrary() above
    // already use.
    libraryConfiguration()->addMusicDirectory(path);
    addFolderToLibrary(path);
}

void MixHouseTrackListModel::removeMusicDirectory(const QString& path)
{
    // Real mixxx: removing a watched directory only stops rescanning it --
    // tracks already added to the library from it are untouched.
    libraryConfiguration()->removeMusicDirectory(path);
}

void MixHouseTrackListModel::runStartupRescanIfNeeded()
{
    if (m_ranStartupRescan || !libraryConfiguration()->rescanOnStartup()) {
        return;
    }
    m_ranStartupRescan = true;
    const QStringList dirs = libraryConfiguration()->musicDirectories();
    if (dirs.isEmpty()) {
        return;
    }
    // Real bug this fixes, confirmed against a real ~35k-track
    // library: this used to call library()->addTracksFromFolder() once per
    // directory, blocking on THIS (the UI) thread for the whole walk --
    // right after a big rekordbox import added thousands of files at once,
    // that ran long enough for macOS's own "Application Not Responding"
    // watchdog to fire. See IMixHouseLibrary::startBackgroundScan's own
    // comment for the real, threaded fix (a dedicated worker thread with
    // its own DB connection, matching real mixxx's own LibraryScanner) and
    // load()'s own scanFinished subscription for where the summary dialog/
    // error now gets shown, generically, for every background scan.
    library()->startBackgroundScan(dirs);
    emit scanningChanged();
}

QVariantList MixHouseTrackListModel::overviewBuckets(int row, int bucketCount)
{
    QVariantList out;
    if (row < 0 || row >= static_cast<int>(m_tracks.size()) || bucketCount <= 0) {
        return out;
    }
    const MixHouseTrack& track = m_tracks[static_cast<size_t>(row)];
    const int trackId = track.id;

    auto it = m_overviewCache.find(trackId);
    if (it == m_overviewCache.end()) {
        MixHouseWaveformOverview overview;
        // rekordbox's own waveform first, for every track it has analysed:
        // the goal is to look like rekordbox, and rekordbox already stored
        // each column's finished colour and height. Recomputing bands from our
        // own analysis can only approximate that. Our analysis stays as the
        // fallback for files rekordbox has never seen (and while its index is
        // still loading -- overviewDataChanged() re-fetches once it is ready).
        if (rekordboxAnalysis() && !track.filePath.isEmpty()) {
            overview = rekordboxAnalysis()->waveformOverview(track.filePath, track.bpm);
        }
        if (!overview.valid) {
            // Another DJ app's track: the Music Collection's analysis of the
            // same file.
            const int libraryId = isExternalTrack(track) ? libraryIdForFile(track.filePath) : trackId;
            if (libraryId >= 0) {
                overview = decodeWaveformOverview(library()->overviewAnalysisBlob(libraryId));
            }
        }
        it = m_overviewCache.insert(trackId, overview);
    }
    if (!it->valid) {
        return out;   // not analyzed (yet) -- an empty cell, not a fabricated flat line
    }

    const std::vector<MixHouseOverviewBucket> buckets = resampleOverview(*it, bucketCount);
    out.reserve(static_cast<int>(buckets.size()));
    for (const MixHouseOverviewBucket& b : buckets) {
        QVariantMap m;
        m["low"] = b.low;
        m["mid"] = b.mid;
        m["high"] = b.high;
        m["all"] = b.all;
        m["hasColor"] = b.hasColor;
        m["color"] = static_cast<uint>(b.color);   // 0xRRGGBB
        out.push_back(m);
    }
    return out;
}

int MixHouseTrackListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_tracks.size());
}

int MixHouseTrackListModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_uiColumnCount;
}

void MixHouseTrackListModel::setUiColumnCount(int n)
{
    if (m_uiColumnCount == n) {
        return;
    }
    // GlideMix: a column added later (a tag column) is inserted, not a reset:
    // a reset makes the table fill every row down to where it is scrolled.
    if (n > m_uiColumnCount && m_uiColumnCount > 1) {
        beginInsertColumns(QModelIndex(), m_uiColumnCount, n - 1);
        m_uiColumnCount = n;
        endInsertColumns();
    } else {
        beginResetModel();
        m_uiColumnCount = n;
        endResetModel();
    }
    emit uiColumnCountChanged();
}

QVariant MixHouseTrackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_tracks.size())) {
        return QVariant();
    }
    const MixHouseTrack& t = m_tracks[static_cast<size_t>(index.row())];
    switch (role) {
    case TitleRole:       return t.title;
    case ArtistRole:      return t.artist;
    case AlbumRole:       return t.album;
    case GenreRole:       return t.genre;
    case YearRole:        return t.year;
    case BpmRole:         return t.bpm;
    case EnergyRole:      return t.energy;
    case KeyRole:         return t.key;
    case KeyIdRole:       return t.keyId;
    case DurationRole:    return t.durationSec;
    case RatingRole:      return t.rating;
    case HasBeatgridRole: return t.hasBeatgrid();
    case FilePathRole:    return t.filePath;
    case ColorRole:       return t.color;
    case FirstBeatSecRole:  return t.beats.firstBeatSec;
    case BeatLengthSecRole: return t.beats.beatLengthSec;
    case ConstantTempoRole: return t.beats.constantTempo;
    case AlbumArtistRole: return t.albumArtist;
    case ComposerRole:    return t.composer;
    case GroupingRole:    return t.grouping;
    case TrackNumberRole: return t.trackNumber;
    case InProjectRole:   return m_projectFiles.contains(t.filePath);
    case KeyMatchRole:
        // The same rules as Related (the settings behind the gears).
        return m_matchOn && m_reference.trackId >= 0 && t.id != m_reference.trackId && meetsRules(t);
    case FileTypeRole:    return t.fileType;
    case CommentRole:     return t.comment;
    case BitrateRole:     return t.bitrate;
    case ReplayGainRole:  return t.replaygain;
    case DateAddedRole:   return t.dateAdded;
    case TimesPlayedRole: return t.timesPlayed;
    case LastPlayedAtRole: return t.lastPlayedAt;
    case BpmLockedRole:   return t.bpmLocked;
    case TrackIdRole:     return t.id;
    case CoverArtTypeRole: return t.coverArtType;
    case SampleRateRole:  return t.sampleRate;
    case AnalysisProgressRole: return m_analysisProgressByTrack.value(t.id, -1);
    case AnalyzedRole:    return t.analyzed;
    default:              return QVariant();
    }
}

QHash<int, QByteArray> MixHouseTrackListModel::roleNames() const
{
    return {
        { TitleRole, "title" }, { ArtistRole, "artist" }, { AlbumRole, "album" },
        { GenreRole, "genre" }, { YearRole, "year" }, { BpmRole, "bpm" }, { EnergyRole, "energy" },
        { KeyRole, "musicalKey" }, { KeyIdRole, "keyId" }, { DurationRole, "durationSec" },
        { RatingRole, "rating" }, { HasBeatgridRole, "hasBeatgrid" },
        { FilePathRole, "filePath" }, { ColorRole, "color" },
        { FirstBeatSecRole, "firstBeatSec" }, { BeatLengthSecRole, "beatLengthSec" },
        { ConstantTempoRole, "constantTempo" },
        { AlbumArtistRole, "albumArtist" }, { ComposerRole, "composer" },
        { GroupingRole, "grouping" }, { TrackNumberRole, "trackNumber" }, { InProjectRole, "inProject" }, { KeyMatchRole, "keyMatch" },
        { FileTypeRole, "fileType" }, { CommentRole, "comment" },
        { BitrateRole, "bitrate" }, { ReplayGainRole, "replaygain" },
        { DateAddedRole, "dateAdded" }, { TimesPlayedRole, "timesPlayed" },
        { LastPlayedAtRole, "lastPlayedAt" }, { BpmLockedRole, "bpmLocked" },
        { TrackIdRole, "trackId" }, { CoverArtTypeRole, "coverArtType" },
        { SampleRateRole, "sampleRate" },
        { AnalysisProgressRole, "analysisProgress" }, { AnalyzedRole, "analyzed" }
    };
}

bool MixHouseTrackListModel::scanning() const { return library()->isScanning(); }
QString MixHouseTrackListModel::scanStatusText() const
{
    return muse::qtrc("mixhouse", "Scanning… %1 tracks found, %2 new")
           .arg(m_scanScanned).arg(m_scanAdded);
}

int MixHouseTrackListModel::trackCount() const { return library()->trackCount(); }
int MixHouseTrackListModel::matchingCount() const
{
    // A smart playlist, Duplicates, a folder or another app's list: the
    // collection query would count the whole collection.
    if (m_relatedView) {
        return static_cast<int>(m_tracks.size());   // the Related tab: its own songs
    }
    if (!m_externalSource.isEmpty()) {
        const int shown = static_cast<int>(m_tracks.size());
        if (m_externalSource == u"smart" && shown >= m_pageLimit && m_query.filter.isEmpty()
            && m_smartCounts.contains(m_externalPlaylist)) {
            return m_smartCounts.value(m_externalPlaylist);
        }
        return shown;
    }
    return static_cast<int>(library()->matchingTracks(m_query).ids.size());
}
bool MixHouseTrackListModel::available() const { return library()->isAvailable(); }

QString MixHouseTrackListModel::resultSummary() const
{
    const int count = matchingCount();
    if (count <= 0) {
        return QString();
    }
    const QString songs = count == 1 ? muse::qtrc("mixhouse", "1 song")
                          : muse::qtrc("mixhouse", "%1 songs").arg(QLocale().toString(count));
    // Only the rows loaded can be added up.
    if (count > static_cast<int>(m_tracks.size())) {
        return songs;
    }
    double seconds = 0.0;
    for (const MixHouseTrack& t : m_tracks) {
        seconds += std::max(0.0, t.durationSec);
    }
    const qint64 total = static_cast<qint64>(seconds);
    return songs + QStringLiteral(", %1:%2:%3").arg(total / 3600)
           .arg((total / 60) % 60, 2, 10, QLatin1Char('0')).arg(total % 60, 2, 10, QLatin1Char('0'));
}

void MixHouseTrackListModel::updateProjectFiles()
{
    QSet<QString> files;
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (project && beatGrids()) {
        for (const trackedit::TrackId trackId : project->trackIdList()) {
            for (const trackedit::Clip& clip : project->clipList(trackId)) {
                IMixHouseBeatGrids::ClipFile file;
                if (beatGrids()->clipFile(clip.key, file)) {
                    files.insert(file.path);
                }
            }
        }
    }
    if (files == m_projectFiles) {
        return;
    }
    m_projectFiles = std::move(files);
    if (rowCount() > 0) {
        emit dataChanged(index(0), index(rowCount() - 1), { InProjectRole });
    }
}

QString MixHouseTrackListModel::statusMessage() const
{
    if (library()->isAvailable()) {
        return QStringLiteral("%1 tracks — %2").arg(library()->trackCount()).arg(library()->databasePath());
    }
    return library()->lastError();
}

// GlideMix: a folder was added in Preferences > Library, or a drive was
// plugged in or out: read the DJ apps again. The drives are asked on a worker
// thread -- on the UI thread one slow drive froze the whole window (1.1.29,
// dead mapped network drives on Windows) -- and one check at a time.
void MixHouseTrackListModel::checkDjAppPlaces()
{
    if (m_djAppPlacesChecking) {
        return;
    }
    m_djAppPlacesChecking = true;
    QPointer<MixHouseTrackListModel> self(this);
    std::thread([self]() {
        DjAppReaderScope reader;   // quitting waits for this one too
        if (reader.quitting()) {
            return;
        }
        const QString places = djAppPlacesSignature();
        if (reader.quitting() || !qApp) {
            return;
        }
        QMetaObject::invokeMethod(qApp, [self, places]() {
            if (!self) {
                return;
            }
            self->m_djAppPlacesChecking = false;
            if (!self->m_djAppPlacesKnown) {
                self->m_djAppPlacesKnown = true;   // the first look: the apps are being read already
                self->m_djAppPlaces = places;
                return;
            }
            if (places != self->m_djAppPlaces) {
                self->m_djAppPlaces = places;
                reloadDjApps();
                emit self->loaded();
            }
        }, Qt::QueuedConnection);
    }).detach();
}

// ---- GlideMix fast library (in-memory index) --------------------------------
// Idea from rbxport's rbl-index (GPL-2.0-or-later, github.com/chrisle/rbxport):
// the whole collection in memory once, a view is only the order of its rows.
// Here SQL still decides which songs a view has and their order (search
// syntax, playlists, crates, key filters) -- ids only, which is quick -- and
// the rows come from memory. No 2000-row pages: the list has every song.

void MixHouseTrackListModel::ensureIndex()
{
    if (m_indexLoaded) {
        // Songs edited since the last look: re-read only those rows.
        const QList<int> changed = library()->takeChangedTrackIds();
        if (!changed.isEmpty()) {
            refreshIndexRows(changed);
        }
        return;
    }
    QElapsedTimer took;
    took.start();
    library()->takeChangedTrackIds();   // the full read below has them all
    IMixHouseLibrary::TrackQuery all;
    all.limit = 0;
    MixHouseTrackList rows = library()->tracks(all);
    m_index.clear();
    m_index.reserve(rows.size());
    for (MixHouseTrack& t : rows) {
        const int id = t.id;
        m_index.emplace(id, std::move(t));
    }
    m_indexSortKeys.clear();
    m_indexLoaded = true;
    LOGI() << "GlideMix fast library: " << m_index.size() << " songs in memory in " << took.elapsed() << " ms";
}

void MixHouseTrackListModel::refreshIndexRows(const QList<int>& ids)
{
    // In batches: an IN list per few thousand ids.
    for (qsizetype from = 0; from < ids.size(); from += 2000) {
        IMixHouseLibrary::TrackQuery q;
        q.trackIds = ids.mid(from, 2000);
        const MixHouseTrackList rows = library()->tracks(q);
        QSet<int> found;
        for (const MixHouseTrack& t : rows) {
            found.insert(t.id);
            m_index[t.id] = t;
        }
        for (int id : q.trackIds) {
            if (!found.contains(id)) {
                m_index.erase(id);   // removed or hidden
            }
        }
    }
    for (auto& [key, keys] : m_indexSortKeys) {
        for (int id : ids) {
            keys.erase(id);
        }
    }
}

void MixHouseTrackListModel::updateIndexRow(const MixHouseTrack& t)
{
    if (!m_indexLoaded) {
        return;
    }
    m_index[t.id] = t;
    for (auto& [key, keys] : m_indexSortKeys) {
        keys.erase(t.id);
    }
}

MixHouseTrackList MixHouseTrackListModel::indexedTracks()
{
    QElapsedTimer took;
    took.start();
    ensureIndex();
    IMixHouseLibrary::TrackQuery q = m_query;
    q.limit = 0;
    q.idsOnly = true;
    const MixHouseTrackList order = library()->tracks(q);
    const qint64 sqlMs = took.elapsed();

    // A song the index hasn't got (added by another connection): read it now.
    QList<int> missing;
    for (const MixHouseTrack& o : order) {
        if (m_index.find(o.id) == m_index.end()) {
            missing << o.id;
        }
    }
    if (!missing.isEmpty()) {
        refreshIndexRows(missing);
    }

    MixHouseTrackList out;
    out.reserve(order.size());
    for (const MixHouseTrack& o : order) {
        const auto it = m_index.find(o.id);
        if (it == m_index.end()) {
            continue;
        }
        out.push_back(it->second);
        out.back().playlistPosition = o.playlistPosition;
    }

    // Text columns: SQL's order is ASCII; sort like the old list did, with a
    // collator (accents where a person expects them), using cached sort keys.
    using SortKey = IMixHouseLibrary::SortKey;
    const SortKey key = m_query.sort;
    const bool textSort = key == SortKey::Title || key == SortKey::Artist || key == SortKey::Album
                          || key == SortKey::Genre || key == SortKey::AlbumArtist || key == SortKey::Composer
                          || key == SortKey::Grouping || key == SortKey::FileType || key == SortKey::Comment;
    if (textSort && out.size() > 1) {
        auto textOf = [key](const MixHouseTrack& t) -> const QString& {
            switch (key) {
            case SortKey::Artist: return t.artist;
            case SortKey::Album: return t.album;
            case SortKey::Genre: return t.genre;
            case SortKey::AlbumArtist: return t.albumArtist;
            case SortKey::Composer: return t.composer;
            case SortKey::Grouping: return t.grouping;
            case SortKey::FileType: return t.fileType;
            case SortKey::Comment: return t.comment;
            default: return t.title;
            }
        };
        static QCollator collator = []() {
            QCollator c;
            c.setCaseSensitivity(Qt::CaseInsensitive);
            return c;
        }();
        auto& keys = m_indexSortKeys[static_cast<int>(key)];
        std::vector<std::pair<const QCollatorSortKey*, size_t> > order2;
        order2.reserve(out.size());
        for (size_t i = 0; i < out.size(); ++i) {
            auto it = keys.find(out[i].id);
            if (it == keys.end()) {
                it = keys.emplace(out[i].id, collator.sortKey(textOf(out[i]))).first;
            }
            order2.emplace_back(&it->second, i);
        }
        const bool asc = m_query.ascending;
        std::stable_sort(order2.begin(), order2.end(), [asc](const auto& a, const auto& b) {
            const int cmp = a.first->compare(*b.first);
            return asc ? cmp < 0 : cmp > 0;
        });
        MixHouseTrackList sorted;
        sorted.reserve(out.size());
        for (const auto& [k, i] : order2) {
            sorted.push_back(std::move(out[i]));
        }
        out = std::move(sorted);
    }
    LOGI() << "GlideMix fast library: view of " << out.size() << " songs in " << took.elapsed()
           << " ms (order from SQL " << sqlMs << " ms)";
    return out;
}

// ---- GlideMix Match and Related ---------------------------------------------
// The reference is the song under the playhead; in a transition the incoming
// one (the clip that started later). Match lights the Key cells of songs
// whose key mixes with it; Related lists the collection songs that go with it.

void MixHouseTrackListModel::subscribeReference()
{
    if (m_referenceSubscribed || !globalContext()) {
        return;
    }
    m_referenceSubscribed = true;
    m_referenceTimer.setSingleShot(true);
    m_referenceTimer.setInterval(200);   // a few updates a second while playing
    QObject::connect(&m_referenceTimer, &QTimer::timeout, this, [this]() { updateReference(); });
    if (const auto state = globalContext()->playbackState()) {
        state->playbackPositionChanged().onReceive(this, [this](muse::secs_t) {
            if ((m_matchOn || m_relatedOn) && !m_referenceTimer.isActive()) {
                m_referenceTimer.start();
            }
        });
    }
    globalContext()->currentTrackeditProjectChanged().onNotify(this, [this]() {
        if (m_matchOn || m_relatedOn) {
            m_referenceTimer.start();
        }
    });
    if (projectHistory()) {
        projectHistory()->historyChanged().onReceive(this, [this](trackedit::HistoryEvent) {
            if (m_matchOn || m_relatedOn) {
                m_referenceTimer.start();
            }
        });
    }
}

void MixHouseTrackListModel::setMatchOn(bool on)
{
    if (m_matchOn == on) {
        return;
    }
    m_matchOn = on;
    QSettings().setValue(m_relatedView ? QStringLiteral("GlideMix/relatedTabMatchOn") : QStringLiteral("GlideMix/matchOn"), on);
    subscribeReference();
    updateReference();
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, std::max(0, m_uiColumnCount - 1)), { KeyMatchRole });
    }
    emit matchChanged();
}

void MixHouseTrackListModel::setMatchWidth(int width)
{
    width = std::clamp(width, 0, 3);
    if (m_matchWidth == width) {
        return;
    }
    m_matchWidth = width;
    QSettings().setValue(QStringLiteral("GlideMix/matchKeyWidth"), width);
    if (rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, std::max(0, m_uiColumnCount - 1)), { KeyMatchRole });
    }
    emit matchChanged();
}

void MixHouseTrackListModel::setRelatedOn(bool on)
{
    if (m_relatedOn == on) {
        return;
    }
    m_relatedOn = on;
    QSettings().setValue(QStringLiteral("GlideMix/relatedOn"), on);
    subscribeReference();
    updateReference();
    updateRelated();
    emit relatedChanged();
}

QString MixHouseTrackListModel::referenceText() const
{
    if (m_reference.trackId < 0 && m_reference.keyId <= 0) {
        return QString();
    }
    QStringList parts;
    if (m_reference.keyId > 0) {
        parts << IMixHouseKeyMatch::keyText(m_reference.keyId, libraryConfiguration()->keyNotation());
    }
    if (m_reference.bpm > 0.0) {
        parts << QString::number(m_reference.bpm, 'f', m_reference.bpm == std::round(m_reference.bpm) ? 0 : 1);
    }
    return parts.join(QStringLiteral(" · "));
}

namespace {
QStringList relatedTokens(const QString& text)
{
    QString t = text.toLower();
    for (const char* sep : { " feat. ", " ft. ", " featuring ", " & ", " x ", " vs ", " vs. ", ";", "/" }) {
        t.replace(QLatin1String(sep), QStringLiteral(","));
    }
    QStringList out;
    for (const QString& part : t.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString p = part.trimmed();
        if (!p.isEmpty()) {
            out << p;
        }
    }
    return out;
}

bool sharesToken(const QStringList& a, const QStringList& b)
{
    for (const QString& x : a) {
        if (b.contains(x)) {
            return true;
        }
    }
    return false;
}
}

void MixHouseTrackListModel::updateReference()
{
    Reference ref;
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    const auto state = globalContext() ? globalContext()->playbackState() : nullptr;
    if (project && state && beatGrids()) {
        const double pos = state->playbackPosition().to_double();
        const trackedit::Clip* best = nullptr;
        trackedit::Clip bestClip;
        for (const trackedit::TrackId trackId : project->trackIdList()) {
            for (const trackedit::Clip& clip : project->clipList(trackId)) {
                if (clip.startTime <= pos && pos < clip.endTime && (!best || clip.startTime > bestClip.startTime)) {
                    bestClip = clip;
                    best = &bestClip;
                }
            }
        }
        IMixHouseBeatGrids::ClipFile file;
        if (best && beatGrids()->clipFile(bestClip.key, file)) {
            ensureIndex();
            const MixHouseTrack* song = nullptr;
            for (const auto& [id, t] : m_index) {
                if (t.filePath == file.path) {
                    song = &t;
                    break;
                }
            }
            MixHouseTrack fallback;
            if (!song) {
                IMixHouseLibrary::TrackQuery q;
                q.location = file.path;
                const MixHouseTrackList found = library()->tracks(q);
                if (!found.empty()) {
                    fallback = found.front();
                    song = &fallback;
                }
            }
            if (song) {
                ref.trackId = song->id;
                ref.keyId = song->keyId;
                ref.bpm = song->bpm;
                ref.energy = song->energy;
                ref.genre = song->genre;
                ref.artist = song->artist;
                ref.year = song->year;
                ref.title = song->title;
                ref.composer = song->composer;
                ref.fileType = song->fileType;
                ref.color = song->color;
                ref.genres = relatedTokens(song->genre);
                ref.artists = relatedTokens(song->artist);
            }
            // A transposed clip sounds in another key: match that one.
            if (keyMatch()) {
                const int sounding = keyMatch()->keyInfo(bestClip.key).effectiveKeyId;
                if (sounding > 0) {
                    ref.keyId = sounding;
                }
            }
        }
    }
    if (ref == m_reference) {
        return;
    }
    LOGI() << "GlideMix match: reference song " << ref.trackId << " key " << ref.keyId << " bpm " << ref.bpm;
    m_reference = ref;
    prepareRuleData();
    emit referenceChanged();
    if (m_matchOn && rowCount() > 0) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, std::max(0, m_uiColumnCount - 1)), { KeyMatchRole });
    }
    if (m_relatedOn) {
        updateRelated();
        emit relatedChanged();
        if (m_relatedView) {
            load();
        }
    }
}

QVariantMap MixHouseTrackListModel::relatedRules() const
{
    QVariantMap rules = QSettings().value(QStringLiteral("GlideMix/relatedRules")).toMap();
    auto def = [&rules](const char* key, const QVariant& value) {
        if (!rules.contains(QLatin1String(key))) {
            rules.insert(QLatin1String(key), value);
        }
    };
    def("bpmOn", true);
    def("bpmPercent", 5);
    def("halfDouble", true);
    def("keyOn", true);
    def("keyWidth", 2);
    def("energyOn", false);
    def("genreOn", false);
    def("artistOn", false);
    def("yearOn", false);
    def("yearRange", 0);
    def("ratingOn", false);
    def("minRating", 1);
    def("addedOn", false);
    def("addedDays", 30);
    def("composerOn", false);
    def("remixerOn", false);
    def("labelOn", false);
    def("formatOn", false);
    def("historyOn", false);
    def("historyRange", 3);
    def("tagOn", false);
    def("tagName", QString());
    def("colorOn", false);
    def("commentOn", false);
    def("commentText", QString());
    return rules;
}

void MixHouseTrackListModel::setRelatedRules(const QVariantMap& rules)
{
    QSettings().setValue(QStringLiteral("GlideMix/relatedRules"), rules);
    for (MixHouseTrackListModel* model : s_liveModels) {
        model->loadRules();
        model->updateRelated();
        emit model->relatedChanged();
        if (model->m_matchOn && model->rowCount() > 0) {
            emit model->dataChanged(model->index(0, 0),
                                    model->index(model->rowCount() - 1, std::max(0, model->m_uiColumnCount - 1)), { KeyMatchRole });
        }
        if (model->m_relatedView) {
            model->load();
        }
    }
}

void MixHouseTrackListModel::setRelatedView(bool on)
{
    if (m_relatedView == on) {
        return;
    }
    m_relatedView = on;
    // The tab has its own Match switch (the library keeps its own).
    m_matchOn = QSettings().value(on ? QStringLiteral("GlideMix/relatedTabMatchOn") : QStringLiteral("GlideMix/matchOn"),
                                  false).toBool();
    m_relatedOn = on;
    emit relatedChanged();
    emit matchChanged();
}

void MixHouseTrackListModel::openRelatedTab()
{
    if (dispatcher()) {
        dispatcher()->dispatch("dock-set-open", muse::actions::ActionData::make_arg2<QString, bool>(
                                   QStringLiteral("mixHouseRelatedPanel"), true));
    }
}

// GlideMix: one rule set for Match (the lit keys) and Related (the list),
// set in the settings window behind the gears (Chris, 2026-11-09).
void MixHouseTrackListModel::loadRules()
{
    const QVariantMap r = relatedRules();
    MatchRules& m = m_rules;
    m.bpmOn = r.value(QStringLiteral("bpmOn")).toBool();
    m.bpmPercent = r.value(QStringLiteral("bpmPercent")).toDouble();
    m.halfDouble = r.value(QStringLiteral("halfDouble")).toBool();
    m.keyOn = r.value(QStringLiteral("keyOn")).toBool();
    m.keyWidth = std::clamp(r.value(QStringLiteral("keyWidth")).toInt(), 0, 3);
    m.energyOn = r.value(QStringLiteral("energyOn")).toBool();
    m.genreOn = r.value(QStringLiteral("genreOn")).toBool();
    m.artistOn = r.value(QStringLiteral("artistOn")).toBool();
    m.composerOn = r.value(QStringLiteral("composerOn")).toBool();
    m.colorOn = r.value(QStringLiteral("colorOn")).toBool();
    m.yearOn = r.value(QStringLiteral("yearOn")).toBool();
    m.yearRange = r.value(QStringLiteral("yearRange")).toInt();
    m.ratingOn = r.value(QStringLiteral("ratingOn")).toBool();
    m.minRating = r.value(QStringLiteral("minRating")).toInt();
    m.addedOn = r.value(QStringLiteral("addedOn")).toBool();
    m.addedSince = QDate::currentDate().addDays(-r.value(QStringLiteral("addedDays")).toInt()).toString(Qt::ISODate);
    m.commentOn = r.value(QStringLiteral("commentOn")).toBool();
    m.commentText = r.value(QStringLiteral("commentText")).toString();
    m.labelOn = r.value(QStringLiteral("labelOn")).toBool();
    m.remixerOn = r.value(QStringLiteral("remixerOn")).toBool();
    m.formatOn = r.value(QStringLiteral("formatOn")).toBool();
    m.historyOn = r.value(QStringLiteral("historyOn")).toBool();
    m.historyRange = std::max(1, r.value(QStringLiteral("historyRange")).toInt());
    m.tagOn = r.value(QStringLiteral("tagOn")).toBool();
    m.tagName = r.value(QStringLiteral("tagName")).toString();
    prepareRuleData();
}

void MixHouseTrackListModel::prepareRuleData()
{
    if (!m_ruleDataReady || !library()) {
        return;
    }
    if (m_rules.labelOn && m_ruleLabels.isEmpty()) {
        m_ruleLabels = library()->customTagValues(QStringLiteral("LABEL"));
    }
    if (m_rules.remixerOn && m_ruleRemixers.isEmpty()) {
        m_ruleRemixers = library()->customTagValues(QStringLiteral("REMIXER"));
    }
    if (m_rules.tagOn && !m_rules.tagName.isEmpty() && m_ruleTagValuesName != m_rules.tagName) {
        m_ruleTagValues = library()->customTagValues(m_rules.tagName);
        m_ruleTagValuesName = m_rules.tagName;
    }
    m_ruleHistory.clear();
    if (m_rules.historyOn && m_reference.trackId >= 0) {
        // Not "near": a macro in the Windows headers.
        const QList<int> played = library()->historyNeighbours(m_reference.trackId, m_rules.historyRange);
        m_ruleHistory = QSet<int>(played.begin(), played.end());
    }
}

bool MixHouseTrackListModel::meetsRules(const MixHouseTrack& t) const
{
    const MatchRules& m = m_rules;
    const Reference& ref = m_reference;
    if (m.bpmOn && !match::bpmWithin(ref.bpm, t.bpm, m.bpmPercent, m.halfDouble)) {
        return false;
    }
    if (m.keyOn && !match::keysMix(ref.keyId, t.keyId, static_cast<match::KeyWidth>(m.keyWidth))) {
        return false;
    }
    if (m.energyOn && (ref.energy <= 0.0 || t.energy <= 0.0 || std::round(t.energy) < std::round(ref.energy)
                       || std::round(t.energy) > std::round(ref.energy) + 1)) {
        return false;
    }
    if (m.genreOn && (ref.genres.isEmpty() || !sharesToken(ref.genres, relatedTokens(t.genre)))) {
        return false;
    }
    if (m.artistOn) {
        const bool hit = !ref.artists.isEmpty()
                         && (sharesToken(ref.artists, relatedTokens(t.artist))
                             || std::any_of(ref.artists.begin(), ref.artists.end(), [&t](const QString& a) {
                                    return a.size() > 2 && t.title.contains(a, Qt::CaseInsensitive);
                                }));
        if (!hit) {
            return false;
        }
    }
    if (m.composerOn && (ref.composer.isEmpty() || t.composer.compare(ref.composer, Qt::CaseInsensitive) != 0)) {
        return false;
    }
    if (m.colorOn && (ref.color < 0 || t.color != ref.color)) {
        return false;
    }
    if (m.yearOn) {
        const int y = t.year.left(4).toInt();
        const int refYear = ref.year.left(4).toInt();
        if (refYear <= 0 || y <= 0 || std::abs(y - refYear) > m.yearRange) {
            return false;
        }
    }
    if (m.ratingOn && t.rating < m.minRating) {
        return false;
    }
    if (m.addedOn && t.dateAdded.left(10) < m.addedSince) {
        return false;
    }
    if (m.commentOn && !m.commentText.isEmpty() && !t.comment.contains(m.commentText, Qt::CaseInsensitive)) {
        return false;
    }
    auto sameTag = [&](const QHash<int, QString>& values) {
        const QStringList mine = relatedTokens(values.value(ref.trackId));
        return !mine.isEmpty() && sharesToken(mine, relatedTokens(values.value(t.id)));
    };
    if (m.labelOn && !sameTag(m_ruleLabels)) {
        return false;
    }
    if (m.remixerOn && !sameTag(m_ruleRemixers)) {
        return false;
    }
    if (m.tagOn && !m.tagName.isEmpty() && !sameTag(m_ruleTagValues)) {
        return false;
    }
    if (m.formatOn && t.fileType.compare(ref.fileType, Qt::CaseInsensitive) != 0) {
        return false;
    }
    if (m.historyOn && !m_ruleHistory.contains(t.id)) {
        return false;
    }
    return true;
}

void MixHouseTrackListModel::updateRelated()
{
    m_relatedTracks.clear();
    m_relatedSongs.clear();
    if (!m_relatedOn || m_reference.trackId < 0) {
        return;
    }
    QElapsedTimer took;
    took.start();
    ensureIndex();
    for (const auto& [id, t] : m_index) {
        if (id != m_reference.trackId && meetsRules(t)) {
            m_relatedTracks.push_back(t);
        }
    }
    // Closest first: the same key, then the nearest BPM.
    const int refKey = m_reference.keyId;
    const double refBpm = m_reference.bpm;
    std::stable_sort(m_relatedTracks.begin(), m_relatedTracks.end(), [&](const MixHouseTrack& a, const MixHouseTrack& b) {
        const bool sa = a.keyId == refKey;
        const bool sb = b.keyId == refKey;
        if (sa != sb) {
            return sa;
        }
        return match::bpmDistance(refBpm, a.bpm) < match::bpmDistance(refBpm, b.bpm);
    });
    constexpr size_t kMaxShown = 500;
    if (!m_relatedView && m_relatedTracks.size() > kMaxShown) {
        m_relatedTracks.resize(kMaxShown);
    }
    const MixHouseKeyNotation notation = libraryConfiguration()->keyNotation();
    for (const MixHouseTrack& t : m_relatedTracks) {
        QVariantMap row;
        row.insert(QStringLiteral("trackId"), t.id);
        row.insert(QStringLiteral("title"), t.title);
        row.insert(QStringLiteral("artist"), t.artist);
        row.insert(QStringLiteral("bpm"), t.bpm);
        row.insert(QStringLiteral("keyId"), t.keyId);
        row.insert(QStringLiteral("key"), t.keyId > 0 ? IMixHouseKeyMatch::keyText(t.keyId, notation) : t.key);
        row.insert(QStringLiteral("energy"), t.energy);
        row.insert(QStringLiteral("genre"), t.genre);
        m_relatedSongs.append(row);
    }
    LOGI() << "GlideMix related: " << m_relatedTracks.size() << " songs in " << took.elapsed() << " ms";
}

void MixHouseTrackListModel::loadRelatedSong(int index)
{
    if (index >= 0 && index < static_cast<int>(m_relatedTracks.size())) {
        loadTrack(m_relatedTracks[static_cast<size_t>(index)]);
    }
}

void MixHouseTrackListModel::dragRelatedSong(int index)
{
    if (index >= 0 && index < static_cast<int>(m_relatedTracks.size())) {
        startExternalDragTracks({ m_relatedTracks[static_cast<size_t>(index)] });
    }
}
