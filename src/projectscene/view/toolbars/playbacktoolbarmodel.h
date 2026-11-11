/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include "framework/actions/iactionsdispatcher.h"
#include "framework/uicomponents/qml/Muse/UiComponents/abstracttoolbarmodel.h"

#include "framework/global/modularity/ioc.h"
#include "framework/ui/iuiactionsregister.h"
#include "framework/ui/iuiconfiguration.h"
#include "framework/ui/iuistate.h"

#include "context/iglobalcontext.h"
#include "context/iuicontextresolver.h"
#include "playback/iplaybackconfiguration.h"
#include "playback/iplaybackuistate.h"
#include "playback/iplaybackcontroller.h"
#include "record/irecordcontroller.h"

namespace au::project {
class IAudacityProject;
}

namespace au::projectscene {
class PlaybackToolBarModel : public muse::uicomponents::AbstractToolBarModel
{
    Q_OBJECT

    Q_PROPERTY(bool isEnabled READ isEnabled NOTIFY isEnabledChanged)
    //! GlideMix: the clocks (timecode, BPM, time signature) live in their
    //! own centered row; this model then serves either only them or the rest.
    Q_PROPERTY(bool clocksOnly READ clocksOnly WRITE setClocksOnly NOTIFY clocksOnlyChanged)
    //! GlideMix: 0 icon and text, 1 icon only, 2 text only (Customize Toolbar).
    Q_PROPERTY(int displayStyle READ displayStyle NOTIFY displayStyleChanged)
    //! GlideMix: the zoom sliders, shown at the right end of the toolbar
    //! beside the customize button rather than in the row.
    Q_PROPERTY(QVariantList rightItems READ rightItems NOTIFY rightItemsChanged)

    muse::GlobalInject<muse::ui::IUiConfiguration> uiConfiguration;
    muse::GlobalInject<playback::IPlaybackConfiguration> configuration;

    muse::ContextInject<muse::ui::IUiState> uiState { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> viewActions { this };
    muse::ContextInject<muse::ui::IUiActionsRegister> uiActionsRegister{ this };
    muse::ContextInject<context::IGlobalContext> context{ this };
    muse::ContextInject<context::IUiContextResolver> uicontextResolver { this };
    muse::ContextInject<playback::IPlaybackController> playbackController{ this };
    muse::ContextInject<playback::IPlaybackUiState> playbackUiState{ this };
    muse::ContextInject<record::IRecordController> recordController{ this };

public:
    explicit PlaybackToolBarModel(QObject* parent = nullptr);

    enum ItemType
    {
        PLAYBACK_LEVEL = muse::uicomponents::ToolBarItemType::USER_TYPE + 1,
        RECORD_LEVEL,
        PLAYBACK_TIME,
        PLAYBACK_BPM,
        PLAYBACK_TIME_SIGNATURE,
        PLAYBACK_CONTROL,
        PROJECT_CONTROL,
        SNAP,
        // GlideMix
        METRONOME,
        TEXT_TOGGLE,
        BROADCAST,
        SLIDER,
        //! GlideMix: Customize Toolbar's Flexible Space, as wide as the room left.
        FLEX_SPACE
    };
    Q_ENUM(ItemType)

    Q_INVOKABLE void load() override;
    Q_INVOKABLE void resetView();

    bool isEnabled() const;

    bool clocksOnly() const;
    void setClocksOnly(bool clocksOnly);
    QVariantList rightItems() const;

signals:
    void isEnabledChanged();
    void displayStyleChanged();
    void clocksOnlyChanged();
    void rightItemsChanged();

private:
    void reload();
    void setupProjectConnections(project::IAudacityProject& project);

    void onActionsStateChanges(const muse::actions::ActionCodeList& codes) override;

    void updateStates();
    void updatePlayState();
    void updateStopState();
    void updateRecordState();
    void updatePerformStopState();
    void updateLoopState();
    void updateClipGainAutomationState();
    void updateSplitState();
    void updateGlobalSpectrogramViewState();

    void updateToggleState(const muse::actions::ActionCode& actionCode, bool isOn);

    QColor themeColor(muse::ui::ThemeStyleKey key) const;

    void updateActions();

    muse::uicomponents::ToolBarItem* makeLocalItem(const muse::actions::ActionCode& actionCode);

    QList<muse::uicomponents::ToolBarItem*> m_rightItems;

    bool m_inited = false;
    bool m_clocksOnly = false;
    int m_displayStyle = 1;
public:
    int displayStyle() const { return m_displayStyle; }
private:
};
}
