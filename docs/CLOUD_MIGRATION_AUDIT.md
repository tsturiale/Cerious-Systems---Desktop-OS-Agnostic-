# Cerious Systems Cloud Migration Audit

Last updated: 2026-06-24

## Deployable Package

The deployable package is the repository root, `Cerious local`. Move or clone
this folder as a unit. Runtime code must not depend on legacy project archives,
dated Codex worktrees, browser caches, or any
other path outside this repository root.

## Active Runtime

- Client: React browser terminal served by the native C++ gateway.
- Gateway: `native/gateway-cpp`.
- Active simulation exchange: `native/cerious-exchange-cpp`.
- Market data: `native/price-feed-cpp` Databento CME live MBP-1 and historical chart data.
- Order routing: `native/fix-engine-cpp` when enabled by environment/session config.
- Startup: `Start-CeriousApp.ps1` for local host services; `Start-CeriousStartupService.ps1` for Windows tray/login supervision.

## Tracked Runtime Data

- `data/algo-definitions`: saved algorithm definitions and definition registry.
- `data/product-definitions`: product definitions, tick sizes, tick values, and spread metadata used by native services.
- `data/window-payloads/cerious`: Cerious-native advisory widget payloads.
- `data/workspace-store`: saved workspace layouts and active workspace state.
- `data/workspaces` and `data/workspace-storage`: workspace registry/storage data where present.
- `data/fills`: fill journal state used by the local service layer.
- `.env.example`: documented environment variable contract.

## Ignored Local State

The following are local runtime/history artifacts and are excluded from source
control: `.env`, `data/credentials`, logs, build output, deleted/backed-up algo
definitions, snapshots, launcher backups, session backups, and old imported
legacy archives.

## Server Authority Rule

Trading-critical truth belongs to the native service layer:

- market data, top of book, last trade
- product definitions and tick values
- study snapshots and regression outputs
- algo lifecycle and deployment state
- orders, fills, positions, open PnL, realized PnL
- audit and alert events

The UI renders service-published state and submits typed commands. It must not
calculate fallback trading values, invent product definitions, synthesize fills,
or own order-routing decisions.

## Cloud Migration Rules

1. Clone or copy the repository root as the deployable unit.
2. Provide `.env.example` values through cloud secrets or environment variables.
3. Do not commit the real `.env` or private credential files.
4. Keep product definitions, workspace state, algo definitions, and Cerious
   widget payloads inside `data`.
5. If a future service needs an external location, document the environment
   variable that points to it and keep the default path repository-relative.
6. Run build and health checks before marking a migrated package ready.
