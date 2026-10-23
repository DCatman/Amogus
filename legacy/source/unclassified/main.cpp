// Glowny plik projektu Amogus Blocker.
// Koordynuje baze SQLite, Watchdoga, serwer wtyczek, kalendarz,
// BrowserEnforcer (permanentna ochrona 60s) oraz ProfileManager.
// Laczy sie z calym systemem aplikacji.

#include <QApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include "mainwindow.h"
#include "appblocker.h"
#include "ruleserver.h"
#include "browserenforcer.h"
#include "watchdog.h"
#include "database_manager.h"
#include "profile_manager.h"
#include "calendar_engine.h"
#include "subject_manager.h"
#include "google_calendar_sync.h"
#include "overdue_tracker.h"

int main(int argc, char *argv[])
{
    if (Watchdog::runIfWatchdogMode(argc, argv)) {
        return 0;
    }

    QApplication a(argc, argv);
    a.setQuitOnLastWindowClosed(false);
    a.setOrganizationName("Amogus");
    a.setApplicationName("AmogusBlocker");

    QFile styleFile(":/styles.qss");
    if (styleFile.open(QFile::ReadOnly)) {
        a.setStyleSheet(QString::fromUtf8(styleFile.readAll()));
        styleFile.close();
    }

    if (!DatabaseManager::instance().initializeDatabase()) {
        QMessageBox::critical(nullptr, "BŁĄD KRYTYCZNY BAZY DANYCH",
                              "Sterownik QSQLITE nie został załadowany lub plik data.db jest niedostępny.\n\n"
                              "Aplikacja uruchomi się, ale ŻADNE zmiany NIE BĘDĄ ZAPISYWANE!");
    }

    AppBlocker appBlocker;
    RuleServer ruleServer;
    BrowserEnforcer browserEnforcer;
    Watchdog watchdog;
    ProfileManager profileManager;
    CalendarEngine calendarEngine;
    OverdueTracker overdueTracker;
    SubjectManager subjectManager(&calendarEngine, &overdueTracker);
    GoogleCalendarSync googleSync(&calendarEngine);

    MainWindow window(&profileManager, &calendarEngine, &subjectManager, &overdueTracker, &browserEnforcer, &googleSync);

    watchdog.startSupervisor();

    QObject::connect(&profileManager, &ProfileManager::activeRulesChanged,
                     [&ruleServer, &appBlocker](bool active, const QStringList &exceptions, const QStringList &blockedApps) {
                         ruleServer.setConfiguration(active, exceptions);
                         appBlocker.setRules(blockedApps);
                         if (active) {
                             appBlocker.start(500);
                         } else {
                             appBlocker.stop();
                         }

                         QJsonObject json;
                         json["active"] = active;
                         json["exceptions"] = QJsonArray::fromStringList(exceptions);
                         json["blockedApps"] = QJsonArray::fromStringList(blockedApps);
                         DatabaseManager::instance().syncRulesToSharedMemory(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
                     });

    QObject::connect(&calendarEngine, &CalendarEngine::calendarWindowActiveChanged,
                     &profileManager, &ProfileManager::setCalendarWindowActive);

    QObject::connect(&calendarEngine, &CalendarEngine::triggerProfileActivation,
                     &profileManager, &ProfileManager::setCalendarWindowActive);

    QObject::connect(&ruleServer, &RuleServer::heartbeatReceived,
                     &browserEnforcer, &BrowserEnforcer::onHeartbeatReceived);

    QObject::connect(&browserEnforcer, &BrowserEnforcer::statusUpdated,
                     &window, &MainWindow::onBrowserStatusUpdated);
    QObject::connect(&browserEnforcer, &BrowserEnforcer::browserKilled,
                     &window, &MainWindow::onBrowserKilled);

    QObject::connect(&window, &MainWindow::requestStop,
                     [&watchdog, &a]() {
                         watchdog.stopSupervisor();
                         DatabaseManager::instance().closeDatabase();
                         a.quit();
                     });

    QObject::connect(&a, &QCoreApplication::aboutToQuit, []() {
        DatabaseManager::instance().closeDatabase();
    });

    ruleServer.startServer(13370);
    googleSync.initialize();

    window.show();
    return a.exec();
}
