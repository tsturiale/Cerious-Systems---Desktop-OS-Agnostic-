# Cerious Systems Current Build Architecture

Status: current cloud-native/local browser build.

This document describes what is built now, what is active at runtime, and what
is intentionally parked for later work.

## Product Shape

Cerious Systems is currently a browser-launched trading terminal backed by
native C++ services. The browser UI is a React/Zustand terminal. The UI does not
own trading calculations, order matching, PnL, study calculations, market data,
or algorithmic execution decisions. It renders server-published state and sends
typed user commands to the backend.

## Active Runtime Stack

The active local stack is:

```text
Browser terminal
    |
    v
cerious_gateway.exe              127.0.0.1:8000
    |
    +-- cerious_exchange_server.exe    127.0.0.1:8011
    +-- cerious_price_feed.exe         Databento live CME feed process
    +-- cerious_price_history.exe      Databento historical/chart backfill
    +-- data/*                         saved workspace, algos, definitions, journals
```

The host/tray layer starts and supervises the local service stack. The normal
browser entry point is:

```text
http://127.0.0.1:8000/
```

## Active Services

### gateway-cpp

Location:

```text
native/gateway-cpp
```

Role:

- Serves the browser terminal.
- Owns the HTTP contract consumed by the UI.
- Normalizes server state into UI-ready read models.
- Coordinates market-data subscriptions.
- Publishes product definitions, study outputs, algo definitions, advisory
  widgets, order state, fill state, PnL summaries, and trade analytics reports.
- Bridges user commands to the active exchange/order service.

Important rule:

The gateway may format read-model payloads for the terminal, but it should not
move business authority into React. Calculations must remain backend-owned.

### cerious-exchange-cpp

Location:

```text
native/cerious-exchange-cpp
```

Role:

- Active local simulation exchange.
- Owns deterministic central-limit-order-book behavior for simulation.
- Owns accepted orders, working orders, cancels, replaces, fills, positions,
  realized PnL, open PnL, session PnL, and drawdown state.
- Publishes order/fill/position read models to the gateway.

Design direction:

- Single backend authority for matching and state.
- UI reports exchange state; it does not decide when fills happen.
- Future external exchange adapters should preserve the same normalized order,
  fill, position, and PnL contract.

### price-feed-cpp

Location:

```text
native/price-feed-cpp
```

Role:

- Databento CME market-data ingestion.
- Live MBP-1 top-of-book and last-trade stream.
- Historical chart and study backfill through the price-history helper.
- Supplies backend market state to depth ladders, charts, studies, advisory
  widgets, and trade analytics.

Notes:

- CME sessions can be closed while the service is healthy.
- Depth ladders should render no live book when the market is closed, while the
  subscription path remains ready for the next market event.

### cerious-host-cpp

Location:

```text
native/cerious-host-cpp
```

Role:

- Local service launcher/supervisor.
- Starts gateway, exchange, price feed, and history helpers.
- Provides the tray/host workflow used by the local browser build.

## Browser UI

Location:

```text
apps/terminal
```

Role:

- React renders the terminal.
- Zustand stores the latest backend snapshots for UI rendering.
- User actions are sent to backend endpoints.

Non-goals:

- No order matching in React.
- No PnL authority in React.
- No study authority in React.
- No market-data business rules in React.
- No exchange-specific business branching in React.

## Cerious Desktop Version Lane

Location:

```text
clients/openfin-terminal
```

Status:

OpenFin/HERE Desktop version work.

Role:

- Launch the existing Cerious terminal in a finance-grade desktop runtime.
- Provide desktop shortcut/window-shell/runtime packaging.
- Connect to the same gateway/backend contract as the browser terminal.
- Render backend snapshots and submit user commands through the same UI.

Non-goals:

- No duplicated matching logic.
- No duplicated PnL logic.
- No duplicated study logic.
- No exchange-specific trading authority inside OpenFin.

This keeps the backend deployable on Linux while allowing browser and Desktop
clients to consume the same service contract.

## State Ownership

Backend-owned:

- Product definitions
- Tick size and tick value
- Live market books
- Historical bars
- Linear regression and study values
- Algo definitions and runtime algo state
- Matching/fills/positions
- Open, closed, and day PnL
- Session drawdown
- Trade analytics
- Advisory widget subscription payloads

Frontend-owned:

- Window layout rendering
- User selection controls
- Local visual interaction state
- Zustand snapshot cache for rendered backend state

## Algorithms

Saved algorithm definitions live under:

```text
data/algo-definitions
```

Workflow:

1. Algo Builder saves reusable definitions.
2. Algo Manager loads definitions from the backend.
3. Staged rows show what is being considered for deployment.
4. Deployed rows become active managed algos.
5. Runtime send prices come from backend study/calculation services according
   to the algo definition.
6. Orders are sent to the active exchange path.

The exchange path is not responsible for deciding algorithm intent. It only
accepts/cancels/replaces/matches orders and publishes fills/state.

## Charts and Studies

Charts consume backend bar and study endpoints. Linear regression is a backend
calculation based on a user-defined lookback supplied by the endpoint request or
the algo definition. There should be no hard-coded lookback period embedded in
the active application contract.

The same backend study authority should serve:

- Charts
- Algo Manager send-price previews
- Active algo order updates
- Advisory widgets that need study values

## Trade Analytics

Trade Analytics import is backend-owned by `gateway-cpp`.

Current behavior:

- UI uploads the CSV file.
- C++ gateway parses it.
- Fills are sorted chronologically.
- FIFO inventory is rebuilt per product.
- Product definitions supply tick size and tick value.
- Historical backend market marks are used to estimate open-position equity
  during the imported fill sequence when available.
- Metrics are returned to the UI as a backend report.

The UI does not parse CSV contents or calculate analytics.

## Advisory Widgets

Advisory widgets include:

- Daily Summary
- GOOSE
- Macro Regime Summary
- Live Spread Signals
- Relative Spread Visuals
- Relative Spread Charts
- Cross-Spread Opportunity Map
- Model Research & Governance

These are backend endpoint payloads. Their cadence and subscription model must
remain server-side. React should render the returned data only.

## Parked FIX Adapter

Location:

```text
native/fix-engine-cpp
```

Status:

- Source exists.
- It is not part of the active runtime stack.
- No `cerious_fix_engine` process is launched in the current build.
- Current order routing does not touch FIX.

Purpose later:

- Future FIX gateway adapter.
- FIX session management.
- FIX message construction/parsing.
- Optional Aeron IPC.
- Technical audit logging for gateway/session errors.

Business rule:

FIX is an infrastructure route adapter, not the business exchange destination
shown in the normal trader workflow.

## Aeron Status

Aeron support currently exists inside the parked FIX adapter lane. It is not yet
the universal service bus for the whole platform.

Target direction:

```text
price-feed-cpp -> Aeron market-data stream
exchange-cpp   -> Aeron order/fill/state stream
algo-cpp       -> Aeron command/state stream
gateway-cpp    -> read-model bridge for browser UI
```

Current reality:

The active browser/local build still uses the C++ gateway, local HTTP service
contracts, exchange process, and price-feed process boundaries.

## Data and Deployable Folders

Important project folders:

```text
apps/terminal
assets
data/advisory
data/algo-definitions
data/exchange
data/product-definitions
data/window-payloads
data/workspace-store
docs
native/cerious-exchange-cpp
native/cerious-host-cpp
native/gateway-cpp
native/price-feed-cpp
native/fix-engine-cpp
packaging
tools
```

Ignored/non-transport items:

```text
.env
data/credentials/*.json
data/logs
native/**/build
apps/terminal/dist
node_modules
*.log
```

Use `.env.example` as the portable configuration reference. Do not commit live
API keys, passwords, private keys, generated logs, or local build output.

## Startup

Canonical local startup:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CeriousApp.ps1 -HostOnly
```

Health checks:

```powershell
Invoke-RestMethod http://127.0.0.1:8000/api/health
Invoke-RestMethod http://127.0.0.1:8011/health
Invoke-RestMethod http://127.0.0.1:8000/api/market-data/status
```

## Transport Readiness Checklist

Before moving to a new computer or server:

1. Confirm `.env.example` is complete and `.env` is not committed.
2. Confirm `data/credentials` does not contain committed secrets.
3. Confirm `native/**/build`, `apps/terminal/dist`, logs, and runtime caches are
   not committed.
4. Commit source, docs, scripts, product definitions, workspace definitions,
   advisory payloads, exchange state schemas, and saved algo definitions.
5. Rebuild native services on the target machine.
6. Run frontend build and React dumb-terminal audit.
7. Start Cerious and check gateway/exchange/market-data health.
