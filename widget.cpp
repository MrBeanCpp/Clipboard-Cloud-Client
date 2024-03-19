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
#include <QSettings>
#include <QFile>
#include <QMessageBox>
#include <QMenu>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    setWindowTitle("Clipboard-Cloud by [MrBeanCpp]"); // https://github.com/MrBeanCpp

    this->manager = new QNetworkAccessManager(this);
    this->tipWidget = new TipWidget(this);

    initSystemTray();

    ui->edit_server->setPlaceholderText("Server Url");
    ui->edit_userid->setPlaceholderText("User Id: Unique Identifier.");
    ui->edit_uuid->setPlaceholderText("uuid");

    if (QFile::exists(SETTINGS_FILE)) {
        readSettings();
    } else {
        initSettings();
    }

    // test server-url available
    connect(ui->btn_server_test, &QPushButton::clicked, this, [=]() {
        // Get请求也隐式支持HEAD请求，减少带宽消耗
        QNetworkReply *reply = manager->head(QNetworkRequest(ui->edit_server->text() + "/test"));
        QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
            bool isOk = reply->error() == QNetworkReply::NoError;
            QMessageBox::information(this, "Test Server", isOk ? "Connected" : "Error");
            reply->deleteLater();
        });
    });
    connect(ui->btn_uuid_reset, &QPushButton::clicked, this, &Widget::initUUID);
    connect(ui->btn_save, &QPushButton::clicked, this, &Widget::writeSettings);


    // userId = "mrbeanc"; //改为从文件读取 //TODO 改为 (uuid + userId + day)的hash值，每日动态变化，保证安全性
    static QString hashId = Util::genSHA256(uuid + userId);
    // static QString baseUrl = "https://124.220.81.213"; //https
    // baseUrl = "http://localhost:8080";
    qDebug() << hashId;

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

void Widget::initSystemTray()
{
    if (this->sysTray) return;
    this->sysTray = new QSystemTrayIcon(this);
    connect(sysTray, &QSystemTrayIcon::activated, this, [=](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger){
            show();
        }
    });

    QMenu* menu = new QMenu(this);
    menu->setStyleSheet("QMenu{background-color:rgb(15,15,15);color:rgb(220,220,220);}"
                        "QMenu:selected{background-color:rgb(60,60,60);}");
    QAction* act_setting = new QAction("Settings", menu);
    QAction* act_autoStart = new QAction("AutoStart", menu);
    QAction* act_quit = new QAction("Quit>>", menu);

    connect(act_setting, &QAction::triggered, this, &Widget::show);
    act_autoStart->setCheckable(true);
    act_autoStart->setChecked(Util::isAutoRun(REG_APP_NAME));
    connect(act_autoStart, &QAction::toggled, this, [=](bool checked) {
        Util::setAutoRun(REG_APP_NAME, checked);
        sysTray->showMessage("Tip", checked ? "已添加启动项" : "已移除启动项");
    });
    connect(act_quit, &QAction::triggered, qApp, &QApplication::quit);

    menu->addAction(act_setting);
    menu->addAction(act_autoStart);
    menu->addAction(act_quit);

    sysTray->setContextMenu(menu);
    updateConnectionStatus(true);
    sysTray->show();
}

void Widget::readSettings()
{
    if (!QFile::exists(SETTINGS_FILE)){
        qDebug() << "NO .ini file";
        return;
    }
    qDebug() << "Read settings:" << SETTINGS_FILE;

    QSettings ini(SETTINGS_FILE, QSettings::IniFormat);
    QString baseUrl = ini.value("server/url").toString();
    QString userId = ini.value("user/id").toString();
    QString uuid = ini.value("user/uuid").toString();

    if (baseUrl.isEmpty() || userId.isEmpty() || uuid.isEmpty()) {
        qWarning() << "WARN: Settings file Error.";
        QMessageBox::warning(this, "Settings Error", "Settings file Error.");
        return;
    }

    qDebug() << "Read settings:" << baseUrl << userId << uuid;
    this->baseUrl = baseUrl;
    this->userId = userId;
    this->uuid = uuid;
}

void Widget::writeSettings()
{
    this->baseUrl = ui->edit_server->text();
    this->userId = ui->edit_userid->text();
    this->uuid = ui->edit_uuid->text();

    if (baseUrl.isEmpty() || userId.isEmpty() || uuid.isEmpty()){
        QMessageBox::critical(this, "ERROR", "Something is Empty!!");
        return;
    }

    QSettings ini(SETTINGS_FILE, QSettings::IniFormat);
    ini.setValue("server/url", baseUrl);
    ini.setValue("user/id", userId);
    ini.setValue("user/uuid", uuid);

    qDebug() << "Write settings:" << baseUrl << userId << uuid;
    QMessageBox::information(this, "Save Settings", "Saved");
}

void Widget::initSettings()
{
    initUUID();
    this->baseUrl = defaultServerUrl;

    this->show();
}

void Widget::initUUID()
{
    this->uuid = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ui->edit_uuid->setText(uuid);
    showQrCode(uuid);
}

void Widget::showSettingData()
{
    ui->edit_server->setText(baseUrl);
    ui->edit_userid->setText(userId);
    ui->edit_uuid->setText(uuid);
    showQrCode(uuid);

    ui->edit_userid->setFocus();
}

void Widget::showQrCode(const QString& text)
{
    QImage qrImage = QRUtil::encodeText(text, 8);
    ui->label_qr->setPixmap(QPixmap::fromImage(qrImage));
    ui->label_qr->adjustSize();
    ui->label_qr->setToolTip(text);
}

void Widget::showEvent(QShowEvent* event)
{
    showSettingData();
    QWidget::showEvent(event);
}
