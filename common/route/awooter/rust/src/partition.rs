use std::{
    cmp::Ordering,
    collections::HashMap,
    sync::{atomic::AtomicUsize, Mutex, RwLock},
};

use colored::Colorize;
use indicatif::{ParallelProgressIterator, ProgressBar, ProgressStyle};
use itertools::Itertools;
use rayon::prelude::*;

use crate::{
    npnr::{self, NetIndex},
    route::Arc,
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Quadrant {
    Northeast,
    Southeast,
    Southwest,
    Northwest,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Direction {
    North,
    South,
    East,
    West,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum FullSegment {
    Quadrant(Quadrant),
    Direction(Direction),
    Exact,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Axis {
    NorthSouth,
    EastWest,
}

impl Direction {
    fn between(a: Quadrant, b: Quadrant) -> Option<Direction> {
        match (a, b) {
            (Quadrant::Northeast, Quadrant::Northwest) => Some(Direction::North),
            (Quadrant::Northwest, Quadrant::Northeast) => Some(Direction::North),
            (Quadrant::Northeast, Quadrant::Southeast) => Some(Direction::East),
            (Quadrant::Southeast, Quadrant::Northeast) => Some(Direction::East),
            (Quadrant::Southeast, Quadrant::Southwest) => Some(Direction::South),
            (Quadrant::Southwest, Quadrant::Southeast) => Some(Direction::South),
            (Quadrant::Northwest, Quadrant::Southwest) => Some(Direction::West),
            (Quadrant::Southwest, Quadrant::Northwest) => Some(Direction::West),
            _ => None,
        }
    }

    fn axis(&self) -> Axis {
        match self {
            Direction::North | Direction::South => Axis::NorthSouth,
            Direction::East | Direction::West => Axis::EastWest,
        }
    }
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
#[derive(Clone, Copy, Debug)]
pub struct Coord {
    pub x: i32,
    pub y: i32,
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

    pub fn segment_from(&self, other: &Self) -> Quadrant {
        match (self.is_north_of(other), self.is_east_of(other)) {
            (true, true) => Quadrant::Northeast,
            (true, false) => Quadrant::Northwest,
            (false, true) => Quadrant::Southeast,
            (false, false) => Quadrant::Southwest,
        }
    }

    pub fn full_segment(&self, from: &Self) -> FullSegment {
        match (
            self.is_north_of(from),
            self.is_east_of(from),
            self.is_south_of(from),
            self.is_west_of(from),
        ) {
            (true, true, false, false) => FullSegment::Quadrant(Quadrant::Northeast),
            (true, false, false, true) => FullSegment::Quadrant(Quadrant::Northwest),
            (false, true, true, false) => FullSegment::Quadrant(Quadrant::Southeast),
            (false, false, true, true) => FullSegment::Quadrant(Quadrant::Southwest),
            (true, false, false, false) => FullSegment::Direction(Direction::North),
            (false, true, false, false) => FullSegment::Direction(Direction::East),
            (false, false, true, false) => FullSegment::Direction(Direction::South),
            (false, false, false, true) => FullSegment::Direction(Direction::West),
            (false, false, false, false) => FullSegment::Exact,
            _ => unreachable!(),
        }
    }

    pub fn intersect(&self, other: &Self, axis: Axis, split_point: &Self) -> Self {
        match axis {
            Axis::NorthSouth => Coord {
                x: split_line_over_y(((*self).into(), (*other).into()), split_point.y),
                y: split_point.y,
            },
            Axis::EastWest => Coord {
                x: split_point.x,
                y: split_line_over_x(((*self).into(), (*other).into()), split_point.x),
            },
        }
    }

    pub fn clamp_in_direction(
        &self,
        direction: Direction,
        min_bounds: &Self,
        partition_point: &Self,
        max_bounds: &Self,
    ) -> Self {
        match direction {
            Direction::North => Coord {
                x: self.x.clamp(min_bounds.x, partition_point.x - 1),
                y: self.y,
            },
            Direction::East => Coord {
                x: self.x,
                y: self.y.clamp(min_bounds.y, partition_point.y - 1),
            },
            Direction::South => Coord {
                x: self.x.clamp(partition_point.x + 1, max_bounds.x - 1),
                y: self.y,
            },
            Direction::West => Coord {
                x: self.x,
                y: self.y.clamp(partition_point.y + 1, max_bounds.y - 1),
            },
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
impl Into<npnr::Loc> for Coord {
    fn into(self) -> npnr::Loc {
        npnr::Loc {
            x: self.x,
            y: self.y,
            z: 0,
        }
    }
}

pub fn find_partition_point(
    ctx: &npnr::Context,
    nets: &npnr::Nets,
    arcs: &[Arc],
    pips: &[npnr::PipId],
    x_start: i32,
    x_finish: i32,
    y_start: i32,
    y_finish: i32,
) -> (i32, i32, Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>) {
    let mut x = ((x_finish - x_start) / 2) + x_start;
    let mut y = ((y_finish - y_start) / 2) + y_start;
    let mut x_diff = 0; //(x_finish - x_start) / 4;
    let mut y_diff = 0; //(y_finish - y_start) / 4;

    while x_diff != 0 {
        let (ne, se, sw, nw) = approximate_partition_results(arcs, (x, y));
        let north = ne + nw;
        let south = se + sw;

        let nets = (north + south) as f64;

        let ne_dist = f64::abs(((ne as f64) / nets) - 0.25);
        let se_dist = f64::abs(((se as f64) / nets) - 0.25);
        let sw_dist = f64::abs(((sw as f64) / nets) - 0.25);
        let nw_dist = f64::abs(((nw as f64) / nets) - 0.25);

        let distortion = 100.0 * (ne_dist + se_dist + sw_dist + nw_dist);

        // Stop early if Good Enough.
        if distortion <= 5.0 {
            break;
        }

        x += match north.cmp(&south) {
            std::cmp::Ordering::Less => x_diff,
            std::cmp::Ordering::Equal => 0,
            std::cmp::Ordering::Greater => -x_diff,
        };

        let east = ne + se;
        let west = nw + sw;
        y += match east.cmp(&west) {
            std::cmp::Ordering::Less => y_diff,
            std::cmp::Ordering::Equal => 0,
            std::cmp::Ordering::Greater => -y_diff,
        };

        x_diff >>= 1;
        y_diff >>= 1;
    }

    let (ne, se, sw, nw, special) = partition(
        ctx,
        nets,
        arcs,
        pips,
        x,
        y,
        (x_start, x_finish),
        (y_start, y_finish),
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

    (x, y, ne, se, sw, nw, special)
}

fn approximate_partition_results(
    arcs: &[Arc],
    partition_point: (i32, i32),
) -> (usize, usize, usize, usize) {
    let mut count_ne = 0;
    let mut count_se = 0;
    let mut count_sw = 0;
    let mut count_nw = 0;
    for arc in arcs {
        // TODO(SpaceCat~Chan): stop being lazy and merge Loc and Coord already
        let source_is_north = arc.get_source_loc().x < partition_point.0;
        let source_is_east = arc.get_source_loc().y < partition_point.1;
        let sink_is_north = arc.get_sink_loc().x < partition_point.0;
        let sink_is_east = arc.get_sink_loc().y < partition_point.1;
        if source_is_north == sink_is_north && source_is_east == sink_is_east {
            match (source_is_north, source_is_east) {
                (true, true) => count_ne += 1,
                (false, true) => count_se += 1,
                (false, false) => count_sw += 1,
                (true, false) => count_nw += 1,
            }
        } else if source_is_north != sink_is_north && source_is_east == sink_is_east {
            if source_is_east {
                count_ne += 1;
                count_se += 1;
            } else {
                count_nw += 1;
                count_sw += 1;
            }
        } else if source_is_north == sink_is_north {
            if source_is_north {
                count_ne += 1;
                count_nw += 1;
            } else {
                count_se += 1;
                count_sw += 1;
            }
        } else {
            // all of this calculation is not be needed and an approximation would be good enough
            // but i can't be bothered (yes this is all copy-pasted from the actual partitioner)
            let mut middle_horiz = (
                partition_point.0,
                split_line_over_x(
                    (arc.get_source_loc(), arc.get_sink_loc()),
                    partition_point.0,
                ),
            );

            let mut middle_vert = (
                split_line_over_y(
                    (arc.get_source_loc(), arc.get_sink_loc()),
                    partition_point.1,
                ),
                partition_point.1,
            );

            // need to avoid the partition point
            if middle_horiz.1 == partition_point.1 || middle_vert.0 == partition_point.0 {
                if source_is_east != sink_is_north {
                    middle_horiz.1 = partition_point.1 + 1;
                    middle_vert.0 = partition_point.0 - 1;
                } else {
                    middle_horiz.1 = partition_point.1 + 1;
                    middle_vert.0 = partition_point.0 + 1;
                }
            }
            let horiz_happens_first = (middle_horiz.1 < partition_point.1) == source_is_east;

            // note: if you invert all the bools it adds to the same things, not sure how make less redundant
            match (source_is_north, source_is_east, horiz_happens_first) {
                (true, true, true) => {
                    count_ne += 1;
                    count_se += 1;
                    count_sw += 1;
                }
                (true, false, true) => {
                    count_nw += 1;
                    count_se += 1;
                    count_sw += 1;
                }
                (false, true, true) => {
                    count_ne += 1;
                    count_nw += 1;
                    count_se += 1;
                }
                (false, false, true) => {
                    count_ne += 1;
                    count_nw += 1;
                    count_sw += 1;
                }
                (true, true, false) => {
                    count_ne += 1;
                    count_nw += 1;
                    count_sw += 1;
                }
                (true, false, false) => {
                    count_ne += 1;
                    count_nw += 1;
                    count_se += 1;
                }
                (false, true, false) => {
                    count_nw += 1;
                    count_se += 1;
                    count_sw += 1;
                }
                (false, false, false) => {
                    count_ne += 1;
                    count_se += 1;
                    count_sw += 1;
                }
            }
        }
    }

    (count_ne, count_se, count_sw, count_nw)
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
fn partition(
    ctx: &npnr::Context,
    nets: &npnr::Nets,
    arcs: &[Arc],
    pips: &[npnr::PipId],
    x: i32,
    y: i32,
    x_bounds: (i32, i32),
    y_bounds: (i32, i32),
) -> (Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>) {
    let min_bounds = Coord::new(x_bounds.0, y_bounds.0);
    let max_bounds = Coord::new(x_bounds.1, y_bounds.1);
    let partition_coords = Coord::new(x, y);

    let mut ne: Vec<Arc> = Vec::new();
    let mut se: Vec<Arc> = Vec::new();
    let mut sw: Vec<Arc> = Vec::new();
    let mut nw: Vec<Arc> = Vec::new();
    let special = Mutex::new(Vec::new());
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

    let pip_selector = PipSelector::new(ctx, pips, (x_bounds, y_bounds), (x, y).into(), nets);

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

        let mut bad_nets = std::collections::HashSet::new();

        let _is_general_routing = |wire: &str| {
            wire.contains("H00")
                || wire.contains("V00")
                || wire.contains("H01")
                || wire.contains("V01")
                || wire.contains("H02")
                || wire.contains("V02")
                || wire.contains("H06")
                || wire.contains("V06")
        };

        for arc in arcs {
            if bad_nets.contains(&arc.net()) {
                continue;
            }

            let source_loc = arc.get_source_loc();
            let source_coords: Coord = source_loc.into();
            let sink_loc = arc.get_sink_loc();
            let sink_coords: Coord = sink_loc.into();

            // test for annoying special case
            let mut have_any_in_same_segment = false;
            for pip in ctx.get_downhill_pips(arc.source_wire()) {
                let pip_coord: Coord = ctx.pip_location(pip).into();
                let pip_seg = pip_coord.segment_from(&partition_coords);
                have_any_in_same_segment |= pip_seg == source_coords.segment_from(&partition_coords)
            }
            if !have_any_in_same_segment {
                bad_nets.insert(arc.net());
            }
            let mut have_any_in_same_segment = false;
            for pip in ctx.get_uphill_pips(arc.sink_wire()) {
                let pip_coord: Coord = ctx.pip_location(pip).into();
                let pip_seg = pip_coord.segment_from(&partition_coords);
                have_any_in_same_segment |= pip_seg == sink_coords.segment_from(&partition_coords)
            }
            if !have_any_in_same_segment {
                bad_nets.insert(arc.net());
            }
        }

        let arcs = arcs
            .into_par_iter()
            .progress_with(progress)
            .flat_map(|arc| {
                let raw_net = nets.net_from_index(arc.net());
                let source_loc = arc.get_source_loc();
                let source_coords: Coord = source_loc.into();
                let source_is_north = source_coords.is_north_of(&partition_coords);
                let source_is_east = source_coords.is_east_of(&partition_coords);
                let sink_loc = arc.get_sink_loc();
                let sink_coords: Coord = sink_loc.into();
                let sink_is_north = sink_coords.is_north_of(&partition_coords);
                let sink_is_east = sink_coords.is_east_of(&partition_coords);
                let _name = ctx
                    .name_of(nets.name_from_index(arc.net()))
                    .to_str()
                    .unwrap()
                    .to_string();
                let _verbose = false; //name == "soc0.processor.with_fpu.fpu_0.fpu_multiply_0.rin_CCU2C_S0_4$CCU2_FCI_INT";

                if bad_nets.contains(&arc.net()) {
                    special.lock().unwrap().push(arc.clone());
                    return vec![];
                }

                if source_is_north == sink_is_north && source_is_east == sink_is_east {
                    let seg = source_coords.segment_from(&partition_coords);
                    vec![(seg, arc.clone())]
                } else if source_is_north != sink_is_north && source_is_east == sink_is_east {
                    let (seg1, seg2) = match (source_is_north, source_is_east) {
                        (true, true) => (Quadrant::Northeast, Quadrant::Southeast),
                        (true, false) => (Quadrant::Northwest, Quadrant::Southwest),
                        (false, true) => (Quadrant::Southeast, Quadrant::Northeast),
                        (false, false) => (Quadrant::Southwest, Quadrant::Northwest),
                    };
                    if let Some(partition) = partition_single_arc(
                        ctx,
                        &pip_selector,
                        arc,
                        raw_net,
                        partition_coords,
                        &min_bounds,
                        &max_bounds,
                        &[seg1, seg2],
                    ) {
                        partition
                    } else {
                        let (seg1, seg2, seg3, seg4) = match (source_is_north, source_is_east) {
                            (true, true) => (
                                Quadrant::Northeast,
                                Quadrant::Northwest,
                                Quadrant::Southwest,
                                Quadrant::Southeast,
                            ),
                            (true, false) => (
                                Quadrant::Northwest,
                                Quadrant::Northeast,
                                Quadrant::Southeast,
                                Quadrant::Southwest,
                            ),
                            (false, true) => (
                                Quadrant::Southeast,
                                Quadrant::Southwest,
                                Quadrant::Northwest,
                                Quadrant::Northeast,
                            ),
                            (false, false) => (
                                Quadrant::Southwest,
                                Quadrant::Southeast,
                                Quadrant::Northeast,
                                Quadrant::Northwest,
                            ),
                        };
                        partition_single_arc(
                            ctx,
                            &pip_selector,
                            arc,
                            raw_net,
                            partition_coords,
                            &min_bounds,
                            &max_bounds,
                            &[seg1, seg2, seg3, seg4],
                        )
                        .expect("failed to partition arc on NorthSouth axis")
                    }
                } else if source_is_north == sink_is_north && source_is_east != sink_is_east {
                    let (seg1, seg2) = match (source_is_north, source_is_east) {
                        (true, true) => (Quadrant::Northeast, Quadrant::Northwest),
                        (true, false) => (Quadrant::Northwest, Quadrant::Northeast),
                        (false, true) => (Quadrant::Southeast, Quadrant::Southwest),
                        (false, false) => (Quadrant::Southwest, Quadrant::Southeast),
                    };
                    if let Some(partition) = partition_single_arc(
                        ctx,
                        &pip_selector,
                        arc,
                        raw_net,
                        partition_coords,
                        &min_bounds,
                        &max_bounds,
                        &[seg1, seg2],
                    ) {
                        partition
                    } else {
                        let (seg1, seg2, seg3, seg4) = match (source_is_north, source_is_east) {
                            (true, true) => (
                                Quadrant::Northeast,
                                Quadrant::Southeast,
                                Quadrant::Southwest,
                                Quadrant::Northwest,
                            ),
                            (true, false) => (
                                Quadrant::Northwest,
                                Quadrant::Southwest,
                                Quadrant::Southeast,
                                Quadrant::Northeast,
                            ),
                            (false, true) => (
                                Quadrant::Southeast,
                                Quadrant::Northeast,
                                Quadrant::Northwest,
                                Quadrant::Southwest,
                            ),
                            (false, false) => (
                                Quadrant::Southwest,
                                Quadrant::Northwest,
                                Quadrant::Northeast,
                                Quadrant::Southeast,
                            ),
                        };
                        partition_single_arc(
                            ctx,
                            &pip_selector,
                            arc,
                            raw_net,
                            partition_coords,
                            &min_bounds,
                            &max_bounds,
                            &[seg1, seg2, seg3, seg4],
                        )
                        .expect("failed to partition arc on EastWest axis")
                    }
                } else {
                    let middle_horiz = (x, split_line_over_x((source_loc, sink_loc), x));
                    let horiz_happens_first = (middle_horiz.1 < y) == source_is_east;

                    let grab_segment_order = |horiz_happens_first| match (
                        source_is_north,
                        source_is_east,
                        horiz_happens_first,
                    ) {
                        (true, true, true) => (
                            Quadrant::Northeast,
                            Quadrant::Southeast,
                            Quadrant::Southwest,
                        ),
                        (true, false, true) => (
                            Quadrant::Northwest,
                            Quadrant::Southwest,
                            Quadrant::Southeast,
                        ),
                        (false, true, true) => (
                            Quadrant::Southeast,
                            Quadrant::Northeast,
                            Quadrant::Northwest,
                        ),
                        (false, false, true) => (
                            Quadrant::Southwest,
                            Quadrant::Northwest,
                            Quadrant::Northeast,
                        ),
                        (true, true, false) => (
                            Quadrant::Northeast,
                            Quadrant::Northwest,
                            Quadrant::Southwest,
                        ),
                        (true, false, false) => (
                            Quadrant::Northwest,
                            Quadrant::Northeast,
                            Quadrant::Southeast,
                        ),
                        (false, true, false) => (
                            Quadrant::Southeast,
                            Quadrant::Southwest,
                            Quadrant::Northwest,
                        ),
                        (false, false, false) => (
                            Quadrant::Southwest,
                            Quadrant::Southeast,
                            Quadrant::Northeast,
                        ),
                    };
                    let (seg1, seg2, seg3) = grab_segment_order(horiz_happens_first);
                    if let Some(partition) = partition_single_arc(
                        ctx,
                        &pip_selector,
                        arc,
                        raw_net,
                        partition_coords,
                        &min_bounds,
                        &max_bounds,
                        &[seg1, seg2, seg3],
                    ) {
                        partition
                    } else {
                        // flip `horiz_happens_first` and hope for the best
                        let (seg1, seg2, seg3) = grab_segment_order(!horiz_happens_first);
                        partition_single_arc(
                            ctx,
                            &pip_selector,
                            arc,
                            raw_net,
                            partition_coords,
                            &min_bounds,
                            &max_bounds,
                            &[seg1, seg2, seg3],
                        )
                        .expect("Failed to partition Diagonal arc")
                    }

                    // sorry lofty ^^; you'll have to move this into `partition_single_arc`
                    //if verbose {
                    //    log_info!(
                    //        "split arc {} to {} across pips {} and {}\n",
                    //        ctx.name_of_wire(arc.get_source_wire()).to_str().unwrap(),
                    //        ctx.name_of_wire(arc.get_sink_wire()).to_str().unwrap(),
                    //        ctx.name_of_pip(horiz_pip).to_str().unwrap(),
                    //        ctx.name_of_pip(vert_pip).to_str().unwrap()
                    //    );
                    //}
                }
            })
            .collect::<Vec<_>>();

        for (segment, arc) in arcs {
            match segment {
                Quadrant::Northeast => ne.push(arc),
                Quadrant::Southeast => se.push(arc),
                Quadrant::Southwest => sw.push(arc),
                Quadrant::Northwest => nw.push(arc),
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

    (ne, se, sw, nw, special.into_inner().unwrap())
}

fn partition_single_arc(
    ctx: &npnr::Context,
    pip_selector: &PipSelector,
    arc: &Arc,
    raw_net: *mut npnr::NetInfo,
    partition_point: Coord,
    min_bounds: &Coord,
    max_bounds: &Coord,
    segments: &[Quadrant],
) -> Option<Vec<(Quadrant, Arc)>> {
    let start_coord: Coord = arc.get_source_loc().into();
    let end_coord: Coord = arc.get_sink_loc().into();
    let mut current_arc = arc.clone();
    let mut arcs = vec![];
    for (from_quad, to_quad) in segments.iter().tuple_windows() {
        let direction = Direction::between(*from_quad, *to_quad).unwrap();
        let intersection = start_coord.intersect(&end_coord, direction.axis(), &partition_point);
        let intersection =
            intersection.clamp_in_direction(direction, min_bounds, &partition_point, max_bounds);
        let pip = pip_selector.find_pip(
            ctx,
            intersection.into(),
            current_arc.get_source_loc(),
            current_arc.net(),
            raw_net,
        )?;
        let (before_arc, after_arc) = current_arc.split(ctx, pip);
        arcs.push((*from_quad, before_arc));
        current_arc = after_arc;
    }
    arcs.push((*segments.last().unwrap(), current_arc));
    Some(arcs)
}

pub fn find_partition_point_and_sanity_check(
    ctx: &npnr::Context,
    nets: &npnr::Nets,
    arcs: &[Arc],
    pips: &[npnr::PipId],
    x_start: i32,
    x_finish: i32,
    y_start: i32,
    y_finish: i32,
) -> (i32, i32, Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>, Vec<Arc>) {
    println!(
        "bounds: {:?} -> {:?}",
        (x_start, y_start),
        (x_finish, y_finish)
    );
    let (x_part, y_part, ne, se, sw, nw, special) =
        find_partition_point(ctx, nets, arcs, pips, x_start, x_finish, y_start, y_finish);

    let mut invalid_arcs_in_ne = 0;
    let mut invalid_arcs_in_se = 0;
    let mut invalid_arcs_in_sw = 0;
    let mut invalid_arcs_in_nw = 0;

    let mut out_of_bound_arcs_in_ne = 0;
    let mut out_of_bound_arcs_in_se = 0;
    let mut out_of_bound_arcs_in_sw = 0;
    let mut out_of_bound_arcs_in_nw = 0;

    println!("\nne:");
    for arc in &ne {
        if arc.get_source_loc().x > x_part
            || arc.get_source_loc().y > y_part
            || arc.get_sink_loc().x > x_part
            || arc.get_sink_loc().y > y_part
        {
            invalid_arcs_in_ne += 1;
        }
        if arc.get_source_loc().x <= x_start
            || arc.get_source_loc().y <= y_start
            || arc.get_sink_loc().x <= x_start
            || arc.get_sink_loc().y <= y_start
        {
            println!(
                "oob: {:?} -> {:?}",
                arc.get_source_loc(),
                arc.get_sink_loc()
            );
            out_of_bound_arcs_in_ne += 1;
        }
    }
    println!("\nse:");
    for arc in &se {
        if arc.get_source_loc().x < x_part
            || arc.get_source_loc().y > y_part
            || arc.get_sink_loc().x < x_part
            || arc.get_sink_loc().y > y_part
        {
            invalid_arcs_in_se += 1;
        }
        if arc.get_source_loc().x >= x_finish
            || arc.get_source_loc().y <= y_start
            || arc.get_sink_loc().x >= x_finish
            || arc.get_sink_loc().y <= y_start
        {
            println!(
                "oob: {:?} -> {:?}",
                arc.get_source_loc(),
                arc.get_sink_loc()
            );
            out_of_bound_arcs_in_se += 1;
        }
    }
    println!("\nsw:");
    for arc in &sw {
        if arc.get_source_loc().x < x_part
            || arc.get_source_loc().y < y_part
            || arc.get_sink_loc().x < x_part
            || arc.get_sink_loc().y < y_part
        {
            invalid_arcs_in_sw += 1;
        }
        if arc.get_source_loc().x >= x_finish
            || arc.get_source_loc().y >= y_finish
            || arc.get_sink_loc().x >= x_finish
            || arc.get_sink_loc().y >= y_finish
        {
            println!(
                "oob: {:?} -> {:?}",
                arc.get_source_loc(),
                arc.get_sink_loc()
            );
            out_of_bound_arcs_in_sw += 1;
        }
    }
    println!("\nnw:");
    for arc in &nw {
        if arc.get_source_loc().x > x_part
            || arc.get_source_loc().y < y_part
            || arc.get_sink_loc().x > x_part
            || arc.get_sink_loc().y < y_part
        {
            invalid_arcs_in_nw += 1;
        }
        if arc.get_source_loc().x <= x_start
            || arc.get_source_loc().y >= y_finish
            || arc.get_sink_loc().x <= x_start
            || arc.get_sink_loc().y >= y_finish
        {
            println!(
                "oob: {:?} -> {:?}",
                arc.get_source_loc(),
                arc.get_sink_loc()
            );
            out_of_bound_arcs_in_nw += 1;
        }
    }

    if [
        invalid_arcs_in_ne,
        invalid_arcs_in_se,
        invalid_arcs_in_sw,
        invalid_arcs_in_nw,
        out_of_bound_arcs_in_ne,
        out_of_bound_arcs_in_se,
        out_of_bound_arcs_in_sw,
        out_of_bound_arcs_in_nw,
    ]
    .into_iter()
    .all(|x| x == 0)
    {
        log_info!("{}\n", "Found no invalid arcs.".green());
    } else {
        println!("{}", "found invalid arcs!".yellow());
        println!(
            "count crossing borders in ne: {}",
            invalid_arcs_in_ne.to_string().bold()
        );
        println!(
            "count crossing borders in se: {}",
            invalid_arcs_in_se.to_string().bold()
        );
        println!(
            "count crossing borders in sw: {}",
            invalid_arcs_in_sw.to_string().bold()
        );
        println!(
            "count crossing borders in nw: {}",
            invalid_arcs_in_nw.to_string().bold()
        );
        println!(
            "count going out of bounds in ne: {}",
            out_of_bound_arcs_in_ne.to_string().bold()
        );
        println!(
            "count going out of bounds in se: {}",
            out_of_bound_arcs_in_se.to_string().bold()
        );
        println!(
            "count going out of bounds in sw: {}",
            out_of_bound_arcs_in_sw.to_string().bold()
        );
        println!(
            "count going out of bounds in nw: {}",
            out_of_bound_arcs_in_nw.to_string().bold()
        );
    }

    (x_part, y_part, ne, se, sw, nw, special)
}

struct PipSelector {
    used_pips: HashMap<npnr::PipId, Mutex<Option<npnr::NetIndex>>>,
    used_wires: HashMap<npnr::WireId, Mutex<Option<npnr::NetIndex>>>,

    // how to derive index described in `find_pip_index`
    pips: [HashMap<(i32, i32), Vec<npnr::PipId>>; 8],
    pip_selection_cache: [HashMap<NetIndex, RwLock<Option<npnr::PipId>>>; 8],

    partition_loc: npnr::Loc,
    boundaries: ((i32, i32), (i32, i32)),
}

impl PipSelector {
    /// explores the pips and creates a pip selector from the results
    fn new(
        ctx: &npnr::Context,
        pips: &[npnr::PipId],
        bounds: ((i32, i32), (i32, i32)),
        partition_point: npnr::Loc,
        nets: &npnr::Nets,
    ) -> Self {
        let mut pips_n_e = HashMap::new();
        let mut pips_e_n = HashMap::new();
        let mut pips_s_e = HashMap::new();
        let mut pips_w_n = HashMap::new();
        let mut pips_n_w = HashMap::new();
        let mut pips_e_s = HashMap::new();
        let mut pips_s_w = HashMap::new();
        let mut pips_w_s = HashMap::new();

        let mut candidates = 0;
        let mut north = 0;
        let mut east = 0;
        let mut south = 0;
        let mut west = 0;
        for &pip in pips {
            let loc = ctx.pip_location(pip);
            if (loc.x == partition_point.x || loc.y == partition_point.y)
                && loc.x >= bounds.0 .0
                && loc.x <= bounds.0 .1
                && loc.y > bounds.1 .0
                && loc.y < bounds.1 .1
            {
                //correctly classifying the pips on the partition point is pretty much impossible
                //just avoid the partition point
                if loc.x == partition_point.x && loc.y == partition_point.y {
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

                // a stack, to do recursion, because we need that i guess
                let mut pips = vec![];
                const MAX_PIP_SEARCH_DEPTH: usize = 3;

                if loc.y == partition_point.y {
                    // pip is on east-west border

                    let (mut src_has_east, mut src_has_west, src_has_middle) =
                        (false, false, false);
                    let (mut dst_has_east, mut dst_has_west, dst_has_middle) =
                        (false, false, false);

                    for src_pip in ctx.get_uphill_pips(ctx.pip_src_wire(pip)) {
                        pips.push((src_pip, 0));
                    }
                    while let Some((src_pip, depth)) = pips.pop() {
                        let src_pip_coord: Coord = ctx.pip_location(src_pip).into();
                        if (src_pip_coord.x < partition_point.x) && (loc.x < partition_point.x)
                            || (src_pip_coord.x > partition_point.x) && (loc.x > partition_point.x)
                        {
                            src_has_east |= src_pip_coord.is_east_of(&partition_point.into());
                            src_has_west |= src_pip_coord.is_west_of(&partition_point.into());
                            if src_pip_coord.y == loc.y && depth < MAX_PIP_SEARCH_DEPTH {
                                for _src_pip in ctx.get_uphill_pips(ctx.pip_src_wire(src_pip)) {
                                    //pips.push((src_pip, depth + 1));
                                }
                            }
                        }
                    }

                    for dst_pip in ctx.get_downhill_pips(ctx.pip_dst_wire(pip)) {
                        pips.push((dst_pip, 0));
                    }
                    while let Some((dst_pip, depth)) = pips.pop() {
                        let dst_pip_coord: Coord = ctx.pip_location(dst_pip).into();

                        if (dst_pip_coord.x < partition_point.x) && (loc.x < partition_point.x)
                            || (dst_pip_coord.x > partition_point.x) && (loc.x > partition_point.x)
                        {
                            dst_has_east |= dst_pip_coord.is_east_of(&partition_point.into());
                            dst_has_west |= dst_pip_coord.is_west_of(&partition_point.into());
                            if dst_pip_coord.y == loc.y && depth < MAX_PIP_SEARCH_DEPTH {
                                for _dst_pip in ctx.get_downhill_pips(ctx.pip_dst_wire(dst_pip)) {
                                    //pips.push((dst_pip, depth + 1));
                                }
                            }
                        }
                    }

                    if (src_has_east && (dst_has_west || dst_has_middle))
                        || (src_has_middle && dst_has_west)
                    {
                        west += 1;
                        if loc.x < partition_point.x {
                            pips_w_n.entry((loc.x, loc.y)).or_insert(vec![]).push(pip);
                        } else {
                            pips_w_s.entry((loc.x, loc.y)).or_insert(vec![]).push(pip);
                        }
                    }
                    if (src_has_west && (dst_has_east || dst_has_middle))
                        || (src_has_middle && dst_has_east)
                    {
                        east += 1;
                        if loc.x < partition_point.x {
                            pips_e_n.entry((loc.x, loc.y)).or_insert(vec![]).push(pip);
                        } else {
                            pips_e_s.entry((loc.x, loc.y)).or_insert(vec![]).push(pip);
                        }
                    }
                } else {
                    // pip is on south-north border

                    let (mut src_has_north, mut src_has_south, src_has_middle) =
                        (false, false, false);
                    let (mut dst_has_north, mut dst_has_south, dst_has_middle) =
                        (false, false, false);

                    for src_pip in ctx.get_uphill_pips(ctx.pip_src_wire(pip)) {
                        pips.push((src_pip, 0));
                    }

                    while let Some((src_pip, depth)) = pips.pop() {
                        let src_pip_coord: Coord = ctx.pip_location(src_pip).into();
                        if (src_pip_coord.y < partition_point.y) && (loc.y < partition_point.y)
                            || (src_pip_coord.y > partition_point.y) && (loc.y > partition_point.y)
                        {
                            src_has_north |= src_pip_coord.is_north_of(&partition_point.into());
                            src_has_south |= src_pip_coord.is_south_of(&partition_point.into());
                            if src_pip_coord.x == loc.x && depth < MAX_PIP_SEARCH_DEPTH {
                                // yaaaaaaay, we need to everything again for this pip :)
                                for _src_pip in ctx.get_uphill_pips(ctx.pip_src_wire(src_pip)) {
                                    //pips.push((src_pip, depth + 1));
                                }
                            }
                        }
                    }

                    for dst_pip in ctx.get_downhill_pips(ctx.pip_dst_wire(pip)) {
                        pips.push((dst_pip, 0));
                    }

                    while let Some((dst_pip, depth)) = pips.pop() {
                        let dst_pip_coord: Coord = ctx.pip_location(dst_pip).into();
                        if (dst_pip_coord.y < partition_point.y) && (loc.y < partition_point.y)
                            || (dst_pip_coord.y > partition_point.y) && (loc.y > partition_point.y)
                        {
                            dst_has_north |= dst_pip_coord.is_north_of(&partition_point.into());
                            dst_has_south |= dst_pip_coord.is_south_of(&partition_point.into());
                            if dst_pip_coord.x == loc.x && depth < MAX_PIP_SEARCH_DEPTH {
                                for _dst_pip in ctx.get_downhill_pips(ctx.pip_dst_wire(dst_pip)) {
                                    //pips.push((dst_pip, depth + 1));
                                }
                            }
                        }
                    }

                    if (src_has_north && (dst_has_south || dst_has_middle))
                        || (src_has_middle && dst_has_south)
                    {
                        south += 1;
                        if loc.y < partition_point.y {
                            pips_s_e.entry((loc.x, loc.y)).or_insert(vec![]).push(pip);
                        } else {
                            pips_s_w.entry((loc.x, loc.y)).or_insert(vec![]).push(pip);
                        }
                    }
                    if (src_has_south && (dst_has_north || dst_has_middle))
                        || (src_has_middle && dst_has_north)
                    {
                        north += 1;
                        if loc.y < partition_point.y {
                            pips_n_e.entry((loc.x, loc.y)).or_insert(vec![]).push(pip);
                        } else {
                            pips_n_w.entry((loc.x, loc.y)).or_insert(vec![]).push(pip);
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

        let mut used_pips = HashMap::with_capacity(pips.len());
        let mut used_wires = HashMap::new();

        let selected_pips = pips_n_e
            .iter()
            .chain(pips_e_n.iter())
            .chain(pips_e_s.iter())
            .chain(pips_s_e.iter())
            .chain(pips_s_w.iter())
            .chain(pips_w_s.iter())
            .chain(pips_w_n.iter())
            .chain(pips_n_w.iter());

        for pip in selected_pips.flat_map(|(_, pips)| pips.iter()) {
            used_pips.insert(*pip, Mutex::new(None));
            used_wires.insert(ctx.pip_src_wire(*pip), Mutex::new(None));
            used_wires.insert(ctx.pip_dst_wire(*pip), Mutex::new(None));
        }

        let mut caches = [
            HashMap::new(),
            HashMap::new(),
            HashMap::new(),
            HashMap::new(),
            HashMap::new(),
            HashMap::new(),
            HashMap::new(),
            HashMap::new(),
        ];

        let nets = nets.to_vec();
        for cache in &mut caches {
            for (_, net) in nets.iter() {
                let net = unsafe { net.as_ref().unwrap() };
                cache.insert(net.index(), RwLock::new(None));
            }
        }

        PipSelector {
            used_pips,
            used_wires,
            pips: [
                pips_w_n, pips_w_s, pips_e_n, pips_e_s, pips_s_e, pips_s_w, pips_n_e, pips_n_w,
            ],
            pip_selection_cache: caches,

            partition_loc: partition_point,
            boundaries: bounds,
        }
    }

    /// finds a pip hopefully close to `desired_pip_location` which has a source accessable from `coming_from`
    /// returns `None` if no pip can be found
    fn find_pip(
        &self,
        ctx: &npnr::Context,
        desired_pip_location: npnr::Loc,
        coming_from: npnr::Loc,
        net: npnr::NetIndex,
        raw_net: *mut npnr::NetInfo,
    ) -> Option<npnr::PipId> {
        let pip_index = self.find_pip_index(desired_pip_location, coming_from);
        self.raw_find_pip(ctx, pip_index, desired_pip_location, net, raw_net)
    }

    /// an emergency find pip function for when the exact pip location doesn't matter, only where comes from and goes to
    fn segment_based_find_pip(
        &self,
        ctx: &npnr::Context,
        from_segment: Quadrant,
        to_segment: Quadrant,
        net: npnr::NetIndex,
        raw_net: *mut npnr::NetInfo,
    ) -> Option<npnr::PipId> {
        let (pip_index, offset) = match (from_segment, to_segment) {
            (Quadrant::Northeast, Quadrant::Northwest) => (0, (-1, 0)),
            (Quadrant::Southeast, Quadrant::Southwest) => (1, (1, 0)),
            (Quadrant::Northwest, Quadrant::Northeast) => (2, (-1, 0)),
            (Quadrant::Southwest, Quadrant::Southeast) => (3, (1, 0)),
            (Quadrant::Northeast, Quadrant::Southeast) => (4, (0, -1)),
            (Quadrant::Northwest, Quadrant::Southwest) => (5, (0, 1)),
            (Quadrant::Southeast, Quadrant::Northeast) => (6, (0, -1)),
            (Quadrant::Southwest, Quadrant::Northwest) => (7, (0, 1)),
            _ => panic!("tried to find pip between two diagonal or identical segments"),
        };
        let desired_location = (
            self.partition_loc.x + offset.0,
            self.partition_loc.y + offset.1,
        );
        self.raw_find_pip(ctx, pip_index, desired_location.into(), net, raw_net)
    }

    fn raw_find_pip(
        &self,
        ctx: &npnr::Context,
        pip_index: usize,
        desired_pip_location: npnr::Loc,
        net: npnr::NetIndex,
        raw_net: *mut npnr::NetInfo,
    ) -> Option<npnr::PipId> {
        // adding a scope to avoid holding the lock for too long
        {
            let cache = self.pip_selection_cache[pip_index]
                .get(&net)
                .unwrap()
                .read()
                .unwrap();
            if let Some(pip) = *cache {
                return Some(pip);
            }
        }

        let pips = &self.pips[pip_index];

        let (selected_pip, mut candidate, mut source, mut sink) = self
            .pip_index_to_position_iter(pip_index, (desired_pip_location.x, desired_pip_location.y))
            .flat_map(|pos| pips.get(&pos))
            .flat_map(|vec| vec.iter())
            .filter_map(|pip| {
                if !ctx.pip_avail_for_net(*pip, raw_net) {
                    return None;
                }

                let source = ctx.pip_src_wire(*pip);
                let sink = ctx.pip_dst_wire(*pip);

                let (source, sink) = match sink.cmp(&source) {
                    Ordering::Greater => {
                        let source = self.used_wires.get(&source).unwrap().lock().unwrap();
                        let sink = self.used_wires.get(&sink).unwrap().lock().unwrap();
                        (source, sink)
                    }
                    Ordering::Equal => return None,
                    Ordering::Less => {
                        let sink = self.used_wires.get(&sink).unwrap().lock().unwrap();
                        let source = self.used_wires.get(&source).unwrap().lock().unwrap();
                        (source, sink)
                    }
                };

                let candidate = self.used_pips.get(pip).unwrap().lock().unwrap();
                if candidate.map(|other| other != net).unwrap_or(false) {
                    return None;
                }
                if source.map(|other| other != net).unwrap_or(false) {
                    return None;
                }
                if sink.map(|other| other != net).unwrap_or(false) {
                    return None;
                }

                Some((pip, candidate, source, sink))
            })
            .next()?;

        {
            let mut cache = self.pip_selection_cache[pip_index]
                .get(&net)
                .unwrap()
                .write()
                .unwrap();

            // while we were looking, someone else might have found a pip
            if let Some(other_pip) = *cache {
                return Some(other_pip);
            }

            *cache = Some(*selected_pip);
        }

        *candidate = Some(net);
        *source = Some(net);
        *sink = Some(net);

        Some(*selected_pip)
    }

    /// takes in a desired pip location and where the arc is coming from and figures out which index to use in the `self.pips` array
    fn find_pip_index(&self, desired_pip_location: npnr::Loc, coming_from: npnr::Loc) -> usize {
        let desired_coord: Coord = desired_pip_location.into();
        let from_coord: Coord = coming_from.into();
        match (
            desired_coord.full_segment(&self.partition_loc.into()),
            from_coord.is_north_of(&self.partition_loc.into()),
            from_coord.is_east_of(&self.partition_loc.into()),
        ) {
            (FullSegment::Direction(Direction::North), _, true) => 0,
            (FullSegment::Direction(Direction::South), _, true) => 1,
            (FullSegment::Direction(Direction::North), _, false) => 2,
            (FullSegment::Direction(Direction::South), _, false) => 3,
            (FullSegment::Direction(Direction::East), true, _) => 4,
            (FullSegment::Direction(Direction::West), true, _) => 5,
            (FullSegment::Direction(Direction::East), false, _) => 6,
            (FullSegment::Direction(Direction::West), false, _) => 7,
            (FullSegment::Exact, _, _) => panic!("can't find pips on the partition point"),
            _ => panic!("pip must be on partition boundaries somewhere"),
        }
    }

    fn pip_index_to_position_iter(
        &self,
        pip_index: usize,
        start_position: (i32, i32),
    ) -> impl Iterator<Item = (i32, i32)> {
        let (offset, times_up, times_down) = match pip_index {
            0 | 2 => (
                (1, 0),
                self.partition_loc.x - start_position.0 - 1,
                start_position.0 - self.boundaries.0 .0,
            ),
            1 | 3 => (
                (1, 0),
                self.boundaries.0 .1 - start_position.0 - 1,
                start_position.0 - self.partition_loc.x - 1,
            ),
            4 | 6 => (
                (0, 1),
                self.partition_loc.y - start_position.1 - 1,
                start_position.1 - self.boundaries.1 .0,
            ),
            5 | 7 => (
                (0, 1),
                self.boundaries.1 .1 - start_position.1 - 1,
                start_position.1 - self.partition_loc.y - 1,
            ),
            _ => unreachable!(),
        };

        // cursed iterator magic :3
        // (rust, can we please have generators yet?)
        (0..=times_up)
            .map(move |ind| {
                (
                    offset.0 * ind + start_position.0,
                    offset.1 * ind + start_position.1,
                )
            })
            .zip_longest((1..=times_down).map(move |ind| {
                (
                    offset.0 * -ind + start_position.0,
                    offset.1 * -ind + start_position.1,
                )
            }))
            .flat_map(|zip| match zip {
                itertools::EitherOrBoth::Both(a, b) => [Some(a), Some(b)].into_iter(),
                itertools::EitherOrBoth::Left(a) => [Some(a), None].into_iter(),
                itertools::EitherOrBoth::Right(a) => [Some(a), None].into_iter(),
            })
            .flatten()
    }
}
