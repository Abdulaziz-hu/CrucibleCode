cmake_minimum_required(VERSION 3.21)

project(ForgeIDE
    VERSION 0.1.0
    DESCRIPTION "Forge - a lightweight cross-platform C++ IDE built on Qt 6"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FORGE_USE_KSYNTAX
       "Use KSyntaxHighlighting (MIT) for syntax highlighting when it is installed" ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets)
qt_standard_project_setup()

set(FORGE_HAVE_KSYNTAX OFF)
if(FORGE_USE_KSYNTAX)
    find_package(KF6SyntaxHighlighting QUIET)
    if(KF6SyntaxHighlighting_FOUND)
        set(FORGE_HAVE_KSYNTAX ON)
    endif()
endif()
message(STATUS "KSyntaxHighlighting: ${FORGE_HAVE_KSYNTAX}")

qt_add_executable(forge WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/MainWindow.h        src/MainWindow.cpp
    src/CodeEditor.h        src/CodeEditor.cpp
    src/SyntaxHighlighter.h src/SyntaxHighlighter.cpp
    src/ThemeManager.h      src/ThemeManager.cpp
    src/BuildManager.h      src/BuildManager.cpp
    src/FindBar.h           src/FindBar.cpp
    src/SearchPanel.h       src/SearchPanel.cpp
    src/SettingsDialog.h    src/SettingsDialog.cpp
)

target_link_libraries(forge PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
target_compile_definitions(forge PRIVATE FORGE_VERSION="${PROJECT_VERSION}")

if(FORGE_HAVE_KSYNTAX)
    target_link_libraries(forge PRIVATE KF6::SyntaxHighlighting)
    target_compile_definitions(forge PRIVATE FORGE_HAVE_KSYNTAX)
endif()

if(MSVC)
    target_compile_options(forge PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(forge PRIVATE -Wall -Wextra)
endif()

set_target_properties(forge PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Forge"
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.forgeide.forge"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")

# ---- install / deploy -------------------------------------------------------
install(TARGETS forge
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin)

if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    qt_generate_deploy_app_script(
        TARGET forge
        OUTPUT_SCRIPT forge_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT ${forge_deploy_script})
endif()
