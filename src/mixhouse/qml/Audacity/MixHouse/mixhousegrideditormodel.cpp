/*
 * GlideMix
 */
#include "mixhousegrideditormodel.h"

#include <algorithm>
#include <cmath>
#include <thread>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>

#include "settings.h"
#include "au3-mixtape-engine/MixtapeDeck.h"

#include "mixhouse/imixhouseexternallibrary.h"
#include "mixhouse/mixhouseaudiosource.h"

#include "log.h"

using namespace au::mixhouse;

namespace {
constexpr int kBinFrames = 32;   // 0.7 ms at 44.1 kHz: deep zoom stays detailed
constexpr size_t kMaxUndo = 200;
constexpr double kMinBpm = 40.0;
constexpr double kMaxBpm = 300.0;
using au::playback::IMetronome;

double monotonicSeconds()
{
    static QElapsedTimer clock;
    if (!clock.isValid()) {
        clock.start();
    }
    return clock.nsecsElapsed() / 1e9;
}

//! Decodes the file the way the timeline imports it and summarises it in
//! three bands (below 200 Hz, 200 Hz - 2.5 kHz, above), rekordbox-style.
bool decodeTrack(const QString& path, mixtape::DeckAudio& audio, MixHouseGridEditorModel::Waveform& waveform,
                 QString& error)
{
    const AudioInfo probe = probeAudioFile(path);
    if (!probe.valid || probe.channels <= 0 || probe.sampleRate <= 0) {
        error = probe.error.isEmpty() ? QStringLiteral("unsupported audio file") : probe.error;
        return false;
    }
    // mp3s always decode as stereo (mixhouseaudiosource.cpp: mpg123_open_fixed).
    const unsigned channels = path.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive) ? 2u
                              : static_cast<unsigned>(probe.channels);
    audio.channels = channels;
    audio.sampleRate = probe.sampleRate;
    if (probe.frames > 0) {
        audio.samples.reserve(static_cast<size_t>(probe.frames) * channels);
    }
    const AudioInfo info = decodeAudioFile(path, [&](const float* samples, long long frames) {
        audio.samples.insert(audio.samples.end(), samples, samples + frames * channels);
        return true;
    });
    if (!info.valid || audio.samples.empty()) {
        error = info.error.isEmpty() ? QStringLiteral("could not decode the file") : info.error;
        return false;
    }

    const double rate = audio.sampleRate;
    const double aLow = 1.0 - std::exp(-2.0 * M_PI * 200.0 / rate);
    const double aHigh = 1.0 - std::exp(-2.0 * M_PI * 2500.0 / rate);
    double l1 = 0, l2 = 0, h1 = 0, h2 = 0;
    const int64_t frames = audio.Frames();
    const size_t bins = static_cast<size_t>((frames + kBinFrames - 1) / kBinFrames);
    waveform.binSec = kBinFrames / rate;
    waveform.all.assign(bins, 0.0f);
    waveform.low.assign(bins, 0.0f);
    waveform.mid.assign(bins, 0.0f);
    waveform.high.assign(bins, 0.0f);
    for (int64_t i = 0; i < frames; ++i) {
        double m = 0.0;
        for (unsigned c = 0; c < channels; ++c) {
            m += audio.samples[static_cast<size_t>(i) * channels + c];
        }
        m /= channels;
        l1 += aLow * (m - l1);
        l2 += aLow * (l1 - l2);
        h1 += aHigh * (m - h1);
        h2 += aHigh * (h1 - h2);
        const size_t b = static_cast<size_t>(i / kBinFrames);
        waveform.all[b] = std::max(waveform.all[b], static_cast<float>(std::abs(m)));
        waveform.low[b] = std::max(waveform.low[b], static_cast<float>(std::abs(l2)));
        waveform.mid[b] = std::max(waveform.mid[b], static_cast<float>(std::abs(h2 - l2)));
        waveform.high[b] = std::max(waveform.high[b], static_cast<float>(std::abs(m - h2)));
    }
    // Each band against its own loudest (99.5th percentile), so the colour
    // shows how bass, mids and highs balance in this track.
    for (auto* band : { &waveform.all, &waveform.low, &waveform.mid, &waveform.high }) {
        std::vector<float> sorted = *band;
        const size_t at = std::min(sorted.size() - 1, static_cast<size_t>(sorted.size() * 0.995));
        std::nth_element(sorted.begin(), sorted.begin() + static_cast<long>(at), sorted.end());
        const float top = std::max(1e-6f, sorted[at]);
        for (float& v : *band) {
            v = std::min(1.0f, v / top);
        }
    }
    return true;
}
}

MixHouseGridEditorModel::MixHouseGridEditorModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)), m_alive(std::make_shared<bool>(true))
{
    m_timer.setInterval(16);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &MixHouseGridEditorModel::tick);
}

MixHouseGridEditorModel::~MixHouseGridEditorModel()
{
    *m_alive = false;
    closeDeck();
}

void MixHouseGridEditorModel::componentComplete()
{
    if (gridEditor()) {
        gridEditor()->trackOpened().onReceive(this, [this](int trackId) { open(trackId); });
        if (gridEditor()->currentTrackId() >= 0) {
            open(gridEditor()->currentTrackId());
        }
    }
    if (libraryConfiguration()) {
        libraryConfiguration()->settingsChanged().onNotify(this, [this]() { emit optionsChanged(); });
    }
    if (appMetronome()) {
        appMetronome()->settingsChanged().onNotify(this, [this]() {
            updateDeckClicks();
            emit optionsChanged();
        });
    }
}

void MixHouseGridEditorModel::closeDeck()
{
    m_timer.stop();
    if (m_deck) {
        m_deck->Close();
        m_deck.reset();
    }
}

void MixHouseGridEditorModel::setStatus(const QString& text)
{
    if (m_status != text) {
        m_status = text;
        emit statusChanged();
    }
}

void MixHouseGridEditorModel::open(int trackId)
{
    if (!library() || trackId < 0 || isExternalLibraryTrack(trackId)) {
        return;
    }
    if (trackId == m_trackId && (m_audio || m_loading)) {
        return;
    }
    IMixHouseLibrary::TrackQuery query;
    query.trackId = trackId;
    const MixHouseTrackList found = library()->tracks(query);
    if (found.empty()) {
        return;
    }
    const MixHouseTrack& t = found.front();

    closeDeck();
    m_trackId = trackId;
    m_filePath = t.filePath;
    m_title = t.title.isEmpty() ? QFileInfo(t.filePath).completeBaseName() : t.title;
    m_artist = t.artist;
    m_keyText = t.key;
    m_trackColor = t.color >= 0 ? QColor::fromRgb(static_cast<QRgb>(t.color)) : QColor();
    m_errorText.clear();
    m_audio.reset();
    m_waveform.reset();
    m_undo.clear();
    m_redo.clear();
    m_taps.clear();
    m_loopActive = false;
    m_playing = false;
    m_position = 0.0;
    m_selectedPhrase = -1;
    m_locked = t.bpmLocked;

    if (t.beats.valid && t.beats.constantTempo && t.beats.bpm > 0.0) {
        const double period = 60.0 / t.beats.bpm;
        m_grid.bpm = t.beats.bpm;
        m_grid.firstBeatSec = t.beats.firstBeatSec - std::floor(t.beats.firstBeatSec / period) * period;
        m_grid.downbeatSec = t.downbeatSec >= 0.0 ? t.downbeatSec : m_grid.firstBeatSec;
    } else {
        m_grid.bpm = t.bpm > 0.0 ? t.bpm : 120.0;
        m_grid.firstBeatSec = 0.0;
        m_grid.downbeatSec = 0.0;
    }
    m_gridSource = t.downbeatDetector == QStringLiteral("rekordbox") ? tr("imported grid")
                   : t.downbeatDetector == QStringLiteral("manual") ? tr("Edited")
                   : t.beats.valid ? tr("Analysed") : tr("No grid yet");
    m_cue = m_grid.downbeatSec;

    const auto edited = library()->trackPhraseEdits(trackId);
    m_phrasesEdited = edited.has_value();
    m_phrases = edited ? *edited : unEditedPhrases();

    m_loading = true;
    const int generation = ++m_loadGeneration;
    emit trackChanged();
    emit gridChanged();
    emit phrasesChanged();
    emit historyChanged();
    emit loopChanged();
    emit positionChanged();
    emit playingChanged();
    emit cueChanged();
    emit waveformChanged();
    setStatus(tr("Loading %1…").arg(m_title));

    const auto alive = m_alive;
    const QString path = m_filePath;
    std::thread([this, alive, generation, trackId, path]() {
        auto audio = std::make_shared<mixtape::DeckAudio>();
        auto waveform = std::make_shared<Waveform>();
        QString error;
        if (!decodeTrack(path, *audio, *waveform, error)) {
            audio.reset();
            waveform.reset();
        }
        if (!*alive) {
            return;
        }
        QTimer::singleShot(0, qApp, [this, alive, generation, trackId, audio, waveform, error]() {
            if (*alive && generation == m_loadGeneration) {
                onLoaded(trackId, audio, waveform, error);
            }
        });
    }).detach();
}

void MixHouseGridEditorModel::onLoaded(int trackId, std::shared_ptr<mixtape::DeckAudio> audio,
                                       std::shared_ptr<Waveform> waveform, const QString& error)
{
    m_loading = false;
    if (!audio) {
        m_errorText = error;
        setStatus(tr("Could not open %1: %2").arg(m_title, error));
        emit trackChanged();
        return;
    }
    m_audio = audio;
    m_waveform = waveform;
    m_deck = std::make_unique<mixtape::Deck>();
    if (!m_deck->Open(m_audio, muse::settings()->value(muse::Settings::Key("au3audio", "AudioIO/PlaybackDevice")).toString())) {
        setStatus(tr("No audio output: %1").arg(QString::fromStdString(m_deck->LastError())));
    } else {
        setStatus(tr("%1 — %2").arg(m_title, m_gridSource));
    }
    m_deck->SetSlip(m_slip);
    updateDeckClicks();
    updateDeckGrid();
    m_deck->Seek(m_cue);
    m_position = m_cue;
    m_timer.start();
    LOGI() << "Beatgrid editor: loaded track " << trackId << " (" << m_audio->Duration() << " s)";
    emit trackChanged();
    emit waveformChanged();
    emit positionChanged();
}

void MixHouseGridEditorModel::tick()
{
    if (!m_deck) {
        return;
    }
    const double position = m_deck->Position();
    const bool playing = m_deck->IsPlaying();
    if (std::abs(position - m_position) > 1e-4) {
        m_position = position;
        emit positionChanged();
    }
    if (playing != m_playing) {
        m_playing = playing;
        emit playingChanged();
    }
}

QColor MixHouseGridEditorModel::bandColor(float low, float mid, float high)
{
    // Mids under-weighted a little: rekordbox's colours lean red/blue (pink
    // kicks, blue hats), green only where the mids really lead.
    // Squared, so the leading band wins clearly and colours don't wash to white.
    double r = std::pow(low, 2.0);
    double g = std::pow(mid * 0.75, 2.0);
    double b = std::pow(high * 0.9, 2.0);
    const double top = std::max({ r, g, b, 1e-6 });
    r /= top;
    g /= top;
    b /= top;
    const double floorLevel = 0.06;
    const auto channel = [floorLevel](double v) { return static_cast<int>(std::lround(255.0 * (floorLevel + (1.0 - floorLevel) * v))); };
    return QColor(channel(r), channel(g), channel(b));
}

double MixHouseGridEditorModel::livePosition() const
{
    return m_deck && m_playing && !m_scratching ? m_deck->Position() : m_position;
}

double MixHouseGridEditorModel::durationSec() const
{
    return m_audio ? m_audio->Duration() : 0.0;
}

QString MixHouseGridEditorModel::barBeatText() const
{
    if (m_grid.bpm <= 0.0) {
        return QString();
    }
    const double beat = beatSec();
    const double sinceBarOne = m_position - m_grid.downbeatSec + beat * 0.02;   // don't flicker right on a beat
    const long long beats = static_cast<long long>(std::floor(sinceBarOne / beat));
    const long long bar = beats >= 0 ? beats / 4 + 1 : -((-beats - 1) / 4 + 1);
    const long long inBar = ((beats % 4) + 4) % 4 + 1;
    return QStringLiteral("%1.%2").arg(bar).arg(inBar);
}

// ---- options

bool MixHouseGridEditorModel::quantize() const
{
    return libraryConfiguration() ? libraryConfiguration()->quantizeEnabled() : true;
}

void MixHouseGridEditorModel::setQuantize(bool on)
{
    if (libraryConfiguration() && on != quantize()) {
        libraryConfiguration()->setQuantizeEnabled(on);
    }
}

void MixHouseGridEditorModel::setSlip(bool on)
{
    if (m_slip == on) {
        return;
    }
    m_slip = on;
    if (m_deck) {
        m_deck->SetSlip(on);
    }
    emit optionsChanged();
}

void MixHouseGridEditorModel::setMetronome(bool on)
{
    if (m_metronome == on) {
        return;
    }
    m_metronome = on;
    updateDeckGrid();
    emit optionsChanged();
}

double MixHouseGridEditorModel::metronomeLevel() const
{
    return appMetronome() ? appMetronome()->level() : 0.5;
}

void MixHouseGridEditorModel::setMetronomeLevel(double level)
{
    if (appMetronome() && std::abs(level - appMetronome()->level()) > 1e-4) {
        appMetronome()->setLevel(std::clamp(level, 0.0, 1.0));
    }
}

QVariantList MixHouseGridEditorModel::metronomeMenu() const
{
    // Same menu as the toolbar's metronome (MetronomeToolBarItem::rebuildMenu).
    QVariantList menu;
    const auto m = appMetronome();
    if (!m) {
        return menu;
    }
    const auto item = [](const QString& id, const QString& title, bool checked) {
        return QVariantMap { { "id", id }, { "title", title }, { "checkable", true }, { "checked", checked } };
    };
    const auto header = [](const QString& title) {
        return QVariantMap { { "id", "" }, { "title", title }, { "enabled", false } };
    };
    menu << header(tr("Click level"));
    for (int pct : { 25, 50, 75, 100 }) {
        menu << item(QStringLiteral("level:%1").arg(pct), QStringLiteral("%1%").arg(pct), std::lround(m->level() * 100) == pct);
    }
    menu << QVariantMap {};
    menu << header(tr("Sound"));
    const QStringList voices { tr("Classic"), tr("Wood"), tr("Click") };
    for (int i = 0; i < voices.size(); ++i) {
        menu << item(QStringLiteral("voice:%1").arg(i), voices[i], static_cast<int>(m->voice()) == i);
    }
    menu << QVariantMap {};
    QVariantList pitch, tonality, damp;
    for (int st : { -12, -7, -5, -2, 0, 2, 5, 7, 12 }) {
        pitch << item(QStringLiteral("pitch:%1").arg(st), st == 0 ? tr("0  (default)") : QStringLiteral("%1%2 st").arg(st > 0 ? "+" : "").arg(st),
                      m->pitch() == st);
    }
    const QStringList tonalNames { tr("Click"), tr("Mostly click"), tr("Balanced"), tr("Mostly tone"), tr("Tone") };
    for (int i = 0; i < tonalNames.size(); ++i) {
        tonality << item(QStringLiteral("tonality:%1").arg(i), tonalNames[i], std::lround(m->tonality() * 4) == i);
    }
    const QStringList dampNames { tr("Long ring"), tr("Ring"), tr("Medium"), tr("Short"), tr("Very dry") };
    for (int i = 0; i < dampNames.size(); ++i) {
        damp << item(QStringLiteral("damp:%1").arg(i), dampNames[i], std::lround(m->damp() * 4) == i);
    }
    menu << QVariantMap { { "id", "tune" }, { "title", tr("Tune") }, { "subitems", QVariantList {
        QVariantMap { { "id", "tune-pitch" }, { "title", tr("Tuning") }, { "subitems", pitch } },
        QVariantMap { { "id", "tune-tonality" }, { "title", tr("Tonality") }, { "subitems", tonality } },
        QVariantMap { { "id", "tune-damp" }, { "title", tr("Damp") }, { "subitems", damp } } } } };
    return menu;
}

void MixHouseGridEditorModel::handleMetronomeMenu(const QString& itemId)
{
    const auto m = appMetronome();
    if (!m) {
        return;
    }
    const QStringList parts = itemId.split(':');
    const QString kind = parts.value(0);
    const double v = parts.value(1).toDouble();
    if (kind == "level") {
        m->setLevel(v / 100.0);
    } else if (kind == "voice") {
        m->setVoice(static_cast<IMetronome::Voice>(static_cast<int>(v)));
    } else if (kind == "pitch") {
        m->setTuning(static_cast<int>(v), m->tonality(), m->damp());
    } else if (kind == "tonality") {
        m->setTuning(m->pitch(), v / 4.0, m->damp());
    } else if (kind == "damp") {
        m->setTuning(m->pitch(), m->tonality(), v / 4.0);
    }
}

void MixHouseGridEditorModel::updateDeckClicks()
{
    if (!m_deck || !m_audio || !appMetronome()) {
        return;
    }
    const auto metronome = appMetronome();
    const int rate = static_cast<int>(std::lround(m_audio->sampleRate));
    const IMetronome::Clicks* set = metronome->clicks();
    if (!set) {
        metronome->prepare(rate);
        set = metronome->clicks();
    }
    if (!set || set->beat.empty() || set->sampleRate <= 0) {
        return;
    }
    // The metronome renders for whatever rate the timeline last played at;
    // resample to this track's rate rather than re-render the shared one.
    const double ratio = double(set->sampleRate) / rate;
    const auto resample = [ratio](const std::vector<float>& in) {
        std::vector<float> out(static_cast<size_t>(in.size() / ratio));
        for (size_t i = 0; i < out.size(); ++i) {
            const double p = i * ratio;
            const size_t j = static_cast<size_t>(p);
            const double f = p - j;
            out[i] = j + 1 < in.size() ? static_cast<float>(in[j] * (1.0 - f) + in[j + 1] * f) : in.back();
        }
        return out;
    };
    auto clicks = std::make_shared<mixtape::DeckClicks>();
    clicks->beat = resample(set->beat);
    clicks->accent = resample(set->accent);
    clicks->gain = set->gain;
    m_deck->SetClicks(std::move(clicks));
}

void MixHouseGridEditorModel::updateDeckGrid()
{
    if (m_deck) {
        m_deck->SetMetronome(m_metronome, m_grid.bpm, m_grid.firstBeatSec, m_grid.downbeatSec);
    }
}

void MixHouseGridEditorModel::setLoopBeats(int beats)
{
    beats = std::clamp(beats, 1, 64);
    if (beats == m_loopBeats) {
        return;
    }
    m_loopBeats = beats;
    if (m_loopActive && m_deck) {
        m_loopEnd = m_loopStart + beats * beatSec();
        m_deck->SetLoop(true, m_loopStart, m_loopEnd);
    }
    emit loopChanged();
}

void MixHouseGridEditorModel::setPxPerSec(double pxPerSec)
{
    pxPerSec = std::clamp(pxPerSec, 8.0, 40000.0);   // deep enough to put a beat on the start of a kick
    if (std::abs(pxPerSec - m_pxPerSec) > 1e-9) {
        m_pxPerSec = pxPerSec;
        emit zoomChanged();
    }
}

void MixHouseGridEditorModel::zoomBy(double factor)
{
    setPxPerSec(m_pxPerSec * factor);
}

// ---- grid helpers

double MixHouseGridEditorModel::nearestBeat(double seconds) const
{
    const double beat = beatSec();
    return m_grid.firstBeatSec + std::round((seconds - m_grid.firstBeatSec) / beat) * beat;
}

double MixHouseGridEditorModel::nearestBar(double seconds) const
{
    const double bar = 4.0 * beatSec();
    return m_grid.downbeatSec + std::round((seconds - m_grid.downbeatSec) / bar) * bar;
}

double MixHouseGridEditorModel::barAt(double seconds) const
{
    return (seconds - m_grid.downbeatSec) / (4.0 * beatSec()) + 1.0;
}

double MixHouseGridEditorModel::quantized(double seconds) const
{
    const double t = quantize() && m_grid.bpm > 0.0 ? nearestBeat(seconds) : seconds;
    return std::clamp(t, 0.0, durationSec());
}

// ---- transport

void MixHouseGridEditorModel::playPause()
{
    if (!m_deck) {
        return;
    }
    if (m_cuePreview) {
        m_cuePreview = false;   // CUE held + PLAY: keeps playing when CUE is let go
        return;
    }
    m_deck->SetPlaying(!m_deck->IsPlaying());
    tick();
}

void MixHouseGridEditorModel::cuePressed()
{
    if (!m_deck) {
        return;
    }
    // A CDJ's CUE: playing -> back to the cue and stop; stopped on the cue ->
    // play while held; stopped elsewhere -> the cue moves here.
    if (m_deck->IsPlaying()) {
        m_deck->SetPlaying(false);
        m_deck->Seek(m_cue);
    } else if (std::abs(m_position - m_cue) < 0.002) {
        m_cuePreview = true;
        m_deck->SetPlaying(true);
    } else {
        m_cue = quantized(m_position);
        m_deck->Seek(m_cue);
        emit cueChanged();
    }
    tick();
}

void MixHouseGridEditorModel::cueReleased()
{
    if (m_deck && m_cuePreview) {
        m_cuePreview = false;
        m_deck->SetPlaying(false);
        m_deck->Seek(m_cue);
        tick();
    }
}

void MixHouseGridEditorModel::seek(double seconds)
{
    if (!m_deck) {
        return;
    }
    const double t = quantized(seconds);
    m_deck->Seek(t);
    m_position = t;
    emit positionChanged();
}

void MixHouseGridEditorModel::seekFraction(double fraction)
{
    seek(std::clamp(fraction, 0.0, 1.0) * durationSec());
}

void MixHouseGridEditorModel::scratchBegin()
{
    if (!m_deck || m_scratching) {
        return;
    }
    m_scratching = true;
    m_deck->BeginSlipAction();
    m_deck->SetScrubbing(true);
}

void MixHouseGridEditorModel::scratchTo(double seconds)
{
    if (!m_deck || !m_scratching) {
        return;
    }
    const double t = std::clamp(seconds, 0.0, durationSec());
    m_deck->Seek(t);
    m_position = t;
    emit positionChanged();
}

void MixHouseGridEditorModel::scratchEnd()
{
    if (!m_deck || !m_scratching) {
        return;
    }
    m_scratching = false;
    m_deck->SetScrubbing(false);
    m_deck->EndSlipAction();
    tick();
}

void MixHouseGridEditorModel::toggleLoop()
{
    if (!m_deck) {
        return;
    }
    if (m_loopActive) {
        m_loopActive = false;
        m_deck->SetLoop(false, 0.0, 0.0);
    } else {
        const double beat = beatSec();
        m_loopStart = quantize()
                      ? m_grid.firstBeatSec + std::floor((m_position - m_grid.firstBeatSec) / beat + 1e-6) * beat
                      : m_position;
        m_loopStart = std::max(0.0, m_loopStart);
        m_loopEnd = m_loopStart + m_loopBeats * beat;
        m_loopActive = true;
        m_deck->SetLoop(true, m_loopStart, m_loopEnd);
    }
    emit loopChanged();
}

void MixHouseGridEditorModel::halveLoop()
{
    setLoopBeats(std::max(1, m_loopBeats / 2));
}

void MixHouseGridEditorModel::doubleLoop()
{
    setLoopBeats(m_loopBeats * 2);
}

void MixHouseGridEditorModel::jump(int direction)
{
    if (!m_deck) {
        return;
    }
    const double distance = (direction < 0 ? -1.0 : 1.0) * m_loopBeats * beatSec();
    if (m_loopActive) {
        m_loopStart += distance;
        m_loopEnd += distance;
        m_deck->SetLoop(true, m_loopStart, m_loopEnd);
        emit loopChanged();
    }
    const double t = std::clamp(m_position + distance, 0.0, durationSec());
    m_deck->Seek(t);   // a whole number of beats: the phase stays
    m_position = t;
    emit positionChanged();
}

void MixHouseGridEditorModel::jumpToPhrase(int direction)
{
    if (m_phrases.empty()) {
        return;
    }
    const double margin = 0.25 * beatSec();
    double target = -1.0;
    if (direction > 0) {
        for (const MixHousePhrase& p : m_phrases) {
            if (p.startSec > m_position + margin) {
                target = p.startSec;
                break;
            }
        }
    } else {
        for (auto it = m_phrases.rbegin(); it != m_phrases.rend(); ++it) {
            if (it->startSec < m_position - margin) {
                target = it->startSec;
                break;
            }
        }
    }
    if (target >= 0.0 && m_deck) {
        m_deck->Seek(target);
        m_position = target;
        emit positionChanged();
    }
}

// ---- GRID EDIT

MixHouseGridEditorModel::Grid MixHouseGridEditorModel::withBpm(double bpm) const
{
    Grid grid = m_grid;
    bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
    const double oldBeat = beatSec();
    const double newBeat = 60.0 / bpm;
    // The beat under the playhead stays where it is; the grid stretches around it.
    const double anchor = nearestBeat(m_position);
    grid.bpm = bpm;
    grid.firstBeatSec = anchor;
    const double beatsToBarOne = std::round((m_grid.downbeatSec - anchor) / oldBeat);
    grid.downbeatSec = anchor + beatsToBarOne * newBeat;
    return grid;
}

void MixHouseGridEditorModel::applyGrid(const Grid& proposed, const QString& what)
{
    if (!loaded() && !m_loading) {
        return;
    }
    if (m_locked) {
        setStatus(tr("The grid is locked — unlock it to edit"));
        return;
    }
    Grid grid = proposed;
    const double beat = 60.0 / grid.bpm;
    const double bar = 4.0 * beat;
    grid.firstBeatSec -= std::floor(grid.firstBeatSec / beat) * beat;
    while (grid.downbeatSec < 0.0) {
        grid.downbeatSec += bar;
    }
    pushUndo();
    m_grid = grid;
    if (library() && !library()->setTrackBeatGrid(m_trackId, grid.bpm, grid.firstBeatSec, grid.downbeatSec,
                                                  QStringLiteral("manual"))) {
        setStatus(tr("Could not save the grid"));
    } else {
        setStatus(what);
    }
    m_gridSource = tr("Edited");
    updateDeckGrid();
    if (m_loopActive && m_deck) {
        m_loopEnd = m_loopStart + m_loopBeats * beat;
        m_deck->SetLoop(true, m_loopStart, m_loopEnd);
        emit loopChanged();
    }
    emit gridChanged();
    emit positionChanged();   // bar.beat text
}

void MixHouseGridEditorModel::shiftGrid(double milliseconds)
{
    Grid grid = m_grid;
    grid.firstBeatSec += milliseconds / 1000.0;
    grid.downbeatSec += milliseconds / 1000.0;
    applyGrid(grid, tr("Grid moved %1 ms %2").arg(std::abs(milliseconds), 0, 'f', 0)
              .arg(milliseconds < 0 ? tr("earlier") : tr("later")));
}

void MixHouseGridEditorModel::setBarOneHere()
{
    // The playhead becomes beat 1 of a bar; bar numbers stay near where they were.
    Grid grid = m_grid;
    const double bar = 4.0 * beatSec();
    grid.firstBeatSec = m_position;
    grid.downbeatSec = m_position + std::round((m_grid.downbeatSec - m_position) / bar) * bar;
    applyGrid(grid, tr("Beat 1 of the bar set at the playhead"));
}

void MixHouseGridEditorModel::setBeatHere()
{
    const double delta = m_position - nearestBeat(m_position);
    Grid grid = m_grid;
    grid.firstBeatSec += delta;
    grid.downbeatSec += delta;
    applyGrid(grid, tr("Nearest beat moved to the playhead"));
}

void MixHouseGridEditorModel::adjustBpm(double delta)
{
    applyGrid(withBpm(m_grid.bpm + delta), tr("BPM %1").arg(std::clamp(m_grid.bpm + delta, kMinBpm, kMaxBpm), 0, 'f', 2));
}

void MixHouseGridEditorModel::setBpmValue(double bpm)
{
    if (bpm <= 0.0 || std::abs(bpm - m_grid.bpm) < 1e-6) {
        return;
    }
    applyGrid(withBpm(bpm), tr("BPM %1").arg(std::clamp(bpm, kMinBpm, kMaxBpm), 0, 'f', 2));
}

void MixHouseGridEditorModel::scaleBpm(double factor)
{
    const double bpm = std::clamp(m_grid.bpm * factor, kMinBpm, kMaxBpm);
    Grid grid = m_grid;
    const double anchor = nearestBeat(m_position);
    const double newBeat = 60.0 / bpm;
    grid.bpm = bpm;
    grid.firstBeatSec = anchor;
    // ×2 keeps every beat; ×½ keeps every other one: bar 1 goes to the nearest beat left.
    grid.downbeatSec = anchor + std::round((m_grid.downbeatSec - anchor) / newBeat) * newBeat;
    applyGrid(grid, tr("BPM %1").arg(bpm, 0, 'f', 2));
}

void MixHouseGridEditorModel::tap()
{
    const double now = monotonicSeconds();
    if (!m_taps.empty() && now - m_taps.back() > 2.0) {
        m_taps.clear();
    }
    m_taps.push_back(now);
    if (m_taps.size() > 8) {
        m_taps.erase(m_taps.begin());
    }
    if (m_taps.size() < 4) {
        setStatus(tr("Tap %1 more").arg(4 - static_cast<int>(m_taps.size())));
        return;
    }
    const double interval = (m_taps.back() - m_taps.front()) / double(m_taps.size() - 1);
    const double bpm = std::round(60.0 / interval * 100.0) / 100.0;
    applyGrid(withBpm(bpm), tr("Tapped BPM %1").arg(bpm, 0, 'f', 2));
}

void MixHouseGridEditorModel::toggleGridLock()
{
    if (!library() || m_trackId < 0) {
        return;
    }
    if (library()->setTrackBpmLocked(m_trackId, !m_locked)) {
        m_locked = !m_locked;
        setStatus(m_locked ? tr("Grid locked") : tr("Grid unlocked"));
        emit gridChanged();
    }
}

void MixHouseGridEditorModel::resetGridToRekordbox()
{
    if (!rekordboxPhrases() || m_locked) {
        return;
    }
    const MixHouseRekordboxGrid rb = rekordboxPhrases()->beatGrid(m_filePath, m_grid.bpm);
    if (!rb.valid) {
        setStatus(tr("the imported analysis has no constant grid for this track"));
        return;
    }
    const double bar = 4.0 * 60.0 / rb.bpm;
    Grid grid;
    grid.bpm = rb.bpm;
    grid.firstBeatSec = rb.firstBeatSec;
    grid.downbeatSec = rb.firstDownbeatSec
                       + std::max(0.0, std::round((m_grid.downbeatSec - rb.firstDownbeatSec) / bar)) * bar;
    applyGrid(grid, tr("Grid back to the imported one"));
    if (library()) {
        library()->setTrackBeatGrid(m_trackId, grid.bpm, m_grid.firstBeatSec, m_grid.downbeatSec,
                                    QStringLiteral("rekordbox"));
    }
    m_gridSource = tr("imported grid");
}

// ---- PHRASE EDIT

QStringList MixHouseGridEditorModel::phraseLabels() const
{
    return mixHousePhraseLabels();
}

QVariantList MixHouseGridEditorModel::phrasesVariant() const
{
    QVariantList list;
    for (const MixHousePhrase& p : m_phrases) {
        QVariantMap item;
        item.insert(QStringLiteral("start"), p.startSec);
        item.insert(QStringLiteral("end"), p.endSec);
        item.insert(QStringLiteral("fill"), p.fillStartSec);
        item.insert(QStringLiteral("label"), p.label);
        item.insert(QStringLiteral("color"), p.color);
        list.append(item);
    }
    return list;
}

void MixHouseGridEditorModel::setSelectedPhrase(int index)
{
    index = index >= 0 && index < static_cast<int>(m_phrases.size()) ? index : -1;
    if (index != m_selectedPhrase) {
        m_selectedPhrase = index;
        emit phrasesChanged();
    }
}

int MixHouseGridEditorModel::phraseIndexAt(double seconds) const
{
    for (size_t i = 0; i < m_phrases.size(); ++i) {
        if (seconds >= m_phrases[i].startSec && seconds < m_phrases[i].endSec) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MixHouseGridEditorModel::selectPhraseAt(double seconds)
{
    setSelectedPhrase(phraseIndexAt(seconds));
}

void MixHouseGridEditorModel::applyPhrases(std::vector<MixHousePhrase> phrases, const QString& what)
{
    if (m_trackId < 0) {
        return;
    }
    pushUndo();
    m_phrases = std::move(phrases);
    m_phrasesEdited = true;
    if (library()) {
        library()->setTrackPhraseEdits(m_trackId, &m_phrases);
    }
    if (m_selectedPhrase >= static_cast<int>(m_phrases.size())) {
        m_selectedPhrase = static_cast<int>(m_phrases.size()) - 1;
    }
    setStatus(what);
    emit phrasesChanged();
}

void MixHouseGridEditorModel::cutPhrase()
{
    if (!loaded()) {
        return;
    }
    // Phrases start on bars: with quantize on the cut goes to the nearest one.
    const double t = std::clamp(quantize() ? nearestBar(m_position) : m_position, 0.0, durationSec());
    std::vector<MixHousePhrase> phrases = m_phrases;
    if (phrases.empty()) {
        if (t <= beatSec()) {
            return;
        }
        MixHousePhrase first;
        first.startSec = 0.0;
        first.endSec = t;
        first.label = QStringLiteral("Intro 1");
        first.color = mixHousePhraseColor(first.label);
        MixHousePhrase second;
        second.startSec = t;
        second.endSec = durationSec();
        second.label = QStringLiteral("Up 1");
        second.color = mixHousePhraseColor(second.label);
        phrases = { first, second };
        m_selectedPhrase = 1;
        applyPhrases(std::move(phrases), tr("Phrase cut at bar %1").arg(std::lround(barAt(t))));
        return;
    }
    const int i = phraseIndexAt(t);
    if (i < 0) {
        return;
    }
    const MixHousePhrase whole = phrases[static_cast<size_t>(i)];
    if (t - whole.startSec < beatSec() * 0.99 || whole.endSec - t < beatSec() * 0.99) {
        setStatus(tr("Too close to the phrase's edge to cut"));
        return;
    }
    MixHousePhrase before = whole;
    before.endSec = t;
    before.fillStartSec = -1.0;
    MixHousePhrase after = whole;
    after.startSec = t;
    after.fillStartSec = whole.fillStartSec >= t ? whole.fillStartSec : -1.0;
    phrases[static_cast<size_t>(i)] = before;
    phrases.insert(phrases.begin() + i + 1, after);
    m_selectedPhrase = i + 1;
    applyPhrases(std::move(phrases), tr("Phrase cut at bar %1").arg(std::lround(barAt(t))));
}

void MixHouseGridEditorModel::setPhraseLabel(const QString& label)
{
    if (m_selectedPhrase < 0 || label.isEmpty() || m_phrases[static_cast<size_t>(m_selectedPhrase)].label == label) {
        return;
    }
    std::vector<MixHousePhrase> phrases = m_phrases;
    phrases[static_cast<size_t>(m_selectedPhrase)].label = label;
    phrases[static_cast<size_t>(m_selectedPhrase)].color = mixHousePhraseColor(label);
    applyPhrases(std::move(phrases), tr("Phrase is now %1").arg(label));
}

void MixHouseGridEditorModel::deletePhrase()
{
    if (m_selectedPhrase < 0) {
        return;
    }
    std::vector<MixHousePhrase> phrases = m_phrases;
    const size_t i = static_cast<size_t>(m_selectedPhrase);
    if (phrases.size() == 1) {
        phrases.clear();
        m_selectedPhrase = -1;
    } else if (i > 0) {
        phrases[i - 1].endSec = phrases[i].endSec;
        phrases[i - 1].fillStartSec = phrases[i].fillStartSec;
        phrases.erase(phrases.begin() + static_cast<long>(i));
        m_selectedPhrase = static_cast<int>(i) - 1;
    } else {
        phrases[1].startSec = phrases[0].startSec;
        phrases.erase(phrases.begin());
        m_selectedPhrase = 0;
    }
    applyPhrases(std::move(phrases), tr("Phrase merged into its neighbour"));
}

void MixHouseGridEditorModel::toggleFill()
{
    if (m_selectedPhrase < 0) {
        return;
    }
    std::vector<MixHousePhrase> phrases = m_phrases;
    MixHousePhrase& p = phrases[static_cast<size_t>(m_selectedPhrase)];
    if (p.fillStartSec >= 0.0) {
        p.fillStartSec = -1.0;
    } else {
        p.fillStartSec = std::max(p.startSec, p.endSec - 4.0 * beatSec());   // the last bar
    }
    const bool on = p.fillStartSec >= 0.0;
    applyPhrases(std::move(phrases), on ? tr("Fill-in on the phrase's last bar") : tr("Fill-in removed"));
}

void MixHouseGridEditorModel::setPhraseName(const QString& name)
{
    const QString trimmed = name.trimmed();
    if (m_selectedPhrase < 0 || trimmed.isEmpty() || m_phrases[static_cast<size_t>(m_selectedPhrase)].label == trimmed) {
        return;
    }
    std::vector<MixHousePhrase> phrases = m_phrases;
    phrases[static_cast<size_t>(m_selectedPhrase)].label = trimmed;
    applyPhrases(std::move(phrases), tr("Phrase renamed to %1").arg(trimmed));
}

void MixHouseGridEditorModel::setPhraseColor(const QColor& color)
{
    if (m_selectedPhrase < 0 || !color.isValid() || m_phrases[static_cast<size_t>(m_selectedPhrase)].color == color) {
        return;
    }
    std::vector<MixHousePhrase> phrases = m_phrases;
    phrases[static_cast<size_t>(m_selectedPhrase)].color = color;
    applyPhrases(std::move(phrases), tr("Phrase colour changed"));
}

QVariantList MixHouseGridEditorModel::colorPalette() const
{
    QVariantList palette;
    if (!projectSceneConfiguration()) {
        return palette;
    }
    for (int index = 1; index <= 96; ++index) {
        palette.push_back(QVariantMap { { "index", index },
                                        { "color", projectSceneConfiguration()->clipColor(index).toQColor() } });
    }
    return palette;
}

void MixHouseGridEditorModel::setPhraseColorIndex(int index)
{
    if (projectSceneConfiguration() && index >= 1 && index <= 96) {
        setPhraseColor(projectSceneConfiguration()->clipColor(index).toQColor());
    }
}

QColor MixHouseGridEditorModel::bodyColor() const
{
    if (m_trackColor.isValid()) {
        return m_trackColor;
    }
    return QColor(0xFF, 0x80, 0xB8);   // the timeline's default clip pink
}

void MixHouseGridEditorModel::beginPhraseDrag()
{
    if (m_phraseDragging || m_trackId < 0) {
        return;
    }
    m_phraseDragging = true;
    m_dragOriginal = m_phrases;
    pushUndo();
}

void MixHouseGridEditorModel::dragPhraseEdge(int index, int edge, double seconds)
{
    if (!m_phraseDragging || index < 0 || index >= static_cast<int>(m_dragOriginal.size())) {
        return;
    }
    const size_t i = static_cast<size_t>(index);
    const double minLength = beatSec();
    double t = quantize() ? nearestBeat(seconds) : seconds;
    std::vector<MixHousePhrase> phrases = m_dragOriginal;
    if (edge == 0) {
        const double low = i > 0 ? m_dragOriginal[i - 1].startSec + minLength : 0.0;
        t = std::clamp(t, low, m_dragOriginal[i].endSec - minLength);
        phrases[i].startSec = t;
        if (i > 0) {
            phrases[i - 1].endSec = t;
            if (phrases[i - 1].fillStartSec >= t) {
                phrases[i - 1].fillStartSec = -1.0;
            }
        }
    } else {
        const double high = i + 1 < phrases.size() ? m_dragOriginal[i + 1].endSec - minLength : durationSec();
        t = std::clamp(t, m_dragOriginal[i].startSec + minLength, high);
        phrases[i].endSec = t;
        if (i + 1 < phrases.size()) {
            phrases[i + 1].startSec = t;
        }
    }
    if (phrases[i].fillStartSec >= 0.0) {
        phrases[i].fillStartSec = std::clamp(phrases[i].fillStartSec, phrases[i].startSec, phrases[i].endSec);
    }
    m_phrases = std::move(phrases);
    m_selectedPhrase = index;
    emit phrasesChanged();
}

void MixHouseGridEditorModel::dragPhrase(int index, double delta)
{
    if (!m_phraseDragging || index < 0 || index >= static_cast<int>(m_dragOriginal.size())) {
        return;
    }
    const size_t i = static_cast<size_t>(index);
    const double minLength = beatSec();
    if (quantize()) {
        delta = std::round(delta / beatSec()) * beatSec();
    }
    const MixHousePhrase& original = m_dragOriginal[i];
    const double length = original.endSec - original.startSec;
    // Neighbours give way but keep at least a beat.
    const double low = i > 0 ? m_dragOriginal[i - 1].startSec + minLength : 0.0;
    const double high = (i + 1 < m_dragOriginal.size() ? m_dragOriginal[i + 1].endSec - minLength : durationSec()) - length;
    const double start = std::clamp(original.startSec + delta, low, std::max(low, high));
    std::vector<MixHousePhrase> phrases = m_dragOriginal;
    const double moved = start - original.startSec;
    phrases[i].startSec = start;
    phrases[i].endSec = start + length;
    if (phrases[i].fillStartSec >= 0.0) {
        phrases[i].fillStartSec += moved;
    }
    if (i > 0) {
        phrases[i - 1].endSec = start;
        if (phrases[i - 1].fillStartSec >= start) {
            phrases[i - 1].fillStartSec = -1.0;
        }
    }
    if (i + 1 < phrases.size()) {
        phrases[i + 1].startSec = start + length;
        if (phrases[i + 1].fillStartSec >= 0.0 && phrases[i + 1].fillStartSec < phrases[i + 1].startSec) {
            phrases[i + 1].fillStartSec = -1.0;
        }
    }
    m_phrases = std::move(phrases);
    m_selectedPhrase = index;
    emit phrasesChanged();
}

void MixHouseGridEditorModel::endPhraseDrag()
{
    if (!m_phraseDragging) {
        return;
    }
    m_phraseDragging = false;
    const bool changed = m_dragOriginal.size() != m_phrases.size()
                         || !std::equal(m_phrases.begin(), m_phrases.end(), m_dragOriginal.begin(),
                                        [](const MixHousePhrase& a, const MixHousePhrase& b) {
        return a.startSec == b.startSec && a.endSec == b.endSec;
    });
    if (!changed) {
        m_undo.pop_back();   // a click, not a drag
        emit historyChanged();
        return;
    }
    m_phrasesEdited = true;
    if (library()) {
        library()->setTrackPhraseEdits(m_trackId, &m_phrases);
    }
    setStatus(tr("Phrase moved"));
    emit phrasesChanged();
}

void MixHouseGridEditorModel::dragLoopEdge(int edge, double seconds)
{
    if (!m_loopActive || !m_deck) {
        return;
    }
    const double t = std::clamp(quantize() ? nearestBeat(seconds) : seconds, 0.0, durationSec());
    const double minLength = quantize() ? beatSec() : 0.01;
    if (edge == 0) {
        m_loopStart = std::min(t, m_loopEnd - minLength);
    } else {
        m_loopEnd = std::max(t, m_loopStart + minLength);
    }
    m_deck->SetLoop(true, m_loopStart, m_loopEnd);
    emit loopChanged();
}

// rekordbox's phrases, or for a track it never analysed, GlideMix's own.
std::vector<MixHousePhrase> MixHouseGridEditorModel::unEditedPhrases() const
{
    std::vector<MixHousePhrase> phrases = rekordboxPhrases() ? rekordboxPhrases()->phrases(m_filePath, m_grid.bpm)
                                                             : std::vector<MixHousePhrase>();
    if (phrases.empty() && library() && m_trackId >= 0) {
        phrases = library()->detectedPhrases(m_trackId);
    }
    return phrases;
}

void MixHouseGridEditorModel::resetPhrases()
{
    if (m_trackId < 0 || !m_phrasesEdited) {
        return;
    }
    pushUndo();
    if (library()) {
        library()->setTrackPhraseEdits(m_trackId, nullptr);
    }
    m_phrases = unEditedPhrases();
    m_phrasesEdited = false;
    m_selectedPhrase = -1;
    setStatus(tr("Phrases back to the imported ones"));
    emit phrasesChanged();
}

// ---- undo

void MixHouseGridEditorModel::pushUndo()
{
    m_undo.push_back({ m_grid, m_phrases, m_phrasesEdited });
    if (m_undo.size() > kMaxUndo) {
        m_undo.erase(m_undo.begin());
    }
    m_redo.clear();
    emit historyChanged();
}

void MixHouseGridEditorModel::restore(const Snapshot& snapshot)
{
    const bool gridChangedNow = snapshot.grid.bpm != m_grid.bpm || snapshot.grid.firstBeatSec != m_grid.firstBeatSec
                                || snapshot.grid.downbeatSec != m_grid.downbeatSec;
    m_grid = snapshot.grid;
    m_phrases = snapshot.phrases;
    m_phrasesEdited = snapshot.phrasesEdited;
    if (library() && m_trackId >= 0) {
        if (gridChangedNow && !m_locked) {
            library()->setTrackBeatGrid(m_trackId, m_grid.bpm, m_grid.firstBeatSec, m_grid.downbeatSec,
                                        QStringLiteral("manual"));
        }
        library()->setTrackPhraseEdits(m_trackId, m_phrasesEdited ? &m_phrases : nullptr);
    }
    if (m_selectedPhrase >= static_cast<int>(m_phrases.size())) {
        m_selectedPhrase = -1;
    }
    updateDeckGrid();
    emit gridChanged();
    emit phrasesChanged();
    emit positionChanged();
}

void MixHouseGridEditorModel::undo()
{
    if (m_undo.empty()) {
        return;
    }
    m_redo.push_back({ m_grid, m_phrases, m_phrasesEdited });
    const Snapshot snapshot = m_undo.back();
    m_undo.pop_back();
    restore(snapshot);
    setStatus(tr("Undone"));
    emit historyChanged();
}

void MixHouseGridEditorModel::redo()
{
    if (m_redo.empty()) {
        return;
    }
    m_undo.push_back({ m_grid, m_phrases, m_phrasesEdited });
    const Snapshot snapshot = m_redo.back();
    m_redo.pop_back();
    restore(snapshot);
    setStatus(tr("Redone"));
    emit historyChanged();
}
