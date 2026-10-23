// Plik implementacji okna ChooseOverduesDialog.
// Wyswietla statystyki tempa nadrabiania zaleglosci, pozwala zaznaczyc kafelki na dzis.
// Zaktualizowano kolumny do obslugi rozbijania minut z zadan na kroki.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "choose_overdues_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QDate>
#include <QTime>
#include <algorithm>

// 1. Wstawienie zaznaczonych zadan do kalendarza na dzis w postaci scalonego bloku
void ChooseOverduesDialog::onScheduleSelectedClicked()
{
    QList<OverdueTaskItem> chosen;
    int totalMin = 0;

    for (int i = 0; i < m_taskTable->rowCount(); ++i) {
        QTableWidgetItem *item = m_taskTable->item(i, 0);
        if (item && item->checkState() == Qt::Checked) {
            if (i < m_taskList.size()) {
                chosen.append(m_taskList[i]);
                totalMin += m_taskList[i].durationMinutes;
            }
        }
    }

    if (!chosen.isEmpty() && m_tracker && m_calendarEngine) {
        if (totalMin <= 0) totalMin = 60;

        QDate today = QDate::currentDate();
        QTime nowTime = QTime::currentTime();
        int curMin = nowTime.hour() * 60 + nowTime.minute();
        int startMin = std::max(curMin + 15, 17 * 60);
        startMin = ((startMin + 14) / 15) * 15;

        if (startMin + totalMin > 1440) {
            startMin = std::max(0, 1440 - totalMin);
            startMin = (startMin / 15) * 15;
        }

        QTime startTime(startMin / 60, startMin % 60);
        m_tracker->scheduleBundledTasks(m_calendarEngine, chosen, today, startTime, totalMin);
    }
}

// 2. Przymusowe auto-zablokowanie
void ChooseOverduesDialog::onForceAutoLockClicked()
{
    m_tracker->forceAutoLockSchedule(m_calendarEngine);
    QMessageBox::information(this, "Wymuszono Auto-Lock", "Automatycznie wybrano i zablokowano najważniejsze zaległości (kolokwia i laby) do godziny 24:00!");
    accept();
}

// 3. Zablokowanie planu na sztywno (LOCK)
void ChooseOverduesDialog::onLockPlanClicked()
{
    onScheduleSelectedClicked();
    QMessageBox::information(this, "Plan zablokowany", "Plan nauki na dziś został zablokowany! Kafelki są aktywne w kalendarzu z profilem 'overdue'.");
    accept();
}

// 4. Odswiezenie listy zadan i statystyk tempa
void ChooseOverduesDialog::refreshTaskList()
{
    m_taskList = m_tracker->getPendingOverdueTasks(m_calendarEngine);

    int totalBlocks = 0;
    int daysToDeadline = 7;
    double blocksPerDay = 0.0;
    double hoursPerDay = 0.0;
    m_tracker->calculatePacingMetrics(totalBlocks, daysToDeadline, blocksPerDay, hoursPerDay);

    m_pacingBanner->setText(
        QString("⚡ KALKULATOR TEMPA ZALEGŁOŚCI:\n"
                "• Łącznie zaległości: %1 bloków\n"
                "• Czas do najbliższego kolokwium / terminu: %2 dni\n"
                "• WYMAGANE TEMPO: musisz robić średnio %3 bloków / dzień (ok. %4 godz./dzień), aby być na bieżąco!")
            .arg(totalBlocks)
            .arg(daysToDeadline)
            .arg(blocksPerDay, 0, 'f', 1)
            .arg(hoursPerDay, 0, 'f', 1)
    );

    m_taskTable->setRowCount(m_taskList.size());
    for (int i = 0; i < m_taskList.size(); ++i) {
        const auto &t = m_taskList[i];

        QTableWidgetItem *chkItem = new QTableWidgetItem(t.label);
        chkItem->setCheckState(Qt::Unchecked);
        m_taskTable->setItem(i, 0, chkItem);

        QTableWidgetItem *weekItem = new QTableWidgetItem(QString("Tydzień %1").arg(t.weekNumber));
        weekItem->setTextAlignment(Qt::AlignCenter);
        m_taskTable->setItem(i, 1, weekItem);

        QTableWidgetItem *typeItem = new QTableWidgetItem(t.isExam ? "🚨 KOLOKWIUM" : (t.trackType == "lab" ? "Laboratorium" : "Wykład"));
        typeItem->setTextAlignment(Qt::AlignCenter);
        if (t.isExam) {
            typeItem->setForeground(QColor(255, 59, 48));
        }
        m_taskTable->setItem(i, 2, typeItem);

        QTableWidgetItem *timeItem = new QTableWidgetItem(QString("Brakuje %1 min").arg(t.durationMinutes));
        timeItem->setTextAlignment(Qt::AlignCenter);
        m_taskTable->setItem(i, 3, timeItem);
    }
}

// 5. Budowa interfejsu
void ChooseOverduesDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(10);

    QLabel *hdr = new QLabel("CHOOSE OVERDUES // WYBÓR ZALEGŁOŚCI NA DZIŚ", this);
    hdr->setObjectName("dialogHeader");
    mainLayout->addWidget(hdr);

    m_pacingBanner = new QLabel(this);
    m_pacingBanner->setObjectName("lockWarningBanner");
    mainLayout->addWidget(m_pacingBanner);

    m_taskTable = new QTableWidget(this);
    m_taskTable->setColumnCount(4);
    QStringList headers;
    headers << "Przedmiot (Zaznacz na dziś)" << "Tydzień" << "Typ" << "Czas";
    m_taskTable->setHorizontalHeaderLabels(headers);
    m_taskTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_taskTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_taskTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_taskTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    mainLayout->addWidget(m_taskTable, 1);

    QHBoxLayout *btnRow = new QHBoxLayout();
    m_btnForceAutoLock = new QPushButton("⚡ Wymuś Auto-Lock (Najważniejsze)", this);
    m_btnScheduleSelected = new QPushButton("➕ Wstaw zaznaczone do kalendarza", this);
    m_btnLockPlan = new QPushButton("🔒 ZABLOKUJ PLAN (LOCK)", this);
    m_btnLockPlan->setObjectName("primaryGreenBtn");

    btnRow->addWidget(m_btnForceAutoLock);
    btnRow->addStretch();
    btnRow->addWidget(m_btnScheduleSelected);
    btnRow->addWidget(m_btnLockPlan);
    mainLayout->addLayout(btnRow);

    connect(m_btnScheduleSelected, &QPushButton::clicked, this, &ChooseOverduesDialog::onScheduleSelectedClicked);
    connect(m_btnLockPlan, &QPushButton::clicked, this, &ChooseOverduesDialog::onLockPlanClicked);
    connect(m_btnForceAutoLock, &QPushButton::clicked, this, &ChooseOverduesDialog::onForceAutoLockClicked);

    refreshTaskList();
}

// 6. Konstruktor
ChooseOverduesDialog::ChooseOverduesDialog(OverdueTracker *tracker, CalendarEngine *calendarEngine, QWidget *parent)
    : QDialog(parent)
    , m_tracker(tracker)
    , m_calendarEngine(calendarEngine)
    , m_pacingBanner(nullptr)
    , m_taskTable(nullptr)
    , m_btnScheduleSelected(nullptr)
    , m_btnLockPlan(nullptr)
    , m_btnForceAutoLock(nullptr)
{
    setWindowTitle("Choose Overdues & Pacing Calculator");
    resize(720, 520);
    setupUi();
}
