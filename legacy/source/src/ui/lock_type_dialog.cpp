// Plik implementacji okna LockTypeDialog.
// Prezentuje opcje None, Timer, Random Text oraz Minigame z parametrami.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "lock_type_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

// 1. Zapis konfiguracji i zamkniecie dialogu
void LockTypeDialog::onSaveClicked()
{
    accept();
}

// 2. Obsluga przelaczania radio buttonow i aktualizacja opisow
void LockTypeDialog::onRadioToggled()
{
    if (m_rbNone->isChecked()) {
        m_descriptionLabel->setText("Don't lock this block. Allow changes and turning On/Off at any time.");
        m_timeEdit->setVisible(false);
        m_charsSpin->setVisible(false);
    } else if (m_rbTimer->isChecked()) {
        m_descriptionLabel->setText("Lock this block until a specific hour today. No changes allowed until timer expires.");
        m_timeEdit->setVisible(true);
        m_charsSpin->setVisible(false);
    } else if (m_rbChars->isChecked()) {
        m_descriptionLabel->setText("Lock this block with Japanese text verification. Unlocking requires typing characters.");
        m_timeEdit->setVisible(false);
        m_charsSpin->setVisible(true);
    } else if (m_rbMinigame->isChecked()) {
        m_descriptionLabel->setText("Lock this block with a Minigame Challenge. Unlocking requires beating a minigame.");
        m_timeEdit->setVisible(false);
        m_charsSpin->setVisible(false);
    }
}

// 3. Budowa interfejsu
void LockTypeDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *header = new QLabel(QString("CHOOSE A LOCK FOR '%1'").arg(m_profileName.toUpper()), this);
    header->setObjectName("dialogHeader");
    header->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(header);

    QHBoxLayout *radioRow = new QHBoxLayout();
    m_rbNone = new QRadioButton("None", this);
    m_rbTimer = new QRadioButton("Timer", this);
    m_rbChars = new QRadioButton("Random Text", this);
    m_rbMinigame = new QRadioButton("Minigame", this);

    radioRow->addWidget(m_rbNone);
    radioRow->addWidget(m_rbTimer);
    radioRow->addWidget(m_rbChars);
    radioRow->addWidget(m_rbMinigame);
    mainLayout->addLayout(radioRow);

    m_descriptionLabel = new QLabel(this);
    m_descriptionLabel->setWordWrap(true);
    mainLayout->addWidget(m_descriptionLabel);

    m_timeEdit = new QTimeEdit(QTime::currentTime().addSecs(7200), this);
    m_timeEdit->setDisplayFormat("hh:mm");
    mainLayout->addWidget(m_timeEdit);

    QHBoxLayout *charsRow = new QHBoxLayout();
    charsRow->addWidget(new QLabel("Liczba znaków do odblokowania:", this));
    m_charsSpin = new QSpinBox(this);
    m_charsSpin->setRange(100, 10000);
    m_charsSpin->setSingleStep(500);
    m_charsSpin->setValue(m_currentChars > 0 ? m_currentChars : 1000);
    charsRow->addWidget(m_charsSpin);
    mainLayout->addLayout(charsRow);

    mainLayout->addStretch();

    QHBoxLayout *actionRow = new QHBoxLayout();
    actionRow->addStretch();
    m_cancelBtn = new QPushButton("Close without saving", this);
    m_saveBtn = new QPushButton("Save", this);
    m_saveBtn->setObjectName("primaryGreenBtn");
    actionRow->addWidget(m_cancelBtn);
    actionRow->addWidget(m_saveBtn);
    mainLayout->addLayout(actionRow);

    connect(m_rbNone, &QRadioButton::toggled, this, &LockTypeDialog::onRadioToggled);
    connect(m_rbTimer, &QRadioButton::toggled, this, &LockTypeDialog::onRadioToggled);
    connect(m_rbChars, &QRadioButton::toggled, this, &LockTypeDialog::onRadioToggled);
    connect(m_rbMinigame, &QRadioButton::toggled, this, &LockTypeDialog::onRadioToggled);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_saveBtn, &QPushButton::clicked, this, &LockTypeDialog::onSaveClicked);

    if (m_currentType == LockType::UntilTime) {
        m_rbTimer->setChecked(true);
    } else if (m_currentType == LockType::Characters) {
        m_rbChars->setChecked(true);
    } else if (m_currentType == LockType::Minigame) {
        m_rbMinigame->setChecked(true);
    } else {
        m_rbNone->setChecked(true);
    }
    onRadioToggled();
}

// 4. Gettery parametrow
LockType LockTypeDialog::getSelectedLockType() const
{
    if (m_rbTimer->isChecked()) return LockType::UntilTime;
    if (m_rbChars->isChecked()) return LockType::Characters;
    if (m_rbMinigame->isChecked()) return LockType::Minigame;
    return LockType::None;
}

int LockTypeDialog::getSelectedChars() const
{
    return m_charsSpin->value();
}

QDateTime LockTypeDialog::getSelectedUntilTime() const
{
    QTime time = m_timeEdit->time();
    QDateTime target(QDate::currentDate(), time);
    if (target < QDateTime::currentDateTime()) {
        target = target.addDays(1);
    }
    return target;
}

// 5. Konstruktor okna
LockTypeDialog::LockTypeDialog(const QString &profileName,
                               LockType currentType,
                               int currentChars,
                               const QDateTime &currentUntilTime,
                               QWidget *parent)
    : QDialog(parent)
    , m_profileName(profileName)
    , m_currentType(currentType)
    , m_currentChars(currentChars)
    , m_currentUntilTime(currentUntilTime)
{
    setWindowTitle(QString("Blokada - %1").arg(profileName));
    resize(540, 310);
    setupUi();
}
