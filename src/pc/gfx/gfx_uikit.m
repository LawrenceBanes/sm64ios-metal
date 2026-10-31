//
//  gfx_uikit.m
//  sm64ios
//
//  Created by Christian Kosman on 9/21/21.
//

#include <math.h>
#include <stdarg.h>
#include <unistd.h>
#import <objc/runtime.h>
#import "gfx_uikit.h"
#if TARGET_OS_IOS
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#endif

UIViewController *gameViewController;
UIWindow *mainWindow;
#if TARGET_OS_IOS
UIWindow *externalWindow;
static __weak UIWindowScene *sm64WindowScene;
static UIWindow *sm64DiagnosticsWindow;
static NSFileHandle *sm64DiagnosticsFile;
static NSString *sm64DiagnosticsPath;
static NSDateFormatter *sm64DiagnosticsDateFormatter;
static __weak id sm64DiagnosticsController;
static BOOL sm64DiagnosticsLaunchMarkerWritten;

@interface SM64DiagnosticsViewController : UIViewController <MTKViewDelegate>
@property(nonatomic, strong) UIView *panel;
@property(nonatomic, strong) UILabel *titleLabel;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UIButton *toggleButton;
@property(nonatomic, strong) MTKView *probeView;
@property(nonatomic, strong) UIButton *probeCloseButton;
@property(nonatomic, strong) id<MTLCommandQueue> probeQueue;
@property(nonatomic) CGRect hitRect;
@property(nonatomic) NSUInteger probeFrameCount;
@property(nonatomic) BOOL probeAvailabilityLogged;
@property(nonatomic) BOOL collapsed;
@property(nonatomic, strong) NSMutableArray<NSString *> *recentStatus;
- (void)appendStatus:(NSString *)status;
- (void)collapsePanel;
- (void)togglePanel;
- (void)startColorProbe;
- (void)stopColorProbe;
- (void)shareLog;
@end

@interface SM64DiagnosticsPassthroughView : UIView
@property(nonatomic) CGRect hitRect;
@end

@implementation SM64DiagnosticsPassthroughView
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    (void)event;
    return CGRectContainsPoint(self.hitRect, point);
}
@end

@implementation SM64DiagnosticsViewController

- (void)loadView {
    SM64DiagnosticsPassthroughView *root = [[[SM64DiagnosticsPassthroughView alloc] initWithFrame:UIScreen.mainScreen.bounds] autorelease];
    root.backgroundColor = UIColor.clearColor;
    root.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.view = root;

    self.panel = [[[UIView alloc] initWithFrame:CGRectZero] autorelease];
    self.panel.backgroundColor = [UIColor colorWithWhite:0.06 alpha:0.93];
    self.panel.layer.cornerRadius = 10.0;
    self.panel.layer.borderWidth = 1.0;
    self.panel.layer.borderColor = [UIColor colorWithWhite:0.7 alpha:0.8].CGColor;
    self.panel.clipsToBounds = YES;
    [root addSubview:self.panel];

    self.titleLabel = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    UILabel *title = self.titleLabel;
    title.text = @"SM64 startup diagnostics";
    title.textColor = UIColor.whiteColor;
    title.font = [UIFont boldSystemFontOfSize:14.0];
    [self.panel addSubview:title];
    title.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [title.leadingAnchor constraintEqualToAnchor:self.panel.leadingAnchor constant:10],
        [title.topAnchor constraintEqualToAnchor:self.panel.topAnchor constant:7],
        [title.heightAnchor constraintEqualToConstant:20]
    ]];

    self.toggleButton = [UIButton buttonWithType:UIButtonTypeSystem];
    [self.toggleButton setTitle:@"Hide" forState:UIControlStateNormal];
    [self.toggleButton setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    self.toggleButton.titleLabel.font = [UIFont systemFontOfSize:12.0 weight:UIFontWeightSemibold];
    [self.toggleButton addTarget:self action:@selector(togglePanel) forControlEvents:UIControlEventTouchUpInside];
    [self.panel addSubview:self.toggleButton];
    self.toggleButton.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.toggleButton.trailingAnchor constraintEqualToAnchor:self.panel.trailingAnchor constant:-8],
        [self.toggleButton.centerYAnchor constraintEqualToAnchor:title.centerYAnchor],
        [self.toggleButton.widthAnchor constraintEqualToConstant:44],
        [self.toggleButton.heightAnchor constraintEqualToConstant:28]
    ]];

    self.statusLabel = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    self.statusLabel.textColor = [UIColor colorWithWhite:0.94 alpha:1.0];
    self.statusLabel.font = [UIFont monospacedSystemFontOfSize:9.5 weight:UIFontWeightRegular];
    self.statusLabel.numberOfLines = 5;
    self.statusLabel.lineBreakMode = NSLineBreakByTruncatingTail;
    [self.panel addSubview:self.statusLabel];
    self.statusLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [self.statusLabel.leadingAnchor constraintEqualToAnchor:self.panel.leadingAnchor constant:10],
        [self.statusLabel.trailingAnchor constraintEqualToAnchor:self.panel.trailingAnchor constant:-10],
        [self.statusLabel.topAnchor constraintEqualToAnchor:title.bottomAnchor constant:3],
        [self.statusLabel.heightAnchor constraintEqualToConstant:61]
    ]];

    UIStackView *buttons = [[[UIStackView alloc] initWithFrame:CGRectZero] autorelease];
    buttons.axis = UILayoutConstraintAxisHorizontal;
    buttons.distribution = UIStackViewDistributionFillEqually;
    buttons.spacing = 5;
    [self.panel addSubview:buttons];
    buttons.translatesAutoresizingMaskIntoConstraints = NO;
    [NSLayoutConstraint activateConstraints:@[
        [buttons.leadingAnchor constraintEqualToAnchor:self.panel.leadingAnchor constant:8],
        [buttons.trailingAnchor constraintEqualToAnchor:self.panel.trailingAnchor constant:-8],
        [buttons.bottomAnchor constraintEqualToAnchor:self.panel.bottomAnchor constant:-7],
        [buttons.heightAnchor constraintEqualToConstant:34]
    ]];
    UIButton *share = [self buttonWithTitle:@"Share log" action:@selector(shareLog)];
    UIButton *probe = [self buttonWithTitle:@"Metal color test" action:@selector(startColorProbe)];
    [buttons addArrangedSubview:share];
    [buttons addArrangedSubview:probe];
    self.recentStatus = [NSMutableArray array];
    [self appendStatus:@"Native UIKit diagnostic panel ready"];
}

- (UIButton *)buttonWithTitle:(NSString *)title action:(SEL)action {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont systemFontOfSize:11.0 weight:UIFontWeightSemibold];
    button.backgroundColor = [UIColor colorWithWhite:0.23 alpha:1.0];
    button.layer.cornerRadius = 6.0;
    [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return button;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGFloat width = MIN(350.0, MAX(250.0, self.view.bounds.size.width - 24.0));
    CGFloat panelWidth = self.collapsed ? 118.0 : width;
    CGFloat panelHeight = self.collapsed ? 38.0 : 154.0;
    CGFloat top = self.view.safeAreaInsets.top + 6.0;
    CGRect frame = CGRectMake(10.0, top, panelWidth, panelHeight);
    self.panel.frame = frame;
    ((SM64DiagnosticsPassthroughView *)self.view).hitRect = self.probeView ? self.view.bounds : CGRectInset(frame, -2, -2);
    if (self.collapsed) {
        self.titleLabel.hidden = YES;
        self.statusLabel.hidden = YES;
        for (UIView *subview in self.panel.subviews) {
            if ([subview isKindOfClass:[UIStackView class]]) subview.hidden = YES;
        }
        [self.toggleButton setTitle:@"Diagnostics" forState:UIControlStateNormal];
        self.toggleButton.frame = CGRectMake(3, 4, panelWidth - 6, 30);
    } else {
        self.titleLabel.hidden = NO;
        self.statusLabel.hidden = NO;
        for (UIView *subview in self.panel.subviews) {
            if ([subview isKindOfClass:[UIStackView class]]) subview.hidden = NO;
        }
        [self.toggleButton setTitle:@"Hide" forState:UIControlStateNormal];
    }
}

- (void)appendStatus:(NSString *)status {
    if (!status.length) return;
    [self.recentStatus addObject:status];
    while (self.recentStatus.count > 5) [self.recentStatus removeObjectAtIndex:0];
    self.statusLabel.text = [self.recentStatus componentsJoinedByString:@"\n"];
}

- (void)collapsePanel {
    self.collapsed = YES;
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
}

- (void)togglePanel {
    self.collapsed = !self.collapsed;
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
}

- (void)shareLog {
    if (!sm64DiagnosticsPath.length) return;
    NSURL *url = [NSURL fileURLWithPath:sm64DiagnosticsPath];
    UIActivityViewController *activity = [[[UIActivityViewController alloc] initWithActivityItems:@[url] applicationActivities:nil] autorelease];
    UIPopoverPresentationController *popover = activity.popoverPresentationController;
    popover.sourceView = self.panel;
    popover.sourceRect = self.panel.bounds;
    [self presentViewController:activity animated:YES completion:nil];
}

- (void)startColorProbe {
    if (self.probeView) return;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) {
        gfx_ios_diag_log("Native Metal probe failed: MTLCreateSystemDefaultDevice returned nil");
        return;
    }
    id<MTLCommandQueue> queue = [device newCommandQueue];
    self.probeQueue = queue;
    [queue release];
    if (!self.probeQueue) {
        gfx_ios_diag_log("Native Metal probe failed: newCommandQueue returned nil");
        [device release];
        return;
    }
    gfx_ios_diag_log("Native Metal color test started device=%s; independent MTKView clear/present path", device.name.UTF8String ?: "unknown");
    self.probeView = [[[MTKView alloc] initWithFrame:self.view.bounds device:device] autorelease];
    if (!self.probeView) {
        gfx_ios_diag_log("Native Metal probe failed: MTKView initialization returned nil");
        self.probeQueue = nil;
        [device release];
        return;
    }
    self.probeView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    self.probeView.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
    self.probeView.framebufferOnly = YES;
    self.probeView.clearColor = MTLClearColorMake(0.95, 0.0, 0.65, 1.0);
    self.probeView.preferredFramesPerSecond = 10;
    self.probeView.delegate = self;
    self.probeAvailabilityLogged = NO;
    [self.view addSubview:self.probeView];
    self.probeCloseButton = [self buttonWithTitle:@"Close color test" action:@selector(stopColorProbe)];
    self.probeCloseButton.frame = CGRectMake(MAX(12.0, self.view.bounds.size.width - 155.0), self.view.safeAreaInsets.top + 10.0, 143.0, 38.0);
    self.probeCloseButton.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleBottomMargin;
    self.probeCloseButton.backgroundColor = [UIColor colorWithWhite:0.08 alpha:0.92];
    [self.view addSubview:self.probeCloseButton];
    [self.probeView setNeedsDisplay];
    ((SM64DiagnosticsPassthroughView *)self.view).hitRect = self.view.bounds;
    [device release];
}

- (void)stopColorProbe {
    if (!self.probeView) return;
    self.probeView.delegate = nil;
    [self.probeView removeFromSuperview];
    self.probeView = nil;
    [self.probeCloseButton removeFromSuperview];
    self.probeCloseButton = nil;
    self.probeQueue = nil;
    gfx_ios_diag_log("Native Metal color test stopped after %lu draw callbacks", (unsigned long)self.probeFrameCount);
    self.probeFrameCount = 0;
    ((SM64DiagnosticsPassthroughView *)self.view).hitRect = self.collapsed ? CGRectMake(8, 0, 130, 52) : CGRectMake(8, 0, self.panel.bounds.size.width + 4, self.panel.bounds.size.height + 14);
}

- (void)drawInMTKView:(MTKView *)view {
    MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;
    id<CAMetalDrawable> drawable = view.currentDrawable;
    if (!pass || !drawable) {
        if (!self.probeAvailabilityLogged) {
            self.probeAvailabilityLogged = YES;
            gfx_ios_diag_log("Native Metal probe has no drawable/pass descriptor size=%.0fx%.0f", view.drawableSize.width, view.drawableSize.height);
        }
        return;
    }
    id<MTLCommandBuffer> command = [self.probeQueue commandBuffer];
    if (!command) {
        gfx_ios_diag_log("Native Metal probe failed: command buffer unavailable");
        return;
    }
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.95, 0.0, 0.65, 1.0);
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (!encoder) {
        gfx_ios_diag_log("Native Metal probe failed: render command encoder unavailable");
        return;
    }
    [encoder endEncoding];
    [command presentDrawable:drawable];
    NSUInteger submitted = ++self.probeFrameCount;
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        if (completed.status == MTLCommandBufferStatusError) {
            gfx_ios_diag_log("Native Metal probe GPU command error frame=%lu: %s", (unsigned long)submitted, completed.error.localizedDescription.UTF8String ?: "unknown");
        } else if (submitted == 1) {
            gfx_ios_diag_log("Native Metal probe first color frame completed drawable=%.0fx%.0f; check for bright magenta screen", view.drawableSize.width, view.drawableSize.height);
        }
    }];
    [command commit];
    if (submitted == 1) gfx_ios_diag_log("Native Metal probe first frame submitted drawable=%.0fx%.0f", view.drawableSize.width, view.drawableSize.height);
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
    gfx_ios_diag_log("Native Metal probe drawable resized to %.0fx%.0f", size.width, size.height);
}
@end

static SM64DiagnosticsViewController *sm64_diag_controller(void) {
    return (SM64DiagnosticsViewController *)sm64DiagnosticsController;
}

static void sm64_diag_create_window(UIWindowScene *scene) {
    if (!scene || sm64DiagnosticsWindow) return;
    SM64DiagnosticsViewController *controller = [[SM64DiagnosticsViewController alloc] init];
    sm64DiagnosticsWindow = [[UIWindow alloc] initWithWindowScene:scene];
    sm64DiagnosticsWindow.frame = scene.coordinateSpace.bounds;
    sm64DiagnosticsWindow.backgroundColor = UIColor.clearColor;
    sm64DiagnosticsWindow.windowLevel = UIWindowLevelNormal + 2;
    sm64DiagnosticsWindow.rootViewController = controller;
    sm64DiagnosticsController = controller;
    [controller release];
    // Keep the SDL/game window key. This transparent, pass-through overlay
    // handles touches only over its small panel or while the color probe runs.
    sm64DiagnosticsWindow.hidden = NO;
    gfx_ios_diag_log("Native diagnostic window attached scene=%@ bounds=(%.0f,%.0f %.0fx%.0f) level=%.1f", scene.session.persistentIdentifier, scene.coordinateSpace.bounds.origin.x, scene.coordinateSpace.bounds.origin.y, scene.coordinateSpace.bounds.size.width, scene.coordinateSpace.bounds.size.height, sm64DiagnosticsWindow.windowLevel);
}

void gfx_ios_diag_log(const char *format, ...) {
    if (!format) return;
    va_list args;
    va_start(args, format);
    NSString *message = [[[NSString alloc] initWithFormat:[NSString stringWithUTF8String:format] arguments:args] autorelease];
    va_end(args);
    if (!message) message = @"<diagnostic format error>";

    @synchronized ([NSObject class]) {
        if (!sm64DiagnosticsPath) {
            NSURL *documents = [[NSFileManager defaultManager] URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
            sm64DiagnosticsPath = [[[documents URLByAppendingPathComponent:@"SM64Diagnostics.log"] path] retain];
            [[NSFileManager defaultManager] createFileAtPath:sm64DiagnosticsPath contents:nil attributes:nil];
            sm64DiagnosticsFile = [[NSFileHandle fileHandleForWritingAtPath:sm64DiagnosticsPath] retain];
            [sm64DiagnosticsFile seekToEndOfFile];
        }
        if (!sm64DiagnosticsDateFormatter) {
            sm64DiagnosticsDateFormatter = [[NSDateFormatter alloc] init];
            sm64DiagnosticsDateFormatter.locale = [[NSLocale alloc] initWithLocaleIdentifier:@"en_US_POSIX"];
            sm64DiagnosticsDateFormatter.timeZone = [NSTimeZone localTimeZone];
            sm64DiagnosticsDateFormatter.dateFormat = @"yyyy-MM-dd'T'HH:mm:ss.SSSZZZZZ";
        }
        if (!sm64DiagnosticsLaunchMarkerWritten) {
            NSDictionary *info = NSBundle.mainBundle.infoDictionary;
            NSString *version = [NSString stringWithFormat:@"%@ (%@)", info[@"CFBundleShortVersionString"] ?: @"unknown", info[@"CFBundleVersion"] ?: @"unknown"];
            NSString *marker = [NSString stringWithFormat:@"%@ === process start pid=%d appVersion=%@ bundle=%@ iOS=%@ ===\n", [sm64DiagnosticsDateFormatter stringFromDate:[NSDate date]], getpid(), version, NSBundle.mainBundle.bundleIdentifier ?: @"unknown", UIDevice.currentDevice.systemVersion];
            @try {
                [sm64DiagnosticsFile writeData:[marker dataUsingEncoding:NSUTF8StringEncoding]];
                [sm64DiagnosticsFile synchronizeFile];
            } @catch (NSException *exception) {
                NSLog(@"SM64 diagnostic launch marker write failed: %@", exception.reason);
            }
            sm64DiagnosticsLaunchMarkerWritten = YES;
        }
        NSString *line = [NSString stringWithFormat:@"%@ %@\n", [sm64DiagnosticsDateFormatter stringFromDate:[NSDate date]], message];
        NSData *data = [line dataUsingEncoding:NSUTF8StringEncoding];
        @try {
            [sm64DiagnosticsFile writeData:data];
            [sm64DiagnosticsFile synchronizeFile];
        } @catch (NSException *exception) {
            NSLog(@"SM64 diagnostic log write failed at %@: %@", sm64DiagnosticsPath, exception.reason);
        }
    }
    NSLog(@"[SM64-DIAG] %@", message);
    dispatch_async(dispatch_get_main_queue(), ^{
        [sm64_diag_controller() appendStatus:message];
    });
}

void gfx_ios_diag_mark_first_game_frame(void) {
    gfx_ios_diag_log("First SM64 Metal command buffer completed on GPU; this does not confirm visible pixels");
    dispatch_async(dispatch_get_main_queue(), ^{
        [sm64_diag_controller() collapsePanel];
    });
}

@interface SM64SceneDelegate : UIResponder <UIWindowSceneDelegate>
@end

@implementation SM64SceneDelegate

- (void)scene:(UIScene *)scene willConnectToSession:(UISceneSession *)session options:(UISceneConnectionOptions *)connectionOptions {
    (void)session;
    (void)connectionOptions;
    if ([scene isKindOfClass:[UIWindowScene class]]) {
        sm64WindowScene = (UIWindowScene *)scene;
        gfx_ios_diag_log("UIApplication scene connected delegate=%s scene=%s role=%s state=%ld bounds=(%.0f,%.0f %.0fx%.0f) scale=%.2f", NSStringFromClass(UIApplication.sharedApplication.delegate.class).UTF8String, NSStringFromClass(scene.class).UTF8String, scene.session.role.UTF8String, (long)scene.activationState, sm64WindowScene.coordinateSpace.bounds.origin.x, sm64WindowScene.coordinateSpace.bounds.origin.y, sm64WindowScene.coordinateSpace.bounds.size.width, sm64WindowScene.coordinateSpace.bounds.size.height, sm64WindowScene.screen.scale);
        gfx_ios_diag_log("Publishing SDL startup notification after UIWindowScene connection scene=%@", sm64WindowScene.session.persistentIdentifier);
        [[NSNotificationCenter defaultCenter] postNotificationName:@"SM64WindowSceneDidConnectNotification" object:sm64WindowScene];
        sm64_diag_create_window(sm64WindowScene);
    }
}

- (void)sceneDidBecomeActive:(UIScene *)scene {
    gfx_ios_diag_log("UIKit scene became active scene=%@ state=%ld", scene.session.persistentIdentifier, (long)scene.activationState);
}

- (void)sceneWillResignActive:(UIScene *)scene {
    gfx_ios_diag_log("UIKit scene will resign active scene=%@ state=%ld", scene.session.persistentIdentifier, (long)scene.activationState);
}

- (void)sceneWillEnterForeground:(UIScene *)scene {
    gfx_ios_diag_log("UIKit scene entering foreground scene=%@", scene.session.persistentIdentifier);
}

- (void)sceneDidEnterBackground:(UIScene *)scene {
    gfx_ios_diag_log("UIKit scene entered background scene=%@", scene.session.persistentIdentifier);
}

- (void)sceneDidDisconnect:(UIScene *)scene {
    if (sm64WindowScene == scene) {
        gfx_ios_diag_log("UIKit scene disconnected scene=%@", scene.session.persistentIdentifier);
        sm64WindowScene = nil;
    }
}

@end

UIWindowScene *gfx_uikit_window_scene_for_screen(UIScreen *screen) {
    if (screen != [UIScreen mainScreen]) {
        gfx_ios_diag_log("SDL UIKit scene lookup rejected non-main screen=%@", screen);
        return nil;
    }

    if (sm64WindowScene && sm64WindowScene.screen == screen) {
        gfx_ios_diag_log("SDL UIKit scene lookup reused active scene=%@ state=%ld", sm64WindowScene.session.persistentIdentifier, (long)sm64WindowScene.activationState);
        return sm64WindowScene;
    }

    for (UIScene *scene in [UIApplication sharedApplication].connectedScenes) {
        if ([scene isKindOfClass:[UIWindowScene class]] &&
            [scene.session.role isEqualToString:UIWindowSceneSessionRoleApplication] &&
            ((UIWindowScene *)scene).screen == screen &&
            scene.activationState != UISceneActivationStateUnattached) {
            sm64WindowScene = (UIWindowScene *)scene;
            gfx_ios_diag_log("SDL UIKit scene lookup selected connected scene=%@ state=%ld", scene.session.persistentIdentifier, (long)scene.activationState);
            return sm64WindowScene;
        }
    }
    gfx_ios_diag_log("SDL UIKit scene lookup found no active scene for screen=%@ connectedScenes=%lu", screen, (unsigned long)UIApplication.sharedApplication.connectedScenes.count);
    return nil;
}
#else
void gfx_ios_diag_log(const char *format, ...) {
    (void)format;
}

void gfx_ios_diag_mark_first_game_frame(void) {}
#endif

@implementation OverlayView

- (id)initWithFrame:(CGRect)frame {
    if(self = [super initWithFrame:frame]) {
        [self setBackgroundColor:[UIColor clearColor]];
    }
    return self;
}

- (void)drawRect:(CGRect)rect {
    CGContextRef context = UIGraphicsGetCurrentContext();
    CGContextClearRect(context, rect);
}

@end

void gfx_uikit_init(UIViewController *viewControllerPointer) {
    gameViewController = viewControllerPointer;
    
    mainWindow = [[[UIApplication sharedApplication] delegate] window];
    
#if TARGET_OS_IOS
    gfx_ios_diag_log("UIKit host init SDL controller=%s controllerView=%@ viewFrame=(%.0f,%.0f %.0fx%.0f) viewBounds=(%.0f,%.0f %.0fx%.0f) delegateWindow=%@", NSStringFromClass(viewControllerPointer.class).UTF8String ?: "nil", viewControllerPointer.view, viewControllerPointer.view.frame.origin.x, viewControllerPointer.view.frame.origin.y, viewControllerPointer.view.frame.size.width, viewControllerPointer.view.frame.size.height, viewControllerPointer.view.bounds.origin.x, viewControllerPointer.view.bounds.origin.y, viewControllerPointer.view.bounds.size.width, viewControllerPointer.view.bounds.size.height, mainWindow);
    if (mainWindow) {
        gfx_ios_diag_log("UIKit delegate window scene=%@ root=%s hidden=%d alpha=%.2f level=%.1f bounds=(%.0f,%.0f %.0fx%.0f)", mainWindow.windowScene.session.persistentIdentifier, NSStringFromClass(mainWindow.rootViewController.class).UTF8String ?: "nil", mainWindow.hidden, mainWindow.alpha, mainWindow.windowLevel, mainWindow.bounds.origin.x, mainWindow.bounds.origin.y, mainWindow.bounds.size.width, mainWindow.bounds.size.height);
    }
    for (UIScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (![scene isKindOfClass:[UIWindowScene class]]) continue;
        UIWindowScene *windowScene = (UIWindowScene *)scene;
        gfx_ios_diag_log("UIKit connected window scene=%@ state=%ld screen=%@ bounds=(%.0f,%.0f %.0fx%.0f) windows=%lu", scene.session.persistentIdentifier, (long)scene.activationState, windowScene.screen, windowScene.coordinateSpace.bounds.origin.x, windowScene.coordinateSpace.bounds.origin.y, windowScene.coordinateSpace.bounds.size.width, windowScene.coordinateSpace.bounds.size.height, (unsigned long)windowScene.windows.count);
        for (UIWindow *window in windowScene.windows) {
            gfx_ios_diag_log("UIKit scene window=%@ root=%s key=%d hidden=%d alpha=%.2f level=%.1f frame=(%.0f,%.0f %.0fx%.0f)", window, NSStringFromClass(window.rootViewController.class).UTF8String ?: "nil", window.isKeyWindow, window.hidden, window.alpha, window.windowLevel, window.frame.origin.x, window.frame.origin.y, window.frame.size.width, window.frame.size.height);
        }
    }
    UIStoryboard *storyboard = [UIStoryboard storyboardWithName:@"Main" bundle:nil];
    tcvc = [[storyboard instantiateViewControllerWithIdentifier:@"TouchControlsViewController"] retain];
    
    if([[UIScreen screens] count] > 1) {
        setup_external_screen();
    } else {
        [gameViewController.view addSubview:tcvc.view];
    }
    
    [gameViewController setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
    UIWindow *sdlWindow = viewControllerPointer.view.window;
    gfx_ios_diag_log("UIKit host attached touch controls controller=%s touchView=%@ to SDL controller view; SDL view window=%@ scene=%@ visible=%d alpha=%.2f frame=(%.0f,%.0f %.0fx%.0f) bounds=(%.0f,%.0f %.0fx%.0f)", NSStringFromClass(tcvc.class).UTF8String, tcvc.view, sdlWindow, sdlWindow.windowScene.session.persistentIdentifier, !viewControllerPointer.view.hidden && viewControllerPointer.view.alpha > 0, viewControllerPointer.view.alpha, viewControllerPointer.view.frame.origin.x, viewControllerPointer.view.frame.origin.y, viewControllerPointer.view.frame.size.width, viewControllerPointer.view.frame.size.height, viewControllerPointer.view.bounds.origin.x, viewControllerPointer.view.bounds.origin.y, viewControllerPointer.view.bounds.size.width, viewControllerPointer.view.bounds.size.height);
    for (UIView *subview in viewControllerPointer.view.subviews) {
        gfx_ios_diag_log("UIKit SDL controller subview=%s classView=%@ hidden=%d alpha=%.2f frame=(%.0f,%.0f %.0fx%.0f) interaction=%d", NSStringFromClass(subview.class).UTF8String, subview, subview.hidden, subview.alpha, subview.frame.origin.x, subview.frame.origin.y, subview.frame.size.width, subview.frame.size.height, subview.userInteractionEnabled);
    }
#elif TARGET_OS_TV
    RemoteInputController *remoteInput = [[RemoteInputController alloc] initWithTarget:gameViewController];
    [remoteInput.onMenuButtonPressed addObject:[NSValue valueWithPointer:tvos_present_main_menu]];
    [remoteInput.onPlayPauseButtonPressed addObject:[NSValue valueWithPointer:simulate_start_button_press]];
#endif
}

#if TARGET_OS_IOS
void setup_external_screen() {
    [tcvc.view removeFromSuperview];
    mainWindow.rootViewController = tcvc;
    
    UIStoryboard *storyboard = [UIStoryboard storyboardWithName:@"Main" bundle:nil];
    ExternalGameViewController *externalVc = [storyboard instantiateViewControllerWithIdentifier:@"ExternalScreen"];
    UIScreen *screen = [UIScreen screens][1];
    externalWindow = [[objc_getClass("SDL_uikitwindow") alloc] initWithFrame:screen.bounds];
    externalWindow.rootViewController = nil;
    externalWindow.rootViewController = externalVc;
    externalWindow.screen = screen;
    externalWindow.screen.overscanCompensation = UIScreenOverscanCompensationScale;
    externalWindow.hidden = NO;
    [externalVc.view addSubview:gameViewController.view];
    gameViewController.view.frame = externalVc.view.bounds;
    gameViewController.view.contentScaleFactor = 1.0;
}

void teardown_external_screen() {
    if(externalWindow != nil) {
        [tcvc.view removeFromSuperview];
        mainWindow.rootViewController = gameViewController;
        [gameViewController.view addSubview:tcvc.view];
        UIScreen *screen = [UIScreen screens][0];
        gameViewController.view.frame = screen.bounds;
        gameViewController.view.contentScaleFactor = screen.scale;
        externalWindow.hidden = YES;
        externalWindow = nil;
    }
}

void gfx_uikit_set_touchscreen_callbacks(void (*down)(void* event), void (*motion)(void* event), void (*up)(void* event)) {
    [tcvc set_touchscreen_callbacks:down motion:motion up:up];
}
#endif

#if TARGET_OS_TV
void tvos_present_main_menu() {
    present_viewcontroller(@"MenuNav", true);
}
#endif
