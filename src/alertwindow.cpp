#include "alertwindow.h"

#include <QApplication>
#include <QFile>
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
#include <QTimer>

#include <cstring>

#ifdef BADPROCESS_GUARD_HAVE_X11
#include <X11/Xlib.h>
#endif

#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <cerrno>
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif



#ifdef Q_OS_WIN
struct CloseWindowContext {
    DWORD pid = 0;
    int posted = 0;
};

static BOOL CALLBACK postCloseToProcessWindow(HWND hwnd, LPARAM lParam) {
    auto *context = reinterpret_cast<CloseWindowContext *>(lParam);
    DWORD ownerPid = 0;
    GetWindowThreadProcessId(hwnd, &ownerPid);
    if (ownerPid != context->pid)
        return TRUE;

    // Only target real top-level windows.  Owned popup/tool windows normally
    // close with their owner, and sending WM_CLOSE to them first can produce
    // odd application-specific behavior.
    if (GetWindow(hwnd, GW_OWNER) != nullptr)
        return TRUE;

    if (PostMessageW(hwnd, WM_CLOSE, 0, 0))
        ++context->posted;
    return TRUE;
}

static bool requestCloseWindows(int pid) {
    CloseWindowContext context;
    context.pid = DWORD(pid);
    EnumWindows(postCloseToProcessWindow, reinterpret_cast<LPARAM>(&context));
    return context.posted > 0;
}
#endif

#if defined(Q_OS_LINUX) && defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
static bool currentProcessIdentity(int pid, ProcessIdentity *identity) {
    QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
    if (!statFile.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    const QByteArray stat = statFile.readAll();
    const int closeParen = stat.lastIndexOf(')');
    if (closeParen < 0)
        return false;

    const QList<QByteArray> fields = stat.mid(closeParen + 2).split(' ');
    // /proc/<pid>/stat field 22 is starttime.  After removing fields 1
    // (pid) and 2 (comm), it is index 19 in the remaining list.
    if (fields.size() <= 19)
        return false;

    bool ok = false;
    const quint64 startTime = fields.at(19).toULongLong(&ok);
    if (!ok)
        return false;

    identity->pid = pid;
    identity->startTime = startTime;
    return true;
}
#endif

static QString actOnProcess(const ProcessIdentity &expected, bool force) {
    if (expected.pid <= 0)
        return QStringLiteral("The selected process identity is invalid.");

#ifdef Q_OS_WIN
    const DWORD actionAccess = force ? PROCESS_TERMINATE : 0;
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | actionAccess, FALSE, DWORD(expected.pid));
    if (!handle)
        handle = OpenProcess(PROCESS_QUERY_INFORMATION | actionAccess, FALSE, DWORD(expected.pid));
    if (!handle) {
        const DWORD errorCode = GetLastError();
        return QStringLiteral("Cannot open PID %1 (Windows error %2).").arg(expected.pid).arg(errorCode);
    }

    QString error;
    FILETIME creationTime, exitTime, kernelTime, userTime;
    if (!GetProcessTimes(handle, &creationTime, &exitTime, &kernelTime, &userTime)) {
        error = QStringLiteral("Cannot verify the selected process (Windows error %1).").arg(GetLastError());
    } else {
        ULARGE_INTEGER creation;
        creation.LowPart = creationTime.dwLowDateTime;
        creation.HighPart = creationTime.dwHighDateTime;
        if (creation.QuadPart != expected.startTime) {
            error = QStringLiteral("The PID now belongs to a different process. No action was taken.");
        } else if (force) {
            if (!TerminateProcess(handle, 1))
                error = QStringLiteral("Cannot terminate the selected process (Windows error %1).").arg(GetLastError());
        } else if (!requestCloseWindows(expected.pid)) {
            error = QStringLiteral("No close request could be sent to the selected process's top-level windows.");
        }
    }
    // Keep the process object alive through validation and the action so its
    // PID cannot be reused, including while enumerating windows for Close.
    CloseHandle(handle);
    return error;
#elif defined(Q_OS_LINUX) && defined(SYS_pidfd_open) && defined(SYS_pidfd_send_signal)
    const int fd = int(::syscall(SYS_pidfd_open, expected.pid, 0U));
    if (fd < 0) {
        const int errorCode = errno;
        return QStringLiteral("Cannot access PID %1 for safe signalling: %2.")
            .arg(expected.pid).arg(QString::fromLocal8Bit(std::strerror(errorCode)));
    }

    QString error;
    ProcessIdentity current;
    // Open the pidfd before checking /proc. If the process exits after this
    // check, signalling the pidfd fails instead of acting on a reused PID.
    if (!currentProcessIdentity(expected.pid, &current) || !(current == expected)) {
        error = QStringLiteral("The selected process has exited or its identity could not be verified. No action was taken.");
    } else if (::syscall(SYS_pidfd_send_signal, fd, force ? SIGKILL : SIGTERM, nullptr, 0U) < 0) {
        error = QStringLiteral("Cannot signal the selected process: %1.")
            .arg(QString::fromLocal8Bit(std::strerror(errno)));
    }
    ::close(fd);
    return error;
#else
    Q_UNUSED(force)
    return QStringLiteral("This build does not support safe process signalling on this platform.");
#endif
}

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

static QRect availableGeometryForRestoreScreen() {
    if (QScreen *screen = QGuiApplication::primaryScreen())
        return screen->availableGeometry();
    return QRect(0, 0, 1024, 768);
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
    m_animation->setDuration(333);
    m_animation->setEasingCurve(QEasingCurve::OutCubic);

    m_nameAnimationTimer.setInterval(100);
    m_nameAnimationTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_nameAnimationTimer, &QTimer::timeout, this, &AlertWindow::updateNameAnimation);

    connect(m_settingsButton, &QToolButton::clicked, this, &AlertWindow::showSettings);
    connect(m_config, &Configuration::changed, this, &AlertWindow::applyConfiguration);
    applyConfiguration();
}

void AlertWindow::setAnimatedHeight(int height) {
    m_animatedHeight = qMax(0, height);
#ifdef Q_OS_WIN
    // Keep valid native geometry while the logical height is zero. The window
    // is still hidden below when an empty alert finishes collapsing.
    setFixedHeight(qMax(1, m_animatedHeight));
#else
    setFixedHeight(m_animatedHeight);
#endif
    update();
    if (m_animatedHeight <= 0 && m_processes.isEmpty())
        hide();
}

void AlertWindow::setBadProcesses(const QVector<BadProcess> &processes) {
    m_processes = processes;

    if (m_dragging) {
        applyProcessRows(processes, m_dragRowCount);
        return;
    }

    applyProcessRows(processes, processes.size());
    animateToContentHeight();
}

void AlertWindow::applyProcessRows(const QVector<BadProcess> &processes, int visibleRows) {
    visibleRows = qMax(0, visibleRows);

    while (m_entries.size() < visibleRows) {
        auto *entry = new ProcessEntryWidget(this);
        entry->setDarkMode(m_config->darkMode());
        entry->setCustomFontEnabled(m_config->useCustomFont(), m_config->customFont());
        connect(entry, &ProcessEntryWidget::terminateRequested, this, &AlertWindow::confirmTerminate);
        m_layout->addWidget(entry);
        m_entries.append(entry);
    }

    for (int i = 0; i < m_entries.size(); ++i) {
        const bool visible = i < visibleRows;
        m_entries[i]->setVisible(visible);
        if (!visible)
            continue;

        if (i < processes.size())
            m_entries[i]->setProcess(processes[i]);
        else
            m_entries[i]->setEmpty();
    }
    updateNameAnimation();
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
        m_dragRowCount = qMax(1, m_processes.size());
        m_animation->stop();
        setAnimatedHeight(height());
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
        m_dragRowCount = 0;
        const QPoint safePos = clampedPosition(pos());
        if (safePos != pos())
            move(safePos);
        m_config->setWindowPosition(safePos);
        const QVector<BadProcess> processes = m_processes;
        applyProcessRows(processes, processes.size());
        animateToContentHeight();
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
    updateNameAnimation();
    update();
    if (isVisible())
        applyAllWorkspacesHint();
    if (!m_dragging && !m_processes.isEmpty())
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
        QPoint targetPosition;
        if (m_config->hasWindowPosition()) {
            targetPosition = clampedPosition(m_config->windowPosition());
        } else {
            const QRect screen = availableGeometryForWindow(this);
            targetPosition = QPoint(screen.right() - width() - 18, screen.top() + 18);
        }
        move(targetPosition);
        show();
#ifdef Q_OS_WIN
        // Apply the intended position again after native window creation.
        move(targetPosition);
#endif
        raise();
        // Some WMs only accept _NET_WM_STATE requests after the window has
        // become managed.  Queue it rather than setting pre-map properties.
        QTimer::singleShot(0, this, &AlertWindow::applyAllWorkspacesHint);
        QTimer::singleShot(200, this, &AlertWindow::applyAllWorkspacesHint);
    }

    if (m_animation->state() == QAbstractAnimation::Running &&
        m_animation->endValue().toInt() == targetHeight) {
        return;
    }

    m_animation->stop();
    if (m_animatedHeight == targetHeight) {
        setAnimatedHeight(targetHeight);
        return;
    }
    m_animation->setStartValue(height());
    m_animation->setEndValue(targetHeight);
    m_animation->start();
}

void AlertWindow::showSettings() {
    if (!m_settingsDialog) {
        m_config->reloadFromDiskPreservingWindowPosition();
        m_settingsDialog = new SettingsDialog(m_config, this);
        m_settingsDialog->setAttribute(Qt::WA_DeleteOnClose, true);
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void AlertWindow::restorePosition() {
    const QPoint newPos = sanePositionOnPrimaryScreen();
    move(newPos);
    m_config->setWindowPosition(newPos);
    if (isVisible()) {
        raise();
        applyAllWorkspacesHint();
    }
}

void AlertWindow::confirmTerminate(BadProcess process) {
    // Own a copy: box.exec() keeps sampling active, so the originating row may
    // be updated or reassigned before the user confirms the action.
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Close or kill application?"));
    box.setText(QStringLiteral("Close or kill %1 PID %2?").arg(process.label).arg(process.root.pid));
#ifdef Q_OS_WIN
    box.setInformativeText(QStringLiteral("%1\n\nCPU tree: %2%\n\nClose: asks the application's top-level windows to close.\nKill: forcibly terminates the process with TerminateProcess().")
                               .arg(process.command)
                               .arg(process.cpuPercent, 0, 'f', 1));
#else
    box.setInformativeText(QStringLiteral("%1\n\nCPU tree: %2%\n\nClose: sends SIGTERM to the process.\nKill: sends SIGKILL to the process.")
                               .arg(process.command)
                               .arg(process.cpuPercent, 0, 'f', 1));
#endif
    QPushButton *close = box.addButton(QStringLiteral("Close"), QMessageBox::AcceptRole);
    QPushButton *kill = box.addButton(QStringLiteral("Kill"), QMessageBox::DestructiveRole);
#ifdef Q_OS_WIN
    close->setToolTip(QStringLiteral("Post WM_CLOSE to top-level windows owned by this PID."));
    kill->setToolTip(QStringLiteral("Forcibly terminate this PID with TerminateProcess()."));
#else
    close->setToolTip(QStringLiteral("Send SIGTERM to this PID."));
    kill->setToolTip(QStringLiteral("Send SIGKILL to this PID."));
#endif
    box.addButton(QMessageBox::Cancel);
    box.exec();

    const bool wantsClose = (box.clickedButton() == close);
    const bool wantsKill = (box.clickedButton() == kill);
    if (!wantsClose && !wantsKill)
        return;

    const QString error = actOnProcess(process.root, wantsKill);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Process action failed"), error);
        emit immediateRefreshRequested();
        return;
    }

    QTimer::singleShot(120, this, [this] { emit immediateRefreshRequested(); });
    QTimer::singleShot(600, this, [this] { emit immediateRefreshRequested(); });
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

QPoint AlertWindow::sanePositionOnPrimaryScreen() const {
    const QRect screen = availableGeometryForRestoreScreen();
    return QPoint(screen.left() + 20, screen.top() + 20);
}

QPoint AlertWindow::clampedPosition(const QPoint &pos) const {
    QRect screen = availableGeometryForRestoreScreen();
    for (QScreen *candidate : QGuiApplication::screens()) {
        if (candidate->geometry().contains(pos)) {
            screen = candidate->availableGeometry();
            break;
        }
    }
    const int margin = 20;
    const int minX = screen.left();
    const int minY = screen.top();
    const int maxX = qMax(minX, screen.right() - qMin(width(), margin));
    const int maxY = qMax(minY, screen.bottom() - qMin(height(), margin));

    return QPoint(qBound(minX, pos.x(), maxX),
                  qBound(minY, pos.y(), maxY));
}

void AlertWindow::applyAllWorkspacesHint() {
#ifdef BADPROCESS_GUARD_HAVE_X11
    if (!m_config)
        return;
    if (QGuiApplication::platformName() != QLatin1String("xcb"))
        return;

    Display *display = XOpenDisplay(nullptr);
    if (!display)
        return;

    const Window window = static_cast<Window>(winId());
    const Window root = DefaultRootWindow(display);
    const Atom stateAtom = XInternAtom(display, "_NET_WM_STATE", False);
    const Atom stickyAtom = XInternAtom(display, "_NET_WM_STATE_STICKY", False);

    if (stateAtom != None && stickyAtom != None) {
        XEvent event;
        memset(&event, 0, sizeof(event));
        event.xclient.type = ClientMessage;
        event.xclient.window = window;
        event.xclient.message_type = stateAtom;
        event.xclient.format = 32;
        event.xclient.data.l[0] = m_config->allWorkspaces() ? 1 : 0; // ADD : REMOVE
        event.xclient.data.l[1] = static_cast<long>(stickyAtom);
        event.xclient.data.l[2] = 0;
        event.xclient.data.l[3] = 1; // normal application source indication
        event.xclient.data.l[4] = 0;
        XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
    }

    XFlush(display);
    XCloseDisplay(display);
#else
    Q_UNUSED(this)
#endif
}

void AlertWindow::resizeEvent(QResizeEvent *event) {
    QFrame::resizeEvent(event);
    positionSettingsButton();
}

void AlertWindow::updateNameAnimation() {
    bool animate = false;
    if (m_config->animateNames()) {
        for (ProcessEntryWidget *entry : m_entries) {
            if (!entry->isHidden() && entry->hasActiveProcess()) {
                animate = true;
                break;
            }
        }
    }
    if (animate && !m_nameAnimationTimer.isActive()) {
        m_nameAnimationClock.start();
        m_nameAnimationTimer.start();
    } else if (!animate) {
        m_nameAnimationTimer.stop();
        m_nameAnimationClock.invalidate();
    }

    const QColor normal(m_config->darkMode() ? QStringLiteral("#f5f5f5") : QStringLiteral("#111111"));
    QColor pulsing = normal;
    if (animate) {
        const qint64 duration = m_config->animationDuration();
        const qint64 phase = m_nameAnimationClock.elapsed() % (2 * duration);
        const double progress = double(phase <= duration ? phase : 2 * duration - phase) / duration;
        const QColor target = m_config->animationColor();
        pulsing = QColor(qRound(normal.red() + (target.red() - normal.red()) * progress),
                         qRound(normal.green() + (target.green() - normal.green()) * progress),
                         qRound(normal.blue() + (target.blue() - normal.blue()) * progress));
    }
    for (ProcessEntryWidget *entry : m_entries) {
        if (!entry->isHidden())
            entry->setNameColor(animate && entry->hasActiveProcess() ? pulsing : normal);
    }
}
