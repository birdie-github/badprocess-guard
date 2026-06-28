#include "alertwindow.h"
#include "configuration.h"
#include "processmonitor.h"

#include <QApplication>
#include <QCommandLineParser>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("badprocess-guard"));
    QApplication::setApplicationDisplayName(QStringLiteral("badprocess-guard"));
    QApplication::setOrganizationName(QStringLiteral("badprocess-guard"));

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

    AlertWindow window(&config);
    QObject::connect(&monitor, &ProcessMonitor::badProcessesChanged,
                     &window, &AlertWindow::setBadProcesses);
    QObject::connect(&window, &AlertWindow::immediateRefreshRequested,
                     &monitor, &ProcessMonitor::refreshNow);

    if (parser.isSet(testAlertOpt)) {
        QVector<BadProcess> fake;
        fake.append({QStringLiteral("Firefox"), {int(QApplication::applicationPid()), 1},
                     QStringLiteral("firefox -no-remote -P example"), 123.4, 12});
        window.setBadProcesses(fake);
    }

    monitor.start();

    return app.exec();
}
