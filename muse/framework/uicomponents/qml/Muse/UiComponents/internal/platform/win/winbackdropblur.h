/*
 * GlideMix: real backdrop blur behind menus and dropdowns on Windows 11
 * (DWM's transient-window backdrop, the acrylic of the system's own menus),
 * done by the system compositor, so GlideMix's own drawing does no extra work.
 */
#pragma once

#include <QRectF>

class QWindow;

namespace muse::uicomponents {
//! Asks DWM for the acrylic backdrop and rounded corners on the popup's
//! window (menus and dropdowns, whose window is exactly their body). False where Windows has no such backdrop
//! (before Windows 11 22H2): the popup then stays opaque.
bool winSetBackdropBlur(QWindow* window, const QRectF& rect, qreal radius, bool dark);
}
