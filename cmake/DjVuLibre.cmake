include(FetchContent)
# Pin the upstream 3.5.30.1 release, including its security fixes.
FetchContent_Declare(djvulibre
    GIT_REPOSITORY https://git.code.sf.net/p/djvu/djvulibre-git
    GIT_TAG af7431e5b024cc24eb04f8609c271ae68542fd5e
    GIT_PROGRESS FALSE)
FetchContent_MakeAvailable(djvulibre)
# Upstream uses autotools. Build its library sources directly for all our toolchains.
file(GLOB djvu_sources CONFIGURE_DEPENDS "${djvulibre_SOURCE_DIR}/libdjvu/*.cpp")
add_library(djvu_decoder STATIC ${djvu_sources})
set_target_properties(djvu_decoder PROPERTIES AUTOMOC OFF AUTOUIC OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(djvu_decoder PUBLIC "${djvulibre_SOURCE_DIR}/libdjvu")
target_compile_definitions(djvu_decoder PUBLIC DJVUAPI= DDJVUAPI= MINILISPAPI=
    PRIVATE HAVE_NAMESPACES=1 HAVE_STDINCLUDES=1 HAVE_STDINT_H=1
    HAVE_WCHAR_H=1 HAVE_WCTYPE_H=1 HAVE_MBSTATE_T=1 HAVE_WCRTOMB=1
    HAVE_ISWSPACE=1 HAVE_VSNPRINTF=1 HAVE_SNPRINTF=1 HAVE_STRERROR=1
    DJVULIBRE_VERSION="3.5.30.1")
if(WIN32)
    target_compile_definitions(djvu_decoder PRIVATE WIN32 NOMINMAX _CRT_SECURE_NO_WARNINGS)
else()
    find_package(Threads REQUIRED)
    target_link_libraries(djvu_decoder PUBLIC Threads::Threads)
    # Autoconf derives these separately from HAVE_*; the Windows headers supply
    # them automatically. They also affect GString's public class declarations,
    # so consumers of the C++ headers (including our fixture encoder) need them.
    target_compile_definitions(djvu_decoder PUBLIC HAS_WCHAR=1 HAS_WCTYPE=1 HAS_MBSTATE=1
        HAVE_PTHREAD=1 HAVE_INTEL_ATOMIC_BUILTINS=1)
    target_compile_definitions(djvu_decoder PRIVATE UNIX=1
        HAVE_UNISTD_H=1 HAVE_SYS_MMAN_H=1 HAVE_GETPWUID=1 HAVE_MKSTEMP=1)
endif()
install(FILES "${djvulibre_SOURCE_DIR}/COPYING" DESTINATION licenses RENAME DjVuLibre-COPYING.txt)
# Include the exact decoder source used to build the distributed binary.
install(DIRECTORY "${djvulibre_SOURCE_DIR}/" DESTINATION sources/djvulibre
    PATTERN ".git" EXCLUDE)
