#include "processentrywidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QtGlobal>

ProcessEntryWidget::ProcessEntryWidget(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

    m_stopButton = new QToolButton(this);
    m_stopButton->setText(QStringLiteral("🛑"));
    m_stopButton->setToolTip(QStringLiteral("Terminate process tree"));
    m_stopButton->setAutoRaise(true);
    m_stopButton->setCursor(Qt::PointingHandCursor);
    m_stopButton->setFixedSize(20, 20);

    m_text = new QLabel(this);
    m_text->setTextInteractionFlags(Qt::NoTextInteraction);
    m_text->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Preferred);
    m_text->setTextFormat(Qt::RichText);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    layout->addWidget(m_stopButton);
    layout->addWidget(m_text);

    connect(m_stopButton, &QToolButton::clicked, this, [this] {
        emit terminateRequested(m_process);
    });
}

void ProcessEntryWidget::setProcess(const BadProcess &process) {
    m_process = process;
    m_text->setText(QStringLiteral("<b>%1</b> · %2 · %3%")
                        .arg(process.label.toHtmlEscaped())
                        .arg(process.root.pid)
                        .arg(qRound(process.cpuPercent)));
    setToolTip(QStringLiteral("%1\nPID: %2\nCPU: %3%\nProcesses: %4")
                   .arg(process.command)
                   .arg(process.root.pid)
                   .arg(process.cpuPercent, 0, 'f', 1)
                   .arg(process.processCount));
    updateGeometry();
}

void ProcessEntryWidget::setDarkMode(bool dark) {
    const QString color = dark ? QStringLiteral("#f5f5f5") : QStringLiteral("#111111");
    m_text->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(color));
    m_stopButton->setStyleSheet(QStringLiteral("QToolButton { color: %1; border: 0; background: transparent; }").arg(color));
}

void ProcessEntryWidget::setCustomFontEnabled(bool enabled, const QFont &font) {
    if (enabled) {
        m_text->setFont(font);
        m_stopButton->setFont(font);
    } else {
        m_text->setFont(QFont());
        m_stopButton->setFont(QFont());
    }
    updateGeometry();
}
