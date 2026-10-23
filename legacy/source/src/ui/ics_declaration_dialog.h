// Plik naglowkowy okna IcsDeclarationDialog.
// Informuje o priorytecie kolokwiow, wyswietla tor przedmiotu oraz tydzien semestru.
// Laczy sie z OverdueTracker i CalendarEngine.

#ifndef ICS_DECLARATION_DIALOG_H
#define ICS_DECLARATION_DIALOG_H

#include <QDialog>
#include <QTimeEdit>
#include <QPushButton>
#include <QLabel>
#include "overdue_tracker.h"
#include "calendar_engine.h"

class IcsDeclarationDialog : public QDialog
{
    Q_OBJECT

public:
    explicit IcsDeclarationDialog(const QString &displayName, int durationHours, int weekIdx,
                                  bool isExam,
                                  OverdueTracker *tracker, CalendarEngine *calendarEngine,
                                  QWidget *parent = nullptr);
    ~IcsDeclarationDialog() override = default;

private slots:
    void onConfirmClicked();
    void onPostponeClicked();

private:
    void setupUi();

    QString m_displayName;
    int m_durationHours;
    int m_weekIdx;
    bool m_isExam;

    OverdueTracker *m_tracker;
    CalendarEngine *m_calendarEngine;

    QTimeEdit *m_timeStartEdit;
    QPushButton *m_confirmBtn;
    QPushButton *m_postponeBtn;
    QLabel *m_statusLabel;
    QLabel *m_afkDebtNotice;
};

#endif // ICS_DECLARATION_DIALOG_H
