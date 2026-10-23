// Plik implementacji modulu DashboardView.
// Umieszcza sekcje ACTIVE BLOCKS na gorze oraz kalendarz z aktywna obsluga tworzenia eventow pod spodem.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "dashboard_view.h"
#include "event_editor_dialog.h"

// 1. Obsluga narysowania nowego bloku mysza na kalendarzu w Dashboardzie
void DashboardView::onCanvasDrawnBlock(const QDateTime &start, const QDateTime &end)
{
    CalendarEvent ev;
    ev.title = "Nowe wydarzenie";
    ev.start = start;
    ev.end = end;
    ev.category = "Event";
    ev.color = "#4285f4";

    EventEditorDialog dlg(ev, false, m_profileManager, this);
    if (dlg.exec() == QDialog::Accepted) {
        CalendarEvent createdEv = dlg.getEventData();
        m_calendarEngine->addEvent(createdEv);

        if (dlg.isRecurrenceEnabled()) {
            int weeks = dlg.getRecurrenceWeeksCount();
            m_calendarEngine->setBatchUpdating(true);
            for (int w = 1; w < weeks; ++w) {
                CalendarEvent recEv = createdEv;
                recEv.id = QString();
                recEv.seriesId = createdEv.seriesId.isEmpty() ? createdEv.id : createdEv.seriesId;
                recEv.start = createdEv.start.addDays(w * 7);
                recEv.end = createdEv.end.addDays(w * 7);
                m_calendarEngine->addEvent(recEv);
            }
            m_calendarEngine->setBatchUpdating(false);
        }
    }

    if (m_calendarCanvas) {
        m_calendarCanvas->refreshFromEngine();
    }
}

// 2. Odswiezenie listy aktywnych blokow na gorze dashboardu
void DashboardView::refreshDashboard()
{
    QLayoutItem *child = nullptr;
    while ((child = m_activeBlocksLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    const auto &profiles = m_profileManager->getAllProfiles();
    bool any = false;

    for (const auto &p : profiles) {
        if (p.isActive) {
            any = true;
            QWidget *card = new QWidget(this);
            card->setObjectName("dashboardItemCard");
            QHBoxLayout *row = new QHBoxLayout(card);
            row->setContentsMargins(12, 8, 12, 8);

            QLabel *title = new QLabel("🛡 " + p.name, this);
            title->setObjectName("dashboardCardTitle");
            row->addWidget(title);

            QLabel *mode = new QLabel(p.scheduleType == ScheduleType::BlockedAtAllTimes ? "Blocked at all times" : "Scheduled in calendar", this);
            mode->setObjectName("dashboardCardSubtitle");
            row->addWidget(mode);

            row->addStretch();

            QLabel *lockStatus = new QLabel(p.isLocked ? "Locked" : "Unlocked", this);
            lockStatus->setObjectName(p.isLocked ? "badgeLocked" : "badgeUnlocked");
            row->addWidget(lockStatus);

            m_activeBlocksLayout->addWidget(card);
        }
    }

    if (!any) {
        QLabel *empty = new QLabel("Brak aktywnych blokad w tej chwili.", this);
        empty->setObjectName("dashboardCardSubtitle");
        m_activeBlocksLayout->addWidget(empty);
    }

    if (m_calendarCanvas) {
        m_periodLabel->setText(m_calendarCanvas->getWeekPeriodLabel());
    }
}

// 3. Budowa ukladu graficznego i polaczenie interakcji kalendarza
void DashboardView::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(12);

    QLabel *activeTitle = new QLabel("ACTIVE BLOCKS", this);
    activeTitle->setObjectName("viewHeaderTitle");
    mainLayout->addWidget(activeTitle);

    m_activeBlocksLayout = new QVBoxLayout();
    m_activeBlocksLayout->setSpacing(6);
    mainLayout->addLayout(m_activeBlocksLayout);

    mainLayout->addSpacing(10);

    QHBoxLayout *calHeader = new QHBoxLayout();
    QLabel *calTitle = new QLabel("HARMONOGRAM / KALENDARZ", this);
    calTitle->setObjectName("viewSectionTitle");
    calHeader->addWidget(calTitle);

    calHeader->addStretch();

    QPushButton *prevBtn = new QPushButton("◀ Poprzedni", this);
    QPushButton *todayBtn = new QPushButton("Dziś", this);
    QPushButton *nextBtn = new QPushButton("Następny ▶", this);
    m_periodLabel = new QLabel(this);
    m_periodLabel->setObjectName("periodHeaderLabel");

    calHeader->addWidget(m_periodLabel);
    calHeader->addWidget(prevBtn);
    calHeader->addWidget(todayBtn);
    calHeader->addWidget(nextBtn);
    mainLayout->addLayout(calHeader);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    m_calendarCanvas = new CalendarCanvas(m_calendarEngine, m_profileManager, this);
    scroll->setWidget(m_calendarCanvas);
    mainLayout->addWidget(scroll, 1);

    connect(prevBtn, &QPushButton::clicked, this, [this]() {
        m_calendarCanvas->prevWeek();
        m_periodLabel->setText(m_calendarCanvas->getWeekPeriodLabel());
    });
    connect(todayBtn, &QPushButton::clicked, this, [this]() {
        m_calendarCanvas->goToToday();
        m_periodLabel->setText(m_calendarCanvas->getWeekPeriodLabel());
    });
    connect(nextBtn, &QPushButton::clicked, this, [this]() {
        m_calendarCanvas->nextWeek();
        m_periodLabel->setText(m_calendarCanvas->getWeekPeriodLabel());
    });

    connect(m_calendarCanvas, &CalendarCanvas::newBlockDrawnByUser, this, &DashboardView::onCanvasDrawnBlock);
    connect(m_profileManager, &ProfileManager::profilesUpdated, this, &DashboardView::refreshDashboard);

    refreshDashboard();
}

// 4. Konstruktor DashboardView
DashboardView::DashboardView(ProfileManager *profileManager, CalendarEngine *calendarEngine, QWidget *parent)
    : QWidget(parent)
    , m_profileManager(profileManager)
    , m_calendarEngine(calendarEngine)
    , m_activeBlocksLayout(nullptr)
    , m_calendarCanvas(nullptr)
    , m_periodLabel(nullptr)
{
    setupUi();
}
