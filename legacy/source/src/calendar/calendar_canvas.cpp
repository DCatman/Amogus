// Plik implementacji modulu CalendarCanvas.
// Realizuje interaktywne przesuwanie i rysowanie blokow, weryfikacje kłódki przy probie usuniecia,
// rozszerzenia lub przesuniecia oraz rysowanie siatki z wyroznieniem kafelkow ICS.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "calendar_canvas.h"
#include "event_editor_dialog.h"
#include "profile_manager.h"
#include <QFontMetrics>
#include <QMessageBox>
#include <algorithm>
#include <cmath>

// 1. Zaokraglanie czasu do pelnych 15 minut
int CalendarCanvas::snap15(int rawMin)
{
    int snapped = static_cast<int>(std::round(rawMin / 15.0f)) * 15;
    return std::clamp(snapped, 0, 1440);
}

// 2. Przeliczanie pozycji Y na minuty w dobie
int CalendarCanvas::yToMinutes(float y) const
{
    float frac = (y - HEADER_HEIGHT) / HOUR_HEIGHT;
    return snap15(static_cast<int>((frac + START_HOUR) * 60.0f));
}

// 3. Przeliczanie minut na wspolrzedna pikselowa Y
float CalendarCanvas::minutesToY(int min) const
{
    float frac = (min / 60.0f) - START_HOUR;
    return HEADER_HEIGHT + (frac * HOUR_HEIGHT);
}

// 4. Sprawdzenie czy wydarzenie nalezy do zablokowanego profilu
bool CalendarCanvas::isEventLocked(const CalendarEvent &ev) const
{
    if (ev.linkedProfile.isEmpty() || !m_profileManager) return false;
    return m_profileManager->isProfileLocked(ev.linkedProfile);
}

// 5. Przeliczanie kolizji i ukladanie slotow w kolumnach
void CalendarCanvas::rebuildLayoutSlots()
{
    m_slots.clear();
    if (!m_engine) return;

    const auto &events = m_engine->getEvents();
    float colW = (width() - LEFT_GUTTER) / 7.0f;

    for (int day = 0; day < 7; ++day) {
        QDate curDate = m_currentMonday.addDays(day);
        std::vector<RenderSlot> daySlots;

        for (const auto &ev : events) {
            if (ev.start.date() == curDate) {
                int sm = ev.start.time().hour() * 60 + ev.start.time().minute();
                int em = ev.end.time().hour() * 60 + ev.end.time().minute();
                if (em <= sm) em = 1440;

                RenderSlot s;
                s.event = ev;
                s.startMin = sm;
                s.endMin = em;
                daySlots.push_back(s);
            }
        }

        std::sort(daySlots.begin(), daySlots.end(), [](const RenderSlot &a, const RenderSlot &b) {
            if (a.startMin != b.startMin) return a.startMin < b.startMin;
            return a.endMin > b.endMin;
        });

        for (size_t i = 0; i < daySlots.size(); ++i) {
            std::vector<int> used;
            for (size_t j = 0; j < i; ++j) {
                if (daySlots[i].startMin < daySlots[j].endMin && daySlots[j].startMin < daySlots[i].endMin) {
                    used.push_back(daySlots[j].colIndex);
                }
            }
            int c = 0;
            while (std::find(used.begin(), used.end(), c) != used.end()) c++;
            daySlots[i].colIndex = c;
        }

        for (size_t i = 0; i < daySlots.size(); ++i) {
            int maxC = daySlots[i].colIndex;
            for (size_t j = 0; j < daySlots.size(); ++j) {
                if (daySlots[i].startMin < daySlots[j].endMin && daySlots[j].startMin < daySlots[i].endMin) {
                    maxC = std::max(maxC, daySlots[j].colIndex);
                }
            }
            daySlots[i].totalCols = maxC + 1;
        }

        float dayX = LEFT_GUTTER + day * colW;
        for (auto &s : daySlots) {
            float slotW = (colW - 4.0f) / s.totalCols;
            float slotX = dayX + s.colIndex * slotW + 2.0f;
            float y1 = minutesToY(s.startMin);
            float y2 = minutesToY(s.endMin);
            float blockH = std::max(26.0f, y2 - y1);

            s.renderedRect = QRectF(slotX, y1 + 1.0f, slotW - 2.0f, blockH - 2.0f);
            s.statusBadgeRect = QRectF(s.renderedRect.right() - 14.0f, s.renderedRect.top() + 4.0f, 12.0f, 12.0f);
            s.resizeBottomHandle = QRectF(s.renderedRect.left(), s.renderedRect.bottom() - 6.0f, s.renderedRect.width(), 6.0f);
            m_slots.push_back(s);
        }
    }
}

// 6. Obsluga klawiatury (Delete z weryfikacja kłódki)
void CalendarCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete && !m_selectedIds.empty()) {
        QString toDelete = *m_selectedIds.begin();
        if (m_engine) {
            for (const auto &ev : m_engine->getEvents()) {
                if (ev.id == toDelete) {
                    if (isEventLocked(ev)) {
                        QMessageBox::warning(this, "Plan zablokowany", "Ten blok jest powiązany ze zablokowanym profilem. Nie można go usunąć.");
                        return;
                    }
                    break;
                }
            }
            m_engine->removeEvent(toDelete);
        }
        m_selectedIds.clear();
        update();
        return;
    }
    QWidget::keyPressEvent(event);
}

// 7. Upuszczenie myszy: zakonczenie przesuwania lub tworzenia z zatwierdzeniem do bazy
void CalendarCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;

    if (m_isPotentialClick && !m_dragTargetEvent.id.isEmpty() && m_activeDragOp == 0) {
        EventEditorDialog dlg(m_dragTargetEvent, true, m_profileManager, this);
        if (dlg.exec() == QDialog::Accepted) {
            if (dlg.isDeleteRequested()) {
                if (m_engine) m_engine->removeEvent(m_dragTargetEvent.id);
            } else {
                if (m_engine) m_engine->updateEvent(dlg.getEventData());
            }
        }
        m_dragTargetEvent = CalendarEvent();
        m_isPotentialClick = false;
        rebuildLayoutSlots();
        update();
        return;
    }

    if (m_activeDragOp != 0) {
        if (m_isPotentialClick) {
            EventEditorDialog dlg(m_dragTargetEvent, true, m_profileManager, this);
            if (dlg.exec() == QDialog::Accepted) {
                if (dlg.isDeleteRequested()) {
                    if (m_engine) m_engine->removeEvent(m_dragTargetEvent.id);
                } else {
                    if (m_engine) m_engine->updateEvent(dlg.getEventData());
                }
            }
        } else {
            QDate newDate = m_currentMonday.addDays(m_targetDayIdx);
            m_dragTargetEvent.start = QDateTime(newDate, QTime(m_targetStartMin / 60, m_targetStartMin % 60));
            m_dragTargetEvent.end = QDateTime(newDate, QTime(m_targetEndMin / 60, m_targetEndMin % 60));
            if (m_engine) m_engine->updateEvent(m_dragTargetEvent);
            emit eventMovedByUser(m_dragTargetEvent);
        }
        m_activeDragOp = 0;
        rebuildLayoutSlots();
        update();
        return;
    }

    if (m_isDraggingCreation) {
        m_isDraggingCreation = false;
        if (m_dragEndMin > m_dragStartMin) {
            QDateTime st(m_currentMonday.addDays(m_dragDayIdx), QTime(m_dragStartMin / 60, m_dragStartMin % 60));
            QDateTime en(m_currentMonday.addDays(m_dragDayIdx), QTime(m_dragEndMin / 60, m_dragEndMin % 60));
            emit newBlockDrawnByUser(st, en);
        }
        update();
    }
}

// 8. Przesuwanie kursora myszy z obsluga Move i ResizeBottom
void CalendarCanvas::mouseMoveEvent(QMouseEvent *event)
{
    float colW = (width() - LEFT_GUTTER) / 7.0f;

    if (m_activeDragOp != 0) {
        if ((event->position() - m_mousePressPos).manhattanLength() > 5) {
            m_isPotentialClick = false;
        }

        int curMin = yToMinutes(event->position().y());
        int curDay = std::clamp(static_cast<int>((event->position().x() - LEFT_GUTTER) / colW), 0, 6);

        if (m_activeDragOp == 1) {
            int duration = m_origEndMin - m_origStartMin;
            m_targetDayIdx = curDay;
            m_targetStartMin = std::clamp(curMin, 0, 1440 - duration);
            m_targetEndMin = m_targetStartMin + duration;
        } else if (m_activeDragOp == 2) {
            m_targetEndMin = std::clamp(curMin, m_targetStartMin + 15, 1440);
        }
        update();
        return;
    }

    if (m_isDraggingCreation) {
        m_dragEndMin = std::clamp(yToMinutes(event->position().y()), m_dragStartMin + 15, 1440);
        update();
        return;
    }

    QPointF pos = event->position();
    bool onHandle = false;
    for (const auto &s : m_slots) {
        if (s.resizeBottomHandle.contains(pos)) {
            if (!isEventLocked(s.event)) {
                setCursor(Qt::SizeVerCursor);
                onHandle = true;
            }
            break;
        }
    }
    if (!onHandle) {
        setCursor(Qt::ArrowCursor);
    }
}

// 9. Klikniecie myszy: uniemozliwia przesuwanie i rozciaganie zablokowanych blokow
void CalendarCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) return;

    QPointF pos = event->position();
    float colW = (width() - LEFT_GUTTER) / 7.0f;
    m_mousePressPos = pos;
    m_isPotentialClick = true;

    for (const auto &slot : m_slots) {
        bool locked = isEventLocked(slot.event);

        if (slot.resizeBottomHandle.contains(pos)) {
            if (locked) {
                return;
            }
            m_activeDragOp = 2;
            m_dragTargetEvent = slot.event;
            m_targetDayIdx = m_origDayIdx = slot.event.start.date().dayOfWeek() - 1;
            m_targetStartMin = m_origStartMin = slot.startMin;
            m_targetEndMin = m_origEndMin = slot.endMin;
            m_isPotentialClick = false;
            return;
        }

        if (slot.renderedRect.contains(pos)) {
            if (locked) {
                m_activeDragOp = 0;
                m_dragTargetEvent = slot.event;
                m_selectedIds.clear();
                m_selectedIds.insert(slot.event.id);
                m_isPotentialClick = true;
                update();
                return;
            }

            m_activeDragOp = 1;
            m_dragTargetEvent = slot.event;
            m_targetDayIdx = m_origDayIdx = slot.event.start.date().dayOfWeek() - 1;
            m_targetStartMin = m_origStartMin = slot.startMin;
            m_targetEndMin = m_origEndMin = slot.endMin;
            m_selectedIds.clear();
            m_selectedIds.insert(slot.event.id);
            update();
            return;
        }
    }

    if (pos.x() >= LEFT_GUTTER && pos.y() >= HEADER_HEIGHT) {
        m_selectedIds.clear();
        m_dragDayIdx = std::clamp(static_cast<int>((pos.x() - LEFT_GUTTER) / colW), 0, 6);
        m_dragStartMin = yToMinutes(pos.y());
        m_dragEndMin = m_dragStartMin + 30;
        m_isDraggingCreation = true;
        update();
    }
}

// 10. Rysowanie siatki z wyroznieniem kafelkow nauki ICS
void CalendarCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    float colW = (width() - LEFT_GUTTER) / 7.0f;
    QDate today = QDate::currentDate();
    QTime nowTime = QTime::currentTime();

    for (int i = 0; i < 7; ++i) {
        QDate d = m_currentMonday.addDays(i);
        float x = LEFT_GUTTER + i * colW;
        bool isToday = (d == today);

        QRectF hRect(x + 1, 2, colW - 2, HEADER_HEIGHT - 4);
        p.setPen(Qt::NoPen);
        p.setBrush(isToday ? QColor(40, 48, 60) : QColor(26, 26, 26));
        p.drawRoundedRect(hRect, 4, 4);

        p.setPen(isToday ? QColor(108, 179, 63) : QColor(200, 200, 200));
        p.setFont(QFont("Segoe UI", 9, isToday ? QFont::Bold : QFont::Normal));
        p.drawText(hRect, Qt::AlignCenter, QString("%1 %2").arg(d.toString("ddd").toUpper(), d.toString("dd")));

        p.setPen(QColor(38, 38, 38));
        p.drawLine(QPointF(x, HEADER_HEIGHT), QPointF(x, height()));
    }

    for (int hr = START_HOUR; hr <= END_HOUR; ++hr) {
        float y = HEADER_HEIGHT + (hr - START_HOUR) * HOUR_HEIGHT;
        p.setPen(QColor(110, 110, 110));
        p.setFont(QFont("Segoe UI", 8));
        p.drawText(QRectF(2, y - 8, LEFT_GUTTER - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString("%1:00").arg(hr, 2, 10, QChar('0')));

        p.setPen(QColor(32, 32, 32));
        p.drawLine(QPointF(LEFT_GUTTER, y), QPointF(width(), y));
    }

    for (const auto &slot : m_slots) {
        bool sel = (m_selectedIds.count(slot.event.id) > 0);
        bool isIcsStudyBlock = (slot.event.linkedProfile == "overdue");

        QColor baseColor(slot.event.color.isEmpty() ? "#4285f4" : slot.event.color);
        if (!baseColor.isValid()) baseColor = QColor("#4285f4");

        p.setBrush(baseColor);
        if (isIcsStudyBlock) {
            p.setPen(QPen(sel ? QColor(255, 255, 255) : QColor(255, 215, 0), 2.0f, Qt::DashLine));
        } else {
            p.setPen(sel ? QPen(QColor(255, 255, 255), 2.0f) : QPen(baseColor.darker(110), 1.0f));
        }
        p.drawRoundedRect(slot.renderedRect, 4, 4);

        if (!slot.event.linkedProfile.isEmpty()) {
            p.setPen(QColor(255, 255, 255));
            p.setFont(QFont("Segoe UI", 8));
            p.drawText(slot.renderedRect.adjusted(0, 2, -4, 0), Qt::AlignRight | Qt::AlignTop, "🔒");
        }

        p.setPen(QColor(255, 255, 255, 220));
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        QString timeTxt = QString("%1-%2").arg(slot.event.start.toString("hh:mm"), slot.event.end.toString("hh:mm"));
        p.drawText(slot.renderedRect.adjusted(5, 3, -18, 0), Qt::AlignLeft | Qt::AlignTop, timeTxt);

        p.setPen(QColor(255, 255, 255));
        p.setFont(QFont("Segoe UI", 8));
        QRectF titleRect = slot.renderedRect.adjusted(5, 16, -5, -2);
        QString elidedTitle = p.fontMetrics().elidedText(slot.event.title, Qt::ElideRight, static_cast<int>(titleRect.width()));
        p.drawText(titleRect, Qt::AlignLeft | Qt::AlignTop, elidedTitle);
    }

    if (m_activeDragOp != 0 && !m_isPotentialClick) {
        float x = LEFT_GUTTER + m_targetDayIdx * colW;
        float y1 = minutesToY(m_targetStartMin);
        float y2 = minutesToY(m_targetEndMin);
        QRectF ghostBox(x + 2, y1, colW - 4, std::max(16.0f, y2 - y1));

        p.setPen(QPen(QColor(255, 215, 0), 2.0f, Qt::DashLine));
        p.setBrush(QColor(255, 215, 0, 70));
        p.drawRoundedRect(ghostBox, 4, 4);
    }

    if (m_isDraggingCreation) {
        float x = LEFT_GUTTER + m_dragDayIdx * colW;
        float y1 = minutesToY(m_dragStartMin);
        float y2 = minutesToY(m_dragEndMin);
        QRectF box(x + 2, y1, colW - 4, std::max(16.0f, y2 - y1));

        p.setPen(QPen(QColor(108, 179, 63), 1.6f, Qt::DashLine));
        p.setBrush(QColor(108, 179, 63, 60));
        p.drawRoundedRect(box, 4, 4);
    }

    if (today >= m_currentMonday && today <= m_currentMonday.addDays(6)) {
        int dayOff = m_currentMonday.daysTo(today);
        float x = LEFT_GUTTER + dayOff * colW;
        float y = minutesToY(nowTime.hour() * 60 + nowTime.minute());

        p.setPen(QPen(QColor(255, 59, 48), 1.8f));
        p.drawLine(QPointF(x, y), QPointF(x + colW, y));
        p.setBrush(QColor(255, 59, 48));
        p.drawEllipse(QPointF(x, y), 3.5f, 3.5f);
    }
}

// 11. Nawigacja
void CalendarCanvas::resizeEvent(QResizeEvent *) { rebuildLayoutSlots(); }
void CalendarCanvas::nextWeek() { m_currentMonday = m_currentMonday.addDays(7); rebuildLayoutSlots(); update(); }
void CalendarCanvas::prevWeek() { m_currentMonday = m_currentMonday.addDays(-7); rebuildLayoutSlots(); update(); }
void CalendarCanvas::goToToday()
{
    QDate today = QDate::currentDate();
    m_currentMonday = today.addDays(-(today.dayOfWeek() - 1));
    rebuildLayoutSlots();
    update();
}
QString CalendarCanvas::getWeekPeriodLabel() const
{
    return QString("%1 — %2").arg(m_currentMonday.toString("dd MMM"), m_currentMonday.addDays(6).toString("dd MMM yyyy"));
}
QDate CalendarCanvas::getCurrentMonday() const { return m_currentMonday; }
void CalendarCanvas::setSelectedDate(const QDate &d)
{
    m_currentMonday = d.addDays(-(d.dayOfWeek() - 1));
    rebuildLayoutSlots();
    update();
}
void CalendarCanvas::refreshFromEngine() { rebuildLayoutSlots(); update(); }
void CalendarCanvas::setProfileManager(ProfileManager *profileManager) { m_profileManager = profileManager; }

// 12. Konstruktor
CalendarCanvas::CalendarCanvas(CalendarEngine *engine, ProfileManager *profileManager, QWidget *parent)
    : QWidget(parent)
    , m_engine(engine)
    , m_profileManager(profileManager)
    , m_isDraggingCreation(false)
    , m_dragDayIdx(0)
    , m_dragStartMin(0)
    , m_dragEndMin(0)
    , m_activeDragOp(0)
    , m_clockTimer(new QTimer(this))
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    goToToday();

    int totalH = HEADER_HEIGHT + static_cast<int>((END_HOUR - START_HOUR) * HOUR_HEIGHT) + 20;
    setFixedHeight(totalH);

    if (m_engine) {
        connect(m_engine, &CalendarEngine::eventsUpdated, this, &CalendarCanvas::refreshFromEngine);
    }
    connect(m_clockTimer, &QTimer::timeout, this, [this]() { update(); });
    m_clockTimer->start(15000);
}
