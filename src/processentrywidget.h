#pragma once

#include "processmonitor.h"

#include <QColor>
#include <QToolButton>
#include <QWidget>

class QLabel;

class ProcessEntryWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProcessEntryWidget(QWidget *parent = nullptr);
    void setProcess(const BadProcess &process);
    void setEmpty();
    void setDarkMode(bool dark);
    void setNameColor(const QColor &color);
    bool hasActiveProcess() const { return m_process.root.pid > 0 && m_process.active; }
    void setCustomFontEnabled(bool enabled, const QFont &font);

signals:
    void terminateRequested(const BadProcess &process);

private:
    BadProcess m_process;
    QToolButton *m_stopButton = nullptr;
    QLabel *m_name = nullptr;
    QLabel *m_text = nullptr;
};
