# OBSBOT Tiny 3 Command Center — Qt 6 / QML build.
#
# Repo-local SDK: links the vendored libdev.so from ../sdk/libdev_v2.1.0_8 with an
# rpath pointing at that directory. No global SDK install, no sudo. The launcher
# script also exports LD_LIBRARY_PATH as a belt-and-braces fallback.
cmake_minimum_required(VERSION 3.21)

project(obsbot4linux VERSION 0.2.9 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# ---- Vendored OBSBOT SDK (repo-local) --------------------------------------
set(SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/libdev_v2.1.0_8")
set(SDK_INC  "${SDK_ROOT}/include")
set(SDK_LIB  "${SDK_ROOT}/linux/x86_64-release")

if(NOT EXISTS "${SDK_LIB}/libdev.so")
    message(FATAL_ERROR "libdev.so not found at ${SDK_LIB} — is the repo-local SDK present?")
endif()

# ---- Qt 6 ------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Quick Gui Qml QuickControls2 Multimedia)
qt_standard_project_setup(REQUIRES 6.5)

# QML sources live in the qml/ subdirectory. Opt into the modern qmldir handling
# when the running Qt knows this policy (6.8+); silences QTP0004 and keeps type
# resolution deterministic across Qt 6.8→6.10+. Guarded so 6.5–6.7 still build.
if(QT_KNOWN_POLICY_QTP0004)
    qt_policy(SET QTP0004 NEW)
endif()

qt_add_executable(obsbot4linux
    src/main.cpp
    src/CameraController.cpp
    src/CameraWorker.cpp
    src/PreviewEngine.cpp
    src/Settings.cpp
)

# Theme.qml is a singleton (pragma Singleton).
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(obsbot4linux
    URI Obsbot
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/GlassPanel.qml
        qml/ActionButton.qml
        qml/ToggleChip.qml
        qml/Segmented.qml
        qml/Stepper.qml
        qml/StatusPill.qml
        qml/NavRail.qml
        qml/RingGlyph.qml
        qml/PtzPad.qml
        qml/Viewfinder.qml
        qml/LogView.qml
        qml/KeyValue.qml
        qml/SectionLabel.qml
        qml/ControlPage.qml
        qml/ImagePage.qml
        qml/TrackingPage.qml
        qml/PresetsPage.qml
        qml/LogPage.qml
)

target_include_directories(obsbot4linux PRIVATE ${SDK_INC})
target_compile_options(obsbot4linux PRIVATE -Wall -Wextra)
target_compile_definitions(obsbot4linux PRIVATE APP_VERSION="${PROJECT_VERSION}")

target_link_libraries(obsbot4linux PRIVATE
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Gui
    Qt6::Qml
    Qt6::Multimedia
    "${SDK_LIB}/libdev.so"
    pthread
)

# Repo-local runtime linking: find libdev.so.1 (SONAME) from the vendored dir.
set_target_properties(obsbot4linux PROPERTIES
    BUILD_WITH_INSTALL_RPATH FALSE
    BUILD_RPATH   "${SDK_LIB}"
    INSTALL_RPATH "${SDK_LIB}"
)

# ---- Install rules (used to assemble the AppImage AppDir) ------------------
include(GNUInstallDirs)
install(TARGETS obsbot4linux
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/obsbot4linux.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/icons/obsbot4linux-256.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
    RENAME obsbot4linux.png)
install(FILES packaging/icons/obsbot4linux-128.png
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps
    RENAME obsbot4linux.png)
install(FILES packaging/icons/obsbot4linux.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)

# ---- Hardware-free tests (no SDK, no camera) -------------------------------
option(BUILD_TESTS "Build hardware-free tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_executable(settings_roundtrip_test tests/settings_roundtrip_test.cpp src/Settings.cpp)
    target_link_libraries(settings_roundtrip_test PRIVATE Qt6::Core)
    add_test(NAME settings_roundtrip COMMAND settings_roundtrip_test)
endif()
