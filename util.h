#ifndef UTIL_H
#define UTIL_H

#include <QString>


class Util {
private:
    Util() = delete;

public:
    // 根据数据量，转化为以合适的单位 (B, KB, MB, GB)，返回String
    static QString printDataSize(int bytes);

};

#endif // UTIL_H
