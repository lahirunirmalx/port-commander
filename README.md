# APcommander

A small suite of **C + SDL2** Linux GUIs for everyday system tasks.

It bundles eight independent binaries — one dashboard plus seven tools:

- **`apcommander`** — dashboard launcher (the main entry point).
- **`portcommander`** — inspect open TCP/UDP sockets and safely kill processes.
- **`wificommander`** — list nearby Wi-Fi networks and start/stop a hotspot via `nmcli`, with a join-QR for the active hotspot.
- **`adc_gui`** — 8-channel 24-bit ADC monitor (per-channel calibration, scope traces, statistics) over a serial link.
- **`psu_gui`** — full GUI for a dual-channel DC power supply: VFD readouts, bar meters, scope, keypad, **TRACKING** (link Ch1→Ch2).
- **`psu_gui_single`** — single-channel variant of the full PSU GUI, with a collapsible keypad.
- **`psu_gui_toolbar`** — minimal compact strip for both channels: large V/A readouts, **OUT** toggle, **SET** popup.
- **`psu_gui_toolbar_single`** — single-channel toolbar variant.

The dashboard is a thin launcher: it `fork`/`execvp`s whichever sibling binary you click and hides itself while the child runs. Every tool also runs standalone, so each can be tested in isolation.

## Why

The Wi-Fi tool exists because GNOME's *"Turn On Wi-Fi Hotspot"* toggle silently fails on some drivers, while the underlying `nmcli device wifi hotspot ssid X password Y` command works fine. `wificommander` is a small SDL2 wrapper around that working flow — type SSID + password, click *Start Hotspot*, scan the QR with your phone.

`portcommander` was the original tool: a focused replacement for `lsof | grep` when you want to find what is holding a port and end it cleanly with `SIGTERM`.

The ADC and PSU GUIs were imported from sibling instrument projects (24-bit ADC monitor, ESP32-bridged DC power supply) so all the bench tooling lives behind one launcher.

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

This produces eight binaries side by side in `build/`:

```
build/apcommander                  # dashboard
build/portcommander                # network ports
build/wificommander                # Wi-Fi / hotspot
build/adc_gui                      # 24-bit ADC monitor
build/psu_gui                      # PSU full, dual channel
build/psu_gui_single               # PSU full, single channel
build/psu_gui_toolbar              # PSU compact strip, dual channel
build/psu_gui_toolbar_single       # PSU compact strip, single channel
```

`apcommander` looks for the seven tools as siblings via `/proc/self/exe`, so they need to live in the same directory (the build directory satisfies this). Tiles for missing binaries render with a red `(binary not found)` footer and clicks become no-ops.

## Run

```bash
./build/apcommander
```

Click a tile or press `1`–`7` to launch a tool. The dashboard hides itself while the chosen tool runs and re-appears when you close it. `Esc` / `Q` on the dashboard exits.

You can also run any tool directly:

```bash
./build/portcommander
./build/wificommander
./build/adc_gui                       # default port /dev/ttyUSB0; demo if missing
./build/psu_gui /dev/ttyUSB0
./build/psu_gui_single /dev/ttyACM0
./build/psu_gui_toolbar /dev/ttyUSB0
./build/psu_gui_toolbar_single /dev/ttyUSB0
```

The PSU/ADC tools accept the serial device path as their first argument and fall back to a demo mode when the port can't be opened.

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

### ADC Monitor

- 8 channels with individual enable/disable
- Per-channel calibration (offset, gain, reference)
- Display scaling: µV / mV / V
- Statistics (min / max / avg) and oscilloscope traces
- Falls back to simulated data when the serial port can't be opened

### PSU GUIs

The full GUIs (`psu_gui`, `psu_gui_single`) mimic a bench instrument: VFD-style readouts, bar meters, temperature strip, voltage/current scope traces, on-screen keypad, **OUTPUT** toggle, **SET VOLTAGE** / **SET CURRENT**. The dual variant adds **TRACKING** to mirror Ch1 to Ch2.

The toolbar variants (`psu_gui_toolbar`, `psu_gui_toolbar_single`) are a compact strip: large V/A readouts, **CV/CC** indicator, **OUT** toggle, and a **SET** popup for setpoints.

Both communicate with an ESP32 bridge using a small text protocol (`STATUS <ch>`, `WRITE <ch> <reg> <val>`, `LINK`); see the upstream firmware for details.

## Permissions

Neither tool escalates privileges on its own.

- `lsof` may hide sockets owned by other users unless run as root. If you need full visibility, run `sudo ./build/portcommander` (or `sudo ./build/apcommander`).
- `nmcli` actions go through PolicyKit / D-Bus, so they work as your normal desktop user without sudo on a standard NetworkManager setup.

## CI

GitHub Actions workflow at [`.github/workflows/build.yml`](.github/workflows/build.yml) builds all three targets on Ubuntu using SDL2 / SDL2_ttf. Each successful run uploads the **`portcommander-linux-x64`** artifact.

## Project layout

```
src/    portcommander       — lsof parser, /proc detail loader, SDL UI, kill action
wifi/   wificommander       — nmcli wrapper, QR codec, hotspot UI
adc/    adc_gui             — 24-bit ADC monitor (imported from dev-psu-gui)
psu/    psu_gui*            — DC PSU GUIs (imported from dev-modbus/psu-gui)
dash/   apcommander         — dashboard / launcher
```

The `adc/` and `psu/` trees were imported from sibling instrument projects and are kept self-contained — each has its own copy of `serial_port.{c,h}` and a per-tool protocol module. See [`CLAUDE.md`](CLAUDE.md) for the architecture in more depth.

## License

MIT — see [`LICENSE`](LICENSE).
