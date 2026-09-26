cmake_minimum_required(VERSION 3.24)
project(DocumentViewer VERSION 1.0 LANGUAGES CXX)
set(DOCUMENT_VIEWER_VERSION "" CACHE STRING "Displayed version (defaults to the Git release tag)")
if(NOT DOCUMENT_VIEWER_VERSION)
    find_package(Git QUIET)
    if(Git_FOUND)
        execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE DOCUMENT_VIEWER_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    endif()
    if(NOT DOCUMENT_VIEWER_VERSION)
        set(DOCUMENT_VIEWER_VERSION "dev")
    endif()
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
include(cmake/EnableLto.cmake)
find_package(Qt6 6.11 REQUIRED COMPONENTS Widgets CorePrivate Network)
qt_standard_project_setup()
include(cmake/Pdfium.cmake)
include(cmake/DjVuLibre.cmake)
if(MSVC)
    add_compile_options(/utf-8)
endif()
if(WIN32)
    add_compile_definitions(NOMINMAX)
endif()

add_library(viewer_core
    src/pdfview.cpp src/pdfview.h
    src/pdfselection.cpp src/textpage.h
    src/epub.cpp src/epub.h
    src/djvu.cpp src/djvu.h
    src/window.cpp src/window.h
    src/updater.cpp src/updater.h
    src/documentrequests.cpp src/documentrequests.h)
target_sources(viewer_core PRIVATE src/fileassociations.cpp src/fileassociations.h)
if(WIN32)
    target_link_libraries(viewer_core PRIVATE shell32)
elseif(APPLE)
    target_link_libraries(viewer_core PRIVATE "-framework CoreServices")
endif()
qt_add_resources(viewer_core updater_scripts
    PREFIX "/updates"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/updates"
    FILES packaging/updates/install.ps1 packaging/updates/install.sh)
target_link_libraries(viewer_core PUBLIC Qt6::Widgets Qt6::Network pdfium_runtime)
target_link_libraries(viewer_core PRIVATE Qt6::CorePrivate djvu_decoder)
target_include_directories(viewer_core PUBLIC src)
target_compile_definitions(viewer_core PUBLIC DOCUMENT_VIEWER_VERSION="${DOCUMENT_VIEWER_VERSION}")
qt_add_executable(DocumentViewer WIN32 MACOSX_BUNDLE src/main.cpp src/viewerstyle.h)
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
    set_target_properties(DocumentViewer PROPERTIES MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in")
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
install(FILES README.md LICENSE THIRD_PARTY.md DESTINATION .)
install(FILES CMakeLists.txt CMakePresets.json README.md LICENSE THIRD_PARTY.md
    DESTINATION sources/document-viewer)
install(DIRECTORY src cmake tests resources packaging scripts DESTINATION sources/document-viewer
    PATTERN "__pycache__" EXCLUDE)
file(GLOB viewer_build_scripts "${CMAKE_CURRENT_SOURCE_DIR}/*.ps1" "${CMAKE_CURRENT_SOURCE_DIR}/*.cmd")
install(FILES ${viewer_build_scripts} DESTINATION sources/document-viewer)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(viewer_tests tests/viewer_tests.cpp)
    target_link_libraries(viewer_tests PRIVATE viewer_core Qt6::Test Qt6::CorePrivate)
    copy_pdfium_runtime(viewer_tests)
    add_test(NAME viewer_tests COMMAND viewer_tests -platform offscreen)
    qt_add_executable(djvu_tests tests/djvu_tests.cpp)
    target_link_libraries(djvu_tests PRIVATE viewer_core Qt6::Test djvu_decoder)
    target_compile_definitions(djvu_tests PRIVATE HAVE_NAMESPACES=1)
    set_target_properties(djvu_tests PROPERTIES WIN32_EXECUTABLE FALSE)
    copy_pdfium_runtime(djvu_tests)
    add_test(NAME djvu_tests COMMAND djvu_tests -platform offscreen)
    qt_add_executable(request_tests tests/request_tests.cpp)
    target_link_libraries(request_tests PRIVATE viewer_core Qt6::Test)
    add_test(NAME request_tests COMMAND request_tests -o -,txt -o "${CMAKE_BINARY_DIR}/Testing/Temporary/request-tests.txt,txt")
    qt_add_executable(updater_tests tests/updater_tests.cpp)
    target_link_libraries(updater_tests PRIVATE viewer_core Qt6::Test)
    add_test(NAME updater_tests COMMAND updater_tests)
    if(WIN32)
        qt_add_executable(fileassociations_tests tests/fileassociations_tests.cpp)
        target_link_libraries(fileassociations_tests PRIVATE viewer_core Qt6::Test advapi32)
        set_target_properties(fileassociations_tests PROPERTIES WIN32_EXECUTABLE FALSE)
        add_test(NAME fileassociations_tests COMMAND fileassociations_tests)
        add_test(NAME update_installer_tests COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/update_windows_tests.ps1" -BuildDirectory "${CMAKE_BINARY_DIR}")
    elseif(UNIX)
        add_test(NAME update_installer_tests COMMAND sh
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/update_unix_tests.sh" "${CMAKE_BINARY_DIR}")
    endif()
    set_target_properties(viewer_tests request_tests updater_tests PROPERTIES WIN32_EXECUTABLE FALSE)
endif()
