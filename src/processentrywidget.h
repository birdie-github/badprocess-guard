#pragma once

#include "processmonitor.h"

#include <QToolButton>
#include <QWidget>

class QLabel;

class ProcessEntryWidget : public QWidget {
    Q_OBJECT

public:
    explicit ProcessEntryWidget(QWidget *parent = nullptr);
    void setProcess(const BadProcess &process);
    void setDarkMode(bool dark);
    void setCustomFontEnabled(bool enabled, const QFont &font);

signals:
    void terminateRequested(const BadProcess &process);

private:
    BadProcess m_process;
    QToolButton *m_stopButton = nullptr;
    QLabel *m_text = nullptr;
};
