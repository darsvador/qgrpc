#include <QCoreApplication>
#include <QNetworkReply>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QtEndian>
namespace{
const char *GrpcAcceptEncodingHeader = "grpc-accept-encoding";
const char *AcceptEncodingHeader = "accept-encoding";
const char *TEHeader = "te";
const char *GrpcStatusHeader = "grpc-status";
const char *GrpcStatusMessage = "grpc-message";
const int GrpcMessageSizeHeaderSize = 5;
}
struct Http2GrpcChannelPrivate {

    QUrl url;
    QNetworkAccessManager nm;
    QObject lambdaContext;
    QSslConfiguration sslConfig;

    QNetworkReply *post(const QString &method, const QString &service, const QByteArray &args, bool stream = false) {
        QUrl callUrl = url;
        callUrl.setPath("/" + service + "/" + method);

        qDebug() << "Service call url: " << callUrl;
        QNetworkRequest request(callUrl);
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/grpc");
        request.setRawHeader(GrpcAcceptEncodingHeader, "identity,deflate,gzip");
        request.setRawHeader(AcceptEncodingHeader, "identity,gzip");
        request.setRawHeader(TEHeader, "trailers");
        request.setSslConfiguration(sslConfig);
        //request.setDecompressedSafetyCheckThreshold(-1);
        //request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);

        request.setAttribute(QNetworkRequest::Http2DirectAttribute, true);

        QByteArray msg(GrpcMessageSizeHeaderSize, '\0');
        *reinterpret_cast<int *>(msg.data() + 1) = qToBigEndian(args.size());
        msg += args;
        qDebug() << "SEND: " << msg.size();
        //qDebug()<<"req:"<<request;

        QNetworkReply *networkReply = nm.post(request, msg);


        if (!stream) {
            QTimer::singleShot(6000, networkReply, [networkReply]() {
                Http2GrpcChannelPrivate::abortNetworkReply(networkReply);
            });
        }
        return networkReply;
    }

    static void abortNetworkReply(QNetworkReply *networkReply) {
        if (networkReply->isRunning()) {
            networkReply->abort();
        } else {
            networkReply->deleteLater();
        }
    }

    static QByteArray processReply(QNetworkReply *networkReply, QNetworkReply::NetworkError &statusCode) {
        //Check if no network error occured
        if (networkReply->error() != QNetworkReply::NoError) {
            statusCode = networkReply->error();
            return {};
        }

        //Check if server answer with error
        auto errCode = networkReply->rawHeader(GrpcStatusHeader).toInt();
        if (errCode != 0) {
            statusCode = QNetworkReply::NetworkError::ProtocolUnknownError;
            return {};
        }
        statusCode = QNetworkReply::NetworkError::NoError;

        //Message size doesn't matter for now
        return networkReply->readAll().mid(GrpcMessageSizeHeaderSize);
    }

    Http2GrpcChannelPrivate(const QUrl &_url)
        : url(_url)
    {
        if (url.scheme().isEmpty()) {
            url.setScheme("http");
        }
    }

    static int getExpectedDataSize(const QByteArray &container) {
        return qFromBigEndian(*reinterpret_cast<const int *>(container.data() + 1)) + GrpcMessageSizeHeaderSize;
    }

    void call(const QString &method, const QString &service, const QByteArray &args, QByteArray &ret)
    {
        QEventLoop loop;

        QNetworkReply *networkReply = post(method, service, args);
        QObject::connect(networkReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

        if (!networkReply->isFinished()) {
            loop.exec();
        }

        auto grpcStatus = QNetworkReply::NetworkError::ProtocolUnknownError;
        ret = processReply(networkReply, grpcStatus);

        networkReply->deleteLater();
        qDebug() << __func__ << "RECV: " << ret.toHex() << "grpcStatus" << grpcStatus;
        return;
    }

};


int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    QTimer timer;
    Http2GrpcChannelPrivate p(QUrl{"http://127.0.0.1:15480"});
    QObject::connect(&timer, &QTimer::timeout, [&p](){
        QByteArray arr;
        QByteArray args("\n'inbound>>>socks-in-1>>>traffic>>>uplink");
        p.call("GetStats","v2ray.core.app.stats.command.StatsService",args,arr);

    });
    timer.start(1000);
    return a.exec();
}
