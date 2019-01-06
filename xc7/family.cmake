if (NOT DEFINED TORC_ROOT)
    # Adapted from https://cliutils.gitlab.io/modern-cmake/chapters/projects/submodule.html
    find_package(Git QUIET)
    if(GIT_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.gitmodules")
    # Update submodules as needed
        option(GIT_SUBMODULE "Check submodules during build" ON)
        if(GIT_SUBMODULE)
            message(STATUS "Submodule update")
            execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                            RESULT_VARIABLE GIT_SUBMOD_RESULT)
            if(NOT GIT_SUBMOD_RESULT EQUAL "0")
                message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
            endif()
        endif()
    endif()

    add_dependencies(nextpnr-${family} torc)
    if (BUILD_TESTS)
        add_dependencies(nextpnr-${family}-test torc)
    endif()
    add_custom_target(torc ALL
                      COMMAND $(MAKE) > /dev/null 2> /dev/null
                      COMMENT "Building torc (may take some time...)"
                      WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/torc/src)
    find_package(Boost REQUIRED COMPONENTS serialization iostreams ${boost_libs} ${boost_python_lib})

    set(TORC_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
endif()

target_compile_definitions(nextpnr-${family} PRIVATE -DTORC_ROOT="${TORC_ROOT}")
target_include_directories(nextpnr-${family} PUBLIC ${TORC_ROOT}/torc/src)
if (BUILD_TESTS)
    target_compile_definitions(nextpnr-${family}-test PRIVATE -DTORC_ROOT="${TORC_ROOT}")
    target_include_directories(nextpnr-${family}-test PUBLIC ${TORC_ROOT}/torc/src)
endif()
if (BUILD_GUI)
    target_include_directories(gui_${family} PUBLIC ${TORC_ROOT}/torc/src)
endif()

set(TORC_OBJS
    ${TORC_ROOT}/torc/src/torc/architecture/Arc.o
    ${TORC_ROOT}/torc/src/torc/architecture/ArcUsage.o
    ${TORC_ROOT}/torc/src/torc/architecture/Array.o
    ${TORC_ROOT}/torc/src/torc/architecture/DDB.o
    ${TORC_ROOT}/torc/src/torc/architecture/DDBConsoleStreams.o
    ${TORC_ROOT}/torc/src/torc/architecture/DDBStreamHelper.o
    ${TORC_ROOT}/torc/src/torc/architecture/DigestStream.o
    ${TORC_ROOT}/torc/src/torc/architecture/ExtendedWireInfo.o
    ${TORC_ROOT}/torc/src/torc/architecture/InstancePin.o
    ${TORC_ROOT}/torc/src/torc/architecture/OutputStreamHelpers.o
    ${TORC_ROOT}/torc/src/torc/architecture/Package.o
    ${TORC_ROOT}/torc/src/torc/architecture/Pad.o
    ${TORC_ROOT}/torc/src/torc/architecture/PrimitiveConn.o
    ${TORC_ROOT}/torc/src/torc/architecture/PrimitiveDef.o
    ${TORC_ROOT}/torc/src/torc/architecture/PrimitiveElement.o
    ${TORC_ROOT}/torc/src/torc/architecture/PrimitiveElementPin.o
    ${TORC_ROOT}/torc/src/torc/architecture/PrimitivePin.o
    ${TORC_ROOT}/torc/src/torc/architecture/Segments.o
    ${TORC_ROOT}/torc/src/torc/architecture/Site.o
    ${TORC_ROOT}/torc/src/torc/architecture/Sites.o
    ${TORC_ROOT}/torc/src/torc/architecture/Tiles.o
    ${TORC_ROOT}/torc/src/torc/architecture/TileInfo.o
    ${TORC_ROOT}/torc/src/torc/architecture/Tilewire.o
    ${TORC_ROOT}/torc/src/torc/architecture/Versions.o
    ${TORC_ROOT}/torc/src/torc/architecture/VprExporter.o
    ${TORC_ROOT}/torc/src/torc/architecture/WireInfo.o
    ${TORC_ROOT}/torc/src/torc/architecture/WireUsage.o
    ${TORC_ROOT}/torc/src/torc/architecture/XdlImporter.o
    ${TORC_ROOT}/torc/src/torc/architecture/XilinxDatabaseTypes.o

    ${TORC_ROOT}/torc/src/torc/common/Annotated.o
    ${TORC_ROOT}/torc/src/torc/common/DeviceDesignator.o
	${TORC_ROOT}/torc/src/torc/common/Devices.o
	${TORC_ROOT}/torc/src/torc/common/DirectoryTree.o
	${TORC_ROOT}/torc/src/torc/common/DottedVersion.o
	${TORC_ROOT}/torc/src/torc/common/NullOutputStream.o

    ${TORC_ROOT}/torc/src/torc/externals/zlib/zfstream.o
    z

    ${TORC_ROOT}/torc/src/torc/physical/Circuit.o
    ${TORC_ROOT}/torc/src/torc/physical/ConfigMap.o
    ${TORC_ROOT}/torc/src/torc/physical/Config.o
    ${TORC_ROOT}/torc/src/torc/physical/Design.o
    ${TORC_ROOT}/torc/src/torc/physical/Factory.o
    ${TORC_ROOT}/torc/src/torc/physical/Instance.o
    ${TORC_ROOT}/torc/src/torc/physical/InstancePin.o
    ${TORC_ROOT}/torc/src/torc/physical/InstanceReference.o
    ${TORC_ROOT}/torc/src/torc/physical/Module.o
    ${TORC_ROOT}/torc/src/torc/physical/ModuleTransformer.o
    ${TORC_ROOT}/torc/src/torc/physical/Named.o
    ${TORC_ROOT}/torc/src/torc/physical/Net.o
    ${TORC_ROOT}/torc/src/torc/physical/OutputStreamHelpers.o
    ${TORC_ROOT}/torc/src/torc/physical/Pip.o
    ${TORC_ROOT}/torc/src/torc/physical/Port.o
    ${TORC_ROOT}/torc/src/torc/physical/Progenitor.o
    ${TORC_ROOT}/torc/src/torc/physical/Progeny.o
    ${TORC_ROOT}/torc/src/torc/physical/Renamable.o
    ${TORC_ROOT}/torc/src/torc/physical/Routethrough.o
    ${TORC_ROOT}/torc/src/torc/physical/TilewirePlaceholder.o
    ${TORC_ROOT}/torc/src/torc/physical/XdlExporter.o
)

target_link_libraries(nextpnr-${family} PRIVATE ${TORC_OBJS})
if (BUILD_TESTS)
    target_link_libraries(nextpnr-${family}-test PRIVATE ${TORC_OBJS})
endif()
