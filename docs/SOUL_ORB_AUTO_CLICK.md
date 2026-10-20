# Soul-orb-only Auto Click and Windows fallback

Open Auto Click in the Automation sidebar group. Select the Regular view or
Vindicta Ultimate profile, enable Auto click and choose Target → Soul orbs.
Activation defaults to Automatic: close the menu and place the crosshair on
a visible, claimable orb. No side button is needed. Choose Hold side button
if you prefer held activation. Camera focus can remain off; its Players,
Minions and Soul orbs switches do not affect Auto Click. Both profiles save
their settings independently.

Enemy-player clicks require either side button. Both modes use MAKCU when a device
is connected and Windows input otherwise. Auto Parry and all camera movement
use the same automatic fallback. Manual left mouse takes priority over Auto
click. Game focus, a living local hero, fresh data and map visibility are required.

## Orb state

Build 6793's CItemXP exposes launch time, attackable start/end and launch number
through schema. Its client hit-prediction function also checks an already-hit
latch before allowing another predicted hit. The resolver now witnesses that
function and extracts the latch offset instead of relying on an unchecked
private offset. Updating schema and reader profiles includes this contract.

The reader compares these values with the verified game-time clock, checks
coherence during each sample and rejects missing, stale, paused or invalid
state. Click input expires before the attackable window closes. Health is not
used as an orb-liveness test: the sandbox capture confirmed zero-health orbs
through their valid and protected phases. Entity handles may stay allocated
and be relaunched, so launch changes are checked too.

Orb clicking intersects a conservative sphere inside the current collision
bounds. It uses no prediction, activation padding or nearest-edge fallback.
Known player/minion collision geometry in front and static map cover block the
shot. Protected, expired and already-hit orbs never authorize a click.

The live sandbox generator measured a 0.65 s initial protection period and a
0.70 s attackable window in this session. These durations are observations,
not constants used by the reader or controller.

## Input ownership and diagnostics

MAKCU retains its existing V4.094 km.move, km.click and km.press transport.
Windows uses relative mouse motion, complete left-button pulses and scan-code
keyboard pulses. A separate release worker prevents a rendering stall or COM
discovery wait from leaving an accepted Windows input down. Backend handoff
does not replay a queued action through the other backend.

Connection identifies the active backend. With diagnostics enabled, makcu.json
includes Windows movement, click, parry, release and error counts. auto-click.json
includes orb-only mode, whether a side button is required, the selected intersection and the current pause reason.
The read-only --probe path includes orb state and projected orb-ray queries;
it never starts the input worker or opens a serial port.
