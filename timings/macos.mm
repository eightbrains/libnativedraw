#import <Cocoa/Cocoa.h>

#include "timings.h"
#include "../src/nativedraw.h"

std::shared_ptr<Timings> gTimings;

@interface TimingView : NSView
- (void)drawRect:(NSRect)dirtyRect;
@end

@implementation TimingView
- (void)drawRect:(NSRect)dirtyRect
{
    [super drawRect:dirtyRect];

    if (gTimings) {
        int width = int(std::ceil(self.bounds.size.width));
        int height = int(std::ceil(self.bounds.size.width));
        CGFloat dpi = 72.0f * self.window.backingScaleFactor;

        auto cgContext = NSGraphicsContext.currentContext.CGContext;
        auto dc = eb::DrawContext::fromCoreGraphics(cgContext, width, height, dpi);
        if (gTimings->runNext(dc.get()) == Timings::CONTINUE) {
            // self.needsDisplay will be cleared when drawRect finishes, so we
            // need to set it afterwards.
            dispatch_async(dispatch_get_main_queue(), ^{
                self.needsDisplay = true;
            });
        } else {
            [self.window close];
        }
    }
}
@end

//-----------------------------------------------------------------------------
@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@interface AppDelegate ()
@property (retain) NSWindow *window;
@end

@implementation AppDelegate
- (id)init {
    if ([super init]) {
    }
    return self;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender {
    return YES;
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
    gTimings = std::make_shared<Timings>();

    const int w = 1024;
    const int h = 768;
    self.window = [[NSWindow alloc]
                   initWithContentRect: NSMakeRect(0, 0, w, h)
                   styleMask: NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                   backing: NSBackingStoreBuffered
                   defer: NO];
    // Setting the window's colorspace to sRGBColorSpace speeds image drawing considerably
    // if the monitor's calibration is set to "sRGB IEC61966-2.1".
    //self.window.colorSpace = NSColorSpace.sRGBColorSpace;
    self.window.contentView = [[TimingView alloc] init];
    self.window.contentView.frame = NSMakeRect(0, 0, w, h);
    [self.window makeKeyAndOrderFront:nil];
}
@end

//-----------------------------------------------------------------------------
int main(int argc, const char *argv[])
{
    // If we use NSApplicationMain() we'll need an Info.plist file, and we're
    // trying to keep things simple here.
    @autoreleasepool {
        AppDelegate *delegate = [[AppDelegate alloc] init];
        NSApplication.sharedApplication.delegate = delegate;
        [NSApp run];
    }
}
