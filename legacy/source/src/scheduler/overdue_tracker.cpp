// Plik implementacji silnika OverdueTracker.
// Implementuje bezposrednia modyfikacje etapu komorki oraz zarzadza cyklem ICS.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "overdue_tracker.h"
#include "calendar_engine.h"
#include "subject_manager.h"
#include "database_manager.h"
#include <windows.h>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <algorithm>

// 1. Sprawdzanie bezczynnosci WinAPI
static DWORD getSystemIdleTimeMsWinApi()
{
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (GetLastInputInfo(&lii)) {
        DWORD currentTick = GetTickCount();
        return (currentTick - lii.dwTime);
    }
    return 0;
}

// 2. Czas trwania poszczegolnych etapow cyklu ICS
int OverdueTracker::calculateStageDuration(const QString &trackType, int stage) const
{
    if (trackType == "lecture") {
        if (stage == 0) return 3;
        if (stage == 1) return 2;
        if (stage == 3) return 1;
        if (stage == 6) return 2;
        if (stage == 12) return 1;
    } else {
        if (stage == -6) return 3;
        if (stage == -3) return 1;
        if (stage == -2) return 1;
        if (stage == -1) return 2;
    }
    return 2;
}

// 3. Przejscie do kolejnego etapu
int OverdueTracker::getNextStage(const QString &trackType, int currentStage) const
{
    if (trackType == "lecture") {
        if (currentStage == 0) return 1;
        if (currentStage == 1) return 3;
        if (currentStage == 3) return 6;
        if (currentStage == 6) return 12;
        return 999;
    } else {
        if (currentStage == -6) return -3;
        if (currentStage == -3) return -2;
        if (currentStage == -2) return -1;
        return 999;
    }
}

// 4. Wyznaczenie poczatku semestru
QDate OverdueTracker::getSemesterStartDate() const
{
    QDate oct1(2026, 10, 1);
    int dayOfWeek = oct1.dayOfWeek();
    return oct1.addDays(-(dayOfWeek - 1));
}

// 5. Pobranie biezacego tygodnia semestru
int OverdueTracker::getCurrentSemesterWeek() const
{
    QDate semStart = getSemesterStartDate();
    QDate today = QDate::currentDate();
    int days = semStart.daysTo(today);
    if (days < 0) return 1;
    return std::clamp((days / 7) + 1, 1, 15);
}

// 6. Utrzymuje stan Unstarted (▢) dopoki uzytkownik nie zaplanuje nauki
void OverdueTracker::recalculateWeeklyStages()
{
    bool changed = false;

    for (auto &t : m_tracks) {
        for (auto &c : t.cells) {
            if (!c.hasClass) continue;

            if (c.status == ClassCellStatus::Completed ||
                c.status == ClassCellStatus::ExamPassed ||
                c.status == ClassCellStatus::ExamFailed) {
                continue;
            }

            if (c.isExam) {
                if (c.status != ClassCellStatus::StageActive) {
                    c.status = ClassCellStatus::StageActive;
                    changed = true;
                }
                continue;
            }

            if (c.status == ClassCellStatus::Empty) {
                c.status = ClassCellStatus::Unstarted;
                c.stageNumber = (t.trackType == "lecture") ? 0 : -6;
                changed = true;
            }
        }
    }

    if (changed) {
        for (const auto &t : m_tracks) {
            saveTrackToDatabase(t);
        }
        emit trackerDataChanged();
    }
}

// 7. Detekcja bezczynnosci AFK
void OverdueTracker::checkAfkState()
{
    DWORD idleMs = getSystemIdleTimeMsWinApi();
    int idleSec = static_cast<int>(idleMs / 1000);

    if (idleSec >= 300) {
        m_consecutiveIdleSeconds = idleSec;
        m_sessionAfkSecondsAccrued += 1;
        emit afkStatusChanged(true, m_consecutiveIdleSeconds);
    } else {
        m_consecutiveIdleSeconds = 0;
        emit afkStatusChanged(false, 0);
    }
}

// 8. Taktowanie odliczania aktywnej sesji
void OverdueTracker::onTick()
{
    if (!m_isSessionActive) return;

    checkAfkState();

    qint64 secondsRemaining = QDateTime::currentDateTime().secsTo(m_sessionDeclaredEnd);

    if (secondsRemaining <= 0) {
        endOverdueSession();
    } else {
        int remMin = static_cast<int>(secondsRemaining / 60);
        int debtMin = static_cast<int>(std::ceil(m_sessionAfkSecondsAccrued / 60.0));
        emit sessionTimeUpdated(remMin, debtMin);
    }
}

// 9. Trwaly zapis toru do SQLite
void OverdueTracker::saveTrackToDatabase(const SubjectTrackData &track)
{
    QJsonArray arr;
    for (const auto &c : track.cells) {
        QJsonObject o;
        o["week"] = c.weekNumber;
        o["hasClass"] = c.hasClass;
        o["isExam"] = c.isExam;
        o["label"] = c.label;
        o["status"] = static_cast<int>(c.status);
        o["stage"] = c.stageNumber;
        o["date"] = c.scheduledDate.toString(Qt::ISODate);
        arr.append(o);
    }

    QJsonObject root;
    root["subjectName"] = track.subjectName;
    root["trackType"] = track.trackType;
    root["displayName"] = track.displayName;
    root["colorDot"] = track.colorDot;
    root["cells"] = arr;

    QString jsonStr = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    DatabaseManager::instance().saveOverdueEntry(track.displayName, track.frequencyWeeks, 0, jsonStr);
}

// 10. Usuniecie toru
void OverdueTracker::removeTrack(const QString &displayName)
{
    m_tracks.removeIf([&displayName](const SubjectTrackData &t) {
        return t.displayName.compare(displayName, Qt::CaseInsensitive) == 0;
    });
    DatabaseManager::instance().executeQuery(QString("DELETE FROM overdue_tracker WHERE subject = '%1';").arg(displayName));
    emit trackerDataChanged();
}

// 11. Zmiana nazwy przedmiotu
void OverdueTracker::renameSubject(const QString &oldName, const QString &newName)
{
    for (auto &t : m_tracks) {
        if (t.subjectName.compare(oldName, Qt::CaseInsensitive) == 0) {
            DatabaseManager::instance().executeQuery(QString("DELETE FROM overdue_tracker WHERE subject = '%1';").arg(t.displayName));
            t.subjectName = newName;
            QString suffix = (t.trackType == "lecture") ? "[Wykład]" : ((t.trackType == "exercise") ? "[Ćwiczenia]" : "[Laboratorium]");
            t.displayName = QString("%1 %2").arg(newName, suffix);
            saveTrackToDatabase(t);
        }
    }
    emit trackerDataChanged();
}

// 12. Zerowanie dlugu AFK
void OverdueTracker::clearAfkDebt()
{
    m_persistentAfkDebtSeconds = 0;
    m_sessionAfkSecondsAccrued = 0;
    DatabaseManager::instance().setMetadataValue("afk_debt_seconds", "0");
    emit afkDebtTransferred(0);
}

// 13. Reset wszystkich torow
void OverdueTracker::clearAllTracks()
{
    m_tracks.clear();
    m_isSessionActive = false;
    m_timer->stop();
    clearAfkDebt();
    emit trackerDataChanged();
}

// 14. Odczyt torow z bazy
void OverdueTracker::loadFromDatabase()
{
    m_tracks.clear();
    m_persistentAfkDebtSeconds = DatabaseManager::instance().getMetadataValue("afk_debt_seconds", "0").toInt();

    QSqlQuery q = DatabaseManager::instance().executeQuery(
        "SELECT subject, frequency_weeks, current_stage, status_icon FROM overdue_tracker;"
    );

    while (q.next()) {
        QString jsonStr = q.value(3).toString();
        QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());

        if (doc.isObject()) {
            QJsonObject root = doc.object();
            SubjectTrackData t;
            t.subjectName = root.value("subjectName").toString();
            t.trackType = root.value("trackType").toString();
            t.displayName = root.value("displayName").toString();
            t.colorDot = root.value("colorDot").toString();

            int freq = q.value(1).toInt();
            t.frequencyWeeks = (freq > 0) ? freq : 1;

            QJsonArray arr = root.value("cells").toArray();
            for (const auto &v : arr) {
                QJsonObject o = v.toObject();
                ClassProgressCell c;
                c.weekNumber = o.value("week").toInt(1);
                c.hasClass = o.value("hasClass").toBool(false);
                c.isExam = o.value("isExam").toBool(false);
                c.label = o.value("label").toString();
                c.status = static_cast<ClassCellStatus>(o.value("status").toInt(1));
                c.stageNumber = o.value("stage").toInt(0);
                c.scheduledDate = QDate::fromString(o.value("date").toString(), Qt::ISODate);
                t.cells.append(c);
            }
            m_tracks.append(t);
        }
    }

    recalculateWeeklyStages();
}

// 15. Bezposrednia aktualizacja etapu komorki (wywolywana po zaplanowaniu w Visual Plannerze)
void OverdueTracker::updateCellStage(const QString &displayName, int weekNumber, int stageNumber)
{
    for (auto &t : m_tracks) {
        if (t.displayName == displayName) {
            if (weekNumber >= 1 && weekNumber <= t.cells.size()) {
                auto &c = t.cells[weekNumber - 1];
                c.status = ClassCellStatus::StageActive;
                c.stageNumber = stageNumber;
                c.scheduledDate = QDate::currentDate();
                saveTrackToDatabase(t);
                emit trackerDataChanged();
                return;
            }
        }
    }
}

// 16. Sprawdzenie postPone
bool OverdueTracker::isPostponeAllowed(const QString &displayName, const QDateTime &nowTime) const
{
    int reqHours = 2;
    for (const auto &t : m_tracks) {
        if (t.displayName == displayName) {
            for (const auto &c : t.cells) {
                if (c.status == ClassCellStatus::StageActive) {
                    reqHours = calculateStageDuration(t.trackType, c.stageNumber);
                    break;
                }
            }
            break;
        }
    }

    int totalRequiredSecs = (reqHours * 3600) + m_persistentAfkDebtSeconds;
    QDateTime limit01(nowTime.date().addDays(1), QTime(1, 0, 0));
    QDateTime estimatedEnd = nowTime.addSecs(totalRequiredSecs);

    return (estimatedEnd > limit01);
}

// 17. Przelozenie sesji
bool OverdueTracker::executePostpone(const QString &displayName)
{
    if (!isPostponeAllowed(displayName, QDateTime::currentDateTime())) {
        return false;
    }

    for (auto &t : m_tracks) {
        if (t.displayName == displayName) {
            for (auto &c : t.cells) {
                if (c.status == ClassCellStatus::StageActive) {
                    c.scheduledDate = c.scheduledDate.addDays(1);
                    saveTrackToDatabase(t);
                    emit trackerDataChanged();
                    return true;
                }
            }
        }
    }
    return false;
}

// 18. Zakonczenie sesji
void OverdueTracker::endOverdueSession()
{
    m_isSessionActive = false;
    m_timer->stop();

    if (m_sessionAfkSecondsAccrued > 0) {
        m_persistentAfkDebtSeconds += m_sessionAfkSecondsAccrued;
        m_sessionAfkSecondsAccrued = 0;
        DatabaseManager::instance().setMetadataValue("afk_debt_seconds", QString::number(m_persistentAfkDebtSeconds));
        emit afkDebtTransferred(getPendingAfkDebtMinutes());
    } else {
        m_persistentAfkDebtSeconds = 0;
        DatabaseManager::instance().setMetadataValue("afk_debt_seconds", "0");
        emit afkDebtTransferred(0);
    }

    emit sessionFinished();
}

// 19. Rozpoczecie sesji
void OverdueTracker::startOverdueSession(const QString &displayName, int durationHours)
{
    m_activeDisplayName = displayName;
    m_isSessionActive = true;
    m_sessionAfkSecondsAccrued = 0;
    m_consecutiveIdleSeconds = 0;

    int totalSecs = (durationHours * 3600) + m_persistentAfkDebtSeconds;
    m_sessionDeclaredEnd = QDateTime::currentDateTime().addSecs(totalSecs);
    m_timer->start(1000);
}

// 20. Manualne cyklowanie komórek (nieużywane w trybie sterowania kalendarzem)
void OverdueTracker::cycleCellStatus(const QString &displayName, int weekNumber)
{
    Q_UNUSED(displayName);
    Q_UNUSED(weekNumber);
}

// 21. Pobranie listy zadan z zachowaniem priorytetu
QList<OverdueTaskItem> OverdueTracker::getPendingOverdueTasks(CalendarEngine *engine) const
{
    QList<OverdueTaskItem> list;
    int currentWeek = getCurrentSemesterWeek();

    auto countCalendarSessions = [&](const QString &displayName, int weekNum, int stageNum) -> int {
        int cnt = 0;
        if (engine) {
            QString searchStr = QString("T%1 (%2)").arg(weekNum).arg(stageNum > 0 ? QString("+%1").arg(stageNum) : QString::number(stageNum));
            for (const auto &ev : engine->getEvents()) {
                if (ev.linkedProfile == "overdue" && ev.title.contains(displayName) && ev.title.contains(searchStr)) {
                    cnt++;
                }
            }
        }
        return cnt;
    };

    // 1. Kolokwia
    for (const auto &t : m_tracks) {
        for (const auto &c : t.cells) {
            if (c.hasClass && c.isExam && c.status != ClassCellStatus::Completed && c.status != ClassCellStatus::ExamPassed) {
                if (c.weekNumber >= currentWeek && c.weekNumber <= currentWeek + 1) {
                    QString itemLabel = QString("%1 T%2 - Kolokwium").arg(t.displayName).arg(c.weekNumber);
                    list.append({ t.displayName, t.subjectName, t.trackType, c.weekNumber, itemLabel, true, 180, "#d50000", 1, false, 0 });
                }
            }
        }
    }

    // 2. Laby i cwiczenia
    for (const auto &t : m_tracks) {
        if (t.trackType == "lab" || t.trackType == "exercise") {
            for (const auto &c : t.cells) {
                if (c.hasClass && !c.isExam && c.status != ClassCellStatus::Completed && c.status != ClassCellStatus::ExamPassed) {
                    if (c.weekNumber >= currentWeek && c.weekNumber <= currentWeek + 1) {
                        int curStage = (c.status == ClassCellStatus::Unstarted || c.status == ClassCellStatus::Empty) ? -6 : c.stageNumber;
                        int duration = calculateStageDuration(t.trackType, curStage) * 60;
                        QString stageStr = (curStage > 0) ? QString("+%1").arg(curStage) : QString::number(curStage);
                        QString itemLabel = QString("%1 T%2 (%3)").arg(t.displayName).arg(c.weekNumber).arg(stageStr);
                        QString color = (t.trackType == "lab") ? "#0b8043" : "#f6bf26";
                        list.append({ t.displayName, t.subjectName, t.trackType, c.weekNumber, itemLabel, false, duration, color, 2, false, curStage });
                    }
                }
            }
        }
    }

    // 3. Wyklady
    for (const auto &t : m_tracks) {
        if (t.trackType == "lecture") {
            for (const auto &c : t.cells) {
                if (c.hasClass && !c.isExam && c.status != ClassCellStatus::Completed && c.status != ClassCellStatus::ExamPassed) {
                    if (c.weekNumber <= currentWeek) {
                        int curStage = (c.status == ClassCellStatus::Unstarted || c.status == ClassCellStatus::Empty) ? 0 : c.stageNumber;
                        int duration = calculateStageDuration(t.trackType, curStage) * 60;
                        QString stageStr = (curStage > 0) ? QString("+%1").arg(curStage) : QString::number(curStage);
                        QString itemLabel = QString("%1 T%2 (%3)").arg(t.displayName).arg(c.weekNumber).arg(stageStr);
                        list.append({ t.displayName, t.subjectName, t.trackType, c.weekNumber, itemLabel, false, duration, "#4285f4", 3, false, curStage });
                    }
                }
            }
        }
    }

    return list;
}

// 22. Wyliczenie metryk tempa
void OverdueTracker::calculatePacingMetrics(int &outTotalBlocks, int &outDaysToDeadline, double &outRequiredBlocksPerDay, double &outRequiredHoursPerDay) const
{
    auto tasks = getPendingOverdueTasks();
    outTotalBlocks = tasks.size();

    QDate today = QDate::currentDate();
    QDate deadlineDate = today.addDays(7);

    int currentWeek = getCurrentSemesterWeek();
    QDate semStart = getSemesterStartDate();

    for (const auto &task : tasks) {
        if (task.isExam && task.weekNumber >= currentWeek) {
            QDate examEstimated = semStart.addDays((task.weekNumber - 1) * 7 + 4);
            if (examEstimated >= today && examEstimated < deadlineDate) {
                deadlineDate = examEstimated;
            }
        }
    }

    outDaysToDeadline = std::max(1, static_cast<int>(today.daysTo(deadlineDate)));
    outRequiredBlocksPerDay = static_cast<double>(outTotalBlocks) / static_cast<double>(outDaysToDeadline);
    outRequiredHoursPerDay = outRequiredBlocksPerDay * 1.5;
}

// 23. Wstawianie scalonego, jednolitego bloku zadan
void OverdueTracker::scheduleBundledTasks(CalendarEngine *calendarEngine, const QList<OverdueTaskItem> &tasks,
                                         const QDate &targetDate, const QTime &startTime, int totalDurationMin)
{
    if (!calendarEngine || tasks.isEmpty()) return;

    QTime endTime = startTime.addSecs(totalDurationMin * 60);

    QStringList titles;
    bool hasExam = false;
    for (const auto &t : tasks) {
        QString stageStr = (t.nextStage > 0) ? QString("+%1").arg(t.nextStage) : QString::number(t.nextStage);
        if (t.isExam) {
            titles.append(QString("%1 T%2").arg(t.displayName).arg(t.weekNumber));
            hasExam = true;
        } else {
            titles.append(QString("%1 T%2 (%3)").arg(t.displayName).arg(t.weekNumber).arg(stageStr));
        }

        updateCellStage(t.displayName, t.weekNumber, t.nextStage);
    }

    QString bundleTitle = QString("%1: %2")
                              .arg(hasExam ? "🎯 [KOLOKWIUM]" : "📚 [SESJA NAUKI]")
                              .arg(titles.join(" + "));

    CalendarEvent ev;
    ev.id = QString("bundle_overdue_%1_%2").arg(targetDate.toString("yyyyMMdd")).arg(startTime.hour() * 60 + startTime.minute());
    ev.title = bundleTitle;
    ev.category = "Profile blokowania";
    ev.linkedProfile = "overdue";
    ev.color = hasExam ? "#d50000" : (tasks.first().color);
    ev.start = QDateTime(targetDate, startTime);
    ev.end = QDateTime(targetDate, endTime);

    calendarEngine->addEvent(ev);
    emit trackerDataChanged();
}

// 24. Automatyczne zaplanowanie wybranych zadan na dzisiejszy dzien
void OverdueTracker::scheduleChosenTasks(CalendarEngine *calendarEngine, const QList<OverdueTaskItem> &tasks)
{
    if (!calendarEngine || tasks.isEmpty()) return;

    int totalDurationMin = 0;
    for (const auto &t : tasks) {
        totalDurationMin += t.durationMinutes;
    }
    if (totalDurationMin <= 0) totalDurationMin = 60;

    QDate today = QDate::currentDate();
    QTime nowTime = QTime::currentTime();
    int curMin = nowTime.hour() * 60 + nowTime.minute();
    int searchStartMin = std::max(curMin + 15, 17 * 60);
    searchStartMin = ((searchStartMin + 14) / 15) * 15;

    if (searchStartMin + totalDurationMin > 1440) {
        searchStartMin = std::max(0, 1440 - totalDurationMin);
        searchStartMin = (searchStartMin / 15) * 15;
    }

    QTime startTime(searchStartMin / 60, searchStartMin % 60);
    scheduleBundledTasks(calendarEngine, tasks, today, startTime, totalDurationMin);
}

// 25. Wymuszone zablokowanie planu
void OverdueTracker::forceAutoLockSchedule(CalendarEngine *calendarEngine)
{
    autoScheduleDailyBlocks(calendarEngine);
}

// 26. Synchronizacja z planem
void OverdueTracker::syncFromSubjectManager(const QList<AcademicSubject> &subjects)
{
    m_tracks.clear();
    DatabaseManager::instance().executeQuery("DELETE FROM overdue_tracker;");

    QDate semStart = getSemesterStartDate();

    struct TrackDef {
        QString type;
        QString suffix;
        QString dot;
    };
    const QList<TrackDef> trackTypes = {
        { "lecture",  "[Wykład]",      "🔵" },
        { "exercise", "[Ćwiczenia]",   "🟠" },
        { "lab",      "[Laboratorium]","🟢" }
    };

    for (const auto &sub : subjects) {
        for (const auto &def : trackTypes) {
            QList<SubjectClassItem> matchedClasses;
            for (const auto &c : sub.classes) {
                if (c.type == def.type) {
                    matchedClasses.append(c);
                } else if (c.type == "exam") {
                    QString lbl = c.label.toLower();
                    if (def.type == "lecture" && (lbl.contains("wykład") || lbl.contains("lecture") || sub.isLecture)) {
                        matchedClasses.append(c);
                    } else if (def.type == "lab" && (lbl.contains("lab") || lbl.contains("laboratorium"))) {
                        matchedClasses.append(c);
                    } else if (def.type == "exercise" && (lbl.contains("ćwicz") || lbl.contains("exercise"))) {
                        matchedClasses.append(c);
                    } else if (def.type == "exercise" && !sub.isLecture) {
                        matchedClasses.append(c);
                    }
                }
            }

            if (matchedClasses.isEmpty()) continue;

            SubjectTrackData track;
            track.subjectName = sub.name;
            track.trackType = def.type;
            track.displayName = QString("%1 %2").arg(sub.name, def.suffix);
            track.colorDot = def.dot;
            track.frequencyWeeks = (def.type == "lab") ? 2 : 1;

            for (int w = 1; w <= 15; ++w) {
                ClassProgressCell cell;
                cell.weekNumber = w;
                cell.hasClass = false;
                cell.isExam = false;
                cell.stageNumber = (def.type == "lecture") ? 0 : -6;
                cell.status = ClassCellStatus::Unstarted;
                track.cells.append(cell);
            }

            for (const auto &clsItem : matchedClasses) {
                if (!clsItem.start.isValid()) continue;
                int week = (semStart.daysTo(clsItem.start.date()) / 7) + 1;
                if (week >= 1 && week <= 15) {
                    ClassProgressCell &cell = track.cells[week - 1];
                    cell.hasClass = true;
                    cell.scheduledDate = clsItem.start.date();
                    cell.label = clsItem.label;

                    QString lblLower = clsItem.label.toLower();
                    if (clsItem.type == "exam" || lblLower.contains("kolo") || lblLower.contains("egzamin") || lblLower.contains("test")) {
                        cell.isExam = true;
                    }
                }
            }

            m_tracks.append(track);
            saveTrackToDatabase(track);
        }
    }

    recalculateWeeklyStages();
    emit trackerDataChanged();
}

// 27. Sprawdzenie pojedynczej sesji
bool OverdueTracker::hasPendingSessionToday(QString &outDisplayName, int &outDurationHours, int &outWeekIdx, QString &outStageLabel, bool &outIsExam)
{
    int currentWeek = getCurrentSemesterWeek();

    for (const auto &t : m_tracks) {
        for (const auto &c : t.cells) {
            if (c.hasClass && c.isExam && c.weekNumber >= currentWeek && c.weekNumber <= currentWeek + 1) {
                if (c.status != ClassCellStatus::Completed && c.status != ClassCellStatus::ExamPassed) {
                    outDisplayName = t.displayName;
                    outDurationHours = 3;
                    outWeekIdx = c.weekNumber;
                    outStageLabel = "🚨 KOLOKWIUM";
                    outIsExam = true;
                    return true;
                }
            }
        }
    }

    for (const auto &t : m_tracks) {
        for (const auto &c : t.cells) {
            if (c.hasClass && c.status == ClassCellStatus::StageActive) {
                outDisplayName = t.displayName;
                outDurationHours = calculateStageDuration(t.trackType, c.stageNumber);
                outWeekIdx = c.weekNumber;
                outStageLabel = (t.trackType == "lecture") ? QString("+%1").arg(c.stageNumber) : QString("%1").arg(c.stageNumber);
                outIsExam = c.isExam;
                return true;
            }
        }
    }
    return false;
}

// 28. Reczne wywolanie auto-schedulera
void OverdueTracker::autoScheduleDailyBlocks(CalendarEngine *calendarEngine)
{
    if (!calendarEngine) return;

    QDate today = QDate::currentDate();
    QTime nowTime = QTime::currentTime();

    struct Interval { int startMin; int endMin; };
    std::vector<Interval> busy;

    for (const auto &ev : calendarEngine->getEvents()) {
        if (ev.start.date() == today) {
            int sm = ev.start.time().hour() * 60 + ev.start.time().minute();
            int em = ev.end.time().hour() * 60 + ev.end.time().minute();
            if (em <= sm) em = 1440;
            busy.push_back({sm, em});
        }
    }

    auto queue = getPendingOverdueTasks(calendarEngine);
    if (queue.isEmpty()) return;

    int curMin = nowTime.hour() * 60 + nowTime.minute();
    int searchStartMin = std::max(curMin + 15, 16 * 60);
    searchStartMin = ((searchStartMin + 14) / 15) * 15;

    int scheduledMinutesTotal = 0;
    int maxDailyAllowedMinutes = 240;
    int taskIdx = 0;
    int candidateStart = searchStartMin;

    while (candidateStart < 1440 && taskIdx < queue.size() && scheduledMinutesTotal < maxDailyAllowedMinutes) {
        const auto &task = queue[taskIdx];
        if (task.isAlreadyScheduled) { taskIdx++; continue; }

        int candidateEnd = candidateStart + task.durationMinutes;
        if (candidateEnd > 1440) break;

        bool collision = false;
        for (const auto &b : busy) {
            if (candidateStart < b.endMin && candidateEnd > b.startMin) {
                candidateStart = ((b.endMin + 14) / 15) * 15;
                collision = true;
                break;
            }
        }

        if (collision) continue;

        QString stageStr = (task.nextStage > 0) ? QString("+%1").arg(task.nextStage) : QString::number(task.nextStage);

        CalendarEvent ev;
        ev.id = QString("auto_overdue_%1_%2").arg(today.toString("yyyyMMdd")).arg(candidateStart);
        ev.title = QString("%1: %2 T%3 (%4)").arg(task.isExam ? "🎯 [KOLOKWIUM]" : "📚 [NAUKA]", task.displayName).arg(task.weekNumber).arg(stageStr);
        ev.category = "Profile blokowania";
        ev.linkedProfile = "overdue";
        ev.color = task.color;
        ev.start = QDateTime(today, QTime(candidateStart / 60, candidateStart % 60));
        ev.end = QDateTime(today, QTime(candidateEnd / 60, candidateEnd % 60));

        calendarEngine->addEvent(ev);
        busy.push_back({candidateStart, candidateEnd});
        scheduledMinutesTotal += task.durationMinutes;

        updateCellStage(task.displayName, task.weekNumber, task.nextStage);

        taskIdx++;
        candidateStart = candidateEnd;
    }

    emit trackerDataChanged();
}

// 29. Czas trwania sesji
int OverdueTracker::getRequiredDurationHours(const QString &displayName, int weekIdx) const
{
    for (const auto &t : m_tracks) {
        if (t.displayName == displayName) {
            if (weekIdx >= 1 && weekIdx <= t.cells.size()) {
                const auto &c = t.cells[weekIdx - 1];
                if (c.isExam) return 3;
                return calculateStageDuration(t.trackType, c.stageNumber);
            }
        }
    }
    return 2;
}

// 30. Gettery dlugu AFK
int OverdueTracker::getPendingAfkDebtSeconds() const { return m_persistentAfkDebtSeconds; }
int OverdueTracker::getPendingAfkDebtMinutes() const { return static_cast<int>(std::ceil(m_persistentAfkDebtSeconds / 60.0)); }

// 31. Pobranie torow
const QList<SubjectTrackData> &OverdueTracker::getTracks() const
{
    return m_tracks;
}

// 32. Konstruktor OverdueTracker
OverdueTracker::OverdueTracker(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_isSessionActive(false)
    , m_consecutiveIdleSeconds(0)
    , m_sessionAfkSecondsAccrued(0)
    , m_persistentAfkDebtSeconds(0)
{
    connect(m_timer, &QTimer::timeout, this, &OverdueTracker::onTick);
    loadFromDatabase();
}
