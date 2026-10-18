/*
 * GlideMix: macOS updates through Sparkle (built with GLIDEMIX_SPARKLE).
 */
#pragma once

namespace muse::update::sparkle {
//! Starts Sparkle's updater: it checks the feed in the app's SUFeedURL once
//! a day by itself and shows its own windows (new version with the release
//! notes, download, install and relaunch).
void start();
//! Help > Check for update: Sparkle's check, which also says "up to date".
void checkNow();
}
