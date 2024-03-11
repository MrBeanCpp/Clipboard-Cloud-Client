#ifndef WIDGET_H
#define WIDGET_H

#include <QNetworkAccessManager>
#include <QSystemTrayIcon>
#include <QWidget>
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

private:
    Ui::Widget *ui;

    const QString APP_NAME = "Clipboard-Cloud";
    QNetworkAccessManager* manager = nullptr;
    QSystemTrayIcon *sysTray = nullptr;
    bool isConnected = true; //与服务器的连接状态
    bool isMeSetClipboard = false; //是否是本程序设置了剪贴板
    TipWidget *tipWidget = nullptr;
};
#endif // WIDGET_H
