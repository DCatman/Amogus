// Plik naglowkowy okna ChooseOverduesDialog.
// Prezentuje kalkulator tempa (ile blokow dziennie przerabiac przed kolokwium),
// liste oczekujacych zadan oraz przycisk 🔒 Zablokuj plan (Lock).
// Laczy sie z OverdueTracker i CalendarEngine.

#ifndef CHOOSE_OVERDUES_DIALOG_H
#define CHOOSE_OVERDUES_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include "overdue_tracker.h"
#include "calendar_engine.h"

class ChooseOverduesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ChooseOverduesDialog(OverdueTracker *tracker, CalendarEngine *calendarEngine, QWidget *parent = nullptr);
    ~ChooseOverduesDialog() override = default;

private slots:
    void onScheduleSelectedClicked();
    void onLockPlanClicked();
    void onForceAutoLockClicked();

private:
    void setupUi();
    void refreshTaskList();

    OverdueTracker *m_tracker;
    CalendarEngine *m_calendarEngine;

    QLabel *m_pacingBanner;
    QTableWidget *m_taskTable;
    QPushButton *m_btnScheduleSelected;
    QPushButton *m_btnLockPlan;
    QPushButton *m_btnForceAutoLock;

    QList<OverdueTaskItem> m_taskList;
};

#endif // CHOOSE_OVERDUES_DIALOG_H
