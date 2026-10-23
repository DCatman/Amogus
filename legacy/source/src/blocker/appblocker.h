// Plik naglowkowy modulu AppBlocker.
// Skanuje okna i procesy Windows: blokuje wg masek, file:/, nazw exe oraz folder ProgramData\Amogus.
// Laczy sie z main.cpp oraz ProfileManager.

#ifndef APPBLOCKER_H
#define APPBLOCKER_H

#include <QObject>
#include <QStringList>
#include <QTimer>

class AppBlocker : public QObject
{
    Q_OBJECT

public:
    explicit AppBlocker(QObject *parent = nullptr);
    ~AppBlocker() override = default;

public slots:
    void setRules(const QStringList &rules);
    void start(int intervalMs = 500);
    void stop();

private slots:
    void onTimerTick();

private:
    void scanAndEnforceRules();

    QTimer *m_timer;
    QStringList m_rules;
    bool m_isRunning;
};

#endif // APPBLOCKER_H