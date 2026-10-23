// Plik naglowkowy okna FixExtensionsDialog.
// Posiada automatyczne odswiezanie czasu rzeczywistego (QTimer 1s),
// prezentuje stan procesow Windows, stan wtyczki oraz umozliwia 1-kliknieciem
// otwarcie katalogu wtyczki w Eksploratorze.
// Laczy sie z BrowserEnforcer i MainWindow.

#ifndef FIX_EXTENSIONS_DIALOG_H
#define FIX_EXTENSIONS_DIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include "browserenforcer.h"

class FixExtensionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FixExtensionsDialog(BrowserEnforcer *enforcer, QWidget *parent = nullptr);
    ~FixExtensionsDialog() override = default;

public slots:
    void refreshStatus();

private slots:
    void onOpenFolderClicked();

private:
    void setupUi();

    BrowserEnforcer *m_enforcer;
    QTableWidget *m_table;
    QLabel *m_summaryLabel;
    QTimer *m_liveTimer;
};

#endif // FIX_EXTENSIONS_DIALOG_H