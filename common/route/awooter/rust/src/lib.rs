use std::ptr::NonNull;

#[macro_use]
mod npnr;

enum Subpartition {
    Part(Box<Partition>),
    Nets(Vec<Net>),
}

struct Partition {
    parts: [Option<Subpartition>; 4],
    borders: [[Vec<npnr::WireId>; 4]; 4]
}

struct Net {
    source: npnr::WireId,
    sinks: Vec<npnr::WireId>
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

fn route(ctx: &mut npnr::Context) -> bool {
    log_info!("Hello from Rust!\n");
    log_info!(
        "Running on a {}x{} grid\n",
        ctx.grid_dim_x(),
        ctx.grid_dim_y()
    );
    let _belid = npnr::BelId::null();
    log_info!("Managed to survive BelId()\n");

    let nets = npnr::NetIter::new(ctx).into_iter().collect::<Vec<_>>();
    log_info!("Found {} nets\n", nets.len());

    true
}