cmake_minimum_required(VERSION 3.16)

project(seadrive-dolphin-ext)

option(BUILD_WITH_QT6 "Build for Plasma 6 (KF6); OFF builds for Plasma 5 (KF5)" ON)
if(BUILD_WITH_QT6)
    set(QT_MAJOR_VERSION 6)
else()
    set(QT_MAJOR_VERSION 5)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})
include(KDEInstallDirs)

find_package(Qt${QT_MAJOR_VERSION} REQUIRED COMPONENTS Widgets)
find_package(KF${QT_MAJOR_VERSION} REQUIRED COMPONENTS KIO CoreAddons)

add_library(seadrive-dolphin-overlay MODULE seadrive-overlay-plugin.cpp)
target_link_libraries(seadrive-dolphin-overlay KF${QT_MAJOR_VERSION}::KIOWidgets)
install(TARGETS seadrive-dolphin-overlay DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf${QT_MAJOR_VERSION}/overlayicon)

add_library(seadrive-dolphin-actions MODULE seadrive-action-plugin.cpp)
target_link_libraries(seadrive-dolphin-actions KF${QT_MAJOR_VERSION}::KIOWidgets KF${QT_MAJOR_VERSION}::CoreAddons)
install(TARGETS seadrive-dolphin-actions DESTINATION ${KDE_INSTALL_PLUGINDIR}/kf${QT_MAJOR_VERSION}/kfileitemaction)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/icons/
        DESTINATION ${KDE_INSTALL_ICONDIR}/hicolor)
