# SPDX-License-Identifier: Apache-2.0
# Capture on every build, before any component compiles. An unchanged snapshot
# keeps the generated C timestamp, so a no-op build does not relink.
idf_build_get_property(mybot_source_python PYTHON)
idf_build_get_property(mybot_source_elf EXECUTABLE)
idf_build_get_property(mybot_source_components BUILD_COMPONENTS)
set(mybot_source_script "${CMAKE_CURRENT_LIST_DIR}/capture_build_source.py")
set(mybot_source_c "${CMAKE_BINARY_DIR}/mybot_build_source.c")
add_custom_target(mybot_capture_build_source
    COMMAND "${mybot_source_python}" "${mybot_source_script}" capture
        --project-root "${PROJECT_SOURCE_DIR}" --build-dir "${CMAKE_BINARY_DIR}"
    BYPRODUCTS "${mybot_source_c}" "${CMAKE_BINARY_DIR}/mybot_build_source.json"
    VERBATIM)
foreach(mybot_source_component IN LISTS mybot_source_components)
    idf_component_get_property(mybot_source_target ${mybot_source_component} COMPONENT_LIB)
    add_dependencies(${mybot_source_target} mybot_capture_build_source)
endforeach()
add_dependencies(${mybot_source_elf} mybot_capture_build_source)
add_dependencies(bootloader mybot_capture_build_source)
add_dependencies(partition_table_bin mybot_capture_build_source)
target_sources(${mybot_source_elf} PRIVATE "${mybot_source_c}")
target_link_options(${mybot_source_elf} PRIVATE "-Wl,--undefined=mybot_build_source_sha256")

# All flash segments must exist before finalizing. app and merge-bin have
# different dependency paths in IDF, so both wait for this completed record.
add_custom_target(mybot_finalize_build_source
    COMMAND "${mybot_source_python}" "${mybot_source_script}" finalize
        --project-root "${PROJECT_SOURCE_DIR}" --build-dir "${CMAKE_BINARY_DIR}"
    DEPENDS gen_project_binary bootloader partition_table_bin
    BYPRODUCTS "${CMAKE_BINARY_DIR}/mybot_build_provenance.json"
    VERBATIM)
add_dependencies(app mybot_finalize_build_source)
add_dependencies(merge-bin mybot_finalize_build_source)
