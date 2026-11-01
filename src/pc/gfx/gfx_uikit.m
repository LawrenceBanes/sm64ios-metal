Warning: truncated output (original token count: 13309)
Total output lines: 998

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
#include "game/mario.h"
#if TARGET_OS_IOS
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>
#if defined(RAPI_METAL)
#import "gfx_metal.h"
#endif
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
static BOOL sm64OrientationSourceLogged;
static BOOL sm64DiagnosticsEmbeddedInSDL;
static int sm64RenderDiagnosticMode;

@interface SM64DiagnosticsViewController : UIViewController <MTKViewDelegate>
@property(nonatomic, strong) UIView *panel;
@property(nonatomic, strong) UILabel *titleLabel;
@property(nonatomic, strong) UILabel *statusLabel;
@property(nonatomic, strong) UILabel *portalStatusLabel;
@property(nonatomic, strong) UIButton *toggleButton;
@property(nonatomic, strong) MTKView *probeView;
@property(nonatomic, strong) UIButton *probeCloseButton;
@property(nonatomic, strong) UIButton *textureCacheButton;
@property(nonatomic, strong) UIButton *renderModeButton;
@property(nonatomic, strong) UIButton *graphicsButton;
@property(nonatomic, strong) UIButton *gameplayButton;
@property(nonatomic, strong) UIView *graphicsPanel;
@property(nonatomic, strong) UISegmentedControl *presetControl;
@property(nonatomic, strong) UISegmentedControl *scaleControl;
@property(nonatomic, strong) UISegmentedControl *msaaControl;
@property(nonatomic, strong) UISegmentedControl *anisotropyControl;
@property(nonatomic, strong) UILabel *performanceLabel;
@property(nonatomic, strong) NSTimer *performanceTimer;
@property(nonatomic, strong) id<MTLCommandQueue> probeQueue;
@property(nonatomic) CGRect hitRect;
@property(nonatomic) NSUInteger probeFrameCount;
@property(nonatomic) BOOL probeAvailabilityLogged;
@property(nonatomic) BOOL collapsed;
@property(nonatomic) BOOL graphicsSettingsVisible;
@property(nonatomic) uint64_t portalStatusGeneration;
@property(nonatomic, strong) NSMutableArray<NSString *> *recentStatus;
- (void)appendStatus:(NSString *)status;
- (void)showPortalStatus:(NSString *)status;
- (void)collapsePanel;
- (void)togglePanel;
- (void)startColorProbe;
- (void)stopColorProbe;
- (void)shareLog;
- (void)updateTextureCacheDiagnosticsButton;
- (void)toggleTextureCacheDiagnostics;
- (void)cycleRenderDiagnosticMode;
- (void)toggleGraphicsSettings;
- (void)applyGraphicsSettingsSelection:(UISegmentedControl *)sender;
- (void)refreshGraphicsSettings;
- (void)refreshGraphicsPerformance;
- (void)resetGraphicsSettings;
- (void)showGameplayActions;
@end

@interface SM64DiagnosticsPassthroughView : UIView
@property(nonatomic) CGRect hitRect;
@end

@implementation SM64DiagnosticsPassthroughView
- (BOOL)pointInside:(CGPoint)point withEvent:(UIEvent *)event {
    (void)event;
    BOOL inside = CGRectContainsPoint(self.hitRect, point);
    static uint64_t hit_test_count;
    uint64_t sequence = ++hit_test_count;
    if (sequence <= 12 || sequence % 240 == 0) {
        gfx_ios_diag_log("UIKit diagnostics overlay hit-test seq=%llu point=(%.1f,%.1f) panelHit=%d hitRect=(%.1f,%.1f %.1fx%.1f) embeddedInSDL=%d", (unsigned long long)sequence, point.x, point.y, inside, self.hitRect.origin.x, self.hitRect.origin.y, self.hitRect.size.width, self.hitRect.size.height, sm64DiagnosticsEmbeddedInSDL);
    }
    return inside;
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

    self.portalStatusLabel = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    self.portalStatusLabel.backgroundColor = [UIColor colorWithWhite:0.03 alpha:0.88];
    self.portalStatusLabel.textColor = UIColor.whiteColor;
    self.portalStatusLabel.font = [UIFont boldSystemFontOfSize:15.0];
    self.portalStatusLabel.textAlignment = NSTextAlignmentCenter;
    self.portalStatusLabel.numberOfLines = 2;
    self.portalStatusLabel.layer.cornerRadius = 10.0;
    self.portalStatusLabel.layer.borderWidth = 1.0;
    self.portalStatusLabel.layer.borderColor = [UIColor colorWithWhite:0.9 alpha:0.5].CGColor;
    self.portalStatusLabel.clipsToBounds = YES;
    self.portalStatusLabel.userInteractionEnabled = NO;
    self.portalStatusLabel.hidden = YES;
    [root addSubview:self.portalStatusLabel];

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
    UIButton *probe = [self buttonWithTitle:@"Metal test" action:@selector(startColorProbe)];
    self.textureCacheButton = [self buttonWithTitle:@"Tex log: OFF" action:@selector(toggleTextureCacheDiagnostics)];
    self.renderModeButton = [self buttonWithTitle:@"Mode: Normal" action:@selector(cycleRenderDiagnosticMode)];
    self.graphicsButton = [self buttonWithTitle:@"Graphics" action:@selector(toggleGraphicsSettings)];
    self.gameplayButton = [self buttonWithTitle:@"Gameplay" action:@selector(showGameplayActions)];
    [buttons addArrangedSubview:share];
    [buttons addArrangedSubview:probe];
    [buttons addArrangedSubview:self.textureCacheButton];
    [buttons addArrangedSubview:self.renderModeButton];
    [buttons addArrangedSubview:self.graphicsButton];
    [buttons addArrangedSubview:self.gameplayButton];
    self.renderModeButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    self.textureCacheButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    self.graphicsButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    self.gameplayButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    [self updateTextureCacheDiagnosticsButton];
    [self buildGraphicsSettingsPanel];
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

- (void)showGameplayActions {
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:@"Gameplay test controls"
        message:@"Kart: press L nearby to enter or exit. Touch joystick left/right steers; push up for forward throttle or down for reverse. B accelerates and Z only brakes. Press A to hop; hold A with left/right steering after the hop to powerslide, then release A after charging for a mini-turbo. Use Spawn kart nearby in other levels. Luigi is a temporary green palette on Mario’s model, Yoshi remains available, and the portal surface is an animated fallback, not a linked world view."
        preferredStyle:UIAlertControllerStyleActionSheet];
    [menu addAction:[UIAlertAction actionWithTitle:@"Toggle Mario / temporary Luigi look" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        mario_ios_request_character_switch();
        gfx_ios_diag_log("Gameplay menu queued Mario/temporary-Luigi palette switch");
        [self appendStatus:@"Character switch queued for game thread"];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Toggle Yoshi mount" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        mario_ios_request_yoshi_toggle();
        gfx_ios_diag_log("Gameplay menu queued Yoshi mount/dismount request");
        [self appendStatus:@"Yoshi mount request queued; approach Yoshi if it does not mount"];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Spawn / reposition kart nearby" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        mario_ios_request_kart_spawn();
        gfx_ios_diag_log("Gameplay menu queued kart spawn/reposition request");
        [self appendStatus:@"Kart spawn request queued; press L next to it to drive"];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Fire blue portal" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        mario_ios_request_portal_blue();
        gfx_ios_diag_log("Gameplay menu queued blue portal placement");
        [self appendStatus:@"Blue portal placement queued; aim at a broad static wall"];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Fire orange portal" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        mario_ios_request_portal_orange();
        gfx_ios_diag_log("Gameplay menu queued orange portal placement");
        [self appendStatus:@"Orange portal placement queued; aim at another broad static wall"];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Toggle portal gun" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        mario_ios_request_portal_enable_toggle();
        gfx_ios_diag_log("Gameplay menu queued portal gun enable toggle");
        [self appendStatus:@"Portal gun toggle queued for game thread"];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Reset portals" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) {
        (void)action;
        mario_ios_request_portal_reset();
        gfx_ios_diag_log("Gameplay menu queued portal reset");
        [self appendStatus:@"Portal reset queued for game thread"];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    UIPopoverPresentationController *popover = menu.popoverPresentationController;
    popover.sourceView = self.gameplayButton;
    popover.sourceRect = self.gameplayButton.bounds;
    [self presentViewController:menu animated:YES completion:nil];
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    CGFloat width = MIN(430.0, MAX(250.0, self.view.bounds.size.width - 24.0));
    CGFloat panelWidth = self.collapsed ? 118.0 : width;
    CGFloat panelHeight = self.collapsed ? 38.0 : 154.0;
    CGFloat top = self.view.safeAreaInsets.top + 6.0;
    CGRect frame = CGRectMake(MAX(10.0, (self.view.bounds.size.width - panelWidth) * 0.5), top, panelWidth, panelHeight);
    self.panel.frame = frame;
    CGFloat toastWidth = MIN(360.0, MAX(220.0, self.view.bounds.size.width - 32.0));
    self.portalStatusLabel.frame = CGRectMake((self.view.bounds.size.width - toastWidth) * 0.5,
        top + panelHeight + 8.0, toastWidth, 50.0);
    ((SM64DiagnosticsPassthroughView *)self.view).hitRect = (self.probeView || self.graphicsSettingsVisible) ? self.view.bounds : CGRectInset(frame, -2, -2);
    CGFloat settingsWidth = MIN(520.0, MAX(320.0, self.view.bounds.size.width - 24.0));
    CGFloat settingsHeight = MIN(374.0, MAX(330.0, self.view.bounds.size.height - 20.0));
    self.graphicsPanel.frame = CGRectMake((self.view.bounds.size.width - settingsWidth) * 0.5,
        (self.view.bounds.size.height - settingsHeight) * 0.5, settingsWidth, settingsHeight);
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

- (void)buildGraphicsSettingsPanel {
    self.graphicsPanel = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 520, 374)] autorelease];
    self.graphicsPanel.backgroundColor = [UIColor colorWithWhite:0.04 alpha:0.97];
    self.graphicsPanel.layer.cornerRadius = 12.0;
    self.graphicsPanel.layer.borderWidth = 1.0;
    self.graphicsPanel.layer.borderColor = [UIColor colorWithWhite:0.7 alpha:0.85].CGColor;
    self.graphicsPanel.hidden = YES;
    [self.view addSubview:self.graphicsPanel];

    UILabel *heading = [[[UILabel alloc] initWithFrame:CGRectMake(14, 8, 260, 24)] autorelease];
    heading.text = @"Graphics Settings";
    heading.textColor = UIColor.whiteColor;
    heading.font = [UIFont boldSystemFontOfSize:16.0];
    [self.graphicsPanel addSubview:heading];
    UIButton *close = [self buttonWithTitle:@"Close" action:@selector(toggleGraphicsSettings)];
    close.frame = CGRectMake(0, 5, 62, 32);
    close.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    [self.graphicsPanel addSubview:close];
    self.graphicsPanel.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin |
        UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleBottomMargin;

    self.presetControl = [self graphicsControl:@[@"Classic", @"Enhanced"] y:38];
    [self addGraphicsLabel:@"Render scale" y:78];
    self.scaleControl = [self graphicsControl:@[@"50%", @"75%", @"100%"] y:96];
    [self addGraphicsLabel:@"Anti-aliasing" y:134];
    self.msaaControl = [self graphicsControl:@[@"Off", @"2×", @"4×"] y:152];
    [self addGraphicsLabel:@"Anisotropic filtering" y:190];
    self.anisotropyControl = [self graphicsControl:@[@"Off", @"2×", @"4×", @"8×", @"16×"] y:208];

    self.performanceLabel = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    self.performanceLabel.textColor = [UIColor colorWithWhite:0.94 alpha:1.0];
    self.performanceLabel.font = [UIFont monospacedSystemFontOfSize:10.0 weight:UIFontWeightRegular];
    self.performanceLabel.numberOfLines = 4;
    self.performanceLabel.frame = CGRectMake(14, 245, 492, 72);
    self.performanceLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [self.graphicsPanel addSubview:self.performanceLabel];

    UIButton *reset = [self buttonWithTitle:@"Reset defaults" action:@selector(resetGraphicsSettings)];
    reset.frame = CGRectMake(14, 327, 145, 34);
    reset.autoresizingMask = UIViewAutoresizingFlexibleTopMargin;
    [self.graphicsPanel addSubview:reset];
    UIButton *done = [self buttonWithTitle:@"Done" action:@selector(toggleGraphicsSettings)];
    done.frame = CGRectMake(0, 327, 80, 34);
    done.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleTopMargin;
    [self.graphicsPanel addSubview:done];
}

- (UISegmentedControl *)graphicsControl:(NSArray<NSString *> *)titles y:(CGFloat)y {
    UISegmentedControl *control = [[[UISegmentedControl alloc] initWithItems:titles] autorelease];
    control.frame = CGRectMake(14, y, 492, 31);
    control.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    control.backgroundColor = [UIColor colorWithWhite:0.20 alpha:1.0];
    [control addTarget:self action:@selector(applyGraphicsSettingsSelection:) forControlEvents:UIControlEventValueChanged];
    [self.graphicsPanel addSubview:control];
    return control;
}

- (void)addGraphicsLabel:(NSString *)text y:(CGFloat)y {
    UILabel *label = [[[UILabel alloc] initWithFrame:CGRectMake(14, y, 240, 17)] autorelease];
    label.text = text;
    label.textColor = [UIColor colorWithWhite:0.86 alpha:1.0];
    label.font = [UIFont systemFontOfSize:11.0 weight:UIFontWeightSemibold];
    [self.graphicsPanel addSubview:label];
}

- (void)appendStatus:(NSString *)status {
    if (!status.l…3309 tokens truncated…iagnosticsViewController *controller = [[SM64DiagnosticsViewController alloc] init];
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
    [controller collapsePanel];
}

static void sm64_diag_attach_to_game_controller(UIViewController *host) {
    SM64DiagnosticsViewController *controller = sm64_diag_controller();
    UIWindow *diagnosticWindow = sm64DiagnosticsWindow;
    if (!host || !controller) return;
    [controller retain];

    if (controller.parentViewController == host) {
        if (controller.view.superview != host.view) {
            controller.view.frame = host.view.bounds;
            controller.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
            [host.view addSubview:controller.view];
        } else {
            [host.view bringSubviewToFront:controller.view];
        }
        if (diagnosticWindow) {
            diagnosticWindow.hidden = YES;
            if (diagnosticWindow.rootViewController == controller)
                diagnosticWindow.rootViewController = nil;
            [diagnosticWindow release];
            sm64DiagnosticsWindow = nil;
        }
        sm64DiagnosticsEmbeddedInSDL = YES;
        gfx_ios_diag_log("Native diagnostics already embedded in SDL controller; repeated attachment reused host=%@ window=%@", host, host.view.window);
        [controller release];
        return;
    }

    if (controller.parentViewController) {
        UIViewController *oldParent = controller.parentViewController;
        gfx_ios_diag_log("Detaching native diagnostics from prior container=%@ before SDL host=%@", oldParent, host);
        [controller willMoveToParentViewController:nil];
        [controller.view removeFromSuperview];
        [controller removeFromParentViewController];
    }

    // A window root controller is still owned by UIKit even though its
    // parentViewController is nil. Release that root relationship before
    // adopting the retained controller as the SDL container's child.
    if (diagnosticWindow) {
        gfx_ios_diag_log("Moving native diagnostics out of diagnostic UIWindow root=%@ into SDL host=%@ window=%@", diagnosticWindow.rootViewController, host, host.view.window);
        diagnosticWindow.hidden = YES;
        if (diagnosticWindow.rootViewController == controller) {
            diagnosticWindow.rootViewController = nil;
        } else if (diagnosticWindow.rootViewController) {
            gfx_ios_diag_log("Unexpected diagnostic UIWindow root during migration root=%@ expected=%@", diagnosticWindow.rootViewController, controller);
        }
    }
    [controller.view removeFromSuperview];
    [host addChildViewController:controller];
    controller.view.frame = host.view.bounds;
    controller.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    controller.view.opaque = NO;
    [host.view addSubview:controller.view];
    [controller didMoveToParentViewController:host];
    sm64DiagnosticsEmbeddedInSDL = YES;
    [host.view setNeedsLayout];
    [host.view layoutIfNeeded];

    if (diagnosticWindow) {
        [diagnosticWindow release];
        sm64DiagnosticsWindow = nil;
    }
    [controller release];
    gfx_ios_diag_log("Native diagnostics embedded as pass-through SDL subview; separate diagnostic UIWindow root released before containment sceneWindows=%lu hostSubviews=%lu", (unsigned long)host.view.window.windowScene.windows.count, (unsigned long)host.view.subviews.count);
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

void gfx_ios_portal_status(const char *message) {
    if (!message || !message[0]) return;
    NSString *status = [NSString stringWithUTF8String:message];
    if (!status.length) return;
    dispatch_async(dispatch_get_main_queue(), ^{
        [sm64_diag_controller() showPortalStatus:status];
    });
}

bool gfx_ios_texture_cache_diagnostics_enabled(void) {
#if TARGET_OS_IOS
    return [[NSUserDefaults standardUserDefaults] boolForKey:@"SM64TextureCacheDiagnosticsEnabled"];
#else
    return false;
#endif
}

int gfx_ios_render_diagnostic_mode(void) {
    return sm64RenderDiagnosticMode;
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
#if defined(RAPI_METAL)
    gfx_metal_set_scene_active(true);
#endif
}

- (void)sceneWillResignActive:(UIScene *)scene {
    gfx_ios_diag_log("UIKit scene will resign active scene=%@ state=%ld", scene.session.persistentIdentifier, (long)scene.activationState);
#if defined(RAPI_METAL)
    gfx_metal_set_scene_active(false);
#endif
}

- (void)sceneWillEnterForeground:(UIScene *)scene {
    gfx_ios_diag_log("UIKit scene entering foreground scene=%@", scene.session.persistentIdentifier);
}

- (void)sceneDidEnterBackground:(UIScene *)scene {
    gfx_ios_diag_log("UIKit scene entered background scene=%@", scene.session.persistentIdentifier);
#if defined(RAPI_METAL)
    gfx_metal_set_scene_active(false);
#endif
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

UIInterfaceOrientation gfx_ios_interface_orientation(void) {
    if (@available(iOS 27.0, *)) {
        UIWindowScene *scene = gfx_uikit_window_scene_for_screen(UIScreen.mainScreen);
        UIInterfaceOrientation orientation = scene ? scene.effectiveGeometry.interfaceOrientation : UIInterfaceOrientationUnknown;
        if (!sm64OrientationSourceLogged) {
            sm64OrientationSourceLogged = YES;
            gfx_ios_diag_log("UIKit orientation source=UIWindowScene.effectiveGeometry orientation=%ld scene=%@ bounds=(%.0fx%.0f)", (long)orientation, scene.session.persistentIdentifier, scene.coordinateSpace.bounds.size.width, scene.coordinateSpace.bounds.size.height);
        }
        return orientation;
    }
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    UIInterfaceOrientation orientation = UIApplication.sharedApplication.statusBarOrientation;
#pragma clang diagnostic pop
    return orientation;
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
    tcvc.view.frame = gameViewController.view.bounds;
    tcvc.view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    
    if([[UIScreen screens] count] > 1) {
        setup_external_screen();
    } else {
        [gameViewController.view addSubview:tcvc.view];
    }

    // The startup panel uses a temporary scene window before SDL exists.
    // Once the SDL controller is ready, keep diagnostics in its view tree so
    // a second UIWindow cannot sit above the Metal surface or consume game input.
    sm64_diag_attach_to_game_controller(viewControllerPointer);
    
    [gameViewController setNeedsUpdateOfScreenEdgesDeferringSystemGestures];
    UIWindow *sdlWindow = viewControllerPointer.view.window;
    gfx_ios_diag_log("UIKit host attached touch controls controller=%s touchView=%@ to SDL controller view; SDL view window=%@ scene=%@ visible=%d alpha=%.2f frame=(%.0f,%.0f %.0fx%.0f) bounds=(%.0f,%.0f %.0fx%.0f)", NSStringFromClass(tcvc.class).UTF8String, tcvc.view, sdlWindow, sdlWindow.windowScene.session.persistentIdentifier, !viewControllerPointer.view.hidden && viewControllerPointer.view.alpha > 0, viewControllerPointer.view.alpha, viewControllerPointer.view.frame.origin.x, viewControllerPointer.view.frame.origin.y, viewControllerPointer.view.frame.size.width, viewControllerPointer.view.frame.size.height, viewControllerPointer.view.bounds.origin.x, viewControllerPointer.view.bounds.origin.y, viewControllerPointer.view.bounds.size.width, viewControllerPointer.view.bounds.size.height);
    NSUInteger subviewIndex = 0;
    for (UIView *subview in viewControllerPointer.view.subviews) {
        gfx_ios_diag_log("UIKit SDL controller subview zIndex=%lu class=%s view=%@ hidden=%d alpha=%.2f frame=(%.0f,%.0f %.0fx%.0f) interaction=%d", (unsigned long)subviewIndex++, NSStringFromClass(subview.class).UTF8String, subview, subview.hidden, subview.alpha, subview.frame.origin.x, subview.frame.origin.y, subview.frame.size.width, subview.frame.size.height, subview.userInteractionEnabled);
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
