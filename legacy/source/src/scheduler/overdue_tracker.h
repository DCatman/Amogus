// Plik naglowkowy silnika OverdueTracker (Spaced Learning System & Cykle ICS).
// Utrzymuje stan Unstarted, aktywuje etapy oraz zarządza progresem faz ICS.
// Metoda loadFromDatabase() zostala przeniesiona do public slots, umozliwiajac
// bezpieczne przeladowanie danych bezposrednio po przywroceniu bazy z pliku backup.json.
// Laczy sie z DatabaseManager, SubjectManager, CalendarEngine, CalendarCanvas oraz MainWindow.

#ifndef OVERDUE_TRACKER_H
#define OVERDUE_TRACKER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include <QTimer>
#include "subject_types.h"

class CalendarEngine;

enum class ClassCellStatus {
    Empty,
    Unstarted,
    StageActive,
    Completed,
    ExamPassed,
    ExamFailed
};

struct ClassProgressCell {
    int weekNumber = 1;
    bool hasClass = false;
    bool isExam = false;
    QString label;
    ClassCellStatus status = ClassCellStatus::Empty;
    int stageNumber = 0;
    QDate scheduledDate;
};

struct SubjectTrackData {
    QString subjectName;
    QString trackType;
    QString displayName;
    QString colorDot;
    int frequencyWeeks = 1;
    QList<ClassProgressCell> cells;
};

struct OverdueTaskItem {
    QString displayName;
    QString subjectName;
    QString trackType;
    int weekNumber;
    QString label;
    bool isExam;
    int durationMinutes;
    QString color;
    int priority;
    bool isAlreadyScheduled = false;
    int nextStage = 0;
};

class OverdueTracker : public QObject
{
    Q_OBJECT

public:
    explicit OverdueTracker(QObject *parent = nullptr);
    ~OverdueTracker() override = default;

    const QList<SubjectTrackData> &getTracks() const;
    int getCurrentSemesterWeek() const;
    QDate getSemesterStartDate() const;

    QList<OverdueTaskItem> getPendingOverdueTasks(CalendarEngine *engine = nullptr) const;
    void calculatePacingMetrics(int &outTotalBlocks, int &outDaysToDeadline, double &outRequiredBlocksPerDay, double &outRequiredHoursPerDay) const;

    bool isPostponeAllowed(const QString &displayName, const QDateTime &nowTime) const;
    int getRequiredDurationHours(const QString &displayName, int weekIdx) const;
    bool hasPendingSessionToday(QString &outDisplayName, int &outDurationHours, int &outWeekIdx, QString &outStageLabel, bool &outIsExam);

    int getPendingAfkDebtSeconds() const;
    int getPendingAfkDebtMinutes() const;

public slots:
    void loadFromDatabase();
    void autoScheduleDailyBlocks(CalendarEngine *calendarEngine);
    void scheduleBundledTasks(CalendarEngine *calendarEngine, const QList<OverdueTaskItem> &tasks,
                              const QDate &targetDate, const QTime &startTime, int totalDurationMin);
    void scheduleChosenTasks(CalendarEngine *calendarEngine, const QList<OverdueTaskItem> &tasks);
    void forceAutoLockSchedule(CalendarEngine *calendarEngine);

    void updateCellStage(const QString &displayName, int weekNumber, int stageNumber);
    void syncFromSubjectManager(const QList<AcademicSubject> &subjects);
    void cycleCellStatus(const QString &displayName, int weekNumber);
    void startOverdueSession(const QString &displayName, int durationHours);
    void endOverdueSession();
    bool executePostpone(const QString &displayName);
    void removeTrack(const QString &displayName);
    void clearAllTracks();
    void renameSubject(const QString &oldName, const QString &newName);
    void clearAfkDebt();

signals:
    void sessionTimeUpdated(int remainingMinutes, int currentAfkDebtMinutes);
    void afkStatusChanged(bool isAfk, int idleSeconds);
    void afkDebtTransferred(int totalDebtMinutes);
    void sessionFinished();
    void trackerDataChanged();

private slots:
    void onTick();

private:
    void checkAfkState();
    void saveTrackToDatabase(const SubjectTrackData &track);
    void recalculateWeeklyStages();

    int calculateStageDuration(const QString &trackType, int stage) const;
    int getNextStage(const QString &trackType, int currentStage) const;

    QList<SubjectTrackData> m_tracks;
    QTimer *m_timer;

    bool m_isSessionActive;
    QString m_activeDisplayName;
    QDateTime m_sessionDeclaredEnd;

    int m_consecutiveIdleSeconds;
    int m_sessionAfkSecondsAccrued;
    int m_persistentAfkDebtSeconds;
};

#endif // OVERDUE_TRACKER_H
