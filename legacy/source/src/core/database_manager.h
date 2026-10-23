// Plik naglowkowy zarzadcy bazy danych DatabaseManager.
// Zapewnia zapis i odczyt z bazy SQLite oraz import/export backup.json z polem linked_subject.
// Laczy sie z SubjectManager, CalendarEngine, ProfileManager i OverdueTracker.

#ifndef DATABASE_MANAGER_H
#define DATABASE_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QSqlDatabase>
#include <QSqlQuery>

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    static DatabaseManager &instance();

    bool initializeDatabase();
    void closeDatabase();
    bool clearAllDatabaseData();
    bool isOpen() const;

    bool isKernelLockActive() const;
    void setKernelLockEnabled(bool enabled);

    QString getDatabaseFilePath() const;
    QString getDefaultBackupFilePath() const;
    QSqlDatabase getDatabase();

    bool exportBackupToJson(const QString &filePath = QString());
    bool importBackupFromJson(const QString &filePath = QString());

    void syncRulesToSharedMemory(const QString &activeRulesJson);
    QString readRulesFromSharedMemory();

    bool isMetadataFlag(const QString &key);
    void setMetadataFlag(const QString &key, const QString &val);
    QString getMetadataValue(const QString &key, const QString &defaultVal = QString());
    void setMetadataValue(const QString &key, const QString &val);

    bool saveProfile(const QString &name, bool isLocked, const QString &webEx, const QString &apps,
                     const QString &scheduleType, const QString &breaksConfig);
    bool deleteProfile(const QString &name);

    bool saveCalendarEvent(const QString &id, const QString &seriesId, const QString &title, const QString &category,
                           const QString &startStr, const QString &endStr, const QString &profileName, const QString &color,
                           const QString &googleId = QString(), qint64 updatedAt = 0, const QString &linkedSubject = QString());
    bool deleteCalendarEvent(const QString &id);
    bool deleteCalendarSeries(const QString &seriesId);

    bool saveSubjectRecord(const QString &name, const QString &color, bool isLecture, const QString &notes = QString());
    bool deleteSubjectRecord(const QString &name);
    bool saveSubjectClassRecord(const QString &id, const QString &subjectName, const QString &classType,
                                int classIndex, const QString &startStr, const QString &endStr, const QString &label);
    bool deleteSubjectClassRecord(const QString &id);
    bool clearSubjectClasses(const QString &subjectName);

    bool saveOverdueEntry(const QString &subject, int frequencyWeeks, int currentStage, const QString &statusIcon);

    QSqlQuery executeQuery(const QString &sql);

private:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    bool createTables();
    bool runSelfTest();

    void *m_sharedMemoryHandle;
    void *m_sharedMemoryBuffer;
    bool m_isInitialized;
    bool m_kernelLockEnabled;
    mutable QString m_activeDbPath;
};

#endif // DATABASE_MANAGER_H
