// Plik implementacji EditBlockDialog.
// Realizuje masowy import regul z tekstu i plikow TXT (z podzialem na wyjatki stron i aplikacje title:/file:).
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "edit_block_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QTextEdit>

// 1. Sprawdzenie czy dany blok jest zablokowany klodka
bool EditBlockDialog::isCurrentBlockLocked() const
{
    if (m_profileManager) {
        return m_profileManager->isProfileLocked(m_profile.name);
    }
    return m_profile.isLocked;
}

// 2. Parser masowego tekstu (wyjatki stron oraz aplikacje title: i file:)
void EditBlockDialog::parseAndAppendRules(const QString &text)
{
    QStringList lines = text.split(QRegularExpression("[\r\n]+"), Qt::SkipEmptyParts);
    bool inAppsSection = false;
    int addedExceptions = 0;
    int addedApps = 0;

    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith("#") || line.startsWith("//")) continue;

        if (line.contains("do appliakcji:", Qt::CaseInsensitive) ||
            line.contains("do aplikacji:", Qt::CaseInsensitive) ||
            line.contains("[apps]", Qt::CaseInsensitive)) {
            inAppsSection = true;
            continue;
        }

        if (line.contains("do stron:", Qt::CaseInsensitive) ||
            line.contains("wyjątki:", Qt::CaseInsensitive) ||
            line.contains("[exceptions]", Qt::CaseInsensitive)) {
            inAppsSection = false;
            continue;
        }

        if (inAppsSection || line.startsWith("title:", Qt::CaseInsensitive) || line.startsWith("file:", Qt::CaseInsensitive)) {
            QString appRule = line;
            if (appRule.startsWith("title:", Qt::CaseInsensitive)) {
                appRule = appRule.mid(6).trimmed();
            }

            // Unikanie duplikatow
            bool exists = false;
            for (int i = 0; i < m_appsList->count(); ++i) {
                if (m_appsList->item(i)->text().compare(appRule, Qt::CaseInsensitive) == 0) {
                    exists = true; break;
                }
            }
            if (!exists) {
                m_appsList->addItem(appRule);
                addedApps++;
            }
        } else {
            bool exists = false;
            for (int i = 0; i < m_exceptionsList->count(); ++i) {
                if (m_exceptionsList->item(i)->text().compare(line, Qt::CaseInsensitive) == 0) {
                    exists = true; break;
                }
            }
            if (!exists) {
                m_exceptionsList->addItem(line);
                addedExceptions++;
            }
        }
    }

    QMessageBox::information(this, "Import zakończony",
                             QString("Pomyślnie zaimportowano:\n• Wyjątki stron: +%1\n• Aplikacje i okna: +%2")
                                 .arg(addedExceptions).arg(addedApps));
}

// 3. Masowy import z okna dialogowego (wklejanie listy reguł)
void EditBlockDialog::onBulkImportTextClicked()
{
    if (isCurrentBlockLocked()) {
        QMessageBox::warning(this, "Profil zablokowany", "Profil jest aktywny i zablokowany. Masowy import jest zablokowany.");
        return;
    }

    QDialog dlg(this);
    dlg.setWindowTitle("Masowy import reguł (Wklej tekst)");
    dlg.resize(620, 500);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel("Wklej poniżej listę reguł (wyjątki stron, domeny oraz po 'do aplikacji:' reguły title: i file:):", &dlg));

    QTextEdit *edit = new QTextEdit(&dlg);
    edit->setPlaceholderText("*.html*\n*.pdf\n*github*\n\ndo aplikacji:\ntitle:*age of empires*\nfile:C:/Games/...");
    layout->addWidget(edit);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *btnCancel = new QPushButton("Anuluj", &dlg);
    QPushButton *btnOk = new QPushButton("Zatwierdź i dodaj", &dlg);
    btnOk->setObjectName("primaryGreenBtn");
    btnRow->addStretch();
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnOk);
    layout->addLayout(btnRow);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

    if (dlg.exec() == QDialog::Accepted) {
        parseAndAppendRules(edit->toPlainText());
    }
}

// 4. Masowy import bezposrednio z pliku .txt
void EditBlockDialog::onBulkImportFileClicked()
{
    if (isCurrentBlockLocked()) {
        QMessageBox::warning(this, "Profil zablokowany", "Profil jest aktywny i zablokowany.");
        return;
    }

    QString path = QFileDialog::getOpenFileName(this, "Wybierz plik tekstowy z regułami", "", "Pliki tekstowe (*.txt);;Wszystkie pliki (*.*)");
    if (path.isEmpty()) return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Błąd", "Nie można otworzyć wskazanego pliku.");
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();
    parseAndAppendRules(content);
}

// 5. Obsluga klawisza Delete na listach
bool EditBlockDialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent *ke = static_cast<QKeyEvent *>(event);
        if (ke->key() == Qt::Key_Delete) {
            if (obj == m_appsList) {
                onRemoveApp();
                return true;
            } else if (obj == m_exceptionsList) {
                onRemoveException();
                return true;
            } else if (obj == m_websitesList) {
                onRemoveWebsite();
                return true;
            }
        }
    }
    return QDialog::eventFilter(obj, event);
}

// 6. Zapis konfiguracji i akceptacja
void EditBlockDialog::onSaveClicked()
{
    m_profile.blockedWebsites.clear();
    for (int i = 0; i < m_websitesList->count(); ++i) {
        m_profile.blockedWebsites.append(m_websitesList->item(i)->text().trimmed());
    }

    m_profile.websiteExceptions.clear();
    for (int i = 0; i < m_exceptionsList->count(); ++i) {
        m_profile.websiteExceptions.append(m_exceptionsList->item(i)->text().trimmed());
    }

    m_profile.blockedApps.clear();
    for (int i = 0; i < m_appsList->count(); ++i) {
        m_profile.blockedApps.append(m_appsList->item(i)->text().trimmed());
    }

    m_profile.scheduleType = m_rbCalendar->isChecked() ? ScheduleType::ScheduledInCalendar : ScheduleType::BlockedAtAllTimes;

    m_profile.breaks.clickBreakEnabled = m_chkClickBreak->isChecked();
    m_profile.breaks.clickBreakMinutes = m_spinClickMin->value();

    m_profile.breaks.charBreakEnabled = m_chkCharBreak->isChecked();
    m_profile.breaks.charBreakMinutes = m_spinCharMin->value();
    m_profile.breaks.charBreakChars = m_spinCharCount->value();

    m_profile.breaks.criticalBreakEnabled = m_chkCritBreak->isChecked();

    accept();
}

// 7. Obsluga usuwania calego profilu blokowania
void EditBlockDialog::onDeleteProfileClicked()
{
    if (m_profile.isOverdue) {
        QMessageBox::warning(this, "Profil nieusuwalny", "Profil 'overdue' jest wbudowanym profilem systemowym i nie można go usunąć.");
        return;
    }

    if (isCurrentBlockLocked()) {
        QMessageBox::warning(this, "Blokada aktywna", "Ten profil jest zablokowany! Aby go usunąć, musisz najpierw przejść przez ekran odblokowania.");
        return;
    }

    auto res = QMessageBox::question(this, "Usuń profil", QString("Czy na pewno chcesz bezpowrotnie usunąć profil '%1'?").arg(m_profile.name));
    if (res == QMessageBox::Yes) {
        m_deletionRequested = true;
        accept();
    }
}

// 8. Usuwanie aplikacji
void EditBlockDialog::onRemoveApp()
{
    if (isCurrentBlockLocked()) {
        QMessageBox::warning(this, "Blokada aktywna", "Profil jest zablokowany! Nie można usuwać zablokowanych aplikacji.");
        return;
    }
    qDeleteAll(m_appsList->selectedItems());
}

// 9. Dodanie maski okna
void EditBlockDialog::onAddWindowTitle()
{
    bool ok = false;
    QString title = QInputDialog::getText(this, "Maska okna", "Podaj tytuł lub maskę okna (np. *note* lub *Terraria*):", QLineEdit::Normal, "", &ok);
    if (ok && !title.trimmed().isEmpty()) {
        m_appsList->addItem(title.trimmed());
    }
}

// 10. Dodanie folderu
void EditBlockDialog::onAddFolder()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Wybierz folder do zablokowania");
    if (!dir.isEmpty()) {
        m_appsList->addItem("file:/" + dir + "/*");
    }
}

// 11. Dodanie pliku wykonywalnego
void EditBlockDialog::onAddExeFile()
{
    QString file = QFileDialog::getOpenFileName(this, "Wybierz plik wykonywalny", "", "Programy (*.exe)");
    if (!file.isEmpty()) {
        m_appsList->addItem(QFileInfo(file).fileName());
    }
}

// 12. Usuniecie wyjatku strony
void EditBlockDialog::onRemoveException()
{
    qDeleteAll(m_exceptionsList->selectedItems());
}

// 13. Dodanie pojedynczego wyjatku strony
void EditBlockDialog::onAddException()
{
    if (isCurrentBlockLocked()) {
        QMessageBox::warning(this, "Blokada aktywna", "Profil jest zablokowany! W trybie aktywnej blokady nie wolno dodawać nowych wyjątków stron.");
        return;
    }

    QString text = m_exceptionInput->text().trimmed();
    if (!text.isEmpty()) {
        m_exceptionsList->addItem(text);
        m_exceptionInput->clear();
    }
}

// 14. Usuniecie zablokowanej witryny
void EditBlockDialog::onRemoveWebsite()
{
    if (isCurrentBlockLocked()) {
        QMessageBox::warning(this, "Blokada aktywna", "Profil jest zablokowany! Nie można usuwać zablokowanych stron.");
        return;
    }
    qDeleteAll(m_websitesList->selectedItems());
}

// 15. Dodanie zablokowanej witryny
void EditBlockDialog::onAddWebsite()
{
    QString text = m_websiteInput->text().trimmed();
    if (!text.isEmpty()) {
        m_websitesList->addItem(text);
        m_websiteInput->clear();
    }
}

// 16. Zakladka Breaks & Schedule
QWidget *EditBlockDialog::createBreaksTab()
{
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QLabel *lblSched = new QLabel("SPOSÓB WYSTĘPOWANIA BLOKADY:", this);
    lblSched->setObjectName("tabSectionTitle");
    layout->addWidget(lblSched);

    m_rbAllTimes = new QRadioButton("Blocked at all times (blokada działa stale)", this);
    m_rbCalendar = new QRadioButton("Scheduled in calendar (blokada działa według kalendarza)", this);
    if (m_profile.scheduleType == ScheduleType::ScheduledInCalendar) {
        m_rbCalendar->setChecked(true);
    } else {
        m_rbAllTimes->setChecked(true);
    }
    layout->addWidget(m_rbAllTimes);
    layout->addWidget(m_rbCalendar);

    layout->addSpacing(14);
    QLabel *lblBreaks = new QLabel("DOSTĘPNE TRYBY PRZERW:", this);
    lblBreaks->setObjectName("tabSectionTitle");
    layout->addWidget(lblBreaks);

    QHBoxLayout *r1 = new QHBoxLayout();
    m_chkClickBreak = new QCheckBox("Przerwa po kliknięciu na:", this);
    m_chkClickBreak->setChecked(m_profile.breaks.clickBreakEnabled);
    m_spinClickMin = new QSpinBox(this);
    m_spinClickMin->setRange(1, 120);
    m_spinClickMin->setValue(m_profile.breaks.clickBreakMinutes);
    m_spinClickMin->setSuffix(" min");
    r1->addWidget(m_chkClickBreak);
    r1->addWidget(m_spinClickMin);
    r1->addStretch();
    layout->addLayout(r1);

    QHBoxLayout *r2 = new QHBoxLayout();
    m_chkCharBreak = new QCheckBox("Przerwa za opłatą znaków na:", this);
    m_chkCharBreak->setChecked(m_profile.breaks.charBreakEnabled);
    m_spinCharMin = new QSpinBox(this);
    m_spinCharMin->setRange(1, 120);
    m_spinCharMin->setValue(m_profile.breaks.charBreakMinutes);
    m_spinCharMin->setSuffix(" min");
    m_spinCharCount = new QSpinBox(this);
    m_spinCharCount->setRange(100, 10000);
    m_spinCharCount->setSingleStep(250);
    m_spinCharCount->setValue(m_profile.breaks.charBreakChars);
    m_spinCharCount->setSuffix(" znaków");
    r2->addWidget(m_chkCharBreak);
    r2->addWidget(m_spinCharMin);
    r2->addWidget(new QLabel("Wymaga:", this));
    r2->addWidget(m_spinCharCount);
    r2->addStretch();
    layout->addLayout(r2);

    QHBoxLayout *r3 = new QHBoxLayout();
    m_chkCritBreak = new QCheckBox("Przerwa krytyczna (3 min, sztywny cooldown 60 min)", this);
    m_chkCritBreak->setChecked(m_profile.breaks.criticalBreakEnabled);
    r3->addWidget(m_chkCritBreak);
    r3->addStretch();
    layout->addLayout(r3);

    layout->addStretch();
    return tab;
}

// 17. Zakladka Apps z opcja masowego importu
QWidget *EditBlockDialog::createAppsTab()
{
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QHBoxLayout *btnRow = new QHBoxLayout();
    QPushButton *addExe = new QPushButton("+ Add exe...", this);
    QPushButton *addFolder = new QPushButton("+ Add folder...", this);
    QPushButton *addTitle = new QPushButton("+ Add title...", this);
    m_btnBulkApps = new QPushButton("📋 Wklej listę...", this);
    btnRow->addWidget(addExe);
    btnRow->addWidget(addFolder);
    btnRow->addWidget(addTitle);
    btnRow->addWidget(m_btnBulkApps);
    layout->addLayout(btnRow);

    m_appsList = new QListWidget(this);
    m_appsList->addItems(m_profile.blockedApps);
    m_appsList->installEventFilter(this);
    layout->addWidget(m_appsList);

    m_removeAppBtn = new QPushButton("Remove selected (Delete)", this);
    m_removeAppBtn->setEnabled(!isCurrentBlockLocked());
    layout->addWidget(m_removeAppBtn);

    connect(addExe, &QPushButton::clicked, this, &EditBlockDialog::onAddExeFile);
    connect(addFolder, &QPushButton::clicked, this, &EditBlockDialog::onAddFolder);
    connect(addTitle, &QPushButton::clicked, this, &EditBlockDialog::onAddWindowTitle);
    connect(m_btnBulkApps, &QPushButton::clicked, this, &EditBlockDialog::onBulkImportTextClicked);
    connect(m_removeAppBtn, &QPushButton::clicked, this, &EditBlockDialog::onRemoveApp);

    return tab;
}

// 18. Zakladka Exceptions z opcja masowego importu
QWidget *EditBlockDialog::createExceptionsTab()
{
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    bool locked = isCurrentBlockLocked();

    QHBoxLayout *inputRow = new QHBoxLayout();
    m_exceptionInput = new QLineEdit(this);
    m_exceptionInput->setPlaceholderText(locked ? "Dodawanie wyjątków jest zablokowane w trakcie aktywnej sesji" : "np. *altium*, *.pdf, google.com/*");
    m_exceptionInput->setEnabled(!locked);

    m_addExceptionBtn = new QPushButton("+ Add (Enter)", this);
    m_addExceptionBtn->setEnabled(!locked);

    m_btnBulkExceptions = new QPushButton("📋 Wklej listę...", this);
    m_btnBulkExceptions->setEnabled(!locked);

    inputRow->addWidget(m_exceptionInput);
    inputRow->addWidget(m_addExceptionBtn);
    inputRow->addWidget(m_btnBulkExceptions);
    layout->addLayout(inputRow);

    m_exceptionsList = new QListWidget(this);
    m_exceptionsList->addItems(m_profile.websiteExceptions);
    m_exceptionsList->installEventFilter(this);
    layout->addWidget(m_exceptionsList);

    m_removeExceptionBtn = new QPushButton("Remove selected (Delete)", this);
    layout->addWidget(m_removeExceptionBtn);

    connect(m_addExceptionBtn, &QPushButton::clicked, this, &EditBlockDialog::onAddException);
    connect(m_exceptionInput, &QLineEdit::returnPressed, this, &EditBlockDialog::onAddException);
    connect(m_btnBulkExceptions, &QPushButton::clicked, this, &EditBlockDialog::onBulkImportTextClicked);
    connect(m_removeExceptionBtn, &QPushButton::clicked, this, &EditBlockDialog::onRemoveException);

    return tab;
}

// 19. Zakladka Websites
QWidget *EditBlockDialog::createWebsitesTab()
{
    QWidget *tab = new QWidget(this);
    QVBoxLayout *layout = new QVBoxLayout(tab);

    QHBoxLayout *inputRow = new QHBoxLayout();
    m_websiteInput = new QLineEdit(this);
    m_websiteInput->setPlaceholderText("Wpisz * dla całego internetu lub domenę");
    QPushButton *addBtn = new QPushButton("+ Add (Enter)", this);
    inputRow->addWidget(m_websiteInput);
    inputRow->addWidget(addBtn);
    layout->addLayout(inputRow);

    m_websitesList = new QListWidget(this);
    m_websitesList->addItems(m_profile.blockedWebsites.isEmpty() ? QStringList{"*"} : m_profile.blockedWebsites);
    m_websitesList->installEventFilter(this);
    layout->addWidget(m_websitesList);

    m_removeWebsiteBtn = new QPushButton("Remove selected (Delete)", this);
    m_removeWebsiteBtn->setEnabled(!isCurrentBlockLocked());
    layout->addWidget(m_removeWebsiteBtn);

    connect(addBtn, &QPushButton::clicked, this, &EditBlockDialog::onAddWebsite);
    connect(m_websiteInput, &QLineEdit::returnPressed, this, &EditBlockDialog::onAddWebsite);
    connect(m_removeWebsiteBtn, &QPushButton::clicked, this, &EditBlockDialog::onRemoveWebsite);

    return tab;
}

// 20. Budowa okna
void EditBlockDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *header = new QLabel(QString("WHAT DOES '%1' BLOCK?").arg(m_profile.name.toUpper()), this);
    header->setObjectName("dialogHeader");
    header->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(header);

    if (isCurrentBlockLocked()) {
        QLabel *lockBanner = new QLabel("🔒 PROFIL AKTYWNIE ZABLOKOWANY: Trwa sesja nauki. Dodawanie aplikacji i usuwanie wyjątków stron jest dozwolone. Usuwanie aplikacji oraz dodawanie wyjątków jest zablokowane.", this);
        lockBanner->setObjectName("lockWarningBanner");
        lockBanner->setWordWrap(true);
        mainLayout->addWidget(lockBanner);
    }

    QTabWidget *tabs = new QTabWidget(this);
    tabs->addTab(createWebsitesTab(), "Websites");
    tabs->addTab(createExceptionsTab(), "Website Exceptions");
    tabs->addTab(createAppsTab(), "Apps");
    tabs->addTab(createBreaksTab(), "Breaks & Schedule");
    mainLayout->addWidget(tabs);

    QHBoxLayout *bulkRow = new QHBoxLayout();
    m_btnBulkImportText = new QPushButton("📋 Wklej pełną listę reguł (Tekst)", this);
    m_btnBulkImportText->setObjectName("primaryGreenBtn");
    m_btnBulkImportFile = new QPushButton("📂 Wczytaj reguły z pliku .txt", this);
    bulkRow->addWidget(m_btnBulkImportText);
    bulkRow->addWidget(m_btnBulkImportFile);
    bulkRow->addStretch();
    mainLayout->addLayout(bulkRow);

    QHBoxLayout *actionRow = new QHBoxLayout();
    m_btnDeleteProfile = new QPushButton("Usuń ten profil", this);
    m_btnDeleteProfile->setObjectName("giveUpBtn");
    m_btnDeleteProfile->setEnabled(!isCurrentBlockLocked() && !m_profile.isOverdue);
    actionRow->addWidget(m_btnDeleteProfile);

    actionRow->addStretch();
    QPushButton *cancelBtn = new QPushButton("Close without saving", this);
    QPushButton *saveBtn = new QPushButton("Save (Enter)", this);
    saveBtn->setObjectName("primaryGreenBtn");
    actionRow->addWidget(cancelBtn);
    actionRow->addWidget(saveBtn);
    mainLayout->addLayout(actionRow);

    connect(m_btnBulkImportText, &QPushButton::clicked, this, &EditBlockDialog::onBulkImportTextClicked);
    connect(m_btnBulkImportFile, &QPushButton::clicked, this, &EditBlockDialog::onBulkImportFileClicked);
    connect(m_btnDeleteProfile, &QPushButton::clicked, this, &EditBlockDialog::onDeleteProfileClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &EditBlockDialog::onSaveClicked);
}

// 21. Sprawdzenie czy zadano usuniecia profilu
bool EditBlockDialog::isProfileDeletionRequested() const
{
    return m_deletionRequested;
}

// 22. Getter profilu
BlockProfile EditBlockDialog::getUpdatedProfile() const
{
    return m_profile;
}

// 23. Konstruktor
EditBlockDialog::EditBlockDialog(const BlockProfile &profile, ProfileManager *profileManager, QWidget *parent)
    : QDialog(parent)
    , m_profile(profile)
    , m_profileManager(profileManager)
    , m_deletionRequested(false)
    , m_websiteInput(nullptr)
    , m_websitesList(nullptr)
    , m_removeWebsiteBtn(nullptr)
    , m_exceptionInput(nullptr)
    , m_addExceptionBtn(nullptr)
    , m_btnBulkExceptions(nullptr)
    , m_exceptionsList(nullptr)
    , m_removeExceptionBtn(nullptr)
    , m_appsList(nullptr)
    , m_removeAppBtn(nullptr)
    , m_btnBulkApps(nullptr)
    , m_rbAllTimes(nullptr)
    , m_rbCalendar(nullptr)
    , m_chkClickBreak(nullptr)
    , m_spinClickMin(nullptr)
    , m_chkCharBreak(nullptr)
    , m_spinCharMin(nullptr)
    , m_spinCharCount(nullptr)
    , m_chkCritBreak(nullptr)
    , m_btnBulkImportText(nullptr)
    , m_btnBulkImportFile(nullptr)
    , m_btnDeleteProfile(nullptr)
{
    setWindowTitle(QString("Edycja bloku - %1").arg(profile.name));
    resize(700, 620);
    setupUi();
}
