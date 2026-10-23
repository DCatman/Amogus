// Plik implementacji modulu AppBlocker.
// Skanuje okna i procesy Windows: obsluguje prefiksy title:, file:, maski plikow .exe
// oraz tytulow okien, a takze blokuje dostep do katalogu ProgramData\Amogus.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "appblocker.h"
#include <windows.h>
#include <QRegularExpression>
#include <QFileInfo>
#include <QDir>

// 1. Dopasowanie wzorca wieloznacznego (wildcard)
static bool matchWildcard(const QString &pattern, const QString &text)
{
    QString cleanPattern = pattern.trimmed();
    if (cleanPattern.isEmpty() || text.isEmpty()) {
        return false;
    }
    QRegularExpression regex(QRegularExpression::wildcardToRegularExpression(cleanPattern),
                             QRegularExpression::CaseInsensitiveOption);
    return regex.match(text).hasMatch();
}

// 2. Normalizacja sciezki wpisanej przez uzytkownika (file:/...)
static QString normalizeRulePath(const QString &rule)
{
    QString path = rule.trimmed();
    if (path.startsWith("file:///", Qt::CaseInsensitive)) {
        path = path.mid(8);
    } else if (path.startsWith("file:/", Qt::CaseInsensitive)) {
        path = path.mid(6);
    } else if (path.startsWith("file:", Qt::CaseInsensitive)) {
        path = path.mid(5);
    }
    return QDir::fromNativeSeparators(path);
}

// 3. Pobranie pelnej sciezki procesu za pomoca WinAPI
static QString getProcessExecutablePath(HANDLE hProcess)
{
    wchar_t buffer[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProcess, 0, buffer, &size)) {
        return QDir::fromNativeSeparators(QString::fromWCharArray(buffer));
    }
    return QString();
}

// 4. Zamkniecie procesu (WM_CLOSE i TerminateProcess)
static void terminateProcessById(DWORD pid, HWND hwnd)
{
    if (hwnd) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess) {
        TerminateProcess(hProcess, 1);
        CloseHandle(hProcess);
    }
}

// 5. Weryfikacja okna pod katem prob eksploracji katalogu ProgramData\Amogus
static bool isProtectedDirectoryExploration(const QString &windowTitle)
{
    QString lower = windowTitle.toLower();
    return lower.contains("programdata\\amogus") || lower.contains("programdata/amogus");
}

// 6. Weryfikacja pojedynczego okna i procesu wzgledem regul (wsparcie dla title:, file: i .exe)
static void checkWindowAgainstRules(HWND hwnd, const QStringList &rules)
{
    if (!IsWindowVisible(hwnd)) return;

    wchar_t titleBuffer[512];
    GetWindowTextW(hwnd, titleBuffer, 512);
    QString windowTitle = QString::fromWCharArray(titleBuffer).trimmed();

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0 || pid == GetCurrentProcessId()) return;

    // A. Blokada dostepu do folderu ProgramData\Amogus
    if (isProtectedDirectoryExploration(windowTitle)) {
        terminateProcessById(pid, hwnd);
        return;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
    QString processPath;
    QString processName;
    if (hProcess) {
        processPath = getProcessExecutablePath(hProcess);
        processName = QFileInfo(processPath).fileName();
        CloseHandle(hProcess);
    }

    for (const QString &rawRule : rules) {
        QString rule = rawRule.trimmed();
        if (rule.isEmpty()) continue;

        bool isExplicitTitleRule = false;
        if (rule.startsWith("title:", Qt::CaseInsensitive)) {
            rule = rule.mid(6).trimmed();
            isExplicitTitleRule = true;
        }

        // B. Dopasowanie po sciezce (file:... lub sciezka dyskowa)
        if (!isExplicitTitleRule && (rule.startsWith("file:", Qt::CaseInsensitive) || rule.contains("/") || rule.contains("\\"))) {
            QString normalizedRule = normalizeRulePath(rule);
            if (!processPath.isEmpty() && matchWildcard(normalizedRule, processPath)) {
                terminateProcessById(pid, hwnd);
                return;
            }
        }

        // C. Dopasowanie po nazwie pliku .exe
        if (!isExplicitTitleRule && !processName.isEmpty() && matchWildcard(rule, processName)) {
            terminateProcessById(pid, hwnd);
            return;
        }

        // D. Dopasowanie po masce tytulu okna (*note* lub title:*...*)
        if (!windowTitle.isEmpty() && matchWildcard(rule, windowTitle)) {
            terminateProcessById(pid, hwnd);
            return;
        }
    }
}

// 7. Callback EnumWindows
static BOOL CALLBACK enumWindowsCallback(HWND hwnd, LPARAM lParam)
{
    const QStringList *rules = reinterpret_cast<const QStringList *>(lParam);
    if (rules) {
        checkWindowAgainstRules(hwnd, *rules);
    }
    return TRUE;
}

// 8. Przebieg petli skanujacej okna
void AppBlocker::scanAndEnforceRules()
{
    if (!m_isRunning) return;
    EnumWindows(enumWindowsCallback, reinterpret_cast<LPARAM>(&m_rules));
}

// 9. Slot timera wywolujacy skanowanie
void AppBlocker::onTimerTick()
{
    scanAndEnforceRules();
}

// 10. Aktualizacja regul
void AppBlocker::setRules(const QStringList &rules)
{
    m_rules = rules;
}

// 11. Zatrzymanie blokowania
void AppBlocker::stop()
{
    m_isRunning = false;
    m_timer->stop();
}

// 12. Uruchomienie blokowania
void AppBlocker::start(int intervalMs)
{
    m_isRunning = true;
    m_timer->start(intervalMs);
}

// 13. Konstruktor
AppBlocker::AppBlocker(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_isRunning(false)
{
    connect(m_timer, &QTimer::timeout, this, &AppBlocker::onTimerTick);
}
