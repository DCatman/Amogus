// Plik naglowkowy OverdueTrackerWidget oraz okna VisualOverduePlannerDialog.
// Zapewnia matryce 2D z pelnymi etapami {0, 1, 3, 6, 12} dla wykladow i cwiczen oraz {-6..-1} dla labow.
// Obsluguje opcjonalne blokowanie komputera (monit TAK/NIE) oraz skrocone nazwy zadan.
// Laczy sie z OverdueTracker, SubjectManager, ProfileManager, CalendarEngine i CalendarCanvas.

#ifndef OVERDUE_TRACKER_WIDGET_H
#define OVERDUE_TRACKER_WIDGET_H

#include <QWidget>
#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimeEdit>
#include <QSpinBox>
#include "overdue_tracker.h"
#include "calendar_engine.h"
#include "calendar_canvas.h"
#include "subject_manager.h"
#include "profile_manager.h"

struct ArmedStageItem {
    QString displayName;
    QString subjectName;
    QString trackType;
    int weekNumber = 1;
    int stage = 0;
    bool isExam = false;

    bool operator==(const ArmedStageItem &other) const {
        return displayName == other.displayName &&
               weekNumber == other.weekNumber &&
               stage == other.stage &&
               isExam == other.isExam;
    }
};

class VisualOverduePlannerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit VisualOverduePlannerDialog(OverdueTracker *tracker, CalendarEngine *calendarEngine,
                                        ProfileManager *profileManager, QWidget *parent = nullptr);
    ~VisualOverduePlannerDialog() override = default;

private slots:
    void onLockPlanClicked();
    void onClearSelectionClicked();
    void onPrevWeekClicked();
    void onNextWeekClicked();
    void onTodayClicked();
    void onCanvasDrawnBlock(const QDateTime &start, const QDateTime &end);
    void onStageButtonClicked(const ArmedStageItem &item, bool checked);

private:
    void setupUi();
    void refreshPlannerMatrix();
    QWidget *createStageCellWidget(const SubjectTrackData &track, int weekNum, const ClassProgressCell &cell);
    void updateSelectionSummary();

    OverdueTracker *m_tracker;
    CalendarEngine *m_calendarEngine;
    ProfileManager *m_profileManager;

    CalendarCanvas *m_canvas;
    QLabel *m_periodLabel;
    QLabel *m_pacingBanner;
    QLabel *m_selectionInfoLabel;
    QTableWidget *m_plannerGrid;

    QPushButton *m_btnLockPlan;
    QPushButton *m_btnClearSelection;

    QList<ArmedStageItem> m_selectedStages;
};

class OverdueTrackerWidget : public QWidget
{
    Q_OBJECT

public:
    explicit OverdueTrackerWidget(OverdueTracker *tracker, CalendarEngine *calendarEngine,
                                  ProfileManager *profileManager = nullptr, QWidget *parent = nullptr);
    ~OverdueTrackerWidget() override = default;

    void setSubjectManager(SubjectManager *subjectManager);
    void setProfileManager(ProfileManager *profileManager);

public slots:
    void refreshGrid();
    void onSessionTimeUpdated(int remainingMinutes, int currentAfkDebtMinutes);
    void onAfkStatusChanged(bool isAfk, int idleSeconds);
    void onAfkDebtTransferred(int totalDebtMinutes);

private slots:
    void onCellDoubleClicked(int row, int column);
    void onDeleteTrackClicked();
    void onResyncClicked();
    void onOpenVisualPlannerClicked();

private:
    void setupUi();

    OverdueTracker *m_tracker;
    CalendarEngine *m_calendarEngine;
    ProfileManager *m_profileManager;
    SubjectManager *m_subjectManager;

    QTableWidget *m_table;
    QPushButton *m_btnOpenPlanner;
    QPushButton *m_btnDeleteTrack;
    QPushButton *m_btnResync;

    QLabel *m_pacingBanner;
    QLabel *m_sessionStatusLabel;
    QLabel *m_afkWarningLabel;
    QLabel *m_afkDebtBanner;
};

#endif // OVERDUE_TRACKER_WIDGET_H
