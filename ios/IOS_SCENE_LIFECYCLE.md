# iOS scene lifecycle investigation

## Findings

The previously built app's `Info.plist` did not contain `UIApplicationSceneManifest`. The Xcode target supplied a main storyboard and landscape orientation keys, but neither item adopts the UIKit scene lifecycle.

The pinned SDL2 revision is `5d249570393f7a37e037abf22cd6012a4cc56a71` (2.32.10). Its `src/main/uikit/SDL_uikit_main.c` calls `SDL_UIKitRunApp`; `src/video/uikit/SDL_uikitappdelegate.m` then calls `UIApplicationMain` with `SDLUIKitDelegate`. That class conforms to `UIApplicationDelegate`, but this revision has no `UISceneDelegate`, scene manifest, or `application:configurationForConnectingSceneSession:options:` implementation. Its `src/video/uikit/SDL_uikitwindow.m` creates the primary window with `initWithFrame:` instead of associating it with a `UIWindowScene`.

Those facts make legacy lifecycle adoption a strong match for the crash timing and UIKit stack, before game initialization. They do not prove that it is the only cause; the rebuilt IPA and an iOS 27 device launch are needed to confirm.

Apple's [scene migration guide](https://developer.apple.com/documentation/uikit/transitioning-to-the-uikit-scene-based-life-cycle) requires an `UIApplicationSceneManifest` with a scene configuration, or a dynamic configuration method on the app delegate. Apple's [iOS 27 migration note](https://developer.apple.com/documentation/technotes/tn3187-migrating-to-the-uikit-scene-based-life-cycle) says apps built with the latest SDK must adopt scenes to launch.

## Implementation

- `sm64ios/sm64ios/Info.plist` declares one `UIWindowSceneSessionRoleApplication` configuration with `SM64SceneDelegate`.
- `gfx_uikit.m` implements the scene delegate and a resolver for the connected application scene on the main display.
- `ios/patches/SDL2-2.32.10-uikit-scenes.patch` is applied to the verified SDL2 source checkout during the pinned SDL build. The primary SDL UIKit window is constructed with `initWithWindowScene:`. The existing frame-based path remains for non-primary displays.
- SDL's existing UIKit window remains the `UIApplicationDelegate.window` returned to `gfx_uikit_init`, so touch controls and game view setup continue through the current host code. Landscape constraints and the Metal view creation path are unchanged.
- SDL 2.32.10 already observes UIApplication lifecycle notifications in `SDL_uikitevents.m` for pause/resume and termination; the scene addition does not replace those event handlers.
- Linux CI verifies the plist configuration, scene delegate source, bridge patch application, and existing host hooks. IPA validation checks the generated manifest and confirms the delegate class name is present in the compiled executable.

## Remaining validation

The full macOS build and stricter scene-aware IPA validation passed in run `41137642774`; this establishes compile/link and packaging correctness only. Install and launch the replacement IPA on the iPhone 14 Pro Max running iOS 27.0 to confirm the crash is resolved, the SDL scene-backed window appears, and the Metal view reaches its first presented frame. Also verify touch controls and landscape orientation.
