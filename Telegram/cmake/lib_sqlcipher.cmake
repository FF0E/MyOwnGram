# This file is part of Telegram Desktop,
# the official desktop application for the Telegram messaging service.
#
# For license and copyright information please follow this link:
# https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

if (LINUX OR DESKTOP_APP_USE_PACKAGED)
    find_package(PkgConfig REQUIRED)
    # The package version is the bundled SQLite version.
    pkg_check_modules(SQLCIPHER REQUIRED IMPORTED_TARGET sqlcipher>=3.26)
    add_library(lib_sqlcipher INTERFACE)
    target_link_libraries(lib_sqlcipher
    INTERFACE
        PkgConfig::SQLCIPHER
        desktop-app::external_openssl
    )
    target_compile_definitions(lib_sqlcipher INTERFACE SQLITE_HAS_CODEC)
else()
    add_library(lib_sqlcipher STATIC)
    init_target(lib_sqlcipher "(external)")
    target_sources(lib_sqlcipher PRIVATE ${libs_loc}/sqlcipher/sqlite3.c)
    target_include_directories(lib_sqlcipher SYSTEM PUBLIC ${libs_loc}/sqlcipher)
    target_compile_definitions(lib_sqlcipher
    PUBLIC
        SQLITE_HAS_CODEC
    PRIVATE
        SQLITE_TEMP_STORE=3
        SQLITE_THREADSAFE=2
        SQLITE_ENABLE_FTS5
        SQLITE_OMIT_LOAD_EXTENSION
        SQLITE_EXTRA_INIT=sqlcipher_extra_init
        SQLITE_EXTRA_SHUTDOWN=sqlcipher_extra_shutdown
        SQLCIPHER_CRYPTO_OPENSSL
    )
    target_link_libraries(lib_sqlcipher PRIVATE desktop-app::external_openssl)
    if (MSVC)
        add_library(lib_sqlcipher_warnings_off INTERFACE)
        target_compile_options(lib_sqlcipher_warnings_off INTERFACE /W0 /WX-)
        target_link_libraries(lib_sqlcipher PRIVATE lib_sqlcipher_warnings_off)
    else()
        target_compile_options(lib_sqlcipher PRIVATE -w)
    endif()
endif()

add_library(tdesktop::lib_sqlcipher ALIAS lib_sqlcipher)
