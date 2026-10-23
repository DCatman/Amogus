// Plik naglowkowy modulu UsosScheduleLoader.
// Wczytuje plan zajec akademickich z usos_schedule.json oraz exams.json do CalendarEngine
// z obsluga flagi is_skipped (mozliwosc pomijania wykladow bez uruchamiania blokady).
// Laczy sie z CalendarEngine.

#ifndef USOS_SCHEDULE_LOADER_H
#define USOS_SCHEDULE_LOADER_H

#include <QObject>
#include <QString>
#include "calendar_engine.h"

class UsosScheduleLoader : public QObject
{
    Q_OBJECT

public:
    explicit UsosScheduleLoader(CalendarEngine *engine, QObject *parent = nullptr);
    ~UsosScheduleLoader() override = default;

public slots:
    bool loadUsosSchedule();
    bool loadExamsSchedule();
    void toggleSkipClass(const QString &eventId, bool skipped);

private:
    CalendarEngine *m_engine;
};

#endif // USOS_SCHEDULE_LOADER_H