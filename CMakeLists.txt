cmake_minimum_required(VERSION 3.28)

project(
    FloatingDamageNumbersNG
    VERSION 0.1.0
    LANGUAGES CXX
)

option(FDN_BUILD_SKSE_PLUGIN "Build the SKSE plugin DLL" ON)
option(FDN_BUILD_TESTS "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

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
    find_package(CommonLibSSE REQUIRED)
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
        SOURCES ${FDN_PLUGIN_SOURCES}
    )

    target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/include)
    target_link_libraries(${PROJECT_NAME} PRIVATE FDNCore spdlog::spdlog)
    target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)

    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /FIFDN/Skyrim/CommonLibPCH.h /W4 /permissive- /Zc:preprocessor /Zc:__cplusplus)
    endif()

    target_compile_definitions(${PROJECT_NAME}
        PRIVATE
            FDN_PLUGIN_NAME="${PROJECT_NAME}"
            FDN_PLUGIN_VERSION="${PROJECT_VERSION}"
    )

    set(FDN_PACKAGE_DIR "${PROJECT_BINARY_DIR}/package/SKSE/Plugins" CACHE PATH "Package output directory for SKSE plugin files")
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${FDN_PACKAGE_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${FDN_PACKAGE_DIR}/$<TARGET_FILE_NAME:${PROJECT_NAME}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PROJECT_SOURCE_DIR}/package/SKSE/Plugins/FloatingDamageNumbersNG.ini" "${FDN_PACKAGE_DIR}/FloatingDamageNumbersNG.ini"
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
endif()
