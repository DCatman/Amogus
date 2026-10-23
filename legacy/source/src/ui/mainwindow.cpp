// Plik implementacji klasy MainWindow.
// Obsluguje zamykanie programu przez [X], wyzwania odblokowania oraz autoryzacje wlaczenia trybu Debug
// (wymog zaliczenia 2000 znakow badz ukladanki bialych puzzli w rozmiarze 10x10).
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "mainwindow.h"
#include "blocks_view.h"
#include "dashboard_view.h"
#include "calendar_widget.h"
#include "subjects_view.h"
#include "overdue_tracker_widget.h"
#include "fix_extensions_dialog.h"
#include "google_sync_dialog.h"
#include "unlock_dialog.h"
#include "minigame_unlock_dialog.h"
#include "whitepuzzlewidget.h"
#include "database_manager.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QMenu>
#include <QAction>
#include <QStyle>

// 1. Aktywacja okna z zasobnika systemowego
void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
        showNormal();
        activateWindow();
    }
}

// 2. Otwarcie panelu kontrolnego Google Calendar Sync
void MainWindow::onGoogleSyncPanelClicked()
{
    GoogleSyncDialog dlg(m_googleSync, m_calendarEngine, this);
    dlg.exec();
}

// 3. Otwarcie okna Fix Extensions
void MainWindow::onFixExtensionsClicked()
{
    FixExtensionsDialog dlg(m_browserEnforcer, this);
    dlg.exec();
}

// 4. Zapis backupu do pliku backup.json
void MainWindow::onCreateBackupClicked()
{
    if (DatabaseManager::instance().exportBackupToJson()) {
        QMessageBox::information(this, "Backup JSON", "Pomyślnie utworzono kopię zapasową w pliku backup.json!");
    } else {
        QMessageBox::warning(this, "Błąd", "Nie udało się zapisać pliku kopii zapasowej.");
    }
}

// 5. Przywrocenie bazy z pliku backup.json (Dostepne tylko w trybie Debug)
void MainWindow::onRestoreBackupClicked()
{
    if (!m_profileManager || !m_profileManager->isDebugMode()) {
        QMessageBox::warning(this, "Brak uprawnień", "Przywracanie bazy z pliku backup.json wymaga włączonego trybu Debug!");
        return;
    }

    auto confirm = QMessageBox::question(
        this, "Przywróć bazę",
        "CZY NA PEWNO CHCESZ PRZYWRÓCIĆ WSZYSTKIE DANE Z PLIKU backup.json?\n"
        "Bieżące dane w bazie zostaną zastąpione danymi z pliku kopii zapasowej.",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No
    );

    if (confirm == QMessageBox::Yes) {
        if (DatabaseManager::instance().importBackupFromJson()) {
            m_profileManager->loadFromDatabase();
            m_calendarEngine->loadFromDatabase();
            if (m_subjectManager) m_subjectManager->loadFromDatabase();
            m_overdueTracker->loadFromDatabase();

            QMessageBox::information(this, "Sukces", "Pomyślnie przywrócono całą bazę z backup.json!");
        } else {
            QMessageBox::critical(this, "Błąd", "Nie udało się odczytać pliku backup.json lub plik jest uszkodzony.");
        }
    }
}

// 6. Obsluga weryfikacji wlaczenia trybu Debug (2000 znakow lub puzzle 10x10)
void MainWindow::onDebugModeToggled(bool checked)
{
    if (!checked) {
        if (m_profileManager) m_profileManager->setDebugMode(false);
        m_btnGoogleSyncPanel->setVisible(false);
        m_fixExtensionsBtn->setVisible(false);
        m_btnCreateBackup->setVisible(false);
        m_btnRestoreBackup->setVisible(false);
        return;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Odblokowanie trybu Debug");
    msgBox.setText("Włączenie trybu Debug wymaga zaliczenia jednego z poniższych wyzwań:\n\n"
                   "• Przepisanie 2000 znaków (Japoński Romaji)\n"
                   "• Ułożenie białych puzzli w rozmiarze 10x10 (100 elementów)\n\n"
                   "Wybierz formę autoryzacji:");
    QPushButton *btnChars = msgBox.addButton("Wyzwanie japoński (2000 znaków)", QMessageBox::ActionRole);
    QPushButton *btnPuzzle = msgBox.addButton("Wyzwanie białe puzzle 10x10", QMessageBox::ActionRole);
    QPushButton *btnCancel = msgBox.addButton("Anuluj", QMessageBox::RejectRole);

    msgBox.exec();

    bool passed = false;
    if (msgBox.clickedButton() == btnChars) {
        UnlockDialog drill(2000, this);
        if (drill.exec() == QDialog::Accepted) {
            passed = true;
        }
    } else if (msgBox.clickedButton() == btnPuzzle) {
        MinigameUnlockDialog miniDlg(this);
        miniDlg.setWindowTitle("Wyzwanie Debug Mode - Białe puzzle 10x10");
        WhitePuzzleWidget *puzzleWidget = new WhitePuzzleWidget(&miniDlg);
        puzzleWidget->setDifficulty(WhitePuzzleWidget::Difficulty::Grid10x10, true);
        miniDlg.setCustomMinigame(puzzleWidget);
        if (miniDlg.exec() == QDialog::Accepted) {
            passed = true;
        }
    }

    if (passed) {
        if (m_profileManager) m_profileManager->setDebugMode(true);
        m_btnGoogleSyncPanel->setVisible(true);
        m_fixExtensionsBtn->setVisible(true);
        m_btnCreateBackup->setVisible(true);
        m_btnRestoreBackup->setVisible(true);
        QMessageBox::information(this, "Tryb Debug aktywny", "Wyzwanie zaliczone pomyślnie! Tryb Debug został włączony.");
    } else {
        m_chkDebugMode->blockSignals(true);
        m_chkDebugMode->setChecked(false);
        m_chkDebugMode->blockSignals(false);
        if (m_profileManager) m_profileManager->setDebugMode(false);
        m_btnGoogleSyncPanel->setVisible(false);
        m_fixExtensionsBtn->setVisible(false);
        m_btnCreateBackup->setVisible(false);
        m_btnRestoreBackup->setVisible(false);
    }
}

// 7. Glowne okno zamykania / odblokowywania (Wyzwanie japonski / Wyzwanie puzzle / Ukryj)
void MainWindow::onExitRequested()
{
    if (m_profileManager && m_profileManager->isDebugMode()) {
        emit requestStop();
        return;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Zamykanie aplikacji");
    msgBox.setText("Aplikacja Amogus Blocker działa permanentnie.\nWybierz akcję:");
    QPushButton *btnChars = msgBox.addButton("Wyzwanie japoński", QMessageBox::ActionRole);
    QPushButton *btnMini = msgBox.addButton("Wyzwanie białe puzzle", QMessageBox::ActionRole);
    QPushButton *btnHide = msgBox.addButton("Ukryj", QMessageBox::ActionRole);

    msgBox.exec();

    if (msgBox.clickedButton() == btnChars) {
        UnlockDialog drill(1000, this);
        if (drill.exec() == QDialog::Accepted) {
            emit requestStop();
        }
    } else if (msgBox.clickedButton() == btnMini) {
        MinigameUnlockDialog miniDlg(this);
        if (miniDlg.exec() == QDialog::Accepted) {
            emit requestStop();
        }
    } else if (msgBox.clickedButton() == btnHide) {
        hide();
        m_trayIcon->showMessage("Amogus Blocker", "Aplikacja działa w tle. Aby ją wyłączyć, użyj wyzwania Unlock.", QSystemTrayIcon::Information, 1500);
    }
}

// 8. Zdarzenie klikniecia [X] w prawym gornym rogu okna
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_profileManager && m_profileManager->isDebugMode()) {
        emit requestStop();
        event->accept();
        return;
    }

    event->ignore();
    onExitRequested();
}

// 9. Nawigacja widokami
void MainWindow::onNavButtonClicked(int viewIndex)
{
    m_stackedWidget->setCurrentIndex(viewIndex);
}

// 10. Powiadomienie w trayu o wylaczeniu przegladarki
void MainWindow::onBrowserKilled(const QString &displayName)
{
    m_trayIcon->showMessage("Amogus Blocker",
                            QString("Wyłączono %1 (brak wtyczki)!").arg(displayName),
                            QSystemTrayIcon::Warning, 3000);
}

// 11. Aktualizacja statusu przegladarek
void MainWindow::onBrowserStatusUpdated(const QMap<QString, BrowserTrackState> &states)
{
    m_latestBrowserStates = states;
}

// 12. Boczny pasek nawigacyjny z dynamicznym ukrywaniem narzedzi technicznych
QWidget *MainWindow::createSidebar()
{
    QWidget *sidebar = new QWidget(this);
    sidebar->setObjectName("sidebarContainer");
    sidebar->setFixedWidth(210);

    QVBoxLayout *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(12, 16, 12, 16);
    layout->setSpacing(6);

    QLabel *logo = new QLabel("AMOGUS\nBLOCKER", this);
    logo->setObjectName("sidebarLogo");
    layout->addWidget(logo);

    layout->addSpacing(16);

    m_navDashboardBtn = new QPushButton(" Dashboard", this);
    m_navDashboardBtn->setObjectName("navButton");
    m_navBlocksBtn = new QPushButton(" Blocks", this);
    m_navBlocksBtn->setObjectName("navButton");
    m_navCalendarBtn = new QPushButton(" Calendar", this);
    m_navCalendarBtn->setObjectName("navButton");
    m_navSubjectsBtn = new QPushButton(" Przedmioty", this);
    m_navSubjectsBtn->setObjectName("navButton");
    m_navOverdueBtn = new QPushButton(" Overdue Tracker", this);
    m_navOverdueBtn->setObjectName("navButton");

    layout->addWidget(m_navDashboardBtn);
    layout->addWidget(m_navBlocksBtn);
    layout->addWidget(m_navCalendarBtn);
    layout->addWidget(m_navSubjectsBtn);
    layout->addWidget(m_navOverdueBtn);

    layout->addStretch();

    m_btnGoogleSyncPanel = new QPushButton("📅 Google Sync Panel", this);
    m_btnGoogleSyncPanel->setObjectName("primaryGreenBtn");
    m_btnGoogleSyncPanel->setVisible(false);
    layout->addWidget(m_btnGoogleSyncPanel);

    m_fixExtensionsBtn = new QPushButton("Fix extensions", this);
    m_fixExtensionsBtn->setObjectName("fixExtensionsBtn");
    m_fixExtensionsBtn->setVisible(false);
    layout->addWidget(m_fixExtensionsBtn);

    m_btnCreateBackup = new QPushButton("💾 Zapisz backup.json", this);
    m_btnCreateBackup->setObjectName("primaryGreenBtn");
    m_btnCreateBackup->setVisible(false);
    layout->addWidget(m_btnCreateBackup);

    m_btnRestoreBackup = new QPushButton("🔄 Przywróć backup (Debug)", this);
    m_btnRestoreBackup->setObjectName("giveUpBtn");
    m_btnRestoreBackup->setVisible(false);
    layout->addWidget(m_btnRestoreBackup);

    m_chkDebugMode = new QCheckBox("🛠 Tryb Debug", this);
    m_chkDebugMode->setObjectName("debugModeCheck");
    layout->addWidget(m_chkDebugMode);

    m_exitBtn = new QPushButton("Wyłącz aplikację", this);
    m_exitBtn->setObjectName("giveUpBtn");
    layout->addWidget(m_exitBtn);

    connect(m_navDashboardBtn, &QPushButton::clicked, this, [this]() { onNavButtonClicked(0); });
    connect(m_navBlocksBtn, &QPushButton::clicked, this, [this]() { onNavButtonClicked(1); });
    connect(m_navCalendarBtn, &QPushButton::clicked, this, [this]() { onNavButtonClicked(2); });
    connect(m_navSubjectsBtn, &QPushButton::clicked, this, [this]() { onNavButtonClicked(3); });
    connect(m_navOverdueBtn, &QPushButton::clicked, this, [this]() { onNavButtonClicked(4); });

    connect(m_btnGoogleSyncPanel, &QPushButton::clicked, this, &MainWindow::onGoogleSyncPanelClicked);
    connect(m_fixExtensionsBtn, &QPushButton::clicked, this, &MainWindow::onFixExtensionsClicked);
    connect(m_btnCreateBackup, &QPushButton::clicked, this, &MainWindow::onCreateBackupClicked);
    connect(m_btnRestoreBackup, &QPushButton::clicked, this, &MainWindow::onRestoreBackupClicked);
    connect(m_chkDebugMode, &QCheckBox::toggled, this, &MainWindow::onDebugModeToggled);
    connect(m_exitBtn, &QPushButton::clicked, this, &MainWindow::onExitRequested);

    return sidebar;
}

// 13. Budowa ukladu glownego
void MainWindow::setupUi()
{
    QWidget *central = new QWidget(this);
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    mainLayout->addWidget(createSidebar());

    m_stackedWidget = new QStackedWidget(this);
    m_stackedWidget->addWidget(new DashboardView(m_profileManager, m_calendarEngine, this));
    m_stackedWidget->addWidget(new BlocksView(m_profileManager, m_calendarEngine, this));
    m_stackedWidget->addWidget(new CalendarWidget(m_calendarEngine, this));

    SubjectsView *subjectsView = new SubjectsView(m_subjectManager, m_calendarEngine, this);
    m_stackedWidget->addWidget(subjectsView);

    OverdueTrackerWidget *trackerWidget = new OverdueTrackerWidget(m_overdueTracker, m_calendarEngine, m_profileManager, this);
    trackerWidget->setSubjectManager(m_subjectManager);
    m_stackedWidget->addWidget(trackerWidget);

    mainLayout->addWidget(m_stackedWidget);
    setCentralWidget(central);

    m_stackedWidget->setCurrentIndex(0);

    m_trayIcon = new QSystemTrayIcon(this);
    m_trayIcon->setIcon(style()->standardIcon(QStyle::SP_ComputerIcon));

    QMenu *trayMenu = new QMenu(this);
    QAction *actShow = trayMenu->addAction("Otwórz Amogus Blocker");
    QAction *actExit = trayMenu->addAction("Wyłącz aplikację...");

    connect(actShow, &QAction::triggered, this, [this]() {
        showNormal();
        activateWindow();
    });
    connect(actExit, &QAction::triggered, this, &MainWindow::onExitRequested);

    m_trayIcon->setContextMenu(trayMenu);
    m_trayIcon->show();
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::onTrayIconActivated);
}

// 14. Konstruktor MainWindow
MainWindow::MainWindow(ProfileManager *profileManager,
                       CalendarEngine *calendarEngine,
                       SubjectManager *subjectManager,
                       OverdueTracker *overdueTracker,
                       BrowserEnforcer *browserEnforcer,
                       GoogleCalendarSync *googleSync,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_profileManager(profileManager)
    , m_calendarEngine(calendarEngine)
    , m_subjectManager(subjectManager)
    , m_overdueTracker(overdueTracker)
    , m_browserEnforcer(browserEnforcer)
    , m_googleSync(googleSync)
{
    setWindowTitle("Amogus Blocker Pro");
    resize(1180, 780);
    setupUi();
}
