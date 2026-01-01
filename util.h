#ifndef UTIL_H
#define UTIL_H

#include <QString>
#include <QCryptographicHash>
#include <QImage>
#include <QNetworkAccessManager>

class Util {
private:
    Util() = delete;

    static const QString REG_AUTORUN;

public:
    // 根据数据量，转化为以合适的单位 (B, KB, MB, GB)，返回String
    static QString printDataSize(int bytes);
    static QString genSHA256(const QString& str);

    static QString appPath(void);
    static void setAutoRun(const QString& appName, bool autoRun);
    static bool isAutoRun(const QString& appName);

    static QByteArray clipboardData(bool* isText = nullptr);
    static QString genUUID(void);

    static QString saveImageToTemp(const QImage& image, const char *format = nullptr);
    static bool isHttpUrl(const QString& s);
    static QString extractFirstHttpUrl(const QString& text);
    static void downloadFaviconIcoToTemp(QNetworkAccessManager* nam, const QString& pageUrlStr, std::function<void(QString localPath)> cb, int timeoutMs = 2000);

};

#endif // UTIL_H
