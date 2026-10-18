/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "macosmainwindowbridge.h"

#include <Cocoa/Cocoa.h>
#include <QWindow>

#include <algorithm>

using namespace muse::ui;

static NSWindow* nsWindowForQWindow(QWindow* qWindow)
{
    if (!qWindow) {
        return nullptr;
    }

    NSView* nsView = (__bridge NSView*)reinterpret_cast<void*>(qWindow->winId());
    NSWindow* nsWindow = [nsView window];
    return nsWindow;
}

MacOSMainWindowBridge::MacOSMainWindowBridge(QObject* parent)
    : MainWindowBridge(parent)
{
}

void MacOSMainWindowBridge::init()
{
    MainWindowBridge::init();

    uiConfiguration()->applyPlatformStyle(m_window);

    uiConfiguration()->currentThemeChanged().onNotify(this, [this]() {
        uiConfiguration()->applyPlatformStyle(m_window);
    });
}

bool MacOSMainWindowBridge::fileModified() const
{
    //! NOTE QWindow misses an API for this, so we'll do it ourselves.
    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow) {
        return false;
    }

    return [nsWindow isDocumentEdited];
}

void MacOSMainWindowBridge::setFileModified(bool modified)
{
    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow) {
        return;
    }

    if ([nsWindow isDocumentEdited] == modified) {
        return;
    }

    [nsWindow setDocumentEdited:modified];
    emit fileModifiedChanged();
}

qreal MacOSMainWindowBridge::fullScreenTopInset() const
{
    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow || !([nsWindow styleMask] & NSWindowStyleMaskFullScreen) || !nsWindow.screen) {
        return 0.0;
    }
    //! In full screen AppKit shows the title bar in a window of its own over
    //! ours. Shown and on our screen, it is where our content has to start;
    //! hidden (the menu bar hides in full screen), it sits above the screen.
    const CGFloat screenTop = NSMaxY(nsWindow.screen.frame);
    CGFloat inset = 0.0;
    for (NSWindow* w in [NSApp windows]) {
        if (w == nsWindow || !w.isVisible || w.screen != nsWindow.screen) {
            continue;
        }
        if ([NSStringFromClass([w class]) rangeOfString:@"FullScreen"].location == NSNotFound) {
            continue;
        }
        inset = std::max(inset, screenTop - NSMinY(w.frame));
    }
    return std::clamp(double(inset), 0.0, 150.0);
}
