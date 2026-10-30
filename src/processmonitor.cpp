#include "processmonitor.h"
#include "configuration.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#include <algorithm>
#include <cstring>
#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <cerrno>
#include <fcntl.h>
#endif

static double cpuPercent(quint64 beforeUnits, quint64 afterUnits, double elapsedSeconds, double unitsPerSecond) {
    if (afterUnits <= beforeUnits || elapsedSeconds <= 0.0 || unitsPerSecond <= 0.0)
        return 0.0;
    // Deliberately NOT normalized by total CPU count.  100% means one fully
    // busy logical CPU, 200% means two, matching the Linux /proc semantics.
    return 100.0 * double(afterUnits - beforeUnits) / (unitsPerSecond * elapsedSeconds);
}

ProcessMonitor::ProcessMonitor(Configuration *config, QObject *parent)
    : QObject(parent), m_config(config), m_bootId(bootIdentity()) {
    connect(m_config, &Configuration::ignoredApplicationsChanged,
            this, &ProcessMonitor::ignoredApplicationsChanged);
#ifdef Q_OS_WIN
    // GetProcessTimes() returns 100 ns units.  Do not divide by CPU count:
    // one fully busy logical CPU must be reported as ~100%, not Task
    // Manager-style percent of the entire machine.
    m_cpuUnitsPerSecond = 10000000.0;
#else
    m_cpuUnitsPerSecond = double(sysconf(_SC_CLK_TCK));
    if (m_cpuUnitsPerSecond <= 0.0)
        m_cpuUnitsPerSecond = 100.0;
#endif

    loadProcessMapping();

    m_timer.setInterval(m_intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ProcessMonitor::sample);

    m_expiryTimer.setSingleShot(true);
    m_expiryTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_expiryTimer, &QTimer::timeout, this, &ProcessMonitor::expireLinger);
}

void ProcessMonitor::start() {
    if (!m_clock.isValid())
        m_clock.start();
    pruneIgnoreRules();
    m_previous = readSnapshot();
    m_previousNs = m_clock.nsecsElapsed();
    if (m_debug) {
        qInfo().noquote() << QStringLiteral("badprocess-guard: initial snapshot: %1 processes, interval=%2 ms, tree-threshold=%3%, process-threshold=%4%, tree-roots=%5, exact-mappings=%6, contains-mappings=%7")
                             .arg(m_previous.size())
                             .arg(m_intervalMs)
                             .arg(m_treeThresholdPercent, 0, 'f', 1)
                             .arg(m_processThresholdPercent, 0, 'f', 1)
                             .arg(m_mapping.treeRoots.size())
                             .arg(m_mapping.exactNames.size())
                             .arg(m_mapping.containsNames.size());
    }
    m_timer.start();
}

void ProcessMonitor::setIntervalMs(int ms) {
    m_intervalMs = qMax(250, ms);
    m_timer.setInterval(m_intervalMs);
}

void ProcessMonitor::setTreeThresholdPercent(double percent) {
    m_treeThresholdPercent = qMax(0.0, percent);
}

void ProcessMonitor::setProcessThresholdPercent(double percent) {
    m_processThresholdPercent = qMax(0.0, percent);
}

void ProcessMonitor::setLingerMs(int ms) {
    m_lingerMs = qMax(0, ms);
    scheduleExpiryTimer(monotonicMs());
}

void ProcessMonitor::refreshNow() {
    sampleInternal(false);
}

void ProcessMonitor::setDebugEnabled(bool enabled) {
    m_debug = enabled;
}

void ProcessMonitor::sample() {
    sampleInternal(true);
}

void ProcessMonitor::sampleInternal(bool honorLinger) {
    if (!m_clock.isValid())
        m_clock.start();

#ifdef Q_OS_WIN
    // Periodic, expiry and explicit refreshes can arrive together. Avoid
    // measuring Windows CPU accounting over a tiny interval or scanning twice
    // in succession. The expiry timer also delivers any deferred refresh.
    if (!m_previous.isEmpty()) {
        const qint64 remainingNs = 125000000LL - (m_clock.nsecsElapsed() - m_previousNs);
        if (remainingNs > 0) {
            m_expiryTimer.start(int((remainingNs + 999999LL) / 1000000LL));
            return;
        }
    }
#endif

    pruneIgnoreRules();
    const Snapshot current = readSnapshot();
    const qint64 nowNs = m_clock.nsecsElapsed();
    const qint64 nowMs = nowNs / 1000000LL;
    const double elapsed = qMax(0.001, double(nowNs - m_previousNs) / 1000000000.0);

    const QVector<BadProcess> currentBad = measureBadProcesses(m_previous, current, elapsed);
    const QVector<BadProcess> bad = filterIgnored(applyLinger(currentBad, current, nowMs, honorLinger), current);
    QSet<ProcessIdentity> visible;
    for (const BadProcess &process : bad)
        visible.insert(process.root);
    for (auto it = m_recentProcesses.begin(); it != m_recentProcesses.end();) {
        if (!visible.contains(it.key())) {
            m_recentLastSeenMs.remove(it.key());
            it = m_recentProcesses.erase(it);
        } else {
            ++it;
        }
    }

    if (m_debug) {
        QStringList summary;
        for (const BadProcess &process : bad) {
            summary << QStringLiteral("%1 pid=%2 cpu=%3% count=%4")
                           .arg(process.label)
                           .arg(process.root.pid)
                           .arg(process.cpuPercent, 0, 'f', 1)
                           .arg(process.processCount);
        }
        qInfo().noquote() << QStringLiteral("badprocess-guard: sample: %1 processes scanned, elapsed=%2s, alert-duration=%3 ms, honor-duration=%4, bad=[%5]")
                             .arg(current.size())
                             .arg(elapsed, 0, 'f', 3)
                             .arg(m_lingerMs)
                             .arg(honorLinger ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(summary.join(QStringLiteral("; ")));
    }

    emitIfChanged(bad);
    scheduleExpiryTimer(nowMs);

    m_previous = current;
    m_previousNs = nowNs;
}

void ProcessMonitor::expireLinger() {
#ifdef Q_OS_WIN
    // A linger timeout only tells us that the last bad sample is old enough;
    // it does not tell us that the process has recovered.  Refresh now so a
    // continuously hot process stays visible instead of disappearing until
    // the next periodic sample.  Linger is bypassed because its timeout has
    // already elapsed.
    sampleInternal(false);
#else
    // Expiry is not evidence of recovery. Recheck CPU usage, but avoid a
    // tiny measurement window if the periodic timer has just sampled.
    if (!m_previous.isEmpty()) {
        const qint64 remainingNs = 125000000LL - (m_clock.nsecsElapsed() - m_previousNs);
        if (remainingNs > 0) {
            m_expiryTimer.start(int((remainingNs + 999999LL) / 1000000LL));
            return;
        }
    }
    // Keep each recovered entry until its own deadline, while refreshing
    // continuously busy entries from the new sample.
    sampleInternal(true);
#endif
}

void ProcessMonitor::emitIfChanged(const QVector<BadProcess> &bad) {
    bool changed = bad.size() != m_lastEmitted.size();
    if (!changed) {
        for (int i = 0; i < bad.size(); ++i) {
            if (!(bad[i].root == m_lastEmitted[i].root) ||
                bad[i].label != m_lastEmitted[i].label ||
                bad[i].command != m_lastEmitted[i].command ||
                bad[i].processCount != m_lastEmitted[i].processCount ||
                bad[i].active != m_lastEmitted[i].active ||
                bad[i].tree != m_lastEmitted[i].tree ||
                bad[i].applicationKey != m_lastEmitted[i].applicationKey ||
                bad[i].executablePath != m_lastEmitted[i].executablePath ||
                bad[i].ignoreUnavailableReason != m_lastEmitted[i].ignoreUnavailableReason ||
                qAbs(bad[i].cpuPercent - m_lastEmitted[i].cpuPercent) >= 0.5) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        m_lastEmitted = bad;
        emit badProcessesChanged(bad);
    }
}

void ProcessMonitor::scheduleExpiryTimer(qint64 nowMs) {
    m_expiryTimer.stop();

    if (m_lingerMs <= 0 || m_recentLastSeenMs.isEmpty())
        return;

    qint64 nextDelay = std::numeric_limits<qint64>::max();
    for (auto it = m_recentLastSeenMs.constBegin(); it != m_recentLastSeenMs.constEnd(); ++it) {
        const qint64 delay = it.value() + m_lingerMs - nowMs;
        if (delay <= 0) {
            nextDelay = 0;
            break;
        }
        nextDelay = qMin(nextDelay, delay);
    }

    if (nextDelay != std::numeric_limits<qint64>::max())
        m_expiryTimer.start(int(qMin<qint64>(nextDelay, std::numeric_limits<int>::max())));
}

qint64 ProcessMonitor::monotonicMs() const {
    return m_clock.isValid() ? m_clock.nsecsElapsed() / 1000000LL : 0;
}

QVector<BadProcess> ProcessMonitor::applyLinger(const QVector<BadProcess> &current, const Snapshot &snapshot,
                                             qint64 nowMs, bool honorLinger) {
    QSet<ProcessIdentity> present;
    QVector<BadProcess> result;

    for (const BadProcess &process : current) {
        present.insert(process.root);
        m_recentProcesses.insert(process.root, process);
        m_recentLastSeenMs.insert(process.root, nowMs);
        result.append(process);
    }

    QVector<ProcessIdentity> toRemove;
    for (auto it = m_recentProcesses.constBegin(); it != m_recentProcesses.constEnd(); ++it) {
        const ProcessIdentity id = it.key();
        if (present.contains(id))
            continue;

        const qint64 lastSeen = m_recentLastSeenMs.value(id, 0);
        if (honorLinger && m_lingerMs > 0 && nowMs - lastSeen < m_lingerMs) {
            // The hold is for live processes that recovered, not a history of
            // exited compiler jobs. Reuse this scan's identity/state whenever
            // possible; a missing entry alone could be a transient read error.
            const auto sampled = snapshot.constFind(id.pid);
            const bool exited = sampled != snapshot.constEnd()
                ? !(sampled->id == id) || sampled->exited
                : runState(id) == RunState::Ended;
            if (exited) {
                toRemove.append(id);
                continue;
            }
            BadProcess lingering = it.value();
            lingering.active = false;
            result.append(lingering);
        } else {
            toRemove.append(id);
        }
    }

    for (const ProcessIdentity &id : toRemove) {
        m_recentProcesses.remove(id);
        m_recentLastSeenMs.remove(id);
    }

    std::sort(result.begin(), result.end(), [](const BadProcess &a, const BadProcess &b) {
        if (a.cpuPercent == b.cpuPercent)
            return a.root.pid < b.root.pid;
        return a.cpuPercent > b.cpuPercent;
    });

    return result;
}

ProcessMonitor::Snapshot ProcessMonitor::readSnapshot() const {
    Snapshot result;
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W entry;
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            ProcInfo info;
            const int pid = static_cast<int>(entry.th32ProcessID);
            info.id.pid = pid;
            info.ppid = static_cast<int>(entry.th32ParentProcessID);
            info.comm = QString::fromWCharArray(entry.szExeFile);
            info.argv << info.comm;

            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
            if (!process) {
                process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, entry.th32ProcessID);
            }
            if (!process)
                continue;

            FILETIME creationTime, exitTime, kernelTime, userTime;
            if (!GetProcessTimes(process, &creationTime, &exitTime, &kernelTime, &userTime)) {
                CloseHandle(process);
                continue;
            }
            info.sampledNs = m_clock.nsecsElapsed();
            info.exited = exitTime.dwLowDateTime != 0 || exitTime.dwHighDateTime != 0;

            ULARGE_INTEGER ct, kt, ut;
            ct.LowPart = creationTime.dwLowDateTime;
            ct.HighPart = creationTime.dwHighDateTime;
            kt.LowPart = kernelTime.dwLowDateTime;
            kt.HighPart = kernelTime.dwHighDateTime;
            ut.LowPart = userTime.dwLowDateTime;
            ut.HighPart = userTime.dwHighDateTime;

            wchar_t imagePath[32768];
            DWORD pathLen = DWORD(sizeof(imagePath) / sizeof(imagePath[0]));
            if (QueryFullProcessImageNameW(process, 0, imagePath, &pathLen) && pathLen > 0) {
                info.argv.clear();
                info.argv << QString::fromWCharArray(imagePath, int(pathLen));
            }

            CloseHandle(process);

            info.id.startTime = ct.QuadPart;
            info.cpuTicks = kt.QuadPart + ut.QuadPart;
            result.insert(pid, info);
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
#else
    const QDir proc(QStringLiteral("/proc"));
    const QFileInfoList entries = proc.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo &entry : entries) {
        bool ok = false;
        const int pid = entry.fileName().toInt(&ok);
        if (!ok)
            continue;
        ProcInfo info;
        if (readProc(pid, &info))
            result.insert(pid, info);
    }
#endif
    return result;
}

bool ProcessMonitor::readProc(int pid, ProcInfo *info) {
#ifdef Q_OS_WIN
    Q_UNUSED(pid)
    Q_UNUSED(info)
    return false;
#else
    QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString stat = QString::fromUtf8(statFile.readAll());
    const int open = stat.indexOf(QLatin1Char('('));
    const int close = stat.lastIndexOf(QLatin1Char(')'));
    if (open < 0 || close <= open)
        return false;

    const QString comm = stat.mid(open + 1, close - open - 1);
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    const QStringList rest = stat.mid(close + 2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
#else
    const QStringList rest = stat.mid(close + 2).split(QLatin1Char(' '), QString::SkipEmptyParts);
#endif
    if (rest.size() <= 19)
        return false;

    bool okPpid = false, okUtime = false, okStime = false, okStart = false;
    const int ppid = rest.at(1).toInt(&okPpid);
    const quint64 utime = rest.at(11).toULongLong(&okUtime);
    const quint64 stime = rest.at(12).toULongLong(&okStime);
    const quint64 start = rest.at(19).toULongLong(&okStart);
    if (!okPpid || !okUtime || !okStime || !okStart)
        return false;

    QFile cmdFile(QStringLiteral("/proc/%1/cmdline").arg(pid));
    QStringList argv;
    if (cmdFile.open(QIODevice::ReadOnly)) {
        const QByteArray raw = cmdFile.readAll();
        const QList<QByteArray> parts = raw.split('\0');
        for (const QByteArray &part : parts) {
            if (!part.isEmpty())
                argv << QString::fromLocal8Bit(part);
        }
    }

    if (!argv.isEmpty()) {
        for (const QChar character : argv.first()) {
            if (character.isSpace()) {
                const QString executable = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
                info->executableName = QFileInfo(executable).fileName();
                break;
            }
        }
    }

    info->id = {pid, start};
    info->ppid = ppid;
    info->cpuTicks = utime + stime;
    info->exited = rest.at(0) == QLatin1String("Z") || rest.at(0) == QLatin1String("X") ||
                   rest.at(0) == QLatin1String("x");
    info->comm = comm;
    info->argv = argv;
    return true;
#endif
}

QString ProcessMonitor::basenameOfArgv0(const ProcInfo &info) {
    if (!info.argv.isEmpty()) {
        const QString &argv0 = info.argv.first();

        QString windowsPath = argv0;
        if (windowsPath.size() >= 2 && windowsPath.startsWith(QLatin1Char('"')) &&
            windowsPath.endsWith(QLatin1Char('"'))) {
            windowsPath = windowsPath.mid(1, windowsPath.size() - 2);
        }
        const bool drivePath = windowsPath.size() >= 3 && windowsPath.at(0).isLetter() &&
                               windowsPath.at(1) == QLatin1Char(':') &&
                               (windowsPath.at(2) == QLatin1Char('\\') || windowsPath.at(2) == QLatin1Char('/'));
        const bool uncPath = windowsPath.startsWith(QStringLiteral("\\\\"));
        if (drivePath || uncPath) {
            // Wine exposes Windows executable paths in argv[0]. Spaces are
            // part of the path, and backslashes are separators even on Linux.
            const int separator = qMax(windowsPath.lastIndexOf(QLatin1Char('\\')),
                                       windowsPath.lastIndexOf(QLatin1Char('/')));
            return windowsPath.mid(separator + 1);
        }

        // argv[0] may contain a real path with spaces or a rewritten process
        // title. Prefer the kernel's executable name when available; never
        // guess an argument boundary from whitespace. Wine paths above keep
        // their Windows executable name instead of the Wine loader's name.
        if (!info.executableName.isEmpty())
            return info.executableName;

        return QFileInfo(argv0).fileName();
    }
    return info.comm;
}

QString ProcessMonitor::commandOf(const ProcInfo &info) {
    if (!info.argv.isEmpty())
        return info.argv.join(QStringLiteral(" "));
    return info.comm;
}

QStringList ProcessMonitor::processMappingSearchPaths() const {
    QStringList paths;

    const QString userConfigRoot = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (!userConfigRoot.isEmpty())
        paths << QDir(userConfigRoot).filePath(QStringLiteral("badprocess-guard/process-mapping.ini"));

    const QString appConfigDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!appConfigDir.isEmpty())
        paths << QDir(appConfigDir).filePath(QStringLiteral("process-mapping.ini"));

    const QString appDir = QCoreApplication::applicationDirPath();
    paths << QDir(appDir).filePath(QStringLiteral("process-mapping.ini"));
    paths << QDir(appDir).filePath(QStringLiteral("../process-mapping.ini"));
    paths << QDir(appDir).filePath(QStringLiteral("../share/badprocess-guard/process-mapping.ini"));
    paths << QStringLiteral("/usr/share/badprocess-guard/process-mapping.ini");
    paths << QStringLiteral("/usr/local/share/badprocess-guard/process-mapping.ini");

    return paths;
}

void ProcessMonitor::loadProcessMapping() {
    bool loaded = false;
    for (const QString &path : processMappingSearchPaths()) {
        loadProcessMappingFile(path, &loaded);
        if (loaded) {
            if (m_debug)
                qInfo().noquote() << QStringLiteral("badprocess-guard: loaded process mapping: %1").arg(path);
            break;
        }
    }
}

void ProcessMonitor::loadProcessMappingFile(const QString &path, bool *loaded) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QString section;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;

        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;

        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key.isEmpty() || value.isEmpty())
            continue;

        if (section == QLatin1String("tree-roots")) {
            m_mapping.treeRoots.append({key, value});
        } else if (section == QLatin1String("exact")) {
            m_mapping.exactNames.insert(key, value);
        } else if (section == QLatin1String("contains")) {
            m_mapping.containsNames.append(qMakePair(key, value));
        }
    }

    *loaded = !m_mapping.treeRoots.isEmpty() || !m_mapping.exactNames.isEmpty() || !m_mapping.containsNames.isEmpty();
}

QString ProcessMonitor::displayNameFor(const ProcInfo &info) const {
    const QString base = basenameOfArgv0(info);
    const auto exactIt = m_mapping.exactNames.constFind(base);
    if (exactIt != m_mapping.exactNames.constEnd())
        return exactIt.value();

    const QString command = commandOf(info);
    for (const auto &rule : m_mapping.containsNames) {
        if (command.contains(rule.first))
            return rule.second;
    }

    return base.isEmpty() ? info.comm : base;
}

QHash<int, QVector<int>> ProcessMonitor::buildChildren(const Snapshot &snapshot) {
    QHash<int, QVector<int>> children;
    for (const ProcInfo &info : snapshot) {
        const auto parent = snapshot.constFind(info.ppid);
        // Windows retains the original parent PID even after that parent exits.
        if (parent != snapshot.constEnd() && parent->id.startTime <= info.id.startTime)
            children[info.ppid].append(info.id.pid);
    }
    return children;
}

QSet<int> ProcessMonitor::collectTreePids(int rootPid, const QHash<int, QVector<int>> &children) {
    QSet<int> seen;
    QVector<int> stack{rootPid};
    while (!stack.isEmpty()) {
        const int pid = stack.takeLast();
        if (seen.contains(pid))
            continue;
        seen.insert(pid);
        for (int child : children.value(pid))
            stack.append(child);
    }
    return seen;
}

double ProcessMonitor::processCpuPercent(const ProcInfo &before, const ProcInfo &after, double elapsedSeconds) const {
#ifdef Q_OS_WIN
    // Each process is read at a different point in the scan. Its CPU delta
    // must use the interval between its own readings, not the scan end times.
    elapsedSeconds = double(after.sampledNs - before.sampledNs) / 1000000000.0;
#endif
    return cpuPercent(before.cpuTicks, after.cpuTicks, elapsedSeconds, m_cpuUnitsPerSecond);
}

QVector<BadProcess> ProcessMonitor::measureBadProcesses(const Snapshot &before, const Snapshot &after, double elapsedSeconds) {
    QSet<ProcessIdentity> badTreeMembers;
    QSet<ProcessIdentity> treeRootsSeen;
    QVector<BadProcess> bad = measureTrees(before, after, elapsedSeconds, &badTreeMembers, &treeRootsSeen);

    for (const ProcessIdentity &member : badTreeMembers) {
        if (treeRootsSeen.contains(member))
            continue;
        m_recentProcesses.remove(member);
        m_recentLastSeenMs.remove(member);
    }

    const QVector<BadProcess> leaves = measureLeaves(before, after, elapsedSeconds, badTreeMembers);
    for (const BadProcess &leaf : leaves)
        bad.append(leaf);

    std::sort(bad.begin(), bad.end(), [](const BadProcess &a, const BadProcess &b) {
        if (a.cpuPercent == b.cpuPercent)
            return a.root.pid < b.root.pid;
        return a.cpuPercent > b.cpuPercent;
    });
    return bad;
}

QVector<BadProcess> ProcessMonitor::measureTrees(const Snapshot &before, const Snapshot &after, double elapsedSeconds,
                                                 QSet<ProcessIdentity> *badTreeMembers,
                                                 QSet<ProcessIdentity> *treeRootsSeen) const {
    QHash<ProcessIdentity, ProcInfo> beforeById;
    for (const ProcInfo &info : before)
        beforeById.insert(info.id, info);

    const QHash<int, QVector<int>> children = buildChildren(after);
    QVector<BadProcess> bad;

    auto rootLabelForBase = [this](const QString &base) -> QString {
        for (const RootRule &rule : m_mapping.treeRoots) {
            if (rule.executableBasename == base)
                return rule.label;
        }
        return QString();
    };

    auto hasChromiumStyleTypeArgument = [](const ProcInfo &info) -> bool {
        for (const QString &arg : info.argv) {
            if (arg.startsWith(QLatin1String("--type=")) ||
                arg.contains(QLatin1String(" --type="))) {
                return true;
            }
        }
        return false;
    };

    auto hasConfiguredRootAncestor = [&](const ProcInfo &candidate) -> bool {
        QSet<int> seen;
        int parentPid = candidate.ppid;
        while (parentPid > 1 && !seen.contains(parentPid)) {
            seen.insert(parentPid);
            const auto parentIt = after.constFind(parentPid);
            if (parentIt == after.constEnd())
                return false;
            if (!rootLabelForBase(basenameOfArgv0(*parentIt)).isEmpty())
                return true;
            parentPid = parentIt->ppid;
        }
        return false;
    };

    for (const ProcInfo &root : after) {
        if (root.exited)
            continue;
        const QString base = basenameOfArgv0(root);

        const QString label = rootLabelForBase(base);
        if (label.isEmpty())
            continue;

        // Chromium-family helper processes reuse the browser executable
        // basename ("chrome") but advertise their role via --type=...
        if (hasChromiumStyleTypeArgument(root))
            continue;

        if (hasConfiguredRootAncestor(root))
            continue;

        const QSet<int> pids = collectTreePids(root.id.pid, children);
        double percent = 0.0;
        int counted = 0;
        QSet<ProcessIdentity> members;

        for (int pid : pids) {
            const auto afterIt = after.constFind(pid);
            if (afterIt == after.constEnd())
                continue;
            const auto beforeIt = beforeById.constFind(afterIt->id);
            if (beforeIt == beforeById.constEnd())
                continue;
            percent += processCpuPercent(*beforeIt, *afterIt, elapsedSeconds);
            members.insert(afterIt->id);
            ++counted;
        }

        if (counted == 0)
            continue;

        if (treeRootsSeen)
            treeRootsSeen->insert(root.id);

        if (m_debug) {
            qInfo().noquote() << QStringLiteral("badprocess-guard: tree root %1 pid=%2 cpu=%3% counted=%4 command=%5")
                                 .arg(label)
                                 .arg(root.id.pid)
                                 .arg(percent, 0, 'f', 1)
                                 .arg(counted)
                                 .arg(commandOf(root));
        }

        // Suppress generic per-process alerts for members of configured
        // process trees even when the tree itself is below the tree threshold.
        // Known multi-process applications should be represented by their
        // aggregate tree entry, not by random hot children.
        for (const ProcessIdentity &member : members)
            badTreeMembers->insert(member);

        if (percent >= m_treeThresholdPercent) {
            BadProcess process{label, root.id, commandOf(root), percent, counted};
            process.tree = true;
            describeApplication(&process, root);
            bad.append(process);
        }
    }

    return bad;
}

QVector<BadProcess> ProcessMonitor::measureLeaves(const Snapshot &before, const Snapshot &after, double elapsedSeconds, const QSet<ProcessIdentity> &suppressedMembers) const {
    QHash<ProcessIdentity, ProcInfo> beforeById;
    for (const ProcInfo &info : before)
        beforeById.insert(info.id, info);

    QVector<BadProcess> bad;
    for (const ProcInfo &process : after) {
        if (process.exited || suppressedMembers.contains(process.id))
            continue;

        const auto beforeIt = beforeById.constFind(process.id);
        if (beforeIt == beforeById.constEnd())
            continue;

        const double percent = processCpuPercent(*beforeIt, process, elapsedSeconds);
        if (percent < m_processThresholdPercent)
            continue;

        const QString label = displayNameFor(process);
        if (m_debug) {
            qInfo().noquote() << QStringLiteral("badprocess-guard: process %1 pid=%2 cpu=%3% command=%4")
                                 .arg(label)
                                 .arg(process.id.pid)
                                 .arg(percent, 0, 'f', 1)
                                 .arg(commandOf(process));
        }
        BadProcess alert{label, process.id, commandOf(process), percent, 1};
        describeApplication(&alert, process);
        bad.append(alert);
    }
    return bad;
}

QString ProcessMonitor::bootIdentity() {
#ifdef Q_OS_WIN
    // Windows creation times are absolute FILETIMEs, unlike Linux boot ticks.
    return QStringLiteral("windows-filetime");
#else
    QFile file(QStringLiteral("/proc/sys/kernel/random/boot_id"));
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    const QString boot = QString::fromLatin1(file.readAll()).trimmed();
    return boot.isEmpty() ? QString() : QStringLiteral("linux:") + boot;
#endif
}

ProcessMonitor::RunState ProcessMonitor::runState(const ProcessIdentity &id) {
    if (id.pid <= 0 || id.startTime == 0)
        return RunState::Unknown;
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(id.pid));
    if (!handle)
        return GetLastError() == ERROR_INVALID_PARAMETER ? RunState::Ended : RunState::Unknown;
    FILETIME creation, exit, kernel, user;
    const bool ok = GetProcessTimes(handle, &creation, &exit, &kernel, &user);
    CloseHandle(handle);
    if (!ok)
        return RunState::Unknown;
    ULARGE_INTEGER start;
    start.LowPart = creation.dwLowDateTime;
    start.HighPart = creation.dwHighDateTime;
    return start.QuadPart == id.startTime && exit.dwLowDateTime == 0 && exit.dwHighDateTime == 0
        ? RunState::Running : RunState::Ended;
#else
    const QByteArray path = QStringLiteral("/proc/%1/stat").arg(id.pid).toLocal8Bit();
    const int fd = ::open(path.constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return errno == ENOENT || errno == ESRCH ? RunState::Ended : RunState::Unknown;
    QFile file;
    if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
        ::close(fd);
        return RunState::Unknown;
    }
    const QByteArray stat = file.readAll();
    const int end = stat.lastIndexOf(')');
    if (end < 0)
        return RunState::Unknown;
    const QList<QByteArray> fields = stat.mid(end + 2).simplified().split(' ');
    if (fields.size() <= 19)
        return RunState::Unknown;
    bool ok = false;
    const quint64 start = fields.at(19).toULongLong(&ok);
    if (!ok)
        return RunState::Unknown;
    return start == id.startTime && fields.first() != "Z" && fields.first() != "X" && fields.first() != "x"
        ? RunState::Running : RunState::Ended;
#endif
}

ProcessMonitor::ApplicationIdentity ProcessMonitor::applicationIdentity(const ProcInfo &info) {
    ApplicationIdentity result;
    result.error = QStringLiteral("The executable path could not be verified. You can still ignore this run.");
#ifdef Q_OS_WIN
    // argv[0] comes from QueryFullProcessImageNameW, not the command line.
    if (info.argv.isEmpty())
        return result;
    const QString path = QDir::fromNativeSeparators(info.argv.first());
    if (!QDir::isAbsolutePath(path))
        return result;
    result.path = QDir::cleanPath(path);
    result.key = QStringLiteral("windows:") + result.path.toCaseFolded();
#else
    QString executable = QFileInfo(QStringLiteral("/proc/%1/exe").arg(info.id.pid)).symLinkTarget();
    if (executable.isEmpty())
        return result;
    // Package updates can unlink the old image while it is still running.
    // Keep matching its original path, not the kernel's deletion annotation.
    if (executable.endsWith(QStringLiteral(" (deleted)")) && !QFileInfo::exists(executable))
        executable.chop(10);

    QString argv0 = info.argv.value(0);
    if (argv0.size() >= 2 && argv0.startsWith(QLatin1Char('"')) && argv0.endsWith(QLatin1Char('"')))
        argv0 = argv0.mid(1, argv0.size() - 2);
    const bool drivePath = argv0.size() >= 3 && argv0.at(0).isLetter() && argv0.at(1) == QLatin1Char(':') &&
        (argv0.at(2) == QLatin1Char('\\') || argv0.at(2) == QLatin1Char('/'));
    const bool uncPath = argv0.startsWith(QStringLiteral("\\\\"));
    const QString loader = QFileInfo(executable).fileName();
    const bool wineLoader = loader == QLatin1String("wine") || loader == QLatin1String("wine64") ||
        loader == QLatin1String("wine-preloader") || loader == QLatin1String("wine64-preloader");
    if (drivePath || uncPath || wineLoader) {
        result.error = QStringLiteral("The Wine application and prefix could not be verified. Ignoring the Wine loader would affect other applications; use the current-run option instead.");
        QFile environment(QStringLiteral("/proc/%1/environ").arg(info.id.pid));
        if (!environment.open(QIODevice::ReadOnly))
            return result;
        QString prefix, home;
        const QList<QByteArray> variables = environment.readAll().split('\0');
        for (const QByteArray &variable : variables) {
            if (variable.startsWith("WINEPREFIX="))
                prefix = QString::fromLocal8Bit(variable.mid(11));
            else if (variable.startsWith("HOME="))
                home = QString::fromLocal8Bit(variable.mid(5));
        }
        if (prefix.isEmpty() && !home.isEmpty())
            prefix = QDir(home).filePath(QStringLiteral(".wine"));
        if (!QDir::isAbsolutePath(prefix))
            return result;
        prefix = QFileInfo(prefix).canonicalFilePath();
        if (prefix.isEmpty())
            return result;
        if (drivePath || uncPath) {
            QString windowsPath = QDir::cleanPath(argv0.replace(QLatin1Char('\\'), QLatin1Char('/')));
            if (uncPath && !windowsPath.startsWith(QStringLiteral("//")))
                windowsPath.prepend(QLatin1Char('/'));
            result.path = QStringLiteral("%1 (Wine prefix: %2)").arg(windowsPath, prefix);
            result.key = QStringLiteral("wine:") + prefix + QLatin1Char('\n') + windowsPath.toCaseFolded();
        } else {
            // Some Wine versions retain a Unix executable path in argv[0].
            // Never substitute the loader when that path is unavailable.
            const QString path = QDir::isAbsolutePath(argv0) ? QFileInfo(argv0).canonicalFilePath() : QString();
            if (path.isEmpty() || path == executable ||
                (!path.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) &&
                 !path.endsWith(QStringLiteral(".com"), Qt::CaseInsensitive)))
                return result;
            result.path = QStringLiteral("%1 (Wine prefix: %2)").arg(path, prefix);
            result.key = QStringLiteral("wine-unix:") + prefix + QLatin1Char('\n') + path;
        }
    } else {
        result.path = QDir::cleanPath(executable);
        result.key = QStringLiteral("linux:") + result.path;
    }
    // Do not associate a replacement PID's executable with the sampled run.
    if (runState(info.id) != RunState::Running) {
        result.key.clear();
        result.path.clear();
        return result;
    }
#endif
    result.error.clear();
    return result;
}

void ProcessMonitor::describeApplication(BadProcess *process, const ProcInfo &info) const {
    const ApplicationIdentity application = applicationIdentity(info);
    process->applicationKey = application.key;
    process->executablePath = application.path;
    process->ignoreUnavailableReason = application.error;
}

QString ProcessMonitor::ignoreProcess(const BadProcess &process, bool permanent) {
    IgnoreRule rule;
    rule.label = process.label;
    rule.path = process.executablePath;
    rule.tree = process.tree;
    rule.permanent = permanent;
    if (permanent) {
        // The copied alert identifies the application selected by the user,
        // even if it has exited or exec'd something else during the dialog.
        if (process.applicationKey.isEmpty())
            return process.ignoreUnavailableReason.isEmpty()
                ? QStringLiteral("The selected application's executable path is unavailable.")
                : process.ignoreUnavailableReason;
        rule.applicationKey = process.applicationKey;
    } else {
        if (m_bootId.isEmpty())
            return QStringLiteral("The system boot identity is unavailable. A persistent current-run exclusion cannot be created safely.");
        if (runState(process.root) != RunState::Running)
            return QStringLiteral("The selected process has exited or its identity could not be verified. No exclusion was added.");
        rule.pid = process.root.pid;
        rule.startTime = process.root.startTime;
        rule.bootId = m_bootId;
    }
    m_config->addIgnoreRule(rule);
    return QString();
}

void ProcessMonitor::pruneIgnoreRules() {
    QStringList expired;
    for (const IgnoreRule &rule : m_config->ignoredApplications()) {
        if (!rule.permanent && ((!m_bootId.isEmpty() && rule.bootId != m_bootId) ||
            (rule.bootId == m_bootId && runState({rule.pid, rule.startTime}) == RunState::Ended)))
            expired.append(rule.id);
    }
    // Access-denied/missing metadata is not proof of process exit.
    m_config->removeIgnoreRules(expired);
}

QVector<BadProcess> ProcessMonitor::filterIgnored(const QVector<BadProcess> &bad, const Snapshot &snapshot) const {
    const QVector<IgnoreRule> &rules = m_config->ignoredApplications();
    if (rules.isEmpty())
        return bad;
    QHash<QString, bool> applications;
    QHash<ProcessIdentity, bool> runs;
    for (const IgnoreRule &rule : rules) {
        if (rule.permanent)
            applications[rule.applicationKey] = applications.value(rule.applicationKey) || rule.tree;
        else if (!m_bootId.isEmpty() && rule.bootId == m_bootId) {
            const ProcessIdentity id{rule.pid, rule.startTime};
            runs[id] = runs.value(id) || rule.tree;
        }
    }
    // Resolve only alert candidates and their ancestors, once per filter pass.
    // Keep the complete snapshot intact so ignored children still contribute
    // to the CPU totals of an otherwise monitored parent.
    QHash<int, ApplicationIdentity> identities;
    for (const BadProcess &process : bad) {
        const auto info = snapshot.constFind(process.root.pid);
        if (process.active && info != snapshot.constEnd() && info->id == process.root)
            identities.insert(process.root.pid, {process.applicationKey, process.executablePath, process.ignoreUnavailableReason});
    }
    QVector<BadProcess> result;
    for (const BadProcess &process : bad) {
        bool ignored = runs.contains(process.root) || applications.contains(process.applicationKey);
        auto current = snapshot.constFind(process.root.pid);
        if (current != snapshot.constEnd() && !(current->id == process.root))
            current = snapshot.constEnd();
        QSet<int> visited;
        bool ancestor = false;
        while (!ignored && current != snapshot.constEnd() && !visited.contains(current->id.pid)) {
            visited.insert(current->id.pid);
            const auto run = runs.constFind(current->id);
            ignored = run != runs.constEnd() && (!ancestor || run.value());
            if (!ignored && !applications.isEmpty()) {
                if (!identities.contains(current->id.pid))
                    identities.insert(current->id.pid, applicationIdentity(*current));
                const auto app = applications.constFind(identities.value(current->id.pid).key);
                ignored = app != applications.constEnd() && (!ancestor || app.value());
            }
            const auto parent = snapshot.constFind(current->ppid);
            // On Windows a dead parent's PID can refer to an unrelated newer process.
            if (parent == snapshot.constEnd() || parent->id.startTime > current->id.startTime)
                break;
            current = parent;
            ancestor = true;
        }
        if (!ignored)
            result.append(process);
    }
    return result;
}

void ProcessMonitor::ignoredApplicationsChanged() {
    const QVector<BadProcess> visible = filterIgnored(m_lastEmitted, m_previous);
    QSet<ProcessIdentity> retained;
    for (const BadProcess &process : visible)
        retained.insert(process.root);
    for (auto it = m_recentProcesses.begin(); it != m_recentProcesses.end();) {
        if (!retained.contains(it.key())) {
            m_recentLastSeenMs.remove(it.key());
            it = m_recentProcesses.erase(it);
        } else {
            ++it;
        }
    }
    emitIfChanged(visible);
    scheduleExpiryTimer(monotonicMs());
    // Removing a rule restores alerts at the next normal CPU sample. Do not
    // shorten the measurement window or reset the sampling timer for this UI action.
}
