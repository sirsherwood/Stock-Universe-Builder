cmake_minimum_required(VERSION 3.21)

project(StockUniverseBuilder VERSION 1.0.0 LANGUAGES CXX)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(UniverseBuilderCore
    main.cpp
    Asset_Pull/assetPull.cpp
    Data_Pull/AlpacaClient.cpp
    Data_Processing/DataProcessor.cpp
)

target_compile_features(UniverseBuilderCore PRIVATE cxx_std_17)
target_link_libraries(UniverseBuilderCore PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

if(WIN32)
    set_target_properties(UniverseBuilderCore PROPERTIES OUTPUT_NAME "UniverseBuilderCore")
    target_compile_definitions(UniverseBuilderCore PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )

    add_executable(StockUniverseBuilderGUI WIN32
        GUI/StockUniverseBuilderGui.cpp
    )
    target_compile_features(StockUniverseBuilderGUI PRIVATE cxx_std_17)
    target_compile_definitions(StockUniverseBuilderGUI PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
    )
    target_link_libraries(StockUniverseBuilderGUI PRIVATE comctl32 shell32 user32 gdi32)
    set_target_properties(StockUniverseBuilderGUI PROPERTIES
        OUTPUT_NAME "StockUniverseBuilder"
    )
else()
    # Preserve the existing Linux executable name and release workflow.
    set_target_properties(UniverseBuilderCore PROPERTIES OUTPUT_NAME "StockUniverseBuilder")
endif()

if(MSVC)
    target_compile_options(UniverseBuilderCore PRIVATE /W4 /permissive- /utf-8)
    target_compile_definitions(UniverseBuilderCore PRIVATE CURL_STATICLIB)
    set_property(TARGET UniverseBuilderCore PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(StockUniverseBuilderGUI PRIVATE /W4 /permissive- /utf-8)
    set_property(TARGET StockUniverseBuilderGUI PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(UniverseBuilderCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
