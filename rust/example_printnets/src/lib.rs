use nextpnr::{Context, Nets};

#[no_mangle]
pub extern "C" fn rust_example_printnets(ctx: &mut Context) {
    let nets = Nets::new(ctx);
    let nets_vec = nets.to_vec();

    println!("Nets in design:");
    for (&name, _net) in nets_vec {
        println!("  {}", ctx.name_of(name).to_str().unwrap());
    }
}
