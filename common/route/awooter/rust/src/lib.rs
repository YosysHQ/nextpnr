#![feature(c_unwind)]

use std::{ptr::NonNull, time::Instant};

use colored::Colorize;

use crate::partition::Coord;

#[macro_use]
mod npnr;
mod partition;
mod route;

#[no_mangle]
pub extern "C-unwind" fn npnr_router_awooter(ctx: Option<NonNull<npnr::Context>>) -> bool {
    let ctx: &mut npnr::Context = unsafe { ctx.expect("non-null context").as_mut() };
    route(ctx)

    /*std::panic::catch_unwind(move || {
        let ctx: &mut npnr::Context = unsafe { ctx.expect("non-null context").as_mut() };
        route(ctx)
    })
    .unwrap_or_else(|x| {
        if let Ok(x) = x.downcast::<String>() {
            log_error!("caught panic: {}", x);
        }
        false
    })*/
}

fn extract_arcs_from_nets(ctx: &npnr::Context, nets: &npnr::Nets) -> Vec<route::Arc> {
    let mut arcs = vec![];
    for (name, net) in nets.to_vec().iter() {
        let net = unsafe { net.as_mut().unwrap() };
        if net.is_global() {
            continue;
        }
        let port_ref = net.driver();
        let port_ref = unsafe { port_ref.as_ref().unwrap() };
        if let Some(cell) = port_ref.cell() {
            let source = cell.location();
            let source_wire = ctx.source_wire(net);

            for sink_ref in nets.users_by_name(**name).unwrap().iter() {
                let sink = sink_ref.cell().unwrap();
                let sink = sink.location();
                for sink_wire in ctx.sink_wires(net, *sink_ref) {
                    arcs.push(route::Arc::new(
                        source_wire,
                        source,
                        sink_wire,
                        sink,
                        net.index(),
                    ))
                }
            }
        }
    }
    arcs
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
    for (&name, net) in nets.to_vec().iter() {
        let _src = ctx.source_wire(**net);
        let net = unsafe { net.as_mut().unwrap() };
        let users = nets.users_by_name(name).unwrap().iter();
        for user in users {
            count += ctx.sink_wires(net, *user).len();
        }
    }

    log_info!("Found {} arcs\n", count.to_string().bold());

    let binding = nets.to_vec();
    let (name, net) = binding
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
        .users_by_name(**name)
        .unwrap()
        .iter()
        .fold(0, |acc, sink| acc + ctx.sink_wires(net, *sink).len())
        .to_string();

    log_info!(
        "Highest non-global fanout net is {}\n",
        ctx.name_of(**name).to_str().unwrap().bold()
    );
    log_info!("  with {} arcs\n", count.bold());

    let mut x0 = 0;
    let mut y0 = 0;
    let mut x1 = 0;
    let mut y1 = 0;

    for sink in nets.users_by_name(**name).unwrap().iter() {
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

    log_info!(
        "rayon reports {} threads available\n",
        rayon::current_num_threads().to_string().bold()
    );

    let start = Instant::now();

    let arcs = extract_arcs_from_nets(ctx, &nets);

    let (x_part, y_part, ne, se, sw, nw, misc) = partition::find_partition_point_and_sanity_check(
        ctx,
        &nets,
        &arcs[..],
        pips,
        0,
        ctx.grid_dim_x(),
        0,
        ctx.grid_dim_y(),
    );

    let time = Instant::now() - start;

    log_info!("Partitioning took {:.2}s\n", time.as_secs_f32());

    let mut router = route::Router::new(Coord::new(0, 0), Coord::new(x_part, y_part));
    log_info!("Routing northeast arcs");
    router.route(ctx, &nets, &ne);
    log_info!("Routing southeast arcs");
    router.route(ctx, &nets, &se);
    log_info!("Routing southwest arcs");
    router.route(ctx, &nets, &sw);
    log_info!("Routing northwest arcs");
    router.route(ctx, &nets, &nw);
    log_info!("Routing miscellaneous arcs");
    router.route(ctx, &nets, &misc);
    //let mut router = route::Router::new(Coord::new(0, 0), Coord::new(x_part, y_part));

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