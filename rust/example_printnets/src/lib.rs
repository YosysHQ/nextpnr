use nextpnr::Context;

#[no_mangle]
pub extern "C" fn rust_example_printnets(ctx: &mut Context) {
    println!("Nets in design:");
    for (name, net) in &ctx.nets() {
        let net = unsafe { &*net };
        let driver = net.driver();
        println!("  {}:", ctx.name_of(name).to_str().unwrap());
        if let Some(driver) = driver {
            let cell = driver.cell().map_or("(no cell)", |cell| {
                ctx.name_of(cell.name()).to_str().unwrap()
            });
            let port = ctx.name_of(driver.port()).to_str().unwrap();
            println!("    driver: {cell}.{port}");
        } else {
            println!("    driver: (no driver)");
        }
        println!("    sinks:");
        for (user, port) in net.users() {
            let user = unsafe { &*user };
            let user = ctx.name_of(user.name()).to_str().unwrap();
            let port = ctx.name_of(port).to_str().unwrap();
            println!("      {user}.{port}");
        }
    }
}
