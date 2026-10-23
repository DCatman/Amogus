// Plik implementacji widoku OverdueTrackerWidget oraz VisualOverduePlannerDialog.
// Tworzy zwięzłe nazwy zadan oraz automatycznie przypisuje linkedSubject, dzieki czemu czas trafia do statystyk.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "overdue_tracker_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QHeaderView>
#include <QMessageBox>
#include <QRegularExpression>
#include <algorithm>

// 1. Czyszczenie nazwy przedmiotu ze znacznikow [Wykład], [Ćwiczenia], [Laboratorium]
static QString cleanSubjectName(const QString &displayName)
{
    QString s = displayName;
    s.remove(QRegularExpression("\\[.*?\\]"));
    return s.trimmed();
}

// 2. Zliczanie wystapien danej sesji w kalendarzu
static int countSessionsInCalendar(CalendarEngine *engine, const QString &displayName, int weekNum, int stageNum, bool isExam)
{
    if (!engine) return 0;
    int count = 0;
    QString targetWeek = QString("T%1").arg(weekNum);
    QString stageStr = QString::number(stageNum);

    for (const auto &ev : engine->getEvents()) {
        if (ev.title.contains(cleanSubjectName(displayName)) && ev.title.contains(targetWeek)) {
            if (isExam && (ev.title.contains("KOLO") || ev.title.contains("KOLOKWIUM") || ev.title.contains("Kolo"))) {
                count++;
            } else if (!isExam && (ev.title.contains(QString("(%1)").arg(stageStr)) || ev.title.contains(QString("(+%1)").arg(stageStr)))) {
                count++;
            }
        }
    }
    return count;
}

// 3. Formatowanie indeksu gornego dla wielokrotnych sesji
static QString toSuperscript(int count)
{
    if (count <= 0) return QString();
    if (count == 1) return QString::fromUtf8("¹");
    if (count == 2) return QString::fromUtf8("²");
    if (count == 3) return QString::fromUtf8("³");
    if (count == 4) return QString::fromUtf8("⁴");
    if (count == 5) return QString::fromUtf8("⁵");
    return QString("(%1)").arg(count);
}

// 4. Formatowanie kafelka tygodnia w glownym widoku trackera
static QTableWidgetItem *createClassCellItem(const ClassProgressCell &cell, const QString &trackType)
{
    if (!cell.hasClass) {
        QTableWidgetItem *emptyItem = new QTableWidgetItem("-");
        emptyItem->setTextAlignment(Qt::AlignCenter);
        emptyItem->setForeground(QColor(60, 60, 60));
        return emptyItem;
    }

    QString text;
    QColor textColor(255, 255, 255);
    QColor bgColor(Qt::transparent);

    if (cell.isExam) {
        bgColor = QColor(213, 0, 0, 180);
    }

    if (cell.status == ClassCellStatus::Completed) {
        text = "✓";
        textColor = QColor(255, 255, 255);
    } else if (cell.status == ClassCellStatus::ExamPassed) {
        text = "✔";
        textColor = QColor(52, 199, 89);
    } else if (cell.status == ClassCellStatus::ExamFailed) {
        text = "✔";
        textColor = QColor(255, 59, 48);
    } else if (cell.isExam) {
        text = "KOLO";
        textColor = QColor(255, 255, 255);
    } else if (cell.status == ClassCellStatus::StageActive) {
        text = (trackType == "lab") ? QString("%1").arg(cell.stageNumber) : QString("+%1").arg(cell.stageNumber);
        textColor = QColor(255, 214, 10);
    } else {
        text = "▢";
        textColor = QColor(100, 100, 100);
    }

    QTableWidgetItem *item = new QTableWidgetItem(text);
    item->setTextAlignment(Qt::AlignCenter);
    item->setForeground(textColor);
    if (bgColor != Qt::transparent) {
        item->setBackground(bgColor);
    }
    return item;
}

// 5. Klikniecie mikro-kafelka etapu ICS
void VisualOverduePlannerDialog::onStageButtonClicked(const ArmedStageItem &item, bool checked)
{
    if (checked) {
        if (!m_selectedStages.contains(item)) {
            m_selectedStages.append(item);
        }
    } else {
        m_selectedStages.removeAll(item);
    }
    updateSelectionSummary();
}

// 6. Aktualizacja etykiety wyboru
void VisualOverduePlannerDialog::updateSelectionSummary()
{
    if (m_selectedStages.isEmpty()) {
        m_selectionInfoLabel->setText("Kliknij kwadracik (np. 0) w tabeli po prawej, a następnie przeciągnij po siatce kalendarza.");
        return;
    }

    QMap<QString, QStringList> grouped;
    for (const auto &item : m_selectedStages) {
        QString sub = cleanSubjectName(item.displayName);
        QString stageStr = item.isExam ? "KOLO" : QString::number(item.stage);
        grouped[sub].append(QString("T%1(%2)").arg(item.weekNumber).arg(stageStr));
    }

    QStringList parts;
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        parts.append(QString("%1 %2").arg(it.key(), it.value().join("+")));
    }

    m_selectionInfoLabel->setText(QString("UZBROJONO: %1. Przeciągnij na kalendarzu, aby utworzyć blok!").arg(parts.join(" + ")));
}

// 7. Reset biezacego wyboru
void VisualOverduePlannerDialog::onClearSelectionClicked()
{
    m_selectedStages.clear();
    refreshPlannerMatrix();
    updateSelectionSummary();
}

// 8. Tworzenie kontenera z mikro-kafelkami etapow dla komorki (Przedmiot, Tydzien)
QWidget *VisualOverduePlannerDialog::createStageCellWidget(const SubjectTrackData &track, int weekNum, const ClassProgressCell &cell)
{
    if (!cell.hasClass) return nullptr;

    QWidget *container = new QWidget();
    QHBoxLayout *layout = new QHBoxLayout(container);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(3);
    layout->setAlignment(Qt::AlignCenter);

    // Przycisk K pojawia sie wylacznie w tygodniach z kolokwium
    if (cell.isExam) {
        ArmedStageItem examItem{ track.displayName, track.subjectName, track.trackType, weekNum, 0, true };
        int examCount = countSessionsInCalendar(m_calendarEngine, track.displayName, weekNum, 0, true);
        bool isSelected = m_selectedStages.contains(examItem);

        QString examText = (examCount > 0) ? "✓" : "K";
        if (isSelected) examText = QString("[%1]").arg(examText);

        QPushButton *btnExam = new QPushButton(examText, container);
        btnExam->setObjectName(examCount > 0 ? "stageBtnExamScheduled" : "stageBtnExam");
        btnExam->setCheckable(true);
        btnExam->setChecked(isSelected);
        btnExam->setFixedSize(24, 24);

        QString hoverExam = QString("%1 T%2 [KOLOKWIUM%3]\nZaplanowano: %4 sesji przygotowania (kliknij, aby zaplanować kolejną).")
                                .arg(cleanSubjectName(track.displayName)).arg(weekNum).arg(toSuperscript(examCount)).arg(examCount);
        btnExam->setToolTip(hoverExam);

        connect(btnExam, &QPushButton::toggled, this, [this, examItem, btnExam, examCount](bool c) {
            QString base = (examCount > 0) ? "✓" : "K";
            btnExam->setText(c ? QString("[%1]").arg(base) : base);
            onStageButtonClicked(examItem, c);
        });

        layout->addWidget(btnExam);
    }

    QList<int> stages;
    if (track.trackType == "lab") {
        stages = { -6, -3, -2, -1 };
    } else {
        stages = { 0, 1, 3, 6, 12 };
    }

    bool prevCompletedOrScheduled = true;

    for (int s : stages) {
        ArmedStageItem item{ track.displayName, track.subjectName, track.trackType, weekNum, s, false };
        int count = countSessionsInCalendar(m_calendarEngine, track.displayName, weekNum, s, false);
        bool isSelected = m_selectedStages.contains(item);

        bool isUnlocked = prevCompletedOrScheduled || (count > 0);

        QString baseText = (count > 0) ? "✓" : QString::number(s);
        QString displayText = isSelected ? QString("[%1]").arg(baseText) : baseText;

        QPushButton *btn = new QPushButton(displayText, container);
        btn->setObjectName(count > 0 ? "stageBtnScheduled" : (isUnlocked ? "stageBtnUnlocked" : "stageBtnLocked"));
        btn->setCheckable(true);
        btn->setEnabled(isUnlocked);
        btn->setChecked(isSelected);
        btn->setFixedSize(s < 0 ? 26 : 22, 24);

        QString stageName = (s >= 0) ? QString("+%1").arg(s) : QString::number(s);
        QString stageDesc = (s == 0) ? "Zrozumienie materiału (sesja wstępna)" :
                            (s > 0 ? QString("Powtórka po czasie") : QString("Przygotowanie przed zajęciami"));

        QString hoverText = QString("%1 T%2 (%3%4) - %5\nZaplanowano już: %6 sesji (kliknij, aby zaplanować kolejną).")
                                .arg(cleanSubjectName(track.displayName)).arg(weekNum).arg(stageName).arg(toSuperscript(count)).arg(stageDesc).arg(count);
        btn->setToolTip(hoverText);

        connect(btn, &QPushButton::toggled, this, [this, item, btn, count, s](bool c) {
            QString base = (count > 0) ? "✓" : QString::number(s);
            btn->setText(c ? QString("[%1]").arg(base) : base);
            onStageButtonClicked(item, c);
        });

        layout->addWidget(btn);
        prevCompletedOrScheduled = (count > 0);
    }

    return container;
}

// 9. Przeciagniecie na kalendarzu z automatycznym linkowaniem linkedSubject
void VisualOverduePlannerDialog::onCanvasDrawnBlock(const QDateTime &start, const QDateTime &end)
{
    if (m_selectedStages.isEmpty()) {
        QMessageBox::information(this, "Wybór etapu", "Zaznacz najpierw przynajmniej jeden kafelek (np. 0) w tabeli po prawej, a następnie przeciągnij na kalendarzu!");
        return;
    }

    QMessageBox msgBox(this);
    msgBox.setWindowTitle("Potwierdzenie planowania");
    msgBox.setText("Czy chcesz zablokować komputer na czas trwania tej sesji?");
    QPushButton *btnBlock = msgBox.addButton("TAK (Zablokuj komputer)", QMessageBox::YesRole);
    QPushButton *btnNormal = msgBox.addButton("NIE (Zwykłe wydarzenie)", QMessageBox::NoRole);
    msgBox.exec();

    bool shouldBlock = (msgBox.clickedButton() == btnBlock);

    QMap<QString, QStringList> grouped;
    bool hasExam = false;

    for (const auto &item : m_selectedStages) {
        QString sub = cleanSubjectName(item.displayName);
        QString stageStr = item.isExam ? "KOLO" : QString::number(item.stage);
        grouped[sub].append(QString("T%1(%2)").arg(item.weekNumber).arg(stageStr));
        if (item.isExam) hasExam = true;

        m_tracker->updateCellStage(item.displayName, item.weekNumber, item.stage);
    }

    QStringList groups;
    for (auto it = grouped.begin(); it != grouped.end(); ++it) {
        groups.append(QString("%1 %2").arg(it.key(), it.value().join("+")));
    }
    QString bundleTitle = groups.join(" + ");

    CalendarEvent ev;
    ev.id = QString("ics_%1_%2").arg(start.date().toString("yyyyMMdd")).arg(start.time().toString("hhmm"));
    ev.title = bundleTitle;
    ev.category = shouldBlock ? "Profile blokowania" : "Event";
    ev.linkedProfile = shouldBlock ? "overdue" : "";
    
    // Jesli blok dotyczy tylko jednego przedmiotu, przypisujemy go bezposrednio do linkedSubject
    if (grouped.size() == 1) {
        ev.linkedSubject = m_selectedStages.first().subjectName;
    } else {
        ev.linkedSubject = "";
    }

    ev.color = hasExam ? "#d50000" : (m_selectedStages.first().trackType == "lab" ? "#0b8043" : (m_selectedStages.first().trackType == "exercise" ? "#f6bf26" : "#4285f4"));
    ev.start = start;
    ev.end = end;

    m_calendarEngine->addEvent(ev);
    m_canvas->refreshFromEngine();

    m_selectedStages.clear();
    refreshPlannerMatrix();
    updateSelectionSummary();
}

// 10. Odswiezenie calej matrycy 2D
void VisualOverduePlannerDialog::refreshPlannerMatrix()
{
    if (!m_tracker || !m_plannerGrid) return;

    const auto &tracks = m_tracker->getTracks();
    m_plannerGrid->setRowCount(tracks.size());

    int curWeek = m_tracker->getCurrentSemesterWeek();

    for (int r = 0; r < tracks.size(); ++r) {
        const auto &t = tracks[r];

        QTableWidgetItem *nameItem = new QTableWidgetItem(QString("%1  %2").arg(t.colorDot, t.displayName));
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_plannerGrid->setItem(r, 0, nameItem);

        for (int w = 1; w <= 15; ++w) {
            if (w <= t.cells.size()) {
                const auto &c = t.cells[w - 1];
                QWidget *cellW = createStageCellWidget(t, w, c);
                if (cellW) {
                    m_plannerGrid->setCellWidget(r, w, cellW);
                } else {
                    QTableWidgetItem *dashItem = new QTableWidgetItem("-");
                    dashItem->setTextAlignment(Qt::AlignCenter);
                    dashItem->setForeground(QColor(60, 60, 60));
                    dashItem->setFlags(dashItem->flags() & ~Qt::ItemIsEditable);
                    m_plannerGrid->setItem(r, w, dashItem);
                }
            }
        }
    }

    int totalBlocks = 0;
    int daysToDeadline = 7;
    double blocksPerDay = 0.0;
    double hoursPerDay = 0.0;
    m_tracker->calculatePacingMetrics(totalBlocks, daysToDeadline, blocksPerDay, hoursPerDay);

    if (m_pacingBanner) {
        m_pacingBanner->setText(
            QString("⚡ TEMPO: %1 zaległości | Najbliższy termin za: %2 dni | Bieżący tydzień: T%3")
                .arg(totalBlocks).arg(daysToDeadline).arg(curWeek)
        );
    }
}

// 11. Nawigacja kalendarza
void VisualOverduePlannerDialog::onPrevWeekClicked()
{
    if (m_canvas) {
        m_canvas->prevWeek();
        if (m_periodLabel) m_periodLabel->setText(m_canvas->getWeekPeriodLabel());
    }
}

void VisualOverduePlannerDialog::onNextWeekClicked()
{
    if (m_canvas) {
        m_canvas->nextWeek();
        if (m_periodLabel) m_periodLabel->setText(m_canvas->getWeekPeriodLabel());
    }
}

void VisualOverduePlannerDialog::onTodayClicked()
{
    if (m_canvas) {
        m_canvas->goToToday();
        if (m_periodLabel) m_periodLabel->setText(m_canvas->getWeekPeriodLabel());
    }
}

// 12. Zablokowanie planu
void VisualOverduePlannerDialog::onLockPlanClicked()
{
    if (m_profileManager) {
        m_profileManager->setProfileLock("overdue", LockType::Characters, 1000, QDateTime());
        m_profileManager->setProfileActive("overdue", true);
    }
    QMessageBox::information(this, "Plan zablokowany", "Plan został zablokowany! Kafelki nauki są chronione (wymóg odblokowania: 1000 znaków).");
    accept();
}

// 13. Budowa interfejsu VisualOverduePlannerDialog
void VisualOverduePlannerDialog::setupUi()
{
    QHBoxLayout *rootLayout = new QHBoxLayout(this);
    rootLayout->setContentsMargins(16, 14, 16, 14);
    rootLayout->setSpacing(14);

    QVBoxLayout *leftCalCol = new QVBoxLayout();

    QHBoxLayout *calNavRow = new QHBoxLayout();
    m_periodLabel = new QLabel(this);
    m_periodLabel->setObjectName("periodHeaderLabel");
    QPushButton *btnPrev = new QPushButton("◀ Poprzedni", this);
    QPushButton *btnToday = new QPushButton("Dziś", this);
    QPushButton *btnNext = new QPushButton("Następny ▶", this);

    calNavRow->addWidget(m_periodLabel);
    calNavRow->addStretch();
    calNavRow->addWidget(btnPrev);
    calNavRow->addWidget(btnToday);
    calNavRow->addWidget(btnNext);
    leftCalCol->addLayout(calNavRow);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_canvas = new CalendarCanvas(m_calendarEngine, m_profileManager, this);
    scroll->setWidget(m_canvas);
    leftCalCol->addWidget(scroll, 1);

    rootLayout->addLayout(leftCalCol, 5);

    QVBoxLayout *rightPanelCol = new QVBoxLayout();

    QLabel *panelTitle = new QLabel("ZAZNACZ FAZĘ (np. 0) I PRZECIĄGNIJ NA KALENDARZU:", this);
    panelTitle->setObjectName("viewSectionTitle");
    rightPanelCol->addWidget(panelTitle);

    m_pacingBanner = new QLabel(this);
    m_pacingBanner->setObjectName("lockWarningBanner");
    rightPanelCol->addWidget(m_pacingBanner);

    m_selectionInfoLabel = new QLabel(this);
    m_selectionInfoLabel->setObjectName("dashboardCardTitle");
    m_selectionInfoLabel->setWordWrap(true);
    rightPanelCol->addWidget(m_selectionInfoLabel);

    m_plannerGrid = new QTableWidget(this);
    m_plannerGrid->setColumnCount(16);

    int curWeek = m_tracker->getCurrentSemesterWeek();
    QStringList headers;
    headers << "Przedmiot (Tor zajęć)";
    for (int i = 1; i <= 15; ++i) {
        if (i == curWeek) {
            headers << QString("T%1 [TERAZ]").arg(i);
        } else {
            headers << QString("T%1").arg(i);
        }
    }
    m_plannerGrid->setHorizontalHeaderLabels(headers);
    m_plannerGrid->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_plannerGrid->horizontalHeader()->setMinimumSectionSize(145);
    for (int i = 1; i <= 15; ++i) {
        m_plannerGrid->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    m_plannerGrid->verticalHeader()->setDefaultSectionSize(38);
    m_plannerGrid->setShowGrid(true);
    rightPanelCol->addWidget(m_plannerGrid, 1);

    QHBoxLayout *actionBtns = new QHBoxLayout();
    m_btnClearSelection = new QPushButton("Wyczyść zaznaczenie", this);
    m_btnLockPlan = new QPushButton("🔒 ZABLOKUJ PLAN (1000 znaków)", this);
    m_btnLockPlan->setObjectName("primaryGreenBtn");
    actionBtns->addWidget(m_btnClearSelection);
    actionBtns->addStretch();
    actionBtns->addWidget(m_btnLockPlan);
    rightPanelCol->addLayout(actionBtns);

    rootLayout->addLayout(rightPanelCol, 6);

    connect(btnPrev, &QPushButton::clicked, this, &VisualOverduePlannerDialog::onPrevWeekClicked);
    connect(btnToday, &QPushButton::clicked, this, &VisualOverduePlannerDialog::onTodayClicked);
    connect(btnNext, &QPushButton::clicked, this, &VisualOverduePlannerDialog::onNextWeekClicked);

    connect(m_canvas, &CalendarCanvas::newBlockDrawnByUser, this, &VisualOverduePlannerDialog::onCanvasDrawnBlock);
    connect(m_btnClearSelection, &QPushButton::clicked, this, &VisualOverduePlannerDialog::onClearSelectionClicked);
    connect(m_btnLockPlan, &QPushButton::clicked, this, &VisualOverduePlannerDialog::onLockPlanClicked);

    m_periodLabel->setText(m_canvas->getWeekPeriodLabel());
    refreshPlannerMatrix();
    updateSelectionSummary();
}

// 14. Konstruktor VisualOverduePlannerDialog
VisualOverduePlannerDialog::VisualOverduePlannerDialog(OverdueTracker *tracker, CalendarEngine *calendarEngine,
                                                       ProfileManager *profileManager, QWidget *parent)
    : QDialog(parent)
    , m_tracker(tracker)
    , m_calendarEngine(calendarEngine)
    , m_profileManager(profileManager)
    , m_canvas(nullptr)
    , m_periodLabel(nullptr)
    , m_pacingBanner(nullptr)
    , m_selectionInfoLabel(nullptr)
    , m_plannerGrid(nullptr)
    , m_btnLockPlan(nullptr)
    , m_btnClearSelection(nullptr)
{
    setWindowTitle("Visual Overdue Planner // Spaced Learning System");
    resize(1440, 820);
    setupUi();
}

// 15. Otwarcie Visual Plannera
void OverdueTrackerWidget::onOpenVisualPlannerClicked()
{
    if (!m_tracker || !m_calendarEngine) {
        QMessageBox::warning(this, "Błąd", "Silnik kalendarza nie został załadowany.");
        return;
    }

    VisualOverduePlannerDialog dlg(m_tracker, m_calendarEngine, m_profileManager, this);
    dlg.exec();
    refreshGrid();
}

// 16. Usuniecie wybranego wiersza
void OverdueTrackerWidget::onDeleteTrackClicked()
{
    int row = m_table->currentRow();
    if (row < 0 || row >= m_tracker->getTracks().size()) {
        QMessageBox::information(this, "Wybór", "Zaznacz wiersz do usunięcia.");
        return;
    }

    QString name = m_tracker->getTracks()[row].displayName;
    auto res = QMessageBox::question(this, "Usuń wiersz", QString("Czy na pewno chcesz usunąć tor '%1' z Overdue Trackera?").arg(name));
    if (res == QMessageBox::Yes) {
        m_tracker->removeTrack(name);
        refreshGrid();
    }
}

// 17. Odswiezenie z planu
void OverdueTrackerWidget::onResyncClicked()
{
    if (m_subjectManager) {
        m_tracker->syncFromSubjectManager(m_subjectManager->getSubjects());
        refreshGrid();
        QMessageBox::information(this, "Synchronizacja", "Przeliczono tory zajęć oraz tygodnie semestru!");
    }
}

// 18. Informacja po dwukliku
void OverdueTrackerWidget::onCellDoubleClicked(int row, int column)
{
    Q_UNUSED(row);
    Q_UNUSED(column);
    QMessageBox::information(this, "Informacja", "Postępy w nauce (+0, +1...) aktualizują się w pełni automatycznie, gdy przeciągasz kafelki w oknie Visual Planner na kalendarz.");
}

// 19. Odswiezenie widoku glownego trackera
void OverdueTrackerWidget::refreshGrid()
{
    const auto &tracks = m_tracker->getTracks();
    m_table->setRowCount(tracks.size());

    int curWeek = m_tracker->getCurrentSemesterWeek();

    for (int r = 0; r < tracks.size(); ++r) {
        const auto &t = tracks[r];

        QTableWidgetItem *nameItem = new QTableWidgetItem(QString("%1  %2").arg(t.colorDot, t.displayName));
        m_table->setItem(r, 0, nameItem);

        for (int w = 1; w <= 15 && w <= t.cells.size(); ++w) {
            m_table->setItem(r, w, createClassCellItem(t.cells[w - 1], t.trackType));
        }
    }

    int totalBlocks = 0;
    int daysToDeadline = 7;
    double blocksPerDay = 0.0;
    double hoursPerDay = 0.0;
    m_tracker->calculatePacingMetrics(totalBlocks, daysToDeadline, blocksPerDay, hoursPerDay);

    m_pacingBanner->setText(
        QString("⚡ KALKULATOR TEMPA: Masz %1 zaległych bloków. Do najbliższego terminu: %2 dni. Aby być na bieżąco, rób: %3 bloków/dzień (~%4 godz./dzień).")
            .arg(totalBlocks).arg(daysToDeadline).arg(blocksPerDay, 0, 'f', 1).arg(hoursPerDay, 0, 'f', 1)
    );

    int debtMin = m_tracker->getPendingAfkDebtMinutes();
    if (debtMin > 0) {
        m_afkDebtBanner->setText(QString("⏳ Dług AFK przeniesiony na następny blok: +%1 min.").arg(debtMin));
    } else {
        m_afkDebtBanner->setText(QString("Bieżący tydzień semestru: T%1 | Dług AFK: 0 min.").arg(curWeek));
    }
}

// 20. Aktualizacja dlugu AFK
void OverdueTrackerWidget::onAfkDebtTransferred(int totalDebtMinutes)
{
    if (totalDebtMinutes > 0) {
        m_afkDebtBanner->setText(QString("⏳ Dług AFK przeniesiony na następny blok: +%1 min.").arg(totalDebtMinutes));
    } else {
        m_afkDebtBanner->setText("Dług AFK: 0 min (brak zaległości).");
    }
}

// 21. Stan AFK
void OverdueTrackerWidget::onAfkStatusChanged(bool isAfk, int idleSeconds)
{
    if (isAfk) {
        m_afkWarningLabel->setText(QString("⚠️ STAN AFK AKTYWNY! Bezczynność: %1s (czas zostanie doliczony do sesji nauki).").arg(idleSeconds));
    } else {
        m_afkWarningLabel->setText("");
    }
}

// 22. Odliczanie aktywnej sesji
void OverdueTrackerWidget::onSessionTimeUpdated(int remainingMinutes, int currentAfkDebtMinutes)
{
    m_sessionStatusLabel->setText(
        QString("Automatyczna sesja Overdue: Pozostało %1 min | AFK: %2 min")
            .arg(remainingMinutes).arg(currentAfkDebtMinutes)
    );
}

// 23. Settery managerow
void OverdueTrackerWidget::setSubjectManager(SubjectManager *subjectManager)
{
    m_subjectManager = subjectManager;
}

void OverdueTrackerWidget::setProfileManager(ProfileManager *profileManager)
{
    m_profileManager = profileManager;
}

// 24. Budowa interfejsu OverdueTrackerWidget
void OverdueTrackerWidget::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(10);

    QLabel *hdr = new QLabel("OVERDUE TRACKER // SPACED LEARNING SYSTEM", this);
    hdr->setObjectName("viewHeaderTitle");
    mainLayout->addWidget(hdr);

    m_pacingBanner = new QLabel(this);
    m_pacingBanner->setObjectName("lockWarningBanner");
    mainLayout->addWidget(m_pacingBanner);

    m_afkDebtBanner = new QLabel(this);
    m_afkDebtBanner->setObjectName("periodHeaderLabel");
    mainLayout->addWidget(m_afkDebtBanner);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(16);

    int curWeek = m_tracker->getCurrentSemesterWeek();
    QStringList headers;
    headers << "Przedmiot (Tor zajęć)";
    for (int i = 1; i <= 15; ++i) {
        if (i == curWeek) {
            headers << QString("T%1 [TERAZ]").arg(i);
        } else {
            headers << QString("T%1").arg(i);
        }
    }
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int i = 1; i <= 15; ++i) {
        m_table->horizontalHeader()->setSectionResizeMode(i, QHeaderView::ResizeToContents);
    }
    mainLayout->addWidget(m_table);

    m_sessionStatusLabel = new QLabel("Profil 'overdue' aktywuje się automatycznie o wyznaczonej godzinie z kalendarza.", this);
    m_sessionStatusLabel->setObjectName("dashboardCardTitle");
    mainLayout->addWidget(m_sessionStatusLabel);

    m_afkWarningLabel = new QLabel(this);
    m_afkWarningLabel->setObjectName("cheatWarningActive");
    mainLayout->addWidget(m_afkWarningLabel);

    QHBoxLayout *btnRow = new QHBoxLayout();
    m_btnOpenPlanner = new QPushButton("📅 Zaplanuj w kalendarzu (Visual Planner)", this);
    m_btnOpenPlanner->setObjectName("primaryGreenBtn");
    m_btnDeleteTrack = new QPushButton("🗑 Usuń zaznaczony wiersz", this);
    m_btnDeleteTrack->setObjectName("giveUpBtn");
    m_btnResync = new QPushButton("🔄 Odśwież z planu", this);

    btnRow->addWidget(m_btnOpenPlanner);
    btnRow->addWidget(m_btnDeleteTrack);
    btnRow->addWidget(m_btnResync);
    btnRow->addStretch();
    mainLayout->addLayout(btnRow);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, &OverdueTrackerWidget::onCellDoubleClicked);
    connect(m_btnOpenPlanner, &QPushButton::clicked, this, &OverdueTrackerWidget::onOpenVisualPlannerClicked);
    connect(m_btnDeleteTrack, &QPushButton::clicked, this, &OverdueTrackerWidget::onDeleteTrackClicked);
    connect(m_btnResync, &QPushButton::clicked, this, &OverdueTrackerWidget::onResyncClicked);

    connect(m_tracker, &OverdueTracker::trackerDataChanged, this, &OverdueTrackerWidget::refreshGrid);
    connect(m_tracker, &OverdueTracker::sessionTimeUpdated, this, &OverdueTrackerWidget::onSessionTimeUpdated);
    connect(m_tracker, &OverdueTracker::afkStatusChanged, this, &OverdueTrackerWidget::onAfkStatusChanged);
    connect(m_tracker, &OverdueTracker::afkDebtTransferred, this, &OverdueTrackerWidget::onAfkDebtTransferred);

    refreshGrid();
}

// 25. Konstruktor OverdueTrackerWidget
OverdueTrackerWidget::OverdueTrackerWidget(OverdueTracker *tracker, CalendarEngine *calendarEngine,
                                           ProfileManager *profileManager, QWidget *parent)
    : QWidget(parent)
    , m_tracker(tracker)
    , m_calendarEngine(calendarEngine)
    , m_profileManager(profileManager)
    , m_subjectManager(nullptr)
    , m_table(nullptr)
    , m_btnOpenPlanner(nullptr)
    , m_btnDeleteTrack(nullptr)
    , m_btnResync(nullptr)
    , m_pacingBanner(nullptr)
    , m_sessionStatusLabel(nullptr)
    , m_afkWarningLabel(nullptr)
    , m_afkDebtBanner(nullptr)
{
    setupUi();
}
