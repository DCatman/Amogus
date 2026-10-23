// Plik naglowkowy prostego widoku kalendarza (CalendarWidget).
// Umozliwia dodawanie zadan przez wybor czasu i kategorii oraz wyswietla zaimportowane sesje.
// Laczy sie z CalendarEngine oraz GoogleCalendarSync.

#ifndef CALENDAR_WIDGET_H
#define CALENDAR_WIDGET_H

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QDateTimeEdit>
#include "calendar_engine.h"

class CalendarWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarWidget(CalendarEngine *engine, QWidget *parent = nullptr);
    ~CalendarWidget() override = default;

public slots:
    void refreshEventsList();

private slots:
    void onAddEventClicked();

private:
    void setupUi();

    CalendarEngine *m_engine;
    QTableWidget *m_table;
    QLineEdit *m_titleEdit;
    QComboBox *m_categoryBox;
    QDateTimeEdit *m_startEdit;
    QDateTimeEdit *m_endEdit;
    QPushButton *m_addButton;
};

#endif // CALENDAR_WIDGET_H