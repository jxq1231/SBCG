# User-owned STM32F746 ETH memory integration. Do not move into generated CMake.
set(_sbcg_ioc "${CMAKE_CURRENT_SOURCE_DIR}/SBCG.ioc")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sbcg_ioc}")
file(STRINGS "${_sbcg_ioc}" _sbcg_eth_mode REGEX "^ETH\\.MediaInterface=")

# Older project snapshots without ETH continue using the original CubeMX script.
if(_sbcg_eth_mode)
    if(NOT _sbcg_eth_mode STREQUAL "ETH.MediaInterface=HAL_ETH_RMII_MODE")
        message(FATAL_ERROR "SBCG ETH layout supports the reviewed RMII configuration only")
    endif()
    foreach(_sbcg_direction IN ITEMS Tx Rx)
        file(STRINGS "${_sbcg_ioc}" _sbcg_address
            REGEX "^ETH\\.${_sbcg_direction}DescAddress=")
        string(TOLOWER "${_sbcg_address}" _sbcg_address)
        if(_sbcg_direction STREQUAL "Tx")
            set(_sbcg_expected "eth.txdescaddress=0x2004c000")
        else()
            set(_sbcg_expected "eth.rxdescaddress=0x2004c0a0")
        endif()
        if(NOT _sbcg_address STREQUAL _sbcg_expected)
            message(FATAL_ERROR
                "ETH ${_sbcg_direction} address differs from reviewed linker layout. "
                "Regenerate in CubeMX and review cmake/linker/stm32f746_eth.ld together.")
        endif()
    endforeach()

    # A fixed LwIP heap is invisible to the normal .bss/newlib heap accounting.
    # Reserve its full 16 KiB window and move MSP below it when LwIP is enabled.
    file(STRINGS "${_sbcg_ioc}" _sbcg_lwip REGEX "^LWIP\\.Version=")
    if(_sbcg_lwip)
        set(_sbcg_lwipopts "${CMAKE_CURRENT_SOURCE_DIR}/LWIP/Target/lwipopts.h")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_sbcg_lwipopts}")
        file(STRINGS "${_sbcg_lwipopts}" _sbcg_heap_address
            REGEX "^#define[ \t]+LWIP_RAM_HEAP_POINTER[ \t]+")
        file(STRINGS "${_sbcg_lwipopts}" _sbcg_heap_size
            REGEX "^#define[ \t]+MEM_SIZE[ \t]+")
        if(NOT _sbcg_heap_address MATCHES "^#define[ \t]+LWIP_RAM_HEAP_POINTER[ \t]+0x20048000$"
           OR NOT _sbcg_heap_size MATCHES "^#define[ \t]+MEM_SIZE[ \t]+4096$")
            message(FATAL_ERROR "LwIP heap differs from reviewed 0x20048000/4096 layout")
        endif()
        target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:--defsym=__sbcg_has_lwip=1")
        set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${_sbcg_lwipopts}")
    endif()

    set(_sbcg_base_script "${CMAKE_CURRENT_SOURCE_DIR}/STM32F746xx_FLASH.ld")
    set(_sbcg_eth_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/linker/stm32f746_eth.ld")
    set(_sbcg_base_option "-T \"${_sbcg_base_script}\"")
    string(FIND "${CMAKE_EXE_LINKER_FLAGS}" "${_sbcg_base_option}" _sbcg_script_index)
    if(_sbcg_script_index EQUAL -1)
        message(FATAL_ERROR "CubeMX linker flags changed; review SBCG ETH wrapper integration")
    endif()
    # Select one top-level script. The wrapper INCLUDEs the unchanged base script.
    string(REPLACE "${_sbcg_base_option}" "-T \"${_sbcg_eth_script}\""
        CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    # GNU ld resolves INCLUDE through its -L search path, including paths with spaces.
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:-L,${CMAKE_CURRENT_SOURCE_DIR}")
    set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
        "${_sbcg_base_script}" "${_sbcg_eth_script}" "${_sbcg_ioc}")
endif()
