// Plik implementacji modulu DatabaseManager.
// Zapewnia trwaly zapis i odczyt danych w SQLite oraz backup JSON z obsluga kolumny linked_subject.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "database_manager.h"
#include <windows.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSqlError>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QDebug>

static const wchar_t *SHARED_MEM_NAME = L"Local\\AmogusRulesMemoryMap";
static const DWORD SHARED_MEM_SIZE = 65536;

// 1. Zapis bufora do pamieci wspoldzielonej RAM
static void writeRawBufferToSharedMemory(void *buffer, const QString &text)
{
    if (!buffer) return;
    QByteArray bytes = text.toUtf8();
    DWORD copySize = static_cast<DWORD>(bytes.size());
    if (copySize >= SHARED_MEM_SIZE - sizeof(DWORD)) {
        copySize = SHARED_MEM_SIZE - sizeof(DWORD) - 1;
    }
    memcpy(buffer, &copySize, sizeof(DWORD));
    memcpy(static_cast<char *>(buffer) + sizeof(DWORD), bytes.constData(), copySize);
}

// 2. Odczyt bufora z pamieci wspoldzielonej RAM
static QString readRawBufferFromSharedMemory(void *buffer)
{
    if (!buffer) return QString();
    DWORD dataSize = 0;
    memcpy(&dataSize, buffer, sizeof(DWORD));
    if (dataSize == 0 || dataSize >= SHARED_MEM_SIZE) return QString();
    QByteArray bytes(static_cast<char *>(buffer) + sizeof(DWORD), dataSize);
    return QString::fromUtf8(bytes);
}

// 3. Weryfikacja zapisu do katalogu
static bool testDirectoryWritable(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) return false;
    }
    QString testFilePath = dir.filePath("write_test.tmp");
    QFile testFile(testFilePath);
    if (!testFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    testFile.write("ok", 2);
    testFile.close();
    testFile.remove();
    return true;
}

// 4. Domyslna sciezka pliku backupu JSON
QString DatabaseManager::getDefaultBackupFilePath() const
{
    QDir projDir("C:/Qt_projects/Amogus");
    if (projDir.exists() && testDirectoryWritable("C:/Qt_projects/Amogus")) {
        return "C:/Qt_projects/Amogus/backup.json";
    }
    return QDir(QCoreApplication::applicationDirPath()).filePath("backup.json");
}

// 5. Eksport wszystkich tabel bazy SQLite do pliku JSON
bool DatabaseManager::exportBackupToJson(const QString &filePath)
{
    if (!isOpen()) return false;

    QString targetPath = filePath.isEmpty() ? getDefaultBackupFilePath() : filePath;

    QJsonObject root;
    root["version"] = 1;
    root["created_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 1. app_metadata
    {
        QJsonArray arr;
        QSqlQuery q(getDatabase());
        if (q.exec("SELECT key, val FROM app_metadata;")) {
            while (q.next()) {
                QJsonObject item;
                item["key"] = q.value(0).toString();
                item["val"] = q.value(1).toString();
                arr.append(item);
            }
        }
        root["app_metadata"] = arr;
    }

    // 2. profiles
    {
        QJsonArray arr;
        QSqlQuery q(getDatabase());
        if (q.exec("SELECT name, is_locked, web_exceptions, blocked_apps, schedule_type, breaks_config FROM profiles;")) {
            while (q.next()) {
                QJsonObject item;
                item["name"] = q.value(0).toString();
                item["is_locked"] = q.value(1).toInt();
                item["web_exceptions"] = q.value(2).toString();
                item["blocked_apps"] = q.value(3).toString();
                item["schedule_type"] = q.value(4).toString();
                item["breaks_config"] = q.value(5).toString();
                arr.append(item);
            }
        }
        root["profiles"] = arr;
    }

    // 3. calendar_events
    {
        QJsonArray arr;
        QSqlQuery q(getDatabase());
        if (q.exec("SELECT id, series_id, title, category, start_time, end_time, profile_name, color, google_id, updated_at, linked_subject FROM calendar_events;")) {
            while (q.next()) {
                QJsonObject item;
                item["id"] = q.value(0).toString();
                item["series_id"] = q.value(1).toString();
                item["title"] = q.value(2).toString();
                item["category"] = q.value(3).toString();
                item["start_time"] = q.value(4).toString();
                item["end_time"] = q.value(5).toString();
                item["profile_name"] = q.value(6).toString();
                item["color"] = q.value(7).toString();
                item["google_id"] = q.value(8).toString();
                item["updated_at"] = q.value(9).toLongLong();
                item["linked_subject"] = q.value(10).toString();
                arr.append(item);
            }
        }
        root["calendar_events"] = arr;
    }

    // 4. subjects
    {
        QJsonArray arr;
        QSqlQuery q(getDatabase());
        if (q.exec("SELECT name, color, is_lecture, notes FROM subjects;")) {
            while (q.next()) {
                QJsonObject item;
                item["name"] = q.value(0).toString();
                item["color"] = q.value(1).toString();
                item["is_lecture"] = q.value(2).toInt();
                item["notes"] = q.value(3).toString();
                arr.append(item);
            }
        }
        root["subjects"] = arr;
    }

    // 5. subject_classes
    {
        QJsonArray arr;
        QSqlQuery q(getDatabase());
        if (q.exec("SELECT id, subject_name, class_type, class_index, start_time, end_time, label FROM subject_classes;")) {
            while (q.next()) {
                QJsonObject item;
                item["id"] = q.value(0).toString();
                item["subject_name"] = q.value(1).toString();
                item["class_type"] = q.value(2).toString();
                item["class_index"] = q.value(3).toInt();
                item["start_time"] = q.value(4).toString();
                item["end_time"] = q.value(5).toString();
                item["label"] = q.value(6).toString();
                arr.append(item);
            }
        }
        root["subject_classes"] = arr;
    }

    // 6. overdue_tracker
    {
        QJsonArray arr;
        QSqlQuery q(getDatabase());
        if (q.exec("SELECT subject, frequency_weeks, current_stage, status_icon FROM overdue_tracker;")) {
            while (q.next()) {
                QJsonObject item;
                item["subject"] = q.value(0).toString();
                item["frequency_weeks"] = q.value(1).toInt();
                item["current_stage"] = q.value(2).toInt();
                item["status_icon"] = q.value(3).toString();
                arr.append(item);
            }
        }
        root["overdue_tracker"] = arr;
    }

    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// 6. Import wszystkich tabel bazy z pliku JSON
bool DatabaseManager::importBackupFromJson(const QString &filePath)
{
    if (!isOpen()) return false;

    QString targetPath = filePath.isEmpty() ? getDefaultBackupFilePath() : filePath;
    QFile file(targetPath);
    if (!file.open(QIODevice::ReadOnly)) {
        targetPath = QDir(QCoreApplication::applicationDirPath()).filePath("backup.json");
        file.setFileName(targetPath);
        if (!file.open(QIODevice::ReadOnly)) return false;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    QSqlDatabase db = getDatabase();
    db.transaction();

    {
        QSqlQuery q(db);
        q.exec("DELETE FROM app_metadata;");
        q.exec("DELETE FROM profiles;");
        q.exec("DELETE FROM calendar_events;");
        q.exec("DELETE FROM subjects;");
        q.exec("DELETE FROM subject_classes;");
        q.exec("DELETE FROM overdue_tracker;");
    }

    for (const auto &val : root.value("app_metadata").toArray()) {
        QJsonObject o = val.toObject();
        QSqlQuery q(db);
        q.prepare("INSERT OR REPLACE INTO app_metadata (key, val) VALUES (:k, :v);");
        q.bindValue(":k", o.value("key").toString());
        q.bindValue(":v", o.value("val").toString());
        q.exec();
    }

    for (const auto &val : root.value("profiles").toArray()) {
        QJsonObject o = val.toObject();
        QSqlQuery q(db);
        q.prepare("INSERT OR REPLACE INTO profiles (name, is_locked, web_exceptions, blocked_apps, schedule_type, breaks_config) "
                  "VALUES (:name, :locked, :web, :apps, :stype, :bcfg);");
        q.bindValue(":name", o.value("name").toString());
        q.bindValue(":locked", o.value("is_locked").toInt());
        q.bindValue(":web", o.value("web_exceptions").toString());
        q.bindValue(":apps", o.value("blocked_apps").toString());
        q.bindValue(":stype", o.value("schedule_type").toString());
        q.bindValue(":bcfg", o.value("breaks_config").toString());
        q.exec();
    }

    for (const auto &val : root.value("calendar_events").toArray()) {
        QJsonObject o = val.toObject();
        QSqlQuery q(db);
        q.prepare("INSERT OR REPLACE INTO calendar_events (id, series_id, title, category, start_time, end_time, profile_name, color, google_id, updated_at, linked_subject) "
                  "VALUES (:id, :sid, :title, :cat, :start, :end, :prof, :col, :gid, :upd, :lsub);");
        q.bindValue(":id", o.value("id").toString());
        q.bindValue(":sid", o.value("series_id").toString());
        q.bindValue(":title", o.value("title").toString());
        q.bindValue(":cat", o.value("category").toString());
        q.bindValue(":start", o.value("start_time").toString());
        q.bindValue(":end", o.value("end_time").toString());
        q.bindValue(":prof", o.value("profile_name").toString());
        q.bindValue(":col", o.value("color").toString());
        q.bindValue(":gid", o.value("google_id").toString());
        q.bindValue(":upd", o.value("updated_at").toVariant().toLongLong());
        q.bindValue(":lsub", o.value("linked_subject").toString());
        q.exec();
    }

    for (const auto &val : root.value("subjects").toArray()) {
        QJsonObject o = val.toObject();
        QSqlQuery q(db);
        q.prepare("INSERT OR REPLACE INTO subjects (name, color, is_lecture, notes) VALUES (:name, :color, :is_lec, :notes);");
        q.bindValue(":name", o.value("name").toString());
        q.bindValue(":color", o.value("color").toString());
        q.bindValue(":is_lec", o.value("is_lecture").toInt());
        q.bindValue(":notes", o.value("notes").toString());
        q.exec();
    }

    for (const auto &val : root.value("subject_classes").toArray()) {
        QJsonObject o = val.toObject();
        QSqlQuery q(db);
        q.prepare("INSERT OR REPLACE INTO subject_classes (id, subject_name, class_type, class_index, start_time, end_time, label) "
                  "VALUES (:id, :sname, :ctype, :cidx, :st, :et, :lbl);");
        q.bindValue(":id", o.value("id").toString());
        q.bindValue(":sname", o.value("subject_name").toString());
        q.bindValue(":ctype", o.value("class_type").toString());
        q.bindValue(":cidx", o.value("class_index").toInt());
        q.bindValue(":st", o.value("start_time").toString());
        q.bindValue(":et", o.value("end_time").toString());
        q.bindValue(":lbl", o.value("label").toString());
        q.exec();
    }

    for (const auto &val : root.value("overdue_tracker").toArray()) {
        QJsonObject o = val.toObject();
        QSqlQuery q(db);
        q.prepare("INSERT OR REPLACE INTO overdue_tracker (subject, frequency_weeks, current_stage, status_icon) VALUES (:sub, :freq, :stage, :icon);");
        q.bindValue(":sub", o.value("subject").toString());
        q.bindValue(":freq", o.value("frequency_weeks").toInt());
        q.bindValue(":stage", o.value("current_stage").toInt());
        q.bindValue(":icon", o.value("status_icon").toString());
        q.exec();
    }

    db.commit();

    QSqlQuery pragma(db);
    pragma.exec("PRAGMA wal_checkpoint(FULL);");
    pragma.finish();

    return true;
}

// 7. Odczyt generycznej wartosci z app_metadata
QString DatabaseManager::getMetadataValue(const QString &key, const QString &defaultVal)
{
    if (!isOpen()) return defaultVal;
    QSqlQuery q(getDatabase());
    q.prepare("SELECT val FROM app_metadata WHERE key = :key;");
    q.bindValue(":key", key);
    if (q.exec() && q.next()) {
        QString val = q.value(0).toString();
        q.finish();
        return val;
    }
    q.finish();
    return defaultVal;
}

// 8. Zapis generycznej wartosci do app_metadata
void DatabaseManager::setMetadataValue(const QString &key, const QString &val)
{
    setMetadataFlag(key, val);
}

// 9. Zapis rekordu Overdue Tracker
bool DatabaseManager::saveOverdueEntry(const QString &subject, int frequencyWeeks, int currentStage, const QString &statusIcon)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("INSERT OR REPLACE INTO overdue_tracker (subject, frequency_weeks, current_stage, status_icon) "
              "VALUES (:sub, :freq, :stage, :icon);");
    q.bindValue(":sub", subject);
    q.bindValue(":freq", frequencyWeeks);
    q.bindValue(":stage", currentStage);
    q.bindValue(":icon", statusIcon);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 10. Usuniecie terminow zajec przedmiotu
bool DatabaseManager::clearSubjectClasses(const QString &subjectName)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("DELETE FROM subject_classes WHERE subject_name = :sub;");
    q.bindValue(":sub", subjectName);
    bool ok = q.exec();
    q.finish();
    return ok;
}

// 11. Usuniecie pojedynczych zajec
bool DatabaseManager::deleteSubjectClassRecord(const QString &id)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("DELETE FROM subject_classes WHERE id = :id;");
    q.bindValue(":id", id);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 12. Zapis jednostki zajeciowej
bool DatabaseManager::saveSubjectClassRecord(const QString &id, const QString &subjectName, const QString &classType,
                                             int classIndex, const QString &startStr, const QString &endStr, const QString &label)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("INSERT OR REPLACE INTO subject_classes (id, subject_name, class_type, class_index, start_time, end_time, label) "
              "VALUES (:id, :sname, :ctype, :cidx, :st, :et, :lbl);");
    q.bindValue(":id", id);
    q.bindValue(":sname", subjectName);
    q.bindValue(":ctype", classType);
    q.bindValue(":cidx", classIndex);
    q.bindValue(":st", startStr);
    q.bindValue(":et", endStr);
    q.bindValue(":lbl", label);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 13. Usuniecie przedmiotu i jego jednostek
bool DatabaseManager::deleteSubjectRecord(const QString &name)
{
    if (!isOpen()) return false;
    clearSubjectClasses(name);
    QSqlQuery q(getDatabase());
    q.prepare("DELETE FROM subjects WHERE name = :name;");
    q.bindValue(":name", name);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 14. Zapis przedmiotu wraz z polem notatek
bool DatabaseManager::saveSubjectRecord(const QString &name, const QString &color, bool isLecture, const QString &notes)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("INSERT OR REPLACE INTO subjects (name, color, is_lecture, notes) VALUES (:name, :color, :is_lec, :notes);");
    q.bindValue(":name", name);
    q.bindValue(":color", color);
    q.bindValue(":is_lec", isLecture ? 1 : 0);
    q.bindValue(":notes", notes);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 15. Usuniecie serii powtorzen kalendarza
bool DatabaseManager::deleteCalendarSeries(const QString &seriesId)
{
    if (!isOpen() || seriesId.isEmpty()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("DELETE FROM calendar_events WHERE series_id = :sid;");
    q.bindValue(":sid", seriesId);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 16. Usuniecie zdarzenia kalendarza
bool DatabaseManager::deleteCalendarEvent(const QString &id)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("DELETE FROM calendar_events WHERE id = :id;");
    q.bindValue(":id", id);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 17. Zapis zdarzenia kalendarza wraz z kolumna linked_subject
bool DatabaseManager::saveCalendarEvent(const QString &id, const QString &seriesId, const QString &title,
                                        const QString &category, const QString &startStr, const QString &endStr,
                                        const QString &profileName, const QString &color,
                                        const QString &googleId, qint64 updatedAt, const QString &linkedSubject)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("INSERT OR REPLACE INTO calendar_events (id, series_id, title, category, start_time, end_time, profile_name, color, google_id, updated_at, linked_subject) "
              "VALUES (:id, :sid, :title, :cat, :start, :end, :prof, :col, :gid, :upd, :lsub);");
    q.bindValue(":id", id);
    q.bindValue(":sid", seriesId);
    q.bindValue(":title", title);
    q.bindValue(":cat", category);
    q.bindValue(":start", startStr);
    q.bindValue(":end", endStr);
    q.bindValue(":prof", profileName);
    q.bindValue(":col", color);
    q.bindValue(":gid", googleId);
    q.bindValue(":upd", updatedAt);
    q.bindValue(":lsub", linkedSubject);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 18. Usuniecie profilu blokowania
bool DatabaseManager::deleteProfile(const QString &name)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("DELETE FROM profiles WHERE name = :name;");
    q.bindValue(":name", name);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 19. Zapis profilu blokowania
bool DatabaseManager::saveProfile(const QString &name, bool isLocked, const QString &webEx, const QString &apps,
                                  const QString &scheduleType, const QString &breaksConfig)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("INSERT OR REPLACE INTO profiles (name, is_locked, web_exceptions, blocked_apps, schedule_type, breaks_config) "
              "VALUES (:name, :locked, :web, :apps, :stype, :bcfg);");
    q.bindValue(":name", name);
    q.bindValue(":locked", isLocked ? 1 : 0);
    q.bindValue(":web", webEx);
    q.bindValue(":apps", apps);
    q.bindValue(":stype", scheduleType);
    q.bindValue(":bcfg", breaksConfig);
    bool ok = q.exec();
    q.finish();

    exportBackupToJson();
    return ok;
}

// 20. Zapis flagi metadanych
void DatabaseManager::setMetadataFlag(const QString &key, const QString &val)
{
    if (!isOpen()) return;
    QSqlQuery q(getDatabase());
    q.prepare("INSERT OR REPLACE INTO app_metadata (key, val) VALUES (:key, :val);");
    q.bindValue(":key", key);
    q.bindValue(":val", val);
    q.exec();
    q.finish();

    exportBackupToJson();
}

// 21. Odczyt flagi metadanych
bool DatabaseManager::isMetadataFlag(const QString &key)
{
    if (!isOpen()) return false;
    QSqlQuery q(getDatabase());
    q.prepare("SELECT val FROM app_metadata WHERE key = :key;");
    q.bindValue(":key", key);
    if (q.exec() && q.next()) {
        QString val = q.value(0).toString();
        q.finish();
        return val == "1" || val.compare("true", Qt::CaseInsensitive) == 0;
    }
    q.finish();
    return false;
}

// 22. Pomocnicze wykonanie zapytania
QSqlQuery DatabaseManager::executeQuery(const QString &sql)
{
    if (!isOpen()) return QSqlQuery();
    QSqlQuery q(getDatabase());
    q.exec(sql);
    return q;
}

// 23. Odczyt stanu blokady pliku
bool DatabaseManager::isKernelLockActive() const
{
    return m_kernelLockEnabled;
}

// 24. Zmiana stanu blokady pliku
void DatabaseManager::setKernelLockEnabled(bool enabled)
{
    m_kernelLockEnabled = enabled;
}

// 25. Odczyt regul z pamieci RAM
QString DatabaseManager::readRulesFromSharedMemory()
{
    return readRawBufferFromSharedMemory(m_sharedMemoryBuffer);
}

// 26. Kopia regul w pamieci RAM
void DatabaseManager::syncRulesToSharedMemory(const QString &activeRulesJson)
{
    writeRawBufferToSharedMemory(m_sharedMemoryBuffer, activeRulesJson);
}

// 27. Tworzenie schematu tabel bazy danych z kolumna linked_subject
bool DatabaseManager::createTables()
{
    QSqlQuery q(getDatabase());

    q.exec("CREATE TABLE IF NOT EXISTS app_metadata (key TEXT PRIMARY KEY, val TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS profiles (name TEXT PRIMARY KEY, is_locked INTEGER, web_exceptions TEXT, blocked_apps TEXT, schedule_type TEXT, breaks_config TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS calendar_events (id TEXT PRIMARY KEY, series_id TEXT, title TEXT, category TEXT, start_time TEXT, end_time TEXT, profile_name TEXT, color TEXT, google_id TEXT, updated_at INTEGER, linked_subject TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS subjects (name TEXT PRIMARY KEY, color TEXT, is_lecture INTEGER, notes TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS subject_classes (id TEXT PRIMARY KEY, subject_name TEXT, class_type TEXT, class_index INTEGER, start_time TEXT, end_time TEXT, label TEXT);");
    q.exec("CREATE TABLE IF NOT EXISTS overdue_tracker (subject TEXT PRIMARY KEY, frequency_weeks INTEGER, current_stage INTEGER, status_icon TEXT);");

    q.exec("ALTER TABLE calendar_events ADD COLUMN linked_subject TEXT;");

    q.finish();
    return true;
}

// 28. Autodiagnostyka zapisu
bool DatabaseManager::runSelfTest()
{
    QSqlQuery q(getDatabase());
    q.exec("CREATE TABLE IF NOT EXISTS _system_check (key TEXT PRIMARY KEY, val TEXT);");
    q.prepare("INSERT OR REPLACE INTO _system_check (key, val) VALUES ('test_key', 'test_value');");
    bool ok = q.exec();
    q.finish();
    return ok;
}

// 29. Wyznaczanie sciezki pliku bazy
QString DatabaseManager::getDatabaseFilePath() const
{
    if (!m_activeDbPath.isEmpty()) {
        return m_activeDbPath;
    }

    QDir projDir("C:/Qt_projects/Amogus");
    if (projDir.exists() && testDirectoryWritable("C:/Qt_projects/Amogus")) {
        m_activeDbPath = "C:/Qt_projects/Amogus/data.db";
        return m_activeDbPath;
    }

    QString appDir = QCoreApplication::applicationDirPath();
    if (testDirectoryWritable(appDir)) {
        m_activeDbPath = QDir(appDir).filePath("data.db");
        return m_activeDbPath;
    }

    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir(appData).mkpath(".");
    m_activeDbPath = QDir(appData).filePath("data.db");
    return m_activeDbPath;
}

// 30. Sprawdzenie statusu polaczenia bazy
bool DatabaseManager::isOpen() const
{
    if (!QSqlDatabase::contains(QSqlDatabase::defaultConnection)) return false;
    return QSqlDatabase::database(QSqlDatabase::defaultConnection, false).isOpen();
}

// 31. Zestawienie polaczenia SQLite
QSqlDatabase DatabaseManager::getDatabase()
{
    QString dbPath = getDatabaseFilePath();

    if (QSqlDatabase::contains(QSqlDatabase::defaultConnection)) {
        QSqlDatabase existingDb = QSqlDatabase::database(QSqlDatabase::defaultConnection, false);
        if (existingDb.isValid() && existingDb.databaseName() == dbPath) {
            return existingDb;
        }
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    if (!db.isValid()) {
        qCritical() << "[DatabaseManager] Brak sterownika QSQLITE!";
        return db;
    }

    db.setDatabaseName(dbPath);

    if (!db.isOpen()) {
        if (!db.open()) {
            qCritical() << "[DatabaseManager] Nie mozna otworzyc pliku bazy:" << db.lastError().text();
        } else {
            QSqlQuery pragma(db);
            pragma.exec("PRAGMA busy_timeout = 10000;");
            pragma.exec("PRAGMA journal_mode = WAL;");
            pragma.exec("PRAGMA synchronous = NORMAL;");
            pragma.finish();
        }
    }

    return db;
}

// 32. Zamkniecie polaczenia z baza
void DatabaseManager::closeDatabase()
{
    if (!m_isInitialized) return;
    if (!QCoreApplication::instance()) return;

    exportBackupToJson();

    if (QSqlDatabase::contains(QSqlDatabase::defaultConnection)) {
        QSqlDatabase db = QSqlDatabase::database(QSqlDatabase::defaultConnection, false);
        if (db.isValid() && db.isOpen()) {
            QSqlQuery checkpoint(db);
            checkpoint.exec("PRAGMA wal_checkpoint(FULL);");
            checkpoint.finish();
            db.close();
        }
    }
    m_isInitialized = false;
}

// 33. Calkowity reset danych tabel
bool DatabaseManager::clearAllDatabaseData()
{
    if (!isOpen()) return false;

    QSqlDatabase db = getDatabase();
    QSqlQuery q(db);
    q.exec("DELETE FROM profiles;");
    q.exec("DELETE FROM calendar_events;");
    q.exec("DELETE FROM subjects;");
    q.exec("DELETE FROM subject_classes;");
    q.exec("DELETE FROM overdue_tracker;");
    q.exec("DELETE FROM app_metadata;");
    q.exec("DELETE FROM _system_check;");
    q.exec("PRAGMA wal_checkpoint(TRUNCATE);");
    q.exec("VACUUM;");
    q.finish();

    setMetadataFlag("defaults_seeded", "1");
    exportBackupToJson();
    return true;
}

// 34. Inicjalizacja bazy danych
bool DatabaseManager::initializeDatabase()
{
    if (m_isInitialized) return true;

    QSqlDatabase db = getDatabase();
    if (!db.isValid() || !db.isOpen()) {
        return false;
    }

    createTables();
    runSelfTest();

    m_sharedMemoryHandle = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        NULL,
        PAGE_READWRITE,
        0,
        SHARED_MEM_SIZE,
        SHARED_MEM_NAME
    );

    if (m_sharedMemoryHandle) {
        m_sharedMemoryBuffer = MapViewOfFile(
            static_cast<HANDLE>(m_sharedMemoryHandle),
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            SHARED_MEM_SIZE
        );
    }

    m_isInitialized = true;
    return true;
}

// 35. Instancja singletona
DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager inst;
    return inst;
}

// 36. Destruktor
DatabaseManager::~DatabaseManager()
{
    closeDatabase();
    if (m_sharedMemoryBuffer) {
        UnmapViewOfFile(m_sharedMemoryBuffer);
        m_sharedMemoryBuffer = nullptr;
    }
    if (m_sharedMemoryHandle) {
        CloseHandle(static_cast<HANDLE>(m_sharedMemoryHandle));
        m_sharedMemoryHandle = nullptr;
    }
}

// 37. Konstruktor
DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
    , m_sharedMemoryHandle(nullptr)
    , m_sharedMemoryBuffer(nullptr)
    , m_isInitialized(false)
    , m_kernelLockEnabled(true)
{
}
