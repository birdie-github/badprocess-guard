#include "settingsdialog.h"

#include <QCheckBox>
#include <QApplication>
#include <QGuiApplication>
#include <QDoubleSpinBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(Configuration *config, QWidget *parent)
    : QDialog(parent), m_config(config) {
    setWindowTitle(QStringLiteral("badprocess-guard settings"));
    setModal(false);

    m_config->beginDeferredSave();
    connect(this, &QDialog::finished, m_config, &Configuration::endDeferredSave);

    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(10, 100);
    m_opacitySlider->setSingleStep(1);
    m_opacitySlider->setPageStep(5);

    m_opacityValue = new QLabel(this);
    m_opacityValue->setMinimumWidth(42);

    auto *opacityRow = new QWidget(this);
    auto *opacityLayout = new QHBoxLayout(opacityRow);
    opacityLayout->setContentsMargins(0, 0, 0, 0);
    opacityLayout->addWidget(m_opacitySlider, 1);
    opacityLayout->addWidget(m_opacityValue);

    m_darkMode = new QCheckBox(QStringLiteral("Dark mode"), this);
    m_useCustomFont = new QCheckBox(QStringLiteral("Use custom font"), this);
    m_customFontButton = new QPushButton(QStringLiteral("Custom Font…"), this);

    m_refreshInterval = new QSpinBox(this);
    m_refreshInterval->setRange(100, 600000);
    m_refreshInterval->setSingleStep(100);
    m_refreshInterval->setSuffix(QStringLiteral(" ms"));
    m_refreshInterval->setToolTip(QStringLiteral("How often process CPU usage is sampled. Lower values react faster but cost more CPU. Mininum: 100ms."));

    m_alertDuration = new QSpinBox(this);
    m_alertDuration->setRange(250, 600000);
    m_alertDuration->setSingleStep(100);
    m_alertDuration->setSuffix(QStringLiteral(" ms"));
    m_alertDuration->setToolTip(QStringLiteral("How long a recovered process remains visible before disappearing. Minimum: 250ms."));

    m_treeThreshold = new QDoubleSpinBox(this);
    m_treeThreshold->setRange(10.0, 1000000.0);
    m_treeThreshold->setDecimals(1);
    m_treeThreshold->setSingleStep(1.0);
    m_treeThreshold->setSuffix(QStringLiteral(" %"));
    m_treeThreshold->setToolTip(QStringLiteral("Aggregate CPU threshold for configured process trees. 100% means one fully busy logical CPU. Minimum: 10%."));

    m_processThreshold = new QDoubleSpinBox(this);
    m_processThreshold->setRange(5.0, 1000000.0);
    m_processThreshold->setDecimals(1);
    m_processThreshold->setSingleStep(1.0);
    m_processThreshold->setSuffix(QStringLiteral(" %"));
    m_processThreshold->setToolTip(QStringLiteral("CPU threshold for individual processes. 100% means one fully busy logical CPU. Minimum: 5%."));

    m_allWorkspaces = new QCheckBox(QStringLiteral("All Workspaces"), this);
    m_allWorkspaces->setToolTip(QStringLiteral("Show the alert window on all X11 workspaces."));
    const bool runningOnX11 = QGuiApplication::platformName().compare(QStringLiteral("xcb"), Qt::CaseInsensitive) == 0;

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Opacity"), opacityRow);
    form->addRow(QStringLiteral("Refresh interval"), m_refreshInterval);
    form->addRow(QStringLiteral("Alert duration"), m_alertDuration);
    form->addRow(QStringLiteral("Tree threshold"), m_treeThreshold);
    form->addRow(QStringLiteral("Process threshold"), m_processThreshold);
    form->addRow(QString(), m_darkMode);
    form->addRow(QString(), m_useCustomFont);
    form->addRow(QString(), m_customFontButton);
    if (runningOnX11)
        form->addRow(QString(), m_allWorkspaces);

    auto *closeButton = new QPushButton(QStringLiteral("Close"), this);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    buttons->addWidget(closeButton);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addLayout(buttons);

    refreshFromConfig();

    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        m_opacityValue->setText(QStringLiteral("%1%").arg(value));
        m_config->setOpacityPercent(value);
    });
    connect(m_darkMode, &QCheckBox::toggled, m_config, &Configuration::setDarkMode);
    connect(m_refreshInterval, QOverload<int>::of(&QSpinBox::valueChanged),
            m_config, &Configuration::setRefreshInterval);
    connect(m_alertDuration, QOverload<int>::of(&QSpinBox::valueChanged),
            m_config, &Configuration::setAlertDuration);
    connect(m_treeThreshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            m_config, &Configuration::setTreeThresholdPercent);
    connect(m_processThreshold, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            m_config, &Configuration::setProcessThresholdPercent);
    connect(m_allWorkspaces, &QCheckBox::toggled, m_config, &Configuration::setAllWorkspaces);
    connect(m_useCustomFont, &QCheckBox::toggled, this, [this](bool checked) {
        m_customFontButton->setEnabled(checked);
        m_config->setUseCustomFont(checked);
    });
    connect(m_customFontButton, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QFont font = QFontDialog::getFont(&ok, m_config->customFont(), this, QStringLiteral("Select notification font"));
        if (ok)
            m_config->setCustomFont(font);
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
}

void SettingsDialog::refreshFromConfig() {
    m_opacitySlider->setValue(m_config->opacityPercent());
    m_opacityValue->setText(QStringLiteral("%1%").arg(m_config->opacityPercent()));
    m_darkMode->setChecked(m_config->darkMode());
    m_useCustomFont->setChecked(m_config->useCustomFont());
    m_customFontButton->setEnabled(m_config->useCustomFont());
    m_refreshInterval->setValue(m_config->refreshInterval());
    m_alertDuration->setValue(m_config->alertDuration());
    m_treeThreshold->setValue(m_config->treeThresholdPercent());
    m_processThreshold->setValue(m_config->processThresholdPercent());
    m_allWorkspaces->setChecked(m_config->allWorkspaces());
}
