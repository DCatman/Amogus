// Plik implementacji modulu UnlockDialog.
// Zapewnia ulepszone wprowadzanie tekstu: uzytkownik wpisuje angielskie slowo,
// zatrzymuje sie, a po 2 sekundach pojawia sie podpowiedz Romaji, ktora moze po prostu dopisac.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "unlock_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QStyle>
#include <random>
#include <algorithm>

// 1. Pole tekstowe blokujace wklejanie ze schowka oraz pochlaniajace Enter
SecureTypingEdit::SecureTypingEdit(QWidget *parent)
    : QLineEdit(parent)
{
}

void SecureTypingEdit::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        emit returnPressed();
        event->accept();
        return;
    }

    if (event->matches(QKeySequence::Paste) ||
        ((event->modifiers() & Qt::ControlModifier) && event->key() == Qt::Key_V) ||
        ((event->modifiers() & Qt::ShiftModifier) && event->key() == Qt::Key_Insert)) {
        event->ignore();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void SecureTypingEdit::contextMenuEvent(QContextMenuEvent *event)
{
    event->ignore();
}

// 2. Baza par slow z poszerzonym slownictwem czasownikow i ich odmian
void UnlockDialog::initVocabBank()
{
    m_vocabBank = {
        {"water", "mizu"}, {"circuit", "kairo"}, {"electron", "denshi"}, {"resistor", "teikou"},
        {"eat", "taberu"}, {"he is eating", "tabete iru"}, {"do not eat", "tabenai de"},
        {"drink", "nomu"}, {"she drank", "nonda"}, {"I will drink", "nomimasu"},
        {"read", "yomu"}, {"read (command)", "yonde kudasai"}, {"was reading", "yonde ita"},
        {"write", "kaku"}, {"wrote", "kaita"}, {"please write", "kaite kudasai"},
        {"strength", "chikara"}, {"strong", "tsuyoi"}, {"muscle", "kinniku"},
        {"calculation", "keisan"}, {"school", "gakkou"}, {"teacher", "sensei"},
        {"fast", "hayai"}, {"new", "atarashii"}, {"time", "jikan"}, {"today", "kyou"},
        {"tomorrow", "ashita"}, {"yesterday", "kinou"}, {"morning", "asa"},
        {"think", "kangaeru"}, {"build", "tsukuru"}, {"charge", "juuden"},
        {"worry", "shinpai"}, {"quiet", "shizuka"}, {"hot", "atsui"}, {"cold", "samui"}
    };
}

// 3. Uzupelnianie bufora par
void UnlockDialog::refillQueue()
{
    std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<> dist(0, static_cast<int>(m_vocabBank.size()) - 1);
    while (m_activeQueue.size() < 30) {
        m_activeQueue.push_back(m_vocabBank[dist(gen)]);
    }
}

// 4. Odswiezenie kafelkow - zawsze rozpoczyna pokazujac wylacznie slowo angielskie
void UnlockDialog::updateUiDisplay()
{
    if (m_activeQueue.empty()) refillQueue();
    m_promptWordLabel->setText(m_activeQueue.front().english);

    m_upcomingList->clear();
    for (size_t i = 1; i < std::min<size_t>(6, m_activeQueue.size()); ++i) {
        m_upcomingList->addItem(QString("%1.  %2").arg(i).arg(m_activeQueue[i].english));
    }

    m_progressBar->setValue(std::min(m_currentCharsTyped, m_targetChars));
    m_statsLabel->setText(QString("Postęp: %1 / %2 znaków (%3%)")
                              .arg(m_currentCharsTyped).arg(m_targetChars)
                              .arg((m_currentCharsTyped * 100) / std::max(1, m_targetChars)));
}

// 5. Pokazanie podpowiedzi po 2 sekundach niepisania
void UnlockDialog::onTypingPauseTimeout()
{
    if (m_activeQueue.empty() || m_isCompleting) return;
    m_promptWordLabel->setText(m_activeQueue.front().full());
}

// 6. Śledzenie pisania tekstu - zatrzymuje lub odpala timer
void UnlockDialog::onInputTextChanged(const QString &text)
{
    if (m_activeQueue.empty() || m_isCompleting) return;
    
    QString typed = text.trimmed().toLower();
    QString targetEng = m_activeQueue.front().english.toLower();
    
    if (typed == targetEng) {
        m_typingPauseTimer->start(2000);
    } else {
        m_typingPauseTimer->stop();
    }
}

// 7. Anti-cheat reset
void UnlockDialog::triggerAntiCheatReset(const QString &reason)
{
    if (m_isCompleting) return;
    m_currentCharsTyped = 0;
    m_typingPauseTimer->stop();
    
    m_activeQueue.clear();
    refillQueue();
    m_inputEdit->clear();
    m_feedbackLabel->setText(QString("ANTI-CHEAT: %1 Licznik zresetowany do 0!").arg(reason));
    m_feedbackLabel->setObjectName("cheatWarningActive");
    m_feedbackLabel->style()->unpolish(m_feedbackLabel);
    m_feedbackLabel->style()->polish(m_feedbackLabel);
    updateUiDisplay();
}

// 8. Weryfikacja calosci wpisu po nacisnieciu klawisza Enter
void UnlockDialog::onVerifyEntered()
{
    if (m_activeQueue.empty() || m_isCompleting) return;

    QString typed = m_inputEdit->text().trimmed().toLower();
    QString fullTarget = m_activeQueue.front().full().toLower();

    if (typed == fullTarget) {
        int added = m_activeQueue.front().full().length() + 1;
        m_currentCharsTyped += added;
        m_activeQueue.erase(m_activeQueue.begin());
        m_inputEdit->clear();
        m_typingPauseTimer->stop();
        m_feedbackLabel->setText(QString("Poprawnie! Dodano +%1 znaków.").arg(added));
        m_feedbackLabel->setObjectName("feedbackSuccess");
        updateUiDisplay();

        if (m_currentCharsTyped >= m_targetChars) {
            m_isCompleting = true;
            emit challengeCompleted();
            accept();
        }
    } else {
        m_feedbackLabel->setText("Błędnie! Wymagana pełna para (angielski i romaji): " + m_activeQueue.front().full());
        m_feedbackLabel->setObjectName("feedbackError");
        m_promptWordLabel->setText(m_activeQueue.front().full());
    }

    m_feedbackLabel->style()->unpolish(m_feedbackLabel);
    m_feedbackLabel->style()->polish(m_feedbackLabel);
}

// 9. Zmiana fokusu okna
void UnlockDialog::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange && !isActiveWindow() && m_currentCharsTyped > 0 && !m_isCompleting) {
        triggerAntiCheatReset("Wykryto przełączenie okna!");
    }
    QDialog::changeEvent(event);
}

// 10. Klawiatura: Enter weryfikuje wpis, Escape jest calkowicie ignorowany
void UnlockDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        onVerifyEntered();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        event->ignore();
        return;
    }
    QDialog::keyPressEvent(event);
}

// 11. Budowa okna
void UnlockDialog::setupUi()
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(10);

    QLabel *lblHeader = new QLabel(QString("ODBLOKOWANIE BLOKADY // WYZWANIE %1 ZNAKÓW").arg(m_targetChars), this);
    lblHeader->setObjectName("dialogHeader");
    root->addWidget(lblHeader);

    QLabel *lblCheatWarning = new QLabel("Anti-cheat: Utrzymuj fokus okna aż do ukończenia wszystkich znaków.", this);
    lblCheatWarning->setObjectName("cheatWarningLabel");
    root->addWidget(lblCheatWarning);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, m_targetChars);
    root->addWidget(m_progressBar);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setObjectName("statsLabel");
    root->addWidget(m_statsLabel);

    QLabel *lblQueueTitle = new QLabel("NADCHODZĄCE SŁOWA (LOOK AHEAD):", this);
    lblQueueTitle->setObjectName("queueTitleLabel");
    root->addWidget(lblQueueTitle);

    m_upcomingList = new QListWidget(this);
    m_upcomingList->setFixedHeight(74);
    root->addWidget(m_upcomingList);

    QFrame *card = new QFrame(this);
    card->setObjectName("typingCardFrame");
    QVBoxLayout *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(8);

    QLabel *promptHint = new QLabel("AKTYWNA PARA DO PRZEPISANIA (ANGIELSKI + JAPOŃSKI ROMAJI):", card);
    promptHint->setObjectName("promptHintLabel");
    cardLayout->addWidget(promptHint);

    m_promptWordLabel = new QLabel("water", card);
    m_promptWordLabel->setObjectName("targetWordPrompt");
    m_promptWordLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    cardLayout->addWidget(m_promptWordLabel);

    m_inputEdit = new SecureTypingEdit(card);
    m_inputEdit->setPlaceholderText("Wpisz po angielsku. Jeśli nie pamiętasz romaji, zaczekaj 2 sekundy po napisaniu na podpowiedź.");
    m_inputEdit->setFixedHeight(46);
    cardLayout->addWidget(m_inputEdit);

    m_feedbackLabel = new QLabel("Wpisz parę i naciśnij Enter.", card);
    m_feedbackLabel->setObjectName("feedbackNormal");
    cardLayout->addWidget(m_feedbackLabel);
    root->addWidget(card);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    m_btnCancel = new QPushButton("PODDAJ SIĘ (POZOSTAW BLOKADĘ)", this);
    m_btnCancel->setObjectName("giveUpBtn");
    m_btnCancel->setAutoDefault(false);
    m_btnCancel->setDefault(false);
    btnRow->addWidget(m_btnCancel);
    root->addLayout(btnRow);

    connect(m_inputEdit, &QLineEdit::textChanged, this, &UnlockDialog::onInputTextChanged);
    connect(m_inputEdit, &QLineEdit::returnPressed, this, &UnlockDialog::onVerifyEntered);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    initVocabBank();
    refillQueue();
    updateUiDisplay();
}

// 12. Konstruktor
UnlockDialog::UnlockDialog(int targetChars, QWidget *parent)
    : QDialog(parent)
    , m_targetChars(targetChars)
    , m_currentCharsTyped(0)
    , m_isCompleting(false)
    , m_typingPauseTimer(new QTimer(this))
    , m_btnCancel(nullptr)
{
    m_typingPauseTimer->setSingleShot(true);
    connect(m_typingPauseTimer, &QTimer::timeout, this, &UnlockDialog::onTypingPauseTimeout);

    setWindowTitle(QString("Cold Turkey Lock - Wyzwanie %1 znaków").arg(m_targetChars));
    resize(720, 560);
    setupUi();
}
