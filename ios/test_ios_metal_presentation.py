#!/usr/bin/env python3
"""Source-level regression checks for the custom CAMetalLayer presentation path."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
METAL = (ROOT / "src/pc/gfx/gfx_metal.m").read_text()


def require(needle: str, description: str) -> None:
    if needle not in METAL:
        raise SystemExit(f"Metal presentation regression missing {description}: {needle}")


def main() -> None:
    require("[metal_layer nextDrawable]", "a fresh layer drawable for each custom game frame")
    if "metal_view.currentDrawable" in METAL or "metal_view.currentRenderPassDescriptor" in METAL:
        raise SystemExit("custom render loop must not use MTKView's draw-callback-scoped drawable")
    require("pass.colorAttachments[0].texture = metal_drawable.texture", "render pass targets the acquired layer drawable")
    require("pass.depthAttachment.texture = depth_texture", "custom render pass has a depth attachment")
    require("MTLPixelFormatDepth32Float", "depth format remains aligned with the game pipeline")
    require("metal_depth_width == width && metal_depth_height == height", "depth texture is reused only at matching dimensions")
    require("metal_depth_texture release", "depth texture is released when its size changes and on shutdown")
    require("drawableObject=%p", "drawable wrapper identity is captured at acquisition")
    require("drawableTexture=%p", "underlying drawable texture identity is captured at acquisition")
    require("identityMatches=%d", "Core Animation callback can be matched to its submitted wrapper")
    require("textureIdentityMatches=%d", "Core Animation callback can be matched to the submitted backing texture")
    require("repeatedTextureImmediately=%d", "consecutive backing texture reuse is distinguished from wrapper reuse")
    require("callback_drawable = (id<CAMetalDrawable>)presented_drawable", "presentation callback restores the CAMetalDrawable texture interface explicitly")
    require("@synchronized ([NSObject class])", "callback and acquisition counters are synchronized")
    require("++presentation_callback_count", "every presented-handler invocation increments a callback counter")
    require("++displayed_drawable_count", "nonzero presentedTime is counted separately from callbacks")
    require("presented_time > 0.0", "a callback is not mistaken for proof that pixels reached display")
    require("[metal_command addScheduledHandler:", "command scheduling is independently observed")
    require("[metal_command addCompletedHandler:", "GPU completion is independently observed")
    require("[metal_command presentDrawable:submitted_drawable]", "the same submitted layer drawable is explicitly presented")
    if not (METAL.index("addPresentedHandler") < METAL.index("presentDrawable:submitted_drawable") < METAL.index("[metal_command commit]")):
        raise SystemExit("presentation callback and drawable presentation must be registered before command commit")
    print("CAMetalLayer fresh drawable, depth target, and presentation callback checks passed")


if __name__ == "__main__":
    main()
