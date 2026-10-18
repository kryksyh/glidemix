/*
 * GlideMix
 */
#include "mixhousephrasedetector.h"

#include <algorithm>
#include <cmath>

using namespace au::mixhouse;

namespace {
constexpr int kBlockBars = 8;

enum class Kind {
    Drop,    ///< loud, with bass
    Break,   ///< no bass
    Middle,  ///< bass, but not at full level
};

struct Block {
    double start = 0.0;
    double end = 0.0;
    double energy = 0.0;
    double bass = 0.0;
    double bars = 0.0;   ///< length in bars (a segment's weight when merging)
    Kind kind = Kind::Middle;
    QString label;
};

//! The overview stores each column twice (two channels); the louder counts.
double meanOver(const std::vector<uint8_t>& values, double secPerColumn, double from, double to)
{
    const int columns = static_cast<int>(values.size() / 2);
    if (columns <= 0) {
        return 0.0;
    }
    const int a = std::clamp(static_cast<int>(from / secPerColumn), 0, columns - 1);
    const int b = std::clamp(static_cast<int>(std::ceil(to / secPerColumn)), a + 1, columns);
    double sum = 0.0;
    for (int c = a; c < b; ++c) {
        sum += std::max(values[2 * c], values[2 * c + 1]);
    }
    return sum / (b - a);
}

double percentile(std::vector<double> values, double q)
{
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const size_t i = std::min(values.size() - 1, static_cast<size_t>(q * (values.size() - 1) + 0.5));
    return values[i];
}
}

std::vector<MixHousePhrase> au::mixhouse::detectPhrases(const MixHouseWaveformOverview& overview, double durationSec,
                                                        double bpm, double downbeatSec)
{
    if (!overview.valid || overview.all.size() < 4 || durationSec <= 0.0 || bpm <= 0.0) {
        return {};
    }
    const std::vector<uint8_t>& bassValues = overview.low.size() == overview.all.size() ? overview.low : overview.all;
    const double secPerColumn = durationSec / (overview.all.size() / 2);
    const double barSec = 240.0 / bpm;

    // Bars counted from the downbeat, back to the first whole bar in the file.
    double origin = downbeatSec >= 0.0 ? downbeatSec : 0.0;
    origin -= std::floor(origin / barSec) * barSec;
    std::vector<double> barEnergy, barBass;
    for (double t = origin; t + barSec * 0.5 < durationSec; t += barSec) {
        const double end = std::min(durationSec, t + barSec);
        barEnergy.push_back(meanOver(overview.all, secPerColumn, t, end));
        barBass.push_back(meanOver(bassValues, secPerColumn, t, end));
    }
    const int bars = static_cast<int>(barEnergy.size());
    if (bars < 3 * kBlockBars) {
        return {};
    }
    const double energyScale = std::max(1.0, percentile(barEnergy, 0.95));
    const double bassScale = std::max(1.0, percentile(barBass, 0.95));

    // The 8-bar blocks start on bar 1 of a phrase, which the downbeat does not
    // always mark: of the eight possible starts, the one whose block edges see
    // the biggest changes wins -- that is where the song changes section.
    const auto blocksFrom = [&](int offset) {
        std::vector<Block> blocks;
        for (int first = offset - kBlockBars; first < bars; first += kBlockBars) {
            const int a = std::max(0, first);
            const int b = std::min(bars, first + kBlockBars);
            if (b - a <= 0) {
                continue;
            }
            Block block;
            block.start = origin + a * barSec;
            block.end = std::min(durationSec, origin + b * barSec);
            for (int i = a; i < b; ++i) {
                block.energy += barEnergy[i] / energyScale;
                block.bass += barBass[i] / bassScale;
            }
            block.energy /= (b - a);
            block.bass /= (b - a);
            block.bars = b - a;
            // A stub of a bar or two joins its neighbour.
            if (b - a <= 2 && !blocks.empty()) {
                blocks.back().end = block.end;
                blocks.back().bars += block.bars;
                continue;
            }
            blocks.push_back(block);
        }
        return blocks;
    };
    std::vector<Block> blocks;
    double bestScore = -1.0;
    for (int offset = 0; offset < kBlockBars; ++offset) {
        std::vector<Block> candidate = blocksFrom(offset);
        double score = 0.0;
        for (size_t i = 1; i < candidate.size(); ++i) {
            score += std::abs(candidate[i].energy - candidate[i - 1].energy)
                     + std::abs(candidate[i].bass - candidate[i - 1].bass);
        }
        if (score > bestScore) {
            bestScore = score;
            blocks = std::move(candidate);
        }
    }
    if (blocks.size() < 3) {
        return {};
    }
    blocks.front().start = 0.0;
    blocks.back().end = durationSec;

    // Sections, then names. Neighbouring blocks that sound alike are one
    // section: the closest pair is merged until every pair differs clearly,
    // and a section shorter than 16 bars joins the neighbour it is closest to
    // (rekordbox's phrases are rarely shorter). Judging 8-bar blocks one by
    // one instead made every small swell a new phrase.
    const auto distance = [](const Block& a, const Block& b) {
        return std::abs(a.energy - b.energy) + 1.5 * std::abs(a.bass - b.bass);
    };
    const auto merge = [](Block& into, const Block& from) {
        const double total = into.bars + from.bars;
        into.energy = (into.energy * into.bars + from.energy * from.bars) / total;
        into.bass = (into.bass * into.bars + from.bass * from.bars) / total;
        into.start = std::min(into.start, from.start);
        into.end = std::max(into.end, from.end);
        into.bars = total;
    };
    constexpr double kSameSection = 0.12;
    for (;;) {
        size_t best = 0;
        double bestDistance = 1e9;
        for (size_t i = 1; i < blocks.size(); ++i) {
            const double d = distance(blocks[i - 1], blocks[i]);
            if (d < bestDistance) {
                bestDistance = d;
                best = i;
            }
        }
        if (blocks.size() < 2 || bestDistance >= kSameSection) {
            break;
        }
        merge(blocks[best - 1], blocks[best]);
        blocks.erase(blocks.begin() + best);
    }
    for (bool changed = true; changed && blocks.size() > 1;) {
        changed = false;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (blocks[i].bars >= 2 * kBlockBars - 0.5) {
                continue;
            }
            size_t other = i == 0 ? 1 : i - 1;
            if (i > 0 && i + 1 < blocks.size() && distance(blocks[i], blocks[i + 1]) < distance(blocks[i], blocks[i - 1])) {
                other = i + 1;
            }
            const size_t keep = std::min(i, other);
            merge(blocks[keep], blocks[std::max(i, other)]);
            blocks.erase(blocks.begin() + std::max(i, other));
            changed = true;
            break;
        }
    }
    if (blocks.size() < 2) {
        return {};
    }

    // Names. Only the loudest sections with the bass in are drops (Chorus);
    // the rest is named by where it sits, as rekordbox does: before the first
    // drop the Intro, then the build (Up); after a drop the break (Down), then
    // the build into the next one (Up); after the last drop the Outro.
    double topEnergy = 0.0, topBass = 0.0;
    for (const Block& b : blocks) {
        topEnergy = std::max(topEnergy, b.energy);
        topBass = std::max(topBass, b.bass);
    }
    if (topBass <= 0.05) {
        return {};   // no bass anywhere: nothing to go on
    }
    // Tuned on 1,377 DJ-labelled tracks (named the same 65%
    // of the time): 8-bar Intro and Outro, drops from 80% of the loudest.
    constexpr double kIntroBars = 8.0;
    constexpr double kOutroBars = 8.0;
    constexpr double kDropLevel = 0.8;
    int firstDrop = -1, lastDrop = -1;
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        Block& b = blocks[i];
        const bool bassOn = b.bass >= 0.55 * topBass;
        b.kind = !bassOn ? Kind::Break : (b.energy >= kDropLevel * topEnergy ? Kind::Drop : Kind::Middle);
        if (b.kind == Kind::Drop) {
            firstDrop = firstDrop < 0 ? i : firstDrop;
            lastDrop = i;
        }
    }
    if (firstDrop < 0) {
        return {};
    }

    // A section split at a bar, `bars` from its start: the first part gets
    // `first`, the rest `rest`.
    std::vector<Block> named;
    const auto splitInto = [&](const Block& b, double bars, const QString& first, const QString& rest) {
        if (bars <= 0.0 || bars >= b.bars - 0.5) {
            Block whole = b;
            whole.label = bars <= 0.0 ? rest : first;
            named.push_back(whole);
            return;
        }
        Block a = b, c = b;
        a.end = b.start + bars * barSec;
        a.bars = bars;
        a.label = first;
        c.start = a.end;
        c.bars = b.bars - bars;
        c.label = rest;
        named.push_back(a);
        named.push_back(c);
    };
    for (int i = 0; i < static_cast<int>(blocks.size()); ++i) {
        const Block& b = blocks[i];
        if (b.kind == Kind::Drop && i == 0 && b.bars > kIntroBars + 8) {
            splitInto(b, kIntroBars, QStringLiteral("Intro"), QStringLiteral("Chorus"));
        } else if (b.kind == Kind::Drop) {
            Block d = b;
            d.label = QStringLiteral("Chorus");
            named.push_back(d);
        } else if (i < firstDrop) {
            // The intro, then the build to the first drop.
            const double introLeft = i == 0 ? std::min(b.bars, i + 1 == firstDrop ? kIntroBars : 2 * kIntroBars) : 0.0;
            splitInto(b, introLeft, QStringLiteral("Intro"), QStringLiteral("Up"));
        } else if (i > lastDrop) {
            // The break after the last drop, then the last bars as the Outro.
            splitInto(b, std::max(0.0, b.bars - kOutroBars), QStringLiteral("Down"), QStringLiteral("Outro"));
        } else {
            // Between drops: the break, then the build into the next drop.
            const double downBars = b.bars >= 4 * kBlockBars ? 16.0 : (b.bars >= 2 * kBlockBars ? 8.0 : 0.0);
            splitInto(b, downBars, QStringLiteral("Down"), QStringLiteral("Up"));
        }
    }
    // A track that ends on a drop still gets its last bars as the Outro.
    if (!named.empty() && named.back().label == u"Chorus" && named.back().bars > kOutroBars + 8) {
        Block last = named.back();
        named.pop_back();
        splitInto(last, last.bars - kOutroBars, QStringLiteral("Chorus"), QStringLiteral("Outro"));
    }
    blocks = std::move(named);

    // Blocks with the same name make one phrase; repeats are numbered as
    // rekordbox numbers them (Chorus 1, Chorus 2, Up 1..3).
    std::vector<MixHousePhrase> phrases;
    int chorus = 0, up = 0;
    for (const Block& b : blocks) {
        if (!phrases.empty() && phrases.back().label.startsWith(b.label)) {
            phrases.back().endSec = b.end;
            continue;
        }
        QString label = b.label;
        if (label == u"Chorus") {
            label += QStringLiteral(" %1").arg(std::min(++chorus, 2));
        } else if (label == u"Up") {
            label += QStringLiteral(" %1").arg(std::min(++up, 3));
        } else if (label == u"Intro" || label == u"Outro") {
            label += QStringLiteral(" 1");
        }
        MixHousePhrase phrase;
        phrase.startSec = b.start;
        phrase.endSec = b.end;
        phrase.label = label;
        phrase.color = mixHousePhraseColor(label);
        phrases.push_back(phrase);
    }
    return phrases;
}
