// Plik naglowkowy modulu Watchdog.
// Odpowiada za permanentna ochrone procesu przed wymuszonym zamknieciem w Menedzerze Zadan (DACL WinAPI),
// nadzor procesu AmogusGuard.exe oraz przywracanie binarek w czasie rzeczywistym.
// Laczy sie z main.cpp.

#ifndef WATCHDOG_H
#define WATCHDOG_H

#include <QObject>
#include <QString>
#include <QTimer>

class Watchdog : public QObject
{
    Q_OBJECT

public:
    explicit Watchdog(QObject *parent = nullptr);
    ~Watchdog() override;

    static bool runIfWatchdogMode(int argc, char *argv[]);
    static void setProcessDaclProtection(bool enable);

public slots:
    void startSupervisor();
    void stopSupervisor();

private slots:
    void checkAndEnsureGuardAlive();

private:
    void *m_hCleanExitEvent;
    QTimer *m_mutualCheckTimer;
    bool m_isRunning;
};

#endif // WATCHDOG_H
