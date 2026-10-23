// Plik implementacji minigry w biale puzzle WhitePuzzleWidget.
// Obsluguje siatki 6x6, 8x8 oraz 10x10 z mozliwoscia sprzetowego zablokowania wyboru comboboxa,
// wygaszanie dzwieku systemowego Windows, rysowanie Ghost Outlines i detekcje blednych polaczen.
// Funkcje ulozone od najmniej abstrakcyjnych u gory do najbardziej abstrakcyjnych na dole.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "whitepuzzlewidget.h"
#include <QPainter>
#include <QMouseEvent>
#include <QRandomGenerator>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QEvent>
#include <cmath>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

static const GUID CLSID_MMDeviceEnumerator_Local = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
static const GUID IID_IMMDeviceEnumerator_Local  = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
static const GUID IID_IAudioEndpointVolume_Local = { 0x5BC69E87, 0x3029, 0x476F, { 0x80, 0xF2, 0x01, 0x77, 0xD2, 0x1D, 0xDD, 0x30 } };
#endif

// 1. Zapisanie stanu glosnosci i wyciszenie systemu Windows
static bool captureAndMuteSystemAudio(float &outSavedVolume, int &outSavedMute)
{
#ifdef _WIN32
    HRESULT hrCo = CoInitialize(nullptr);
    IMMDeviceEnumerator *deviceEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator_Local,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator_Local,
        reinterpret_cast<void**>(&deviceEnumerator)
    );

    bool captured = false;
    if (SUCCEEDED(hr) && deviceEnumerator) {
        IMMDevice *defaultDevice = nullptr;
        hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice);
        if (SUCCEEDED(hr) && defaultDevice) {
            IAudioEndpointVolume *endpointVolume = nullptr;
            hr = defaultDevice->Activate(
                IID_IAudioEndpointVolume_Local,
                CLSCTX_INPROC_SERVER,
                nullptr,
                reinterpret_cast<void**>(&endpointVolume)
            );
            if (SUCCEEDED(hr) && endpointVolume) {
                float vol = 1.0f;
                BOOL mute = FALSE;
                if (SUCCEEDED(endpointVolume->GetMasterVolumeLevelScalar(&vol))) {
                    outSavedVolume = vol;
                }
                if (SUCCEEDED(endpointVolume->GetMute(&mute))) {
                    outSavedMute = mute ? 1 : 0;
                }
                endpointVolume->SetMasterVolumeLevelScalar(0.0f, nullptr);
                endpointVolume->SetMute(TRUE, nullptr);
                endpointVolume->Release();
                captured = true;
            }
            defaultDevice->Release();
        }
        deviceEnumerator->Release();
    }

    waveOutSetVolume(nullptr, 0);

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
    return captured;
#else
    Q_UNUSED(outSavedVolume);
    Q_UNUSED(outSavedMute);
    return false;
#endif
}

// 2. Przywrocenie zapamietanego stanu dzwieku Windows
static void revertSystemAudio(float savedVolume, int savedMute)
{
#ifdef _WIN32
    HRESULT hrCo = CoInitialize(nullptr);
    IMMDeviceEnumerator *deviceEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator_Local,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator_Local,
        reinterpret_cast<void**>(&deviceEnumerator)
    );

    if (SUCCEEDED(hr) && deviceEnumerator) {
        IMMDevice *defaultDevice = nullptr;
        hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice);
        if (SUCCEEDED(hr) && defaultDevice) {
            IAudioEndpointVolume *endpointVolume = nullptr;
            hr = defaultDevice->Activate(
                IID_IAudioEndpointVolume_Local,
                CLSCTX_INPROC_SERVER,
                nullptr,
                reinterpret_cast<void**>(&endpointVolume)
            );
            if (SUCCEEDED(hr) && endpointVolume) {
                endpointVolume->SetMasterVolumeLevelScalar(savedVolume, nullptr);
                endpointVolume->SetMute(savedMute ? TRUE : FALSE, nullptr);
                endpointVolume->Release();
            }
            defaultDevice->Release();
        }
        deviceEnumerator->Release();
    }

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
#else
    Q_UNUSED(savedVolume);
    Q_UNUSED(savedMute);
#endif
}

// 3. Egzekwowanie wyciszenia podczas trwania minigry
static void enforceZeroVolume()
{
#ifdef _WIN32
    waveOutSetVolume(nullptr, 0);

    HRESULT hrCo = CoInitialize(nullptr);
    IMMDeviceEnumerator *deviceEnumerator = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator_Local,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_IMMDeviceEnumerator_Local,
        reinterpret_cast<void**>(&deviceEnumerator)
    );

    if (SUCCEEDED(hr) && deviceEnumerator) {
        IMMDevice *defaultDevice = nullptr;
        hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice);
        if (SUCCEEDED(hr) && defaultDevice) {
            IAudioEndpointVolume *endpointVolume = nullptr;
            hr = defaultDevice->Activate(
                IID_IAudioEndpointVolume_Local,
                CLSCTX_INPROC_SERVER,
                nullptr,
                reinterpret_cast<void**>(&endpointVolume)
            );
            if (SUCCEEDED(hr) && endpointVolume) {
                endpointVolume->SetMasterVolumeLevelScalar(0.0f, nullptr);
                endpointVolume->SetMute(TRUE, nullptr);
                endpointVolume->Release();
            }
            defaultDevice->Release();
        }
        deviceEnumerator->Release();
    }

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }
#endif
}

// 4. Generowanie wektora krzywej Beziera dla krawedzi
static void appendEdgeSegment(QPainterPath &path, const QPointF &start, const QPointF &end, int edgeSign)
{
    if (edgeSign == 0) {
        path.lineTo(end);
        return;
    }

    QPointF v = end - start;
    qreal length = std::sqrt(v.x() * v.x() + v.y() * v.y());
    if (length <= 0.0001) {
        path.lineTo(end);
        return;
    }

    QPointF normal(v.y() / length, -v.x() / length);
    qreal sign = static_cast<qreal>(edgeSign);

    path.lineTo(start + v * 0.35);

    QPointF p1 = start + v * 0.35 + normal * (sign * length * 0.05);
    QPointF p2 = start + v * 0.40 + normal * (sign * length * 0.22);
    QPointF p3 = start + v * 0.45 + normal * (sign * length * 0.22);
    path.cubicTo(p1, p2, p3);

    QPointF p4 = start + v * 0.50 + normal * (sign * length * 0.26);
    QPointF p5 = start + v * 0.55 + normal * (sign * length * 0.22);
    path.quadTo(p4, p5);

    QPointF p6 = start + v * 0.60 + normal * (sign * length * 0.22);
    QPointF p7 = start + v * 0.65 + normal * (sign * length * 0.05);
    QPointF p8 = start + v * 0.65;
    path.cubicTo(p6, p7, p8);

    path.lineTo(end);
}

// 5. Pobranie oczekiwanego ksztaltu krawedzi dla danego pola planszy
int WhitePuzzleWidget::getExpectedTopEdge(int r, int c) const
{
    return (r == 0) ? 0 : -m_horizEdges[r - 1][c];
}

int WhitePuzzleWidget::getExpectedBottomEdge(int r, int c) const
{
    return (r == m_gridRows - 1) ? 0 : m_horizEdges[r][c];
}

int WhitePuzzleWidget::getExpectedLeftEdge(int r, int c) const
{
    return (c == 0) ? 0 : -m_vertEdges[r][c - 1];
}

int WhitePuzzleWidget::getExpectedRightEdge(int r, int c) const
{
    return (c == m_gridCols - 1) ? 0 : m_vertEdges[r][c];
}

// 6. Rozsypanie elementow na tacce w czytelnej siatce z przesunieciem
void WhitePuzzleWidget::scramblePieces()
{
    qreal trayStartX = m_boardRect.right() + 35.0;
    qreal trayEndX = width() - m_pieceSize - 20.0;
    qreal trayStartY = 75.0;
    qreal trayEndY = height() - m_pieceSize - 20.0;

    qreal availW = std::max(100.0, trayEndX - trayStartX);
    qreal stepX = m_pieceSize * 0.95;
    int trayCols = std::max(1, static_cast<int>(availW / stepX));

    QVector<int> shuffledIndices(m_pieces.size());
    for (int i = 0; i < m_pieces.size(); ++i) shuffledIndices[i] = i;
    for (int i = shuffledIndices.size() - 1; i > 0; --i) {
        int j = QRandomGenerator::global()->bounded(i + 1);
        std::swap(shuffledIndices[i], shuffledIndices[j]);
    }

    for (int idx = 0; idx < shuffledIndices.size(); ++idx) {
        int i = shuffledIndices[idx];
        m_pieces[i].placedRow = -1;
        m_pieces[i].placedCol = -1;

        int colIdx = idx % trayCols;
        int rowIdx = idx / trayCols;

        qreal baseX = trayStartX + colIdx * stepX;
        qreal baseY = trayStartY + rowIdx * (m_pieceSize * 0.85);

        baseX = std::clamp(baseX, trayStartX, trayEndX);
        baseY = std::clamp(baseY, trayStartY, trayEndY);

        qreal jx = QRandomGenerator::global()->bounded(10) - 5;
        qreal jy = QRandomGenerator::global()->bounded(10) - 5;

        m_pieces[i].currentPos = QPointF(baseX + jx, baseY + jy);
    }
}

// 7. Generowanie klockow z ich sciezka glowna oraz 4 osobnymi krawedziami
void WhitePuzzleWidget::generatePuzzle()
{
    m_pieces.clear();
    m_pieces.reserve(m_gridRows * m_gridCols);

    m_horizEdges = QVector<QVector<int>>(m_gridRows - 1, QVector<int>(m_gridCols, 0));
    for (int r = 0; r < m_gridRows - 1; ++r) {
        for (int c = 0; c < m_gridCols; ++c) {
            m_horizEdges[r][c] = (QRandomGenerator::global()->bounded(2) == 0) ? 1 : -1;
        }
    }

    m_vertEdges = QVector<QVector<int>>(m_gridRows, QVector<int>(m_gridCols - 1, 0));
    for (int r = 0; r < m_gridRows; ++r) {
        for (int c = 0; c < m_gridCols - 1; ++c) {
            m_vertEdges[r][c] = (QRandomGenerator::global()->bounded(2) == 0) ? 1 : -1;
        }
    }

    QPointF pTL(0, 0);
    QPointF pTR(m_pieceSize, 0);
    QPointF pBR(m_pieceSize, m_pieceSize);
    QPointF pBL(0, m_pieceSize);

    for (int r = 0; r < m_gridRows; ++r) {
        for (int c = 0; c < m_gridCols; ++c) {
            PuzzlePiece piece;
            piece.targetRow = r;
            piece.targetCol = c;
            piece.placedRow = -1;
            piece.placedCol = -1;

            piece.topEdge = (r == 0) ? 0 : -m_horizEdges[r - 1][c];
            piece.bottomEdge = (r == m_gridRows - 1) ? 0 : m_horizEdges[r][c];
            piece.leftEdge = (c == 0) ? 0 : -m_vertEdges[r][c - 1];
            piece.rightEdge = (c == m_gridCols - 1) ? 0 : m_vertEdges[r][c];

            piece.topPath = QPainterPath();
            piece.topPath.moveTo(pTL);
            appendEdgeSegment(piece.topPath, pTL, pTR, piece.topEdge);

            piece.rightPath = QPainterPath();
            piece.rightPath.moveTo(pTR);
            appendEdgeSegment(piece.rightPath, pTR, pBR, piece.rightEdge);

            piece.bottomPath = QPainterPath();
            piece.bottomPath.moveTo(pBR);
            appendEdgeSegment(piece.bottomPath, pBR, pBL, piece.bottomEdge);

            piece.leftPath = QPainterPath();
            piece.leftPath.moveTo(pBL);
            appendEdgeSegment(piece.leftPath, pBL, pTL, piece.leftEdge);

            piece.shape = QPainterPath();
            piece.shape.moveTo(pTL);
            appendEdgeSegment(piece.shape, pTL, pTR, piece.topEdge);
            appendEdgeSegment(piece.shape, pTR, pBR, piece.rightEdge);
            appendEdgeSegment(piece.shape, pBR, pBL, piece.bottomEdge);
            appendEdgeSegment(piece.shape, pBL, pTL, piece.leftEdge);
            piece.shape.closeSubpath();

            m_pieces.append(piece);
        }
    }

    scramblePieces();
}

// 8. Weryfikacja calosciowego poprawnego ulozenia
void WhitePuzzleWidget::checkWinCondition()
{
    int placedCount = 0;
    bool allCorrect = true;

    for (const PuzzlePiece &piece : m_pieces) {
        if (piece.placedRow != -1 && piece.placedCol != -1) {
            placedCount++;
            if (piece.topEdge != getExpectedTopEdge(piece.placedRow, piece.placedCol) ||
                piece.bottomEdge != getExpectedBottomEdge(piece.placedRow, piece.placedCol) ||
                piece.leftEdge != getExpectedLeftEdge(piece.placedRow, piece.placedCol) ||
                piece.rightEdge != getExpectedRightEdge(piece.placedRow, piece.placedCol)) {
                allCorrect = false;
            }
        } else {
            allCorrect = false;
        }
    }

    if (placedCount == m_pieces.size() && allCorrect) {
        m_isCompleted = true;
        m_soundMuteTimer->stop();
        restoreSystemAudio();
        m_statusLabel->setText("Gratulacje! Wszystkie puzzle ułożone bezbłędnie!");
        m_statusLabel->setStyleSheet("color: #1b5e20; font-weight: bold;");
        emit gameWon();
        emit challengeWon();
    } else if (placedCount == m_pieces.size()) {
        m_statusLabel->setText("Wszystkie elementy na planszy, ale niepasujące krawędzie zaznaczono na czerwono!");
        m_statusLabel->setStyleSheet("color: #dc1414; font-weight: bold;");
    } else {
        m_statusLabel->setText(QString("Na planszy: %1 / %2 elementów (błędne krawędzie podświetlają się na czerwono)").arg(placedCount).arg(m_pieces.size()));
        m_statusLabel->setStyleSheet("color: #37474f; font-weight: bold;");
    }
}

// 9. Rysowanie planszy, Ghost Outlines oraz klockow
void WhitePuzzleWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.fillRect(rect(), QColor(233, 236, 242));

    painter.setPen(QPen(QColor(176, 181, 194), 1.5, Qt::SolidLine));
    painter.setBrush(QColor(213, 217, 226));
    painter.drawRect(m_boardRect);

    painter.setPen(QPen(QColor(196, 201, 212), 1.0, Qt::DotLine));
    for (int r = 1; r < m_gridRows; ++r) {
        qreal y = m_boardRect.top() + r * m_pieceSize;
        painter.drawLine(QPointF(m_boardRect.left(), y), QPointF(m_boardRect.right(), y));
    }
    for (int c = 1; c < m_gridCols; ++c) {
        qreal x = m_boardRect.left() + c * m_pieceSize;
        painter.drawLine(QPointF(x, m_boardRect.top()), QPointF(x, m_boardRect.bottom()));
    }

    for (const auto &p : m_pieces) {
        QPointF slotPos(m_boardRect.left() + p.targetCol * m_pieceSize, m_boardRect.top() + p.targetRow * m_pieceSize);

        painter.save();
        painter.translate(slotPos);
        painter.setPen(QPen(QColor(100, 110, 130), 1.8, Qt::DashLine));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(p.shape);
        painter.restore();
    }

    QPen normalPiecePen(QColor(150, 156, 170), 1.2);
    QPen errorPen(QColor(220, 20, 20), 2.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QBrush pieceBrush(QColor(251, 252, 253));

    for (int i = 0; i < m_pieces.size(); ++i) {
        if (i == m_draggedIndex) {
            continue;
        }

        const auto &piece = m_pieces[i];
        painter.save();
        painter.translate(piece.currentPos);

        painter.setPen(Qt::NoPen);
        painter.setBrush(pieceBrush);
        painter.drawPath(piece.shape);

        if (piece.placedRow != -1 && piece.placedCol != -1) {
            int pr = piece.placedRow;
            int pc = piece.placedCol;

            bool topErr = (piece.topEdge != getExpectedTopEdge(pr, pc));
            bool rightErr = (piece.rightEdge != getExpectedRightEdge(pr, pc));
            bool bottomErr = (piece.bottomEdge != getExpectedBottomEdge(pr, pc));
            bool leftErr = (piece.leftEdge != getExpectedLeftEdge(pr, pc));

            painter.setPen(topErr ? errorPen : normalPiecePen);
            painter.drawPath(piece.topPath);

            painter.setPen(rightErr ? errorPen : normalPiecePen);
            painter.drawPath(piece.rightPath);

            painter.setPen(bottomErr ? errorPen : normalPiecePen);
            painter.drawPath(piece.bottomPath);

            painter.setPen(leftErr ? errorPen : normalPiecePen);
            painter.drawPath(piece.leftPath);
        } else {
            painter.setPen(normalPiecePen);
            painter.drawPath(piece.shape);
        }

        painter.restore();
    }

    if (m_draggedIndex >= 0 && m_draggedIndex < m_pieces.size()) {
        painter.save();
        painter.translate(m_pieces[m_draggedIndex].currentPos);

        QPen dragPen(QColor(82, 88, 102), 2.0);
        QBrush dragBrush(QColor(100, 150, 255, 180));
        painter.setPen(dragPen);
        painter.setBrush(dragBrush);
        painter.drawPath(m_pieces[m_draggedIndex].shape);
        painter.restore();
    }
}

// 10. Chwycenie klocka mysza
void WhitePuzzleWidget::mousePressEvent(QMouseEvent *event)
{
    if (m_isCompleted || event->button() != Qt::LeftButton) {
        return;
    }

    QPointF mousePos = event->position();
    m_draggedIndex = -1;

    for (int i = m_pieces.size() - 1; i >= 0; --i) {
        QPointF localPos = mousePos - m_pieces[i].currentPos;
        if (m_pieces[i].shape.contains(localPos)) {
            m_draggedIndex = i;
            m_dragOffset = localPos;

            m_pieces[i].placedRow = -1;
            m_pieces[i].placedCol = -1;

            PuzzlePiece selected = m_pieces.takeAt(i);
            m_pieces.append(selected);
            m_draggedIndex = m_pieces.size() - 1;

            update();
            break;
        }
    }
}

// 11. Przesuwanie klocka kursorem
void WhitePuzzleWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_draggedIndex < 0 || m_draggedIndex >= m_pieces.size()) {
        return;
    }

    m_pieces[m_draggedIndex].currentPos = event->position() - m_dragOffset;
    update();
}

// 12. Upuszczenie klocka: neutralny snap do pola
void WhitePuzzleWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_draggedIndex < 0 || m_draggedIndex >= m_pieces.size()) {
        return;
    }

    PuzzlePiece &piece = m_pieces[m_draggedIndex];
    QPointF pieceCenter = piece.currentPos + QPointF(m_pieceSize * 0.5, m_pieceSize * 0.5);

    if (m_boardRect.contains(pieceCenter)) {
        int targetC = std::clamp(static_cast<int>((pieceCenter.x() - m_boardRect.left()) / m_pieceSize), 0, m_gridCols - 1);
        int targetR = std::clamp(static_cast<int>((pieceCenter.y() - m_boardRect.top()) / m_pieceSize), 0, m_gridRows - 1);

        for (int i = 0; i < m_pieces.size(); ++i) {
            if (i != m_draggedIndex && m_pieces[i].placedRow == targetR && m_pieces[i].placedCol == targetC) {
                m_pieces[i].placedRow = -1;
                m_pieces[i].placedCol = -1;
                m_pieces[i].currentPos = QPointF(m_boardRect.right() + 35.0, m_boardRect.top() + targetR * m_pieceSize);
                break;
            }
        }

        piece.currentPos = QPointF(m_boardRect.left() + targetC * m_pieceSize, m_boardRect.top() + targetR * m_pieceSize);
        piece.placedRow = targetR;
        piece.placedCol = targetC;
    } else {
        piece.placedRow = -1;
        piece.placedCol = -1;
    }

    m_draggedIndex = -1;
    checkWinCondition();
    update();
}

// 13. Zmiana aktywnosci okna (Anti-Cheat)
void WhitePuzzleWidget::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::ActivationChange) {
        if (!isActiveWindow()) {
            handleWindowDeactivation();
        }
    }
    QWidget::changeEvent(event);
}

// 14. Utrata fokusu okna (Anti-Cheat)
void WhitePuzzleWidget::focusOutEvent(QFocusEvent *event)
{
    handleWindowDeactivation();
    QWidget::focusOutEvent(event);
}

// 15. Reakcja na klikniecie poza okno
void WhitePuzzleWidget::handleWindowDeactivation()
{
    if (m_isCompleted) {
        return;
    }

    m_statusLabel->setText("Kliknięto poza okno! Puzzle zresetowane.");
    m_statusLabel->setStyleSheet("color: #c62828; font-weight: bold;");
    emit gameLost();
    emit challengeLost();
    resetGame();
}

// 16. Bezpieczne przywrocenie pierwotnej glosnosci
void WhitePuzzleWidget::restoreSystemAudio()
{
    if (m_hasAudioState) {
        revertSystemAudio(m_savedVolume, m_savedMute);
        m_hasAudioState = false;
    }
}

// 17. Reset stanu gry
void WhitePuzzleWidget::resetGame()
{
    m_isCompleted = false;
    m_draggedIndex = -1;
    generatePuzzle();
    checkWinCondition();
    update();
}

// 18. Metody MinigameBaseWidget
void WhitePuzzleWidget::startMinigame()
{
    resetGame();
}

void WhitePuzzleWidget::resetMinigame()
{
    resetGame();
}

// 19. Konfiguracja trudnosci: 6x6, 8x8 oraz 10x10 z mozliwoscia zablokowania comboboxa
void WhitePuzzleWidget::setDifficulty(Difficulty diff, bool lockSelection)
{
    m_difficulty = diff;

    switch (m_difficulty) {
    case Difficulty::Grid6x6:
        m_gridRows = 6;
        m_gridCols = 6;
        m_pieceSize = 85.0;
        break;
    case Difficulty::Grid8x8:
        m_gridRows = 8;
        m_gridCols = 8;
        m_pieceSize = 65.0;
        break;
    case Difficulty::Grid10x10:
        m_gridRows = 10;
        m_gridCols = 10;
        m_pieceSize = 52.0;
        break;
    }

    m_boardRect = QRectF(30.0, 75.0, m_gridCols * m_pieceSize, m_gridRows * m_pieceSize);

    if (m_difficultyCombo) {
        int idx = m_difficultyCombo->findData(static_cast<int>(diff));
        if (idx >= 0) {
            m_difficultyCombo->blockSignals(true);
            m_difficultyCombo->setCurrentIndex(idx);
            m_difficultyCombo->blockSignals(false);
        }
        if (lockSelection) {
            m_difficultyCombo->setEnabled(false);
        }
    }

    resetGame();
}

// 20. Destruktor
WhitePuzzleWidget::~WhitePuzzleWidget()
{
    m_soundMuteTimer->stop();
    restoreSystemAudio();
}

// 21. Konstruktor WhitePuzzleWidget (domyslnie 8x8)
WhitePuzzleWidget::WhitePuzzleWidget(QWidget *parent)
    : MinigameBaseWidget(parent)
    , m_difficulty(Difficulty::Grid8x8)
    , m_gridRows(8)
    , m_gridCols(8)
    , m_pieceSize(65.0)
    , m_boardRect(30.0, 75.0, 520.0, 520.0)
    , m_draggedIndex(-1)
    , m_soundMuteTimer(new QTimer(this))
    , m_isCompleted(false)
    , m_savedVolume(1.0f)
    , m_savedMute(0)
    , m_hasAudioState(false)
{
    setMinimumSize(1260, 780);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_KeyCompression, false);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(12, 12, 12, 12);

    QHBoxLayout *topControlsLayout = new QHBoxLayout();

    QLabel *diffLabel = new QLabel("Rozmiar siatki:", this);
    diffLabel->setStyleSheet("color: #2d3139; font-weight: bold;");

    m_difficultyCombo = new QComboBox(this);
    m_difficultyCombo->addItem("6x6 (36 puzzli)", static_cast<int>(Difficulty::Grid6x6));
    m_difficultyCombo->addItem("8x8 (64 puzzle)", static_cast<int>(Difficulty::Grid8x8));
    m_difficultyCombo->addItem("10x10 (100 puzzli)", static_cast<int>(Difficulty::Grid10x10));

    m_resetButton = new QPushButton("Rozsyp ponownie", this);

    m_statusLabel = new QLabel("Dopasuj elementy do obrysów na planszy. Błędne krawędzie podświetlają się na czerwono!", this);
    m_statusLabel->setStyleSheet("color: #37474f; font-weight: bold;");

    topControlsLayout->addWidget(diffLabel);
    topControlsLayout->addWidget(m_difficultyCombo);
    topControlsLayout->addWidget(m_resetButton);
    topControlsLayout->addSpacing(20);
    topControlsLayout->addWidget(m_statusLabel);
    topControlsLayout->addStretch();

    mainLayout->addLayout(topControlsLayout);
    mainLayout->addStretch();

    connect(m_difficultyCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        setDifficulty(static_cast<Difficulty>(m_difficultyCombo->itemData(index).toInt()), false);
    });

    connect(m_resetButton, &QPushButton::clicked, this, &WhitePuzzleWidget::resetGame);

    m_hasAudioState = captureAndMuteSystemAudio(m_savedVolume, m_savedMute);

    connect(m_soundMuteTimer, &QTimer::timeout, this, &enforceZeroVolume);
    m_soundMuteTimer->start(200);

    m_difficultyCombo->setCurrentIndex(1);
    setDifficulty(Difficulty::Grid8x8, false);
}
