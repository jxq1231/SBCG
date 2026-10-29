# User-owned SDRAM application registration; GPIO/RCC/MPU/HAL remain CubeMX-owned.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/SBCG.ioc" _sbcg_sdram
    REGEX "^FMC\\.CASLatency1=")
if(_sbcg_sdram)
    # Fail before compiling/flashing if the generated MPU blocks FMC registers.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/main.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Monitor/check_sdram_mpu.py")
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" -B "${CMAKE_CURRENT_SOURCE_DIR}/Tools/Monitor/check_sdram_mpu.py"
        RESULT_VARIABLE _sdram_mpu_result OUTPUT_VARIABLE _sdram_mpu_output ERROR_VARIABLE _sdram_mpu_error)
    if(NOT _sdram_mpu_result EQUAL 0)
        message(FATAL_ERROR "Generated SDRAM MPU configuration failed preflight; regenerate in CubeMX.\n${_sdram_mpu_output}${_sdram_mpu_error}")
    endif()
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SBCG_USE_SDRAM=1)
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        App/BSP/bsp_sdram.c App/BSP/sdram_test.c)
endif()
