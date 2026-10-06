# Resource audit

Measurements were taken on this machine on 2026-10-04. CPU percentages represent all logical processors. GPU utilization is a single Windows 3D-engine counter snapshot, not a time average or combined game/overlay usage.

| Updated overlay | Measurement |
|---|---:|
| Active CPU average (11 samples, menu closed) | 1.03% total CPU |
| Hidden CPU average (34 samples) | 0.008% total CPU |
| Active rendering average | 218 FPS, 240 Hz target |
| Resident RAM | 54.20–54.23 MiB |
| Private committed memory | 76.99–77.02 MiB |
| 3D GPU snapshot | 7% |
| Dedicated / shared GPU memory snapshot | 46.36 / 4.65 MiB |
| Process handles during 45 seconds | 1591–1593 |
| Local-team probe | Team 2; nearby players on teams 2 and 3; enemy filter keeps 1 of 2 |

The before sample recorded 60.17–60.25 MiB resident RAM, 88.22–88.30 MiB private memory and a steady 1887 handles. It had a different game/frame load (63 FPS average), so its 0.49% average CPU cannot be used to claim a relative speed improvement.

## Cleanup and stress checks

The resource test creates and destroys the complete GUI three times and renders 2,200 frames per cycle, including a 1,200-frame initialization phase before the 1,000 measured frames, toggling glass effects and window visibility. It verifies bounded memory growth and stable process/GDI/USER handle counts after warm-up. The resource report records private-memory and handle/object counts for the measured phase. Deferred Windows/driver initialization during focus transitions required warming all paths before evaluating steady-state growth. Fonts and animation entries are now reset when the owning GUI context is destroyed; shader textures and D3D objects use explicit shutdown and COM smart pointers. Reader model/RTTI caches now have size bounds. Process, snapshot and timer handles were reviewed for cleanup.

Window lookup is cached. Delayed DXGI tracing is disabled by default; use --game-frames only for experiments. Full-screen high-refresh transparent-window presentation and glass effects still consume GPU time; this audit does not isolate their individual costs.

The short runtime sample and stress checks show no sustained leak evidence. They do not establish absence of rare leaks over hours, across every resize/display mode, or every game transition. Raw samples: resources-before.json, resources-after.json, gpu-after.json, gpu-memory-after.json, resource-test.json. All four CTest suites passed.


The final 6,600-frame run passed all cycles. In the final measured 1,000-frame phase, private memory stayed at 96,608,256 bytes, handles at 1635, GDI objects at 7 and USER objects at 18. All four suites passed in 88.35 seconds.

