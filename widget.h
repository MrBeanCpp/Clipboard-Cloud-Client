#ifndef WIDGET_H
#define WIDGET_H

#include <QNetworkAccessManager>
#include <QSystemTrayIcon>
#include <QWidget>
#include <QApplication>
#include "TipWidget.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class Widget;
}
QT_END_NAMESPACE

class Widget : public QWidget
{
    Q_OBJECT

public:
    Widget(QWidget *parent = nullptr);
    ~Widget();
private:
    void postClipboard();
    void pollCloudClip();
    void updateConnectionStatus(bool isConnected);
    void initSystemTray();
    void readSettings();
    void writeSettings();
    void initSettings();
    void reflashUUID();
    void showSettingData();
    void showQrCode(const QString& text);
    QString genHashID(void);

signals:
    void appReady();

private:
    Ui::Widget *ui;

    const QString APP_NAME = "Dog-Paw";
    const QString REG_APP_NAME = "Dog-Paw.MrBeanCpp";
    const QString SETTINGS_FILE = qApp->applicationDirPath() +  "/settings.ini";
    QNetworkAccessManager* manager = nullptr;
    QSystemTrayIcon *sysTray = nullptr;
    bool isConnected = false; //与服务器的连接状态
    bool isMeSetClipboard = false; //是否是本程序设置了剪贴板
    TipWidget *tipWidget = nullptr;
    QNetworkReply* pollingReply = nullptr;

    const QString defaultServerUrl = "https://clipboard.aliaba.fun";
    QString baseUrl;
    QString userId;
    QString uuid;
    QString hashId;

    bool isAppReady = false; //是否已经初始化完成
    bool recvOnly = false; //仅接收模式，关闭监听剪贴板，不自动发送数据（for 隐私保护）

    // QWidget interface
protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void closeEvent(QCloseEvent* event) override;
};
#endif // WIDGET_H
