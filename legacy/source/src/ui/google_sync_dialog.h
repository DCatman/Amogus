// Plik naglowkowy panelu kontrolnego GoogleSyncDialog.
// Zapewnia przyciski logowania OAuth w przegladarce, recznego wprowadzania tokenu,
// wstrzymywania synchronizacji, wymuszonego uploadu, downloadu, deduplikacji i czyszczenia.
// Laczy sie z GoogleCalendarSync i CalendarEngine.

#ifndef GOOGLE_SYNC_DIALOG_H
#define GOOGLE_SYNC_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QPlainTextEdit>

class GoogleCalendarSync;
class CalendarEngine;

class GoogleSyncDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GoogleSyncDialog(GoogleCalendarSync *sync, CalendarEngine *engine, QWidget *parent = nullptr);
    ~GoogleSyncDialog() override = default;

public slots:
    void appendLog(const QString &msg);
    void updateStatusDisplay();

private slots:
    void onLoginOAuthClicked();
    void onManualTokenClicked();
    void onToggleSyncClicked();
    void onTwoWaySyncClicked();
    void onUploadAllClicked();
    void onDownloadAllClicked();
    void onDeduplicateLocalClicked();
    void onWipeRemoteCalendarClicked();

private:
    void setupUi();

    GoogleCalendarSync *m_sync;
    CalendarEngine *m_engine;

    QLabel *m_statusLabel;
    QLabel *m_authLabel;
    QLabel *m_credentialsPathLabel;

    QPushButton *m_btnLoginOAuth;
    QPushButton *m_btnManualToken;
    QPushButton *m_btnToggleSync;
    QPushButton *m_btnTwoWaySync;
    QPushButton *m_btnUploadAll;
    QPushButton *m_btnDownloadAll;
    QPushButton *m_btnDeduplicateLocal;
    QPushButton *m_btnWipeRemote;

    QPlainTextEdit *m_logConsole;
};

#endif // GOOGLE_SYNC_DIALOG_H
