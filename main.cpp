#include "widget.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    QApplication a(argc, argv);
    QApplication::setQuitOnLastWindowClosed(false); //否则，在窗口没有show的时候，弹窗，并关闭弹窗，程序居然会退出
    Widget w;
    // w.show();
    return a.exec();
}
