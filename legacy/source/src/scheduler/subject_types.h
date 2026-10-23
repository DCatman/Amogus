// Plik naglowkowy typow danych akademickich (SubjectClassItem, AcademicSubject).
// Definiuje struktury jednostek zajeciowych, egzaminow oraz pole notatek i linkow przedmiotu.
// Laczy sie z SubjectManager, OverdueTracker i CalendarEngine.

#ifndef SUBJECT_TYPES_H
#define SUBJECT_TYPES_H

#include <QString>
#include <QDateTime>
#include <QList>
#include <QMetaType>

struct SubjectClassItem {
    QString id;
    QString type; // "lecture", "lab", "exercise", "exam"
    int index = 1;
    QString label;
    QDateTime start;
    QDateTime end;
};

struct AcademicSubject {
    QString name;
    QString color = "#ff9800";
    bool isLecture = true;
    QString notes; // Notatki, linki i materialy dla danego przedmiotu
    QList<SubjectClassItem> classes;
};

Q_DECLARE_METATYPE(SubjectClassItem)
Q_DECLARE_METATYPE(AcademicSubject)

#endif // SUBJECT_TYPES_H
