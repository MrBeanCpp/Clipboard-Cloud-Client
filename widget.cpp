#include "widget.h"
#include "ui_widget.h"
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

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QImage qrImage = QRUtil::encodeText(id);
    ui->label_qr->setPixmap(QPixmap::fromImage(qrImage));
    ui->label_qr->adjustSize();
    ui->label_qr->setToolTip(id);

    static QString clipId = "mrbeanc";
    static QString ip = "localhost";
    static QString port = "8080";
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
                data = data.toBase64(); // 转换为 Base64 编码
            }
        } else if (clipData->hasText()) { // TODO
            data = clipData->text().toUtf8();
        }

        QNetworkRequest request(QUrl(QString("http://%1:%2/clipboard/%3/win").arg(ip, port, clipId)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject jsonData;
        jsonData.insert("data", QString::fromUtf8(data));
        jsonData.insert("isText", !clipData->hasImage()); //有可能同时hasText，所以以image为准

        QJsonDocument doc(jsonData);
        QByteArray postData = doc.toJson();

        QNetworkReply *reply = manager.post(request, postData);

        QObject::connect(reply, &QNetworkReply::finished, [=]() {
            if (reply->error() == QNetworkReply::NoError) {
                qDebug() << "Copy to Cloud";
            } else {
                qDebug() << "Post Error:" << reply->errorString();
            }
            reply->deleteLater();
        });
    });

    static std::function<void(void)> pollCloudClip = [=](){
        QNetworkRequest request(QUrl(QString("http://%1:%2/clipboard/long-polling/%3/win").arg(ip, port, clipId)));
        QNetworkReply *reply = manager.get(request);
        qDebug() << "Start long polling...";

        connect(reply, &QNetworkReply::finished, this, [=]() {
            qDebug() << "Long polling done.";
            if (reply->error() == QNetworkReply::NoError) {
                QByteArray replyData = reply->readAll();
                QJsonDocument doc = QJsonDocument::fromJson(replyData);
                QJsonObject jsonData = doc.object();

                const QString os = jsonData.value("os").toString();
                const QString data = jsonData.value("data").toString();
                const bool isText = jsonData.value("isText").toBool();

                if (os == "ios" && !data.isEmpty()) {
                    if (isText) {
                        qApp->clipboard()->setText(data);
                    } else { // Not support now
                        // qApp->clipboard()->setImage(QImage::fromData(QByteArray::fromBase64(data.toUtf8())));
                    }
                    isMeSetClipboard = true;
                    qDebug() << "Paste from IOS";
                }
            } else {
                qDebug() << "Get Error:" << reply->errorString();
            }
            reply->deleteLater();

            QTimer::singleShot(2000, this, pollCloudClip);
        });
    };

    pollCloudClip();
}

Widget::~Widget()
{
    delete ui;
}
