/*
 * MixHouse DJ 2
 */
#include "mixhouselink.h"
#include "mixhouseuiactions.h"

#include "context/uicontext.h"
#include "context/shortcutcontext.h"
#include "types/translatablestring.h"

using namespace muse;
using namespace muse::ui;
using namespace au::mixhouse;

namespace {
const muse::actions::ActionCode ADD_FILES_ACTION_CODE("mixhouse-add-files");
const muse::actions::ActionCode ADD_FOLDER_ACTION_CODE("mixhouse-add-folder");
const muse::actions::ActionCode IMPORT_REKORDBOX_ACTION_CODE("mixhouse-import-rekordbox");
const muse::actions::ActionCode IMPORT_ITUNES_ACTION_CODE("mixhouse-import-itunes");
const muse::actions::ActionCode SYNC_ACTION_CODE("mixhouse-sync");
const muse::actions::ActionCode TEMPO_LANE_ACTION_CODE("mixhouse-tempo-lane");
const muse::actions::ActionCode ENERGY_LANE_ACTION_CODE("mixhouse-energy-lane");
const muse::actions::ActionCode LINK_ACTION_CODE("mixhouse-link");
}

const UiActionList MixHouseUiActions::m_actions = {
    // Grouped under one "Add to Library" submenu in AppMenuModel::makeFileMenu()
    // (src/appshell/qml/Audacity/AppShell/appmenumodel.cpp) -- the same
    // Track/Folder/Playlist-import grouping the brief pointed at in rekordbox's
    // own File > Import submenu.
    UiAction(ADD_FILES_ACTION_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Add File(s)…"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Add one or more audio files to the music library")
             ),
    UiAction(ADD_FOLDER_ACTION_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Add Folder…"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Recursively add every audio file under a folder to the music library")
             ),
    UiAction(IMPORT_REKORDBOX_ACTION_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Import from Rekordbox XML…"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Import tracks and playlists from a rekordbox.xml collection export")
             ),
    UiAction(IMPORT_ITUNES_ACTION_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Import from iTunes Library XML…"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Import tracks and playlists from an iTunes/Apple Music Library.xml export")
             ),
    // Beat grid tools (MixHouseBeatGrids), the parked MixHouse DJ app's grid keys.
    UiAction("mixhouse-grid-earlier",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Nudge beat grid earlier"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Move the selected tracks' beat grid 5 ms earlier")
             ),
    UiAction("mixhouse-grid-later",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Nudge beat grid later"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Move the selected tracks' beat grid 5 ms later")
             ),
    UiAction("mixhouse-bar-earlier",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Move bar start one beat earlier"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Bar 1 of the selected tracks starts one beat earlier")
             ),
    UiAction("mixhouse-bar-later",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Move bar start one beat later"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Bar 1 of the selected tracks starts one beat later")
             ),
    UiAction("mixhouse-grid-faster",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Beat grid faster"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Tighten the selected tracks' beat grid by 0.01 BPM")
             ),
    UiAction("mixhouse-grid-slower",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Beat grid slower"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Widen the selected tracks' beat grid by 0.01 BPM")
             ),
    UiAction("mixhouse-grid-undo",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Undo beat grid change"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Undo the last beat grid change")
             ),
    UiAction("mixhouse-save-as-sample",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Save as sample…"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Save the selected part of the song as a WAV sample in the Samples tab")
             ),
    UiAction("mixhouse-save-stem-drums",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: a stem in the selection's Stems submenu; keep it short
             TranslatableString("action", "Drums"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Save the drums of the selected part as a sample")
             ),
    UiAction("mixhouse-save-stem-bass",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: a stem in the selection's Stems submenu; keep it short
             TranslatableString("action", "Bass"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Save the bass of the selected part as a sample")
             ),
    UiAction("mixhouse-save-stem-vocals",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: a stem in the selection's Stems submenu; keep it short
             TranslatableString("action", "Vocals"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Save the vocals of the selected part as a sample")
             ),
    UiAction("mixhouse-save-stem-other",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: a stem in the selection's Stems submenu; keep it short
             TranslatableString("action", "Other"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Save everything but drums, bass and vocals of the selected part as a sample")
             ),
    UiAction("mixhouse-save-stems-all",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: a stem in the selection's Stems submenu; keep it short
             TranslatableString("action", "All four"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Save all four stems of the selected part as samples")
             ),
    UiAction("mixhouse-remove-drums",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: in the selection's Stems submenu; keep it short
             TranslatableString("action", "Remove drums"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Take the drums out of the selected part of the song")
             ),
    UiAction("mixhouse-remove-bass",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: in the selection's Stems submenu; keep it short
             TranslatableString("action", "Remove bass"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Take the bass out of the selected part of the song")
             ),
    UiAction("mixhouse-remove-vocals",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: in the selection's Stems submenu; keep it short
             TranslatableString("action", "Remove vocals"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Take the vocals out of the selected part of the song")
             ),
    UiAction("mixhouse-remove-other",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: in the selection's Stems submenu; keep it short
             TranslatableString("action", "Remove other"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Take everything but drums, bass and vocals out of the selected part of the song")
             ),
    UiAction("mixhouse-split-selection-stems",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: in the selection's Stems submenu; keep it short
             TranslatableString("action", "Split into stems"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Move the selected part of the song onto four stem lanes under it")
             ),
    UiAction("mixhouse-split-grid-jumps",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Split at grid jumps"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Split the song where its beats jump off its grid and put each part back on the bar")
             ),
    UiAction("mixhouse-anchor-in-playhead",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Set IN anchor at playhead"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Move the selected song's IN anchor to the playhead")
             ),
    UiAction("mixhouse-anchor-out-playhead",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Set OUT anchor at playhead"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Move the selected song's OUT anchor to the playhead")
             ),
    UiAction("mixhouse-edit-beatgrid",
             au::context::UiCtxProjectOpened,
             au::context::CTX_PROJECT_FOCUSED,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Open in Beatgrid editor"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Edit this track's beat grid and phrases in the Beatgrid workspace")
             ),
    UiAction("mixhouse-broadcast",
             au::context::UiCtxProjectOpened,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Broadcast"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Stream what is playing to your radio server"),
             Checkable::Yes
             ),
    UiAction("mixhouse-perform",
             au::context::UiCtxProjectOpened,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Perform"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description",
                                "Play the mix to the audience (outputs 1-2, red playhead) or pause it; Play becomes the headphone preview (outputs 3-4)"),
             Checkable::Yes
             ),
    UiAction("mixhouse-reset-performance",
             au::context::UiCtxProjectOpened,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Reset performance position"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Stop the performance and put the red playhead back at the start"),
             IconCode::Code::STOP_FILL
             ),
    UiAction("mixhouse-perform-from-playhead",
             au::context::UiCtxProjectOpened,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Perform from the playhead"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Move the red playhead to the white playhead (while the performance is paused)")
             ),
    UiAction("mixhouse-mix-now",
             au::context::UiCtxProjectOpened,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Mix now"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Mix now: brings the next song in at the next bar (moves it forward on the timeline)")
             ),
    UiAction("mixhouse-automation-arm",
             au::context::UiCtxProjectOpened,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Arm"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description",
                                "Record automation: while it plays, every control you move (EQ, effects) is written into its line"),
             Checkable::Yes
             ),
    UiAction("mixhouse-reenable-automation",
             au::context::UiCtxProjectOpened,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Re-enable"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description",
                                "Re-enable automation: moving a control by hand stops its line (it turns grey). Click to make every stopped line play again"),
             Checkable::Yes
             ),
    UiAction("mixhouse-keylock",
             au::context::UiCtxProjectOpened,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Keylock"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Keylock: tempo changes keep the pitch. Off plays like a turntable"),
             Checkable::Yes
             ),
    UiAction(SYNC_ACTION_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Sync"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Sync: songs you move lock to the beat grid (to a bar when Snap is off), so mixes stay in time"),
             Checkable::Yes
             ),
    UiAction(TEMPO_LANE_ACTION_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Tempo"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Tempo lane: shows the mix's tempo line above the tracks. Drag its points to speed the mix up or slow it down"),
             Checkable::Yes
             ),
    UiAction(ENERGY_LANE_ACTION_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: shown as a menu item or a button label; keep it short
             TranslatableString("action", "Energy"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description", "Energy lane: shows the mix's energy (1-10) over time, so you see how the set builds"),
             Checkable::Yes
             ),
    UiAction(LINK_ACTION_CODE,
             au::context::UiCtxAny,
             au::context::CTX_ANY,
             //: Action title: the Ableton Link button (Ableton's guidelines: "Link")
             TranslatableString("action", "Link"),
             //: Action description: shown as a tooltip; can be a full sentence
             TranslatableString("action_description",
                                "Ableton Link: other apps on your network (Resolume, Traktor, Ableton Live...) follow the mix's tempo and beat while it plays"),
             Checkable::Yes
             ),
};

void MixHouseUiActions::init()
{
    if (MixHouseLink* link = MixHouseLink::instance()) {
        link->changed().onNotify(this, [this]() { m_actionCheckedChanged.send({ LINK_ACTION_CODE }); });
    }
    if (libraryConfiguration()) {
        libraryConfiguration()->settingsChanged().onNotify(this, [this]() {
            m_actionCheckedChanged.send({ SYNC_ACTION_CODE, TEMPO_LANE_ACTION_CODE, ENERGY_LANE_ACTION_CODE, "mixhouse-keylock" });
        });
    }
}

const UiActionList& MixHouseUiActions::actionsList() const
{
    return m_actions;
}

bool MixHouseUiActions::actionEnabled(const UiAction& act) const
{
    if (act.code == "mixhouse-perform-from-playhead") {
        // The red playhead only moves while the audience isn't listening.
        return !m_performance || !m_performance->performing();
    }
    if (act.code == "mixhouse-reenable-automation") {
        return m_automationOverridden;
    }
    if (act.code == "mixhouse-reset-performance") {
        // Nothing to stop until the audience has been played to.
        return m_performance && m_performance->performanceActive();
    }
    // Unlike most File-menu actions, the rest don't depend on a project being
    // open -- they write straight to the library database.
    return true;
}

void MixHouseUiActions::setAutomationArmed(bool armed)
{
    m_automationArmed = armed;
    m_actionCheckedChanged.send({ "mixhouse-automation-arm" });
}

void MixHouseUiActions::setAutomationOverridden(bool overridden)
{
    if (overridden == m_automationOverridden) {
        return;
    }
    m_automationOverridden = overridden;
    m_actionCheckedChanged.send({ "mixhouse-reenable-automation" });
    m_actionEnabledChanged.send({ "mixhouse-reenable-automation" });
}

void MixHouseUiActions::setPerformance(std::shared_ptr<playback::IPerformance> performance)
{
    m_performance = std::move(performance);
    if (m_performance) {
        m_performance->performanceStateChanged().onNotify(this, [this]() {
            m_actionCheckedChanged.send({ "mixhouse-perform" });
            m_actionEnabledChanged.send({ "mixhouse-perform-from-playhead", "mixhouse-reset-performance" });
        });
    }
}

bool MixHouseUiActions::actionChecked(const UiAction& act) const
{
    if (act.code == "mixhouse-perform") {
        return m_performance && m_performance->performing();
    }
    if (act.code == "mixhouse-broadcast") {
        return m_broadcaster() && m_broadcaster()->state() == broadcast::IBroadcaster::State::Live;
    }
    if (act.code == SYNC_ACTION_CODE) {
        return libraryConfiguration() && libraryConfiguration()->syncEnabled();
    }
    if (act.code == "mixhouse-keylock") {
        return libraryConfiguration() && libraryConfiguration()->keylockEnabled();
    }
    if (act.code == "mixhouse-reenable-automation") {
        return m_automationOverridden;   // lit, as Ableton's Control Bar button lights up
    }
    if (act.code == "mixhouse-automation-arm") {
        return m_automationArmed;
    }
    if (act.code == TEMPO_LANE_ACTION_CODE) {
        return libraryConfiguration() && libraryConfiguration()->tempoLaneVisible();
    }
    if (act.code == ENERGY_LANE_ACTION_CODE) {
        return libraryConfiguration() && libraryConfiguration()->energyLaneVisible();
    }
    if (act.code == LINK_ACTION_CODE) {
        return MixHouseLink::instance() && MixHouseLink::instance()->enabled();
    }
    return false;
}

muse::async::Channel<muse::actions::ActionCodeList> MixHouseUiActions::actionEnabledChanged() const
{
    return m_actionEnabledChanged;
}

muse::async::Channel<muse::actions::ActionCodeList> MixHouseUiActions::actionCheckedChanged() const
{
    return m_actionCheckedChanged;
}
