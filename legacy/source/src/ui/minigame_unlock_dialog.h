// Plik naglowkowy dialogu odblokowania poprzez minigre MinigameUnlockDialog.
// Definiuje abstrakcyjna klase bazowa MinigameBaseWidget sluzaca jako szkielet dla minigier
// oraz okno dialogowe MinigameUnlockDialog zarzadzajace procesem odblokowania.
// Laczy sie z ProfileManager, BlocksView, MainWindow oraz WhitePuzzleWidget.

#ifndef MINIGAME_UNLOCK_DIALOG_H
#define MINIGAME_UNLOCK_DIALOG_H

#include <QDialog>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class MinigameBaseWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MinigameBaseWidget(QWidget *parent = nullptr) : QWidget(parent) {}
    ~MinigameBaseWidget() override = default;

    virtual void startMinigame() = 0;
    virtual void resetMinigame() = 0;

signals:
    void challengeWon();
    void challengeLost();
};

class MinigameUnlockDialog : public QDialog
{
    Q_OBJECT

public:
    explicit MinigameUnlockDialog(QWidget *parent = nullptr);
    ~MinigameUnlockDialog() override = default;

    void setCustomMinigame(MinigameBaseWidget *customWidget);

private slots:
    void onChallengeWon();
    void onChallengeLost();

private:
    void setupUi();

    QVBoxLayout *m_gameContainerLayout;
    MinigameBaseWidget *m_currentMinigame;
    QLabel *m_headerLabel;
    QPushButton *m_giveUpButton;
};

#endif // MINIGAME_UNLOCK_DIALOG_H
