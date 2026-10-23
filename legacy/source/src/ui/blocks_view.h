// Plik naglowkowy glownego widoku WEBSITE & APP BLOCKS.
// Zarzadza lista blokow, dodawaniem nowych profili i wywolywaniem okien edycji oraz weryfikacji.
// Posiada referencje do ProfileManager i CalendarEngine, przekazywane dalej do wierszy BlockRowWidget.
// Laczy sie z ProfileManager, CalendarEngine, BlockRowWidget, UnlockDialog, EditBlockDialog i LockTypeDialog.

#ifndef BLOCKS_VIEW_H
#define BLOCKS_VIEW_H

#include <QWidget>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include "profile_manager.h"
#include "calendar_engine.h"
#include "block_row_widget.h"

class BlocksView : public QWidget
{
    Q_OBJECT

public:
    explicit BlocksView(ProfileManager *profileManager, CalendarEngine *calendarEngine, QWidget *parent = nullptr);
    ~BlocksView() override = default;

public slots:
    void refreshBlockList();

private slots:
    void onNewBlockClicked();
    void onToggleRequested(const QString &profileName);
    void onEditRequested(const QString &profileName);
    void onLockConfigRequested(const QString &profileName);
    void onBreakRequested(const QString &profileName);

private:
    void setupUi();

    ProfileManager *m_profileManager;
    CalendarEngine *m_calendarEngine;
    QVBoxLayout *m_cardsContainerLayout;
    QPushButton *m_newBlockButton;
    QPushButton *m_bottomNewBlockButton;
};

#endif // BLOCKS_VIEW_H
