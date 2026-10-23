// Plik naglowkowy dialogu konfiguracji wystepowania profilu (ScheduleConfigDialog).
// Umozliwia wybor: "Blocked at all times" vs "Scheduled in calendar" (z powtorzeniami co 7 dni).
// Laczy sie z ProfileManager i CalendarEngine.

#ifndef SCHEDULE_CONFIG_DIALOG_H
#define SCHEDULE_CONFIG_DIALOG_H

#include <QDialog>
#include <QRadioButton>
#include <QTimeEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include "profile_manager.h"
#include "calendar_engine.h"

class ScheduleConfigDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ScheduleConfigDialog(const BlockProfile &profile, CalendarEngine *calendarEngine, QWidget *parent = nullptr);
    ~ScheduleConfigDialog() override = default;

    ScheduleType getSelectedScheduleType() const;

private slots:
    void onRadioToggled();
    void onSaveClicked();

private:
    void setupUi();

    BlockProfile m_profile;
    CalendarEngine *m_calendarEngine;

    QRadioButton *m_rbAllTimes;
    QRadioButton *m_rbCalendar;

    QWidget *m_calendarSettingsBox;
    QTimeEdit *m_timeStart;
    QTimeEdit *m_timeEnd;
    QSpinBox *m_spinWeeks;
    QList<QCheckBox*> m_dayCheckboxes;
};

#endif // SCHEDULE_CONFIG_DIALOG_H