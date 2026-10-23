// Plik implementacji dialogu odblokowania poprzez minigre MinigameUnlockDialog.
// Zapewnia osadzenie WhitePuzzleWidget w ukladzie okna i zarzadzanie statusem wygranej/przegranej.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "minigame_unlock_dialog.h"
#include "whitepuzzlewidget.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>

// 1. Reakcja na porazke w minigrze
void MinigameUnlockDialog::onChallengeLost()
{
    QMessageBox::warning(this, "Przegrana", "Nie udało się ukończyć minigry (utrata fokusu okna). Blokada pozostaje aktywna.");
    if (m_currentMinigame) {
        m_currentMinigame->resetMinigame();
    }
}

// 2. Reakcja na wygranie minigry
void MinigameUnlockDialog::onChallengeWon()
{
    QMessageBox::information(this, "Sukces!", "Białe puzzle ułożone pomyślnie! Blokada została zdjęta, a dźwięk przywrócony.");
    accept();
}

// 3. Podmiana aktywnego widgetu minigry na wlasna implementacje
void MinigameUnlockDialog::setCustomMinigame(MinigameBaseWidget *customWidget)
{
    if (!customWidget) return;

    if (m_currentMinigame) {
        m_gameContainerLayout->removeWidget(m_currentMinigame);
        m_currentMinigame->deleteLater();
    }

    m_currentMinigame = customWidget;
    m_gameContainerLayout->addWidget(m_currentMinigame);

    connect(m_currentMinigame, &MinigameBaseWidget::challengeWon, this, &MinigameUnlockDialog::onChallengeWon);
    connect(m_currentMinigame, &MinigameBaseWidget::challengeLost, this, &MinigameUnlockDialog::onChallengeLost);

    m_currentMinigame->startMinigame();
}

// 4. Budowa interfejsu z wbudowanym WhitePuzzleWidget (dopasowanie do siatek 10x10 - 14x14)
void MinigameUnlockDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 10, 14, 10);
    mainLayout->setSpacing(8);

    m_headerLabel = new QLabel("WYZWANIE ODBLOKOWANIA // BIAŁE PUZZLE (GHOST OUTLINES)", this);
    m_headerLabel->setObjectName("dialogHeader");
    m_headerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_headerLabel);

    QWidget *containerWidget = new QWidget(this);
    m_gameContainerLayout = new QVBoxLayout(containerWidget);
    m_gameContainerLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(containerWidget, 1);

    m_currentMinigame = new WhitePuzzleWidget(this);
    m_gameContainerLayout->addWidget(m_currentMinigame);
    connect(m_currentMinigame, &MinigameBaseWidget::challengeWon, this, &MinigameUnlockDialog::onChallengeWon);
    connect(m_currentMinigame, &MinigameBaseWidget::challengeLost, this, &MinigameUnlockDialog::onChallengeLost);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_giveUpButton = new QPushButton("Poddaj się (Zamknij)", this);
    m_giveUpButton->setObjectName("giveUpBtn");
    btnRow->addWidget(m_giveUpButton);
    mainLayout->addLayout(btnRow);

    connect(m_giveUpButton, &QPushButton::clicked, this, &QDialog::reject);
}

// 5. Konstruktor MinigameUnlockDialog
MinigameUnlockDialog::MinigameUnlockDialog(QWidget *parent)
    : QDialog(parent)
    , m_gameContainerLayout(nullptr)
    , m_currentMinigame(nullptr)
    , m_headerLabel(nullptr)
    , m_giveUpButton(nullptr)
{
    setWindowTitle("Wyzwanie odblokowania - Białe Puzzle (Ghost Outlines)");
    resize(1300, 840);
    setupUi();
}
