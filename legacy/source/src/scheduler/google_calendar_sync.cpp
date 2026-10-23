// Plik implementacji daemona GoogleCalendarSync.
// Zapewnia debouncing operacji sieciowych w osobnym watku (eliminacja zacinania GUI),
// zgodnosc z Qt6 dla operacji na QMap, brak blokowania glownego watku oraz obsluge stref RFC3339.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "google_calendar_sync.h"
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <QDir>
#include <QFile>
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QDateTime>
#include <QThread>
#include <QTcpSocket>
#include <QDesktopServices>
#include <QUrl>
#include <QUrlQuery>
#include <sstream>
#include <algorithm>
#include <set>
#include <map>

#ifdef _WIN32
#define POPEN _popen
#define PCLOSE _pclose
#else
#define POPEN popen
#define PCLOSE pclose
#endif

// 1. Niskopoziomowe wykonanie komendy curl.exe
std::string GoogleCalendarSync::executeCurl(const std::string &command)
{
    std::string result;
    char buffer[4096];
    FILE *pipe = POPEN(command.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }
    PCLOSE(pipe);
    return result;
}

// 2. Formatowanie daty i godziny do formatu RFC3339 z offsetem strefy
QString GoogleCalendarSync::formatRfc3339(const QDateTime &dt)
{
    QDateTime localDt = dt.toLocalTime();
    QString base = localDt.toString("yyyy-MM-ddTHH:mm:ss");

    int offsetSecs = localDt.offsetFromUtc();
    QString sign = (offsetSecs >= 0) ? "+" : "-";
    int absSecs = std::abs(offsetSecs);
    int hours = absSecs / 3600;
    int mins = (absSecs % 3600) / 60;

    return QString("%1%2%3:%4")
        .arg(base)
        .arg(sign)
        .arg(hours, 2, 10, QChar('0'))
        .arg(mins, 2, 10, QChar('0'));
}

// 3. Generowanie unikalnej sygnatury wydarzenia
QString GoogleCalendarSync::createSignature(const QString &dateStr, const QString &timeStr, const QString &title)
{
    QString cleanTitle = title.toLower().trimmed();
    cleanTitle.remove(QRegularExpression("\\[.*?\\]"));
    cleanTitle = cleanTitle.trimmed();
    return QString("%1_%2_%3").arg(dateStr.trimmed(), timeStr.trimmed().left(5), cleanTitle);
}

// 4. Konwersja Color ID Google Calendar na kod HEX
QString GoogleCalendarSync::colorIdToHex(const QString &colorId)
{
    static const QMap<QString, QString> palette = {
        {"1",  "#7986cb"}, // Lavender
        {"2",  "#33b679"}, // Sage
        {"3",  "#8e24aa"}, // Grape
        {"4",  "#e67c73"}, // Flamingo
        {"5",  "#f6bf26"}, // Banana
        {"6",  "#f4511e"}, // Tangerine
        {"7",  "#039be5"}, // Peacock
        {"8",  "#616161"}, // Graphite
        {"9",  "#3f51b5"}, // Blueberry
        {"10", "#0b8043"}, // Basil
        {"11", "#d50000"}  // Tomato
    };
    return palette.value(colorId, "#039be5");
}

// 5. Dopasowanie HEX do najblizszego koloru Google (RGB Euclidean distance)
QString GoogleCalendarSync::hexToGoogleColorId(const QString &hexColor, const QString &category)
{
    QString cat = category.toUpper().trimmed();
    if (cat.contains("EXAM") || cat.contains("KOLOKWIUM") || cat.contains("EGZAMIN")) return "11";
    if (cat.contains("WYK")) return "7";
    if (cat.contains("CWI")) return "5";
    if (cat.contains("LAB")) return "10";
    if (cat.contains("PROJ")) return "6";

    QColor target(hexColor);
    if (!target.isValid()) return "7";

    static const std::vector<std::pair<QString, QColor>> googleColors = {
        {"1",  QColor("#7986cb")},
        {"2",  QColor("#33b679")},
        {"3",  QColor("#8e24aa")},
        {"4",  QColor("#e67c73")},
        {"5",  QColor("#f6bf26")},
        {"6",  QColor("#f4511e")},
        {"7",  QColor("#039be5")},
        {"8",  QColor("#616161")},
        {"9",  QColor("#3f51b5")},
        {"10", QColor("#0b8043")},
        {"11", QColor("#d50000")}
    };

    QString bestId = "7";
    double minDistance = 1e9;

    for (const auto &[id, col] : googleColors) {
        double dR = target.red() - col.red();
        double dG = target.green() - col.green();
        double dB = target.blue() - col.blue();
        double dist = dR * dR + dG * dG + dB * dB;
        if (dist < minDistance) {
            minDistance = dist;
            bestId = id;
        }
    }

    return bestId;
}

// 6. Sciezka do pliku credentials.json
QString GoogleCalendarSync::getCredentialsFilePath() const
{
    const QStringList candidates = {
        "C:/Qt_projects/amogus/Library/credentials.json",
        "Library/credentials.json",
        "C:/Qt_projects/amogus/#Library Files/credentials.json",
        "#Library Files/credentials.json"
    };

    for (const auto &path : candidates) {
        if (QFile::exists(path)) return path;
    }
    return "C:/Qt_projects/amogus/Library/credentials.json";
}

// 7. Sciezka do pliku tokenu
QString GoogleCalendarSync::getTokenFilePath() const
{
    const QStringList candidates = {
        "C:/Qt_projects/amogus/Library/google_token.json",
        "Library/google_token.json",
        "C:/Qt_projects/amogus/#Settings/google_token.json",
        "#Settings/google_token.json"
    };

    for (const auto &path : candidates) {
        if (QFile::exists(path)) return path;
    }
    return "C:/Qt_projects/amogus/Library/google_token.json";
}

// 8. Odczyt danych z credentials.json
bool GoogleCalendarSync::loadCredentials(std::string &clientId, std::string &clientSecret, std::string &tokenUri)
{
    QFile f(getCredentialsFilePath());
    if (!f.open(QIODevice::ReadOnly)) return false;

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    QJsonObject installed = root.contains("installed") ? root["installed"].toObject() : root["web"].toObject();
    if (installed.isEmpty()) installed = root;

    clientId = installed.value("client_id").toString().toStdString();
    clientSecret = installed.value("client_secret").toString().toStdString();
    tokenUri = installed.value("token_uri").toString("https://oauth2.googleapis.com/token").toStdString();

    return (!clientId.empty() && !clientSecret.empty());
}

// 9. Zapis aktywnego tokenu
void GoogleCalendarSync::saveToken()
{
    QJsonObject root;
    root["refresh_token"] = QString::fromStdString(m_refreshToken);
    root["access_token"] = QString::fromStdString(m_accessToken);

    const QStringList targetPaths = {
        getTokenFilePath(),
        "C:/Qt_projects/amogus/Library/google_token.json",
        "Library/google_token.json"
    };

    for (const auto &path : targetPaths) {
        QFile f(path);
        QDir().mkpath(QFileInfo(f).dir().absolutePath());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
            f.close();
        }
    }
}

// 10. Wczytanie zapisanego tokenu
bool GoogleCalendarSync::loadToken()
{
    const QStringList checkPaths = {
        getTokenFilePath(),
        "C:/Qt_projects/amogus/Library/google_token.json",
        "Library/google_token.json",
        "C:/Qt_projects/amogus/#Settings/google_token.json",
        "#Settings/google_token.json",
        getCredentialsFilePath()
    };

    for (const auto &path : checkPaths) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
            f.close();
            if (doc.isObject()) {
                QJsonObject root = doc.object();
                QString refToken = root.value("refresh_token").toString();
                if (refToken.isEmpty() && root.contains("installed")) {
                    refToken = root.value("installed").toObject().value("refresh_token").toString();
                }
                if (refToken.isEmpty() && root.contains("web")) {
                    refToken = root.value("web").toObject().value("refresh_token").toString();
                }

                if (!refToken.isEmpty()) {
                    m_refreshToken = refToken.toStdString();
                    m_accessToken = root.value("access_token").toString().toStdString();
                    m_tokenExpiryTime = std::chrono::steady_clock::now();
                    return true;
                }
            }
        }
    }
    return false;
}

// 11. Wymiana kodu autoryzacji na tokeny
bool GoogleCalendarSync::exchangeAuthCodeForTokens(const QString &authCode, int redirectPort)
{
    if (m_clientId.empty() || m_clientSecret.empty()) return false;

    QString redirectUri = QString("http://127.0.0.1:%1").arg(redirectPort);
    QMetaObject::invokeMethod(this, [this]() {
        emit syncLogMessage("Wymiana kodu autoryzacji na token sesji Google OAuth...");
    }, Qt::QueuedConnection);

    std::ostringstream cmd;
    cmd << "curl.exe -s --max-time 15 -X POST \"" << m_tokenUri << "\" "
        << "-d \"client_id=" << m_clientId << "\" "
        << "-d \"client_secret=" << m_clientSecret << "\" "
        << "-d \"code=" << authCode.toStdString() << "\" "
        << "-d \"grant_type=authorization_code\" "
        << "-d \"redirect_uri=" << redirectUri.toStdString() << "\"";

    std::string resp = executeCurl(cmd.str());
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(resp));
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    if (root.contains("refresh_token")) {
        m_refreshToken = root.value("refresh_token").toString().toStdString();
    }
    if (root.contains("access_token")) {
        m_accessToken = root.value("access_token").toString().toStdString();
        int expSec = root.value("expires_in").toInt(3600);
        m_tokenExpiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(expSec - 60);
    }

    if (!m_refreshToken.empty()) {
        saveToken();
        QMetaObject::invokeMethod(this, [this]() {
            emit syncLogMessage("Pomyślnie uzyskano i utrwalono token Google!");
            emit syncFinished(true, "Autoryzacja zakończona sukcesem! Kalendarz połączony.");
        }, Qt::QueuedConnection);
        triggerTwoWaySync();
        return true;
    }

    return false;
}

// 12. Odswiezenie access_token przez cURL
bool GoogleCalendarSync::refreshAccessToken()
{
    if (m_refreshToken.empty() || m_clientId.empty()) return false;

    std::ostringstream cmd;
    cmd << "curl.exe -s --max-time 12 -X POST \"" << m_tokenUri << "\" "
        << "-d \"client_id=" << m_clientId << "\" "
        << "-d \"client_secret=" << m_clientSecret << "\" "
        << "-d \"refresh_token=" << m_refreshToken << "\" "
        << "-d \"grant_type=refresh_token\"";

    std::string resp = executeCurl(cmd.str());
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(resp));
    if (!doc.isObject()) return false;

    QJsonObject root = doc.object();
    if (root.contains("access_token")) {
        m_accessToken = root.value("access_token").toString().toStdString();
        int expiresIn = root.value("expires_in").toInt(3600);
        m_tokenExpiryTime = std::chrono::steady_clock::now() + std::chrono::seconds(expiresIn - 60);
        saveToken();
        return true;
    }

    return false;
}

// 13. Pobranie waznego tokenu
std::string GoogleCalendarSync::getValidAccessToken()
{
    if (!m_accessToken.empty() && std::chrono::steady_clock::now() < m_tokenExpiryTime) {
        return m_accessToken;
    }
    if (refreshAccessToken()) {
        return m_accessToken;
    }
    return "";
}

// 14. Usuniecie pojedynczego zdarzenia z Google
void GoogleCalendarSync::deleteRemoteEvent(const QString &googleId)
{
    if (googleId.isEmpty()) return;
    std::string token = getValidAccessToken();
    if (token.empty()) return;

    std::ostringstream cmd;
    cmd << "curl.exe -s --max-time 8 -X DELETE \"https://www.googleapis.com/calendar/v3/calendars/primary/events/"
        << googleId.toStdString() << "\" -H \"Authorization: Bearer " << token << "\"";
    executeCurl(cmd.str());
}

// 15. Hurtowe usuwanie wydarzen
void GoogleCalendarSync::deleteRemoteEventsBatch(const std::vector<std::string> &googleIds)
{
    if (googleIds.empty()) return;
    std::string token = getValidAccessToken();
    if (token.empty()) return;

    for (const auto &id : googleIds) {
        if (id.empty()) continue;
        std::ostringstream cmd;
        cmd << "curl.exe -s --max-time 8 -X DELETE \"https://www.googleapis.com/calendar/v3/calendars/primary/events/"
            << id << "\" -H \"Authorization: Bearer " << token << "\"";
        executeCurl(cmd.str());
    }
}

// 16. Aktualizacja (PATCH) wydarzenia
bool GoogleCalendarSync::patchRemoteEvent(const QString &googleId, const QJsonObject &localObj)
{
    if (googleId.isEmpty()) return false;
    std::string token = getValidAccessToken();
    if (token.empty()) return false;

    QString tempFile = QDir::tempPath() + QString("/gpatch_%1_%2.json")
                              .arg(QDateTime::currentMSecsSinceEpoch())
                              .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QFile f(tempFile);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(QJsonDocument(localObj).toJson(QJsonDocument::Compact));
    f.close();

    QString natPath = QDir::toNativeSeparators(tempFile);
    std::ostringstream cmd;
    cmd << "curl.exe -s --max-time 12 -X PATCH \"https://www.googleapis.com/calendar/v3/calendars/primary/events/"
        << googleId.toStdString() << "\" "
        << "-H \"Authorization: Bearer " << token << "\" "
        << "-H \"Content-Type: application/json\" "
        << "-d \"@" << natPath.toStdString() << "\"";

    std::string resp = executeCurl(cmd.str());
    QFile::remove(tempFile);

    QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(resp));
    return (d.isObject() && d.object().contains("id"));
}

// 17. Wypchniecie pojedynczego wydarzenia z formatowaniem RFC3339
std::string GoogleCalendarSync::postRemoteEvent(const QJsonObject &localObj)
{
    std::string token = getValidAccessToken();
    if (token.empty()) return "";

    QString tempFile = QDir::tempPath() + QString("/gpost_%1_%2.json")
                              .arg(QDateTime::currentMSecsSinceEpoch())
                              .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    QFile f(tempFile);
    if (!f.open(QIODevice::WriteOnly)) return "";
    f.write(QJsonDocument(localObj).toJson(QJsonDocument::Compact));
    f.close();

    QString natPath = QDir::toNativeSeparators(tempFile);
    std::ostringstream cmd;
    cmd << "curl.exe -s --max-time 15 -X POST \"https://www.googleapis.com/calendar/v3/calendars/primary/events\" "
        << "-H \"Authorization: Bearer " << token << "\" "
        << "-H \"Content-Type: application/json\" "
        << "-d \"@" << natPath.toStdString() << "\"";

    std::string resp = executeCurl(cmd.str());
    QFile::remove(tempFile);

    QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(resp));
    if (d.isObject()) {
        if (d.object().contains("id")) {
            return d.object().value("id").toString().toStdString();
        }
        if (d.object().contains("error")) {
            QString errMsg = d.object().value("error").toObject().value("message").toString();
            QMetaObject::invokeMethod(this, [this, errMsg]() {
                emit syncLogMessage(QString("Błąd Google API przy dodawaniu: %1").arg(errMsg));
            }, Qt::QueuedConnection);
        }
    }
    return "";
}

// 18. Pobranie wszystkich zdalnych wydarzen
std::vector<QJsonObject> GoogleCalendarSync::fetchAllRemoteEvents(const std::string &token)
{
    std::vector<QJsonObject> allItems;
    std::string pageToken = "";

    while (true) {
        std::ostringstream cmd;
        cmd << "curl.exe -s --max-time 20 -X GET \"https://www.googleapis.com/calendar/v3/calendars/primary/events"
            << "?singleEvents=true&maxResults=250"
            << (pageToken.empty() ? "" : "&pageToken=" + pageToken)
            << "\" -H \"Authorization: Bearer " << token << "\"";

        std::string resp = executeCurl(cmd.str());
        QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(resp));
        if (!doc.isObject()) break;

        QJsonObject root = doc.object();
        QJsonArray items = root.value("items").toArray();
        for (const auto &v : items) {
            if (v.isObject()) allItems.push_back(v.toObject());
        }

        QString next = root.value("nextPageToken").toString();
        if (next.isEmpty()) break;
        pageToken = next.toStdString();
    }
    return allItems;
}

// 19. Asynchroniczny watek obslugujacy zbuforowana kolejke mutacji
void GoogleCalendarSync::runProcessDebouncedQueue(QMap<QString, CalendarEvent> mutations, QStringList deletions)
{
    std::string token = getValidAccessToken();
    if (token.empty()) {
        m_isSyncInProgress = false;
        return;
    }

    // 1. Wykonaj usuniecia
    for (const auto &gId : deletions) {
        if (!gId.isEmpty()) {
            deleteRemoteEvent(gId);
        }
    }

    // 2. Wykonaj mutacje
    QList<CalendarEvent> localPatches;
    for (auto ev : mutations) {
        QString chosenColorId = hexToGoogleColorId(ev.color, ev.category);

        QJsonObject payload;
        payload["summary"] = ev.title;
        payload["description"] = QString("Kategoria: %1").arg(ev.category);
        payload["colorId"] = chosenColorId;

        QJsonObject stObj, enObj;
        stObj["dateTime"] = formatRfc3339(ev.start);
        stObj["timeZone"] = "Europe/Warsaw";
        enObj["dateTime"] = formatRfc3339(ev.end.isValid() ? ev.end : ev.start.addSecs(3600));
        enObj["timeZone"] = "Europe/Warsaw";

        payload["start"] = stObj;
        payload["end"] = enObj;

        if (!ev.googleId.isEmpty()) {
            patchRemoteEvent(ev.googleId, payload);
        } else {
            std::string newId = postRemoteEvent(payload);
            if (!newId.empty()) {
                ev.googleId = QString::fromStdString(newId);
                ev.color = colorIdToHex(chosenColorId);
                localPatches.append(ev);
            }
        }
    }

    m_isSyncInProgress = false;

    if (!localPatches.isEmpty()) {
        QMetaObject::invokeMethod(this, [this, localPatches]() {
            if (m_calendarEngine) {
                m_calendarEngine->applyRemoteSync(localPatches, {});
            }
        }, Qt::QueuedConnection);
    }
}

// 20. Asynchroniczny watek czyszczenia kalendarza (API Clear)
void GoogleCalendarSync::runClearWorker()
{
    std::string token = getValidAccessToken();
    if (token.empty()) {
        m_isSyncInProgress = false;
        QMetaObject::invokeMethod(this, [this]() {
            emit syncLogMessage("Nie można wyczyścić kalendarza: brak ważnego tokenu.");
            emit syncFinished(false, "Brak autoryzacji Google.");
        }, Qt::QueuedConnection);
        return;
    }

    std::ostringstream cmd;
    cmd << "curl.exe -s --max-time 20 -X POST \"https://www.googleapis.com/calendar/v3/calendars/primary/clear\" "
        << "-H \"Authorization: Bearer " << token << "\" "
        << "-H \"Content-Length: 0\" "
        << "-d \"\"";

    executeCurl(cmd.str());
    m_isSyncInProgress = false;

    QMetaObject::invokeMethod(this, [this]() {
        emit syncLogMessage("Zdalny kalendarz Google został zresetowany.");
        emit syncFinished(true, "Kalendarz Google został pomyślnie wyczyszczony.");
    }, Qt::QueuedConnection);
}

// 21. Asynchroniczny watek pobierania wszystkich wydarzen (Download All)
void GoogleCalendarSync::runDownloadAllWorker()
{
    std::string token = getValidAccessToken();
    if (token.empty()) {
        m_isSyncInProgress = false;
        QMetaObject::invokeMethod(this, [this]() {
            emit syncFinished(false, "Brak autoryzacji Google OAuth.");
        }, Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        emit syncLogMessage("Pobieranie wszystkich wpisów ze zdalnego kalendarza Google...");
    }, Qt::QueuedConnection);

    auto remoteEvents = fetchAllRemoteEvents(token);
    QList<CalendarEvent> importedEvents;

    for (const auto &ro : remoteEvents) {
        QString gId = ro.value("id").toString();
        QString summary = ro.value("summary").toString();
        QString remoteColorId = ro.value("colorId").toString();
        QJsonObject startObj = ro.value("start").toObject();
        QJsonObject endObj = ro.value("end").toObject();

        QString startStr = startObj.contains("dateTime") ? startObj.value("dateTime").toString() : startObj.value("date").toString();
        QString endStr = endObj.contains("dateTime") ? endObj.value("dateTime").toString() : endObj.value("date").toString();

        QDateTime startDt = QDateTime::fromString(startStr, Qt::ISODate);
        QDateTime endDt = QDateTime::fromString(endStr, Qt::ISODate);
        if (!startDt.isValid()) continue;
        if (!endDt.isValid()) endDt = startDt.addSecs(3600);

        CalendarEvent ev;
        ev.id = "ev_gcal_" + gId;
        ev.googleId = gId;
        ev.title = summary.isEmpty() ? "Wydarzenie Google" : summary;
        ev.category = summary.startsWith("[") ? "Automatyczne" : "Event";
        ev.color = colorIdToHex(remoteColorId);
        ev.start = startDt;
        ev.end = endDt;
        ev.updatedAt = QDateTime::fromString(ro.value("updated").toString(), Qt::ISODate).toMSecsSinceEpoch();

        importedEvents.append(ev);
    }

    int count = importedEvents.size();
    m_isSyncInProgress = false;

    QMetaObject::invokeMethod(this, [this, importedEvents, count]() {
        if (m_calendarEngine) {
            m_calendarEngine->applyRemoteSync(importedEvents, {});
        }
        emit syncLogMessage(QString("Pobrano i zaimportowano %1 wydarzeń z Google.").arg(count));
        emit syncFinished(true, QString("Pomyślnie pobrano %1 wydarzeń z Google.").arg(count));
    }, Qt::QueuedConnection);
}

// 22. Asynchroniczny watek eksportu wszystkich lokalnych wydarzen (Upload All)
void GoogleCalendarSync::runUploadAllWorker(QList<CalendarEvent> localSnapshot)
{
    std::string token = getValidAccessToken();
    if (token.empty()) {
        m_isSyncInProgress = false;
        QMetaObject::invokeMethod(this, [this]() {
            emit syncFinished(false, "Brak autoryzacji Google OAuth.");
        }, Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(this, [this]() {
        emit syncLogMessage("Eksport lokalnych wydarzeń do Google Calendar...");
    }, Qt::QueuedConnection);

    auto remoteEvents = fetchAllRemoteEvents(token);
    std::set<QString> remoteSigs;

    for (const auto &ro : remoteEvents) {
        QJsonObject st = ro.value("start").toObject();
        QString dt = st.contains("dateTime") ? st.value("dateTime").toString() : st.value("date").toString();
        QString dateStr = dt.left(10);
        QString timeStr = dt.length() >= 16 ? dt.mid(11, 5) : "00:00";
        remoteSigs.insert(createSignature(dateStr, timeStr, ro.value("summary").toString()));
    }

    int uploaded = 0;
    QList<CalendarEvent> patchedLocalEvents;

    for (auto ev : localSnapshot) {
        QString dateStr = ev.start.toString("yyyy-MM-dd");
        QString timeStr = ev.start.toString("HH:mm");
        QString sig = createSignature(dateStr, timeStr, ev.title);

        if (remoteSigs.count(sig) > 0) continue;

        QString chosenColorId = hexToGoogleColorId(ev.color, ev.category);

        QJsonObject payload;
        payload["summary"] = ev.title;
        payload["description"] = QString("Kategoria: %1").arg(ev.category);
        payload["colorId"] = chosenColorId;

        QJsonObject stObj, enObj;
        stObj["dateTime"] = formatRfc3339(ev.start);
        stObj["timeZone"] = "Europe/Warsaw";
        enObj["dateTime"] = formatRfc3339(ev.end.isValid() ? ev.end : ev.start.addSecs(3600));
        enObj["timeZone"] = "Europe/Warsaw";

        payload["start"] = stObj;
        payload["end"] = enObj;

        std::string newId = postRemoteEvent(payload);
        if (!newId.empty()) {
            remoteSigs.insert(sig);
            ev.googleId = QString::fromStdString(newId);
            ev.color = colorIdToHex(chosenColorId);
            patchedLocalEvents.append(ev);
            uploaded++;
        }
    }

    m_isSyncInProgress = false;
    QMetaObject::invokeMethod(this, [this, uploaded, patchedLocalEvents]() {
        if (m_calendarEngine && !patchedLocalEvents.isEmpty()) {
            m_calendarEngine->applyRemoteSync(patchedLocalEvents, {});
        }
        emit syncLogMessage(QString("Wysłano %1 nowych wydarzeń do kalendarza Google.").arg(uploaded));
        emit syncFinished(true, QString("Pomyślnie wysłano %1 wydarzeń do Google.").arg(uploaded));
    }, Qt::QueuedConnection);
}

// 23. Glowny watek pasywnego Two-Way Sync z zaawansowanym rozwiazywaniem konfliktow
void GoogleCalendarSync::runTwoWaySyncWorker(QList<CalendarEvent> localSnapshot)
{
    std::string token = getValidAccessToken();
    if (token.empty()) {
        m_isSyncInProgress = false;
        return;
    }

    auto remoteEvents = fetchAllRemoteEvents(token);
    std::map<QString, std::vector<std::string>> remoteSigMap;
    std::map<QString, QJsonObject> remoteGoogleIdMap;
    std::vector<std::string> duplicatesToDelete;

    for (const auto &ro : remoteEvents) {
        std::string gId = ro.value("id").toString().toStdString();
        QString qgId = QString::fromStdString(gId);
        remoteGoogleIdMap[qgId] = ro;

        QJsonObject st = ro.value("start").toObject();
        QString dt = st.contains("dateTime") ? st.value("dateTime").toString() : st.value("date").toString();
        QString dateStr = dt.left(10);
        QString timeStr = dt.length() >= 16 ? dt.mid(11, 5) : "00:00";
        QString sig = createSignature(dateStr, timeStr, ro.value("summary").toString());

        if (remoteSigMap.count(sig) > 0) {
            duplicatesToDelete.push_back(gId);
        } else {
            remoteSigMap[sig].push_back(gId);
        }
    }

    // Usuwanie duplikatow w chmurze
    if (!duplicatesToDelete.empty()) {
        deleteRemoteEventsBatch(duplicatesToDelete);
    }

    // 1. Sprawdzanie usuniec z Google Calendar
    QStringList deletedFromGoogleIds;
    for (const auto &lev : localSnapshot) {
        if (!lev.googleId.isEmpty() && remoteGoogleIdMap.count(lev.googleId) == 0) {
            deletedFromGoogleIds.append(lev.googleId);
        }
    }

    // 2. Uzgadnianie lokalnych wydarzen
    std::set<QString> localSignatures;
    QList<CalendarEvent> updatedLocalEvents;
    int pushed = 0;

    for (auto ev : localSnapshot) {
        if (!ev.googleId.isEmpty() && deletedFromGoogleIds.contains(ev.googleId)) {
            continue;
        }

        QString dateStr = ev.start.toString("yyyy-MM-dd");
        QString timeStr = ev.start.toString("HH:mm");
        QString sig = createSignature(dateStr, timeStr, ev.title);
        localSignatures.insert(sig);

        QString chosenColorId = hexToGoogleColorId(ev.color, ev.category);

        if (ev.googleId.isEmpty() && remoteSigMap.count(sig) == 0) {
            QJsonObject payload;
            payload["summary"] = ev.title;
            payload["description"] = QString("Kategoria: %1").arg(ev.category);
            payload["colorId"] = chosenColorId;

            QJsonObject stObj, enObj;
            stObj["dateTime"] = formatRfc3339(ev.start);
            stObj["timeZone"] = "Europe/Warsaw";
            enObj["dateTime"] = formatRfc3339(ev.end.isValid() ? ev.end : ev.start.addSecs(3600));
            enObj["timeZone"] = "Europe/Warsaw";

            payload["start"] = stObj;
            payload["end"] = enObj;

            std::string newId = postRemoteEvent(payload);
            if (!newId.empty()) {
                ev.googleId = QString::fromStdString(newId);
                ev.color = colorIdToHex(chosenColorId);
                updatedLocalEvents.append(ev);
                pushed++;
            }
        } else if (!ev.googleId.isEmpty() && remoteGoogleIdMap.count(ev.googleId) > 0) {
            const QJsonObject &remObj = remoteGoogleIdMap[ev.googleId];
            QString rTitle = remObj.value("summary").toString();
            QString rColorId = remObj.value("colorId").toString();
            QJsonObject rStart = remObj.value("start").toObject();
            QJsonObject rEnd = remObj.value("end").toObject();
            QString rStStr = rStart.contains("dateTime") ? rStart.value("dateTime").toString() : rStart.value("date").toString();
            QString rEnStr = rEnd.contains("dateTime") ? rEnd.value("dateTime").toString() : rEnd.value("date").toString();

            QDateTime rStartDt = QDateTime::fromString(rStStr, Qt::ISODate);
            QDateTime rEndDt = QDateTime::fromString(rEnStr, Qt::ISODate);
            QString rColor = colorIdToHex(rColorId);

            bool titleDiffers = (ev.title.trimmed() != rTitle.trimmed());
            bool timeDiffers = (ev.start != rStartDt || ev.end != rEndDt);
            bool colorDiffers = (ev.color != rColor);

            if (titleDiffers || timeDiffers || colorDiffers) {
                qint64 remUpdated = QDateTime::fromString(remObj.value("updated").toString(), Qt::ISODate).toMSecsSinceEpoch();

                if (remUpdated > ev.updatedAt) {
                    ev.title = rTitle;
                    ev.start = rStartDt;
                    ev.end = rEndDt;
                    ev.color = rColor;
                    ev.updatedAt = remUpdated;
                    updatedLocalEvents.append(ev);
                } else {
                    QJsonObject patchPayload;
                    patchPayload["summary"] = ev.title;
                    patchPayload["colorId"] = hexToGoogleColorId(ev.color, ev.category);
                    QJsonObject stObj, enObj;
                    stObj["dateTime"] = formatRfc3339(ev.start);
                    stObj["timeZone"] = "Europe/Warsaw";
                    enObj["dateTime"] = formatRfc3339(ev.end);
                    enObj["timeZone"] = "Europe/Warsaw";
                    patchPayload["start"] = stObj;
                    patchPayload["end"] = enObj;

                    patchRemoteEvent(ev.googleId, patchPayload);
                }
            }
        }
    }

    // 3. Pobieranie nowych wydarzen bezposrednio z Google
    for (const auto &ro : remoteEvents) {
        QString gId = ro.value("id").toString();
        QJsonObject st = ro.value("start").toObject();
        QString dt = st.contains("dateTime") ? st.value("dateTime").toString() : st.value("date").toString();
        QString dateStr = dt.left(10);
        QString timeStr = dt.length() >= 16 ? dt.mid(11, 5) : "00:00";
        QString sig = createSignature(dateStr, timeStr, ro.value("summary").toString());

        bool alreadyLocal = false;
        for (const auto &lev : localSnapshot) {
            if (lev.googleId == gId) {
                alreadyLocal = true;
                break;
            }
        }

        if (!alreadyLocal && localSignatures.count(sig) == 0) {
            QString summary = ro.value("summary").toString();
            QString rColorId = ro.value("colorId").toString();
            QJsonObject en = ro.value("end").toObject();
            QString endStr = en.contains("dateTime") ? en.value("dateTime").toString() : en.value("date").toString();

            CalendarEvent ev;
            ev.id = "ev_gcal_" + gId;
            ev.googleId = gId;
            ev.title = summary.isEmpty() ? "Google Event" : summary;
            ev.category = summary.startsWith("[") ? "Automatyczne" : "Event";
            ev.color = colorIdToHex(rColorId);
            ev.start = QDateTime::fromString(dt, Qt::ISODate);
            ev.end = QDateTime::fromString(endStr, Qt::ISODate);
            if (!ev.end.isValid()) ev.end = ev.start.addSecs(3600);
            ev.updatedAt = QDateTime::fromString(ro.value("updated").toString(), Qt::ISODate).toMSecsSinceEpoch();

            updatedLocalEvents.append(ev);
            localSignatures.insert(sig);
        }
    }

    m_isSyncInProgress = false;

    QMetaObject::invokeMethod(this, [this, updatedLocalEvents, deletedFromGoogleIds, pushed]() {
        if (m_calendarEngine) {
            m_calendarEngine->applyRemoteSync(updatedLocalEvents, deletedFromGoogleIds);
        }
        if (pushed > 0 || !updatedLocalEvents.isEmpty() || !deletedFromGoogleIds.isEmpty()) {
            emit syncLogMessage(QString("Pasywny sync: ↑%1 wysłano, ↓%2 zaktualizowano, ✕%3 usunięto.")
                                    .arg(pushed).arg(updatedLocalEvents.size()).arg(deletedFromGoogleIds.size()));
        }
    }, Qt::QueuedConnection);
}

// 24. Wystrzelenie asynchronicznej kolejki po uplywie debouncera (600 ms)
void GoogleCalendarSync::onDebounceTimerTimeout()
{
    if (m_isSyncPaused || !isAuthenticated()) return;

    QMap<QString, CalendarEvent> mutations;
    QStringList deletions;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        mutations = m_pendingMutations;
        deletions = m_pendingDeletions;
        m_pendingMutations.clear();
        m_pendingDeletions.clear();
    }

    if (mutations.isEmpty() && deletions.isEmpty()) return;

    if (m_isSyncInProgress.exchange(true)) {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        for (auto it = mutations.cbegin(); it != mutations.cend(); ++it) {
            m_pendingMutations.insert(it.key(), it.value());
        }
        m_pendingDeletions.append(deletions);
        m_debounceTimer->start(600);
        return;
    }

    std::thread([this, mutations, deletions]() {
        runProcessDebouncedQueue(mutations, deletions);
    }).detach();
}

// 25. Reakcja na zakonczenie wsadowej modyfikacji terminow
void GoogleCalendarSync::onBatchCompletedLocally()
{
    if (m_isSyncPaused || !isAuthenticated()) return;
    m_debounceTimer->start(800);
}

// 26. Kolejkowanie usuniecia wydarzenia
void GoogleCalendarSync::onEventDeletedLocally(const QString &id, const QString &googleId)
{
    Q_UNUSED(id);
    if (googleId.isEmpty() || m_isSyncPaused || !isAuthenticated()) return;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingDeletions.append(googleId);
    }
    m_debounceTimer->start(600);
}

// 27. Kolejkowanie mutacji (dodanie / zmiana nazwy / zmiana godziny)
void GoogleCalendarSync::onEventMutatedLocally(const CalendarEvent &event)
{
    if (m_isSyncPaused || !isAuthenticated()) return;

    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_pendingMutations[event.id] = event;
    }
    m_debounceTimer->start(600);
}

// 28. Obsluga serwera autoryzacji OAuth
void GoogleCalendarSync::onAuthServerNewConnection()
{
    QTcpSocket *socket = m_authServer->nextPendingConnection();
    if (!socket) return;

    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        QByteArray req = socket->readAll();
        QString reqStr = QString::fromUtf8(req);

        QRegularExpression rx("code=([^& \\r\\n]+)");
        auto match = rx.match(reqStr);

        if (match.hasMatch() && !m_isAuthExchanging.exchange(true)) {
            QString authCode = QUrl::fromPercentEncoding(match.captured(1).toUtf8());

            QString html = "<html><body style='background:#121212;color:#fff;font-family:sans-serif;text-align:center;padding-top:80px;'>"
                           "<h1 style='color:#4caf50;'>Amogus Blocker // Autoryzacja udana!</h1>"
                           "<p>Token został pomyślnie przechwycony. Możesz zamknąć tę kartę przeglądarki i wrócić do aplikacji.</p>"
                           "</body></html>";

            QByteArray res = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
                             QByteArray::number(html.toUtf8().size()) + "\r\nConnection: close\r\n\r\n" + html.toUtf8();
            socket->write(res);
            socket->flush();
            socket->disconnectFromHost();

            quint16 port = m_authServer->serverPort();
            m_authServer->close();

            std::thread([this, authCode, port]() {
                exchangeAuthCodeForTokens(authCode, port);
                m_isAuthExchanging = false;
            }).detach();
        } else {
            socket->write("HTTP/1.1 200 OK\r\nConnection: close\r\n\r\nOK");
            socket->disconnectFromHost();
        }
    });
}

// 29. Uruchomienie logowania OAuth
bool GoogleCalendarSync::startOAuthLogin()
{
    if (m_clientId.empty()) {
        if (!loadCredentials(m_clientId, m_clientSecret, m_tokenUri)) {
            emit syncLogMessage("Błąd: Nie można odczytać pliku credentials.json!");
            return false;
        }
    }

    if (m_authServer->isListening()) {
        m_authServer->close();
    }

    quint16 port = 8088;
    if (!m_authServer->listen(QHostAddress::LocalHost, port)) {
        if (!m_authServer->listen(QHostAddress::LocalHost, 0)) {
            emit syncLogMessage("Nie można uruchomić lokalnego serwera nasłuchującego.");
            return false;
        }
        port = m_authServer->serverPort();
    }

    QString redirectUri = QString("http://127.0.0.1:%1").arg(port);
    QUrl authUrl("https://accounts.google.com/o/oauth2/v2/auth");
    QUrlQuery q;
    q.addQueryItem("client_id", QString::fromStdString(m_clientId));
    q.addQueryItem("redirect_uri", redirectUri);
    q.addQueryItem("response_type", "code");
    q.addQueryItem("scope", "https://www.googleapis.com/auth/calendar");
    q.addQueryItem("access_type", "offline");
    q.addQueryItem("prompt", "consent");
    authUrl.setQuery(q);

    emit syncLogMessage(QString("Otwieranie przeglądarki do autoryzacji Google (port %1)...").arg(port));
    QDesktopServices::openUrl(authUrl);
    return true;
}

// 30. Reczne wklejenie tokenu
void GoogleCalendarSync::setManualRefreshToken(const QString &refreshToken)
{
    if (refreshToken.trimmed().isEmpty()) return;
    m_refreshToken = refreshToken.trimmed().toStdString();
    saveToken();
    emit syncLogMessage("Ręcznie ustawiono refresh_token. Weryfikacja połączenia...");

    std::thread([this]() {
        if (refreshAccessToken()) {
            QMetaObject::invokeMethod(this, [this]() {
                emit syncFinished(true, "Token zaakceptowany! Połączono z Google API.");
            }, Qt::QueuedConnection);
            triggerTwoWaySync();
        }
    }).detach();
}

// 31. Taktowanie pasywnego daemona w tle (co 15 sekund)
void GoogleCalendarSync::onBackgroundTimerTick()
{
    if (!m_isSyncPaused && isAuthenticated() && !m_isSyncInProgress && !m_debounceTimer->isActive()) {
        triggerTwoWaySync();
    }
}

// 32. Asynchroniczny Wipe kalendarza Google
void GoogleCalendarSync::clearPrimaryCalendar()
{
    if (m_isSyncInProgress.exchange(true)) {
        emit syncLogMessage("Inna operacja synchronizacji jest już w toku.");
        return;
    }

    emit syncLogMessage("Rozpoczynanie operacji czyszczenia zdalnego kalendarza...");
    std::thread([this]() {
        runClearWorker();
    }).detach();
}

// 33. Asynchroniczne pobranie wszystkich wydarzen
void GoogleCalendarSync::downloadAllFromGoogle()
{
    if (m_isSyncInProgress.exchange(true)) {
        emit syncLogMessage("Inna operacja synchronizacji jest już w toku.");
        return;
    }

    std::thread([this]() {
        runDownloadAllWorker();
    }).detach();
}

// 34. Asynchroniczne wyslanie wszystkich wydarzen
void GoogleCalendarSync::uploadAllToGoogle()
{
    if (!m_calendarEngine) return;
    if (m_isSyncInProgress.exchange(true)) {
        emit syncLogMessage("Inna operacja synchronizacji jest już w toku.");
        return;
    }

    QList<CalendarEvent> snapshot = m_calendarEngine->getEvents();
    std::thread([this, snapshot]() {
        runUploadAllWorker(snapshot);
    }).detach();
}

// 35. Asynchroniczne wywolanie Two-Way Sync
void GoogleCalendarSync::triggerTwoWaySync()
{
    if (!m_calendarEngine) return;
    if (m_isSyncInProgress.exchange(true)) {
        return;
    }

    QList<CalendarEvent> snapshot = m_calendarEngine->getEvents();
    std::thread([this, snapshot]() {
        runTwoWaySyncWorker(snapshot);
    }).detach();
}

// 36. Zmiana stanu wstrzymania
void GoogleCalendarSync::setSyncPaused(bool paused)
{
    m_isSyncPaused = paused;
    emit syncStateChanged(!m_isSyncPaused);
    emit syncLogMessage(paused ? "Pasywna synchronizacja ZATRZYMANA." : "Pasywna synchronizacja WZNOWIONA.");
}

// 37. Gettery statusu
bool GoogleCalendarSync::isSyncInProgress() const { return m_isSyncInProgress.load(); }
bool GoogleCalendarSync::isAuthenticated() const { return !m_refreshToken.empty(); }
bool GoogleCalendarSync::isSyncPaused() const { return m_isSyncPaused; }

// 38. Podpiecie CalendarEngine z obsluga wsadowa
void GoogleCalendarSync::setCalendarEngine(CalendarEngine *engine)
{
    m_calendarEngine = engine;
    if (m_calendarEngine) {
        connect(m_calendarEngine, &CalendarEngine::eventMutatedLocally, this, &GoogleCalendarSync::onEventMutatedLocally);
        connect(m_calendarEngine, &CalendarEngine::eventDeletedLocally, this, &GoogleCalendarSync::onEventDeletedLocally);
        connect(m_calendarEngine, &CalendarEngine::batchCompletedLocally, this, &GoogleCalendarSync::onBatchCompletedLocally);
    }
}

// 39. Inicjalizacja polaczenia
bool GoogleCalendarSync::initialize()
{
    if (!loadCredentials(m_clientId, m_clientSecret, m_tokenUri)) {
        emit syncLogMessage("Brak lub niepoprawny plik Library/credentials.json!");
        return false;
    }

    if (!loadToken()) {
        emit syncLogMessage("Wymagane pierwsze logowanie: Użyj przycisku 'Zaloguj do Google (OAuth)'.");
        return false;
    }

    std::thread([this]() {
        bool tokenOk = refreshAccessToken();
        if (tokenOk) {
            QMetaObject::invokeMethod(this, [this]() {
                emit syncLogMessage("Pasywny daemon Google Calendar połączony i aktywny.");
            }, Qt::QueuedConnection);
            triggerTwoWaySync();
        }
    }).detach();

    return true;
}

// 40. Destruktor
GoogleCalendarSync::~GoogleCalendarSync()
{
    if (m_daemonTimer) m_daemonTimer->stop();
    if (m_debounceTimer) m_debounceTimer->stop();
    if (m_authServer && m_authServer->isListening()) m_authServer->close();
}

// 41. Konstruktor
GoogleCalendarSync::GoogleCalendarSync(CalendarEngine *engine, QObject *parent)
    : QObject(parent)
    , m_calendarEngine(nullptr)
    , m_daemonTimer(new QTimer(this))
    , m_debounceTimer(new QTimer(this))
    , m_authServer(new QTcpServer(this))
    , m_isSyncPaused(false)
    , m_isSyncInProgress(false)
    , m_isAuthExchanging(false)
{
    m_debounceTimer->setSingleShot(true);
    connect(m_debounceTimer, &QTimer::timeout, this, &GoogleCalendarSync::onDebounceTimerTimeout);

    connect(m_authServer, &QTcpServer::newConnection, this, &GoogleCalendarSync::onAuthServerNewConnection);
    connect(m_daemonTimer, &QTimer::timeout, this, &GoogleCalendarSync::onBackgroundTimerTick);
    m_daemonTimer->start(15000);

    setCalendarEngine(engine);
}
