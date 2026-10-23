// Plik implementacji wiersza profilu blokowania BlockRowWidget.
// Wykorzystuje CalendarEngine oraz odzwierciedla rzeczywisty stan klodki w oparciu o aktywnosc profilu.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "block_row_widget.h"
#include "profile_calendar_dialog.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>

// 1. Odswiezenie wygladu i stanow przyciskow w wierszu
void BlockRowWidget::refreshUi()
{
    m_titleButton->setText(m_profile.name);

    if (m_profile.isOverdue) {
        m_subtitleButton->setText("Aktywowany automatycznie (ICS / Kalendarz)");
    } else if (m_profile.scheduleType == ScheduleType::BlockedAtAllTimes) {
        m_subtitleButton->setText("Blocked at all times");
    } else {
        m_subtitleButton->setText("Scheduled in calendar");
    }

    if (m_profile.isActive) {
        m_indicatorBar->setObjectName("indicatorActive");
        m_toggleButton->setText("On");
        m_toggleButton->setObjectName("toggleOnBtn");
    } else {
        m_indicatorBar->setObjectName("indicatorInactive");
        m_toggleButton->setText("Off");
        m_toggleButton->setObjectName("toggleOffBtn");
    }

    bool isLockedEffective = m_profileManager ? m_profileManager->isProfileLocked(m_profile.name) : m_profile.isLocked;

    if (isLockedEffective) {
        if (m_profile.lockType == LockType::Characters) {
            m_lockBadgeButton->setText(QString("%1 chars locked").arg(m_profile.requiredChars));
        } else if (m_profile.lockType == LockType::UntilTime) {
            m_lockBadgeButton->setText(QString("Until %1 locked").arg(m_profile.lockUntilTime.toString("hh:mm")));
        } else if (m_profile.lockType == LockType::Minigame) {
            m_lockBadgeButton->setText("Minigame locked");
        } else {
            m_lockBadgeButton->setText("Locked");
        }
        m_lockBadgeButton->setObjectName("lockLockedBtn");
    } else {
        m_lockBadgeButton->setText("Unlocked");
        m_lockBadgeButton->setObjectName("lockUnlockedBtn");
    }

    m_btnClickBreak->setVisible(m_profile.isActive && m_profile.breaks.clickBreakEnabled);
    m_btnCharBreak->setVisible(m_profile.isActive && m_profile.breaks.charBreakEnabled);
    m_btnCritBreak->setVisible(m_profile.isActive && m_profile.breaks.criticalBreakEnabled);

    m_btnClickBreak->setText(QString("Break %1m").arg(m_profile.breaks.clickBreakMinutes));
    m_btnCharBreak->setText(QString("Chars (%1)").arg(m_profile.breaks.charBreakChars));
    m_btnCritBreak->setText("Critical 3m");

    m_indicatorBar->style()->unpolish(m_indicatorBar);
    m_indicatorBar->style()->polish(m_indicatorBar);
    m_toggleButton->style()->unpolish(m_toggleButton);
    m_toggleButton->style()->polish(m_toggleButton);
    m_lockBadgeButton->style()->unpolish(m_lockBadgeButton);
    m_lockBadgeButton->style()->polish(m_lockBadgeButton);
}

// 2. Budowa ukladu graficznego wiersza
void BlockRowWidget::setupUi()
{
    setObjectName("blockRowCard");
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 6, 16, 6);
    mainLayout->setSpacing(10);

    m_indicatorBar = new QWidget(this);
    m_indicatorBar->setFixedWidth(6);
    mainLayout->addWidget(m_indicatorBar);

    QVBoxLayout *titleLayout = new QVBoxLayout();
    titleLayout->setSpacing(2);
    m_titleButton = new QPushButton(this);
    m_titleButton->setObjectName("blockTitleButton");
    m_titleButton->setCursor(Qt::PointingHandCursor);

    m_subtitleButton = new QPushButton(this);
    m_subtitleButton->setObjectName("flatRowBtn");
    m_subtitleButton->setCursor(Qt::PointingHandCursor);

    titleLayout->addWidget(m_titleButton, 0, Qt::AlignLeft);
    titleLayout->addWidget(m_subtitleButton, 0, Qt::AlignLeft);
    mainLayout->addLayout(titleLayout, 3);

    QHBoxLayout *breaksRow = new QHBoxLayout();
    breaksRow->setSpacing(6);

    m_btnClickBreak = new QPushButton(this);
    m_btnClickBreak->setObjectName("breakActionBtn");

    m_btnCharBreak = new QPushButton(this);
    m_btnCharBreak->setObjectName("breakActionBtn");

    m_btnCritBreak = new QPushButton(this);
    m_btnCritBreak->setObjectName("breakCritBtn");

    breaksRow->addWidget(m_btnClickBreak);
    breaksRow->addWidget(m_btnCharBreak);
    breaksRow->addWidget(m_btnCritBreak);
    mainLayout->addLayout(breaksRow, 2);

    mainLayout->addStretch();

    QVBoxLayout *toggleAndLockCol = new QVBoxLayout();
    toggleAndLockCol->setAlignment(Qt::AlignCenter);
    toggleAndLockCol->setSpacing(4);

    m_toggleButton = new QPushButton(this);
    m_toggleButton->setFixedWidth(72);
    toggleAndLockCol->addWidget(m_toggleButton, 0, Qt::AlignCenter);

    m_lockBadgeButton = new QPushButton(this);
    m_lockBadgeButton->setFixedWidth(120);
    toggleAndLockCol->addWidget(m_lockBadgeButton, 0, Qt::AlignCenter);

    mainLayout->addLayout(toggleAndLockCol);

    connect(m_titleButton, &QPushButton::clicked, this, [this]() {
        emit editRequested(m_profile.name);
    });

    connect(m_subtitleButton, &QPushButton::clicked, this, [this]() {
        if (m_calendarEngine && m_profileManager) {
            ProfileCalendarDialog dlg(m_profile, m_calendarEngine, m_profileManager, this);
            dlg.exec();
        }
    });

    connect(m_toggleButton, &QPushButton::clicked, this, [this]() {
        emit toggleRequested(m_profile.name);
    });
    connect(m_lockBadgeButton, &QPushButton::clicked, this, [this]() {
        emit lockConfigRequested(m_profile.name);
    });
    connect(m_btnClickBreak, &QPushButton::clicked, this, [this]() {
        emit clickBreakRequested(m_profile.name);
    });
    connect(m_btnCharBreak, &QPushButton::clicked, this, [this]() {
        emit charBreakRequested(m_profile.name);
    });
    connect(m_btnCritBreak, &QPushButton::clicked, this, [this]() {
        emit criticalBreakRequested(m_profile.name);
    });

    refreshUi();
}

// 3. Aktualizacja danych profilu
void BlockRowWidget::updateProfileData(const BlockProfile &profile)
{
    m_profile = profile;
    refreshUi();
}

// 4. Pobranie nazwy profilu
QString BlockRowWidget::getProfileName() const
{
    return m_profile.name;
}

// 5. Konstruktor BlockRowWidget
BlockRowWidget::BlockRowWidget(const BlockProfile &profile, CalendarEngine *calendarEngine,
                               ProfileManager *profileManager, QWidget *parent)
    : QWidget(parent)
    , m_profile(profile)
    , m_calendarEngine(calendarEngine)
    , m_profileManager(profileManager)
{
    setupUi();
}
