cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME SBCG)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# User-owned linker extension; CubeMX-generated linker/toolchain stay intact.
include(cmake/ethernet-layout.cmake)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Add touchgfx generated sources and libraries
add_subdirectory(cmake/touchgfx)

# Keep the ETH handle layout consistent across application, HAL and LwIP.
target_compile_definitions(stm32cubemx INTERFACE HAL_ETH_USE_PTP)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
    App/app_main.c
    App/BSP/bsp_console.c
    App/BSP/bsp_reset.c
    App/BSP/bsp_ethernet.c
    App/Network/ethernet_monitor.c
    App/Sync/sync_estimator.c
    App/BSP/bsp_dht11.c
    App/Temperature/dht11_frame.c
    App/Temperature/sound_speed.c
    App/Temperature/temperature_service.c
    App/Console/temperature_console.c
    App/BSP/bsp_audio_codec.c
    App/Audio/codec_monitor.c
)

include(cmake/audio-layout.cmake)
include(cmake/sdram.cmake)
include(cmake/lcd.cmake)

# LwIP generation registers LAN8742 in STM32_Drivers. Older PHY-only
# snapshots still need the user source entry, but never compile both copies.
if(TARGET LwIP)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_LWIP=1)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_BOARD_IDENTITY=1)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_BOARD_SYNC=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        App/Network/ptp_wire.c App/Network/ptp_board_sync.c
        App/Sync/clock_servo.c App/Sync/time_sync.c)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        App/BSP/board_identity.c App/BSP/bsp_board_identity.c)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Network/udp_heartbeat.c)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Network/udp_probe.c)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Network/udp_time_exchange.c)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/BSP/bsp_ptp.c App/Sync/ptp_clock.c)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Sync/ptp_rx.c)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Sync/sync_observation.c)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/BSP/bsp_ptp_tx.c App/Sync/ptp_tx.c)
else()
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE Drivers/BSP/Components/lan8742/lan8742.c)
endif()

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    App
    Drivers/BSP/Components/lan8742
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    SBCG_USE_RESET_DIAGNOSTIC=1
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
