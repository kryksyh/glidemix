/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "modularity/ioc.h"
#include "playback/iplayback.h"
#include "context/iglobalcontext.h"

#include "projectscene/iprojectviewstate.h"
#include "../../types/projectscenetypes.h"

namespace au::projectscene {
using Direction = DirectionType::Direction;
class SnapTimeFormatter : public muse::Contextable
{
    muse::ContextInject<playback::IPlayback> playback{ this };
    muse::ContextInject<context::IGlobalContext> globalContext{ this };   // GlideMix: Smart snap reads the zoom

public:
    SnapTimeFormatter(const muse::modularity::ContextPtr& ctx)
        : muse::Contextable(ctx) {}

    muse::secs_t snapTime(muse::secs_t time, const Snap& snap, trackedit::TimeSignature timeSig) const;
    muse::secs_t singleStep(muse::secs_t time, const Snap& snap, Direction direction, trackedit::TimeSignature timeSig) const;

    muse::secs_t snapToItem(muse::secs_t time, muse::secs_t tolerance, const std::set<muse::secs_t> itemsBoundaries) const;

private:
    double snapTypeMultiplier(SnapType type, bool triplets, trackedit::TimeSignature timeSig) const;

    double beatDuration(trackedit::TimeSignature timeSig) const;
    double barMultiplier(trackedit::TimeSignature timeSig) const;
    double smartMultiplier(trackedit::TimeSignature timeSig) const;
    double beatsMultiplier(SnapType type, bool triplets, trackedit::TimeSignature timeSig) const;

    double determineStep(double multiplier, Direction direction) const;
};
}
