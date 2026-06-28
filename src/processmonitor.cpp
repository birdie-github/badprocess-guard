#include "processmonitor.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QDebug>

#include <algorithm>

#include <unistd.h>

static double cpuPercent(quint64 beforeTicks, quint64 afterTicks, double elapsedSeconds, long clockTicks) {
    if (afterTicks <= beforeTicks || elapsedSeconds <= 0.0 || clockTicks <= 0)
        return 0.0;
    return 100.0 * double(afterTicks - beforeTicks) / (double(clockTicks) * elapsedSeconds);
}

ProcessMonitor::ProcessMonitor(QObject *parent) : QObject(parent) {
    m_clockTicks = sysconf(_SC_CLK_TCK);
    if (m_clockTicks <= 0)
        m_clockTicks = 100;

    loadProcessMapping();

    m_timer.setInterval(m_intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ProcessMonitor::sample);
}

void ProcessMonitor::start() {
    m_previous = readSnapshot();
    m_previousNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
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
    const Snapshot current = readSnapshot();
    const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    const qint64 nowMs = nowNs / 1000000LL;
    const double elapsed = qMax(0.001, double(nowNs - m_previousNs) / 1000000000.0);

    const QVector<BadProcess> currentBad = measureBadProcesses(m_previous, current, elapsed);
    const QVector<BadProcess> bad = applyLinger(currentBad, nowMs, honorLinger);

    if (m_debug) {
        QStringList summary;
        for (const BadProcess &process : bad) {
            summary << QStringLiteral("%1 pid=%2 cpu=%3% count=%4")
                           .arg(process.label)
                           .arg(process.root.pid)
                           .arg(process.cpuPercent, 0, 'f', 1)
                           .arg(process.processCount);
        }
        qInfo().noquote() << QStringLiteral("badprocess-guard: sample: %1 processes scanned, elapsed=%2s, linger=%3 ms, honor-linger=%4, bad=[%5]")
                             .arg(current.size())
                             .arg(elapsed, 0, 'f', 3)
                             .arg(m_lingerMs)
                             .arg(honorLinger ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(summary.join(QStringLiteral("; ")));
    }

    bool changed = bad.size() != m_lastEmitted.size();
    if (!changed) {
        for (int i = 0; i < bad.size(); ++i) {
            if (!(bad[i].root == m_lastEmitted[i].root) || qAbs(bad[i].cpuPercent - m_lastEmitted[i].cpuPercent) >= 0.5) {
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        m_lastEmitted = bad;
        emit badProcessesChanged(bad);
    }

    m_previous = current;
    m_previousNs = nowNs;
}

QVector<BadProcess> ProcessMonitor::applyLinger(const QVector<BadProcess> &current, qint64 nowMs, bool honorLinger) {
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
        if (honorLinger && m_lingerMs > 0 && nowMs - lastSeen <= m_lingerMs) {
            result.append(it.value());
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
    return result;
}

bool ProcessMonitor::readProc(int pid, ProcInfo *info) {
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

    info->id = {pid, start};
    info->ppid = ppid;
    info->cpuTicks = utime + stime;
    info->comm = comm;
    info->argv = argv;
    return true;
}

QString ProcessMonitor::basenameOfArgv0(const ProcInfo &info) {
    if (!info.argv.isEmpty())
        return QFileInfo(info.argv.first()).fileName();
    return info.comm;
}

QString ProcessMonitor::commandOf(const ProcInfo &info) {
    if (!info.argv.isEmpty())
        return info.argv.join(QStringLiteral(" "));
    return info.comm;
}

QStringList ProcessMonitor::processMappingSearchPaths() const {
    QStringList paths;

    const QString configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!configDir.isEmpty())
        paths << QDir(configDir).filePath(QStringLiteral("process-mapping.ini"));

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

    if (!loaded) {
        // Last-resort minimal rules.  Normal builds ship process-mapping.ini,
        // so these are only for accidental launches from unusual directories.
        m_mapping.treeRoots.append({QStringLiteral("firefox"), QStringLiteral("Firefox")});
        m_mapping.treeRoots.append({QStringLiteral("thunderbird"), QStringLiteral("Thunderbird")});
        m_mapping.exactNames.insert(QStringLiteral("firefox"), QStringLiteral("Firefox"));
        m_mapping.exactNames.insert(QStringLiteral("firefox-bin"), QStringLiteral("Firefox"));
        m_mapping.exactNames.insert(QStringLiteral("thunderbird"), QStringLiteral("Thunderbird"));
        m_mapping.exactNames.insert(QStringLiteral("thunderbird-bin"), QStringLiteral("Thunderbird"));
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
    for (const ProcInfo &info : snapshot)
        children[info.ppid].append(info.id.pid);
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

QVector<BadProcess> ProcessMonitor::measureBadProcesses(const Snapshot &before, const Snapshot &after, double elapsedSeconds) {
    QSet<ProcessIdentity> badTreeMembers;
    QVector<BadProcess> bad = measureTrees(before, after, elapsedSeconds, &badTreeMembers);
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

QVector<BadProcess> ProcessMonitor::measureTrees(const Snapshot &before, const Snapshot &after, double elapsedSeconds, QSet<ProcessIdentity> *badTreeMembers) const {
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
        const QString base = basenameOfArgv0(root);
        const QString label = rootLabelForBase(base);
        if (label.isEmpty())
            continue;
        if (hasConfiguredRootAncestor(root))
            continue;

        const QSet<int> pids = collectTreePids(root.id.pid, children);
        quint64 beforeTicks = 0;
        quint64 afterTicks = 0;
        int counted = 0;
        QSet<ProcessIdentity> members;

        for (int pid : pids) {
            const auto afterIt = after.constFind(pid);
            if (afterIt == after.constEnd())
                continue;
            const auto beforeIt = beforeById.constFind(afterIt->id);
            if (beforeIt == beforeById.constEnd())
                continue;
            beforeTicks += beforeIt->cpuTicks;
            afterTicks += afterIt->cpuTicks;
            members.insert(afterIt->id);
            ++counted;
        }

        if (!beforeById.contains(root.id))
            continue;

        const double percent = cpuPercent(beforeTicks, afterTicks, elapsedSeconds, m_clockTicks);
        if (m_debug) {
            qInfo().noquote() << QStringLiteral("badprocess-guard: tree root %1 pid=%2 cpu=%3% counted=%4 command=%5")
                                 .arg(label)
                                 .arg(root.id.pid)
                                 .arg(percent, 0, 'f', 1)
                                 .arg(counted)
                                 .arg(commandOf(root));
        }

        if (percent >= m_treeThresholdPercent) {
            for (const ProcessIdentity &member : members)
                badTreeMembers->insert(member);
            bad.append({label, root.id, commandOf(root), percent, counted});
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
        if (suppressedMembers.contains(process.id))
            continue;

        const auto beforeIt = beforeById.constFind(process.id);
        if (beforeIt == beforeById.constEnd())
            continue;

        const double percent = cpuPercent(beforeIt->cpuTicks, process.cpuTicks, elapsedSeconds, m_clockTicks);
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
        bad.append({label, process.id, commandOf(process), percent, 1});
    }
    return bad;
}
