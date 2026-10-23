// Plik naglowkowy silnika kalendarza CalendarEngine.
// Zarzadza lista zdarzen w pamieci RAM oraz trwaloscia w bazie SQLite.
// Wprowadzono pole linkedSubject do CalendarEvent, umozliwiajace bezposrednie powiazanie bloku z przedmiotem.
// Laczy sie z DatabaseManager, GoogleCalendarSync, SubjectManager, ProfileManager oraz CalendarCanvas.

#ifndef CALENDAR_ENGINE_H
#define CALENDAR_ENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QList>
#include <QTimer>

struct CalendarEvent {
    QString id;
    QString seriesId;
    QString title;
    QString category;
    QString color = "#4285f4";
    QDateTime start;
    QDateTime end;
    QString linkedProfile;
    QString linkedSubject;
    QString googleId;
    qint64 updatedAt = 0;
};

class CalendarEngine : public QObject
{
    Q_OBJECT

public:
    explicit CalendarEngine(QObject *parent = nullptr);
    ~CalendarEngine() override = default;

    const QList<CalendarEvent> &getEvents() const;
    bool isBatchUpdating() const;
    bool isAnyEventActiveForProfile(const QString &profileName) const;

public slots:
    void loadFromDatabase();
    void setBatchUpdating(bool batch);
    void addEvent(const CalendarEvent &event);
    void updateEvent(const CalendarEvent &event);
    void removeEvent(const QString &id);
    void removeSeries(const QString &seriesId);
    void removeEventsForSubject(const QString &subjectName);
    void clearEventsByCategory(const QString &category);
    void clearAllEvents();
    int deduplicateLocalEvents();
    void scheduleRecurringProfile(const QString &profileName, const QTime &startTime, const QTime &endTime,
                                  const QList<int> &daysOfWeek, int occurrencesCount);

    void applyRemoteSync(const QList<CalendarEvent> &remoteEvents, const QStringList &deletedGoogleIds);

signals:
    void triggerProfileActivation(const QString &profileName, bool activate);
    void calendarWindowActiveChanged(const QString &profileName, bool windowActive);
    void eventsUpdated();
    void eventMutatedLocally(const CalendarEvent &event);
    void eventDeletedLocally(const QString &id, const QString &googleId);
    void batchCompletedLocally();

private slots:
    void checkSchedules();

private:
    QList<CalendarEvent> m_events;
    QTimer *m_scheduleCheckTimer;
    bool m_isBatchUpdating;
};

#endif // CALENDAR_ENGINE_H
