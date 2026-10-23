// Plik implementacji serwera reguł RuleServer.
// Nasłuchuje na porcie 13370 (AnyIPv4), odczytuje nazwę przeglądarki z parametru HTTP oraz
// awaryjnie z nagłówka User-Agent (gwarantuje niezawodny odbiór heartbeat).
// Funkcje ułożone od najmniej abstrakcyjnych u góry do najbardziej abstrakcyjnych na dole.

#include "ruleserver.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>

// 1. Formatowanie odpowiedzi HTTP z naglowkami CORS
static QByteArray formatHttpResponse(int statusCode, const QByteArray &contentType, const QByteArray &body)
{
    QByteArray response;
    response += QString("HTTP/1.1 %1 OK\r\n").arg(statusCode).toUtf8();
    response += "Access-Control-Allow-Origin: *\r\n";
    response += "Access-Control-Allow-Methods: GET, OPTIONS\r\n";
    response += "Access-Control-Allow-Headers: *\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

// 2. Niezawodna ekstrakcja parametru przegladarki i emisja sygnalu heartbeat
static void processIncomingData(QTcpSocket *socket, bool blockingActive, const QStringList &exceptions, RuleServer *serverInstance)
{
    QByteArray requestData = socket->readAll();
    QString requestStr = QString::fromUtf8(requestData);

    if (requestStr.startsWith("OPTIONS")) {
        socket->write(formatHttpResponse(200, "text/plain", ""));
        socket->disconnectFromHost();
        return;
    }

    // Podwójna detekcja: z parametru URL lub bezposrednio z User-Agent
    QString browserName;
    QRegularExpression rx("browser=([a-zA-Z0-9_%+-]+)");
    QRegularExpressionMatch match = rx.match(requestStr);
    if (match.hasMatch()) {
        browserName = match.captured(1);
    } else {
        if (requestStr.contains("Edg/", Qt::CaseInsensitive)) browserName = "Edge";
        else if (requestStr.contains("OPR/", Qt::CaseInsensitive) || requestStr.contains("Opera", Qt::CaseInsensitive)) browserName = "Opera";
        else if (requestStr.contains("Brave", Qt::CaseInsensitive)) browserName = "Brave";
        else if (requestStr.contains("Chrome/", Qt::CaseInsensitive)) browserName = "Chrome";
    }

    if (!browserName.isEmpty() && serverInstance) {
        emit serverInstance->heartbeatReceived(browserName);
    }

    QJsonObject root;
    root["blockingActive"] = blockingActive;

    QJsonArray exArray;
    for (const QString &ex : exceptions) {
        if (!ex.trimmed().isEmpty()) {
            exArray.append(ex.trimmed());
        }
    }
    root["exceptions"] = exArray;

    QByteArray jsonResponse = QJsonDocument(root).toJson(QJsonDocument::Compact);
    socket->write(formatHttpResponse(200, "application/json", jsonResponse));
    socket->disconnectFromHost();
}

// 3. Nowe polaczenie TCP
void RuleServer::onNewConnection()
{
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
            processIncomingData(socket, m_blockingActive, m_exceptions, this);
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

// 4. Konfiguracja
void RuleServer::setConfiguration(bool active, const QStringList &exceptions)
{
    m_blockingActive = active;
    m_exceptions = exceptions;
}

// 5. Zatrzymanie serwera
void RuleServer::stopServer()
{
    if (m_server->isListening()) m_server->close();
}

// 6. Uruchomienie serwera na AnyIPv4 (odbiera zarowno 127.0.0.1 jak i localhost)
bool RuleServer::startServer(quint16 port)
{
    if (m_server->isListening()) {
        m_server->close();
    }
    bool ok = m_server->listen(QHostAddress::AnyIPv4, port);
    if (!ok) {
        ok = m_server->listen(QHostAddress::LocalHost, port);
    }
    return ok;
}

// 7. Konstruktor
RuleServer::RuleServer(QObject *parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_blockingActive(false)
{
    connect(m_server, &QTcpServer::newConnection, this, &RuleServer::onNewConnection);
}
