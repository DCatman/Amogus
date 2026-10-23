// Plik implementacji okna EventEditorDialog.
// Obsluguje bezposrednie powiazanie wydarzenia z przedmiotem, co automatycznie wlicza czas do licznika nauki.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "event_editor_dialog.h"
#include "database_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

// 1. Zadanie usuniecia calej serii powtorzen
void EventEditorDialog::onDeleteSeriesClicked()
{
    if (m_profileManager && !m_event.linkedProfile.isEmpty() && m_profileManager->isProfileLocked(m_event.linkedProfile)) {
        QMessageBox::warning(this, "Plan zablokowany", "Ten blok jest powiązany ze zablokowanym profilem i nie może zostać usunięty.");
        return;
    }

    if (QMessageBox::question(this, "Usuń serię", "Czy na pewno chcesz usunąć całą serię powtórzeń?") == QMessageBox::Yes) {
        m_deleteSeriesRequested = true;
        accept();
    }
}

// 2. Bezwarunkowe usuniecie kafelka
void EventEditorDialog::onDebugForceDeleteClicked()
{
    if (m_profileManager && !m_event.linkedProfile.isEmpty() && m_profileManager->isProfileLocked(m_event.linkedProfile)) {
        QMessageBox::warning(this, "Plan zablokowany", "Ten blok jest zablokowany. Nie można go usunąć.");
        return;
    }

    m_deleteRequested = true;
    accept();
}

// 3. Zadanie usuniecia pojedynczego zdarzenia
void EventEditorDialog::onDeleteClicked()
{
    if (m_profileManager && !m_event.linkedProfile.isEmpty() && m_profileManager->isProfileLocked(m_event.linkedProfile)) {
        QMessageBox::warning(this, "Plan zablokowany", "Ten blok jest powiązany ze zablokowanym profilem i nie może zostać usunięty.");
        return;
    }

    m_deleteRequested = true;
    accept();
}

// 4. Zatwierdzenie danych z zapisaniem powiazania przedmiotu
void EventEditorDialog::onSaveClicked()
{
    if (m_profileManager && !m_event.linkedProfile.isEmpty() && m_profileManager->isProfileLocked(m_event.linkedProfile)) {
        QMessageBox::warning(this, "Plan zablokowany", "Ten blok jest zablokowany i nie można go modyfikować.");
        return;
    }

    m_event.title = m_titleEdit->text().trimmed();
    if (m_event.title.isEmpty()) m_event.title = "Nowe wydarzenie";

    m_event.category = m_categoryBox->currentText();
    m_event.linkedProfile = m_linkedProfileBox->currentData().toString();
    m_event.linkedSubject = m_linkedSubjectBox->currentData().toString();
    m_event.start = QDateTime(m_dateEdit->date(), m_startEdit->time());
    m_event.end = QDateTime(m_dateEdit->date(), m_endEdit->time());

    if (m_event.end <= m_event.start) {
        m_event.end = m_event.start.addSecs(3600);
    }
    accept();
}

// 5. Budowa ukladu graficznego
void EventEditorDialog::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);

    bool isLocked = (m_profileManager && !m_event.linkedProfile.isEmpty() && m_profileManager->isProfileLocked(m_event.linkedProfile));

    QLabel *hdr = new QLabel(m_isEditMode ? "EDYCJA WYDARZENIA" : "NOWE WYDARZENIE", this);
    hdr->setObjectName("dialogHeader");
    layout->addWidget(hdr);

    if (isLocked) {
        QLabel *lockBanner = new QLabel("🔒 TEN BLOK JEST ZABLOKOWANY (Trwa sesja nauki).", this);
        lockBanner->setObjectName("lockWarningBanner");
        layout->addWidget(lockBanner);
    }

    layout->addWidget(new QLabel("Nazwa wydarzenia:", this));
    m_titleEdit = new QLineEdit(m_event.title, this);
    m_titleEdit->setReadOnly(isLocked);
    layout->addWidget(m_titleEdit);

    QHBoxLayout *dtRow = new QHBoxLayout();
    m_dateEdit = new QDateEdit(m_event.start.date(), this);
    m_dateEdit->setCalendarPopup(true);
    m_dateEdit->setEnabled(!isLocked);

    m_startEdit = new QTimeEdit(m_event.start.time(), this);
    m_startEdit->setEnabled(!isLocked);

    m_endEdit = new QTimeEdit(m_event.end.time(), this);
    m_endEdit->setEnabled(!isLocked);

    dtRow->addWidget(new QLabel("Data:", this));
    dtRow->addWidget(m_dateEdit);
    dtRow->addWidget(new QLabel("Od:", this));
    dtRow->addWidget(m_startEdit);
    dtRow->addWidget(new QLabel("Do:", this));
    dtRow->addWidget(m_endEdit);
    layout->addLayout(dtRow);

    QHBoxLayout *catProfRow = new QHBoxLayout();
    m_categoryBox = new QComboBox(this);
    m_categoryBox->addItems({"Profile blokowania", "Automatyczne", "Event"});
    m_categoryBox->setCurrentText(m_event.category);
    m_categoryBox->setEnabled(!isLocked);
    catProfRow->addWidget(new QLabel("Kategoria:", this));
    catProfRow->addWidget(m_categoryBox);

    m_linkedProfileBox = new QComboBox(this);
    m_linkedProfileBox->addItem("[Brak powiązania]", "");
    if (m_profileManager) {
        const auto &profiles = m_profileManager->getAllProfiles();
        for (auto it = profiles.begin(); it != profiles.end(); ++it) {
            m_linkedProfileBox->addItem("🔒 " + it.key(), it.key());
        }
    }
    int profIdx = m_linkedProfileBox->findData(m_event.linkedProfile);
    if (profIdx >= 0) m_linkedProfileBox->setCurrentIndex(profIdx);
    m_linkedProfileBox->setEnabled(!isLocked);

    catProfRow->addWidget(new QLabel("Połącz z profilem:", this));
    catProfRow->addWidget(m_linkedProfileBox);
    layout->addLayout(catProfRow);

    // Nowe pole: bezposrednie powiazanie wydarzenia z nauka przedmiotu
    QHBoxLayout *subRow = new QHBoxLayout();
    m_linkedSubjectBox = new QComboBox(this);
    m_linkedSubjectBox->addItem("[Brak powiązania z przedmiotem]", "");

    QSqlQuery qSub = DatabaseManager::instance().executeQuery("SELECT name FROM subjects ORDER BY name ASC;");
    while (qSub.next()) {
        QString sName = qSub.value(0).toString();
        m_linkedSubjectBox->addItem("📚 " + sName, sName);
    }
    int subIdx = m_linkedSubjectBox->findData(m_event.linkedSubject);
    if (subIdx >= 0) m_linkedSubjectBox->setCurrentIndex(subIdx);
    m_linkedSubjectBox->setEnabled(!isLocked);

    subRow->addWidget(new QLabel("Połącz z nauką przedmiotu:", this));
    subRow->addWidget(m_linkedSubjectBox);
    layout->addLayout(subRow);

    if (!m_isEditMode) {
        QHBoxLayout *repRow = new QHBoxLayout();
        m_repeatBox = new QComboBox(this);
        m_repeatBox->addItem("Nie powtarzaj", 0);
        m_repeatBox->addItem("Powtarzaj co 7 dni (co tydzień)", 1);
        repRow->addWidget(new QLabel("Powtarzalność:", this));
        repRow->addWidget(m_repeatBox);

        m_occurrencesSpin = new QSpinBox(this);
        m_occurrencesSpin->setRange(2, 52);
        m_occurrencesSpin->setValue(8);
        m_occurrencesSpin->setSuffix(" razy");
        repRow->addWidget(new QLabel("Ile razy:", this));
        repRow->addWidget(m_occurrencesSpin);
        layout->addLayout(repRow);
    }

    QHBoxLayout *actionRow = new QHBoxLayout();
    if (m_isEditMode) {
        QPushButton *delBtn = new QPushButton("Usuń", this);
        delBtn->setObjectName("giveUpBtn");
        delBtn->setEnabled(!isLocked);
        actionRow->addWidget(delBtn);
        connect(delBtn, &QPushButton::clicked, this, &EventEditorDialog::onDeleteClicked);

        QPushButton *debugDelBtn = new QPushButton("Usuń [DEBUG FORCE]", this);
        debugDelBtn->setObjectName("giveUpBtn");
        debugDelBtn->setEnabled(!isLocked);
        actionRow->addWidget(debugDelBtn);
        connect(debugDelBtn, &QPushButton::clicked, this, &EventEditorDialog::onDebugForceDeleteClicked);

        if (!m_event.seriesId.isEmpty()) {
            QPushButton *delSeriesBtn = new QPushButton("Usuń całą serię", this);
            delSeriesBtn->setObjectName("giveUpBtn");
            delSeriesBtn->setEnabled(!isLocked);
            actionRow->addWidget(delSeriesBtn);
            connect(delSeriesBtn, &QPushButton::clicked, this, &EventEditorDialog::onDeleteSeriesClicked);
        }
    }

    actionRow->addStretch();
    QPushButton *cancelBtn = new QPushButton("Anuluj", this);
    QPushButton *saveBtn = new QPushButton("Zapisz", this);
    saveBtn->setObjectName("primaryGreenBtn");
    saveBtn->setEnabled(!isLocked);
    actionRow->addWidget(cancelBtn);
    actionRow->addWidget(saveBtn);
    layout->addLayout(actionRow);

    connect(m_titleEdit, &QLineEdit::returnPressed, this, &EventEditorDialog::onSaveClicked);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &EventEditorDialog::onSaveClicked);
}

// 6. Gettery
CalendarEvent EventEditorDialog::getEventData() const { return m_event; }
bool EventEditorDialog::isDeleteRequested() const { return m_deleteRequested; }
bool EventEditorDialog::isDeleteEntireSeriesRequested() const { return m_deleteSeriesRequested; }
bool EventEditorDialog::isRecurrenceEnabled() const { return m_repeatBox && m_repeatBox->currentIndex() == 1; }
int EventEditorDialog::getRecurrenceWeeksCount() const { return m_occurrencesSpin ? m_occurrencesSpin->value() : 1; }

// 7. Konstruktor
EventEditorDialog::EventEditorDialog(const CalendarEvent &event, bool isEditMode,
                                     ProfileManager *profileManager, QWidget *parent)
    : QDialog(parent)
    , m_event(event)
    , m_isEditMode(isEditMode)
    , m_deleteRequested(false)
    , m_deleteSeriesRequested(false)
    , m_profileManager(profileManager)
    , m_repeatBox(nullptr)
    , m_occurrencesSpin(nullptr)
{
    setWindowTitle(isEditMode ? "Edycja wydarzenia" : "Dodaj wydarzenie do kalendarza");
    resize(580, 420);
    setupUi();
}
