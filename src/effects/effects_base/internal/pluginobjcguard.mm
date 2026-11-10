/*
 * GlideMix
 *
 * Some plug-ins ship their own copy of Qt inside the bundle (AIR / Wizoo
 * plug-ins carry Qt 5.15.2). Their Qt classes have their own names, but the
 * Objective-C categories Qt adds to system classes do not -- for example
 * `+[NSImage imageFromQImage:]` (QtGui, "QtExtras"). Loading such a plug-in
 * replaces GlideMix's Qt 6 method with the plug-in's Qt 5 one for the whole
 * process. The next time GlideMix shows a picture cursor, Qt 6 hands its QImage
 * to the plug-in's Qt 5 code and the app dies (EXC_BAD_ACCESS in the plug-in,
 * called from QWindowPrivate::setCursor). Seen with AIR Compressor and AIR
 * Enhancer, AU and VST3, 2026-11-10.
 *
 * The guard notes our own implementations of every method Qt added to the
 * common AppKit/Foundation classes at start-up. When a later image replaces
 * one, the method gets a small dispatcher: a call coming from the plug-in's own
 * image runs the plug-in's version, any other call runs ours. Both copies of Qt
 * keep working.
 */
#include "pluginobjcguard.h"

#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <objc/runtime.h>

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "log.h"

namespace {
struct Owner {
    const void* imageBase = nullptr;
    IMP imp = nullptr;
};

struct Guarded {
    Class cls = nil;          // the class or metaclass that holds the method
    SEL sel = nullptr;
    IMP ours = nullptr;       // GlideMix's Qt implementation
    std::vector<Owner> others; // plug-ins' implementations, by image
};

std::mutex& GuardMutex()
{
    static std::mutex m;
    return m;
}

// Keyed by selector: the Qt category methods have distinct names. Entries are
// never removed, so the dispatcher can read them without the lock.
std::unordered_map<SEL, Guarded*>& Table()
{
    static std::unordered_map<SEL, Guarded*> table;
    return table;
}

bool g_installed = false;

const void* ImageOf(const void* address)
{
    Dl_info info {};
    return address && dladdr(address, &info) ? info.dli_fbase : nullptr;
}

bool IsOurQtImage(const void* address)
{
    Dl_info info {};
    if (!address || !dladdr(address, &info) || !info.dli_fname) {
        return false;
    }
    // Called before any plug-in is loaded, so every Qt library found is ours
    // (inside GlideMix.app when installed, ~/Qt in a dev build). A plug-in's
    // own Qt is linked into its bundle's binary, never a separate Qt library.
    const std::string path = info.dli_fname;
    return path.find("/Qt") != std::string::npos || path.find("libqcocoa") != std::string::npos;
}

// The methods involved take only pointer/integer arguments (at most two) and
// return an object or an integer, so one forwarder passing two argument
// registers through serves all of them.
using Forward = void* (*)(id, SEL, void*, void*);

void* __attribute__((noinline)) Dispatch(id self, SEL sel, void* a, void* b)
{
    const void* caller = __builtin_return_address(0);
    const auto it = Table().find(sel);
    if (it == Table().end()) {
        return nullptr;
    }
    const Guarded& g = *it->second;
    IMP imp = g.ours;
    if (!g.others.empty()) {
        const void* callerImage = ImageOf(caller);
        for (const Owner& o : g.others) {
            if (o.imageBase == callerImage) {
                imp = o.imp;
                break;
            }
        }
    }
    return reinterpret_cast<Forward>(imp)(self, sel, a, b);
}

void CollectFrom(Class cls)
{
    unsigned count = 0;
    Method* methods = class_copyMethodList(cls, &count);
    for (unsigned i = 0; i < count; ++i) {
        const IMP imp = method_getImplementation(methods[i]);
        if (!IsOurQtImage(reinterpret_cast<const void*>(imp))) {
            continue;
        }
        const SEL sel = method_getName(methods[i]);
        // +load / +initialize are called by the runtime itself, per category,
        // never through the method table: nothing to keep apart.
        if (sel == @selector(load) || sel == @selector(initialize) || Table().count(sel)) {
            continue;
        }
        auto* g = new Guarded;
        g->cls = cls;
        g->sel = sel;
        g->ours = imp;
        g->others.reserve(16);   // Dispatch reads it unlocked: avoid moving it
        Table()[sel] = g;
    }
    free(methods);
}

void Check()
{
    std::lock_guard<std::mutex> lock(GuardMutex());
    for (auto& [sel, g] : Table()) {
        Method m = class_getInstanceMethod(g->cls, sel);
        if (!m) {
            continue;
        }
        const IMP now = method_getImplementation(m);
        if (now == reinterpret_cast<IMP>(Dispatch) || now == g->ours) {
            continue;
        }
        // A newly loaded image replaced our method: keep its version for its
        // own calls and put the dispatcher in front.
        const void* image = ImageOf(reinterpret_cast<const void*>(now));
        bool known = false;
        for (const Owner& o : g->others) {
            known = known || o.imageBase == image;
        }
        if (!known) {
            g->others.push_back({ image, now });
            Dl_info info {};
            dladdr(reinterpret_cast<const void*>(now), &info);
            LOGW() << "plug-in image replaced Qt method " << sel_getName(sel) << " ("
                   << (info.dli_fname ? info.dli_fname : "?") << "); calls are now kept apart";
        }
        method_setImplementation(m, reinterpret_cast<IMP>(Dispatch));
    }
}

void OnImageAdded(const struct mach_header*, intptr_t)
{
    if (!g_installed) {
        return;
    }
    // Categories may be attached after this notification: check now and
    // again on the main thread before it handles the next event.
    Check();
    dispatch_async(dispatch_get_main_queue(), ^{
        Check();
    });
}
}

namespace au::effects {
void installPluginObjCGuard()
{
    if (g_installed) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(GuardMutex());
        // The classes Qt extends with categories (Qt 6.10: NSImage, NSScreen,
        // NSWindow, NSObject); the others are cheap to look at, in case a
        // later Qt adds more.
        for (NSString* name : { @"NSObject", @"NSImage", @"NSScreen", @"NSWindow", @"NSView",
                                @"NSApplication", @"NSMenu", @"NSMenuItem", @"NSString", @"NSURL",
                                @"NSData", @"NSColor", @"NSFont", @"NSEvent", @"NSCursor" }) {
            Class cls = NSClassFromString(name);
            if (!cls) {
                continue;
            }
            CollectFrom(cls);
            CollectFrom(object_getClass(cls));   // class methods
        }
    }
    LOGI() << "plug-in Objective-C guard watches " << Table().size() << " Qt methods";
    g_installed = true;
    // Also called at once for every image already loaded: Check() finds
    // nothing to do for them.
    _dyld_register_func_for_add_image(OnImageAdded);
}
}
