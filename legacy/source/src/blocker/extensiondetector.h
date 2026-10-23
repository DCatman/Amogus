// Plik nagłówkowy modułu ExtensionDetector.
// Odpowiada za automatyczne wykrywanie obecności i stanu wtyczki "Amogus Blocker Companion"
// w profilach przeglądarek Chrome, Edge oraz Opera bez podawania ID.
// Łączy się z resztą aplikacji w pliku main.cpp.

#ifndef EXTENSIONDETECTOR_H
#define EXTENSIONDETECTOR_H

#include <QObject>
#include <QString>
#include <QList>

enum class ExtensionState {
    BrowserNotFound,
    NotInstalled,
    Disabled,
    Enabled
};

struct BrowserReport {
    QString browserName;
    ExtensionState state;
    QString details;
};

class ExtensionDetector : public QObject
{
    Q_OBJECT

public:
    explicit ExtensionDetector(QObject *parent = nullptr);
    ~ExtensionDetector() override = default;

public slots:
    void checkCompanionExtension();

signals:
    void scanFinished(const QList<BrowserReport> &reports);

private:
    bool findCompanionInSettings(const QString &filePath, int &outState) const;
    ExtensionState inspectProfile(const QString &profilePath, QString &outDetails) const;
    BrowserReport checkChromiumBrowser(const QString &browserName, const QString &userDataPath) const;
    BrowserReport checkOperaBrowser(const QString &browserName, const QString &operaPath) const;
};

#endif // EXTENSIONDETECTOR_H