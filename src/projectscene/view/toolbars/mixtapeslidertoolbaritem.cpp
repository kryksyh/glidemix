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
        const int height = static_cast<int>(std::lround(kMinTrackHeight + value * (kMaxTrackHeight - kMinTrackHeight)));
        for (const trackedit::TrackId& id : project->trackIdList()) {
            vs->setTrackHeight(id, height);
        }
    }

    if (!qFuzzyCompare(m_value + 1.0, value + 1.0)) {
        m_value = value;
        emit valueChanged();
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
        // GlideMix: a new lane takes the height the slider set for the others,
        // not the default (lanes 4-6 came in twice as tall as 1-3).
        if (auto project = globalContext()->currentTrackeditProject()) {
            const auto matchHeight = [this](const trackedit::Track& added) {
                auto vs = viewState();
                auto project = globalContext()->currentTrackeditProject();
                if (!vs || !project) {
                    return;
                }
                for (const trackedit::TrackId id : project->trackIdList()) {
                    if (id != added.id) {
                        vs->setTrackHeight(added.id, vs->trackHeight(id).val);
                        return;
                    }
                }
            };
            project->trackAdded().onReceive(this, matchHeight);
            project->trackInserted().onReceive(this, [matchHeight](const trackedit::Track& t, int) { matchHeight(t); });
        }
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
