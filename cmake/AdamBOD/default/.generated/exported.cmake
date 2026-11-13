set(DEPENDENT_MP_BIN2HEXAdamBOD_default_ysv_s_3R "c:/Program Files/Microchip/xc32/v6.00/bin/xc32-bin2hex.exe")
set(DEPENDENT_DEPENDENT_TARGET_ELFAdamBOD_default_ysv_s_3R ${CMAKE_CURRENT_LIST_DIR}/../../../../out/AdamBOD/default.elf)
set(DEPENDENT_TARGET_DIRAdamBOD_default_ysv_s_3R ${CMAKE_CURRENT_LIST_DIR}/../../../../out/AdamBOD)
set(DEPENDENT_BYPRODUCTSAdamBOD_default_ysv_s_3R ${DEPENDENT_TARGET_DIRAdamBOD_default_ysv_s_3R}/${sourceFileNameAdamBOD_default_ysv_s_3R}.c)
add_custom_command(
    OUTPUT ${DEPENDENT_TARGET_DIRAdamBOD_default_ysv_s_3R}/${sourceFileNameAdamBOD_default_ysv_s_3R}.c
    COMMAND ${DEPENDENT_MP_BIN2HEXAdamBOD_default_ysv_s_3R} --image ${DEPENDENT_DEPENDENT_TARGET_ELFAdamBOD_default_ysv_s_3R} --image-generated-c ${sourceFileNameAdamBOD_default_ysv_s_3R}.c --image-generated-h ${sourceFileNameAdamBOD_default_ysv_s_3R}.h --image-copy-mode ${modeAdamBOD_default_ysv_s_3R} --image-offset ${addressAdamBOD_default_ysv_s_3R} 
    WORKING_DIRECTORY ${DEPENDENT_TARGET_DIRAdamBOD_default_ysv_s_3R}
    DEPENDS ${DEPENDENT_DEPENDENT_TARGET_ELFAdamBOD_default_ysv_s_3R})
add_custom_target(
    dependent_produced_source_artifactAdamBOD_default_ysv_s_3R 
    DEPENDS ${DEPENDENT_TARGET_DIRAdamBOD_default_ysv_s_3R}/${sourceFileNameAdamBOD_default_ysv_s_3R}.c
    )
