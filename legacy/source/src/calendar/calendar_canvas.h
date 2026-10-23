// Plik naglowkowy 24-godzinnego kalendarza graficznego (CalendarCanvas).
// Zapewnia interaktywne rysowanie slotow, plynne przesuwanie myszka z przyciaganiem do 15 minut
// oraz pelna blokade przesuwania, rozszerzania i usuwania zablokowanych kafelkow.
// Laczy sie z CalendarEngine, ProfileManager i VisualOverduePlannerDialog.

#ifndef CALENDAR_CANVAS_H
#define CALENDAR_CANVAS_H

#include <QWidget>
#include <QPainter>
#include <QDate>
#include <QTime>
#include <QTimer>
#include <QMouseEvent>
#include <QKeyEvent>
#include <vector>
#include <set>
#include "calendar_engine.h"

class ProfileManager;

struct RenderSlot {
    CalendarEvent event;
    int startMin = 0;
    int endMin = 0;
    int colIndex = 0;
    int totalCols = 1;
    QRectF renderedRect;
    QRectF statusBadgeRect;
    QRectF resizeBottomHandle;
};

class CalendarCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit CalendarCanvas(CalendarEngine *engine, ProfileManager *profileManager = nullptr, QWidget *parent = nullptr);
    ~CalendarCanvas() override = default;

    void setProfileManager(ProfileManager *profileManager);
    void setSelectedDate(const QDate &d);
    void goToToday();
    void nextWeek();
    void prevWeek();
    QString getWeekPeriodLabel() const;
    QDate getCurrentMonday() const;

public slots:
    void refreshFromEngine();

signals:
    void eventMovedByUser(const CalendarEvent &event);
    void newBlockDrawnByUser(const QDateTime &start, const QDateTime &end);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    bool isEventLocked(const CalendarEvent &ev) const;
    void rebuildLayoutSlots();
    int yToMinutes(float y) const;
    float minutesToY(int min) const;
    static int snap15(int rawMin);

    CalendarEngine *m_engine;
    ProfileManager *m_profileManager;
    QDate m_currentMonday;
    std::vector<RenderSlot> m_slots;
    std::set<QString> m_selectedIds;

    bool m_isDraggingCreation;
    int m_dragDayIdx;
    int m_dragStartMin;
    int m_dragEndMin;

    int m_activeDragOp;
    CalendarEvent m_dragTargetEvent;
    int m_origDayIdx;
    int m_origStartMin;
    int m_origEndMin;
    int m_targetDayIdx;
    int m_targetStartMin;
    int m_targetEndMin;
    QPointF m_mousePressPos;
    bool m_isPotentialClick;

    QTimer *m_clockTimer;

    const int START_HOUR = 0;
    const int END_HOUR = 24;
    const float HOUR_HEIGHT = 42.0f;
    const int HEADER_HEIGHT = 32;
    const int LEFT_GUTTER = 48;
};

#endif // CALENDAR_CANVAS_H
