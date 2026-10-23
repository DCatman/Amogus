// Plik implementacji modulu CalendarWidget.
// Czysta architektura UI: definicja widgetow i layoutu bez wywolan setStyleSheet.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#include "calendar_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>

// 1. Obsluga dodania nowego zdarzenia
void CalendarWidget::onAddEventClicked()
{
    CalendarEvent ev;
    ev.title = m_titleEdit->text().trimmed();
    if (ev.title.isEmpty()) ev.title = "Nowe wydarzenie";

    ev.category = m_categoryBox->currentText();
    ev.start = m_startEdit->dateTime();
    ev.end = m_endEdit->dateTime();
    ev.linkedProfile = (ev.category == "Profile blokowania") ? "overdue" : "";

    m_engine->addEvent(ev);
    m_titleEdit->clear();
    refreshEventsList();
}

// 2. Odswiezenie listy zdarzen w tabeli
void CalendarWidget::refreshEventsList()
{
    const auto &events = m_engine->getEvents();
    m_table->setRowCount(events.size());

    for (int i = 0; i < events.size(); ++i) {
        const auto &ev = events[i];
        m_table->setItem(i, 0, new QTableWidgetItem(ev.title));
        m_table->setItem(i, 1, new QTableWidgetItem(ev.category));
        m_table->setItem(i, 2, new QTableWidgetItem(ev.start.toString("yyyy-MM-dd hh:mm")));
        m_table->setItem(i, 3, new QTableWidgetItem(ev.end.toString("yyyy-MM-dd hh:mm")));
    }
}

// 3. Budowa prostego interfejsu
void CalendarWidget::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    QStringList headers;
    headers << "Nazwa" << "Kategoria" << "Początek" << "Koniec";
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    mainLayout->addWidget(m_table);

    QHBoxLayout *formLayout = new QHBoxLayout();
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText("Nazwa wydarzenia...");
    formLayout->addWidget(m_titleEdit);

    m_categoryBox = new QComboBox(this);
    m_categoryBox->addItem("Profile blokowania");
    m_categoryBox->addItem("Automatyczne");
    m_categoryBox->addItem("Event");
    formLayout->addWidget(m_categoryBox);

    m_startEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_startEdit->setCalendarPopup(true);
    formLayout->addWidget(m_startEdit);

    m_endEdit = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(7200), this);
    m_endEdit->setCalendarPopup(true);
    formLayout->addWidget(m_endEdit);

    m_addButton = new QPushButton("Dodaj do kalendarza", this);
    formLayout->addWidget(m_addButton);

    mainLayout->addLayout(formLayout);

    connect(m_addButton, &QPushButton::clicked, this, &CalendarWidget::onAddEventClicked);

    refreshEventsList();
}

// 4. Konstruktor widgetu kalendarza
CalendarWidget::CalendarWidget(CalendarEngine *engine, QWidget *parent)
    : QWidget(parent)
    , m_engine(engine)
{
    setupUi();
    connect(m_engine, &CalendarEngine::eventsUpdated, this, &CalendarWidget::refreshEventsList);
}