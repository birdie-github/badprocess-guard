# badprocess-guard

A small Qt Widgets desktop guard for Linux CPU-heavy processes.

It uses Linux `/proc` CPU accounting. Every sample is treated as current state:
if a watched process tree or an individual process is above threshold, the
compact frameless translucent overlay appears immediately; when nothing is above
threshold, it disappears.

Watched trees are currently:

- Firefox roots named `firefox`
- Thunderbird roots named `thunderbird`

A watched tree is shown as one compact line for the root process, with the CPU
usage summed across the root and all descendants. Other CPU-heavy processes are
shown individually.

The stop button opens a confirmation dialog with:

- Terminate = `SIGTERM`
- Kill -9 = `SIGKILL`
- Cancel

Settings are stored in:

```text
~/.config/badprocess-guard/badprocess-guard.ini
```

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
- Overlay opacity: 50%
- Minimum configurable opacity: 10%

CPU percentages are not divided by CPU count. 100% means one fully busy logical
CPU; 200% means two logical CPUs, and so on.

## Debugging

Useful commands:

```sh
./build/badprocess-guard --debug
./build/badprocess-guard --debug --tree-threshold 1 --process-threshold 1
./build/badprocess-guard --test-alert
```

`--debug` prints snapshots, watched roots, aggregate tree CPU percentages,
individual bad processes, and the current bad-process list to stderr.

`--test-alert` shows a fake single-line alert immediately, so it verifies the
window, transparency, animation, settings button and font/theme handling even
when the monitor has found nothing.


Process mapping
---------------

Friendly names and aggregate tree roots live in `process-mapping.ini`.  The
build copies this file next to the executable, so `./build/badprocess-guard`
will find it automatically.  A user override can be placed at:

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

Matching is deliberately simple: exact executable basename, or full command
line contains a literal substring.  There are no regexes, shell expansion, or
commands executed from the mapping file.

The visible HUD row is intentionally compact:

```text
🛑 Firefox · 1648448 · 72%
```

No title, no `PID`/`CPU` labels, and no command-line arguments in the visible
text.  Full details remain available in the tooltip and termination dialog.
