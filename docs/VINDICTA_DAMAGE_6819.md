# Vindicta Assassinate damage — build 6819

Checked 24 October 2026 against the archived server DLL, the installed server DLL, the installed ability resource and a read-only inspection of the loaded Assassinate weapon info. The two server files have the same SHA-256: `4855EF0712962F43AA4D9F623B258ADA286D3E6F8FD2A2C8272984BFECF777E8`. The extracted `scripts/abilities.vdata_c` hash is `307F5303832141A2FFDA63841F3C8709B0007A08C07AD8939412D0DADAE900FC`.

**The authoritative formula.** The server obtains current health and maximum health before applying this collision's damage. It evaluates the named ability properties, chooses a low-health bonus, scales the sum by charge and then adds its own head-hit bonus. In terms of evaluated properties:

```text
low = 100 * current_health / maximum_health < LowHealthEnemyThresholdPct
bonus = evaluated(LowHealthEnemyDamageBonus) if low else 0
charge = remap_clamped(scope_elapsed,
                       MinScopeTimeToShoot,
                       MinScopeTimeToShoot + evaluated(TimeToFullCharge),
                       evaluated(MinChargeDamagePercent) / 100,
                       1)

shot = (evaluated(Damage) + bonus) * charge
if hit group is 1 or 19:
    shot *= 1 + evaluated(HeadshotBonus) / 100
```

The scope-start timestamp must be valid. The firing routine's zero-timestamp branch produces a zero charge factor; this is not an ability that fires unscoped at half damage. Its configured minimum scope delay is zero, so a valid shot just after entering scope starts at 50% power.

For ordinary hero targets, before outgoing/incoming damage modifiers and defenses, the installed data simplifies to:

```text
S = effective Spirit Power used by this ability
U = 1 with the second ultimate upgrade, otherwise 0
L = 1 when the victim is strictly below 50% HP, otherwise 0
Q = 0.5 + 0.5 * clamp(scope_elapsed / 1 second, 0, 1)
H = 1.2 for a qualifying head hit, otherwise 1

nominal_shot = [90 + 0.93*S + L*(90 + 2.30*S + 80*U)] * Q * H
```

This is a threshold bonus, not a continuous missing-health calculation: 49% and 10% health give the same nominal bonus with otherwise identical inputs. Exactly 50% does not qualify. Crossing below 50% as a result of this shot does not retroactively enable its bonus. Changes to maximum health can change eligibility. Barriers are not added to health for this comparison.

Charge applies to both damage components. At 0, 0.25, 0.5, 0.75 and 1 second of valid scope time, the respective multipliers are 50%, 62.5%, 75%, 87.5% and 100%. Additional waiting beyond full charge does not add damage. A half-filled charge meter therefore corresponds to 75% damage.

**Spirit, upgrades and offensive modifiers.** Both damage properties use `scale_function_tech_damage`. Its implementation requests Spirit Power (`EStatsType` 59), ability damage scaling (61) and overall damage scaling (57), in that order. Ability upgrades are incorporated before evaluating these scale functions. Thus Spirit, applicable imbues, ability-damage modifiers, overall outgoing damage modifiers and mode-specific property overrides must be reflected in the evaluated properties. A calculator using already evaluated properties must not multiply those modifiers in again.

The ordinary resource values are:

| Property | Installed value |
|---|---:|
| Damage | 90 + 0.93 × Spirit |
| Low-health bonus | 90 + 2.30 × Spirit |
| Low-health threshold | 50% |
| Headshot bonus | 20% |
| Minimum charge damage | 50% |
| Time to full charge | 1 second |
| First ultimate upgrade | −15 seconds cooldown |
| Second ultimate upgrade | +80 to the low-health bonus |
| Third ultimate upgrade | +4 percentage points of weapon damage per credited kill stack |

Only the second listed upgrade directly raises the nominal shot in this table, and only below the threshold. The third upgrades weapon-damage stacks. Weapon damage, gun DPS, bullet velocity and ordinary gun crit stats are not substitutes for the damage properties used by Assassinate. Levels and souls matter through the resulting stats, items and upgrades rather than a separate level/soul multiplier in this firing calculation.

The head-hit check accepts hit groups 1 and 19. It applies the 20% to the entire charged sum. The loaded Assassinate weapon info had `m_bCanCrit=true`, `m_flCritBonusStart=1` and `m_flCritBonusEnd=1`: the normal hero gun-crit range scale contributes no extra multiplier. The ability's explicit 20% remains the relevant nominal hero head-hit bonus. The loaded NPC crit bonus was 0.2, and the generic damage system has separate non-hero/weakpoint handling; hero examples should not be copied blindly to NPCs and objectives.

**Defenses and conditional effects.** The shot is constructed as `CITADEL_DAMAGETYPE_ABILITY` (2), which enters the Spirit/ability mitigation path. The main stages inspected in the receiver's damage function are:

1. Target incoming all-damage and Spirit-damage amplification. The two normalized increases are combined before mitigation; individual effect aggregation is performed by the modifier system.
2. Generic crit handling and a pre-mitigation modifier event. Ordinary hero Assassinate gun-crit scale is 1 as described above.
3. Effective Spirit resistance, including debuffs, and the attacker's Spirit-resistance piercing. Positive resistance `R` is adjusted to `R * clamp(1 - piercing/100, 0, 1)`; this branch does not apply that piercing adjustment to already-negative resistance. For example, 20% piercing changes 30% resistance to 24%, rather than 10%. The effective resistance multiplier is `max(0, 1 - resistance/100)`. Ability-damage reduction is also applied in this stage.
4. Barrier/block absorption. In the ordinary barrier path, the remaining amount is subtracted before the following all-damage-reduction stage. The barrier handler can depend on source and damage type.
5. Additional all-damage reduction, immunity/invulnerability and death-prevention rules. Some states set damage to zero; a prevent-death rule can clamp it so the target remains at one HP.
6. Applicable ability-versus-NPC/boss scaling, modifier callbacks and the entity/game-rules damage handler, which ultimately applies health loss and produces damage events.

The receiver explicitly records separate mitigation categories for Spirit resistance, barriers, damage reduction, immunity, and items/abilities. These are not all one interchangeable resistance percentage. A barrier also does not disable the under-half-health bonus merely because it is large.

Applicable on-damage item/ability procs can produce additional, separate damage events. Their eligibility, cooldowns, target filters and resistance calculations are separate from the nominal Assassinate packet. Spirit lifesteal changes healing rather than directly adding to that packet. Exact kill prediction needs the active modifiers and proc state, not just a list of purchased items.

**Range and hit validation.** The firing path uses immediate collision queries rather than a travelling projectile. The data specifies 1000 m range and a 4-engine-unit shot radius. Distance and walls affect whether a collision is hit; there is no ordinary weapon-distance falloff factor in the Assassinate-specific damage calculation. FOV/aspect ratio, overlay frame rate and bullet-velocity items are not damage factors in that calculation. The generic weapon-info bullet-speed field does not create a flight-time damage multiplier.

**Examples.** These are nominal full-charge hero hits with no amplification, resistance, barrier or other protection:

| Spirit | Victim health | Second upgrade | Body | Head |
|---:|---|---|---:|---:|
| 0 | At/above half | No | 90 | 108 |
| 0 | Below half | No | 180 | 216 |
| 0 | Below half | Yes | 260 | 312 |
| 100 | At/above half | No | 183 | 219.6 |
| 100 | Below half | No | 503 | 603.6 |
| 100 | Below half | Yes | 583 | 699.6 |

For the last row, a body shot after half a second of scope is `583 × 0.75 = 437.25`. A fully charged head hit against 30% effective Spirit resistance is `699.6 × 0.70 = 489.72` before barriers and other protection. With an ordinary 100-point absorbing barrier and no additional effects, about 389.72 damage reaches health. Final displayed integer health/damage can involve engine rounding and health application; these decimal examples describe the packet arithmetic.

**Evidence and limits.** Current evidence is in [the 6819 research folder](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819>). The core references below are IDA virtual addresses for this exact DLL at image base `0x180000000`, not portable offsets or live process addresses:

| Role | Evidence |
|---|---|
| Authoritative firing/formula | [0x1814C2080, float types clarified](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/mcp/0x1814c2080-typed.c>) |
| Property bindings | [0x1814237D0](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/mcp/0x1814237d0.c>) |
| Installed ability values | [Extracted Assassinate data](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/assassinate.vdata.txt>) |
| Ability damage-info construction/type | [0x18073E980](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/mcp/0x18073e980.c>) |
| Spirit/ability mitigation | [0x180729210](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/mcp/0x180729210.c>) |
| Receiver pipeline and mitigation ordering | [0x180734A30](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/mcp/0x180734a30.c>) |
| Incoming damage amplification | [0x180729870](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/mcp/0x180729870.c>) |
| Immunity and non-lethal states | [0x180728EC0](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/mcp/0x180728ec0.c>) |
| Outgoing scaling dependencies | [0x180B7B0E0](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/mcp/0x180b7b0e0.c>) |
| Loaded weapon crit settings | [Read-only sample](<C:/Users/varos/Desktop/dl/project/verification/vindicta-damage-6819/loaded-weapon-rules.json>) |

The core formula, current base properties and listed mitigation stages were inspected. This is not a claim that every hero-specific damage callback, every item proc, every NPC weakpoint or every custom-mode override has been individually reversed or live-tested. Type annotations in the research IDB corrected float return/argument signatures using the SSE instructions; DLL bytes and game state were not changed. The live sample submitted no input. No overlay executable or configuration was changed for this investigation.
