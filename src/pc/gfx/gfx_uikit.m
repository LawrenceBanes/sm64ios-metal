//
//  gfx_uikit.m
//  sm64ios
//
//  Created by Christian Kosman on 9/21/21.
//

#include <math.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include "gfx_shader64_pack.h"
#import <objc/runtime.h>
#import "gfx_uikit.h"
#include "game/mario.h"
#include "game/ios_reality.h"
#include "scene_diagnostic.h"
#include "game/area.h"
#ifdef IOS_O2R_SCENE_RUNTIME
#include "engine/world_scene_view.h"
#endif
#import "src/ios/CheatsViewController.h"
#if TARGET_OS_IOS
#include "ios_app_branding.h"
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

#if TARGET_OS_IOS
@interface SM64DiagnosticsViewController : UIViewController <MTKViewDelegate, UIDocumentPickerDelegate>
#else
@interface SM64DiagnosticsViewController : UIViewController <MTKViewDelegate>
#endif
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
@property(nonatomic, strong) UIButton *realityButton;
@property(nonatomic, strong) UIView *graphicsPanel;
@property(nonatomic, strong) UISegmentedControl *presetControl;
@property(nonatomic, strong) UISegmentedControl *scaleControl;
@property(nonatomic, strong) UISegmentedControl *msaaControl;
@property(nonatomic, strong) UISegmentedControl *anisotropyControl;
@property(nonatomic, strong) UIButton *ssaoButton;
@property(nonatomic, strong) UIButton *postButton;
@property(nonatomic, strong) UIButton *fxaaButton;
@property(nonatomic, strong) UIButton *exposureButton;
@property(nonatomic, strong) UIButton *fogButton;
@property(nonatomic, strong) UIButton *bloomButton;
@property(nonatomic, strong) UIButton *gradeButton;
@property(nonatomic, strong) UIButton *fogProfileButton;
@property(nonatomic, strong) UIButton *shadowButton;
@property(nonatomic, strong) UIButton *aoQualityButton;
@property(nonatomic, strong) UIButton *skyPresetButton;
@property(nonatomic, strong) UIButton *masterEffectsButton;
@property(nonatomic, strong) UIButton *godRaysButton;
@property(nonatomic, strong) UIButton *shaderPacksButton;
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
#if TARGET_OS_IOS && defined(RAPI_METAL)
- (void)openShaderPacks;
- (void)importShaderPack;
- (void)deleteShaderPack;
- (void)activateShaderPack:(NSString *)packID;
- (void)configureShaderPack:(NSString *)packID;
- (NSDictionary *)shaderManifestForID:(NSString *)packID root:(NSString *)root error:(NSString **)errorOut;
- (void)installShaderArchiveAtURL:(NSURL *)url;
#endif
- (void)toggleSSAO;
- (void)togglePostprocessing;
- (void)toggleFXAA;
- (void)stepExposure;
- (void)toggleFog;
- (void)toggleBloom;
- (void)openColorSettings;
- (void)openAOSettings;
- (void)openFogSettings;
- (void)openToneSettings;
- (void)showGameplayActions;
#ifdef IOS_O2R_SCENE_RUNTIME
- (void)showImportedWorldViewSettings;
#endif
- (void)showRealityDebugger;
- (void)showRealityCategory:(NSInteger)category;
@end

#if TARGET_OS_IOS && defined(RAPI_METAL)
@interface SHADER64ParameterController : UIViewController
@property(nonatomic, copy) NSArray<NSDictionary *> *definitions;
@property(nonatomic, strong) NSMutableArray<NSNumber *> *values;
@property(nonatomic, strong) NSMutableArray *controls;
@property(nonatomic, strong) NSMutableArray<UILabel *> *valueLabels;
@property(nonatomic, copy) void (^onApply)(NSArray<NSNumber *> *values);
@end

@implementation SHADER64ParameterController
- (void)loadView {
    UIView *root = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 420, 560)] autorelease];
    root.backgroundColor = UIColor.systemBackgroundColor;
    self.view = root;
    UIScrollView *scroll = [[[UIScrollView alloc] initWithFrame:root.bounds] autorelease];
    scroll.translatesAutoresizingMaskIntoConstraints = NO;
    [root addSubview:scroll];
    UIStackView *stack = [[[UIStackView alloc] initWithFrame:CGRectZero] autorelease];
    stack.axis = UILayoutConstraintAxisVertical;
    stack.spacing = 14.0;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    [scroll addSubview:stack];
    [NSLayoutConstraint activateConstraints:@[
        [scroll.leadingAnchor constraintEqualToAnchor:root.leadingAnchor],
        [scroll.trailingAnchor constraintEqualToAnchor:root.trailingAnchor],
        [scroll.topAnchor constraintEqualToAnchor:root.topAnchor],
        [scroll.bottomAnchor constraintEqualToAnchor:root.bottomAnchor],
        [stack.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:20],
        [stack.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-20],
        [stack.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:20],
        [stack.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-20],
        [stack.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-40]
    ]];
    self.controls = [NSMutableArray arrayWithCapacity:self.definitions.count];
    self.valueLabels = [NSMutableArray arrayWithCapacity:self.definitions.count];
    for (NSUInteger index = 0; index < self.definitions.count; ++index) {
        NSDictionary *definition = self.definitions[index];
        NSString *type = definition[@"type"];
        UILabel *title = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
        title.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];
        title.text = definition[@"label"];
        [stack addArrangedSubview:title];
        id control = nil;
        if ([type isEqualToString:@"float"]) {
            UISlider *slider = [[[UISlider alloc] initWithFrame:CGRectZero] autorelease];
            slider.minimumValue = [definition[@"min"] floatValue];
            slider.maximumValue = [definition[@"max"] floatValue];
            slider.value = [self.values[index] floatValue];
            slider.enabled = slider.maximumValue > slider.minimumValue;
            [slider addTarget:self action:@selector(parameterChanged:) forControlEvents:UIControlEventValueChanged];
            control = slider;
        } else if ([type isEqualToString:@"boolean"]) {
            UISwitch *toggle = [[[UISwitch alloc] initWithFrame:CGRectZero] autorelease];
            toggle.on = [self.values[index] boolValue];
            [toggle addTarget:self action:@selector(parameterChanged:) forControlEvents:UIControlEventValueChanged];
            control = toggle;
        } else {
            NSArray *options = definition[@"options"];
            UISegmentedControl *selector = [[[UISegmentedControl alloc] initWithItems:options] autorelease];
            selector.selectedSegmentIndex = [self.values[index] integerValue];
            selector.apportionsSegmentWidthsByContent = YES;
            [selector addTarget:self action:@selector(parameterChanged:) forControlEvents:UIControlEventValueChanged];
            control = selector;
        }
        [control setTag:(NSInteger)index];
        [stack addArrangedSubview:control];
        UILabel *value = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
        value.font = [UIFont preferredFontForTextStyle:UIFontTextStyleCaption1];
        [stack addArrangedSubview:value];
        [self.controls addObject:control];
        [self.valueLabels addObject:value];
        [self refreshLabelAtIndex:index];
    }
    UIButton *apply = [UIButton buttonWithType:UIButtonTypeSystem];
    [apply setTitle:@"Apply" forState:UIControlStateNormal];
    [apply addTarget:self action:@selector(applyValues) forControlEvents:UIControlEventTouchUpInside];
    [stack addArrangedSubview:apply];
    UIButton *reset = [UIButton buttonWithType:UIButtonTypeSystem];
    [reset setTitle:@"Reset to Defaults" forState:UIControlStateNormal];
    [reset addTarget:self action:@selector(resetValues) forControlEvents:UIControlEventTouchUpInside];
    [stack addArrangedSubview:reset];
    UIButton *cancel = [UIButton buttonWithType:UIButtonTypeSystem];
    [cancel setTitle:@"Cancel" forState:UIControlStateNormal];
    [cancel addTarget:self action:@selector(cancelChanges) forControlEvents:UIControlEventTouchUpInside];
    [stack addArrangedSubview:cancel];
}

- (void)refreshLabelAtIndex:(NSUInteger)index {
    NSDictionary *definition = self.definitions[index];
    NSString *type = definition[@"type"];
    if ([type isEqualToString:@"boolean"]) self.valueLabels[index].text = [self.values[index] boolValue] ? @"On" : @"Off";
    else if ([type isEqualToString:@"select"]) {
        NSArray *options = definition[@"options"];
        NSInteger selected = [self.values[index] integerValue];
        self.valueLabels[index].text = (selected >= 0 && selected < (NSInteger)options.count) ? options[selected] : @"";
    } else self.valueLabels[index].text = [NSString stringWithFormat:@"%.3f", [self.values[index] doubleValue]];
}

- (void)parameterChanged:(UIControl *)sender {
    NSUInteger index = (NSUInteger)sender.tag;
    NSNumber *value = nil;
    if ([sender isKindOfClass:[UISlider class]]) value = @([(UISlider *)sender value]);
    else if ([sender isKindOfClass:[UISwitch class]]) value = @([(UISwitch *)sender isOn] ? 1 : 0);
    else {
        NSInteger selected = [(UISegmentedControl *)sender selectedSegmentIndex];
        NSArray *options = self.definitions[index][@"options"];
        selected = MAX(0, MIN((NSInteger)options.count - 1, selected));
        [(UISegmentedControl *)sender setSelectedSegmentIndex:selected];
        value = @(selected);
    }
    self.values[index] = value;
    [self refreshLabelAtIndex:index];
}

- (void)applyValues {
    if (self.onApply) self.onApply([[self.values copy] autorelease]);
    [self dismissViewControllerAnimated:YES completion:nil];
}

- (void)resetValues {
    for (NSUInteger index = 0; index < self.definitions.count; ++index) {
        NSDictionary *definition = self.definitions[index];
        NSNumber *value = definition[@"default"];
        self.values[index] = value;
        id control = self.controls[index];
        if ([control isKindOfClass:[UISlider class]]) [(UISlider *)control setValue:value.floatValue animated:YES];
        else if ([control isKindOfClass:[UISwitch class]]) [(UISwitch *)control setOn:value.boolValue animated:YES];
        else [(UISegmentedControl *)control setSelectedSegmentIndex:value.integerValue];
        [self refreshLabelAtIndex:index];
    }
}

- (void)cancelChanges { [self dismissViewControllerAnimated:YES completion:nil]; }
@end
#endif

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
    title.text = [NSString stringWithFormat:@"%@ Diagnostics", ios_app_display_name()];
    title.textColor = UIColor.whiteColor;
    title.font = [UIFont boldSystemFontOfSize:14.0];
    title.adjustsFontSizeToFitWidth = YES;
    title.minimumScaleFactor = 0.7;
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
        [self.toggleButton.heightAnchor constraintEqualToConstant:28],
        [title.trailingAnchor constraintLessThanOrEqualToAnchor:self.toggleButton.leadingAnchor constant:-6]
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
    self.gameplayButton = [self buttonWithTitle:@"Cheats / Play" action:@selector(showGameplayActions)];
    self.realityButton = [self buttonWithTitle:@"Reality" action:@selector(showRealityDebugger)];
    [buttons addArrangedSubview:share];
    [buttons addArrangedSubview:probe];
    [buttons addArrangedSubview:self.textureCacheButton];
    [buttons addArrangedSubview:self.renderModeButton];
    [buttons addArrangedSubview:self.graphicsButton];
    [buttons addArrangedSubview:self.gameplayButton];
    [buttons addArrangedSubview:self.realityButton];
    self.renderModeButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    self.textureCacheButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    self.graphicsButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    self.gameplayButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    self.realityButton.titleLabel.font = [UIFont systemFontOfSize:9.0 weight:UIFontWeightSemibold];
    [self updateTextureCacheDiagnosticsButton];
    [self buildGraphicsSettingsPanel];
    self.recentStatus = [NSMutableArray array];
#ifdef IOS_O2R_SCENE_RUNTIME
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    if ([defaults objectForKey:@"SM64ImportedWorldDistance"] != nil) {
        NSInteger savedDistance = [defaults integerForKey:@"SM64ImportedWorldDistance"];
        if (savedDistance >= WORLD_SCENE_VIEW_ORIGINAL && savedDistance < WORLD_SCENE_VIEW_DISTANCE_COUNT)
            world_scene_view_set_distance((enum WorldSceneViewDistance)savedDistance);
    }
    if ([defaults objectForKey:@"SM64ImportedWorldFog"] != nil)
        world_scene_view_set_fog_enabled([defaults boolForKey:@"SM64ImportedWorldFog"]);
#endif
    [self appendStatus:@"Native UIKit diagnostic panel ready"];
}

- (UIButton *)buttonWithTitle:(NSString *)title action:(SEL)action {
    UIButton *button = [UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    button.titleLabel.font = [UIFont systemFontOfSize:11.0 weight:UIFontWeightSemibold];
    button.titleLabel.adjustsFontSizeToFitWidth = YES;
    button.titleLabel.minimumScaleFactor = 0.62;
    button.backgroundColor = [UIColor colorWithWhite:0.23 alpha:1.0];
    button.layer.cornerRadius = 6.0;
    [button setTitleColor:UIColor.whiteColor forState:UIControlStateNormal];
    [button addTarget:self action:action forControlEvents:UIControlEventTouchUpInside];
    return button;
}

- (void)showCheats {
    UIViewController *controller = ios_cheats_playground_controller();
    [self presentViewController:controller animated:YES completion:nil];
}
- (void)showGameplayActions {
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:[NSString stringWithFormat:@"%@ Gameplay", ios_app_display_name()]
        message:[NSString stringWithFormat:@"%@\n\nKart: press L nearby to enter or exit. Touch joystick left/right steers; push up for forward throttle or down for reverse. B accelerates and Z only brakes. Press A to hop; hold A with left/right steering after the hop to powerslide, then release A after charging for a mini-turbo. Use Spawn kart nearby in other levels. Luigi is a temporary green palette on Mario’s model, Yoshi remains available, and the portal surface is an animated fallback, not a linked world view.", ios_app_release_version()]
        preferredStyle:UIAlertControllerStyleActionSheet];
    [menu addAction:[UIAlertAction actionWithTitle:@"Cheats — Hat, Flight, Bowser, SuperSlide" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        [self dismissViewControllerAnimated:YES completion:^{ [self showCheats]; }];
    }]];
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
#ifdef IOS_SCENE_DIAGNOSTIC
    [menu addAction:[UIAlertAction actionWithTitle:@"Enter / leave WSC2 diagnostic" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        scene_diagnostic_request(SCENE_DIAGNOSTIC_TOGGLE_AREA);
    }]];
    NSString *wsc3Title = area_scene_package_id() == SCENE_DIAGNOSTIC_WSC3_PACKAGE ?
        @"Leave WSC3 Diagnostic" : @"Enter WSC3 Diagnostic";
    [menu addAction:[UIAlertAction actionWithTitle:wsc3Title style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        scene_diagnostic_request(SCENE_DIAGNOSTIC_TOGGLE_WSC3);
    }]];
    NSString *externalTitle = area_scene_package_id() == SCENE_EXTERNAL_PACKAGE ?
        @"Leave Hyrule Field" : @"Load Hyrule Field (WSC5)";
    [menu addAction:[UIAlertAction actionWithTitle:externalTitle style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        gfx_ios_diag_log("Gameplay menu queued Hyrule Field WSC5 %s package=4100",
                         area_scene_package_id() == SCENE_EXTERNAL_PACKAGE ? "leave" : "load");
        scene_diagnostic_request(SCENE_DIAGNOSTIC_TOGGLE_EXTERNAL);
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Probe WSC2 failed-load recovery" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        scene_diagnostic_request(SCENE_DIAGNOSTIC_CHECK_FAILED_LOAD);
    }]];
#ifdef IOS_O2R_SCENE_RUNTIME
    [menu addAction:[UIAlertAction actionWithTitle:@"Imported world view settings" style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
        (void)action;
        [self showImportedWorldViewSettings];
    }]];
#endif
#endif
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    UIPopoverPresentationController *popover = menu.popoverPresentationController;
    popover.sourceView = self.gameplayButton;
    popover.sourceRect = self.gameplayButton.bounds;
    [self presentViewController:menu animated:YES completion:nil];
}

#ifdef IOS_O2R_SCENE_RUNTIME
- (void)showImportedWorldViewSettings {
    NSArray<NSString *> *presets = @[@"Original / compatibility", @"Extended", @"Maximum practical distance"];
    NSString *selected = presets[world_scene_view_get_distance()];
    NSString *fog = world_scene_view_get_fog_enabled() ? @"On" : @"Off";
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:@"Imported World View"
        message:[NSString stringWithFormat:@"Draw distance: %@\nWSC fog: %@\nFog changes appearance independently of geometry visibility.", selected, fog]
        preferredStyle:UIAlertControllerStyleActionSheet];
    NSArray<NSNumber *> *values = @[@(WORLD_SCENE_VIEW_ORIGINAL), @(WORLD_SCENE_VIEW_EXTENDED), @(WORLD_SCENE_VIEW_MAXIMUM)];
    for (NSUInteger i = 0; i < presets.count; ++i) {
        NSString *title = presets[i];
        NSInteger value = values[i].integerValue;
        [menu addAction:[UIAlertAction actionWithTitle:title style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            (void)action;
            world_scene_view_set_distance((enum WorldSceneViewDistance)value);
            [[NSUserDefaults standardUserDefaults] setInteger:value forKey:@"SM64ImportedWorldDistance"];
            gfx_ios_diag_log("Imported-world draw distance preset selected index=%ld", (long)value);
            [self appendStatus:[NSString stringWithFormat:@"Imported world distance: %@", title]];
        }]];
    }
    BOOL fogEnabled = world_scene_view_get_fog_enabled();
    [menu addAction:[UIAlertAction actionWithTitle:(fogEnabled ? @"Turn WSC fog off" : @"Turn WSC fog on")
        style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            (void)action;
            BOOL enabled = !fogEnabled;
            world_scene_view_set_fog_enabled(enabled);
            [[NSUserDefaults standardUserDefaults] setBool:enabled forKey:@"SM64ImportedWorldFog"];
            gfx_ios_diag_log("Imported-world WSC fog %s", enabled ? "enabled" : "disabled");
            [self appendStatus:[NSString stringWithFormat:@"Imported world fog %@", enabled ? @"on" : @"off"]];
        }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    UIPopoverPresentationController *popover = menu.popoverPresentationController;
    popover.sourceView = self.gameplayButton;
    popover.sourceRect = self.gameplayButton.bounds;
    [self presentViewController:menu animated:YES completion:nil];
}
#endif

- (void)showRealityDebugger {
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:@"Reality Debugger"
        message:@"Live changes apply at the next game update. Conflicting effects are rejected. Experimental entries explain what engine support is still missing."
        preferredStyle:UIAlertControllerStyleActionSheet];
    NSArray<NSString *> *titles = @[@"Physics", @"World", @"Player", @"Experimental", @"Object Actions"];
    for (NSInteger category = 0; category < (NSInteger)titles.count; category++) {
        [menu addAction:[UIAlertAction actionWithTitle:titles[category] style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            (void)action;
            [self showRealityCategory:category];
        }]];
    }
    [menu addAction:[UIAlertAction actionWithTitle:@"Reset all Reality cheats" style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) {
        (void)action;
        ios_reality_request_reset();
        gfx_ios_diag_log("Reality Debugger reset queued for game thread");
        [self appendStatus:@"Reality settings reset queued"];
    }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    UIPopoverPresentationController *popover = menu.popoverPresentationController;
    popover.sourceView = self.realityButton;
    popover.sourceRect = self.realityButton.bounds;
    [self presentViewController:menu animated:YES completion:nil];
}

- (void)showRealityCategory:(NSInteger)category {
    const char *categoryName = "Physics";
    switch (category) {
        case IOS_REALITY_CATEGORY_WORLD: categoryName = "World"; break;
        case IOS_REALITY_CATEGORY_PLAYER: categoryName = "Player"; break;
        case IOS_REALITY_CATEGORY_EXPERIMENTAL: categoryName = "Experimental"; break;
        case IOS_REALITY_CATEGORY_ACTION: categoryName = "Object Actions"; break;
        default: break;
    }
    NSString *title = [NSString stringWithUTF8String:categoryName];
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:title
        message:@"Tap a setting for details or to change it. Numeric values are configurable separately."
        preferredStyle:UIAlertControllerStyleActionSheet];
    for (NSInteger index = 0; index < IOS_REALITY_CHEAT_COUNT; index++) {
        const IOSRealityCheatDefinition *definition = ios_reality_definition((IOSRealityCheatId)index);
        if (!definition || definition->category != category) continue;
        IOSRealityCheatId cheatId = (IOSRealityCheatId)index;
        NSString *name = [NSString stringWithUTF8String:definition->name];
        if (!(definition->flags & IOS_REALITY_DEF_SUPPORTED)) {
            NSString *row = [NSString stringWithFormat:@"%@ — unavailable", name];
            NSString *reason = [NSString stringWithUTF8String:definition->unavailable_reason ?: "Not implemented"];
            [menu addAction:[UIAlertAction actionWithTitle:row style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
                (void)action;
                UIAlertController *details = [UIAlertController alertControllerWithTitle:name message:reason preferredStyle:UIAlertControllerStyleAlert];
                [details addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
                [self presentViewController:details animated:YES completion:nil];
                gfx_ios_diag_log("Reality Debugger unavailable feature opened id=%d name=%s reason=%s", (int)cheatId, definition->name, definition->unavailable_reason);
            }]];
        } else if (definition->flags & IOS_REALITY_DEF_ACTION) {
            [menu addAction:[UIAlertAction actionWithTitle:name style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
                (void)action;
                ios_reality_request_action(cheatId);
                gfx_ios_diag_log("Reality Debugger action queued id=%d name=%s", (int)cheatId, definition->name);
                [self appendStatus:[NSString stringWithFormat:@"%@ queued for the game thread", name]];
            }]];
        } else {
            BOOL enabled = ios_reality_global_is_enabled(cheatId);
            NSString *row;
            u8 parameter = ios_reality_global_parameter_index(cheatId);
            if (definition->parameter_count && definition->parameter_values) {
                if (parameter >= definition->parameter_count) parameter = definition->default_parameter;
                row = [NSString stringWithFormat:@"%@  [%@ · %.2gx]", name, enabled ? @"ON" : @"OFF", definition->parameter_values[parameter]];
            } else {
                row = [NSString stringWithFormat:@"%@  [%@]", name, enabled ? @"ON" : @"OFF"];
            }
            [menu addAction:[UIAlertAction actionWithTitle:row style:enabled ? UIAlertActionStyleDestructive : UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
                (void)action;
                ios_reality_request_toggle(cheatId);
                gfx_ios_diag_log("Reality Debugger toggle queued id=%d name=%s current=%d", (int)cheatId, definition->name, enabled);
                [self appendStatus:[NSString stringWithFormat:@"%@ %@ queued", name, enabled ? @"disable" : @"enable"]];
            }]];
            if (definition->parameter_count > 1 && definition->parameter_values) {
                u8 next = (parameter + 1) % definition->parameter_count;
                NSString *valueRow = [NSString stringWithFormat:@"%@ value → %.2gx", name, definition->parameter_values[next]];
                [menu addAction:[UIAlertAction actionWithTitle:valueRow style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
                    (void)action;
                    ios_reality_request_parameter_cycle(cheatId);
                    gfx_ios_diag_log("Reality Debugger parameter cycle queued id=%d name=%s nextIndex=%u nextValue=%.3f", (int)cheatId, definition->name, next, definition->parameter_values[next]);
                    [self appendStatus:[NSString stringWithFormat:@"%@ value change queued", name]];
                }]];
            }
        }
    }
    [menu addAction:[UIAlertAction actionWithTitle:@"Back" style:UIAlertActionStyleCancel handler:nil]];
    UIPopoverPresentationController *popover = menu.popoverPresentationController;
    popover.sourceView = self.realityButton;
    popover.sourceRect = self.realityButton.bounds;
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
    CGFloat settingsHeight = MIN(440.0, MAX(418.0, self.view.bounds.size.height - 12.0));
    self.graphicsPanel.frame = CGRectMake((self.view.bounds.size.width - settingsWidth) * 0.5,
        (self.view.bounds.size.height - settingsHeight) * 0.5, settingsWidth, settingsHeight);
    if (self.graphicsPanel) {
        CGFloat inner = settingsWidth - 28.0;
        self.presetControl.frame = CGRectMake(14, 38, inner, 31);
        self.scaleControl.frame = CGRectMake(14, 96, inner, 31);
        self.msaaControl.frame = CGRectMake(14, 152, inner, 31);
        self.anisotropyControl.frame = CGRectMake(14, 208, inner, 31);
        CGFloat gap = 4.0;
        CGFloat rowWidth = inner - gap * 4.0;
        CGFloat widths1[] = { rowWidth * 0.28, rowWidth * 0.15, rowWidth * 0.15, rowWidth * 0.20, rowWidth * 0.22 };
        UIButton *row1[] = { self.ssaoButton, self.postButton, self.fxaaButton, self.exposureButton, self.fogButton };
        CGFloat x = 14.0;
        for (NSUInteger i = 0; i < 5; ++i) {
            row1[i].frame = CGRectMake(x, 244, widths1[i], 30);
            x += widths1[i] + gap;
        }
        CGFloat widths2[] = { rowWidth * 0.18, rowWidth * 0.22, rowWidth * 0.30, rowWidth * 0.30 };
        UIButton *row2[] = { self.bloomButton, self.gradeButton, self.fogProfileButton, self.shadowButton };
        x = 14.0;
        for (NSUInteger i = 0; i < 4; ++i) {
            row2[i].frame = CGRectMake(x, 278, widths2[i], 30);
            x += widths2[i] + gap;
        }
        CGFloat widths3[] = { rowWidth * 0.28, rowWidth * 0.24, rowWidth * 0.20, rowWidth * 0.28 };
        UIButton *row3[] = { self.aoQualityButton, self.skyPresetButton, self.masterEffectsButton, self.godRaysButton };
        x = 14.0;
        for (NSUInteger i = 0; i < 4; ++i) {
            row3[i].frame = CGRectMake(x, 312, widths3[i], 30);
            x += widths3[i] + gap;
        }
        self.performanceLabel.frame = CGRectMake(14, 346, inner, 36);
        self.shaderPacksButton.frame = CGRectMake(14, 346, inner, 30);
        self.performanceLabel.frame = CGRectMake(14, 378, inner, 26);
    }
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
    self.graphicsPanel = [[[UIView alloc] initWithFrame:CGRectMake(0, 0, 520, 400)] autorelease];
    self.graphicsPanel.backgroundColor = [UIColor colorWithWhite:0.04 alpha:0.97];
    self.graphicsPanel.layer.cornerRadius = 12.0;
    self.graphicsPanel.layer.borderWidth = 1.0;
    self.graphicsPanel.layer.borderColor = [UIColor colorWithWhite:0.7 alpha:0.85].CGColor;
    self.graphicsPanel.hidden = YES;
    [self.view addSubview:self.graphicsPanel];

    UILabel *heading = [[[UILabel alloc] initWithFrame:CGRectMake(14, 8, 240, 24)] autorelease];
    heading.text = [NSString stringWithFormat:@"%@ Graphics", ios_app_display_name()];
    heading.textColor = UIColor.whiteColor;
    heading.font = [UIFont boldSystemFontOfSize:16.0];
    heading.adjustsFontSizeToFitWidth = YES;
    heading.minimumScaleFactor = 0.7;
    [self.graphicsPanel addSubview:heading];
    UIButton *close = [self buttonWithTitle:@"Close" action:@selector(toggleGraphicsSettings)];
    close.frame = CGRectMake(0, 5, 62, 32);
    close.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin;
    [self.graphicsPanel addSubview:close];
    self.graphicsPanel.autoresizingMask = UIViewAutoresizingFlexibleLeftMargin | UIViewAutoresizingFlexibleRightMargin |
        UIViewAutoresizingFlexibleTopMargin | UIViewAutoresizingFlexibleBottomMargin;

    self.presetControl = [self graphicsControl:@[@"Classic", @"Enhanced", @"Cinematic", @"Performance"] y:38];
    [self addGraphicsLabel:@"Render scale" y:78];
    self.scaleControl = [self graphicsControl:@[@"50%", @"75%", @"100%"] y:96];
    [self addGraphicsLabel:@"Anti-aliasing" y:134];
    self.msaaControl = [self graphicsControl:@[@"Off", @"2×", @"4×"] y:152];
    [self addGraphicsLabel:@"Anisotropic filtering" y:190];
    self.anisotropyControl = [self graphicsControl:@[@"Off", @"2×", @"4×", @"8×", @"16×"] y:208];

    self.ssaoButton = [self buttonWithTitle:@"SSAO: Off" action:@selector(toggleSSAO)];
    self.ssaoButton.frame = CGRectMake(14, 244, 140, 30);
    [self.graphicsPanel addSubview:self.ssaoButton];
    self.postButton = [self buttonWithTitle:@"Tone: Off" action:@selector(openToneSettings)];
    self.postButton.frame = CGRectMake(158, 244, 82, 30);
    [self.graphicsPanel addSubview:self.postButton];
    self.fxaaButton = [self buttonWithTitle:@"FXAA: Off" action:@selector(toggleFXAA)];
    self.fxaaButton.frame = CGRectMake(244, 244, 78, 30);
    [self.graphicsPanel addSubview:self.fxaaButton];
    self.exposureButton = [self buttonWithTitle:@"Exposure 0" action:@selector(stepExposure)];
    self.exposureButton.frame = CGRectMake(326, 244, 92, 30);
    [self.graphicsPanel addSubview:self.exposureButton];
    self.fogButton = [self buttonWithTitle:@"Fog: Off" action:@selector(toggleFog)];
    self.fogButton.frame = CGRectMake(422, 244, 84, 30);
    [self.graphicsPanel addSubview:self.fogButton];
    self.bloomButton = [self buttonWithTitle:@"Bloom: Off" action:@selector(toggleBloom)];
    self.bloomButton.frame = CGRectMake(14, 278, 92, 30);
    [self.graphicsPanel addSubview:self.bloomButton];
    self.gradeButton = [self buttonWithTitle:@"Color: Neutral" action:@selector(openColorSettings)];
    self.gradeButton.frame = CGRectMake(112, 278, 120, 30);
    [self.graphicsPanel addSubview:self.gradeButton];
    self.fogProfileButton = [self buttonWithTitle:@"Fog: Standard" action:@selector(openFogSettings)];
    self.fogProfileButton.frame = CGRectMake(238, 278, 154, 30);
    [self.graphicsPanel addSubview:self.fogProfileButton];
    self.shadowButton = [self buttonWithTitle:@"Shadows: Off" action:@selector(openShadowSettings)];
    self.shadowButton.frame = CGRectMake(398, 278, 108, 30);
    [self.graphicsPanel addSubview:self.shadowButton];
    self.aoQualityButton = [self buttonWithTitle:@"AO quality: Standard" action:@selector(openAOSettings)];
    self.aoQualityButton.frame = CGRectMake(14, 312, 144, 30);
    [self.graphicsPanel addSubview:self.aoQualityButton];
    self.skyPresetButton = [self buttonWithTitle:@"Sky: Original" action:@selector(cycleSkyPreset)];
    self.skyPresetButton.frame = CGRectMake(166, 312, 134, 30);
    [self.graphicsPanel addSubview:self.skyPresetButton];
    self.masterEffectsButton = [self buttonWithTitle:@"Effects: On" action:@selector(toggleMasterEffects)];
    self.masterEffectsButton.frame = CGRectMake(304, 312, 100, 30);
    [self.graphicsPanel addSubview:self.masterEffectsButton];
    self.godRaysButton = [self buttonWithTitle:@"Rays: Off" action:@selector(openGodRaySettings)];
    self.godRaysButton.frame = CGRectMake(408, 312, 98, 30);
    [self.graphicsPanel addSubview:self.godRaysButton];

    self.performanceLabel = [[[UILabel alloc] initWithFrame:CGRectZero] autorelease];
    self.performanceLabel.textColor = [UIColor colorWithWhite:0.94 alpha:1.0];
    self.performanceLabel.font = [UIFont monospacedSystemFontOfSize:10.0 weight:UIFontWeightRegular];
    self.performanceLabel.font = [UIFont monospacedSystemFontOfSize:8.0 weight:UIFontWeightRegular];
    self.performanceLabel.numberOfLines = 5;
    self.performanceLabel.frame = CGRectMake(14, 378, 492, 26);
    self.performanceLabel.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [self.graphicsPanel addSubview:self.performanceLabel];

#if TARGET_OS_IOS && defined(RAPI_METAL)
    self.shaderPacksButton = [self buttonWithTitle:@"Shader Pack: Classic64" action:@selector(openShaderPacks)];
    self.shaderPacksButton.frame = CGRectMake(14, 346, 492, 30);
    self.shaderPacksButton.autoresizingMask = UIViewAutoresizingFlexibleWidth;
    [self.graphicsPanel addSubview:self.shaderPacksButton];
#endif

    UIButton *reset = [self buttonWithTitle:@"Reset defaults" action:@selector(resetGraphicsSettings)];
    reset.frame = CGRectMake(14, 408, 145, 30);
    reset.autoresizingMask = UIViewAutoresizingFlexibleTopMargin;
    [self.graphicsPanel addSubview:reset];
    UIButton *done = [self buttonWithTitle:@"Done" action:@selector(toggleGraphicsSettings)];
    done.frame = CGRectMake(0, 408, 80, 30);
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
    if (!status.length) return;
    [self.recentStatus addObject:status];
    while (self.recentStatus.count > 5) [self.recentStatus removeObjectAtIndex:0];
    self.statusLabel.text = [self.recentStatus componentsJoinedByString:@"\n"];
}

- (void)showPortalStatus:(NSString *)status {
    if (!status.length) return;
    self.portalStatusGeneration++;
    uint64_t generation = self.portalStatusGeneration;
    self.portalStatusLabel.text = status;
    self.portalStatusLabel.hidden = NO;
    self.portalStatusLabel.alpha = 1.0;
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
    [self appendStatus:status];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(3.0 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(), ^{
        if (self.portalStatusGeneration == generation) {
            self.portalStatusLabel.hidden = YES;
        }
    });
}

- (void)collapsePanel {
    self.collapsed = YES;
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
}

- (void)togglePanel {
    if (self.graphicsSettingsVisible) [self toggleGraphicsSettings];
    self.collapsed = !self.collapsed;
    [self.view setNeedsLayout];
    [self.view layoutIfNeeded];
}

- (void)toggleGraphicsSettings {
    self.graphicsSettingsVisible = !self.graphicsSettingsVisible;
    self.graphicsPanel.hidden = !self.graphicsSettingsVisible;
    ((SM64DiagnosticsPassthroughView *)self.view).hitRect = self.graphicsSettingsVisible ?
        self.view.bounds : (self.probeView ? self.view.bounds : CGRectInset(self.panel.frame, -2, -2));
    if (self.graphicsSettingsVisible) {
        [self refreshGraphicsSettings];
        [self refreshGraphicsPerformance];
        self.performanceTimer = [NSTimer scheduledTimerWithTimeInterval:0.5 target:self
            selector:@selector(refreshGraphicsPerformance) userInfo:nil repeats:YES];
        gfx_ios_diag_log("Native Metal graphics settings panel opened");
    } else {
        [self.performanceTimer invalidate];
        self.performanceTimer = nil;
        gfx_ios_diag_log("Native Metal graphics settings panel closed");
    }
}

- (void)refreshGraphicsSettings {
#if defined(RAPI_METAL)
    struct GfxMetalSettings settings = gfx_metal_get_settings();
    struct GfxMetalCapabilities capabilities = gfx_metal_get_capabilities();
    self.presetControl.selectedSegmentIndex = MIN(settings.preset, GFX_METAL_PRESET_PERFORMANCE);
    self.scaleControl.selectedSegmentIndex = settings.render_scale_percent == 50 ? 0 :
        (settings.render_scale_percent == 75 ? 1 : 2);
    self.msaaControl.selectedSegmentIndex = settings.sample_count >= 4 ? 2 :
        (settings.sample_count >= 2 ? 1 : 0);
    static const uint32_t levels[] = { 1, 2, 4, 8, 16 };
    self.anisotropyControl.selectedSegmentIndex = 0;
    for (NSUInteger i = 0; i < sizeof(levels) / sizeof(levels[0]); ++i)
        if (levels[i] == settings.anisotropy) self.anisotropyControl.selectedSegmentIndex = (NSInteger)i;
    self.msaaControl.enabled = YES;
    [self.msaaControl setEnabled:capabilities.sample_count_2 forSegmentAtIndex:1];
    [self.msaaControl setEnabled:capabilities.sample_count_4 forSegmentAtIndex:2];
    BOOL singleSampleEffect = gfx_metal_ssao_enabled() || gfx_metal_shadows_enabled() ||
        gfx_metal_god_rays_enabled() || gfx_metal_sun_intensity() != 0u;
    self.msaaControl.enabled = !singleSampleEffect;
    for (NSUInteger i = 1; i < sizeof(levels) / sizeof(levels[0]); ++i)
        [self.anisotropyControl setEnabled:levels[i] <= capabilities.max_anisotropy forSegmentAtIndex:i];
    static const char *const ssao_modes[] = { "SSAO: Off", "SSAO: On", "SSAO: AO View", "SSAO: Depth View" };
    uint32_t ssao_mode = gfx_metal_ssao_debug_mode();
    [self.ssaoButton setTitle:[NSString stringWithUTF8String:ssao_modes[MIN(ssao_mode, 3u)]]
                     forState:UIControlStateNormal];
    [self.postButton setTitle:gfx_metal_post_enabled() ?
        [NSString stringWithFormat:@"Tone: %@", [NSString stringWithUTF8String:gfx_metal_tone_mapping_name()]] : @"Tone: Off"
                     forState:UIControlStateNormal];
    [self.fxaaButton setTitle:gfx_metal_fxaa_enabled() ? @"FXAA: On" : @"FXAA: Off"
                     forState:UIControlStateNormal];
    [self.exposureButton setTitle:[NSString stringWithFormat:@"EV %+.2f", gfx_metal_exposure_stops()]
                          forState:UIControlStateNormal];
    [self.fogButton setTitle:gfx_metal_fog_enabled() ? @"Fog: On" : @"Fog: Off"
                    forState:UIControlStateNormal];
    static NSString *const bloom_names[] = { @"Low", @"Standard", @"High" };
    [self.bloomButton setTitle:[NSString stringWithFormat:@"Bloom: %@ %@",
        gfx_metal_bloom_enabled() ? @"On" : @"Off",
        bloom_names[MIN(gfx_metal_bloom_profile(), 2u)]] forState:UIControlStateNormal];
    static NSString *const grade_names[] = { @"Neutral", @"Warm", @"Vivid", @"Cinematic" };
    [self.gradeButton setTitle:[NSString stringWithFormat:@"Color: %@", grade_names[MIN(gfx_metal_color_grade(), 3u)]]
                       forState:UIControlStateNormal];
    static NSString *const fog_names[] = { @"Standard", @"Light", @"Dense", @"Extended" };
    [self.fogProfileButton setTitle:[NSString stringWithFormat:@"Fog range: %@", fog_names[MIN(gfx_metal_fog_profile(), 3u)]]
                             forState:UIControlStateNormal];
    static NSString *const ao_quality_names[] = { @"Low", @"Standard", @"High" };
    [self.aoQualityButton setTitle:[NSString stringWithFormat:@"AO quality: %@", ao_quality_names[MIN(gfx_metal_ssao_quality(), 2u)]]
                           forState:UIControlStateNormal];
    static NSString *const sky_names[] = { @"Original", @"Day", @"Sunset" };
    [self.skyPresetButton setTitle:[NSString stringWithFormat:@"Sky: %@", sky_names[MIN(gfx_metal_sky_preset(), 2u)]]
                           forState:UIControlStateNormal];
    [self.masterEffectsButton setTitle:gfx_metal_effects_enabled() ? @"Effects: On" : @"Effects: Off"
                               forState:UIControlStateNormal];
    [self.shadowButton setTitle:[NSString stringWithFormat:@"Sun: %@", [NSString stringWithUTF8String:gfx_metal_sun_preset_name()]]
                       forState:UIControlStateNormal];
    static NSString *const ray_quality_names[] = { @"Low", @"Standard", @"High" };
    [self.godRaysButton setTitle:[NSString stringWithFormat:@"Rays: %@ %@", gfx_metal_god_rays_enabled() ? @"On" : @"Off",
                                 ray_quality_names[MIN(gfx_metal_god_ray_quality(), 2u)]]
                        forState:UIControlStateNormal];
#endif
}

- (void)openShadowSettings {
#if defined(RAPI_METAL)
    static NSString *const intensity_names[] = { @"Off", @"Low", @"Medium", @"High" };
    NSString *message = [NSString stringWithFormat:@"Sun direction: %@. Diffuse light intensity: %@. Directional shadows render captured geometry into a depth texture, bounded to 8,192 triangles per frame. Map resolution: %u.", [NSString stringWithUTF8String:gfx_metal_sun_preset_name()], intensity_names[MIN(gfx_metal_sun_intensity(), 3u)], gfx_metal_shadow_resolution()];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Sun and Directional Shadows"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    BOOL enabled = gfx_metal_shadows_enabled();
    [alert addAction:[UIAlertAction actionWithTitle:enabled ? @"Turn Off" : @"Turn On"
        style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            gfx_metal_set_shadows_enabled(!enabled);
            [self refreshGraphicsSettings];
            [self refreshGraphicsPerformance];
            [self appendStatus:gfx_metal_shadows_enabled() ? @"Directional shadow map enabled" : @"Directional shadow map disabled"];
        }]];
    [alert addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"Resolution: %u → next", gfx_metal_shadow_resolution()]
        style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            gfx_metal_cycle_shadow_resolution();
            [self refreshGraphicsSettings];
        }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Change sun direction" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_cycle_sun_preset(); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cycle sunlight intensity" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_cycle_sun_intensity(); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
#endif
}

- (void)openGodRaySettings {
#if defined(RAPI_METAL)
    NSString *message = [NSString stringWithFormat:@"Depth-occluded radial light shafts are a quarter-resolution screen-space approximation, not volumetric scattering. Intensity %.2f×.", gfx_metal_god_ray_intensity()];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Screen-Space Light Shafts"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    BOOL enabled = gfx_metal_god_rays_enabled();
    [alert addAction:[UIAlertAction actionWithTitle:enabled ? @"Turn Off" : @"Turn On"
        style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            gfx_metal_set_god_rays_enabled(!enabled);
            [self refreshGraphicsSettings];
            [self refreshGraphicsPerformance];
            [self appendStatus:gfx_metal_god_rays_enabled() ? @"Screen-space light shafts enabled" : @"Screen-space light shafts disabled"];
        }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cycle quality" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_cycle_god_ray_quality(); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Reduce intensity" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_adjust_god_ray_intensity(-1); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Increase intensity" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_adjust_god_ray_intensity(1); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Reset intensity" style:UIAlertActionStyleDestructive
        handler:^(UIAlertAction *action) { gfx_metal_reset_god_ray_intensity(); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
#endif
}

- (void)togglePostprocessing {
#if defined(RAPI_METAL)
    [self openToneSettings];
#endif
}

- (void)openToneSettings {
#if defined(RAPI_METAL)
    BOOL enabled = gfx_metal_post_enabled();
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Tone Mapping"
        message:@"Tone mapping runs after bloom and color grading; the HUD is composited separately."
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:enabled ? @"Disable tone mapping" : @"Enable tone mapping"
        style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            gfx_metal_set_post_enabled(!enabled);
            [self refreshGraphicsSettings];
        }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Use ACES-inspired curve" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_set_tone_mapping_mode(0u); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Use Reinhard curve" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_set_tone_mapping_mode(1u); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
#endif
}

- (void)toggleFXAA {
#if defined(RAPI_METAL)
    gfx_metal_set_fxaa_enabled(!gfx_metal_fxaa_enabled());
    [self refreshGraphicsSettings];
#endif
}

- (void)stepExposure {
#if defined(RAPI_METAL)
    float exposure = gfx_metal_exposure_stops();
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Exposure"
        message:[NSString stringWithFormat:@"Manual exposure: %+.2f EV (range −2 to +2)", exposure]
        preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"−0.25 EV" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_adjust_exposure(-1); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"+0.25 EV" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_adjust_exposure(1); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Reset" style:UIAlertActionStyleDestructive
        handler:^(UIAlertAction *action) { gfx_metal_reset_exposure(); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
#endif
}

- (void)toggleFog {
#if defined(RAPI_METAL)
    gfx_metal_set_fog_enabled(!gfx_metal_fog_enabled());
    [self refreshGraphicsSettings];
#endif
}

- (void)toggleBloom {
#if defined(RAPI_METAL)
    static NSString *const names[] = { @"Low", @"Standard", @"High" };
    struct GfxMetalBloomSettings current = gfx_metal_bloom_settings(gfx_metal_bloom_profile());
    NSString *message = [NSString stringWithFormat:@"Quarter-resolution bloom. Threshold %.2f, intensity %.2f.",
        current.threshold, current.intensity];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Bloom Settings"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    BOOL enabled = gfx_metal_bloom_enabled();
    [alert addAction:[UIAlertAction actionWithTitle:enabled ? @"Turn Off" : @"Turn On"
        style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
            gfx_metal_set_bloom_enabled(!enabled);
            [self refreshGraphicsSettings];
        }]];
    for (uint32_t profile = 0; profile < 3u; ++profile) {
        uint32_t selected = profile;
        [alert addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"%@ intensity", names[profile]]
            style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
                gfx_metal_set_bloom_profile(selected);
                [self refreshGraphicsSettings];
            }]];
    }
    [alert addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
#endif
}

- (void)cycleColorGrade {
#if defined(RAPI_METAL)
    gfx_metal_cycle_color_grade();
    [self refreshGraphicsSettings];
#endif
}

- (void)openColorSettings {
#if defined(RAPI_METAL)
    struct GfxMetalColorAdjustments value = gfx_metal_get_color_adjustments();
    NSString *message = [NSString stringWithFormat:
        @"Brightness %+.2f  Contrast %.2f  Saturation %.2f  Temperature %+.2f\nPreset: %@",
        value.brightness, value.contrast, value.saturation, value.temperature,
        @[ @"Neutral", @"Warm", @"Vivid", @"Cinematic" ][MIN(gfx_metal_color_grade(), 3u)]];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Color Grading"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    [alert addAction:[UIAlertAction actionWithTitle:@"Cycle grade preset" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_cycle_color_grade(); [self refreshGraphicsSettings]; }]];
    static NSString *const labels[] = { @"Brightness", @"Contrast", @"Saturation", @"Temperature" };
    for (uint32_t parameter = 0; parameter < 4u; ++parameter) {
        for (int direction = -1; direction <= 1; direction += 2) {
            uint32_t selected_parameter = parameter;
            int selected_direction = direction;
            [alert addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"%@ %@",
                labels[parameter], direction < 0 ? @"−" : @"+"] style:UIAlertActionStyleDefault
                handler:^(UIAlertAction *action) {
                    gfx_metal_adjust_color(selected_parameter, selected_direction);
                    [self refreshGraphicsSettings];
                }]];
        }
    }
    [alert addAction:[UIAlertAction actionWithTitle:@"Reset adjustments" style:UIAlertActionStyleDestructive
        handler:^(UIAlertAction *action) { gfx_metal_reset_color_adjustments(); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
#endif
}

- (void)cycleFogProfile {
#if defined(RAPI_METAL)
    gfx_metal_cycle_fog_profile();
    [self refreshGraphicsSettings];
#endif
}

- (void)openFogSettings {
#if defined(RAPI_METAL)
    struct GfxMetalFogAdjustments value = gfx_metal_get_fog_adjustments();
    static NSString *const names[] = { @"Standard", @"Light", @"Dense", @"Extended" };
    NSString *message = [NSString stringWithFormat:@"%@ profile · density ×%.2f · start distance ×%.2f",
        names[MIN(gfx_metal_fog_profile(), 3u)], value.density_scale, value.start_scale];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Distance Fog"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    for (uint32_t profile = 0; profile < 4u; ++profile) {
        uint32_t selected = profile;
        [alert addAction:[UIAlertAction actionWithTitle:names[profile] style:UIAlertActionStyleDefault
            handler:^(UIAlertAction *action) { gfx_metal_set_fog_profile(selected); [self refreshGraphicsSettings]; }]];
    }
    [alert addAction:[UIAlertAction actionWithTitle:@"Reduce density" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_adjust_fog(GFX_METAL_FOG_DENSITY, -1); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Increase density" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_adjust_fog(GFX_METAL_FOG_DENSITY, 1); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Nearer start" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_adjust_fog(GFX_METAL_FOG_START, -1); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Farther start" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { gfx_metal_adjust_fog(GFX_METAL_FOG_START, 1); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Reset fog tuning" style:UIAlertActionStyleDestructive
        handler:^(UIAlertAction *action) { gfx_metal_reset_fog_adjustments(); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
#endif
}

- (void)cycleAOQuality {
#if defined(RAPI_METAL)
    gfx_metal_cycle_ssao_quality();
    [self refreshGraphicsSettings];
#endif
}

- (void)openAOSettings {
#if defined(RAPI_METAL)
    struct GfxMetalAOAdjustments value = gfx_metal_get_ao_adjustments();
    NSString *message = [NSString stringWithFormat:
        @"Quality: %@  Radius ×%.2f  Bias %+.3f  Intensity ×%.2f",
        @[ @"Low", @"Standard", @"High" ][MIN(gfx_metal_ssao_quality(), 2u)],
        value.radius_scale, value.bias_offset, value.intensity_scale];
    UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Ambient Occlusion"
        message:message preferredStyle:UIAlertControllerStyleAlert];
    static NSString *const quality_names[] = { @"Low quality", @"Standard quality", @"High quality" };
    for (uint32_t quality = 0; quality < 3u; ++quality) {
        uint32_t selected = quality;
        [alert addAction:[UIAlertAction actionWithTitle:quality_names[quality] style:UIAlertActionStyleDefault
            handler:^(UIAlertAction *action) { gfx_metal_set_ssao_quality(selected); [self refreshGraphicsSettings]; }]];
    }
    static NSString *const labels[] = { @"Radius", @"Bias", @"Intensity" };
    for (uint32_t parameter = 0; parameter < 3u; ++parameter) {
        for (int direction = -1; direction <= 1; direction += 2) {
            uint32_t selected_parameter = parameter;
            int selected_direction = direction;
            [alert addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"%@ %@",
                labels[parameter], direction < 0 ? @"−" : @"+"] style:UIAlertActionStyleDefault
                handler:^(UIAlertAction *action) {
                    gfx_metal_adjust_ao(selected_parameter, selected_direction);
                    [self refreshGraphicsSettings];
                }]];
        }
    }
    [alert addAction:[UIAlertAction actionWithTitle:@"Reset AO tuning" style:UIAlertActionStyleDestructive
        handler:^(UIAlertAction *action) { gfx_metal_reset_ao_adjustments(); [self refreshGraphicsSettings]; }]];
    [alert addAction:[UIAlertAction actionWithTitle:@"Done" style:UIAlertActionStyleCancel handler:nil]];
    [self presentViewController:alert animated:YES completion:nil];
#endif
}

- (void)cycleSkyPreset {
#if defined(RAPI_METAL)
    gfx_metal_cycle_sky_preset();
    [self refreshGraphicsSettings];
#endif
}

- (void)toggleMasterEffects {
#if defined(RAPI_METAL)
    gfx_metal_set_effects_enabled(!gfx_metal_effects_enabled());
    [self refreshGraphicsSettings];
    [self refreshGraphicsPerformance];
#endif
}

- (void)toggleSSAO {
#if defined(RAPI_METAL)
    gfx_metal_cycle_ssao_mode();
    [self refreshGraphicsSettings];
    [self refreshGraphicsPerformance];
    static const char *const status[] = { "SSAO disabled", "SSAO enabled (single-sample offscreen path)",
                                          "SSAO output view", "Scene depth view" };
    [self appendStatus:[NSString stringWithUTF8String:status[MIN(gfx_metal_ssao_debug_mode(), 3u)]]];
#endif
}

- (void)applyGraphicsSettingsSelection:(UISegmentedControl *)sender {
#if defined(RAPI_METAL)
    struct GfxMetalSettings settings;
    uint32_t preset = (uint32_t)MAX(0, MIN(GFX_METAL_PRESET_PERFORMANCE, self.presetControl.selectedSegmentIndex));
    if (sender == self.presetControl) {
        struct GfxMetalCapabilities capabilities = gfx_metal_get_capabilities();
        if (preset == GFX_METAL_PRESET_CLASSIC) {
            settings = gfx_metal_classic_settings();
            gfx_metal_set_ssao_enabled(false);
            gfx_metal_reset_ao_adjustments();
            gfx_metal_set_post_enabled(false);
            gfx_metal_reset_exposure();
            gfx_metal_set_tone_mapping_mode(0u);
            gfx_metal_set_fxaa_enabled(false);
            gfx_metal_set_bloom_enabled(false);
            gfx_metal_set_bloom_profile(1u);
            gfx_metal_set_fog_enabled(false);
            gfx_metal_set_fog_profile(0u);
            gfx_metal_reset_fog_adjustments();
            gfx_metal_set_color_grade(0u);
            gfx_metal_reset_color_adjustments();
            gfx_metal_set_sky_preset(0u);
            gfx_metal_set_shadows_enabled(false);
            gfx_metal_set_god_rays_enabled(false);
            gfx_metal_reset_god_ray_intensity();
            gfx_metal_set_sun_preset(0u);
            gfx_metal_set_sun_intensity(0u);
            gfx_metal_set_effects_enabled(false);
        } else {
            if (preset == GFX_METAL_PRESET_CINEMATIC) settings = gfx_metal_cinematic_settings();
            else if (preset == GFX_METAL_PRESET_PERFORMANCE) settings = gfx_metal_performance_settings();
            else settings = gfx_metal_enhanced_settings();
            if (preset == GFX_METAL_PRESET_PERFORMANCE) {
                self.scaleControl.selectedSegmentIndex = 1;
                self.msaaControl.selectedSegmentIndex = 0;
                self.anisotropyControl.selectedSegmentIndex = capabilities.max_anisotropy >= 2 ? 1 : 0;
            } else {
            self.scaleControl.selectedSegmentIndex = 2;
            self.msaaControl.selectedSegmentIndex = capabilities.sample_count_4 ? 2 :
                (capabilities.sample_count_2 ? 1 : 0);
            self.anisotropyControl.selectedSegmentIndex = capabilities.max_anisotropy >= 16 && preset == GFX_METAL_PRESET_CINEMATIC ? 4 :
                (capabilities.max_anisotropy >= 8 ? 3 :
                (capabilities.max_anisotropy >= 4 ? 2 :
                 (capabilities.max_anisotropy >= 2 ? 1 : 0)));
            }
            gfx_metal_set_ssao_enabled(true);
            gfx_metal_set_ssao_quality(preset == GFX_METAL_PRESET_PERFORMANCE ? 0u :
                                       (preset == GFX_METAL_PRESET_CINEMATIC ? 2u : 1u));
            gfx_metal_reset_ao_adjustments();
            gfx_metal_set_post_enabled(true);
            gfx_metal_reset_exposure();
            gfx_metal_set_tone_mapping_mode(preset == GFX_METAL_PRESET_PERFORMANCE ? 1u : 0u);
            gfx_metal_set_fxaa_enabled(preset != GFX_METAL_PRESET_PERFORMANCE);
            gfx_metal_set_bloom_enabled(preset == GFX_METAL_PRESET_CINEMATIC);
            gfx_metal_set_bloom_profile(preset == GFX_METAL_PRESET_PERFORMANCE ? 0u :
                                        (preset == GFX_METAL_PRESET_CINEMATIC ? 2u : 1u));
            gfx_metal_set_fog_enabled(preset == GFX_METAL_PRESET_CINEMATIC);
            gfx_metal_set_fog_profile(preset == GFX_METAL_PRESET_CINEMATIC ? 2u : 0u);
            gfx_metal_reset_fog_adjustments();
            gfx_metal_set_color_grade(preset == GFX_METAL_PRESET_CINEMATIC ? 3u : 0u);
            gfx_metal_set_sky_preset(preset == GFX_METAL_PRESET_CINEMATIC ? 2u : 0u);
            gfx_metal_set_shadow_resolution(preset == GFX_METAL_PRESET_CINEMATIC ? 1024u : 512u);
            gfx_metal_set_sun_preset(preset == GFX_METAL_PRESET_CINEMATIC ? 3u : 0u);
            gfx_metal_set_sun_intensity(preset == GFX_METAL_PRESET_ENHANCED ? 1u :
                                        (preset == GFX_METAL_PRESET_CINEMATIC ? 2u : 0u));
            gfx_metal_set_shadows_enabled(preset == GFX_METAL_PRESET_ENHANCED ||
                                          preset == GFX_METAL_PRESET_CINEMATIC);
            gfx_metal_set_god_rays_enabled(preset == GFX_METAL_PRESET_CINEMATIC);
            gfx_metal_set_god_ray_quality(preset == GFX_METAL_PRESET_CINEMATIC ? 2u : 1u);
            gfx_metal_reset_god_ray_intensity();
            gfx_metal_set_effects_enabled(true);
        }
    } else {
        struct GfxMetalSettings current = gfx_metal_get_settings();
        settings = current;
        settings.preset = preset;
        settings.render_scale_percent = self.scaleControl.selectedSegmentIndex == 0 ? 50u :
            (self.scaleControl.selectedSegmentIndex == 1 ? 75u : 100u);
        settings.sample_count = self.msaaControl.selectedSegmentIndex == 0 ? 1u :
            (self.msaaControl.selectedSegmentIndex == 1 ? 2u : 4u);
        static const uint32_t levels[] = { 1, 2, 4, 8, 16 };
        NSInteger aniso_index = MAX(0, self.anisotropyControl.selectedSegmentIndex);
        settings.anisotropy = levels[MIN((NSUInteger)aniso_index, (sizeof(levels) / sizeof(levels[0])) - 1)];
    }
    gfx_metal_apply_settings(settings);
    [self refreshGraphicsSettings];
    [self refreshGraphicsPerformance];
    [self appendStatus:[NSString stringWithFormat:@"Graphics: %s", gfx_metal_preset_name(settings.preset)]];
#endif
}

- (void)refreshGraphicsPerformance {
#if defined(RAPI_METAL)
    struct GfxMetalPerformanceStats stats = { 0 };
    gfx_metal_get_performance_stats(&stats);
    NSString *gpu = stats.gpu_timing_available ?
        [NSString stringWithFormat:@"GPU %.2f ms", stats.gpu_frame_ms] : @"GPU timing unavailable";
    NSString *preset = [NSString stringWithUTF8String:gfx_metal_preset_name(stats.preset)];
    self.performanceLabel.text = [NSString stringWithFormat:
        @"%@%@  |  CPU %.2f ms  |  %.1f FPS est.\n%@\nDraws %llu  |  Triangles %llu\nRender %ux%u (%u%%)  |  MSAA %ux  |  AF %u×\nSSAO %@ %llu px | Bloom %@ %llu px | Shadows %@ %llu tris | Rays %@ %llu px | targets ~%.1f MiB",
        preset, gfx_metal_offscreen_fallback_active() ? @" • enhanced effects suspended (direct path)" : @"",
        stats.cpu_frame_ms, stats.estimated_fps, gpu,
        (unsigned long long)stats.draw_calls, (unsigned long long)stats.triangles,
        stats.render_width, stats.render_height, stats.render_scale_percent,
        stats.sample_count, stats.anisotropy,
        gfx_metal_ssao_enabled() ? @"on" : @"off",
        (unsigned long long)stats.ssao_pixels,
        gfx_metal_bloom_enabled() ? @"on" : @"off",
        (unsigned long long)stats.bloom_pixels,
        gfx_metal_shadows_enabled() ? @"on" : @"off",
        (unsigned long long)stats.shadow_triangles,
        gfx_metal_god_rays_enabled() ? @"on" : @"off",
        (unsigned long long)stats.god_ray_pixels,
        (double)stats.estimated_target_bytes / (1024.0 * 1024.0)];
#if TARGET_OS_IOS
    NSString *activePack = [NSString stringWithUTF8String:gfx_metal_active_shader64_pack()];
    [self.shaderPacksButton setTitle:[NSString stringWithFormat:@"Shader Pack: %@", activePack]
                            forState:UIControlStateNormal];
#endif
#endif
}

- (void)resetGraphicsSettings {
#if defined(RAPI_METAL)
    gfx_metal_set_ssao_enabled(false);
    gfx_metal_set_post_enabled(false);
    gfx_metal_reset_exposure();
    gfx_metal_set_tone_mapping_mode(0u);
    gfx_metal_set_fxaa_enabled(false);
    gfx_metal_set_fog_enabled(false);
    gfx_metal_set_bloom_enabled(false);
    gfx_metal_set_bloom_profile(1u);
    gfx_metal_set_color_grade(0u);
    gfx_metal_reset_color_adjustments();
    gfx_metal_set_fog_profile(0u);
    gfx_metal_reset_fog_adjustments();
    gfx_metal_set_ssao_quality(1u);
    gfx_metal_reset_ao_adjustments();
    gfx_metal_set_sky_preset(0u);
    gfx_metal_set_shadows_enabled(false);
    gfx_metal_set_god_rays_enabled(false);
    gfx_metal_reset_god_ray_intensity();
    gfx_metal_set_shadow_resolution(512u);
    gfx_metal_set_sun_preset(0u);
    gfx_metal_set_sun_intensity(0u);
    gfx_metal_set_effects_enabled(false);
    gfx_metal_apply_settings(gfx_metal_classic_settings());
#if TARGET_OS_IOS
    gfx_metal_use_classic_shader();
#endif
    [self refreshGraphicsSettings];
    [self refreshGraphicsPerformance];
    gfx_ios_diag_log("Metal graphics settings reset to Classic defaults");
    [self appendStatus:@"Graphics reset to Classic defaults"];
#endif
}

#if TARGET_OS_IOS && defined(RAPI_METAL)
- (NSString *)shaderPackRoot {
    NSArray<NSString *> *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if (!paths.count) return nil;
    NSString *root = [paths[0] stringByAppendingPathComponent:@"shaderpacks"];
    [[NSFileManager defaultManager] createDirectoryAtPath:root withIntermediateDirectories:YES attributes:nil error:nil];
    return root;
}

- (void)openShaderPacks {
    NSMutableArray<NSString *> *packIDs = [NSMutableArray array];
    NSString *root = [self shaderPackRoot];
    NSArray *children = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:root error:nil];
        for (NSString *child in children) {
        BOOL isDirectory = NO;
        NSString *folder = [root stringByAppendingPathComponent:child];
        if (![[NSFileManager defaultManager] fileExistsAtPath:folder isDirectory:&isDirectory] || !isDirectory) continue;
        [packIDs addObject:child];
    }
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:@"SHADER64"
        message:@"API 1 packs replace the final scene composite; API 2 packs run a bounded Metal graph with fullscreen and compute passes. Native geometry/material rendering and HUD remain in place. API 1 supports source MSL or compatible iOS metallib; API 2 currently supports source MSL passes. Metal shaders are trusted code and are not sandboxed from GPU execution."
        preferredStyle:UIAlertControllerStyleActionSheet];
    [menu addAction:[UIAlertAction actionWithTitle:@"Classic64 (restore original renderer)" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { (void)action; gfx_metal_use_classic_shader(); [self refreshGraphicsSettings]; [self appendStatus:@"Classic64 renderer restored"]; }]];
    for (NSString *packID in packIDs) {
        NSDictionary *manifest = [self shaderManifestForID:packID root:root error:nil];
        if (!manifest) continue;
        NSString *name = [manifest[@"name"] isKindOfClass:[NSString class]] ? manifest[@"name"] : packID;
        [menu addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"About %@", name]
            style:UIAlertActionStyleDefault handler:^(UIAlertAction *action) {
                (void)action;
                NSString *details = [manifest[@"shaderApiVersion"] integerValue] == 2 ?
                    [NSString stringWithFormat:@"%@ %@\nBy %@\nShader API 2\nRender graph: %lu fullscreen passes",
                        manifest[@"name"], manifest[@"version"], manifest[@"author"], (unsigned long)[manifest[@"passes"] count]] :
                    [NSString stringWithFormat:@"%@ %@\nBy %@\nShader API %@\nStage %@\nEntry %@",
                        manifest[@"name"], manifest[@"version"], manifest[@"author"],
                        manifest[@"shaderApiVersion"], manifest[@"stage"], manifest[@"entryPoint"]];
                UIAlertController *about = [UIAlertController alertControllerWithTitle:@"Shader Pack Details"
                    message:details preferredStyle:UIAlertControllerStyleAlert];
                [about addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
                [self presentViewController:about animated:YES completion:nil];
            }]];
        [menu addAction:[UIAlertAction actionWithTitle:name style:UIAlertActionStyleDefault
            handler:^(UIAlertAction *action) { (void)action; [self activateShaderPack:packID]; }]];
        if ([manifest[@"parameters"] isKindOfClass:[NSArray class]] && [manifest[@"parameters"] count]) {
            [menu addAction:[UIAlertAction actionWithTitle:[NSString stringWithFormat:@"Configure %@…", name] style:UIAlertActionStyleDefault
                handler:^(UIAlertAction *action) { (void)action; [self activateShaderPack:packID]; if ([[NSString stringWithUTF8String:gfx_metal_active_shader64_pack()] isEqualToString:packID]) [self configureShaderPack:packID]; }]];
        }
    }
    [menu addAction:[UIAlertAction actionWithTitle:@"Import ZIP Pack…" style:UIAlertActionStyleDefault
        handler:^(UIAlertAction *action) { (void)action; [self importShaderPack]; }]];
    if (packIDs.count) [menu addAction:[UIAlertAction actionWithTitle:@"Delete an installed pack…" style:UIAlertActionStyleDestructive
        handler:^(UIAlertAction *action) { (void)action; [self deleteShaderPack]; }]];
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    menu.popoverPresentationController.sourceView = self.shaderPacksButton;
    menu.popoverPresentationController.sourceRect = self.shaderPacksButton.bounds;
    [self presentViewController:menu animated:YES completion:nil];
}

- (NSDictionary *)shaderManifestForID:(NSString *)packID root:(NSString *)root error:(NSString **)errorOut {
    NSString *path = [[root stringByAppendingPathComponent:packID] stringByAppendingPathComponent:@"manifest.json"];
    NSData *data = [NSData dataWithContentsOfFile:path];
    NSError *jsonError = nil;
    id value = data ? [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingFragmentsAllowed error:&jsonError] : nil;
    if (![value isKindOfClass:[NSDictionary class]]) {
        if (errorOut) *errorOut = jsonError.localizedDescription ?: @"manifest.json is missing";
        return nil;
    }
    NSDictionary *manifest = value;
    NSString *identifier = manifest[@"id"], *source = manifest[@"source"], *stage = manifest[@"stage"], *entry = manifest[@"entryPoint"];
    NSNumber *format = manifest[@"formatVersion"], *api = manifest[@"shaderApiVersion"];
    NSRegularExpression *regex = [NSRegularExpression regularExpressionWithPattern:@"^[A-Za-z0-9_-]{1,64}$" options:0 error:nil];
    BOOL identifierOK = [identifier isKindOfClass:[NSString class]] && [regex numberOfMatchesInString:identifier options:0 range:NSMakeRange(0, identifier.length)] == 1 && [identifier isEqualToString:packID];
    BOOL metadataOK = [manifest[@"name"] isKindOfClass:[NSString class]] && [manifest[@"name"] length] > 0 && [manifest[@"name"] length] <= 80 &&
        [manifest[@"version"] isKindOfClass:[NSString class]] && [manifest[@"author"] isKindOfClass:[NSString class]];
    BOOL api1 = format.integerValue == 1 && api.integerValue == 1 && [stage isEqualToString:@"postprocess"] &&
        ([source isEqualToString:@"shaders/postprocess.metal"] || [source isEqualToString:@"shaders/postprocess.metallib"]) &&
        [entry isEqualToString:@"shader64_post"];
    BOOL api2 = format.integerValue == 2 && api.integerValue == 2 && [stage isEqualToString:@"render_graph"] &&
        [manifest[@"passes"] isKindOfClass:[NSArray class]] && [manifest[@"passes"] count] > 0 &&
        [manifest[@"passes"] count] <= GFX_SHADER64_MAX_GRAPH_PASSES;
    if (api2) {
        NSRegularExpression *nameRegex = [NSRegularExpression regularExpressionWithPattern:@"^[A-Za-z0-9_-]{1,32}$" options:0 error:nil];
        id textureListValue = manifest[@"textures"];
        NSArray *textureList = [textureListValue isKindOfClass:[NSArray class]] ? textureListValue : nil;
        if ((textureListValue && !textureList) || textureList.count > GFX_SHADER64_MAX_PACK_TEXTURES) api2 = NO;
        NSMutableSet *textureIds = [NSMutableSet set];
        NSMutableSet *texturePaths = [NSMutableSet set];
        NSString *packDirectory = [root stringByAppendingPathComponent:packID];
        for (NSDictionary *texture in textureList ?: @[]) {
            NSString *textureID = [texture isKindOfClass:[NSDictionary class]] ? texture[@"id"] : nil;
            NSString *texturePath = [texture isKindOfClass:[NSDictionary class]] ? texture[@"source"] : nil;
            BOOL textureOK = [textureID isKindOfClass:[NSString class]] &&
                [nameRegex numberOfMatchesInString:textureID options:0 range:NSMakeRange(0, textureID.length)] == 1 &&
                [texturePath isKindOfClass:[NSString class]] && [texturePath hasPrefix:@"textures/"] &&
                ![texturePath containsString:@".."] && texturePath.pathComponents.count == 2 &&
                [texturePath hasSuffix:@".png"] && ![textureIds containsObject:textureID] &&
                ![texturePaths containsObject:texturePath];
            NSString *textureFile = textureOK ? [packDirectory stringByAppendingPathComponent:texturePath] : @"";
            NSDictionary *textureAttributes = textureOK ? [[NSFileManager defaultManager] attributesOfItemAtPath:textureFile error:nil] : nil;
            unsigned long long textureSize = [textureAttributes[NSFileSize] unsignedLongLongValue];
            if (!textureOK || ![textureAttributes[NSFileType] isEqualToString:NSFileTypeRegular] ||
                !textureSize || textureSize > GFX_SHADER64_MAX_PACK_TEXTURE_BYTES) { api2 = NO; break; }
            [textureIds addObject:textureID]; [texturePaths addObject:texturePath];
        }
        NSMutableSet *outputs = [NSMutableSet set];
        for (NSDictionary *pass in manifest[@"passes"]) {
            if (!api2) break;
            if (![pass isKindOfClass:[NSDictionary class]]) { api2 = NO; break; }
            NSString *pid = pass[@"id"], *input = pass[@"input"], *output = pass[@"output"], *path = pass[@"source"], *type = pass[@"type"];
            NSNumber *scale = pass[@"scale"];
            BOOL compute = [type isEqualToString:@"compute"];
            BOOL passOK = [pass isKindOfClass:[NSDictionary class]] &&
                [pid isKindOfClass:[NSString class]] && [nameRegex numberOfMatchesInString:pid options:0 range:NSMakeRange(0, pid.length)] == 1 &&
                [input isKindOfClass:[NSString class]] && [nameRegex numberOfMatchesInString:input options:0 range:NSMakeRange(0, input.length)] == 1 &&
                [output isKindOfClass:[NSString class]] && ([output isEqualToString:@"present"] || [nameRegex numberOfMatchesInString:output options:0 range:NSMakeRange(0, output.length)] == 1) &&
                [path isKindOfClass:[NSString class]] && [path hasPrefix:@"shaders/"] && ![path containsString:@".."] &&
                [[path pathComponents] count] == 2 && [path hasSuffix:@".metal"] &&
                ([type isEqualToString:@"fullscreen"] || compute) &&
                [scale isKindOfClass:[NSNumber class]] && isfinite(scale.doubleValue) && scale.doubleValue >= 0.25 && scale.doubleValue <= 1.0 &&
                (!compute || ![output isEqualToString:@"present"]);
            id sceneDepth = pass[@"sceneDepth"];
            if (sceneDepth && ![sceneDepth isKindOfClass:[NSNumber class]]) passOK = NO;
            id sceneLighting = pass[@"sceneLighting"];
            if (sceneLighting && ![sceneLighting isKindOfClass:[NSNumber class]]) passOK = NO;
            id sceneShadows = pass[@"sceneShadows"];
            if (sceneShadows && ![sceneShadows isKindOfClass:[NSNumber class]]) passOK = NO;
            id sceneNormals = pass[@"sceneNormals"];
            if (sceneNormals && ![sceneNormals isKindOfClass:[NSNumber class]]) passOK = NO;
            id frameInfo = pass[@"frameInfo"];
            if (frameInfo && ![frameInfo isKindOfClass:[NSNumber class]]) passOK = NO;
            id packTextures = pass[@"packTextures"];
            if (packTextures && ![packTextures isKindOfClass:[NSNumber class]]) passOK = NO;
            if ([packTextures boolValue] && (!textureList.count || (!compute && ([sceneDepth boolValue] ||
                [sceneLighting boolValue] || [sceneShadows boolValue])))) passOK = NO;
            if (!compute && [sceneNormals boolValue] && ([sceneDepth boolValue] || [sceneLighting boolValue] ||
                [sceneShadows boolValue])) passOK = NO;
            if (compute && ([sceneShadows boolValue] ||
                ([sceneLighting boolValue] && (![sceneDepth boolValue] || ![sceneNormals boolValue])))) passOK = NO;
            if ([frameInfo boolValue] && (compute || [sceneDepth boolValue] || [sceneLighting boolValue] ||
                [sceneShadows boolValue] || [sceneNormals boolValue] || [packTextures boolValue])) passOK = NO;
            if (!passOK || [outputs containsObject:output]) { api2 = NO; break; }
            [outputs addObject:output];
        }
        if (![[manifest[@"passes"] lastObject][@"output"] isEqualToString:@"present"]) api2 = NO;
        id material = manifest[@"materialShader"];
        if (material) {
            NSString *materialPath = [material isKindOfClass:[NSDictionary class]] ? material[@"source"] : nil;
            if (![materialPath isKindOfClass:[NSString class]] || ![materialPath hasPrefix:@"shaders/"] ||
                [materialPath containsString:@".."] || [materialPath.pathComponents count] != 2 ||
                ![materialPath hasSuffix:@".metal"] ||
                (material[@"packTextures"] && ![material[@"packTextures"] isKindOfClass:[NSNumber class]])) api2 = NO;
            if ([material[@"packTextures"] boolValue] && textureList.count == 0) api2 = NO;
        }
    }
    if (!identifierOK || !metadataOK || (!api1 && !api2)) {
        if (errorOut) *errorOut = @"Manifest must use the supported API 1 postprocess contract or API 2 bounded fullscreen/compute render graph.";
        return nil;
    }
    return manifest;
}

- (void)activateShaderPack:(NSString *)packID {
    NSString *root = [self shaderPackRoot], *validation = nil;
    NSDictionary *manifest = [self shaderManifestForID:packID root:root error:&validation];
    BOOL graphPack = [manifest[@"shaderApiVersion"] integerValue] == 2;
    NSString *relative = [manifest[@"source"] isKindOfClass:[NSString class]] ? manifest[@"source"] : @"";
    BOOL compiledLibrary = [relative isEqualToString:@"shaders/postprocess.metallib"];
    NSString *sourcePath = [[root stringByAppendingPathComponent:packID] stringByAppendingPathComponent:relative];
    NSData *source = manifest && !graphPack ? [NSData dataWithContentsOfFile:sourcePath] : nil;
    NSMutableArray<NSData *> *graphSources = [NSMutableArray array];
    NSData *materialSource = nil;
    struct GfxShader64GraphSource graphPasses[GFX_SHADER64_MAX_GRAPH_PASSES];
    memset(graphPasses, 0, sizeof(graphPasses));
    if (manifest && graphPack) {
        NSArray *definitions = manifest[@"passes"];
        for (NSUInteger i = 0; i < definitions.count; ++i) {
            NSDictionary *definition = definitions[i];
            NSString *path = definition[@"source"];
            NSString *file = [[root stringByAppendingPathComponent:packID] stringByAppendingPathComponent:path];
            NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:file error:nil];
            NSData *bytes = [attrs[NSFileType] isEqualToString:NSFileTypeRegular] && [attrs[NSFileSize] unsignedLongLongValue] <= GFX_SHADER64_MAX_SOURCE_BYTES ? [NSData dataWithContentsOfFile:file] : nil;
            if (!bytes) { validation = @"A graph pass source is missing or exceeds the 256 KiB limit"; break; }
            [graphSources addObject:bytes];
            NSString *type = definition[@"type"];
            unsigned int nodeType = [type isEqualToString:@"compute"] ?
                GFX_SHADER64_NODE_COMPUTE : GFX_SHADER64_NODE_FULLSCREEN;
            graphPasses[i] = (struct GfxShader64GraphSource){
                [definition[@"id"] UTF8String], [definition[@"input"] UTF8String], [definition[@"output"] UTF8String],
                bytes.bytes, bytes.length, [definition[@"scale"] floatValue], nodeType,
                ([definition[@"sceneDepth"] boolValue] ? GFX_SHADER64_RESOURCE_SCENE_DEPTH : 0u) |
                ([definition[@"sceneLighting"] boolValue] ? GFX_SHADER64_RESOURCE_DIRECTIONAL_LIGHT : 0u) |
                ([definition[@"sceneShadows"] boolValue] ? GFX_SHADER64_RESOURCE_SHADOW_MAP : 0u) |
                ([definition[@"sceneNormals"] boolValue] ? GFX_SHADER64_RESOURCE_SCENE_NORMALS : 0u) |
                ([definition[@"packTextures"] boolValue] ? GFX_SHADER64_RESOURCE_PACK_TEXTURES : 0u) };
        }
        NSDictionary *materialDefinition = manifest[@"materialShader"];
        if (validation == nil && materialDefinition) {
            NSString *path = materialDefinition[@"source"];
            NSString *file = [[root stringByAppendingPathComponent:packID] stringByAppendingPathComponent:path];
            NSDictionary *attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:file error:nil];
            materialSource = [attrs[NSFileType] isEqualToString:NSFileTypeRegular] &&
                [attrs[NSFileSize] unsignedLongLongValue] <= GFX_SHADER64_MAX_SOURCE_BYTES ?
                [NSData dataWithContentsOfFile:file] : nil;
            if (!materialSource) validation = @"The material shader is missing or exceeds the 256 KiB limit";
        }
    }
    float parameters[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    NSArray *configured = manifest[@"parameters"];
    if (configured && (![configured isKindOfClass:[NSArray class]] || configured.count > 4)) validation = @"parameters must be an array of at most four float declarations";
    NSArray *savedForPack = [[NSUserDefaults standardUserDefaults] arrayForKey:[@"SM64Shader64Parameters." stringByAppendingString:packID]];
    if ([configured isKindOfClass:[NSArray class]]) for (NSUInteger i = 0; i < configured.count && i < 4; ++i) {
        NSDictionary *definition = configured[i];
        if (![definition isKindOfClass:[NSDictionary class]]) { validation = @"Each parameter must be an object"; break; }
        NSNumber *defaultValue = definition[@"default"];
        NSString *type = definition[@"type"];
        if (![definition[@"name"] isKindOfClass:[NSString class]] || ![definition[@"name"] length] ||
            ![definition[@"label"] isKindOfClass:[NSString class]] || ![definition[@"label"] length] ||
            ![defaultValue isKindOfClass:[NSNumber class]] || !isfinite(defaultValue.doubleValue)) {
            validation = @"Each parameter needs a non-empty name/label and finite numeric default"; break;
        }
        double v = defaultValue.doubleValue;
        if ([type isEqualToString:@"float"]) {
            NSNumber *minimum = definition[@"min"], *maximum = definition[@"max"];
            if (![minimum isKindOfClass:[NSNumber class]] || ![maximum isKindOfClass:[NSNumber class]] ||
                !isfinite(minimum.doubleValue) || !isfinite(maximum.doubleValue) ||
                minimum.doubleValue < -GFX_SHADER64_PARAMETER_ABS_MAX ||
                maximum.doubleValue > GFX_SHADER64_PARAMETER_ABS_MAX ||
                minimum.doubleValue > maximum.doubleValue || v < minimum.doubleValue || v > maximum.doubleValue) {
                validation = @"Float parameter bounds must be finite, within -16…16, and contain the default"; break;
            }
            if (savedForPack.count == configured.count && [savedForPack[i] isKindOfClass:[NSNumber class]] && isfinite([savedForPack[i] doubleValue]))
                v = MIN(maximum.doubleValue, MAX(minimum.doubleValue, [savedForPack[i] doubleValue]));
        } else if ([type isEqualToString:@"boolean"]) {
            if (v != 0.0 && v != 1.0) { validation = @"Boolean parameter defaults must be 0 or 1"; break; }
            if (savedForPack.count == configured.count && [savedForPack[i] isKindOfClass:[NSNumber class]])
                v = [savedForPack[i] boolValue] ? 1.0 : 0.0;
        } else if ([type isEqualToString:@"select"]) {
            NSArray *options = definition[@"options"];
            if (![options isKindOfClass:[NSArray class]] || options.count < 2 || options.count > 8 ||
                floor(v) != v || v < 0 || v >= options.count) {
                validation = @"Select parameters need 2–8 options and a valid integer default index"; break;
            }
            for (id option in options) if (![option isKindOfClass:[NSString class]] || ![option length] || [option length] > 48) {
                validation = @"Select options must be non-empty strings up to 48 characters"; break;
            }
            if (validation) break;
            if (savedForPack.count == configured.count && [savedForPack[i] isKindOfClass:[NSNumber class]] && isfinite([savedForPack[i] doubleValue]))
                v = MIN((double)options.count - 1.0, MAX(0.0, round([savedForPack[i] doubleValue])));
        } else { validation = @"Parameter type must be float, boolean, or select"; break; }
        parameters[i] = (float)v;
    }
    char error[512] = {0};
    NSDictionary *materialDefinition = manifest[@"materialShader"];
    BOOL materialPackTextures = [materialDefinition[@"packTextures"] boolValue];
    BOOL loaded = !validation && (graphPack ? (graphSources.count == [manifest[@"passes"] count] &&
        gfx_metal_load_shader64_graph_with_material(packID.UTF8String, graphPasses, graphSources.count,
        materialSource.bytes, materialSource.length, materialPackTextures, parameters, error, sizeof(error))) :
        (source && (compiledLibrary ? gfx_metal_load_shader64_library(packID.UTF8String,
        source.bytes, source.length, parameters, error, sizeof(error)) : gfx_metal_load_shader64_source(packID.UTF8String,
        source.bytes, source.length, parameters, error, sizeof(error)))));
    if (!loaded) {
        NSString *message = validation ?: (source ? [NSString stringWithUTF8String:error] : @"Shader source file is missing.");
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Shader pack not activated" message:message preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
        [self appendStatus:[NSString stringWithFormat:@"Shader error: %@", message]];
        return;
    }
    NSArray *values = @[@(parameters[0]), @(parameters[1]), @(parameters[2]), @(parameters[3])];
    [[NSUserDefaults standardUserDefaults] setObject:values forKey:[@"SM64Shader64Parameters." stringByAppendingString:packID]];
    gfx_metal_set_shader64_parameters(parameters);
    [self refreshGraphicsSettings];
    [self appendStatus:[NSString stringWithFormat:@"Activated %@", manifest[@"name"]]];
}

- (void)configureShaderPack:(NSString *)packID {
    NSDictionary *manifest = [self shaderManifestForID:packID root:[self shaderPackRoot] error:nil];
    NSArray *definitions = manifest[@"parameters"];
    if (![definitions isKindOfClass:[NSArray class]] || !definitions.count || definitions.count > 4) return;
    NSArray *current = [[NSUserDefaults standardUserDefaults] arrayForKey:[@"SM64Shader64Parameters." stringByAppendingString:packID]];
    NSMutableArray *values = [NSMutableArray arrayWithCapacity:definitions.count];
    for (NSUInteger i = 0; i < definitions.count; ++i) {
        NSDictionary *definition = definitions[i];
        NSNumber *value = current.count == definitions.count && [current[i] isKindOfClass:[NSNumber class]]
            ? current[i] : definition[@"default"];
        [values addObject:value];
    }
    SHADER64ParameterController *controller = [[[SHADER64ParameterController alloc] init] autorelease];
    controller.title = [NSString stringWithFormat:@"%@ Parameters", manifest[@"name"]];
    controller.definitions = definitions;
    controller.values = values;
    controller.modalPresentationStyle = UIModalPresentationFormSheet;
    controller.preferredContentSize = CGSizeMake(420, 560);
    __weak SM64DiagnosticsViewController *weakSelf = self;
    controller.onApply = ^(NSArray<NSNumber *> *selected) {
        SM64DiagnosticsViewController *strongSelf = weakSelf;
        if (!strongSelf) return;
        float parameters[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        for (NSUInteger i = 0; i < selected.count && i < 4; ++i) parameters[i] = [selected[i] floatValue];
        [[NSUserDefaults standardUserDefaults] setObject:selected forKey:[@"SM64Shader64Parameters." stringByAppendingString:packID]];
        gfx_metal_set_shader64_parameters(parameters);
        [strongSelf appendStatus:[NSString stringWithFormat:@"Updated %@ parameters", manifest[@"name"]]];
    };
    [self presentViewController:controller animated:YES completion:nil];
}

- (void)importShaderPack {
    UIDocumentPickerViewController *picker = [[[UIDocumentPickerViewController alloc]
        initWithDocumentTypes:@[@"public.zip-archive"] inMode:UIDocumentPickerModeImport] autorelease];
    picker.delegate = self;
    [self presentViewController:picker animated:YES completion:nil];
}

- (void)deleteShaderPack {
    NSString *root = [self shaderPackRoot];
    NSArray *children = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:root error:nil];
    UIAlertController *menu = [UIAlertController alertControllerWithTitle:@"Remove Shader Pack"
        message:@"The original renderer remains available as Classic64." preferredStyle:UIAlertControllerStyleActionSheet];
    NSString *active = [NSString stringWithUTF8String:gfx_metal_active_shader64_pack()];
    for (NSString *packID in children) {
        if (![self shaderManifestForID:packID root:root error:nil]) continue;
        [menu addAction:[UIAlertAction actionWithTitle:packID style:UIAlertActionStyleDestructive handler:^(UIAlertAction *action) {
            (void)action;
            if ([active isEqualToString:packID]) gfx_metal_use_classic_shader();
            NSError *error = nil;
            BOOL removed = [[NSFileManager defaultManager] removeItemAtPath:[root stringByAppendingPathComponent:packID] error:&error];
            [[NSUserDefaults standardUserDefaults] removeObjectForKey:[@"SM64Shader64Parameters." stringByAppendingString:packID]];
            if (removed) [self appendStatus:[NSString stringWithFormat:@"Removed %@", packID]];
            else [self appendStatus:[NSString stringWithFormat:@"Could not remove %@: %@", packID, error.localizedDescription ?: @"unknown error"]];
            [self refreshGraphicsSettings];
        }]];
    }
    [menu addAction:[UIAlertAction actionWithTitle:@"Cancel" style:UIAlertActionStyleCancel handler:nil]];
    menu.popoverPresentationController.sourceView = self.shaderPacksButton;
    menu.popoverPresentationController.sourceRect = self.shaderPacksButton.bounds;
    [self presentViewController:menu animated:YES completion:nil];
}

- (void)documentPicker:(UIDocumentPickerViewController *)controller didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;
    if (urls.count) [self installShaderArchiveAtURL:urls[0]];
}

- (void)installShaderArchiveAtURL:(NSURL *)url {
    BOOL scoped = [url startAccessingSecurityScopedResource];
    NSNumber *fileSize = nil;
    NSError *sizeError = nil;
    BOOL hasFileSize = [url getResourceValue:&fileSize forKey:NSURLFileSizeKey error:&sizeError];
    long long archiveBytes = hasFileSize ? fileSize.longLongValue : -1;
    if (archiveBytes < 22 || (unsigned long long)archiveBytes > GFX_SHADER64_MAX_ARCHIVE_BYTES) {
        if (scoped) [url stopAccessingSecurityScopedResource];
        [self appendStatus:[NSString stringWithFormat:@"Shader ZIP is unreadable or exceeds 8 MiB: %@",
            sizeError.localizedDescription ?: @"invalid file size"]];
        return;
    }
    NSData *archive = [NSData dataWithContentsOfURL:url];
    if (scoped) [url stopAccessingSecurityScopedResource];
    if (!archive || archive.length < 22 || archive.length > GFX_SHADER64_MAX_ARCHIVE_BYTES) {
        [self appendStatus:@"Shader ZIP is unreadable or exceeds 8 MiB"];
        return;
    }
    NSString *root = [self shaderPackRoot];
    NSString *temporaryID = [NSString stringWithFormat:@".install-%@", NSUUID.UUID.UUIDString];
    NSString *temporaryPath = [root stringByAppendingPathComponent:temporaryID];
    NSError *fileError = nil;
    [[NSFileManager defaultManager] createDirectoryAtPath:temporaryPath withIntermediateDirectories:NO attributes:nil error:&fileError];
    char error[256] = {0};
    BOOL installed = !fileError && gfx_shader64_install_zip(archive.bytes, archive.length,
        temporaryPath.fileSystemRepresentation, error, sizeof(error));
    NSString *manifestPath = [temporaryPath stringByAppendingPathComponent:@"manifest.json"];
    NSData *manifestData = installed ? [NSData dataWithContentsOfFile:manifestPath] : nil;
    NSError *jsonError = nil;
    id value = manifestData ? [NSJSONSerialization JSONObjectWithData:manifestData options:NSJSONReadingFragmentsAllowed error:&jsonError] : nil;
    NSString *packID = [value isKindOfClass:[NSDictionary class]] ? value[@"id"] : nil;
    NSDictionary *manifest = nil;
    BOOL movedIntoFinalPath = NO;
    NSRegularExpression *idRegex = [NSRegularExpression regularExpressionWithPattern:@"^[A-Za-z0-9_-]{1,64}$" options:0 error:nil];
    BOOL packIDSafe = [packID isKindOfClass:[NSString class]] && [idRegex numberOfMatchesInString:packID options:0 range:NSMakeRange(0, packID.length)] == 1;
    if (installed && packIDSafe) {
        NSString *finalPath = [root stringByAppendingPathComponent:packID];
        if ([[NSFileManager defaultManager] fileExistsAtPath:finalPath]) {
            installed = NO; snprintf(error, sizeof(error), "a pack with this ID is already installed");
        } else if (![[NSFileManager defaultManager] moveItemAtPath:temporaryPath toPath:finalPath error:&fileError]) {
            installed = NO; snprintf(error, sizeof(error), "could not finalize installed pack");
        } else {
            movedIntoFinalPath = YES;
            manifest = [self shaderManifestForID:packID root:root error:nil];
            if (!manifest) { installed = NO; snprintf(error, sizeof(error), "invalid manifest or unsupported shader API"); }
        }
    } else if (installed) {
        installed = NO; snprintf(error, sizeof(error), "invalid manifest or unsupported shader API");
    }
    if (!installed) {
        [[NSFileManager defaultManager] removeItemAtPath:temporaryPath error:nil];
        if (movedIntoFinalPath) [[NSFileManager defaultManager] removeItemAtPath:[root stringByAppendingPathComponent:packID] error:nil];
        NSString *message = error[0] ? [NSString stringWithUTF8String:error] : (jsonError.localizedDescription ?: fileError.localizedDescription ?: @"invalid shader ZIP");
        [self appendStatus:[NSString stringWithFormat:@"Import failed: %@", message]];
        UIAlertController *alert = [UIAlertController alertControllerWithTitle:@"Shader pack import failed" message:message preferredStyle:UIAlertControllerStyleAlert];
        [alert addAction:[UIAlertAction actionWithTitle:@"OK" style:UIAlertActionStyleDefault handler:nil]];
        [self presentViewController:alert animated:YES completion:nil];
        return;
    }
    [self appendStatus:[NSString stringWithFormat:@"Installed %@", manifest[@"name"]]];
    [self activateShaderPack:packID];
}
#endif

- (void)updateTextureCacheDiagnosticsButton {
    BOOL enabled = [[NSUserDefaults standardUserDefaults] boolForKey:@"SM64TextureCacheDiagnosticsEnabled"];
    [self.textureCacheButton setTitle:enabled ? @"Tex log: ON" : @"Tex log: OFF" forState:UIControlStateNormal];
}

- (void)toggleTextureCacheDiagnostics {
    NSUserDefaults *defaults = [NSUserDefaults standardUserDefaults];
    BOOL enabled = ![defaults boolForKey:@"SM64TextureCacheDiagnosticsEnabled"];
    [defaults setBool:enabled forKey:@"SM64TextureCacheDiagnosticsEnabled"];
    [self updateTextureCacheDiagnosticsButton];
    gfx_ios_diag_log("Texture-cache diagnostics %s", enabled ? "enabled" : "disabled");
    [self appendStatus:enabled ? @"Texture cache logging enabled" : @"Texture cache logging disabled"];
}

- (void)cycleRenderDiagnosticMode {
    static NSString *const modeNames[] = {@"Normal", @"Flat", @"UV", @"Tex ID", @"Triangles", @"Material Class"};
    sm64RenderDiagnosticMode = (sm64RenderDiagnosticMode + 1) % 6;
    NSString *title = [NSString stringWithFormat:@"Mode: %@", modeNames[sm64RenderDiagnosticMode]];
    [self.renderModeButton setTitle:title forState:UIControlStateNormal];
    gfx_ios_diag_log("Render diagnostic mode changed mode=%d name=%s; Normal mode preserves the regular shader output", sm64RenderDiagnosticMode, modeNames[sm64RenderDiagnosticMode].UTF8String);
    [self appendStatus:[NSString stringWithFormat:@"Render diagnostic: %@", modeNames[sm64RenderDiagnosticMode]]];
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
            NSString *marker = [NSString stringWithFormat:@"%@ === process start pid=%d app=%@ appVersion=%@ release=%@ bundle=%@ iOS=%@ ===\n",
                [sm64DiagnosticsDateFormatter stringFromDate:[NSDate date]], getpid(), ios_app_display_name(),
                version, ios_app_release_version(), NSBundle.mainBundle.bundleIdentifier ?: @"unknown", UIDevice.currentDevice.systemVersion];
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
