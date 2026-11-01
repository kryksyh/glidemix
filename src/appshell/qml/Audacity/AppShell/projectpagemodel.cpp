/*
* Audacity: A Digital Audio Editor
*/
#include "projectpagemodel.h"

#include "framework/global/log.h"
#include "framework/global/async/async.h"
#include "framework/dockwindow/idockwindow.h"

#include "internal/applicationuiactions.h"
#include "projectscene/internal/projectsceneuiactions.h"

using namespace au::appshell;
using namespace muse::actions;

static const ActionQuery PLAYBACK_LEVEL_QUERY("action://playback/level");

ProjectPageModel::ProjectPageModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ProjectPageModel::init()
{
    TRACEFUNC;

    if (!m_inited) {
        for (const ActionCode& actionCode : ApplicationUiActions::toggleDockActions().keys()) {
            DockName dockName = ApplicationUiActions::toggleDockActions()[actionCode];
            dispatcher()->reg(this, actionCode, [=]() { toggleDock(dockName); });
        }

        uiState()->toolConfigChanged(playbackToolBarName()).onNotify(this, [this]() {
            updatePlaybackMeterVisibility();
        });

        playbackUiState()->playbackMeterPositionChanged().onNotify(this, [this]() {
            updatePlaybackMeterVisibility();
        });

        m_inited = true;
    }

    updatePlaybackMeterVisibility();

    //! GlideMix: the Project tab joins the library's tabs only once the library
    //! is docked. Open from the start, both panels were taken for tabs of each
    //! other before either was docked (DockWindow::loadPanels) and floated.
    //! Once per run, so a tab closed by hand stays closed.
    if (!m_projectTabOpened) {
        muse::async::Async::call(this, [this]() {
            const auto window = dockWindowProvider() ? dockWindowProvider()->window() : nullptr;
            if (!window || m_projectTabOpened || !window->isDockOpen(MIXHOUSE_LIBRARY_PANEL_NAME)) {
                return;
            }
            m_projectTabOpened = true;
            if (!window->isDockOpen(MIXHOUSE_PROJECT_PANEL_NAME)) {
                dispatcher()->dispatch("dock-set-open",
                                       ActionData::make_arg2<QString, bool>(MIXHOUSE_PROJECT_PANEL_NAME, true));
            }
        });
    }
}

void ProjectPageModel::updatePlaybackMeterVisibility()
{
    const auto toolConfig = uiState()->toolConfig(playbackToolBarName(),
                                                  au::projectscene::ProjectSceneUiActions::defaultPlaybackToolBarConfig());
    const auto it = std::find_if(toolConfig.items.begin(), toolConfig.items.end(),
                                 [](const muse::ui::ToolConfig::Item& item) {
        return item.intent == PLAYBACK_LEVEL_QUERY.toString();
    });

    if (it != toolConfig.items.end()) {
        //! Both meters are shown together: the top bar carries the Perform
        //! (audience) output and this side panel the headphone cue, so the two
        //! outputs can be watched at once. The position setting therefore no
        //! longer hides the panel -- only the toolbar item's own show flag does.
        const bool meterPanelVisible = it->show;
        muse::async::Async::call(this, [this, meterPanelVisible]() {
            dispatcher()->dispatch("dock-set-open", ActionData::make_arg2<QString, bool>(playbackMeterPanelName(), meterPanelVisible));
        });
    }
}

QString ProjectPageModel::projectToolBarName() const
{
    return PROJECT_TOOLBAR_NAME;
}

QString ProjectPageModel::playbackToolBarName() const
{
    return PLAYBACK_TOOLBAR_NAME;
}

QString ProjectPageModel::undoRedoToolBarName() const
{
    return UNDO_REDO_TOOLBAR_NAME;
}

QString ProjectPageModel::workspacesToolBarName() const
{
    return WORKSPACES_TOOLBAR_NAME;
}

QString ProjectPageModel::tracksPanelName() const
{
    return TRACKS_PANEL_NAME;
}

QString ProjectPageModel::historyPanelName() const
{
    return HISTORY_PANEL_NAME;
}

QString ProjectPageModel::playbackMeterPanelName() const
{
    return PLAYBACK_METER_PANEL_NAME;
}

QString ProjectPageModel::statusBarName() const
{
    return PROJECT_STATUSBAR_NAME;
}

void ProjectPageModel::toggleDock(const QString& name)
{
    dispatcher()->dispatch("dock-toggle", ActionData::make_arg1<QString>(name));
}
