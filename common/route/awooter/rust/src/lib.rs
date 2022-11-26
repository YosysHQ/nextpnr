use std::{
    collections::HashMap,
    ptr::NonNull,
    sync::{atomic::AtomicUsize, Mutex, RwLock},
};

use colored::Colorize;
use indicatif::{ProgressBar, ProgressStyle};
use rayon::prelude::*;

use crate::npnr::PortRef;

#[macro_use]
mod npnr;

enum Subpartition {
    Part(Box<Partition>),
    Nets(Vec<Net>),
}

struct Partition {
    parts: [Option<Subpartition>; 4],
    borders: [[Vec<npnr::WireId>; 4]; 4],
}

struct Net {
    source: npnr::WireId,
    sinks: Vec<npnr::WireId>,
}

#[no_mangle]
pub extern "C" fn npnr_router_awooter(ctx: Option<NonNull<npnr::Context>>) -> bool {
    std::panic::catch_unwind(move || {
        let ctx: &mut npnr::Context = unsafe { ctx.expect("non-null context").as_mut() };
        route(ctx)
    })
    .unwrap_or_else(|x| {
        if let Ok(x) = x.downcast::<String>() {
            log_error!("caught panic: {}", x);
        }
        false
    })
}

type ArcVec = Vec<((i32, i32), (i32, i32))>;

fn find_partition_point(
    ctx: &npnr::Context,
    nets: &npnr::Nets,
    pips: &[npnr::PipId],
    x_start: i32,
    x_finish: i32,
    y_start: i32,
    y_finish: i32,
) -> (i32, i32, ArcVec, ArcVec, ArcVec, ArcVec) {
    let mut x = ((x_finish - x_start) / 2) + x_start;
    let mut y = ((y_finish - y_start) / 2) + y_start;
    let mut x_diff = (x_finish - x_start) / 4;
    let mut y_diff = (y_finish - y_start) / 4;

    let mut ne;
    let mut se;
    let mut sw;
    let mut nw;

    while x_diff != 0 {
        (ne, se, sw, nw) = partition_nets(ctx, nets, pips, x, y);
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

    (ne, se, sw, nw) = partition_nets(ctx, nets, pips, x, y);

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
    if line.0.x == line.0.y {
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

enum Segment {
    Northeast,
    Southeast,
    Southwest,
    Northwest,
}

// A big thank you to @Spacecat-chan for fixing my broken and buggy partition code.
fn partition_nets(
    ctx: &npnr::Context,
    nets: &npnr::Nets,
    pips: &[npnr::PipId],
    x: i32,
    y: i32,
) -> (ArcVec, ArcVec, ArcVec, ArcVec) {
    let mut pips_n = HashMap::new();
    let mut pips_e = HashMap::new();
    let mut pips_s = HashMap::new();
    let mut pips_w = HashMap::new();

    let mut ne = Vec::new();
    let mut se = Vec::new();
    let mut sw = Vec::new();
    let mut nw = Vec::new();
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

    // BUG: because pips don't specify direction, this puts pips of opposite directions
    // in the same entry. This is bad, since it could lead to selecting a pip of the
    // wrong direction.
    //
    // Possibly fixed? I need to double-check.

    let mut candidates = 0;
    let mut north = 0;
    let mut east = 0;
    let mut south = 0;
    let mut west = 0;
    for &pip in pips {
        let loc = ctx.pip_location(pip);
        if loc.x == x || loc.y == y {
            let dir = ctx.pip_direction(pip);

            // This pip seems internal; skip it.
            if dir.x == 0 && dir.y == 0 {
                continue;
            }

            candidates += 1;

            if dir.x < 0 {
                north += 1;
                pips_n
                    .entry((loc.x, loc.y))
                    .and_modify(|pip_list: &mut Vec<(npnr::PipId, RwLock<Vec<npnr::IdString>>)>| {
                        pip_list.push((pip, RwLock::new(Vec::new())))
                    })
                    .or_insert_with(|| vec![(pip, RwLock::new(Vec::new()))]);
            }

            if dir.x > 0 {
                south += 1;
                pips_s
                    .entry((loc.x, loc.y))
                    .and_modify(|pip_list: &mut Vec<(npnr::PipId, RwLock<Vec<npnr::IdString>>)>| {
                        pip_list.push((pip, RwLock::new(Vec::new())))
                    })
                    .or_insert_with(|| vec![(pip, RwLock::new(Vec::new()))]);
            }

            if dir.y < 0 {
                east += 1;
                pips_e
                    .entry((loc.x, loc.y))
                    .and_modify(|pip_list: &mut Vec<(npnr::PipId, RwLock<Vec<npnr::IdString>>)>| {
                        pip_list.push((pip, RwLock::new(Vec::new())))
                    })
                    .or_insert_with(|| vec![(pip, RwLock::new(Vec::new()))]);
            }

            if dir.y > 0 {
                west += 1;
                pips_w
                    .entry((loc.x, loc.y))
                    .and_modify(|pip_list: &mut Vec<(npnr::PipId, RwLock<Vec<npnr::IdString>>)>| {
                        pip_list.push((pip, RwLock::new(Vec::new())))
                    })
                    .or_insert_with(|| vec![(pip, RwLock::new(Vec::new()))]);
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

    let progress = ProgressBar::new(nets.len() as u64);
    progress.set_style(
        ProgressStyle::with_template("[{elapsed}] [{bar:40.cyan/blue}] {msg}")
            .unwrap()
            .progress_chars("━╸ "),
    );

    let mut explored_pips = AtomicUsize::new(0);

    for (name, net) in nets.iter() {
        let mut message = ctx.name_of(*name).to_str().unwrap().to_string();
        let message = if message.len() > 31 {
            message.truncate(28);
            format!("{}...", message)
        } else {
            message
        };
        progress.set_message(message);
        progress.inc(1);
        let net = unsafe { net.as_mut().unwrap() };

        if net.is_global() {
            continue;
        }

        let source = unsafe { net.driver().as_ref().unwrap() };

        let source = source.cell();
        if source.is_none() {
            continue;
        }
        let source = source.unwrap().location();
        let source_is_north = source.x < x;
        let source_is_east = source.y < y;
        let source_wire = ctx.source_wire(net);

        // I want to merge the "find best pip" code into a closure
        // but doing so gives lifetime errors, and you can't describe
        // lifetimes in a closure, as far as I can tell.

        let arcs = nets
            .users_by_name(*name)
            .unwrap()
            .par_iter()
            .flat_map(|sink| {
                ctx.sink_wires(net, (*sink) as *const PortRef)
                    .into_par_iter()
                    .map(move |sink_wire| (sink, sink_wire))
            })
            .flat_map(|(sink, sink_wire)| {
                let sink_loc = sink.cell().unwrap().location();
                let sink_is_north = sink_loc.x < x;
                let sink_is_east = sink_loc.y < y;
                if source_is_north == sink_is_north && source_is_east == sink_is_east {
                    let arc = ((source.x, source.y), (sink_loc.x, sink_loc.y));
                    let seg = match (source_is_north, source_is_east) {
                        (true, true) => Segment::Northeast,
                        (true, false) => Segment::Northwest,
                        (false, true) => Segment::Southeast,
                        (false, false) => Segment::Southwest,
                    };
                    vec![(seg, arc)]
                } else if source_is_north != sink_is_north && source_is_east == sink_is_east {
                    let middle = (x, (source.y + sink_loc.y) / 2);
                    let middle = (middle.0.clamp(1, ctx.grid_dim_x()-1), middle.1.clamp(1, ctx.grid_dim_y()-1));
                    let pips = match source_is_north {
                        true => pips_s.get(&middle).unwrap(),
                        false => pips_n.get(&middle).unwrap(),
                    };

                    let (selected_pip, pip_uses) = pips
                        .iter()
                        .min_by_key(|(pip, uses)| {
                            let src_to_pip =
                                ctx.estimate_delay(source_wire, ctx.pip_src_wire(*pip));
                            let pip_to_snk = ctx.estimate_delay(ctx.pip_dst_wire(*pip), sink_wire);
                            let uses = uses.read().unwrap();
                            let uses = uses.len() - (uses.contains(name) as usize);
                            (1000.0 * (src_to_pip + ((uses + 1) as f32) * pip_to_snk)) as u64
                        })
                        .unwrap();
                    pip_uses.write().unwrap().push(*name);
                    let selected_pip = *selected_pip;
                    explored_pips.fetch_add(pips.len(), std::sync::atomic::Ordering::SeqCst);

                    let pip_loc = ctx.pip_location(selected_pip);
                    let src_to_pip = ((source.x, source.y), (pip_loc.x, pip_loc.y));
                    let pip_to_dst = ((pip_loc.x, pip_loc.y), (sink_loc.x, sink_loc.y));
                    let (seg1, seg2) = match (source_is_north, source_is_east) {
                        (true, true) => (Segment::Northeast, Segment::Southeast),
                        (true, false) => (Segment::Northwest, Segment::Southwest),
                        (false, true) => (Segment::Southeast, Segment::Northeast),
                        (false, false) => (Segment::Southwest, Segment::Northwest),
                    };
                    part_horiz.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                    vec![(seg1, src_to_pip), (seg2, pip_to_dst)]
                } else if source_is_north == sink_is_north && source_is_east != sink_is_east {
                    let middle = ((source.x + sink_loc.x) / 2, y);
                    let middle = (middle.0.clamp(1, ctx.grid_dim_x()-1), middle.1.clamp(1, ctx.grid_dim_y()-1));
                    let pips = match source_is_east {
                        true => pips_w.get(&middle).unwrap(),
                        false => pips_e.get(&middle).unwrap_or_else(|| panic!("\nwhile partitioning an arc between ({}, {}) and ({}, {})\n({}, {}) does not exist in the pip library\n", source.x, source.y, sink_loc.x, sink_loc.y, middle.0, middle.1)),
                    };

                    let (selected_pip, pip_uses) = pips
                        .iter()
                        .min_by_key(|(pip, uses)| {
                            let src_to_pip =
                                ctx.estimate_delay(source_wire, ctx.pip_src_wire(*pip));
                            let pip_to_snk = ctx.estimate_delay(ctx.pip_dst_wire(*pip), sink_wire);
                            let uses = uses.read().unwrap();
                            let uses = uses.len() - (uses.contains(name) as usize);
                            (1000.0 * (src_to_pip + ((uses + 1) as f32) * pip_to_snk)) as u64
                        })
                        .unwrap();
                    pip_uses.write().unwrap().push(*name);
                    let selected_pip = *selected_pip;
                    explored_pips.fetch_add(pips.len(), std::sync::atomic::Ordering::SeqCst);

                    let pip_loc = ctx.pip_location(selected_pip);
                    let src_to_pip = ((source.x, source.y), (pip_loc.x, pip_loc.y));
                    let pip_to_dst = ((pip_loc.x, pip_loc.y), (sink_loc.x, sink_loc.y));
                    let (seg1, seg2) = match (source_is_north, source_is_east) {
                        (true, true) => (Segment::Northeast, Segment::Northwest),
                        (true, false) => (Segment::Northwest, Segment::Northeast),
                        (false, true) => (Segment::Southeast, Segment::Southwest),
                        (false, false) => (Segment::Southwest, Segment::Southeast),
                    };
                    part_vert.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                    vec![(seg1, src_to_pip), (seg2, pip_to_dst)]
                } else {
                    let middle = (x, split_line_over_x((source, sink_loc), x));
                    let middle = (middle.0.clamp(1, ctx.grid_dim_x()-1), middle.1.clamp(1, ctx.grid_dim_y()-1));
                    let pips = match source_is_east {
                        true => pips_w.get(&middle).unwrap(),
                        false => pips_e.get(&middle).unwrap(),
                    };

                    let (horiz_pip, pip_uses) = pips
                        .iter()
                        .min_by_key(|(pip, uses)| {
                            let src_to_pip =
                                ctx.estimate_delay(source_wire, ctx.pip_src_wire(*pip));
                            let pip_to_snk = ctx.estimate_delay(ctx.pip_dst_wire(*pip), sink_wire);
                            let uses = uses.read().unwrap();
                            let uses = uses.len() - (uses.contains(name) as usize);
                            (1000.0 * (src_to_pip + ((uses + 1) as f32) * pip_to_snk)) as u64
                        })
                        .unwrap();
                    pip_uses.write().unwrap().push(*name);
                    let horiz_pip = *horiz_pip;
                    explored_pips.fetch_add(pips.len(), std::sync::atomic::Ordering::SeqCst);

                    let middle = (split_line_over_y((source, sink_loc), y), y);
                    let middle = (middle.0.clamp(1, ctx.grid_dim_x()-1), middle.1.clamp(1, ctx.grid_dim_y()-1));
                    let pips = match source_is_north {
                        true => pips_s.get(&middle).unwrap(),
                        false => pips_n.get(&middle).unwrap(),
                    };

                    let (vert_pip, pip_uses) = pips
                        .iter()
                        .min_by_key(|(pip, uses)| {
                            let src_to_pip =
                                ctx.estimate_delay(source_wire, ctx.pip_src_wire(*pip));
                            let pip_to_snk = ctx.estimate_delay(ctx.pip_dst_wire(*pip), sink_wire);
                            let uses = uses.read().unwrap();
                            let uses = uses.len() - (uses.contains(name) as usize);
                            (1000.0 * (src_to_pip + ((uses + 1) as f32) * pip_to_snk)) as u64
                        })
                        .unwrap();
                    pip_uses.write().unwrap().push(*name);
                    let vert_pip = *vert_pip;
                    explored_pips.fetch_add(pips.len(), std::sync::atomic::Ordering::SeqCst);

                    let horiz_loc = ctx.pip_location(horiz_pip);
                    let horiz_is_east = horiz_loc.y < y;
                    let vert_loc = ctx.pip_location(vert_pip);
                    let (src_to_mid1, mid1_to_mid2, mid2_to_dst) =
                        if horiz_is_east == source_is_east {
                            (
                                ((source.x, source.y), (horiz_loc.x, horiz_loc.y)),
                                ((horiz_loc.x, horiz_loc.y), (vert_loc.x, vert_loc.y)),
                                ((vert_loc.x, vert_loc.y), (sink_loc.x, sink_loc.y)),
                            )
                        } else {
                            (
                                ((source.x, source.y), (vert_loc.x, vert_loc.y)),
                                ((vert_loc.x, vert_loc.y), (horiz_loc.x, horiz_loc.y)),
                                ((horiz_loc.x, horiz_loc.y), (sink_loc.x, sink_loc.y)),
                            )
                        };
                    let (seg1, seg2, seg3) = match (source_is_north, source_is_east, horiz_is_east)
                    {
                        (true, true, true) => {
                            (Segment::Northeast, Segment::Southeast, Segment::Southwest)
                        }
                        (true, true, false) => {
                            (Segment::Northeast, Segment::Northwest, Segment::Southwest)
                        }
                        (true, false, true) => {
                            (Segment::Northwest, Segment::Northeast, Segment::Southeast)
                        }
                        (true, false, false) => {
                            (Segment::Northwest, Segment::Southwest, Segment::Southeast)
                        }
                        (false, true, true) => {
                            (Segment::Southeast, Segment::Northeast, Segment::Northwest)
                        }
                        (false, true, false) => {
                            (Segment::Southeast, Segment::Southwest, Segment::Northwest)
                        }
                        (false, false, true) => {
                            (Segment::Southwest, Segment::Southeast, Segment::Northeast)
                        }
                        (false, false, false) => {
                            (Segment::Southwest, Segment::Northwest, Segment::Northeast)
                        }
                    };
                    part_diag.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
                    vec![
                        (seg1, src_to_mid1),
                        (seg2, mid1_to_mid2),
                        (seg3, mid2_to_dst),
                    ]
                }
            })
            .collect::<Vec<_>>();

        for (segment, arc) in arcs {
            match segment {
                Segment::Northeast => ne.push(arc),
                Segment::Southeast => se.push(arc),
                Segment::Southwest => sw.push(arc),
                Segment::Northwest => nw.push(arc),
            }
        }
    }

    progress.finish_and_clear();

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

fn route(ctx: &mut npnr::Context) -> bool {
    log_info!(
        "{}{}{}{}{}{} from Rust!\n",
        "A".red(),
        "w".green(),
        "o".yellow(),
        "o".blue(),
        "o".magenta(),
        "o".cyan()
    );
    log_info!(
        "Running on a {}x{} grid\n",
        ctx.grid_dim_x().to_string().bold(),
        ctx.grid_dim_y().to_string().bold(),
    );

    let wires = ctx.wires_leaking();
    log_info!("Found {} wires\n", wires.len().to_string().bold());

    let pips = ctx.pips_leaking();
    log_info!("Found {} pips\n", pips.len().to_string().bold());

    let nets = npnr::Nets::new(ctx);
    let nets_str = nets.len().to_string();
    log_info!("Found {} nets\n", nets_str.bold());

    let mut count = 0;
    for (name, net) in nets.iter() {
        let _src = ctx.source_wire(*net);
        let net = unsafe { net.as_mut().unwrap() };
        let users = nets.users_by_name(*name).unwrap().iter();
        for user in users {
            count += ctx.sink_wires(net, *user).len();
        }
    }

    log_info!("Found {} arcs\n", count.to_string().bold());

    let (name, net) = nets
        .iter()
        .max_by_key(|(name, net)| {
            let net = unsafe { net.as_mut().unwrap() };
            if net.is_global() {
                0
            } else {
                nets.users_by_name(**name)
                    .unwrap()
                    .iter()
                    .fold(0, |acc, sink| acc + ctx.sink_wires(net, *sink).len())
            }
        })
        .unwrap();

    let net = unsafe { net.as_mut().unwrap() };
    let count = nets
        .users_by_name(*name)
        .unwrap()
        .iter()
        .fold(0, |acc, sink| acc + ctx.sink_wires(net, *sink).len())
        .to_string();

    log_info!(
        "Highest non-global fanout net is {}\n",
        ctx.name_of(*name).to_str().unwrap().bold()
    );
    log_info!("  with {} arcs\n", count.bold());

    let mut x0 = 0;
    let mut y0 = 0;
    let mut x1 = 0;
    let mut y1 = 0;

    for sink in nets.users_by_name(*name).unwrap().iter() {
        let cell = sink.cell().unwrap().location();
        x0 = x0.min(cell.x);
        y0 = y0.min(cell.y);
        x1 = x1.max(cell.x);
        y1 = y1.max(cell.y);
    }

    let coords_min = format!("({}, {})", x0, y0);
    let coords_max = format!("({}, {})", x1, y1);
    log_info!(
        "  which spans from {} to {}\n",
        coords_min.bold(),
        coords_max.bold()
    );

    log_info!("rayon reports {} threads available\n", rayon::current_num_threads().to_string().bold());

    let (x_part, y_part, ne, se, sw, nw) =
        find_partition_point(ctx, &nets, pips, 0, ctx.grid_dim_x(), 0, ctx.grid_dim_y());

    let mut invalid_arcs_in_ne = 0;
    let mut invalid_arcs_in_se = 0;
    let mut invalid_arcs_in_sw = 0;
    let mut invalid_arcs_in_nw = 0;

    for ((source_x, source_y), (sink_x, sink_y)) in ne {
        if source_x > x_part || source_y > y_part || sink_x > x_part || sink_y > y_part {
            invalid_arcs_in_ne += 1;
        }
    }
    for ((source_x, source_y), (sink_x, sink_y)) in se {
        if source_x < x_part || source_y > y_part || sink_x < x_part || sink_y > y_part {
            invalid_arcs_in_se += 1;
        }
    }
    for ((source_x, source_y), (sink_x, sink_y)) in sw {
        if source_x < x_part || source_y < y_part || sink_x < x_part || sink_y < y_part {
            invalid_arcs_in_sw += 1;
        }
    }
    for ((source_x, source_y), (sink_x, sink_y)) in nw {
        if source_x > x_part || source_y < y_part || sink_x > x_part || sink_y < y_part {
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

    /*log_info!("=== level 2 NE:\n");
    let _ = find_partition_point(&ne, x_start, x, y_start, y);
    log_info!("=== level 2 SE:\n");
    let _ = find_partition_point(&se, x, x_finish, y_start, y);
    log_info!("=== level 2 SW:\n");
    let _ = find_partition_point(&sw, x, x_finish, y, y_finish);
    log_info!("=== level 2 NW:\n");
    let _ = find_partition_point(&nw, x_start, x, y, y_finish);*/

    true
}
