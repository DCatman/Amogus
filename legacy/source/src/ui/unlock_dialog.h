// Plik naglowkowy dialogu odblokowywania profilu Cold Turkey Unlock Dialog.
// Domyślny wymóg ukończenia wyzwania wynosi 1000 znaków.
// Zaimplementowano inteligentny system dopisywania (brak usuwania tekstu przy podpowiedzi) 
// oraz maszynę stanów z QTimerem.
// Laczy sie z ProfileManager, MainWindow i BlocksView.

#ifndef UNLOCK_DIALOG_H
#define UNLOCK_DIALOG_H

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QListWidget>
#include <QPushButton>
#include <QKeyEvent>
#include <QContextMenuEvent>
#include <QTimer>
#include <vector>

class SecureTypingEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit SecureTypingEdit(QWidget *parent = nullptr);
    ~SecureTypingEdit() override = default;

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
};

class UnlockDialog : public QDialog
{
    Q_OBJECT

    struct VocabPair {
        QString english;
        QString romaji;
        QString full() const { return QString("%1 %2").arg(english, romaji); }
    };

public:
    explicit UnlockDialog(int targetChars = 1000, QWidget *parent = nullptr);
    ~UnlockDialog() override = default;

signals:
    void challengeCompleted();

protected:
    void changeEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onVerifyEntered();
    void onInputTextChanged(const QString &text);
    void onTypingPauseTimeout();

private:
    void setupUi();
    void initVocabBank();
    void refillQueue();
    void updateUiDisplay();
    void triggerAntiCheatReset(const QString &reason);

    int m_targetChars;
    int m_currentCharsTyped;
    bool m_isCompleting;

    QTimer *m_typingPauseTimer;

    QLabel *m_promptWordLabel;
    SecureTypingEdit *m_inputEdit;
    QLabel *m_feedbackLabel;
    QProgressBar *m_progressBar;
    QLabel *m_statsLabel;
    QListWidget *m_upcomingList;
    QPushButton *m_btnCancel;

    std::vector<VocabPair> m_vocabBank;
    std::vector<VocabPair> m_activeQueue;
};

#endif // UNLOCK_DIALOG_H
