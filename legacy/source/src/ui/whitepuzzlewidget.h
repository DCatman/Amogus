// Plik naglowkowy minigry WhitePuzzleWidget.
// Skonfigurowany pod rozmiar 8x8 jako glowny poziom oraz 10x10 do zabezpieczenia Debug Mode,
// zawiera mechanizm Ghost Outlines, blokade zmiany trudnosci (lockSelection) oraz czerwone podswietlanie bledow.
// Laczy sie z MinigameUnlockDialog, MainWindow oraz Windows Core Audio API.

#ifndef WHITEPUZZLEWIDGET_H
#define WHITEPUZZLEWIDGET_H

#include "minigame_unlock_dialog.h"
#include <QVector>
#include <QPointF>
#include <QPainterPath>
#include <QTimer>

#ifdef _WIN32
#include <windows.h>
#endif

class QComboBox;
class QPushButton;
class QLabel;

class WhitePuzzleWidget : public MinigameBaseWidget
{
    Q_OBJECT

public:
    enum class Difficulty {
        Grid6x6,  // 6x6 (36 puzzli)
        Grid8x8,  // 8x8 (64 puzzle - glowny)
        Grid10x10 // 10x10 (100 puzzli)
    };

    explicit WhitePuzzleWidget(QWidget *parent = nullptr);
    ~WhitePuzzleWidget() override;

    void setDifficulty(Difficulty diff, bool lockSelection = false);
    void resetGame();

    void startMinigame() override;
    void resetMinigame() override;

signals:
    void gameWon();
    void gameLost();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    struct PuzzlePiece {
        int targetRow;
        int targetCol;
        int placedRow;
        int placedCol;
        int topEdge;
        int rightEdge;
        int bottomEdge;
        int leftEdge;
        QPainterPath shape;
        QPainterPath topPath;
        QPainterPath rightPath;
        QPainterPath bottomPath;
        QPainterPath leftPath;
        QPointF currentPos;
    };

    Difficulty m_difficulty;
    int m_gridRows;
    int m_gridCols;
    qreal m_pieceSize;
    QRectF m_boardRect;

    QVector<QVector<int>> m_horizEdges;
    QVector<QVector<int>> m_vertEdges;

    QVector<PuzzlePiece> m_pieces;
    int m_draggedIndex;
    QPointF m_dragOffset;

    QTimer *m_soundMuteTimer;
    bool m_isCompleted;

    float m_savedVolume;
    int m_savedMute;
    bool m_hasAudioState;

    QComboBox *m_difficultyCombo;
    QPushButton *m_resetButton;
    QLabel *m_statusLabel;

    int getExpectedTopEdge(int r, int c) const;
    int getExpectedBottomEdge(int r, int c) const;
    int getExpectedLeftEdge(int r, int c) const;
    int getExpectedRightEdge(int r, int c) const;

    void generatePuzzle();
    void scramblePieces();
    void checkWinCondition();
    void handleWindowDeactivation();
    void restoreSystemAudio();
};

#endif // WHITEPUZZLEWIDGET_H
