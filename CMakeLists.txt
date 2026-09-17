cmake_minimum_required(VERSION 3.24)
project(DocumentViewer VERSION 1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(cmake/EnableLto.cmake)
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets CorePrivate Network)
qt_standard_project_setup()
include(cmake/Pdfium.cmake)
if(MSVC)
    add_compile_options(/utf-8)
endif()
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

add_library(viewer_core
    src/pdfview.cpp src/pdfview.h
    src/epub.cpp src/epub.h
    src/window.cpp src/window.h
    src/updater.cpp src/updater.h
    src/documentrequests.cpp src/documentrequests.h)
qt_add_resources(viewer_core updater_scripts
    PREFIX "/updates"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/updates"
    FILES packaging/updates/install.ps1 packaging/updates/install.sh)
target_link_libraries(viewer_core PUBLIC Qt6::Widgets Qt6::Network pdfium_runtime)
target_link_libraries(viewer_core PRIVATE Qt6::CorePrivate)
target_include_directories(viewer_core PUBLIC src)
qt_add_executable(DocumentViewer WIN32 MACOSX_BUNDLE src/main.cpp)
set_target_properties(DocumentViewer PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "org.documentviewer.app")
qt_add_resources(DocumentViewer app_icon
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons"
    FILES resources/icons/DocumentViewer.png)
if(WIN32)
    enable_language(RC)
    include(cmake/AppIcon.cmake)
    add_windows_app_icon(DocumentViewer)
elseif(APPLE)
    set_source_files_properties(resources/icons/DocumentViewer.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(DocumentViewer PRIVATE resources/icons/DocumentViewer.icns)
    set_target_properties(DocumentViewer PROPERTIES MACOSX_BUNDLE_ICON_FILE DocumentViewer.icns)
endif()
target_link_libraries(DocumentViewer PRIVATE viewer_core)
copy_pdfium_runtime(DocumentViewer)

install(TARGETS DocumentViewer BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(WIN32)
    install(FILES "$<TARGET_FILE:pdfium_runtime>" DESTINATION ${CMAKE_INSTALL_BINDIR})
elseif(APPLE)
    # The installed executable has only bundle-relative rpaths. Stage PDFium
    # before macdeployqt scans dependencies so it can resolve that path.
    install(FILES "$<TARGET_FILE:pdfium_runtime>" DESTINATION DocumentViewer.app/Contents/Frameworks)
else()
    install(FILES "$<TARGET_FILE:pdfium_runtime>" DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
qt_generate_deploy_app_script(TARGET DocumentViewer OUTPUT_SCRIPT deploy_script NO_TRANSLATIONS)
install(SCRIPT ${deploy_script})
install(FILES "${pdfium_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME PDFium-LICENSE.txt)
install(DIRECTORY "${pdfium_SOURCE_DIR}/licenses/" DESTINATION licenses/pdfium)
install(FILES README.md LICENSE DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(viewer_tests tests/viewer_tests.cpp)
    target_link_libraries(viewer_tests PRIVATE viewer_core Qt6::Test Qt6::CorePrivate)
    copy_pdfium_runtime(viewer_tests)
    add_test(NAME viewer_tests COMMAND viewer_tests -platform offscreen)
    qt_add_executable(request_tests tests/request_tests.cpp)
    target_link_libraries(request_tests PRIVATE viewer_core Qt6::Test)
    add_test(NAME request_tests COMMAND request_tests -o -,txt -o "${CMAKE_BINARY_DIR}/Testing/Temporary/request-tests.txt,txt")
    qt_add_executable(updater_tests tests/updater_tests.cpp)
    target_link_libraries(updater_tests PRIVATE viewer_core Qt6::Test)
    add_test(NAME updater_tests COMMAND updater_tests)
    if(WIN32)
        add_test(NAME update_installer_tests COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/update_windows_tests.ps1" -BuildDirectory "${CMAKE_BINARY_DIR}")
    elseif(UNIX)
        add_test(NAME update_installer_tests COMMAND sh
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/update_unix_tests.sh" "${CMAKE_BINARY_DIR}")
    endif()
    set_target_properties(viewer_tests request_tests updater_tests PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
