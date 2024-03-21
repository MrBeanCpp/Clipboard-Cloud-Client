#include "util.h"

#include <QApplication>
#include <QSettings>
#include <QDir>
#include <QMimeData>
#include <QDebug>
#include <QImage>
#include <QBuffer>
#include <QClipboard>
#include <QUuid>

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

QByteArray Util::clipboardData(bool* isText)
{
    QByteArray data;
    const QMimeData* clipData = qApp->clipboard()->mimeData();
    if (clipData->formats().isEmpty()) { //复制 then [粘贴文件]的时候，剪贴板会变化，并且formats为空，WTF？
        qWarning() << "WARN: No formats";
        return data;
    }

    if (isText != nullptr) {
        *isText = !clipData->hasImage(); //有可能同时hasText，所以以image为准
    }
    if (clipData->hasImage()) {
        //format: application/x-qt-image
        QImage image = qvariant_cast<QImage>(clipData->imageData()); //不能直接toByteArray，需要先转换为QImage，否则为空
        if (!image.isNull()) {
            QBuffer buffer(&data);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "jpg"); // 将 QImage 保存为 jpg 格式
            buffer.close();
            // data = data.toBase64(); // 转换为 Base64 编码，防止老式设备进行隐式编解码导致信息丢失
        }
    } else if (clipData->hasText()) {
        data = clipData->text().toUtf8();
    } else {
        qWarning() << "WARN: This Type is not supported NOW." << clipData->formats();
    }
    return data;
}

QString Util::genUUID()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
