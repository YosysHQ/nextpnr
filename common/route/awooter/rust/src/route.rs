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

    pub fn split(&self, ctx: &npnr::Context, pip: npnr::PipId) -> (Self, Self) {
        let pip_src = ctx.pip_src_wire(pip);
        let pip_dst = ctx.pip_dst_wire(pip);
        (Self {
            source_wire: self.source_wire,
            source_loc: self.source_loc,
            sink_wire: pip_src,
            sink_loc: ctx.pip_location(pip),
            net: self.net
        },
        Self {
            source_wire: pip_dst,
            source_loc: ctx.pip_location(pip),
            sink_wire: self.sink_wire,
            sink_loc: self.sink_loc,
            net: self.net
        })
    }
}

pub struct Router {
    arcs: Vec<Arc>
}
