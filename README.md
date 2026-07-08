# Cerious OpenFin Desktop

Desktop version of Cerious Systems using the OpenFin/HERE runtime over the same
cloud-native React terminal and modular C++ backend services.

The goal of this repository is a professional desktop work experience without
forking trading logic away from the server-side architecture.

## Primary References

- [`docs/CURRENT_BUILD_ARCHITECTURE.md`](docs/CURRENT_BUILD_ARCHITECTURE.md)
- [`docs/TRANSPORT_MANIFEST.md`](docs/TRANSPORT_MANIFEST.md)
- [`docs/OPENFIN_DESKTOP_WORKFLOW.md`](docs/OPENFIN_DESKTOP_WORKFLOW.md)
- [`docs/MACBOOK_AND_LINUX_RUNBOOK.md`](docs/MACBOOK_AND_LINUX_RUNBOOK.md)

- Active client: browser terminal at `http://127.0.0.1:8000/`
- Desktop client: OpenFin/HERE runtime loading the same terminal URL
- UI role: render server-published state and submit typed user commands
- Backend role: own market data, study values, orders, fills, positions, PnL,
  algorithm lifecycle, audit trail, and exchange/simulation routing
- Current market-data path: Databento C++ CME MBP-1/top-of-book/last-trade
  feed plus historical chart backfill
- Current simulation path: `cerious-exchange-cpp`, a C++ deterministic FIFO
  CLOB simulation exchange
- Service transport direction: C++ service boundaries first. Aeron IPC exists in
  the parked FIX adapter lane, but the active local build currently uses the C++
  gateway/exchange/price-feed process contract described below.
- Prior desktop experiments are not the Desktop version path.
- Parked FIX workflow: source exists for future FIX gateway integration, but no FIX
  routing daemon is launched or touched by the current runtime stack.
- Desktop direction: `clients/openfin-terminal` packages the existing terminal
  in OpenFin/HERE and consumes the same backend contract.

## Runtime Architecture

Cerious local now starts as a native service stack:

```text
Chrome or Edge app-mode terminal / local portal
        |
        v
native/gateway-cpp/cerious_gateway.exe       port 8000
        |
        +--> native/cerious-exchange-cpp/cerious_exchange_server.exe   port 8011
        +--> native/price-feed-cpp/cerious_price_feed.exe
        +--> native/price-feed-cpp/cerious_price_history.exe
```

The frontend renders state. Trading state, matching, fills, positions, PnL, price ownership, and order routing belong to native C++ services.

## Desktop Version Lane

This repository is the Cerious Desktop version line. The OpenFin/HERE client
lane lives here:

```text
clients/openfin-terminal
```

It launches the same terminal and backend contract as the browser workflow. It
does not duplicate backend or browser business logic.

## macOS / Linux Quick Start

This repo now includes a Unix lane for MacBook development and Linux backend
deployment.

```bash
git clone https://github.com/tsturiale/Cerious-Systems---Desktop-OS-Agnostic-.git
cd Cerious-Systems---Desktop-OS-Agnostic-
npm run bootstrap:unix
cp .env.example .env
# edit .env with DATABENTO_API_KEY and auth values
npm run build:native:unix
npm run build:frontend
npm run start:backend:unix
```

Desktop client on macOS or Linux:

```bash
npm run desktop:openfin:launch:unix
```

Desktop client against a Linux/server backend:

```bash
export CERIOUS_TERMINAL_URL="https://your-cerious-host.example.com"
export CERIOUS_DESKTOP_PROFILE="production"
npm run desktop:openfin:launch:unix
```

Full runbook:

```text
docs/MACBOOK_AND_LINUX_RUNBOOK.md
```

Validate the Desktop manifests:

```powershell
npm.cmd run desktop:openfin:validate
```

Launch the local Desktop version after starting the backend:

```powershell
npm.cmd --prefix clients/openfin-terminal install
npm.cmd run desktop:openfin:launch
```

## Canonical Startup

Use:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CeriousApp.ps1 -HostOnly
```

The launcher starts:

- `cerious_exchange_server.exe` on `127.0.0.1:8011`
- `cerious_gateway.exe` on `127.0.0.1:8000`

The portal URL is:

[http://127.0.0.1:8000/](http://127.0.0.1:8000/)

On macOS/Linux local development, use:

```bash
npm run start:backend:unix
```

For Windows login startup and tray health, use:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Install-CeriousStartupService.ps1 -RunNow
```

The Cerious Startup Service monitors one contract:

- gateway health: `/api/health`
- market-data session health: `/api/market-data/status`
- execution-session health: `/api/execution/status`

Market-data connection/subscription health and price-book readiness are separate. A connected Databento session can be healthy while waiting for the next MBP-1 price event.

## Native Modules

`native/gateway-cpp`

Native local gateway. Owns the local HTTP contract used by the browser terminal
and proxies order state to the active execution destination.

`native/cerious-exchange-cpp`

Active deterministic local simulation exchange. Owns accepted orders,
cancel/replace, FIFO CLOB matching, market-data-triggered resting limit fills,
positions, and PnL for simulation mode.

`native/price-feed-cpp`

Databento C++ live and historical market data clients.

`native/fix-engine-cpp`

C++ FIX 4.4 order-routing engine and local command/status API. This is parked
future infrastructure. It is not launched by the active local/browser workflow
and is not part of current simulation/manual/algo order routing.

## Data

`data/algo-definitions`

Saved algorithm definitions. The native gateway publishes these to the client through `/api/algo-manager/state`.

`data/workspace-store/tsturiale`

Saved workspace layouts and latest default workspace.

`data/fills`

Fill journals used by the native service layer.

`data/product-definitions`

Exchange/product definitions, including tick size and tick value inputs used by
the C++ exchange and PnL engines.

`data/window-payloads/cerious`

Cerious-native payloads for advisory widgets and research windows.

## Build

Visual Studio and CMake are expected on Windows. If `cmake` is not on PATH, use the Visual Studio bundled CMake:

```powershell
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S native\gateway-cpp -B native\gateway-cpp\build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\gateway-cpp\build --config Release --parallel
```

Repeat for `native\cerious-exchange-cpp`, `native\price-feed-cpp`, and
`native\fix-engine-cpp` as needed.

## Health Checks

```powershell
Invoke-RestMethod http://127.0.0.1:8000/api/health
Invoke-RestMethod http://127.0.0.1:8011/health
```

Expected gateway response includes:

```json
{
  "ok": true,
  "app": "cerious-systems",
  "runtime": "cpp",
  "backend": "native-cpp",
  "executionDestination": "cerious-exchange"
}
```

## Deployment Rule

Do not add trading-critical state or order-routing logic to the UI. If the UI needs a value, the native service layer must publish it.
