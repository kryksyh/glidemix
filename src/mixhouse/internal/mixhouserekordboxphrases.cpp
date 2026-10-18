/*
 * GlideMix
 */
#include "mixhouserekordboxphrases.h"
#include "mixhouse/glidemixpaths.h"

#include <QDataStream>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <thread>

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTimer>

#include "log.h"

#include "mp3guessenc.h"

using namespace au::mixhouse;

namespace {
// rekordbox ANLZ files are big-endian: a file header ("PMAI"), then tagged
// sections, each "TAG" + header length + total length.
quint32 u32(const QByteArray& b, qsizetype at)
{
    if (at < 0 || at + 4 > b.size()) {
        return 0;
    }
    const auto* p = reinterpret_cast<const uchar*>(b.constData() + at);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

quint16 u16(const QByteArray& b, qsizetype at)
{
    if (at < 0 || at + 2 > b.size()) {
        return 0;
    }
    const auto* p = reinterpret_cast<const uchar*>(b.constData() + at);
    return quint16((p[0] << 8) | p[1]);
}

//! Offset of the section `tag`, or -1.
qsizetype findSection(const QByteArray& b, const char* tag)
{
    qsizetype pos = u32(b, 4);   // file header length
    while (pos + 12 <= b.size()) {
        const quint32 total = u32(b, pos + 8);
        if (b.mid(pos, 4) == tag) {
            return pos;
        }
        if (total < 12) {
            break;
        }
        pos += total;
    }
    return -1;
}

//! The file name PPTH holds ("?/Name.mp3" -> "Name.mp3").
QString pathName(const QByteArray& b)
{
    const qsizetype s = findSection(b, "PPTH");
    if (s < 0) {
        return {};
    }
    const quint32 length = u32(b, s + 12);
    QString path;
    for (quint32 i = 0; i + 1 < length; i += 2) {
        const char16_t c = u16(b, s + 16 + i);
        if (c == 0) {
            break;
        }
        path.append(QChar(c));
    }
    return path.mid(path.lastIndexOf(u'/') + 1);
}

struct Beat {
    double timeSec;
    double bpm;
    int barBeat;   // 1..4, 1 = the bar's first beat
};

//! PQTZ: every beat of the grid (beat number in the bar, BPM x 100, time in ms).
std::vector<Beat> readBeats(const QByteArray& dat)
{
    std::vector<Beat> beats;
    const qsizetype s = findSection(dat, "PQTZ");
    if (s < 0) {
        return beats;
    }
    const quint32 headerLength = u32(dat, s + 4);
    const quint32 count = u32(dat, s + 20);
    beats.reserve(count);
    for (quint32 i = 0; i < count; ++i) {
        const qsizetype e = s + headerLength + qsizetype(i) * 8;
        if (e + 8 > dat.size()) {
            break;
        }
        beats.push_back({ u32(dat, e + 4) / 1000.0, u16(dat, e + 2) / 100.0, u16(dat, e) });
    }
    return beats;
}

//! rekordbox's phrase names by mood (1 high, 2 mid, 3 low), kind and the
//! entry's variant bytes k1/k2/k3 (the numbers rekordbox shows: Intro 1/2,
//! Up 1/2/3, Chorus 1/2, Outro 1/2).
QString phraseLabel(int mood, int kind, int k1, int k2, int k3)
{
    if (mood == 1) {
        switch (kind) {
        case 1: return k1 == 1 ? QStringLiteral("Intro 1") : QStringLiteral("Intro 2");
        case 2: return k2 == 0 ? QStringLiteral("Up 1") : k3 == 0 ? QStringLiteral("Up 2") : QStringLiteral("Up 3");
        case 3: return QStringLiteral("Down");
        case 5: return k1 == 1 ? QStringLiteral("Chorus 1") : QStringLiteral("Chorus 2");
        case 6: return k1 == 1 ? QStringLiteral("Outro 1") : QStringLiteral("Outro 2");
        default: return QString();
        }
    }
    if (kind == 1) {
        return QStringLiteral("Intro");
    }
    if (kind == 8) {
        return QStringLiteral("Bridge");
    }
    if (kind == 9) {
        return QStringLiteral("Chorus");
    }
    if (kind == 10) {
        return QStringLiteral("Outro");
    }
    if (mood == 2 && kind >= 2 && kind <= 7) {
        return QStringLiteral("Verse %1").arg(kind - 1);
    }
    if (mood == 3 && kind >= 2 && kind <= 7) {
        return QStringLiteral("Verse %1").arg(kind <= 4 ? 1 : 2);
    }
    return QString();
}

//! rekordbox's own phrase colours (its Phrase ribbon, the screenshot).
QColor phraseColor(const QString& label)
{
    if (label.startsWith(u"Intro")) {
        return QColor(0xD0, 0x3A, 0x1E);   // red
    }
    if (label.startsWith(u"Up") || label.startsWith(u"Verse")) {
        return QColor(0x93, 0x40, 0xF0);   // purple
    }
    if (label.startsWith(u"Down") || label.startsWith(u"Bridge")) {
        return QColor(0xA3, 0x7A, 0x33);   // khaki
    }
    if (label.startsWith(u"Chorus")) {
        return QColor(0x18, 0xB0, 0x1C);   // green
    }
    return QColor(0x6C, 0x90, 0xD6);       // Outro: steel blue
}

//! PSSI phrases, timed with the analysis's own beat grid.
std::vector<MixHousePhrase> readPhrases(const QByteArray& ext, const std::vector<Beat>& beats)
{
    std::vector<MixHousePhrase> result;
    const qsizetype s = findSection(ext, "PSSI");
    if (s < 0 || beats.size() < 2) {
        return result;
    }
    const quint32 headerLength = u32(ext, s + 4);
    const quint32 entryBytes = u32(ext, s + 12);
    const int count = u16(ext, s + 16);
    const int mood = u16(ext, s + 18);
    const int endBeat = u16(ext, s + 26);
    if (entryBytes < 6) {
        return result;
    }
    const double lastBeatLength = beats.back().bpm > 0.0 ? 60.0 / beats.back().bpm : 0.5;
    auto beatTime = [&](int beat) {   // beats count from 1
        if (beat < 1) {
            return beats.front().timeSec;
        }
        if (static_cast<size_t>(beat) <= beats.size()) {
            return beats[static_cast<size_t>(beat - 1)].timeSec;
        }
        return beats.back().timeSec + (beat - static_cast<int>(beats.size())) * lastBeatLength;
    };
    for (int i = 0; i < count; ++i) {
        const qsizetype e = s + headerLength + qsizetype(i) * entryBytes;
        if (e + qsizetype(entryBytes) > ext.size()) {
            break;
        }
        const int beat = u16(ext, e + 2);
        const int kind = u16(ext, e + 4);
        const auto byte = [&](int offset) { return offset < int(entryBytes) ? int(uchar(ext.at(e + offset))) : 0; };
        const int nextBeat = i + 1 < count ? u16(ext, e + entryBytes + 2) : endBeat;
        MixHousePhrase phrase;
        phrase.label = phraseLabel(mood, kind, byte(7), byte(9), byte(19));
        if (phrase.label.isEmpty() || nextBeat <= beat) {
            continue;
        }
        phrase.startSec = beatTime(beat);
        phrase.endSec = beatTime(nextBeat);
        // Fill-in flag and the beat it starts on (entry bytes 21, 22-23).
        if (entryBytes >= 24 && byte(21) != 0) {
            const int fillBeat = u16(ext, e + 22);
            if (fillBeat > beat && fillBeat < nextBeat) {
                phrase.fillStartSec = beatTime(fillBeat);
            }
        }
        phrase.color = phraseColor(phrase.label);
        result.push_back(phrase);
    }
    return result;
}

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
}

QColor au::mixhouse::mixHousePhraseColor(const QString& label)
{
    return phraseColor(label);
}

QStringList au::mixhouse::mixHousePhraseLabels()
{
    // rekordbox's high-mood names first (EDM), then its mid/low-mood ones.
    return { QStringLiteral("Intro 1"), QStringLiteral("Intro 2"), QStringLiteral("Up 1"), QStringLiteral("Up 2"),
             QStringLiteral("Up 3"), QStringLiteral("Down"), QStringLiteral("Chorus 1"), QStringLiteral("Chorus 2"),
             QStringLiteral("Outro 1"), QStringLiteral("Outro 2"), QStringLiteral("Verse 1"), QStringLiteral("Verse 2"),
             QStringLiteral("Bridge") };
}

MixHouseRekordboxPhrases::MixHouseRekordboxPhrases()
    : m_index(std::make_shared<QHash<QString, QStringList> >()), m_alive(std::make_shared<std::atomic<bool> >(true))
{
}

MixHouseRekordboxPhrases::~MixHouseRekordboxPhrases()
{
    *m_alive = false;
}

QString MixHouseRekordboxPhrases::analysisRoot()
{
    return au::mixhouse::paths::rekordboxAnalysisDir();
}

//! GlideMix: walking rekordbox's analysis folders takes 5-12 s on a big
//! collection, and until it is done the library draws its own fallback
//! waveforms (mostly red on bass-heavy tracks) that then change. The last
//! index is kept on disk and loaded at once; the walk still runs in the
//! background and replaces it when done.
static QString indexCachePath()
{
    return au::mixhouse::paths::glidemixDataPath(QStringLiteral("Library/rekordbox-index.bin"));
}

static constexpr quint32 kIndexCacheVersion = 1;

void MixHouseRekordboxPhrases::init()
{
    if (!QFileInfo::exists(analysisRoot())) {
        return;
    }
    QFile cache(indexCachePath());
    if (cache.open(QIODevice::ReadOnly)) {
        QDataStream in(&cache);
        quint32 version = 0;
        auto index = std::make_shared<QHash<QString, QStringList> >();
        in >> version;
        if (version == kIndexCacheVersion) {
            in >> *index;
        }
        if (in.status() == QDataStream::Ok && !index->isEmpty()) {
            m_index = index;
            LOGI() << "GlideMix: rekordbox analysis index loaded from cache: " << index->size() << " tracks";
        }
    }
    buildIndex();
}

void MixHouseRekordboxPhrases::buildIndex()
{
    const auto alive = m_alive;
    std::thread([this, alive]() {
        QElapsedTimer timer;
        timer.start();
        auto index = std::make_shared<QHash<QString, QStringList> >();
        QDirIterator it(analysisRoot(), { QStringLiteral("ANLZ0000.DAT") }, QDir::Files, QDirIterator::Subdirectories);
        int files = 0;
        while (it.hasNext() && *alive) {
            const QString dat = it.next();
            QFile file(dat);
            if (!file.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString name = pathName(file.read(2048));   // PPTH is the first section
            if (!name.isEmpty()) {
                (*index)[name.normalized(QString::NormalizationForm_C)].append(QFileInfo(dat).absolutePath());
                ++files;
            }
        }
        const qint64 ms = timer.elapsed();
        if (!*alive) {
            return;
        }
        QTimer::singleShot(0, qApp, [this, alive, index, files, ms]() {
            if (!*alive) {
                return;
            }
            // The same as the cached index (the usual case): nothing on
            // screen changes, so no refresh.
            const bool changed = !m_index || *m_index != *index;
            m_index = index;
            LOGI() << "GlideMix: rekordbox analysis indexed in " << ms << " ms: " << files << " tracks"
                   << (changed ? "" : " (same as cached)");
            if (!changed) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(m_cacheMutex);
                m_cache.clear();
            }
            QDir().mkpath(QFileInfo(indexCachePath()).absolutePath());
            QSaveFile cache(indexCachePath());
            if (cache.open(QIODevice::WriteOnly)) {
                QDataStream out(&cache);
                out << kIndexCacheVersion << *index;
                cache.commit();
            }
            m_ready.notify();
        });
    }).detach();
}

QString MixHouseRekordboxPhrases::analysisFolder(const QString& filePath, double bpmHint) const
{
    // Everything read from rekordbox comes through here: off in Preferences,
    // no track has any.
    if (libraryConfiguration() && !libraryConfiguration()->useRekordboxAnalysis()) {
        return QString();
    }
    const std::shared_ptr<const QHash<QString, QStringList> > index = m_index;
    const QStringList folders = index->value(QFileInfo(filePath).fileName().normalized(QString::NormalizationForm_C));
    if (folders.size() <= 1 || bpmHint <= 0.0) {
        return folders.value(0);
    }
    // Same-named files: the analysis whose tempo matches.
    QString best;
    double bestDistance = 1e9;
    for (const QString& folder : folders) {
        const std::vector<Beat> beats = readBeats(readFile(folder + QStringLiteral("/ANLZ0000.DAT")));
        const double distance = beats.empty() ? 1e8 : std::abs(beats.front().bpm - bpmHint);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = folder;
        }
    }
    return best;
}

std::vector<MixHousePhrase> MixHouseRekordboxPhrases::phrases(const QString& filePath, double bpmHint) const
{
    if (filePath.isEmpty() || m_index->isEmpty()) {
        return {};
    }
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        const auto cached = m_cache.constFind(filePath);
        if (cached != m_cache.constEnd()) {
            return cached.value();
        }
    }
    std::vector<MixHousePhrase> result;
    const QString folder = analysisFolder(filePath, bpmHint);
    if (!folder.isEmpty()) {
        result = readPhrases(readFile(folder + QStringLiteral("/ANLZ0000.EXT")),
                             readBeats(readFile(folder + QStringLiteral("/ANLZ0000.DAT"))));
        const double offset = timingOffsetSec(filePath);
        for (MixHousePhrase& phrase : result) {
            phrase.startSec = std::max(0.0, phrase.startSec - offset);
            phrase.endSec = std::max(0.0, phrase.endSec - offset);
            if (phrase.fillStartSec >= 0.0) {
                phrase.fillStartSec = std::max(0.0, phrase.fillStartSec - offset);
            }
        }
    }
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_cache.insert(filePath, result);
    return result;
}

double MixHouseRekordboxPhrases::timingOffsetSec(const QString& filePath) const
{
    if (libraryConfiguration() && !libraryConfiguration()->useRekordboxAnalysis()) {
        return 0.0;
    }
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        const auto cached = m_offsetCache.constFind(filePath);
        if (cached != m_offsetCache.constEnd()) {
            return cached.value();
        }
    }
    // Mixxx's RekordboxFeature (PR #2119): rekordbox decodes MP3s from a
    // different first sample than other decoders, by how the file was
    // encoded. Our mpg123 import decodes sample-for-sample like Core Audio
    // (measured on this library, 2026-09-15), so Mixxx's Core Audio column
    // applies. Other formats: no offset measured.
    double offsetMs = 0.0;
    if (filePath.endsWith(QStringLiteral(".mp3"), Qt::CaseInsensitive)) {
        static std::mutex guessMutex;   // mp3guessenc keeps global state
        std::lock_guard<std::mutex> guessLock(guessMutex);
        switch (mp3guessenc_timing_shift_case(QFile::encodeName(filePath).constData())) {
        case EXIT_CODE_CASE_A: offsetMs = 12.0;
            break;
        case EXIT_CODE_CASE_B: offsetMs = 13.0;
            break;
        case EXIT_CODE_CASE_C: offsetMs = 26.0;
            break;
        case EXIT_CODE_CASE_D: offsetMs = 50.0;
            break;
        default:
            break;
        }
    }
    std::lock_guard<std::mutex> lock(m_cacheMutex);
    m_offsetCache.insert(filePath, offsetMs / 1000.0);
    return offsetMs / 1000.0;
}

MixHouseRekordboxGrid MixHouseRekordboxPhrases::beatGrid(const QString& filePath, double bpmHint) const
{
    MixHouseRekordboxGrid grid;
    if (filePath.isEmpty() || m_index->isEmpty()) {
        return grid;
    }
    const QString folder = analysisFolder(filePath, bpmHint);
    if (folder.isEmpty()) {
        return grid;
    }
    const std::vector<Beat> beats = readBeats(readFile(folder + QStringLiteral("/ANLZ0000.DAT")));
    if (beats.size() < 8 || beats.front().bpm <= 0.0) {
        return grid;
    }
    const double bpm = beats.front().bpm;
    for (const Beat& beat : beats) {
        if (std::abs(beat.bpm - bpm) > 0.005) {
            return grid;   // a tempo change: our grids are constant
        }
    }
    // The beats are whole milliseconds: fit the first beat through all of them.
    const double period = 60.0 / bpm;
    double sum = 0.0;
    for (size_t i = 0; i < beats.size(); ++i) {
        sum += beats[i].timeSec - double(i) * period;
    }
    const double first = sum / double(beats.size()) - timingOffsetSec(filePath);
    size_t downbeat = 0;
    while (downbeat < beats.size() && beats[downbeat].barBeat != 1) {
        ++downbeat;
    }
    if (downbeat == beats.size()) {
        return grid;
    }
    const auto atOrAfterZero = [](double t, double step) {
        while (t < 0.0) {
            t += step;
        }
        return t;
    };
    grid.bpm = bpm;
    grid.firstBeatSec = atOrAfterZero(first, period);
    grid.firstDownbeatSec = atOrAfterZero(first + double(downbeat) * period, 4.0 * period);
    grid.valid = true;
    return grid;
}

namespace {
//! PWV5: rekordbox's scrolling-deck colour waveform, 2 bytes a column at 150
//! columns a second -- red, green and blue in 3 bits each, then the height in
//! 5. `maxColumns` 0 keeps every stored column (the timeline lane wants them
//! all); a budget reduces them by keeping the TALLEST column of each group and
//! ITS colour, because max-pooling the channels separately blends neighbours
//! into colours rekordbox never drew.
au::mixhouse::MixHouseWaveformOverview rbDecodePwv5(const QByteArray& ext, quint32 maxColumns)
{
    au::mixhouse::MixHouseWaveformOverview overview;
    const qsizetype s = findSection(ext, "PWV5");
    if (s < 0) {
        return overview;
    }
    const quint32 headerLength = u32(ext, s + 4);
    const quint32 count = u32(ext, s + 16);
    const quint32 columns = maxColumns > 0 ? std::min(count, maxColumns) : count;
    if (columns == 0) {
        return overview;
    }
    overview.rgb.reserve(columns);
    for (quint32 c = 0; c < columns; ++c) {
        const quint32 from = quint32(quint64(c) * count / columns);
        const quint32 to = std::min(count, std::max(from + 1, quint32(quint64(c + 1) * count / columns)));
        quint16 best = 0;
        int bestHeight = -1;
        quint8 low = 0, mid = 0, high = 0;
        for (quint32 k = from; k < to; ++k) {
            const qsizetype e = s + headerLength + qsizetype(k) * 2;
            if (e + 2 > ext.size()) {
                break;
            }
            const quint16 v = u16(ext, e);
            const int height = (v >> 2) & 31;
            if (height > bestHeight) {
                bestHeight = height;
                best = v;
            }
            low = std::max<quint8>(low, quint8(((v >> 13) & 7) * 255 / 7));
            mid = std::max<quint8>(mid, quint8(((v >> 10) & 7) * 255 / 7));
            high = std::max<quint8>(high, quint8(((v >> 7) & 7) * 255 / 7));
        }
        if (bestHeight < 0) {
            break;
        }
        // The stored height is rekordbox's own already-normalised level (5
        // bits), used as is rather than re-derived from peak and average.
        const quint8 all = quint8(bestHeight * 255 / 31);
        const quint32 rgb = (quint32(((best >> 13) & 7) * 255 / 7) << 16)
                            | (quint32(((best >> 10) & 7) * 255 / 7) << 8)
                            | quint32(((best >> 7) & 7) * 255 / 7);
        // The overview format interleaves two channels.
        for (int channel = 0; channel < 2; ++channel) {
            overview.all.push_back(all);
            overview.low.push_back(low);
            overview.mid.push_back(mid);
            overview.high.push_back(high);
        }
        overview.rgb.push_back(rgb);
    }
    overview.valid = !overview.all.empty();
    return overview;
}
}

MixHouseWaveformOverview MixHouseRekordboxPhrases::waveformOverview(const QString& filePath, double bpmHint) const
{
    MixHouseWaveformOverview overview;
    if (filePath.isEmpty() || m_index->isEmpty()) {
        return overview;
    }
    const QString folder = analysisFolder(filePath, bpmHint);
    if (folder.isEmpty()) {
        return overview;
    }
    const QByteArray ext = readFile(folder + QStringLiteral("/ANLZ0000.EXT"));

    // The library Preview column is rekordbox's PWV4: the whole track in 1200
    // columns of six bytes. Bytes 3, 4 and 5 are the low, mid and high band
    // levels; bytes 0 to 2 are not used by this view. The colour and height
    // rekordbox draws from them were measured against its own screen (same
    // tracks, side by side), not taken from its code:
    //   hue         the three bands as R, G, B, normalised by the strongest
    //   saturation  0.573 * (1 - min/max) + 0.0011 * max + 0.088
    //   brightness  0.0017 * max + 0.722   (nearly constant: the "dusty" look)
    //   height      0.115 * max + 1 px, on a row about 19 px tall
    // and a pale 1 px baseline runs on after the last column with any signal.
    // PWV5 (the 150-a-second scrolling-deck waveform) draws differently, with
    // hairy full-strength colours, which is why the Preview column must not
    // use it.
    const qsizetype p4 = findSection(ext, "PWV4");
    if (p4 >= 0) {
        const quint32 headerLength4 = u32(ext, p4 + 4);
        const quint32 entrySize4 = u32(ext, p4 + 12);
        const quint32 count4 = u32(ext, p4 + 16);
        if (entrySize4 == 6 && count4 > 0) {
            const auto byteAt = [&](quint32 column, int index) -> int {
                const qsizetype e = p4 + headerLength4 + qsizetype(column) * 6 + index;
                return e < ext.size() ? int(quint8(ext[e])) : 0;
            };
            int lastSignal = -1;
            for (quint32 c = 0; c < count4; ++c) {
                if (std::max({ byteAt(c, 3), byteAt(c, 4), byteAt(c, 5) }) > 0) {
                    lastSignal = int(c);
                }
            }
            for (quint32 c = 0; c < count4; ++c) {
                const int lo = byteAt(c, 3);
                const int mi = byteAt(c, 4);
                const int hi = byteAt(c, 5);
                const int mx = std::max({ lo, mi, hi });
                const int mn = std::min({ lo, mi, hi });
                quint8 all = 0;
                quint32 rgb = 0;
                if (mx > 0) {
                    const QColor chroma = QColor::fromRgbF(float(lo) / mx, float(mi) / mx, float(hi) / mx);
                    const float hue = std::max(0.0f, chroma.hsvHueF());
                    const float sat = std::clamp(0.573f * (1.0f - float(mn) / mx) + 0.0011f * mx + 0.088f, 0.05f, 1.0f);
                    const float val = std::clamp(0.0017f * mx + 0.722f, 0.4f, 1.0f);
                    rgb = QColor::fromHsvF(hue, sat, val).rgb() & 0xFFFFFF;
                    all = quint8(std::clamp((0.115f * mx + 1.0f) / 19.0f, 0.0f, 1.0f) * 255.0f);
                } else if (int(c) > lastSignal && lastSignal >= 0) {
                    rgb = 0x969696;   // the pale baseline after the end of the audio
                    all = quint8(255.0f / 19.0f);
                }
                // The overview format interleaves two channels.
                for (int channel = 0; channel < 2; ++channel) {
                    overview.all.push_back(all);
                    // Raw band bytes (0 to 127) doubled to fit 8 bits: see
                    // MixHouseWaveformOverview::rekordboxPreview.
                    overview.low.push_back(quint8(std::min(255, lo * 2)));
                    overview.mid.push_back(quint8(std::min(255, mi * 2)));
                    overview.high.push_back(quint8(std::min(255, hi * 2)));
                }
                overview.rgb.push_back(rgb);
            }
            overview.rekordboxPreview = true;
            overview.valid = !overview.all.empty();
            return overview;
        }
    }

    return rbDecodePwv5(ext, 2000);
}


MixHouseWaveformOverview MixHouseRekordboxPhrases::waveformDetail(const QString& filePath, double bpmHint) const
{
    MixHouseWaveformOverview overview;
    if (filePath.isEmpty() || m_index->isEmpty()) {
        return overview;
    }
    const QString folder = analysisFolder(filePath, bpmHint);
    if (folder.isEmpty()) {
        return overview;
    }
    // Every stored column, not the 2000 the library's Preview column settles
    // for: a timeline lane is drawn at a pixel a column or finer.
    return rbDecodePwv5(readFile(folder + QStringLiteral("/ANLZ0000.EXT")), 0);
}

muse::async::Notification MixHouseRekordboxPhrases::ready() const
{
    return m_ready;
}
