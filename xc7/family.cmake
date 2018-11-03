include_directories(/opt/torc/src)
#include_directories(torc/externals/zlib)

target_link_libraries(
    nextpnr-${family}
	PRIVATE /opt/torc/src/torc/architecture/Arc.o 
	PRIVATE /opt/torc/src/torc/architecture/ArcUsage.o 
	PRIVATE /opt/torc/src/torc/architecture/Array.o 
	PRIVATE /opt/torc/src/torc/architecture/DDB.o 
	PRIVATE /opt/torc/src/torc/architecture/DDBConsoleStreams.o 
	PRIVATE /opt/torc/src/torc/architecture/DDBStreamHelper.o 
	PRIVATE /opt/torc/src/torc/architecture/DigestStream.o 
	PRIVATE /opt/torc/src/torc/architecture/ExtendedWireInfo.o 
	PRIVATE /opt/torc/src/torc/architecture/InstancePin.o 
	PRIVATE /opt/torc/src/torc/architecture/OutputStreamHelpers.o 
	PRIVATE /opt/torc/src/torc/architecture/Package.o 
	PRIVATE /opt/torc/src/torc/architecture/Pad.o 
	PRIVATE /opt/torc/src/torc/architecture/PrimitiveConn.o 
	PRIVATE /opt/torc/src/torc/architecture/PrimitiveDef.o 
	PRIVATE /opt/torc/src/torc/architecture/PrimitiveElement.o 
	PRIVATE /opt/torc/src/torc/architecture/PrimitiveElementPin.o 
	PRIVATE /opt/torc/src/torc/architecture/PrimitivePin.o 
	PRIVATE /opt/torc/src/torc/architecture/Segments.o 
	PRIVATE /opt/torc/src/torc/architecture/Site.o 
	PRIVATE /opt/torc/src/torc/architecture/Sites.o 
	PRIVATE /opt/torc/src/torc/architecture/Tiles.o 
	PRIVATE /opt/torc/src/torc/architecture/TileInfo.o 
	PRIVATE /opt/torc/src/torc/architecture/Tilewire.o 
	PRIVATE /opt/torc/src/torc/architecture/Versions.o 
	PRIVATE /opt/torc/src/torc/architecture/VprExporter.o 
	PRIVATE /opt/torc/src/torc/architecture/WireInfo.o 
	PRIVATE /opt/torc/src/torc/architecture/WireUsage.o 
	PRIVATE /opt/torc/src/torc/architecture/XdlImporter.o 
	PRIVATE /opt/torc/src/torc/architecture/XilinxDatabaseTypes.o 

    PRIVATE /opt/torc/src/torc/common/Annotated.o 
	PRIVATE /opt/torc/src/torc/common/DeviceDesignator.o 
	PRIVATE /opt/torc/src/torc/common/Devices.o 
	PRIVATE /opt/torc/src/torc/common/DirectoryTree.o 
	PRIVATE /opt/torc/src/torc/common/DottedVersion.o 
	PRIVATE /opt/torc/src/torc/common/NullOutputStream.o 
    PRIVATE boost_regex

    PRIVATE /opt/torc/src/torc/externals/zlib/zfstream.o
    PRIVATE z

    PRIVATE /opt/torc/src/torc/physical/Circuit.o
    PRIVATE /opt/torc/src/torc/physical/ConfigMap.o
    PRIVATE /opt/torc/src/torc/physical/Config.o
    PRIVATE /opt/torc/src/torc/physical/Design.o
    PRIVATE /opt/torc/src/torc/physical/Factory.o
    PRIVATE /opt/torc/src/torc/physical/Instance.o
    PRIVATE /opt/torc/src/torc/physical/InstancePin.o
    PRIVATE /opt/torc/src/torc/physical/InstanceReference.o
    PRIVATE /opt/torc/src/torc/physical/Module.o
    PRIVATE /opt/torc/src/torc/physical/ModuleTransformer.o
    PRIVATE /opt/torc/src/torc/physical/Named.o
    PRIVATE /opt/torc/src/torc/physical/Net.o
    PRIVATE /opt/torc/src/torc/physical/OutputStreamHelpers.o
    PRIVATE /opt/torc/src/torc/physical/Pip.o
    PRIVATE /opt/torc/src/torc/physical/Port.o
    PRIVATE /opt/torc/src/torc/physical/Progenitor.o
    PRIVATE /opt/torc/src/torc/physical/Progeny.o
    PRIVATE /opt/torc/src/torc/physical/Renamable.o
    PRIVATE /opt/torc/src/torc/physical/Routethrough.o
    PRIVATE /opt/torc/src/torc/physical/TilewirePlaceholder.o
    PRIVATE /opt/torc/src/torc/physical/XdlExporter.o
#    PRIVATE /opt/torc/src/torc/physical/XdlImporter.o
)
