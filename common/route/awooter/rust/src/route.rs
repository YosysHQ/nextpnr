use crate::npnr;

pub struct Arc {
    source_wire: npnr::WireId,
    source_loc: npnr::Loc,
    sink_wire: npnr::WireId,
    sink_loc: npnr::Loc,
    net: *mut npnr::NetInfo,
}

impl Arc {
    pub fn new(source_wire: npnr::WireId, source_loc: npnr::Loc, sink_wire: npnr::WireId, sink_loc: npnr::Loc, net: *mut npnr::NetInfo) -> Self {
        Self {
            source_wire, source_loc, sink_wire, sink_loc, net
        }
    }
}

