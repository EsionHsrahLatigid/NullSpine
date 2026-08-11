cmake_minimum_required(VERSION 3.31)

if(APPLE)
    project(NullSpine VERSION 0.1.0 LANGUAGES C CXX OBJC OBJCXX)
else()
    project(NullSpine VERSION 0.1.0 LANGUAGES C CXX)
endif()

option(NULLSPINE_BUILD_PLUGIN "Build the YUP Standalone/VST3/AU targets" ON)
option(NULLSPINE_BUILD_TESTS "Build deterministic DSP regression tests" ON)
option(NULLSPINE_ADHOC_SIGN "Ad-hoc sign local macOS plugin and app bundles after building" ON)
set(NULLSPINE_CREATE_AU OFF)
if(APPLE)
    set(NULLSPINE_CREATE_AU ON)
endif()
set(NULLSPINE_YUP_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../yup" CACHE PATH
    "Path to a local YUP checkout; fetched from upstream when unavailable")

add_library(nullspine_engine STATIC
    source/NullSpineEngine.cpp)
target_include_directories(nullspine_engine PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/include")
target_compile_features(nullspine_engine PUBLIC cxx_std_20)
set_target_properties(nullspine_engine PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(nullspine_engine PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NULLSPINE_BUILD_TESTS)
    include(CTest)

    function(enable_test_assertions target_name)
        if(MSVC)
            target_compile_options(${target_name} PRIVATE /UNDEBUG)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(${target_name} PRIVATE -UNDEBUG)
        endif()
    endfunction()

    add_executable(nullspine_engine_tests
        tests/NullSpineEngineTests.cpp)
    target_link_libraries(nullspine_engine_tests PRIVATE nullspine_engine)
    target_compile_features(nullspine_engine_tests PRIVATE cxx_std_20)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(nullspine_engine_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    enable_test_assertions(nullspine_engine_tests)
    add_test(NAME nullspine_engine_tests COMMAND nullspine_engine_tests)
endif()

if(NULLSPINE_BUILD_PLUGIN)
    set(YUP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_PLUGINS OFF CACHE BOOL "" FORCE)
    set(YUP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_PLUGINVAL OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_VST3_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_CLAP_VALIDATOR OFF CACHE BOOL "" FORCE)
    set(YUP_ENABLE_AUVAL_VALIDATOR OFF CACHE BOOL "" FORCE)

    if(EXISTS "${NULLSPINE_YUP_SOURCE_DIR}/CMakeLists.txt")
        add_subdirectory("${NULLSPINE_YUP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/yup")
    else()
        include(FetchContent)
        FetchContent_Declare(yup
            GIT_REPOSITORY https://github.com/kunitoki/yup.git
            GIT_TAG 9a1c9bc699b6a714f6f52486462d98a140c8bf95)
        FetchContent_MakeAvailable(yup)
    endif()

    # YUP's iconutil-based helper rejects its generated iconset on current
    # macOS. Keep the compatibility fix project-local until upstream changes.
    if(APPLE)
        include(cmake/YupMacOSIconWorkaround.cmake)
    endif()

    yup_audio_plugin(
        TARGET_NAME nullspine
        TARGET_VERSION ${PROJECT_VERSION}
        TARGET_IDE_GROUP "NullSpine"
        TARGET_APP_ID "jp.ehl.nullspine"
        TARGET_APP_NAMESPACE "jp.ehl"
        TARGET_CXX_STANDARD 20
        PLUGIN_ID "jp.ehl.nullspine"
        PLUGIN_NAME "NullSpine"
        PLUGIN_VENDOR "ehl_"
        PLUGIN_EMAIL ""
        PLUGIN_VERSION "${PROJECT_VERSION}"
        PLUGIN_DESCRIPTION "MIDI-triggered impulse-to-resonator spine synth"
        PLUGIN_URL "https://github.com/EsionHsrahLatigid/NullSpine"
        PLUGIN_IS_SYNTH ON
        PLUGIN_IS_MONO OFF
        PLUGIN_CREATE_CLAP OFF
        PLUGIN_CREATE_VST3 ON
        PLUGIN_CREATE_AU ${NULLSPINE_CREATE_AU}
        PLUGIN_AU_SUBTYPE "NlSp"
        PLUGIN_AU_MANUFACTURER "EHL1"
        PLUGIN_AU_SANDBOX_SAFE ON
        PLUGIN_CREATE_AUv3 OFF
        PLUGIN_CREATE_LV2 OFF
        PLUGIN_CREATE_AAX OFF
        PLUGIN_CREATE_STANDALONE ON
        PLUGIN_COPY_AFTER_BUILD OFF
        MODULES
            yup::yup_gui
            yup::yup_audio_processors)

    target_sources(nullspine_shared INTERFACE
        source/ParameterGridEditor.cpp
        source/ParameterGridEditor.h
        source/ProductState.h
        source/NullSpinePlugin.cpp
        source/NullSpinePlugin.h)
    target_link_libraries(nullspine_shared INTERFACE nullspine_engine)
    target_include_directories(nullspine_shared INTERFACE
        "${CMAKE_CURRENT_LIST_DIR}/include"
        "${CMAKE_CURRENT_LIST_DIR}/source")

    if(NULLSPINE_BUILD_TESTS)
        add_executable(nullspine_plugin_bridge_tests
            tests/NullSpinePluginBridgeTests.cpp
            source/NullSpinePlugin.cpp)
        target_link_libraries(nullspine_plugin_bridge_tests PRIVATE nullspine_engine yup::yup_audio_processors)
        target_include_directories(nullspine_plugin_bridge_tests PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/include"
            "${CMAKE_CURRENT_LIST_DIR}/source")
        target_compile_definitions(nullspine_plugin_bridge_tests PRIVATE
            NULLSPINE_HEADLESS_TEST=1
            NULLSPINE_SOURCE_DIR="${CMAKE_CURRENT_LIST_DIR}")
        target_compile_features(nullspine_plugin_bridge_tests PRIVATE cxx_std_20)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
            target_compile_options(nullspine_plugin_bridge_tests PRIVATE -Wall -Wextra -Wpedantic)
        endif()
        enable_test_assertions(nullspine_plugin_bridge_tests)
        add_test(NAME nullspine_plugin_bridge_tests COMMAND nullspine_plugin_bridge_tests)
    endif()

    set(nullspine_release_bundle_targets
        nullspine_standalone_plugin
        nullspine_vst3_plugin)
    if(APPLE)
        list(APPEND nullspine_release_bundle_targets nullspine_au_plugin)
    endif()

    add_custom_target(nullspine_release_bundles
        DEPENDS ${nullspine_release_bundle_targets})

    if(APPLE)
        set_target_properties(nullspine_standalone_plugin PROPERTIES
            MACOSX_BUNDLE_GUI_IDENTIFIER "jp.ehl.nullspine"
            MACOSX_BUNDLE_BUNDLE_NAME "NullSpine"
            XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "jp.ehl.nullspine")
    endif()

    if(APPLE AND NULLSPINE_ADHOC_SIGN)
        find_program(NULLSPINE_CODESIGN_EXECUTABLE codesign REQUIRED)
        add_custom_command(TARGET nullspine_vst3_plugin POST_BUILD
            COMMAND "${NULLSPINE_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:nullspine_vst3_plugin>"
            COMMENT "Ad-hoc signing NullSpine VST3 for local loading"
            VERBATIM)
        add_custom_command(TARGET nullspine_standalone_plugin POST_BUILD
            COMMAND "${NULLSPINE_CODESIGN_EXECUTABLE}" --force --deep --sign -
                    "$<TARGET_BUNDLE_DIR:nullspine_standalone_plugin>"
            COMMENT "Ad-hoc signing NullSpine standalone app for local launch"
            VERBATIM)
    endif()
endif()
