#include "alertwindow.h"

#include <QApplication>
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QDesktopWidget>
#endif
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>
#include <QResizeEvent>

#include <signal.h>

static QRect availableGeometryForWindow(QWidget *window) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
    if (QScreen *screen = window->screen())
        return screen->availableGeometry();
    if (QScreen *screen = QGuiApplication::primaryScreen())
        return screen->availableGeometry();
#endif
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    return QApplication::desktop()->availableGeometry(window);
#else
    return QRect(0, 0, 1024, 768);
#endif
}

AlertWindow::AlertWindow(Configuration *config, QWidget *parent)
    : QFrame(parent), m_config(config) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setMouseTracking(true);
    setFixedWidth(180);
    setAnimatedHeight(0);

    m_layout = new QVBoxLayout(this);
    // Right margin leaves room for the floating gear button without spending a
    // separate title row on it.
    m_layout->setContentsMargins(8, 4, 28, 4);
    m_layout->setSpacing(1);

    m_settingsButton = new QToolButton(this);
    m_settingsButton->setText(QStringLiteral("⚙"));
    m_settingsButton->setAutoRaise(true);
    m_settingsButton->setToolTip(QStringLiteral("Settings"));
    m_settingsButton->setCursor(Qt::PointingHandCursor);
    m_settingsButton->setFixedSize(20, 20);
    m_settingsButton->raise();

    m_animation = new QPropertyAnimation(this, QByteArrayLiteral("animatedHeight"), this);
    m_animation->setDuration(180);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_settingsButton, &QToolButton::clicked, this, &AlertWindow::showSettings);
    connect(m_config, &Configuration::changed, this, &AlertWindow::applyConfiguration);
    applyConfiguration();
}

void AlertWindow::setAnimatedHeight(int height) {
    m_animatedHeight = qMax(0, height);
    setFixedHeight(m_animatedHeight);
    update();
    if (m_animatedHeight <= 0 && m_processes.isEmpty())
        hide();
}

void AlertWindow::setBadProcesses(const QVector<BadProcess> &processes) {
    m_processes = processes;

    while (m_entries.size() < processes.size()) {
        auto *entry = new ProcessEntryWidget(this);
        connect(entry, &ProcessEntryWidget::terminateRequested, this, &AlertWindow::confirmTerminate);
        m_layout->addWidget(entry);
        m_entries.append(entry);
    }

    for (int i = 0; i < m_entries.size(); ++i) {
        const bool visible = i < processes.size();
        m_entries[i]->setVisible(visible);
        if (visible) {
            m_entries[i]->setProcess(processes[i]);
            m_entries[i]->setDarkMode(m_config->darkMode());
            m_entries[i]->setCustomFontEnabled(m_config->useCustomFont(), m_config->customFont());
        }
    }

    animateToContentHeight();
}

void AlertWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)
    if (height() <= 0)
        return;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const int alpha = qRound(255.0 * m_config->opacityPercent() / 100.0);
    QColor background = m_config->darkMode() ? QColor(24, 24, 28, alpha) : QColor(245, 245, 245, alpha);
    QColor border = m_config->darkMode() ? QColor(255, 255, 255, 60) : QColor(0, 0, 0, 55);
    const QRectF r = rect().adjusted(1, 1, -1, -1);
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(r, 14, 14);
}

void AlertWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
#else
        m_dragOffset = event->globalPos() - frameGeometry().topLeft();
#endif
        event->accept();
        return;
    }
    QFrame::mousePressEvent(event);
}

void AlertWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_dragging) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        move(event->globalPosition().toPoint() - m_dragOffset);
#else
        move(event->globalPos() - m_dragOffset);
#endif
        event->accept();
        return;
    }
    QFrame::mouseMoveEvent(event);
}

void AlertWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (m_dragging && event->button() == Qt::LeftButton) {
        m_dragging = false;
        m_config->setWindowPosition(pos());
        event->accept();
        return;
    }
    QFrame::mouseReleaseEvent(event);
}

void AlertWindow::applyConfiguration() {
    const QString textColor = m_config->darkMode() ? QStringLiteral("#f5f5f5") : QStringLiteral("#111111");
    setStyleSheet(QStringLiteral(
        "QFrame { background: transparent; }"
        "QToolButton { color: %1; border: 0; background: transparent; font-size: 14px; }"
        "QToolButton:hover { background: rgba(127,127,127,45); border-radius: 5px; }"
    ).arg(textColor));

    if (m_config->useCustomFont())
        setFont(m_config->customFont());
    else
        setFont(QFont());

    for (ProcessEntryWidget *entry : m_entries) {
        entry->setDarkMode(m_config->darkMode());
        entry->setCustomFontEnabled(m_config->useCustomFont(), m_config->customFont());
    }
    update();
    if (!m_processes.isEmpty())
        animateToContentHeight();
}

void AlertWindow::animateToContentHeight() {
    m_layout->activate();
    const int targetHeight = m_processes.isEmpty() ? 0 : contentHeightForRows(m_processes.size());
    const int targetWidth = m_processes.isEmpty() ? width() : contentWidth();

    if (targetWidth > 0 && targetWidth != width())
        setFixedWidth(targetWidth);
    positionSettingsButton();

    if (targetHeight > 0 && !isVisible()) {
        if (m_config->hasWindowPosition()) {
            move(m_config->windowPosition());
        } else {
            const QRect screen = availableGeometryForWindow(this);
            move(screen.right() - width() - 18, screen.top() + 18);
        }
        show();
        raise();
    }

    m_animation->stop();
    m_animation->setStartValue(height());
    m_animation->setEndValue(targetHeight);
    m_animation->start();
}

void AlertWindow::showSettings() {
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(m_config, this);
        m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose, true);
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void AlertWindow::confirmTerminate(const BadProcess &process) {
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Terminate application?"));
    box.setText(QStringLiteral("Terminate %1 PID %2?").arg(process.label).arg(process.root.pid));
    box.setInformativeText(QStringLiteral("%1\n\nCPU tree: %2%")
                               .arg(process.command)
                               .arg(process.cpuPercent, 0, 'f', 1));
    QPushButton *terminate = box.addButton(QStringLiteral("Terminate"), QMessageBox::AcceptRole);
    QPushButton *kill9 = box.addButton(QStringLiteral("Kill -9"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();

    if (box.clickedButton() == terminate)
        ::kill(process.root.pid, SIGTERM);
    else if (box.clickedButton() == kill9)
        ::kill(process.root.pid, SIGKILL);
}

int AlertWindow::contentHeightForRows(int rows) const {
    Q_UNUSED(rows)
    return m_layout->sizeHint().height();
}

int AlertWindow::contentWidth() const {
    const int suggested = m_layout->sizeHint().width();
    return qBound(90, suggested, 520);
}

void AlertWindow::positionSettingsButton() {
    if (!m_settingsButton)
        return;
    m_settingsButton->move(width() - m_settingsButton->width() - 5, 3);
    m_settingsButton->raise();
}

void AlertWindow::resizeEvent(QResizeEvent *event) {
    QFrame::resizeEvent(event);
    positionSettingsButton();
}
