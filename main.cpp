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

        qDebug() << "new connection from" << client->peerAddress() << clientId;

        QObject::connect(client, &QWebSocket::textMessageReceived, [client, clientId, &manager, &request](const QString &message){
            qDebug() << "received" << client->peerAddress() << clientId /*<< message*/;

            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8(), &error);
            if (error.error != QJsonParseError::NoError)
            {
                qWarning() << client->peerAddress() << client->requestUrl().path() << "could not parse json" << error.errorString();
                return;
            }

            if (!doc.isArray())
            {
                qWarning() << client->peerAddress() << client->requestUrl().path() << "json is not an array";
                return;
            }

            auto docArr = doc.array();

            const qint64 uptime = docArr[0].toDouble();
            const auto utc = QDateTime::fromMSecsSinceEpoch(docArr[1].toDouble());
            const auto freememory8 = docArr[2].toInt();

            QString data;
            data += QString{"system,host=%0 uptime=%1,freememory8=%2"}
                    .arg(clientId)
                    .arg(uptime)
                    .arg(freememory8);

            if (!docArr[3].isNull())
                data += QString{",rssi=%0"}.arg(docArr[3].toInt());

            data += QString{" %0\n"}.arg(utc.toMSecsSinceEpoch());

            if (!docArr[4].isNull() || !docArr[5].isNull())
            {
                data += QString{"inputs,host=%0,type=potis,kind=raw %1%2%3 %4\n"}
                        .arg(clientId)
                        .arg(!docArr[4].isNull() ? QString{"gas=%0"}.arg(docArr[4].toInt()) : "")
                        .arg(!docArr[4].isNull() && !docArr[5].isNull() ? "," : "")
                        .arg(!docArr[5].isNull() ? QString{"brems=%0"}.arg(docArr[5].toInt()) : "")
                    .arg(utc.toMSecsSinceEpoch());
            }

            if (!docArr[6].isNull() || !docArr[7].isNull())
            {
                data += QString{"inputs,host=%0,type=potis,kind=processed %1%2%3 %4\n"}
                        .arg(clientId)
                        .arg(!docArr[6].isNull() ? QString{"gas=%0"}.arg(docArr[6].toDouble()) : "")
                        .arg(!docArr[6].isNull() && !docArr[7].isNull() ? "," : "")
                        .arg(!docArr[7].isNull() ? QString{"brems=%0"}.arg(docArr[7].toDouble()) : "")
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

            if (!docArr[8].isNull())
                addController(docArr[8].toArray(), "front");

            if (!docArr[9].isNull())
                addController(docArr[9].toArray(), "back");

            QNetworkReply *reply = manager.post(request, data.toUtf8());
            QObject::connect(reply, &QNetworkReply::finished, reply, [reply](){
                if (reply->error() != QNetworkReply::NoError)
                    qWarning() << "request finished" << reply->error() << reply->errorString();
                //qDebug() << reply->readAll();
                reply->deleteLater();
            });
        });

        QObject::connect(client, &QWebSocket::disconnected, [](){
            qDebug() << "disconnected";
        });
    });

    quint16 port = settings.value("port").toInt();
    if (!server.listen(QHostAddress::Any, port))
        qFatal("could not start listening %s", qPrintable(server.errorString()));

    qDebug() << "listening on port" << port;

    return app.exec();
}
