# The following functions contains all the flags passed to the different build stages.

set(PACK_REPO_PATH "C:/Users/adamt/.mchp_packs" CACHE PATH "Path to the root of a pack repository.")

function(AdamBOD_default_default_XC32_assemble_rule target)
    set(options
        "-g"
        "${ASSEMBLER_PRE}"
        "-mprocessor=32MX170F256B"
        "-Wa,--defsym=__MPLAB_BUILD=1${MP_EXTRA_AS_POST}"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MX_DFP/1.7.380")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
endfunction()
function(AdamBOD_default_default_XC32_assembleWithPreprocess_rule target)
    set(options
        "-x"
        "assembler-with-cpp"
        "-g"
        "${MP_EXTRA_AS_PRE}"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MX_DFP/1.7.380"
        "-mprocessor=32MX170F256B"
        "-Wa,--defsym=__MPLAB_BUILD=1${MP_EXTRA_AS_POST}")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target} PRIVATE "XPRJ_default=default")
endfunction()
function(AdamBOD_default_default_XC32_compile_rule target)
    set(options
        "-g"
        "${CC_PRE}"
        "-x"
        "c"
        "-c"
        "-mprocessor=32MX170F256B"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MX_DFP/1.7.380")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target} PRIVATE "XPRJ_default=default")
    target_include_directories(${target}
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../../../ProjectHeaders"
        PRIVATE "c:/Users/adamt/Documents/GitHub/BrothofDuty/ProjectHeaders"
        PRIVATE "c:/Users/adamt/Documents/GitHub/BrothofDuty/FrameworkHeaders")
endfunction()
function(AdamBOD_default_default_XC32_compile_cpp_rule target)
    set(options
        "-g"
        "${CC_PRE}"
        "-mprocessor=32MX170F256B"
        "-frtti"
        "-fexceptions"
        "-fno-check-new"
        "-fenforce-eh-specs"
        "-fno-common"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MX_DFP/1.7.380")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target} PRIVATE "XPRJ_default=default")
endfunction()
function(AdamBOD_default_dependentObject_rule target)
    set(options
        "-mprocessor=32MX170F256B"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MX_DFP/1.7.380")
    list(REMOVE_ITEM options "")
    target_compile_options(${target} PRIVATE "${options}")
endfunction()
function(AdamBOD_default_link_rule target)
    set(options
        "-g"
        "${MP_EXTRA_LD_PRE}"
        "-mprocessor=32MX170F256B"
        "-Wl,--defsym=__MPLAB_BUILD=1${MP_EXTRA_LD_POST},--no-code-in-dinit,--no-dinit-in-serial-mem,-Map=mem.map,--report-mem,--memorysummary,memoryfile.xml"
        "-mdfp=${PACK_REPO_PATH}/Microchip/PIC32MX_DFP/1.7.380")
    list(REMOVE_ITEM options "")
    target_link_options(${target} PRIVATE "${options}")
    target_compile_definitions(${target} PRIVATE "XPRJ_default=default")
endfunction()
