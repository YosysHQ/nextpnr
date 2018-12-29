# Adapted https://cliutils.gitlab.io/modern-cmake/chapters/projects/submodule.html
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS ".gitmodules")
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
add_custom_target(torc ALL
                  COMMAND $(MAKE) > /dev/null 2> /dev/null
                  COMMENT "Building torc (may take some time...)"
                  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/torc/src)
find_package(Boost REQUIRED COMPONENTS serialization iostreams ${boost_libs} ${boost_python_lib})

include_directories(torc/src)
target_link_libraries(
    nextpnr-${family} PRIVATE

    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Arc.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/ArcUsage.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Array.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/DDB.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/DDBConsoleStreams.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/DDBStreamHelper.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/DigestStream.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/ExtendedWireInfo.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/InstancePin.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/OutputStreamHelpers.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Package.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Pad.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/PrimitiveConn.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/PrimitiveDef.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/PrimitiveElement.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/PrimitiveElementPin.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/PrimitivePin.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Segments.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Site.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Sites.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Tiles.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/TileInfo.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Tilewire.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/Versions.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/VprExporter.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/WireInfo.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/WireUsage.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/XdlImporter.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/architecture/XilinxDatabaseTypes.o

    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/common/Annotated.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/common/DeviceDesignator.o
	${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/common/Devices.o
	${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/common/DirectoryTree.o
	${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/common/DottedVersion.o
	${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/common/NullOutputStream.o

    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/externals/zlib/zfstream.o
    z

    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Circuit.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/ConfigMap.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Config.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Design.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Factory.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Instance.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/InstancePin.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/InstanceReference.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Module.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/ModuleTransformer.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Named.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Net.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/OutputStreamHelpers.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Pip.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Port.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Progenitor.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Progeny.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Renamable.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/Routethrough.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/TilewirePlaceholder.o
    ${CMAKE_CURRENT_SOURCE_DIR}/torc/src/torc/physical/XdlExporter.o
)
