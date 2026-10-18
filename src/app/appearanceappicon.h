/*
 * GlideMix
 */
#pragma once

namespace au::app {
/// Keeps the Dock icon matching the system appearance: the dark icon in Dark
/// Mode, the light icon in Light Mode, switching live when the user changes
/// it. The bundle's own AppIcon.icns (Finder, Launchpad) is the dark one --
/// a static .icns can't carry appearance variants; that needs an asset
/// catalog compiled by Xcode's actool, which this build doesn't use.
void installAppearanceDependentAppIcon();
}
