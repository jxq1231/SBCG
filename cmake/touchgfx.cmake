# User-owned U3.1 integration. CubeMX owns target sources; Designer owns the
# TouchGFX OBJECT target, frontend/font sources, engine archive and USE_BPP.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(_sbcg_touchgfx_inputs
    SBCG.ioc Core/Src/main.c Core/Src/stm32f7xx_hal_msp.c
    Core/Inc/main.h Core/Src/stm32f7xx_it.c Core/Inc/stm32f7xx_hal_conf.h
    TouchGFX/App/app_touchgfx.c TouchGFX/App/app_touchgfx.h
    TouchGFX/target/generated/TouchGFXConfiguration.cpp
    TouchGFX/target/generated/TouchGFXGeneratedHAL.cpp
    TouchGFX/target/generated/OSWrappers.cpp
    TouchGFX/target/generated/STM32DMA.cpp
    cmake/touchgfx/CMakeLists.txt
    App/UI/touchgfx_service.c cmake/touchgfx.cmake
    Tools/Monitor/check_touchgfx_config.py Tools/Monitor/check_ltdc_config.py)
foreach(_input IN LISTS _sbcg_touchgfx_inputs)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${_input}")
endforeach()
execute_process(COMMAND "${Python3_EXECUTABLE}" -B
    "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Monitor/check_touchgfx_config.py"
    --root "${CMAKE_CURRENT_SOURCE_DIR}" --allow-generated-process-call
    RESULT_VARIABLE _touchgfx_result OUTPUT_VARIABLE _touchgfx_output ERROR_VARIABLE _touchgfx_error)
if(NOT _touchgfx_result EQUAL 0)
    message(FATAL_ERROR "TouchGFX configuration failed preflight.\n${_touchgfx_output}${_touchgfx_error}")
endif()

# Designer's generated CMake refers to the project-local SDK. Check that exact
# installation; an external SDK fallback would not change the generated paths.
if(NOT TARGET TouchGFX)
    message(FATAL_ERROR "Missing Designer TouchGFX target. Generate the approved application so the root CMake includes cmake/touchgfx.")
endif()
get_target_property(_touchgfx_target_type TouchGFX TYPE)
if(NOT _touchgfx_target_type STREQUAL "OBJECT_LIBRARY")
    message(FATAL_ERROR "Designer TouchGFX target must be an OBJECT library.")
endif()
get_target_property(_touchgfx_defines stm32cubemx INTERFACE_COMPILE_DEFINITIONS)
if(NOT "USE_BPP=16" IN_LIST _touchgfx_defines)
    message(FATAL_ERROR "Designer framebuffer_bpp must be 16. Regenerate the application to produce USE_BPP=16 in cmake/touchgfx/CMakeLists.txt.")
endif()
set(_touchgfx_sdk "${CMAKE_CURRENT_SOURCE_DIR}/Middlewares/ST/touchgfx")
set(_touchgfx_library "${_touchgfx_sdk}/lib/core/cortex_m7/gcc/libtouchgfx-float-abi-hard.a")
if(NOT EXISTS "${_touchgfx_sdk}/framework/include/touchgfx/hal/HAL.hpp" OR NOT EXISTS "${_touchgfx_library}")
    message(FATAL_ERROR "Designer requires TouchGFX 4.26.1 installed at Middlewares/ST/touchgfx, including the Cortex-M7 GCC hard-float library. Install/regenerate the local SDK before building.")
endif()
set(_touchgfx_version_header "${_touchgfx_sdk}/framework/include/touchgfx/Version.hpp")
if(NOT EXISTS "${_touchgfx_version_header}")
    message(FATAL_ERROR "TouchGFX SDK version header is missing: ${_touchgfx_version_header}")
endif()
file(STRINGS "${_touchgfx_version_header}" _touchgfx_version REGEX "^#define TOUCHGFX_VERSION 42601$")
if(NOT _touchgfx_version)
    message(FATAL_ERROR "SBCG U3.1 requires TouchGFX SDK 4.26.1 (TOUCHGFX_VERSION 42601).")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_touchgfx_version_header}")
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    App/BSP/bsp_touchgfx_display.c
    App/UI/touchgfx_service.c App/UI/status_snapshot.c App/UI/status_view.c)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    SBCG_USE_LCD=1 SBCG_USE_TOUCHGFX=1)
foreach(_target IN ITEMS ${CMAKE_PROJECT_NAME} TouchGFX)
    set_property(TARGET ${_target} PROPERTY CXX_STANDARD 17)
    set_property(TARGET ${_target} PROPERTY CXX_STANDARD_REQUIRED ON)
endforeach()
# Both generated target CPP and Designer frontend CPP consume App interfaces.
target_include_directories(stm32cubemx INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/App")

# A CubeMX auto-process call may remain in generated main(). Route it to a
# no-op; only the user-owned scheduled service invokes the real entry point.
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--wrap=MX_TouchGFX_Process")

# Never allow the U2 poller/drawing stack to share LTDC or framebuffer ownership.
get_target_property(_touchgfx_registered_sources ${CMAKE_PROJECT_NAME} SOURCES)
foreach(_source IN LISTS _touchgfx_registered_sources)
    if(_source MATCHES "(^|/)(bsp_lcd|status_service|status_screen|rgb565_canvas)\\.c$")
        message(FATAL_ERROR "Legacy U2 display source registered with TouchGFX: ${_source}")
    endif()
endforeach()
