# SHADER64 color pipeline findings

## Recording evidence

The supplied iPhone recording shows the same Graphics screen with the Classic
preset selected while the active shader pack changes from Classic64 to
LumenSurface64. The outdoor scene becomes globally much darker after selecting
Lumen. Later, the user raises **Ambient contribution** to its maximum; the
scene returns to a bright appearance while **World shadow strength** is also
raised. This points to Lumen's ambient material multiplier as the demonstrated
darkening source. It does not prove gamma behavior by itself.

## Source audit

- The drawable is `BGRA8Unorm`, not `BGRA8Unorm_sRGB`; Metal therefore does not
  automatically apply an sRGB render-target transfer at presentation.
- The SHADER64 graph intermediate is `RGBA16Float`.
- Lumen's final `present` shader samples and returns its input unchanged.
- Fast3D's N64 combiner and native fog produce the base color before the
  optional Lumen material hook. Applying a second fixed blue fog in the Lumen
  graph duplicated scene fog.
- Lumen's former defaults multiplied material color by a 0.22 ambient floor,
  added direct light, blended a checker into Mario, then darkened the graph by
  shadow visibility. Its half-resolution graph also discarded scene samples.

The code-level evidence does not support a double-gamma diagnosis. A full
hardware capture is still needed to establish every attachment's transfer
function and the numerical values entering/exiting each stage.

## Corrected defaults

LumenSurface64 0.3.0 now defaults to exact base-color preservation: direct
lighting, material texture mixing, and world shadowing are opt-in; ambient is
1.0. Its graph runs at full resolution and no longer applies another fog blend.
This is a conservative color/exposure baseline, not a claim that the new
lighting looks correct on-device.

## On-device diagnostic procedure

1. Use one stationary outdoor view. Compare Classic64 and LumenSurface64 with
   all four Lumen controls at their defaults.
2. Install ColorPathProbe64. Its left, center, and right bands show raw scene
   color, a `pow(color, 2.2)` comparison, and a `pow(color, 1/2.2)` comparison;
   magenta separators identify the bands. This is a visual diagnostic, not a
   prescribed correction.
3. Return to LumenSurface64 and change one control at a time: first direct
   light with ambient held at 1, then shadow strength, then Mario texture mix.
4. Capture the same scene and camera angle for each setting. Check whether the
   raw ColorPathProbe64 band matches Classic64 in brightness and color.

Host tests verify the shader defaults, identity branches, pixel-format
configuration, and diagnostic shader structure. They do not validate GPU
pixels, physical-device color management, or lighting quality.
