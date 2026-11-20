//
//  TouchControlsViewController.m
//  sm64ios
//
//  Created by Christian Kosman on 10/25/21.
//

#import "TouchControlsViewController.h"
#import "src/pc/controller/controller_touchscreen.h"
#import "src/pc/gfx/gfx_uikit.h"
#include "src/pc/configfile.h"
#include <PR/os_cont.h>

static uint64_t ios_touch_began_count;
static uint64_t ios_touch_motion_count;
static uint64_t ios_touch_ended_count;
static uint64_t ios_touch_action_count;
static int ios_next_touch_id = 1;

@implementation OverlayImageView

- (id)initWithFrame:(CGRect)frame {
    if(self = [super initWithFrame:frame]) {
        [self setBackgroundColor:[UIColor clearColor]];
    }
    return self;
}

- (void)setRotation:(CGFloat)angle {
    CGFloat radians = angle / 180.0 * M_PI;
    CGAffineTransform rotation = CGAffineTransformRotate(self.transform, radians);
    self.transform = rotation;
}

@end

@implementation GestureRecognizer {
    void (*touch_down_callback)(void* event);
    void (*touch_motion_callback)(void* event);
    void (*touch_up_callback)(void* event);
    
    NSMapTable<UITouch *, NSNumber *> *trackedTouchIDs;
}

- (id)initWithTarget:(id)target action:(SEL)action {
    if((self = [super initWithTarget:target action:action])) {
        
    }
    trackedTouchIDs = [[NSMapTable strongToStrongObjectsMapTable] retain];
    return self;
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesBegan:touches withEvent:event];
    for (UITouch *touch in touches) {
        if ([trackedTouchIDs objectForKey:touch]) continue;
        int touchID = ios_next_touch_id++;
        if (ios_next_touch_id <= 0) ios_next_touch_id = 1;
        [trackedTouchIDs setObject:@(touchID) forKey:touch];
        CGPoint position = [touch locationInView:self.view];
        struct TouchEvent touchEvent;
        touchEvent.x = position.x;
        touchEvent.y = position.y;
        touchEvent.touchID = touchID;
        uint64_t sequence = ++ios_touch_began_count;
        if (sequence <= 12 || sequence % 120 == 0) {
            CGPoint windowPoint = [touch locationInView:self.view.window];
            UIView *hitView = [self.view.window hitTest:windowPoint withEvent:event];
            UIWindowScene *scene = self.view.window.windowScene;
            UIWindow *topWindow = nil;
            for (UIWindow *candidate in scene.windows) {
                if (!candidate.hidden && candidate.alpha > 0 && (!topWindow || candidate.windowLevel > topWindow.windowLevel)) topWindow = candidate;
            }
            gfx_ios_diag_log("UIKit touch began seq=%llu id=%d pointInTouchView=(%.1f,%.1f) viewBounds=(%.1f,%.1f %.1fx%.1f) receiver=%@ hitView=%@ window=%@ topVisibleWindow=%@ scene=%@ interaction=%d callback=%d tracked=%lu", (unsigned long long)sequence, touchEvent.touchID, touchEvent.x, touchEvent.y, self.view.bounds.origin.x, self.view.bounds.origin.y, self.view.bounds.size.width, self.view.bounds.size.height, self.view, hitView, self.view.window, topWindow, scene.session.persistentIdentifier, self.view.userInteractionEnabled, touch_down_callback != NULL, (unsigned long)trackedTouchIDs.count);
        }
        if (touch_down_callback != NULL) touch_down_callback((void *)&touchEvent);
    }
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesMoved:touches withEvent:event];
    for (UITouch *touch in touches) {
        NSNumber *trackedID = [trackedTouchIDs objectForKey:touch];
        if (!trackedID) {
            gfx_ios_diag_log("UIKit touch moved without tracked begin; ignoring touch=%@", touch);
            continue;
        }
        CGPoint position = [touch locationInView:self.view];
        struct TouchEvent touchEvent;
        touchEvent.x = position.x;
        touchEvent.y = position.y;
        touchEvent.touchID = trackedID.intValue;
        uint64_t sequence = ++ios_touch_motion_count;
        if (sequence <= 4 || sequence % 240 == 0)
            gfx_ios_diag_log("UIKit touch moved seq=%llu id=%d point=(%.1f,%.1f) callback=%d", (unsigned long long)sequence, touchEvent.touchID, touchEvent.x, touchEvent.y, touch_motion_callback != NULL);
        if (touch_motion_callback != NULL) touch_motion_callback((void *)&touchEvent);
    }
}

- (void)handleTouchesEnded:(NSSet<UITouch *> *)touches {
    for (UITouch *touch in touches) {
        NSNumber *trackedID = [trackedTouchIDs objectForKey:touch];
        if (!trackedID) {
            gfx_ios_diag_log("UIKit touch ended without tracked begin; ignoring touch=%@ cancelled=%d", touch, touch.phase == UITouchPhaseCancelled);
            continue;
        }
        CGPoint position = [touch locationInView:self.view];
        struct TouchEvent touchEvent;
        touchEvent.x = position.x;
        touchEvent.y = position.y;
        touchEvent.touchID = trackedID.intValue;
        uint64_t sequence = ++ios_touch_ended_count;
        if (sequence <= 12 || sequence % 120 == 0)
            gfx_ios_diag_log("UIKit touch ended seq=%llu id=%d point=(%.1f,%.1f) callback=%d cancelled=%d", (unsigned long long)sequence, touchEvent.touchID, touchEvent.x, touchEvent.y, touch_up_callback != NULL, touch.phase == UITouchPhaseCancelled);
        if (touch_up_callback != NULL) touch_up_callback((void *)&touchEvent);
        [trackedTouchIDs removeObjectForKey:touch];
    }
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesEnded:touches withEvent:event];
    [self handleTouchesEnded:touches];
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesCancelled:touches withEvent:event];
    [self handleTouchesEnded:touches];
}

- (void)set_touchscreen_callbacks:(void (*)(void* event))down motion:(void (*)(void* event))motion up:(void (*)(void* event))up {
    touch_down_callback = down;
    touch_motion_callback = motion;
    touch_up_callback = up;
}

- (void)reset {
    [super reset];
    NSEnumerator<UITouch *> *touches = trackedTouchIDs.keyEnumerator;
    UITouch *touch = nil;
    while ((touch = [touches nextObject])) {
        NSNumber *trackedID = [trackedTouchIDs objectForKey:touch];
        if (trackedID && touch_up_callback) {
            struct TouchEvent touchEvent = { .touchID = trackedID.intValue, .x = 0, .y = 0 };
            touch_up_callback((void *)&touchEvent);
        }
    }
    [trackedTouchIDs removeAllObjects];
}

@end

@implementation TouchControlsViewController {
    void (*touch_down_callback)(void* event);
    void (*touch_motion_callback)(void* event);
    void (*touch_up_callback)(void* event);
    
    GestureRecognizer *gestureRecognizer;
    OverlayImageView *dpadDownButton;
#ifdef IOS_HAT_MOVEMENT
    OverlayImageView *hatButton;
#endif
    bool hidden;
}

- (void)viewDidLayoutSubviews {
    [super viewDidLayoutSubviews];
    static CGRect lastLoggedBounds;
    if (CGRectEqualToRect(lastLoggedBounds, self.view.bounds)) return;
    lastLoggedBounds = self.view.bounds;
    gfx_ios_diag_log("UIKit touch layout bounds=(%.1f,%.1f %.1fx%.1f) frame=(%.1f,%.1f %.1fx%.1f) window=%@ scene=%@ interaction=%d", self.view.bounds.origin.x, self.view.bounds.origin.y, self.view.bounds.size.width, self.view.bounds.size.height, self.view.frame.origin.x, self.view.frame.origin.y, self.view.frame.size.width, self.view.frame.size.height, self.view.window, self.view.window.windowScene.session.persistentIdentifier, self.view.userInteractionEnabled);
    OverlayImageView *controls[] = { self.m_iv_l, self.m_iv_r, self.m_iv_a, self.m_iv_b, self.m_iv_z, self.m_iv_up, self.m_iv_down, self.m_iv_left, self.m_iv_right, self.m_iv_start, self.m_iv_menu, self.m_iv_joystick, self.m_iv_joystick_bg, dpadDownButton };
    for (NSUInteger i = 0; i < sizeof(controls) / sizeof(controls[0]); ++i) {
        OverlayImageView *control = controls[i];
        if (!control) continue;
        CGPoint origin = [control convertPoint:CGPointZero toView:self.view];
        gfx_ios_diag_log("UIKit touch control layout tag=%d image=%@ frame=(%.1f,%.1f %.1fx%.1f) convertedOrigin=(%.1f,%.1f) bounds=(%.1fx%.1f) interaction=%d hidden=%d", control.buttonTag, control.image, control.frame.origin.x, control.frame.origin.y, control.frame.size.width, control.frame.size.height, origin.x, origin.y, control.bounds.size.width, control.bounds.size.height, control.userInteractionEnabled, control.hidden);
    }
}

- (void)viewDidLoad {
    [super viewDidLoad];
    
    self.view.multipleTouchEnabled = YES;
    gestureRecognizer = [[GestureRecognizer alloc] initWithTarget:self action:@selector(handleTouchAction:)];
    // Touch controls use the recognizer's direct callback path, while allowing
    // UIKit's normal view responder chain to finish the same touch sequence.
    gestureRecognizer.cancelsTouchesInView = NO;
    gestureRecognizer.delaysTouchesBegan = NO;
    gestureRecognizer.delaysTouchesEnded = NO;
    [gestureRecognizer set_touchscreen_callbacks:touch_down_callback motion:touch_motion_callback up:touch_up_callback];
    [self.view addGestureRecognizer:gestureRecognizer];
    gfx_ios_diag_log("UIKit touch recognizer installed touchView=%@ bounds=(%.1f,%.1f %.1fx%.1f) interaction=%d multipleTouch=%d cancelsTouches=%d subviews=%lu", self.view, self.view.bounds.origin.x, self.view.bounds.origin.y, self.view.bounds.size.width, self.view.bounds.size.height, self.view.userInteractionEnabled, self.view.multipleTouchEnabled, gestureRecognizer.cancelsTouchesInView, (unsigned long)self.view.subviews.count);
    
    [self.m_gl_up setRotation:0.0];
    [self.m_gl_down setRotation:180.0];
    [self.m_gl_left setRotation:270.0];
    [self.m_gl_right setRotation:90.0];
    
    CGFloat scale = ((float)configTouchUiScale/100.0);
    self.m_group_lr.transform = CGAffineTransformMakeScale(scale, scale);
    self.m_group_joystick.transform = CGAffineTransformMakeScale(scale, scale);
    self.m_group_startmenu.transform = CGAffineTransformMakeScale(scale, scale);
    self.m_group_main.transform = CGAffineTransformMakeScale(scale, scale);
    self.m_group_cbuttons.transform = CGAffineTransformMakeScale(scale, scale);

    // CoopDX possession and other mods use the N64 d-pad independently of
    // the C buttons. Place a dedicated down control above the left stick.
    dpadDownButton = [[[OverlayImageView alloc] initWithFrame:CGRectMake(80, -60, 56, 56)] autorelease];
    dpadDownButton.image = [UIImage imageNamed:@"touch_button"];
    dpadDownButton.buttonTag = D_JPAD;
    dpadDownButton.accessibilityLabel = @"D-pad down";
    dpadDownButton.isAccessibilityElement = YES;
    [self.m_group_joystick addSubview:dpadDownButton];

    UILabel *dpadLabel = [[[UILabel alloc] initWithFrame:dpadDownButton.bounds] autorelease];
    dpadLabel.text = @"D↓";
    dpadLabel.textColor = [UIColor whiteColor];
    dpadLabel.font = [UIFont boldSystemFontOfSize:16.0];
    dpadLabel.textAlignment = NSTextAlignmentCenter;
    dpadLabel.userInteractionEnabled = NO;
    [dpadDownButton addSubview:dpadLabel];
    
#ifdef IOS_HAT_MOVEMENT
    // The lower left slot in the A/B/Z cluster is unused.
    hatButton = [[[OverlayImageView alloc] initWithFrame:CGRectMake(0, 72, 64, 64)] autorelease];
    hatButton.image = [UIImage imageNamed:@"touch_button"];
    hatButton.buttonTag = HAT_BUTTON;
    [self.m_group_main addSubview:hatButton];

    UILabel *label = [[[UILabel alloc] initWithFrame:hatButton.bounds] autorelease];
    label.text = @"HAT";
    label.textColor = [UIColor whiteColor];
    label.font = [UIFont boldSystemFontOfSize:17.0];
    label.textAlignment = NSTextAlignmentCenter;
    [hatButton addSubview:label];
#endif

    [self setTouchscreenImageViews];
}

- (void)setTouchscreenImageViews {
    self.m_iv_l.buttonTag = L_TRIG;
    self.m_iv_r.buttonTag = R_TRIG;
    self.m_iv_a.buttonTag = A_BUTTON;
    self.m_iv_b.buttonTag = B_BUTTON;
    self.m_iv_z.buttonTag = Z_TRIG;
    self.m_iv_up.buttonTag = U_CBUTTONS;
    self.m_iv_down.buttonTag = D_CBUTTONS;
    self.m_iv_left.buttonTag = L_CBUTTONS;
    self.m_iv_right.buttonTag = R_CBUTTONS;
    self.m_iv_start.buttonTag = START_BUTTON;
    self.m_iv_menu.buttonTag = 69;
    self.m_iv_joystick.buttonTag = 420;
    self.m_iv_joystick_bg.buttonTag = 69420;
    
    NSMutableArray *imageViews = [[NSMutableArray alloc] init];
    [imageViews addObject:self.m_iv_l];
    [imageViews addObject:self.m_iv_r];
    [imageViews addObject:self.m_iv_a];
    [imageViews addObject:self.m_iv_b];
    [imageViews addObject:self.m_iv_z];
#ifdef IOS_HAT_MOVEMENT
    [imageViews addObject:hatButton];
#endif
    [imageViews addObject:self.m_iv_up];
    [imageViews addObject:self.m_iv_down];
    [imageViews addObject:self.m_iv_left];
    [imageViews addObject:self.m_iv_right];
    [imageViews addObject:self.m_iv_start];
    [imageViews addObject:self.m_iv_menu];
    [imageViews addObject:self.m_iv_joystick];
    [imageViews addObject:self.m_iv_joystick_bg];
    [imageViews addObject:dpadDownButton];
    touchscreen_set_imageviews(imageViews);
}

- (void)handleTouchAction:(GestureRecognizer *)sender {
    uint64_t sequence = ++ios_touch_action_count;
    if (sequence <= 8 || sequence % 120 == 0) {
        gfx_ios_diag_log("UIKit gesture target action seq=%llu state=%ld touches=%lu receiver=%@ callbackRoute=direct-controller", (unsigned long long)sequence, (long)sender.state, (unsigned long)sender.numberOfTouches, sender.view);
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesBegan:touches withEvent:event];
    static uint64_t responder_began_count;
    uint64_t sequence = ++responder_began_count;
    if (sequence <= 8 || sequence % 120 == 0)
        gfx_ios_diag_log("UIKit touch view-controller responder began seq=%llu touches=%lu view=%@ window=%@", (unsigned long long)sequence, (unsigned long)touches.count, self.view, self.view.window);
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesMoved:touches withEvent:event];
    static uint64_t responder_moved_count;
    uint64_t sequence = ++responder_moved_count;
    if (sequence <= 4 || sequence % 240 == 0)
        gfx_ios_diag_log("UIKit touch view-controller responder moved seq=%llu touches=%lu", (unsigned long long)sequence, (unsigned long)touches.count);
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesEnded:touches withEvent:event];
    static uint64_t responder_ended_count;
    uint64_t sequence = ++responder_ended_count;
    if (sequence <= 8 || sequence % 120 == 0)
        gfx_ios_diag_log("UIKit touch view-controller responder ended seq=%llu touches=%lu", (unsigned long long)sequence, (unsigned long)touches.count);
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    [super touchesCancelled:touches withEvent:event];
    static uint64_t responder_cancelled_count;
    uint64_t sequence = ++responder_cancelled_count;
    if (sequence <= 8 || sequence % 120 == 0)
        gfx_ios_diag_log("UIKit touch view-controller responder cancelled seq=%llu touches=%lu", (unsigned long long)sequence, (unsigned long)touches.count);
}

- (void)set_touchscreen_callbacks:(void (*)(void* event))down motion:(void (*)(void* event))motion up:(void (*)(void* event))up {
    touch_down_callback = down;
    touch_motion_callback = motion;
    touch_up_callback = up;
    [gestureRecognizer set_touchscreen_callbacks:touch_down_callback motion:touch_motion_callback up:touch_up_callback];
    gfx_ios_diag_log("UIKit touch callbacks bound down=%d motion=%d up=%d recognizer=%@", touch_down_callback != NULL, touch_motion_callback != NULL, touch_up_callback != NULL, gestureRecognizer);
}

- (void)setTouchControlsHidden:(bool)hide {
    if(hidden == hide) return;
    hidden = hide;
    for(UIView *overlayView in self.view.subviews) {
        overlayView.hidden = hide;
    }
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures {
    return UIRectEdgeBottom;
}

@end
