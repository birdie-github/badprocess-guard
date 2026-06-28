# badprocess-guard

A small Qt Widgets desktop guard for Linux CPU-heavy processes.

It uses Linux `/proc` CPU accounting. Every sample is treated as current state:
if a watched process tree or an individual process is above threshold, the
compact frameless translucent overlay appears immediately; when nothing is above
threshold, it disappears.

Watched trees are configured in `process-mapping.ini`, not hardcoded in the UI.
A watched tree is shown as one compact line for the root process, with CPU usage
summed across the root and all descendants. Other CPU-heavy processes are shown
individually.

The stop button opens a confirmation dialog with:

- Terminate = `SIGTERM`
- Kill -9 = `SIGKILL`
- Cancel

After Terminate or Kill -9, the monitor performs immediate refreshes that bypass
both the normal sampling interval and the linger duration.

Settings are stored in:

```text
~/.config/badprocess-guard/badprocess-guard.ini
```

Useful setting without UI:

```ini
[window]
AllWorkspaces=true
```

On X11/XFCE this applies the `_NET_WM_STATE_STICKY` EWMH state so the alert
window appears on all workspaces. On Wayland this is not available generically.

The default is `false`; enable it explicitly only if your window manager handles
sticky tool windows correctly.

## Build

Qt5:

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

Qt6:

```bash
cmake -S . -B build-qt6 -DBADPROCESS_GUARD_QT6=ON
cmake --build build-qt6 -j$(nproc)
```

## Run

```bash
./build/badprocess-guard
```

## Defaults

- Sample interval: 5 seconds
- Watched-tree CPU threshold: 50%
- Individual-process CPU threshold: 50%
- Linger duration after a normal sample: 3000 ms
- Overlay opacity: 50%
- Minimum configurable opacity: 10%
- All X11 workspaces: disabled by default

CPU percentages are not divided by CPU count. 100% means one fully busy logical
CPU; 200% means two logical CPUs, and so on.

## Debugging

Useful commands:

```sh
./build/badprocess-guard --debug
./build/badprocess-guard --debug --tree-threshold 1 --process-threshold 1
./build/badprocess-guard --interval-ms 1000 --linger-ms 3000
./build/badprocess-guard --test-alert
```

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
