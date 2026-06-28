#include "configuration.h"

#include <QtGlobal>

Configuration::Configuration(QObject *parent)
    : QObject(parent),
      m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QStringLiteral("badprocess-guard"),
                 QStringLiteral("badprocess-guard")) {
    load();
}

void Configuration::load() {
    m_settings.beginGroup(QStringLiteral("General"));

    m_opacityPercent = qBound(10, m_settings.value(QStringLiteral("Opacity"), 50).toInt(), 100);
    m_darkMode = m_settings.value(QStringLiteral("DarkMode"), true).toBool();
    m_allWorkspaces = m_settings.value(QStringLiteral("AllWorkspaces"), false).toBool();
    m_refreshInterval = qMax(250, m_settings.value(QStringLiteral("RefreshInterval"), 5000).toInt());
    m_alertDuration = qMax(0, m_settings.value(QStringLiteral("AlertDuration"), 3000).toInt());
    m_treeThresholdPercent = qMax(0.0, m_settings.value(QStringLiteral("TreeThreshold"), 50.0).toDouble());
    m_processThresholdPercent = qMax(0.0, m_settings.value(QStringLiteral("ProcessThreshold"), 50.0).toDouble());

    const QString fontString = m_settings.value(QStringLiteral("Font")).toString();
    m_useCustomFont = !fontString.isEmpty();
    if (m_useCustomFont)
        m_customFont.fromString(fontString);

    if (m_settings.contains(QStringLiteral("WindowX")) && m_settings.contains(QStringLiteral("WindowY"))) {
        m_windowPosition = QPoint(m_settings.value(QStringLiteral("WindowX")).toInt(),
                                  m_settings.value(QStringLiteral("WindowY")).toInt());
        m_hasWindowPosition = true;
    }

    m_settings.endGroup();
}

void Configuration::save() {
    m_settings.beginGroup(QStringLiteral("General"));
    m_settings.setValue(QStringLiteral("Opacity"), m_opacityPercent);
    m_settings.setValue(QStringLiteral("DarkMode"), m_darkMode);
    m_settings.setValue(QStringLiteral("Font"), m_useCustomFont ? m_customFont.toString() : QString());
    m_settings.setValue(QStringLiteral("AllWorkspaces"), m_allWorkspaces);
    m_settings.setValue(QStringLiteral("RefreshInterval"), m_refreshInterval);
    m_settings.setValue(QStringLiteral("AlertDuration"), m_alertDuration);
    m_settings.setValue(QStringLiteral("TreeThreshold"), m_treeThresholdPercent);
    m_settings.setValue(QStringLiteral("ProcessThreshold"), m_processThresholdPercent);
    if (m_hasWindowPosition) {
        m_settings.setValue(QStringLiteral("WindowX"), m_windowPosition.x());
        m_settings.setValue(QStringLiteral("WindowY"), m_windowPosition.y());
    }
    m_settings.endGroup();
    m_settings.sync();
}

void Configuration::setOpacityPercent(int value) {
    value = qBound(10, value, 100);
    if (m_opacityPercent == value)
        return;
    m_opacityPercent = value;
    save();
    emit changed();
}

void Configuration::setDarkMode(bool enabled) {
    if (m_darkMode == enabled)
        return;
    m_darkMode = enabled;
    save();
    emit changed();
}

void Configuration::setUseCustomFont(bool enabled) {
    if (m_useCustomFont == enabled)
        return;
    m_useCustomFont = enabled;
    save();
    emit changed();
}

void Configuration::setCustomFont(const QFont &font) {
    if (m_useCustomFont && m_customFont == font)
        return;
    m_customFont = font;
    m_useCustomFont = true;
    save();
    emit changed();
}

void Configuration::setWindowPosition(const QPoint &pos) {
    if (m_hasWindowPosition && m_windowPosition == pos)
        return;
    m_windowPosition = pos;
    m_hasWindowPosition = true;
    save();
}

void Configuration::setAllWorkspaces(bool enabled) {
    if (m_allWorkspaces == enabled)
        return;
    m_allWorkspaces = enabled;
    save();
    emit changed();
}
