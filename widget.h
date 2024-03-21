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
    void updateConnectionStatus(bool isConnected);
    void initSystemTray();
    void readSettings();
    void writeSettings();
    void initSettings();
    void reflashUUID();
    void showSettingData();
    void showQrCode(const QString& text);

signals:
    void appReady();

private:
    Ui::Widget *ui;

    const QString APP_NAME = "Clipboard-Cloud";
    const QString REG_APP_NAME = "Clipboard-Cloud.MrBeanCpp";
    const QString SETTINGS_FILE = qApp->applicationDirPath() +  "/settings.ini";
    QNetworkAccessManager* manager = nullptr;
    QSystemTrayIcon *sysTray = nullptr;
    bool isConnected = false; //与服务器的连接状态
    bool isMeSetClipboard = false; //是否是本程序设置了剪贴板
    TipWidget *tipWidget = nullptr;

    const QString defaultServerUrl = "https://124.220.81.213";
    QString baseUrl;
    QString userId;
    QString uuid;
    QString hashId;

    bool isAppReady = false; //是否已经初始化完成

    // QWidget interface
protected:
    virtual void showEvent(QShowEvent* event) override;
    virtual void closeEvent(QCloseEvent* event) override;
};
#endif // WIDGET_H
