// Plik naglowkowy zarzadcy profili ProfileManager.
// Odpowiada za przechowywanie profili, egzekwowanie blokad kłódek oraz obsługę trybu Debug Mode.
// Udostepnia publiczny slot loadFromDatabase() do przeladowania po przywroceniu backupu.
// Laczy sie z DatabaseManager, CalendarEngine, AppBlocker, RuleServer oraz widokami BlocksView i MainWindow.

#ifndef PROFILE_MANAGER_H
#define PROFILE_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QDateTime>
#include <QTimer>

enum class LockType {
    None,
    UntilTime,
    Characters,
    Minigame
};

enum class ScheduleType {
    BlockedAtAllTimes,
    ScheduledInCalendar
};

struct BreakSuite {
    bool charBreakEnabled = true;
    int charBreakChars = 1000;
    int charBreakMinutes = 15;

    bool clickBreakEnabled = true;
    int clickBreakMinutes = 10;

    bool criticalBreakEnabled = true;
    int criticalBreakMinutes = 3;
    QDateTime lastCriticalUsage;
};

struct BlockProfile {
    QString name;
    QStringList blockedWebsites;
    QStringList websiteExceptions;
    QStringList blockedApps;
    bool isActive = false;
    bool isInCalendarWindow = false;
    LockType lockType = LockType::None;
    int requiredChars = 1000;
    QDateTime lockUntilTime;
    bool isLocked = false;
    bool isOverdue = false;
    ScheduleType scheduleType = ScheduleType::BlockedAtAllTimes;
    BreakSuite breaks;
};

class ProfileManager : public QObject
{
    Q_OBJECT

public:
    explicit ProfileManager(QObject *parent = nullptr);
    ~ProfileManager() override = default;

    const QMap<QString, BlockProfile> &getAllProfiles() const;
    const BlockProfile *getProfile(const QString &name) const;
    bool isProfileLocked(const QString &name) const;
    int getCriticalBreakCooldownRemaining(const QString &name) const;
    bool isDebugMode() const;

public slots:
    void loadFromDatabase();
    void setDebugMode(bool enabled);
    void addOrUpdateProfile(const BlockProfile &profile);
    void deleteProfile(const QString &profileName);
    void setProfileActive(const QString &profileName, bool active);
    void setCalendarWindowActive(const QString &profileName, bool windowActive);
    void setProfileLock(const QString &profileName, LockType type, int chars, const QDateTime &untilTime);
    void unlockProfile(const QString &profileName);
    void resetAll();

    bool triggerClickBreak(const QString &profileName);
    bool triggerCharBreak(const QString &profileName);
    bool triggerCriticalBreak(const QString &profileName);
    void endBreak();

signals:
    void activeRulesChanged(bool active, const QStringList &exceptions, const QStringList &blockedApps);
    void profilesUpdated();
    void breakStatusChanged(bool onBreak, const QString &profileName, int secondsLeft);
    void debugModeChanged(bool enabled);

private slots:
    void onBreakTimerTick();
    void checkLockExpirations();

private:
    void recalculateAndEmitRules();
    void persistProfile(const BlockProfile &p);

    QMap<QString, BlockProfile> m_profiles;
    bool m_isOnBreak;
    bool m_debugMode;
    QString m_breakProfileName;
    int m_breakSecondsRemaining;
    QTimer *m_breakTimer;
    QTimer *m_lockExpirationTimer;
};

#endif // PROFILE_MANAGER_H
