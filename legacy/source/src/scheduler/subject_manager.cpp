// Plik implementacji zarzadcy przedmiotow SubjectManager.
// Wymusza sztywne kolory dla typu zajec: #0b8043 (laboratoria), #d50000 (kolokwia/egzaminy),
// #f6bf26 (cwiczenia), #4285f4 (wyklady). Scalanie kolokwium sprawia, ze caly kafelek staje sie kolokwium.
// Obsluguje pelny cykl edycji zajec, notatek, zmiany nazw i czyszczenia bazy.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "subject_manager.h"
#include "calendar_engine.h"
#include "overdue_tracker.h"
#include "database_manager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QMap>
#include <QSqlQuery>

// 1. Normalizacja typu zajec
static QString normalizeClassType(const QString &rawType)
{
    QString t = rawType.trimmed().toLower();
    if (t.contains("wykład") || t == "lecture") return "lecture";
    if (t.contains("lab") || t == "laboratory") return "lab";
    if (t.contains("ćwicz") || t == "exercise") return "exercise";
    if (t.contains("egzamin") || t.contains("kolo") || t == "exam") return "exam";
    return "lecture";
}

// 2. Sztywne przypisanie kolorow wedlug typu i nazwy zajec
static QString resolveClassColor(const QString &type, const QString &label)
{
    QString t = type.trimmed().toLower();
    QString lbl = label.trimmed().toLower();

    // Kolokwium lub egzamin ma bezwzgledny priorytet (kafelek staje sie w calosci czerwony)
    if (t == "exam" || lbl.contains("kolo") || lbl.contains("egzamin") || lbl.contains("test")) {
        return "#d50000"; // Kolokwium / Egzamin: czerwony
    }
    if (t == "lab" || lbl.contains("lab")) {
        return "#0b8043"; // Laboratoria: zielony
    }
    if (t == "exercise" || lbl.contains("ćwicz") || lbl.contains("cwicz") || lbl.contains("powtórka") || lbl.contains("powtorka")) {
        return "#f6bf26"; // Cwiczenia: zolty
    }
    return "#4285f4"; // Wyklady: niebieski
}

// 3. Parsowanie pojedynczego obiektu z JSON z unikalnym ID i scalaniem kolokwium
static SubjectClassItem parseClassItemJson(const QJsonObject &obj, const QString &subjectName)
{
    SubjectClassItem item;
    item.label = obj.value("name").toString();
    if (item.label.isEmpty()) {
        item.label = obj.value("label").toString();
    }

    QString rawType = obj.value("type").toString();
    QString lblLower = item.label.toLower();

    if (lblLower.contains("kolo") || lblLower.contains("egzamin") || rawType.toLower() == "exam") {
        item.type = "exam";
    } else {
        item.type = normalizeClassType(rawType);
    }

    item.index = obj.value("index").toInt(1);
    QString startStr = obj.value("start").toString();
    QString endStr = obj.value("end").toString();

    item.start = QDateTime::fromString(startStr, Qt::ISODate);
    item.end = QDateTime::fromString(endStr, Qt::ISODate);

    if (item.label.isEmpty()) {
        if (item.type == "lecture") item.label = QString("Wykład %1").arg(item.index);
        else if (item.type == "lab") item.label = QString("Lab %1").arg(item.index);
        else if (item.type == "exercise") item.label = QString("Ćwiczenia %1").arg(item.index);
        else if (item.type == "exam") item.label = "Kolokwium";
        else item.label = QString("Zajęcia %1").arg(item.index);
    }

    QString timeTag = item.start.isValid() ? item.start.toString("yyyyMMdd_hhmm") : QString::number(item.index);
    item.id = QString("sub_%1_%2_%3_%4").arg(subjectName, item.type, QString::number(item.index), timeTag);
    return item;
}

// 4. Trwaly zapis przedmiotu wraz z notatkami w SQLite
void SubjectManager::persistSubjectToDb(const AcademicSubject &sub)
{
    DatabaseManager::instance().saveSubjectRecord(sub.name, sub.color, sub.isLecture, sub.notes);
    DatabaseManager::instance().clearSubjectClasses(sub.name);

    for (const auto &c : sub.classes) {
        DatabaseManager::instance().saveSubjectClassRecord(
            c.id, sub.name, c.type, c.index,
            c.start.toString(Qt::ISODate),
            c.end.toString(Qt::ISODate),
            c.label
        );
    }
}

// 5. Sekwencyjne wczytanie przedmiotow, terminow i notatek z SQLite
void SubjectManager::loadFromDatabase()
{
    m_subjects.clear();

    struct TempSubjectRecord {
        QString name;
        QString color;
        bool isLecture;
        QString notes;
    };
    QList<TempSubjectRecord> loadedSubjects;

    {
        QSqlQuery qSub = DatabaseManager::instance().executeQuery("SELECT name, color, is_lecture, notes FROM subjects;");
        while (qSub.next()) {
            loadedSubjects.append({
                qSub.value(0).toString(),
                qSub.value(1).toString(),
                qSub.value(2).toInt() == 1,
                qSub.value(3).toString()
            });
        }
        qSub.finish();
    }

    QMap<QString, QList<SubjectClassItem>> classesMap;
    {
        QSqlQuery qCls = DatabaseManager::instance().executeQuery(
            "SELECT id, subject_name, class_type, class_index, start_time, end_time, label "
            "FROM subject_classes ORDER BY class_index ASC;"
        );
        while (qCls.next()) {
            QString sname = qCls.value(1).toString();
            SubjectClassItem item;
            item.id = qCls.value(0).toString();
            item.type = qCls.value(2).toString();
            item.index = qCls.value(3).toInt();
            item.start = QDateTime::fromString(qCls.value(4).toString(), Qt::ISODate);
            item.end = QDateTime::fromString(qCls.value(5).toString(), Qt::ISODate);
            item.label = qCls.value(6).toString();
            classesMap[sname].append(item);
        }
        qCls.finish();
    }

    for (const auto &rec : loadedSubjects) {
        AcademicSubject sub;
        sub.name = rec.name;
        sub.color = rec.color;
        sub.isLecture = rec.isLecture;
        sub.notes = rec.notes;
        sub.classes = classesMap.value(rec.name);
        m_subjects.append(sub);
    }

    emit subjectsUpdated();
}

// 6. Wyszukiwanie przedmiotu
const AcademicSubject *SubjectManager::findSubject(const QString &name) const
{
    for (const auto &sub : m_subjects) {
        if (sub.name.compare(name, Qt::CaseInsensitive) == 0) return &sub;
    }
    return nullptr;
}

// 7. Pobranie wszystkich przedmiotow
const QList<AcademicSubject> &SubjectManager::getSubjects() const
{
    return m_subjects;
}

// 8. Aktualizacja notatek i linkow dla danego przedmiotu
void SubjectManager::updateSubjectNotes(const QString &subjectName, const QString &notes)
{
    for (auto &sub : m_subjects) {
        if (sub.name.compare(subjectName, Qt::CaseInsensitive) == 0) {
            sub.notes = notes;
            DatabaseManager::instance().saveSubjectRecord(sub.name, sub.color, sub.isLecture, sub.notes);
            return;
        }
    }
}

// 9. Aktualizacja szczegolow pojedynczych zajec
void SubjectManager::updateClassDetails(const QString &subjectName, const QString &classId, const QString &newLabel, const QDateTime &newStart, const QDateTime &newEnd)
{
    for (auto &sub : m_subjects) {
        if (sub.name.compare(subjectName, Qt::CaseInsensitive) == 0) {
            for (auto &c : sub.classes) {
                if (c.id == classId) {
                    c.label = newLabel;
                    c.start = newStart;
                    c.end = newEnd;

                    QString lblLower = newLabel.toLower();
                    if (lblLower.contains("kolo") || lblLower.contains("egzamin") || lblLower.contains("test")) {
                        c.type = "exam";
                    }

                    DatabaseManager::instance().saveSubjectClassRecord(
                        c.id, sub.name, c.type, c.index,
                        c.start.toString(Qt::ISODate),
                        c.end.toString(Qt::ISODate),
                        c.label
                    );

                    if (m_calendarEngine) {
                        CalendarEvent ev;
                        ev.id = c.id;
                        ev.seriesId = "";
                        ev.title = QString("[%1] %2").arg(sub.name, c.label);
                        ev.category = (c.type == "exam") ? "Event" : "Automatyczne";
                        ev.color = resolveClassColor(c.type, c.label);
                        ev.start = c.start;
                        ev.end = c.end;
                        ev.linkedProfile = "";
                        m_calendarEngine->updateEvent(ev);
                    }
                    break;
                }
            }
            break;
        }
    }

    if (m_overdueTracker) {
        m_overdueTracker->syncFromSubjectManager(m_subjects);
    }
    emit subjectsUpdated();
}

// 10. Usuniecie pojedynczych zajec z przedmiotu
void SubjectManager::removeClassFromSubject(const QString &subjectName, const QString &classId)
{
    for (auto &sub : m_subjects) {
        if (sub.name.compare(subjectName, Qt::CaseInsensitive) == 0) {
            sub.classes.removeIf([&classId](const SubjectClassItem &c) {
                return c.id == classId;
            });

            DatabaseManager::instance().deleteSubjectClassRecord(classId);

            if (m_calendarEngine) {
                m_calendarEngine->removeEvent(classId);
            }

            syncAllToCalendarAndTracker();
            emit subjectsUpdated();
            return;
        }
    }
}

// 11. Usuniecie calego przedmiotu
void SubjectManager::removeSubject(const QString &name)
{
    if (m_calendarEngine) {
        m_calendarEngine->removeEventsForSubject(name);
    }
    m_subjects.removeIf([&name](const AcademicSubject &s) {
        return s.name.compare(name, Qt::CaseInsensitive) == 0;
    });
    DatabaseManager::instance().deleteSubjectRecord(name);
    syncAllToCalendarAndTracker();
    emit subjectsUpdated();
}

// 12. Zmiana nazwy przedmiotu z zachowaniem terminow i powiazan
void SubjectManager::renameSubject(const QString &oldName, const QString &newName)
{
    if (oldName.compare(newName, Qt::CaseInsensitive) == 0 || newName.trimmed().isEmpty()) return;

    for (auto &sub : m_subjects) {
        if (sub.name.compare(oldName, Qt::CaseInsensitive) == 0) {
            DatabaseManager::instance().deleteSubjectRecord(oldName);
            if (m_calendarEngine) {
                m_calendarEngine->removeEventsForSubject(oldName);
            }
            if (m_overdueTracker) {
                m_overdueTracker->renameSubject(oldName, newName);
            }

            sub.name = newName.trimmed();
            for (auto &c : sub.classes) {
                QString timeTag = c.start.isValid() ? c.start.toString("yyyyMMdd_hhmm") : QString::number(c.index);
                c.id = QString("sub_%1_%2_%3_%4").arg(sub.name, c.type, QString::number(c.index), timeTag);
            }
            persistSubjectToDb(sub);
            break;
        }
    }

    syncAllToCalendarAndTracker();
    emit subjectsUpdated();
}

// 13. Calkowite usuniecie wszystkich przedmiotow i ich terminow
void SubjectManager::clearAllSubjects()
{
    for (const auto &sub : m_subjects) {
        DatabaseManager::instance().deleteSubjectRecord(sub.name);
        if (m_calendarEngine) {
            m_calendarEngine->removeEventsForSubject(sub.name);
        }
    }
    m_subjects.clear();
    syncAllToCalendarAndTracker();
    emit subjectsUpdated();
}

// 14. Dodanie lub aktualizacja przedmiotu
void SubjectManager::addOrUpdateSubject(const AcademicSubject &subject)
{
    bool found = false;
    for (auto &s : m_subjects) {
        if (s.name.compare(subject.name, Qt::CaseInsensitive) == 0) {
            s = subject;
            found = true;
            break;
        }
    }
    if (!found) {
        m_subjects.append(subject);
    }

    persistSubjectToDb(subject);
}

// 15. Pelna synchronizacja z kalendarzem z wymuszeniem sztywnych kolorow
void SubjectManager::syncAllToCalendarAndTracker()
{
    if (m_calendarEngine) {
        m_calendarEngine->clearEventsByCategory("Automatyczne");

        for (const auto &sub : m_subjects) {
            for (const auto &c : sub.classes) {
                if (!c.start.isValid()) continue;

                CalendarEvent ev;
                ev.id = c.id;
                ev.seriesId = "";
                ev.title = QString("[%1] %2").arg(sub.name, c.label);
                ev.category = (c.type == "exam") ? "Event" : "Automatyczne";
                ev.color = resolveClassColor(c.type, c.label);
                ev.start = c.start;
                ev.end = c.end;
                ev.linkedProfile = "";

                m_calendarEngine->addEvent(ev);
            }
        }
    }

    if (m_overdueTracker) {
        m_overdueTracker->syncFromSubjectManager(m_subjects);
    }
}

// 16. Import z pliku JSON
bool SubjectManager::importFromJsonFile(const QString &filePath, QString &outError)
{
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        outError = "Nie można otworzyć pliku: " + filePath;
        return false;
    }
    QByteArray data = f.readAll();
    f.close();
    return importFromJsonString(QString::fromUtf8(data), outError);
}

// 17. Import z ciagu JSON
bool SubjectManager::importFromJsonString(const QString &jsonContent, QString &outError)
{
    QJsonParseError parseErr;
    QJsonDocument doc = QJsonDocument::fromJson(jsonContent.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError) {
        outError = "Błąd parsowania JSON: " + parseErr.errorString();
        return false;
    }

    if (!doc.isObject()) {
        outError = "Główny element JSON musi zawierać obiekt 'subjects'.";
        return false;
    }

    QJsonArray subArray = doc.object().value("subjects").toArray();
    if (subArray.isEmpty()) {
        outError = "Tablica 'subjects' jest pusta.";
        return false;
    }

    for (const auto &subVal : subArray) {
        QJsonObject subObj = subVal.toObject();
        AcademicSubject sub;
        sub.name = subObj.value("name").toString().trimmed();
        if (sub.name.isEmpty()) continue;

        sub.color = "#4285f4";

        bool hasLabs = false;
        QJsonArray classesArray = subObj.value("classes").toArray();
        for (const auto &cVal : classesArray) {
            SubjectClassItem item = parseClassItemJson(cVal.toObject(), sub.name);
            if (item.type == "lab" || item.type == "exercise") {
                hasLabs = true;
            }
            sub.classes.append(item);
        }

        sub.isLecture = !hasLabs;
        addOrUpdateSubject(sub);
    }

    syncAllToCalendarAndTracker();
    emit subjectsUpdated();
    return true;
}

// 18. Konstruktor SubjectManager
SubjectManager::SubjectManager(CalendarEngine *calendarEngine, OverdueTracker *overdueTracker, QObject *parent)
    : QObject(parent)
    , m_calendarEngine(calendarEngine)
    , m_overdueTracker(overdueTracker)
{
    loadFromDatabase();
    syncAllToCalendarAndTracker();
}
