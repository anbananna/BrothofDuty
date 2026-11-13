# The following variables contains the files used by the different stages of the build process.
set(AdamBOD_default_default_XC32_FILE_TYPE_assemble)
set_source_files_properties(${AdamBOD_default_default_XC32_FILE_TYPE_assemble} PROPERTIES LANGUAGE ASM)

# For assembly files, add "." to the include path for each file so that .include with a relative path works
foreach(source_file ${AdamBOD_default_default_XC32_FILE_TYPE_assemble})
        set_source_files_properties(${source_file} PROPERTIES INCLUDE_DIRECTORIES "$<PATH:NORMAL_PATH,$<PATH:REMOVE_FILENAME,${source_file}>>")
endforeach()

set(AdamBOD_default_default_XC32_FILE_TYPE_assembleWithPreprocess)
set_source_files_properties(${AdamBOD_default_default_XC32_FILE_TYPE_assembleWithPreprocess} PROPERTIES LANGUAGE ASM)

# For assembly files, add "." to the include path for each file so that .include with a relative path works
foreach(source_file ${AdamBOD_default_default_XC32_FILE_TYPE_assembleWithPreprocess})
        set_source_files_properties(${source_file} PROPERTIES INCLUDE_DIRECTORIES "$<PATH:NORMAL_PATH,$<PATH:REMOVE_FILENAME,${source_file}>>")
endforeach()

set(AdamBOD_default_default_XC32_FILE_TYPE_compile
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/DM_DisplayStarter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/ES_CheckEvents.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/ES_DeferRecall.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/ES_Framework.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/ES_LookupTables.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/ES_Port.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/ES_PostList.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/ES_Queue.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/ES_Timers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/FontStuff.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/LEDService.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/NeoPixelService.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/PIC32_AD_Lib.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/PIC32_SPI_HAL_Starter.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/PWM_PIC32.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/circular_buffer_no_modulo_threadsafe.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/dbprintf.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../FrameworkSource/terminal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectSource/EventCheckers.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectSource/GameService.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectSource/InputService.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectSource/ServoService.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectSource/TestHarnessService0.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectSource/main.c")
set_source_files_properties(${AdamBOD_default_default_XC32_FILE_TYPE_compile} PROPERTIES LANGUAGE C)
set(AdamBOD_default_default_XC32_FILE_TYPE_compile_cpp)
set_source_files_properties(${AdamBOD_default_default_XC32_FILE_TYPE_compile_cpp} PROPERTIES LANGUAGE CXX)
set(AdamBOD_default_default_XC32_FILE_TYPE_link)
set(AdamBOD_default_image_name "default.elf")
set(AdamBOD_default_image_base_name "default")

# The output directory of the final image.
set(AdamBOD_default_output_dir "${CMAKE_CURRENT_SOURCE_DIR}/../../../out/AdamBOD")

# The full path to the final image.
set(AdamBOD_default_full_path_to_image ${AdamBOD_default_output_dir}/${AdamBOD_default_image_name})
