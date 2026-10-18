/*
* Audacity: A Digital Audio Editor
*/
#pragma once

#include <QObject>

#include "global/async/asyncable.h"

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "projectscene/iprojectsceneuistate.h"
#include "actions/iactionsdispatcher.h"

namespace au::projectscene {
class TracksViewStateModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    // Context of user interaction
    Q_PROPERTY(int tracksVerticalOffset READ tracksVerticalOffset NOTIFY tracksVerticalOffsetChanged FINAL)
    //! GlideMix: goes up whenever a track's height or place changes, so a binding
    //! that calls trackVerticalPosition() / trackHeight() can read it to follow.
    Q_PROPERTY(int tracksLayoutRevision READ tracksLayoutRevision NOTIFY tracksLayoutChanged FINAL)
    Q_PROPERTY(bool tracksVerticalScrollLocked READ tracksVerticalScrollLocked NOTIFY tracksVerticalScrollLockedChanged FINAL)
    Q_PROPERTY(int tracksVerticalScrollPadding READ tracksVerticalScrollPadding FINAL CONSTANT)

    Q_PROPERTY(bool altPressed READ altPressed NOTIFY altPressedChanged FINAL)
    Q_PROPERTY(bool ctrlPressed READ ctrlPressed NOTIFY ctrlPressedChanged FINAL)
    Q_PROPERTY(bool keyboardMoveActive READ keyboardMoveActive NOTIFY keyboardMoveActiveChanged FINAL)

    Q_PROPERTY(bool snapEnabled READ snapEnabled NOTIFY snapEnabledChanged FINAL)
    Q_PROPERTY(bool clipGainAutomationEnabled READ clipGainAutomationEnabled NOTIFY clipGainAutomationEnabledChanged FINAL)
    //! GlideMix: the lanes are black (the colour waveform's own
    //! background), so they drop the light wash they wear over the theme's grey.
    Q_PROPERTY(bool blackCanvas READ blackCanvas NOTIFY blackCanvasChanged FINAL)

    muse::ContextInject<context::IGlobalContext> globalContext{ this };
    muse::ContextInject<IProjectSceneUiState> projectSceneUiState { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher{ this };

public:
    TracksViewStateModel(QObject* parent = nullptr);

    Q_INVOKABLE void init();
    bool clipGainAutomationEnabled() const;
    bool blackCanvas() const;
    Q_INVOKABLE void toggleClipGainAutomation();
    Q_INVOKABLE void showClipGainAutomation();
    Q_INVOKABLE void showPluginAutomation();

    bool tracksVerticalScrollLocked() const;
    int tracksVerticalScrollPadding() const;

    // context of user interaction
    int tracksVerticalOffset() const;
    bool altPressed() const;
    bool ctrlPressed() const;
    bool keyboardMoveActive() const;

    Q_INVOKABLE void changeTracksVerticalOffset(int deltaY);
    Q_INVOKABLE void setMouseY(double y);

    Q_INVOKABLE void insureVerticallyVisible(int viewContentY, int viewHeight, int itemY, int itemHeight);

    Q_INVOKABLE void requestVerticalScrollLock();
    Q_INVOKABLE void requestVerticalScrollUnlock();

    Q_INVOKABLE bool snapEnabled() const;
    Q_INVOKABLE bool isItemEditInProgress() const;
    Q_INVOKABLE trackedit::TrackId trackAtPosition(double x, double y) const;

    int tracksLayoutRevision() const { return m_tracksLayoutRevision; }
    Q_INVOKABLE int trackHeight(trackedit::TrackId trackId) const;
    Q_INVOKABLE int trackVerticalPosition(trackedit::TrackId trackId) const;

signals:
    void blackCanvasChanged();
    void clipGainAutomationEnabledChanged();
    void tracksVerticalOffsetChanged();
    void tracksLayoutChanged();
    void tracksVerticalScrollLockedChanged();
    void altPressedChanged();
    void ctrlPressedChanged();
    void keyboardMoveActiveChanged();

    void snapEnabledChanged();

private:
    static constexpr int m_tracksVerticalScrollPadding = 228;

    IProjectViewStatePtr viewState() const;

    muse::ValCh<int> m_tracksVerticalOffset;
    muse::ValCh<bool> m_tracksVerticalScrollLocked;

    muse::ValCh<bool> m_altPressed;
    muse::ValCh<bool> m_ctrlPressed;
    muse::ValCh<bool> m_keyboardMoveActive;

    bool m_snapEnabled;
    int m_tracksLayoutRevision = 0;
};
}
