use std::ptr::NonNull;

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

type ArcSlice = [((i32, i32), (i32, i32))];
type ArcVec = Vec<((i32, i32), (i32, i32))>;

fn partition(arcs: &ArcSlice, x: i32, y: i32) -> (ArcVec, ArcVec, ArcVec, ArcVec) {
    let mut ne = Vec::new();
    let mut se = Vec::new();
    let mut sw = Vec::new();
    let mut nw = Vec::new();
    let mut part_horiz = 0;
    let mut part_vert = 0;
    let mut part_diag = 0;

    log_info!("Partitioning arcs along X = {}, Y = {}\n", x, y);

    for (source, sink) in arcs {
        let (source_x, source_y) = source;
        let (sink_x, sink_y) = sink;
        let source_is_north = *source_x < x;
        let source_is_east = *source_y < y;
        let sink_is_north = *sink_x < x;
        let sink_is_east = *sink_y < y;

        // If these segments are already inside a partition, just store them as-is.
        if source_is_north == sink_is_north && source_is_east == sink_is_east {
            match (source_is_north, source_is_east) {
                (true, true) => ne.push((*source, *sink)),
                (true, false) => nw.push((*source, *sink)),
                (false, true) => se.push((*source, *sink)),
                (false, false) => sw.push((*source, *sink)),
            }
            continue;
        }

        // Partition horizontally.
        if source_is_north != sink_is_north && source_is_east == sink_is_east {
            match source_is_east {
                true => {
                    ne.push((*source, (x, *sink_y)));
                    se.push(((x, *sink_y), *sink));
                }
                false => {
                    nw.push((*source, (x, *sink_y)));
                    sw.push(((x, *sink_y), *sink));
                }
            }
            part_horiz += 1;
            continue;
        }

        // Partition vertically.
        if source_is_north == sink_is_north && source_is_east != sink_is_east {
            match source_is_north {
                true => {
                    ne.push((*source, (*sink_x, y)));
                    nw.push(((*sink_x, y), *sink));
                }
                false => {
                    se.push((*source, (*sink_x, y)));
                    sw.push(((*sink_x, y), *sink));
                }
            }
            part_vert += 1;
            continue;
        }

        // Partition both ways.
        match (source_is_north, source_is_east) {
            (true, true) => {
                ne.push((*source, (x, *source_y)));
                se.push(((x, *source_y), (*sink_x, y)));
                sw.push(((*sink_x, y), *sink))
            }
            (true, false) => {
                nw.push((*source, (x, *source_y)));
                sw.push(((x, *source_y), (*sink_x, y)));
                se.push(((*sink_x, y), *sink))
            }
            (false, true) => {
                se.push((*source, (x, *source_y)));
                ne.push(((x, *source_y), (*sink_x, y)));
                nw.push(((*sink_x, y), *sink))
            }
            (false, false) => {
                sw.push((*source, (x, *source_y)));
                nw.push(((x, *source_y), (*sink_x, y)));
                ne.push(((*sink_x, y), *sink))
            }
        }
        part_diag += 1;
    }

    /*log_info!("  {} arcs partitioned horizontally\n", part_horiz);
    log_info!("  {} arcs partitioned vertically\n", part_vert);
    log_info!("  {} arcs partitioned both ways\n", part_diag);
    log_info!("  {} arcs in the northeast\n", ne.len());
    log_info!("  {} arcs in the southeast\n", se.len());
    log_info!("  {} arcs in the southwest\n", sw.len());
    log_info!("  {} arcs in the northwest\n", nw.len());*/

    (ne, se, sw, nw)
}

fn find_partition_point(
    arcs: &ArcSlice,
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
        (ne, se, sw, nw) = partition(arcs, x, y);
        let north = ne.len() + nw.len();
        let south = se.len() + sw.len();
        if north > south {
            x -= x_diff;
        } else if north < south {
            x += x_diff;
        }

        let east = ne.len() + se.len();
        let west = nw.len() + sw.len();
        if east > west {
            y -= y_diff;
        } else if east < west {
            y += y_diff;
        }

        x_diff >>= 1;
        y_diff >>= 1;

        let nets = (north + south) as f64;

        let ne_dist = f64::abs(((ne.len() as f64) / nets) - 0.25);
        let se_dist = f64::abs(((se.len() as f64) / nets) - 0.25);
        let sw_dist = f64::abs(((sw.len() as f64) / nets) - 0.25);
        let nw_dist = f64::abs(((nw.len() as f64) / nets) - 0.25);

        log_info!(
            "Distortion: {:.02}%\n",
            100.0 * (ne_dist + se_dist + sw_dist + nw_dist)
        );
    }

    (ne, se, sw, nw) = partition(arcs, x, y);

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

fn route(ctx: &mut npnr::Context) -> bool {
    log_info!("Awoooo from Rust!\n");
    log_info!(
        "Running on a {}x{} grid\n",
        ctx.grid_dim_x(),
        ctx.grid_dim_y()
    );

    let nets = ctx.net_iter().collect::<Vec<_>>();
    log_info!("Found {} nets\n", nets.len());

    let mut count = 0;
    for (_name, net) in &nets {
        let _src = ctx.source_wire(*net);
        let net = unsafe { net.as_mut().unwrap() };
        for user in net.users() {
            count += ctx.sink_wires(net, user).count();
        }
    }

    log_info!("Found {} arcs\n", count);

    let (name, net) = nets
        .iter()
        .max_by_key(|(_name, net)| {
            let net = unsafe { net.as_mut().unwrap() };
            if net.is_global() {
                0
            } else {
                net.users()
                    .fold(0, |acc, sink| acc + ctx.sink_wires(net, sink).count())
            }
        })
        .unwrap();

    let net = unsafe { net.as_mut().unwrap() };
    let count = net
        .users()
        .fold(0, |acc, sink| acc + ctx.sink_wires(net, sink).count());

    log_info!(
        "Highest non-global fansnout net is {} with {} arcs\n",
        ctx.name_of(*name).to_str().unwrap(),
        count
    );

    let mut x0 = 0;
    let mut y0 = 0;
    let mut x1 = 0;
    let mut y1 = 0;

    for sink in net.users() {
        let sink = unsafe { sink.as_ref().unwrap() };
        let cell = sink.cell().unwrap();
        x0 = x0.min(cell.location_x());
        y0 = y0.min(cell.location_y());
        x1 = x1.max(cell.location_x());
        y1 = y1.max(cell.location_y());
    }

    log_info!("  which spans ({}, {}) to ({}, {})\n", x0, y0, x1, y1);

    let mut arcs = Vec::new();

    for (_name, net) in &nets {
        let net = unsafe { net.as_mut().unwrap() };
        let source = unsafe { net.driver().as_ref().unwrap() };

        let source_cell = source.cell();
        if source_cell.is_none() {
            continue;
        }
        let source_cell = source_cell.unwrap();
        let source_x = source_cell.location_x();
        let source_y = source_cell.location_y();

        for sink in net.users() {
            let sink = unsafe { sink.as_ref().unwrap() };
            let sink_x = sink.cell().unwrap().location_x();
            let sink_y = sink.cell().unwrap().location_y();

            arcs.push(((source_x, source_y), (sink_x, sink_y)));
        }
    }

    let x_start = 0;
    let x_finish = ctx.grid_dim_x();
    let y_start = 0;
    let y_finish = ctx.grid_dim_y();
    log_info!("=== level 1:\n");
    let (x, y, ne, se, sw, nw) = find_partition_point(&arcs, x_start, x_finish, y_start, y_finish);

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
