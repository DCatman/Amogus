// Plik implementacji modulu ProfileManager.
// Zapewnia egzekwowanie blokad. Jesli profil jest wylaczony (Off) lub znajduje sie poza oknem kalendarza,
// restrykcje blokady kłódki nie sa nakladane, umozliwiajac swobodna konfiguracje i edycje.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "profile_manager.h"
#include "database_manager.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlQuery>
#include <utility>

// 1. Zapis profilu do SQLite
void ProfileManager::persistProfile(const BlockProfile &p)
{
    QJsonObject bJson;
    bJson["char_enabled"] = p.breaks.charBreakEnabled;
    bJson["char_chars"] = p.breaks.charBreakChars;
    bJson["char_min"] = p.breaks.charBreakMinutes;
    bJson["click_enabled"] = p.breaks.clickBreakEnabled;
    bJson["click_min"] = p.breaks.clickBreakMinutes;
    bJson["crit_enabled"] = p.breaks.criticalBreakEnabled;
    bJson["crit_min"] = p.breaks.criticalBreakMinutes;
    bJson["crit_last"] = p.breaks.lastCriticalUsage.toString(Qt::ISODate);

    QString bStr = QString::fromUtf8(QJsonDocument(bJson).toJson(QJsonDocument::Compact));
    QString sType = (p.scheduleType == ScheduleType::ScheduledInCalendar) ? "calendar" : "all_times";

    DatabaseManager::instance().saveProfile(
        p.name,
        p.isLocked,
        p.websiteExceptions.join("\n"),
        p.blockedApps.join("\n"),
        sType,
        bStr
    );
}

// 2. Taktowanie aktywnej przerwy
void ProfileManager::onBreakTimerTick()
{
    if (!m_isOnBreak) return;

    if (m_breakSecondsRemaining > 0) {
        m_breakSecondsRemaining--;
        emit breakStatusChanged(true, m_breakProfileName, m_breakSecondsRemaining);
    } else {
        endBreak();
    }
}

// 3. Zakonczenie przerwy
void ProfileManager::endBreak()
{
    m_isOnBreak = false;
    m_breakTimer->stop();
    emit breakStatusChanged(false, "", 0);
    recalculateAndEmitRules();
}

// 4. Przerwa krytyczna
bool ProfileManager::triggerCriticalBreak(const QString &profileName)
{
    auto it = m_profiles.find(profileName);
    if (it == m_profiles.end()) return false;
    BlockProfile &p = it.value();
    if (!p.breaks.criticalBreakEnabled) return false;

    if (!m_debugMode && p.breaks.lastCriticalUsage.isValid()) {
        qint64 elapsed = p.breaks.lastCriticalUsage.secsTo(QDateTime::currentDateTime());
        if (elapsed < 3600) {
            return false;
        }
    }

    p.breaks.lastCriticalUsage = QDateTime::currentDateTime();
    persistProfile(p);

    m_breakProfileName = profileName;
    m_breakSecondsRemaining = 3 * 60;
    m_isOnBreak = true;
    m_breakTimer->start(1000);

    emit breakStatusChanged(true, profileName, m_breakSecondsRemaining);
    recalculateAndEmitRules();
    emit profilesUpdated();
    return true;
}

// 5. Przerwa znakowa
bool ProfileManager::triggerCharBreak(const QString &profileName)
{
    auto it = m_profiles.constFind(profileName);
    if (it == m_profiles.constEnd()) return false;
    const BlockProfile &p = it.value();
    if (!p.breaks.charBreakEnabled) return false;

    m_breakProfileName = profileName;
    m_breakSecondsRemaining = p.breaks.charBreakMinutes * 60;
    m_isOnBreak = true;
    m_breakTimer->start(1000);

    emit breakStatusChanged(true, profileName, m_breakSecondsRemaining);
    recalculateAndEmitRules();
    return true;
}

// 6. Przerwa po kliknieciu
bool ProfileManager::triggerClickBreak(const QString &profileName)
{
    auto it = m_profiles.constFind(profileName);
    if (it == m_profiles.constEnd()) return false;
    const BlockProfile &p = it.value();
    if (!p.breaks.clickBreakEnabled) return false;

    m_breakProfileName = profileName;
    m_breakSecondsRemaining = p.breaks.clickBreakMinutes * 60;
    m_isOnBreak = true;
    m_breakTimer->start(1000);

    emit breakStatusChanged(true, profileName, m_breakSecondsRemaining);
    recalculateAndEmitRules();
    return true;
}

// 7. Odczyt cooldownu przerwy krytycznej
int ProfileManager::getCriticalBreakCooldownRemaining(const QString &name) const
{
    if (m_debugMode) return 0;

    auto it = m_profiles.constFind(name);
    if (it == m_profiles.constEnd()) return 0;
    const auto &p = it.value();
    if (!p.breaks.lastCriticalUsage.isValid()) return 0;

    qint64 elapsed = p.breaks.lastCriticalUsage.secsTo(QDateTime::currentDateTime());
    if (elapsed < 3600) {
        return static_cast<int>(3600 - elapsed);
    }
    return 0;
}

// 8. Sprawdzanie wygasniecia blokad czasowych
void ProfileManager::checkLockExpirations()
{
    bool changed = false;
    QDateTime now = QDateTime::currentDateTime();

    for (auto it = m_profiles.begin(); it != m_profiles.end(); ++it) {
        if (it->isLocked && it->lockType == LockType::UntilTime) {
            if (it->lockUntilTime.isValid() && now >= it->lockUntilTime) {
                it->isLocked = false;
                it->lockType = LockType::None;
                persistProfile(it.value());
                changed = true;
            }
        }
    }
    if (changed) emit profilesUpdated();
}

// 9. Przeliczanie aktywnych regul blokowania
void ProfileManager::recalculateAndEmitRules()
{
    if (m_debugMode) {
        emit activeRulesChanged(false, QStringList(), QStringList());
        return;
    }

    if (m_isOnBreak) {
        emit activeRulesChanged(false, QStringList(), QStringList());
        return;
    }

    bool anyActive = false;
    QStringList aggregatedExceptions;
    QStringList aggregatedApps;

    for (const auto &p : std::as_const(m_profiles)) {
        bool shouldEnforce = false;
        if (p.isOverdue) {
            shouldEnforce = p.isInCalendarWindow;
        } else if (p.scheduleType == ScheduleType::ScheduledInCalendar) {
            shouldEnforce = p.isInCalendarWindow;
        } else {
            shouldEnforce = p.isActive;
        }

        if (shouldEnforce) {
            anyActive = true;
            aggregatedExceptions.append(p.websiteExceptions);
            aggregatedApps.append(p.blockedApps);
        }
    }

    aggregatedExceptions.removeDuplicates();
    aggregatedApps.removeDuplicates();

    emit activeRulesChanged(anyActive, aggregatedExceptions, aggregatedApps);
}

// 10. Aktualizacja obecnosci biezacego czasu w oknie kalendarza
void ProfileManager::setCalendarWindowActive(const QString &profileName, bool windowActive)
{
    auto it = m_profiles.find(profileName);
    if (it != m_profiles.end()) {
        if (it->isOverdue) {
            it->isActive = windowActive;
        }
        if (it->isInCalendarWindow != windowActive) {
            it->isInCalendarWindow = windowActive;
            recalculateAndEmitRules();
            emit profilesUpdated();
        }
    }
}

// 11. Odblokowanie profilu
void ProfileManager::unlockProfile(const QString &profileName)
{
    auto it = m_profiles.find(profileName);
    if (it != m_profiles.end()) {
        it->isLocked = false;
        it->lockType = LockType::None;
        it->requiredChars = 0;
        it->lockUntilTime = QDateTime();
        persistProfile(it.value());
        emit profilesUpdated();
    }
}

// 12. Ustawienie blokady
void ProfileManager::setProfileLock(const QString &profileName, LockType type, int chars, const QDateTime &untilTime)
{
    auto it = m_profiles.find(profileName);
    if (it != m_profiles.end()) {
        BlockProfile &p = it.value();
        p.lockType = type;
        p.requiredChars = chars;
        p.lockUntilTime = untilTime;
        p.isLocked = (type != LockType::None);
        persistProfile(p);
        emit profilesUpdated();
    }
}

// 13. Aktywacja profilu (uzbrojenie)
void ProfileManager::setProfileActive(const QString &profileName, bool active)
{
    auto it = m_profiles.find(profileName);
    if (it != m_profiles.end()) {
        it->isActive = active;
        persistProfile(it.value());
        recalculateAndEmitRules();
        emit profilesUpdated();
    }
}

// 14. Calkowity reset profilow w pamieci
void ProfileManager::resetAll()
{
    m_profiles.clear();
    m_isOnBreak = false;
    m_breakTimer->stop();
    recalculateAndEmitRules();
    emit profilesUpdated();
}

// 15. Usuniecie profilu
void ProfileManager::deleteProfile(const QString &profileName)
{
    auto it = m_profiles.find(profileName);
    if (it != m_profiles.end()) {
        if (it->isOverdue) return;
        m_profiles.erase(it);
        DatabaseManager::instance().deleteProfile(profileName);
        recalculateAndEmitRules();
        emit profilesUpdated();
    }
}

// 16. Dodanie lub edycja profilu
void ProfileManager::addOrUpdateProfile(const BlockProfile &profile)
{
    m_profiles.insert(profile.name, profile);
    persistProfile(profile);
    recalculateAndEmitRules();
    emit profilesUpdated();
}

// 17. Sprawdzenie stanu klodki: profil nieaktywny lub poza oknem kalendarza NIE jest zablokowany do edycji
bool ProfileManager::isProfileLocked(const QString &name) const
{
    if (m_debugMode) return false;

    auto it = m_profiles.constFind(name);
    if (it == m_profiles.constEnd()) return false;
    const BlockProfile &p = it.value();
    if (!p.isLocked) return false;

    // Jesli profil dziala wg kalendarza i jest poza oknem sesji, restrykcje blokady edycji nie obowiazuja
    if (p.scheduleType == ScheduleType::ScheduledInCalendar) {
        if (!p.isInCalendarWindow) return false;
    } else {
        if (!p.isActive) return false;
    }

    if (p.lockType == LockType::UntilTime) {
        return QDateTime::currentDateTime() < p.lockUntilTime;
    }
    return true;
}

// 18. Sprawdzenie aktywnego trybu Debug Mode
bool ProfileManager::isDebugMode() const
{
    return m_debugMode;
}

// 19. Wlaczenie / wylaczenie trybu Debug Mode
void ProfileManager::setDebugMode(bool enabled)
{
    if (m_debugMode != enabled) {
        m_debugMode = enabled;
        recalculateAndEmitRules();
        emit debugModeChanged(m_debugMode);
        emit profilesUpdated();
    }
}

// 20. Pobranie wskaznika do profilu
const BlockProfile *ProfileManager::getProfile(const QString &name) const
{
    auto it = m_profiles.constFind(name);
    if (it != m_profiles.constEnd()) {
        return &(it.value());
    }
    return nullptr;
}

// 21. Pobranie wszystkich profili
const QMap<QString, BlockProfile> &ProfileManager::getAllProfiles() const
{
    return m_profiles;
}

// 22. Wczytanie profili z bazy SQLite
void ProfileManager::loadFromDatabase()
{
    m_profiles.clear();

    if (!DatabaseManager::instance().isOpen()) {
        return;
    }

    QSqlQuery q = DatabaseManager::instance().executeQuery(
        "SELECT name, is_locked, web_exceptions, blocked_apps, schedule_type, breaks_config FROM profiles;"
    );

    while (q.next()) {
        BlockProfile p;
        p.name = q.value(0).toString();
        p.isLocked = (q.value(1).toInt() == 1);
        p.websiteExceptions = q.value(2).toString().split("\n", Qt::SkipEmptyParts);
        p.blockedApps = q.value(3).toString().split("\n", Qt::SkipEmptyParts);
        p.blockedWebsites << "*";

        QString sType = q.value(4).toString();
        p.scheduleType = (sType == "calendar") ? ScheduleType::ScheduledInCalendar : ScheduleType::BlockedAtAllTimes;

        QJsonDocument bDoc = QJsonDocument::fromJson(q.value(5).toString().toUtf8());
        if (bDoc.isObject()) {
            QJsonObject bObj = bDoc.object();
            p.breaks.charBreakEnabled = bObj.value("char_enabled").toBool(true);
            p.breaks.charBreakChars = bObj.value("char_chars").toInt(1000);
            p.breaks.charBreakMinutes = bObj.value("char_min").toInt(15);
            p.breaks.clickBreakEnabled = bObj.value("click_enabled").toBool(true);
            p.breaks.clickBreakMinutes = bObj.value("click_min").toInt(10);
            p.breaks.criticalBreakEnabled = bObj.value("crit_enabled").toBool(true);
            p.breaks.criticalBreakMinutes = bObj.value("crit_min").toInt(3);
            p.breaks.lastCriticalUsage = QDateTime::fromString(bObj.value("crit_last").toString(), Qt::ISODate);
        }

        p.isOverdue = (p.name == "overdue");
        p.isActive = false;
        p.isInCalendarWindow = false;

        m_profiles.insert(p.name, p);
    }
    q.finish();

    if (!m_profiles.contains("overdue")) {
        BlockProfile overdue;
        overdue.name = "overdue";
        overdue.blockedWebsites << "*";
        overdue.websiteExceptions << "*.pdf" << "*altium*" << "*arduino*" << "*github*";
        overdue.blockedApps << "*note*" << "discord.exe" << "steam.exe";
        overdue.isActive = false;
        overdue.isInCalendarWindow = false;
        overdue.isOverdue = true;
        overdue.scheduleType = ScheduleType::ScheduledInCalendar;
        m_profiles.insert(overdue.name, overdue);
        persistProfile(overdue);
    }
}

// 23. Konstruktor ProfileManager
ProfileManager::ProfileManager(QObject *parent)
    : QObject(parent)
    , m_isOnBreak(false)
    , m_debugMode(false)
    , m_breakSecondsRemaining(0)
    , m_breakTimer(new QTimer(this))
    , m_lockExpirationTimer(new QTimer(this))
{
    connect(m_breakTimer, &QTimer::timeout, this, &ProfileManager::onBreakTimerTick);
    connect(m_lockExpirationTimer, &QTimer::timeout, this, &ProfileManager::checkLockExpirations);

    loadFromDatabase();
    m_lockExpirationTimer->start(10000);
}
