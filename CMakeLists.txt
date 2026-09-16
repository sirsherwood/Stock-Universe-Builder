cmake_minimum_required(VERSION 3.21)

project(StockUniverseBuilder VERSION 1.0.0 LANGUAGES CXX)

find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(StockUniverseBuilder
    main.cpp
    Asset_Pull/assetPull.cpp
    Data_Pull/AlpacaClient.cpp
    Data_Processing/DataProcessor.cpp
)

target_compile_features(StockUniverseBuilder PRIVATE cxx_std_17)
target_link_libraries(StockUniverseBuilder PRIVATE CURL::libcurl nlohmann_json::nlohmann_json)

if(MSVC)
    target_compile_options(StockUniverseBuilder PRIVATE /W4 /permissive-)
    target_compile_definitions(StockUniverseBuilder PRIVATE CURL_STATICLIB)
    set_property(TARGET StockUniverseBuilder PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(StockUniverseBuilder PRIVATE -Wall -Wextra -Wpedantic)
endif()
