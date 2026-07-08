# Cerious Desktop Installer Direction

This folder is the installer design placeholder for the OpenFin/HERE Desktop
version.

The final installer should install a thin desktop shell, not a second trading
system.

Installer responsibilities:

- Register the Cerious Desktop manifest.
- Install or bootstrap the OpenFin/HERE runtime/RVM dependency.
- Add the Cerious branded desktop/start-menu shortcut.
- Optionally install the local service supervisor for localhost mode.
- Preserve the same backend URL/environment profile used by the browser client.

Installer non-responsibilities:

- No trading logic.
- No market-data logic.
- No order state.
- No algorithm state.
- No PnL or analytics math.

The runtime should always consume the C++ backend service contract.
