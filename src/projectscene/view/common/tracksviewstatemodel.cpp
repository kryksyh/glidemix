/*
* Audacity: A Digital Audio Editor
*/
#include "tracksviewstatemodel.h"

using namespace au::projectscene;
using namespace au::project;

TracksViewStateModel::TracksViewStateModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

IProjectViewStatePtr TracksViewStateModel::viewState() const
{
    IAudacityProjectPtr prj = globalContext()->currentProject();
    return prj ? prj->viewState() : nullptr;
}

void TracksViewStateModel::init()
{
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        init();
    }, muse::async::Asyncable::Mode::SetReplace);

    if (const auto uiState = projectSceneUiState()) {
        uiState->laneBackgroundChanged().onNotify(this, [this]() {
            emit blackCanvasChanged();
        }, muse::async::Asyncable::Mode::SetReplace);
    }

    IProjectViewStatePtr vs = viewState();
    emit clipGainAutomationEnabledChanged();
    if (!vs) {
        return;
    }

    vs->clipGainAutomationEnabled().ch.onReceive(this, [this](bool) {
        emit clipGainAutomationEnabledChanged();
    }, muse::async::Asyncable::Mode::SetReplace);

    const auto bumpLayout = [this]() {
        ++m_tracksLayoutRevision;
        emit tracksLayoutChanged();
    };
    vs->totalTrackHeight().ch.onReceive(this, [bumpLayout](int) { bumpLayout(); }, muse::async::Asyncable::Mode::SetReplace);
    if (const auto project = globalContext()->currentTrackeditProject()) {
        project->trackMoved().onReceive(this, [bumpLayout](const trackedit::Track&, int) { bumpLayout(); },
                                        muse::async::Asyncable::Mode::SetReplace);
        project->tracksChanged().onReceive(this, [bumpLayout](const std::vector<trackedit::Track>&) { bumpLayout(); },
                                           muse::async::Asyncable::Mode::SetReplace);
    }

    m_tracksVerticalOffset = vs->tracksVerticalOffset();
    m_tracksVerticalOffset.ch.onReceive(this, [this](int y) {
        m_tracksVerticalOffset.val = y;
        // GlideMix: told to QML after this send has finished. QML scrolling
        // the lanes creates lane headers, each with its own model that
        // subscribes to this channel; subscribing in the middle of the send
        // crashed the app (the 4th song dropped, 2026-10-12).
        QMetaObject::invokeMethod(this, [this]() { emit tracksVerticalOffsetChanged(); }, Qt::QueuedConnection);
    }, muse::async::Asyncable::Mode::SetReplace);

    m_tracksVerticalScrollLocked = vs->tracksVerticalScrollLocked();
    m_tracksVerticalScrollLocked.ch.onReceive(this, [this](bool locked) {
        m_tracksVerticalScrollLocked.val = locked;
        emit tracksVerticalScrollLockedChanged();
    }, muse::async::Asyncable::Mode::SetReplace);

    m_snapEnabled = vs->isSnapEnabled();
    vs->snap().ch.onReceive(this, [this](const Snap& snap) {
        if (m_snapEnabled != snap.enabled) {
            m_snapEnabled = snap.enabled;
            emit snapEnabledChanged();
        }
    }, muse::async::Asyncable::Mode::SetReplace);

    m_altPressed = vs->altPressed();
    m_altPressed.ch.onReceive(this, [this](bool v) {
        m_altPressed.val = v;
        emit altPressedChanged();
    }, muse::async::Asyncable::Mode::SetReplace);

    m_ctrlPressed = vs->ctrlPressed();
    m_ctrlPressed.ch.onReceive(this, [this](bool v) {
        m_ctrlPressed.val = v;
        emit ctrlPressedChanged();
    }, muse::async::Asyncable::Mode::SetReplace);

    m_keyboardMoveActive = vs->keyboardMoveActive();
    m_keyboardMoveActive.ch.onReceive(this, [this](bool v) {
        m_keyboardMoveActive.val = v;
        emit keyboardMoveActiveChanged();
    }, muse::async::Asyncable::Mode::SetReplace);
}

bool TracksViewStateModel::clipGainAutomationEnabled() const
{
    const auto vs = viewState();
    return vs && vs->clipGainAutomationEnabled().val;
}

void TracksViewStateModel::toggleClipGainAutomation()
{
    if (viewState()) {
        // Use the toolbar action, including its spectrogram handling.
        dispatcher()->dispatch("clip-gain");
    }
}

void TracksViewStateModel::showClipGainAutomation()
{
    const auto vs = viewState();
    if (!vs) {
        return;
    }

    // Keep the last plug-in/parameter identity so the effects panel can
    // restore it, but give the track's volume lane visual priority.
    auto focus = vs->pluginAutomationFocus();
    if (focus.visible) {
        focus.visible = false;
        vs->setPluginAutomationFocus(std::move(focus));
    }

    if (!vs->clipGainAutomationEnabled().val) {
        // Use the regular action so toolbar state and spectrogram handling
        // remain identical to pressing the automation button.
        dispatcher()->dispatch("clip-gain");
    }
}

void TracksViewStateModel::showPluginAutomation()
{
    const auto vs = viewState();
    if (!vs) {
        return;
    }

    auto focus = vs->pluginAutomationFocus();
    if (!focus.instanceId.empty() && !focus.parameterId.empty() && !focus.visible) {
        focus.visible = true;
        vs->setPluginAutomationFocus(std::move(focus));
    }
}

bool TracksViewStateModel::blackCanvas() const
{
    const auto uiState = projectSceneUiState();
    return uiState && uiState->blackCanvas();
}

bool TracksViewStateModel::snapEnabled() const
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        return vs->isSnapEnabled();
    }

    return false;
}

bool TracksViewStateModel::isItemEditInProgress() const
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        return !muse::RealIsEqual(vs->itemEditStartTimeOffset(), -1.0);
    }
    return false;
}

au::trackedit::TrackId TracksViewStateModel::trackAtPosition(double x, double y) const
{
    UNUSED(x);
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        return vs->trackAtPosition(y);
    }
    return {};
}

void TracksViewStateModel::changeTracksVerticalOffset(int deltaY)
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        vs->changeTracksVerticalOffset(deltaY);
    }
}

void TracksViewStateModel::setMouseY(double y)
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        vs->setMousePositionY(y);
    }
}

void TracksViewStateModel::requestVerticalScrollLock()
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        vs->setTracksVerticalScrollLocked(true);
    }
}

void TracksViewStateModel::requestVerticalScrollUnlock()
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        vs->setTracksVerticalScrollLocked(false);
    }
}

void TracksViewStateModel::insureVerticallyVisible(int viewContentY, int viewHeight, int itemY, int itemHeight)
{
    static constexpr int PADDING = 10;

    int itemTop = itemY;
    int itemBottom = itemY + itemHeight;

    int viewTop = viewContentY;
    int viewBottom = viewContentY + viewHeight;

    int delta = 0;

    if (itemTop >= (viewTop + PADDING) && itemBottom <= (viewBottom - PADDING)) {
        delta = 0;
    } else if (itemTop < (viewTop + PADDING)) {
        delta = itemTop - (viewTop + PADDING);
    } else if (itemBottom > (viewBottom - PADDING)) {
        delta = itemBottom - (viewBottom - PADDING);
    }

    int newOffset = viewContentY + delta;
    if (newOffset < 0) {
        changeTracksVerticalOffset(0);
    } else {
        changeTracksVerticalOffset(newOffset);
    }
}

int TracksViewStateModel::tracksVerticalOffset() const
{
    return m_tracksVerticalOffset.val;
}

bool TracksViewStateModel::tracksVerticalScrollLocked() const
{
    return m_tracksVerticalScrollLocked.val;
}

int TracksViewStateModel::tracksVerticalScrollPadding() const
{
    return m_tracksVerticalScrollPadding;
}

int TracksViewStateModel::trackHeight(trackedit::TrackId trackId) const
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        return vs->trackHeight(trackId).val;
    }
    return {};
}

int TracksViewStateModel::trackVerticalPosition(trackedit::TrackId trackId) const
{
    IProjectViewStatePtr vs = viewState();
    if (vs) {
        return vs->trackVerticalPosition(trackId);
    }
    return {};
}

bool TracksViewStateModel::altPressed() const
{
    return m_altPressed.val;
}

bool TracksViewStateModel::ctrlPressed() const
{
    return m_ctrlPressed.val;
}

bool TracksViewStateModel::keyboardMoveActive() const
{
    return m_keyboardMoveActive.val;
}
