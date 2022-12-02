use std::{
    cmp::Ordering,
    collections::HashMap,
    ops::RangeBounds,
    sync::{atomic::AtomicUsize, Mutex},
};

use colored::Colorize;
use indicatif::{ParallelProgressIterator, ProgressBar, ProgressStyle};
use rayon::prelude::*;

use crate::{npnr, route::Arc};

pub enum Segment {
    Northeast,
    Southeast,
    Southwest,
    Northwest,
}

//        (x < P.x)
//            N
//            ^
//            |
// (y > P.y)  |  (y < P.y)
//     W <----P----> E
//            |
//            |
//            v
//            S
//        (x > P.x)
#[derive(Clone, Copy)]
pub struct Coord {
    x: i32,
    y: i32,
}

impl Coord {
    pub fn new(x: i32, y: i32) -> Self {
        Self { x, y }
    }

    pub fn is_north_of(&self, other: &Self) -> bool {
        self.x < other.x
    }

    pub fn is_east_of(&self, other: &Self) -> bool {
        self.y < other.y
    }

    pub fn is_south_of(&self, other: &Self) -> bool {
        self.x > other.x
    }

    pub fn is_west_of(&self, other: &Self) -> bool {
        self.y > other.y
    }

    pub fn segment_from(&self, other: &Self) -> Segment {
        match (self.is_north_of(other), self.is_east_of(other)) {
            (true, true) => Segment::Northeast,
            (true, false) => Segment::Northwest,
            (false, true) => Segment::Southeast,
            (false, false) => Segment::Southwest,
        }
    }
}

impl From<npnr::Loc> for Coord {
    fn from(other: npnr::Loc) -> Self {
        Self {
            x: other.x,
            y: other.y,
        }
    }
}

pub fn find_partition_point(
    ctx: &npnr::Context,
    arcs: &[Arc],
    pips: &[npnr::PipId],
    x_start: i32,
    x_finish: i32,
    y_start: i32,
    y_finish: i32,
) -> (i32, i32, Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>) {
    let mut x = ((x_finish - x_start) / 2) + x_start;
    let mut y = ((y_finish - y_start) / 2) + y_start;
    let mut x_diff = (x_finish - x_start) / 4;
    let mut y_diff = (y_finish - y_start) / 4;

    let mut ne;
    let mut se;
    let mut sw;
    let mut nw;

    while x_diff != 0 {
        (ne, se, sw, nw) = partition(
            ctx,
            arcs,
            pips,
            x,
            y,
            x_start..=x_finish,
            y_start..=y_finish,
        );
        let north = ne.len() + nw.len();
        let south = se.len() + sw.len();

        let nets = (north + south) as f64;

        let ne_dist = f64::abs(((ne.len() as f64) / nets) - 0.25);
        let se_dist = f64::abs(((se.len() as f64) / nets) - 0.25);
        let sw_dist = f64::abs(((sw.len() as f64) / nets) - 0.25);
        let nw_dist = f64::abs(((nw.len() as f64) / nets) - 0.25);

        let distortion = 100.0 * (ne_dist + se_dist + sw_dist + nw_dist);

        // Stop early if Good Enough.
        if distortion <= 5.0 {
            return (x, y, ne, se, sw, nw);
        }

        x += match north.cmp(&south) {
            std::cmp::Ordering::Less => x_diff,
            std::cmp::Ordering::Equal => 0,
            std::cmp::Ordering::Greater => -x_diff,
        };

        let east = ne.len() + se.len();
        let west = nw.len() + sw.len();
        y += match east.cmp(&west) {
            std::cmp::Ordering::Less => y_diff,
            std::cmp::Ordering::Equal => 0,
            std::cmp::Ordering::Greater => -y_diff,
        };

        x_diff >>= 1;
        y_diff >>= 1;
    }

    (ne, se, sw, nw) = partition(
        ctx,
        arcs,
        pips,
        x,
        y,
        x_start..=x_finish,
        y_start..=y_finish,
    );

    let north = ne.len() + nw.len();
    let south = se.len() + sw.len();
    let nets = (north + south) as f64;

    let ne_dist = f64::abs(((ne.len() as f64) / nets) - 0.25);
    let se_dist = f64::abs(((se.len() as f64) / nets) - 0.25);
    let sw_dist = f64::abs(((sw.len() as f64) / nets) - 0.25);
    let nw_dist = f64::abs(((nw.len() as f64) / nets) - 0.25);

    log_info!(
        "Distortion: {:.02}%\n",
        100.0 * (ne_dist + se_dist + sw_dist + nw_dist)
    );

    (x, y, ne, se, sw, nw)
}

/// finds the y location a line would be split at if you split it at a certain x location
///
/// the function assumes the line goes on forever in both directions, and it truncates the actual coordinate
fn split_line_over_x(line: (npnr::Loc, npnr::Loc), x_location: i32) -> i32 {
    if line.0.x == line.1.x {
        // the line is a straight line in the direction, there is either infinite solutions, or none
        // we simply average the y coordinate to give a "best effort" guess
        return (line.0.y + line.1.y) / 2;
    }

    let x_diff = line.0.x - line.1.x;
    let y_diff = line.0.y - line.1.y;

    // i hope for no overflows, maybe promote to i64 to be sure?
    (y_diff * x_location + line.0.y * x_diff - line.0.x * y_diff) / x_diff
}

/// finds the x location a line would be split at if you split it at a certain y location, assuming the line goes on forever in both directions
fn split_line_over_y(line: (npnr::Loc, npnr::Loc), y_location: i32) -> i32 {
    // laziness supreme!
    split_line_over_x(
        (
            npnr::Loc {
                x: line.0.y,
                y: line.0.x,
                z: 0,
            },
            npnr::Loc {
                x: line.1.y,
                y: line.1.x,
                z: 0,
            },
        ),
        y_location,
    )
}

// A big thank you to @Spacecat-chan for fixing my broken and buggy partition code.

// SpaceCat~Chan:
/// ### Current State of the Partitioner:  
/// after some annoying discoveries, it turns out that partitioning correctly requires ensuring much
/// more that just having each pip not be reused by different nets, it turns out we also need to care
/// about the source and sink wires, so this current partitioner is written only to produce a partition
/// result which is correct, without any care about speed or actual pathing optimality
fn partition<R: RangeBounds<i32>>(
    ctx: &npnr::Context,
    arcs: &[Arc],
    pips: &[npnr::PipId],
    x: i32,
    y: i32,
    x_bounds: R,
    y_bounds: R,
) -> (Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>) {
    let partition_coords = Coord::new(x, y);

    // first direction: where is this pip going
    // second direction: where is this pip located
    let mut pips_n_e = Vec::new();
    let mut pips_e_n = Vec::new();
    let mut pips_s_e = Vec::new();
    let mut pips_w_n = Vec::new();
    let mut pips_n_w = Vec::new();
    let mut pips_e_s = Vec::new();
    let mut pips_s_w = Vec::new();
    let mut pips_w_s = Vec::new();

    let mut ne: Vec<Arc> = Vec::new();
    let mut se: Vec<Arc> = Vec::new();
    let mut sw: Vec<Arc> = Vec::new();
    let mut nw: Vec<Arc> = Vec::new();
    let mut part_horiz = AtomicUsize::new(0);
    let mut part_vert = AtomicUsize::new(0);
    let mut part_diag = AtomicUsize::new(0);

    let x_str = format!("X = {}", x);
    let y_str = format!("Y = {}", y);
    log_info!(
        "Partitioning arcs along {}, {}\n",
        x_str.bold(),
        y_str.bold()
    );

    let mut candidates = 0;
    let mut north = 0;
    let mut east = 0;
    let mut south = 0;
    let mut west = 0;
    for &pip in pips {
        let loc = ctx.pip_location(pip);
        if (loc.x == x || loc.y == y) && x_bounds.contains(&loc.x) && y_bounds.contains(&loc.y) {
            //correctly classifying the pips on the partition point is pretty much impossible
            //just avoid the partition point
            if loc.x == x && loc.y == y {
                continue;
            }

            let is_general_routing = |wire: &str| {
                wire.contains("H01")
                    || wire.contains("V01")
                    || wire.contains("H02")
                    || wire.contains("V02")
                    || wire.contains("H06")
                    || wire.contains("V06")
            };

            let src_wire = ctx.pip_src_wire(pip);
            let dst_wire = ctx.pip_dst_wire(pip);
            let src_name = ctx.name_of_wire(src_wire).to_str().unwrap();
            let dst_name = ctx.name_of_wire(dst_wire).to_str().unwrap();
            if !is_general_routing(src_name) || !is_general_routing(dst_name) {
                // ECP5 hack: whitelist allowed wires.
                continue;
            }

            candidates += 1;

            let pip_arc = std::sync::Arc::new((pip, AtomicUsize::new(0)));
            if loc.y == y {
                // pip is on east-west border

                let (mut src_has_east, mut src_has_west, mut src_has_middle) =
                    (false, false, false);
                let (mut dst_has_east, mut dst_has_west, mut dst_has_middle) =
                    (false, false, false);

                for src_pip in ctx.get_uphill_pips(ctx.pip_src_wire(pip)) {
                    let src_pip_coord: Coord = ctx.pip_location(src_pip).into();
                    if (src_pip_coord.x < x) == (loc.x < x) {
                        src_has_middle |= src_pip_coord.y == loc.y;
                        src_has_east |= src_pip_coord.is_east_of(&partition_coords);
                        src_has_west |= src_pip_coord.is_west_of(&partition_coords);
                    }
                }
                for dst_pip in ctx.get_downhill_pips(ctx.pip_dst_wire(pip)) {
                    let dst_pip_coord: Coord = ctx.pip_location(dst_pip).into();
                    if (dst_pip_coord.x < x) == (loc.x < x) {
                        dst_has_middle |= dst_pip_coord.y == loc.y;
                        dst_has_east |= dst_pip_coord.is_east_of(&partition_coords);
                        dst_has_west |= dst_pip_coord.is_west_of(&partition_coords);
                    }
                }
                if (src_has_east && (dst_has_west || dst_has_middle))
                    || (src_has_middle && dst_has_west)
                {
                    west += 1;
                    if loc.x < x {
                        pips_w_n.push(pip_arc.clone());
                    } else {
                        pips_w_s.push(pip_arc.clone());
                    }
                }
                if (src_has_west && (dst_has_east || dst_has_middle))
                    || (src_has_middle && dst_has_east)
                {
                    east += 1;
                    if loc.x < x {
                        pips_e_n.push(pip_arc.clone());
                    } else {
                        pips_e_s.push(pip_arc.clone());
                    }
                }
            } else {
                // pip is on south-north border

                let (mut src_has_north, mut src_has_south, mut src_has_middle) =
                    (false, false, false);
                let (mut dst_has_north, mut dst_has_south, mut dst_has_middle) =
                    (false, false, false);

                for src_pip in ctx.get_uphill_pips(ctx.pip_src_wire(pip)) {
                    let src_pip_coord: Coord = ctx.pip_location(src_pip).into();
                    if (src_pip_coord.y < y) == (loc.y < y) {
                        src_has_middle |= src_pip_coord.x == loc.x;
                        src_has_north |= src_pip_coord.is_north_of(&partition_coords);
                        src_has_south |= src_pip_coord.is_south_of(&partition_coords);
                    }
                }
                for dst_pip in ctx.get_downhill_pips(ctx.pip_dst_wire(pip)) {
                    let dst_pip_coord: Coord = ctx.pip_location(dst_pip).into();
                    if (dst_pip_coord.y < y) == (loc.y < y) {
                        dst_has_middle |= dst_pip_coord.x == loc.x;
                        dst_has_north |= dst_pip_coord.is_north_of(&partition_coords);
                        dst_has_south |= dst_pip_coord.is_south_of(&partition_coords);
                    }
                }
                if (src_has_north && (dst_has_south || dst_has_middle))
                    || (src_has_middle && dst_has_south)
                {
                    south += 1;
                    if loc.y < y {
                        pips_s_e.push(pip_arc.clone());
                    } else {
                        pips_s_w.push(pip_arc.clone());
                    }
                }
                if (src_has_south && (dst_has_north || dst_has_middle))
                    || (src_has_middle && dst_has_north)
                {
                    north += 1;
                    if loc.y < y {
                        pips_n_e.push(pip_arc.clone());
                    } else {
                        pips_n_w.push(pip_arc.clone());
                    }
                }
            }
        }
    }
    log_info!(
        "  Out of {} candidate pips:\n",
        candidates.to_string().bold()
    );
    log_info!("    {} are north-bound\n", north.to_string().bold());
    log_info!("    {} are east-bound\n", east.to_string().bold());
    log_info!("    {} are south-bound\n", south.to_string().bold());
    log_info!("    {} are west-bound\n", west.to_string().bold());

    //let mut unique_sinks = HashSet::new();
    //let mut unique_sources = HashSet::new();
    //for pip in pips_n.iter().flat_map(|(_, pips)| pips.iter()) {
    //    unique_sources.insert(ctx.pip_src_wire(pip.0));
    //    unique_sinks.insert(ctx.pip_dst_wire(pip.0));
    //}
    //log_info!("among the north-bound pips, there are:\n");
    //log_info!(
    //    "    {} unique sources\n",
    //    unique_sources.len().to_string().bold()
    //);
    //log_info!(
    //    "    {} unique sinks\n",
    //    unique_sinks.len().to_string().bold()
    //);

    let mut used_pips: HashMap<npnr::PipId, Mutex<Option<npnr::NetIndex>>> = HashMap::new();
    let mut used_wires: HashMap<npnr::WireId, Mutex<Option<npnr::NetIndex>>> = HashMap::new();

    used_pips.reserve(pips.len());

    let selected_pips = pips_n_e
        .iter()
        .chain(pips_e_n.iter())
        .chain(pips_e_s.iter())
        .chain(pips_s_e.iter())
        .chain(pips_s_w.iter())
        .chain(pips_w_s.iter())
        .chain(pips_w_n.iter())
        .chain(pips_n_w.iter());

    for pip in selected_pips {
        let (pip, _) = pip.as_ref();
        used_pips.insert(*pip, Mutex::new(None));
        used_wires.insert(ctx.pip_src_wire(*pip), Mutex::new(None));
        used_wires.insert(ctx.pip_dst_wire(*pip), Mutex::new(None));
    }

    let find_best_pip = |pips: &Vec<std::sync::Arc<(npnr::PipId, AtomicUsize)>>, arc: &Arc| {
        let (selected_pip, pip_uses) = pips
            .iter()
            .map(|a| a.as_ref())
            .find(|(pip, _uses)| {
                let source = ctx.pip_src_wire(*pip);
                let sink = ctx.pip_dst_wire(*pip);

                let (mut source, mut sink) = match sink.cmp(&source) {
                    Ordering::Greater => {
                        let source = used_wires.get(&source).unwrap().lock().unwrap();
                        let sink = used_wires.get(&sink).unwrap().lock().unwrap();
                        (source, sink)
                    }
                    Ordering::Equal => return false,
                    Ordering::Less => {
                        let sink = used_wires.get(&sink).unwrap().lock().unwrap();
                        let source = used_wires.get(&source).unwrap().lock().unwrap();
                        (source, sink)
                    }
                };

                let mut candidate = used_pips.get(pip).unwrap().lock().unwrap();
                if candidate.map(|net| net != arc.net()).unwrap_or(false) {
                    return false;
                }
                if source.map(|net| net != arc.net()).unwrap_or(false) {
                    return false;
                }
                if sink.map(|net| net != arc.net()).unwrap_or(false) {
                    return false;
                }

                *candidate = Some(arc.net());
                *source = Some(arc.net());
                *sink = Some(arc.net());

                true
            })
            .expect("unable to find a pip");
        pip_uses.fetch_add(1, std::sync::atomic::Ordering::Release);
        *selected_pip
    };

    let mut explored_pips = AtomicUsize::new(0);

    let mut overused_wires = 1;

    while overused_wires > 0 {
        let progress = ProgressBar::new(arcs.len() as u64);
        progress.set_style(
            ProgressStyle::with_template("[{elapsed}] [{bar:40.cyan/blue}] {msg:30!}")
                .unwrap()
                .progress_chars("━╸ "),
        );

        progress.set_message(format!("overused wires: {}", overused_wires));

        let overuse = Mutex::new(HashMap::new());

        let arcs = arcs
            .into_par_iter()
            .progress_with(progress)
            .flat_map(|arc| {
                let source_loc = arc.get_source_loc();
                let source_coords: Coord = source_loc.into();
                let source_is_north = source_coords.is_north_of(&partition_coords);
                let source_is_east = source_coords.is_east_of(&partition_coords);
                let sink_loc = arc.get_sink_loc();
                let sink_coords: Coord = sink_loc.into();
                let sink_is_north = sink_coords.is_north_of(&partition_coords);
                let sink_is_east = sink_coords.is_east_of(&partition_coords);
                // let name = ctx.name_of(nets.name_from_index(arc.net())).to_str().unwrap().to_string();
                let verbose = false; // name == "IBusCachedPlugin_fetchPc_pc_LUT4_Z_15_B_CCU2C_S0$CCU2_FCI_INT";
                                     //"soc0.processor.with_fpu.fpu_0.fpu_multiply_0.rin_CCU2C_S0_24_B1_LUT4_Z_B_CCU2C_S0_CIN_CCU2C_COUT_S1_LUT4_D_Z_LUT4_Z_D_CCU2C_S0_CIN_CCU2C_COUT_CIN_CCU2C_COUT$CCU2_FCI_INT";
                if source_is_north == sink_is_north && source_is_east == sink_is_east {
                    let seg = source_coords.segment_from(&Coord::new(x, y));
                    vec![(seg, arc.clone())]
                } else if source_is_north != sink_is_north && source_is_east == sink_is_east {
                    let middle = (x, (source_coords.y + sink_coords.y) / 2);
                    let mut middle = (
                        middle.0.clamp(1, ctx.grid_dim_x() - 1),
                        middle.1.clamp(1, ctx.grid_dim_y() - 1),
                    );
                    // need to avoid the partition point
                    if middle.1 == y {
                        middle.1 = y + 1;
                    }
                    let pips = match (source_is_north, source_is_east) {
                        (true, false) => &pips_s_w,
                        (false, false) => &pips_n_w,
                        (true, true) => &pips_s_e,
                        (false, true) => &pips_n_e,
                    };

                    let selected_pip = find_best_pip(pips, arc);
                    explored_pips.fetch_add(pips.len(), std::sync::atomic::Ordering::Relaxed);

                    if verbose {
                        log_info!(
                            "split arc {} to {} vertically across pip {}\n",
                            ctx.name_of_wire(arc.get_source_wire()).to_str().unwrap(),
                            ctx.name_of_wire(arc.get_sink_wire()).to_str().unwrap(),
                            ctx.name_of_pip(selected_pip).to_str().unwrap()
                        );
                    }

                    let mut overuse = overuse.lock().unwrap();
                    if let Some(entry) = overuse.get_mut(&ctx.pip_dst_wire(selected_pip)) {
                        *entry += 1;
                    } else {
                        overuse.insert(ctx.pip_dst_wire(selected_pip), 1);
                    }

                    let (src_to_pip, pip_to_dst) = arc.split(ctx, selected_pip);
                    let (seg1, seg2) = match (source_is_north, source_is_east) {
                        (true, true) => (Segment::Northeast, Segment::Southeast),
                        (true, false) => (Segment::Northwest, Segment::Southwest),
                        (false, true) => (Segment::Southeast, Segment::Northeast),
                        (false, false) => (Segment::Southwest, Segment::Northwest),
                    };
                    part_horiz.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    vec![(seg1, src_to_pip), (seg2, pip_to_dst)]
                } else if source_is_north == sink_is_north && source_is_east != sink_is_east {
                    let middle = ((source_coords.x + sink_coords.x) / 2, y);
                    let mut middle = (
                        middle.0.clamp(1, ctx.grid_dim_x() - 1),
                        middle.1.clamp(1, ctx.grid_dim_y() - 1),
                    );
                    // need to avoid the partition point
                    if middle.0 == x {
                        middle.0 = x + 1;
                    }

                    let pips = match (source_is_east, source_is_north) {
                        (true, false) => &pips_w_s,
                        (false, false) => &pips_e_s,
                        (true, true) => &pips_w_n,
                        (false, true) => &pips_e_n,
                    };

                    let selected_pip = find_best_pip(pips, arc);
                    explored_pips.fetch_add(pips.len(), std::sync::atomic::Ordering::Relaxed);

                    if verbose {
                        log_info!(
                            "split arc {} to {} horizontally across pip {}\n",
                            ctx.name_of_wire(arc.get_source_wire()).to_str().unwrap(),
                            ctx.name_of_wire(arc.get_sink_wire()).to_str().unwrap(),
                            ctx.name_of_pip(selected_pip).to_str().unwrap()
                        );
                    }

                    let mut overuse = overuse.lock().unwrap();
                    if let Some(entry) = overuse.get_mut(&ctx.pip_dst_wire(selected_pip)) {
                        *entry += 1;
                    } else {
                        overuse.insert(ctx.pip_dst_wire(selected_pip), 1);
                    }

                    let (src_to_pip, pip_to_dst) = arc.split(ctx, selected_pip);
                    let (seg1, seg2) = match (source_is_north, source_is_east) {
                        (true, true) => (Segment::Northeast, Segment::Northwest),
                        (true, false) => (Segment::Northwest, Segment::Northeast),
                        (false, true) => (Segment::Southeast, Segment::Southwest),
                        (false, false) => (Segment::Southwest, Segment::Southeast),
                    };
                    part_vert.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    vec![(seg1, src_to_pip), (seg2, pip_to_dst)]
                } else {
                    let middle_horiz = (x, split_line_over_x((source_loc, sink_loc), x));
                    let mut middle_horiz = (
                        middle_horiz.0.clamp(1, ctx.grid_dim_x() - 1),
                        middle_horiz.1.clamp(1, ctx.grid_dim_y() - 1),
                    );
                    let middle_vert = (split_line_over_y((source_loc, sink_loc), y), y);
                    let mut middle_vert = (
                        middle_vert.0.clamp(1, ctx.grid_dim_x() - 1),
                        middle_vert.1.clamp(1, ctx.grid_dim_y() - 1),
                    );

                    let horiz_happens_first = (middle_horiz.1 < y) == source_is_east;

                    // need to avoid the partition point
                    if middle_horiz.1 == y || middle_vert.0 == x {
                        if source_is_east != sink_is_north {
                            middle_horiz.1 = y + 1;
                            middle_vert.0 = x - 1;
                        } else {
                            middle_horiz.1 = y + 1;
                            middle_vert.0 = x + 1;
                        }
                    }

                    let pips = match (source_is_north, source_is_east, horiz_happens_first) {
                        (true, false, true) => &pips_s_w,
                        (false, false, true) => &pips_n_w,
                        (true, true, true) => &pips_s_e,
                        (false, true, true) => &pips_n_e,
                        (true, false, false) => &pips_s_e,
                        (false, false, false) => &pips_n_e,
                        (true, true, false) => &pips_s_w,
                        (false, true, false) => &pips_n_w,
                    };

                    let horiz_pip = find_best_pip(pips, arc);
                    explored_pips.fetch_add(pips.len(), std::sync::atomic::Ordering::Relaxed);

                    let pips = match (source_is_east, source_is_north, horiz_happens_first) {
                        (true, false, true) => &pips_w_n,
                        (false, false, true) => &pips_e_n,
                        (true, true, true) => &pips_w_s,
                        (false, true, true) => &pips_e_s,
                        (true, false, false) => &pips_w_s,
                        (false, false, false) => &pips_e_s,
                        (true, true, false) => &pips_w_n,
                        (false, true, false) => &pips_e_n,
                    };

                    let vert_pip = find_best_pip(pips, arc);
                    explored_pips.fetch_add(pips.len(), std::sync::atomic::Ordering::Relaxed);

                    if verbose {
                        log_info!(
                            "split arc {} to {} across pips {} and {}\n",
                            ctx.name_of_wire(arc.get_source_wire()).to_str().unwrap(),
                            ctx.name_of_wire(arc.get_sink_wire()).to_str().unwrap(),
                            ctx.name_of_pip(horiz_pip).to_str().unwrap(),
                            ctx.name_of_pip(vert_pip).to_str().unwrap()
                        );
                    }

                    let mut overuse = overuse.lock().unwrap();
                    if let Some(entry) = overuse.get_mut(&ctx.pip_dst_wire(horiz_pip)) {
                        *entry += 1;
                    } else {
                        overuse.insert(ctx.pip_dst_wire(horiz_pip), 1);
                    }
                    if let Some(entry) = overuse.get_mut(&ctx.pip_dst_wire(vert_pip)) {
                        *entry += 1;
                    } else {
                        overuse.insert(ctx.pip_dst_wire(vert_pip), 1);
                    }

                    let (src_to_mid1, mid1_to_mid2, mid2_to_dst) = if horiz_happens_first {
                        let (a, b) = arc.split(ctx, horiz_pip);
                        let (b, c) = b.split(ctx, vert_pip);
                        (a, b, c)
                    } else {
                        let (a, b) = arc.split(ctx, vert_pip);
                        let (b, c) = b.split(ctx, horiz_pip);
                        (a, b, c)
                    };

                    let (seg1, seg2, seg3) =
                        match (source_is_north, source_is_east, horiz_happens_first) {
                            (true, true, true) => {
                                (Segment::Northeast, Segment::Southeast, Segment::Southwest)
                            }
                            (true, false, true) => {
                                (Segment::Northwest, Segment::Southwest, Segment::Southeast)
                            }
                            (false, true, true) => {
                                (Segment::Southeast, Segment::Northeast, Segment::Northwest)
                            }
                            (false, false, true) => {
                                (Segment::Southwest, Segment::Northwest, Segment::Northeast)
                            }
                            (true, true, false) => {
                                (Segment::Northeast, Segment::Northwest, Segment::Southwest)
                            }
                            (true, false, false) => {
                                (Segment::Northwest, Segment::Northeast, Segment::Southeast)
                            }
                            (false, true, false) => {
                                (Segment::Southeast, Segment::Southwest, Segment::Northwest)
                            }
                            (false, false, false) => {
                                (Segment::Southwest, Segment::Southeast, Segment::Northeast)
                            }
                        };
                    part_diag.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
                    vec![
                        (seg1, src_to_mid1),
                        (seg2, mid1_to_mid2),
                        (seg3, mid2_to_dst),
                    ]
                }
            })
            .collect::<Vec<_>>();

        let overuse = overuse.into_inner().unwrap();

        for (segment, arc) in arcs {
            match segment {
                Segment::Northeast => ne.push(arc),
                Segment::Southeast => se.push(arc),
                Segment::Southwest => sw.push(arc),
                Segment::Northwest => nw.push(arc),
            }
        }

        overused_wires = 0;
    }

    log_info!(
        "  {} pips explored\n",
        explored_pips.get_mut().to_string().bold()
    );

    let north = ne.len() + nw.len();
    let south = se.len() + sw.len();

    let nets = (north + south) as f64;

    let ne_dist = ((ne.len() as f64) / nets) - 0.25;
    let se_dist = ((se.len() as f64) / nets) - 0.25;
    let sw_dist = ((sw.len() as f64) / nets) - 0.25;
    let nw_dist = ((nw.len() as f64) / nets) - 0.25;

    let ne_str = ne.len().to_string();
    let se_str = se.len().to_string();
    let sw_str = sw.len().to_string();
    let nw_str = nw.len().to_string();

    let dist_str = |dist: f64| {
        if dist > 0.20 {
            "(way too many nets)".red()
        } else if dist > 0.05 {
            "(too many nets)".yellow()
        } else if dist < -0.05 {
            "(too few nets)".yellow()
        } else if dist < -0.20 {
            "(way too few nets)".red()
        } else {
            "(balanced)".green()
        }
    };

    log_info!(
        "  {} arcs partitioned horizontally\n",
        part_horiz.get_mut().to_string().bold()
    );
    log_info!(
        "  {} arcs partitioned vertically\n",
        part_vert.get_mut().to_string().bold()
    );
    log_info!(
        "  {} arcs partitioned both ways\n",
        part_diag.get_mut().to_string().bold()
    );
    log_info!(
        "  {} arcs in the northeast {}\n",
        ne_str.color(if ne_dist.abs() > 0.20 {
            colored::Color::Red
        } else if ne_dist.abs() > 0.05 {
            colored::Color::Yellow
        } else {
            colored::Color::Green
        }),
        dist_str(ne_dist)
    );
    log_info!(
        "  {} arcs in the southeast {}\n",
        se_str.color(if se_dist.abs() > 0.20 {
            colored::Color::Red
        } else if se_dist.abs() > 0.05 {
            colored::Color::Yellow
        } else {
            colored::Color::Green
        }),
        dist_str(se_dist)
    );
    log_info!(
        "  {} arcs in the southwest {}\n",
        sw_str.color(if sw_dist.abs() > 0.20 {
            colored::Color::Red
        } else if sw_dist.abs() > 0.05 {
            colored::Color::Yellow
        } else {
            colored::Color::Green
        }),
        dist_str(sw_dist)
    );
    log_info!(
        "  {} arcs in the northwest {}\n",
        nw_str.color(if nw_dist.abs() > 0.20 {
            colored::Color::Red
        } else if nw_dist.abs() > 0.05 {
            colored::Color::Yellow
        } else {
            colored::Color::Green
        }),
        dist_str(nw_dist)
    );

    (ne, se, sw, nw)
}

pub fn find_partition_point_and_sanity_check(
    ctx: &npnr::Context,
    arcs: &[Arc],
    pips: &[npnr::PipId],
    x_start: i32,
    x_finish: i32,
    y_start: i32,
    y_finish: i32,
) -> (i32, i32, Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>) {
    let (x_part, y_part, ne, se, sw, nw) =
        find_partition_point(ctx, arcs, pips, x_start, x_finish, y_start, y_finish);

    let mut invalid_arcs_in_ne = 0;
    let mut invalid_arcs_in_se = 0;
    let mut invalid_arcs_in_sw = 0;
    let mut invalid_arcs_in_nw = 0;

    for arc in &ne {
        if arc.get_source_loc().x > x_part
            || arc.get_source_loc().y > y_part
            || arc.get_sink_loc().x > x_part
            || arc.get_sink_loc().y > y_part
        {
            invalid_arcs_in_ne += 1;
        }
    }
    for arc in &se {
        if arc.get_source_loc().x < x_part
            || arc.get_source_loc().y > y_part
            || arc.get_sink_loc().x < x_part
            || arc.get_sink_loc().y > y_part
        {
            invalid_arcs_in_se += 1;
        }
    }
    for arc in &sw {
        if arc.get_source_loc().x < x_part
            || arc.get_source_loc().y < y_part
            || arc.get_sink_loc().x < x_part
            || arc.get_sink_loc().y < y_part
        {
            invalid_arcs_in_sw += 1;
        }
    }
    for arc in &nw {
        if arc.get_source_loc().x > x_part
            || arc.get_source_loc().y < y_part
            || arc.get_sink_loc().x > x_part
            || arc.get_sink_loc().y < y_part
        {
            invalid_arcs_in_nw += 1;
        }
    }

    if [
        invalid_arcs_in_ne,
        invalid_arcs_in_se,
        invalid_arcs_in_sw,
        invalid_arcs_in_nw,
    ]
    .into_iter()
    .all(|x| x == 0)
    {
        log_info!(
            "{}\n",
            "Found no arcs crossing partition boundaries.".green()
        );
    } else {
        println!("{}", "found arcs crossing partition boundaries!".yellow());
        println!("count in ne: {}", invalid_arcs_in_ne.to_string().bold());
        println!("count in se: {}", invalid_arcs_in_se.to_string().bold());
        println!("count in sw: {}", invalid_arcs_in_sw.to_string().bold());
        println!("count in nw: {}", invalid_arcs_in_nw.to_string().bold());
    }

    (x_part, y_part, ne, se, sw, nw)
}
