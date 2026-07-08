# Chart Engine Work Queue

Last updated: 2026-06-22T12:15:13-05:00

## Queued After Current Stability Work

### Timestamp correctness in chart engine

When implementing the next chart architecture pass, timestamp handling is a required acceptance item, not optional polish.

Observed issue:
- Chart bar hover timestamps and bottom time scale appear stale or shifted.
- Newly opened charts can show old or incorrect time labels immediately.
- Current visual evidence suggests chart times may be 3-4 hours ahead of the expected local/session time.
- Screenshot reference: user-provided chart timestamp capture in the Codex thread.

Required behavior:
- Historical REST bars must carry one canonical timestamp representation from the backend.
- The frontend must convert that timestamp exactly once for chart rendering.
- Lightweight Charts inputs must use the expected epoch-seconds value, not mixed milliseconds and seconds.
- Hover labels, crosshair data, and bottom time axis must agree.
- Chart timestamps should align to the selected bar interval and exchange/session timezone rules.
- Live last-trade updates must update price without forcing the visible chart range or shifting the time axis.

Acceptance checks:
- Open a fresh ES_NQ, YM_ES, and RTY_ES 30m chart and confirm bottom-axis times are not stale.
- Hover several candles and verify the tooltip/crosshair time matches the candle bucket.
- Confirm no chart displays a 3-4 hour offset.
- Confirm saved chart zoom/range remains stable after live last-trade updates.
