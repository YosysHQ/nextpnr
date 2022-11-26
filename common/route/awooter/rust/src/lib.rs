use std::{collections::HashMap, ptr::NonNull};

use colored::Colorize;
use indicatif::{ProgressBar, ProgressStyle};
use rayon::prelude::*;

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

    let mut ne = Vec::new();
    let mut se = Vec::new();
    let mut sw = Vec::new();
    let mut nw = Vec::new();

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

fn partition_nets(
    ctx: &npnr::Context,
    nets: &npnr::Nets,
    pips: &[npnr::PipId],
    x: i32,
    y: i32,
) -> (ArcVec, ArcVec, ArcVec, ArcVec) {
    let mut partition_pips = HashMap::new();

    let mut ne = Vec::new();
    let mut se = Vec::new();
    let mut sw = Vec::new();
    let mut nw = Vec::new();
    let mut part_horiz = 0;
    let mut part_vert = 0;
    let mut part_diag = 0;

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
    //let mut pips_e2w = HashMap::new();
    //let mut pips_w2e = HashMap::new();
    
    for &pip in pips {
        let loc = ctx.pip_location(pip);
        if loc.x == x || loc.y == y {
            let src = ctx.pip_src_wire(pip);
            let dst = ctx.pip_dst_wire(pip);

            partition_pips
                .entry((loc.x, loc.y))
                .and_modify(|pip_list: &mut Vec<(npnr::PipId, Vec<npnr::IdString>)>| {
                    pip_list.push((pip, Vec::new()))
                })
                .or_insert_with(|| vec![(pip, Vec::new())]);
        }
    }

    let progress = ProgressBar::new(nets.len() as u64);
    progress.set_style(
        ProgressStyle::with_template("[{elapsed}] [{bar:40.cyan/blue}] {msg}")
            .unwrap()
            .progress_chars("━╸ "),
    );

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

        for sink in nets.users_by_name(*name).unwrap().iter() {
            let sink = unsafe { sink.as_ref().unwrap() };
            let sink_loc = sink.cell().unwrap().location();
            let sink_is_north = sink_loc.x < x;
            let sink_is_east = sink_loc.y < y;

            for sink_wire in ctx.sink_wires(net, sink) {
                if source_is_north == sink_is_north && source_is_east == sink_is_east {
                    let arc = ((source.x, source.y), (sink_loc.x, sink_loc.y));
                    match (source_is_north, source_is_east) {
                        (true, true) => ne.push(arc),
                        (true, false) => nw.push(arc),
                        (false, true) => se.push(arc),
                        (false, false) => sw.push(arc),
                    }
                } else if source_is_north != sink_is_north && source_is_east == sink_is_east {
                    let middle = ((source.x + sink_loc.x) / 2, y);
                    let pips = partition_pips.get_mut(&middle).unwrap();

                    let (selected_pip, pip_uses) = pips
                        .par_iter_mut()
                        .min_by_key(|(pip, uses)| {
                            let src_to_pip =
                                ctx.estimate_delay(source_wire, ctx.pip_src_wire(*pip));
                            let pip_to_snk = ctx.estimate_delay(ctx.pip_dst_wire(*pip), sink_wire);
                            let uses = uses.len() - (uses.contains(name) as usize);
                            (1000.0 * (src_to_pip + ((uses + 1) as f32) * pip_to_snk)) as u64
                        })
                        .unwrap();
                    pip_uses.push(*name);

                    let pip_loc = ctx.pip_location(*selected_pip);
                    let src_to_pip = ((source.x, source.y), (pip_loc.x, pip_loc.y));
                    let pip_to_dst = ((pip_loc.x, pip_loc.y), (sink_loc.x, sink_loc.y));
                    match (source_is_north, source_is_east) {
                        (true, true) => {
                            ne.push(src_to_pip);
                            se.push(pip_to_dst);
                        }
                        (true, false) => {
                            nw.push(src_to_pip);
                            sw.push(pip_to_dst);
                        }
                        (false, true) => {
                            se.push(src_to_pip);
                            ne.push(pip_to_dst);
                        }
                        (false, false) => {
                            sw.push(src_to_pip);
                            nw.push(pip_to_dst);
                        }
                    }

                    part_horiz += 1;
                } else if source_is_north == sink_is_north && source_is_east != sink_is_east {
                    let middle = (x, (source.y + sink_loc.y) / 2);
                    let pips = partition_pips.get_mut(&middle).unwrap();

                    let (selected_pip, pip_uses) = pips
                        .par_iter_mut()
                        .min_by_key(|(pip, uses)| {
                            let src_to_pip =
                                ctx.estimate_delay(source_wire, ctx.pip_src_wire(*pip));
                            let pip_to_snk = ctx.estimate_delay(ctx.pip_dst_wire(*pip), sink_wire);
                            let uses = uses.len() - (uses.contains(name) as usize);
                            (1000.0 * (src_to_pip + ((uses + 1) as f32) * pip_to_snk)) as u64
                        })
                        .unwrap();
                    pip_uses.push(*name);

                    let pip_loc = ctx.pip_location(*selected_pip);
                    let src_to_pip = ((source.x, source.y), (pip_loc.x, pip_loc.y));
                    let pip_to_dst = ((pip_loc.x, pip_loc.y), (sink_loc.x, sink_loc.y));
                    match (source_is_north, source_is_east) {
                        (true, true) => {
                            ne.push(src_to_pip);
                            nw.push(pip_to_dst);
                        }
                        (true, false) => {
                            nw.push(src_to_pip);
                            ne.push(pip_to_dst);
                        }
                        (false, true) => {
                            se.push(src_to_pip);
                            sw.push(pip_to_dst);
                        }
                        (false, false) => {
                            sw.push(src_to_pip);
                            se.push(pip_to_dst);
                        }
                    }

                    part_vert += 1;
                } else {
                    // BUG: this doesn't bound the pip to be strictly east or west,
                    // leading to a possible situation where when connecting a NW source
                    // to a SE sink, the horizontal pip is found in the E boundary,
                    // then the 
                    let middle = (x, (source.y + sink_loc.y) / 2);
                    let pips = partition_pips.get_mut(&middle).unwrap();

                    let (horiz_pip, pip_uses) = pips
                        .par_iter_mut()
                        .min_by_key(|(pip, uses)| {
                            let src_to_pip =
                                ctx.estimate_delay(source_wire, ctx.pip_src_wire(*pip));
                            let pip_to_snk = ctx.estimate_delay(ctx.pip_dst_wire(*pip), sink_wire);
                            let uses = uses.len() - (uses.contains(name) as usize);
                            (1000.0 * (src_to_pip + ((uses + 1) as f32) * pip_to_snk)) as u64
                        })
                        .unwrap();
                    pip_uses.push(*name);
                    let horiz_pip = *horiz_pip;

                    let middle = ((source.x + sink_loc.x) / 2, y);
                    let pips = partition_pips.get_mut(&middle).unwrap();

                    let (vert_pip, pip_uses) = pips
                        .par_iter_mut()
                        .min_by_key(|(pip, uses)| {
                            let src_to_pip =
                                ctx.estimate_delay(source_wire, ctx.pip_src_wire(*pip));
                            let pip_to_snk = ctx.estimate_delay(ctx.pip_dst_wire(*pip), sink_wire);
                            let uses = uses.len() - (uses.contains(name) as usize);
                            (1000.0 * (src_to_pip + ((uses + 1) as f32) * pip_to_snk)) as u64
                        })
                        .unwrap();
                    pip_uses.push(*name);

                    let horiz_loc = ctx.pip_location(horiz_pip);
                    let vert_loc = ctx.pip_location(*vert_pip);
                    let src_to_horiz = ((source.x, source.y), (horiz_loc.x, horiz_loc.y));
                    let horiz_to_vert = ((horiz_loc.x, horiz_loc.y), (vert_loc.x, vert_loc.y));
                    let vert_to_dst = ((vert_loc.x, vert_loc.y), (sink_loc.x, sink_loc.y));
                    match (source_is_north, source_is_east) {
                        (true, true) => {
                            ne.push(src_to_horiz);
                            nw.push(horiz_to_vert);
                            sw.push(vert_to_dst);
                        }
                        (true, false) => {
                            nw.push(src_to_horiz);
                            ne.push(horiz_to_vert);
                            se.push(vert_to_dst);
                        }
                        (false, true) => {
                            se.push(src_to_horiz);
                            sw.push(horiz_to_vert);
                            nw.push(vert_to_dst);
                        }
                        (false, false) => {
                            sw.push(src_to_horiz);
                            se.push(horiz_to_vert);
                            ne.push(vert_to_dst);
                        }
                    }

                    part_diag += 1;
                }
            }
        }
    }

    progress.finish_and_clear();

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
        part_horiz.to_string().bold()
    );
    log_info!(
        "  {} arcs partitioned vertically\n",
        part_vert.to_string().bold()
    );
    log_info!(
        "  {} arcs partitioned both ways\n",
        part_diag.to_string().bold()
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
            count += ctx.sink_wires(net, *user).count();
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
                    .fold(0, |acc, sink| acc + ctx.sink_wires(net, *sink).count())
            }
        })
        .unwrap();

    let net = unsafe { net.as_mut().unwrap() };
    let count = nets
        .users_by_name(*name)
        .unwrap()
        .iter()
        .fold(0, |acc, sink| acc + ctx.sink_wires(net, *sink).count())
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
        let sink = unsafe { sink.as_ref().unwrap() };
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

    let _ = find_partition_point(ctx, &nets, pips, 0, ctx.grid_dim_x(), 0, ctx.grid_dim_y());

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
