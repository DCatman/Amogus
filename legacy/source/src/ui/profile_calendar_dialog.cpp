// Plik implementacji okna ProfileCalendarDialog.
// Udostepnia pelny widok kalendarza 24h z automatycznym linkowaniem tworzonych blokow do profilu.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "profile_calendar_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

// 1. Nawigacja kalendarza do biezacego dnia
void ProfileCalendarDialog::onTodayClicked()
{
    m_canvas->goToToday();
    m_periodLabel->setText(m_canvas->getWeekPeriodLabel());
}

// 2. Przejscie do nastepnego tygodnia
void ProfileCalendarDialog::onNextWeekClicked()
{
    m_canvas->nextWeek();
    m_periodLabel->setText(m_canvas->getWeekPeriodLabel());
}

// 3. Przejscie do poprzedniego tygodnia
void ProfileCalendarDialog::onPrevWeekClicked()
{
    m_canvas->prevWeek();
    m_periodLabel->setText(m_canvas->getWeekPeriodLabel());
}

// 4. Budowa interfejsu
void ProfileCalendarDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 16, 20, 16);
    mainLayout->setSpacing(10);

    QLabel *hdr = new QLabel(QString("HARMONOGRAM DLA PROFILU: %1").arg(m_profile.name.toUpper()), this);
    hdr->setObjectName("dialogHeader");
    mainLayout->addWidget(hdr);

    QLabel *info = new QLabel(
        "Przeciągnij kursorem po siatce, aby zaplanować blokadę dla tego profilu.\n"
        "Wszystkie nowe wydarzenia zostaną powiązane wyłącznie z tym blokiem.", this);
    info->setObjectName("blockSubtitle");
    mainLayout->addWidget(info);

    QHBoxLayout *navRow = new QHBoxLayout();
    m_periodLabel = new QLabel(this);
    m_periodLabel->setObjectName("periodHeaderLabel");
    QPushButton *prevBtn = new QPushButton("◀ Poprzedni", this);
    QPushButton *todayBtn = new QPushButton("Dziś", this);
    QPushButton *nextBtn = new QPushButton("Następny ▶", this);

    navRow->addWidget(m_periodLabel);
    navRow->addStretch();
    navRow->addWidget(prevBtn);
    navRow->addWidget(todayBtn);
    navRow->addWidget(nextBtn);
    mainLayout->addLayout(navRow);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    m_canvas = new CalendarCanvas(m_calendarEngine, m_profileManager, this);
    scroll->setWidget(m_canvas);
    mainLayout->addWidget(scroll, 1);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    QPushButton *closeBtn = new QPushButton("Zamknij", this);
    closeBtn->setObjectName("primaryGreenBtn");
    btnRow->addWidget(closeBtn);
    mainLayout->addLayout(btnRow);

    connect(prevBtn, &QPushButton::clicked, this, &ProfileCalendarDialog::onPrevWeekClicked);
    connect(todayBtn, &QPushButton::clicked, this, &ProfileCalendarDialog::onTodayClicked);
    connect(nextBtn, &QPushButton::clicked, this, &ProfileCalendarDialog::onNextWeekClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    m_periodLabel->setText(m_canvas->getWeekPeriodLabel());
}

// 5. Konstruktor
ProfileCalendarDialog::ProfileCalendarDialog(const BlockProfile &profile, CalendarEngine *calendarEngine,
                                             ProfileManager *profileManager, QWidget *parent)
    : QDialog(parent)
    , m_profile(profile)
    , m_calendarEngine(calendarEngine)
    , m_profileManager(profileManager)
    , m_canvas(nullptr)
    , m_periodLabel(nullptr)
{
    setWindowTitle(QString("Harmonogram w kalendarzu - %1").arg(profile.name));
    resize(960, 680);
    setupUi();
}
