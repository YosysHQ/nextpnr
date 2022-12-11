use std::{
    collections::{BinaryHeap, HashMap, HashSet},
    time::Instant,
};

use colored::Colorize;
use indicatif::{MultiProgress, ProgressBar, ProgressStyle};
use itertools::Itertools;

use crate::{
    npnr::{self, NetIndex, PipId, WireId},
    partition,
};

#[derive(Clone, Hash, PartialEq, Eq)]
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
    delay: f32,
    congest: f32,
    togo: f32,
    criticality: f32,
    wire: npnr::WireId,
}

impl QueuedWire {
    pub fn new(delay: f32, congest: f32, togo: f32, criticality: f32, wire: npnr::WireId) -> Self {
        Self {
            delay,
            congest,
            togo,
            criticality,
            wire,
        }
    }

    fn score(&self) -> f32 {
        (self.criticality * self.delay) + ((1.0 - self.criticality) * self.congest)
    }
}

impl PartialEq for QueuedWire {
    fn eq(&self, other: &Self) -> bool {
        self.delay == other.delay
            && self.congest == other.congest
            && self.togo == other.togo
            && self.wire == other.wire
    }
}

impl Eq for QueuedWire {}

impl Ord for QueuedWire {
    fn cmp(&self, other: &Self) -> std::cmp::Ordering {
        let me = self.score() + self.togo;
        let other = other.score() + other.togo;
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
    done_sinks: HashMap<WireId, f32>,
}

struct PerWireData {
    wire: WireId,
    curr_cong: u32,
    hist_cong: f32,
    unavailable: bool,
    reserved_net: Option<NetIndex>,
    pip_fwd: PipId,
    visited_fwd: bool,
    pip_bwd: PipId,
    visited_bwd: bool,
}

pub struct Router {
    box_ne: partition::Coord,
    box_sw: partition::Coord,
    pressure: f32,
    history: f32,
    nets: Vec<PerNetData>,
    wire_to_idx: HashMap<WireId, u32>,
    flat_wires: Vec<PerWireData>,
    dirty_wires: Vec<u32>,
}

impl Router {
    pub fn new(
        box_ne: partition::Coord,
        box_sw: partition::Coord,
        pressure: f32,
        history: f32,
    ) -> Self {
        Self {
            box_ne,
            box_sw,
            pressure,
            history,
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
        id: &str,
    ) {
        for _ in 0..nets.len() {
            self.nets.push(PerNetData {
                wires: HashMap::new(),
                done_sinks: HashMap::new(),
            });
        }

        for (idx, &wire) in wires.iter().enumerate() {
            self.flat_wires.push(PerWireData {
                wire,
                curr_cong: 0,
                hist_cong: 0.0,
                unavailable: false,
                reserved_net: None,
                pip_fwd: PipId::null(),
                visited_fwd: false,
                pip_bwd: PipId::null(),
                visited_bwd: false,
            });
            self.wire_to_idx.insert(wire, idx as u32);
        }

        let mut delay = HashMap::new();

        for arc in arcs {
            delay.insert(arc, 1.0_f32);
        }

        let start = Instant::now();

        let mut max_delay = 1.0;
        let mut least_overuse = usize::MAX;
        let mut iters_since_improvement = 0;

        let mut route_arcs = Vec::from_iter(arcs.iter());

        let mut iterations = 0;

        loop {
            iterations += 1;

            let progress = progress.add(ProgressBar::new(route_arcs.len() as u64));
            progress.set_style(
                ProgressStyle::with_template("[{elapsed}] [{bar:40.magenta/red}] {msg:30!}")
                    .unwrap()
                    .progress_chars("━╸ "),
            );

            for arc in route_arcs.iter().sorted_by(|&i, &j| {
                (delay.get(j).unwrap() / max_delay).total_cmp(&(delay.get(i).unwrap() / max_delay))
            }) {
                let net = unsafe { nets.net_from_index(arc.net).as_ref().unwrap() };
                let name = ctx.name_of(nets.name_from_index(arc.net)).to_str().unwrap();

                if net.is_global() {
                    continue;
                }

                //log_info!("{}\n", name);
                //log_info!("  {} to {}\n", ctx.name_of_wire(arc.source_wire).to_str().unwrap(), ctx.name_of_wire(arc.sink_wire).to_str().unwrap());

                let criticality = (delay.get(arc).unwrap() / max_delay).min(0.99).powf(2.5) + 0.1;
                progress.inc(1);
                progress.set_message(format!("{} @ {}: {}", id, iterations, name));
                *delay.get_mut(arc).unwrap() = self.route_arc(ctx, nets, arc, criticality);
            }
            progress.finish_and_clear();

            let mut overused = HashSet::new();
            for wd in &mut self.flat_wires {
                if wd.curr_cong > 1 {
                    overused.insert(wd.wire);
                    wd.hist_cong += (wd.curr_cong as f32) * self.history;
                    if false {
                        log_info!(
                            "wire {} has overuse {}\n",
                            ctx.name_of_wire(wd.wire).to_str().unwrap(),
                            wd.curr_cong
                        );
                    }
                }
            }

            if overused.is_empty() {
                let now = (Instant::now() - start).as_secs_f32();
                progress.println(format!(
                    "{} @ {}: {} in {:.0}m{:.03}s",
                    id,
                    iterations,
                    "routing complete".green(),
                    now / 60.0,
                    now % 60.0
                ));
                break;
            } else if overused.len() < least_overuse {
                least_overuse = overused.len();
                iters_since_improvement = 0;
                progress.println(format!(
                    "{} @ {}: {} wires overused {}",
                    id,
                    iterations,
                    overused.len(),
                    "(new best)".bold()
                ));
            } else {
                iters_since_improvement += 1;
                progress.println(format!(
                    "{} @ {}: {} wires overused",
                    id,
                    iterations,
                    overused.len()
                ));
            }

            let mut next_arcs = Vec::new();
            for arc in arcs {
                for wire in self.nets[arc.net.into_inner() as usize].wires.keys() {
                    if overused.contains(wire) {
                        next_arcs.push(arc);
                    }
                }
            }

            for &arc in &route_arcs {
                self.ripup_arc(ctx, arc);
            }
            for net in &mut self.nets {
                net.done_sinks.clear();
            }

            if iters_since_improvement > 50 {
                iters_since_improvement = 0;
                least_overuse = usize::MAX;
                progress.println(format!(
                    "{} @ {}: {}",
                    id,
                    iterations,
                    "bored; rerouting everything".bold()
                ));
                route_arcs = Vec::from_iter(arcs.iter());
            } else {
                route_arcs = next_arcs;
            }

            max_delay = arcs
                .iter()
                .map(|arc| *delay.get(arc).unwrap())
                .reduce(f32::max)
                .unwrap();
        }
    }

    fn route_arc(
        &mut self,
        ctx: &npnr::Context,
        nets: &npnr::Nets,
        arc: &Arc,
        criticality: f32,
    ) -> f32 {
        if arc.source_wire == arc.sink_wire {
            return 0.0;
        }

        let mut fwd_queue = BinaryHeap::new();
        fwd_queue.push(QueuedWire::new(
            0.0,
            0.0,
            ctx.estimate_delay(arc.source_wire, arc.sink_wire),
            criticality,
            arc.source_wire,
        ));
        let mut bwd_queue = BinaryHeap::new();
        bwd_queue.push(QueuedWire::new(
            0.0,
            0.0,
            ctx.estimate_delay(arc.source_wire, arc.sink_wire),
            criticality,
            arc.sink_wire,
        ));

        let mut found_meeting_point = None;
        let nd = &mut self.nets[arc.net().into_inner() as usize];

        let name = ctx
            .name_of(nets.name_from_index(arc.net))
            .to_str()
            .unwrap()
            .to_string();
        let verbose = ctx.verbose(); //false; //name == "soc0.processor.with_fpu.fpu_0.fpu_multiply_0.rin_CCU2C_S0_4$CCU2_FCI_INT";

        let source_wire = *self.wire_to_idx.get(&arc.source_wire).unwrap();
        let sink_wire = *self.wire_to_idx.get(&arc.sink_wire).unwrap();

        self.flat_wires[source_wire as usize].visited_fwd = true;
        self.flat_wires[sink_wire as usize].visited_bwd = true;
        self.dirty_wires.push(source_wire);
        self.dirty_wires.push(sink_wire);

        let mut delay = 0.0;
        if let Some(old_delay) = nd.done_sinks.get(&arc.get_sink_wire()) {
            found_meeting_point = Some(*self.wire_to_idx.get(&arc.sink_wire).unwrap());
            delay = *old_delay;

            let source = arc.get_source_wire();
            let mut wire = arc.get_sink_wire();
            while wire != source {
                let nd = &mut self.nets[arc.net().into_inner() as usize];
                let (driver, _) = nd.wires.get(&wire).unwrap();
                let driver = *driver;
                self.set_visited_fwd(self.wire_to_idx[&wire], driver);
                wire = ctx.pip_src_wire(driver);
            }
        } else {
            while found_meeting_point.is_none() {
                if let Some(source) = fwd_queue.pop() {
                    if verbose {
                        let source_idx = *self.wire_to_idx.get(&source.wire).unwrap();
                        let source_cong = self.flat_wires[source_idx as usize].curr_cong;
                        log_info!(
                            "fwd: {} @ ({}, {}, {}) = {}\n",
                            ctx.name_of_wire(source.wire).to_str().unwrap(),
                            source.delay,
                            source.congest,
                            source.criticality,
                            source.score()
                        );
                    }

                    for pip in ctx.get_downhill_pips(source.wire) {
                        let pip_loc = ctx.pip_location(pip);
                        let pip_coord = partition::Coord::from(pip_loc);
                        if pip_coord.is_north_of(&self.box_ne) || pip_coord.is_east_of(&self.box_ne)
                        {
                            /*if verbose {
                                log_info!("    out-of-bounds (NE)\n");
                            }*/
                            continue;
                        }
                        if pip_coord.is_south_of(&self.box_sw) || pip_coord.is_west_of(&self.box_sw)
                        {
                            /*if verbose {
                                log_info!("    out-of-bounds (SW)\n");
                            }*/
                            continue;
                        }
                        if !ctx.pip_avail_for_net(pip, nets.net_from_index(arc.net())) {
                            /*if verbose {
                                log_info!("    pip unavailable for net\n");
                            }*/
                            continue;
                        }
                        let wire = ctx.pip_dst_wire(pip);
                        let sink = *self.wire_to_idx.get(&wire).unwrap();
                        if self.was_visited_fwd(sink) {
                            /*if verbose {
                                log_info!("    already visited\n");
                            }*/
                            continue;
                        }
                        let nd = &mut self.nets[arc.net().into_inner() as usize];
                        let nwd = &self.flat_wires[sink as usize];
                        if nwd.unavailable {
                            /*if verbose {
                                log_info!("    unavailable\n");
                            }*/
                            continue;
                        }
                        if let Some(net) = nwd.reserved_net && net != arc.net() {
                        /*if verbose {
                            log_info!("    reserved for other net\n");
                        }*/
                        continue;
                    }
                        // Don't allow the same wire to be bound to the same net with a different driving pip
                        if let Some((found_pip, _)) = nd.wires.get(&wire) && *found_pip != pip {
                        /*if verbose {
                            log_info!("    driven by other pip\n");
                        }*/
                        continue;
                    }

                        let node_delay =
                            ctx.pip_delay(pip) + ctx.wire_delay(wire) + ctx.delay_epsilon();
                        let sum_delay = source.delay + node_delay;
                        let congest = source.congest
                            + (node_delay + nwd.hist_cong)
                                * (1.0 + (nwd.curr_cong as f32 * self.pressure));

                        let qw = QueuedWire::new(
                            sum_delay,
                            congest,
                            ctx.estimate_delay(wire, arc.sink_wire),
                            criticality,
                            wire,
                        );

                        self.set_visited_fwd(sink, pip);

                        if self.was_visited_bwd(sink) {
                            if verbose {
                                let source_cong = self.flat_wires[sink as usize].curr_cong;
                                log_info!(
                                    "bwd: {} @ ({}, {}, {}) = {}\n",
                                    ctx.name_of_wire(wire).to_str().unwrap(),
                                    sum_delay,
                                    congest,
                                    criticality,
                                    qw.score()
                                );
                            }

                            found_meeting_point = Some(sink);
                            delay = sum_delay;
                            break;
                        }

                        fwd_queue.push(qw);

                        if false && verbose {
                            log_info!(
                                "  bwd: {}: -> {} @ ({}, {}, {}) = {}\n",
                                ctx.name_of_pip(pip).to_str().unwrap(),
                                ctx.name_of_wire(ctx.pip_dst_wire(pip)).to_str().unwrap(),
                                delay,
                                congest,
                                criticality,
                                qw.score()
                            );
                        }
                    }
                } else {
                    break;
                }
                if let Some(sink) = bwd_queue.pop() {
                    if verbose {
                        let sink_idx = *self.wire_to_idx.get(&sink.wire).unwrap();
                        let sink_cong = self.flat_wires[sink_idx as usize].curr_cong;
                        log_info!(
                            "bwd: {} @ ({}, {}, {}) = {}\n",
                            ctx.name_of_wire(sink.wire).to_str().unwrap(),
                            sink.delay,
                            sink.congest,
                            sink.criticality,
                            sink.score()
                        );
                    }

                    for pip in ctx.get_uphill_pips(sink.wire) {
                        let pip_loc = ctx.pip_location(pip);
                        let pip_coord = partition::Coord::from(pip_loc);
                        if pip_coord.is_north_of(&self.box_ne) || pip_coord.is_east_of(&self.box_ne)
                        {
                            /*if verbose {
                                log_info!("    out-of-bounds (NE)\n");
                            }*/
                            continue;
                        }
                        if pip_coord.is_south_of(&self.box_sw) || pip_coord.is_west_of(&self.box_sw)
                        {
                            /*if verbose {
                                log_info!("    out-of-bounds (SW)\n");
                            }*/
                            continue;
                        }
                        if !ctx.pip_avail_for_net(pip, nets.net_from_index(arc.net())) {
                            /*if verbose {
                                log_info!("    pip unavailable for net\n");
                            }*/
                            continue;
                        }
                        let wire = ctx.pip_src_wire(pip);
                        let source = *self.wire_to_idx.get(&wire).unwrap();
                        if self.was_visited_bwd(source) {
                            /*if verbose {
                                log_info!("    already visited\n");
                            }*/
                            continue;
                        }
                        let nd = &mut self.nets[arc.net().into_inner() as usize];
                        let nwd = &self.flat_wires[source as usize];
                        if nwd.unavailable {
                            /*if verbose {
                                log_info!("    unavailable\n");
                            }*/
                            continue;
                        }
                        if let Some(net) = nwd.reserved_net && net != arc.net() {
                            /*if verbose {
                                log_info!("    reserved for other net\n");
                            }*/
                            continue;
                        }
                        // Don't allow the same wire to be bound to the same net with a different driving pip
                        if let Some((found_pip, _)) = nd.wires.get(&sink.wire) && *found_pip != pip {
                            /*if verbose {
                                log_info!("    driven by other pip\n");
                            }*/
                            continue;
                        }

                        let node_delay =
                            ctx.pip_delay(pip) + ctx.wire_delay(wire) + ctx.delay_epsilon();
                        let sum_delay = sink.delay + node_delay;
                        let congest = sink.congest
                            + (node_delay + nwd.hist_cong)
                                * (1.0 + (nwd.curr_cong as f32 * self.pressure));

                        let qw = QueuedWire::new(
                            sum_delay,
                            congest,
                            ctx.estimate_delay(wire, arc.source_wire),
                            criticality,
                            wire,
                        );

                        self.set_visited_bwd(source, pip);

                        if self.was_visited_fwd(source) {
                            if verbose {
                                let source_cong = self.flat_wires[source as usize].curr_cong;
                                log_info!(
                                    "bwd: {} @ ({}, {}, {}) = {}\n",
                                    ctx.name_of_wire(wire).to_str().unwrap(),
                                    sum_delay,
                                    congest,
                                    criticality,
                                    qw.score()
                                );
                            }

                            found_meeting_point = Some(source);
                            delay = sum_delay;
                            break;
                        }

                        bwd_queue.push(qw);

                        if false && verbose {
                            log_info!(
                                "  bwd: {}: -> {} @ ({}, {}, {}) = {}\n",
                                ctx.name_of_pip(pip).to_str().unwrap(),
                                ctx.name_of_wire(ctx.pip_dst_wire(pip)).to_str().unwrap(),
                                delay,
                                congest,
                                criticality,
                                qw.score()
                            );
                        }
                    }
                } else {
                    // don't break when bwd goes bad, fwd was written by lofty, who knows all, this was written by dummy kbity
                    //break;
                }
            }
        }

        assert!(
            found_meeting_point.is_some(),
            "didn't find sink wire for net {} between {} ({:?}) and {} ({:?})",
            name,
            ctx.name_of_wire(arc.source_wire).to_str().unwrap(),
            arc.source_loc,
            ctx.name_of_wire(arc.sink_wire).to_str().unwrap(),
            arc.sink_loc,
        );

        if verbose {
            println!(
                "{} [label=\"{}\"]",
                source_wire,
                ctx.name_of_wire(arc.source_wire).to_str().unwrap(),
                //self.flat_wires[wire as usize].curr_cong
            );
        }

        let mut wire = found_meeting_point.unwrap();
        if verbose {
            println!(
                "source: {} [label=\"{}\"]",
                source_wire,
                ctx.name_of_wire(self.flat_wires[source_wire as usize].wire)
                    .to_str()
                    .unwrap(),
                //self.flat_wires[wire as usize].curr_cong
            );
            println!(
                "sink: {} [label=\"{}\"]",
                sink_wire,
                ctx.name_of_wire(self.flat_wires[sink_wire as usize].wire)
                    .to_str()
                    .unwrap(),
                //self.flat_wires[wire as usize].curr_cong
            );
            println!(
                "middle: {} [label=\"{}\"]",
                wire,
                ctx.name_of_wire(self.flat_wires[wire as usize].wire)
                    .to_str()
                    .unwrap(),
                //self.flat_wires[wire as usize].curr_cong
            );
        }

        while wire != source_wire {
            if verbose {
                println!(
                    "{} [label=\"{}\"]",
                    wire,
                    ctx.name_of_wire(self.flat_wires[wire as usize].wire)
                        .to_str()
                        .unwrap(),
                    //self.flat_wires[wire as usize].curr_cong
                );
            }
            let pip = self.flat_wires[wire as usize].pip_fwd;
            assert!(pip != PipId::null());
            if verbose {
                println!(
                    "{} -> {}",
                    *self.wire_to_idx.get(&ctx.pip_src_wire(pip)).unwrap(),
                    wire
                );
            }
            self.bind_pip_internal(arc.net(), wire, pip);
            wire = *self.wire_to_idx.get(&ctx.pip_src_wire(pip)).unwrap();
        }
        let mut wire = found_meeting_point.unwrap();
        while wire != sink_wire {
            let pip = self.flat_wires[wire as usize].pip_bwd;
            assert!(pip != PipId::null());
            // do note that the order is inverted from the fwd loop
            wire = *self.wire_to_idx.get(&ctx.pip_dst_wire(pip)).unwrap();
            self.bind_pip_internal(arc.net(), wire, pip);
        }
        let nd = &mut self.nets[arc.net().into_inner() as usize];
        nd.done_sinks.insert(arc.get_sink_wire(), delay);

        self.reset_wires();

        delay
    }

    fn was_visited_fwd(&self, wire: u32) -> bool {
        self.flat_wires[wire as usize].visited_fwd
    }

    fn was_visited_bwd(&self, wire: u32) -> bool {
        self.flat_wires[wire as usize].visited_bwd
    }

    fn set_visited_fwd(&mut self, wire: u32, pip: PipId) {
        let wd = &mut self.flat_wires[wire as usize];
        if !wd.visited_fwd {
            self.dirty_wires.push(wire);
        }
        wd.pip_fwd = pip;
        wd.visited_fwd = true;
    }

    fn set_visited_bwd(&mut self, wire: u32, pip: PipId) {
        let wd = &mut self.flat_wires[wire as usize];
        if !wd.visited_bwd {
            self.dirty_wires.push(wire);
        }
        wd.pip_bwd = pip;
        wd.visited_bwd = true;
    }

    fn bind_pip_internal(&mut self, netindex: NetIndex, wire: u32, pip: PipId) {
        let wireid = self.flat_wires[wire as usize].wire;
        let net = &mut self.nets[netindex.into_inner() as usize];
        if let Some((bound_pip, usage)) = net.wires.get_mut(&wireid) {
            assert!(*bound_pip == pip);
            *usage += 1;
        } else {
            net.wires.insert(wireid, (pip, 1));
            self.flat_wires[wire as usize].curr_cong += 1;
        }
    }

    fn unbind_pip_internal(&mut self, net: NetIndex, wire: WireId) {
        let net = net.into_inner() as usize;
        let wireidx = *self.wire_to_idx.get(&wire).unwrap() as usize;
        let (_pip, usage) = self.nets[net].wires.get_mut(&wire).unwrap();
        *usage -= 1;
        if *usage == 0 {
            self.flat_wires[wireidx].curr_cong -= 1;
            self.nets[net].wires.remove(&wire);
        }
    }

    fn ripup_arc(&mut self, ctx: &npnr::Context, arc: &Arc) {
        let net = arc.net().into_inner() as usize;
        let source_wire = arc.source_wire;
        let mut wire = arc.sink_wire;
        while wire != source_wire {
            let pip = self.nets[net].wires.get(&wire).unwrap().0;
            assert!(pip != PipId::null());
            self.unbind_pip_internal(arc.net(), wire);
            wire = ctx.pip_src_wire(pip);
        }
    }

    fn reset_wires(&mut self) {
        for &wire in &self.dirty_wires {
            self.flat_wires[wire as usize].pip_fwd = PipId::null();
            self.flat_wires[wire as usize].visited_fwd = false;
            self.flat_wires[wire as usize].pip_bwd = PipId::null();
            self.flat_wires[wire as usize].visited_bwd = false;
        }
        self.dirty_wires.clear();
    }
}
