// Feel free to use it in your port too, but please keep authorship!
// Touch Controls made by: VDavid003
// https://github.com/VDavid003/sm64-port-android
// Modified for iOS
#include <stdlib.h>
#include <stdio.h>

#include <ultra64.h>
#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "controller_api.h"
#include "controller_touch_axis.h"
#import "controller_touchscreen.h"

#if TARGET_OS_IOS
#include "config.h"
#include "sm64.h"
#include "game/game_init.h"
#include "game/memory.h"
#include "game/segment2.h"
#include "gfx_dimensions.h"
#include "pc/gfx/gfx_pc.h"
#include "pc/gfx/gfx_uikit.h"
#include "../configfile.h"

#import "src/ios/ios/HapticsController.h"
#import "src/ios/FrameController.h"

#define SCREEN_WIDTH_API 1280
#define SCREEN_HEIGHT_API 960
#define SCREEN_ASPECT_API ((0.0 + SCREEN_WIDTH_API)/SCREEN_HEIGHT_API)

#define LEFT_EDGE ((int)floorf(SCREEN_WIDTH_API / 2 - SCREEN_HEIGHT_API / 2 * gfx_current_dimensions.aspect_ratio))
#define RIGHT_EDGE ((int)ceilf(SCREEN_WIDTH_API / 2 + SCREEN_HEIGHT_API / 2 * gfx_current_dimensions.aspect_ratio))

#define CORRECT_TOUCH_X(x) ((x * (RIGHT_EDGE - LEFT_EDGE)) + LEFT_EDGE)
#define CORRECT_TOUCH_Y(y) (y * SCREEN_HEIGHT_API)

#define SCREEN_SCALE [[UIScreen mainScreen] scale]

#define N64_TO_IOS_X(x) ((double)(x - LEFT_EDGE) / (double)(RIGHT_EDGE - LEFT_EDGE) * gfx_current_dimensions.width / SCREEN_SCALE)
#define N64_TO_IOS_Y(y) (((0.0 + y) / SCREEN_HEIGHT_API) * gfx_current_dimensions.height / SCREEN_SCALE)

#define N64_DISTANCE_X(a, b) (N64_TO_IOS_X(a) - N64_TO_IOS_X(b))
#define N64_DISTANCE_Y(a, b) (N64_TO_IOS_Y(a) - N64_TO_IOS_Y(b))

enum ControlElementType {
    Joystick,
    Button
};

struct Position {
    s32 x,y;
};

struct ControlElement {
    char *name;
    enum ControlElementType type;
    int touchID;
    //Joystick
    int joyX, joyY;
    //Button
    int buttonID;
    bool menuButton;
    int slideTouch;
    OverlayImageView *imageView;
};

#include "controller_touchscreen_layouts.inc"

static struct ControlElement *ControlElements = ControlElementsDefault;
static int ControlElementsLength = sizeof(ControlElementsDefault)/sizeof(struct ControlElement);

int joystick_size = 128;

static HapticsController *haptics = nil;
static uint64_t ios_controller_touch_down_count;
static uint64_t ios_controller_touch_read_count;
#ifdef IOS_HAT_MOVEMENT
static bool hat_tap_pending;
#endif

#define TRIGGER_DETECT(size) (((pos.x + size > event->x) && (pos.x < event->x)) && ((pos.y + size > event->y) && (pos.y < event->y)))

#define BUTTON_IMAGE_LIGHT [UIImage imageNamed:@"touch_button"]
#define BUTTON_IMAGE_DARK [UIImage imageNamed:@"touch_button_dark"]

void touch_down(struct TouchEvent* event) {
    uint64_t sequence = ++ios_controller_touch_down_count;
    int matched_index = -1;
    CGPoint matched_position = CGPointZero;
    CGRect matched_frame = CGRectZero;
    char candidate_summary[900] = { 0 };
    size_t candidate_summary_length = 0;
    CGRect frame;
    for(int i = 0; i < ControlElementsLength; i++) {
        if (ControlElements[i].touchID == 0 && ControlElements[i].imageView != nil) {
            frame = ControlElements[i].imageView.frame;
            CGFloat scale = ((float)configTouchUiScale/100.0);
            // frame.origin is in the superview's coordinates. Convert local zero
            // from the image view itself so nested controls and transforms use
            // the same coordinate space as GestureRecognizer's point values.
            CGPoint pos = [ControlElements[i].imageView convertPoint:CGPointZero toView:tcvc.view];
            if (sequence <= 12 && candidate_summary_length < sizeof(candidate_summary)) {
                int written = snprintf(candidate_summary + candidate_summary_length,
                                       sizeof(candidate_summary) - candidate_summary_length,
                                       "%s%s@%.0f,%.0f/%.0fx%.0f",
                                       candidate_summary_length ? ";" : "",
                                       ControlElements[i].name ? ControlElements[i].name : "?",
                                       pos.x, pos.y, frame.size.width * scale, frame.size.height * scale);
                if (written > 0) candidate_summary_length += (size_t)written;
            }
            if (TRIGGER_DETECT(frame.size.width * scale)) {
                matched_index = i;
                matched_position = pos;
                matched_frame = frame;
                switch (ControlElements[i].type) {
                    case Joystick:
                        joystick_size = frame.size.width*scale;
                        ControlElements[i].touchID = event->touchID;
                        ControlElements[i].joyX = event->x - pos.x + joystick_size/2 - joystick_size;
                        ControlElements[i].joyY = event->y - pos.y + joystick_size/2 - joystick_size;
                        break;
                    case Button:
                        ControlElements[i].touchID = event->touchID;
                        ControlElements[i].imageView.image = BUTTON_IMAGE_DARK;
#ifdef IOS_HAT_MOVEMENT
                        if (ControlElements[i].buttonID == HAT_BUTTON) hat_tap_pending = true;
#endif
                        break;
                }
            }
        }
    }
    if (sequence <= 12 || sequence % 120 == 0) {
        if (matched_index >= 0)
            gfx_ios_diag_log("Virtual control hit seq=%llu touch=%d control=%s buttonID=%d menu=%d point=(%.1f,%.1f) controlOrigin=(%.1f,%.1f) frameSize=(%.1fx%.1f) touchID=%d candidates={%s}", (unsigned long long)sequence, event->touchID, ControlElements[matched_index].name, ControlElements[matched_index].buttonID, ControlElements[matched_index].menuButton, event->x, event->y, matched_position.x, matched_position.y, matched_frame.size.width, matched_frame.size.height, ControlElements[matched_index].touchID, candidate_summary);
        else
            gfx_ios_diag_log("Virtual control hit-test miss seq=%llu touch=%d point=(%.1f,%.1f) touchViewBounds=(%.1f,%.1f %.1fx%.1f) controls=%d scale=%.2f candidates={%s}", (unsigned long long)sequence, event->touchID, event->x, event->y, tcvc.view.bounds.origin.x, tcvc.view.bounds.origin.y, tcvc.view.bounds.size.width, tcvc.view.bounds.size.height, ControlElementsLength, (float)configTouchUiScale / 100.0f, candidate_summary);
    }
    set_current_input(Touch);
}

void touch_motion(struct TouchEvent* event) {
    CGRect frame;
    for(int i = 0; i < ControlElementsLength; i++) {
#ifdef IOS_HAT_MOVEMENT
        // Jump and attack drags across the hat control do not throw.
        if (ControlElements[i].buttonID == HAT_BUTTON) continue;
#endif
        frame = ControlElements[i].imageView.frame;
        CGFloat scale = ((float)configTouchUiScale/100.0);
        CGPoint pos = [ControlElements[i].imageView convertPoint:CGPointZero toView:tcvc.view];
        if (ControlElements[i].touchID == event->touchID) {
            switch (ControlElements[i].type) {
                case Joystick:
                    ; //workaround
                    s32 x,y;
                    x = event->x - pos.x + joystick_size/2 - joystick_size;
                    y = event->y - pos.y + joystick_size/2 - joystick_size;
                    if (event->x > pos.x + joystick_size)
                        x = joystick_size/2;
                    if (event->x < pos.x)
                        x = -joystick_size/2;
                    if (event->y > pos.y + joystick_size)
                        y = joystick_size/2;
                    if (event->y < pos.y)
                        y = -joystick_size/2;

                    ControlElements[i].joyX = x;
                    ControlElements[i].joyY = y;
                    break;
                case Button:
                    if (ControlElements[i].slideTouch && !TRIGGER_DETECT(frame.size.width * scale)) {
                        ControlElements[i].slideTouch = 0;
                        ControlElements[i].touchID = 0;
                        ControlElements[i].imageView.image = BUTTON_IMAGE_LIGHT;
                    }
                    break;
            }
        } else {
            switch (ControlElements[i].type) {
                case Joystick:
                    break;
                case Button:
                    if (TRIGGER_DETECT(frame.size.width * scale)) {
                        ControlElements[i].slideTouch = 1;
                        ControlElements[i].touchID = event->touchID;
                        ControlElements[i].imageView.image = BUTTON_IMAGE_DARK;
                    }
                    break;
            }
        }
    }
}

static void handle_touch_up(int i) {//seperated for when the layout changes
    ControlElements[i].touchID = 0;
    switch (ControlElements[i].type) {
        case Joystick:
            ControlElements[i].joyX = 0;
            ControlElements[i].joyY = 0;
            break;
        case Button:
            ControlElements[i].imageView.image = BUTTON_IMAGE_LIGHT;
            break;
    }
}

void touch_up(struct TouchEvent* event) {
    for(int i = 0; i < ControlElementsLength; i++) {
        if (ControlElements[i].touchID == event->touchID)
            handle_touch_up(i);
    }
}

void update_touch_controls(void) {
    struct Position pos;
    for (int i = 0; i < ControlElementsLength; i++) {
        switch (ControlElements[i].type) {
            case Joystick:
                ;
                CGRect bounds = ControlElements[i].imageView.subimage.bounds;
                CGFloat scale = ((float)configTouchUiScale/100.0);
                bounds.origin.x = (ControlElements[i].joyX + joystick_size/4)/scale;
                bounds.origin.y = (ControlElements[i].joyY + joystick_size/4)/scale;
                ControlElements[i].imageView.subimage.frame = bounds;
                break;
            case Button:
                ;
                break;
        }
    }
}

void render_touch_controls(void) {
    [tcvc setTouchControlsHidden:NO];
    if((get_current_input() != Touch || configTouchMode == 1) && configTouchMode != 0) {
        [tcvc setTouchControlsHidden:YES];
        return;
    }
    
    //update_touch_controls();
}

static void touchscreen_init(void) {
    if(hapticsSupported) {
        haptics = [[HapticsController alloc] init];
    }
    [frameController.onScreenRefresh addObject:[NSValue valueWithPointer:update_touch_controls]];
    
    joystick_size = 128 * ((float)configTouchUiScale/100.0);
}

void touchscreen_set_imageviews(NSMutableArray *imageViews) {
    for(int i = 0; i < imageViews.count; i++) {
        for(int j = 0; j < ControlElementsLength; j++) {
            if(((OverlayImageView *)imageViews[i]).buttonTag == ControlElements[j].buttonID) {
                ControlElements[j].imageView = imageViews[i];
            }
        }
    }
}

static void touchscreen_read(OSContPad *pad) {
#ifdef IOS_HAT_MOVEMENT
    if (hat_tap_pending) {
        pad->button |= HAT_BUTTON;
        hat_tap_pending = false;
    }
#endif
    uint64_t read_sequence = ++ios_controller_touch_read_count;
    int active_controls = 0;
    s32 joystick_touch = FALSE;
    s32 joystick_pixels_x = 0;
    s32 joystick_pixels_y = 0;
    s32 joystick_axis_x = 0;
    s32 joystick_axis_y = 0;
    for(int i = 0; i < ControlElementsLength; i++) {
        if (ControlElements[i].touchID != 0) ++active_controls;
        switch (ControlElements[i].type) {
            case Joystick:
                joystick_touch = ControlElements[i].touchID != 0;
                joystick_pixels_x = ControlElements[i].joyX;
                joystick_pixels_y = ControlElements[i].joyY;
                if (joystick_touch) {
                    joystick_axis_x = touchscreen_axis_from_delta(joystick_pixels_x, joystick_size, FALSE);
                    joystick_axis_y = touchscreen_axis_from_delta(joystick_pixels_y, joystick_size, TRUE);
                    pad->stick_x = (s8)joystick_axis_x;
                    pad->stick_y = (s8)joystick_axis_y;
                }
                break;
            case Button:
                if (ControlElements[i].touchID) {
                    if(ControlElements[i].menuButton) {
                        if (menu_button_pressed) {
                            if (read_sequence <= 12 || read_sequence % 120 == 0)
                                gfx_ios_diag_log("Virtual MENU input dispatched read=%llu touch=%d callbackInstalled=%d", (unsigned long long)read_sequence, ControlElements[i].touchID, menu_button_pressed != NULL);
                            (*menu_button_pressed)();
                        } else if (read_sequence <= 12) {
                            gfx_ios_diag_log("Virtual MENU input has active touch=%d but menu callback is nil", ControlElements[i].touchID);
                        }
                    } else {
                        pad->button |= ControlElements[i].buttonID;
                    }
                }
                break;
        }
    }
    if (read_sequence <= 3 || read_sequence % 120 == 0)
        gfx_ios_diag_log("Touch controller read heartbeat read=%llu activeControls=%d padButtons=0x%04x stick=(%d,%d) joystickTouch=%d screenDelta=(%d,%d) mappedN64=(%d,%d) inputSource=%d", (unsigned long long)read_sequence, active_controls, pad->button, pad->stick_x, pad->stick_y, joystick_touch, joystick_pixels_x, joystick_pixels_y, joystick_axis_x, joystick_axis_y, get_current_input());
}

static u32 touchscreen_rawkey(void) { //dunno what this does but I'll skip it for now
    return VK_INVALID;
}

static void touchscreen_rumble_play(float strength, float time) {
    if(configHaptics && hapticsSupported) {
        [haptics rumble:strength duration:(time * 1000.0)];
    }
}

static void touchscreen_rumble_stop() {
    if(configHaptics && hapticsSupported) {
        [haptics rumble:0.0 duration:0.0];
    }
}

static void touchscreen_shutdown(void) {
    if(hapticsSupported) {
        [haptics cleanup];
        [haptics release];
    }
}

struct ControllerAPI controller_touchscreen = {
    0,
    touchscreen_init,
    touchscreen_read,
    touchscreen_rawkey,
    touchscreen_rumble_play,
    touchscreen_rumble_stop,
    NULL,
    touchscreen_shutdown
};
#endif
