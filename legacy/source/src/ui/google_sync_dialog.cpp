// Plik implementacji panelu kontrolnego GoogleSyncDialog.
// Zapewnia responsywny interfejs uzytkownika, dynamiczna blokade przyciskow na czas trwania operacji w tle
// oraz ciagly podglad logow bez zamrazania okna.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "google_sync_dialog.h"
#include "google_calendar_sync.h"
#include "calendar_engine.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QInputDialog>
#include <QDateTime>

// 1. Zrzucanie komunikatow do konsoli logow
void GoogleSyncDialog::appendLog(const QString &msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logConsole->appendPlainText(QString("[%1] %2").arg(timestamp, msg));
}

// 2. Aktualizacja kontrolek stanu i aktywnosci przyciskow
void GoogleSyncDialog::updateStatusDisplay()
{
    bool paused = m_sync->isSyncPaused();
    bool authOk = m_sync->isAuthenticated();
    bool busy = m_sync->isSyncInProgress();

    m_statusLabel->setText(paused ? "Status daemona: ZATRZYMANY (Pauza)" : "Status daemona: AKTYWNY (Cykliczny)");
    m_btnToggleSync->setText(paused ? "▶ Wznów synchronizację" : "⏸ Wstrzymaj synchronizację");

    m_authLabel->setText(authOk ? "Autoryzacja: POŁĄCZONO Z GOOGLE API [OK]" : "Autoryzacja: BRAK TOKENU (Wymagane logowanie)");
    m_authLabel->setStyleSheet(authOk ? "color: #34c759; font-weight: bold;" : "color: #ff3b30; font-weight: bold;");

    m_credentialsPathLabel->setText(QString("Plik konfiguracji: %1").arg(m_sync->getCredentialsFilePath()));

    m_btnTwoWaySync->setEnabled(!busy);
    m_btnUploadAll->setEnabled(!busy);
    m_btnDownloadAll->setEnabled(!busy);
    m_btnWipeRemote->setEnabled(!busy);
}

// 3. Reset calego kalendarza Google
void GoogleSyncDialog::onWipeRemoteCalendarClicked()
{
    auto confirm = QMessageBox::warning(
        this,
        "Wyczyść kalendarz Google",
        "CZY NA PEWNO CHCESZ SKASOWAĆ WSZYSTKIE WYDARZENIA W GOOGLE CALENDAR?\n\n"
        "Zostanie wywołana oficjalna metoda API Clear w tle. Zdalne wpisy zostaną usunięte.\n"
        "Lokalny kalendarz w aplikacji pozostanie nienaruszony.",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No
    );

    if (confirm == QMessageBox::Yes) {
        appendLog("Zlecono asynchroniczne wyczyszczenie zdalnego kalendarza Google...");
        m_sync->clearPrimaryCalendar();
        updateStatusDisplay();
    }
}

// 4. Usuwanie lokalnych duplikatow
void GoogleSyncDialog::onDeduplicateLocalClicked()
{
    if (!m_engine) return;
    int count = m_engine->deduplicateLocalEvents();
    appendLog(QString("Lokalna deduplikacja: usunięto %1 zduplikowanych wydarzeń.").arg(count));
    QMessageBox::information(this, "Deduplikacja", QString("Usunięto %1 lokalnych duplikatów z kalendarza.").arg(count));
}

// 5. Pobranie wszystkich wpisow z Google
void GoogleSyncDialog::onDownloadAllClicked()
{
    appendLog("Zlecono pobieranie wszystkich wydarzeń z Google w tle...");
    m_sync->downloadAllFromGoogle();
    updateStatusDisplay();
}

// 6. Eksport lokalnych wpisow do Google
void GoogleSyncDialog::onUploadAllClicked()
{
    appendLog("Zlecono upload lokalnego kalendarza do Google w tle...");
    m_sync->uploadAllToGoogle();
    updateStatusDisplay();
}

// 7. Wymuszenie pelnej procedury Two-Way Sync
void GoogleSyncDialog::onTwoWaySyncClicked()
{
    appendLog("Zlecono dwukierunkową synchronizację w tle...");
    m_sync->triggerTwoWaySync();
    updateStatusDisplay();
}

// 8. Przelaczanie pauzy daemona
void GoogleSyncDialog::onToggleSyncClicked()
{
    m_sync->setSyncPaused(!m_sync->isSyncPaused());
    updateStatusDisplay();
}

// 9. Reczne wklejenie tokenu
void GoogleSyncDialog::onManualTokenClicked()
{
    bool ok = false;
    QString token = QInputDialog::getText(
        this, "Wprowadź Refresh Token",
        "Wklej token sesji Google (refresh_token):",
        QLineEdit::Normal, "", &ok
    );

    if (ok && !token.trimmed().isEmpty()) {
        m_sync->setManualRefreshToken(token.trimmed());
        updateStatusDisplay();
    }
}

// 10. Uruchomienie autoryzacji OAuth w przegladarce
void GoogleSyncDialog::onLoginOAuthClicked()
{
    appendLog("Uruchamianie przeglądarki do autoryzacji Google OAuth2...");
    bool ok = m_sync->startOAuthLogin();
    if (!ok) {
        QMessageBox::critical(this, "Błąd", "Nie można uruchomić logowania. Upewnij się, że plik credentials.json znajduje się w folderze Library.");
    }
}

// 11. Budowa prostego interfejsu panelu
void GoogleSyncDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(18, 16, 18, 16);
    mainLayout->setSpacing(10);

    QLabel *hdr = new QLabel("PANEL OBSŁUGI GOOGLE CALENDAR SYNC", this);
    hdr->setObjectName("dialogHeader");
    mainLayout->addWidget(hdr);

    m_statusLabel = new QLabel(this);
    m_authLabel = new QLabel(this);
    m_credentialsPathLabel = new QLabel(this);

    mainLayout->addWidget(m_statusLabel);
    mainLayout->addWidget(m_authLabel);
    mainLayout->addWidget(m_credentialsPathLabel);

    QHBoxLayout *authRow = new QHBoxLayout();
    m_btnLoginOAuth = new QPushButton("🔑 ZALOGUJ DO GOOGLE (OAuth Login)", this);
    m_btnLoginOAuth->setObjectName("primaryGreenBtn");
    m_btnManualToken = new QPushButton("📝 Wklej Token ręcznie", this);
    authRow->addWidget(m_btnLoginOAuth);
    authRow->addWidget(m_btnManualToken);
    mainLayout->addLayout(authRow);

    QHBoxLayout *row1 = new QHBoxLayout();
    m_btnToggleSync = new QPushButton(this);
    m_btnTwoWaySync = new QPushButton("🔄 Dwukierunkowy Two-Way Sync", this);
    row1->addWidget(m_btnToggleSync);
    row1->addWidget(m_btnTwoWaySync);
    mainLayout->addLayout(row1);

    QHBoxLayout *row2 = new QHBoxLayout();
    m_btnUploadAll = new QPushButton("⬆ Uploaduj wszystko do Google", this);
    m_btnDownloadAll = new QPushButton("⬇ Pobierz wszystko z Google", this);
    m_btnDeduplicateLocal = new QPushButton("✂ Usuń lokalne duplikaty", this);
    row2->addWidget(m_btnUploadAll);
    row2->addWidget(m_btnDownloadAll);
    row2->addWidget(m_btnDeduplicateLocal);
    mainLayout->addLayout(row2);

    QHBoxLayout *row3 = new QHBoxLayout();
    m_btnWipeRemote = new QPushButton("🧹 WYCZYŚĆ CAŁY KALENDARZ GOOGLE (API Clear)", this);
    m_btnWipeRemote->setObjectName("giveUpBtn");
    row3->addWidget(m_btnWipeRemote);
    mainLayout->addLayout(row3);

    QLabel *logLabel = new QLabel("Dziennik zdarzeń i operacji na żywo:", this);
    mainLayout->addWidget(logLabel);

    m_logConsole = new QPlainTextEdit(this);
    m_logConsole->setReadOnly(true);
    m_logConsole->setMinimumHeight(170);
    mainLayout->addWidget(m_logConsole);

    QHBoxLayout *bottomRow = new QHBoxLayout();
    bottomRow->addStretch();
    QPushButton *btnClose = new QPushButton("Zamknij", this);
    bottomRow->addWidget(btnClose);
    mainLayout->addLayout(bottomRow);

    connect(m_btnLoginOAuth, &QPushButton::clicked, this, &GoogleSyncDialog::onLoginOAuthClicked);
    connect(m_btnManualToken, &QPushButton::clicked, this, &GoogleSyncDialog::onManualTokenClicked);
    connect(m_btnToggleSync, &QPushButton::clicked, this, &GoogleSyncDialog::onToggleSyncClicked);
    connect(m_btnTwoWaySync, &QPushButton::clicked, this, &GoogleSyncDialog::onTwoWaySyncClicked);
    connect(m_btnUploadAll, &QPushButton::clicked, this, &GoogleSyncDialog::onUploadAllClicked);
    connect(m_btnDownloadAll, &QPushButton::clicked, this, &GoogleSyncDialog::onDownloadAllClicked);
    connect(m_btnDeduplicateLocal, &QPushButton::clicked, this, &GoogleSyncDialog::onDeduplicateLocalClicked);
    connect(m_btnWipeRemote, &QPushButton::clicked, this, &GoogleSyncDialog::onWipeRemoteCalendarClicked);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    connect(m_sync, &GoogleCalendarSync::syncLogMessage, this, &GoogleSyncDialog::appendLog);
    connect(m_sync, &GoogleCalendarSync::syncFinished, this, [this](bool ok, const QString &res) {
        appendLog(QString("Wynik: [%1] %2").arg(ok ? "SUKCES" : "BŁĄD", res));
        updateStatusDisplay();
    });

    updateStatusDisplay();
}

// 12. Konstruktor
GoogleSyncDialog::GoogleSyncDialog(GoogleCalendarSync *sync, CalendarEngine *engine, QWidget *parent)
    : QDialog(parent)
    , m_sync(sync)
    , m_engine(engine)
    , m_statusLabel(nullptr)
    , m_authLabel(nullptr)
    , m_credentialsPathLabel(nullptr)
    , m_btnLoginOAuth(nullptr)
    , m_btnManualToken(nullptr)
    , m_btnToggleSync(nullptr)
    , m_btnTwoWaySync(nullptr)
    , m_btnUploadAll(nullptr)
    , m_btnDownloadAll(nullptr)
    , m_btnDeduplicateLocal(nullptr)
    , m_btnWipeRemote(nullptr)
    , m_logConsole(nullptr)
{
    setWindowTitle("Google Calendar Sync - Panel kontrolny");
    resize(720, 520);
    setupUi();
}
