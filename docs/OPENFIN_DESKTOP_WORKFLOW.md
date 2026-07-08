# Cerious OpenFin Desktop Workflow

Status: selected Desktop version architecture.

Cerious Desktop uses the OpenFin/HERE runtime to provide a finance-grade desktop
container around the existing Cerious terminal. This keeps the browser workflow
and Desktop workflow on the same backend contract.

## Architecture

```text
C++ backend services
  -> gateway HTTP/WebSocket read models
  -> React/Zustand terminal render cache
  -> Browser client
  -> OpenFin/HERE Desktop client
```

The Desktop version is a shell/runtime layer. It does not replace the backend,
the browser terminal, or Zustand.

## Why This Path

- Preserves the stable Cerious web terminal.
- Adds a natural desktop launch and window shell.
- Supports a future multi-monitor workspace workflow.
- Avoids rebuilding the terminal in a separate native toolkit.
- Keeps backend services as the single authority.

## Runtime Contract

The OpenFin manifest points to the same terminal route as the browser, with a
hidden desktop launcher marker:

```text
http://127.0.0.1:8000/?cerious_client=openfin&cerious_desktop=launcher
```

The launcher opens saved workspace widgets as independent OpenFin windows using
standalone URLs such as:

```text
http://127.0.0.1:8000/?cerious_client=openfin&cerious_window=depthLadder&window_id=depthLadder-3
```

The browser path can still render the canvas. The Desktop version does not use
the canvas as a container.

## Desktop Version Files

```text
clients/openfin-terminal
clients/openfin-terminal/manifests/local.json
clients/openfin-terminal/manifests/staging.template.json
clients/openfin-terminal/manifests/production.template.json
clients/openfin-terminal/scripts/Launch-CeriousOpenFin.ps1
clients/openfin-terminal/installer/README.md
```

## Current Local Launch

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\Start-CeriousApp.ps1 -HostOnly
npm.cmd --prefix clients/openfin-terminal install
npm.cmd --prefix clients/openfin-terminal run launch:local
```

Or use the helper:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\clients\openfin-terminal\scripts\Launch-CeriousOpenFin.ps1 -StartBackend
```

## One-Click Desktop Install

```powershell
npm.cmd --prefix clients/openfin-terminal install
npm.cmd --prefix clients/openfin-terminal run install:shortcut
```

This creates a branded `Cerious Desktop` shortcut on the Windows desktop and in
the Start Menu. The shortcut uses the Cerious icon and launches the OpenFin/HERE
Desktop version without opening a visible console window.

## Local Thin Package

```powershell
npm.cmd --prefix clients/openfin-terminal run package:local
```

Output:

```text
release/CeriousOpenFinDesktop-local.zip
```

This is the local package artifact for the Desktop version. The future
production installer should harden this flow with signing and environment
selection, but the launch contract is now executable.

## Future Work

1. Add OpenFin Platform Provider when the desktop layout contract is ready.
2. Add persisted multi-window desktop workspace restore.
3. Package the manifest into a signed installer.
4. Add environment selection for local, staging, and production.
5. Add OpenFin health/log collection to the support workflow.

## Non-Negotiable Rule

Backend services own trading authority. OpenFin and React only display state and
submit typed commands.
