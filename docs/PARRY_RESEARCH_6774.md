# Deadlock parry and Counterspell: build 6774

Research date: 12 October 2026. This is a static reverse-engineering report of the current game data and server/client binaries. No auto-parry was implemented, and no game inputs were submitted during this investigation.

## Findings

The purchasable spell-parry item is **Counterspell**, internally `upgrade_counterspell`. It is passive: when ready, it adds a separate spell-protection window to the ordinary parry input. The normal melee parry and Counterspell share an activation input, but have different eligibility rules, durations, cooldowns and rewards.

Ordinary parry evaluates an incoming melee attack's flags. Counterspell evaluates incoming damage and attempted spell effects through two separate paths. Consequently, neither an attack's appearance nor its projectile/hitscan label is enough to classify it.

The main verified base values are:

| Property | Ordinary melee parry | Counterspell |
| --- | --- | --- |
| Activation | Innate, instant cast; normally bound to F | Passive listener for the ordinary parry cast |
| Protection window | 0.75 seconds | 0.8 seconds |
| Base cooldown | 4.5 seconds, affected by the parry-cooldown stat | 23 seconds, affected by item cooldown |
| Successful protection | Blocks eligible melee; normally stuns its attacker | Zeros qualifying damage and prevents qualifying new spell effects |
| Main reward | 2.75-second parried-stun modifier; configured 25% vulnerability | Base 150 heal, +20 Spirit Power and +1.75 m/s for 6 seconds |
| General projectile reflection | None | None |
| Protection after first success | Separate 0.3-second success-grace setting | Remains armed until the spell window expires; rewards only once |

All figures are build-specific base data. Runtime stats, other modifiers and attack-specific exceptions must still be considered.

## Evidence and version

The installed `steam.inf` reports ClientVersion/ServerVersion **6774**, SourceRevision **11112755**, built **9 October 2026 at 16:19:46**. The archived 6774 DLLs matched the installed DLLs by SHA-256. Current 6774 server analysis completed before its functions were decompiled.

| Input | SHA-256 |
| --- | --- |
| `client.dll` | `E2BAFED8EBDFD239D506B4F59B24A09719F945C6A63F69766171349B886603D4` |
| `server.dll` | `9A97830B33A1B53DD766902C185D83E53C44924A5DA60732331433C28DBA0BA2` |
| `scripts/abilities.vdata_c` | `7681D8F9E283F4565C738C2F5F58CDCDE2610ADA8DB7164D8439396C96788877` |
| `scripts/modifiers.vdata_c` | `FE8F118B796A104C4613783FBA69C356137BD970253131E230896443C81E0DEB` |

The schema contract used was the project's current `data/schema-dump/deadlock`, generated for this build. The older Downloads dump was not used to decode current state/event IDs. Archived 6766 code was used as an initial comparison, then the relevant conclusions were checked against current 6774 server code.

Evidence is preserved under `verification/parry-6774`: extracted assets, hashes, RTTI/vtable catalogs, selected assembly and decompilations. `mcp/50926` contains **current 6774** decompilations; `mcp/50928` contains **older 6766** comparison material. The server code matters because it decides whether an actual hit or effect is accepted; client animation alone does not establish that.

## Ordinary melee parry

### Activation and timing

`citadel_ability_melee_parry` is an innate, no-target, instant-cast ability. Its base cast delay is zero. The code records the parry start time and installs the active parry modifier. Incoming melee resolution then dispatches `CHECK_FOR_PARRY`; the modifier's handler evaluates the active window and the separate success-grace deadline.

The configuration contains several clocks that should not be combined indiscriminately:

| Field | Value | Meaning supported by the investigation |
| --- | --- | --- |
| `m_flActiveTime` | 0.75 s | Ordinary acceptance window before a success |
| `m_flWhiffDuration` | 1.0 s | Separate whiff/recovery setting; not an extra second of protection |
| `AbilityCooldown` | 4.5 s | Base cooldown property |
| `m_flSuccessActiveTime` | 0.3 s | Success-grace setting used by the acceptance handler |
| `m_flParryEndVisualTime` | 0.6 s | Visual tail, not additional defensive time |
| Mash-protection default | 0.25 s | Input-repeat protection setting |
| `m_flMovementRestrictionTime` | 0 s | This particular restriction timer is disabled |

Valve's [10 April patch notes](https://forums.playdeadlock.com/threads/04-10-2026-update.125825/) describe the anti-mash interval as starting at the initial parry input rather than restarting after every success. The current constructor still supplies 0.25 seconds. The precise effective repeat deadline should be measured before automation: the success function mutates its start-time field, and float arguments to cooldown calls are not fully represented by the inferred decompiler prototype.

### What a successful ordinary parry does

For an ordinary eligible hero melee, the defender prevents the attack and the attacker receives the parried-stun modifier. Its base duration is **2.75 seconds**, and its VData explicitly disables ordinary duration reduction for that modifier.

The configured `VictimDamageTakenScale` is **25**. A useful code/text mismatch exists here: the current parried-stun modifier registers its vulnerability callback under `ALL_DAMAGE_TAKEN_INCREASE` (current enum value 30), rather than under a melee-only increase. The callback evaluates the modifier's bound property. This supports a **25% all-damage vulnerability contribution** in the current implementation, despite older tutorial wording about melee damage. Actual final damage stacking should still be checked with a controlled live damage comparison.

The success routine distinguishes hero attackers before invoking its cooldown-reset path. Therefore, a successful hero parry does not simply force the defender to wait the full whiff cooldown. Repeat-input protection and current state still matter.

Non-hero attacks have separate behavior. The current success routine's hero-only cooldown reset must not be generalized to troopers. Valve's 28 May announcement also describes trooper parries as stunning the trooper without resetting the player's cooldown. That announcement says a trooper hit does not end the parry early; I did not establish that final detail conclusively in the current build, whose shared success routine also mutates the active timers.

Boss victims have another branch, with configured melee-disable and calm durations of **7.0** and **2.25 seconds**. This was not mapped exhaustively to each boss class, so it is not proof that every Guardian/Walker attack gives the ordinary hero stun.

### Direction, distance and facing

There is **no crosshair-angle, facing cone or look-at-attacker test in the shared defensive acceptance handler** inspected here. Looking directly at an attacker is not what makes this handler return success.

The attack must still hit according to its own attack geometry, range and trace rules. A nearby enemy, an enemy in the camera FOV, or an enemy who starts winding up a punch is not automatically an incoming hit. Normal parry does not create a map-wide interception radius.

### When the input can be rejected

The current cast gate and VData identify these relevant conditions:

- The dedicated `PARRY_DISABLED` state rejects it. Urn handling applies relevant restrictions; while carrying the Urn, the input is also used to throw the held object.
- Sleep and command-restricted/stunned states can reject the cast.
- Zipline state is explicitly rejected in the inspected gate.
- Cooldown and general ability-busy/state checks still apply.
- Silence alone is not a sufficient reason to reject this innate ability: its behavior includes `NOT_SILENCABLE`.
- Ground and air dash states have explicit handling. Some abilities can also allow parry through dedicated state/behavior exceptions; an indiscriminate “any ability means no parry” rule would be wrong.

The inspected dash-related data includes residual-motion parameters. Parrying during a dash should not be modeled as continuing the complete dash unchanged, nor assumed to set velocity to zero immediately in every current circumstance. Valve's September announcement introduced dash cancellation and reload pausing; later current data contains additional movement-taper settings.

## What determines whether a melee is parryable

`AttackData_t` carries independent booleans:

| Member | Offset in current attack data | Meaning |
| --- | --- | --- |
| `m_bIsHeavyAttack` | `+0x18` | Heavy/light classification |
| `m_bCanBeParried` | `+0x19` | Whether the shared melee resolver should attempt parry |
| `m_bParryOnlyBlocksParrier` | `+0x1A` | Whether a parry removes only the defending victim rather than stopping the broader attack |
| `m_bParryStunsAttacker` | `+0x1B` | Whether successful parry should stun its attacker |

The current constructor writes bytes `00 01 00 01`: light by default, parryable, not limited to blocking only the parrier, and attacker stun enabled. The ordinary generic light/heavy attacks use these parry defaults unless overridden. **Light melee can be parried too; heavy melee is not the only category.**

Confirmed exceptions:

| Attack/mechanism | Finding |
| --- | --- |
| Bebop Uppercut, `citadel_ability_uppercut` | Its `m_UppercutAttackData` explicitly sets `m_bCanBeParried = false`. Ordinary F cannot counter it through the normal melee-parry route. Counterspell's separate ability-damage/effect rules still need to be considered. |
| Baba bench heavy attack, `EBabaBenchMeleeAttack_Heavy` | Explicitly parryable, but `m_bParryOnlyBlocksParrier = true` and `m_bParryStunsAttacker = false`. Its parry blocks for the defending player without the usual attacker stun; other caught victims are not automatically protected. This is the heavy-entry rule, not a claim about every Baba attack. |
| Active parry-piercing modifier | Shared melee handling queries `PARRY_PIERCE` and can bypass the normal success/stun path. |
| Runed Gauntlets | Its definition supplies a parry-piercing mechanism, but this extracted item is a **Street Brawl requirement, tier 5** item. It must not be presented as a universal normal-match shop counter. The pierce state must actually be active. |

Some attacks queue their damage. The delayed-hit resolver checks the parry flags again, which is another reason to use the damage/impact phase rather than merely watching the first animation frame.

## Counterspell

### Identity, ownership and base stats

The item is `upgrade_counterspell`, a passive tier-3 Armor/Vitality item definition. Its watcher listens for its owner's ordinary parry ability cast (current internal slot 19). It does not require pressing a separate Counterspell item hotkey.

| Property | Current base value |
| --- | --- |
| Item cooldown | 23 seconds |
| Spell protection window | 0.8 seconds |
| Passive maximum health | +50 |
| Passive Spirit Power | +5 |
| Successful block heal | 150 |
| Successful block Spirit Power | +20 |
| Successful block movement speed | +1.75 m/s |
| Reward buff duration | 6 seconds |

The item cooldown property uses the item-cooldown stat. The reward-duration property has its own duration scaling. These are separate from the spell-window property; the 6-second reward does **not** mean six seconds of spell immunity.

### Arming, success and cooldown

The watcher maintains two independent flags: **armed** and **reward already granted**.

1. An accepted ordinary parry cast checks that the item is ready.
2. The watcher becomes armed and clears its reward flag, then starts its 0.8-second timer.
3. A qualifying damage event or prevented effect can trigger success.
4. The first success gives the heal/buff/feedback and invokes the item's cooldown path.
5. Further qualifying events during the same armed window can still be blocked. The reward flag prevents another heal/buff/cooldown trigger for each hit.
6. Expiry clears the watcher flags and disables its timer.

The arming and expiry functions do not themselves start the item cooldown. In the inspected paths, the **item cooldown is consumed by the first successful proc**, not just by opening an unused spell window. A whiff can still leave ordinary parry unavailable through its own recovery/cooldown.

Buying Counterspell does not guarantee that every F press has spell protection: the item must be ready, and the ordinary cast must actually be accepted. Conversely, ordinary melee parry remains a separate mechanic while Counterspell is cooling down.

### Damage path

The current damage callback requires the damage event to belong to the watcher owner and come from a valid opposing attacker. It requires the watcher to be armed, then explicitly rejects **`DMG_DOT`** events.

For the remaining events, it accepts either:

- the damage category `ECitadelDamageType::ABILITY`; or
- an executing source ability classified as `Signature` or `Ultimate`.

On acceptance, it triggers the success/reward routine and sets the event's damage to zero.

This second source-ability condition matters: some abilities produce bullet- or melee-category damage. Their category alone does not rule out Counterspell if the required signature/ultimate execution context is present. Conversely, an ordinary primary-weapon bullet is not accepted just because it is a fast projectile.

There is no requirement in this callback for a projectile entity, a projectile travel time, a crosshair ray, or the defender to face the attacker.

### Spell-effect path

Spell effects are evaluated separately through the generic avoid-spell/modifier-creation path. The target's armed watcher supplies `AVOID_SPELL`. A qualifying attempted modifier can be prevented and report the reason `AVOID_SPELL` through `MODIFIER_CREATION_PREVENTED`, which then triggers Counterspell's success handler.

The generic query rejects same-team cases and respects the source ability's **`DONT_TRIGGER_SPELL_BLOCK`** behavior. Thus, damage prevention and effect prevention are not identical tests.

This is why a compound ability needs component-level analysis. Its direct damage, stun, knockback, area effect, tether, teleport and subsequent ticks can be delivered by different calls. Preventing one does not prove the entire action was cancelled. In particular, a source's effect-path opt-out does not by itself establish that its damage bypasses the separate damage callback.

Current extracted assets include effect-block opt-outs in `rutger_rocket`, `citadel_ability_kali_spinning_blade`, `ability_kali_dust_storm`, `thumper_ability_1` and `ability_wrecking_ball`. Some are prototype/unused content; this is evidence that the opt-out exists, not a list of currently playable counters.

### What Counterspell does not do

- It does not grant universal invulnerability.
- It does not generally block ordinary weapon fire.
- Its inspected damage path explicitly excludes damage-over-time ticks.
- It does not remove an existing debuff merely by arming. Preventing a newly attempted effect is different from cleansing one already installed.
- It does not erase damage that has already resolved.
- Its success routine does not reflect the incoming spell or directly apply the ordinary melee attacker-stun routine.
- Its reward buff lasting six seconds does not keep the spell-block window open for six seconds.

An ability that initially applies a DOT may still have a separate preventable application or direct-impact event. “DOT ticks excluded” must not be misread as “every component of every DOT ability is necessarily unpreventable.”

## Practical eligibility table

“Conditional” means that the actual damage/effect implementation must satisfy the relevant gate; it is not proof that every ability in that row is blocked completely.

| Incoming event | Ordinary parry | Counterspell when armed and ready |
| --- | --- | --- |
| Normal light melee | Yes, with normal flags | Ordinary melee parry handles it; buying the item is unnecessary for this route |
| Normal charged heavy melee | Yes, with normal flags | Same ordinary route |
| Melee with `CanBeParried = false` | No through ordinary melee handler | Conditional if there is qualifying ability damage/effect |
| Melee while the attacker has active parry pierce | Normal protection can be bypassed | Not sufficient information to classify separate spell components |
| Ordinary primary weapon bullet | No | Not generally blocked by the inspected Counterspell predicate |
| Direct enemy ability damage, not DOT | No through ordinary melee handler | Qualifies under the ability-damage category predicate |
| Signature/ultimate damage using another category | No solely because it is an ability | Conditional on the executing source context; a dedicated accepted branch exists |
| Qualifying new enemy spell debuff/stun | No general spell prevention | Can be prevented through avoid-spell/modifier creation |
| Existing burn/poison/other DOT tick marked `DMG_DOT` | No general protection | Explicitly excluded by the damage callback |
| Continuous beam/area damage | No general protection | Per-event classification matters; DOT-marked ticks are excluded |
| Existing debuff | Does not cleanse | Does not cleanse merely by arming |
| Environmental damage or self/friendly damage | No general protection | Not general protection; enemy/valid-source checks matter |
| Teleport, displacement, execute, scripted action | No blanket rule | Must inspect the action's individual damage/effect calls; no universal result established |
| Attack outside the active window | No ordinary acceptance | No protection once the spell watcher has expired |

## When to parry

The useful reference point is **when the server processes the incoming hit/effect**. The parry needs to be active then. For an initially unused window, the broad timing requirement is impact during the next 0.75 seconds for ordinary melee, or during the next 0.8 seconds for Counterspell. These are not promises about exact equality boundaries or a recommended fixed input lead.

For melee, watch the committed hit phase, attack reach and relative movement. An enemy beginning a charge can cancel, miss, move away or hit someone else. Pressing immediately on every charge animation can waste the window. Light attacks can be parried but offer less visible warning.

For travelling spells, arrival time matters more than cast time. For instant/hitscan damage there may be no useful post-launch reaction interval; the available cue can instead be the cast windup. Delayed ground effects and multi-stage attacks must be timed against their damaging/application stage.

The defensive handler does not impose a facing test, so camera centering should not be substituted for threat prediction. Similarly, visibility of the attack animation alone does not prove that it will collide with the local player.

Client prediction, snapshot age and network delivery can make the displayed animation differ from the server's current decision point. This is an engineering implication of the server-side hit resolution inspected here; this investigation did not measure a universal latency compensation constant. A rule such as “always parry exactly 100 ms before impact” would not be supported.

Feedback particles have their own lifetime. A shield-looking visual or a late success-looking animation is therefore insufficient evidence that protection is still active. Read the actual ability/window state where available.

## Other mechanics with similar names

**Rebuttal** (`upgrade_melee_rebuttal`) enhances successful melee parries. Current data supplies +75 health, 18% melee resistance, 1.75 seconds of parry-cooldown reduction, a 100%-of-parried-damage healing property and a 30% damage reward for six seconds. Its localized description also describes returning damage to the attacker. These are Rebuttal's rewards; they should not be attributed to Counterspell's own spell-block implementation. Its entire return-damage execution path was not audited in this investigation.

**Apollo's Riposte** (`ability_fencer_riposte`) is a separate hero ability, not the purchased item requested here. Its data includes a 0.3-second parry window and a damage-threshold property of 60. It needs a separate full trace before being treated as equivalent to the innate parry.

## Implications for a later auto-parry

The research establishes the defensive predicates, but the listed server function addresses are **not** ready-to-use live-client reader offsets. Implementing automation still requires a validated client-side contract for incoming threats and the local defensive state.

A suitable next investigation would record threats without submitting inputs. For each candidate it would need to establish:

1. Whether the attack actually threatens the local player's collision geometry.
2. The attack's phase, predicted contact/application time and data age.
3. Whether ordinary parry, Counterspell, both or neither can protect the relevant component.
4. The local cast state, effective cooldown and active protection deadline; Counterspell ownership/readiness independently.
5. Whether the attack bypasses parry, only blocks the defender, or omits attacker stun.
6. Whether the evidence is sufficient to act, with a documented reason for rejected candidates.

First validate those decisions in a read-only sandbox timeline against actual impacts. An enemy-distance or FOV check alone would trigger on many harmless attacks and miss important non-projectile effects. No automatic input, new input transport or auto-parry feature was added here.

## Limits and unresolved details

This is a detailed audit of the common defensive paths and selected exceptions, **not a fully live-tested hero-by-hero ability matrix**. In particular:

- Compound ultimates, scripted relocation/execute effects, bullet-triggered item procs and ability-specific direct overrides need individual traces before asserting complete protection.
- Exact repeated-parry cooldown deadlines and tick-boundary behavior need a live time-series check. The 0.25-second setting and official intent are known; exact current execution should not be inferred from an imperfect float-call prototype.
- Current code registers the parried vulnerability as all-damage. A controlled damage test should resolve the disagreement with older tutorial wording and establish final stacking.
- The current non-hero path has no hero cooldown reset. The older trooper “does not end early” wording was not conclusively reconciled with current timer mutation.
- The boss-specific branch was identified but not mapped to every current boss class and attack.
- Counterspell's repeat protection is evident from its retained armed flag and independent reward gate. No live barrage test was run in this investigation.

No existing self-test, CTest or resource/stress suite was run. No overlay executable was rebuilt or deployed.

## Current-build function map

Addresses below are **6774 `server.dll` virtual addresses at image base `0x180000000`**, not ASLR-adjusted runtime addresses. Labels are analytical descriptions; generated `sub_...` names are not Valve symbols.

| Address | Role / evidence |
| --- | --- |
| `0x18023EC80` | `AttackData_t` constructor; default parry booleans. Saved by a query inside the function at `0x18023EC8A`. |
| `0x1802488F0` | Ordinary parry cast-state gate |
| `0x180298A10` | Ordinary parry cast/start setup |
| `0x18024A770` | Active parry `CHECK_FOR_PARRY` handler; timing and probe handling |
| `0x18024C590` | Shared melee parry dispatch; attacker-stun flag and parry-pierce check |
| `0x180254CB0` | Melee hit processing; per-attack parry flags |
| `0x18024AD20` | Delayed melee damage recheck |
| `0x18029E5F0` | Ordinary parry success, timers, hero cooldown path and success event. Saved as `0x18029E890.c`, a query inside the same function. |
| `0x18029E0C0` | Parried attacker modifier and boss-specific branch |
| `0x18020DBF0` | Parried-stun value callback registration, including all-damage value 30 |
| `0x18025D7E0` | Parried-stun vulnerability property getter |
| `0x180454080` | Counterspell watcher arming on owner parry cast |
| `0x180472700` | Counterspell damage predicate and damage-zeroing |
| `0x180337C00` | Counterspell notification for prevented spell modifier |
| `0x180479340` | Once-per-window rewards/cooldown and spell-parry feedback |
| `0x180337BB0` | Counterspell window expiry / flag reset |
| `0x1803AAA10` | Armed watcher supplies avoid-spell value |
| `0x180516AC0` | Generic avoid-spell query, including source opt-out |
| `0x1804BC570` | Generic modifier-creation eligibility and avoidance gate |
| `0x18050E5C0` | Modifier-prevention event dispatch |
| `0x1817FE3B0` / `0x1817FE4E0` | Modifier timer start/stop; separate from item cooldown |

## Evidence files and external context

Primary local evidence:

- `verification/parry-6774/citadel_ability_melee_parry.vdata.txt`
- `verification/parry-6774/upgrade_counterspell.vdata.txt`
- `verification/parry-6774/upgrade_melee_rebuttal.vdata.txt`
- `verification/parry-6774/upgrade_runed_gauntlets.vdata.txt`
- `verification/parry-6774/ability_baba_bench_melee.vdata.txt`
- `verification/parry-6774/game-data/assets/scripts/abilities.vdata_c.txt` (includes Bebop's `citadel_ability_uppercut`)
- `verification/parry-6774/mcp/50926/` (current server decompilations)
- `verification/parry-6774/binary-catalog.json`, `extra-catalog.json`, `localization-evidence.json`
- `data/schema-dump/deadlock/sdk/server/` (current state/value/event enums and damage/attack structures)

Official historical context was checked against Valve's [10 April announcement](https://forums.playdeadlock.com/threads/04-10-2026-update.125825/) and [Steam's official announcement feed](https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=1422450&count=25&maxlength=0&feeds=steam_community_announcements), with relevant extracts saved as `verification/parry-6774/official-news.json`. The latter includes the 28 May trooper change, 22 May item changes and 16 September reload/dash changes. Historical notes explain intent but do not override current binary evidence.

Community bug reports were used only as investigation leads, not as proof of current mechanics.
