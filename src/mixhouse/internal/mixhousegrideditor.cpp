/*
 * GlideMix
 */
#include "mixhousegrideditor.h"

#include <QString>

#include "log.h"

using namespace au::mixhouse;

MixHouseGridEditor::MixHouseGridEditor(const muse::modularity::ContextPtr& ctx)
    : muse::Contextable(ctx)
{
}

void MixHouseGridEditor::openTrack(int trackId)
{
    if (trackId < 0) {
        return;
    }
    m_trackId = trackId;
    if (dispatcher()) {
        // The workspace module's own "select-workspace" (Workspace menu).
        dispatcher()->dispatch("select-workspace", muse::actions::ActionData::make_arg1<std::string>(kWorkspaceName));
        dispatcher()->dispatch("dock-set-open",
                               muse::actions::ActionData::make_arg2<QString, bool>(QString::fromLatin1(kPanelName), true));
    }
    LOGI() << "Beatgrid editor: track " << trackId;
    m_trackOpened.send(trackId);
}
