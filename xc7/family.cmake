include_directories(.)
#include_directories(torc/externals/zlib)

target_link_libraries(
    nextpnr-${family}
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Arc.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/ArcUsage.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Array.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/DDB.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/DDBConsoleStreams.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/DDBStreamHelper.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/DigestStream.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/ExtendedWireInfo.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/InstancePin.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/OutputStreamHelpers.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Package.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Pad.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/PrimitiveConn.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/PrimitiveDef.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/PrimitiveElement.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/PrimitiveElementPin.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/PrimitivePin.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Segments.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Site.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Sites.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Tiles.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/TileInfo.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Tilewire.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/Versions.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/VprExporter.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/WireInfo.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/WireUsage.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/XdlImporter.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/architecture/XilinxDatabaseTypes.o 

    PRIVATE ${CMAKE_SOURCE_DIR}/torc/common/Annotated.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/common/DeviceDesignator.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/common/Devices.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/common/DirectoryTree.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/common/DottedVersion.o 
	PRIVATE ${CMAKE_SOURCE_DIR}/torc/common/NullOutputStream.o 
    PRIVATE boost_regex

    PRIVATE ${CMAKE_SOURCE_DIR}/torc/externals/zlib/zfstream.o
    PRIVATE z

    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Circuit.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/ConfigMap.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Config.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Design.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Factory.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Instance.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/InstancePin.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/InstanceReference.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Module.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/ModuleTransformer.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Named.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Net.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/OutputStreamHelpers.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Pip.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Port.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Progenitor.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Progeny.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Renamable.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/Routethrough.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/TilewirePlaceholder.o
    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/XdlExporter.o
#    PRIVATE ${CMAKE_SOURCE_DIR}/torc/physical/XdlImporter.o
)
