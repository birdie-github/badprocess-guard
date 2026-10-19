#include "configuration.h"

#include <QtGlobal>
#include <QUuid>

Configuration::Configuration(QObject *parent)
    : QObject(parent),
      m_settings(QSettings::IniFormat, QSettings::UserScope,
                 QStringLiteral("badprocess-guard"),
                 QStringLiteral("badprocess-guard")) {
    load();
}

void Configuration::load() {
    m_hasWindowPosition = false;
    m_settings.beginGroup(QStringLiteral("Settings"));

    m_opacityPercent = qBound(10, m_settings.value(QStringLiteral("Opacity"), 50).toInt(), 100);
    m_darkMode = m_settings.value(QStringLiteral("DarkMode"), true).toBool();
    m_animateNames = m_settings.value(QStringLiteral("AnimateNames"), true).toBool();
    m_animationColor = QColor(m_settings.value(QStringLiteral("AnimationColor"), QStringLiteral("#ff0000")).toString());
    if (!m_animationColor.isValid())
        m_animationColor = QColor(QStringLiteral("#ff0000"));
    m_animationColor.setAlpha(255);
    m_animationDuration = qBound(100, m_settings.value(QStringLiteral("AnimationDuration"), 3000).toInt(), 999000);
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
    loadIgnoreRules();
}

void Configuration::saveWindowPosition()
{
    if (!m_hasWindowPosition)
        return;

    m_settings.beginGroup(QStringLiteral("Settings"));
    m_settings.setValue(QStringLiteral("WindowX"), m_windowPosition.x());
    m_settings.setValue(QStringLiteral("WindowY"), m_windowPosition.y());
    m_settings.endGroup();
    m_settings.sync();
}

void Configuration::reloadFromDiskPreservingWindowPosition()
{
    const QPoint windowPosition = m_windowPosition;
    const bool hasWindowPosition = m_hasWindowPosition;
    const int refreshInterval = m_refreshInterval;
    const int alertDuration = m_alertDuration;
    const double treeThreshold = m_treeThresholdPercent;
    const double processThreshold = m_processThresholdPercent;

    m_settings.sync();
    load();
    m_windowPosition = windowPosition;
    m_hasWindowPosition = hasWindowPosition;
    emit changed();
    emit ignoredApplicationsChanged();
    if (m_refreshInterval != refreshInterval || m_alertDuration != alertDuration ||
        !qFuzzyCompare(m_treeThresholdPercent + 1.0, treeThreshold + 1.0) ||
        !qFuzzyCompare(m_processThresholdPercent + 1.0, processThreshold + 1.0)) {
        emit monitoringChanged();
    }
}

void Configuration::save() {
    m_settings.beginGroup(QStringLiteral("Settings"));
    m_settings.setValue(QStringLiteral("Opacity"), m_opacityPercent);
    m_settings.setValue(QStringLiteral("DarkMode"), m_darkMode);
    m_settings.setValue(QStringLiteral("AnimateNames"), m_animateNames);
    m_settings.setValue(QStringLiteral("AnimationColor"), m_animationColor.name());
    m_settings.setValue(QStringLiteral("AnimationDuration"), m_animationDuration);
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

void Configuration::beginDeferredSave() {
    m_deferSaves = true;
}

void Configuration::endDeferredSave() {
    m_deferSaves = false;
    if (m_hasDeferredSave) {
        m_hasDeferredSave = false;
        save();
    }
}

void Configuration::scheduleSave() {
    if (m_deferSaves) {
        m_hasDeferredSave = true;
        return;
    }
    save();
}

void Configuration::setOpacityPercent(int value) {
    value = qBound(10, value, 100);
    if (m_opacityPercent == value)
        return;
    m_opacityPercent = value;
    scheduleSave();
    emit changed();
}

void Configuration::setDarkMode(bool enabled) {
    if (m_darkMode == enabled)
        return;
    m_darkMode = enabled;
    scheduleSave();
    emit changed();
}

void Configuration::setUseCustomFont(bool enabled) {
    if (m_useCustomFont == enabled)
        return;
    m_useCustomFont = enabled;
    scheduleSave();
    emit changed();
}

void Configuration::setCustomFont(const QFont &font) {
    if (m_useCustomFont && m_customFont == font)
        return;
    m_customFont = font;
    m_useCustomFont = true;
    scheduleSave();
    emit changed();
}

void Configuration::setWindowPosition(const QPoint &pos) {
    if (m_hasWindowPosition && m_windowPosition == pos)
        return;
    m_windowPosition = pos;
    m_hasWindowPosition = true;
    saveWindowPosition();
}

void Configuration::setAllWorkspaces(bool enabled) {
    if (m_allWorkspaces == enabled)
        return;
    m_allWorkspaces = enabled;
    scheduleSave();
    emit changed();
}
void Configuration::setRefreshInterval(int ms) {
    ms = qMax(250, ms);
    if (m_refreshInterval == ms)
        return;
    m_refreshInterval = ms;
    scheduleSave();
    emit changed();
    emit monitoringChanged();
}

void Configuration::setAlertDuration(int ms) {
    ms = qMax(0, ms);
    if (m_alertDuration == ms)
        return;
    m_alertDuration = ms;
    scheduleSave();
    emit changed();
    emit monitoringChanged();
}

void Configuration::setTreeThresholdPercent(double percent) {
    percent = qMax(0.0, percent);
    if (qFuzzyCompare(m_treeThresholdPercent + 1.0, percent + 1.0))
        return;
    m_treeThresholdPercent = percent;
    scheduleSave();
    emit changed();
    emit monitoringChanged();
}

void Configuration::setProcessThresholdPercent(double percent) {
    percent = qMax(0.0, percent);
    if (qFuzzyCompare(m_processThresholdPercent + 1.0, percent + 1.0))
        return;
    m_processThresholdPercent = percent;
    scheduleSave();
    emit changed();
    emit monitoringChanged();
}

void Configuration::setAnimateNames(bool enabled) {
    if (m_animateNames == enabled)
        return;
    m_animateNames = enabled;
    scheduleSave();
    emit changed();
}

void Configuration::setAnimationColor(const QColor &color) {
    if (!color.isValid())
        return;
    const QColor opaque(color.red(), color.green(), color.blue());
    if (m_animationColor == opaque)
        return;
    m_animationColor = opaque;
    scheduleSave();
    emit changed();
}

void Configuration::setAnimationDuration(int ms) {
    ms = qBound(100, ms, 999000);
    if (m_animationDuration == ms)
        return;
    m_animationDuration = ms;
    scheduleSave();
    emit changed();
}

void Configuration::loadIgnoreRules() {
    m_ignoredApplications.clear();
    const int count = m_settings.beginReadArray(QStringLiteral("IgnoredApplications"));
    for (int i = 0; i < count; ++i) {
        m_settings.setArrayIndex(i);
        IgnoreRule rule;
        rule.id = m_settings.value(QStringLiteral("Id")).toString();
        rule.label = m_settings.value(QStringLiteral("Name")).toString();
        rule.path = m_settings.value(QStringLiteral("Path")).toString();
        rule.applicationKey = m_settings.value(QStringLiteral("ApplicationKey")).toString();
        rule.permanent = m_settings.value(QStringLiteral("Permanent"), false).toBool();
        rule.tree = m_settings.value(QStringLiteral("Tree"), false).toBool();
        rule.pid = m_settings.value(QStringLiteral("Pid"), -1).toInt();
        rule.startTime = m_settings.value(QStringLiteral("StartTime")).toULongLong();
        rule.bootId = m_settings.value(QStringLiteral("BootId")).toString();
        if (rule.id.isEmpty() || (rule.permanent ? rule.applicationKey.isEmpty()
                : (rule.pid <= 0 || rule.startTime == 0 || rule.bootId.isEmpty())))
            continue;
        m_ignoredApplications.append(rule);
    }
    m_settings.endArray();
}

void Configuration::saveIgnoreRules() {
    // These are immediate actions, independent of deferred appearance edits.
    m_settings.remove(QStringLiteral("IgnoredApplications"));
    m_settings.beginWriteArray(QStringLiteral("IgnoredApplications"), m_ignoredApplications.size());
    for (int i = 0; i < m_ignoredApplications.size(); ++i) {
        m_settings.setArrayIndex(i);
        const IgnoreRule &rule = m_ignoredApplications.at(i);
        m_settings.setValue(QStringLiteral("Id"), rule.id);
        m_settings.setValue(QStringLiteral("Name"), rule.label);
        m_settings.setValue(QStringLiteral("Path"), rule.path);
        m_settings.setValue(QStringLiteral("ApplicationKey"), rule.applicationKey);
        m_settings.setValue(QStringLiteral("Permanent"), rule.permanent);
        m_settings.setValue(QStringLiteral("Tree"), rule.tree);
        if (!rule.permanent) {
            m_settings.setValue(QStringLiteral("Pid"), rule.pid);
            m_settings.setValue(QStringLiteral("StartTime"), QString::number(rule.startTime));
            m_settings.setValue(QStringLiteral("BootId"), rule.bootId);
        }
    }
    m_settings.endArray();
    m_settings.sync();
}

void Configuration::addIgnoreRule(IgnoreRule rule) {
    for (IgnoreRule &existing : m_ignoredApplications) {
        const bool sameTarget = rule.permanent == existing.permanent &&
            (rule.permanent ? rule.applicationKey == existing.applicationKey
             : rule.pid == existing.pid && rule.startTime == existing.startTime && rule.bootId == existing.bootId);
        if (!sameTarget)
            continue;
        if (rule.tree && !existing.tree) {
            existing.tree = true;
            saveIgnoreRules();
            emit ignoredApplicationsChanged();
        }
        return;
    }
    rule.id = QUuid::createUuid().toString();
    m_ignoredApplications.append(rule);
    saveIgnoreRules();
    emit ignoredApplicationsChanged();
}

void Configuration::removeIgnoreRules(const QStringList &ids) {
    bool removed = false;
    for (int i = m_ignoredApplications.size() - 1; i >= 0; --i) {
        if (ids.contains(m_ignoredApplications.at(i).id)) {
            m_ignoredApplications.removeAt(i);
            removed = true;
        }
    }
    if (removed) {
        saveIgnoreRules();
        emit ignoredApplicationsChanged();
    }
}
