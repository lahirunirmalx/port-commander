# Port Commander

A small, maintainable **C + SDL2** Linux GUI for inspecting network port usage.

It reads socket/process data from:
- `lsof -i -P -n -F`
- `/proc/<pid>/status`
- `/proc/<pid>/cmdline`
- `ps -p <pid> -o etime=`

## Features

- Live table of TCP/UDP sockets (PID, protocol, state, command, socket endpoint)
- Fast client-side filtering (`Ctrl+F`)
- Process detail card view for selected PID
- Safe two-step kill flow (SIGTERM only)
- No hidden privilege escalation behavior

## Requirements

- Linux
- `cmake`, `build-essential`, `pkg-config`
- `libsdl2-dev`, `libsdl2-ttf-dev`
- `lsof`, `procps`, `fonts-dejavu-core`

Debian/Ubuntu:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
  libsdl2-dev libsdl2-ttf-dev lsof procps fonts-dejavu-core
```

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/portcommander
```

## Controls

- `F5`: refresh socket list
- `Ctrl+F`: focus filter box
- `Esc`: quit (or exit filter focus / kill confirmation)
- Mouse click on row: load process details
- Mouse wheel: scroll rows
- Kill button: two-click confirmation, sends `SIGTERM`

## Permissions

`lsof` may hide sockets from other users unless run with elevated privileges.

If you need full visibility:

```bash
sudo ./build/portcommander
```

The program itself does **not** run `sudo` for you.

## CI

GitHub Actions workflow at `.github/workflows/build.yml` builds on Ubuntu using SDL2/SDL2_ttf packages.

## License

MIT — see [`LICENSE`](LICENSE).
