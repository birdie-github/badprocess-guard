# badprocess-guard

A small Qt Widgets desktop guard for CPU-heavy process trees.

On Linux it uses `/proc` CPU accounting. On Windows it uses the Win32 process
snapshot and process-time APIs. CPU percentages use the same semantics on both
platforms: 100% means one fully busy logical CPU, 200% means two logical CPUs,
and so on.

Every sample is treated as current state: if a watched process tree or an
individual process is above threshold, the compact frameless translucent overlay
appears immediately; when nothing is above threshold, it disappears after the
configured alert duration.

Watched trees are configured in `process-mapping.ini`, not hardcoded in the UI.
A watched tree is shown as one compact line for the root process, with CPU usage
summed across the root and all descendants. Other CPU-heavy processes are shown
individually.

The stop button opens a confirmation dialog with:

- Linux: Close = `SIGTERM`, Kill = `SIGKILL`
- Windows: Close posts `WM_CLOSE` to the process's top-level windows; Kill uses `TerminateProcess`
- Cancel

After Close or Kill, the monitor performs immediate refreshes that bypass
both the normal sampling interval and the alert duration.

Settings are stored in:

```text
~/.config/badprocess-guard/badprocess-guard.ini
```

The configuration schema is intentionally clean and flat:

```ini
[Settings]
RefreshInterval=5000
AlertDuration=3000
Opacity=50
DarkMode=true
Font=
AllWorkspaces=false
TreeThreshold=50
ProcessThreshold=50
```

`RefreshInterval` and `AlertDuration` are milliseconds.

`Font=` empty means the application default font is used. A non-empty Qt font
string enables the custom font.

`AllWorkspaces` is Linux/X11-only. On X11/XFCE this applies the
`_NET_WM_STATE_STICKY` EWMH state so the alert window appears on all workspaces.
On Wayland and Windows it is ignored. The default is `false`.

## Build

Qt5:

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

Qt6:

```bash
cmake -S . -B build-qt6 -DQT6_ENABLE=ON
cmake --build build-qt6 -j$(nproc)
```

## Run

```bash
./build/badprocess-guard
```

Useful overrides:

```bash
./build/badprocess-guard --refresh-interval 1500 --alert-duration 3000
./build/badprocess-guard --debug --tree-threshold 1 --process-threshold 1
./build/badprocess-guard --test-alert
```

## Defaults

- Refresh interval: 5000 ms
- Alert duration after a normal sample: 3000 ms
- Watched-tree CPU threshold: 50%
- Individual-process CPU threshold: 50%
- Overlay opacity: 50%
- Minimum configurable opacity: 10%
- All X11 workspaces: disabled by default

## Debugging

`--debug` prints snapshots, watched roots, aggregate tree CPU percentages,
individual bad processes, and the current bad-process list to stderr.

`--test-alert` shows a fake single-line alert immediately, so it verifies the
window, transparency, animation, settings button and font/theme handling even
when the monitor has found nothing.

## Process mapping

Friendly names and aggregate tree roots live in `process-mapping.ini`. The build
copies this file next to the executable, so `./build/badprocess-guard` will find
it automatically. A user override can be placed at:

```text
~/.config/badprocess-guard/process-mapping.ini
```

Sections:

```ini
[tree-roots]
firefox=Firefox

[exact]
firefox-bin=Firefox
7z=7-Zip

[contains]
libdatetime.so=DateTime
```

Matching is deliberately simple: exact executable basename, or full command line
contains a literal substring. There are no regexes, shell expansion, or commands
executed from the mapping file.

The visible HUD row is intentionally compact:

```text
🛑 Firefox · 1648448 · 72%
```

No title, no `PID`/`CPU` labels, and no command-line arguments in the visible
text. Full details remain available in the tooltip and termination dialog.
