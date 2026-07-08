# Cerious Qt Native Terminal

This branch introduces a C++/Qt native terminal lane for Cerious Systems.

The Qt terminal is a native client over the existing C++ backend contract. It
does not own trading calculations, matching, PnL, study math, advisory
intelligence, market-data interpretation, or algorithmic decisions.

## Runtime Shape

```text
cerious_qt_terminal
        |
        v
cerious_gateway          HTTP read/write contract
        |
        +-- cerious_exchange_server
        +-- cerious_price_feed
        +-- cerious_price_history
        +-- Linux backend services in future deployment
```

## Current Native Surface

- Gateway health.
- Market-data status.
- Product/market list.
- Working orders.
- Fills.
- Positions.
- Manual order ticket using `/api/order`.

Depth ladders, charts, algo manager, and advisory windows should be added as
native views over backend endpoints. Do not duplicate the React implementation
logic and do not add client-side trading authority.

## Build Requirements

- CMake 3.24+
- C++20 compiler
- Qt 6.5+ with `Widgets` and `Network`
- Ninja recommended

Linux:

```bash
sudo apt-get install -y build-essential cmake ninja-build qt6-base-dev
cmake --preset linux-release
cmake --build --preset linux-release
```

macOS:

```bash
brew install cmake ninja qt
cmake --preset macos-release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build --preset macos-release
```

Windows:

```powershell
cmake --preset windows-msvc -DCMAKE_PREFIX_PATH="C:\Qt\6.7.3\msvc2019_64"
cmake --build --preset windows-msvc
```

## Run

Start the backend first, then launch:

```bash
./build/linux-release/cerious_qt_terminal --gateway http://127.0.0.1:8000
```

The gateway URL can point at a Linux backend server when the services move off
the workstation.
