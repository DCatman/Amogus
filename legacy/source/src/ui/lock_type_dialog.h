// Plik naglowkowy okna wyboru kłódki CHOOSE A LOCK FOR [NAME].
// Zapewnia wybor miedzy: None, Timer, Random Text oraz Minigame Challenge.
// Laczy sie z ProfileManager i BlocksView.

#ifndef LOCK_TYPE_DIALOG_H
#define LOCK_TYPE_DIALOG_H

#include <QDialog>
#include <QRadioButton>
#include <QTimeEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QLabel>
#include "profile_manager.h"

class LockTypeDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LockTypeDialog(const QString &profileName,
                            LockType currentType,
                            int currentChars,
                            const QDateTime &currentUntilTime,
                            QWidget *parent = nullptr);
    ~LockTypeDialog() override = default;

    LockType getSelectedLockType() const;
    int getSelectedChars() const;
    QDateTime getSelectedUntilTime() const;

private slots:
    void onRadioToggled();
    void onSaveClicked();

private:
    void setupUi();

    QString m_profileName;
    LockType m_currentType;
    int m_currentChars;
    QDateTime m_currentUntilTime;

    QRadioButton *m_rbNone;
    QRadioButton *m_rbTimer;
    QRadioButton *m_rbChars;
    QRadioButton *m_rbMinigame;

    QLabel *m_descriptionLabel;
    QTimeEdit *m_timeEdit;
    QSpinBox *m_charsSpin;

    QPushButton *m_saveBtn;
    QPushButton *m_cancelBtn;
};

#endif // LOCK_TYPE_DIALOG_H
