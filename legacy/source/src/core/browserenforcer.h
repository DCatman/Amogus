// Plik naglowkowy modulu BrowserEnforcer.
// Odpowiada za permanentny monitoring procesow przegladarek w systemie Windows
// oraz twarde zamykanie przegladarek bez aktywnej wtyczki po 60 sekundach braku ochrony.
// Laczy sie z RuleServer, MainWindow i FixExtensionsDialog.

#ifndef BROWSERENFORCER_H
#define BROWSERENFORCER_H

#include <QObject>
#include <QTimer>
#include <QMap>
#include <QString>

struct BrowserTrackState {
    QString displayName;
    QString processName;
    bool isRunning = false;
    qint64 lastHeartbeatTimestamp = 0;
    int secondsRemaining = 60;
};

class BrowserEnforcer : public QObject
{
    Q_OBJECT

public:
    explicit BrowserEnforcer(QObject *parent = nullptr);
    ~BrowserEnforcer() override = default;

    const QMap<QString, BrowserTrackState> &getBrowserStates() const;

public slots:
    void setEnabled(bool enabled = true);
    void onHeartbeatReceived(const QString &browserName);
    void forceScan();

signals:
    void statusUpdated(const QMap<QString, BrowserTrackState> &states);
    void browserKilled(const QString &displayName);

private slots:
    void onTimerTick();

private:
    void checkAndEnforceBrowsers();

    QTimer *m_tickTimer;
    bool m_isEnabled;
    QMap<QString, BrowserTrackState> m_browsers;
};

#endif // BROWSERENFORCER_H
