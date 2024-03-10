#include "widget.h"
#include "ui_widget.h"
#include "util.h"
#include <QClipboard>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QBuffer>
#include <QRcode/QRUtil.h>
#include <QUuid>
#include <QTimer>
#include <QSystemTrayIcon>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    QSystemTrayIcon *sysTray = new QSystemTrayIcon(QIcon(":/img/clipboard.ico"));
    sysTray->setToolTip("Clipboard-Cloud");
    sysTray->show();


    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QImage qrImage = QRUtil::encodeText(id);
    ui->label_qr->setPixmap(QPixmap::fromImage(qrImage));
    ui->label_qr->adjustSize();
    ui->label_qr->setToolTip(id);

    static QString clipId = "mrbeanc"; //改为从文件读取
    static QString ip = "124.220.81.213"; //https
    // static QString ip = "localhost";
    static QNetworkAccessManager manager;
    static bool isMeSetClipboard = false;

    connect(qApp->clipboard(), &QClipboard::dataChanged, this, [=](){
        if (isMeSetClipboard) { // 避免检测到自身对剪切板的修改
            isMeSetClipboard = false;
            return;
        }

        const QMimeData* clipData = qApp->clipboard()->mimeData();
        QByteArray data;
        if (clipData->hasImage()) { // to base64
            // qDebug() << clipData->formats(); // application/x-qt-image
            QImage image = qvariant_cast<QImage>(clipData->imageData()); //不能直接toByteArray，需要先转换为QImage，否则为空
            if (!image.isNull()) {
                QBuffer buffer(&data);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "jpg"); // 将 QImage 保存为 jpg 格式
                buffer.close();
                data = data.toBase64(); // 转换为 Base64 编码，防止老式设备进行隐式编解码导致信息丢失
            }
        } else if (clipData->hasText()) { // TODO
            data = clipData->text().toUtf8();
        } else {
            qDebug() << "WARN: This Type is not supported NOW.";
            sysTray->showMessage("WARN", "This Type is not supported NOW.");
            return;
        }

        if (data.size() > 1024 * 1024 * 2) { // 2MB
            qDebug() << "WARN: Data too large, ignore.";
            sysTray->showMessage("WARN", "Data too large, ignore.");
            return;
        }

        QNetworkRequest request(QUrl(QString("https://%1/clipboard/%2/win").arg(ip, clipId)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject jsonData;
        jsonData.insert("data", QString::fromUtf8(data));
        jsonData.insert("isText", !clipData->hasImage()); //有可能同时hasText，所以以image为准

        QJsonDocument doc(jsonData);
        QByteArray postData = doc.toJson();

        QNetworkReply *reply = manager.post(request, postData);

        QObject::connect(reply, &QNetworkReply::finished, [=]() {
            if (reply->error() == QNetworkReply::NoError) {
                qDebug() << "Copied to Cloud. OK." << Util::printDataSize(data.size());
            } else {
                qDebug() << "Post Error:" << reply->errorString();
            }
            reply->deleteLater();
        });
    });

    static std::function<void(void)> pollCloudClip = [=](){
        QNetworkRequest request(QUrl(QString("https://%1/clipboard/long-polling/%2/win").arg(ip, clipId)));
        // 可以加入心跳机制确保更快重连（丢弃失败的连接），毕竟90s还是太长
        // 不过等我遇到问题再加吧hh 应该是小概率事件，相信HTTP！
        request.setTransferTimeout(90 * 1000); // 90s超时时间，避免服务端掉线 & 网络异常造成的无响应永久等待
        QNetworkReply *reply = manager.get(request);
        qDebug() << "Start long polling...";

        connect(reply, &QNetworkReply::finished, this, [=]() {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qDebug() << "Long polling done." << statusCode;
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray replyData = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(replyData);
                QJsonObject jsonData = doc.object();

                const QString os = jsonData.value("os").toString();
                const QString data = jsonData.value("data").toString();
                const bool isText = jsonData.value("isText").toBool();

                if (os == "ios" && !data.isEmpty()) {
                    isMeSetClipboard = true;
                    if (isText) {
                        qApp->clipboard()->setText(data);
                    } else {
                        qApp->clipboard()->setImage(QImage::fromData(QByteArray::fromBase64(data.toUtf8())));
                    }
                    sysTray->showMessage("Pasted from IOS", isText ? data : "[Image]"); //可以在 系统-通知 中关闭声音
                    qDebug() << "Pasted from IOS;" << Util::printDataSize(data.toUtf8().size());
                }

                if (statusCode == 200 && data.isEmpty()) { //有时出现 200、NoError，但是无数据的情况 Why！！
                    qDebug() << "WTF! 200 but no data";
                    qDebug() << jsonData;
                    qDebug() << replyData;
                    qDebug() << reply->errorString();
                }
            } else {
                qDebug() << "Get Error:" << reply->errorString();
            }
            reply->deleteLater();

            QTimer::singleShot(1000, this, pollCloudClip);
        });
    };

    pollCloudClip();
}

Widget::~Widget()
{
    delete ui;
}
