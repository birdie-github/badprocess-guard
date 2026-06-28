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
    m_opacityPercent = qBound(10, m_settings.value(QStringLiteral("appearance/opacityPercent"), 50).toInt(), 100);
    m_darkMode = m_settings.value(QStringLiteral("appearance/darkMode"), true).toBool();
    m_useCustomFont = m_settings.value(QStringLiteral("appearance/useCustomFont"), false).toBool();
    m_allWorkspaces = m_settings.value(QStringLiteral("window/AllWorkspaces"),
                                       m_settings.value(QStringLiteral("AllWorkspaces"), true)).toBool();

    const QString fontString = m_settings.value(QStringLiteral("appearance/customFont")).toString();
    if (!fontString.isEmpty())
        m_customFont.fromString(fontString);

    if (m_settings.contains(QStringLiteral("window/x")) && m_settings.contains(QStringLiteral("window/y"))) {
        m_windowPosition = QPoint(m_settings.value(QStringLiteral("window/x")).toInt(),
                                  m_settings.value(QStringLiteral("window/y")).toInt());
        m_hasWindowPosition = true;
    }
}

void Configuration::save() {
    m_settings.setValue(QStringLiteral("appearance/opacityPercent"), m_opacityPercent);
    m_settings.setValue(QStringLiteral("appearance/darkMode"), m_darkMode);
    m_settings.setValue(QStringLiteral("appearance/useCustomFont"), m_useCustomFont);
    m_settings.setValue(QStringLiteral("appearance/customFont"), m_customFont.toString());
    m_settings.setValue(QStringLiteral("window/AllWorkspaces"), m_allWorkspaces);
    if (m_hasWindowPosition) {
        m_settings.setValue(QStringLiteral("window/x"), m_windowPosition.x());
        m_settings.setValue(QStringLiteral("window/y"), m_windowPosition.y());
    }
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
    if (m_customFont == font)
        return;
    m_customFont = font;
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
