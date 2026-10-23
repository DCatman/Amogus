// Plik naglowkowy zarzadcy przedmiotow SubjectManager.
// Obsluguje import z JSON, modyfikacje terminow, synchronizacje z kalendarzem
// oraz udostepnia publiczny slot loadFromDatabase() do przeladowania po operacji restore.
// Laczy sie z DatabaseManager, CalendarEngine, OverdueTracker, subject_types.h oraz MainWindow.

#ifndef SUBJECTS_MANAGER_H
#define SUBJECTS_MANAGER_H

#include <QObject>
#include <QString>
#include <QDateTime>
#include <QList>
#include "subject_types.h"

class CalendarEngine;
class OverdueTracker;

class SubjectManager : public QObject
{
    Q_OBJECT

public:
    explicit SubjectManager(CalendarEngine *calendarEngine, OverdueTracker *overdueTracker, QObject *parent = nullptr);
    ~SubjectManager() override = default;

    const QList<AcademicSubject> &getSubjects() const;
    const AcademicSubject *findSubject(const QString &name) const;

public slots:
    void loadFromDatabase();
    bool importFromJsonString(const QString &jsonContent, QString &outError);
    bool importFromJsonFile(const QString &filePath, QString &outError);
    void addOrUpdateSubject(const AcademicSubject &subject);
    void renameSubject(const QString &oldName, const QString &newName);
    void updateClassDetails(const QString &subjectName, const QString &classId, const QString &newLabel, const QDateTime &newStart, const QDateTime &newEnd);
    void updateSubjectNotes(const QString &subjectName, const QString &notes);
    void removeSubject(const QString &name);
    void removeClassFromSubject(const QString &subjectName, const QString &classId);
    void syncAllToCalendarAndTracker();
    void clearAllSubjects();

signals:
    void subjectsUpdated();

private:
    void persistSubjectToDb(const AcademicSubject &sub);

    CalendarEngine *m_calendarEngine;
    OverdueTracker *m_overdueTracker;
    QList<AcademicSubject> m_subjects;
};

#endif // SUBJECTS_MANAGER_H
