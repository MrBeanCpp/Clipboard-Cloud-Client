#include "util.h"

#include <QApplication>
#include <QSettings>
#include <QDir>

const QString Util::REG_AUTORUN = "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"; //HKEY_CURRENT_USER仅仅对当前用户有效，但不需要管理员权限

QString Util::printDataSize(int bytes)
{
    if (bytes < 1024) {
        return QString::number(bytes) + " B";
    } else if (bytes < 1024 * 1024) {
        return QString::number(bytes / 1024) + " KB";
    } else if (bytes < 1024 * 1024 * 1024) {
        return QString::number(bytes / 1024 / 1024) + " MB";
    } else {
        return QString::number(bytes / 1024 / 1024 / 1024) + " GB";
    }
}

QString Util::genSHA256(const QString& str)
{
    return QString(QCryptographicHash::hash(str.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString Util::appPath()
{
    return QDir::toNativeSeparators(QApplication::applicationFilePath());
}

void Util::setAutoRun(const QString& appName, bool autoRun)
{
    QSettings reg(REG_AUTORUN, QSettings::NativeFormat);
    if (autoRun)
        reg.setValue(appName, appPath());
    else
        reg.remove(appName);
}

bool Util::isAutoRun(const QString& appName)
{
    QSettings reg(REG_AUTORUN, QSettings::NativeFormat);
    return reg.value(appName).toString() == appPath();
}
