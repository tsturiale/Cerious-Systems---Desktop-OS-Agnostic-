# Deployment Readiness Audit

Last updated: 2026-06-24

## Scope

This audit covers the deployable Cerious Systems root. The goal is a portable
browser terminal plus native C++ service stack with no runtime dependency on
legacy project folders.

## Required Folders

- `apps/terminal`: React terminal source.
- `native/gateway-cpp`: C++ HTTP/WebSocket gateway and service authority API.
- `native/cerious-exchange-cpp`: active deterministic FIFO local exchange.
- `native/price-feed-cpp`: Databento live and historical price services.
- `native/fix-engine-cpp`: FIX service lane when enabled.
- `data/algo-definitions`: saved algo definitions.
- `data/product-definitions`: product/tick definition library.
- `data/window-payloads/cerious`: Cerious advisory widget payloads.
- `data/workspace-store`: saved terminal layouts and active workspace state.
- `assets` and `apps/terminal/public`: bundled branding and browser assets.
- `docs`: architecture, migration, and operating documentation.

## Explicitly Excluded

- legacy source archives
- retired simulator source and build output
- logs, build directories, snapshots, session backups, launcher backups
- local `.env` and private credentials

## Runtime Contract

The C++ service layer owns market data, product definitions, study values,
orders, fills, positions, and PnL. The UI consumes state and submits commands.
No UI fallback math is allowed for trading-critical values.

## Verification Commands

```powershell
npm.cmd run build:frontend

& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S native\cerious-exchange-cpp -B native\cerious-exchange-cpp\build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\cerious-exchange-cpp\build --config Release --parallel

& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S native\gateway-cpp -B native\gateway-cpp\build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\gateway-cpp\build --config Release --parallel

& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' -S native\cerious-host-cpp -B native\cerious-host-cpp\build
& 'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe' --build native\cerious-host-cpp\build --config Release --parallel

Invoke-RestMethod http://127.0.0.1:8000/api/health
Invoke-RestMethod http://127.0.0.1:8000/api/execution/status
Invoke-RestMethod http://127.0.0.1:8000/api/cerious/product-definitions
Invoke-RestMethod http://127.0.0.1:8011/health
```

## Pass Criteria

- frontend production build completes
- gateway, exchange, and host C++ builds complete
- gateway health returns `ok: true`, `runtime: cpp`, and `backend: native-cpp`
- execution status is healthy and points to `cerious-exchange`
- product definitions return from the gateway
- exchange health returns `ok: true`
