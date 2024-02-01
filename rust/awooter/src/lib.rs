use nextpnr::{Context, IdString, NetInfo, Nets, PortRef};

mod route;

fn make_route_arcs(arcs: &[(IdString, &NetInfo, &PortRef)]) -> Vec<route::Arc> {
    let mut v = Vec::new();

    for (index, &(name, info, port)) in arcs.iter().enumerate() {
        v.push(route::Arc::new(info.driver()))
    }

    v
}

#[no_mangle]
pub extern "C" fn rust_awooter(ctx: &mut Context) {
    let nets = Nets::new(ctx);
    let arcs = nets.to_arc_vec();
    let wires = ctx.wires_leaking();
    let mut router = route::Router::new(&nets, wires, 5.0, 0.5);
    let mut progress = indicatif::MultiProgress::new();
    let thread = route::RouterThread::new(arcs, "main", &progress);
    router.route(ctx, &nets, &mut thread);
}