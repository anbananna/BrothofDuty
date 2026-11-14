include("${CMAKE_CURRENT_LIST_DIR}/rule.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/file.cmake")

set(AdamBOD_default_library_list )

set(OUTPUT_SUFFIX elf)
# Handle files with suffix s, for group default-XC32
if(AdamBOD_default_default_XC32_FILE_TYPE_assemble)
add_library(AdamBOD_default_default_XC32_assemble OBJECT ${AdamBOD_default_default_XC32_FILE_TYPE_assemble})
    AdamBOD_default_default_XC32_assemble_rule(AdamBOD_default_default_XC32_assemble)
    list(APPEND AdamBOD_default_library_list "$<TARGET_OBJECTS:AdamBOD_default_default_XC32_assemble>")

endif()

# Handle files with suffix S, for group default-XC32
if(AdamBOD_default_default_XC32_FILE_TYPE_assembleWithPreprocess)
add_library(AdamBOD_default_default_XC32_assembleWithPreprocess OBJECT ${AdamBOD_default_default_XC32_FILE_TYPE_assembleWithPreprocess})
    AdamBOD_default_default_XC32_assembleWithPreprocess_rule(AdamBOD_default_default_XC32_assembleWithPreprocess)
    list(APPEND AdamBOD_default_library_list "$<TARGET_OBJECTS:AdamBOD_default_default_XC32_assembleWithPreprocess>")

endif()

# Handle files with suffix [cC], for group default-XC32
if(AdamBOD_default_default_XC32_FILE_TYPE_compile)
add_library(AdamBOD_default_default_XC32_compile OBJECT ${AdamBOD_default_default_XC32_FILE_TYPE_compile})
    AdamBOD_default_default_XC32_compile_rule(AdamBOD_default_default_XC32_compile)
    list(APPEND AdamBOD_default_library_list "$<TARGET_OBJECTS:AdamBOD_default_default_XC32_compile>")

endif()

# Handle files with suffix cpp, for group default-XC32
if(AdamBOD_default_default_XC32_FILE_TYPE_compile_cpp)
add_library(AdamBOD_default_default_XC32_compile_cpp OBJECT ${AdamBOD_default_default_XC32_FILE_TYPE_compile_cpp})
    AdamBOD_default_default_XC32_compile_cpp_rule(AdamBOD_default_default_XC32_compile_cpp)
    list(APPEND AdamBOD_default_library_list "$<TARGET_OBJECTS:AdamBOD_default_default_XC32_compile_cpp>")

endif()

# Handle files with suffix [cC], for group default-XC32
if(AdamBOD_default_default_XC32_FILE_TYPE_dependentObject)
add_library(AdamBOD_default_default_XC32_dependentObject OBJECT ${AdamBOD_default_default_XC32_FILE_TYPE_dependentObject})
    AdamBOD_default_default_XC32_dependentObject_rule(AdamBOD_default_default_XC32_dependentObject)
    list(APPEND AdamBOD_default_library_list "$<TARGET_OBJECTS:AdamBOD_default_default_XC32_dependentObject>")

endif()


# Main target for this project
add_executable(AdamBOD_default_image_ysv_s_3R ${AdamBOD_default_library_list})

set_target_properties(AdamBOD_default_image_ysv_s_3R PROPERTIES
    OUTPUT_NAME "default"
    SUFFIX ".elf"
    RUNTIME_OUTPUT_DIRECTORY "${AdamBOD_default_output_dir}")
target_link_libraries(AdamBOD_default_image_ysv_s_3R PRIVATE ${AdamBOD_default_default_XC32_FILE_TYPE_link})
# Add the link options from the rule file.
AdamBOD_default_link_rule( AdamBOD_default_image_ysv_s_3R)

# Add bin2hex target for converting built file to a .hex file.
string(REGEX REPLACE [.]elf$ .hex AdamBOD_default_image_name_hex ${AdamBOD_default_image_name})
add_custom_target(AdamBOD_default_Bin2Hex ALL
    COMMAND ${MP_BIN2HEX} \"${AdamBOD_default_output_dir}/${AdamBOD_default_image_name}\"
    DEPENDS "${AdamBOD_default_output_dir}/${AdamBOD_default_image_name}"
    BYPRODUCTS ${AdamBOD_default_output_dir}/${AdamBOD_default_image_name_hex}
    COMMENT "Convert built file to .hex")
add_dependencies(AdamBOD_default_Bin2Hex AdamBOD_default_image_ysv_s_3R)




