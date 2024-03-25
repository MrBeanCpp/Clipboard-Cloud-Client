QT       += core gui
QT += network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    QRcode/qrcodegen.cpp \
    main.cpp \
    tipwidget.cpp \
    util.cpp \
    widget.cpp

HEADERS += \
    QRcode/QRUtil.h \
    QRcode/qrcodegen.hpp \
    tipwidget.h \
    util.h \
    widget.h

FORMS += \
    tipwidget.ui \
    widget.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

msvc {
    QMAKE_CFLAGS += /utf-8
    QMAKE_CXXFLAGS += /utf-8
}

RC_ICONS = img/dog-paw.ico

RESOURCES += \
    res.qrc

TARGET = DogPaw
