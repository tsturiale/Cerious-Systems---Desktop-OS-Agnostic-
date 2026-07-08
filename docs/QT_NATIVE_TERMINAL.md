# Qt Native Terminal

This repository is the desktop-native Cerious line. The Qt terminal is a native
C++ client over the existing C++ service contract.

## Design Standard

The Qt terminal is not a second trading engine.

Backend authority remains in C++ services:

- Market data.
- Product definitions.
- Tick size and tick value.
- Linear regression/study calculations.
- Algo definitions and deployment rules.
- Matching, fills, working orders, positions, PnL, drawdown.
- Advisory-widget cadence and payload generation.

Qt responsibilities:

- Render backend snapshots.
- Collect operator input.
- Submit typed commands to backend endpoints.
- Maintain local visual state only.

## OS-Agnostic Shape

```text
Windows/macOS/Linux Qt client
        |
        | HTTP now; binary IPC/Aeron/WebSocket can be added later behind the same client model
        v
Linux-capable C++ gateway
        |
        +-- Linux-capable exchange/order service
        +-- Linux-capable market-data services
        +-- Linux-capable study/analytics services
```

The backend should be deployable on Linux. The client may run on Windows,
macOS, or Linux and point at the backend gateway URL.

## Current Implementation

Location:

```text
clients/qt-terminal
```

Current native views:

- Gateway/market-data/execution health.
- Market/product list.
- Working orders.
- Fills.
- Positions.
- Manual limit order entry through `/api/order`.

The first Qt client deliberately consumes the same server read models as the
browser terminal:

- `GET /api/health`
- `GET /api/markets`
- `GET /api/cerious/order-state`
- `POST /api/order`

## Next Native Milestones

1. Add a backend fast depth-book snapshot endpoint suitable for native ladders.
2. Build the Qt depth ladder against that endpoint.
3. Add Qt Algo Manager as a backend-snapshot view.
4. Add Qt chart view only after the chart data contract is final and server-owned.
5. Replace polling with a normalized backend event stream when the service bus is ready.

Do not port React logic into Qt. Port backend contracts into Qt views.
