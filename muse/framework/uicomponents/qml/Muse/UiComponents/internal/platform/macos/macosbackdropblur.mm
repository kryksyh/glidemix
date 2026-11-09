#include "macosbackdropblur.h"

#include <QWindow>

#import <AppKit/AppKit.h>

static const NSUserInterfaceItemIdentifier kBlurId = @"GlideMixBackdropBlur";

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

    // The blur view sits beside Qt's view in the window's frame view, behind
    // it. Qt's view stays the window's content view: moving it into another
    // view stopped Qt drawing popups that resize after they open (tooltips
    // showed as empty boxes, 1.2.0).
    NSView* frameView = qtView.superview;
    if (!frameView) {
        return;
    }
    if (nsWindow.opaque) {
        nsWindow.opaque = NO;
        nsWindow.backgroundColor = NSColor.clearColor;
    }

    NSVisualEffectView* blur = nil;
    for (NSView* v in frameView.subviews) {
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
        [frameView addSubview:blur positioned:NSWindowBelow relativeTo:qtView];
    }

    blur.appearance = [NSAppearance appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    blur.layer.cornerRadius = radius;
    blur.hidden = rect.isEmpty();

    // rect is in Qt's view coordinates (top-left origin); place it in the
    // frame view, whose origin is bottom-left.
    const NSRect qtFrame = qtView.frame;
    blur.frame = NSMakeRect(qtFrame.origin.x + rect.x(),
                            qtFrame.origin.y + qtFrame.size.height - rect.y() - rect.height(),
                            rect.width(), rect.height());

    // The system shadow follows the popup's own shape (rounded corners, arrow).
    nsWindow.hasShadow = YES;
    [nsWindow invalidateShadow];
}
