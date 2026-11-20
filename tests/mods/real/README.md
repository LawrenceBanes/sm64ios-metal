# Real CoopDX compatibility fixture: SM64 Coop Achievements 2.1.0

`sm64coop-achievements-2.1.0.zip` is the original, unmodified release archive
from <https://github.com/gg21kiy/sm64coop-achievements/releases/tag/2.1.0>.
SHA-256: `7001744cb4023be31460960a0e4ff060f4311f573ccdac880564deb532617a3d`.

The repository identifies the code as MIT-licensed and states that the license
covers code only; game assets and trademarks are excluded. The release ZIP
contains nine Lua scripts and documentation only (no bundled images, custom
models, or audio). The corresponding license is preserved in
`SM64COOP-ACHIEVEMENTS-LICENSE.txt`.

The mod has no external Lua-module dependency. It expects CoopDX APIs for
save-file and level state, Mario update hooks, mod storage, menu controls,
texture lookup, sound playback, chat messages, pause-state and mouse input,
and DJUI HUD rendering. The port currently supports the state/hook/storage
path and three menu-control types; the visual, sound, and pause/mouse UI path
is unsupported. This makes the archive a useful partial compatibility test,
not a fully working port of the mod.

The host fixture imports these exact ZIP bytes through the production ZIP
extractor, then discovers and executes the extracted Lua scripts. It verifies
startup, 11 registered menu controls, a menu slider callback, repeated
`HOOK_MARIO_UPDATE` dispatch, persisted “First Star!” progress, disable and
re-enable, and reload persistence. Its unsupported sound and HUD callbacks log
the missing APIs and are disabled independently while the mod remains enabled.
The host test supplies the Mario state and dispatches callbacks directly; it is
not an iPhone or live gameplay test.

The archive and scripts are never rewritten. SHA-256 is checked by
`tests/mods/run_host_tests.sh` before the fixture runs.
