# MAKCU mouse-input backend

Changed 4 October 2026.

Camera corrections now use `km.move(dx,dy)\r\n` through the MAKCU CH343 management serial port. There is no SendInput/mouse_event fallback. The physical side buttons are still observed through Windows button state, and the existing replay/practice permissions, selected bone, prediction, speed, foreground, freshness and visibility checks remain in effect.

The MAKCU MCP supplied the movement command, version handshake, serial settings, command limits and firmware differences. Read-only probing found this device on COM3 at 4,000,000 baud. Binary firmware-version query returned 4073 (V4.073). No firmware update or persistent device-setting change was performed.

## Connection and timing

- Discover present `VID_1A86&PID_55D3` CH343 COM ports. Optional `MAKCU_PORT=COM3` environment override selects a particular port.
- Open exclusively with 8N1 and no hardware/software flow control. Probe 4,000,000 and 115,200 separately; no automatic baud-change command is sent.
- Require a valid MAKCU identity result line and consume its prompt. An echoed `km.version()` is insufficient. Disable command echo for this session after identification.
- One worker owns serial reads/writes; the render loop does not open ports or wait for serial I/O. Writes have an eight-millisecond timeout. Periodic version queries detect a lost/unresponsive connection; retry discovery after disconnect.
- Keep at most one queued correction. A newer update cancels/replaces the old one; stale corrections over 20 ms are discarded. Check foreground and held side button again before writing.
- Menu, focus release, disabled feature, occlusion and invalid/stale target data cancel queued corrections through the camera update. Shutdown stops/joins the serial worker before console cleanup completes.

A command already handed to the serial driver/device cannot be recalled. Firmware may also interpolate movement; this backend does not alter interpolation settings or claim a guaranteed lower end-to-end latency than SendInput. The queue count measures submitted commands; `makcu.json` separately reports commands actually written, failures and replacements. Successful serial writes do not prove that every HID report reached the game.

## Free movement

Free movement is available again through a different control rule: find the screen center's closest point on the projected head–body–pelvis polyline and correct only the remaining error. Moving along the line does not move a separately accumulated raw-input parameter. The endpoints clamp movement, and perspective-correct interpolation recovers the corresponding world position for visibility and prediction. Soul orbs use their center; minions without human bone names use their collision center.

The previous raw-Y implementation was disabled because MAKCU V4 lacks the old `km.axis` text stream. Hardware-generated corrections cannot carry a Windows SendInput tag, so that implementation would feed our own corrections back into manual movement. The current geometry rule does not need that stream or the firmware history API. Selected-bone focus remains available by turning Free movement off.

## Diagnostics and checks

`DeadlockOverlay.exe --makcu-probe` verifies connection and several heartbeat replies, writes `makcu-probe.json`, and exits without movement commands. Normal `--diagnostics` writes `makcu.json` once per reporting interval. The GUI connection card displays serial status.

Protocol self-checks cover signed movement limits, zero/out-of-range commands, echoed-query rejection, identity/noise parsing and stale/backward timestamps. Renderer, console-shutdown and resource checks remain part of `build.ps1`. Hardware connection was verified on this device; actual in-game movement requires the side-button test with the new executable.

References: [MAKCU API](https://makcu.com/en/api/), [firmware capability matrix](https://makcu.com/api/versions), and [official SDK protocol](https://github.com/terrafirma2021/mak-suite/tree/main/protocol). Documentation differs across firmware generations; the connection here is confirmed V4.073, not a claim that an arbitrary V3/V4 device has identical telemetry.

Live verification: the updated executable connected on COM3 at 4,000,000 baud and emitted focus corrections in practice mode. The user confirmed the movement feels good. The saved capture contains two recovered transport/protocol failures, so this is not a claim of zero errors. The diagnostic write_failures counter currently includes rejected protocol responses as well as failed writes. Evidence is in verification/makcu-build.

