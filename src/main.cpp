#include "alertwindow.h"
#include "configuration.h"
#include "processmonitor.h"

#include <QAction>
#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QMenu>
#include <QSystemTrayIcon>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false);
    QApplication::setApplicationName(QStringLiteral("badprocess-guard"));
    QApplication::setApplicationDisplayName(QStringLiteral("badprocess-guard"));
    QApplication::setOrganizationName(QStringLiteral("badprocess-guard"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/gear_metallic.svg")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Compact guard window for CPU-heavy process trees."));
    parser.addHelpOption();

    QCommandLineOption intervalOpt(QStringLiteral("refresh-interval"),
                                   QStringLiteral("Sampling interval in milliseconds."),
                                   QStringLiteral("ms"));
    QCommandLineOption thresholdOpt(QStringLiteral("tree-threshold"),
                                    QStringLiteral("Watched process-tree CPU threshold percentage."),
                                    QStringLiteral("percent"), QStringLiteral("50"));
    QCommandLineOption processThresholdOpt(QStringLiteral("process-threshold"),
                                           QStringLiteral("Individual-process CPU threshold percentage."),
                                           QStringLiteral("percent"), QStringLiteral("50"));
    QCommandLineOption alertDurationOpt(QStringLiteral("alert-duration"),
                                        QStringLiteral("Milliseconds to keep a disappeared bad process visible after a normal sample."),
                                        QStringLiteral("ms"));
    QCommandLineOption debugOpt(QStringLiteral("debug"),
                                QStringLiteral("Print monitor/debug information to stderr."));
    QCommandLineOption testAlertOpt(QStringLiteral("test-alert"),
                                    QStringLiteral("Show a fake alert immediately; useful for testing window rendering."));
    parser.addOption(intervalOpt);
    parser.addOption(thresholdOpt);
    parser.addOption(processThresholdOpt);
    parser.addOption(alertDurationOpt);
    parser.addOption(debugOpt);
    parser.addOption(testAlertOpt);
    parser.process(app);

    Configuration config;
    ProcessMonitor monitor;
    monitor.setIntervalMs(parser.isSet(intervalOpt) ? parser.value(intervalOpt).toInt() : config.refreshInterval());
    monitor.setTreeThresholdPercent(parser.isSet(thresholdOpt) ? parser.value(thresholdOpt).toDouble() : config.treeThresholdPercent());
    monitor.setProcessThresholdPercent(parser.isSet(processThresholdOpt) ? parser.value(processThresholdOpt).toDouble() : config.processThresholdPercent());
    monitor.setLingerMs(parser.isSet(alertDurationOpt) ? parser.value(alertDurationOpt).toInt() : config.alertDuration());
    monitor.setDebugEnabled(parser.isSet(debugOpt));
    QObject::connect(&config, &Configuration::changed, &monitor, [&config, &monitor] {
        monitor.setIntervalMs(config.refreshInterval());
        monitor.setTreeThresholdPercent(config.treeThresholdPercent());
        monitor.setProcessThresholdPercent(config.processThresholdPercent());
        monitor.setLingerMs(config.alertDuration());
        monitor.refreshNow();
    });

    AlertWindow window(&config);
    QObject::connect(&monitor, &ProcessMonitor::badProcessesChanged,
                     &window, &AlertWindow::setBadProcesses);
    QObject::connect(&window, &AlertWindow::immediateRefreshRequested,
                     &monitor, &ProcessMonitor::refreshNow);

    QMenu trayMenu;
    QAction *settingsAction = trayMenu.addAction(QStringLiteral("Settings"));
    QAction *exitAction = trayMenu.addAction(QStringLiteral("Exit"));

    QSystemTrayIcon trayIcon(QIcon(QStringLiteral(":/icons/gear_metallic.svg")));
    trayIcon.setToolTip(QStringLiteral("badprocess-guard"));
    trayIcon.setContextMenu(&trayMenu);
    QObject::connect(settingsAction, &QAction::triggered, &window, &AlertWindow::showSettings);
    QObject::connect(exitAction, &QAction::triggered, &app, &QApplication::quit);
    QObject::connect(&trayIcon, &QSystemTrayIcon::activated, &window,
                     [&window](QSystemTrayIcon::ActivationReason reason) {
                         if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
                             window.showSettings();
                     });
    if (QSystemTrayIcon::isSystemTrayAvailable())
        trayIcon.show();

    if (parser.isSet(testAlertOpt)) {
        QVector<BadProcess> fake;
        fake.append({QStringLiteral("Firefox"), {int(QApplication::applicationPid()), 1},
                     QStringLiteral("firefox -no-remote -P example"), 123.4, 12});
        window.setBadProcesses(fake);
    }

    monitor.start();

    return app.exec();
}
