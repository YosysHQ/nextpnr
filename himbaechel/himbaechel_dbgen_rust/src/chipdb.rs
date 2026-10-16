use std::collections::HashMap;
use std::io::*;
use std::fs::File;

use crate::IdString;
use crate::StringPool;

use crate::bbafile::*;

use std::hash::Hasher;
use fasthash::{metro::Hasher128_2, FastHasher, HasherExt};

pub trait BBAStruct {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()>;
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()>;
}

#[derive(Copy, Clone, PartialEq, PartialOrd)]
pub enum PinType {
    INPUT = 0,
    OUTPUT = 1,
    INOUT = 2
}

pub struct BelPin {
    pub name: IdString,
    pub wire: i32,
    pub dir: PinType,
}

impl BBAStruct for BelPin {
    fn serialise_lists(&self, _: &str, _ : &mut BBAWriter) -> Result<()> {
        Ok(())
    }
    fn serialise(&self, _: &str, bba : &mut BBAWriter) -> Result<()> {
        bba.u32_val(self.name.index)?;
        bba.i32_val(self.wire)?;
        bba.u32_val(self.dir as u32)?;
        Ok(())
    }
}

pub const BEL_FLAG_GLOBAL : u32 = 0x01;
pub const BEL_FLAG_HIDDEN : u32 = 0x01;

pub struct BelData {
    pub index : usize,
    pub name : IdString,
    pub bel_type: IdString,
    pub z : u16,
    pub flags : u32,
    pub site : u32,
    pub checker_idx:  u32,
    pub pins : Vec<BelPin>,
    pub extra_data : Option<Box<dyn BBAStruct>>,
}

impl BelData {
    pub fn finalise(&mut self) {
        // sort pins for fast binary search lookups
        self.pins.sort_by_key(|p| p.name.index);
    }
}

impl BBAStruct for BelData {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.label(&format!("{}_pins", context))?;
        // pins array
        for (i, pin) in self.pins.iter().enumerate() {
            pin.serialise(&format!("{}_pin{}", context, i), bba)?;
        }
        // extra data (optional)
        if let Some(extra_data) = &self.extra_data {
            extra_data.serialise_lists(&format!("{}_extra_data", context), bba)?;
            bba.label(&format!("{}_extra_data", context))?;
            extra_data.serialise(&format!("{}_extra_data", context), bba)?;
        }
        Ok(())
    }
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.u32_val(self.name.index)?;
        bba.u32_val(self.bel_type.index)?;
        bba.u16_val(self.z)?;
        bba.u16_val(0)?;
        bba.u32_val(self.flags)?;
        bba.u32_val(self.site)?;
        bba.u32_val(self.checker_idx)?;
        bba.ref_slice(&format!("{}_pins", context), self.pins.len())?;
        if self.extra_data.is_some() {
            bba.ref_label(&format!("{}_extra_data", context))?;
        } else {
            bba.u32_val(0)?;
        }

        Ok(())
    }
}

pub struct BelPinRef {
    pub bel : u32,
    pub pin : IdString,
}

impl BBAStruct for BelPinRef {
    fn serialise_lists(&self, _: &str, _ : &mut BBAWriter) -> Result<()> {
        Ok(())
    }
    fn serialise(&self, _: &str, bba : &mut BBAWriter) -> Result<()> {
        bba.u32_val(self.bel)?;
        bba.u32_val(self.pin.index)?;
        Ok(())
    }
}

pub struct TileWireData {
    pub index: usize,
    pub name: IdString,
    pub wire_type: IdString,
    pub gfx_wire_id: u32,
    pub const_value: IdString,
    pub flags: u32,
    pub timing_idx: i32,

    pub pips_uphill: Vec<u32>,
    pub pips_downhill: Vec<u32>,
    pub bel_pins: Vec<BelPinRef>,
}

impl BBAStruct for TileWireData {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        // uphill pips
        bba.label(&format!("{}_pips_uh", context))?;
        for pip_idx in self.pips_uphill.iter() {
            bba.u32_val(*pip_idx)?;
        }
        // downhill pips
        bba.label(&format!("{}_pips_dh", context))?;
        for pip_idx in self.pips_downhill.iter() {
            bba.u32_val(*pip_idx)?;
        }
        // bel pins array
        bba.label(&format!("{}_bel_pins", context))?;
        for (i, pin) in self.bel_pins.iter().enumerate() {
            pin.serialise(&format!("{}_bp{}", context, i), bba)?;
        }
        Ok(())
    }
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.u32_val(self.name.index)?;
        bba.u32_val(self.wire_type.index)?;
        bba.u32_val(self.gfx_wire_id)?;
        bba.u32_val(self.const_value.index)?;
        bba.u32_val(self.flags)?;
        bba.i32_val(self.timing_idx)?;
        bba.ref_slice(&format!("{}_pips_uh", context), self.pips_uphill.len())?;
        bba.ref_slice(&format!("{}_pips_dh", context), self.pips_downhill.len())?;
        bba.ref_slice(&format!("{}_bel_pins", context), self.bel_pins.len())?;
        Ok(())
    }
}

pub struct PipData {
    pub index: usize,
    pub src_wire: u32,
    pub dst_wire: u32,
    pub pip_type: IdString,
    pub flags: u32,
    pub timing_idx: i32,
    pub extra_data: Option<Box<dyn BBAStruct>>,
}

impl BBAStruct for PipData {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        // extra data (optional)
        if let Some(extra_data) = &self.extra_data {
            extra_data.serialise_lists(&format!("{}_extra_data", context), bba)?;
            bba.label(&format!("{}_extra_data", context))?;
            extra_data.serialise(&format!("{}_extra_data", context), bba)?;
        }
        Ok(())
    }
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.u32_val(self.src_wire)?;
        bba.u32_val(self.dst_wire)?;
        bba.u32_val(self.pip_type.index)?;
        bba.u32_val(self.flags)?;
        bba.i32_val(self.timing_idx)?;

        if self.extra_data.is_some() {
            bba.ref_label(&format!("{}_extra_data", context))?;
        } else {
            bba.u32_val(0)?;
        }

        Ok(())
    }
}

// TODO: groups

pub struct TileType {
    pub type_name: IdString,
    pub bels: Vec<BelData>,
    pub pips: Vec<PipData>,
    pub wires: Vec<TileWireData>,

    pub wire2idx: HashMap<IdString, u32>,

    pub extra_data: Option<Box<dyn BBAStruct>>,

}

impl TileType {
    pub fn new(name: IdString) -> TileType {
        TileType {
            type_name: name,
            bels: Vec::new(),
            pips: Vec::new(),
            wires: Vec::new(),

            wire2idx: HashMap::new(),
            extra_data: None,
        }
    }

    pub fn create_bel<'a>(&'a mut self, name: IdString, bel_type: IdString, z: u16) -> &'a mut BelData {
        let index = self.bels.len();

        self.bels.push(BelData {
            index: index,
            name: name,
            bel_type: bel_type,
            z: z,
            flags: 0,
            site: 0,
            checker_idx: 0,
            pins: Vec::new(),
            extra_data: None
        });

        &mut self.bels[index]
    }

    pub fn add_bel_pin(&mut self, bel_index: usize, pin: IdString, wire: IdString, dir: PinType) {
        let wire_index = *self.wire2idx.get(&wire).unwrap();
        self.bels[bel_index].pins.push(BelPin {
            name: pin,
            wire: wire_index as i32,
            dir: dir
        });
        self.wires[wire_index as usize].bel_pins.push(BelPinRef {
            bel: bel_index as u32,
            pin: pin
        });
    }

    pub fn create_wire<'a>(&'a mut self, name: IdString, wire_type: IdString) -> &'a mut TileWireData {
        let index = self.wires.len();

        self.wires.push(TileWireData {
            index: index,
            name: name,
            wire_type: wire_type,
            gfx_wire_id: 0, // TODO
            const_value: IdString::null(),
            flags: 0,
            timing_idx: -1,

            pips_uphill: Vec::new(),
            pips_downhill: Vec::new(),
            bel_pins: Vec::new(),
        });
        self.wire2idx.insert(name, index as u32);

        &mut self.wires[index]
    }

    pub fn has_wire(&self, name: IdString) -> bool {
        self.wire2idx.contains_key(&name)
    }

    pub fn create_pip<'a>(&'a mut self, src: IdString, dst: IdString) -> &'a mut PipData {
        let index = self.pips.len();
        let src_idx = *self.wire2idx.get(&src).unwrap();
        let dst_idx = *self.wire2idx.get(&dst).unwrap();

        self.pips.push(PipData {
            index: index,
            src_wire: src_idx,
            dst_wire: dst_idx,
            pip_type: IdString::null(),
            flags: 0,
            timing_idx: -1, // todo
            extra_data: None,
        });

        self.wires[src_idx as usize].pips_downhill.push(index as u32);
        self.wires[dst_idx as usize].pips_uphill.push(index as u32);

        &mut self.pips[index]
    }

    pub fn finalise(&mut self) {
        for bel in self.bels.iter_mut() {
            bel.finalise();
        }
    }
}

impl BBAStruct for TileType {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        // list children of members
        for (i, bel) in self.bels.iter().enumerate() {
            bel.serialise_lists(&format!("{}_bel{}", context, i), bba)?;
        }
        for (i, wire) in self.wires.iter().enumerate() {
            wire.serialise_lists(&format!("{}_wire{}", context, i), bba)?;
        }
        for (i, pip) in self.pips.iter().enumerate() {
            pip.serialise_lists(&format!("{}_pip{}", context, i), bba)?;
        }
        // lists of members
        bba.label(&format!("{}_bels", context))?;
        for (i, bel) in self.bels.iter().enumerate() {
            bel.serialise(&format!("{}_bel{}", context, i), bba)?;
        }
        bba.label(&format!("{}_wires", context))?;
        for (i, wire) in self.wires.iter().enumerate() {
            wire.serialise(&format!("{}_wire{}", context, i), bba)?;
        }
        bba.label(&format!("{}_pips", context))?;
        for (i, pip) in self.pips.iter().enumerate() {
            pip.serialise(&format!("{}_pip{}", context, i), bba)?;
        }
        // extra data (optional)
        if let Some(extra_data) = &self.extra_data {
            extra_data.serialise_lists(&format!("{}_extra_data", context), bba)?;
            bba.label(&format!("{}_extra_data", context))?;
            extra_data.serialise(&format!("{}_extra_data", context), bba)?;
        }
        Ok(())
    }
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.u32_val(self.type_name.index)?;
        bba.ref_slice(&format!("{}_bels", context), self.bels.len())?;
        bba.ref_slice(&format!("{}_wires", context), self.wires.len())?;
        bba.ref_slice(&format!("{}_pips", context), self.pips.len())?;
        bba.null_slice()?; // no groups
        if self.extra_data.is_some() {
            bba.ref_label(&format!("{}_extra_data", context))?;
        } else {
            bba.u32_val(0)?;
        }
        Ok(())
    }
}

// Pre deduplication (nodes flattened, absolute coords)
pub struct NodeWire {
    pub x: u16,
    pub y: u16,
    pub wire: IdString, 
}

#[derive(Clone)]
pub struct NodeShapeWire {
    pub dx: i16,
    pub dy: i16,
    pub wire: u16, 
}


#[derive(Clone)]
pub struct NodeShape {
    pub wires: Vec<NodeShapeWire>,
    pub timing_idx: i32,
}

impl NodeShape {
    pub fn new() -> NodeShape {
        NodeShape {
            wires: Vec::new(),
            timing_idx: -1,
        }
    }
    pub fn key(&self) -> u128 {
        let mut h = Hasher128_2::new();
        for w in self.wires.iter() {
            h.write_i16(w.dx);
            h.write_i16(w.dy);
            h.write_u16(w.wire);

        }
        h.write_i32(self.timing_idx);
        h.finish_ext()
    }
}

impl BBAStruct for NodeShape {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.label(&format!("{}_wires", context))?;
        for w in self.wires.iter() {
            bba.i16_val(w.dx)?;
            bba.i16_val(w.dy)?;
            bba.u16_val(w.wire)?;
        }
        if self.wires.len() % 2 != 0 { // word alignment
            bba.u16_val(0)?;
        }
        Ok(())
    }
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.ref_slice(&format!("{}_wires", context), self.wires.len())?;
        bba.i32_val(self.timing_idx)?;
        Ok(())
    }
}

pub const MODE_TILE_WIRE : u16 = 0x7000;
pub const  MODE_IS_ROOT : u16 = 0x7001;
pub const  MODE_ROW_CONST : u16 = 0x7002;
pub const  MODE_GLB_CONST: u16 = 0x7003;

#[derive(Clone)]
pub struct RelNodeRef {
    pub dx_mode: u16,
    pub dy: u16,
    pub wire: u16,
}

impl BBAStruct for RelNodeRef {
    fn serialise_lists(&self, _: &str, _: &mut BBAWriter) -> Result<()> {
        Ok(())
    }
    fn serialise(&self, _: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.u16_val(self.dx_mode)?;
        bba.u16_val(self.dy)?;
        bba.u16_val(self.wire)?;
        Ok(())
    }
}

#[derive(Clone)]
pub struct TileRoutingShape {
    pub wire_to_node: Vec<RelNodeRef>
}

impl TileRoutingShape {
    pub fn new() -> TileRoutingShape {
        TileRoutingShape { wire_to_node: Vec::new() }
    }
    pub fn key(&self) -> u128 {
        let mut h = Hasher128_2::new();
        for w in self.wire_to_node.iter() {
            h.write_u16(w.dx_mode);
            h.write_u16(w.dy);
            h.write_u16(w.wire);

        }
        h.finish_ext()
    }
}


impl BBAStruct for TileRoutingShape {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.label(&format!("{}_w2n", context))?;
        for w in self.wire_to_node.iter() {
            w.serialise("", bba)?;
        }
        if self.wire_to_node.len() % 2 != 0 { // word alignment
            bba.u16_val(0)?;
        }
        Ok(())
    }
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.ref_slice(&format!("{}_w2n", context), self.wire_to_node.len())?;
        bba.i32_val(-1)?; // unused timing index
        Ok(())
    }
}

pub struct TileInst {
    pub x: u16,
    pub y: u16,
    pub type_idx: Option<u32>,
    pub name_prefix: IdString,
    pub loc_type: u32,
    pub shape: TileRoutingShape,
    pub shape_idx: u32,
    pub extra_data: Option<Box<dyn BBAStruct>>,
}

impl TileInst {
    pub fn new(x: u16, y: u16) -> TileInst {
        TileInst {
            x: x,
            y: y,
            type_idx: None,
            name_prefix: IdString::null(),
            loc_type: 0,
            shape: TileRoutingShape::new(),
            shape_idx: 0,
            extra_data: None
        }
    }
}

impl BBAStruct for TileInst {
    fn serialise_lists(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        // extra data (optional)
        if let Some(extra_data) = &self.extra_data {
            extra_data.serialise_lists(&format!("{}_extra_data", context), bba)?;
            bba.label(&format!("{}_extra_data", context))?;
            extra_data.serialise(&format!("{}_extra_data", context), bba)?;
        }
        Ok(())
    }
    fn serialise(&self, context: &str, bba: &mut BBAWriter) -> Result<()> {
        bba.u32_val(self.name_prefix.index)?;
        bba.u32_val(self.type_idx.unwrap())?;
        bba.u32_val(self.shape_idx)?;

        if self.extra_data.is_some() {
            bba.ref_label(&format!("{}_extra_data", context))?;
        } else {
            bba.u32_val(0)?;
        }
        Ok(())
    }
}

pub struct Chip {
    pub uarch: String,
    pub name: String,
    pub width: u16,
    pub height: u16,
    pub tile_types: Vec<TileType>,
    pub tiles: Vec<Vec<TileInst>>,
    pub tile_type_idx: HashMap<IdString, u32>,
    node_shapes: Vec<NodeShape>,
    node_shape_idx: HashMap<u128, u32>,
    tile_shapes: Vec<TileRoutingShape>,
    tile_shape_idx: HashMap<u128, u32>,
    // TODO: packages
    extra_data: Option<Box<dyn BBAStruct>>,
    // TODO: timing
    // TODO: gfx_wire_ids
}

impl Chip {
    pub fn new(uarch: &str, name: &str, width: u16, height: u16) -> Chip {
        Chip {
            uarch: uarch.to_string(),
            name: name.to_string(),
            width: width,
            height: height,
            tile_types: Vec::new(),
            tiles: (0..height).map(|y| (0..width).map(|x| TileInst::new(x, y)).collect()).collect(),
            tile_type_idx: HashMap::new(),
            node_shapes: Vec::new(),
            node_shape_idx: HashMap::new(),
            tile_shapes: Vec::new(),
            tile_shape_idx: HashMap::new(),
            extra_data: None
        }
    }
    pub fn create_tile_type<'a>(&'a mut self, name: IdString) -> &'a mut TileType {
        let index = self.tile_types.len();
        self.tile_types.push(TileType::new(name));
        self.tile_type_idx.insert(name, index as u32);
        &mut self.tile_types[index]
    }
    pub fn set_tile_type(&mut self, x: u16, y: u16, tile_type: IdString) {
        self.tiles[y as usize][x as usize].type_idx = Some(*self.tile_type_idx.get(&tile_type).unwrap());
    }
    pub fn tile_type_at<'a>(&'a self, x: u16, y: u16) -> &'a TileType {
        &self.tile_types[self.tiles[y as usize][x as usize].type_idx.unwrap() as usize]
    }
    pub fn tile_type_at_mut<'a>(&'a mut self, x: u16, y: u16) -> &'a mut TileType {
        &mut self.tile_types[self.tiles[y as usize][x as usize].type_idx.unwrap() as usize]
    }
    pub fn add_node(&mut self, wires: &[NodeWire]) {
        // add a node - joining between multiple tile wires into a single connection (from nextpnr's point of view)
        // all the tile wires must exist, and the tile types must be set, first

        let x0 = wires[0].x as i16;
        let y0 = wires[0].y as i16;
        let mut shape = NodeShape::new();
        for w in wires.iter() {
            let wire_index :u16 = (*self.tile_type_at(w.x, w.y).wire2idx.get(&w.wire).unwrap()).try_into().unwrap();
            shape.wires.push(NodeShapeWire {dx: (w.x as i16)-x0, dy: (w.y as i16)-y0, wire: wire_index})
        }
        // deduplicate node shapes
        let key = shape.key();
        let shape_idx = if let Some(found_idx) = self.node_shape_idx.get(&key) {
            *found_idx
        } else {
            let new_idx = self.node_shapes.len().try_into().unwrap();
            self.node_shapes.push(shape.clone());
            self.node_shape_idx.insert(key, new_idx);
            new_idx
        };
        // update tile wire to node ref
        for (i, w) in wires.iter().enumerate() {
            let tile_inst = &mut self.tiles[w.y as usize][w.x as usize];
            let wire_idx = shape.wires[i].wire;
            // allocate space in the mapping as needed
            while wire_idx as usize >= tile_inst.shape.wire_to_node.len() {
                tile_inst.shape.wire_to_node.push(RelNodeRef { dx_mode: MODE_TILE_WIRE, dy: 0, wire: 0});
            }

            let w2n_entry = &mut tile_inst.shape.wire_to_node[wire_idx as usize];

            // check we never try and add the same wire to multiple nodes
            assert!(w2n_entry.dx_mode == MODE_TILE_WIRE);

            if i == 0 {
                // root of the node. reference to the node shape index
                w2n_entry.dx_mode = MODE_IS_ROOT;
                w2n_entry.dy = (shape_idx & 0xFFFF) as u16;
                w2n_entry.wire = ((shape_idx >> 16) & 0xFFFF) as u16;
            } else {
                // tile wire reference relative to node root
                let dx = x0 - w.x as i16;
                let dy = y0 - w.y as i16;
                assert!(dx < (MODE_TILE_WIRE as i16));
                w2n_entry.dx_mode = dx as u16;
                w2n_entry.dy = dy as u16;
                w2n_entry.wire = shape.wires[0].wire;
            }
        }
    }
    pub fn flatten_tile_shapes(&mut self) {
        eprintln!("Deduplicating tile shapes...");
        for row in self.tiles.iter_mut() {
            for tile in row.iter_mut() {
                let key = tile.shape.key();
                if let Some(shape_idx) = self.tile_shape_idx.get(&key) {
                    tile.shape_idx = *shape_idx;
                } else {
                    tile.shape_idx = self.tile_shapes.len().try_into().unwrap();
                    self.tile_shapes.push(tile.shape.clone());
                    self.tile_shape_idx.insert(key, tile.shape_idx);
                }
            }
        }
        eprintln!("{} unique tile routing shapes", self.tile_shapes.len());
    }
    pub fn finalise(&mut self) {
        self.flatten_tile_shapes();
        for tile_type in self.tile_types.iter_mut() {
            tile_type.finalise();
        }
    }
    pub fn serialise(&mut self, strs: &StringPool, bba: &mut BBAWriter) -> Result<()> {
        self.finalise();
        for (i, tt) in self.tile_types.iter().enumerate() {
            tt.serialise_lists(&format!("tt{}", i), bba)?;
        }
        for (i, shp) in self.node_shapes.iter().enumerate() {
            shp.serialise_lists(&format!("nshp{}", i), bba)?;
        }
        // TODO: packages
        for (y, row) in self.tiles.iter().enumerate() {
            for (x, tinst) in row.iter().enumerate() {
                tinst.serialise_lists(&format!("tinst_{}_{}", x, y), bba)?;
            }
        }
        strs.serialise_lists("constids", bba)?;

        if let Some(extra_data) = &self.extra_data {
            extra_data.serialise_lists("extra_data", bba)?;
            bba.label("extra_data")?;
            extra_data.serialise("extra_data", bba)?;
        }

        bba.label("tile_types")?;
        for (i, tt) in self.tile_types.iter().enumerate() {
            tt.serialise(&format!("tt{}", i), bba)?;
        }
        bba.label("node_shapes")?;
        for (i, shp) in self.node_shapes.iter().enumerate() {
            shp.serialise(&format!("nshp{}", i), bba)?;
        }
        bba.label("tile_shapes")?;
        for (i, tsh) in self.tile_shapes.iter().enumerate() {
            tsh.serialise(&format!("tsh{}", i), bba)?;
        }
        // TODO: packages
        bba.label("tile_insts")?;
        for (y, row) in self.tiles.iter().enumerate() {
            for (x, tinst) in row.iter().enumerate() {
                tinst.serialise(&format!("tinst_{}_{}", x, y), bba)?;
            }
        }
        // TODO: speed grades
        bba.label("constids")?;
        strs.serialise("constids", bba)?;

        bba.label("chip_info")?;
        bba.u32_val(0x00ca7ca7)?; // magic
        bba.u32_val(6)?; // chipdb version
        bba.u32_val(self.width as u32)?;
        bba.u32_val(self.height as u32)?;

        bba.str_val(&self.uarch)?;
        bba.str_val(&self.name)?;
        bba.str_val("rust_dbgen")?; // generator

        bba.ref_slice("tile_types", self.tile_types.len())?;
        bba.ref_slice("tile_insts", self.width as usize * self.height as usize)?;
        bba.ref_slice("node_shapes", self.node_shapes.len())?;
        bba.ref_slice("tile_shapes", self.tile_shapes.len())?;
        bba.null_slice()?; // TODO: packages
        bba.null_slice()?; // TODO: speed grades
        bba.ref_label("constids")?;

        if self.extra_data.is_some() {
            bba.ref_label("extra_data")?;
        } else {
            bba.u32_val(0)?;
        }

        Ok(())
    }

    pub fn write_bba(&mut self, strs: &StringPool, filename: &str) -> Result<()> {
        let mut bbafile = File::create(filename)?;
        let mut bba = BBAWriter::new(&mut bbafile);
        bba.pre("#include \"nextpnr.h\"")?;
        bba.pre("NEXTPNR_NAMESPACE_BEGIN")?;
        bba.pre("NEXTPNR_NAMESPACE_BEGIN")?;
        bba.pre("NEXTPNR_NAMESPACE_END")?;
        bba.push("chipdb_blob")?;
        bba.ref_label("chip_info")?;

        self.serialise(strs, &mut bba)?;

        bba.pop()?;
        Ok(())
    }
}
