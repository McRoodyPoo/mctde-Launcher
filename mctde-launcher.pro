# Linux launcher window (Qt 6 Widgets). Built by build_linux.sh; the Windows launcher builds with
# build_launcher.bat. Output: bin/mctde-launcher (window; with a command it acts as the CLI).
TEMPLATE = app
TARGET = mctde-launcher
DESTDIR = $$PWD/bin
OBJECTS_DIR = $$OUT_PWD/obj
RCC_DIR = $$OUT_PWD/rcc
MOC_DIR = $$OUT_PWD/moc
QT += widgets
CONFIG += c++17 release warn_on
CONFIG -= debug_and_release
# Don't pin the binary to the exact Qt minor it was built against (lets it run on older Qt 6).
DEFINES += QT_NO_VERSION_TAGGING
QMAKE_CXXFLAGS_WARN_ON += -Wno-misleading-indentation
INCLUDEPATH += .

SOURCES += \
    core/install.cpp core/caches.cpp core/platform_posix.cpp \
    linux/gui.cpp linux/cli.cpp linux/steam.cpp linux/launch.cpp linux/vdf.cpp linux/pe_version.cpp linux/update.cpp linux/sha256.cpp linux/settings.cpp linux/exe_opener.cpp linux/gamepad.cpp
HEADERS += \
    core/install.h core/caches.h core/platform.h \
    linux/cli.h linux/steam.h linux/launch.h linux/vdf.h linux/pe_version.h linux/update.h linux/sha256.h linux/version.h linux/theme.h linux/settings.h linux/exe_opener.h linux/gamepad.h
RESOURCES += linux/resources.qrc
