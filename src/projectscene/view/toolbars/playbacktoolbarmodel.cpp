/*
* Audacity: A Digital Audio Editor
*/
#include "playbacktoolbarmodel.h"

#include <QQmlContext>
#include <QQmlEngine>

#include "framework/global/containers.h"

#include "internal/projectsceneuiactions.h"

#include "playback/view/toolbars/playbacktoolbarlevelitem.h"
#include "playback/view/toolbars/playbacktoolbarcontrolitem.h"
#include "playback/view/toolbars/playbacktoolbartimeitem.h"
#include "playback/view/toolbars/playbacktoolbarbpmitem.h"
#include "playback/view/toolbars/playbacktoolbartimesignatureitem.h"
#include "record/view/toolbars/playbacktoolbarrecordlevelitem.h"
#include "projectscene/view/toolbars/snaptoolbaritem.h"
#include "projectscene/view/toolbars/metronometoolbaritem.h"
#include "projectscene/view/toolbars/performtoolbaritem.h"
#include "projectscene/view/toolbars/broadcasttoolbaritem.h"
#include "projectscene/view/toolbars/tempobpmtoolbaritem.h"
#include "projectscene/view/toolbars/mixtapeslidertoolbaritem.h"

using namespace muse::uicomponents;
using namespace muse::ui;
using namespace muse::actions;
using namespace au::projectscene;
using namespace au::playback;

static const QString TOOLBAR_NAME("playbackToolBar");

static const ActionQuery PLAYBACK_TOGGLE_PLAY_PAUSE_QUERY("action://playback/toggle-play-pause");
static const ActionQuery PLAYBACK_STOP_QUERY("action://playback/stop");

static const ActionQuery RECORD_START_QUERY("action://record/start");
static const ActionQuery RECORD_PAUSE_QUERY("action://record/pause");
static const ActionQuery RECORD_STOP_QUERY("action://record/stop");
static const ActionQuery RECORD_LEVEL_QUERY("action://record/level");

static const ActionQuery PLAYBACK_REWIND_START_QUERY("action://playback/rewind-start");
static const ActionQuery PLAYBACK_REWIND_END_QUERY("action://playback/rewind-end");
static const ActionCode LOOP_ACTION_CODE("toggle-loop-region");

//! The red of PERFORM and of its Stop: the audience is listening. Kept in step
//! with the literal in PlaybackToolBar.qml's text-toggle component.
static const QColor PERFORM_COLOR("#E5484D");

static const ActionCode CLIP_GAIN_AUTOMATION_CODE("clip-gain");
static const ActionCode SPLIT_TOOL_ACTION_CODE("split-tool");

static const ActionCode TOGGLE_GLOBAL_SPECTROGRAM_VIEW_ACTION_CODE("action://trackedit/global-view-spectrogram");

static const ActionQuery PLAYBACK_LEVEL_QUERY("action://playback/level");
static const ActionCode PLAYBACK_TIME("playback-time");
static const ActionCode PLAYBACK_BPM("playback-bpm");
static const ActionCode PLAYBACK_TIME_SIGNATURE("playback-time-signature");

static const ActionCode SNAP_ACTION_CODE("snap");

static PlaybackToolBarModel::ItemType itemType(const ActionCode& actionCode)
{
    std::map<ActionCode, PlaybackToolBarModel::ItemType> types = {
        { PLAYBACK_LEVEL_QUERY.toString(), PlaybackToolBarModel::PLAYBACK_LEVEL },
        { PLAYBACK_TIME, PlaybackToolBarModel::PLAYBACK_TIME },
        { PLAYBACK_BPM, PlaybackToolBarModel::PLAYBACK_BPM },
        { PLAYBACK_TIME_SIGNATURE, PlaybackToolBarModel::PLAYBACK_TIME_SIGNATURE },
        { RECORD_LEVEL_QUERY.toString(), PlaybackToolBarModel::RECORD_LEVEL },
        { PLAYBACK_TOGGLE_PLAY_PAUSE_QUERY.toString(), PlaybackToolBarModel::PLAYBACK_CONTROL },
        { PLAYBACK_STOP_QUERY.toString(), PlaybackToolBarModel::PLAYBACK_CONTROL },
        { RECORD_START_QUERY.toString(), PlaybackToolBarModel::PLAYBACK_CONTROL },
        { PLAYBACK_REWIND_START_QUERY.toString(), PlaybackToolBarModel::PLAYBACK_CONTROL },
        { PLAYBACK_REWIND_END_QUERY.toString(), PlaybackToolBarModel::PLAYBACK_CONTROL },
        { LOOP_ACTION_CODE, PlaybackToolBarModel::PLAYBACK_CONTROL },
        { CLIP_GAIN_AUTOMATION_CODE, PlaybackToolBarModel::PLAYBACK_CONTROL },
        { SPLIT_TOOL_ACTION_CODE, PlaybackToolBarModel::PLAYBACK_CONTROL },
        { TOGGLE_GLOBAL_SPECTROGRAM_VIEW_ACTION_CODE, PlaybackToolBarModel::PLAYBACK_CONTROL },
        { SNAP_ACTION_CODE, PlaybackToolBarModel::SNAP },
        { "metronome", PlaybackToolBarModel::METRONOME },
        { "mixhouse-sync", PlaybackToolBarModel::TEXT_TOGGLE },
        { "mixhouse-tempo-lane", PlaybackToolBarModel::TEXT_TOGGLE },
        { "mixhouse-energy-lane", PlaybackToolBarModel::TEXT_TOGGLE },
        { "mixhouse-keylock", PlaybackToolBarModel::TEXT_TOGGLE },
        { "mixhouse-mix-now", PlaybackToolBarModel::TEXT_TOGGLE },
        { "mixhouse-automation-arm", PlaybackToolBarModel::TEXT_TOGGLE },
        { "mixhouse-reenable-automation", PlaybackToolBarModel::TEXT_TOGGLE },
        { "mixhouse-perform", PlaybackToolBarModel::TEXT_TOGGLE },
        { "mixhouse-broadcast", PlaybackToolBarModel::BROADCAST },
        { "mixhouse-reset-performance", PlaybackToolBarModel::PLAYBACK_CONTROL },
        { "mixtape-track-height", PlaybackToolBarModel::SLIDER },
        { "mixtape-zoom-slider", PlaybackToolBarModel::SLIDER }
    };

    return muse::value(types, actionCode, PlaybackToolBarModel::PROJECT_CONTROL);
}

PlaybackToolBarModel::PlaybackToolBarModel(QObject* parent)
    : AbstractToolBarModel(parent)
{
}

void PlaybackToolBarModel::load()
{
    if (!m_inited) {
        uiState()->toolConfigChanged(TOOLBAR_NAME).onNotify(this, [this]() {
            reload();
        });

        uiConfiguration()->currentThemeChanged().onNotify(this, [this]() {
            reload();
        });

        context()->currentProjectChanged().onNotify(this, [this]() {
            reload();
            emit isEnabledChanged();
            if (context()->currentProject()) {
                setupProjectConnections(*context()->currentProject());
            }
        });
        if (context()->currentProject()) {
            setupProjectConnections(*context()->currentProject());
        }

        playbackUiState()->playbackMeterPositionChanged().onNotify(this, [this]() {
            reload();
        });

        m_inited = true;
    }

    // GlideMix: a change can arrive (e.g. Perform mode ending on its own)
    // while this toolbar's QML is being torn down. New items then have no
    // context to get their services from, and building them crashed.
    const QQmlContext* qml = qmlContext(this);
    if (!qml || !qml->engine()) {
        return;
    }

    updateActions();

    AbstractToolBarModel::load();
}

void PlaybackToolBarModel::reload()
{
    load();
    updateStates();
}

void PlaybackToolBarModel::setupProjectConnections(project::IAudacityProject& project)
{
    const auto vs = project.viewState();
    vs->clipGainAutomationEnabled().ch.onReceive(this, [this](bool){ updateClipGainAutomationState(); });
    vs->splitToolEnabled().ch.onReceive(this, [this](bool){ updateSplitState(); });
    vs->globalSpectrogramToggleIsOnChanged().onNotify(this, [this] { updateGlobalSpectrogramViewState(); });
}

void PlaybackToolBarModel::onActionsStateChanges(const muse::actions::ActionCodeList& codes)
{
    if (uicontextResolver()->currentUiContext() == context::UiCtxDialogOpened) {
        return;
    }

    if (containsAction(codes, PLAYBACK_TOGGLE_PLAY_PAUSE_QUERY.toString())
        || containsAction(codes, RECORD_PAUSE_QUERY.toString())) {
        updatePlayState();
    }

    if (containsAction(codes, PLAYBACK_STOP_QUERY.toString()) || containsAction(codes, RECORD_STOP_QUERY.toString())) {
        updateStopState();
    }

    if (containsAction(codes, RECORD_START_QUERY.toString())) {
        updateRecordState();
    }

    if (containsAction(codes, LOOP_ACTION_CODE)) {
        updateLoopState();
    }

    if (containsAction(codes, "mixhouse-perform") || containsAction(codes, "mixhouse-reset-performance")) {
        updatePerformStopState();
    }

    // Secondary menus (right-click PERFORM) keep the state they were built
    // with unless refreshed here.
    for (ToolBarItem* toolBarItem : items()) {
        for (MenuItem* menuItem : toolBarItem->menuItems()) {
            const ActionCode code = menuItem->action().code;
            if (containsAction(codes, code)) {
                menuItem->setState(uiActionsRegister()->actionState(code));
            }
        }
    }

    // The zoom sliders are kept apart from items() (they sit at the right
    // end), so the base class never updates them: without this they kept
    // the disabled state they were made with before a project opened.
    for (ToolBarItem* item : m_rightItems) {
        const ActionCode code = item->action().code;
        if (containsAction(codes, code)) {
            item->setState(uiActionsRegister()->actionState(code));
        }
    }

    AbstractToolBarModel::onActionsStateChanges(codes);
}

void PlaybackToolBarModel::updateStates()
{
    updatePlayState();
    updateStopState();
    updateRecordState();
    updateLoopState();
    updatePerformStopState();
    updateClipGainAutomationState();
    updateSplitState();
    updateGlobalSpectrogramViewState();
}

void PlaybackToolBarModel::updatePlayState()
{
    PlaybackToolBarControlItem* item = dynamic_cast<PlaybackToolBarControlItem*>(findItemPtr(PLAYBACK_TOGGLE_PLAY_PAUSE_QUERY.toString()));

    if (item == nullptr) {
        return;
    }

    bool isPlaying = playbackController()->isPlaying();
    bool isPaused = playbackController()->isPaused();
    bool isRecording = recordController()->isRecording();
    bool isLeadIn = recordController()->isLeadInRecording();

    // During recording it pauses the recorder
    UiAction action = uiActionsRegister()->action(PLAYBACK_TOGGLE_PLAY_PAUSE_QUERY.toString());

    // During lead-in, show as playing (green background) since audio is playing back
    bool showAsPlaying = isPlaying || isLeadIn;

    bool leadInPaused = isLeadIn && isPaused;
    if ((showAsPlaying && !leadInPaused) || (isRecording && !isLeadIn)) {
        action.iconCode = IconCode::Code::PAUSE_FILL;
    }
    item->setAction(action);
    item->setSelected(showAsPlaying);

    QColor iconColor = themeColor(PLAY_COLOR);
    QColor backgroundColor = themeColor(BUTTON_COLOR);
    if (showAsPlaying) {
        iconColor = themeColor(FONT_PRIMARY_COLOR);
        backgroundColor = themeColor(PLAY_COLOR);
    } else if (isRecording) {
        iconColor = themeColor(FONT_PRIMARY_COLOR);
    }

    item->setIconColor(iconColor);
    item->setBackgroundColor(backgroundColor);
}

void PlaybackToolBarModel::updateStopState()
{
    PlaybackToolBarControlItem* item = dynamic_cast<PlaybackToolBarControlItem*>(findItemPtr(PLAYBACK_STOP_QUERY.toString()));

    if (item == nullptr) {
        return;
    }

    const UiAction action = uiActionsRegister()->action(PLAYBACK_STOP_QUERY.toString());
    item->setAction(action);
    const QColor iconColor = themeColor(FONT_PRIMARY_COLOR);
    item->setIconColor(iconColor);
}

//! The Stop beside PERFORM is red whenever there is a performance to stop, so
//! it reads as the partner of the red PERFORM rather than of ordinary playback.
void PlaybackToolBarModel::updatePerformStopState()
{
    auto* item = dynamic_cast<PlaybackToolBarControlItem*>(findItemPtr(ActionCode("mixhouse-reset-performance")));

    if (item == nullptr) {
        return;
    }

    const bool performing = uiActionsRegister()->actionState("mixhouse-perform").checked;
    const bool canStop = uiActionsRegister()->actionState("mixhouse-reset-performance").enabled;

    // Paused mid-performance: filled in, like a held stop, so the square stays
    // legible against the red.
    const bool paused = canStop && !performing;
    item->setSelected(paused);
    item->setIconColor(paused ? themeColor(BACKGROUND_PRIMARY_COLOR)
                       : canStop ? PERFORM_COLOR : themeColor(FONT_PRIMARY_COLOR));
    item->setBackgroundColor(paused ? PERFORM_COLOR : themeColor(BUTTON_COLOR));
}

void PlaybackToolBarModel::updateRecordState()
{
    PlaybackToolBarControlItem* item = dynamic_cast<PlaybackToolBarControlItem*>(findItemPtr(RECORD_START_QUERY.toString()));

    if (item == nullptr) {
        return;
    }

    bool isRecording = recordController()->isRecording();
    bool isLeadIn = recordController()->isLeadInRecording();

    // During lead-in pre-roll, the button should not appear as actively recording
    item->setSelected(isRecording && !isLeadIn);

    QColor iconColor = themeColor(RECORD_COLOR);
    QColor backgroundColor = themeColor(BUTTON_COLOR);
    if (isRecording && !isLeadIn) {
        iconColor = themeColor(BACKGROUND_PRIMARY_COLOR);
        backgroundColor = themeColor(RECORD_COLOR);
    }

    item->setIconColor(iconColor);
    item->setBackgroundColor(backgroundColor);
}

void PlaybackToolBarModel::updateLoopState()
{
    PlaybackToolBarControlItem* item = dynamic_cast<PlaybackToolBarControlItem*>(findItemPtr(LOOP_ACTION_CODE));

    if (item == nullptr) {
        return;
    }

    bool isLooping = playbackController()->isLoopRegionActive();
    item->setSelected(isLooping);

    item->setIconColor(themeColor(FONT_PRIMARY_COLOR));
    item->setBackgroundColor(themeColor(isLooping ? ACCENT_COLOR : BUTTON_COLOR));
}

void PlaybackToolBarModel::updateToggleState(const ActionCode& actionCode, bool isOn)
{
    PlaybackToolBarControlItem* item = dynamic_cast<PlaybackToolBarControlItem*>(findItemPtr(actionCode));

    if (item == nullptr) {
        return;
    }

    item->setSelected(isOn);
    item->setBackgroundColor(themeColor(isOn ? ACCENT_COLOR : BUTTON_COLOR));
}

void PlaybackToolBarModel::updateClipGainAutomationState()
{
    auto prj = context()->currentProject();

    if (!prj) {
        return;
    }

    updateToggleState(CLIP_GAIN_AUTOMATION_CODE, prj->viewState()->clipGainAutomationEnabled().val);
}

void PlaybackToolBarModel::updateSplitState()
{
    auto prj = context()->currentProject();

    if (!prj) {
        return;
    }

    updateToggleState(SPLIT_TOOL_ACTION_CODE, prj->viewState()->splitToolEnabled().val);
}

void PlaybackToolBarModel::updateGlobalSpectrogramViewState()
{
    auto prj = context()->currentProject();

    if (!prj) {
        return;
    }

    updateToggleState(TOGGLE_GLOBAL_SPECTROGRAM_VIEW_ACTION_CODE, prj->viewState()->globalSpectrogramToggleIsOn());
}

QColor PlaybackToolBarModel::themeColor(muse::ui::ThemeStyleKey key) const
{
    return uiConfiguration()->currentTheme().values.value(key).value<QColor>();
}

void PlaybackToolBarModel::updateActions()
{
    ToolBarItemList items;
    QList<ToolBarItem*> rightItems;

    muse::ui::ToolConfig playbackConfig = uiState()->toolConfig(TOOLBAR_NAME,
                                                                ProjectSceneUiActions::defaultPlaybackToolBarConfig());

    for (const muse::ui::ToolConfig::Item& citem : playbackConfig.items) {
        if (!citem.show) {
            continue;
        }

        //! The level item always stays in the toolbar. It meters the Perform
        //! (audience) output while the side-bar panel meters the headphone cue,
        //! so both outputs are visible at once the way a DJ mixer shows master
        //! and cue side by side. It is therefore no longer skipped when the
        //! meter position is SideBar.

        if (citem.isSeparator()) {
            // No separators in the clocks row, and never two in a row once
            // the clocks are taken out of this one.
            if (!m_clocksOnly && !items.isEmpty() && items.last()->type() != ToolBarItemType::SEPARATOR) {
                items << AbstractToolBarModel::makeSeparator();
            }
            continue;
        }

        const ItemType type = itemType(citem.intent);
        // GlideMix: the one meter is the side panel's (Room or Cue);
        // this item's show flag still opens and closes that panel.
        if (type == PLAYBACK_LEVEL) {
            continue;
        }
        const bool isClock = type == PLAYBACK_TIME || type == PLAYBACK_BPM || type == PLAYBACK_TIME_SIGNATURE;
        if (isClock != m_clocksOnly) {
            continue;
        }

        ToolBarItem* item = makeLocalItem(citem.intent);
        if (!item) {
            continue;
        }

        item->setIsTransparent(false);

        if (type == SLIDER) {
            rightItems << item;
            continue;
        }
        items << item;
    }

    // GlideMix: Snap sits between KEYLOCK and PERFORM, whatever order a
    // workspace saved -- it is used with SYNC/KEYLOCK while arranging.
    int snapAt = -1, keylockAt = -1;
    for (int i = 0; i < items.size(); ++i) {
        const QString code = QString::fromStdString(items[i]->action().code);
        if (code == QStringLiteral("snap")) {
            snapAt = i;
        } else if (code == QStringLiteral("mixhouse-keylock")) {
            keylockAt = i;
        }
    }
    if (snapAt >= 0 && keylockAt >= 0) {
        ToolBarItem* snap = items.takeAt(snapAt);
        if (snapAt < keylockAt) {
            --keylockAt;
        }
        items.insert(keylockAt + 1, snap);
        // No separator left doubled or dangling where Snap was.
        for (int i = items.size() - 1; i > 0; --i) {
            if (items[i]->type() == ToolBarItemType::SEPARATOR && items[i - 1]->type() == ToolBarItemType::SEPARATOR) {
                items.removeAt(i);
            }
        }
        while (!items.isEmpty() && items.last()->type() == ToolBarItemType::SEPARATOR) {
            items.removeLast();
        }
    }

    // GlideMix: the performance's Stop sits right after PERFORM, whatever
    // order a workspace saved (it used to come after the broadcast button).
    int performAt = -1, stopAt = -1;
    for (int i = 0; i < items.size(); ++i) {
        const QString code = QString::fromStdString(items[i]->action().code);
        if (code == QStringLiteral("mixhouse-perform")) {
            performAt = i;
        } else if (code == QStringLiteral("mixhouse-reset-performance")) {
            stopAt = i;
        }
    }
    if (performAt >= 0 && stopAt >= 0 && stopAt != performAt + 1) {
        ToolBarItem* stop = items.takeAt(stopAt);
        if (stopAt < performAt) {
            --performAt;
        }
        items.insert(performAt + 1, stop);
    }

    setItems(items);

    const QList<ToolBarItem*> old = m_rightItems;
    m_rightItems = rightItems;
    emit rightItemsChanged();
    for (ToolBarItem* item : old) {
        item->deleteLater();
    }
}

QVariantList PlaybackToolBarModel::rightItems() const
{
    QVariantList list;
    for (ToolBarItem* item : m_rightItems) {
        list << QVariant::fromValue(item);
    }
    return list;
}

ToolBarItem* PlaybackToolBarModel::makeLocalItem(const ActionCode& actionCode)
{
    PlaybackToolBarModel::ItemType type = itemType(actionCode);

    if (type == PlaybackToolBarModel::PROJECT_CONTROL) {
        ToolBarItem* item = AbstractToolBarModel::makeItem(actionCode);
        item->setType(static_cast<ToolBarItemType::Type>(type));
        return item;
    }

    ToolBarItem* result = nullptr;
    const UiAction& action = uiActionsRegister()->action(actionCode);

    switch (type) {
    case PlaybackToolBarModel::PLAYBACK_LEVEL:
        result = new PlaybackToolBarLevelItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    case PlaybackToolBarModel::PLAYBACK_TIME:
        result = new PlaybackToolBarTimeItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    case PlaybackToolBarModel::PLAYBACK_BPM:
        result = new TempoBpmToolBarItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    case PlaybackToolBarModel::PLAYBACK_TIME_SIGNATURE:
        result = new PlaybackToolBarTimeSignatureItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    case PlaybackToolBarModel::RECORD_LEVEL:
        result = new record::PlaybackToolBarRecordLevelItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    case PlaybackToolBarModel::PLAYBACK_CONTROL: {
        PlaybackToolBarControlItem* item = new PlaybackToolBarControlItem(action, static_cast<ToolBarItemType::Type>(type), this);
        item->setIconColor(themeColor(FONT_PRIMARY_COLOR));
        item->setBackgroundColor(themeColor(BUTTON_COLOR));
        result = std::move(item);
        break;
    }
    case PlaybackToolBarModel::SNAP:
        result = new projectscene::SnapToolBarItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    case PlaybackToolBarModel::METRONOME:
        result = new projectscene::MetronomeToolBarItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    case PlaybackToolBarModel::BROADCAST:
        result = new projectscene::BroadcastToolBarItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    case PlaybackToolBarModel::TEXT_TOGGLE:
        if (actionCode == "mixhouse-perform") {
            result = new projectscene::PerformToolBarItem(action, static_cast<ToolBarItemType::Type>(type), this);
            break;
        }
        result = new ToolBarItem(action, static_cast<ToolBarItemType::Type>(type), this);
        result->setShowTitle(true);
        break;
    case PlaybackToolBarModel::SLIDER:
        result = new projectscene::MixtapeSliderToolBarItem(action, static_cast<ToolBarItemType::Type>(type), this);
        break;
    default:
        break;
    }

    if (result) {
        result->setState(uiActionsRegister()->actionState(actionCode));
    }

    return result;
}

bool PlaybackToolBarModel::clocksOnly() const
{
    return m_clocksOnly;
}

void PlaybackToolBarModel::setClocksOnly(bool clocksOnly)
{
    if (m_clocksOnly == clocksOnly) {
        return;
    }
    m_clocksOnly = clocksOnly;
    emit clocksOnlyChanged();
    if (m_inited) {
        reload();
    }
}

bool PlaybackToolBarModel::isEnabled() const
{
    return context()->currentProject() != nullptr;
}
