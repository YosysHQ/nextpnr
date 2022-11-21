use std::{ffi::CStr, collections::binary_heap::Iter};

use libc::c_char;

#[derive(Clone, Copy)]
#[repr(C)]
pub enum PlaceStrength {
    None = 0,
    Weak = 1,
    Strong = 2,
    Placer = 3,
    Fixed = 4,
    Locked = 5,
    User = 6,
}

#[repr(C)]
pub struct CellInfo {
    private: [u8; 0],
}

#[repr(C)]
pub struct NetInfo {
    private: [u8; 0],
}

#[repr(transparent)]
pub struct IdString(libc::c_int);

/// A type representing a bel name.
#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct BelId {
    _private: u64,
}

impl BelId {
    pub fn null() -> Self {
        unsafe { npnr_belid_null() }
    }
}

#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct PipId {
    _private: u64,
}

impl PipId {
    pub fn null() -> Self {
        todo!()
    }
}

#[derive(Clone, Copy)]
#[repr(transparent)]
pub struct WireId {
    _private: u64,
}

impl WireId {
    pub fn null() -> Self {
        todo!()
    }
}

#[repr(C)]
pub struct Context {
    _private: [u8; 0],
}

impl Context {
    /// Get grid X dimension. All bels and pips must have X coordinates in the range `0 .. getGridDimX()-1` (inclusive).
    pub fn grid_dim_x(&self) -> i32 {
        unsafe { npnr_context_get_grid_dim_x(self) as i32 }
    }

    /// Get grid Y dimension. All bels and pips must have Y coordinates in the range `0 .. getGridDimY()-1` (inclusive).
    pub fn grid_dim_y(&self) -> i32 {
        unsafe { npnr_context_get_grid_dim_y(self) as i32 }
    }

    /// Bind a given bel to a given cell with the given strength.
    pub fn bind_bel(&mut self, bel: BelId, cell: &mut CellInfo, strength: PlaceStrength) {
        unsafe { npnr_context_bind_bel(self, bel, cell, strength) }
    }

    /// Unbind a bel.
    pub fn unbind_bel(&mut self, bel: BelId) {
        unsafe { npnr_context_unbind_bel(self, bel) }
    }

    /// Returns true if the bel is available. A bel can be unavailable because it is bound, or because it is exclusive to some other resource that is bound.
    pub fn check_bel_avail(&self, bel: BelId) -> bool {
        unsafe { npnr_context_check_bel_avail(self, bel) }
    }

    /// Return the cell the given bel is bound to, or nullptr if the bel is not bound.
    pub fn bound_bel_cell(&self, bel: BelId) -> Option<&CellInfo> {
        unsafe { npnr_context_get_bound_bel_cell(self, bel).as_ref() }
    }

    /// Bind a wire to a net. This method must be used when binding a wire that is driven by a bel pin. Use bindPip() when binding a wire that is driven by a pip.
    pub fn bind_wire(&mut self, wire: WireId, net: &mut NetInfo, strength: PlaceStrength) {
        unsafe { npnr_context_bind_wire(self, wire, net, strength) }
    }

    /// Unbind a wire. For wires that are driven by a pip, this will also unbind the driving pip.
    pub fn unbind_wire(&mut self, wire: WireId) {
        unsafe { npnr_context_unbind_wire(self, wire) }
    }

    /// Bid a pip to a net. This also bind the destination wire of that pip.
    pub fn bind_pip(&mut self, pip: PipId, net: &mut NetInfo, strength: PlaceStrength) {
        unsafe { npnr_context_bind_pip(self, pip, net, strength) }
    }

    /// Unbind a pip and the wire driven by that pip.
    pub fn unbind_pip(&mut self, pip: PipId) {
        unsafe { npnr_context_unbind_pip(self, pip) }
    }

    /// Get the source wire for a pip.
    pub fn pip_src_wire(&self, pip: PipId) -> WireId {
        unsafe { npnr_context_get_pip_src_wire(self, pip) }
    }

    /// Get the destination wire for a pip.
    pub fn pip_dst_wire(&self, pip: PipId) -> WireId {
        unsafe { npnr_context_get_pip_dst_wire(self, pip) }
    }

    // TODO: Should this be a Duration? Does that even make sense?
    pub fn estimate_delay(&self, src: WireId, dst: WireId) -> f32 {
        unsafe { npnr_context_estimate_delay(self, src, dst) as f32 }
    }

    pub fn check(&self) {
        unsafe { npnr_context_check(self) }
    }

    pub fn debug(&self) -> bool {
        unsafe { npnr_context_debug(self)}
    }

    pub fn id(&self, s: &str) -> IdString {
        let s = std::ffi::CString::new(s).unwrap();
        unsafe { npnr_context_id(self, s.as_ptr()) }
    }

    pub fn name_of(&self, s: IdString) -> &CStr {
        unsafe { CStr::from_ptr(npnr_context_name_of(self, s)) }
    }

    pub fn verbose(&self) -> bool {
        unsafe { npnr_context_verbose(self) }
    }
}

extern "C" {
    pub fn npnr_log_info(format: *const c_char);
    pub fn npnr_log_error(format: *const c_char);

    fn npnr_belid_null() -> BelId;

    fn npnr_context_get_grid_dim_x(ctx: *const Context) -> libc::c_int;
    fn npnr_context_get_grid_dim_y(ctx: *const Context) -> libc::c_int;
    fn npnr_context_bind_bel(
        ctx: *mut Context,
        bel: BelId,
        cell: *mut CellInfo,
        strength: PlaceStrength,
    );
    fn npnr_context_unbind_bel(ctx: *mut Context, bel: BelId);
    fn npnr_context_check_bel_avail(ctx: *const Context, bel: BelId) -> bool;
    fn npnr_context_get_bound_bel_cell(ctx: *const Context, bel: BelId) -> *const CellInfo;
    fn npnr_context_bind_wire(
        ctx: *mut Context,
        wire: WireId,
        net: *mut NetInfo,
        strength: PlaceStrength,
    );
    fn npnr_context_unbind_wire(ctx: *mut Context, wire: WireId);
    fn npnr_context_bind_pip(
        ctx: *mut Context,
        pip: PipId,
        net: *mut NetInfo,
        strength: PlaceStrength,
    );
    fn npnr_context_unbind_pip(ctx: *mut Context, pip: PipId);
    fn npnr_context_get_pip_src_wire(ctx: *const Context, pip: PipId) -> WireId;
    fn npnr_context_get_pip_dst_wire(ctx: *const Context, pip: PipId) -> WireId;
    fn npnr_context_estimate_delay(ctx: *const Context, src: WireId, dst: WireId) -> libc::c_float;

    fn npnr_context_check(ctx: *const Context);
    fn npnr_context_debug(ctx: *const Context) -> bool;
    fn npnr_context_id(ctx: *const Context, s: *const c_char) -> IdString;
    fn npnr_context_name_of(ctx: *const Context, s: IdString) -> *const libc::c_char;
    fn npnr_context_verbose(ctx: *const Context) -> bool;

    fn npnr_context_nets_key(ctx: *const Context, n: u32) -> IdString;
    fn npnr_context_nets_value(ctx: *const Context, n: u32) -> *mut NetInfo;
    // fn npnr_context_nets(ctx: *const Context) -> *mut *mut NetInfo;
}

/// In case you missed the C++ comment; this is O(n^2) because FFI is misert.
/// It's probably best to run it exactly once.
pub struct NetIter<'a> {
    ctx: &'a Context,
    n: u32
}

impl<'a> NetIter<'a> {
    pub fn new(ctx: &'a Context) -> Self {
        Self {
            ctx,
            n: 0
        }
    }
}

impl<'a> Iterator for NetIter<'a> {
    type Item = (IdString, *mut NetInfo);

    fn next(&mut self) -> Option<Self::Item> {
        let str = unsafe { npnr_context_nets_key(self.ctx, self.n) };
        let val = unsafe { npnr_context_nets_value(self.ctx, self.n) };
        if val.is_null() {
            return None;
        }
        self.n += 1;
        Some((str, val))
    }
}

macro_rules! log_info {
    ($($t:tt)*) => {
        let s = std::ffi::CString::new(format!($($t)*)).unwrap();
        unsafe { crate::npnr::npnr_log_info(s.as_ptr()); }
    };
}

macro_rules! log_error {
    ($($t:tt)*) => {
        let s = std::ffi::CString::new(format!($($t)*)).unwrap();
        unsafe { crate::npnr::npnr_log_error(s.as_ptr()); }
    };
}