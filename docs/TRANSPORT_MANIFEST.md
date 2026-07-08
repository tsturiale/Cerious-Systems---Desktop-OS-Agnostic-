# Cerious Systems Transport Manifest

Use this checklist when pushing the build to GitHub, moving to a new computer,
or preparing a server deployment.

## Commit and Transport

Source and application code:

```text
apps/terminal
clients/openfin-terminal
native/cerious-exchange-cpp
native/cerious-host-cpp
native/gateway-cpp
native/price-feed-cpp
native/fix-engine-cpp
tools
packaging
```

Configuration templates and docs:

```text
.env.example
README.md
docs
package.json
.gitignore
.github
```

Portable data/configuration:

```text
assets
data/advisory
data/algo-definitions
data/exchange
data/product-definitions
data/window-payloads
data/workspace-store
```

Launcher/service scripts:

```text
Start-CeriousApp.ps1
Start-CeriousStartupService.ps1
Install-CeriousStartupService.ps1
Launch-Cerious.vbs
Launch-CeriousStartupService.vbs
cerious.ico
```

## Do Not Commit

Secrets and local credentials:

```text
.env
data/credentials/*.json
```

Generated build/runtime output:

```text
node_modules
apps/terminal/dist
native/**/build
build
dist
target
```

Logs and local runtime caches:

```text
*.log
cerious-*.log
data/logs
data/runtime
data/fills/backups
data/algo-definitions/_backups
data/algo-definitions/_deleted
data/snapshots
data/session-backups
data/launcher-backups
data/acme-imports
```

These exclusions are represented in `.gitignore`.

## New Machine Bring-Up

1. Clone the GitHub repository.
2. Create a local `.env` from `.env.example`.
3. Add local API keys and credentials only to `.env` or an ignored credentials
   path.
4. Install Node dependencies for the terminal app.
5. Build the browser terminal:

```powershell
npm.cmd run build:frontend
```

6. Build native C++ services with Visual Studio/CMake.
7. Start Cerious:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CeriousApp.ps1 -HostOnly
```

8. Verify:

```powershell
Invoke-RestMethod http://127.0.0.1:8000/api/health
Invoke-RestMethod http://127.0.0.1:8011/health
Invoke-RestMethod http://127.0.0.1:8000/api/market-data/status
npm.cmd run audit:react-dumb-terminal
```

## Current Runtime Truth

Active:

- Browser React/Zustand terminal.
- C++ gateway.
- C++ deterministic exchange.
- C++ Databento price feed/history helpers.
- C++ host/tray supervisor.

Not active:

- Prior desktop experiments.
- FIX order routing.
- Kalshi outbound order routing.
- Twilio alerting.

Parked source only:

- `native/fix-engine-cpp` for future FIX adapter work.
