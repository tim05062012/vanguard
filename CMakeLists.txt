cmake_minimum_required(VERSION 3.16)
project(VANGUARD VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Svg Network)
qt_standard_project_setup()

# Собираем .qrc через AUTORCC, чтобы стили и иконки попадали в exe именно по
# путям из resources/resources.qrc (:/styles/styles.qss, :/icons/logo.svg),
# как их читают MainWindow и IconFactory.
set(CMAKE_AUTORCC ON)

# app.rc (манифест requireAdministrator + VERSIONINFO) компилирует windres.
if (WIN32)
    enable_language(RC)
endif()

qt_add_executable(VANGUARD
    src/main.cpp
    src/MainWindow.cpp
    src/GameScanner.cpp
    src/Optimizer.cpp
    src/SettingsManager.cpp
    src/LocalGamesStore.cpp
    src/AddGameDialog.cpp
    src/CoverCache.cpp
    src/SettingsDialog.cpp
    src/FPSMonitor.cpp
    src/MetricsProvider.cpp
    src/FpsOverlay.cpp
    src/IconFactory.cpp
    src/ui/TitleBar.cpp
    src/ui/Sidebar.cpp
    src/ui/LibraryPage.cpp
    src/ui/GameCard.cpp
    src/ui/MonitorPage.cpp
    src/ui/OptimizePage.cpp
    src/ui/FrameTimeGraph.cpp
    include/MainWindow.h
    include/GameScanner.h
    include/Optimizer.h
    include/SettingsManager.h
    include/LocalGamesStore.h
    include/AddGameDialog.h
    include/CoverCache.h
    include/SettingsDialog.h
    include/FPSMonitor.h
    include/MetricsProvider.h
    include/FpsOverlay.h
    include/IconFactory.h
    include/ui/TitleBar.h
    include/ui/Sidebar.h
    include/ui/LibraryPage.h
    include/ui/GameCard.h
    include/ui/MonitorPage.h
    include/ui/OptimizePage.h
    include/ui/FrameTimeGraph.h
    resources/resources.qrc
    resources/app.rc
)

target_include_directories(VANGUARD PRIVATE include)
target_link_libraries(VANGUARD PRIVATE Qt6::Widgets Qt6::Svg Qt6::Network)

# WinAPI-зависимости MetricsProvider: сеть (GetIfTable2), диск (PDH),
# частота CPU (CallNtPowerInformation). MinGW линкует явно, без pragma comment.
if (WIN32)
    target_link_libraries(VANGUARD PRIVATE iphlpapi pdh powrprof)
endif()

set_target_properties(VANGUARD PROPERTIES
    WIN32_EXECUTABLE TRUE
)

# Раскладка Qt DLL рядом с exe: cmake --build build --target deploy
find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${Qt6_DIR}/../../../bin")
if (WINDEPLOYQT_EXECUTABLE)
    # windeployqt копирует Qt DLL и плагины, но НЕ транзитивные зависимости
    # Qt из MSYS2 (zlib1/libicu*/harfbuzz/... ) — копируем их явно.
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(MINGW_RUNTIME_DLLS
        libb2-1.dll libbrotlicommon.dll libbrotlidec.dll libbz2-1.dll
        libdouble-conversion.dll libffi-8.dll libfreetype-6.dll
        libgio-2.0-0.dll libglib-2.0-0.dll libgmodule-2.0-0.dll
        libgobject-2.0-0.dll libgraphite2.dll libharfbuzz-0.dll
        libiconv-2.dll libicudt78.dll libicuin78.dll libicuuc78.dll
        libintl-8.dll libjpeg-8.dll libmd4c.dll libpcre2-16-0.dll
        libpcre2-8-0.dll libpng16-16.dll libzstd.dll zlib1.dll)
    set(DEPLOY_COPY_DLLS "")
    foreach (_dll IN LISTS MINGW_RUNTIME_DLLS)
        list(APPEND DEPLOY_COPY_DLLS
             COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                     "${MINGW_BIN_DIR}/${_dll}"
                     "$<TARGET_FILE_DIR:VANGUARD>/${_dll}")
    endforeach()

    add_custom_target(deploy
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:VANGUARD>"
        ${DEPLOY_COPY_DLLS}
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/tools"
                "$<TARGET_FILE_DIR:VANGUARD>/tools"
        COMMENT "Копирование Qt DLL, рантайма MinGW и PresentMon рядом с VANGUARD.exe"
        VERBATIM)
else()
    message(STATUS "windeployqt не найден — цель deploy недоступна")
endif()
