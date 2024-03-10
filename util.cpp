#include "util.h"


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
