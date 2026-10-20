# Auto Parry

Auto Parry is a separate sidebar page. It starts **disabled** and uses the
existing MAKCU V4 transport when connected, otherwise Windows input, to press
the configured keyboard parry key. The default is `F`; this must match the game binding.

## First sandbox check

1. Open **Auto Parry** and enable it. No activation button needs to be held.
2. Leave **Observation only** off to send inputs. Turn it on to inspect threats
   without pressing the parry key.
3. Close the menu with Insert and let an enemy
   bot heavy-melee you. The HUD reports the current gate, detected threat and
   cooldown. Menu opening, focus loss, minimization, stale reads and disconnects
   discard pending input.

**Parry before predicted contact** adjusts the trajectory look-ahead from
30–300 ms, initially 120 ms. It is a contact estimate, not a guaranteed time
until the game resolves damage. **Status HUD** and **Threat marker** are
independently optional. Settings persist in the existing settings file.

## Coverage

- When an enemy exposes its standard HoldMelee ability, the reader uses its
  state, attack type, attack definition, range/angle, bonus range and parry
  pierce. Merely charging or standing nearby is insufficient.
- Remote players can hide that ability. Their replicated heavy-melee mask is
  used only after the reader has observed the locked wind-up and its transition
  into the dash. Starting midway through an attack waits for the next wind-up
  because dash and follow-through share a mask. This fallback detects heavy
  attacks only; the Light melee toggle does not manufacture missing light
  attack timing. Baba's special bench form is excluded from this fallback.
- The standard fallback reads the base range/angle and parryable flag from the
  live common melee definition, then applies the remote pawn's readable bonus
  range. It never borrows the local player's item-modified cached range.
  Range and angle resolve by name from the loaded VData property dictionary;
  an unbound local property cache does not block detection. Definition addresses
  are cached until the VData or dictionary changes, while base values and attack
  definitions are read and validated with each sample.
- Optional **Counterspell projectiles** checks explicitly identified Charged
  Shot and Bebop Hook projectiles when the item is owned and available. Other
  projectile classes, hitscan spells, damage-over-time ticks and arbitrary area
  effects are skipped. Projectile handling still needs a separate live test.

## Geometry and input

Melee eligibility checks a cone against the local player's current collision
hull, accounting for measured attacker and defender motion. The attacker eye
origin is approximated from its head bone. Both current and predicted paths
must pass the installed static-map visibility mesh. Dynamic cover and exact
server collision timing remain limitations.

Projectile checks sweep an estimated projectile radius against that hull using
relative velocity. Box expansion is conservative. Camera-focus padding and
predicted focus regions never authorize a parry or click.

The controller checks the local parry cooldown, active window, interruption
states and Counterspell ownership/cooldown. One queued input is revalidated in
the serial worker against foreground window, age and
deadline. MAKCU's `km.press(key,35,0)` owns the full key pulse and release. In
Windows fallback, a separate worker releases the matching scan-code key after
35 ms, independently of rendering. Foreground loss, cancellation and shutdown
release owned Windows input. Failed releases block further actions until cleanup succeeds. A
250 ms minimum input interval and attack identities suppress duplicate inputs.
An "input sent" counter means the selected input backend accepted the action; it is not a count of
successful parries in the game.

## Reader updates and diagnostics

The reader contract includes the new schema fields, state/value enum IDs,
ability vtables, property accessors and private-layout witnesses. The updated
data tool must be deployed alongside the executable. Unknown layouts fail
validation rather than inheriting addresses from a different DLL build.

With `--diagnostics`, `auto-parry.json` reports gates and sampled attacks.
It also reports `melee_definition_valid` and the exact definition-read status;
the menu shows whether the base definition is ready. Missing definition data
is reported directly instead of appearing as an idle enemy or missing item.
`--parry-probe --probe-ms 10000 --parry-result <path>` performs bounded read-only
observation without creating an overlay or opening MAKCU. An optional
`--reader-profile <path>` selects a candidate reader profile.

## Build 6774 verification

Release compiled through CMake, and the rebuilt updater generated a profile
that passed the read-only live validator. A sandbox observation detected two
incoming heavy-melee threat events. A separate sandbox-only read compared
server melee phases with client masks: the movement lock cleared during the
heavy dash before the attack phase. The production reader uses client data.

The installed build's sandbox heavy-melee test passed with user confirmation.
Runtime diagnostics recorded five MAKCU parry writes and zero write failures;
the game's successful-parry timestamp also advanced. During the read-only
follow-up, focus loss and side-button release paused the controller without
increasing the input count.

Both executable copies, both updater copies and their matching profile were
deployed. No existing self-test, CTest, resource or stress suite was run.

## Empty local property cache fix

On client `7602e238c7ce`, remote heavy-melee wind-ups were visible, but the
local player's range and angle property views had null definition pointers.
That blocked the replicated fallback before it could evaluate a punch.
The reader now resolves `MeleeAttackLength` and `MeleeHalfAngle` from the live
VData dictionary, independently of those lazy property views. The updater
resolves the dictionary field through schema and validates its node stride
against a code witness; no range or angle is guessed.

The rebuilt profile passed read-only live validation. A candidate observation
detected an incoming heavy-melee threat, and the installed runtime recorded
six parry inputs with zero serial write failures. The game's successful-parry
end timestamp was later than the pre-deployment observation. Both executables,
both updaters and the matching reader profile were deployed together.
