QT += core testlib
CONFIG += c++17 console testcase debug_and_release
CONFIG -= app_bundle

TARGET = moonlight-mic-tests
TEMPLATE = app

# NOTE: We deliberately do NOT add QT += qml here.
# StreamingPreferences includes QQmlEngine which drags in Qt6Qml.dll and
# Qt6Network.dll. For Test A we test QSettings persistence directly
# (without constructing StreamingPreferences). For Test B we use the plain
# bool-overload of shouldStartMicSender() which avoids the Qt QML dependency.
# This keeps the test binary lightweight and runnable in headless CI.

SOURCES += \
    main.cpp \
    tst_toggle_persistence.cpp \
    tst_gate_logic.cpp \
    tst_mic_lifecycle.cpp \
    tst_display_input_policy.cpp \
    stub_limelight.cpp \
    gate_helper_impl.cpp \
    ../app/streaming/audio/MicAudioSender.cpp

HEADERS += \
    ../app/streaming/session_gate_helper.h \
    ../app/streaming/audio/MicAudioSender.h \
    ../app/SDL_compat.h \
    ../app/utils.h

INCLUDEPATH += \
    ../app \
    ../moonlight-common-c/moonlight-common-c/src

win32 {
    contains(QT_ARCH, x86_64) {
        LIBS += -L$$PWD/../libs/windows/lib/x64
        INCLUDEPATH += $$PWD/../libs/windows/include/x64 $$PWD/../libs/windows/include/x64/SDL2
    }
    contains(QT_ARCH, arm64) {
        LIBS += -L$$PWD/../libs/windows/lib/arm64
        INCLUDEPATH += $$PWD/../libs/windows/include/arm64 $$PWD/../libs/windows/include/arm64/SDL2
    }
    INCLUDEPATH += $$PWD/../libs/windows/include
    LIBS += ws2_32.lib winmm.lib
    LIBS += -lSDL2 -lopus
}

unix:!macx {
    CONFIG += link_pkgconfig
    PKGCONFIG += sdl2 opus
}

macx:!disable-prebuilts {
    INCLUDEPATH += $$PWD/../libs/mac/include $$PWD/../libs/mac/include/SDL2
    LIBS += -L$$PWD/../libs/mac/lib
    LIBS += -lopus -lSDL2
}
