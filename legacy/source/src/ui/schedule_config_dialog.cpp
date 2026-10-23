// Plik implementacji okna ScheduleConfigDialog.
// Konfiguruje wystepowanie bloku w kalendarzu z powtorzeniami co 7 dni.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "schedule_config_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

// 1. Zapis konfiguracji i wygenerowanie blokow w kalendarzu
void ScheduleConfigDialog::onSaveClicked()
{
    if (m_rbCalendar->isChecked()) {
        QList<int> days;
        for (int i = 0; i < m_dayCheckboxes.size(); ++i) {
            if (m_dayCheckboxes[i]->isChecked()) {
                days.append(i + 1); // 1 = Poniedzialek
            }
        }
        if (days.isEmpty()) days.append(QDate::currentDate().dayOfWeek());

        m_calendarEngine->scheduleRecurringProfile(
            m_profile.name,
            m_timeStart->time(),
            m_timeEnd->time(),
            days,
            m_spinWeeks->value()
            );
    }
    accept();
}

// 2. Przelaczanie widocznosci opcji kalendarza
void ScheduleConfigDialog::onRadioToggled()
{
    m_calendarSettingsBox->setVisible(m_rbCalendar->isChecked());
}

// 3. Budowa interfejsu
void ScheduleConfigDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *hdr = new QLabel(QString("HARMONOGRAM DLA: %1").arg(m_profile.name.toUpper()), this);
    hdr->setObjectName("dialogHeader");
    mainLayout->addWidget(hdr);

    m_rbAllTimes = new QRadioButton("Blocked at all times (blokada caly czas aktywna)", this);
    m_rbCalendar = new QRadioButton("Scheduled in calendar (blokada wg terminow w kalendarzu)", this);

    if (m_profile.scheduleType == ScheduleType::ScheduledInCalendar) {
        m_rbCalendar->setChecked(true);
    } else {
        m_rbAllTimes->setChecked(true);
    }

    mainLayout->addWidget(m_rbAllTimes);
    mainLayout->addWidget(m_rbCalendar);

    m_calendarSettingsBox = new QWidget(this);
    QVBoxLayout *cLayout = new QVBoxLayout(m_calendarSettingsBox);

    QHBoxLayout *tRow = new QHBoxLayout();
    m_timeStart = new QTimeEdit(QTime(18, 0), this);
    m_timeEnd = new QTimeEdit(QTime(21, 0), this);
    tRow->addWidget(new QLabel("Godzina od:", this));
    tRow->addWidget(m_timeStart);
    tRow->addWidget(new QLabel("do:", this));
    tRow->addWidget(m_timeEnd);
    cLayout->addLayout(tRow);

    QHBoxLayout *daysRow = new QHBoxLayout();
    QStringList dayNames = {"Pn", "Wt", "Śr", "Czw", "Pt", "Sob", "Ndz"};
    for (int i = 0; i < 7; ++i) {
        QCheckBox *cb = new QCheckBox(dayNames[i], this);
        if (i < 5) cb->setChecked(true);
        m_dayCheckboxes.append(cb);
        daysRow->addWidget(cb);
    }
    cLayout->addWidget(new QLabel("Dni tygodnia powtórzeń (co 7 dni):", this));
    cLayout->addLayout(daysRow);

    QHBoxLayout *occRow = new QHBoxLayout();
    m_spinWeeks = new QSpinBox(this);
    m_spinWeeks->setRange(1, 52);
    m_spinWeeks->setValue(8);
    m_spinWeeks->setSuffix(" powtórzeń");
    occRow->addWidget(new QLabel("Zakres harmonogramu:", this));
    occRow->addWidget(m_spinWeeks);
    cLayout->addLayout(occRow);

    mainLayout->addWidget(m_calendarSettingsBox);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    QPushButton *cancelBtn = new QPushButton("Anuluj", this);
    QPushButton *saveBtn = new QPushButton("Zapisz", this);
    saveBtn->setObjectName("primaryGreenBtn");
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(saveBtn);
    mainLayout->addLayout(btnRow);

    connect(m_rbAllTimes, &QRadioButton::toggled, this, &ScheduleConfigDialog::onRadioToggled);
    connect(m_rbCalendar, &QRadioButton::toggled, this, &ScheduleConfigDialog::onRadioToggled);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(saveBtn, &QPushButton::clicked, this, &ScheduleConfigDialog::onSaveClicked);

    onRadioToggled();
}

// 4. Getter
ScheduleType ScheduleConfigDialog::getSelectedScheduleType() const
{
    return m_rbCalendar->isChecked() ? ScheduleType::ScheduledInCalendar : ScheduleType::BlockedAtAllTimes;
}

// 5. Konstruktor
ScheduleConfigDialog::ScheduleConfigDialog(const BlockProfile &profile, CalendarEngine *calendarEngine, QWidget *parent)
    : QDialog(parent)
    , m_profile(profile)
    , m_calendarEngine(calendarEngine)
{
    setWindowTitle(QString("Harmonogram - %1").arg(profile.name));
    resize(500, 420);
    setupUi();
}