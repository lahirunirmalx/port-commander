# APcommander

A small suite of **C + SDL2** Linux GUIs for everyday system tasks.

It bundles ten independent binaries — one dashboard plus nine tools:

- **`apcommander`** — dashboard launcher (the main entry point).
- **`portcommander`** — inspect open TCP/UDP sockets and safely kill processes.
- **`wificommander`** — list nearby Wi-Fi networks and start/stop a hotspot via `nmcli`, with a join-QR for the active hotspot.
- **`adc_gui`** — 8-channel 24-bit ADC monitor (per-channel calibration, scope traces, statistics) over a serial link.
- **`psu_gui`** — full GUI for a dual-channel DC power supply: VFD readouts, bar meters, scope, keypad, **TRACKING** (link Ch1→Ch2).
- **`psu_gui_single`** — single-channel variant of the full PSU GUI, with a collapsible keypad.
- **`psu_gui_toolbar`** — minimal compact strip for both channels: large V/A readouts, **OUT** toggle, **SET** popup.
- **`psu_gui_toolbar_single`** — single-channel toolbar variant.
- **`dmm_gui`** — bench multimeter: V DC / V AC / A DC / A AC / Ω / Hz with auto-ranging readout, MIN / MAX / AVG stats, HOLD.
- **`eload_gui`** — DC electronic load: CC / CV / CR / CP modes, master INPUT toggle, V / I / P / R live readouts, OVP / OCP / OPP protection.

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

This produces ten binaries side by side in `build/`:

```
build/apcommander                  # dashboard (3×3 tile grid)
build/portcommander                # network ports
build/wificommander                # Wi-Fi / hotspot
build/adc_gui                      # 24-bit ADC monitor
build/psu_gui                      # PSU full, dual channel
build/psu_gui_single               # PSU full, single channel
build/psu_gui_toolbar              # PSU compact strip, dual channel
build/psu_gui_toolbar_single       # PSU compact strip, single channel
build/dmm_gui                      # bench multimeter
build/eload_gui                    # DC electronic load
```

`apcommander` looks for the nine tools as siblings via `SDL_GetBasePath()`, so they need to live in the same directory (the build directory satisfies this). Tiles for missing binaries render with a red `(binary not found)` footer and clicks become no-ops.

## Run

```bash
./build/apcommander
```

Click a tile or press `1`–`9` to launch a tool. The dashboard is a 3×3 grid and stays visible — you can launch multiple tools (up to 32 in parallel) and even start two of the same tool side by side; each tile shows a green border and `● N running` count for its live children. The dashboard reaps finished children every frame so the counters drop automatically when you close a tool. `Esc` / `Q` exits the dashboard; any still-running tools detach and keep working.

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

### Bench Multimeter (`dmm_gui`)

Six-mode DMM: **V DC**, **V AC**, **A DC**, **A AC**, **Ω**, **Hz**. Big VFD-style readout auto-ranges between µ / m / base / k / M units. Side panel tracks **MIN** / **MAX** / **AVG** with a per-mode reset (changing mode also resets stats). **HOLD** freezes the display; **AUTO** toggles auto-ranging. Hotkeys `1`–`6` select mode, `H` toggles HOLD, `R` resets stats. Demo mode generates plausible values for each mode; pass a serial device path on argv[1] to wire up a real backend later.

### DC Electronic Load (`eload_gui`)

Four modes: **CC** (constant current), **CV** (constant voltage), **CR** (constant resistance), **CP** (constant power). Setpoint is per-mode (so switching modes preserves each mode's last value); use **+ / −** or `↑ / ↓` to nudge, or click the field to type. Master **INPUT ON / OFF** lives in the header. Live **V / I / P / R** readouts in a 2×2 grid. Footer slots cycle through OVP / OCP / OPP presets — when a protection trips, INPUT auto-cuts and the trip reason shows in the footer until cleared (click INPUT to reset). Hotkeys `1`–`4` select mode, `Space` toggles INPUT, `↑ / ↓` adjust the active setpoint. Demo mode models a 12 V / 0.05 Ω source.

## Permissions

Neither tool escalates privileges on its own.

- `lsof` may hide sockets owned by other users unless run as root. If you need full visibility, run `sudo ./build/portcommander` (or `sudo ./build/apcommander`).
- `nmcli` actions go through PolicyKit / D-Bus, so they work as your normal desktop user without sudo on a standard NetworkManager setup.

## CI

GitHub Actions workflow at [`.github/workflows/build.yml`](.github/workflows/build.yml) builds all three targets on Ubuntu using SDL2 / SDL2_ttf. Each successful run uploads the **`portcommander-linux-x64`** artifact.

## Project layout

```
src/      portcommander       — Linux: lsof parser, /proc detail, SDL UI, kill
wifi/     wificommander       — Linux: nmcli wrapper, QR codec, hotspot UI
adc/      adc_gui             — POSIX: 24-bit ADC monitor (imported)
psu/      psu_gui*            — POSIX: DC PSU GUIs (imported)
dmm/      dmm_gui             — POSIX: bench multimeter (native)
eload/    eload_gui           — POSIX: DC electronic load (native)
dash/     apcommander         — cross-platform: dashboard / launcher
compat/   apcompat (static)   — cross-platform spawn/poll/path helpers
common/   vfd.h               — header-only 5×7 dot-matrix VFD digit renderer
```

### Platform support

| Tool                          | Linux | macOS  | Windows |
| ----------------------------- | :---: | :---:  | :---:   |
| `apcommander`                 |   ✓   |   ✓¹   |   ✓¹    |
| `portcommander`               |   ✓   |   ✗²   |   ✗     |
| `wificommander`               |   ✓   |   ✗³   |   ✗     |
| `adc_gui`, `psu_gui*`         |   ✓   |   ✓¹   |   ✗⁴    |
| `dmm_gui`, `eload_gui`        |   ✓   |   ✓¹   |   ✗⁴    |

¹ Compiles cleanly today; not regularly tested.
² Uses `/proc` and Linux `lsof` output format.
³ Wraps `nmcli` (NetworkManager); macOS / Windows use entirely different APIs.
⁴ `serial_port.c` uses `<termios.h>`; needs a Win32 COM port backend before it can run on Windows.

CMake auto-detects the host and only builds the supported targets. The Windows path in [`compat/compat.c`](compat/compat.c) (CreateProcess / WaitForSingleObject / GetExitCodeProcess) is sketched but uncompiled — porting starts there.

### Standalone vs dashboard

Each tool is a fully independent SDL2 binary with its own `main()` and event loop. Build only the tool you want:

```bash
cmake --build build --target portcommander          # just that one
cmake --build build --target apcommander            # dashboard + all sibling tools
```

The dashboard is purely a launcher (it `compat_spawn`s the chosen binary and polls); it has no dependency on the other tools' source. You can ship a tool without `apcommander` or vice versa.

The `adc/` and `psu/` trees were imported from sibling instrument projects and are kept self-contained — each has its own copy of `serial_port.{c,h}` and a per-tool protocol module. See [`CLAUDE.md`](CLAUDE.md) for the architecture in more depth.

## License

MIT — see [`LICENSE`](LICENSE).
