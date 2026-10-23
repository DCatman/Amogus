// Plik naglowkowy okna EditBlockDialog.
// Obsluguje edycje regul profili, masowy import z tekstu i plikow TXT
// (zarowno wyjatkow stron, jak i regul aplikacji z prefiksami title: oraz file:).
// Laczy sie z ProfileManager i BlocksView.

#ifndef EDIT_BLOCK_DIALOG_H
#define EDIT_BLOCK_DIALOG_H

#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QRadioButton>
#include <QLabel>
#include "profile_manager.h"

class EditBlockDialog : public QDialog
{
    Q_OBJECT

public:
    explicit EditBlockDialog(const BlockProfile &profile, ProfileManager *profileManager = nullptr, QWidget *parent = nullptr);
    ~EditBlockDialog() override = default;

    BlockProfile getUpdatedProfile() const;
    bool isProfileDeletionRequested() const;

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onAddWebsite();
    void onRemoveWebsite();
    void onAddException();
    void onRemoveException();
    void onAddExeFile();
    void onAddFolder();
    void onAddWindowTitle();
    void onRemoveApp();
    void onBulkImportTextClicked();
    void onBulkImportFileClicked();
    void onDeleteProfileClicked();
    void onSaveClicked();

private:
    bool isCurrentBlockLocked() const;
    void parseAndAppendRules(const QString &text);
    void setupUi();
    QWidget *createWebsitesTab();
    QWidget *createExceptionsTab();
    QWidget *createAppsTab();
    QWidget *createBreaksTab();

    BlockProfile m_profile;
    ProfileManager *m_profileManager;
    bool m_deletionRequested;

    QLineEdit *m_websiteInput;
    QListWidget *m_websitesList;
    QPushButton *m_removeWebsiteBtn;

    QLineEdit *m_exceptionInput;
    QPushButton *m_addExceptionBtn;
    QPushButton *m_btnBulkExceptions;
    QListWidget *m_exceptionsList;
    QPushButton *m_removeExceptionBtn;

    QListWidget *m_appsList;
    QPushButton *m_removeAppBtn;
    QPushButton *m_btnBulkApps;

    QRadioButton *m_rbAllTimes;
    QRadioButton *m_rbCalendar;

    QCheckBox *m_chkClickBreak;
    QSpinBox *m_spinClickMin;
    QCheckBox *m_chkCharBreak;
    QSpinBox *m_spinCharMin;
    QSpinBox *m_spinCharCount;
    QCheckBox *m_chkCritBreak;

    QPushButton *m_btnBulkImportText;
    QPushButton *m_btnBulkImportFile;
    QPushButton *m_btnDeleteProfile;
};

#endif // EDIT_BLOCK_DIALOG_H
