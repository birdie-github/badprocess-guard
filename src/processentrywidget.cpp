#include "processentrywidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>

static QString elideMiddle(const QString &text, int maxChars) {
    if (text.size() <= maxChars)
        return text;
    const int left = maxChars / 2;
    const int right = maxChars - left - 1;
    return text.left(left) + QStringLiteral("…") + text.right(right);
}

ProcessEntryWidget::ProcessEntryWidget(QWidget *parent) : QWidget(parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_stopButton = new QToolButton(this);
    m_stopButton->setText(QStringLiteral("🛑"));
    m_stopButton->setToolTip(QStringLiteral("Terminate or kill this process tree"));
    m_stopButton->setAutoRaise(true);
    m_stopButton->setCursor(Qt::PointingHandCursor);
    m_stopButton->setFixedSize(26, 24);

    m_text = new QLabel(this);
    m_text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(6);
    layout->addWidget(m_stopButton);
    layout->addWidget(m_text, 1);

    connect(m_stopButton, &QToolButton::clicked, this, [this] {
        emit terminateRequested(m_process);
    });
}

void ProcessEntryWidget::setProcess(const BadProcess &process) {
    m_process = process;
    const QString command = elideMiddle(process.command, 80);
    m_text->setText(QStringLiteral("<b>%1</b>  PID %2  CPU %3%  <span style='opacity:.75'>%4</span>")
                        .arg(process.label.toHtmlEscaped())
                        .arg(process.root.pid)
                        .arg(process.cpuPercent, 0, 'f', 1)
                        .arg(command.toHtmlEscaped()));
    setToolTip(QStringLiteral("%1\nPID: %2\nCPU tree: %3%\nProcesses: %4")
                   .arg(process.command)
                   .arg(process.root.pid)
                   .arg(process.cpuPercent, 0, 'f', 1)
                   .arg(process.processCount));
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
}
