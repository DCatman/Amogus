// Plik implementacji modułu ExtensionDetector.
// Przeszukuje pliki konfiguracyjne przeglądarek pod kątem wtyczki "Amogus Blocker Companion".
// Funkcje ułożone od najmniej abstrakcyjnych u góry do najbardziej abstrakcyjnych na dole.

#include "extensiondetector.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <utility>

static const char *COMPANION_NAME = "Amogus Blocker Companion";

// 1. Niskopoziomowe przeszukiwanie pliku JSON pod kątem nazwy wtyczki lub jej ścieżki
bool ExtensionDetector::findCompanionInSettings(const QString &filePath, int &outState) const
{
    QFile file(filePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        return false;
    }

    QJsonObject root = doc.object();
    QJsonObject extensions = root.value("extensions").toObject();
    QJsonObject settings = extensions.value("settings").toObject();

    for (auto it = settings.begin(); it != settings.end(); ++it) {
        QJsonObject extObj = it.value().toObject();
        QString name = extObj.value("manifest").toObject().value("name").toString();
        QString path = extObj.value("path").toString();

        if (name == COMPANION_NAME || path.contains("amogus", Qt::CaseInsensitive)) {
            outState = extObj.value("state").toInt(1);
            return true;
        }
    }

    return false;
}

// 2. Weryfikacja obu plików konfiguracyjnych w danym profilu
ExtensionState ExtensionDetector::inspectProfile(const QString &profilePath, QString &outDetails) const
{
    int state = -1;
    bool found = findCompanionInSettings(profilePath + "/Secure Preferences", state);
    if (!found) {
        found = findCompanionInSettings(profilePath + "/Preferences", state);
    }

    if (!found) {
        outDetails = "Brak wtyczki w profilu";
        return ExtensionState::NotInstalled;
    }

    if (state == 1) {
        outDetails = "Wlaczona";
        return ExtensionState::Enabled;
    }

    outDetails = "Wylaczona w przegladarce";
    return ExtensionState::Disabled;
}

// 3. Sprawdzanie profili przeglądarek opartych na wielu profilach (Chrome, Edge)
BrowserReport ExtensionDetector::checkChromiumBrowser(const QString &browserName, const QString &userDataPath) const
{
    BrowserReport report;
    report.browserName = browserName;

    QDir baseDir(userDataPath);
    if (!baseDir.exists()) {
        report.state = ExtensionState::BrowserNotFound;
        report.details = "Przegladarka niezainstalowana";
        return report;
    }

    QStringList profiles;
    if (QDir(userDataPath + "/Default").exists()) {
        profiles << userDataPath + "/Default";
    }

    const QStringList subDirs = baseDir.entryList(QStringList() << "Profile *", QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &sub : std::as_const(subDirs)) {
        profiles << userDataPath + "/" + sub;
    }

    if (profiles.isEmpty()) {
        report.state = ExtensionState::NotInstalled;
        report.details = "Brak profili";
        return report;
    }

    bool foundAny = false;
    for (const QString &profPath : std::as_const(profiles)) {
        QString details;
        ExtensionState state = inspectProfile(profPath, details);

        if (state == ExtensionState::Disabled) {
            report.state = ExtensionState::Disabled;
            report.details = QString("Wylaczona w: %1").arg(QDir(profPath).dirName());
            return report;
        }

        if (state == ExtensionState::Enabled) {
            foundAny = true;
        }
    }

    if (foundAny) {
        report.state = ExtensionState::Enabled;
        report.details = "Aktywna";
    } else {
        report.state = ExtensionState::NotInstalled;
        report.details = "Wtyczka nie zostala zaladowana";
    }

    return report;
}

// 4. Sprawdzanie instalacji w Operze
BrowserReport ExtensionDetector::checkOperaBrowser(const QString &browserName, const QString &operaPath) const
{
    BrowserReport report;
    report.browserName = browserName;

    QDir operaDir(operaPath);
    if (!operaDir.exists()) {
        report.state = ExtensionState::BrowserNotFound;
        report.details = "Przegladarka niezainstalowana";
        return report;
    }

    QString details;
    report.state = inspectProfile(operaPath, details);
    report.details = details;
    return report;
}

// 5. Automatyczne skanowanie wszystkich przeglądarek pod kątem stworzonej wtyczki
void ExtensionDetector::checkCompanionExtension()
{
    QList<BrowserReport> reports;
    QString localAppData = QProcessEnvironment::systemEnvironment().value("LOCALAPPDATA");
    QString appData = QProcessEnvironment::systemEnvironment().value("APPDATA");

    reports.append(checkChromiumBrowser("Google Chrome", localAppData + "/Google/Chrome/User Data"));
    reports.append(checkChromiumBrowser("Microsoft Edge", localAppData + "/Microsoft/Edge/User Data"));
    reports.append(checkOperaBrowser("Opera", appData + "/Opera Software/Opera Stable"));
    reports.append(checkOperaBrowser("Opera GX", appData + "/Opera Software/Opera GX Stable"));

    emit scanFinished(reports);
}

// 6. Konstruktor modułu
ExtensionDetector::ExtensionDetector(QObject *parent)
    : QObject(parent)
{
}