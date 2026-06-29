#pragma once

#include <QFont>
#include <QObject>
#include <QPoint>
#include <QSettings>

class Configuration : public QObject {
    Q_OBJECT

public:
    explicit Configuration(QObject *parent = nullptr);

    int opacityPercent() const { return m_opacityPercent; }
    bool darkMode() const { return m_darkMode; }
    bool useCustomFont() const { return m_useCustomFont; }
    QFont customFont() const { return m_customFont; }
    QPoint windowPosition() const { return m_windowPosition; }
    bool hasWindowPosition() const { return m_hasWindowPosition; }
    bool allWorkspaces() const { return m_allWorkspaces; }
    int refreshInterval() const { return m_refreshInterval; }
    int alertDuration() const { return m_alertDuration; }
    double treeThresholdPercent() const { return m_treeThresholdPercent; }
    double processThresholdPercent() const { return m_processThresholdPercent; }

    void beginDeferredSave();
    void endDeferredSave();

public slots:
    void setOpacityPercent(int value);
    void setDarkMode(bool enabled);
    void setUseCustomFont(bool enabled);
    void setCustomFont(const QFont &font);
    void setWindowPosition(const QPoint &pos);
    void setAllWorkspaces(bool enabled);
    void setRefreshInterval(int ms);
    void setAlertDuration(int ms);
    void setTreeThresholdPercent(double percent);
    void setProcessThresholdPercent(double percent);

signals:
    void changed();

private:
    void load();
    void save();
    void scheduleSave();

    QSettings m_settings;
    int m_opacityPercent = 50;
    bool m_darkMode = true;
    bool m_useCustomFont = false;
    QFont m_customFont;
    QPoint m_windowPosition;
    bool m_hasWindowPosition = false;
    bool m_allWorkspaces = false;
    int m_refreshInterval = 5000;
    int m_alertDuration = 3000;
    double m_treeThresholdPercent = 50.0;
    double m_processThresholdPercent = 50.0;
    bool m_deferSaves = false;
    bool m_hasDeferredSave = false;
};
