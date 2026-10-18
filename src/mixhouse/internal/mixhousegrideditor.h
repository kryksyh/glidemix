/*
 * GlideMix
 */
#pragma once

#include "actions/iactionsdispatcher.h"
#include "modularity/ioc.h"

#include "../imixhousegrideditor.h"

namespace au::mixhouse {
class MixHouseGridEditor : public IMixHouseGridEditor, public muse::Contextable
{
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher { this };

public:
    explicit MixHouseGridEditor(const muse::modularity::ContextPtr& ctx);

    void openTrack(int trackId) override;
    int currentTrackId() const override { return m_trackId; }
    muse::async::Channel<int> trackOpened() const override { return m_trackOpened; }

    static constexpr const char* kWorkspaceName = "Beatgrid Editor";
    static constexpr const char* kPanelName = "mixHouseGridEditorPanel";

private:
    int m_trackId = -1;
    muse::async::Channel<int> m_trackOpened;
};
}
