#include "settingsdialog.h"

#include <QCheckBox>
#include <QFontDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(Configuration *config, QWidget *parent)
    : QDialog(parent), m_config(config) {
    setWindowTitle(QStringLiteral("badprocess-guard settings"));
    setModal(false);

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

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Opacity"), opacityRow);
    form->addRow(QString(), m_darkMode);
    form->addRow(QString(), m_useCustomFont);
    form->addRow(QString(), m_customFontButton);

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
}
