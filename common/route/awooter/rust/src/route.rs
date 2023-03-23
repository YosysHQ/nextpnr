use std::{
    collections::{BinaryHeap, HashMap, HashSet},
    sync::RwLock,
    time::Instant,
};

use colored::Colorize;
use indicatif::{MultiProgress, ProgressBar, ProgressStyle};
use itertools::Itertools;

use crate::{
    npnr::{self, IdString, Loc, NetIndex, PipId, WireId},
    partition,
};

#[derive(Clone, Hash, PartialEq, Eq)]
pub struct Arc {
    source_wire: WireId,
    source_loc: Option<Loc>,
    sink_wire: WireId,
    sink_loc: Option<Loc>,
    net: NetIndex,
    name: IdString,
}

impl Arc {
    pub fn new(
        source_wire: WireId,
        source_loc: Option<Loc>,
        sink_wire: WireId,
        sink_loc: Option<Loc>,
        net: NetIndex,
        name: IdString,
    ) -> Self {
        Self {
            source_wire,
            source_loc,
            sink_wire,
            sink_loc,
            net,
            name,
        }
    }

    pub fn split(&self, ctx: &npnr::Context, pip: npnr::PipId) -> (Self, Self) {
        // should this still set the sink and source using the pip? not sure
        let pip_src = ctx.pip_src_wire(pip);
        let pip_dst = ctx.pip_dst_wire(pip);
        (
            Self {
                source_wire: self.source_wire,
                source_loc: self.source_loc,
                sink_wire: pip_src,
                sink_loc: Some(ctx.pip_location(pip)),
                net: self.net,
                name: self.name,
            },
            Self {
                source_wire: pip_dst,
                source_loc: Some(ctx.pip_location(pip)),
                sink_wire: self.sink_wire,
                sink_loc: self.sink_loc,
                net: self.net,
                name: self.name,
            },
        )
    }

    pub fn source_wire(&self) -> npnr::WireId {
        self.source_wire
    }
    pub fn sink_wire(&self) -> npnr::WireId {
        self.sink_wire
    }
    pub fn net(&self) -> npnr::NetIndex {
        self.net
    }
    pub fn get_source_loc(&self) -> Option<Loc> {
        self.source_loc
    }
    pub fn get_sink_loc(&self) -> Option<Loc> {
        self.sink_loc
    }
}

#[derive(Copy, Clone)]
struct QueuedWire {
    delay: f32,
    congest: f32,
    togo: f32,
    criticality: f32,
    wire: npnr::WireId,
    from_pip: Option<npnr::PipId>,
}

impl QueuedWire {
    pub fn new(
        delay: f32,
        congest: f32,
        togo: f32,
        criticality: f32,
        wire: npnr::WireId,
        from_pip: Option<npnr::PipId>,
    ) -> Self {
        Self {
            delay,
            congest,
            togo,
            criticality,
            wire,
            from_pip,
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
    done_sinks: HashSet<WireId>,
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

pub struct RouterThread<'a> {
    box_ne: partition::Coord,
    box_sw: partition::Coord,
    arcs: &'a [Arc],
    id: &'a str,
    progress: &'a MultiProgress,
    dirty_wires: Vec<u32>,
}

impl<'a> RouterThread<'a> {
    pub fn new(
        box_ne: partition::Coord,
        box_sw: partition::Coord,
        arcs: &'a [Arc],
        id: &'a str,
        progress: &'a MultiProgress,
    ) -> Self {
        Self {
            box_ne,
            box_sw,
            arcs,
            id,
            progress,
            dirty_wires: Vec::new(),
        }
    }
}

pub struct Router {
    pressure: f32,
    history: f32,
    nets: RwLock<Vec<PerNetData>>,
    wire_to_idx: HashMap<WireId, u32>,
    flat_wires: Vec<RwLock<PerWireData>>,
}

impl Router {
    pub fn new(nets: &npnr::Nets, wires: &[npnr::WireId], pressure: f32, history: f32) -> Self {
        let mut net_vec = Vec::new();
        let mut flat_wires = Vec::new();
        let mut wire_to_idx = HashMap::new();

        for _ in 0..nets.len() {
            net_vec.push(PerNetData {
                wires: HashMap::new(),
                done_sinks: HashSet::new(),
            });
        }

        for (idx, &wire) in wires.iter().enumerate() {
            flat_wires.push(RwLock::new(PerWireData {
                wire,
                curr_cong: 0,
                hist_cong: 0.0,
                unavailable: false,
                reserved_net: None,
                pip_fwd: PipId::null(),
                visited_fwd: false,
                pip_bwd: PipId::null(),
                visited_bwd: false,
            }));
            wire_to_idx.insert(wire, idx as u32);
        }

        Self {
            pressure,
            history,
            nets: RwLock::new(net_vec),
            wire_to_idx,
            flat_wires,
        }
    }

    pub fn find_general_routing(&self, ctx: &npnr::Context, nets: &npnr::Nets, this: &mut RouterThread) -> Vec<Arc> {
        let is_general_routing = |wire: &str| {
            wire.contains("H01")
                || wire.contains("V01")
                || wire.contains("H02")
                || wire.contains("V02")
                || wire.contains("H06")
                || wire.contains("V06")
        };

        let mut delay = HashMap::new();

        for arc in this.arcs {
            delay.insert(arc, 1.0_f32);
        }

        let start = Instant::now();

        let mut max_delay = 1.0;
        let mut least_overuse = usize::MAX;
        let mut iters_since_improvement = 0;

        let mut route_arcs = Vec::from_iter(this.arcs.iter());

        let progress = this.progress.add(ProgressBar::new(0));
        progress.set_style(
            ProgressStyle::with_template("[{elapsed}] [{bar:40.green/green}] {msg:30!}")
                .unwrap()
                .progress_chars("━╸ "),
        );

        let mut new_arcs = Vec::new();
        let mut iterations = 0;

        loop {
            iterations += 1;

            progress.set_position(0);
            progress.set_length(route_arcs.len() as u64);

            for arc in route_arcs.iter().sorted_by(|&i, &j| {
                (delay.get(j).unwrap() / max_delay).total_cmp(&(delay.get(i).unwrap() / max_delay))
            }) {
                let name = ctx.name_of(arc.name).to_str().unwrap();
                progress.inc(1);
                let criticality = (delay.get(arc).unwrap() / max_delay).min(0.99).powf(2.5) + 0.1;
                progress.set_message(format!("{} @ {}: {}", this.id, iterations, name));
                *delay.get_mut(arc).unwrap() = self.route_arc(ctx, nets, arc, criticality);
            }

            let mut overused = HashSet::new();
            for wd in self.flat_wires.iter() {
                let mut wd = wd.write().unwrap();
                if wd.curr_cong > 1 && !is_general_routing(ctx.name_of_wire(wd.wire)) {
                    overused.insert(wd.wire);
                    wd.hist_cong += (wd.curr_cong as f32) * self.history;
                }
            }

            if overused.is_empty() {
                break;
            } else if overused.len() < least_overuse {
                least_overuse = overused.len();
                iters_since_improvement = 0;
                progress.println(format!(
                    "{} @ {}: {} wires overused {}",
                    this.id,
                    iterations,
                    overused.len(),
                    "(new best)".bold()
                ));
            } else {
                iters_since_improvement += 1;
                progress.println(format!(
                    "{} @ {}: {} wires overused",
                    this.id,
                    iterations,
                    overused.len()
                ));
            }

            let mut next_arcs = HashSet::new();
            for arc in this.arcs {
                let nets = &*self.nets.read().unwrap();
                for wire in nets[arc.net.into_inner() as usize]
                    .wires
                    .keys()
                {
                    if overused.contains(wire) {
                        next_arcs.insert(arc);
                    }
                }
            }

            for &arc in &next_arcs {
                self.ripup_arc(ctx, arc);
            }

            {
                let nets = &mut *self.nets.write().unwrap();
                for net in nets.iter_mut() {
                    net.done_sinks.clear();
                }
            }

            if iters_since_improvement > 50 {
                iters_since_improvement = 0;
                least_overuse = usize::MAX;
                progress.println(format!(
                    "{} @ {}: {}",
                    this.id,
                    iterations,
                    "bored; rerouting everything".bold()
                ));
                route_arcs = Vec::from_iter(this.arcs.iter());
            } else {
                route_arcs = Vec::from_iter(next_arcs.into_iter());
            }

            max_delay = this
                .arcs
                .iter()
                .map(|arc| *delay.get(arc).unwrap())
                .reduce(f32::max)
                .unwrap();
        }

        let now = start.elapsed().as_secs_f32();
        progress.println(format!(
            "{} @ {}: {} in {:.0}m{:.03}s",
            this.id,
            iterations,
            "pre-routing complete".green(),
           (now / 60.0).floor(),
            now % 60.0
        ));

        progress.finish_and_clear();

        for arc in this.arcs {
            let (source_wire, sink_wire) = self.ripup_arc_general_routing(ctx, arc);
            new_arcs.push(Arc::new(source_wire, None, sink_wire, None, arc.net, arc.name));
        }

        new_arcs
    }

    pub fn route(&self, ctx: &npnr::Context, nets: &npnr::Nets, this: &mut RouterThread) {
        let mut delay = HashMap::new();

        for arc in this.arcs {
            delay.insert(arc, 1.0_f32);
        }

        let start = Instant::now();

        let mut max_delay = 1.0;
        let mut least_overuse = usize::MAX;
        let mut iters_since_improvement = 0;

        let mut route_arcs = Vec::from_iter(this.arcs.iter());

        let progress = this.progress.add(ProgressBar::new(0));
        progress.set_style(
            ProgressStyle::with_template("[{elapsed}] [{bar:40.magenta/red}] {msg:30!}")
                .unwrap()
                .progress_chars("━╸ "),
        );

        let mut iterations = 0;

        loop {
            iterations += 1;

            progress.set_position(0);
            progress.set_length(route_arcs.len() as u64);

            for arc in route_arcs.iter().sorted_by(|&i, &j| {
                (delay.get(j).unwrap() / max_delay).total_cmp(&(delay.get(i).unwrap() / max_delay))
            }) {
                let name = ctx.name_of(arc.name).to_str().unwrap();
                progress.inc(1);
                let criticality = (delay.get(arc).unwrap() / max_delay);
                progress.set_message(format!("{} @ {}: {}", this.id, iterations, name));
                *delay.get_mut(arc).unwrap() = self.route_arc(ctx, nets, arc, criticality);
            }

            let mut overused = HashSet::new();
            for wd in self.flat_wires.iter() {
                let mut wd = wd.write().unwrap();
                if wd.curr_cong > 1 {
                    overused.insert(wd.wire);
                    wd.hist_cong += (wd.curr_cong as f32) * self.history;
                }
            }

            if overused.is_empty() {
                break;
            } else if overused.len() < least_overuse {
                least_overuse = overused.len();
                iters_since_improvement = 0;
                progress.println(format!(
                    "{} @ {}: {} wires overused {}",
                    this.id,
                    iterations,
                    overused.len(),
                    "(new best)".bold()
                ));
            } else {
                iters_since_improvement += 1;
                progress.println(format!(
                    "{} @ {}: {} wires overused",
                    this.id,
                    iterations,
                    overused.len()
                ));
            }

            let mut next_arcs = HashSet::new();
            for arc in this.arcs {
                let nets = &*self.nets.read().unwrap();
                for wire in nets[arc.net.into_inner() as usize]
                    .wires
                    .keys()
                {
                    if overused.contains(wire) {
                        next_arcs.insert(arc);
                    }
                }
            }

            for &arc in &next_arcs {
                self.ripup_arc(ctx, arc);
            }
            {
                let nets = &mut *self.nets.write().unwrap();
                for net in nets.iter_mut() {
                    net.done_sinks.clear();
                }
            }

            if iters_since_improvement > 50 {
                iters_since_improvement = 0;
                least_overuse = usize::MAX;
                progress.println(format!(
                    "{} @ {}: {}",
                    this.id,
                    iterations,
                    "bored; rerouting everything".bold()
                ));
                route_arcs = Vec::from_iter(this.arcs.iter());
            } else {
                route_arcs = Vec::from_iter(next_arcs.into_iter());
            }

            max_delay = this
                .arcs
                .iter()
                .map(|arc| *delay.get(arc).unwrap())
                .reduce(f32::max)
                .unwrap();
        }

        let now = (Instant::now() - start).as_secs_f32();
        progress.println(format!(
            "{} @ {}: {} in {:.0}m{:.03}s",
            this.id,
            iterations,
            "routing complete".green(),
            now / 60.0,
            now % 60.0
        ));

        progress.finish_and_clear();
    }

    fn can_visit_pip(&self, ctx: &npnr::Context, nets: &npnr::Nets, arc: &Arc, pip: PipId) -> bool {
        let wire = ctx.pip_dst_wire(pip);
        let sink = *self.wire_to_idx.get(&wire).unwrap();
        let nd = &self.nets.read().unwrap()[arc.net().into_inner() as usize];
        let nwd = &self.flat_wires[sink as usize].read().unwrap();
        /*let pip_coord = partition::Coord::from(ctx.pip_location(pip));
        if pip_coord.is_north_of(&self.box_ne) || pip_coord.is_east_of(&self.box_ne) {
            return false;
        }
        if pip_coord.is_south_of(&self.box_sw) || pip_coord.is_west_of(&self.box_sw) {
            return false;
        }*/
        if !ctx.pip_avail_for_net(pip, nets.net_from_index(arc.net())) {
            return false;
        }
        if nwd.unavailable {
            return false;
        }
        if let Some(net) = nwd.reserved_net && net != arc.net() {
            return false;
        }
        // Don't allow the same wire to be bound to the same net with a different driving pip
        if let Some((found_pip, _)) = nd.wires.get(&wire) && *found_pip != pip {
            return false;
        }
        true
    }

    #[allow(clippy::too_many_arguments)]
    fn step<'b, 'a: 'b, I>(
        &'a self,
        ctx: &'b npnr::Context,
        nets: &npnr::Nets,
        arc: &Arc,
        criticality: f32,
        queue: &mut BinaryHeap<QueuedWire>,
        midpoint: &mut Option<u32>,
        target: WireId,
        dirty_wires: &mut Vec<u32>,
        was_visited: impl Fn(&Self, u32) -> bool,
        set_visited: impl Fn(&Self, u32, PipId, &mut Vec<u32>),
        is_done: impl Fn(&Self, u32) -> bool,
        pip_iter: impl Fn(&'b npnr::Context, WireId) -> I,
        pip_wire: impl Fn(&npnr::Context, PipId) -> WireId,
    ) -> bool
    where
        I: Iterator<Item = PipId>,
    {
        if let Some(source) = queue.pop() {
            let source_idx = *self.wire_to_idx.get(&source.wire).unwrap();
            if was_visited(self, source_idx) {
                return true;
            }
            if let Some(pip) = source.from_pip {
                set_visited(self, source_idx, pip, dirty_wires);
            }
            if is_done(self, source_idx) {
                *midpoint = Some(source_idx);
                return false;
            }

            for pip in pip_iter(ctx, source.wire) {
                if !self.can_visit_pip(ctx, nets, arc, pip) {
                    continue;
                }

                let wire = pip_wire(ctx, pip);
                let sink = *self.wire_to_idx.get(&wire).unwrap();
                if was_visited(self, sink) {
                    continue;
                }

                let nwd = &self.flat_wires[sink as usize].read().unwrap();
                let node_delay = ctx.pip_delay(pip) + ctx.wire_delay(wire) + ctx.delay_epsilon();
                let sum_delay = source.delay + node_delay;
                let congest = source.congest
                    + (node_delay + nwd.hist_cong) * (1.0 + (nwd.curr_cong as f32 * self.pressure));

                let qw = QueuedWire::new(
                    sum_delay,
                    congest,
                    ctx.estimate_delay(wire, target),
                    criticality,
                    wire,
                    Some(pip),
                );

                queue.push(qw);
            }

            return true;
        }
        false
    }

    fn route_arc(
        &self,
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
            None,
        ));
        let mut bwd_queue = BinaryHeap::new();
        bwd_queue.push(QueuedWire::new(
            0.0,
            0.0,
            ctx.estimate_delay(arc.source_wire, arc.sink_wire),
            criticality,
            arc.sink_wire,
            None,
        ));

        let mut midpoint = None;

        let source_wire = *self.wire_to_idx.get(&arc.source_wire).unwrap();
        let sink_wire = *self.wire_to_idx.get(&arc.sink_wire).unwrap();

        let mut dirty_wires = Vec::new();

        dirty_wires.push(source_wire);
        dirty_wires.push(sink_wire);

        while midpoint.is_none() {
            // Step forward
            if !self.step(
                ctx,
                nets,
                arc,
                criticality,
                &mut fwd_queue,
                &mut midpoint,
                arc.sink_wire,
                &mut dirty_wires,
                Self::was_visited_fwd,
                Self::set_visited_fwd,
                Self::was_visited_bwd,
                npnr::Context::get_downhill_pips,
                npnr::Context::pip_dst_wire,
            ) {
                break;
            }
            // Step backward
            if !self.step(
                ctx,
                nets,
                arc,
                criticality,
                &mut bwd_queue,
                &mut midpoint,
                arc.source_wire,
                &mut dirty_wires,
                Self::was_visited_bwd,
                Self::set_visited_bwd,
                Self::was_visited_fwd,
                npnr::Context::get_uphill_pips,
                npnr::Context::pip_src_wire,
            ) {
                break;
            }
            self.flat_wires[source_wire as usize]
                .write()
                .unwrap()
                .visited_fwd = true;
            self.flat_wires[sink_wire as usize]
                .write()
                .unwrap()
                .visited_bwd = true;
        }

        assert!(
            midpoint.is_some(),
            "didn't find sink wire for net {} between {} and {}",
            ctx.name_of(arc.name).to_str().unwrap(),
            ctx.name_of_wire(arc.source_wire),
            ctx.name_of_wire(arc.sink_wire),
        );

        let mut wire = midpoint.unwrap();

        let mut calculated_delay = 0.0;

        while wire != source_wire {
            let (pip, wireid) = {
                let nwd = self.flat_wires[wire as usize].read().unwrap();
                (nwd.pip_fwd, nwd.wire)
            };
            assert!(pip != PipId::null());

            let node_delay = ctx.pip_delay(pip) + ctx.wire_delay(wireid) + ctx.delay_epsilon();
            calculated_delay += node_delay;

            self.bind_pip_internal(arc.net(), wire, pip);
            wire = *self.wire_to_idx.get(&ctx.pip_src_wire(pip)).unwrap();
        }
        let mut wire = midpoint.unwrap();
        while wire != sink_wire {
            let (pip, wireid) = {
                let nwd = self.flat_wires[wire as usize].read().unwrap();
                (nwd.pip_bwd, nwd.wire)
            };
            assert!(pip != PipId::null());
            // do note that the order is inverted from the fwd loop
            wire = *self.wire_to_idx.get(&ctx.pip_dst_wire(pip)).unwrap();

            let node_delay = ctx.pip_delay(pip) + ctx.wire_delay(wireid) + ctx.delay_epsilon();
            calculated_delay += node_delay;

            self.bind_pip_internal(arc.net(), wire, pip);
        }
        self.nets.write().unwrap()[arc.net().into_inner() as usize]
            .done_sinks
            .insert(arc.sink_wire);

        self.reset_wires(&dirty_wires);

        calculated_delay
    }

    fn was_visited_fwd(&self, wire: u32) -> bool {
        self.flat_wires[wire as usize].read().unwrap().visited_fwd
    }

    fn was_visited_bwd(&self, wire: u32) -> bool {
        self.flat_wires[wire as usize].read().unwrap().visited_bwd
    }

    fn set_visited_fwd(&self, wire: u32, pip: PipId, dirty_wires: &mut Vec<u32>) {
        let mut wd = self.flat_wires[wire as usize].write().unwrap();
        if !wd.visited_fwd {
            dirty_wires.push(wire);
        }
        wd.pip_fwd = pip;
        wd.visited_fwd = true;
    }

    fn set_visited_bwd(&self, wire: u32, pip: PipId, dirty_wires: &mut Vec<u32>) {
        let mut wd = self.flat_wires[wire as usize].write().unwrap();
        if !wd.visited_bwd {
            dirty_wires.push(wire);
        }
        wd.pip_bwd = pip;
        wd.visited_bwd = true;
    }

    fn bind_pip_internal(&self, netindex: NetIndex, wire: u32, pip: PipId) {
        let wireid = self.flat_wires[wire as usize].read().unwrap().wire;
        let net = &mut self.nets.write().unwrap()[netindex.into_inner() as usize];
        if let Some((bound_pip, usage)) = net.wires.get_mut(&wireid) {
            assert!(*bound_pip == pip);
            *usage += 1;
        } else {
            net.wires.insert(wireid, (pip, 1));
            self.flat_wires[wire as usize].write().unwrap().curr_cong += 1;
        }
    }

    fn unbind_pip_internal(&self, net: NetIndex, wire: WireId) {
        let net = net.into_inner() as usize;
        let wireidx = *self.wire_to_idx.get(&wire).unwrap() as usize;
        let nd = &mut self.nets.write().unwrap()[net];
        let (_pip, usage) = nd.wires.get_mut(&wire).unwrap();
        *usage -= 1;
        if *usage == 0 {
            self.flat_wires[wireidx].write().unwrap().curr_cong -= 1;
            nd.wires.remove(&wire);
        }
    }

    fn ripup_arc(&self, ctx: &npnr::Context, arc: &Arc) {
        let net = arc.net().into_inner() as usize;
        let source_wire = arc.source_wire;
        let mut wire = arc.sink_wire;
        while wire != source_wire {
            let pip = self.nets.read().unwrap()[net].wires.get(&wire).unwrap().0;
            assert!(pip != PipId::null());
            self.unbind_pip_internal(arc.net(), wire);
            wire = ctx.pip_src_wire(pip);
        }
    }

    fn ripup_arc_general_routing(&self, ctx: &npnr::Context, arc: &Arc) -> (WireId, WireId) {
        let is_general_routing = |wire: WireId| {
            let wire = ctx.name_of_wire(wire);
            wire.contains("H01")
                || wire.contains("V01")
                || wire.contains("H02")
                || wire.contains("V02")
                || wire.contains("H06")
                || wire.contains("V06")
        };

        let net = arc.net().into_inner() as usize;
        let source_wire = arc.source_wire;
        let mut wire = arc.sink_wire;
        let mut last_was_general = false;
        let mut w1 = arc.sink_wire;
        let mut w2 = arc.source_wire;
        while wire != source_wire {
            let pip = self.nets.read().unwrap()[net].wires.get(&wire).expect("wire should have driving pip").0;
            assert!(pip != PipId::null());
            if is_general_routing(wire) {
                if !last_was_general {
                    w1 = wire;
                }
                self.unbind_pip_internal(arc.net(), wire);
                last_was_general = true;
            } else {
                if last_was_general {
                    w2 = wire;
                }
                last_was_general = false;
            }
            wire = ctx.pip_src_wire(pip);
        }
        (w2, w1)
    }

    fn reset_wires(&self, dirty_wires: &Vec<u32>) {
        for &wire in dirty_wires {
            let mut nwd = self.flat_wires[wire as usize].write().unwrap();
            nwd.pip_fwd = PipId::null();
            nwd.visited_fwd = false;
            nwd.pip_bwd = PipId::null();
            nwd.visited_bwd = false;
        }
    }
}
