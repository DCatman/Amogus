// Plik naglowkowy glownego okna MainWindow.
// Integruje boczny pasek nawigacyjny, Debug Mode zabezpieczony wyzwaniami (2000 znakow lub puzzle 10x10),
// procedury zamykania przez [X] oraz dynamiczne ukrywanie narzedzi technicznych.
// Laczy sie z ProfileManager, CalendarEngine, SubjectManager, OverdueTracker, BrowserEnforcer i GoogleCalendarSync.

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QCloseEvent>
#include <QMap>
#include <QCheckBox>
#include "profile_manager.h"
#include "calendar_engine.h"
#include "subject_manager.h"
#include "overdue_tracker.h"
#include "browserenforcer.h"
#include "google_calendar_sync.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(ProfileManager *profileManager,
                        CalendarEngine *calendarEngine,
                        SubjectManager *subjectManager,
                        OverdueTracker *overdueTracker,
                        BrowserEnforcer *browserEnforcer,
                        GoogleCalendarSync *googleSync,
                        QWidget *parent = nullptr);
    ~MainWindow() override = default;

signals:
    void requestStop();

public slots:
    void onBrowserStatusUpdated(const QMap<QString, BrowserTrackState> &states);
    void onBrowserKilled(const QString &displayName);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onNavButtonClicked(int viewIndex);
    void onGoogleSyncPanelClicked();
    void onFixExtensionsClicked();
    void onExitRequested();
    void onDebugModeToggled(bool checked);
    void onCreateBackupClicked();
    void onRestoreBackupClicked();
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void setupUi();
    QWidget *createSidebar();

    ProfileManager *m_profileManager;
    CalendarEngine *m_calendarEngine;
    SubjectManager *m_subjectManager;
    OverdueTracker *m_overdueTracker;
    BrowserEnforcer *m_browserEnforcer;
    GoogleCalendarSync *m_googleSync;

    QStackedWidget *m_stackedWidget;
    QPushButton *m_navDashboardBtn;
    QPushButton *m_navBlocksBtn;
    QPushButton *m_navCalendarBtn;
    QPushButton *m_navSubjectsBtn;
    QPushButton *m_navOverdueBtn;

    QPushButton *m_btnGoogleSyncPanel;
    QPushButton *m_btnCreateBackup;
    QPushButton *m_btnRestoreBackup;
    QPushButton *m_fixExtensionsBtn;
    QPushButton *m_exitBtn;
    QCheckBox *m_chkDebugMode;

    QSystemTrayIcon *m_trayIcon;
    QMap<QString, BrowserTrackState> m_latestBrowserStates;
};

#endif // MAINWINDOW_H
