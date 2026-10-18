/*
 * GlideMix
 */
#pragma once

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "actions/iactionsdispatcher.h"

#include "uicomponents/qml/Muse/UiComponents/toolbaritem.h"

#include "projectscene/iprojectviewstate.h"

namespace au::projectscene {
//! The parked MixHouse DJ app's two zoom sliders (ArrangementWindow::
//! dragZoomSlider), as playback-toolbar items:
//!  - "mixtape-track-height": one height for every track, 72..260 px, linear
//!    (the parked range was 70..260; Audacity collapses a track below 72)
//!  - "mixtape-zoom-slider": time zoom, log-mapped 0.2..20000 px/s, keeping
//!    the same focus point the zoom buttons use
class MixtapeSliderToolBarItem : public muse::uicomponents::ToolBarItem
{
    Q_OBJECT

    Q_PROPERTY(double value READ value WRITE setValue NOTIFY valueChanged FINAL)
    Q_PROPERTY(bool isTimeZoom READ isTimeZoom CONSTANT FINAL)

    muse::ContextInject<context::IGlobalContext> globalContext { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> actionsDispatcher { this };

public:
    explicit MixtapeSliderToolBarItem(const muse::ui::UiAction& action, muse::uicomponents::ToolBarItemType::Type type,
                                      QObject* parent = nullptr);

    double value() const;
    void setValue(double value);

    bool isTimeZoom() const;

signals:
    void valueChanged();

private:
    IProjectViewStatePtr viewState() const;
    void onProjectChanged();
    void updateValue();

    bool m_isTimeZoom = false;
    double m_value = 0.0;
};
}
