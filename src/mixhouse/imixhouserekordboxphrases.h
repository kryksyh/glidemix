/*
 * GlideMix
 */
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>
#include <vector>

#include "async/notification.h"
#include "modularity/imoduleinterface.h"

#include "mixhousewaveform.h"

namespace au::mixhouse {
/// A phrase of a track as rekordbox analysed it (Intro, Up, Chorus, ...).
struct MixHousePhrase {
    double startSec = 0.0;   ///< seconds into the file
    double endSec = 0.0;
    double fillStartSec = -1.0;   ///< rekordbox's fill-in: from here to endSec (-1 = none)
    QString label;           ///< e.g. "Intro", "Verse 2"
    QColor color;
};

/// rekordbox's colour for a phrase name (Intro red, Up purple, Down khaki,
/// Chorus green, Outro steel blue, ...).
QColor mixHousePhraseColor(const QString& label);

/// The phrase names rekordbox uses, for the Beatgrid editor's phrase picker.
QStringList mixHousePhraseLabels();

/// rekordbox's beat grid of a track, on OUR decoder's clock.
struct MixHouseRekordboxGrid {
    bool valid = false;            ///< false: not analysed, or the tempo changes
    double bpm = 0.0;
    double firstBeatSec = 0.0;     ///< first grid beat at or after 0 s
    double firstDownbeatSec = 0.0; ///< first beat rekordbox counts as beat 1 of a bar, at or after 0 s
};

/// rekordbox's phrase analysis, read from its own analysis files
/// (~/Library/Pioneer/rekordbox/share/PIONEER/USBANLZ on macOS,
/// %APPDATA%\Pioneer\rekordbox\share\PIONEER\USBANLZ on Windows: PSSI
/// phrases, PQTZ beat grid, PPTH file name), read-only. Tracks are found by
/// file name, told apart by tempo when names repeat.
class IMixHouseRekordboxPhrases : MODULE_EXPORT_INTERFACE
{
    INTERFACE_ID(IMixHouseRekordboxPhrases)

public:
    virtual ~IMixHouseRekordboxPhrases() = default;

    /// Phrases of the audio file at `filePath` (empty when rekordbox hasn't
    /// analysed it, or while the analysis files are still being indexed).
    /// `bpmHint` > 0 picks between analyses of files with the same name.
    virtual std::vector<MixHousePhrase> phrases(const QString& filePath, double bpmHint) const = 0;

    /// rekordbox's constant beat grid of the file (PQTZ), moved by
    /// timingOffsetSec so its beats land on the kicks we decode.
    virtual MixHouseRekordboxGrid beatGrid(const QString& filePath, double bpmHint) const = 0;

    /// How much later rekordbox's times are than our decoder's for this
    /// file (MP3s by encoder case, as Mixxx's RekordboxFeature measures it);
    /// subtract it from any rekordbox time. 0 for other formats.
    virtual double timingOffsetSec(const QString& filePath) const = 0;

    /// rekordbox's colour waveform of the file (PWV5), as a library overview
    /// (height -> all, red/green/blue -> low/mid/high); invalid when there's
    /// none.
    virtual MixHouseWaveformOverview waveformOverview(const QString& filePath, double bpmHint) const = 0;

    /// rekordbox's scrolling-deck colour waveform (PWV5) at its own 150
    /// columns a second, every column kept: what a timeline lane draws, where
    /// the whole-track Preview above would be one colour block every 5 px.
    virtual MixHouseWaveformOverview waveformDetail(const QString& filePath, double bpmHint) const = 0;

    /// Fires once the analysis files are indexed (phrases may appear).
    virtual muse::async::Notification ready() const = 0;
};
}
