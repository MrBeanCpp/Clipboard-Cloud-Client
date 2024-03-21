#ifndef UTIL_H
#define UTIL_H

#include <QString>
#include <QCryptographicHash>

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

};

#endif // UTIL_H
