#include "winbackdropblur.h"

#include <QWindow>
#include <QDebug>

#include <windows.h>
#include <dwmapi.h>

#pragma comment(lib, "dwmapi.lib")

namespace {
// Not in older SDK headers.
constexpr DWORD kWindowCornerPreference = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr int kCornerRound = 2;               // DWMWCP_ROUND (8 px, like our popups)
}

bool muse::uicomponents::winSetBackdropBlur(QWindow* window, const QRectF& rect, qreal radius, bool dark)
{
    if (!window || rect.isEmpty()) {
        return false;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) {
        return false;
    }

    // Once per window. Windows 11's documented backdrop
    // (DWMWA_SYSTEMBACKDROP_TYPE) only shows on the ACTIVE window, and a menu
    // never becomes active, so it stayed solid (tested on the NUC). The shell's
    // own acrylic accent works on inactive windows; it is undocumented, so it
    // is looked up at run time and the popup stays opaque without it.
    static const char* kDone = "glidemixBackdropDone";
    if (!window->property(kDone).isValid()) {
        struct AccentPolicy { int state; int flags; DWORD gradient; int animation; };
        struct CompositionData { int attribute; PVOID data; SIZE_T size; };
        using SetComposition = BOOL(WINAPI*)(HWND, CompositionData*);
        auto setComposition = reinterpret_cast<SetComposition>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetWindowCompositionAttribute"));
        bool ok = false;
        if (setComposition) {
            // ACCENT_ENABLE_ACRYLICBLURBEHIND, a nearly clear tint (our QML fill tints it)
            AccentPolicy accent = { 4, 2, dark ? 0x01101010u : 0x01F0F0F0u, 0 };
            CompositionData data = { 19 /* WCA_ACCENT_POLICY */, &accent, sizeof(accent) };
            ok = setComposition(hwnd, &data) != FALSE;
        }
        DwmSetWindowAttribute(hwnd, kWindowCornerPreference, &kCornerRound, sizeof(kCornerRound));
        window->setProperty(kDone, ok);
        qInfo("GlideMix backdrop: acrylic accent %s", ok ? "on" : "not available");
    }
    if (!window->property(kDone).toBool()) {
        return false;
    }

    // Menus and dropdowns have no padding on Windows (menuview.cpp,
    // StyledDropdownView.qml): the window is the popup's body, so the
    // backdrop and the system's rounded corners fit it without clipping.
    Q_UNUSED(rect)
    Q_UNUSED(radius)
    return true;
}
