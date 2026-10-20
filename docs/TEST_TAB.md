# Test tab: session monitor

Open the overlay with Insert and select **Test** (`</>` / Phosphor Code).

The monitor keeps the latest 60 seconds of overlay timings, sampled five times
per second. Collection uses the existing game snapshot. Windows resource
counters are refreshed once per second. The history is bounded to 301 points.

- **Overlay FPS** counts overlay presentations. It does not measure the game's FPS.
- **Game data read** is the reader's sample duration. Snapshot age measures how
  long ago that snapshot was published. Missing and stale samples leave gaps.
- **Overlay resources** shows private committed memory and process CPU usage as
  a percentage of the machine's total logical processor capacity. Memory changes
  alone do not establish a memory leak.
- **Timing history** shows FPS and reader latency. Hover over a sample for loop,
  presentation and GPU queue-wait timings. Percentiles describe the samples in
  the displayed history, rather than every individual game or overlay frame.
- **Live view** shows the detected hero, current render FOV and aspect ratio,
  map, player count and host input-queue delay. Background rendering pauses are
  identified explicitly.
- **Freeze** holds the history while the metric cards and live view keep updating.
  **Resume** returns to the rolling window. **Reset history** clears the graphs.
- **Export report** writes a local JSON report containing the displayed history,
  current timings, reader profile and view details. It does not upload anything.
  **Open reports** opens `%LOCALAPPDATA%\DeadlockOverlay\reports` in Explorer.
- **Show HUD** enables a compact performance panel at the top right of the game
  overlay. It is off by default and follows the existing foreground/minimization
  rules. This preference is saved with the other settings.

The monitor does not change focus, prediction, calibration or input behavior.
It does not request additional game-data fields or send commands to the input
device. With `--diagnostics`, the latest monitor sample is also written once per
second to `session-monitor.json` beside the executable. `--test-tab` opens the
menu directly to this tab at launch.
