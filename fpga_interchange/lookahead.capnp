@0x97c69817483d9dea;

using Cxx = import "/capnp/c++.capnp";
$Cxx.namespace("lookahead_storage");

using DelayType = Int32;

struct TypeWireId {
    type  @0: Int32;
    index @1: Int32;
}

struct TypeWirePair {
    src @0 : TypeWireId;
    dst @1 : TypeWireId;
}

struct InputSiteWireCost {
    routeTo @0 : TypeWireId;
    cost    @1 : DelayType;
}

struct InputSiteWireCostMap {
    key   @0 : TypeWireId;
    value @1 : List(InputSiteWireCost);
}

struct OutputSiteWireCostMap {
    key               @0 : TypeWireId;
    cheapestRouteFrom @1 : TypeWireId;
    cost              @2 : DelayType;
}

struct SiteToSiteCostMap {
    key  @0 : TypeWirePair;
    cost @1 : DelayType;
}

struct CostMapEntry {
    key     @0 : TypeWirePair;
    data    @1 : List(DelayType);
    xDim    @2 : UInt32;
    yDim    @3 : UInt32;
    xOffset @4 : UInt32;
    yOffset @5 : UInt32;
    penalty @6 : DelayType;
}

struct CostMap {
    costMap @0 : List(CostMapEntry);
}

struct Lookahead {

    chipdbHash      @0 : Text;
    inputSiteWires  @1 : List(InputSiteWireCostMap);
    outputSiteWires @2 : List(OutputSiteWireCostMap);
    siteToSiteCost  @3 : List(SiteToSiteCostMap);
    costMap         @4 : CostMap;
}

