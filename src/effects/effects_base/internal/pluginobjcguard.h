/*
 * GlideMix
 */
#pragma once

namespace au::effects {
//! macOS: keeps GlideMix's Qt Objective-C methods working after a plug-in that
//! carries its own Qt replaces them (see pluginobjcguard.mm). Main thread,
//! before any plug-in is loaded. Does nothing on other systems.
#ifdef __APPLE__
void installPluginObjCGuard();
#else
inline void installPluginObjCGuard() {}
#endif
}
