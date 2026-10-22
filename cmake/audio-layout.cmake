# User-owned integration. CubeMX still owns SAI/DMA/clock initialization.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/SBCG.ioc" _sbcg_audio_mode
    REGEX "^SAI2\\.AudioMode-SAI_A_MasterWithClock=")
if(_sbcg_audio_mode)
    foreach(_block IN ITEMS SAI_A_MasterWithClock SAI_B_SyncSlave)
        file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/SBCG.ioc" _slots
            REGEX "^SAI2\\.SlotActive-${_block}=")
        if(NOT _slots STREQUAL "SAI2.SlotActive-${_block}=0x0000000A")
            message(FATAL_ERROR "Audio requires reviewed slots 1/3 in both SAI blocks; use CubeMX")
        endif()
    endforeach()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_AUDIO=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        App/BSP/bsp_audio.c App/Audio/audio_capture.c App/Audio/audio_statistics.c
        App/Audio/audio_frame_index.c)
    if(TARGET LwIP)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_AUDIO_ANCHOR=1)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Audio/audio_time_anchor.c)
    endif()
    set(_audio_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/linker/stm32f746_audio.ld")
    # Supplement the existing primary script, without editing CubeMX's script.
    # A distinct DMA region is independent of the generated RAM cursor.
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:-T,${_audio_script}")
    set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${_audio_script}")
endif()
