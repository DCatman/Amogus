// Plik naglowkowy ProfileCalendarDialog.
// Prezentuje 24-godzinny kalendarz tygodniowy dedykowany dla konkretnego profilu blokowania.
// Wszystkie tworzone wydarzenia sa automatycznie wiazane z tym profilem.
// Usuwanie wydarzen klawiszem Delete jest zablokowane, gdy profil jest w stanie Locked.
// Laczy sie z CalendarEngine, CalendarCanvas i ProfileManager.

#ifndef PROFILE_CALENDAR_DIALOG_H
#define PROFILE_CALENDAR_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include "profile_manager.h"
#include "calendar_engine.h"
#include "calendar_canvas.h"

class ProfileCalendarDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProfileCalendarDialog(const BlockProfile &profile, CalendarEngine *calendarEngine,
                                   ProfileManager *profileManager, QWidget *parent = nullptr);
    ~ProfileCalendarDialog() override = default;

private slots:
    void onPrevWeekClicked();
    void onNextWeekClicked();
    void onTodayClicked();

private:
    void setupUi();

    BlockProfile m_profile;
    CalendarEngine *m_calendarEngine;
    ProfileManager *m_profileManager;

    CalendarCanvas *m_canvas;
    QLabel *m_periodLabel;
};

#endif // PROFILE_CALENDAR_DIALOG_H