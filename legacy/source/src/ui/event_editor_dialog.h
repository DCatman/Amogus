// Plik naglowkowy okna EventEditorDialog.
// Zapewnia edycje wydarzen oraz wybor opcji bezposredniego powiazania z nauka przedmiotu (linkedSubject).
// Laczy sie z CalendarEngine, ProfileManager i DatabaseManager.

#ifndef EVENT_EDITOR_DIALOG_H
#define EVENT_EDITOR_DIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QTimeEdit>
#include <QDateEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include "calendar_engine.h"
#include "profile_manager.h"

class EventEditorDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EventEditorDialog(const CalendarEvent &event, bool isEditMode,
                               ProfileManager *profileManager, QWidget *parent = nullptr);
    ~EventEditorDialog() override = default;

    CalendarEvent getEventData() const;
    bool isDeleteRequested() const;
    bool isDeleteEntireSeriesRequested() const;
    bool isRecurrenceEnabled() const;
    int getRecurrenceWeeksCount() const;

private slots:
    void onSaveClicked();
    void onDeleteClicked();
    void onDebugForceDeleteClicked();
    void onDeleteSeriesClicked();

private:
    void setupUi();

    CalendarEvent m_event;
    bool m_isEditMode;
    bool m_deleteRequested;
    bool m_deleteSeriesRequested;
    ProfileManager *m_profileManager;

    QLineEdit *m_titleEdit;
    QComboBox *m_categoryBox;
    QComboBox *m_linkedProfileBox;
    QComboBox *m_linkedSubjectBox;
    QDateEdit *m_dateEdit;
    QTimeEdit *m_startEdit;
    QTimeEdit *m_endEdit;
    QComboBox *m_repeatBox;
    QSpinBox *m_occurrencesSpin;
};

#endif // EVENT_EDITOR_DIALOG_H
