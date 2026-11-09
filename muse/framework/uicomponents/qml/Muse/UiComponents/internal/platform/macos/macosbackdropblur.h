/*
 * GlideMix: real backdrop blur behind popups and menus on macOS, done by the
 * system (NSVisualEffectView behind the popup's own window), so GlideMix's
 * own drawing does no extra work per frame.
 */
#pragma once

#include <QRectF>

class QWindow;

namespace muse::uicomponents {
//! Blurs what is behind `rect` (window coordinates, top-left origin) with
//! rounded corners. Call again when the rect changes; an empty rect hides it.
void macosSetBackdropBlur(QWindow* window, const QRectF& rect, qreal radius, bool dark);
}
