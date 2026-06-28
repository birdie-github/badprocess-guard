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

public slots:
    void setOpacityPercent(int value);
    void setDarkMode(bool enabled);
    void setUseCustomFont(bool enabled);
    void setCustomFont(const QFont &font);
    void setWindowPosition(const QPoint &pos);

signals:
    void changed();

private:
    void load();
    void save();

    QSettings m_settings;
    int m_opacityPercent = 50;
    bool m_darkMode = true;
    bool m_useCustomFont = false;
    QFont m_customFont;
    QPoint m_windowPosition;
    bool m_hasWindowPosition = false;
};
