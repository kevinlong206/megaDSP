execute_process(
    COMMAND git apply --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${NAM_SOURCE_DIR}"
    RESULT_VARIABLE can_apply
    OUTPUT_QUIET
    ERROR_QUIET)

if(can_apply EQUAL 0)
    execute_process(
        COMMAND git apply --whitespace=nowarn "${PATCH_FILE}"
        WORKING_DIRECTORY "${NAM_SOURCE_DIR}"
        RESULT_VARIABLE apply_result)
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR "Failed to apply NeuralAmpModelerCore patch")
    endif()
    return()
endif()

execute_process(
    COMMAND git apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${NAM_SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET
    ERROR_QUIET)
if(NOT already_applied EQUAL 0)
    message(FATAL_ERROR
        "NeuralAmpModelerCore source does not match the pinned patch")
endif()
