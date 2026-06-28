#pragma once

#include "configuration.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QSlider;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(Configuration *config, QWidget *parent = nullptr);

private:
    void refreshFromConfig();

    Configuration *m_config = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValue = nullptr;
    QCheckBox *m_darkMode = nullptr;
    QCheckBox *m_useCustomFont = nullptr;
    QPushButton *m_customFontButton = nullptr;
};
