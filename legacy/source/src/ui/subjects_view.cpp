// Plik implementacji widoku SubjectsView.
// Sumuje wylacznie autorskie bloki nauki uzytkownika i poprawnie przydziela 100% czasu,
// gdy blok dotyczy jednego przedmiotu (nawet jesli w nazwie laczy kilka tygodni T1+T2).
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "subjects_view.h"
#include "database_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QRegularExpression>

// 1. Precyzyjne wyliczanie czasu nauki uzytkownika dla danego przedmiotu
int SubjectsView::calculateTotalStudyMinutes(const QString &subjectName) const
{
    if (!m_calendarEngine) return 0;
    int totalMin = 0;
    QString cleanSub = subjectName.trimmed().toLower();

    for (const auto &ev : m_calendarEngine->getEvents()) {
        // A. Wykluczamy importowane zajecia uczelniane
        if (ev.category == "Automatyczne") continue;
        if (ev.id.startsWith("sub_", Qt::CaseInsensitive)) continue;
        if (ev.id.startsWith("usos_", Qt::CaseInsensitive)) continue;
        if (ev.id.startsWith("exam_", Qt::CaseInsensitive)) continue;

        qint64 secs = ev.start.secsTo(ev.end);
        if (secs <= 0) continue;
        int eventDurationMin = static_cast<int>(secs / 60);

        // B. Pierwszenstwo: bezposrednie powiazanie przez linkedSubject
        if (!ev.linkedSubject.isEmpty()) {
            if (ev.linkedSubject.trimmed().toLower() == cleanSub) {
                totalMin += eventDurationMin;
            }
            continue;
        }

        // C. Weryfikacja czy to blok nauki
        bool isStudyBlock = (ev.linkedProfile == "overdue") ||
                            ev.id.startsWith("ics_", Qt::CaseInsensitive) ||
                            ev.id.startsWith("bundle_overdue_", Qt::CaseInsensitive) ||
                            ev.id.startsWith("auto_overdue_", Qt::CaseInsensitive) ||
                            ev.title.contains(QRegularExpression("T\\d+\\(.*?\\)"));

        if (!isStudyBlock) continue;

        QString lowerTitle = ev.title.toLower();
        if (!lowerTitle.contains(cleanSub)) continue;

        // D. Zliczanie unikalnych przedmiotow w tytule z bazy
        int distinctSubjectsInEvent = 0;
        bool matchesThisSubject = false;

        for (const auto &sub : m_subjectManager->getSubjects()) {
            QString sName = sub.name.trimmed().toLower();
            if (lowerTitle.contains(sName)) {
                distinctSubjectsInEvent++;
                if (sName == cleanSub) {
                    matchesThisSubject = true;
                }
            }
        }

        if (matchesThisSubject) {
            int divisor = std::max(1, distinctSubjectsInEvent);
            totalMin += (eventDurationMin / divisor);
        }
    }

    return totalMin;
}

// 2. Zmiana notatek dla aktualnie wybranego przedmiotu
void SubjectsView::onNotesTextChanged()
{
    if (m_isUpdating || m_isUpdatingNotes) return;
    int row = m_subjectsTable->currentRow();
    if (row < 0 || row >= m_subjectManager->getSubjects().size()) return;

    QString subjectName = m_subjectsTable->item(row, 0)->data(Qt::UserRole).toString();
    m_subjectManager->updateSubjectNotes(subjectName, m_notesEdit->toPlainText());
}

// 3. Zdarzenie edycji nazwy przedmiotu w gornej tabeli
void SubjectsView::onSubjectCellChanged(int row, int column)
{
    if (m_isUpdating) return;

    if (column == 0) {
        QString newName = m_subjectsTable->item(row, column)->text().trimmed();
        QString oldName = m_subjectsTable->item(row, column)->data(Qt::UserRole).toString();

        if (newName != oldName && !newName.isEmpty()) {
            m_subjectManager->renameSubject(oldName, newName);
        } else {
            refreshSubjectsList();
        }
    }
}

// 4. Zdarzenie edycji poszczegolnych zajec w dolnej tabeli
void SubjectsView::onClassCellChanged(int row, int column)
{
    if (m_isUpdating) return;

    int subRow = m_subjectsTable->currentRow();
    if (subRow < 0) return;

    QString subjectName = m_subjectsTable->item(subRow, 0)->data(Qt::UserRole).toString();
    QString classId = m_classesTable->item(row, 0)->data(Qt::UserRole).toString();

    QString newLabel = m_classesTable->item(row, 2)->text().trimmed();
    QString startStr = m_classesTable->item(row, 3)->text().trimmed();
    QString endStr = m_classesTable->item(row, 4)->text().trimmed();

    QDateTime start = QDateTime::fromString(startStr, "yyyy-MM-dd HH:mm");
    QDateTime end = QDateTime::fromString(endStr, "yyyy-MM-dd HH:mm");

    if (!start.isValid() || !end.isValid()) {
        QMessageBox::warning(this, "Błąd formatu", "Data musi być podana w formacie:\nyyyy-MM-dd HH:mm\n\n(np. 2026-11-23 11:15)");
        refreshClassesList();
        return;
    }

    m_subjectManager->updateClassDetails(subjectName, classId, newLabel, start, end);
}

// 5. Usuniecie zaznaczonych zajec z przedmiotu
void SubjectsView::onDeleteSelectedClassClicked()
{
    int subRow = m_subjectsTable->currentRow();
    int clsRow = m_classesTable->currentRow();

    if (subRow < 0 || subRow >= m_subjectManager->getSubjects().size()) {
        QMessageBox::information(this, "Wybór", "Wybierz najpierw przedmiot w górnej tabeli.");
        return;
    }

    const auto &sub = m_subjectManager->getSubjects()[subRow];
    if (clsRow < 0 || clsRow >= sub.classes.size()) {
        QMessageBox::information(this, "Wybór", "Wybierz zajęcia w tabeli do usunięcia.");
        return;
    }

    const auto &clsItem = sub.classes[clsRow];
    auto res = QMessageBox::question(this, "Usuń zajęcia",
                                     QString("Czy na pewno chcesz usunąć '%1' (%2) z przedmiotu '%3'?")
                                         .arg(clsItem.label, clsItem.start.toString("yyyy-MM-dd HH:mm"), sub.name));
    if (res == QMessageBox::Yes) {
        m_subjectManager->removeClassFromSubject(sub.name, clsItem.id);
        refreshSubjectsList();
    }
}

// 6. Usuniecie calego przedmiotu
void SubjectsView::onDeleteSubjectClicked()
{
    int row = m_subjectsTable->currentRow();
    if (row < 0 || row >= m_subjectManager->getSubjects().size()) {
        QMessageBox::information(this, "Wybór", "Zaznacz przedmiot do usunięcia z listy.");
        return;
    }

    QString name = m_subjectManager->getSubjects()[row].name;
    auto res = QMessageBox::question(this, "Usuń przedmiot",
                                     QString("Czy na pewno chcesz usunąć przedmiot '%1' oraz wszystkie jego terminy?").arg(name));
    if (res == QMessageBox::Yes) {
        m_subjectManager->removeSubject(name);
        refreshSubjectsList();
    }
}

// 7. Wklejenie tresci JSON
void SubjectsView::onPasteJsonClicked()
{
    bool ok = false;
    QString jsonText = QInputDialog::getMultiLineText(this, "Wklej JSON planu",
                                                     "Wklej kod JSON ze strukturą przedmiotów i terminów:", "", &ok);
    if (!ok || jsonText.trimmed().isEmpty()) return;

    QString error;
    if (m_subjectManager->importFromJsonString(jsonText, error)) {
        QMessageBox::information(this, "Sukces", "Zaimportowano plan!");
        refreshSubjectsList();
    } else {
        QMessageBox::critical(this, "Błąd importu", error);
    }
}

// 8. Import pliku JSON z dysku
void SubjectsView::onImportJsonFileClicked()
{
    QString path = QFileDialog::getOpenFileName(this, "Wybierz plik JSON z planem", "", "Pliki JSON (*.json);;Wszystkie pliki (*.*)");
    if (path.isEmpty()) return;

    QString error;
    if (m_subjectManager->importFromJsonFile(path, error)) {
        QMessageBox::information(this, "Sukces", "Zaimportowano przedmioty z pliku JSON!");
        refreshSubjectsList();
    } else {
        QMessageBox::critical(this, "Błąd importu", error);
    }
}

// 9. Odswiezenie dolnej tabeli terminow oraz panelu notatek
void SubjectsView::refreshClassesList()
{
    int row = m_subjectsTable->currentRow();
    if (row < 0 || row >= m_subjectManager->getSubjects().size()) {
        m_isUpdating = true;
        m_classesTable->setRowCount(0);
        m_classesHeaderLabel->setText("Terminy zajęć: (zaznacz przedmiot powyżej)");

        m_isUpdatingNotes = true;
        m_notesEdit->clear();
        m_notesEdit->setEnabled(false);
        m_notesHeaderLabel->setText("Notatki i linki: (zaznacz przedmiot powyżej)");
        m_isUpdatingNotes = false;

        m_isUpdating = false;
        return;
    }

    const auto &sub = m_subjectManager->getSubjects()[row];
    int totalMin = calculateTotalStudyMinutes(sub.name);
    QString totalTimeFormatted = QString("%1h %2m").arg(totalMin / 60).arg(totalMin % 60);

    m_classesHeaderLabel->setText(QString("Terminy zajęć: %1").arg(sub.name));
    m_notesHeaderLabel->setText(QString("Materiały: %1 (Spędzony czas nauki: %2)").arg(sub.name, totalTimeFormatted));

    m_isUpdatingNotes = true;
    m_notesEdit->setEnabled(true);
    m_notesEdit->setPlainText(sub.notes);
    m_isUpdatingNotes = false;

    m_isUpdating = true;
    m_classesTable->setRowCount(sub.classes.size());
    for (int i = 0; i < sub.classes.size(); ++i) {
        const auto &c = sub.classes[i];

        QTableWidgetItem *typeItem = new QTableWidgetItem(c.type.toUpper());
        typeItem->setData(Qt::UserRole, c.id);
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        m_classesTable->setItem(i, 0, typeItem);

        QTableWidgetItem *idxItem = new QTableWidgetItem(QString::number(c.index));
        idxItem->setFlags(idxItem->flags() & ~Qt::ItemIsEditable);
        m_classesTable->setItem(i, 1, idxItem);

        m_classesTable->setItem(i, 2, new QTableWidgetItem(c.label));
        m_classesTable->setItem(i, 3, new QTableWidgetItem(c.start.toString("yyyy-MM-dd HH:mm")));
        m_classesTable->setItem(i, 4, new QTableWidgetItem(c.end.toString("yyyy-MM-dd HH:mm")));
    }
    m_isUpdating = false;
}

// 10. Zmiana zaznaczenia w tabeli przedmiotow
void SubjectsView::onSubjectSelectionChanged()
{
    refreshClassesList();
}

// 11. Odswiezenie tabeli przedmiotow z prawidlowym licznikiem nauki
void SubjectsView::refreshSubjectsList()
{
    const auto &subjects = m_subjectManager->getSubjects();

    m_isUpdating = true;
    m_subjectsTable->setRowCount(subjects.size());

    for (int r = 0; r < subjects.size(); ++r) {
        const auto &s = subjects[r];

        QTableWidgetItem *nameItem = new QTableWidgetItem(s.name);
        nameItem->setData(Qt::UserRole, s.name);
        m_subjectsTable->setItem(r, 0, nameItem);

        QTableWidgetItem *typeItem = new QTableWidgetItem(s.isLecture ? "Wykład" : "Lab / Ćwiczenia");
        typeItem->setFlags(typeItem->flags() & ~Qt::ItemIsEditable);
        m_subjectsTable->setItem(r, 1, typeItem);

        QTableWidgetItem *countItem = new QTableWidgetItem(QString::number(s.classes.size()));
        countItem->setFlags(countItem->flags() & ~Qt::ItemIsEditable);
        m_subjectsTable->setItem(r, 2, countItem);

        int totalMin = calculateTotalStudyMinutes(s.name);
        QString timeTxt = QString("%1h %2m").arg(totalMin / 60).arg(totalMin % 60);
        QTableWidgetItem *timeItem = new QTableWidgetItem(timeTxt);
        timeItem->setTextAlignment(Qt::AlignCenter);
        timeItem->setFlags(timeItem->flags() & ~Qt::ItemIsEditable);
        m_subjectsTable->setItem(r, 3, timeItem);

        QString nextDateStr = "-";
        for (const auto &c : s.classes) {
            if (c.start >= QDateTime::currentDateTime()) {
                nextDateStr = c.start.toString("yyyy-MM-dd HH:mm");
                break;
            }
        }
        QTableWidgetItem *nextItem = new QTableWidgetItem(nextDateStr);
        nextItem->setFlags(nextItem->flags() & ~Qt::ItemIsEditable);
        m_subjectsTable->setItem(r, 4, nextItem);
    }
    m_isUpdating = false;

    m_statusLabel->setText(QString("Liczba zarejestrowanych przedmiotów: %1").arg(subjects.size()));
    m_dbPathLabel->setText(QString("Plik bazy SQLite: %1").arg(DatabaseManager::instance().getDatabaseFilePath()));
    refreshClassesList();
}

// 12. Budowa interfejsu
void SubjectsView::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(10);

    QLabel *hdr = new QLabel("PRZEDMIOTY I PLAN ZAJĘĆ (SUBJECTS HUB)", this);
    hdr->setObjectName("viewHeaderTitle");
    mainLayout->addWidget(hdr);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("blockSubtitle");
    mainLayout->addWidget(m_statusLabel);

    m_dbPathLabel = new QLabel(this);
    m_dbPathLabel->setObjectName("periodHeaderLabel");
    mainLayout->addWidget(m_dbPathLabel);

    m_subjectsTable = new QTableWidget(this);
    m_subjectsTable->setColumnCount(5);
    QStringList headers;
    headers << "Przedmiot" << "Typ główny" << "Liczba terminów" << "Łączny czas nauki" << "Najbliższe zajęcia";
    m_subjectsTable->setHorizontalHeaderLabels(headers);
    m_subjectsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_subjectsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_subjectsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_subjectsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_subjectsTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_subjectsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_subjectsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_subjectsTable, 1);

    QHBoxLayout *bottomSplitLayout = new QHBoxLayout();
    bottomSplitLayout->setSpacing(12);

    QVBoxLayout *classesCol = new QVBoxLayout();
    m_classesHeaderLabel = new QLabel("Terminy zajęć dla wybranego przedmiotu:", this);
    m_classesHeaderLabel->setObjectName("viewSectionTitle");
    classesCol->addWidget(m_classesHeaderLabel);

    m_classesTable = new QTableWidget(this);
    m_classesTable->setColumnCount(5);
    QStringList clsHeaders;
    clsHeaders << "Typ" << "Indeks" << "Nazwa / Temat" << "Rozpoczęcie" << "Zakończenie";
    m_classesTable->setHorizontalHeaderLabels(clsHeaders);
    m_classesTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_classesTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_classesTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_classesTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_classesTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_classesTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_classesTable->setSelectionMode(QAbstractItemView::SingleSelection);
    classesCol->addWidget(m_classesTable);
    bottomSplitLayout->addLayout(classesCol, 3);

    QVBoxLayout *notesCol = new QVBoxLayout();
    m_notesHeaderLabel = new QLabel("Notatki, materiały i linki:", this);
    m_notesHeaderLabel->setObjectName("viewSectionTitle");
    notesCol->addWidget(m_notesHeaderLabel);

    m_notesEdit = new QTextEdit(this);
    m_notesEdit->setPlaceholderText("Wpisz tutaj notatki, linki do materiałów (Google Drive, USOS, repozytoria), wzory i zagadnienia dla wybranego przedmiotu...");
    notesCol->addWidget(m_notesEdit);
    bottomSplitLayout->addLayout(notesCol, 2);

    mainLayout->addLayout(bottomSplitLayout, 2);

    QHBoxLayout *btnRow = new QHBoxLayout();
    m_importFileBtn = new QPushButton("📂 Importuj plan z .json", this);
    m_importFileBtn->setObjectName("primaryGreenBtn");
    m_pasteJsonBtn = new QPushButton("📋 Wklej kod JSON", this);
    m_deleteClassBtn = new QPushButton("❌ Usuń zaznaczone zajęcia z przedmiotu", this);
    m_deleteClassBtn->setObjectName("giveUpBtn");
    m_deleteSubjectBtn = new QPushButton("🗑 Usuń cały przedmiot", this);

    btnRow->addWidget(m_importFileBtn);
    btnRow->addWidget(m_pasteJsonBtn);
    btnRow->addWidget(m_deleteClassBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_deleteSubjectBtn);
    mainLayout->addLayout(btnRow);

    connect(m_importFileBtn, &QPushButton::clicked, this, &SubjectsView::onImportJsonFileClicked);
    connect(m_pasteJsonBtn, &QPushButton::clicked, this, &SubjectsView::onPasteJsonClicked);
    connect(m_deleteClassBtn, &QPushButton::clicked, this, &SubjectsView::onDeleteSelectedClassClicked);
    connect(m_deleteSubjectBtn, &QPushButton::clicked, this, &SubjectsView::onDeleteSubjectClicked);

    connect(m_subjectsTable, &QTableWidget::itemSelectionChanged, this, &SubjectsView::onSubjectSelectionChanged);
    connect(m_subjectsTable, &QTableWidget::cellChanged, this, &SubjectsView::onSubjectCellChanged);
    connect(m_classesTable, &QTableWidget::cellChanged, this, &SubjectsView::onClassCellChanged);
    connect(m_notesEdit, &QTextEdit::textChanged, this, &SubjectsView::onNotesTextChanged);

    connect(m_subjectManager, &SubjectManager::subjectsUpdated, this, &SubjectsView::refreshSubjectsList);
    if (m_calendarEngine) {
        connect(m_calendarEngine, &CalendarEngine::eventsUpdated, this, &SubjectsView::refreshSubjectsList);
    }

    refreshSubjectsList();
}

// 13. Konstruktor SubjectsView
SubjectsView::SubjectsView(SubjectManager *subjectManager, CalendarEngine *calendarEngine, QWidget *parent)
    : QWidget(parent)
    , m_subjectManager(subjectManager)
    , m_calendarEngine(calendarEngine)
    , m_isUpdating(false)
    , m_isUpdatingNotes(false)
    , m_subjectsTable(nullptr)
    , m_classesTable(nullptr)
    , m_notesEdit(nullptr)
    , m_statusLabel(nullptr)
    , m_classesHeaderLabel(nullptr)
    , m_notesHeaderLabel(nullptr)
    , m_dbPathLabel(nullptr)
    , m_importFileBtn(nullptr)
    , m_pasteJsonBtn(nullptr)
    , m_deleteSubjectBtn(nullptr)
    , m_deleteClassBtn(nullptr)
{
    setupUi();
}
