// Plik naglowkowy kafelka wiersza profilu blokady BlockRowWidget.
// Tytul otwiera edycje, a podtytul otwiera kalendarz korzystajacy z instancji CalendarEngine.
// Laczy sie z ProfileManager, CalendarEngine i BlocksView.

#ifndef BLOCK_ROW_WIDGET_H
#define BLOCK_ROW_WIDGET_H

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include "profile_manager.h"
#include "calendar_engine.h"

class BlockRowWidget : public QWidget
{
    Q_OBJECT

public:
    explicit BlockRowWidget(const BlockProfile &profile, CalendarEngine *calendarEngine,
                            ProfileManager *profileManager, QWidget *parent = nullptr);
    ~BlockRowWidget() override = default;

    QString getProfileName() const;
    void updateProfileData(const BlockProfile &profile);

signals:
    void toggleRequested(const QString &profileName);
    void editRequested(const QString &profileName);
    void lockConfigRequested(const QString &profileName);
    void clickBreakRequested(const QString &profileName);
    void charBreakRequested(const QString &profileName);
    void criticalBreakRequested(const QString &profileName);

private:
    void setupUi();
    void refreshUi();

    BlockProfile m_profile;
    CalendarEngine *m_calendarEngine;
    ProfileManager *m_profileManager;

    QWidget *m_indicatorBar;
    QPushButton *m_titleButton;
    QPushButton *m_subtitleButton;

    QPushButton *m_btnClickBreak;
    QPushButton *m_btnCharBreak;
    QPushButton *m_btnCritBreak;

    QPushButton *m_toggleButton;
    QPushButton *m_lockBadgeButton;
};

#endif // BLOCK_ROW_WIDGET_H
