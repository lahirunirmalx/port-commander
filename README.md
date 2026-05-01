# APcommander

A small suite of **C + SDL2** Linux GUIs for everyday system tasks.

It bundles three independent binaries:

- **`apcommander`** — dashboard launcher (the main entry point).
- **`portcommander`** — inspect open TCP/UDP sockets and safely kill processes.
- **`wificommander`** — list nearby Wi-Fi networks and start/stop a hotspot via `nmcli`, with a join-QR for the active hotspot.

The two tools are also runnable on their own — the dashboard is just a thin launcher that `fork`/`execvp`s whichever sibling binary you click.

## Why

The Wi-Fi tool exists because GNOME's *"Turn On Wi-Fi Hotspot"* toggle silently fails on some drivers, while the underlying `nmcli device wifi hotspot ssid X password Y` command works fine. `wificommander` is a small SDL2 wrapper around that working flow — type SSID + password, click *Start Hotspot*, scan the QR with your phone.

`portcommander` was the original tool: a focused replacement for `lsof | grep` when you want to find what is holding a port and end it cleanly with `SIGTERM`.

## Requirements

- Linux
- `cmake`, `build-essential`, `pkg-config`
- `libsdl2-dev`, `libsdl2-ttf-dev`
- `lsof`, `procps`, `fonts-dejavu-core`
- `network-manager` (provides `nmcli`) — needed by `wificommander` at runtime
- `qrencode` — optional, used by `wificommander` to render the join-QR for an active hotspot

Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
  libsdl2-dev libsdl2-ttf-dev lsof procps fonts-dejavu-core \
  network-manager qrencode
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

This produces three binaries side by side in `build/`:

```
build/apcommander
build/portcommander
build/wificommander
```

`apcommander` looks for the other two as siblings via `/proc/self/exe`, so they need to live in the same directory (the build directory satisfies this).

## Run

```bash
./build/apcommander
```

Click a tile or press `1` / `2` to launch a tool. The dashboard hides itself while the chosen tool runs and re-appears when you close it. `Esc` / `Q` on the dashboard exits.

You can also run either tool directly:

```bash
./build/portcommander
./build/wificommander
```

### Port Commander

- `F5` — refresh socket list
- `Ctrl+F` — focus filter box
- Click a row — load process details (PID, UID, GID, elapsed time, full command line)
- *Kill (SIGTERM)* button — two-click confirmation; sends `SIGTERM` only (never `SIGKILL`)
- `Esc` — exit filter focus / cancel kill confirm / quit

### Wi-Fi Commander

- Left pane: live list of nearby Wi-Fi networks (`*` = currently connected, with signal / security / SSID / BSSID), with a `Ctrl+F` filter
- Right pane: active interface + connection status, an SSID / password form, a band toggle (Auto / 2.4 GHz / 5 GHz), and *Start Hotspot* / *Stop Hotspot*
- When a hotspot is up, the form goes read-only and a join-QR appears below — scan it with a phone to connect (`qrencode` must be installed for the QR; otherwise the panel shows a hint)

## Permissions

Neither tool escalates privileges on its own.

- `lsof` may hide sockets owned by other users unless run as root. If you need full visibility, run `sudo ./build/portcommander` (or `sudo ./build/apcommander`).
- `nmcli` actions go through PolicyKit / D-Bus, so they work as your normal desktop user without sudo on a standard NetworkManager setup.

## CI

GitHub Actions workflow at [`.github/workflows/build.yml`](.github/workflows/build.yml) builds all three targets on Ubuntu using SDL2 / SDL2_ttf. Each successful run uploads the **`portcommander-linux-x64`** artifact.

## Project layout

```
src/    portcommander  — lsof parser, /proc detail loader, SDL UI, kill action
wifi/   wificommander  — nmcli wrapper, QR codec, hotspot UI
dash/   apcommander    — dashboard / launcher
```

See [`CLAUDE.md`](CLAUDE.md) for the architecture in more depth.

## License

MIT — see [`LICENSE`](LICENSE).
