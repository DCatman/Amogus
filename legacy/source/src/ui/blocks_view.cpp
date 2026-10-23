// Plik implementacji widoku BlocksView.
// Umozliwia edycje i przelaczanie profili, ustawia domyslne wyzwanie na 1000 znakow oraz obsluguje minigry jako forme odblokowania.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "blocks_view.h"
#include "block_row_widget.h"
#include "calendar_engine.h"
#include "edit_block_dialog.h"
#include "lock_type_dialog.h"
#include "unlock_dialog.h"
#include "minigame_unlock_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QLabel>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>

// 1. Obsluga przerwy
void BlocksView::onBreakRequested(const QString &profileName)
{
    Q_UNUSED(profileName);
}

// 2. Obsluga konfiguracji klodki
void BlocksView::onLockConfigRequested(const QString &profileName)
{
    const BlockProfile *p = m_profileManager->getProfile(profileName);
    if (!p) return;

    if (m_profileManager->isProfileLocked(profileName)) {
        if (p->lockType == LockType::UntilTime) {
            QMessageBox::warning(this, "Blokada czasowa",
                                 QString("Ten blok jest zablokowany do %1 i nie można go zmodyfikować.")
                                     .arg(p->lockUntilTime.toString("hh:mm")));
            return;
        } else if (p->lockType == LockType::Characters) {
            UnlockDialog drill(p->requiredChars > 0 ? p->requiredChars : 1000, this);
            if (drill.exec() == QDialog::Accepted) {
                m_profileManager->unlockProfile(profileName);
                QMessageBox::information(this, "Odblokowano", "Blok został odblokowany!");
            }
            return;
        } else if (p->lockType == LockType::Minigame) {
            MinigameUnlockDialog miniDlg(this);
            if (miniDlg.exec() == QDialog::Accepted) {
                m_profileManager->unlockProfile(profileName);
                QMessageBox::information(this, "Odblokowano", "Wyzwanie minigry zaliczone! Blok odblokowany.");
            }
            return;
        }
    }

    LockTypeDialog dlg(profileName, p->lockType, p->requiredChars, p->lockUntilTime, this);
    if (dlg.exec() == QDialog::Accepted) {
        m_profileManager->setProfileLock(profileName,
                                         dlg.getSelectedLockType(),
                                         dlg.getSelectedChars(),
                                         dlg.getSelectedUntilTime());
    }
}

// 3. Edycja bloku z mozliwoscia usuniecia profilu
void BlocksView::onEditRequested(const QString &profileName)
{
    const BlockProfile *p = m_profileManager->getProfile(profileName);
    if (!p) return;

    EditBlockDialog dlg(*p, m_profileManager, this);
    if (dlg.exec() == QDialog::Accepted) {
        if (dlg.isProfileDeletionRequested()) {
            m_profileManager->deleteProfile(profileName);
            QMessageBox::information(this, "Usunięto", QString("Profil '%1' został usunięty.").arg(profileName));
        } else {
            m_profileManager->addOrUpdateProfile(dlg.getUpdatedProfile());
        }
    }
}

// 4. Przelaczenie ON/OFF
void BlocksView::onToggleRequested(const QString &profileName)
{
    const BlockProfile *p = m_profileManager->getProfile(profileName);
    if (!p) return;

    if (p->isActive && m_profileManager->isProfileLocked(profileName)) {
        if (p->lockType == LockType::UntilTime) {
            QMessageBox::warning(this, "Blokada czasowa",
                                 QString("Blok jest aktywny i zablokowany do godziny %1.").arg(p->lockUntilTime.toString("hh:mm")));
            return;
        } else if (p->lockType == LockType::Characters) {
            UnlockDialog drill(p->requiredChars > 0 ? p->requiredChars : 1000, this);
            if (drill.exec() == QDialog::Accepted) {
                m_profileManager->unlockProfile(profileName);
                m_profileManager->setProfileActive(profileName, false);
            }
            return;
        } else if (p->lockType == LockType::Minigame) {
            MinigameUnlockDialog miniDlg(this);
            if (miniDlg.exec() == QDialog::Accepted) {
                m_profileManager->unlockProfile(profileName);
                m_profileManager->setProfileActive(profileName, false);
            }
            return;
        }
    }

    m_profileManager->setProfileActive(profileName, !p->isActive);
}

// 5. Utworzenie nowego profilu
void BlocksView::onNewBlockClicked()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "Nowy blok", "Podaj nazwę bloku:", QLineEdit::Normal, "", &ok);
    if (ok && !name.trimmed().isEmpty()) {
        BlockProfile p;
        p.name = name.trimmed();
        p.blockedWebsites << "*";
        p.isActive = false;
        p.lockType = LockType::None;
        p.isLocked = false;
        m_profileManager->addOrUpdateProfile(p);

        EditBlockDialog editDlg(p, m_profileManager, this);
        if (editDlg.exec() == QDialog::Accepted) {
            if (!editDlg.isProfileDeletionRequested()) {
                m_profileManager->addOrUpdateProfile(editDlg.getUpdatedProfile());
            }
        }
    }
}

// 6. Odswiezenie listy wierszy z prawidlowym przekazaniem CalendarEngine
void BlocksView::refreshBlockList()
{
    QLayoutItem *child = nullptr;
    while ((child = m_cardsContainerLayout->takeAt(0)) != nullptr) {
        if (child->widget()) child->widget()->deleteLater();
        delete child;
    }

    const auto &profiles = m_profileManager->getAllProfiles();
    for (const auto &p : profiles) {
        BlockRowWidget *row = new BlockRowWidget(p, m_calendarEngine, m_profileManager, this);

        connect(row, &BlockRowWidget::toggleRequested, this, &BlocksView::onToggleRequested);
        connect(row, &BlockRowWidget::editRequested, this, &BlocksView::onEditRequested);
        connect(row, &BlockRowWidget::lockConfigRequested, this, &BlocksView::onLockConfigRequested);

        connect(row, &BlockRowWidget::clickBreakRequested, this, [this](const QString &name) {
            m_profileManager->triggerClickBreak(name);
        });

        connect(row, &BlockRowWidget::charBreakRequested, this, [this](const QString &name) {
            const BlockProfile *prof = m_profileManager->getProfile(name);
            if (!prof) return;
            UnlockDialog drill(prof->breaks.charBreakChars, this);
            if (drill.exec() == QDialog::Accepted) {
                m_profileManager->triggerCharBreak(name);
            }
        });

        connect(row, &BlockRowWidget::criticalBreakRequested, this, [this](const QString &name) {
            int cooldown = m_profileManager->getCriticalBreakCooldownRemaining(name);
            if (cooldown > 0) {
                QMessageBox::warning(this, "Cooldown", QString("Przerwa krytyczna będzie dostępna za %1 min.").arg(cooldown / 60 + 1));
                return;
            }
            m_profileManager->triggerCriticalBreak(name);
        });

        m_cardsContainerLayout->addWidget(row);
    }
    m_cardsContainerLayout->addStretch();
}

// 7. Budowa ukladu graficznego
void BlocksView::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);

    QHBoxLayout *headerRow = new QHBoxLayout();
    QLabel *titleLabel = new QLabel("WEBSITE & APP BLOCKS", this);
    titleLabel->setObjectName("viewHeaderTitle");
    headerRow->addWidget(titleLabel);
    headerRow->addStretch();

    m_newBlockButton = new QPushButton("+", this);
    m_newBlockButton->setObjectName("roundAddBtn");
    headerRow->addWidget(m_newBlockButton);
    mainLayout->addLayout(headerRow);

    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    QWidget *container = new QWidget();
    m_cardsContainerLayout = new QVBoxLayout(container);
    m_cardsContainerLayout->setContentsMargins(0, 8, 0, 8);
    m_cardsContainerLayout->setSpacing(8);
    scrollArea->setWidget(container);
    mainLayout->addWidget(scrollArea);

    m_bottomNewBlockButton = new QPushButton("+ New block...", this);
    m_bottomNewBlockButton->setObjectName("primaryGreenBtn");
    mainLayout->addWidget(m_bottomNewBlockButton);

    connect(m_newBlockButton, &QPushButton::clicked, this, &BlocksView::onNewBlockClicked);
    connect(m_bottomNewBlockButton, &QPushButton::clicked, this, &BlocksView::onNewBlockClicked);
    connect(m_profileManager, &ProfileManager::profilesUpdated, this, &BlocksView::refreshBlockList);

    refreshBlockList();
}

// 8. Konstruktor widoku BlocksView
BlocksView::BlocksView(ProfileManager *profileManager, CalendarEngine *calendarEngine, QWidget *parent)
    : QWidget(parent)
    , m_profileManager(profileManager)
    , m_calendarEngine(calendarEngine)
    , m_cardsContainerLayout(nullptr)
    , m_newBlockButton(nullptr)
    , m_bottomNewBlockButton(nullptr)
{
    setupUi();
}
