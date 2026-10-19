#pragma once

#include <QString>
#include <QtGlobal>

// Paths identify applications; PID + start time + boot identify one run.
// Neither display names nor command-line substrings are used for matching.
struct IgnoreRule {
    QString id;
    QString label;
    QString path;
    QString applicationKey;
    QString bootId;
    int pid = -1;
    quint64 startTime = 0;
    bool permanent = false;
    bool tree = false;
};
