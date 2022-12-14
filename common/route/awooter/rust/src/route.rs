use std::{
    collections::{BinaryHeap, HashMap, HashSet},
    time::Instant,
};

use colored::Colorize;
use indicatif::{MultiProgress, ProgressBar, ProgressStyle};
use itertools::Itertools;

use crate::{
    npnr::{self, IdString, NetIndex, PipId, WireId},
    partition,
};

#[derive(Clone, Hash, PartialEq, Eq)]
pub struct Arc {
    source_wire: WireId,
    sink_wire: WireId,
    net: NetIndex,
    name: IdString,
}

impl Arc {
    pub fn new(
        source_wire: npnr::WireId,
        sink_wire: npnr::WireId,
        net: NetIndex,
        name: IdString,
    ) -> Self {
        Self {
            source_wire,
            sink_wire,
            net,
            name,
        }
    }

    pub fn split(&self, ctx: &npnr::Context, pip: npnr::PipId) -> (Self, Self) {
        let pip_src = ctx.pip_src_wire(pip);
        let pip_dst = ctx.pip_dst_wire(pip);
        (
            Self {
                source_wire: self.source_wire,
                sink_wire: pip_src,
                net: self.net,
                name: self.name,
            },
            Self {
                source_wire: pip_dst,
                sink_wire: self.sink_wire,
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
        }
    }
}

pub struct Router {
    pressure: f32,
    history: f32,
    nets: Vec<PerNetData>,
    wire_to_idx: HashMap<WireId, u32>,
    flat_wires: Vec<PerWireData>,
    dirty_wires: Vec<u32>,
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
            flat_wires.push(PerWireData {
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
            wire_to_idx.insert(wire, idx as u32);
        }

        Self {
            pressure,
            history,
            nets: net_vec,
            wire_to_idx: HashMap::new(),
            flat_wires: Vec::new(),
            dirty_wires: Vec::new(),
        }
    }

    pub fn route(&mut self, ctx: &npnr::Context, nets: &npnr::Nets, this: &RouterThread) {
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
                let criticality = (delay.get(arc).unwrap() / max_delay).min(0.99).powf(2.5) + 0.1;
                progress.set_message(format!("{} @ {}: {}", this.id, iterations, name));
                *delay.get_mut(arc).unwrap() = self.route_arc(ctx, nets, arc, criticality);
            }

            let mut overused = HashSet::new();
            for wd in self.flat_wires.iter_mut().filter(|wd| wd.curr_cong > 1) {
                overused.insert(wd.wire);
                wd.hist_cong += (wd.curr_cong as f32) * self.history;
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

            let mut next_arcs = Vec::new();
            for arc in this.arcs {
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
                    this.id,
                    iterations,
                    "bored; rerouting everything".bold()
                ));
                route_arcs = Vec::from_iter(this.arcs.iter());
            } else {
                route_arcs = next_arcs;
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
        let nd = &self.nets[arc.net().into_inner() as usize];
        let nwd = &self.flat_wires[sink as usize];
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

    fn step<F1, F2, F3, F4, F5, I>(
        &mut self,
        ctx: &npnr::Context,
        nets: &npnr::Nets,
        arc: &Arc,
        criticality: f32,
        queue: &mut BinaryHeap<QueuedWire>,
        midpoint: &mut Option<u32>,
        target: WireId,
        was_visited: F1,
        set_visited: F2,
        is_done: F3,
        pip_iter: F4,
        pip_wire: F5,
    ) -> bool
    where
        F1: Fn(&Self, u32) -> bool,
        F2: Fn(&mut Self, u32, PipId),
        F3: Fn(&Self, u32) -> bool,
        F4: Fn(&npnr::Context, WireId) -> I,
        F5: Fn(&npnr::Context, PipId) -> WireId,
        I: Iterator<Item = PipId>,
    {
        if let Some(source) = queue.pop() {
            let source_idx = *self.wire_to_idx.get(&source.wire).unwrap();
            if was_visited(self, source_idx) {
                return true;
            }
            if let Some(pip) = source.from_pip {
                set_visited(self, source_idx, pip);
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
                set_visited(self, sink, pip);
                let nwd = &self.flat_wires[sink as usize];
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

        self.dirty_wires.push(source_wire);
        self.dirty_wires.push(sink_wire);

        let nd = &self.nets[arc.net().into_inner() as usize];
        if nd.done_sinks.contains(&arc.sink_wire()) {
            midpoint = Some(*self.wire_to_idx.get(&arc.sink_wire).unwrap());

            let mut wire = arc.sink_wire();
            while wire != arc.source_wire() {
                let driver = nd.wires.get(&wire).unwrap().0;
                self.set_visited_fwd(self.wire_to_idx[&wire], driver);
                wire = ctx.pip_src_wire(driver);
            }
        } else {
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
                    Self::was_visited_bwd,
                    Self::set_visited_bwd,
                    Self::was_visited_fwd,
                    npnr::Context::get_uphill_pips,
                    npnr::Context::pip_src_wire,
                ) {
                    break;
                }
                self.flat_wires[source_wire as usize].visited_fwd = true;
                self.flat_wires[sink_wire as usize].visited_bwd = true;
            }
        }

        assert!(
            midpoint.is_some(),
            "didn't find sink wire for net {} between {} and {}",
            ctx.name_of(arc.name).to_str().unwrap(),
            ctx.name_of_wire(arc.source_wire).to_str().unwrap(),
            ctx.name_of_wire(arc.sink_wire).to_str().unwrap(),
        );

        let mut wire = midpoint.unwrap();

        let mut calculated_delay = 0.0;

        while wire != source_wire {
            let pip = self.flat_wires[wire as usize].pip_fwd;
            assert!(pip != PipId::null());

            let node_delay = ctx.pip_delay(pip)
                + ctx.wire_delay(self.flat_wires[wire as usize].wire)
                + ctx.delay_epsilon();
            calculated_delay += node_delay;

            self.bind_pip_internal(arc.net(), wire, pip);
            wire = *self.wire_to_idx.get(&ctx.pip_src_wire(pip)).unwrap();
        }
        let mut wire = midpoint.unwrap();
        while wire != sink_wire {
            let pip = self.flat_wires[wire as usize].pip_bwd;
            assert!(pip != PipId::null());
            // do note that the order is inverted from the fwd loop
            wire = *self.wire_to_idx.get(&ctx.pip_dst_wire(pip)).unwrap();

            let node_delay = ctx.pip_delay(pip)
                + ctx.wire_delay(self.flat_wires[wire as usize].wire)
                + ctx.delay_epsilon();
            calculated_delay += node_delay;

            self.bind_pip_internal(arc.net(), wire, pip);
        }
        let nd = &mut self.nets[arc.net().into_inner() as usize];
        nd.done_sinks.insert(arc.sink_wire());

        self.reset_wires();

        calculated_delay
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
