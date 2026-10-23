// Plik naglowkowy widoku SubjectsView.
// Rozdziela dolna sekcje na 2 kolumny: tabele terminow (po lewej) oraz edytor notatek/linkow (po prawej).
// Wylicza sumaryczny czas wylacznie z blokow wlasnej nauki (Spaced Learning / Overdue),
// z pominieciem czasu trwania planowych zajec z siatki uczelnianej.
// Laczy sie z SubjectManager, CalendarEngine i DatabaseManager.

#ifndef SUBJECTS_VIEW_H
#define SUBJECTS_VIEW_H

#include <QWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include "subject_manager.h"
#include "calendar_engine.h"

class SubjectsView : public QWidget
{
    Q_OBJECT

public:
    explicit SubjectsView(SubjectManager *subjectManager, CalendarEngine *calendarEngine = nullptr, QWidget *parent = nullptr);
    ~SubjectsView() override = default;

public slots:
    void refreshSubjectsList();
    void refreshClassesList();

private slots:
    void onSubjectSelectionChanged();
    void onNotesTextChanged();
    void onImportJsonFileClicked();
    void onPasteJsonClicked();
    void onDeleteSubjectClicked();
    void onDeleteSelectedClassClicked();
    void onSubjectCellChanged(int row, int column);
    void onClassCellChanged(int row, int column);

private:
    int calculateTotalStudyMinutes(const QString &subjectName) const;
    void setupUi();

    SubjectManager *m_subjectManager;
    CalendarEngine *m_calendarEngine;
    bool m_isUpdating;
    bool m_isUpdatingNotes;

    QTableWidget *m_subjectsTable;
    QTableWidget *m_classesTable;
    QTextEdit *m_notesEdit;

    QLabel *m_statusLabel;
    QLabel *m_classesHeaderLabel;
    QLabel *m_notesHeaderLabel;
    QLabel *m_dbPathLabel;

    QPushButton *m_importFileBtn;
    QPushButton *m_pasteJsonBtn;
    QPushButton *m_deleteSubjectBtn;
    QPushButton *m_deleteClassBtn;
};

#endif // SUBJECTS_VIEW_H
