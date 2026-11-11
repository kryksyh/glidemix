/*
 * GlideMix Samples: see mixhousesamplename.h.
 */
#include "mixhousesamplename.h"

#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace au::mixhouse {
namespace {
const QRegularExpression& separators()
{
    static const QRegularExpression re(QStringLiteral("[\\s_\\-\\.\\(\\)\\[\\],]+"));
    return re;
}

const QRegularExpression& keyToken()
{
    static const QRegularExpression re(
        QStringLiteral("^([A-Ga-g])(#|b|sharp|flat)?(m|min|minor|maj|major|M)?$"));
    return re;
}

bool isBpmNumber(const QString& token, bool allowBpmSuffix)
{
    static const QRegularExpression num(QStringLiteral("^(\\d{2,3})(bpm)?$"), QRegularExpression::CaseInsensitiveOption);
    const auto m = num.match(token);
    if (!m.hasMatch() || (!allowBpmSuffix && !m.captured(2).isEmpty())) {
        return false;
    }
    const int v = m.captured(1).toInt();
    return v >= 70 && v <= 180 && !m.captured(1).startsWith(QLatin1Char('0'));
}

// Pitch class 0 = C .. 11 = B.
int pitchClass(QChar letter, const QString& accidental)
{
    static const int base[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    int pc = base[letter.toUpper().unicode() - 'A'];
    if (accidental == u"#" || accidental == u"sharp") {
        pc += 1;
    } else if (accidental == u"b" || accidental == u"flat") {
        pc += 11;
    }
    return pc % 12;
}

const char* const kOneShotCategories[] = { "kicks", "snares", "claps", "hi hats", "hihats", "hats", "cymbals",
                                           "impacts", "risers", "downlifters", "percussion", "one shots",
                                           "oneshots" };
}

SampleNameInfo parseSampleName(const QString& relativePath)
{
    SampleNameInfo info;
    const QStringList parts = relativePath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return info;
    }
    const QString file = parts.last();
    const int dot = file.lastIndexOf(QLatin1Char('.'));
    info.name = dot > 0 ? file.left(dot) : file;
    if (parts.size() > 1) {
        info.category = parts.at(0);
    }
    if (parts.size() > 2) {
        info.pack = parts.at(1);
    }

    // "Loopcloud" (the sample store's download folder) is a name, not a loop.
    const QString lowerPath = relativePath.toLower().replace(QStringLiteral("loopcloud"), QStringLiteral("lcloud"));
    static const QRegularExpression oneShotRe(QStringLiteral("one[\\s_-]?shot|oneshot|\\bshot\\b"));
    const bool oneShot = oneShotRe.match(lowerPath).hasMatch();

    // Tempo: "128bpm" / "bpm 128" anywhere; else (not for one-shots, whose
    // numbers are an index) the first 70-180 number standing on its own.
    const QString lowerName = info.name.toLower();
    static const QRegularExpression bpmAfter(QStringLiteral("(\\d{2,3}(?:\\.\\d+)?)\\s*bpm"));
    static const QRegularExpression bpmBefore(QStringLiteral("bpm\\s*(\\d{2,3})"));
    auto m = bpmAfter.match(lowerName);
    if (!m.hasMatch()) {
        m = bpmBefore.match(lowerName);
    }
    if (m.hasMatch()) {
        const double v = m.captured(1).toDouble();
        if (v >= 60.0 && v <= 200.0) {
            info.bpm = v;
        }
    }
    const QStringList tokens = info.name.split(separators(), Qt::SkipEmptyParts);
    if (info.bpm <= 0.0 && !oneShot) {
        for (const QString& t : tokens) {
            if (isBpmNumber(t, false)) {
                info.bpm = t.toInt();
                break;
            }
        }
    }

    // Key: "C#m", "Ebmin", "F#", "Am"; a bare capital letter only right next
    // to a tempo number ("HTR_135_C_Synth"), a bare lowercase one only right
    // after a number ("synth_oneshot_0895_e").
    const bool allCaps = info.name.toUpper() == info.name;
    for (int i = 0; i < tokens.size() && info.keyId == 0; ++i) {
        const auto k = keyToken().match(tokens.at(i));
        if (!k.hasMatch()) {
            continue;
        }
        const QString letter = k.captured(1);
        const QString accidental = k.captured(2);
        QString mode = k.captured(3);
        if (accidental.isEmpty() && mode.isEmpty()) {
            const QString prev = i > 0 ? tokens.at(i - 1) : QString();
            const QString next = i + 1 < tokens.size() ? tokens.at(i + 1) : QString();
            if (letter.at(0).isUpper()) {
                // Next to a tempo ("HTR_135_C_Synth"), or the last word right
                // after a number ("Audio_Horn_02_G").
                static const QRegularExpression number(QStringLiteral("^\\d+$"));
                const bool lastAfterNumber = i == tokens.size() - 1 && number.match(prev).hasMatch();
                if (!isBpmNumber(prev, true) && !isBpmNumber(next, true) && !lastAfterNumber) {
                    continue;
                }
            } else {
                // Lowercase: only after a number ("oneshot_0895_e") or before
                // wet/dry ("loop_ground_f_wet"); otherwise it is a word.
                static const QRegularExpression digits(QStringLiteral("^\\d+$"));
                const QString n = next.toLower();
                if (!digits.match(prev).hasMatch() && n != u"wet" && n != u"dry") {
                    continue;
                }
            }
        }
        if (mode == u"M") {
            mode = allCaps ? QStringLiteral("m") : QStringLiteral("maj");   // "DM" in an all-caps name is D minor
        }
        const bool minor = mode == u"m" || mode == u"min" || mode == u"minor";
        info.keyId = pitchClass(letter.at(0), accidental) + (minor ? 13 : 1);
    }

    if (oneShot) {
        info.type = SampleNameInfo::Type::OneShot;
    } else if (lowerPath.contains(QStringLiteral("loop"))) {
        info.type = SampleNameInfo::Type::Loop;
    } else {
        const QString category = info.category.toLower();
        for (const char* c : kOneShotCategories) {
            if (category == QLatin1String(c)) {
                info.type = SampleNameInfo::Type::OneShot;
                break;
            }
        }
    }
    return info;
}

int loopBars(double seconds, double bpm)
{
    if (seconds <= 0.0 || bpm <= 0.0) {
        return 0;
    }
    const double barSec = 4.0 * 60.0 / bpm;
    // A whole number of bars, or a little longer: "wet" loops carry their
    // reverb tail after the last bar (8 bars at 134 BPM is 14.3 s, the file
    // 16.1 s). The longest that fits wins.
    for (int bars : { 32, 16, 8, 4, 2, 1 }) {
        const double length = bars * barSec;
        if (seconds >= length * 0.97 - 0.02 && seconds <= length * 1.15 + 0.02) {
            return bars;
        }
    }
    return 0;
}

SampleNameInfo::Type guessTypeFromLength(double seconds, double bpm)
{
    return loopBars(seconds, bpm) > 0 ? SampleNameInfo::Type::Loop : SampleNameInfo::Type::OneShot;
}
}
