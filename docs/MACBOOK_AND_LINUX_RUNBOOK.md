# Cerious Desktop OS-Agnostic Runbook

This repo is the Cerious Desktop version lane.

It supports two operating modes:

1. MacBook or workstation desktop client.
2. Linux backend server running the C++ gateway/exchange/price services.

The browser terminal and OpenFin Desktop both use the same backend contract. The
desktop shell does not own trading logic.

## Architecture Contract

```text
MacBook OpenFin Desktop or browser
        |
        v
Cerious Gateway C++ service
        |
        +-- Cerious Exchange C++ deterministic FIFO simulator
        +-- Databento C++ live MBP-1 feed child process
        +-- Databento C++ historical/chart child process
        +-- File-backed workspace, algo, product, advisory, fills state
```

React is a terminal. Zustand is a render cache. C++ owns market data, studies,
orders, fills, PnL, state, and exchange simulation.

## MacBook: Pull And Run Against Local Backend

Prerequisites:

- macOS with Xcode Command Line Tools.
- Node.js 20+.
- CMake 3.24+.
- Ninja.
- Git.

Typical setup:

```bash
xcode-select --install
brew install node cmake ninja git
git clone https://github.com/tsturiale/Cerious-Systems---Desktop-OS-Agnostic-.git
cd Cerious-Systems---Desktop-OS-Agnostic-
npm run bootstrap:unix
cp .env.example .env
```

Edit `.env`:

```bash
DATABENTO_API_KEY=your_key_here
CERIOUS_PORTAL_USERNAME=tsturiale
CERIOUS_PORTAL_PASSWORD=your_password_here
CERIOUS_BACKEND_HOST=127.0.0.1
CERIOUS_BACKEND_PORT=8000
CERIOUS_EXCHANGE_HOST=127.0.0.1
CERIOUS_EXCHANGE_HTTP_PORT=8011
```

Build:

```bash
npm run build:native:unix
npm run build:frontend
```

Run local backend:

```bash
npm run start:backend:unix
```

Open browser terminal:

```text
http://127.0.0.1:8000/
```

Launch Desktop version:

```bash
npm run desktop:openfin:launch:unix
```

## MacBook: Run Desktop Against Linux Backend

Set the terminal URL to your Linux gateway URL:

```bash
export CERIOUS_TERMINAL_URL="https://your-cerious-host.example.com"
export CERIOUS_DESKTOP_PROFILE="production"
npm run desktop:openfin:launch:unix
```

This generates an OpenFin manifest in:

```text
clients/openfin-terminal/manifests/production.generated.json
```

The Desktop version then opens workspace windows independently through the
server-published workspace contract.

## Linux Backend Server

Prerequisites on Ubuntu/Debian:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git curl zip unzip tar pkg-config nodejs npm
```

Install repo:

```bash
sudo mkdir -p /opt/cerious-systems
sudo chown "$USER":"$USER" /opt/cerious-systems
git clone https://github.com/tsturiale/Cerious-Systems---Desktop-OS-Agnostic-.git /opt/cerious-systems
cd /opt/cerious-systems
npm run bootstrap:unix
npm run build:native:unix
npm run build:frontend
```

Configure:

```bash
sudo mkdir -p /etc/cerious
sudo cp deploy/systemd/cerious.env.example /etc/cerious/cerious.env
sudo nano /etc/cerious/cerious.env
```

Minimum server values:

```bash
CERIOUS_BACKEND_HOST=0.0.0.0
CERIOUS_BACKEND_PORT=8000
CERIOUS_EXCHANGE_HOST=127.0.0.1
CERIOUS_EXCHANGE_HTTP_PORT=8011
DATABENTO_API_KEY=your_key_here
CERIOUS_PORTAL_USERNAME=tsturiale
CERIOUS_PORTAL_PASSWORD=your_password_here
```

Install services:

```bash
sudo cp deploy/systemd/cerious-exchange.service /etc/systemd/system/
sudo cp deploy/systemd/cerious-gateway.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now cerious-exchange
sudo systemctl enable --now cerious-gateway
```

Check:

```bash
systemctl status cerious-exchange
systemctl status cerious-gateway
curl http://127.0.0.1:8011/health
curl http://127.0.0.1:8000/api/health
```

Production should normally sit behind TLS and a reverse proxy. Keep
`cerious-exchange` bound to `127.0.0.1`; expose only the gateway.

## Environment Variables That Matter

```bash
CERIOUS_BACKEND_HOST=127.0.0.1
CERIOUS_BACKEND_PORT=8000
CERIOUS_EXCHANGE_HOST=127.0.0.1
CERIOUS_EXCHANGE_HTTP_PORT=8011
CERIOUS_EXECUTION_DESTINATION=cerious-exchange
DATABENTO_API_KEY=
CERIOUS_PRICE_FEED_SYMBOLS=ES.v.0,NQ.v.0,YM.v.0,RTY.v.0,MES.v.0,MNQ.v.0,MYM.v.0,M2K.v.0
CERIOUS_PORTAL_USERNAME=
CERIOUS_PORTAL_PASSWORD=
CERIOUS_ADMIN_USERNAME=ADMIN
CERIOUS_ADMIN_PASSWORD=12345678
```

## Verification Checklist

After startup:

```bash
curl http://127.0.0.1:8000/api/health
curl http://127.0.0.1:8000/api/market-data/status
curl http://127.0.0.1:8000/api/execution/status
curl http://127.0.0.1:8000/api/workspaces/saved
```

Expected:

- Gateway reports `"runtime":"cpp"`.
- Execution reports `cerious-exchange` healthy.
- Market data reports Databento status, or a clear missing-key/closed-market reason.
- Workspace saved endpoint returns the default workspace.

## What Is Still Platform Specific

Windows-only:

- PowerShell shortcut installer.
- VBS/CMD desktop launcher.
- Windows tray/startup service.

Unix/macOS:

- `scripts/bootstrap-unix.sh`
- `scripts/build-native-unix.sh`
- `scripts/start-backend-unix.sh`
- `scripts/launch-openfin-unix.sh`
- `deploy/systemd/*` for Linux service deployment.

The trading architecture is shared. Only the launch/supervision wrapper changes
by OS.
