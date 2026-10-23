// Plik implementacji modulu Watchdog.
// Wykorzystuje niezalezny proces AmogusGuard.exe, hardening uprawnien jądra Windows (DACL)
// odrzucajacy próby zakonczenia przez Task Manager / taskkill oraz petle autostartu.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "watchdog.h"
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#include <tlhelp32.h>
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QSettings>
#include <QStringList>

static const wchar_t *CLEAN_EXIT_EVENT_NAME = L"Local\\AmogusCleanExitEvent";
static const wchar_t *WATCHDOG_MUTEX_NAME = L"Local\\AmogusWatchdogMutex";

// 1. Zabezpieczenie procesu w jądrze Windows przed TerminateProcess i Suspend
void Watchdog::setProcessDaclProtection(bool enable)
{
    PSECURITY_DESCRIPTOR pSD = NULL;
    // Blokujemy PROCESS_TERMINATE (0x0001) oraz PROCESS_SUSPEND_RESUME (0x0800) dla grupy Everyone (WD)
    const wchar_t *sddl = enable ? L"D:(D;;0x0801;;;WD)(A;;0x101400;;;WD)" : L"D:(A;;GA;;;WD)";
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &pSD, NULL)) {
        PACL pDacl = NULL;
        BOOL daclPresent = FALSE;
        BOOL daclDefaulted = FALSE;
        if (GetSecurityDescriptorDacl(pSD, &daclPresent, &pDacl, &daclDefaulted)) {
            SetSecurityInfo(GetCurrentProcess(), SE_KERNEL_OBJECT, DACL_SECURITY_INFORMATION, NULL, NULL, pDacl, NULL);
        }
        LocalFree(pSD);
    }
}

// 2. Pobieranie prawidlowej sciezki do glownego pliku wykonywalnego
static QString getMainExecutablePath(const QString &hintPath = QString())
{
    if (!hintPath.isEmpty() && QFile::exists(hintPath)) {
        return QDir::toNativeSeparators(hintPath);
    }

    wchar_t buffer[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, buffer, MAX_PATH);
    QString currentModule = (len > 0) ? QString::fromWCharArray(buffer, len) : QCoreApplication::applicationFilePath();
    QFileInfo fi(currentModule);

    if (fi.fileName().compare("AmogusGuard.exe", Qt::CaseInsensitive) == 0) {
        if (QFile::exists(fi.dir().filePath("Amogus.exe"))) {
            return fi.dir().filePath("Amogus.exe");
        }
        if (QFile::exists(fi.dir().filePath("AmogusBlocker.exe"))) {
            return fi.dir().filePath("AmogusBlocker.exe");
        }
        return fi.dir().filePath("Amogus.exe");
    }

    return QDir::toNativeSeparators(currentModule);
}

// 3. Automatyczne przygotowanie niezaleznego pliku AmogusGuard.exe
static QString ensureGuardExecutableExists(const QString &mainPath)
{
    QFileInfo fi(mainPath);
    QString guardPath = fi.dir().filePath("AmogusGuard.exe");

    if (!QFile::exists(guardPath) || QFileInfo(guardPath).size() != fi.size()) {
        QFile::remove(guardPath);
        QFile::copy(mainPath, guardPath);
    }

    return QDir::toNativeSeparators(guardPath);
}

// 4. Sprawdzanie czy proces o podanej nazwie dziala w systemie
static bool isProcessNameRunning(const QString &exeName)
{
    bool found = false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(PROCESSENTRY32W);

    if (Process32FirstW(snapshot, &pe)) {
        do {
            if (QString::fromWCharArray(pe.szExeFile).compare(exeName, Qt::CaseInsensitive) == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return found;
}

// 5. Niskopoziomowe uruchamianie procesu w tle z pelnym odpieciem od drzewa zadan
static bool launchProcessDetachedWinApi(const QString &appPath, const QStringList &args, PROCESS_INFORMATION *outPi = nullptr)
{
    QString cmd = "\"" + appPath + "\"";
    for (const QString &arg : args) {
        cmd += " \"" + arg + "\"";
    }

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    std::wstring cmdW = cmd.toStdWString();
    std::wstring pathW = appPath.toStdWString();

    DWORD flags = CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;

    BOOL ok = CreateProcessW(
        pathW.c_str(),
        &cmdW[0],
        NULL, NULL, FALSE,
        flags,
        NULL, NULL,
        &si, &pi
    );

    if (ok) {
        if (outPi) {
            *outPi = pi;
        } else {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
        return true;
    }
    return false;
}

// 6. Sprawdzenie czy wyjscie nastapilo legalnie przez zaliczone wyzwanie odblokowania
static bool isCleanExitAuthorized()
{
    HANDLE hEvent = OpenEventW(SYNCHRONIZE, FALSE, CLEAN_EXIT_EVENT_NAME);
    if (!hEvent) return false;

    DWORD res = WaitForSingleObject(hEvent, 0);
    CloseHandle(hEvent);
    return (res == WAIT_OBJECT_0);
}

// 7. Zarzadzanie wpisem autostartu w rejestrze Windows
static void setWindowsAutoRun(bool enable)
{
    QSettings runSettings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    QString mainExe = QDir::toNativeSeparators(getMainExecutablePath());

    if (enable) {
        runSettings.setValue("AmogusBlocker", "\"" + mainExe + "\"");
    } else {
        runSettings.remove("AmogusBlocker");
    }
}

// 8. Petla nadzorcza dzialajaca w osobnym procesie AmogusGuard.exe
static void superviseMainAppLoop(DWORD initialTargetPid, const QString &mainPathHint)
{
    HANDLE hMutex = CreateMutexW(NULL, FALSE, WATCHDOG_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hMutex) CloseHandle(hMutex);
        return;
    }

    Watchdog::setProcessDaclProtection(true);

    DWORD currentTargetPid = initialTargetPid;
    QString mainExe = getMainExecutablePath(mainPathHint);

    while (true) {
        HANDLE hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, currentTargetPid);
        if (!hProcess) {
            if (isCleanExitAuthorized()) break;

            PROCESS_INFORMATION newPi;
            QStringList args;
            if (launchProcessDetachedWinApi(mainExe, args, &newPi)) {
                currentTargetPid = newPi.dwProcessId;
                CloseHandle(newPi.hProcess);
                CloseHandle(newPi.hThread);
                Sleep(50);
                continue;
            } else {
                break;
            }
        }

        HANDLE hCleanExitEvent = OpenEventW(SYNCHRONIZE, FALSE, CLEAN_EXIT_EVENT_NAME);

        if (hCleanExitEvent) {
            HANDLE handles[2] = { hProcess, hCleanExitEvent };
            DWORD res = WaitForMultipleObjects(2, handles, FALSE, INFINITE);

            CloseHandle(hProcess);
            CloseHandle(hCleanExitEvent);

            if (res == WAIT_OBJECT_0 + 1 || isCleanExitAuthorized()) {
                break;
            }
        } else {
            WaitForSingleObject(hProcess, INFINITE);
            CloseHandle(hProcess);

            if (isCleanExitAuthorized()) break;
        }

        PROCESS_INFORMATION newPi;
        QStringList args;
        if (launchProcessDetachedWinApi(mainExe, args, &newPi)) {
            currentTargetPid = newPi.dwProcessId;
            CloseHandle(newPi.hProcess);
            CloseHandle(newPi.hThread);
            Sleep(50);
        } else {
            break;
        }
    }

    Watchdog::setProcessDaclProtection(false);
    if (hMutex) CloseHandle(hMutex);
}

// 9. Weryfikacja czy proces uruchomiono w trybie nadzorcy
bool Watchdog::runIfWatchdogMode(int argc, char *argv[])
{
    if (argc >= 3 && QString::fromLatin1(argv[1]) == "--watchdog") {
        DWORD targetPid = QString::fromLatin1(argv[2]).toULong();
        QString mainPath = (argc >= 4) ? QString::fromLocal8Bit(argv[3]) : QString();
        if (targetPid > 0) {
            superviseMainAppLoop(targetPid, mainPath);
            return true;
        }
    }
    return false;
}

// 10. Pilnowanie przez glowny proces obecnosci strażnika
void Watchdog::checkAndEnsureGuardAlive()
{
    if (!m_isRunning || isCleanExitAuthorized()) return;

    if (!isProcessNameRunning("AmogusGuard.exe")) {
        QString mainExe = getMainExecutablePath();
        QString guardPath = ensureGuardExecutableExists(mainExe);
        QStringList args;
        args << "--watchdog" << QString::number(QCoreApplication::applicationPid()) << mainExe;
        launchProcessDetachedWinApi(guardPath, args);
    }
}

// 11. Calkowite wylaczenie po zaliczeniu wyzwania odblokowania
void Watchdog::stopSupervisor()
{
    m_isRunning = false;
    m_mutualCheckTimer->stop();

    setWindowsAutoRun(false);

    if (m_hCleanExitEvent) {
        SetEvent(static_cast<HANDLE>(m_hCleanExitEvent));
    }

    setProcessDaclProtection(false);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot != INVALID_HANDLE_VALUE) {
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(PROCESSENTRY32W);
        if (Process32FirstW(snapshot, &pe)) {
            do {
                if (QString::fromWCharArray(pe.szExeFile).compare("AmogusGuard.exe", Qt::CaseInsensitive) == 0) {
                    HANDLE hGuard = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hGuard) {
                        TerminateProcess(hGuard, 0);
                        CloseHandle(hGuard);
                    }
                }
            } while (Process32NextW(snapshot, &pe));
        }
        CloseHandle(snapshot);
    }
}

// 12. Uruchomienie nadzoru i utwardzenie procesu glownego
void Watchdog::startSupervisor()
{
    if (m_isRunning) return;

    if (m_hCleanExitEvent) {
        ResetEvent(static_cast<HANDLE>(m_hCleanExitEvent));
    }

    setProcessDaclProtection(true);

    m_isRunning = true;
    setWindowsAutoRun(true);

    QString mainExe = getMainExecutablePath();
    QString guardPath = ensureGuardExecutableExists(mainExe);
    qint64 currentPid = QCoreApplication::applicationPid();

    QStringList args;
    args << "--watchdog" << QString::number(currentPid) << mainExe;

    launchProcessDetachedWinApi(guardPath, args);
    m_mutualCheckTimer->start(200);
}

// 13. Destruktor
Watchdog::~Watchdog()
{
    setProcessDaclProtection(false);
    if (m_hCleanExitEvent) {
        CloseHandle(static_cast<HANDLE>(m_hCleanExitEvent));
        m_hCleanExitEvent = nullptr;
    }
}

// 14. Konstruktor
Watchdog::Watchdog(QObject *parent)
    : QObject(parent)
    , m_hCleanExitEvent(nullptr)
    , m_mutualCheckTimer(new QTimer(this))
    , m_isRunning(false)
{
    m_hCleanExitEvent = CreateEventW(NULL, TRUE, FALSE, CLEAN_EXIT_EVENT_NAME);
    if (m_hCleanExitEvent) {
        ResetEvent(static_cast<HANDLE>(m_hCleanExitEvent));
    }

    connect(m_mutualCheckTimer, &QTimer::timeout, this, &Watchdog::checkAndEnsureGuardAlive);
}
