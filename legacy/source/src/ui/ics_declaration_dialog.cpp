// Plik implementacji okna IcsDeclarationDialog.
// Zapewnia wyrazne ostrzezenie o kolokwium, wyswietla wliczony dlug AFK i harmonogramuje sesje.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "ics_declaration_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

// 1. Odlozenie sesji
void IcsDeclarationDialog::onPostponeClicked()
{
    if (m_isExam) {
        QMessageBox::critical(this, "Brak możliwości odłożenia", "To jest KOLOKWIUM / EGZAMIN! Nie można odłożyć przygotowania na jutro.");
        return;
    }

    if (m_tracker->executePostpone(m_displayName)) {
        QMessageBox::information(this, "postPone", QString("Przełożono sesję dla '%1' na jutro.").arg(m_displayName));
        accept();
    } else {
        QMessageBox::warning(this, "postPone niedostępne",
                             "Przycisk postPone jest dostępny WYŁĄCZNIE wtedy, gdy wyrobienie sesji przed godziną 01:00 w nocy jest fizycznie niemożliwe!");
    }
}

// 2. Potwierdzenie i wstawienie bloku do kalendarza
void IcsDeclarationDialog::onConfirmClicked()
{
    int debtSecs = m_tracker->getPendingAfkDebtSeconds();
    int totalDurationSecs = (m_durationHours * 3600) + debtSecs;

    QTime startTime = m_timeStartEdit->time();
    QTime endTime = startTime.addSecs(totalDurationSecs);

    CalendarEvent ev;
    ev.title = QString("%1: %2 (T%3, %4h%5)")
                   .arg(m_isExam ? "🚨 KOLOKWIUM" : "Overdue")
                   .arg(m_displayName)
                   .arg(m_weekIdx)
                   .arg(m_durationHours)
                   .arg(debtSecs > 0 ? QString(" +%1m AFK").arg(debtSecs / 60) : QString());
    ev.category = "Profile blokowania";
    ev.linkedProfile = "overdue";
    ev.start = QDateTime(QDate::currentDate(), startTime);
    ev.end = QDateTime(QDate::currentDate(), endTime);
    ev.color = m_isExam ? "#d50000" : "#4285f4";

    m_calendarEngine->addEvent(ev);
    m_tracker->startOverdueSession(m_displayName, m_durationHours);

    QMessageBox::information(this, "Zadeklarowano",
                             QString("Zadeklarowano sesję %1 w godzinach %2 - %3. Profil overdue aktywuje się automatycznie!")
                                 .arg(m_displayName, startTime.toString("HH:mm"), endTime.toString("HH:mm")));
    accept();
}

// 3. Budowa interfejsu
void IcsDeclarationDialog::setupUi()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(22, 18, 22, 18);
    layout->setSpacing(12);

    QLabel *hdr = new QLabel(m_isExam ? "🚨 PRIORYTET: WYMAGANE PRZYGOTOWANIE DO KOLOKWIUM!" : "WYMAGANA DEKLARACJA SESJI NAUKI", this);
    hdr->setObjectName("dialogHeader");
    layout->addWidget(hdr);

    int debtMin = m_tracker->getPendingAfkDebtMinutes();
    QString timeDesc = QString("Czas nauki: %1 godz.").arg(m_durationHours);
    if (debtMin > 0) {
        timeDesc += QString(" + %1 min długu AFK przeniesionego z poprzednich sesji!").arg(debtMin);
    }

    QLabel *msg = new QLabel(
        QString("Przedmiot: %1\nTydzień semestru: Tydzień %2\n%3\n(Nauka musi odbyć się dzisiaj przed 01:00 w nocy).")
            .arg(m_displayName).arg(m_weekIdx).arg(timeDesc), this);
    msg->setObjectName(m_isExam ? "giveUpBtn" : "lockWarningBanner");
    layout->addWidget(msg);

    m_afkDebtNotice = new QLabel(this);
    m_afkDebtNotice->setObjectName("blockSubtitle");
    if (debtMin > 0) {
        m_afkDebtNotice->setText(QString("⚠️ Do sesji doliczono %1 min zaległego czasu AFK do odrobienia.").arg(debtMin));
    }
    layout->addWidget(m_afkDebtNotice);

    QHBoxLayout *timeRow = new QHBoxLayout();
    timeRow->addWidget(new QLabel("Wybierz godzinę rozpoczęcia sesji:", this));
    m_timeStartEdit = new QTimeEdit(QTime(20, 0), this);
    m_timeStartEdit->setDisplayFormat("HH:mm");
    timeRow->addWidget(m_timeStartEdit);
    layout->addLayout(timeRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName("blockSubtitle");
    layout->addWidget(m_statusLabel);

    QHBoxLayout *btnRow = new QHBoxLayout();
    m_postponeBtn = new QPushButton("postPone (Odłóż na jutro)", this);
    m_postponeBtn->setObjectName("giveUpBtn");
    m_confirmBtn = new QPushButton("Zadeklaruj sesję (Enter)", this);
    m_confirmBtn->setObjectName("primaryGreenBtn");

    btnRow->addWidget(m_postponeBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_confirmBtn);
    layout->addLayout(btnRow);

    connect(m_confirmBtn, &QPushButton::clicked, this, &IcsDeclarationDialog::onConfirmClicked);
    connect(m_postponeBtn, &QPushButton::clicked, this, &IcsDeclarationDialog::onPostponeClicked);

    if (m_isExam) {
        m_postponeBtn->setEnabled(false);
        m_statusLabel->setText("Opcja postPone wyłączona – kolokwium ma absolutny priorytet.");
    } else {
        bool canPostpone = m_tracker->isPostponeAllowed(m_displayName, QDateTime::currentDateTime());
        m_postponeBtn->setEnabled(canPostpone);
        if (!canPostpone) {
            m_statusLabel->setText("Opcja postPone zablokowana: Masz wystarczająco dużo czasu przed 01:00.");
        }
    }
}

// 4. Konstruktor
IcsDeclarationDialog::IcsDeclarationDialog(const QString &displayName, int durationHours, int weekIdx,
                                           bool isExam,
                                           OverdueTracker *tracker, CalendarEngine *calendarEngine,
                                           QWidget *parent)
    : QDialog(parent)
    , m_displayName(displayName)
    , m_durationHours(durationHours)
    , m_weekIdx(weekIdx)
    , m_isExam(isExam)
    , m_tracker(tracker)
    , m_calendarEngine(calendarEngine)
    , m_timeStartEdit(nullptr)
    , m_confirmBtn(nullptr)
    , m_postponeBtn(nullptr)
    , m_statusLabel(nullptr)
    , m_afkDebtNotice(nullptr)
{
    setWindowTitle(isExam ? "🚨 Kolokwium w tym tygodniu!" : "Deklaracja nauki ICS");
    resize(560, 310);
    setupUi();
}
