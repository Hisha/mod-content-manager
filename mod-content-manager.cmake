# Link bundled StormLib into AzerothCore's static modules library.
if(TARGET StormLib::storm)
    target_link_libraries(modules PUBLIC StormLib::storm)
    message(STATUS "mod-content-manager: bundled StormLib linked")
else()
    message(FATAL_ERROR "mod-content-manager: bundled StormLib target was not created")
endif()