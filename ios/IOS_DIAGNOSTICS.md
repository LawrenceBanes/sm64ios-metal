# iOS startup and video diagnostics

The iOS app writes timestamped checkpoints to `Documents/SM64Diagnostics.log` and flushes each line. The app already exposes its Documents folder to Files through `UIFileSharingEnabled` and `LSSupportsOpeningDocumentsInPlace`.

## Get the log from iPhone

In the Files app, open **Browse → On My iPhone → Super Mario 64** and copy or share `SM64Diagnostics.log`. If that folder is not shown, open SM64 once and return to Files. The native **Share log** button in the diagnostic panel sends the same file through the iOS share sheet.

The compact native diagnostics panel appears at the upper left while the app starts. It updates independently of SDL and the game renderer. Tap **Hide** to collapse it to a small **Diagnostics** button; tap that button to reopen it. The panel accepts touches only over itself so game controls remain usable elsewhere.

Tap **Metal color test** to temporarily cover the game with a bright magenta color rendered by a separate native `MTKView`. This test does not use SDL, the SM64 `GfxRenderingAPI`, game shaders, or game geometry. Tap **Close color test** to return to the game. The log records drawable availability, command submission, and GPU completion for the test.

## Reading the checkpoints

- No scene or diagnostic-window entries: UIKit scene setup or scene delegate activation stopped before this app-level diagnostic path.
- Scene entries but no `SDL_main entered`: SDL's UIKit bootstrap did not enter the game entry point.
- `SDL_Init` or `SDL_CreateWindow` reports an error, or the SDL window is null: SDL video/window initialization failed.
- SDL window records show a different scene, zero dimensions, hidden state, or an unexpected root controller: the SDL UIKit window is not attached or presented as expected. The log also records window levels, sizes, visibility, controller hierarchy, and subview order.
- Backend is not `RAPI_METAL`: the build selected a different graphics renderer. The current iOS configuration should report `RAPI_METAL` and SDL2/UIKit as the window host.
- Metal device, queue, MTKView, shader library, or render-pipeline errors: renderer initialization or SM64 shader setup failed at that stage.
- Repeated `Metal frame drawable unavailable`: the game MTKView has no current drawable/pass. Check the recorded view size, scene, and visibility fields.
- No `Metal presentDrawable submitted` entry: the game frame did not reach Metal's present call. Earlier game-frame checkpoints show how far scheduling, game logic, and rendering got.
- `Metal first game command buffer completed` without `First SM64 game drawable presented by CoreAnimation`: GPU work completed, but the system has not confirmed display presentation for the game drawable.
- Game command buffers complete but the screen stays black: GPU completion confirms command execution, not visible pixels. Compare against the independent magenta test. If magenta appears, UIKit and a basic Metal present work; investigate SM64 geometry, shader/pipeline state, textures, viewport/scissor, or composition next. If the magenta test also fails, its own drawable and GPU errors narrow the issue to the native Metal view/presentation path.

These checkpoints identify where the pipeline stops; they do not claim that any GPU submission produced visible pixels. A real iPhone check remains necessary.
