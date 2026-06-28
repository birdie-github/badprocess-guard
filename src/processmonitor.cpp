#include "processmonitor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

#include <algorithm>

#include <unistd.h>

ProcessMonitor::ProcessMonitor(QObject *parent) : QObject(parent) {
    m_clockTicks = sysconf(_SC_CLK_TCK);
    if (m_clockTicks <= 0)
        m_clockTicks = 100;

    m_rules.append({QStringLiteral("Firefox"), {QStringLiteral("firefox")}});
    m_rules.append({QStringLiteral("Thunderbird"), {QStringLiteral("thunderbird")}});

    m_timer.setInterval(m_intervalMs);
    connect(&m_timer, &QTimer::timeout, this, &ProcessMonitor::sample);
}

void ProcessMonitor::start() {
    m_previous = readSnapshot();
    m_previousNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    if (m_debug) {
        qInfo().noquote() << QStringLiteral("badprocess-guard: initial snapshot: %1 processes, interval=%2 ms, threshold=%3%, consecutive=%4")
                             .arg(m_previous.size())
                             .arg(m_intervalMs)
                             .arg(m_treeThresholdPercent, 0, 'f', 1)
                             .arg(m_consecutiveSamples);
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

void ProcessMonitor::setConsecutiveSamples(int count) {
    m_consecutiveSamples = qMax(1, count);
}

void ProcessMonitor::setDebugEnabled(bool enabled) {
    m_debug = enabled;
}

void ProcessMonitor::sample() {
    const Snapshot current = readSnapshot();
    const qint64 nowNs = QDateTime::currentMSecsSinceEpoch() * 1000000LL;
    const double elapsed = qMax(0.001, double(nowNs - m_previousNs) / 1000000000.0);

    const QVector<BadProcess> bad = measureBadTrees(m_previous, current, elapsed);

    if (m_debug) {
        QStringList summary;
        for (const BadProcess &process : bad) {
            summary << QStringLiteral("%1 pid=%2 cpu=%3% count=%4")
                           .arg(process.label)
                           .arg(process.root.pid)
                           .arg(process.cpuPercent, 0, 'f', 1)
                           .arg(process.processCount);
        }
        qInfo().noquote() << QStringLiteral("badprocess-guard: sample: %1 processes scanned, elapsed=%2s, bad=[%3]")
                             .arg(current.size())
                             .arg(elapsed, 0, 'f', 3)
                             .arg(summary.join(QStringLiteral("; ")));
    }

    if (bad.size() != m_lastEmitted.size()) {
        m_lastEmitted = bad;
        emit badProcessesChanged(bad);
    } else {
        bool changed = false;
        for (int i = 0; i < bad.size(); ++i) {
            if (!(bad[i].root == m_lastEmitted[i].root) || qAbs(bad[i].cpuPercent - m_lastEmitted[i].cpuPercent) >= 0.5) {
                changed = true;
                break;
            }
        }
        if (changed) {
            m_lastEmitted = bad;
            emit badProcessesChanged(bad);
        }
    }

    m_previous = current;
    m_previousNs = nowNs;
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
    if (info.argv.isEmpty())
        return QString();
    return QFileInfo(info.argv.first()).fileName();
}

QString ProcessMonitor::commandOf(const ProcInfo &info) {
    if (!info.argv.isEmpty())
        return info.argv.join(QLatin1Char(' '));
    return info.comm;
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

QVector<BadProcess> ProcessMonitor::measureBadTrees(const Snapshot &before, const Snapshot &after, double elapsedSeconds) {
    QHash<ProcessIdentity, ProcInfo> beforeById;
    for (const ProcInfo &info : before)
        beforeById.insert(info.id, info);

    const QHash<int, QVector<int>> children = buildChildren(after);
    QVector<BadProcess> currentlyBad;
    QSet<ProcessIdentity> liveRootIds;

    for (const ProcInfo &root : after) {
        QString label;
        const QString base = basenameOfArgv0(root);
        for (const RootRule &rule : m_rules) {
            if (rule.executableBasenames.contains(base)) {
                label = rule.label;
                break;
            }
        }
        if (label.isEmpty())
            continue;

        liveRootIds.insert(root.id);
        const QSet<int> pids = collectTreePids(root.id.pid, children);
        quint64 beforeTicks = 0;
        quint64 afterTicks = 0;
        int counted = 0;

        for (int pid : pids) {
            const auto afterIt = after.constFind(pid);
            if (afterIt == after.constEnd())
                continue;
            const auto beforeIt = beforeById.constFind(afterIt->id);
            if (beforeIt == beforeById.constEnd())
                continue;
            beforeTicks += beforeIt->cpuTicks;
            afterTicks += afterIt->cpuTicks;
            ++counted;
        }

        if (!beforeById.contains(root.id))
            continue;

        const double delta = afterTicks > beforeTicks ? double(afterTicks - beforeTicks) : 0.0;
        const double percent = 100.0 * delta / (double(m_clockTicks) * elapsedSeconds);
        HighState &state = m_states[root.id];
        if (m_debug) {
            qInfo().noquote() << QStringLiteral("badprocess-guard: root %1 pid=%2 cpu=%3% counted=%4 consecutive=%5 command=%6")
                                 .arg(label)
                                 .arg(root.id.pid)
                                 .arg(percent, 0, 'f', 1)
                                 .arg(counted)
                                 .arg(state.consecutive)
                                 .arg(commandOf(root));
        }
        if (percent >= m_treeThresholdPercent) {
            ++state.consecutive;
        } else {
            state.consecutive = 0;
            state.active = false;
        }

        if (state.consecutive >= m_consecutiveSamples) {
            state.active = true;
            currentlyBad.append({label, root.id, commandOf(root), percent, counted});
        }
    }

    for (auto it = m_states.begin(); it != m_states.end();) {
        if (!liveRootIds.contains(it.key()))
            it = m_states.erase(it);
        else
            ++it;
    }

    std::sort(currentlyBad.begin(), currentlyBad.end(), [](const BadProcess &a, const BadProcess &b) {
        return a.cpuPercent > b.cpuPercent;
    });
    return currentlyBad;
}
