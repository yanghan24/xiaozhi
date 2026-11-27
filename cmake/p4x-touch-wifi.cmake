# Build the provisioning changes against the locked component without editing its cache.
find_package(Git REQUIRED)
idf_component_get_property(wifi_dir 78__esp-wifi-connect COMPONENT_DIR)
idf_component_get_property(wifi_target 78__esp-wifi-connect COMPONENT_LIB)
set(patch "${CMAKE_CURRENT_LIST_DIR}/p4x-touch-wifi.patch")
set(generated_dir "${CMAKE_BINARY_DIR}/p4x-touch-wifi")
file(MAKE_DIRECTORY "${generated_dir}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${patch}")

set(wifi_files wifi_configuration_ap.cc wifi_station.cc wifi_manager.cc dns_server.cc ssid_manager.cc)
get_target_property(wifi_sources "${wifi_target}" SOURCES)
get_target_property(wifi_source_dir "${wifi_target}" SOURCE_DIR)
foreach(name IN LISTS wifi_files)
    configure_file("${wifi_dir}/${name}" "${generated_dir}/${name}" COPYONLY)
    set(original "${wifi_dir}/${name}")
    foreach(source IN LISTS wifi_sources)
        get_filename_component(source_path "${source}" ABSOLUTE BASE_DIR "${wifi_source_dir}")
        if(source_path STREQUAL original)
            list(REMOVE_ITEM wifi_sources "${source}")
            break()
        endif()
    endforeach()
    list(APPEND wifi_sources "${generated_dir}/${name}")
endforeach()
foreach(header wifi_configuration_ap.h wifi_station.h wifi_manager.h dns_server.h ssid_manager.h)
    configure_file("${wifi_dir}/include/${header}" "${generated_dir}/include/${header}" COPYONLY)
endforeach()

# Without an explicit work tree, Git can discover the parent repository and
# silently skip patch paths outside the current repository subdirectory.
execute_process(COMMAND "${GIT_EXECUTABLE}" --work-tree=. apply --no-index "${patch}"
    WORKING_DIRECTORY "${generated_dir}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Review the P4X provisioning patch for the current esp-wifi-connect version")
endif()
target_include_directories("${wifi_target}" BEFORE PUBLIC "${generated_dir}/include")

# Keep the portal's embedded symbols and plain HTML responses. Use compacted
# copies in this product build, leaving the locked component intact.
idf_build_get_property(python PYTHON)
idf_build_get_property(idf_path IDF_PATH)
set(compactor "${CMAKE_CURRENT_LIST_DIR}/compact-wifi-portal.py")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${compactor}")
list(FILTER wifi_sources EXCLUDE REGEX "(^|/)wifi_configuration(_done)?[.]html[.]S$")
set_property(TARGET "${wifi_target}" PROPERTY SOURCES "${wifi_sources}")
foreach(page wifi_configuration.html wifi_configuration_done.html)
    set(original "${wifi_dir}/assets/${page}")
    set(compact "${generated_dir}/compact_${page}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    execute_process(COMMAND "${python}" "${compactor}" "${original}" "${compact}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not compact the P4X Wi-Fi portal page ${page}")
    endif()
    # Generate the assembly here because the Wi-Fi target belongs to the
    # managed component's CMake directory.
    set(assembly "${compact}.S")
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DDATA_FILE=${compact}"
        "-DSOURCE_FILE=${assembly}" "-DVARIABLE_BASENAME=${page}" "-DFILE_TYPE=TEXT"
        -P "${idf_path}/tools/cmake/scripts/data_file_embed_asm.cmake"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Could not embed the P4X Wi-Fi portal page ${page}")
    endif()
    target_sources("${wifi_target}" PRIVATE "${assembly}")
endforeach()
