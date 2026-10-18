/*
 * GlideMix
 */
#include "appearanceappicon.h"

#import <Cocoa/Cocoa.h>

@interface MTSAppearanceAppIcon : NSObject
- (void)apply;
@end

@implementation MTSAppearanceAppIcon
- (void)apply
{
    NSAppearanceName match = [NSApp.effectiveAppearance
                              bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    NSString* name = [match isEqualToString:NSAppearanceNameDarkAqua] ? @"AppIconDark" : @"AppIconLight";
    NSString* path = [[NSBundle mainBundle] pathForResource:name ofType:@"icns"];
    NSImage* image = path ? [[NSImage alloc] initWithContentsOfFile:path] : nil;
    if (image) {
        NSApp.applicationIconImage = image;
    }
}

- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id>*)change context:(void*)context
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [self apply];
    });
}
@end

namespace au::app {
void installAppearanceDependentAppIcon()
{
    if (!NSApp) {
        return;
    }
    // Lives for the whole process, like NSApp itself.
    static MTSAppearanceAppIcon* observer = [[MTSAppearanceAppIcon alloc] init];
    [NSApp addObserver:observer forKeyPath:@"effectiveAppearance"
               options:NSKeyValueObservingOptionInitial context:nullptr];
}
}
