/*
 * GlideMix: macOS updates through Sparkle (built with GLIDEMIX_SPARKLE).
 */
#include "sparkleupdater.h"

#import <Sparkle/Sparkle.h>

static SPUStandardUpdaterController* s_controller = nil;

void muse::update::sparkle::start()
{
    if (!s_controller) {
        s_controller = [[SPUStandardUpdaterController alloc] initWithStartingUpdater:YES
                                                                     updaterDelegate:nil
                                                                  userDriverDelegate:nil];
    }
}

void muse::update::sparkle::checkNow()
{
    start();
    [s_controller checkForUpdates:nil];
}
