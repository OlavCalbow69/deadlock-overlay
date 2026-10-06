# Presentation and scheduling update

The first DirectComposition trial was a regression. Its nonblocking Present calls returned DXGI_ERROR_WAS_STILL_DRAWING and discarded most frames under load. One live sample recorded 240 loop iterations but only 10 successful presents per second. The reduced CPU call duration did not establish smooth displayed output.

The corrected renderer uses a flip sequential composition swap chain with maximum frame latency 1 and a frame-ready waitable object. It waits before taking the latest reader snapshot and drawing, then presents without the DO_NOT_WAIT flag. Resize preserves the waitable-object flag; cleanup closes the handle. Automatic fallback and `--legacy-renderer` remain available.

The application now requests Windows HIGH_PRIORITY_CLASS at startup. The GUI reports the actual priority class, and frame-performance.json records its numeric value (128 for High). `--normal-priority` provides an A/B comparison. The process remains timer paced and yields while waiting for the GPU; no busy spin was added.

The corrected normal-priority live capture, profile-corrected-composition.json, recorded 29 visible samples. Bars alone averaged 168.3 accepted presents/second (118–207); samples with the menu open averaged 194.4 (94–240, including the activation interval). No busy-queue present errors occurred. Different game activity makes this an observational comparison, not a controlled benchmark. profile-high-priority.json captures the subsequent priority trial.

The FPS counter measures successful overlay presentation submissions, not monitor scanout or game FPS. Monitor refresh is the target. GPU/compositor contention can still prevent 240 FPS. Current tests do not establish that action-packed gameplay stutter is fully resolved.

Diagnostics distinguish CPU submission, Present call duration, frame-ready queue waiting, timer waiting, and reader time. work_ms includes frame-ready waiting. Records are one-second aggregates; the earlier tests did not include queue_wait_ms. Hidden overlays retain the last frame timing record, so correlate with session.json.overlay_visible.

All four suites passed after the queue correction, including 6,600 stress-test frames across three GUI rebuilds. Logic, renderer and console-shutdown checks were repeated after adding priority. The resource checks are bounded stress checks, not a guarantee of long-term leak freedom.

References: [Microsoft frame latency guidance](https://learn.microsoft.com/en-us/windows/uwp/gaming/reduce-latency-with-dxgi-1-3-swap-chains), [GetFrameLatencyWaitableObject](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject), [SetPriorityClass](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-setpriorityclass).
