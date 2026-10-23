// Plik naglowkowy daemona GoogleCalendarSync.
// Zapewnia debouncing mutacji (600ms), kolejkowanie zadan sieciowych i eliminacje zawieszania GUI.
// Laczy sie z CalendarEngine, Library/credentials.json oraz Library/google_token.json.

#ifndef GOOGLE_CALENDAR_SYNC_H
#define GOOGLE_CALENDAR_SYNC_H

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QTimer>
#include <QJsonObject>
#include <QTcpServer>
#include <vector>
#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include "calendar_engine.h"

class GoogleCalendarSync : public QObject
{
    Q_OBJECT

public:
    explicit GoogleCalendarSync(CalendarEngine *engine = nullptr, QObject *parent = nullptr);
    ~GoogleCalendarSync() override;

    void setCalendarEngine(CalendarEngine *engine);
    bool isSyncPaused() const;
    bool isAuthenticated() const;
    bool isSyncInProgress() const;
    QString getCredentialsFilePath() const;
    QString getTokenFilePath() const;

    static QString colorIdToHex(const QString &colorId);
    static QString hexToGoogleColorId(const QString &hexColor, const QString &category = QString());

public slots:
    bool initialize();
    void setSyncPaused(bool paused);
    void triggerTwoWaySync();
    void uploadAllToGoogle();
    void downloadAllFromGoogle();
    void clearPrimaryCalendar();
    bool startOAuthLogin();
    void setManualRefreshToken(const QString &refreshToken);

    void onEventMutatedLocally(const CalendarEvent &event);
    void onEventDeletedLocally(const QString &id, const QString &googleId);
    void onBatchCompletedLocally();

signals:
    void syncLogMessage(const QString &message);
    void syncStateChanged(bool active);
    void syncFinished(bool success, const QString &summary);

private slots:
    void onBackgroundTimerTick();
    void onDebounceTimerTimeout();
    void onAuthServerNewConnection();

private:
    static std::string executeCurl(const std::string &command);
    static QString formatRfc3339(const QDateTime &dt);
    static QString createSignature(const QString &dateStr, const QString &timeStr, const QString &title);

    bool loadCredentials(std::string &clientId, std::string &clientSecret, std::string &tokenUri);
    bool loadToken();
    void saveToken();
    bool exchangeAuthCodeForTokens(const QString &authCode, int redirectPort);
    bool refreshAccessToken();
    std::string getValidAccessToken();

    std::vector<QJsonObject> fetchAllRemoteEvents(const std::string &token);
    std::string postRemoteEvent(const QJsonObject &localObj);
    bool patchRemoteEvent(const QString &googleId, const QJsonObject &localObj);
    void deleteRemoteEvent(const QString &googleId);
    void deleteRemoteEventsBatch(const std::vector<std::string> &googleIds);

    void runTwoWaySyncWorker(QList<CalendarEvent> localSnapshot);
    void runUploadAllWorker(QList<CalendarEvent> localSnapshot);
    void runDownloadAllWorker();
    void runClearWorker();
    void runProcessDebouncedQueue(QMap<QString, CalendarEvent> mutations, QStringList deletions);

    CalendarEngine *m_calendarEngine;
    QTimer *m_daemonTimer;
    QTimer *m_debounceTimer;
    QTcpServer *m_authServer;

    std::mutex m_queueMutex;
    QMap<QString, CalendarEvent> m_pendingMutations;
    QStringList m_pendingDeletions;

    std::string m_clientId;
    std::string m_clientSecret;
    std::string m_tokenUri;
    std::string m_accessToken;
    std::string m_refreshToken;
    std::chrono::steady_clock::time_point m_tokenExpiryTime;

    bool m_isSyncPaused;
    std::atomic<bool> m_isSyncInProgress;
    std::atomic<bool> m_isAuthExchanging;
};

#endif // GOOGLE_CALENDAR_SYNC_H
