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
#include <QRegularExpression>
#include <QTemporaryFile>
#include <QUrl>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QSaveFile>
#include <QElapsedTimer>

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
        static auto extractFilePath = [](const QString& str) {
            static QRegularExpression re("filepath=\"([^\"]*)\"");
            QRegularExpressionMatch match = re.match(str);
            if (match.hasMatch()) {
                return match.captured(1);
            } else {
                return QString();
            }
        };

        // 貌似不同软件的mime格式不一样，这里只处理QQ
        // "<QQRichEditFormat><Info version=\"1001\"></Info><EditElement type=\"1\" imagebiztype=\"7\" textsummary=\"[崇拜]\"
        // filepath=\"E:\\xxx\\Tencent Files\\xxx\\Image\\C2C\\Image8\\a611951b0d...681d24c21a94c.gif\" shortcut=\"\">
        // </EditElement></QQRichEditFormat>"
        auto qqRichData = QString::fromUtf8(clipData->data("application/x-qt-windows-mime;value=\"QQ_Unicode_RichEdit_Format\""));
        auto imagePath = extractFilePath(qqRichData); // 正则提取路径
        // .gif 特殊处理，因为QImage不支持gif的Write，如果直接转换为jpg只会保留一帧
        if (imagePath.endsWith(".gif")) {
            QFile file(imagePath);
            if (file.open(QIODevice::ReadOnly)) {
                data = file.readAll();
                file.close();
                return data;
            } else {
                qCritical() << "Unable to open the file:" << imagePath;
            }
        }

        //format: application/x-qt-image
        QImage image = qvariant_cast<QImage>(clipData->imageData()); //不能直接toByteArray，需要先转换为QImage，否则为空
        if (!image.isNull()) {
            QBuffer buffer(&data);
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "jpg"); // 将 QImage 保存为 jpg 格式
            buffer.close();
            // data = data.toBase64(); // 转换为 Base64 编码，防止老式设备进行隐式编解码导致信息丢失
        } else
            qWarning() << "WARN: QImage is null";
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

QString Util::saveImageToTemp(const QImage& image, const char *format)
{
    QTemporaryFile tmp;
    tmp.setAutoRemove(false); // 关键：否则对象析构就删了，Toast 还没读到
    if (!tmp.open()) return {};

    image.save(&tmp, format);
    tmp.close();

    return QDir::toNativeSeparators(tmp.fileName());
}

bool Util::isHttpUrl(const QString& s)
{
    const QUrl url = QUrl::fromUserInput(s.trimmed());
    return url.isValid()
           && !url.host().isEmpty()
           && (url.scheme() == "http" || url.scheme() == "https");
}

QString Util::extractFirstHttpUrl(const QString& text)
{
    static const QRegularExpression urlRegex(R"((https?://[^\s"'<>()]+))",
                                             QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = urlRegex.match(text);
    if (match.hasMatch()) {
        QString urlStr = match.captured(1);
        if (isHttpUrl(urlStr))
            return urlStr;
    }
    return {};
}

void Util::downloadFaviconIcoToTemp(
    QNetworkAccessManager* nam,
    const QString& pageUrlStr,
    std::function<void(QString localPath)> cb,
    int timeoutMs)
{
    if (!nam) { cb({}); return; }

    QUrl pageUrl = QUrl::fromUserInput(pageUrlStr.trimmed());
    if (!pageUrl.isValid() || pageUrl.host().isEmpty()) { cb({}); return; }

    const QString scheme = pageUrl.scheme().toLower();
    if (scheme != "http" && scheme != "https") { cb({}); return; }

    QUrl icoUrl = pageUrl;
    icoUrl.setPath("/favicon.ico");
    icoUrl.setQuery({});
    icoUrl.setFragment({});
    icoUrl.setUserInfo({});

    const QString tmpDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + "/DogPaw_favicons";
    QDir().mkpath(tmpDir);

    const QByteArray key = QCryptographicHash::hash(icoUrl.host().toUtf8(), QCryptographicHash::Sha1).toHex();
    const QString icoPath = QDir(tmpDir).filePath(QString("favicon_%1.ico").arg(QString::fromLatin1(key)));
    const QString nativeIcoPath = QDir::toNativeSeparators(icoPath);

    // ✅ 缓存命中：文件存在且非空就直接返回
    QFileInfo f(icoPath);
    if (f.exists() && f.isFile() && f.size() > 0) {
        qDebug() << "[favicon] cache hit" << icoUrl.toString()
        << "path" << nativeIcoPath
        << "bytes" << f.size();
        cb(nativeIcoPath);
        return;
    }

    QNetworkRequest req(icoUrl);
    req.setTransferTimeout(timeoutMs);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);

    QElapsedTimer t;
    t.start();

    QNetworkReply* reply = nam->get(req);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, icoPath, nativeIcoPath, cb, t]() {
        qDebug() << "Favicon download took" << t.elapsed() << "ms";

        const QByteArray bytes = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError) && !bytes.isEmpty();
        reply->deleteLater();

        if (!ok) { cb({}); return; }

        QSaveFile f(icoPath); // 原子写入
        if (!f.open(QIODevice::WriteOnly)) { cb({}); return; }
        f.write(bytes);
        if (!f.commit()) { cb({}); return; }

        cb(nativeIcoPath);
    });
}
