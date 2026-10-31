//
//  FrameController.m
//  sm64ios
//
//  Created by Christian Kosman on 11/2/21.
//

#import "FrameController.h"
#import "src/pc/gfx/gfx_uikit.h"

static uint64_t ios_display_link_tick_count;
static double ios_display_link_last_timestamp;

@implementation FrameController

- (id)init {
    self = [super init];
    self.onScreenRefresh = [[NSMutableArray array] retain];
    return self;
}

- (void)startMainLoop {
    self.gfxDisplayLink = [[CADisplayLink displayLinkWithTarget:self selector:@selector(tick:)] retain];
    self.gfxDisplayLink.preferredFramesPerSecond = 60;
    [self.gfxDisplayLink addToRunLoop:[NSRunLoop currentRunLoop] forMode:NSRunLoopCommonModes];
}

- (void)tick:(CADisplayLink *)sender {
    uint64_t tick = ++ios_display_link_tick_count;
    double interval = ios_display_link_last_timestamp > 0.0 ? sender.timestamp - ios_display_link_last_timestamp : 0.0;
    ios_display_link_last_timestamp = sender.timestamp;
    if (tick <= 3 || tick % 120 == 0)
        gfx_ios_diag_log("UIKit CADisplayLink heartbeat tick=%llu interval=%.3fs preferredFPS=%ld paused=%d", (unsigned long long)tick, interval, (long)sender.preferredFramesPerSecond, sender.paused);
    for(int i = 0; i < self.onScreenRefresh.count; i++) {
        ((void (*)())[self.onScreenRefresh[i] pointerValue])();
    }
}

- (void)shutdown {
    [self.gfxDisplayLink invalidate];
    self.gfxDisplayLink = nil;
}

@end
