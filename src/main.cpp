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

    QCommandLineOption intervalOpt(QStringLiteral("interval-ms"),
                                   QStringLiteral("Sampling interval in milliseconds."),
                                   QStringLiteral("ms"), QStringLiteral("5000"));
    QCommandLineOption thresholdOpt(QStringLiteral("tree-threshold"),
                                    QStringLiteral("Tree CPU threshold percentage."),
                                    QStringLiteral("percent"), QStringLiteral("50"));
    QCommandLineOption consecutiveOpt(QStringLiteral("consecutive"),
                                      QStringLiteral("Consecutive high samples required before display."),
                                      QStringLiteral("count"), QStringLiteral("2"));
    QCommandLineOption debugOpt(QStringLiteral("debug"),
                                QStringLiteral("Print monitor/debug information to stderr."));
    QCommandLineOption testAlertOpt(QStringLiteral("test-alert"),
                                    QStringLiteral("Show a fake alert immediately; useful for testing window rendering."));
    parser.addOption(intervalOpt);
    parser.addOption(thresholdOpt);
    parser.addOption(consecutiveOpt);
    parser.addOption(debugOpt);
    parser.addOption(testAlertOpt);
    parser.process(app);

    Configuration config;
    ProcessMonitor monitor;
    monitor.setIntervalMs(parser.value(intervalOpt).toInt());
    monitor.setTreeThresholdPercent(parser.value(thresholdOpt).toDouble());
    monitor.setConsecutiveSamples(parser.value(consecutiveOpt).toInt());
    monitor.setDebugEnabled(parser.isSet(debugOpt));

    AlertWindow window(&config);
    QObject::connect(&monitor, &ProcessMonitor::badProcessesChanged,
                     &window, &AlertWindow::setBadProcesses);

    if (parser.isSet(testAlertOpt)) {
        QVector<BadProcess> fake;
        fake.append({QStringLiteral("Firefox"), {int(QApplication::applicationPid()), 1},
                     QStringLiteral("firefox -no-remote -P example"), 123.4, 12});
        window.setBadProcesses(fake);
    }

    monitor.start();

    return app.exec();
}
