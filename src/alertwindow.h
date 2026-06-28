#pragma once

#include "configuration.h"
#include "processentrywidget.h"
#include "processmonitor.h"
#include "settingsdialog.h"

#include <QFrame>
#include <QPointer>
#include <QPropertyAnimation>
#include <QToolButton>

class QVBoxLayout;

class AlertWindow : public QFrame {
    Q_OBJECT
    Q_PROPERTY(int animatedHeight READ animatedHeight WRITE setAnimatedHeight)

public:
    explicit AlertWindow(Configuration *config, QWidget *parent = nullptr);

    int animatedHeight() const { return m_animatedHeight; }
    void setAnimatedHeight(int height);

public slots:
    void setBadProcesses(const QVector<BadProcess> &processes);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void applyConfiguration();
    void animateToContentHeight();
    void showSettings();
    void confirmTerminate(const BadProcess &process);
    int contentHeightForRows(int rows) const;

    Configuration *m_config = nullptr;
    QVBoxLayout *m_layout = nullptr;
    QToolButton *m_settingsButton = nullptr;
    QVector<ProcessEntryWidget *> m_entries;
    QVector<BadProcess> m_processes;
    QPropertyAnimation *m_animation = nullptr;
    QPointer<SettingsDialog> m_settingsDialog;
    int m_animatedHeight = 0;
    bool m_dragging = false;
    QPoint m_dragOffset;
};
