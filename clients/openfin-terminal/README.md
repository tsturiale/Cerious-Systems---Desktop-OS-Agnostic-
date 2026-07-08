# Cerious OpenFin Desktop

This is the Cerious Desktop version lane.

It packages the existing Cerious terminal in the OpenFin/HERE runtime so the
user gets a managed desktop work experience while the same C++ backend services
remain the authority for trading state.

## Boundary

OpenFin owns:

- Desktop launch.
- Window shell.
- Shortcut and taskbar identity.
- Runtime/container lifecycle.
- Future multi-window and workspace restore.

OpenFin does not own:

- Market data.
- Studies.
- Algorithms.
- Matching.
- Orders.
- Fills.
- Positions.
- PnL.
- Advisory calculations.

Those remain in the C++ backend. React/Zustand remains a render cache and dumb
terminal.

## Local Launch

Start the Cerious backend first:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File ..\..\Start-CeriousApp.ps1 -HostOnly
```

Then launch the Desktop version:

```powershell
npm.cmd --prefix clients/openfin-terminal install
npm.cmd --prefix clients/openfin-terminal run launch:local
```

If the OpenFin CLI is not installed locally yet, this also works:

```powershell
npm.cmd --prefix clients/openfin-terminal run launch:local:npx
```

## macOS / Linux Launch

From the repository root:

```bash
npm run desktop:openfin:launch:unix
```

To launch against a remote Linux/backend server instead of localhost:

```bash
export CERIOUS_TERMINAL_URL="https://your-cerious-host.example.com"
export CERIOUS_DESKTOP_PROFILE="production"
npm run desktop:openfin:launch:unix
```

The Unix launcher generates an OpenFin manifest from the selected terminal URL,
then starts OpenFin through `openfin-cli`.

## One-Click Local Desktop Launcher

Install the branded desktop and Start Menu shortcuts:

```powershell
npm.cmd --prefix clients/openfin-terminal run install:shortcut
```

The shortcut launches:

```text
clients/openfin-terminal/bin/CeriousDesktop.vbs
```

That starts the local backend if needed, waits for gateway health, then launches
the OpenFin/HERE Desktop version.

## Build a Local Thin Package

```powershell
npm.cmd --prefix clients/openfin-terminal run package:local
```

This creates:

```text
release/CeriousOpenFinDesktop-local.zip
```

That zip is the local thin Desktop package. It contains the manifest, launcher,
shortcut installer, and package lock for repeatable OpenFin CLI install.

## Manifest Profiles

```text
manifests/local.json
manifests/staging.template.json
manifests/production.template.json
```

The local manifest launches:

```text
http://127.0.0.1:8000/?cerious_client=openfin&cerious_desktop=launcher
```

That launcher is hidden. It opens each saved workspace item as an independent
OpenFin window using `?cerious_window=<kind>&window_id=<id>`. The Desktop
version does not render the canvas.

Staging and production templates should point to the future cloud terminal host.

## Installer Direction

The installer should remain thin:

1. Install or bootstrap the OpenFin/HERE runtime/RVM layer.
2. Install the Cerious manifest and branded shortcut.
3. Optionally install a local service supervisor for localhost mode.
4. Launch the same terminal URL used by the browser workflow.

No trading logic belongs in the installer or desktop shell.
