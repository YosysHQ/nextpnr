use core::slice;
use std::{collections::HashMap, ffi::CStr, marker::PhantomData};

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

impl CellInfo {
    pub fn location(&self) -> Loc {
        unsafe { npnr_cellinfo_get_location(self) }
    }
}

#[repr(C)]
pub struct NetInfo {
    private: [u8; 0],
}

impl NetInfo {
    pub fn driver(&mut self) -> *mut PortRef {
        unsafe { npnr_netinfo_driver(self) }
    }

    pub fn is_global(&self) -> bool {
        unsafe { npnr_netinfo_is_global(self) }
    }
}

#[repr(C)]
pub struct PortRef {
    private: [u8; 0],
}

impl PortRef {
    pub fn cell(&self) -> Option<&CellInfo> {
        // SAFETY: handing out &s is safe when we have &self.
        unsafe { npnr_portref_cell(self).as_ref() }
    }

    pub fn cell_mut(&mut self) -> Option<&mut CellInfo> {
        // SAFETY: handing out &mut is safe when we have &mut self
        // as getting multiple &mut CellInfo would require multiple &mut PortRef.
        unsafe { npnr_portref_cell(self).as_mut() }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct IdString(libc::c_int);

/// A type representing a bel name.
#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct BelId(u64);

impl BelId {
    /// Return a sentinel value that represents an invalid bel.
    pub fn null() -> Self {
        // SAFETY: BelId() has no safety requirements.
        unsafe { npnr_belid_null() }
    }

    /// Check if this bel is invalid.
    pub fn is_null(self) -> bool {
        self == Self::null()
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct PipId(u64);

impl PipId {
    pub fn null() -> Self {
        unsafe { npnr_pipid_null() }
    }
}

#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct WireId(u64);

impl WireId {
    /// Return a sentinel value that represents an invalid wire.
    pub fn null() -> Self {
        // SAFETY: WireId() has no safety requirements.
        unsafe { npnr_wireid_null() }
    }

    /// Check if this wire is invalid.
    pub fn is_null(self) -> bool {
        self == Self::null()
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Loc {
    pub x: libc::c_int,
    pub y: libc::c_int,
    pub z: libc::c_int,
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
    pub fn bind_bel(&mut self, bel: BelId, cell: *mut CellInfo, strength: PlaceStrength) {
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
    pub fn bind_wire(&mut self, wire: WireId, net: *mut NetInfo, strength: PlaceStrength) {
        unsafe { npnr_context_bind_wire(self, wire, net, strength) }
    }

    /// Unbind a wire. For wires that are driven by a pip, this will also unbind the driving pip.
    pub fn unbind_wire(&mut self, wire: WireId) {
        unsafe { npnr_context_unbind_wire(self, wire) }
    }

    /// Bid a pip to a net. This also bind the destination wire of that pip.
    pub fn bind_pip(&mut self, pip: PipId, net: *mut NetInfo, strength: PlaceStrength) {
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

    pub fn source_wire(&self, net: *const NetInfo) -> WireId {
        unsafe { npnr_context_get_netinfo_source_wire(self, net) }
    }

    pub fn sink_wires(&self, net: *const NetInfo, sink: *const PortRef) -> Vec<WireId> {
        let mut v = Vec::new();
        let mut n = 0;
        loop {
            let wire = unsafe { npnr_context_get_netinfo_sink_wire(self, net, sink, n) };
            if wire.is_null() {
                break;
            }
            n += 1;
            v.push(wire);
        }
        v
    }

    pub fn wires_leaking(&self) -> &[WireId] {
        let mut wires = std::ptr::null_mut();
        let len = unsafe { npnr_context_get_wires_leak(self, &mut wires as *mut *mut WireId) };
        unsafe { std::slice::from_raw_parts(wires, len as usize) }
    }

    pub fn pips_leaking(&self) -> &[PipId] {
        let mut pips = std::ptr::null_mut();
        let len = unsafe { npnr_context_get_pips_leak(self, &mut pips as *mut *mut PipId) };
        unsafe { std::slice::from_raw_parts(pips, len as usize) }
    }

    pub fn pip_location(&self, pip: PipId) -> Loc {
        unsafe { npnr_context_get_pip_location(self, pip) }
    }

    pub fn pip_direction(&self, pip: PipId) -> Loc {
        unsafe { npnr_context_get_pip_direction(self, pip) }
    }

    pub fn check(&self) {
        unsafe { npnr_context_check(self) }
    }

    pub fn debug(&self) -> bool {
        unsafe { npnr_context_debug(self) }
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
    fn npnr_wireid_null() -> WireId;
    fn npnr_pipid_null() -> PipId;

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
    fn npnr_context_get_wires_leak(ctx: *const Context, wires: *mut *mut WireId) -> u64;
    fn npnr_context_get_pips_leak(ctx: *const Context, pips: *mut *mut PipId) -> u64;
    fn npnr_context_get_pip_location(ctx: *const Context, pip: PipId) -> Loc;
    fn npnr_context_get_pip_direction(ctx: *const Context, pip: PipId) -> Loc;

    fn npnr_context_check(ctx: *const Context);
    fn npnr_context_debug(ctx: *const Context) -> bool;
    fn npnr_context_id(ctx: *const Context, s: *const c_char) -> IdString;
    fn npnr_context_name_of(ctx: *const Context, s: IdString) -> *const libc::c_char;
    fn npnr_context_verbose(ctx: *const Context) -> bool;

    fn npnr_context_get_netinfo_source_wire(ctx: *const Context, net: *const NetInfo) -> WireId;
    fn npnr_context_get_netinfo_sink_wire(
        ctx: *const Context,
        net: *const NetInfo,
        sink: *const PortRef,
        n: u32,
    ) -> WireId;

    fn npnr_context_nets_leak(
        ctx: *const Context,
        names: *mut *mut libc::c_int,
        nets: *mut *mut *mut NetInfo,
    ) -> u32;

    fn npnr_netinfo_driver(net: *mut NetInfo) -> *mut PortRef;
    fn npnr_netinfo_users_leak(net: *mut NetInfo, users: *mut *mut *mut PortRef) -> u32;
    fn npnr_netinfo_is_global(net: *const NetInfo) -> bool;

    fn npnr_portref_cell(port: *const PortRef) -> *mut CellInfo;
    fn npnr_cellinfo_get_location(info: *const CellInfo) -> Loc;
}

/// Store for the nets of a context.
pub struct Nets<'a> {
    nets: HashMap<IdString, *mut NetInfo>,
    users: HashMap<IdString, &'a [&'a mut PortRef]>,
    _data: PhantomData<&'a Context>,
}

impl<'a> Nets<'a> {
    /// Create a new store for the nets of a context.
    ///
    /// Note that this leaks memory created by nextpnr; the intention is this is called once.
    pub fn new(ctx: &'a Context) -> Nets<'a> {
        let mut names: *mut libc::c_int = std::ptr::null_mut();
        let mut nets_ptr: *mut *mut NetInfo = std::ptr::null_mut();
        let size = unsafe {
            npnr_context_nets_leak(
                ctx,
                &mut names as *mut *mut libc::c_int,
                &mut nets_ptr as *mut *mut *mut NetInfo,
            )
        };
        let mut nets = HashMap::new();
        let mut users = HashMap::new();
        for i in 0..size {
            let name = unsafe { IdString(*names.add(i as usize)) };
            let net = unsafe { *nets_ptr.add(i as usize) };
            let mut users_ptr = std::ptr::null_mut();
            // SAFETY: net is not null because it's a &mut, and users is only written to.
            // Leaking memory is the most convenient FFI I could think of.
            let len =
                unsafe { npnr_netinfo_users_leak(net, &mut users_ptr as *mut *mut *mut PortRef) };
            let users_slice =
                unsafe { slice::from_raw_parts(users_ptr as *mut &mut PortRef, len as usize) };
            nets.insert(name, net);
            users.insert(name, users_slice);
        }
        // Note: the contents of `names` and `nets_ptr` are now lost.
        Self {
            nets,
            users,
            _data: PhantomData,
        }
    }

    /// Find net users given a net's name.
    pub fn users_by_name(&self, net: IdString) -> Option<&&[&mut PortRef]> {
        self.users.get(&net)
    }

    /// Return the number of nets in the store.
    pub fn len(&self) -> usize {
        self.nets.len()
    }

    pub fn iter(&self) -> std::collections::hash_map::Iter<'_, IdString, *mut NetInfo> {
        self.nets.iter()
    }
}

pub struct NetSinkWireIter<'a> {
    ctx: &'a Context,
    net: *const NetInfo,
    sink: *const PortRef,
    n: u32,
}

impl<'a> Iterator for NetSinkWireIter<'a> {
    type Item = WireId;

    fn next(&mut self) -> Option<Self::Item> {
        let item =
            unsafe { npnr_context_get_netinfo_sink_wire(self.ctx, self.net, self.sink, self.n) };
        if item.is_null() {
            return None;
        }
        self.n += 1;
        Some(item)
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
