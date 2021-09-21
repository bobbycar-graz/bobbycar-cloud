#include <QCoreApplication>
#include <QSettings>
#include <QNetworkAccessManager>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QNetworkReply>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    QSettings settings{"bobbycar-cloud.ini", QSettings::IniFormat};

    QNetworkRequest request{QUrl{settings.value("url").toString()}};
    request.setRawHeader(QByteArray{"Authorization"}, QString{"Token %0"}.arg(settings.value("token").toString()).toUtf8());
    request.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain; charset=utf-8");
    request.setRawHeader(QByteArray{"Accept"}, QByteArray{"application/json"});

    QNetworkAccessManager manager;

    QWebSocketServer server{"bobbycar-cloud", QWebSocketServer::NonSecureMode};

    QObject::connect(&server, &QWebSocketServer::newConnection,
                     [&](){
        QWebSocket *client = server.nextPendingConnection();
        if (!client)
            return;

        auto clientId = client->requestUrl().path();
        if (clientId.startsWith('/'))
            clientId.remove(0, 1);

        qInfo() << "new connection from" << client->peerAddress() << clientId;

        QObject::connect(client, &QWebSocket::textMessageReceived, [client, clientId, &manager, &request](const QString &message){
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError)
            {
                qWarning() << client->peerAddress() << client->requestUrl().path() << "could not parse json" << error.errorString();
                qDebug() << message;
                client->close(QWebSocketProtocol::CloseCodeBadOperation, QString{"could not parse json: %0"}.arg(error.errorString()));
                return;
            }

            if (!doc.isArray())
            {
                qWarning() << client->peerAddress() << client->requestUrl().path() << "json is not an array";
                qDebug() << message;
                client->close(QWebSocketProtocol::CloseCodeBadOperation, "json is not an array");
                return;
            }

            QString data;
            int i{};
            for (const QJsonValue &recordVal : doc.array())
            {
                const auto recordIndex = i++;

                if (!recordVal.isArray())
                {
                    qWarning() << client->peerAddress() << client->requestUrl().path() << "json record" << recordIndex << "is not an array";
                    qDebug() << message;
                    client->close(QWebSocketProtocol::CloseCodeBadOperation, QString{"json record %0 is not an array"}.arg(recordIndex));
                    return;
                }

                const auto &record = recordVal.toArray();

                if (record[0].isNull())
                {
                    qWarning() << client->peerAddress() << client->requestUrl().path() << "json record" << recordIndex << "has invalid uptime";
                    qDebug() << message;
                    client->close(QWebSocketProtocol::CloseCodeBadOperation, QString{"json record %0 has invalid uptime"}.arg(recordIndex));
                    return;
                }

                if (record[1].isNull())
                {
                    qWarning() << client->peerAddress() << client->requestUrl().path() << "json record" << recordIndex << "has invalid utc time";
                    qDebug() << message;
                    client->close(QWebSocketProtocol::CloseCodeBadOperation, QString{"json record %0 has invalid utc time"}.arg(recordIndex));
                    return;
                }

                if (record[2].isNull())
                {
                    qWarning() << client->peerAddress() << client->requestUrl().path() << "json record" << recordIndex << "has invalid freememory8";
                    qDebug() << message;
                    client->close(QWebSocketProtocol::CloseCodeBadOperation, QString{"json record %0 has invalid freememory8"}.arg(recordIndex));
                    return;
                }

                const auto utc = QDateTime::fromMSecsSinceEpoch(record[1].toDouble());

                data += QString{"system,host=%0 uptime=%1,freememory8=%2"}
                        .arg(clientId)
                        .arg(record[0].toDouble())
                        .arg(record[2].toInt());

                if (!record[3].isNull())
                    data += QString{",rssi=%0"}.arg(record[3].toInt());

                data += QString{" %0\n"}.arg(utc.toMSecsSinceEpoch());

                if (!record[4].isNull() || !record[5].isNull())
                {
                    data += QString{"inputs,host=%0,type=potis,kind=raw %1%2%3 %4\n"}
                            .arg(clientId)
                            .arg(!record[4].isNull() ? QString{"gas=%0"}.arg(record[4].toInt()) : "")
                            .arg(!record[4].isNull() && !record[5].isNull() ? "," : "")
                            .arg(!record[5].isNull() ? QString{"brems=%0"}.arg(record[5].toInt()) : "")
                        .arg(utc.toMSecsSinceEpoch());
                }

                if (!record[6].isNull() || !record[7].isNull())
                {
                    data += QString{"inputs,host=%0,type=potis,kind=processed %1%2%3 %4\n"}
                            .arg(clientId)
                            .arg(!record[6].isNull() ? QString{"gas=%0"}.arg(record[6].toDouble()) : "")
                            .arg(!record[6].isNull() && !record[7].isNull() ? "," : "")
                            .arg(!record[7].isNull() ? QString{"brems=%0"}.arg(record[7].toDouble()) : "")
                        .arg(utc.toMSecsSinceEpoch());
                }

                const auto addController = [&](const QJsonArray &controller, const QString &board){
                    data += QString{"measure,host=%0,board=%1 voltage=%2,temperature=%3 %4\n"}
                            .arg(clientId)
                            .arg(board)
                            .arg(controller[0].toDouble())
                            .arg(controller[1].toDouble())
                            .arg(utc.toMSecsSinceEpoch());

                    const auto addMotor = [&](const QJsonArray &motor, const QString &board, const QString &side){
                        data += QString{"command,host=%0,board=%1,side=%2 inputTgt=%3 %4\n"}
                                .arg(clientId)
                                .arg(board)
                                .arg(side)
                                .arg(motor[0].toInt())
                                .arg(utc.toMSecsSinceEpoch());
                        data += QString{"measure,host=%0,board=%1,side=%2 speed=%3,current=%4,error=%5 %6\n"}
                                .arg(clientId)
                                .arg(board)
                                .arg(side)
                                .arg(motor[1].toDouble())
                                .arg(motor[2].toDouble())
                                .arg(motor[3].toInt())
                                .arg(utc.toMSecsSinceEpoch());
                    };

                    addMotor(controller[2].toArray(), board, "left");
                    addMotor(controller[3].toArray(), board, "right");
                };

                if (!record[8].isNull())
                    addController(record[8].toArray(), "front");

                if (!record[9].isNull())
                    addController(record[9].toArray(), "back");
            }

            if (data.isEmpty())
            {
                qWarning() << "received empty from" << client->peerAddress() << clientId;
                qDebug() << message;
                return;
            }

            QNetworkReply *reply = manager.post(request, data.toUtf8());
            QObject::connect(reply, &QNetworkReply::finished, reply, [client, clientId, reply](){
                if (reply->error() == QNetworkReply::NoError)
                    qInfo() << "saved successfully" << client->peerAddress() << clientId;
                else
                {
                    qWarning() << "error while saving" << reply->error() << reply->errorString();
                    qDebug() << reply->readAll();
                }
                reply->deleteLater();
            });
        });

        QObject::connect(client, &QWebSocket::disconnected, [](){
            qInfo() << "disconnected";
        });
    });

    quint16 port = settings.value("port").toInt();
    if (!server.listen(QHostAddress::Any, port))
        qFatal("could not start listening %s", qPrintable(server.errorString()));

    qDebug() << "listening on port" << port;

    return app.exec();
}
