#ifndef CONTROLLER_TOUCH_AXIS_H
#define CONTROLLER_TOUCH_AXIS_H

#include <PR/ultratypes.h>

/*
 * Convert a centered UIKit joystick displacement into the signed N64 stick
 * convention. Screen X grows right, screen Y grows down; N64 Y grows up.
 * The result is computed directly in the signed domain (not via 0..255 and
 * implementation-defined narrowing to s8).
 */
static inline s8 touchscreen_axis_from_delta(s32 screenDelta, s32 axisSize, s32 invert) {
    s32 halfSize;
    s32 value;

    if (axisSize <= 0) return 0;
    halfSize = axisSize / 2;
    if (halfSize <= 0) return 0;
    if (invert) screenDelta = -screenDelta;

    if (screenDelta >= 0) {
        value = (screenDelta * 127) / halfSize;
        return (s8)(value > 127 ? 127 : value);
    }

    value = (screenDelta * 128) / halfSize;
    return (s8)(value < -128 ? -128 : value);
}

#endif
