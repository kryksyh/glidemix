/*
 * GlideMix
 */
#include "mixtapeslidertoolbaritem.h"

#include <algorithm>
#include <cmath>

#include "project/iaudacityproject.h"
#include "trackedit/itrackeditproject.h"

using namespace au::projectscene;

namespace {
// ArrangementWindow::dragZoomSlider() in the parked app.
constexpr double kMinPxPerSec = 0.2;
constexpr double kMaxPxPerSec = 20000.0;   // close enough to see single kicks and hats
constexpr int kMinTrackHeight = 72;
constexpr int kMaxTrackHeight = 260;
}

MixtapeSliderToolBarItem::MixtapeSliderToolBarItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                                   QObject* parent)
    : muse::uicomponents::ToolBarItem(action, type, parent), m_isTimeZoom(action.code == "mixtape-zoom-slider")
{
    m_heightTimer.setSingleShot(true);
    m_heightTimer.setInterval(16);
    connect(&m_heightTimer, &QTimer::timeout, this, &MixtapeSliderToolBarItem::applyTrackHeight);
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        onProjectChanged();
    });
    onProjectChanged();
}

double MixtapeSliderToolBarItem::value() const
{
    return m_value;
}

bool MixtapeSliderToolBarItem::isTimeZoom() const
{
    return m_isTimeZoom;
}

void MixtapeSliderToolBarItem::setValue(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    auto vs = viewState();
    auto project = globalContext()->currentTrackeditProject();
    if (!vs || !project) {
        return;
    }

    if (m_isTimeZoom) {
        const double pxPerSec = kMinPxPerSec * std::pow(kMaxPxPerSec / kMinPxPerSec, value);
        actionsDispatcher()->dispatch("mixtape-zoom-set", muse::actions::ActionData::make_arg1<double>(pxPerSec));
    } else {
        m_pendingHeight = static_cast<int>(std::lround(kMinTrackHeight + value * (kMaxTrackHeight - kMinTrackHeight)));
        if (!m_heightTimer.isActive()) {
            applyTrackHeight();
            m_heightTimer.start();   // later moves within this frame wait for it
        }
    }

    if (!qFuzzyCompare(m_value + 1.0, value + 1.0)) {
        m_value = value;
        emit valueChanged();
    }
}

void MixtapeSliderToolBarItem::applyTrackHeight()
{
    if (m_pendingHeight < 0) {
        return;
    }
    const int height = m_pendingHeight;
    m_pendingHeight = -1;
    auto vs = viewState();
    auto project = globalContext()->currentTrackeditProject();
    if (vs && project) {
        vs->setTracksHeight(project->trackIdList(), height);
    }
}

IProjectViewStatePtr MixtapeSliderToolBarItem::viewState() const
{
    const project::IAudacityProjectPtr prj = globalContext()->currentProject();
    return prj ? prj->viewState() : nullptr;
}

void MixtapeSliderToolBarItem::onProjectChanged()
{
    auto vs = viewState();
    if (!vs) {
        return;
    }
    if (m_isTimeZoom) {
        vs->zoomStateChanged().onNotify(this, [this]() { updateValue(); });
    } else {
        vs->totalTrackHeight().ch.onReceive(this, [this](int) { updateValue(); });
        // GlideMix: a new lane takes the height of the others -- done where
        // its view data is made (ProjectViewState::makeTrackData), which knows
        // its Single wave state; copying here doubled songs 2, 3, 4.
    }
    updateValue();
}

void MixtapeSliderToolBarItem::updateValue()
{
    auto vs = viewState();
    if (!vs) {
        return;
    }

    double value = m_value;
    if (m_isTimeZoom) {
        const double zoom = vs->zoomState().zoom;
        if (zoom > 0.0) {
            value = std::log(zoom / kMinPxPerSec) / std::log(kMaxPxPerSec / kMinPxPerSec);
        }
    } else {
        if (m_pendingHeight >= 0 || m_heightTimer.isActive()) {
            return;   // our own move: the handle already shows it
        }
        auto project = globalContext()->currentTrackeditProject();
        const auto ids = project ? project->trackIdList() : std::vector<trackedit::TrackId> {};
        if (!ids.empty()) {
            const int height = vs->trackHeight(ids.front()).val;
            value = double(height - kMinTrackHeight) / (kMaxTrackHeight - kMinTrackHeight);
        }
    }

    value = std::clamp(value, 0.0, 1.0);
    if (!qFuzzyCompare(m_value + 1.0, value + 1.0)) {
        m_value = value;
        emit valueChanged();
    }
}
