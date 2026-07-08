# Cerious Systems Canonical File Layout

Canonical deployable root:

`Cerious local`

## Folders

`Cerious local`

The complete local/cloud application build. This folder contains the native C++ gateway, native market data/studies/order services, browser terminal assets, Cerious intelligence payloads, workspace state, `.env.example`, and source/build scripts.

## Launch Safety

Browser/local portal launch loads Ted S as saved and does not minimize windows.

## Dependency Rule

Runtime code should not depend on old external worktrees or dated Codex worktrees. If a local file dependency is needed, put it under `data` or `.tools` in `Cerious local`, or use an explicit environment variable documented in `.env`.
