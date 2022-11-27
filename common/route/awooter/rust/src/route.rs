use std::collections::{BinaryHeap, HashMap};

use crate::{npnr::{self, NetIndex, PipId}, partition};

pub struct Arc {
    source_wire: npnr::WireId,
    source_loc: npnr::Loc,
    sink_wire: npnr::WireId,
    sink_loc: npnr::Loc,
    net: npnr::NetIndex,
}

impl Arc {
    pub fn new(source_wire: npnr::WireId, source_loc: npnr::Loc, sink_wire: npnr::WireId, sink_loc: npnr::Loc, net: NetIndex) -> Self {
        Self {
            source_wire, source_loc, sink_wire, sink_loc, net
        }
    }

    pub fn split(&mut self, ctx: &npnr::Context, pip: npnr::PipId) -> (Self, Self) {
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

#[derive(Copy, Clone)]
struct QueuedWire {
    cost: f32,
    togo: f32,
    wire: npnr::WireId,
}

impl QueuedWire {
    pub fn new(cost: f32, togo: f32, wire: npnr::WireId) -> Self {
        Self {
            cost,
            togo,
            wire,
        }
    }
}

impl PartialEq for QueuedWire {
    fn eq(&self, other: &Self) -> bool {
        self.cost == other.cost && self.togo == other.togo && self.wire == other.wire
    }
}

impl Eq for QueuedWire {}

impl Ord for QueuedWire {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        let me = self.cost + self.togo;
        let other = other.cost + other.togo;
        other.total_cmp(&me)
    }
}

impl PartialOrd for QueuedWire {
    fn partial_cmp(&self, other: &Self) -> Option<std::cmp::Ordering> {
        Some(self.cmp(other))
    }
}

pub struct Router {
    box_ne: partition::Coord,
    box_sw: partition::Coord,
    bound_pip: HashMap<npnr::PipId, NetIndex>,
    wire_driver: HashMap<npnr::WireId, npnr::PipId>,
}

impl Router {
    pub fn new(box_ne: partition::Coord, box_sw: partition::Coord) -> Self {
        Self {
            box_ne,
            box_sw,
            bound_pip: HashMap::new(),
            wire_driver: HashMap::new(),
        }
    }

    pub fn route(&mut self, ctx: &npnr::Context, arcs: &[Arc]) {
        for arc in arcs {
            self.route_arc(ctx, arc);
        }
    }

    fn route_arc(&mut self, ctx: &npnr::Context, arc: &Arc) {
        let mut queue = BinaryHeap::new();
        let mut visited = HashMap::new();

        queue.push(QueuedWire::new(0.0, 0.0, arc.source_wire));
        visited.insert(arc.source_wire, npnr::PipId::null());

        while let Some(source) = queue.pop() {
            if source.wire == arc.sink_wire {
                panic!("found the sink!");
                break;
            }

            for pip in ctx.get_downhill_pips(source.wire) {
                let pip_loc = ctx.pip_location(pip);
                let pip_coord = partition::Coord::from(pip_loc);
                if pip_coord.is_north_of(&self.box_ne) || pip_coord.is_east_of(&self.box_ne) {
                    continue;
                }
                if pip_coord.is_south_of(&self.box_sw) || pip_coord.is_west_of(&self.box_sw) {
                    continue;
                }

                /*
                if (!ctx->checkPipAvailForNet(dh, net))
                    continue;
                WireId next = ctx->getPipDstWire(dh);
                int next_idx = wire_to_idx.at(next);
                if (was_visited_fwd(next_idx)) {
                    // Don't expand the same node twice.
                    continue;
                }
                auto &nwd = flat_wires.at(next_idx);
                if (nwd.unavailable)
                    continue;
                // Reserved for another net
                if (nwd.reserved_net != -1 && nwd.reserved_net != net->udata)
                    continue;
                // Don't allow the same wire to be bound to the same net with a different driving pip
                auto fnd_wire = nd.wires.find(next);
                if (fnd_wire != nd.wires.end() && fnd_wire->second.first != dh)
                    continue;
                if (!thread_test_wire(t, nwd))
                    continue; // thread safety issue
                */

                let sink = ctx.pip_dst_wire(pip);
                if visited.contains_key(&sink) {
                    continue;
                }

                let driver = self.wire_driver.get(&sink);
                if let Some(&driver) = driver {
                    if driver != pip {
                        continue;
                    }
                    if *self.bound_pip.get(&driver).unwrap() != arc.net {
                        continue;
                    }
                }

                visited.insert(sink, pip);

                let delay = source.cost + ctx.pip_delay(pip) + ctx.wire_delay(sink) + ctx.delay_epsilon();
                let qw = QueuedWire::new(delay, ctx.estimate_delay(sink, arc.sink_wire), sink);
                queue.push(qw);
            }
        }

        let mut wire = arc.sink_wire;
        while wire != arc.source_wire {
            let pip = *visited.get(&wire).unwrap();
            self.bind_pip(pip, arc.net);
            wire = ctx.pip_src_wire(pip);
        }
    }

    fn bind_pip(&mut self, pip: PipId, net: NetIndex) {
        assert!(!self.bound_pip.contains_key(&pip));
        self.bound_pip.insert(pip, net);
    }
}
