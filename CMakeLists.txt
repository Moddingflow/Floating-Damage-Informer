cmake_minimum_required(VERSION 3.28)

project(
    FloatingDamageNumbersNG
    VERSION 0.2.0
    LANGUAGES CXX
)

option(FDN_BUILD_SKSE_PLUGIN "Build the SKSE plugin DLL" ON)
option(FDN_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

set(FDN_PROJECT_VERSION_TWEAK "${PROJECT_VERSION_TWEAK}")
if(FDN_PROJECT_VERSION_TWEAK STREQUAL "")
    set(FDN_PROJECT_VERSION_TWEAK 0)
endif()

set(FDN_COMMONLIBSSE_NG_VERSION "7.2.0")
set(FDN_COMMONLIBSSE_NG_COMMIT "7a60f4de794095d7b0f8928d1b930a52e9a7da83")
set(FDN_OPENVR_COMMIT "60eb187801956ad277f1cae6680e3a410ee0873b")

# Keep the runtime declaration explicit. This prevents SKSE from loading the
# plugin on an unknown future executable whose layouts have not been reviewed.
# SKSE's declaration format accepts at most 16 entries. Epic runtime 1.6.678
# is deliberately excluded because SKSE does not support the Epic release.
set(FDN_COMPATIBLE_RUNTIMES
    1.4.15.0    # VR
    1.5.97.0    # SE
    1.6.317.0
    1.6.318.0
    1.6.323.0
    1.6.342.0
    1.6.353.0
    1.6.629.0
    1.6.640.0
    1.6.659.0   # GOG
    1.6.1130.0
    1.6.1170.0
    1.6.1179.0  # GOG
    1.7.99.0
    1.7.104.0
)

list(LENGTH FDN_COMPATIBLE_RUNTIMES FDN_COMPATIBLE_RUNTIME_COUNT)
if(NOT FDN_COMPATIBLE_RUNTIME_COUNT EQUAL 15)
    message(FATAL_ERROR "FDN_COMPATIBLE_RUNTIMES must contain exactly 15 entries")
endif()

set(FDN_CORE_HEADERS
    include/FDN/Animation.h
    include/FDN/DamageAggregator.h
    include/FDN/FloatingNumberManager.h
    include/FDN/Settings.h
    include/FDN/Types.h
)

set(FDN_CORE_SOURCES
    src/Animation.cpp
    src/DamageAggregator.cpp
    src/FloatingNumberManager.cpp
    src/Settings.cpp
)

add_library(FDNCore STATIC ${FDN_CORE_HEADERS} ${FDN_CORE_SOURCES})
target_include_directories(FDNCore PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_features(FDNCore PUBLIC cxx_std_23)

if(MSVC)
    target_compile_options(FDNCore PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus)
else()
    target_compile_options(FDNCore PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(FDN_BUILD_SKSE_PLUGIN)
    include(FetchContent)
    message(STATUS "Using CommonLibSSE-NG ${FDN_COMMONLIBSSE_NG_VERSION} (${FDN_COMMONLIBSSE_NG_COMMIT})")

    # Skyrim 1.7.x uses Address Library format 5 and changed several runtime
    # layouts. Pin the first current multi-runtime release that contains the
    # complete 1.7.x support instead of relying on the obsolete registry port.
    set(BUILD_TESTS OFF CACHE BOOL "Disable dependency tests" FORCE)
    set(ENABLE_SKYRIM_SE ON CACHE BOOL "Build CommonLibSSE-NG with SE support" FORCE)
    set(ENABLE_SKYRIM_AE ON CACHE BOOL "Build CommonLibSSE-NG with AE support" FORCE)
    set(ENABLE_SKYRIM_VR ON CACHE BOOL "Build CommonLibSSE-NG with VR support" FORCE)
    set(COMMONLIB_PREBUILT ON CACHE BOOL "Use the verified CommonLibSSE-NG release bundle when compatible")
    set(COMMONLIB_PREBUILT_MULTICONFIG ON CACHE BOOL "Allow the Release bundle with Visual Studio generators" FORCE)

    FetchContent_Declare(
        CommonLibSSE_NG
        GIT_REPOSITORY https://github.com/alandtse/CommonLibSSE-NG.git
        GIT_TAG ${FDN_COMMONLIBSSE_NG_COMMIT}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        # The verified prebuilt bundle already contains OpenVR headers and the
        # import library. Avoid cloning the full OpenVR history during configure.
        GIT_SUBMODULES ""
    )
    FetchContent_MakeAvailable(CommonLibSSE_NG)
    FetchContent_GetProperties(CommonLibSSE_NG SOURCE_DIR FDN_COMMONLIBSSE_NG_SOURCE_DIR)

    # The release bundle already contains these OpenVR files, while a source
    # fallback needs them at build time. Fetch only the pinned files instead of
    # cloning the full OpenVR history; EXPECTED_HASH also reuses valid files.
    set(FDN_OPENVR_FILES
        LICENSE
        headers/openvr.h
        lib/win64/openvr_api.lib
    )
    set(FDN_OPENVR_SHA256
        f56ff606104d4ef18e617921a75c73ad73b5a1a1d70c69590c29de16919e04ad
        0ffc25d09ddb50c78a56f15cc48fc724c55d1f4d06fd2ca009c9f985158e7f8c
        b92394fb290d9a99cae64af5ef5a71bf723dea4640f513acb9e43eeaa86cbcf5
    )
    set(FDN_OPENVR_CACHE_ROOT "${PROJECT_BINARY_DIR}/_deps/openvr-${FDN_OPENVR_COMMIT}")
    list(LENGTH FDN_OPENVR_FILES FDN_OPENVR_FILE_COUNT)
    math(EXPR FDN_OPENVR_LAST_INDEX "${FDN_OPENVR_FILE_COUNT} - 1")
    foreach(FDN_OPENVR_INDEX RANGE ${FDN_OPENVR_LAST_INDEX})
        list(GET FDN_OPENVR_FILES ${FDN_OPENVR_INDEX} FDN_OPENVR_FILE)
        list(GET FDN_OPENVR_SHA256 ${FDN_OPENVR_INDEX} FDN_OPENVR_FILE_SHA256)
        set(FDN_OPENVR_CACHE_FILE "${FDN_OPENVR_CACHE_ROOT}/${FDN_OPENVR_FILE}")
        get_filename_component(FDN_OPENVR_CACHE_DIR "${FDN_OPENVR_CACHE_FILE}" DIRECTORY)
        file(MAKE_DIRECTORY "${FDN_OPENVR_CACHE_DIR}")
        file(DOWNLOAD
            "https://raw.githubusercontent.com/ValveSoftware/openvr/${FDN_OPENVR_COMMIT}/${FDN_OPENVR_FILE}"
            "${FDN_OPENVR_CACHE_FILE}"
            EXPECTED_HASH "SHA256=${FDN_OPENVR_FILE_SHA256}"
            TLS_VERIFY ON
            TIMEOUT 120
            INACTIVITY_TIMEOUT 30
            STATUS FDN_OPENVR_DOWNLOAD_STATUS
        )
        list(GET FDN_OPENVR_DOWNLOAD_STATUS 0 FDN_OPENVR_DOWNLOAD_CODE)
        if(NOT FDN_OPENVR_DOWNLOAD_CODE EQUAL 0)
            list(GET FDN_OPENVR_DOWNLOAD_STATUS 1 FDN_OPENVR_DOWNLOAD_ERROR)
            message(FATAL_ERROR "Failed to fetch pinned OpenVR file ${FDN_OPENVR_FILE}: ${FDN_OPENVR_DOWNLOAD_ERROR}")
        endif()

        set(FDN_OPENVR_SOURCE_FILE "${FDN_COMMONLIBSSE_NG_SOURCE_DIR}/extern/openvr/${FDN_OPENVR_FILE}")
        get_filename_component(FDN_OPENVR_SOURCE_DIR "${FDN_OPENVR_SOURCE_FILE}" DIRECTORY)
        file(MAKE_DIRECTORY "${FDN_OPENVR_SOURCE_DIR}")
        file(COPY_FILE "${FDN_OPENVR_CACHE_FILE}" "${FDN_OPENVR_SOURCE_FILE}" ONLY_IF_DIFFERENT)
    endforeach()

    if(NOT COMMAND add_commonlibsse_plugin)
        include("${FDN_COMMONLIBSSE_NG_SOURCE_DIR}/cmake/CommonLibSSE.cmake")
    endif()

    find_package(directxtk CONFIG REQUIRED)
    find_package(spdlog REQUIRED)

    set(FDN_PLUGIN_SOURCES
        src/Plugin.cpp
        src/Skyrim/DamageEventManager.cpp
        src/Skyrim/HUDHooks.cpp
        src/Skyrim/Runtime.cpp
        src/Skyrim/ScaleformRenderer.cpp
        src/Skyrim/SkyrimRuntime.cpp
        src/Skyrim/WorldToScreen.cpp
    )

    add_commonlibsse_plugin(${PROJECT_NAME}
        AUTHOR "TheWhistle"
        VERSION ${PROJECT_VERSION}
        COMPATIBLE_RUNTIMES ${FDN_COMPATIBLE_RUNTIMES}
        SOURCES ${FDN_PLUGIN_SOURCES}
    )

    target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(${PROJECT_NAME} PRIVATE FDNCore spdlog::spdlog)
    target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus)
    endif()

    target_precompile_headers(${PROJECT_NAME} PRIVATE include/FDN/Skyrim/CommonLibPCH.h)

    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            FDN_PLUGIN_NAME="${PROJECT_NAME}"
            FDN_PLUGIN_VERSION="${PROJECT_VERSION}"
    )

    set(FDN_PACKAGE_ROOT "${PROJECT_BINARY_DIR}/package" CACHE PATH "Package output root")
    set(FDN_PACKAGE_DIR "${FDN_PACKAGE_ROOT}/SKSE/Plugins" CACHE PATH "Package output directory for SKSE plugin files")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FDN_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FDN_PACKAGE_ROOT}/licenses"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${FDN_PACKAGE_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/package/SKSE/Plugins/FloatingDamageNumbersNG.ini" "${FDN_PACKAGE_DIR}/FloatingDamageNumbersNG.ini"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/LICENSE" "${FDN_PACKAGE_ROOT}/LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/CREDITS.md" "${FDN_PACKAGE_ROOT}/CREDITS.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md" "${FDN_PACKAGE_ROOT}/THIRD_PARTY_NOTICES.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FDN_COMMONLIBSSE_NG_SOURCE_DIR}/COPYING" "${FDN_PACKAGE_ROOT}/licenses/CommonLibSSE-NG-COPYING.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FDN_COMMONLIBSSE_NG_SOURCE_DIR}/EXCEPTIONS.md" "${FDN_PACKAGE_ROOT}/licenses/CommonLibSSE-NG-EXCEPTIONS.md"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FDN_COMMONLIBSSE_NG_SOURCE_DIR}/extern/openvr/LICENSE" "${FDN_PACKAGE_ROOT}/licenses/OpenVR-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/directxmath/copyright" "${FDN_PACKAGE_ROOT}/licenses/DirectXMath-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/directxtk/copyright" "${FDN_PACKAGE_ROOT}/licenses/DirectXTK-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/fmt/copyright" "${FDN_PACKAGE_ROOT}/licenses/fmt-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/rapidcsv/copyright" "${FDN_PACKAGE_ROOT}/licenses/rapidcsv-LICENSE.txt"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/spdlog/copyright" "${FDN_PACKAGE_ROOT}/licenses/spdlog-LICENSE.txt"
        COMMENT "Packaging SKSE plugin files"
    )
endif()

if(FDN_BUILD_TESTS)
    enable_testing()
    add_executable(FDNCoreTests
        tests/TestMain.cpp
    )
    target_link_libraries(FDNCoreTests PRIVATE FDNCore)
    target_compile_features(FDNCoreTests PRIVATE cxx_std_23)
    if(MSVC)
        target_compile_options(FDNCoreTests PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus)
    else()
        target_compile_options(FDNCoreTests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME FDNCoreTests COMMAND FDNCoreTests)

    if(FDN_BUILD_SKSE_PLUGIN AND WIN32)
        add_executable(FDNPluginMetadataTests
            tests/PluginMetadataTest.cpp
        )
        target_compile_features(FDNPluginMetadataTests PRIVATE cxx_std_23)
        target_compile_definitions(FDNPluginMetadataTests PRIVATE
            FDN_EXPECTED_PLUGIN_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
            FDN_EXPECTED_PLUGIN_VERSION_MINOR=${PROJECT_VERSION_MINOR}
            FDN_EXPECTED_PLUGIN_VERSION_PATCH=${PROJECT_VERSION_PATCH}
            FDN_EXPECTED_PLUGIN_VERSION_TWEAK=${FDN_PROJECT_VERSION_TWEAK}
        )
        add_dependencies(FDNPluginMetadataTests ${PROJECT_NAME})
        if(MSVC)
            target_compile_options(FDNPluginMetadataTests PRIVATE /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus)
        endif()
        add_test(
            NAME FDNPluginMetadataTests
            COMMAND FDNPluginMetadataTests "$<TARGET_FILE:${PROJECT_NAME}>"
        )
    endif()
endif()
