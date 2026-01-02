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
#include <QTimer>
#include <QSettings>
#include <QFile>
#include <QMessageBox>
#include <QMenu>
#include <QCloseEvent>
#include "wintoastlib.h"
#include "toastHandler.h"
#include <QDesktopServices>
#include <QFileDialog>

Widget::Widget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::Widget)
{
    ui->setupUi(this);
    setWindowTitle(APP_NAME + " by [MrBeanCpp]"); // https://github.com/MrBeanCpp

    this->manager = new QNetworkAccessManager(this);
    QSslConfiguration defaultConfig = QSslConfiguration::defaultConfiguration();
    qDebug() << "default protocol:" << defaultConfig.protocol();
    // 启用SSL session ticket，会增加一点点内存
    defaultConfig.setSslOption(QSsl::SslOptionDisableSessionPersistence, false);
    defaultConfig.setProtocol(QSsl::TlsV1_3);  // 指定 TLS 1.3
    QSslConfiguration::setDefaultConfiguration(defaultConfig);

    this->tipWidget = new TipWidget(this);

    if (QFile::exists(SETTINGS_FILE)) {
        readSettings();
        QTimer::singleShot(0, this, [=](){ emit appReady(); }); // 延迟emit，防止构造函数中信号未连接
    } else {
        initSettings();
    }

    initSystemTray();

    // test server-url available
    connect(ui->btn_server_test, &QPushButton::clicked, this, [=]() {
        // Get请求也隐式支持HEAD请求，减少带宽消耗
        QNetworkRequest request(QNetworkRequest(ui->edit_server->text() + "/test"));
        request.setTransferTimeout(3500);
        QNetworkReply *reply = manager->head(request); //TODO set timeout
        QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
            bool isOk = reply->error() == QNetworkReply::NoError;
            QMessageBox::information(this, "Test Server", isOk ? "Connected" : "Error");
            reply->deleteLater();
        });
    });
    connect(ui->btn_uuid_reset, &QPushButton::clicked, this, &Widget::reflashUUID);
    connect(ui->btn_save, &QPushButton::clicked, this, [=](){
        this->baseUrl = ui->edit_server->text();
        this->userId = ui->edit_userid->text();
        this->uuid = ui->edit_uuid->text();
        this->hashId = genHashID();

        if (pollingReply) {
            qDebug() << "Settings changed, abort old polling.";
            pollingReply->abort(); // stop old polling & start new automatically
            pollingReply = nullptr;
        }

        writeSettings();
        emit appReady();// save to ready while initSettings()

        // show saved animation, to indicate the user
        static const QString OriginalText = ui->btn_save->text(); // only init once
        ui->btn_save->setText("Saved✔");
        QTimer::singleShot(500, this, [=](){ ui->btn_save->setText(OriginalText); });
    });

    //更新连接状态（UI显示）
    //每个请求结束都会触发
    connect(manager, &QNetworkAccessManager::finished, this, [=](QNetworkReply* reply){
        updateConnectionStatus(reply->error() == QNetworkReply::NoError);
        reply->deleteLater();
    });

    //监听剪贴板变化
    //sth.:hexo博客界面 代码块右上角的复制按钮，为什么会产生17次同样数据的剪切板修改, not my problem
    //TODO: 浏览器复制URL会触发三次（应该是浏览器问题？）
    //TODO: lastTime 限制频率，避免重复上传，但是不能只判断内容，只要不是短时间高频率的重复，都应该上传，才符合直觉
    connect(qApp->clipboard(), &QClipboard::dataChanged, this, [=](){
        if (recvOnly) return;
        if (isMeSetClipboard) { // 避免检测到自身对剪切板的修改
            qDebug() << "Info: Me set clipboard, ignore.";
            isMeSetClipboard = false;
            return;
        }
        postClipboard();
    });

    // after: readSettings or save
    connect(this, &Widget::appReady, this, [=](){
        if (isAppReady) return; //防止重复初始化
        this->isAppReady = true;

        sysTray->showMessage("App Ready", "Connecting Server...");
        pollCloudClip(); //发起长轮询，以获取实时推送
    });

    initWinToast(APP_NAME, "Aliaba");
}

Widget::~Widget()
{
    delete ui;
}

void Widget::postClipboard()
{
    if (!isAppReady) {
        sysTray->showMessage("WARN", "App not ready.", QSystemTrayIcon::Warning);
        return;
    }

    bool isText;
    // 1.图像进行 Base64 编码，防止老式设备进行隐式编解码导致信息丢失
    // 2.文本也进行 BASE64 编码，防止外链明文泄露，造成言论安全问题
    QByteArray data = Util::clipboardData(&isText).toBase64();
    if (data.isEmpty()) {
        sysTray->showMessage("WARN", "Clipboard data is empty.");
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
    jsonData.insert("isText", isText);

    QJsonDocument doc(jsonData);
    QByteArray postData = doc.toJson();

    QTime start = QTime::currentTime();
    QNetworkReply *reply = manager->post(request, postData);
    tipWidget->showNormalStyle();

    QObject::connect(reply, &QNetworkReply::finished, this, [=]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() == QNetworkReply::NoError) { // 实验室环境, （第二次发）1KB以上数据（图片）比1KB以下（文本）要快（40ms vs 120ms）离谱！！
            qDebug() << "↑Copied to Cloud √." << statusCode << Util::printDataSize(data.size()) << start.msecsTo(QTime::currentTime()) << "ms";
            tipWidget->hide();
        } else {
            qCritical() << "× !!Post Error:" << statusCode << reply->errorString();
            tipWidget->showFailedStyle();
            QTimer::singleShot(2000, tipWidget, &TipWidget::hide);
            sysTray->showMessage("Post Error", QString("code: %1, msg: %2").arg(statusCode).arg(reply->errorString()), QSystemTrayIcon::Warning);
        }
        reply->deleteLater(); //比delete更安全，因为不确定是否有其他slot未执行
    });

    // SSL会话缓存重用貌似也会触发这个？
    connect(reply, &QNetworkReply::encrypted, this, [=]() {
        qDebug() << "SSL握手完成时间:" << start.msecsTo(QTime::currentTime()) << "ms; sessionTicket size:"
                 << reply->sslConfiguration().sessionTicket().size() << "protocol:"<< reply->sslConfiguration().protocol();
    });
    connect(reply, &QNetworkReply::sslErrors, this, [=](const QList<QSslError>& errors) {
        qDebug() << "SSL握手错误:" << errors;
    });
}

void Widget::pollCloudClip()
{
    QNetworkRequest request(QUrl(QString("%1/clipboard/long-polling/%2/win").arg(baseUrl, hashId)));
    // 可以加入心跳机制确保更快重连（丢弃失败的连接），毕竟90s还是太长
    // 不过等我遇到问题再加吧hh 应该是小概率事件，相信HTTP！
    request.setTransferTimeout(90 * 1000); // 90s超时时间，避免服务端掉线 & 网络异常造成的无响应永久等待
    QNetworkReply *reply = manager->get(request);
    this->pollingReply = reply; // for aborting it when server changed
    qDebug() << "+Start long-polling...";

    connect(reply, &QNetworkReply::finished, this, [=]() {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        qDebug() << "-Long polling done." << statusCode;
        if (reply->error() == QNetworkReply::NoError) {
            QByteArray replyData = reply->readAll();
            QJsonDocument doc = QJsonDocument::fromJson(replyData);
            QJsonObject jsonData = doc.object();

            const QString os = jsonData.value("os").toString();
            const QString base64Data = jsonData.value("data").toString();
            const QByteArray base64Bytes = base64Data.toUtf8();
            const QByteArray data = QByteArray::fromBase64(base64Bytes); //base64解码
            const bool isText = jsonData.value("isText").toBool();

            if (os == "ios" && !data.isEmpty()) {
                isMeSetClipboard = true;
                QString readableSize = Util::printDataSize(base64Bytes.size());
                if (isText) {
                    auto text = QString::fromUtf8(data);
                    qApp->clipboard()->setText(text);
                    auto httpUrl = Util::extractFirstHttpUrl(text);
                    if (!httpUrl.isEmpty()) {
                        qDebug() << "Detected URL in Pasted Text";
                        Util::downloadFaviconIcoToTemp(manager, httpUrl, [=](QString localIcoPath){
                            auto tmCode = Util::parseTencentMeetingCode(httpUrl);
                            if (!tmCode.isEmpty()) { // 腾讯会议链接
                                bool isTMInstalled = Util::isTencentMeetingInstalled();
                                showToastWithActions(localIcoPath, "Tencent Meeting invitation", text, "code: " + tmCode, [=](int actionIndex){
                                    if (actionIndex == 0) // Open
                                        QDesktopServices::openUrl(QUrl(httpUrl));
                                    else if (actionIndex == 1)
                                        Util::openTencentMeetingClient(tmCode);
                                }, "Open in browser 🌐", isTMInstalled ? "Launch App 🖥️" : "");
                            } else { // 普通超链接
                                showToastWithActions(localIcoPath, "Link detected. Click to Open", text, "from iOS", [=](int actionIndex){
                                    if (actionIndex == 0) // Open
                                        QDesktopServices::openUrl(QUrl(httpUrl));
                                });
                            }
                        });
                    } else
                        sysTray->showMessage("↓Pasted Text from IOS", text); //可以在 系统-通知 中关闭声音
                } else {
                    auto img = QImage::fromData(data);
                    qApp->clipboard()->setImage(img);
                    // https://learn.microsoft.com/en-us/windows/apps/develop/notifications/app-notifications/adaptive-interactive-toasts?tabs=appsdk#hero-image
                    auto thumbnail = img.scaled(364, 180, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                    auto thumbPath = Util::saveImageToTemp(thumbnail, "jpg");
                    qDebug() << "Image saved to temp path:" << thumbPath;
                    auto bodyText = QString("%1  (%2 × %3)").arg(readableSize).arg(img.width()).arg(img.height());
                    showToastWithHeroImageText(thumbPath, "Click to save image", bodyText, [img]{ // 弹窗选择保存位置
                        auto path = QFileDialog::getSaveFileName(nullptr, "Save Image", {}, "Images (*.jpg *.png)");
                        if (path.isEmpty()) return;
                        if (img.save(path)) {
                            Util::openExplorerAndSelectFile(path);
                            qDebug() << "Image saved to:" << path;
                        }
                    });
                }
                qDebug() << "↓Pasted from IOS;" << readableSize;
                // TODO 为什么一张照片在这里显示 993 KB，但是copy到QQ聊天框保存到本地后有6.88MB (because .jpg to .png!?)
            }
        } else {
            qCritical() << "× !!Get Error:" << reply->errorString();
        }
        reply->deleteLater();
        this->pollingReply = nullptr;

        QTimer::singleShot(1000, this, &Widget::pollCloudClip);
    });
}

void Widget::updateConnectionStatus(bool isConnected)
{
    if (this->isConnected == isConnected) return;
    this->isConnected = isConnected;

    Q_ASSERT(this->sysTray);

    const QString MSG = "\n[click to Post]";
    if (isConnected) {
        sysTray->setToolTip(APP_NAME + MSG); //TODO 增加在线人数
        sysTray->setIcon(QIcon(":/img/dog-paw.ico"));
    } else {
        sysTray->setToolTip(APP_NAME + " - [Disconnected]" + MSG);
        sysTray->setIcon(QIcon(":/img/dog-paw-fail.ico"));
    }
}

void Widget::initSystemTray()
{
    if (this->sysTray) return;
    this->sysTray = new QSystemTrayIcon(this);
    connect(sysTray, &QSystemTrayIcon::messageClicked, this, &Widget::show);
    connect(sysTray, &QSystemTrayIcon::activated, this, [=](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick){ // 会先触发一次Trigger 很离谱
            show(), activateWindow();
        } else if (reason == QSystemTrayIcon::Trigger){
            this->postClipboard();
        }
    });

    QMenu* menu = new QMenu(this);
    menu->setStyleSheet("QMenu{background-color:rgb(15,15,15);color:rgb(220,220,220);}"
                        "QMenu:selected{background-color:rgb(60,60,60);}");
    QAction* act_post = new QAction("Post↗ Clipboard", menu);
    QAction* act_setting = new QAction("Settings⚙", menu);
    QAction* act_recvOnly = new QAction("Receive-Only", menu);
    QAction* act_autoStart = new QAction("Auto-Start", menu);
    QAction* act_quit = new QAction("Quit>>", menu);

    connect(act_post, &QAction::triggered, this, &Widget::postClipboard);
    connect(act_setting, &QAction::triggered, this, &Widget::showNormal);

    act_recvOnly->setCheckable(true);
    act_recvOnly->setChecked(this->recvOnly);
    connect(act_recvOnly, &QAction::toggled, this, [=](bool checked) {
        this->recvOnly = checked;
        sysTray->showMessage(QString("Receive-Only Mode ") + (recvOnly ? "[ON]" : "[OFF]"),
                             recvOnly ? "STOP Auto-Post" : "START Auto-Post");
        writeSettings();
    });

    act_autoStart->setCheckable(true);
    act_autoStart->setChecked(Util::isAutoRun(REG_APP_NAME));
    connect(act_autoStart, &QAction::toggled, this, [=](bool checked) {
        Util::setAutoRun(REG_APP_NAME, checked);
        sysTray->showMessage("Auto-Start Mode", checked ? "Added [Auto-Start]" : "Removed [Auto-Start]");
    });

    connect(act_quit, &QAction::triggered, qApp, &QApplication::quit);

    menu->addAction(act_post);
    menu->addAction(act_setting);
    menu->addAction(act_recvOnly);
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
    bool recvOnly = ini.value("app/recvOnly", false).toBool();

    if (baseUrl.isEmpty() || uuid.isEmpty()) { // UserID可以为空
        qWarning() << "WARN: Settings file Error.";
        QMessageBox::warning(this, "Settings Error", "Settings file Error.");
        qApp->quit();
        return;
    }

    qDebug() << "Read settings:" << baseUrl << userId << uuid << recvOnly;
    this->baseUrl = baseUrl;
    this->userId = userId;
    this->uuid = uuid;
    this->hashId = genHashID();

    this->recvOnly = recvOnly;
}

void Widget::writeSettings()
{
    if (baseUrl.isEmpty() || uuid.isEmpty()){ // UserID可以为空
        QMessageBox::critical(this, "ERROR", "Something is Empty!!");
        return;
    }

    QSettings ini(SETTINGS_FILE, QSettings::IniFormat);
    ini.setValue("server/url", baseUrl);
    ini.setValue("user/id", userId);
    ini.setValue("user/uuid", uuid);

    ini.setValue("app/recvOnly", recvOnly);

    qDebug() << "Write settings:" << baseUrl << userId << uuid;
}

void Widget::initSettings()
{
    this->uuid = Util::genUUID();
    this->baseUrl = defaultServerUrl;

    this->show();
}

void Widget::reflashUUID() // Reflash UI but no inner data change
{
    auto _uuid = Util::genUUID();
    ui->edit_uuid->setText(_uuid);
    showQrCode(_uuid);
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
    ui->label_qr->setToolTip("UUID");
}

QString Widget::genHashID()
{
    //TODO 改为 (uuid + userId + day)的hash值，每日动态变化，保证安全性
    auto _hashId = Util::genSHA256(uuid + userId); //re-calculate hashId
    qDebug() << "id:" << _hashId;
    return _hashId;
}

void Widget::showEvent(QShowEvent* event)
{
    showSettingData();
    QWidget::showEvent(event); // not necessary
}

void Widget::closeEvent(QCloseEvent* event)
{
    if (!isAppReady) {
        auto btn = QMessageBox::question(this, "WARN", "Save or Not?");
        if (btn == QMessageBox::No)
            event->accept();
        else
            emit ui->btn_save->clicked();
    } else
        event->accept();
}
