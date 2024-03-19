#ifndef QRUTIL_H
#define QRUTIL_H
#include "qrcodegen.hpp"
#include <QImage>
#include <QPainter>
using std::uint8_t;
using qrcodegen::QrCode;
using qrcodegen::QrSegment;

class QRUtil final {
    QRUtil() = delete;
public:
    static QImage generateQRImage(const QrCode &qr, int pixSize = 10, int border = 2) {
        int size = qr.getSize();
        QImage qrImage((size + border * 2) * pixSize, (size + border * 2) * pixSize, QImage::Format_RGB32);
        qrImage.fill(Qt::white); // 填充白色背景

        QPainter painter(&qrImage);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);

        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (qr.getModule(x, y)) {
                    painter.drawRect(QRect((x + border) * pixSize, (y + border) * pixSize, pixSize, pixSize));
                }
            }
        }

        return qrImage;
    }

    static QImage encodeText(const QString& text, int pixSize = 6, int border = 2, QrCode::Ecc errCorLvl = QrCode::Ecc::LOW) {
        const QrCode qr = QrCode::encodeText(text.toStdString().c_str(), errCorLvl);
        return generateQRImage(qr, pixSize, border);
    }
};

#endif // QRUTIL_H
