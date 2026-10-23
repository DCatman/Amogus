// Plik implementacji okna FixExtensionsDialog.
// Zapewnia podglad stanu wtyczek na zywo, dynamiczne odliczanie sekund do zamkniecia przegladarki
// oraz bezposrednie otwieranie folderu wtyczki w Eksploratorze Windows.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "fix_extensions_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <utility>

// 1. Otwarcie folderu wtyczki w Eksploratorze Windows
void FixExtensionsDialog::onOpenFolderClicked()
{
    QString path = "C:/Qt_projects/amogus/extension";
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

// 2. Odswiezenie tabeli stanu na zywo z dynamicznym odliczaniem sekund do zabicia przegladarki
void FixExtensionsDialog::refreshStatus()
{
    m_enforcer->forceScan();
    const auto &states = m_enforcer->getBrowserStates();
    m_table->setRowCount(states.size());

    qint64 now = QDateTime::currentSecsSinceEpoch();
    int row = 0;
    int runningCount = 0;
    int connectedCount = 0;

    for (const auto &b : std::as_const(states)) {
        m_table->setItem(row, 0, new QTableWidgetItem(b.displayName));

        QTableWidgetItem *procItem = new QTableWidgetItem(b.isRunning ? "Uruchomiona" : "Wyłączona");
        procItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(row, 1, procItem);

        bool isConnected = b.isRunning && (b.lastHeartbeatTimestamp > 0) && ((now - b.lastHeartbeatTimestamp) <= 15);

        QTableWidgetItem *plugItem = new QTableWidgetItem();
        plugItem->setTextAlignment(Qt::AlignCenter);

        QTableWidgetItem *secItem = new QTableWidgetItem();
        secItem->setTextAlignment(Qt::AlignCenter);

        if (!b.isRunning) {
            plugItem->setText("Nieaktywna");
            secItem->setText("Brak procesu");
        } else if (isConnected) {
            plugItem->setText("Połączona [OK]");
            secItem->setText("Bezpieczny");
            secItem->setForeground(QColor(52, 199, 89));
            connectedCount++;
        } else {
            plugItem->setText("WYŁĄCZONA / BRAK WTYCZKI");
            plugItem->setForeground(QColor(255, 59, 48));
            secItem->setText(QString("ZAMKNIĘCIE ZA %1s!").arg(b.secondsRemaining));
            secItem->setForeground(QColor(255, 59, 48));
        }

        m_table->setItem(row, 2, plugItem);
        m_table->setItem(row, 3, secItem);

        if (b.isRunning) runningCount++;
        row++;
    }

    m_summaryLabel->setText(QString("Wykryte przeglądarki: %1 | Aktywne połączenia wtyczki: %2")
                                .arg(runningCount).arg(connectedCount));
}

// 3. Budowa ukladu graficznego dialogu z instrukcjami instalacji
void FixExtensionsDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 18, 20, 18);
    mainLayout->setSpacing(12);

    QLabel *hdr = new QLabel("STATUS WTYCZKI W PRZEGLADARKACH (COMPANION EXTENSION)", this);
    hdr->setObjectName("dialogHeader");
    mainLayout->addWidget(hdr);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName("blockSubtitle");
    mainLayout->addWidget(m_summaryLabel);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    QStringList headers;
    headers << "Przeglądarka" << "Proces Windows" << "Stan wtyczki" << "Status ochrony";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    mainLayout->addWidget(m_table);

    QLabel *guide = new QLabel(
        "Instrukcja instalacji wtyczki w przeglądarce:\n"
        "1. Otwórz stronę rozszerzeń: chrome://extensions, edge://extensions lub opera://extensions\n"
        "2. Włącz 'Tryb programisty' (Developer mode) w prawym górnym rogu.\n"
        "3. Kliknij 'Załaduj rozpakowane' (Load unpacked) i wskaż poniższy folder rozszerzenia.\n"
        "4. W Operze upewnij się, że rozszerzenie ma włączony suwak aktywności.", this);
    guide->setObjectName("lockWarningBanner");
    mainLayout->addWidget(guide);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnOpenFolder = new QPushButton("Otwórz folder wtyczki w Eksploratorze", this);
    btnOpenFolder->setObjectName("primaryGreenBtn");
    QPushButton *btnRefresh = new QPushButton("Odśwież stan", this);
    QPushButton *btnClose = new QPushButton("Zamknij", this);

    btnRow->addWidget(btnOpenFolder);
    btnRow->addStretch();
    btnRow->addWidget(btnRefresh);
    btnRow->addWidget(btnClose);
    mainLayout->addLayout(btnRow);

    connect(btnOpenFolder, &QPushButton::clicked, this, &FixExtensionsDialog::onOpenFolderClicked);
    connect(btnRefresh, &QPushButton::clicked, this, &FixExtensionsDialog::refreshStatus);
    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);

    refreshStatus();
}

// 4. Konstruktor
FixExtensionsDialog::FixExtensionsDialog(BrowserEnforcer *enforcer, QWidget *parent)
    : QDialog(parent)
    , m_enforcer(enforcer)
    , m_table(nullptr)
    , m_summaryLabel(nullptr)
    , m_liveTimer(new QTimer(this))
{
    setWindowTitle("Fix extensions - Amogus Blocker");
    resize(740, 480);
    setupUi();

    connect(m_liveTimer, &QTimer::timeout, this, &FixExtensionsDialog::refreshStatus);
    m_liveTimer->start(1000);
}
