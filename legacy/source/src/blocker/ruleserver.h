// Plik nagłówkowy modułu RuleServer.
// Nasłuchuje na porcie 127.0.0.1:13370, udostępniając reguły dla wtyczki oraz rejestrując
// sygnały obecności (heartbeat) z przeglądarek. Łączy się w main.cpp.

#ifndef RULESERVER_H
#define RULESERVER_H

#include <QObject>
#include <QStringList>

class QTcpServer;

class RuleServer : public QObject
{
    Q_OBJECT

public:
    explicit RuleServer(QObject *parent = nullptr);
    ~RuleServer() override = default;

public slots:
    bool startServer(quint16 port = 13370);
    void stopServer();
    void setConfiguration(bool active, const QStringList &exceptions);

signals:
    void heartbeatReceived(const QString &browserName);

private slots:
    void onNewConnection();

private:
    QTcpServer *m_server;
    bool m_blockingActive;
    QStringList m_exceptions;
};

#endif // RULESERVER_H