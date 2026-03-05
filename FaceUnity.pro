QT       += core gui openglwidgets

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
CONFIG += c++20
QMAKE_CXXFLAGS += /std:c++17 /await

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    CameraCapture.cpp \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    CameraCapture.h \
    FilterContext.h \
    Filters.h \
    Logger.h \
    VideoRenderWidget.h \
    mainwindow.h

LIBS += -ld3d11 -ldxgi -ld3dcompiler -ldwmapi -luser32 -lwindowsapp -lmmdevapi -lole32

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target
