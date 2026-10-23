// Plik implementacji silnika kalendarza CalendarEngine.
// Obsluguje zapisywanie i odczyt pola linkedSubject w wydarzeniach kalendarza.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "calendar_engine.h"
#include "database_manager.h"
#include <QSqlQuery>
#include <QUuid>
#include <QSet>
#include <QMap>

// 1. Normalizacja koloru dla zdarzen akademickich
static QString normalizeEventColor(const QString &category, const QString &title, const QString &existingColor)
{
    QString lower = title.toLower();
    if (lower.contains("kolo") || lower.contains("egzamin") || lower.contains("test")) {
        return "#d50000";
    }
    if (lower.contains("lab")) {
        return "#0b8043";
    }
    if (lower.contains("ćwicz") || lower.contains("cwicz") || lower.contains("powtórka") || lower.contains("powtorka")) {
        return "#f6bf26";
    }
    if (category == "Automatyczne" || lower.contains("wykład") || lower.contains("wyklad")) {
        return "#4285f4";
    }
    return existingColor.isEmpty() ? "#4285f4" : existingColor;
}

// 2. Generowanie sygnatury do porownywania tozsamosci wydarzen
static QString createEventSignature(const CalendarEvent &ev)
{
    return QString("%1_%2_%3")
        .arg(ev.title.trimmed().toLower(),
             ev.start.toString(Qt::ISODate),
             ev.end.toString(Qt::ISODate));
}

// 3. Sprawdzenie czy w biezacej chwili trwa wydarzenie dla danego profilu
bool CalendarEngine::isAnyEventActiveForProfile(const QString &profileName) const
{
    if (profileName.isEmpty()) return false;
    QDateTime now = QDateTime::currentDateTime();
    for (const auto &ev : m_events) {
        if (ev.linkedProfile.compare(profileName, Qt::CaseInsensitive) == 0) {
            if (now >= ev.start && now <= ev.end) {
                return true;
            }
        }
    }
    return false;
}

// 4. Usuwanie duplikatow z pamieci i z bazy SQLite
int CalendarEngine::deduplicateLocalEvents()
{
    QSet<QString> seenSignatures;
    QList<QString> idsToDelete;
    QList<CalendarEvent> cleanList;

    for (const auto &ev : m_events) {
        QString sig = createEventSignature(ev);
        if (seenSignatures.contains(sig)) {
            idsToDelete.append(ev.id);
        } else {
            seenSignatures.insert(sig);
            cleanList.append(ev);
        }
    }

    for (const auto &id : idsToDelete) {
        DatabaseManager::instance().deleteCalendarEvent(id);
    }

    int removedCount = idsToDelete.size();
    if (removedCount > 0) {
        m_events = cleanList;
        emit eventsUpdated();
    }
    return removedCount;
}

// 5. Ladowanie wszystkich zdarzen z bazy SQLite (wraz z polem linkedSubject)
void CalendarEngine::loadFromDatabase()
{
    m_events.clear();
    QSqlQuery q = DatabaseManager::instance().executeQuery(
        "SELECT id, series_id, title, category, start_time, end_time, profile_name, color, google_id, updated_at, linked_subject "
        "FROM calendar_events;"
    );

    QSet<QString> seenSignatures;
    QList<QString> duplicateIdsToDelete;

    while (q.next()) {
        CalendarEvent ev;
        ev.id = q.value(0).toString();
        ev.seriesId = q.value(1).toString();
        ev.title = q.value(2).toString();
        ev.category = q.value(3).toString();
        ev.start = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
        ev.end = QDateTime::fromString(q.value(5).toString(), Qt::ISODate);
        ev.linkedProfile = q.value(6).toString();
        ev.color = normalizeEventColor(ev.category, ev.title, q.value(7).toString());
        ev.googleId = q.value(8).toString();
        ev.updatedAt = q.value(9).toLongLong();
        ev.linkedSubject = q.value(10).toString();

        QString sig = createEventSignature(ev);
        if (seenSignatures.contains(sig)) {
            duplicateIdsToDelete.append(ev.id);
            continue;
        }

        seenSignatures.insert(sig);
        m_events.append(ev);
    }
    q.finish();

    for (const auto &dupId : duplicateIdsToDelete) {
        DatabaseManager::instance().deleteCalendarEvent(dupId);
    }

    emit eventsUpdated();
    checkSchedules();
}

// 6. Cykliczne sprawdzanie terminow w celu aktywacji profili
void CalendarEngine::checkSchedules()
{
    QDateTime now = QDateTime::currentDateTime();
    QMap<QString, bool> activeMap;

    for (const auto &ev : m_events) {
        if (!ev.linkedProfile.isEmpty()) {
            if (!activeMap.contains(ev.linkedProfile)) {
                activeMap[ev.linkedProfile] = false;
            }
            if (now >= ev.start && now <= ev.end) {
                activeMap[ev.linkedProfile] = true;
            }
        }
    }

    if (!activeMap.contains("overdue")) {
        activeMap["overdue"] = false;
    }

    for (auto it = activeMap.begin(); it != activeMap.end(); ++it) {
        emit calendarWindowActiveChanged(it.key(), it.value());
        emit triggerProfileActivation(it.key(), it.value());
    }
}

// 7. Calkowite czyszczenie wszystkich zdarzen
void CalendarEngine::clearAllEvents()
{
    for (const auto &ev : m_events) {
        DatabaseManager::instance().deleteCalendarEvent(ev.id);
    }
    m_events.clear();
    emit eventsUpdated();
    checkSchedules();
}

// 8. Usuwanie zdarzen o danej kategorii
void CalendarEngine::clearEventsByCategory(const QString &category)
{
    QList<QString> toDelete;
    for (const auto &ev : m_events) {
        if (ev.category == category) {
            toDelete.append(ev.id);
        }
    }

    for (const auto &id : toDelete) {
        removeEvent(id);
    }
}

// 9. Usuwanie zdarzen powiazanych z nazwa przedmiotu
void CalendarEngine::removeEventsForSubject(const QString &subjectName)
{
    QString prefix = QString("[%1]").arg(subjectName);
    QList<QString> toDelete;

    for (const auto &ev : m_events) {
        if (ev.title.startsWith(prefix, Qt::CaseInsensitive) || ev.id.contains(subjectName, Qt::CaseInsensitive) ||
            ev.linkedSubject.compare(subjectName, Qt::CaseInsensitive) == 0) {
            toDelete.append(ev.id);
        }
    }

    for (const auto &id : toDelete) {
        removeEvent(id);
    }
}

// 10. Usuwanie serii powtorzen
void CalendarEngine::removeSeries(const QString &seriesId)
{
    if (seriesId.isEmpty()) return;
    m_events.removeIf([&seriesId](const CalendarEvent &e) { return e.seriesId == seriesId; });
    DatabaseManager::instance().deleteCalendarSeries(seriesId);
    emit eventsUpdated();
    checkSchedules();
}

// 11. Usuwanie pojedynczego zdarzenia
void CalendarEngine::removeEvent(const QString &id)
{
    QString googleId;
    for (const auto &e : m_events) {
        if (e.id == id) {
            googleId = e.googleId;
            break;
        }
    }

    m_events.removeIf([&id](const CalendarEvent &e) { return e.id == id; });
    DatabaseManager::instance().deleteCalendarEvent(id);

    if (!m_isBatchUpdating) {
        emit eventDeletedLocally(id, googleId);
    }
    emit eventsUpdated();
    checkSchedules();
}

// 12. Aktualizacja istniejacego zdarzenia
void CalendarEngine::updateEvent(const CalendarEvent &event)
{
    for (auto &ev : m_events) {
        if (ev.id == event.id) {
            ev = event;
            ev.color = normalizeEventColor(ev.category, ev.title, ev.color);
            if (ev.updatedAt == 0) {
                ev.updatedAt = QDateTime::currentMSecsSinceEpoch();
            }
            DatabaseManager::instance().saveCalendarEvent(ev.id, ev.seriesId, ev.title, ev.category,
                                                          ev.start.toString(Qt::ISODate),
                                                          ev.end.toString(Qt::ISODate),
                                                          ev.linkedProfile, ev.color,
                                                          ev.googleId, ev.updatedAt, ev.linkedSubject);
            if (!m_isBatchUpdating) {
                emit eventMutatedLocally(ev);
            }
            break;
        }
    }
    emit eventsUpdated();
    checkSchedules();
}

// 13. Dodanie zdarzenia
void CalendarEngine::addEvent(const CalendarEvent &event)
{
    CalendarEvent ev = event;
    if (ev.id.isEmpty()) {
        ev.id = "ev_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    ev.color = normalizeEventColor(ev.category, ev.title, ev.color);
    if (ev.updatedAt == 0) {
        ev.updatedAt = QDateTime::currentMSecsSinceEpoch();
    }

    for (auto &existing : m_events) {
        if (existing.id == ev.id) {
            existing = ev;
            DatabaseManager::instance().saveCalendarEvent(ev.id, ev.seriesId, ev.title, ev.category,
                                                          ev.start.toString(Qt::ISODate),
                                                          ev.end.toString(Qt::ISODate),
                                                          ev.linkedProfile, ev.color,
                                                          ev.googleId, ev.updatedAt, ev.linkedSubject);
            if (!m_isBatchUpdating) {
                emit eventMutatedLocally(existing);
            }
            emit eventsUpdated();
            checkSchedules();
            return;
        }
    }

    m_events.append(ev);
    DatabaseManager::instance().saveCalendarEvent(ev.id, ev.seriesId, ev.title, ev.category,
                                                  ev.start.toString(Qt::ISODate),
                                                  ev.end.toString(Qt::ISODate),
                                                  ev.linkedProfile, ev.color,
                                                  ev.googleId, ev.updatedAt, ev.linkedSubject);

    if (!m_isBatchUpdating) {
        emit eventMutatedLocally(ev);
    }
    emit eventsUpdated();
    checkSchedules();
}

// 14. Harmonogram cykliczny dla profilu blokowania
void CalendarEngine::scheduleRecurringProfile(const QString &profileName, const QTime &startTime, const QTime &endTime,
                                              const QList<int> &daysOfWeek, int occurrencesCount)
{
    QString seriesId = "series_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
    QDate curDate = QDate::currentDate();
    int count = 0;

    setBatchUpdating(true);
    while (count < occurrencesCount) {
        if (daysOfWeek.contains(curDate.dayOfWeek())) {
            CalendarEvent ev;
            ev.id = "ev_" + QUuid::createUuid().toString(QUuid::WithoutBraces);
            ev.seriesId = seriesId;
            ev.title = profileName + " [Harmonogram]";
            ev.category = "Profile blokowania";
            ev.color = "#5c6bc0";
            ev.start = QDateTime(curDate, startTime);
            ev.end = QDateTime(curDate, endTime);
            ev.linkedProfile = profileName;
            ev.linkedSubject = "";
            ev.updatedAt = QDateTime::currentMSecsSinceEpoch();

            m_events.append(ev);
            DatabaseManager::instance().saveCalendarEvent(ev.id, ev.seriesId, ev.title, ev.category,
                                                          ev.start.toString(Qt::ISODate),
                                                          ev.end.toString(Qt::ISODate),
                                                          ev.linkedProfile, ev.color,
                                                          ev.googleId, ev.updatedAt, ev.linkedSubject);
            count++;
        }
        curDate = curDate.addDays(1);
    }
    setBatchUpdating(false);
    emit eventsUpdated();
    checkSchedules();
}

// 15. Zdalna synchronizacja Google Calendar
void CalendarEngine::applyRemoteSync(const QList<CalendarEvent> &remoteEvents, const QStringList &deletedGoogleIds)
{
    for (const auto &delGId : deletedGoogleIds) {
        if (delGId.isEmpty()) continue;
        for (int i = m_events.size() - 1; i >= 0; --i) {
            if (m_events[i].googleId == delGId) {
                DatabaseManager::instance().deleteCalendarEvent(m_events[i].id);
                m_events.removeAt(i);
            }
        }
    }

    for (const auto &rev : remoteEvents) {
        bool found = false;
        for (auto &local : m_events) {
            if ((!rev.googleId.isEmpty() && local.googleId == rev.googleId) || (local.id == rev.id)) {
                local.title = rev.title;
                local.start = rev.start;
                local.end = rev.end;
                local.category = rev.category;
                local.color = normalizeEventColor(local.category, local.title, rev.color);
                local.googleId = rev.googleId;
                local.updatedAt = rev.updatedAt;

                DatabaseManager::instance().saveCalendarEvent(local.id, local.seriesId, local.title, local.category,
                                                              local.start.toString(Qt::ISODate),
                                                              local.end.toString(Qt::ISODate),
                                                              local.linkedProfile, local.color,
                                                              local.googleId, local.updatedAt, local.linkedSubject);
                found = true;
                break;
            }
        }

        if (!found) {
            CalendarEvent newEv = rev;
            if (newEv.id.isEmpty()) {
                newEv.id = "ev_gcal_" + (newEv.googleId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : newEv.googleId);
            }
            newEv.color = normalizeEventColor(newEv.category, newEv.title, newEv.color);
            m_events.append(newEv);

            DatabaseManager::instance().saveCalendarEvent(newEv.id, newEv.seriesId, newEv.title, newEv.category,
                                                          newEv.start.toString(Qt::ISODate),
                                                          newEv.end.toString(Qt::ISODate),
                                                          newEv.linkedProfile, newEv.color,
                                                          newEv.googleId, newEv.updatedAt, newEv.linkedSubject);
        }
    }

    deduplicateLocalEvents();
    emit eventsUpdated();
    checkSchedules();
}

// 16. Sterowanie trybem wsadowym
void CalendarEngine::setBatchUpdating(bool batch)
{
    if (m_isBatchUpdating != batch) {
        m_isBatchUpdating = batch;
        if (!m_isBatchUpdating) {
            emit batchCompletedLocally();
        }
    }
}

// 17. Getter trybu wsadowego
bool CalendarEngine::isBatchUpdating() const
{
    return m_isBatchUpdating;
}

// 18. Pobranie wszystkich zdarzen
const QList<CalendarEvent> &CalendarEngine::getEvents() const
{
    return m_events;
}

// 19. Konstruktor CalendarEngine
CalendarEngine::CalendarEngine(QObject *parent)
    : QObject(parent)
    , m_scheduleCheckTimer(new QTimer(this))
    , m_isBatchUpdating(false)
{
    loadFromDatabase();
    connect(m_scheduleCheckTimer, &QTimer::timeout, this, &CalendarEngine::checkSchedules);
    m_scheduleCheckTimer->start(2000);
}
