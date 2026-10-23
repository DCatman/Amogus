// Plik naglowkowy panelu Dashboard.
// Prezentuje aktywne bloki na gorze oraz pelny kalendarz pod spodem.
// Obsluguje tworzenie nowych wydarzen bezposrednio poprzez przeciaganie mysza po siatce kalendarza.
// Laczy sie z ProfileManager, CalendarEngine, CalendarCanvas oraz EventEditorDialog.

#ifndef DASHBOARD_VIEW_H
#define DASHBOARD_VIEW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QDateTime>
#include "profile_manager.h"
#include "calendar_engine.h"
#include "calendar_canvas.h"

class DashboardView : public QWidget
{
    Q_OBJECT

public:
    explicit DashboardView(ProfileManager *profileManager, CalendarEngine *calendarEngine, QWidget *parent = nullptr);
    ~DashboardView() override = default;

public slots:
    void refreshDashboard();

private slots:
    void onCanvasDrawnBlock(const QDateTime &start, const QDateTime &end);

private:
    void setupUi();

    ProfileManager *m_profileManager;
    CalendarEngine *m_calendarEngine;

    QVBoxLayout *m_activeBlocksLayout;
    CalendarCanvas *m_calendarCanvas;
    QLabel *m_periodLabel;
};

#endif // DASHBOARD_VIEW_H
