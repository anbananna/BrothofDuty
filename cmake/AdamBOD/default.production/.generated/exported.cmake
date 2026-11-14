set(DEPENDENT_MP_BIN2HEXAdamBOD_default_R5nL1JRk "c:/Program Files/Microchip/xc32/v6.00/bin/xc32-bin2hex.exe")
set(DEPENDENT_DEPENDENT_TARGET_ELFAdamBOD_default_R5nL1JRk ${CMAKE_CURRENT_LIST_DIR}/../../../../out/AdamBOD/production/default-production.elf)
set(DEPENDENT_TARGET_DIRAdamBOD_default_R5nL1JRk ${CMAKE_CURRENT_LIST_DIR}/../../../../out/AdamBOD/production)
set(DEPENDENT_BYPRODUCTSAdamBOD_default_R5nL1JRk ${DEPENDENT_TARGET_DIRAdamBOD_default_R5nL1JRk}/${sourceFileNameAdamBOD_default_R5nL1JRk}.c)
add_custom_command(
    OUTPUT ${DEPENDENT_TARGET_DIRAdamBOD_default_R5nL1JRk}/${sourceFileNameAdamBOD_default_R5nL1JRk}.c
    COMMAND ${DEPENDENT_MP_BIN2HEXAdamBOD_default_R5nL1JRk} --image ${DEPENDENT_DEPENDENT_TARGET_ELFAdamBOD_default_R5nL1JRk} --image-generated-c ${sourceFileNameAdamBOD_default_R5nL1JRk}.c --image-generated-h ${sourceFileNameAdamBOD_default_R5nL1JRk}.h --image-copy-mode ${modeAdamBOD_default_R5nL1JRk} --image-offset ${addressAdamBOD_default_R5nL1JRk} 
    WORKING_DIRECTORY ${DEPENDENT_TARGET_DIRAdamBOD_default_R5nL1JRk}
    DEPENDS ${DEPENDENT_DEPENDENT_TARGET_ELFAdamBOD_default_R5nL1JRk})
add_custom_target(
    dependent_produced_source_artifactAdamBOD_default_R5nL1JRk 
    DEPENDS ${DEPENDENT_TARGET_DIRAdamBOD_default_R5nL1JRk}/${sourceFileNameAdamBOD_default_R5nL1JRk}.c
    )
