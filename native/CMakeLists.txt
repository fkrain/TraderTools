cmake_minimum_required(VERSION 3.20)
project(follow_native LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(WEBVIEW2_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/packages/Microsoft.Web.WebView2/build/native")
if(NOT EXISTS "${WEBVIEW2_ROOT}/include/WebView2.h")
  message(FATAL_ERROR "WebView2 SDK missing under third_party/packages. Re-download Microsoft.Web.WebView2.")
endif()

add_library(grid_core STATIC
  grid/src/json_io.cpp
  grid/src/http_win.cpp
  grid/src/quote_tencent.cpp
  grid/src/quote_factory.cpp
  grid/src/order_file.cpp
  grid/src/grid_engine.cpp
  grid/src/grid_backtest.cpp
  grid/src/expr.cpp
  grid/src/bars.cpp
)
target_include_directories(grid_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/grid/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
if(WIN32)
  target_compile_definitions(grid_core PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(grid_core PUBLIC winhttp)
endif()

add_executable(grid_engine grid/src/main.cpp)
target_link_libraries(grid_engine PRIVATE grid_core)

add_executable(grid_rules_test grid/src/grid_rules_test.cpp)
target_link_libraries(grid_rules_test PRIVATE grid_core)
if(MSVC)
  target_compile_options(grid_rules_test PRIVATE /utf-8)
endif()

add_executable(follow_desk WIN32
  desk/src/main.cpp
  desk/src/paths.cpp
  desk/src/app_state.cpp
  desk/src/settings.cpp
  desk/src/broker_status.cpp
  desk/src/portfolios.cpp
  desk/src/follow.cpp
  desk/src/http_api.cpp
  desk/src/webview_host.cpp
  desk/src/product.cpp
  desk/src/product_engine.cpp
  desk/src/desk_modules.cpp
  desk/src/workflow_engine.cpp
)
target_include_directories(follow_desk PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/desk/include
  ${CMAKE_CURRENT_SOURCE_DIR}/grid/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
  ${WEBVIEW2_ROOT}/include
)
target_compile_definitions(follow_desk PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
  _WIN32_WINNT=0x0A00
)
if(MSVC)
  target_compile_options(follow_desk PRIVATE /utf-8 /W3)
  target_compile_options(grid_core PRIVATE /utf-8)
endif()
if(CMAKE_GENERATOR_PLATFORM STREQUAL "x64" OR CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_WV2_ARCH x64)
elseif(CMAKE_GENERATOR_PLATFORM STREQUAL "ARM64")
  set(_WV2_ARCH arm64)
else()
  set(_WV2_ARCH x86)
endif()
message(STATUS "WebView2 arch: ${_WV2_ARCH}")
target_link_libraries(follow_desk PRIVATE
  grid_core
  ole32 oleaut32 uuid version user32 gdi32 shell32 advapi32
  "${WEBVIEW2_ROOT}/${_WV2_ARCH}/WebView2LoaderStatic.lib"
)

install(TARGETS follow_desk RUNTIME DESTINATION bin)
