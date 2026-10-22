/*
 * GlideMix
 */
#include "mixhousephrasecues.h"

#include <algorithm>
#include <cmath>

#include <QRegularExpression>

namespace au::mixhouse {
QString phraseSection(const QString& label)
{
    static const QRegularExpression number(QStringLiteral("\\s*\\d+$"));
    QString section = label.trimmed();
    section.remove(number);
    return section;
}

std::vector<MixHouseCue> phraseCues(const std::vector<MixHousePhrase>& phrases, double firstBeatSec,
                                    double beatLengthSec, int maxHotCues)
{
    const bool grid = beatLengthSec > 0.0;
    const auto onBeat = [&](double sec) {
        if (!grid) {
            return std::max(0.0, sec);
        }
        // Nothing before the first beat: a cue there would start off the grid.
        double beat = firstBeatSec + std::max(0.0, std::round((sec - firstBeatSec) / beatLengthSec)) * beatLengthSec;
        while (beat < 0.0) {
            beat += beatLengthSec;   // a grid that starts before the file does
        }
        return beat;
    };
    const double minSectionSec = grid ? 8.0 * beatLengthSec : 3.0;
    struct Section {
        double startSec = 0.0;
        double endSec = 0.0;
        QString name;
        QColor color;
    };
    std::vector<Section> sections;
    for (const MixHousePhrase& p : phrases) {
        const QString name = phraseSection(p.label);
        if (name.isEmpty() || p.endSec <= p.startSec) {
            continue;
        }
        if (!sections.empty() && sections.back().name == name) {
            sections.back().endSec = std::max(sections.back().endSec, p.endSec);
            continue;
        }
        sections.push_back({ p.startSec, p.endSec, name, p.color });
    }
    for (Section& s : sections) {
        s.startSec = onBeat(s.startSec);
    }
    // A section under two bars (a one-bar intro before the build) is not
    // worth a cue of its own: it joins the next one, named after the longer.
    for (size_t i = 0; i + 1 < sections.size();) {
        Section& a = sections[i];
        Section& b = sections[i + 1];
        if (a.endSec - a.startSec >= minSectionSec) {
            ++i;
            continue;
        }
        if (b.endSec - b.startSec > a.endSec - a.startSec) {
            a.name = b.name;
            a.color = b.color;
        }
        a.endSec = b.endSec;
        sections.erase(sections.begin() + static_cast<std::ptrdiff_t>(i + 1));
    }
    // Two sections joined above can now share a name.
    for (size_t i = 1; i < sections.size();) {
        if (sections[i].name == sections[i - 1].name) {
            sections[i - 1].endSec = sections[i].endSec;
            sections.erase(sections.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
    // Too many: drop the shortest sections between the first and the last
    // (the intro and the outro are where a DJ mixes, they always stay).
    while (static_cast<int>(sections.size()) > std::max(maxHotCues, 2)) {
        size_t shortest = 1;
        for (size_t i = 2; i + 1 < sections.size(); ++i) {
            const double len = sections[i].endSec - sections[i].startSec;
            if (len < sections[shortest].endSec - sections[shortest].startSec) {
                shortest = i;
            }
        }
        sections.erase(sections.begin() + static_cast<std::ptrdiff_t>(shortest));
    }

    std::vector<MixHouseCue> cues;
    cues.reserve(sections.size() * 2);
    for (size_t i = 0; i < sections.size(); ++i) {
        MixHouseCue cue;
        cue.name = sections[i].name;
        cue.hotCueIndex = static_cast<int>(i);
        cue.startSec = sections[i].startSec;
        cue.color = sections[i].color;
        cues.push_back(cue);
    }
    for (const Section& s : sections) {
        MixHouseCue memory;
        memory.name = s.name;
        memory.startSec = s.startSec;
        cues.push_back(memory);
    }
    return cues;
}
}
