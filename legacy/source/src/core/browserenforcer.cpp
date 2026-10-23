// Plik implementacji modulu BrowserEnforcer.
// Zapewnia ciagle odliczanie 60 sekund dla kazdej przegladarki bez wtyczki
// oraz twarde zamykanie procesow przez WinAPI TerminateProcess.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "browserenforcer.h"
#include <windows.h>
#include <tlhelp32.h>
#include <QDateTime>

// 1. Sprawdzanie czy proces o danej nazwie dziala w systemie Windows (wsparcie dla opera.exe oraz opera_gx.exe)
static bool isProcessRunningWinApi(const QString &processName)
{
    bool running = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            QString currentExe = QString::fromWCharArray(entry.szExeFile);
            if (currentExe.compare(processName, Qt::CaseInsensitive) == 0) {
                running = true;
                break;
            }
            if (processName.compare("opera.exe", Qt::CaseInsensitive) == 0 &&
                currentExe.compare("opera_gx.exe", Qt::CaseInsensitive) == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return running;
}

// 2. Wymuszone zamkniecie procesu przez TerminateProcess dla wszystkich instancji
static void terminateProcessInstancesWinApi(const QString &processName)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W entry;
    entry.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &entry)) {
        do {
            QString currentExe = QString::fromWCharArray(entry.szExeFile);
            bool match = (currentExe.compare(processName, Qt::CaseInsensitive) == 0);
            if (!match && processName.compare("opera.exe", Qt::CaseInsensitive) == 0 &&
                currentExe.compare("opera_gx.exe", Qt::CaseInsensitive) == 0) {
                match = true;
            }

            if (match) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, entry.th32ProcessID);
                if (hProcess) {
                    TerminateProcess(hProcess, 1);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

// 3. Glowna petla monitoringu procesow i weryfikacji heartbeat (licznik 60s i twardy kill)
void BrowserEnforcer::checkAndEnforceBrowsers()
{
    qint64 now = QDateTime::currentSecsSinceEpoch();

    for (auto it = m_browsers.begin(); it != m_browsers.end(); ++it) {
        BrowserTrackState &b = it.value();
        bool currentlyRunning = isProcessRunningWinApi(b.processName);
        b.isRunning = currentlyRunning;

        if (!currentlyRunning) {
            b.secondsRemaining = 60;
            b.lastHeartbeatTimestamp = 0;
            continue;
        }

        bool hasRecentHeartbeat = (b.lastHeartbeatTimestamp > 0 && (now - b.lastHeartbeatTimestamp) <= 15);

        if (hasRecentHeartbeat) {
            b.secondsRemaining = 60;
        } else {
            if (m_isEnabled) {
                if (b.secondsRemaining > 0) {
                    b.secondsRemaining--;
                }

                if (b.secondsRemaining <= 0) {
                    terminateProcessInstancesWinApi(b.processName);
                    b.isRunning = false;
                    b.secondsRemaining = 60;
                    b.lastHeartbeatTimestamp = 0;
                    emit browserKilled(b.displayName);
                }
            }
        }
    }

    emit statusUpdated(m_browsers);
}

// 4. Wymuszenie natychmiastowego skanu
void BrowserEnforcer::forceScan()
{
    checkAndEnforceBrowsers();
}

// 5. Taktowanie cykliczne co sekunde
void BrowserEnforcer::onTimerTick()
{
    checkAndEnforceBrowsers();
}

// 6. Rejestracja sygnalu obecnosci z wtyczki
void BrowserEnforcer::onHeartbeatReceived(const QString &browserName)
{
    qint64 now = QDateTime::currentSecsSinceEpoch();
    QString key = browserName.toLower().trimmed();

    for (auto it = m_browsers.begin(); it != m_browsers.end(); ++it) {
        if (key.contains(it.key()) || it.key().contains(key) ||
            (key.contains("edg") && it.key() == "edge") ||
            (key.contains("opr") && it.key() == "opera")) {
            it.value().lastHeartbeatTimestamp = now;
            it.value().secondsRemaining = 60;
            break;
        }
    }
}

// 7. Wlaczanie / wylaczanie egzekwowania
void BrowserEnforcer::setEnabled(bool enabled)
{
    m_isEnabled = enabled;
}

// 8. Pobranie biezacego stanu przegladarek
const QMap<QString, BrowserTrackState> &BrowserEnforcer::getBrowserStates() const
{
    return m_browsers;
}

// 9. Konstruktor modulu BrowserEnforcer (domyslnie wlaczony z permanentnym egzekwowaniem)
BrowserEnforcer::BrowserEnforcer(QObject *parent)
    : QObject(parent)
    , m_tickTimer(new QTimer(this))
    , m_isEnabled(true)
{
    m_browsers["edge"] = { "Microsoft Edge", "msedge.exe", false, 0, 60 };
    m_browsers["chrome"] = { "Google Chrome", "chrome.exe", false, 0, 60 };
    m_browsers["opera"] = { "Opera / Opera GX", "opera.exe", false, 0, 60 };
    m_browsers["brave"] = { "Brave Browser", "brave.exe", false, 0, 60 };

    connect(m_tickTimer, &QTimer::timeout, this, &BrowserEnforcer::onTimerTick);
    m_tickTimer->start(1000);

    forceScan();
}
