#include "widget.h"
#include "ui_widget.h"
#include "util.h"
#include <QClipboard>
#include <QMimeData>
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

    this->tipWidget = new TipWidget(this);

    this->sysTray = new QSystemTrayIcon(this);
    updateConnectionStatus(true);
    sysTray->show();

    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QImage qrImage = QRUtil::encodeText(id);
    ui->label_qr->setPixmap(QPixmap::fromImage(qrImage));
    ui->label_qr->adjustSize();
    ui->label_qr->setToolTip(id);

    static QString userId = "mrbeanc"; //改为从文件读取 //TODO 改为 (uuid + userId + day)的hash值，每日动态变化，保证安全性
    static QString hashId = Util::genSHA256(userId);
    static QString baseUrl = "https://124.220.81.213"; //https
    // static QString baseUrl = "http://localhost:8080";
    this->manager = new QNetworkAccessManager(this);

    //更新连接状态（UI显示）
    //每个请求结束都会触发
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        updateConnectionStatus(reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
    });

    //监听剪贴板变化
    connect(qApp->clipboard(), &QClipboard::dataChanged, this, [=](){
        if (isMeSetClipboard) { // 避免检测到自身对剪切板的修改
            isMeSetClipboard = false;
            return;
        }

        const QMimeData* clipData = qApp->clipboard()->mimeData();
        if (clipData->formats().isEmpty()) { //复制 then [粘贴文件]的时候，剪贴板会变化，并且formats为空，WTF？
            qWarning() << "WARN: No formats";
            return;
        }

        QByteArray data;
        if (clipData->hasImage()) {
            //format: application/x-qt-image
            QImage image = qvariant_cast<QImage>(clipData->imageData()); //不能直接toByteArray，需要先转换为QImage，否则为空
            if (!image.isNull()) {
                QBuffer buffer(&data);
                buffer.open(QIODevice::WriteOnly);
                image.save(&buffer, "jpg"); // 将 QImage 保存为 jpg 格式
                buffer.close();
                data = data.toBase64(); // 转换为 Base64 编码，防止老式设备进行隐式编解码导致信息丢失
            }
        } else if (clipData->hasText()) {
            data = clipData->text().toUtf8();
        } else {
            qWarning() << "WARN: This Type is not supported NOW." << clipData->formats();
            return;
        }

        if (data.size() > 1024 * 1024 * 2) { // 2MB
            qWarning() << "WARN: Data too large, ignore.";
            sysTray->showMessage("WARN", "Data too large, ignore.");
            return;
        }

        QNetworkRequest request(QUrl(QString("%1/clipboard/%2/win").arg(baseUrl, hashId)));
        // 超时会abort()，同时触发finished信号，并产生QNetworkReply::OperationCanceledError 状态码为0
        request.setTransferTimeout(8 * 1000); // 8s超时时间
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QJsonObject jsonData;
        jsonData.insert("data", QString::fromUtf8(data));
        jsonData.insert("isText", !clipData->hasImage()); //有可能同时hasText，所以以image为准

        QJsonDocument doc(jsonData);
        QByteArray postData = doc.toJson();

        QNetworkReply *reply = manager->post(request, postData);
        tipWidget->showNormalStyle();
        QTime start = QTime::currentTime();

        QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (reply->error() == QNetworkReply::NoError) {
                qDebug() << "↑Copied to Cloud √." << statusCode << Util::printDataSize(data.size()) << start.msecsTo(QTime::currentTime()) << "ms";
                tipWidget->hide();
            } else {
                qCritical() << "× !!Post Error:" << statusCode << reply->errorString();
                tipWidget->showFailedStyle();
                QTimer::singleShot(2000, tipWidget, &TipWidget::hide);
            }
            reply->deleteLater(); //比delete更安全，因为不确定是否有其他slot未执行
        });
    });

    static std::function<void(void)> pollCloudClip = [=](){
        QNetworkRequest request(QUrl(QString("%1/clipboard/long-polling/%2/win").arg(baseUrl, hashId)));
        // 可以加入心跳机制确保更快重连（丢弃失败的连接），毕竟90s还是太长
        // 不过等我遇到问题再加吧hh 应该是小概率事件，相信HTTP！
        request.setTransferTimeout(90 * 1000); // 90s超时时间，避免服务端掉线 & 网络异常造成的无响应永久等待
        QNetworkReply *reply = manager->get(request);
        qDebug() << "+Start long-polling...";

        connect(reply, &QNetworkReply::finished, this, [=]() {
            int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            qDebug() << "-Long polling done." << statusCode;
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
                    sysTray->showMessage("↓Pasted from IOS", isText ? data : "[Image]"); //可以在 系统-通知 中关闭声音
                    qDebug() << "↓Pasted from IOS;" << Util::printDataSize(data.toUtf8().size());
                }

                if (statusCode == 200 && data.isEmpty()) { //有时出现 200、NoError，但是无数据的情况 Why！！
                    qCritical() << "WTF! 200 but no data";
                    qDebug() << jsonData;
                    qDebug() << replyData;
                    qDebug() << reply->errorString();
                }
            } else {
                qCritical() << "× !!Get Error:" << reply->errorString();
            }
            reply->deleteLater();

            QTimer::singleShot(1000, this, pollCloudClip);
        });
    };

    pollCloudClip(); //发起长轮询，以获取实时推送
}

Widget::~Widget()
{
    delete ui;
}

void Widget::updateConnectionStatus(bool isConnected)
{
    if (this->isConnected == isConnected) return;
    this->isConnected = isConnected;

    Q_ASSERT(this->sysTray);

    if (isConnected) {
        sysTray->setToolTip(APP_NAME);
        sysTray->setIcon(QIcon(":/img/clipboard.ico"));
    } else {
        sysTray->setToolTip(APP_NAME + " - [Disconnected]");
        sysTray->setIcon(QIcon(":/img/clipboard-fail.ico"));
    }
}
