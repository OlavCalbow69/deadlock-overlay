# Mid Boss HUD

The compact overlay is enabled by default. Toggle **Appearance (top-right settings icon) -> Mid Boss HUD**. It uses the existing focus-gated, click-through overlay and can remain enabled when health bars and focus drawings are disabled.

States:

- **Countdown**: the next spawn timestamp minus the pause-aware game clock, rounded up to seconds. The accent turns gold in the last 30 seconds.
- **Alive**: a `C_NPC_MidBoss` entity has a valid identity, positive health and living life state.
- **Due**: the timestamp has elapsed but no living entity is confirmed yet.
- **Unavailable**: the current map has no usable spawn data, or clock/entity data cannot be validated.

No initial spawn time or respawn schedule is hardcoded. There is no assumption that the first boss spawns ten minutes into the match. A due timestamp alone cannot claim the boss is alive.

The reader contract resolves these `C_CitadelGameRules` fields through the schema: `m_iMidbossKillCount`, `m_tNextMidBossSpawnTime`, `m_nMatchClockUpdateTick`, `m_flMatchClockAtLastUpdate`, `m_bServerPaused`, and `m_eGameState`. Game time is the networked clock anchor plus the elapsed server ticks; a paused clock stays at its anchor. Reads validate matching rules/global pointers and consistent clock anchors. Timing refreshes at 10 Hz without wall-clock extrapolation. Boss discovery reuses the entity scan.

In the archived 6766 IDB, the minimap HUD `sub_181D72380` reads the next-spawn timestamp at rules +0x22C and subtracts elapsed game time (`sub_180768A50`). Its timer formatting adds one second before truncation. The clock routine accounts for game start, pauses and replay time. The current schema and live read validate the network-clock fields used here. Live `dl_midtown` reads confirmed a living boss, followed by its death and a real respawn countdown. Pauses and replay seeks still need an interactive game check.

With `--diagnostics`, `midboss.json` records phase, validation, game clock, next timestamp, remaining time, kills, pause state and sample time. A matching rebuilt updater/profile is required when the reader contract changes.
