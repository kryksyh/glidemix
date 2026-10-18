/*
 * GlideMix
 */
#include "mixhousekeymatch.h"

#include <cmath>

#include "trackedit/itrackeditproject.h"

using namespace au::mixhouse;

namespace {
// Mixxx's ChromaticKey ids (proto/keys.proto): 1..12 = C..B major,
// 13..24 = C..B minor.
bool keyIsMajor(int key) { return key > 0 && key < 13; }
int keyToTonic(int key) { return key - (keyIsMajor(key) ? 1 : 13); }
int tonicToKey(int tonic, bool major) { return tonic + (major ? 1 : 13); }
bool keyIsValid(int key) { return key >= 1 && key <= 24; }

// KeyUtils' s_openKeyToKeys: OpenKey number -> {major, minor}.
constexpr int kOpenKeyToKeys[13][2] = {
    { 0, 0 },
    { 1, 22 },   // 1: C major, A minor
    { 8, 17 },   // 2: G, Em
    { 3, 24 },   // 3: D, Bm
    { 10, 19 },  // 4: A, F#m
    { 5, 14 },   // 5: E, C#m
    { 12, 21 },  // 6: B, G#m
    { 7, 16 },   // 7: F#, Ebm
    { 2, 23 },   // 8: Db, Bbm
    { 9, 18 },   // 9: Ab, Fm
    { 4, 13 },   // 10: Eb, Cm
    { 11, 20 },  // 11: Bb, Gm
    { 6, 15 },   // 12: F, Dm
};

int keyToOpenKeyNumber(int key)
{
    for (int n = 1; n <= 12; ++n) {
        if (kOpenKeyToKeys[n][0] == key || kOpenKeyToKeys[n][1] == key) {
            return n;
        }
    }
    return 0;
}

int shortestStepsToKey(int key, int target)
{
    if (!keyIsValid(key) || !keyIsValid(target)) {
        return 0;
    }
    int steps = keyToTonic(target) - keyToTonic(key);
    if (steps > 6) {
        steps -= 12;
    } else if (steps < -6) {
        steps += 12;
    }
    return steps;
}
}

QColor IMixHouseKeyMatch::openKeyColor(int openKeyNumber)
{
    // Copied from mixxx/src/util/color/predefinedcolorpalettes.cpp (kMIKKeyColor1..12),
    // indexed by Open Key number as KeyUtils::keyToColor does.
    static const QColor kMikKeyColors[12] = {
        QColor(0xF1, 0x7E, 0xDB), QColor(0xD1, 0x8B, 0xFD), QColor(0x9E, 0xB4, 0xFD),
        QColor(0x4D, 0xD3, 0xF8), QColor(0x01, 0xEA, 0xEC), QColor(0x00, 0xEE, 0xCB),
        QColor(0x20, 0xEF, 0x7F), QColor(0x7F, 0xF4, 0x48), QColor(0xE0, 0xCA, 0x6D),
        QColor(0xFD, 0xA0, 0x78), QColor(0xFF, 0x86, 0x93), QColor(0xFD, 0x7E, 0xB3)
    };
    return (openKeyNumber >= 1 && openKeyNumber <= 12) ? kMikKeyColors[openKeyNumber - 1] : QColor();
}

QColor IMixHouseKeyMatch::keyColor(int key)
{
    return keyIsValid(key) ? openKeyColor(keyToOpenKeyNumber(key)) : QColor();
}

QString IMixHouseKeyMatch::openKey(int key)
{
    if (!keyIsValid(key)) {
        return QString();
    }
    return QString::number(keyToOpenKeyNumber(key)) + (keyIsMajor(key) ? QStringLiteral("d") : QStringLiteral("m"));
}

QString IMixHouseKeyMatch::traditional(int key)
{
    static const char* const names[25] = {
        "",
        "C", "Db", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B",
        "Cm", "C#m", "Dm", "Ebm", "Em", "Fm", "F#m", "Gm", "G#m", "Am", "Bbm", "Bm"
    };
    return keyIsValid(key) ? QString::fromLatin1(names[key]) : QString();
}

QString IMixHouseKeyMatch::keyText(int key, MixHouseKeyNotation notation)
{
    if (!keyIsValid(key)) {
        return QString();
    }
    switch (notation) {
    case MixHouseKeyNotation::Lancelot: return camelot(key);
    case MixHouseKeyNotation::LancelotTraditional: return camelot(key) + QLatin1Char('/') + traditional(key);
    case MixHouseKeyNotation::OpenKey: return openKey(key);
    case MixHouseKeyNotation::OpenKeyTraditional: return openKey(key) + QLatin1Char('/') + traditional(key);
    case MixHouseKeyNotation::Traditional: return traditional(key);
    }
    return camelot(key);
}

QString IMixHouseKeyMatch::camelot(int key)
{
    if (!keyIsValid(key)) {
        return QString();
    }
    // Lancelot is OpenKey rotated counter-clockwise by 5 (KeyUtils).
    int number = keyToOpenKeyNumber(key) - 5;
    if (number < 1) {
        number += 12;
    }
    return QString::number(number) + (keyIsMajor(key) ? QStringLiteral("B") : QStringLiteral("A"));
}

int IMixHouseKeyMatch::scaleKeySteps(int key, int steps)
{
    if (!keyIsValid(key)) {
        return 0;
    }
    int tonic = (keyToTonic(key) + steps) % 12;
    if (tonic < 0) {
        tonic += 12;
    }
    return tonicToKey(tonic, keyIsMajor(key));
}

int IMixHouseKeyMatch::shortestStepsToCompatibleKey(int key, int target)
{
    // KeyUtils::shortestStepsToCompatibleKey, line for line.
    if (!keyIsValid(key) || !keyIsValid(target) || key == target) {
        return 0;
    }
    // A mode mismatch converts to the relative key first: same notes, so no
    // up-to-6-semitone chipmunk shift to reach the other mode.
    if (keyIsMajor(key) != keyIsMajor(target)) {
        const int openKey = keyToOpenKeyNumber(target);
        target = kOpenKeyToKeys[openKey][keyIsMajor(target) ? 1 : 0];
    }
    // Compatible keys are 0 (tonic) or +-5 (4th / 5th) semitones away.
    const int distance = shortestStepsToKey(key, target);
    if (distance < -2) {
        return 5 + distance;
    }
    if (distance > 2) {
        return -5 + distance;
    }
    return distance;
}

MixHouseKeyMatch::MixHouseKeyMatch(const muse::modularity::ContextPtr& iocCtx)
    : muse::Contextable(iocCtx)
{
}

int MixHouseKeyMatch::libraryKeyId(const QString& clipTitle) const
{
    if (clipTitle.isEmpty() || !library() || !library()->isAvailable()) {
        return 0;
    }
    // An imported clip is named after its file (same lookup as the beat grids).
    IMixHouseLibrary::TrackQuery query;
    query.fileBaseName = clipTitle;
    query.limit = 1;
    MixHouseTrackList found = library()->tracks(query);
    if (found.empty()) {
        found = externalLibraryTracks(lexicon().get(), rekordboxXml().get(), query);   // loaded from Lexicon DJ / rekordbox
    }
    return found.empty() ? 0 : found.front().keyId;
}

IMixHouseKeyMatch::ClipKeyInfo MixHouseKeyMatch::keyInfo(const trackedit::ClipKey& key) const
{
    ClipKeyInfo info;
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project) {
        return info;
    }
    const trackedit::Clip clip = project->clip(key);
    info.keyId = libraryKeyId(clip.title.toQString());
    info.shiftSemitones = static_cast<int>(std::lround(clip.pitch / 100.0));
    info.effectiveKeyId = scaleKeySteps(info.keyId, info.shiftSemitones);
    return info;
}

IMixHouseKeyMatch::Match MixHouseKeyMatch::match(const trackedit::ClipKey& key) const
{
    Match result;
    const auto project = globalContext() ? globalContext()->currentTrackeditProject() : nullptr;
    if (!project || !transitions()) {
        return result;
    }

    // The partner is the clip this one mixes with. Prefer the track it mixes
    // INTO (it is the incoming side), else the one mixing into it.
    trackedit::ClipKey partner;
    for (const IMixHouseTransitions::Zone& zone : transitions()->zones()) {
        if (zone.incoming == key) {
            partner = zone.outgoing;
            break;
        }
        if (zone.outgoing == key && !partner.isValid()) {
            partner = zone.incoming;
        }
    }
    if (!partner.isValid()) {
        result.myKeyId = keyInfo(key).keyId;
        return result;
    }

    const ClipKeyInfo mine = keyInfo(key);
    const ClipKeyInfo theirs = keyInfo(partner);
    result.hasPartner = true;
    result.partnerTitle = project->clip(partner).title.toQString();
    result.myKeyId = mine.keyId;
    result.partnerKeyId = theirs.effectiveKeyId;
    result.steps = shortestStepsToCompatibleKey(mine.keyId, theirs.effectiveKeyId);
    return result;
}

bool MixHouseKeyMatch::transpose(const trackedit::ClipKey& key, int semitones)
{
    if (!trackeditInteraction()) {
        return false;
    }
    return semitones == 0 ? trackeditInteraction()->resetClipPitch(key)
                          : trackeditInteraction()->changeClipPitch(key, semitones * 100);
}
