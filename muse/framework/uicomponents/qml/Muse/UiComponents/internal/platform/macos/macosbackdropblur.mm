#include "macosbackdropblur.h"

#include <QWindow>

#import <AppKit/AppKit.h>

static const NSUserInterfaceItemIdentifier kBlurId = @"GlideMixBackdropBlur";
static const NSUserInterfaceItemIdentifier kContainerId = @"GlideMixBackdropContainer";

void muse::uicomponents::macosSetBackdropBlur(QWindow* window, const QRectF& rect, qreal radius, bool dark)
{
    if (!window) {
        return;
    }
    NSView* qtView = reinterpret_cast<NSView*>(window->winId());
    NSWindow* nsWindow = qtView.window;
    if (!qtView || !nsWindow) {
        return;
    }

    // Put Qt's view inside a plain container once, so the blur view can sit
    // behind it (Qt's view draws with its own layer and cannot hold subviews).
    NSView* container = qtView.superview;
    if (![container.identifier isEqualToString:kContainerId]) {
        NSRect frame = qtView.frame;
        container = [[NSView alloc] initWithFrame:frame];
        container.identifier = kContainerId;
        container.autoresizesSubviews = YES;
        nsWindow.contentView = container;
        qtView.frame = container.bounds;
        qtView.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [container addSubview:qtView];
        nsWindow.opaque = NO;
        nsWindow.backgroundColor = NSColor.clearColor;
    }

    NSVisualEffectView* blur = nil;
    for (NSView* v in container.subviews) {
        if ([v.identifier isEqualToString:kBlurId]) {
            blur = (NSVisualEffectView*)v;
        }
    }
    if (!blur) {
        blur = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
        blur.identifier = kBlurId;
        blur.blendingMode = NSVisualEffectBlendingModeBehindWindow;
        blur.material = NSVisualEffectMaterialMenu;
        blur.state = NSVisualEffectStateActive;
        blur.wantsLayer = YES;
        blur.layer.masksToBounds = YES;
        [container addSubview:blur positioned:NSWindowBelow relativeTo:qtView];
    }

    blur.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    blur.layer.cornerRadius = radius;
    blur.hidden = rect.isEmpty();

    // AppKit's origin is bottom-left.
    const CGFloat h = container.bounds.size.height;
    blur.frame = NSMakeRect(rect.x(), h - rect.y() - rect.height(), rect.width(), rect.height());

    // The system shadow follows the popup's own shape (rounded corners, arrow).
    nsWindow.hasShadow = YES;
    [nsWindow invalidateShadow];
}
