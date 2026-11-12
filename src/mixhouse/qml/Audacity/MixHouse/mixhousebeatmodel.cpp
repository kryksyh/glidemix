/*
 * GlideMix Samples B8: the Beat window's model. See mixhousebeatmodel.h.
 */
#include "mixhousebeatmodel.h"

#include <QDir>
#include <QMap>
#include <QFile>
#include <QRegularExpression>
#include <optional>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <random>

#include "global/settings.h"
#include "log.h"
#include "translation.h"
#include "project/iaudacityproject.h"
#include "mixhouse/internal/mixhousepreviewplayer.h"
#include "mixhouse/internal/mixhouseprojectaligner.h"
#include "mixhousesamplesmodel.h"
#include "mixhousetracklistmodel.h"
#include "mixhouse/mixhousesamplename.h"

using namespace au::mixhouse;

BeatPattern MixHouseBeatModel::s_pattern;
BeatRow MixHouseBeatModel::s_clipboard;
bool MixHouseBeatModel::s_hasClipboard = false;
bool MixHouseBeatModel::s_followMix = true;
double MixHouseBeatModel::s_ownBpm = 124.0;
QString MixHouseBeatModel::s_genre;

MixHouseBeatModel::MixHouseBeatModel(QObject* parent)
    : QObject(parent), muse::Injectable(muse::iocCtxForQmlObject(this))
{
    s_live.push_back(this);
    // Sounds sent from a clip's right-click ("Send to Beat").
    m_queuePoll.setInterval(300);
    connect(&m_queuePoll, &QTimer::timeout, this, []() {
        for (const QString& path : takeQueuedBeatSamples()) {
            addSampleRow(path);
        }
    });
    m_queuePoll.start();
    QTimer::singleShot(0, this, []() {
        for (const QString& path : takeQueuedBeatSamples()) {
            addSampleRow(path);
        }
    });
    // The running light follows the preview; the loop starts over at its end.
    m_poll.setInterval(30);
    connect(&m_poll, &QTimer::timeout, this, [this]() {
        if (!m_player || !m_playing) {
            return;
        }
        if (!m_player->isPlaying()) {
            m_player->playFrom(0.0);
        }
        const int steps = totalSteps();
        const int step = std::clamp(int(m_player->position() * steps), 0, steps - 1);
        if (step != m_playStep) {
            m_playStep = step;
            emit playStepChanged();
        }
        // The mix tempo moved (the playhead is elsewhere): render again.
        if (std::abs(bpm() - m_renderedBpm) > 0.05) {
            m_rerender.start();
        }
    });
    // A click while it plays: render the loop again a moment later and go on
    // from the same place.
    m_rerender.setSingleShot(true);
    m_rerender.setInterval(120);
    connect(&m_rerender, &QTimer::timeout, this, [this]() {
        if (m_playing) {
            const double at = m_player ? m_player->position() : 0.0;
            if (renderPreview()) {
                m_player->playFrom(at);
            }
        }
    });
}

MixHouseBeatModel::~MixHouseBeatModel()
{
    s_live.erase(std::remove(s_live.begin(), s_live.end(), this), s_live.end());
    stop();
}

std::vector<MixHouseBeatModel*> MixHouseBeatModel::s_live;

bool MixHouseBeatModel::addSampleRow(const QString& path)
{
    if (!QFileInfo(path).isFile() || int(s_pattern.rows.size()) >= kBeatMaxRows) {
        return false;
    }
    BeatRow r;
    r.path = path;
    r.steps.assign(size_t(std::max(1, s_pattern.bars) * std::max(1, s_pattern.stepsPerBar)), 0);
    s_pattern.rows.push_back(r);
    for (MixHouseBeatModel* model : s_live) {
        model->changed();
    }
    return true;
}

namespace {
//! Every per-step list as long as the pattern, missing values at their defaults.
void normalize(BeatRow& r, int n)
{
    r.steps.resize(size_t(n), 0);
    r.velocity.resize(size_t(n), kBeatDefaultVelocity);
    r.pitch.resize(size_t(n), 0.0);
    r.stepPan.resize(size_t(n), 0.0);
    r.shift.resize(size_t(n), 0.0);
    r.repeat.resize(size_t(n), 1);
    r.gate.resize(size_t(n), 1.0);
    r.reverse.resize(size_t(n), 0.0);
    r.drive.resize(size_t(n), 0.0);
    r.filter.resize(size_t(n), 1.0);
    r.reverb.resize(size_t(n), 0.0);
}

//! Moves the steps and their graph values together: new step t takes old step from(t).
template<typename F>
void remap(BeatRow& r, int oldN, int newN, F from)
{
    normalize(r, oldN);
    const BeatRow old = r;
    normalize(r, newN);
    for (int t = 0; t < newN; ++t) {
        const size_t o = size_t(from(t));
        r.steps[size_t(t)] = old.steps[o];
        r.velocity[size_t(t)] = old.velocity[o];
        r.pitch[size_t(t)] = old.pitch[o];
        r.stepPan[size_t(t)] = old.stepPan[o];
        r.shift[size_t(t)] = old.shift[o];
        r.repeat[size_t(t)] = old.repeat[o];
        r.gate[size_t(t)] = old.gate[o];
        r.reverse[size_t(t)] = old.reverse[o];
        r.drive[size_t(t)] = old.drive[o];
        r.filter[size_t(t)] = old.filter[o];
        r.reverb[size_t(t)] = old.reverb[o];
    }
}

QVariantList listOf(const std::vector<double>& values, int n, double fallback)
{
    QVariantList out;
    for (int s = 0; s < n; ++s) {
        out << beatStepValue(values, s, fallback);
    }
    return out;
}
}

QVariantList MixHouseBeatModel::rows() const
{
    const int n = totalSteps();
    QVariantList out;
    for (const BeatRow& r : s_pattern.rows) {
        QVariantList steps, repeat;
        for (int s = 0; s < n; ++s) {
            steps << (s < int(r.steps.size()) ? r.steps[size_t(s)] : 0);
            repeat << beatStepRepeat(r, s);
        }
        out << QVariantMap {
            { QStringLiteral("name"), QFileInfo(r.path).completeBaseName() },
            { QStringLiteral("path"), r.path },
            { QStringLiteral("mute"), r.mute },
            { QStringLiteral("solo"), r.solo },
            { QStringLiteral("gain"), r.gain },
            { QStringLiteral("pan"), r.pan },
            { QStringLiteral("cutSelf"), r.cutSelf },
            { QStringLiteral("swingMix"), r.swingMix },
            { QStringLiteral("role"), r.role },
            { QStringLiteral("locked"), r.locked },
            { QStringLiteral("length"), r.length > 0 ? r.length : n },
            { QStringLiteral("choke"), r.choke },
            { QStringLiteral("role"), r.role },
            { QStringLiteral("attack"), r.attack },
            { QStringLiteral("release"), r.release },
            { QStringLiteral("keyShift"), r.keyShift },
            { QStringLiteral("steps"), steps },
            { QStringLiteral("velocity"), listOf(r.velocity, n, kBeatDefaultVelocity) },
            { QStringLiteral("pitch"), listOf(r.pitch, n, 0.0) },
            { QStringLiteral("stepPan"), listOf(r.stepPan, n, 0.0) },
            { QStringLiteral("shift"), listOf(r.shift, n, 0.0) },
            { QStringLiteral("repeat"), repeat },
            { QStringLiteral("gate"), listOf(r.gate, n, 1.0) },
            { QStringLiteral("reverse"), listOf(r.reverse, n, 0.0) },
            { QStringLiteral("drive"), listOf(r.drive, n, 0.0) },
            { QStringLiteral("filter"), listOf(r.filter, n, 1.0) },
            { QStringLiteral("reverb"), listOf(r.reverb, n, 0.0) },
        };
    }
    return out;
}

BeatRow* MixHouseBeatModel::row(int index)
{
    return index >= 0 && index < int(s_pattern.rows.size()) ? &s_pattern.rows[size_t(index)] : nullptr;
}

void MixHouseBeatModel::changed()
{
    emit patternChanged();
    if (m_playing) {
        m_rerender.start();
    }
}

void MixHouseBeatModel::setBars(int bars)
{
    bars = bars <= 1 ? 1 : bars <= 2 ? 2 : bars <= 4 ? 4 : bars <= 8 ? 8 : 16;
    if (bars == s_pattern.bars) {
        return;
    }
    // Longer: the bars so far repeat; shorter: the first bars stay.
    const int oldSteps = totalSteps();
    const int newSteps = bars * s_pattern.stepsPerBar;
    for (BeatRow& r : s_pattern.rows) {
        remap(r, oldSteps, newSteps, [&](int t) { return t % oldSteps; });
        if (r.length >= newSteps) {
            r.length = 0;   // a cycle as long as the pattern is no cycle of its own
        }
    }
    s_pattern.bars = bars;
    changed();
}

void MixHouseBeatModel::setSwing(double swing)
{
    s_pattern.swing = std::clamp(swing, 0.0, 1.0);
    changed();
}

double MixHouseBeatModel::bpm() const
{
    if (!s_followMix) {
        return s_ownBpm;
    }
    const auto state = globalContext() ? globalContext()->playbackState() : nullptr;
    const double at = state ? state->playbackPosition().to_double() : 0.0;
    double tempo = tempoMap() ? tempoMap()->bpmAt(at) : 0.0;
    if (tempo <= 0.0) {
        const auto prj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
        tempo = prj ? prj->timeSignature().tempo : 0.0;
    }
    return tempo > 0.0 ? tempo : 120.0;
}

void MixHouseBeatModel::setSampleLine(int id)
{
    if (id != m_sampleLine) {
        m_sampleLine = id;
        emit sampleLineChanged();
    }
}

void MixHouseBeatModel::setRowSample(int index, const QString& pathOrUrl)
{
    const QUrl url(pathOrUrl);
    const QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (!QFileInfo(path).isFile()) {
        return;
    }
    if (index >= int(s_pattern.rows.size())) {
        if (int(s_pattern.rows.size()) >= kBeatMaxRows) {
            return;
        }
        BeatRow r;
        r.steps.assign(size_t(totalSteps()), 0);
        s_pattern.rows.push_back(r);
        index = int(s_pattern.rows.size()) - 1;
    }
    row(index)->path = path;
    changed();
}

void MixHouseBeatModel::removeRow(int index)
{
    if (row(index)) {
        s_pattern.rows.erase(s_pattern.rows.begin() + index);
        changed();
    }
}

void MixHouseBeatModel::setStep(int index, int step, int level)
{
    BeatRow* r = row(index);
    if (!r || step < 0 || step >= totalSteps()) {
        return;
    }
    r->steps.resize(size_t(totalSteps()), 0);
    if (r->steps[size_t(step)] == level) {
        return;
    }
    r->steps[size_t(step)] = std::clamp(level, 0, 3);
    changed();
}

void MixHouseBeatModel::cycleLevel(int index, int step)
{
    BeatRow* r = row(index);
    const int n = totalSteps();
    if (!r || step < 0 || step >= n) {
        return;
    }
    normalize(*r, n);
    // Shift-click: normal (78 %) -> accent (100 %) -> soft (50 %) -> normal; the step goes on.
    double& v = r->velocity[size_t(step)];
    v = v >= 0.95 ? 0.5 : (v > 0.6 ? 1.0 : kBeatDefaultVelocity);
    r->steps[size_t(step)] = 2;
    changed();
}

void MixHouseBeatModel::setStepValue(int index, int step, const QString& what, double value)
{
    BeatRow* r = row(index);
    const int n = totalSteps();
    if (!r || step < 0 || step >= n) {
        return;
    }
    normalize(*r, n);
    const size_t s = size_t(step);
    if (what == QLatin1String("velocity")) {
        r->velocity[s] = std::clamp(value, 0.0, 1.0);
    } else if (what == QLatin1String("pitch")) {
        r->pitch[s] = std::clamp(std::round(value), -24.0, 24.0);
    } else if (what == QLatin1String("stepPan")) {
        r->stepPan[s] = std::clamp(value, -1.0, 1.0);
    } else if (what == QLatin1String("shift")) {
        r->shift[s] = std::clamp(value, 0.0, 1.0);
    } else if (what == QLatin1String("repeat")) {
        r->repeat[s] = std::clamp(int(std::lround(value)), 1, 4);
    } else if (what == QLatin1String("gate")) {
        r->gate[s] = std::clamp(value, 0.05, 1.0);
    } else if (what == QLatin1String("reverse")) {
        r->reverse[s] = value >= 0.5 ? 1.0 : 0.0;
    } else if (what == QLatin1String("drive")) {
        r->drive[s] = std::clamp(value, 0.0, 1.0);
    } else if (what == QLatin1String("filter")) {
        r->filter[s] = std::clamp(value, 0.0, 1.0);
    } else if (what == QLatin1String("reverb")) {
        r->reverb[s] = std::clamp(value, 0.0, 1.0);
    } else {
        return;
    }
    changed();
}

void MixHouseBeatModel::resetStepValue(int index, int step, const QString& what)
{
    const double fallback = what == QLatin1String("velocity") ? kBeatDefaultVelocity
                            : (what == QLatin1String("repeat") || what == QLatin1String("gate") || what == QLatin1String("filter")) ? 1.0
                            : 0.0;
    setStepValue(index, step, what, fallback);
}

void MixHouseBeatModel::toggleMute(int index)
{
    if (BeatRow* r = row(index)) {
        r->mute = !r->mute;
        changed();
    }
}

void MixHouseBeatModel::toggleSolo(int index)
{
    if (BeatRow* r = row(index)) {
        r->solo = !r->solo;
        changed();
    }
}

void MixHouseBeatModel::setGain(int index, double gain)
{
    if (BeatRow* r = row(index)) {
        r->gain = std::clamp(gain, 0.0, 1.0);
        changed();
    }
}

void MixHouseBeatModel::setPan(int index, double pan)
{
    if (BeatRow* r = row(index)) {
        r->pan = std::clamp(pan, -1.0, 1.0);
        changed();
    }
}

void MixHouseBeatModel::toggleCutSelf(int index)
{
    if (BeatRow* r = row(index)) {
        r->cutSelf = !r->cutSelf;
        changed();
    }
}

void MixHouseBeatModel::setSwingMix(int index, double mix)
{
    if (BeatRow* r = row(index)) {
        r->swingMix = std::clamp(mix, 0.0, 1.0);
        changed();
    }
}

void MixHouseBeatModel::setChoke(int index, int group)
{
    if (BeatRow* r = row(index)) {
        r->choke = std::clamp(group, 0, 4);
        changed();
    }
}

void MixHouseBeatModel::advancedFill(int index, int hits, int length, int rotate, bool euclidean)
{
    BeatRow* r = row(index);
    if (!r) {
        return;
    }
    const int n = totalSteps();
    length = std::clamp(length, 1, n);
    hits = std::clamp(hits, 0, length);
    normalize(*r, n);
    std::vector<bool> on(size_t(length), false);
    if (euclidean) {
        on = evenRhythm(hits, length);
    } else if (hits > 0) {
        const int every = std::max(1, length / hits);
        for (int i = 0; i < hits && i * every < length; ++i) {
            on[size_t(i * every)] = true;
        }
    }
    for (int s = 0; s < n; ++s) {
        r->steps[size_t(s)] = 0;
    }
    for (int s = 0; s < length; ++s) {
        const int from = ((s - rotate) % length + length) % length;
        r->steps[size_t(s)] = on[size_t(from)] ? 2 : 0;
    }
    r->length = length >= n ? 0 : length;
    changed();
}

int MixHouseBeatModel::totalSteps() const
{
    return s_pattern.bars * s_pattern.stepsPerBar;
}

void MixHouseBeatModel::fillIn(int index, int division, bool offbeat)
{
    BeatRow* r = row(index);
    if (!r || division <= 0) {
        return;
    }
    const int n = totalSteps();
    normalize(*r, n);
    r->length = 0;
    std::fill(r->steps.begin(), r->steps.end(), 0);
    // `division` hits a bar (4 = quarters ... 32 = 32nds), each on the grid step
    // nearest its time; offbeat: half way between them.
    const double spacing = double(s_pattern.stepsPerBar) / division;
    for (int k = 0; k < s_pattern.bars * division; ++k) {
        const int st = int(std::lround((k + (offbeat ? 0.5 : 0.0)) * spacing));
        if (st >= 0 && st < n) {
            r->steps[size_t(st)] = 2;
        }
    }
    changed();
}

void MixHouseBeatModel::quickFill(int index, const QString& kind)
{
    if (kind == QLatin1String("rnd")) {
        randomize(index);
    } else if (kind == QLatin1String("clear")) {
        clearRow(index);
    } else if (kind == QLatin1String("offbeat")) {
        fillIn(index, 4, true);
    } else if (kind.startsWith(QLatin1Char('q'))) {
        fillIn(index, kind.mid(1).toInt(), false);
    }
}

void MixHouseBeatModel::setStepsPerBar(int spb)
{
    spb = spb == 32 || spb == 24 ? spb : 16;
    if (spb == s_pattern.stepsPerBar) {
        return;
    }
    // Every step keeps its time: old step o lands on the new step nearest it.
    const int oldSpb = s_pattern.stepsPerBar;
    const int oldN = totalSteps();
    const int newN = s_pattern.bars * spb;
    for (BeatRow& r : s_pattern.rows) {
        normalize(r, oldN);
        const BeatRow old = r;
        normalize(r, newN);
        std::fill(r.steps.begin(), r.steps.end(), 0);
        for (int o = 0; o < oldN; ++o) {
            if (old.steps[size_t(o)] <= 0) {
                continue;
            }
            const int t = std::min(newN - 1, int(std::lround(double(o) * spb / oldSpb)));
            r.steps[size_t(t)] = old.steps[size_t(o)];
            r.velocity[size_t(t)] = old.velocity[size_t(o)];
            r.pitch[size_t(t)] = old.pitch[size_t(o)];
            r.stepPan[size_t(t)] = old.stepPan[size_t(o)];
            r.shift[size_t(t)] = old.shift[size_t(o)];
            r.repeat[size_t(t)] = old.repeat[size_t(o)];
            r.gate[size_t(t)] = old.gate[size_t(o)];
            r.reverse[size_t(t)] = old.reverse[size_t(o)];
            r.drive[size_t(t)] = old.drive[size_t(o)];
            r.filter[size_t(t)] = old.filter[size_t(o)];
            r.reverb[size_t(t)] = old.reverb[size_t(o)];
        }
        r.length = r.length > 0 ? std::max(1, int(std::lround(double(r.length) * spb / oldSpb))) : 0;
    }
    s_pattern.stepsPerBar = spb;
    changed();
}

void MixHouseBeatModel::setAttack(int index, double seconds)
{
    if (BeatRow* r = row(index)) {
        r->attack = std::clamp(seconds, 0.0, 2.0);
        changed();
    }
}

void MixHouseBeatModel::setRelease(int index, double seconds)
{
    if (BeatRow* r = row(index)) {
        r->release = std::clamp(seconds, 0.0, 2.0);
        changed();
    }
}

void MixHouseBeatModel::setKeyShift(int index, int semitones)
{
    if (BeatRow* r = row(index)) {
        r->keyShift = std::clamp(semitones, -24, 24);
        changed();
    }
}

void MixHouseBeatModel::copySteps(int index)
{
    if (const BeatRow* r = row(index)) {
        s_clipboard = *r;
        s_hasClipboard = true;
        emit patternChanged();
    }
}

void MixHouseBeatModel::pasteSteps(int index)
{
    BeatRow* r = row(index);
    if (!r || !s_hasClipboard) {
        return;
    }
    // The steps and their graph, not the sound or the row's settings.
    const int n = totalSteps();
    BeatRow src = s_clipboard;
    normalize(src, int(std::max(src.steps.size(), size_t(1))));
    normalize(*r, n);
    for (int t = 0; t < n; ++t) {
        const size_t o = size_t(t) % src.steps.size();
        r->steps[size_t(t)] = src.steps[o];
        r->velocity[size_t(t)] = src.velocity[o];
        r->pitch[size_t(t)] = src.pitch[o];
        r->stepPan[size_t(t)] = src.stepPan[o];
        r->shift[size_t(t)] = src.shift[o];
        r->repeat[size_t(t)] = src.repeat[o];
        r->gate[size_t(t)] = src.gate[o];
        r->reverse[size_t(t)] = src.reverse[o];
        r->drive[size_t(t)] = src.drive[o];
        r->filter[size_t(t)] = src.filter[o];
        r->reverb[size_t(t)] = src.reverb[o];
    }
    r->length = src.length;
    changed();
}

QString MixHouseBeatModel::patternsFolder() const
{
    return beatsFolder() + QStringLiteral("/Patterns");
}

QStringList MixHouseBeatModel::savedPatterns() const
{
    QStringList out;
    for (const QFileInfo& fi : QDir(patternsFolder()).entryInfoList({ QStringLiteral("*.glidebeat") }, QDir::Files, QDir::Name)) {
        out << fi.completeBaseName();
    }
    return out;
}

bool MixHouseBeatModel::savePattern(const QString& wanted)
{
    if (s_pattern.rows.empty()) {
        return false;
    }
    QDir().mkpath(patternsFolder());
    QString name = wanted.trimmed();
    static const QRegularExpression kBad(QStringLiteral("[\\\\/:*?\"<>|]"));
    name.replace(kBad, QStringLiteral(" "));
    if (name.isEmpty()) {
        for (int k = 1;; ++k) {
            name = (s_genre.isEmpty() ? muse::qtrc("mixhouse", "Pattern") : genreTitle(s_genre)) + QStringLiteral(" %1").arg(k);
            if (!QFileInfo::exists(patternsFolder() + QStringLiteral("/") + name + QStringLiteral(".glidebeat"))) {
                break;
            }
        }
    }
    const bool ok = saveBeatPattern(s_pattern, bpm(), patternsFolder() + QStringLiteral("/") + name + QStringLiteral(".glidebeat"));
    if (activity()) {
        activity()->post(ok ? muse::qtrc("mixhouse", "Pattern saved: %1 (in the genre menu under Your patterns)").arg(name)
                            : muse::qtrc("mixhouse", "Could not save the pattern"));
    }
    emit patternChanged();
    return ok;
}

void MixHouseBeatModel::loadPattern(const QString& name)
{
    QStringList names = savedPatterns();
    if (names.isEmpty()) {
        if (activity()) {
            activity()->post(muse::qtrc("mixhouse", "No saved patterns yet: make a beat and press Save pattern"));
        }
        return;
    }
    // "" = any of them (Serato's "User").
    QString pick = name;
    if (pick.isEmpty() || !names.contains(pick)) {
        static std::mt19937 rng(std::random_device {}());
        pick = names[int(std::uniform_int_distribution<int>(0, int(names.size()) - 1)(rng))];
    }
    BeatPattern loaded;
    if (!loadBeatPattern(patternsFolder() + QStringLiteral("/") + pick + QStringLiteral(".glidebeat"), loaded)) {
        return;
    }
    stop();
    s_pattern = loaded;
    s_genre = QStringLiteral("user");
    changed();
    if (activity()) {
        activity()->post(muse::qtrc("mixhouse", "Pattern: %1").arg(pick));
    }
}

void MixHouseBeatModel::cloneRow(int index)
{
    if (BeatRow* r = row(index); r && int(s_pattern.rows.size()) < kBeatMaxRows) {
        const BeatRow copy = *r;
        s_pattern.rows.insert(s_pattern.rows.begin() + index + 1, copy);
        changed();
    }
}

void MixHouseBeatModel::previewRow(int index)
{
    const BeatRow* r = row(index);
    if (!r || r->path.isEmpty()) {
        return;
    }
    if (!m_shotPlayer) {
        m_shotPlayer = std::make_unique<MixHousePreviewPlayer>();
    }
    const std::string device = muse::settings()->value(
        muse::Settings::Key("au3audio", "AudioIO/PlaybackDevice")).toString();
    if (m_shotPlayer->load(r->path.toStdString(), device)) {
        m_shotPlayer->setVolume(QSettings().value(QStringLiteral("GlideMix/previewVolume"), 0.7).toDouble() * r->gain);
        m_shotPlayer->playFrom(0.0);
    }
}

void MixHouseBeatModel::fill(int index, int every)
{
    if (BeatRow* r = row(index)) {
        every = std::max(1, every);
        for (size_t s = 0; s < r->steps.size(); ++s) {
            r->steps[s] = s % size_t(every) == 0 ? 2 : 0;
        }
        changed();
    }
}

void MixHouseBeatModel::evenOut(int index, int hits)
{
    if (BeatRow* r = row(index)) {
        const int spb = s_pattern.stepsPerBar;
        const std::vector<bool> bar = evenRhythm(hits, spb);
        for (size_t s = 0; s < r->steps.size(); ++s) {
            r->steps[s] = bar[s % size_t(spb)] ? 2 : 0;
        }
        changed();
    }
}

void MixHouseBeatModel::shift(int index, int by)
{
    if (BeatRow* r = row(index)) {
        const int n = totalSteps();
        remap(*r, n, n, [&](int t) { return ((t - by) % n + n) % n; });
        changed();
    }
}

void MixHouseBeatModel::reverse(int index)
{
    if (BeatRow* r = row(index)) {
        const int n = totalSteps();
        remap(*r, n, n, [&](int t) { return n - 1 - t; });
        changed();
    }
}

void MixHouseBeatModel::invert(int index)
{
    if (BeatRow* r = row(index)) {
        for (int& s : r->steps) {
            s = s > 0 ? 0 : 2;
        }
        changed();
    }
}

void MixHouseBeatModel::randomize(int index)
{
    if (BeatRow* r = row(index)) {
        static std::mt19937 rng(std::random_device {}());
        std::uniform_int_distribution<int> hit(0, 99);
        normalize(*r, totalSteps());
        for (size_t s = 0; s < r->steps.size(); ++s) {
            // Busier on the beats, sparser in between: random but still a groove.
            const int chance = s % 4 == 0 ? 60 : s % 2 == 0 ? 35 : 20;
            const int roll = hit(rng);
            r->steps[s] = roll < chance ? 2 : 0;
            r->velocity[s] = roll < chance / 4 ? 1.0 : roll > chance * 3 / 4 ? 0.5 : kBeatDefaultVelocity;
        }
        changed();
    }
}

void MixHouseBeatModel::clearRow(int index)
{
    if (BeatRow* r = row(index)) {
        std::fill(r->steps.begin(), r->steps.end(), 0);
        changed();
    }
}

void MixHouseBeatModel::clearAll()
{
    for (BeatRow& r : s_pattern.rows) {
        std::fill(r.steps.begin(), r.steps.end(), 0);
    }
    changed();
}

bool MixHouseBeatModel::renderPreview()
{
    // Two files in turn: the new loop is written while the old one may still play.
    static int turn = 0;
    const QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                         + QStringLiteral("/glidemix-beat-preview-%1.wav").arg(turn++ % 2);
    QString error;
    m_renderedBpm = bpm();
    if (!renderBeat(s_pattern, m_renderedBpm, path, error)) {
        if (m_player) {
            m_player->stop();
        }
        return false;
    }
    if (!m_player) {
        m_player = std::make_unique<MixHousePreviewPlayer>();
    }
    const std::string device = muse::settings()->value(
        muse::Settings::Key("au3audio", "AudioIO/PlaybackDevice")).toString();
    if (!m_player->load(path.toStdString(), device)) {
        return false;
    }
    m_player->setVolume(QSettings().value(QStringLiteral("GlideMix/previewVolume"), 0.7).toDouble());
    m_previewPath = path;
    return true;
}

namespace {
//! One genre's groove for one kind of sound: hits (step, velocity, repeat) over a bar.
struct Hit16 {
    int step;
    double vel;
    int rep = 1;
};
//! One genre: its usual tempo, swing, whether it uses a snare (else a clap),
//! and a bar of 16 steps per kind of sound:
//! X accent, x normal, o soft (ghost), 2 / 3 a roll of 2 or 3 hits, . nothing.
struct Genre {
    const char* id;
    double bpm;
    double swingPercent;
    bool snare;
    const char* kick;
    const char* clap;
    const char* hat;
    const char* openhat;
    const char* perc;
};

const Genre kGenres[] = {
    //  id            bpm  swing snare  kick                 clap / snare         hat                  open hat             perc
    { "house",        124, 54, false, "X...X...X...X...", "....x.......x...", "..x...x...x...x.", "..x...x...x...x.", "...o.......o..o." },
    { "deephouse",    120, 56, false, "X...X...X...X...", "....x.......x...", "..o...o...o...o.", "..x...........x.", ".o.....o..o....." },
    { "techhouse",    126, 52, false, "X...X...X...X...", "....x.......x...", "xoxoxoxoxoxoxoxo", "..x...x...x...x.", "...x..x....x..x." },
    { "techno",       132, 50, false, "X...X...X...X...", "....o.......o...", "oxXxoxXxoxXxoxXx", "..x...x...x...x.", "...x...x..x..x.." },
    { "afrohouse",    122, 55, false, "X...X...X...X...", "....x.......x...", "x.ox.ox.x.ox.ox.", "..........x.....", "x..x..x...x.x..." },
    { "amapiano",     113, 54, false, "X.....x...X.....", "....x.......x...", "x.x.x.x.x.x.x.x.", "......x.......x.", "...x.o.x..o..x.o" },
    { "disco",        118, 50, false, "X...X...X...X...", "....x.......x...", "x.x.x.x.x.x.x.x.", "..x...x...x...x.", "x.o.x.o.x.o.x.o." },
    { "ukgarage",     132, 62, false, "X......x..X.....", "....x.......x...", "..x...x...x..xx.", "......x.......x.", "...o.o.....o..o." },
    { "breaks",       130, 54, true,  "X.....X...X..x..", "....x..o....x...", "x.x.x.x.x.x.x.x.", "..............x.", ".......o......o." },
    { "dnb",          174, 50, true,  "X.........X.....", "....X.......X...", "x.x.x.x.x.x.x.x.", ".......x........", "...............o" },
    { "dubstep",      140, 50, true,  "X.........x.....", "........X.......", "x.x.x.x.x.x.x.x.", "......x.......x.", "...o.......o...." },
    { "hiphop",        92, 56, true,  "X......x..x.....", "....X.......X...", "x.x.x.x.x.x.x.x.", "..............x.", "......o........o" },
    { "boombap",       88, 58, true,  "X.....x...x.x...", "....X.......X...", "x.x.x.x.x.x.x.x.", "......x.........", ".o.....o.....o.." },
    { "lofi",          80, 60, true,  "X.....x...X.....", "....x.......x...", "x.o.x.o.x.o.x.o.", "..............o.", "......o...o....." },
    { "trap",         140, 50, true,  "X.....x...x.....", "........X.......", "x.x.x.3.x.2.x.x3", "...............x", ".............x.." },
    { "drill",        142, 50, true,  "X.....x....x..x.", "......X.......X.", "x..x..3.x..x.2..", "........x.......", "...x.......x...." },
    { "reggaeton",     96, 50, true,  "X...X...X...X...", "...x..x....x..x.", "x.x.x.x.x.x.x.x.", "..............x.", "..x...x...x...x." },
    { "dembow",       118, 50, true,  "X...X...X...X...", "...X..X....X..X.", "x.x.x.x.x.x.x.x.", "......x.......x.", ".x.x.x.x.x.x.x.x" },
};

const Genre* genreById(const QString& id)
{
    for (const Genre& g : kGenres) {
        if (id == QLatin1String(g.id)) {
            return &g;
        }
    }
    return nullptr;
}

std::vector<Hit16> grooveFor(const QString& genre, const QString& role)
{
    std::vector<Hit16> out;
    const Genre* g = genreById(genre);
    if (!g) {
        return out;
    }
    const char* bar = role == QLatin1String("kick") ? g->kick
                      : (role == QLatin1String("clap") || role == QLatin1String("snare")) ? g->clap
                      : role == QLatin1String("hat") ? g->hat
                      : role == QLatin1String("openhat") ? g->openhat
                      : role == QLatin1String("perc") ? g->perc : nullptr;
    for (int s = 0; bar && s < 16 && bar[s]; ++s) {
        switch (bar[s]) {
        case 'X': out.push_back({ s, 1.0 }); break;
        case 'x': out.push_back({ s, 0.75 }); break;
        case 'o': out.push_back({ s, 0.45 }); break;
        case '2': out.push_back({ s, 0.65, 2 }); break;
        case '3': out.push_back({ s, 0.65, 3 }); break;
        default: break;
        }
    }
    return out;
}

//! What kind of sound a sample is, from its folders and name.
bool sampleIs(const MixHouseSample& s, const QString& role)
{
    const QString text = (s.category + QLatin1Char(' ') + s.location + QLatin1Char(' ') + s.name).toLower();
    const auto has = [&](const char* w) { return text.contains(QLatin1String(w)); };
    if (role == QLatin1String("kick")) {
        return has("kick") && !has("snare");
    }
    if (role == QLatin1String("clap")) {
        return has("clap");
    }
    if (role == QLatin1String("snare")) {
        return has("snare");
    }
    if (role == QLatin1String("hat")) {
        return (has("hat") || has("hh")) && !has("open") && !has("ohh");
    }
    if (role == QLatin1String("openhat")) {
        return (has("hat") || has("hh")) && (has("open") || has("ohh"));
    }
    if (role == QLatin1String("perc")) {
        return has("percussion") || has("perc") || has("shaker") || has("tom") || has("rim") || has("conga") || has("bongo");
    }
    return false;
}
}

QString MixHouseBeatModel::pickSample(const QString& role, const QString& avoid) const
{
    if (!samples()) {
        return {};
    }
    if (samples()->samples().empty()) {
        samples()->load();   // the Samples tab was never opened yet
    }
    std::vector<const MixHouseSample*> pool;
    for (const MixHouseSample& s : samples()->samples()) {
        if (!s.loop && !s.missing && s.durationSec > 0.0 && s.durationSec < 4.0 && s.path != avoid && sampleIs(s, role)) {
            pool.push_back(&s);
        }
    }
    if (pool.empty() && role == QLatin1String("snare")) {
        return pickSample(QStringLiteral("clap"), avoid);
    }
    if (pool.empty() && role == QLatin1String("openhat")) {
        return pickSample(QStringLiteral("hat"), avoid);
    }
    if (pool.empty()) {
        return {};
    }
    static std::mt19937 rng(std::random_device {}());
    return pool[std::uniform_int_distribution<size_t>(0, pool.size() - 1)(rng)]->path;
}

void MixHouseBeatModel::applyTemplate(BeatRow& r, bool vary) const
{
    const int n = totalSteps();
    normalize(r, n);
    std::fill(r.steps.begin(), r.steps.end(), 0);
    std::fill(r.velocity.begin(), r.velocity.end(), kBeatDefaultVelocity);
    std::fill(r.repeat.begin(), r.repeat.end(), 1);
    static std::mt19937 rng(std::random_device {}());
    std::uniform_int_distribution<int> roll(0, 99);
    const std::vector<Hit16> groove = grooveFor(s_genre, r.role);
    for (int bar = 0; bar < s_pattern.bars; ++bar) {
        for (const Hit16& h : groove) {
            const int spb = s_pattern.stepsPerBar;
            const size_t s = size_t(bar * spb + (h.step * spb + 8) / 16);
            // A new groove: now and then a hit off the beat goes or moves a step.
            if (vary && h.step % 4 != 0 && roll(rng) < 20) {
                continue;
            }
            r.steps[s] = 2;
            r.velocity[s] = h.vel;
            r.repeat[s] = h.rep;
        }
        if (vary) {
            // ... and a soft ghost note here and there.
            for (int k = 0; k < s_pattern.stepsPerBar; ++k) {
                const size_t s = size_t(bar * s_pattern.stepsPerBar + k);
                if (r.steps[s] == 0 && k % 2 == 1 && roll(rng) < 10) {
                    r.steps[s] = 2;
                    r.velocity[s] = 0.4;
                }
            }
        }
    }
}

void MixHouseBeatModel::startBeat(const QString& genreId)
{
    static std::mt19937 rng(std::random_device {}());
    QString id = genreId;
    bool randomGroove = false;
    if (id == QLatin1String("random")) {
        // Random: any genre's kit and tempo, and a groove of its own.
        id = QLatin1String(kGenres[std::uniform_int_distribution<size_t>(0, std::size(kGenres) - 1)(rng)].id);
        randomGroove = true;
    }
    const Genre* g = genreById(id);
    if (!g) {
        return;
    }
    s_genre = id;
    const QStringList roles { QStringLiteral("kick"), g->snare ? QStringLiteral("snare") : QStringLiteral("clap"),
                              QStringLiteral("hat"), QStringLiteral("openhat"), QStringLiteral("perc") };
    // Locked rows stay as they are; the rest is the genre's kit and groove.
    std::vector<BeatRow> rows;
    for (const BeatRow& r : s_pattern.rows) {
        if (r.locked) {
            rows.push_back(r);
        }
    }
    for (const QString& role : roles) {
        if (std::any_of(rows.begin(), rows.end(), [&](const BeatRow& r) { return r.role == role; })) {
            continue;
        }
        BeatRow r;
        r.role = role;
        r.path = pickSample(role, QString());
        if (r.path.isEmpty()) {
            continue;   // no sample of that kind in the Samples tab
        }
        applyTemplate(r, randomGroove);
        // The closed hat chokes the open one, as on a drum machine.
        if (role == QLatin1String("hat") || role == QLatin1String("openhat")) {
            r.choke = 1;
        }
        rows.push_back(r);
    }
    if (rows.empty()) {
        if (activity()) {
            activity()->post(muse::qtrc("mixhouse", "No kicks, claps or hats found: add your sample folders in the Samples tab first"));
        }
        return;
    }
    s_pattern.rows = rows;
    s_pattern.swing = std::clamp((g->swingPercent - 50.0) / 25.0, 0.0, 1.0);
    // The genre's own tempo; the beat follows the mix only when asked to.
    s_ownBpm = g->bpm;
    emit bpmChanged();
    changed();
    if (activity() && randomGroove) {
        activity()->post(muse::qtrc("mixhouse", "Random beat: %1 kit at %2 BPM").arg(genreTitle(id)).arg(int(g->bpm)));
    }
}

QString MixHouseBeatModel::genreTitle(const QString& id)
{
    static const QMap<QString, QString> titles {
        { "house", muse::qtrc("mixhouse", "House") }, { "deephouse", muse::qtrc("mixhouse", "Deep house") },
        { "techhouse", muse::qtrc("mixhouse", "Tech house") }, { "techno", muse::qtrc("mixhouse", "Techno") },
        { "afrohouse", muse::qtrc("mixhouse", "Afro house") }, { "amapiano", muse::qtrc("mixhouse", "Amapiano") },
        { "disco", muse::qtrc("mixhouse", "Disco") }, { "ukgarage", muse::qtrc("mixhouse", "UK garage") },
        { "breaks", muse::qtrc("mixhouse", "Breaks") }, { "dnb", muse::qtrc("mixhouse", "Drum & bass") },
        { "dubstep", muse::qtrc("mixhouse", "Dubstep") }, { "hiphop", muse::qtrc("mixhouse", "Hip-hop") },
        { "boombap", muse::qtrc("mixhouse", "Boom bap") }, { "lofi", muse::qtrc("mixhouse", "Lo-fi") },
        { "trap", muse::qtrc("mixhouse", "Trap") }, { "drill", muse::qtrc("mixhouse", "Drill") },
        { "reggaeton", muse::qtrc("mixhouse", "Reggaeton") }, { "dembow", muse::qtrc("mixhouse", "Dembow") },
    };
    return titles.value(id, id);
}

QVariantList MixHouseBeatModel::genres() const
{
    QVariantList out;
    for (const Genre& g : kGenres) {
        out << QVariantMap { { QStringLiteral("id"), QLatin1String(g.id) },
                             { QStringLiteral("title"), genreTitle(QLatin1String(g.id)) },
                             { QStringLiteral("bpm"), g.bpm } };
    }
    return out;
}

void MixHouseBeatModel::setFollowMix(bool follow)
{
    if (follow != s_followMix) {
        s_followMix = follow;
        emit bpmChanged();
        changed();
    }
}

void MixHouseBeatModel::setOwnBpm(double bpm)
{
    bpm = std::clamp(std::round(bpm * 10.0) / 10.0, 60.0, 200.0);
    if (std::abs(bpm - s_ownBpm) > 0.01) {
        s_ownBpm = bpm;
        emit bpmChanged();
        changed();
    }
}

void MixHouseBeatModel::diceRow(int index)
{
    BeatRow* r = row(index);
    if (!r) {
        return;
    }
    QString role = r->role;
    if (role.isEmpty() && samples()) {
        // A row the user filled by hand: its kind from its sample.
        for (const MixHouseSample& s : samples()->samples()) {
            if (s.path == r->path) {
                for (const char* k : { "kick", "snare", "clap", "openhat", "hat", "perc" }) {
                    if (sampleIs(s, QLatin1String(k))) {
                        role = QLatin1String(k);
                        break;
                    }
                }
                break;
            }
        }
        r->role = role;
    }
    const QString path = pickSample(role, r->path);
    if (!path.isEmpty()) {
        r->path = path;
        changed();
        previewRow(index);
    }
}

void MixHouseBeatModel::diceAll()
{
    for (size_t i = 0; i < s_pattern.rows.size(); ++i) {
        if (!s_pattern.rows[i].locked) {
            const QString path = pickSample(s_pattern.rows[i].role, s_pattern.rows[i].path);
            if (!path.isEmpty()) {
                s_pattern.rows[i].path = path;
            }
        }
    }
    changed();
}

void MixHouseBeatModel::toggleLock(int index)
{
    if (BeatRow* r = row(index)) {
        r->locked = !r->locked;
        changed();
    }
}

void MixHouseBeatModel::generateSteps(int index)
{
    if (BeatRow* r = row(index)) {
        if (s_genre.isEmpty() || grooveFor(s_genre, r->role).empty()) {
            randomize(index);
            return;
        }
        applyTemplate(*r, true);
        changed();
    }
}

void MixHouseBeatModel::tuneToSong(int index)
{
    BeatRow* r = row(index);
    const auto editPrj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!r || !editPrj || m_sampleLine < 0) {
        return;
    }
    const auto post = [&](const QString& text) {
        if (activity()) {
            activity()->post(text);
        }
    };
    // The sample's key: the Samples tab's, else what its name says.
    int sampleKey = 0;
    if (samples()) {
        for (const MixHouseSample& s : samples()->samples()) {
            if (s.path == r->path) {
                sampleKey = s.keyId;
                break;
            }
        }
    }
    if (sampleKey == 0) {
        sampleKey = parseSampleName(QFileInfo(r->path).fileName()).keyId;
    }
    if (sampleKey == 0) {
        post(muse::qtrc("mixhouse", "No key in this sample's name, so it can't be tuned"));
        return;
    }
    // The song: the lane right above the sample line, the clip under the playhead.
    const std::vector<trackedit::Track> tracks = editPrj->trackList();
    trackedit::TrackId song = -1;
    for (size_t i = 1; i < tracks.size(); ++i) {
        if (tracks[i].id == trackedit::TrackId(m_sampleLine)) {
            song = tracks[i - 1].id;
        }
    }
    const auto state = globalContext()->playbackState();
    const double at = state ? state->playbackPosition().to_double() : 0.0;
    int songKey = 0;
    for (const trackedit::Clip& clip : editPrj->clipList(song)) {
        if (songKey == 0 || (clip.startTime <= at && at < clip.endTime)) {
            const int id = beatGrids() ? beatGrids()->clipLibraryTrackId(clip.key) : -1;
            if (id >= 0 && library()) {
                IMixHouseLibrary::TrackQuery query;
                query.trackIds = { id };
                const MixHouseTrackList found = library()->tracks(query);
                if (!found.empty() && found.front().keyId > 0) {
                    songKey = found.front().keyId;
                }
            }
        }
    }
    if (songKey == 0) {
        post(muse::qtrc("mixhouse", "The song above has no key yet (analyse it in the library)"));
        return;
    }
    // Root to root, the shortest way (-6..+5 semitones).
    const int from = (sampleKey - 1) % 12;
    const int to = (songKey - 1) % 12;
    const int shift = ((to - from) % 12 + 18) % 12 - 6;
    normalize(*r, totalSteps());
    std::fill(r->pitch.begin(), r->pitch.end(), double(shift));
    changed();
    post(muse::qtrc("mixhouse", "Tuned to the song's key: %1 semitones").arg(shift > 0 ? QStringLiteral("+%1").arg(shift) : QString::number(shift)));
}

void MixHouseBeatModel::togglePlay()
{
    if (m_playing) {
        stop();
        return;
    }
    // One preview at a time.
    MixHouseTrackListModel::stopAllPreviews();
    MixHouseSamplesModel::stopAllPreviews();
    if (!renderPreview()) {
        if (activity()) {
            activity()->post(muse::qtrc("mixhouse", "Put a one-shot on a row and click some steps first"));
        }
        return;
    }
    m_player->playFrom(0.0);
    m_playing = true;
    m_poll.start();
    emit playingChanged();
}

void MixHouseBeatModel::stop()
{
    m_poll.stop();
    m_rerender.stop();
    if (m_player) {
        m_player->stop();
    }
    if (m_playing) {
        m_playing = false;
        emit playingChanged();
    }
    if (m_playStep != -1) {
        m_playStep = -1;
        emit playStepChanged();
    }
}

QString MixHouseBeatModel::beatsFolder() const
{
    const QString projects = projectConfiguration() ? projectConfiguration()->userProjectsPath().toQString()
                             : QDir::homePath() + QStringLiteral("/Music/Glidemix/Projects");
    return projects + QStringLiteral("/Samples/Beats");
}

void MixHouseBeatModel::setClipId(int id)
{
    if (id == m_clipId) {
        return;
    }
    m_clipId = id;
    // Editing a placed beat: its pattern comes back from the file next to it.
    const auto editPrj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (editPrj && m_sampleLine >= 0 && id >= 0) {
        const trackedit::Clip clip = editPrj->clip(trackedit::ClipKey(trackedit::TrackId(m_sampleLine), trackedit::TrackItemId(id)));
        BeatPattern loaded;
        if (loadBeatPattern(beatsFolder() + QStringLiteral("/") + clip.title.toQString() + QStringLiteral(".glidebeat"), loaded)) {
            stop();
            s_pattern = loaded;
            emit patternChanged();
        }
    }
    emit clipIdChanged();
}

bool MixHouseBeatModel::putOnSampleLine()
{
    const auto prj = globalContext() ? globalContext()->currentProject() : nullptr;
    const auto editPrj = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!prj || !editPrj || m_sampleLine < 0) {
        return false;
    }
    const trackedit::TrackId line = trackedit::TrackId(m_sampleLine);
    // The beat being edited, if it is still on the line.
    std::optional<trackedit::Clip> old;
    if (m_clipId >= 0) {
        for (const trackedit::Clip& clip : editPrj->clipList(line)) {
            if (clip.key.itemId == trackedit::TrackItemId(m_clipId)) {
                old = clip;
            }
        }
    }

    // Its own file, named so the Samples tab reads it as a loop at this tempo.
    // An edited beat keeps its number.
    const double tempo = bpm();
    const QString dir = beatsFolder();
    QDir().mkpath(dir);
    const QString bpmText = std::abs(tempo - std::round(tempo)) < 0.05 ? QString::number(int(std::round(tempo)))
                            : QString::number(tempo, 'f', 1);
    const auto nameFor = [&](int n) {
        return dir + (s_pattern.bars == 1 ? QStringLiteral("/Beat %1 1 bar (%2bpm).wav").arg(n).arg(bpmText)
                      : QStringLiteral("/Beat %1 %2 bars (%3bpm).wav").arg(n).arg(s_pattern.bars).arg(bpmText));
    };
    QString oldPath;
    int number = 0;
    if (old) {
        oldPath = dir + QStringLiteral("/") + old->title.toQString() + QStringLiteral(".wav");
        static const QRegularExpression kNumber(QStringLiteral("^Beat (\\d+)"));
        number = kNumber.match(old->title.toQString()).captured(1).toInt();
    }
    QString path = number > 0 ? nameFor(number) : QString();
    if (path.isEmpty() || (QFileInfo::exists(path) && path != oldPath)) {
        for (int n = 1;; ++n) {
            path = nameFor(n);
            if (!QFileInfo::exists(path)) {
                break;
            }
        }
    }
    QString error;
    if (!renderBeat(s_pattern, tempo, path, error)) {
        if (activity()) {
            activity()->post(error);
        }
        return false;
    }
    saveBeatPattern(s_pattern, tempo, beatPatternPathFor(path));
    if (!oldPath.isEmpty() && oldPath != path) {
        QFile::remove(oldPath);
        QFile::remove(beatPatternPathFor(oldPath));
    }

    const trackedit::TimeSignature ts = editPrj->timeSignature();
    const double projectTempo = ts.tempo > 0.0 ? ts.tempo : tempo;
    const double bar = 60.0 / projectTempo * (4.0 / std::max(1, ts.lower)) * std::max(1, ts.upper);
    double start = 0.0;
    if (old) {
        // Updated in place: the new loop starts where the old one did.
        start = old->startTime;
        trackeditInteraction()->removeClip(old->key);
    } else {
        // On the bar under the playhead; after anything already on the line.
        const auto state = globalContext()->playbackState();
        start = std::max(0.0, std::floor((state ? state->playbackPosition().to_double() : 0.0) / bar + 1e-6) * bar);
        const double length = s_pattern.bars * 4 * 60.0 / projectTempo;
        for (bool moved = true; moved;) {
            moved = false;
            for (const trackedit::Clip& clip : editPrj->clipList(line)) {
                if (clip.startTime < start + length - 1e-3 && clip.endTime > start + 1e-3) {
                    start = std::ceil(clip.endTime / bar - 1e-3) * bar;
                    moved = true;
                }
            }
        }
    }
    std::vector<trackedit::ClipKey> before;
    for (const trackedit::Clip& clip : editPrj->clipList(line)) {
        before.push_back(clip.key);
    }
    prj->importIntoTracks({ muse::io::path_t(path) }, { line }, start, { false });
    trackedit::ClipKeyList created;
    for (const trackedit::Clip& clip : editPrj->clipList(line)) {
        if (std::none_of(before.begin(), before.end(), [&](const trackedit::ClipKey& k) { return k.itemId == clip.key.itemId; })) {
            created.push_back(clip.key);
        }
    }
    if (created.empty()) {
        return false;
    }
    prepareSampleClips(globalContext(), created, tempo);
    if (projectHistory()) {
        projectHistory()->modifyState();
    }
    // From now on this window edits the beat it just placed.
    m_clipId = int(created.front().itemId);
    emit clipIdChanged();
    // In the Samples tab too, to use again under other songs.
    if (samples()) {
        samples()->addFolder(QFileInfo(dir).absolutePath());
    }
    if (activity()) {
        activity()->post((old ? muse::qtrc("mixhouse", "Beat updated: %1") : muse::qtrc("mixhouse", "Beat on the sample line: %1"))
                         .arg(QFileInfo(path).completeBaseName()));
    }
    LOGI() << "GlideMix beat: " << path << " at " << start << (old ? " (updated)" : "");
    return true;
}
