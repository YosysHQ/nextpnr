use std::collections::{BinaryHeap, HashMap};

use indicatif::{MultiProgress, ProgressBar, ProgressStyle};

use crate::{
    npnr::{self, NetIndex, PipId, WireId},
    partition,
};

#[derive(Clone)]
pub struct Arc {
    source_wire: npnr::WireId,
    source_loc: npnr::Loc,
    sink_wire: npnr::WireId,
    sink_loc: npnr::Loc,
    net: npnr::NetIndex,
}

impl Arc {
    pub fn new(
        source_wire: npnr::WireId,
        source_loc: npnr::Loc,
        sink_wire: npnr::WireId,
        sink_loc: npnr::Loc,
        net: NetIndex,
    ) -> Self {
        Self {
            source_wire,
            source_loc,
            sink_wire,
            sink_loc,
            net,
        }
    }

    pub fn split(&self, ctx: &npnr::Context, pip: npnr::PipId) -> (Self, Self) {
        let pip_src = ctx.pip_src_wire(pip);
        let pip_dst = ctx.pip_dst_wire(pip);
        (
            Self {
                source_wire: self.source_wire,
                source_loc: self.source_loc,
                sink_wire: pip_src,
                sink_loc: ctx.pip_location(pip),
                net: self.net,
            },
            Self {
                source_wire: pip_dst,
                source_loc: ctx.pip_location(pip),
                sink_wire: self.sink_wire,
                sink_loc: self.sink_loc,
                net: self.net,
            },
        )
    }

    pub fn get_source_loc(&self) -> npnr::Loc {
        self.source_loc
    }
    pub fn get_sink_loc(&self) -> npnr::Loc {
        self.sink_loc
    }
    pub fn get_source_wire(&self) -> npnr::WireId {
        self.source_wire
    }
    pub fn get_sink_wire(&self) -> npnr::WireId {
        self.sink_wire
    }
    pub fn net(&self) -> npnr::NetIndex {
        self.net
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
        Self { cost, togo, wire }
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


struct PerNetData {
    wires: HashMap<WireId, (PipId, u32)>,
}

struct PerWireData {
    wire: WireId, 
    curr_cong: u32,
    hist_cong: f32,
    unavailable: bool,
    reserved_net: Option<NetIndex>,
    pip_fwd: PipId,
    visited_fwd: bool,
}

pub struct Router {
    box_ne: partition::Coord,
    box_sw: partition::Coord,
    nets: Vec<PerNetData>,
    wire_to_idx: HashMap<WireId, u32>,
    flat_wires: Vec<PerWireData>,
    dirty_wires: Vec<u32>,
}

impl Router {
    pub fn new(box_ne: partition::Coord, box_sw: partition::Coord) -> Self {
        Self {
            box_ne,
            box_sw,
            nets: Vec::new(),
            wire_to_idx: HashMap::new(),
            flat_wires: Vec::new(),
            dirty_wires: Vec::new(),
        }
    }

    pub fn route(
        &mut self,
        ctx: &npnr::Context,
        nets: &npnr::Nets,
        wires: &[npnr::WireId],
        arcs: &[Arc],
        progress: &MultiProgress,
    ) {
        log_info!("Setting up router...\n");
        for _ in 0..nets.len() {
            self.nets.push(PerNetData { wires: HashMap::new() });
        }

        for (idx, &wire) in wires.iter().enumerate() {
            self.flat_wires.push(PerWireData {
                wire,
                curr_cong: 0,
                hist_cong: 0.0,
                unavailable: false,
                reserved_net: None,
                pip_fwd: PipId::null(),
                visited_fwd: false 
            });
            self.wire_to_idx.insert(wire, idx as u32);
        }

        let progress = progress.add(ProgressBar::new(arcs.len() as u64));
        progress.set_style(
            ProgressStyle::with_template("[{elapsed}] [{bar:40.magenta/red}] {msg:30!}")
                .unwrap()
                .progress_chars("━╸ "),
        );

        for arc in arcs {
            let net = unsafe { nets.net_from_index(arc.net).as_ref().unwrap() };
            let name = ctx
                .name_of(nets.name_from_index(arc.net))
                .to_str()
                .unwrap()
                .to_string();

            if net.is_global() {
                continue;
            }
            
            progress.inc(1);
            progress.set_message(name);
            self.route_arc(ctx, nets, arc);
        }
        progress.finish_and_clear()
    }

    fn route_arc(&mut self, ctx: &npnr::Context, nets: &npnr::Nets, arc: &Arc) {
        let mut queue = BinaryHeap::new();
        queue.push(QueuedWire::new(0.0, 0.0, arc.source_wire));

        let mut found_sink = false;

        let name = ctx
            .name_of(nets.name_from_index(arc.net))
            .to_str()
            .unwrap()
            .to_string();
        let verbose = false; //name == "soc0.processor.with_fpu.fpu_0.fpu_multiply_0.rin_CCU2C_S0_4$CCU2_FCI_INT";

        while let Some(source) = queue.pop() {
            if source.wire == arc.sink_wire {
                found_sink = true;
                break;
            }

            if verbose {
                log_info!("{}:\n", ctx.name_of_wire(source.wire).to_str().unwrap());
            }

            for pip in ctx.get_downhill_pips(source.wire) {
                if verbose {
                    log_info!("  {}\n", ctx.name_of_pip(pip).to_str().unwrap());
                }

                let pip_loc = ctx.pip_location(pip);
                let pip_coord = partition::Coord::from(pip_loc);
                if pip_coord.is_north_of(&self.box_ne) || pip_coord.is_east_of(&self.box_ne) {
                    if verbose {
                        log_info!("    out-of-bounds (NE)\n");
                    }
                    continue;
                }
                if pip_coord.is_south_of(&self.box_sw) || pip_coord.is_west_of(&self.box_sw) {
                    if verbose {
                        log_info!("    out-of-bounds (SW)\n");
                    }
                    continue;
                }
                if !ctx.pip_avail_for_net(pip, nets.net_from_index(arc.net())) {
                    if verbose {
                        log_info!("    pip unavailable for net\n");
                    }
                    continue;
                }
                let wire = ctx.pip_dst_wire(pip);
                let sink = *self.wire_to_idx.get(&wire).unwrap();
                if self.was_visited_fwd(sink) {
                    if verbose {
                        log_info!("    already visited\n");
                    }
                    continue;
                }
                let nd = &mut self.nets[arc.net().into_inner() as usize];
                let nwd = &self.flat_wires[sink as usize];
                if nwd.unavailable {
                    if verbose {
                        log_info!("    unavailable\n");
                    }
                    continue;
                }
                if let Some(net) = nwd.reserved_net && net != arc.net() {
                    if verbose {
                        log_info!("    reserved for other net\n");
                    }
                    continue;
                }
                // Don't allow the same wire to be bound to the same net with a different driving pip
                if let Some((found_pip, _)) = nd.wires.get(&wire) && *found_pip != pip {
                    if verbose {
                        log_info!("    driven by other pip\n");
                    }
                    continue;
                }

                self.set_visited_fwd(sink, pip);

                let delay =
                    source.cost + ctx.pip_delay(pip) + ctx.wire_delay(wire) + ctx.delay_epsilon();
                let qw = QueuedWire::new(delay, ctx.estimate_delay(wire, arc.sink_wire), wire);
                queue.push(qw);
            }
        }

        assert!(
            found_sink,
            "didn't find sink wire for net {} between {} and {}",
            name,
            ctx.name_of_wire(arc.source_wire).to_str().unwrap(),
            ctx.name_of_wire(arc.sink_wire).to_str().unwrap()
        );

        let source_wire = *self.wire_to_idx.get(&arc.source_wire).unwrap();
        let mut wire = *self.wire_to_idx.get(&arc.sink_wire).unwrap();
        while wire != source_wire {
            if verbose {
                println!("Wire: {}", ctx.name_of_wire(self.flat_wires[wire as usize].wire).to_str().unwrap());
            }
            let pip = self.flat_wires[wire as usize].pip_fwd;
            assert!(pip != PipId::null());
            self.bind_pip_internal(arc.net(), wire, pip);
            wire = *self.wire_to_idx.get(&ctx.pip_src_wire(pip)).unwrap();
        }

        self.reset_wires();
    }

    fn was_visited_fwd(&self, wire: u32) -> bool {
        self.flat_wires[wire as usize].visited_fwd
    }

    fn set_visited_fwd(&mut self, wire: u32, pip: PipId) {
        let wd = &mut self.flat_wires[wire as usize];
        if !wd.visited_fwd {
            self.dirty_wires.push(wire);
        }
        wd.pip_fwd = pip;
        wd.visited_fwd = true;
    }

    fn bind_pip_internal(&mut self, net: NetIndex, wire: u32, pip: PipId) {
        let wireid = self.flat_wires[wire as usize].wire;
        let net = &mut self.nets[net.into_inner() as usize];
        if let Some((bound_pip, usage)) = net.wires.get_mut(&wireid) {
            assert!(*bound_pip == pip);
            *usage += 1;
        } else {
            net.wires.insert(wireid, (pip, 1));
            self.flat_wires[wire as usize].curr_cong += 1;
        }
    }
    
    fn reset_wires(&mut self) {
        for &wire in &self.dirty_wires {
            self.flat_wires[wire as usize].pip_fwd = PipId::null();
            self.flat_wires[wire as usize].visited_fwd = false;
        }
        self.dirty_wires.clear();
    }
    /*
    void unbind_pip_internal(PerNetData &net, store_index<PortRef> user, WireId wire)
    {
        auto &wd = wire_data(wire);
        auto &b = net.wires.at(wd.w);
        --b.second;
        if (b.second == 0) {
            // No remaining arcs of this net bound to this wire
            --wd.curr_cong;
            net.wires.erase(wd.w);
        }
    }
    */
}
