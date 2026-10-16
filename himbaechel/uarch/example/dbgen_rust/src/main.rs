use std::env;
use std::io::*;
use himbaechel_dbgen::idstring::*;
use himbaechel_dbgen::chipdb::*;

// Grid size including IOBs at edges
const X : u16 = 100;
const Y : u16 = 100;

// LUT input count
const K : u16 = 4;

// SLICEs per tile
const N : u16 = 8;

// local wire count
const WL : u16 = N * (K + 1) + 16;

// 1/Fc for bel input wire pips; local wire pips and neighbour pips
const SI : u16 = 6;
const SQ : u16 = 6;
const SL : u16 = 1;

const DIRS : [(&str, i16, i16); 8] = [
    ("N", 0, -1),
    ("NE", 1, -1),
    ("E", 1, 0),
    ("SE", 1, 1),
    ("S", 0, 1),
    ("SW", -1, 1),
    ("W", -1, 0),
    ("NW", -1, -1)

];

fn create_switch_matrix(tt: &mut TileType, strs: &mut StringPool, tmg: &mut TimingPool, inputs: &[String], outputs: &[String]) {
    let vcc = tt.create_wire(strs.id("VCC"), strs.id("VCC"));
    vcc.const_value = strs.id("VCC");
    let gnd = tt.create_wire(strs.id("GND"), strs.id("GND"));
    gnd.const_value = strs.id("GND");
    // local switch wires
    for i in 0..WL {
        tt.create_wire(strs.id(&format!("SWITCH{}", i)), strs.id("SWITCH"));
    }
    // neighbour wires, later connected in nodes
    for i in 0..WL {
        for (d, _, _) in DIRS {
            tt.create_wire(strs.id(&format!("{}{}", d, i)), strs.id(&format!("NEIGH_{}", d)));
        }
    }
    // input pips
    for (i, w) in inputs.iter().enumerate() {
        for j in (((i as u16) % SI)..WL).step_by(SI.into()) {
            tt.create_pip(strs.id(&format!("SWITCH{}", j)), strs.id(w)).with_timing(tmg.pip_class(strs.id("SWINPUT")));
        }
    }
    // output pips
    for (i, w) in outputs.iter().enumerate() {
        for j in (((i as u16) % SQ)..WL).step_by(SQ.into()) {
            tt.create_pip(strs.id(w), strs.id(&format!("SWITCH{}", j))).with_timing(tmg.pip_class(strs.id("SWINPUT")));
        }
    }
    // constant pips
    for i in 0..WL {
        tt.create_pip(strs.id("GND"), strs.id(&format!("SWITCH{}", i)));
        tt.create_pip(strs.id("VCC"), strs.id(&format!("SWITCH{}", i)));
    }
    // neighbour local pips
    for i in 0..WL {
        for (j, (d, _, _)) in DIRS.iter().enumerate() {
            tt.create_pip(strs.id(&format!("{}{}", d, (i + j as u16) % WL)), strs.id(&format!("SWITCH{}", i))).with_timing(tmg.pip_class(strs.id("SWNEIGH")));
        }
    }
    // clock "ladder"
    if !tt.has_wire(strs.id("CLK")) {
        tt.create_wire(strs.id("CLK"), strs.id("TILE_CLK"));
    }
    tt.create_wire(strs.id("CLK_PREV"), strs.id("CLK_ROUTE"));
    tt.create_pip(strs.id("CLK_PREV"), strs.id("CLK"));
}

fn create_logic_tiletype(chip: &mut Chip, strs: &mut StringPool, tmg: &mut TimingPool) {
    let tt = chip.create_tile_type(strs.id("LOGIC"));
    let mut inputs = Vec::<String>::new();
    let mut outputs = Vec::<String>::new();
    // init wires
    for i in 0..N {
        for j in 0..K {
            inputs.push(format!("L{}_{}", i, j));
            tt.create_wire(strs.id(&format!("L{}_{}", i, j)), strs.id("LUT_INPUT"));
        }
        tt.create_wire(strs.id(&format!("L{}_D", i)), strs.id("FF_DATA"));
        tt.create_wire(strs.id(&format!("L{}_O", i)), strs.id("LUT_OUT"));
        tt.create_wire(strs.id(&format!("L{}_Q", i)), strs.id("FF_OUT"));
        outputs.push(format!("L{}_O", i));
        outputs.push(format!("L{}_Q", i));
    }

    tt.create_wire(strs.id("CLK"), strs.id("TILE_CLK"));

    // create logic cells
    for i in 0..N {
        // LUT
        let lut = tt.create_bel(strs.id(&format!("L{}_LUT", i)), strs.id("LUT4"), i*2 + 0).index;
        for j in 0..K {
            tt.add_bel_pin(lut, strs.id(&format!("I[{}]", j)), strs.id(&format!("L{}_{}", i, j)), PinType::INPUT);
        }
        tt.add_bel_pin(lut, strs.id("F"), strs.id(&format!("L{}_O", i)), PinType::OUTPUT);
        // FF data can come from LUT output or LUT I3
        tt.create_pip(strs.id(&format!("L{}_O", i)), strs.id(&format!("L{}_D", i)));
        tt.create_pip(strs.id(&format!("L{}_{}", i, K-1)), strs.id(&format!("L{}_D", i)));
        // FF
        let ff = tt.create_bel(strs.id(&format!("L{}_FF", i)), strs.id("DFF"), i*2 + 1).index;
        tt.add_bel_pin(ff, strs.id("D"), strs.id(&format!("L{}_D", i)), PinType::INPUT);
        tt.add_bel_pin(ff, strs.id("CLK"), strs.id("CLK"), PinType::INPUT);
        tt.add_bel_pin(ff, strs.id("Q"), strs.id(&format!("L{}_Q", i)), PinType::OUTPUT);
    }
    create_switch_matrix(tt, strs, tmg, &inputs, &outputs);
}

const N_IO: u16 = 2;

fn create_io_tiletype(chip: &mut Chip, strs: &mut StringPool, tmg: &mut TimingPool) {
    let tt = chip.create_tile_type(strs.id("IO"));
    let mut inputs = Vec::<String>::new();
    let mut outputs = Vec::<String>::new();
    // init wires
    for i in 0..N_IO {
        tt.create_wire(strs.id(&format!("IO{}_T", i)), strs.id("IO_T"));
        tt.create_wire(strs.id(&format!("IO{}_I", i)), strs.id("IO_I"));
        tt.create_wire(strs.id(&format!("IO{}_O", i)), strs.id("IO_O"));
        tt.create_wire(strs.id(&format!("IO{}_PAD", i)), strs.id("IO_PAD"));

        inputs.push(format!("IO{}_I", i));
        inputs.push(format!("IO{}_T", i));

        outputs.push(format!("IO{}_O", i));
    }
    tt.create_wire(strs.id("CLK"), strs.id("TILE_CLK"));
    for i in 0..N_IO {
        let io = tt.create_bel(strs.id(&format!("IO{}", i)), strs.id("IOB"), i).index;
        tt.add_bel_pin(io, strs.id("I"), strs.id(&format!("IO{}_I", i)), PinType::INPUT);
        tt.add_bel_pin(io, strs.id("T"), strs.id(&format!("IO{}_T", i)), PinType::INPUT);
        tt.add_bel_pin(io, strs.id("O"), strs.id(&format!("IO{}_O", i)), PinType::OUTPUT);
        tt.add_bel_pin(io, strs.id("PAD"), strs.id(&format!("IO{}_PAD", i)), PinType::INOUT);
    }
    // Actually used in top left IO only
    tt.create_wire(strs.id("GCLK_OUT"), strs.id("GCLK"));
    tt.create_pip(strs.id("IO0_O"), strs.id("GCLK_OUT"));
    create_switch_matrix(tt, strs, tmg, &inputs, &outputs);

}

fn create_corner_tiletype(chip: &mut Chip, strs: &mut StringPool) {
    let tt = chip.create_tile_type(strs.id("NULL"));

    tt.create_wire(strs.id("CLK"), strs.id("TILE_CLK"));
    tt.create_wire(strs.id("CLK_PREV"), strs.id("CLK_ROUTE"));
    tt.create_pip(strs.id("CLK_PREV"), strs.id("CLK"));

    let vcc = tt.create_wire(strs.id("VCC"), strs.id("VCC"));
    vcc.const_value = strs.id("VCC");
    let gnd = tt.create_wire(strs.id("GND"), strs.id("GND"));
    gnd.const_value = strs.id("GND");

    let gnd_bel = tt.create_bel(strs.id("GND_DRV"), strs.id("GND_DRV"), 0).index;
    tt.add_bel_pin(gnd_bel, strs.id("GND"), strs.id("GND"), PinType::OUTPUT);
    let vcc_bel = tt.create_bel(strs.id("VCC_DRV"), strs.id("VCC_DRV"), 1).index;
    tt.add_bel_pin(vcc_bel, strs.id("VCC"), strs.id("VCC"), PinType::OUTPUT);
}

fn is_corner(x: i16, y: i16) -> bool {
    ((x == 0) || (x == (X as i16 - 1))) && ((y == 0) || (y == (Y as i16 - 1)))
}

fn create_nodes(ch: &mut Chip, strs: &mut StringPool) {
    for y in 0..Y as i16 {
        for x in 0..X as i16 {
            if !is_corner(x, y) {
                // connect up neighbours
                let mut local_nodes : Vec<Vec<NodeWire>> = (0..WL).map(
                    |i| vec![NodeWire{x: x as u16, y: y as u16, wire: strs.id(&format!("SWITCH{}", i))}]).collect();
                for (d, dx, dy) in DIRS {
                    let x1 = x - dx;
                    let y1 = y - dy;
                    if x1 < 0 || x1 >= (X as i16) || y1 < 0 || y1 >= (Y as i16) || is_corner(x1, y1) {
                        continue
                    }
                    for i in 0..WL {
                        local_nodes[i as usize].push(NodeWire{x: x1 as u16, y: y1 as u16, wire: strs.id(&format!("{}{}", d, i))})
                    }
                }
                for n in local_nodes.iter() {
                    ch.add_node(n);
                }
            }
            // connect up clock ladder (not intended to be a sensible clock structure)
            if y != 1 {
                let mut clk_node = if y == 0 {
                    if x == 0 {
                        vec![NodeWire {x: 1, y: 0, wire: strs.id("GCLK_OUT")}]
                    } else {
                        vec![NodeWire {x: (x-1) as u16, y: y as u16, wire: strs.id("CLK")}]
                    }
                } else {
                    vec![NodeWire {x: x as u16, y: (y-1) as u16, wire: strs.id("CLK")}]
                };
                clk_node.push(NodeWire {x: x as u16, y: y as u16, wire: strs.id("CLK_PREV")});
                if y == 0 {
                    clk_node.push(NodeWire {x: x as u16, y: (y+1) as u16, wire: strs.id("CLK_PREV")})
                }
                ch.add_node(&clk_node);
            }
        }
    }
} 

fn set_timings(strs: &mut StringPool, tmg: &mut TimingPool) {
    let def = strs.id("DEFAULT");
    tmg.add_speed(def);
    tmg.set_pip_class(def, strs.id("SWINPUT"),
        TimingValue::single(80), // 80ps intrinsic delay
        TimingValue::single(5000), // 5pF
        TimingValue::single(1000), // 1ohm
        true
    );
    tmg.set_pip_class(def, strs.id("SWOUTPUT"),
        TimingValue::single(100), // 100ps intrinsic delay
        TimingValue::single(5000), // 5pF
        TimingValue::single(1000), // 1ohm
        true
    );
    tmg.set_pip_class(def, strs.id("SWNEIGH"),
        TimingValue::single(120), // 120ps intrinsic delay
        TimingValue::single(7000), // 7pF
        TimingValue::single(1200), // 1.2ohm
        true
    );

    // TODO: also support node/wire delays and add an example of them
    let lut = tmg.add_cell_variant(def, strs.id("LUT4"));
    for j in 0..K {
        lut.add_comb_arc(strs.id(&format!("I[{}]", j)), strs.id("F"), TimingValue::single(150 + (j as i32) * 15));
    }
    let dff = tmg.add_cell_variant(def, strs.id("DFF"));
    dff.add_setup_hold(strs.id("CLK"), strs.id("D"), ClockEdge::RISING, TimingValue::single(150), TimingValue::single(25));
    dff.add_clock_out(strs.id("CLK"), strs.id("Q"), ClockEdge::RISING, TimingValue::single(200));
}

fn main () -> Result<()> {
    let args: Vec<String> = env::args().collect();

    let mut strs = StringPool::new();
    let mut tmg = TimingPool::new();
    strs.read_constids(&args[1])?;

    let mut ch = Chip::new("example", "EX1", X, Y);

    create_logic_tiletype(&mut ch, &mut strs, &mut tmg);
    create_io_tiletype(&mut ch, &mut strs, &mut tmg);
    create_corner_tiletype(&mut ch, &mut strs);

    // setup tile grid
    for y in 0..Y {
        for x in 0..X {
            if x == 0 || x == X-1 { // left/right side IO
                if y == 0 || y == Y-1 { // corner
                    ch.set_tile_type(x, y, strs.id("NULL"));
                } else {
                    ch.set_tile_type(x, y, strs.id("IO"));
                }
            } else if y == 0 || y == Y-1 { // top/bottom side IO
                ch.set_tile_type(x, y, strs.id("IO"));
            } else {
                ch.set_tile_type(x, y, strs.id("LOGIC"));
            }
        }

    }

    create_nodes(&mut ch, &mut strs);
    set_timings(&mut strs, &mut tmg);

    ch.write_bba(&strs, &mut tmg, &args[2])?;

    Ok(())
}
