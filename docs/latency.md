# Latency measurements

**Decision: the strobe flips through DirectComposition.**

## Flip bench, 2026-11-05

`bin\Release\flip_bench.exe \\.\DISPLAY1 <method> 200` on `DISPLAY1`, 1280x1024 at a reported 399 Hz, RTX 5070, no game running. Two runs each. All numbers measured, in milliseconds. "Call" is the flip call alone. "Flush" is from before the call to the return of `DwmFlush`.

| Run | Method | Call median | Call p99 | Flush median | Flush p95 | Flush p99 | Flush max |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | `slwa` | 0.724 | 1.226 | 3.139 | 4.062 | 4.674 | 5.668 |
| 1 | `ulw` | 0.126 | 0.573 | 2.659 | 4.123 | 4.606 | 4.741 |
| 1 | `dcomp` | 0.017 | 0.027 | 1.772 | 2.577 | 2.733 | 3.028 |
| 2 | `slwa` | 0.487 | 0.888 | 2.351 | 3.481 | 3.695 | 3.783 |
| 2 | `ulw` | 0.087 | 0.240 | 2.088 | 3.347 | 3.531 | 3.540 |
| 2 | `dcomp` | 0.018 | 0.045 | 1.763 | 2.824 | 3.121 | 3.445 |

`dcomp` passes the gate: it beats `slwa` on median and p99 in both runs.

Limit of this measure: `DwmFlush` shows when a composition pass ended, not that a given change was in it. A DirectComposition commit is asynchronous and could land one pass later, about 2.5 ms at this refresh rate (estimate). Phase 2's PresentMon runs don't settle this either, since they track the game's frames. Only a high-speed camera or a photodiode would.
