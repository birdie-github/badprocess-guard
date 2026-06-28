# badprocess-guard

A small Qt Widgets desktop guard for Linux process trees.

It watches configured process roots, currently Firefox and Thunderbird, using
Linux `/proc` CPU accounting. When a configured process tree stays above the CPU
threshold, it shows a compact frameless translucent overlay. Each bad process
root appears on one line with a stop button, name, PID, CPU percentage, and a
settings gear.

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
- Tree CPU threshold: 50%
- Required consecutive high samples: 2
- Overlay opacity: 50%
- Minimum configurable opacity: 10%

CPU percentages are not divided by CPU count. 100% means one fully busy logical
CPU; 200% means two logical CPUs, and so on.

## Debugging

The overlay is intentionally invisible when no configured process tree is above
the threshold for the required number of samples. With the defaults this means:

- first CPU interval: 5 seconds;
- second consecutive high interval: another 5 seconds;
- visible only if Firefox or Thunderbird is still above the tree threshold.

Useful commands:

```sh
./build/badprocess-guard --debug
./build/badprocess-guard --debug --tree-threshold 1 --consecutive 1
./build/badprocess-guard --test-alert
```

`--debug` prints snapshots, matching roots, aggregate CPU percentages and the
current bad-process list to stderr.

`--test-alert` shows a fake single-line alert immediately, so it verifies the
window, transparency, animation, settings button and font/theme handling even
when the monitor has found nothing.
