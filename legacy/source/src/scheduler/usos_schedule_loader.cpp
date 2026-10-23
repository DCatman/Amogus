// Plik implementacji modulu UsosScheduleLoader.
// Parsuje pliki JSON planu lekcji i egzaminow oraz wstrzykuje je do kalendarza z flaga is_skipped.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "usos_schedule_loader.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

// 1. Przelaczenie statusu pominiecia zajec (is_skipped)
void UsosScheduleLoader::toggleSkipClass(const QString &eventId, bool skipped)
{
    const auto &events = m_engine->getEvents();
    for (const auto &ev : events) {
        if (ev.id == eventId) {
            CalendarEvent updated = ev;
            updated.linkedProfile = skipped ? "" : "overdue"; // Pominiecie usuwa powiazanie z blokada
            m_engine->updateEvent(updated);
            break;
        }
    }
}

// 2. Ladowanie egzaminow z exams.json
bool UsosScheduleLoader::loadExamsSchedule()
{
    QFile f("Library/exams.json");
    if (!f.exists()) f.setFileName("exams.json");
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return false;

    QJsonArray entries = doc.object().value("entries").toArray();
    for (const auto &v : entries) {
        QJsonObject o = v.toObject();
        QString dStr = o.value("date").toString().replace(".", "-");
        QString tr = o.value("time_range").toString();

        QString stStr = "08:00";
        QString etStr = "10:00";
        QRegularExpression rx("(\\d{2}:\\d{2})");
        auto it = rx.globalMatch(tr);
        if (it.hasNext()) stStr = it.next().captured(1);
        if (it.hasNext()) etStr = it.next().captured(1);

        CalendarEvent ev;
        ev.id = "exam_" + dStr + "_" + stStr;
        ev.title = "EGZAMIN: " + o.value("course").toString();
        ev.category = "Event";
        ev.start = QDateTime::fromString(dStr + "T" + stStr + ":00", Qt::ISODate);
        ev.end = QDateTime::fromString(dStr + "T" + etStr + ":00", Qt::ISODate);
        ev.linkedProfile = "";

        if (ev.start.isValid()) {
            m_engine->addEvent(ev);
        }
    }
    return true;
}

// 3. Ladowanie planu z usos_schedule.json
bool UsosScheduleLoader::loadUsosSchedule()
{
    QFile f("Library/usos_schedule.json");
    if (!f.exists()) f.setFileName("usos_schedule.json");
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return false;

    QJsonArray entries = doc.object().value("entries").toArray();
    for (const auto &v : entries) {
        QJsonObject o = v.toObject();
        QString dt = o.value("datetime").toString();
        if (dt.length() < 22) continue;

        QString dStr = dt.left(10);
        QString stStr = dt.mid(11, 5);
        QString etStr = dt.mid(17, 5);

        CalendarEvent ev;
        ev.id = "usos_" + dStr + "_" + stStr;
        ev.title = QString("[%1] %2").arg(o.value("type").toString(), o.value("name").toString());
        ev.category = "Automatyczne";
        ev.start = QDateTime::fromString(dStr + "T" + stStr + ":00", Qt::ISODate);
        ev.end = QDateTime::fromString(dStr + "T" + etStr + ":00", Qt::ISODate);
        ev.linkedProfile = o.value("is_skipped").toBool(false) ? "" : "overdue";

        if (ev.start.isValid()) {
            m_engine->addEvent(ev);
        }
    }
    return true;
}

// 4. Konstruktor
UsosScheduleLoader::UsosScheduleLoader(CalendarEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    loadUsosSchedule();
    loadExamsSchedule();
}