# BadProcess Guard

**BadProcess Guard is a lightweight CPU usage monitor and high-CPU process detector for Windows and Linux.**

It runs quietly in the background and automatically alerts you when an application or process starts consuming excessive CPU. Instead of opening Task Manager, Process Explorer, `top`, or `htop` and searching for the culprit manually, BadProcess Guard immediately shows the offending application in a small desktop overlay and lets you close or kill it.

It is designed for situations such as:

* an application unexpectedly using 100% of one CPU core or more;
* a background process causing high CPU usage, excessive fan noise, heat, or battery drain;
* a browser, archiver, compiler, game launcher, updater, or other application getting stuck and consuming CPU;
* identifying runaway or CPU-hogging processes without constantly watching Task Manager or a system monitor;
* monitoring aggregate CPU usage of an entire application process tree, such as a browser and all of its child processes.

BadProcess Guard supports both **Windows and Linux** and uses native process accounting on each platform.

## Features

* Automatically detects high-CPU / CPU-hogging processes.
* Displays a compact alert only when excessive CPU usage is detected.
* Shows the application name, PID, and CPU usage.
* Lets you **Close** or forcibly **Kill** the offending process directly from the alert.
* Supports configurable CPU thresholds.
* Supports configurable sampling intervals and alert duration.
* Can monitor individual processes as well as complete process trees.
* Aggregates CPU usage across parent and child processes for configured applications.
* Supports friendly application names through `process-mapping.ini`.
* Lightweight: no permanent main window and no continuously visible system-monitor interface.
* Native Windows process monitoring using Win32 APIs.
* Native Linux process monitoring using `/proc`.
* Qt5 and Qt6 compatible.
* Open source under the GPL-2.0 license.

## Why BadProcess Guard?

Traditional tools such as Windows Task Manager, Process Explorer, `top`, and `htop` are excellent for investigating CPU usage after you notice a problem. BadProcess Guard solves a slightly different problem: **noticing the problem in the first place**.

A runaway process may consume CPU for minutes or hours before you notice increased fan noise, poor responsiveness, high temperatures, or reduced battery life. BadProcess Guard continuously checks CPU usage in the background and brings the offending process to your attention automatically.

The application is intentionally small and focused. It is **not** intended to replace Task Manager, Process Explorer, `top`, `htop`, or a full system monitor.

## How CPU usage is measured

CPU percentages use the same semantics on Windows and Linux:

* **100%** = one fully occupied logical CPU;
* **200%** = two fully occupied logical CPUs;
* **400%** = four fully occupied logical CPUs.

This makes it possible to detect applications that heavily load one or several CPU cores even on modern multi-core systems.

On Linux, BadProcess Guard reads CPU accounting information from `/proc`.

On Windows, it uses native Win32 process enumeration and process-time APIs.

## Process-tree monitoring

Some applications consist of many processes. Browsers are an obvious example: the parent process itself may use little CPU while several child processes together consume a significant amount.

BadProcess Guard can therefore monitor configured **process trees**. CPU consumption from the root process and all of its descendants is summed and displayed as a single application entry.

Other CPU-heavy processes that are not configured as process trees are reported individually.

## Alert behavior

Every CPU sample represents the current system state.

When a configured process tree or an individual process exceeds its CPU threshold, BadProcess Guard displays a compact frameless desktop alert.

When CPU usage returns below the threshold, the alert disappears after the configured alert duration.

The alert intentionally contains only the essential information:

`🛑 Firefox · 1648448 · 72%`

Full process details remain available in the tooltip and termination dialog.

## Closing processes

The stop button opens a confirmation dialog.

On Linux:

* **Close** sends `SIGTERM`.
* **Kill** sends `SIGKILL`.

On Windows:

* **Close** sends `WM_CLOSE` to the application's top-level windows.
* **Kill** terminates the process using `TerminateProcess`.

After a Close or Kill operation, BadProcess Guard refreshes the process list immediately rather than waiting for the next normal sampling interval.

## Configuration

Settings include:

* CPU sampling / refresh interval;
* alert duration;
* individual-process CPU threshold;
* process-tree CPU threshold;
* overlay opacity;
* dark mode;
* custom font;
* Linux/X11 all-workspaces behavior.

Settings are stored in:

`~/.config/badprocess-guard/badprocess-guard.ini`

The default configuration is:

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

`RefreshInterval` and `AlertDuration` are expressed in milliseconds.

`AllWorkspaces` is Linux/X11-only. On X11 it makes the alert visible on all virtual desktops. It is ignored on Windows and Wayland.

## Process mapping

Friendly application names and aggregate process-tree roots are configured through `process-mapping.ini`.

Example:

```ini
[tree-roots]
firefox=Firefox

[exact]
firefox-bin=Firefox
7z=7-Zip

[contains]
libdatetime.so=DateTime
```

This allows BadProcess Guard to display recognizable application names instead of raw executable names and to treat multi-process applications as a single CPU consumer.

Matching is deliberately simple: executable basename matching or literal command-line substring matching. No regular expressions, shell expansion, or external commands are executed.

## Build

### Qt 5

```bash
cmake -S . -B build
cmake --build build -j$(nproc)
```

### Qt 6

```bash
cmake -S . -B build-qt6 -DQT6_ENABLE=ON
cmake --build build-qt6 -j$(nproc)
```

## Run

```bash
./build/badprocess-guard
```

Useful command-line overrides:

```bash
./build/badprocess-guard --refresh-interval 1500 --alert-duration 3000
./build/badprocess-guard --debug --tree-threshold 1 --process-threshold 1
./build/badprocess-guard --test-alert
```

`--debug` prints process snapshots, watched roots, aggregate process-tree CPU percentages, individual high-CPU processes, and the current alert list.

`--test-alert` displays a fake alert immediately, allowing the UI, transparency, settings button, font, and theme handling to be tested without deliberately creating a CPU-intensive process.

## Default settings

* Refresh interval: **5000 ms**
* Alert duration: **3000 ms**
* Process-tree CPU threshold: **50%**
* Individual-process CPU threshold: **50%**
* Overlay opacity: **50%**
* Minimum configurable opacity: **10%**
* All X11 workspaces: **disabled**

## Platforms

### Windows

BadProcess Guard monitors running processes using native Win32 APIs and can close graphical applications or terminate offending processes directly.

### Linux

BadProcess Guard obtains process and CPU accounting information from `/proc`.

X11 additionally supports displaying the alert across all virtual workspaces.

## Similar tools

BadProcess Guard complements rather than replaces tools such as:

* Windows Task Manager
* Microsoft Process Explorer
* Resource Monitor
* `top`
* `htop`
* `btop`

Those programs are designed primarily for interactive system monitoring and investigation. BadProcess Guard is designed to remain unobtrusive until a CPU-hogging process actually appears.

## About

BadProcess Guard is a lightweight open-source Windows and Linux utility for automatically detecting CPU-hungry applications, runaway processes, and high-CPU process trees.

Project idea by Artem S. Tashkinov. Implementation developed with ChatGPT.
