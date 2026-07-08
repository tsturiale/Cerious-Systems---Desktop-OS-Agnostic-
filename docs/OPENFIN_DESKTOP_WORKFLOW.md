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
desktop client marker:

```text
http://127.0.0.1:8000/?cerious_client=openfin&cerious_view=desktop
```

The route marker is only presentation context. It must not move trading logic
into the client.

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

## Future Work

1. Add OpenFin Platform Provider when the desktop layout contract is ready.
2. Add persisted multi-window desktop workspace restore.
3. Package the manifest into a signed installer.
4. Add environment selection for local, staging, and production.
5. Add OpenFin health/log collection to the support workflow.

## Non-Negotiable Rule

Backend services own trading authority. OpenFin and React only display state and
submit typed commands.
