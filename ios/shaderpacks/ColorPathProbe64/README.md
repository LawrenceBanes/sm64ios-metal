# Color Path Probe 64

This temporary diagnostic divides the rendered scene into three vertical bands:

- Left: raw input color.
- Center: a hypothetical `pow(color, 2.2)` conversion.
- Right: a hypothetical `pow(color, 1/2.2)` conversion.

Magenta lines mark the boundaries. The two power curves are comparison probes,
not a claim that either is the right transfer function for every source. Compare
this pack with Classic64 on the same scene and view. Restore Classic64 or
LumenSurface64 after capturing the probe.
