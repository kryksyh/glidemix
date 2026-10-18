/*
 * GlideMix
 */
#include "tempobpmtoolbaritem.h"

#include <cmath>

using namespace au::projectscene;

TempoBpmToolBarItem::TempoBpmToolBarItem(const muse::ui::UiAction& action,
                                         muse::uicomponents::ToolBarItemType::Type type, QObject* parent)
    : playback::PlaybackToolBarBPMItem(action, type, parent)
{
    if (tempoMap()) {
        tempoMap()->tempoMapChanged().onNotify(this, [this]() { updateValues(); });
    }
    if (playbackState()) {
        playbackState()->playbackPositionChanged().onReceive(this, [this](const muse::secs_t&) {
            if (tempoMap() && !tempoMap()->isConstant()) {
                updateValues();
            }
        });
    }
    updateValues();
}

void TempoBpmToolBarItem::updateValues()
{
    if (!tempoMap() || tempoMap()->isConstant() || !playbackState()) {
        playback::PlaybackToolBarBPMItem::updateValues();
        return;
    }
    // One decimal, so a ramp reads smoothly without flicker in the last digit.
    const double bpm = tempoMap()->bpmAt(playbackState()->playbackPosition().raw());
    setDisplayedValue(std::round(bpm * 10.0) / 10.0);
}
