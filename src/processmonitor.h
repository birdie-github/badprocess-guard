#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QHash>
#include <QSet>
#include <QStringList>

struct ProcessIdentity {
    int pid = -1;
    quint64 startTime = 0;

    bool operator==(const ProcessIdentity &other) const {
        return pid == other.pid && startTime == other.startTime;
    }
};

inline uint qHash(const ProcessIdentity &id, uint seed = 0) {
    return qHash(qMakePair(id.pid, id.startTime), seed);
}

struct BadProcess {
    QString label;
    ProcessIdentity root;
    QString command;
    double cpuPercent = 0.0;
    int processCount = 0;
};

class ProcessMonitor : public QObject {
    Q_OBJECT

public:
    explicit ProcessMonitor(QObject *parent = nullptr);

    int intervalMs() const { return m_intervalMs; }
    double treeThresholdPercent() const { return m_treeThresholdPercent; }
    double processThresholdPercent() const { return m_processThresholdPercent; }

public slots:
    void start();
    void setIntervalMs(int ms);
    void setTreeThresholdPercent(double percent);
    void setProcessThresholdPercent(double percent);
    void setDebugEnabled(bool enabled);

signals:
    void badProcessesChanged(const QVector<BadProcess> &processes);

private slots:
    void sample();

private:
    struct ProcInfo {
        ProcessIdentity id;
        int ppid = -1;
        quint64 cpuTicks = 0;
        QString comm;
        QStringList argv;
    };

    struct RootRule {
        QString label;
        QSet<QString> executableBasenames;
    };

    using Snapshot = QHash<int, ProcInfo>;

    Snapshot readSnapshot() const;
    static bool readProc(int pid, ProcInfo *info);
    static QString basenameOfArgv0(const ProcInfo &info);
    static QString commandOf(const ProcInfo &info);
    static QHash<int, QVector<int>> buildChildren(const Snapshot &snapshot);
    static QSet<int> collectTreePids(int rootPid, const QHash<int, QVector<int>> &children);
    QVector<BadProcess> measureBadProcesses(const Snapshot &before, const Snapshot &after, double elapsedSeconds);
    QVector<BadProcess> measureTrees(const Snapshot &before, const Snapshot &after, double elapsedSeconds, QSet<ProcessIdentity> *badTreeMembers) const;
    QVector<BadProcess> measureLeaves(const Snapshot &before, const Snapshot &after, double elapsedSeconds, const QSet<ProcessIdentity> &suppressedMembers) const;

    QTimer m_timer;
    Snapshot m_previous;
    qint64 m_previousNs = 0;
    long m_clockTicks = 100;
    int m_intervalMs = 5000;
    double m_treeThresholdPercent = 50.0;
    double m_processThresholdPercent = 50.0;
    bool m_debug = false;
    QVector<RootRule> m_rules;
    QVector<BadProcess> m_lastEmitted;
};
