TEMPLATE = subdirs
SUBDIRS = \
    moonlight-common-c \
    qmdnsengine \
    app \
    h264bitstream \
    tests

# Build the dependencies in parallel before the final app
app.depends = qmdnsengine moonlight-common-c h264bitstream
win32:!winrt {
    SUBDIRS += AntiHooking
    app.depends += AntiHooking
}

# Tests only need the lib dependencies, not the app itself
tests.file = tests/moonlight-mic-tests.pro
tests.depends = moonlight-common-c

# Support debug and release builds from command line for CI
CONFIG += debug_and_release

# Run our compile tests
load(configure)
qtCompileTest(SL)
qtCompileTest(EGL)
