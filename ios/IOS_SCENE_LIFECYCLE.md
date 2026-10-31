# iOS scene lifecycle investigation

## Findings

The previously built app's `Info.plist` did not contain `UIApplicationSceneManifest`. The Xcode target supplied a main storyboard and landscape orientation keys, but neither item adopts the UIKit scene lifecycle.

The pinned SDL2 revision is `5d249570393f7a37e037abf22cd6012a4cc56a71` (2.32.10). Its `src/main/uikit/SDL_uikit_main.c` calls `SDL_UIKitRunApp`; `src/video/uikit/SDL_uikitappdelegate.m` then calls `UIApplicationMain` with `SDLUIKitDelegate`. That class conforms to `UIApplicationDelegate`, but this revision has no `UISceneDelegate`, scene manifest, or `application:configurationForConnectingSceneSession:options:` implementation. Its `src/video/uikit/SDL_uikitwindow.m` creates the primary window with `initWithFrame:` instead of associating it with a `UIWindowScene`.

The device logs later confirmed a second ordering bug after scene adoption: SDL2's app delegate calls `postFinishLaunch` on the main run loop, and that method synchronously invokes `forward_main` (`SDL_main`) from `application:didFinishLaunchingWithOptions:`. SM64 performs filesystem, audio, and game setup before creating its SDL window. That startup work kept the main run loop busy, so UIKit could not deliver `scene:willConnectToSession:` until after SDL had already tried to create its window. The scene notification arrived about 2.5 seconds after SDL's failed window request. This explains why a scene manifest alone did not fix startup ordering.

Apple's [scene migration guide](https://developer.apple.com/documentation/uikit/transitioning-to-the-uikit-scene-based-life-cycle) requires an `UIApplicationSceneManifest` with a scene configuration, or a dynamic configuration method on the app delegate. Apple's [iOS 27 migration note](https://developer.apple.com/documentation/technotes/tn3187-migrating-to-the-uikit-scene-based-life-cycle) says apps built with the latest SDK must adopt scenes to launch.

## Implementation

- `sm64ios/sm64ios/Info.plist` declares one `UIWindowSceneSessionRoleApplication` configuration with `SM64SceneDelegate`.
- `gfx_uikit.m` implements the scene delegate and a resolver for the connected application scene on the main display. After storing the connected scene, it posts `SM64WindowSceneDidConnectNotification`.
- The pinned SDL app delegate installs a notification observer before scheduling its initial launch callback. If the scene is not connected, the callback returns immediately to UIKit's run loop. The scene notification schedules `SDL_main` on a later main-loop turn, after UIKit has delivered the scene connection. A guard prevents duplicate SDL entry. No semaphore, sleep, or synchronous wait is used.
- `ios/patches/SDL2-2.32.10-uikit-scenes.patch` is applied to the verified SDL2 source checkout during the pinned SDL build. The primary SDL UIKit window is constructed with `initWithWindowScene:`. The existing frame-based path remains for non-primary displays.
- SDL's existing UIKit window remains the `UIApplicationDelegate.window` returned to `gfx_uikit_init`, so touch controls and game view setup continue through the current host code. Landscape constraints and the Metal view creation path are unchanged.
- After `gfx_init`, the app checks that SDL created a visible window in the active application scene and that the actual game `MTKView` has a nonzero drawable. SDL initialization errors or zero dimensions stop startup before timers/game frames begin. The game view remains a subview of SDL's scene-backed controller; the separate diagnostic color probe is not used by game rendering or readiness checks.
- Metal records its device, queue, pixel formats, dimensions, frame encoding, `presentDrawable`, GPU completion, and the drawable's Core Animation presentation callback. SDL's existing background/foreground pause behavior is retained; resize and foreground events re-layout the Metal view and revalidate drawable dimensions.
- SDL 2.32.10 already observes UIApplication lifecycle notifications in `SDL_uikitevents.m` for pause/resume and termination; the scene addition does not replace those event handlers.
- Linux CI verifies the plist configuration, scene delegate source, bridge patch application, and existing host hooks. IPA validation checks the generated manifest and confirms the delegate class name is present in the compiled executable.

## Remaining validation

Linux checks cover the deterministic launch/scene callback orderings, SDL failure gates, and scene-backed Metal readiness checks. Full Xcode 27.1 arm64 IPA builds and validation passed in Actions runs `41155642876` and `41159410954`. Install and launch the latest replacement on the iPhone 14 Pro Max running iOS 27.0; inspect `SM64Diagnostics.log` for `scene connected` before `SDL_CreateWindow`, a ready game MTKView with nonzero drawable size, and `First SM64 game drawable presented by CoreAnimation`. Device evidence is required before claiming visible gameplay; the independent magenta probe is not a substitute.
