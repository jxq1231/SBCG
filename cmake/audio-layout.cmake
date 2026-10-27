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
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_AUDIO=1 SBCG_USE_SOUND_DETECTOR=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        App/Detector/sound_detector.c App/Detector/sound_detector_service.c
        App/Detector/toa_estimator.c)
    # CPU-owned copied PCM and bounded post-End search; no hardware changes.
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_CHIRP=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        App/Detector/chirp_matcher.c App/Detector/chirp_template.c
        App/Detector/chirp_window.c App/Detector/chirp_service.c)
    # Quiet R1 measurement with EVAL=0 showed 79 ms in the O0 gate/scan path.
    # Optimize the complete PCM hot path in Debug, including the shared gate;
    # preserve its tested integer semantics, no fast-math or hardware changes.
    set_source_files_properties(
        App/Detector/chirp_window.c App/Detector/chirp_service.c
        App/Detector/sound_detector.c PROPERTIES
        COMPILE_OPTIONS "$<$<CONFIG:Debug>:-O2>")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        App/BSP/bsp_audio.c App/Audio/audio_capture.c App/Audio/audio_statistics.c
        App/Audio/audio_frame_index.c)
    if(TARGET LwIP)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_SOUND_TIME=1)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE
            App/Detector/sound_event_time.c App/Detector/standard_sequence.c App/Detector/event_classifier.c App/Sync/time_domain.c
            App/Measure/sound_pair.c App/Measure/measurement_result.c App/Measure/measurement_gate.c App/Measure/ranging_tracker.c App/Measure/result_exchange_state.c App/Calibration/calibration.c
            App/Network/sound_event_wire.c App/Network/measurement_wire.c App/Network/measurement_ack.c App/Network/domain_wire.c App/Network/temperature_wire.c App/Network/ranging_status_wire.c App/Network/udp_sound_event.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_AUDIO_ANCHOR=1)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE App/Audio/audio_time_anchor.c)
        target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_AUDIO_MAP=1)
        target_sources(${CMAKE_PROJECT_NAME} PRIVATE
            App/Audio/audio_time_map.c App/Audio/audio_time_map_monitor.c)
    endif()
    set(_audio_script "${CMAKE_CURRENT_SOURCE_DIR}/cmake/linker/stm32f746_audio.ld")
    # Supplement the existing primary script, without editing CubeMX's script.
    # A distinct DMA region is independent of the generated RAM cursor.
    target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "LINKER:-T,${_audio_script}")
    set_property(TARGET ${CMAKE_PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${_audio_script}")
endif()
