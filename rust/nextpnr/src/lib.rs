use std::{ffi::CStr, marker::PhantomData, sync::Mutex};

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
    #[must_use]
    pub fn location(&self) -> Loc {
        unsafe { npnr_cellinfo_get_location(self) }
    }
}

#[repr(C)]
pub struct NetInfo {
    private: [u8; 0],
}

impl NetInfo {
    pub fn driver(&mut self) -> Option<&mut PortRef> {
        unsafe { npnr_netinfo_driver(self) }
    }

    #[must_use]
    pub fn is_global(&self) -> bool {
        unsafe { npnr_netinfo_is_global(self) }
    }

    #[must_use]
    pub fn index(&self) -> NetIndex {
        unsafe { npnr_netinfo_udata(self) }
    }
}

#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct NetIndex(i32);

impl NetIndex {
    #[must_use]
    pub fn into_inner(self) -> i32 {
        self.0
    }
}

#[repr(C)]
pub struct PortRef {
    private: [u8; 0],
}

impl PortRef {
    #[must_use]
    pub fn cell(&self) -> Option<&CellInfo> {
        // SAFETY: handing out &s is safe when we have &self.
        unsafe { npnr_portref_cell(self) }
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct IdString(libc::c_int);

/// A type representing a bel name.
#[derive(Clone, Copy, PartialEq, Eq)]
#[repr(transparent)]
pub struct BelId(u64);

impl Default for BelId {
    fn default() -> Self {
        Self::null()
    }
}

impl BelId {
    /// Return a sentinel value that represents an invalid bel.
    #[must_use]
    pub fn null() -> Self {
        npnr_belid_null()
    }

    /// Check if this bel is invalid.
    #[must_use]
    pub fn is_null(self) -> bool {
        self == Self::null()
    }
}

#[derive(Clone, Copy, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct PipId(u64);

impl Default for PipId {
    fn default() -> Self {
        Self::null()
    }
}

impl PipId {
    #[must_use]
    pub fn null() -> Self {
        npnr_pipid_null()
    }
}

#[derive(Clone, Copy, PartialOrd, Ord, PartialEq, Eq, Hash)]
#[repr(transparent)]
pub struct WireId(u64);

impl Default for WireId {
    fn default() -> Self {
        Self::null()
    }
}

impl WireId {
    /// Return a sentinel value that represents an invalid wire.
    #[must_use]
    pub fn null() -> Self {
        // SAFETY: WireId() has no safety requirements.
        npnr_wireid_null()
    }

    /// Check if this wire is invalid.
    #[must_use]
    pub fn is_null(self) -> bool {
        self == Self::null()
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
pub struct Loc {
    pub x: libc::c_int,
    pub y: libc::c_int,
    pub z: libc::c_int,
}

impl From<(i32, i32)> for Loc {
    fn from(pos: (i32, i32)) -> Self {
        Self {
            x: pos.0,
            y: pos.1,
            z: 0,
        }
    }
}

static RINGBUFFER_MUTEX: Mutex<()> = Mutex::new(());
static ARCH_MUTEX: Mutex<()> = Mutex::new(());

#[repr(C)]
pub struct Context {
    _private: [u8; 0],
}

impl Context {
    /// Get grid X dimension. All bels and pips must have X coordinates in the range `0 .. getGridDimX()-1` (inclusive).
    #[must_use]
    pub fn grid_dim_x(&self) -> i32 {
        unsafe { npnr_context_get_grid_dim_x(self) }
    }

    /// Get grid Y dimension. All bels and pips must have Y coordinates in the range `0 .. getGridDimY()-1` (inclusive).
    #[must_use]
    pub fn grid_dim_y(&self) -> i32 {
        unsafe { npnr_context_get_grid_dim_y(self) }
    }

    /// Bind a given bel to a given cell with the given strength.
    pub fn bind_bel(&mut self, bel: BelId, cell: &mut CellInfo, strength: PlaceStrength) {
        let _lock = ARCH_MUTEX.lock().unwrap();
        unsafe { npnr_context_bind_bel(self, bel, cell, strength) }
    }

    /// Unbind a bel.
    pub fn unbind_bel(&mut self, bel: BelId) {
        let _lock = ARCH_MUTEX.lock().unwrap();
        unsafe { npnr_context_unbind_bel(self, bel) }
    }

    /// Returns true if the bel is available. A bel can be unavailable because it is bound, or because it is exclusive to some other resource that is bound.
    #[must_use]
    pub fn check_bel_avail(&self, bel: BelId) -> bool {
        unsafe { npnr_context_check_bel_avail(self, bel) }
    }

    /// Bind a wire to a net. This method must be used when binding a wire that is driven by a bel pin. Use `bindPip` when binding a wire that is driven by a pip.
    pub fn bind_wire(&mut self, wire: WireId, net: &mut NetInfo, strength: PlaceStrength) {
        let _lock = ARCH_MUTEX.lock().unwrap();
        unsafe { npnr_context_bind_wire(self, wire, net, strength) }
    }

    /// Unbind a wire. For wires that are driven by a pip, this will also unbind the driving pip.
    pub fn unbind_wire(&mut self, wire: WireId) {
        let _lock = ARCH_MUTEX.lock().unwrap();
        unsafe { npnr_context_unbind_wire(self, wire) }
    }

    /// Bind a pip to a net. This also binds the destination wire of that pip.
    pub fn bind_pip(&mut self, pip: PipId, net: &mut NetInfo, strength: PlaceStrength) {
        let _lock = ARCH_MUTEX.lock().unwrap();
        unsafe { npnr_context_bind_pip(self, pip, net, strength) }
    }

    /// Unbind a pip and the wire driven by that pip.
    pub fn unbind_pip(&mut self, pip: PipId) {
        let _lock = ARCH_MUTEX.lock().unwrap();
        unsafe { npnr_context_unbind_pip(self, pip) }
    }

    /// Get the source wire for a pip.
    #[must_use]
    pub fn pip_src_wire(&self, pip: PipId) -> WireId {
        unsafe { npnr_context_get_pip_src_wire(self, pip) }
    }

    /// Get the destination wire for a pip.
    #[must_use]
    pub fn pip_dst_wire(&self, pip: PipId) -> WireId {
        unsafe { npnr_context_get_pip_dst_wire(self, pip) }
    }

    // TODO: Should this be a Duration? Does that even make sense?
    #[must_use]
    pub fn estimate_delay(&self, src: WireId, dst: WireId) -> f32 {
        unsafe { npnr_context_estimate_delay(self, src, dst) }
    }

    #[must_use]
    pub fn pip_delay(&self, pip: PipId) -> f32 {
        unsafe { npnr_context_get_pip_delay(self, pip) }
    }

    #[must_use]
    pub fn wire_delay(&self, wire: WireId) -> f32 {
        unsafe { npnr_context_get_wire_delay(self, wire) }
    }

    #[must_use]
    pub fn delay_epsilon(&self) -> f32 {
        unsafe { npnr_context_delay_epsilon(self) }
    }

    #[must_use]
    pub fn source_wire(&self, net: &NetInfo) -> WireId {
        unsafe { npnr_context_get_netinfo_source_wire(self, net) }
    }

    #[must_use]
    pub fn sink_wires(&self, net: &NetInfo, sink: &PortRef) -> Vec<WireId> {
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

    #[must_use]
    pub fn bels(&self) -> BelIter<'_> {
        let iter = unsafe { npnr_context_get_bels(self) };
        BelIter { iter, phantom_data: PhantomData }
    }

    #[must_use]
    pub fn pips(&self) -> PipIter<'_> {
        let iter = unsafe { npnr_context_get_pips(self) };
        PipIter { iter, phantom_data: PhantomData }
    }

    #[must_use]
    pub fn wires(&self) -> WireIter<'_> {
        let iter = unsafe { npnr_context_get_wires(self) };
        WireIter { iter, phantom_data: PhantomData }
    }

    #[must_use]
    pub fn get_downhill_pips(&self, wire: WireId) -> DownhillPipsIter<'_> {
        let iter = unsafe { npnr_context_get_pips_downhill(self, wire) };
        DownhillPipsIter {
            iter,
            phantom_data: PhantomData,
        }
    }

    #[must_use]
    pub fn get_uphill_pips(&self, wire: WireId) -> UphillPipsIter<'_> {
        let iter = unsafe { npnr_context_get_pips_uphill(self, wire) };
        UphillPipsIter {
            iter,
            phantom_data: PhantomData,
        }
    }

    #[must_use]
    pub fn pip_location(&self, pip: PipId) -> Loc {
        unsafe { npnr_context_get_pip_location(self, pip) }
    }

    #[must_use]
    pub fn pip_direction(&self, pip: PipId) -> Loc {
        let mut src = Loc{x: 0, y: 0, z: 0};
        let mut dst = Loc{x: 0, y: 0, z: 0};

        let mut pips = 0;
        for pip in self.get_uphill_pips(self.pip_src_wire(pip)) {
            let loc = self.pip_location(pip);
            src.x += loc.x;
            src.y += loc.y;
            pips += 1;
        }
        if pips != 0 {
            src.x /= pips;
            src.y /= pips;
        }

        let mut pips = 0;
        for pip in self.get_downhill_pips(self.pip_dst_wire(pip)) {
            let loc = self.pip_location(pip);
            dst.x += loc.x;
            dst.y += loc.y;
            pips += 1;
        }
        if pips != 0 {
            dst.x /= pips;
            dst.y /= pips;
        }

        Loc{x: dst.x - src.x, y: dst.y - src.y, z: 0}
    }

    #[must_use]
    pub fn pip_avail_for_net(&self, pip: PipId, net: &mut NetInfo) -> bool {
        unsafe { npnr_context_check_pip_avail_for_net(self, pip, net) }
    }

    pub fn check(&self) {
        unsafe { npnr_context_check(self) }
    }

    #[must_use]
    pub fn debug(&self) -> bool {
        unsafe { npnr_context_debug(self) }
    }

    #[must_use]
    pub fn id(&self, s: &str) -> IdString {
        let s = std::ffi::CString::new(s).unwrap();
        unsafe { npnr_context_id(self, s.as_ptr()) }
    }

    #[must_use]
    pub fn name_of(&self, s: IdString) -> &CStr {
        let _lock = RINGBUFFER_MUTEX.lock().unwrap();
        unsafe { CStr::from_ptr(npnr_context_name_of(self, s)) }
    }

    #[must_use]
    pub fn name_of_pip(&self, pip: PipId) -> &CStr {
        let _lock = RINGBUFFER_MUTEX.lock().unwrap();
        unsafe { CStr::from_ptr(npnr_context_name_of_pip(self, pip)) }
    }

    #[must_use]
    pub fn name_of_wire(&self, wire: WireId) -> &CStr {
        let _lock = RINGBUFFER_MUTEX.lock().unwrap();
        unsafe { CStr::from_ptr(npnr_context_name_of_wire(self, wire)) }
    }

    #[must_use]
    pub fn verbose(&self) -> bool {
        unsafe { npnr_context_verbose(self) }
    }

    #[must_use]
    pub fn nets(&self) -> Nets<'_> {
        Nets { ctx: self }
    }
}

unsafe extern "C" {
    pub fn npnr_log_info(format: *const c_char);
    pub fn npnr_log_error(format: *const c_char);

    safe fn npnr_belid_null() -> BelId;
    safe fn npnr_wireid_null() -> WireId;
    safe fn npnr_pipid_null() -> PipId;

    fn npnr_context_get_grid_dim_x(ctx: &Context) -> libc::c_int;
    fn npnr_context_get_grid_dim_y(ctx: &Context) -> libc::c_int;
    fn npnr_context_bind_bel(
        ctx: &mut Context,
        bel: BelId,
        cell: &mut CellInfo,
        strength: PlaceStrength,
    );
    fn npnr_context_unbind_bel(ctx: &mut Context, bel: BelId);
    fn npnr_context_check_bel_avail(ctx: &Context, bel: BelId) -> bool;
    fn npnr_context_bind_wire(
        ctx: &mut Context,
        wire: WireId,
        net: &mut NetInfo,
        strength: PlaceStrength,
    );
    fn npnr_context_unbind_wire(ctx: &mut Context, wire: WireId);
    fn npnr_context_bind_pip(
        ctx: &mut Context,
        pip: PipId,
        net: &mut NetInfo,
        strength: PlaceStrength,
    );
    fn npnr_context_unbind_pip(ctx: &mut Context, pip: PipId);
    fn npnr_context_get_pip_src_wire(ctx: &Context, pip: PipId) -> WireId;
    fn npnr_context_get_pip_dst_wire(ctx: &Context, pip: PipId) -> WireId;
    fn npnr_context_estimate_delay(ctx: &Context, src: WireId, dst: WireId) -> f32;
    fn npnr_context_delay_epsilon(ctx: &Context) -> f32;
    fn npnr_context_get_pip_delay(ctx: &Context, pip: PipId) -> f32;
    fn npnr_context_get_wire_delay(ctx: &Context, wire: WireId) -> f32;
    fn npnr_context_get_pip_location(ctx: &Context, pip: PipId) -> Loc;
    fn npnr_context_check_pip_avail_for_net(
        ctx: &Context,
        pip: PipId,
        net: &NetInfo,
    ) -> bool;

    fn npnr_context_check(ctx: &Context);
    fn npnr_context_debug(ctx: &Context) -> bool;
    fn npnr_context_id(ctx: &Context, s: *const c_char) -> IdString;
    fn npnr_context_name_of(ctx: &Context, s: IdString) -> *const libc::c_char;
    fn npnr_context_name_of_pip(ctx: &Context, pip: PipId) -> *const libc::c_char;
    fn npnr_context_name_of_wire(ctx: &Context, wire: WireId) -> *const libc::c_char;
    fn npnr_context_verbose(ctx: &Context) -> bool;

    fn npnr_context_get_netinfo_source_wire(ctx: &Context, net: &NetInfo) -> WireId;
    fn npnr_context_get_netinfo_sink_wire(
        ctx: &Context,
        net: &NetInfo,
        sink: &PortRef,
        n: u32,
    ) -> WireId;

    fn npnr_netinfo_driver(net: &mut NetInfo) -> Option<&mut PortRef>;
    fn npnr_netinfo_users_leak(net: &NetInfo, users: *mut *mut *const PortRef) -> u32;
    fn npnr_netinfo_is_global(net: &NetInfo) -> bool;
    fn npnr_netinfo_udata(net: &NetInfo) -> NetIndex;
    fn npnr_netinfo_udata_set(net: &mut NetInfo, value: NetIndex);

    fn npnr_portref_cell(port: &PortRef) -> Option<&CellInfo>;
    fn npnr_cellinfo_get_location(info: &CellInfo) -> Loc;

    fn npnr_context_get_pips_downhill(ctx: &Context, wire: WireId) -> &mut RawDownhillIter;
    fn npnr_delete_downhill_iter(iter: &mut RawDownhillIter);
    fn npnr_inc_downhill_iter(iter: &mut RawDownhillIter);
    fn npnr_deref_downhill_iter(iter: &mut RawDownhillIter) -> PipId;
    fn npnr_is_downhill_iter_done(iter: &mut RawDownhillIter) -> bool;

    fn npnr_context_get_pips_uphill(ctx: &Context, wire: WireId) -> &mut RawUphillIter;
    fn npnr_delete_uphill_iter(iter: &mut RawUphillIter);
    fn npnr_inc_uphill_iter(iter: &mut RawUphillIter);
    fn npnr_deref_uphill_iter(iter: &mut RawUphillIter) -> PipId;
    fn npnr_is_uphill_iter_done(iter: &mut RawUphillIter) -> bool;

    fn npnr_context_get_bels(ctx: &Context) -> &mut RawBelIter;
    fn npnr_delete_bel_iter(iter: &mut RawBelIter);
    fn npnr_inc_bel_iter(iter: &mut RawBelIter);
    fn npnr_deref_bel_iter(iter: &mut RawBelIter) -> BelId;
    fn npnr_is_bel_iter_done(iter: &mut RawBelIter) -> bool;

    fn npnr_context_get_pips(ctx: &Context) -> &mut RawPipIter;
    fn npnr_delete_pip_iter(iter: &mut RawPipIter);
    fn npnr_inc_pip_iter(iter: &mut RawPipIter);
    fn npnr_deref_pip_iter(iter: &mut RawPipIter) -> PipId;
    fn npnr_is_pip_iter_done(iter: &mut RawPipIter) -> bool;

    fn npnr_context_get_wires(ctx: &Context) -> &mut RawWireIter;
    fn npnr_delete_wire_iter(iter: &mut RawWireIter);
    fn npnr_inc_wire_iter(iter: &mut RawWireIter);
    fn npnr_deref_wire_iter(iter: &mut RawWireIter) -> WireId;
    fn npnr_is_wire_iter_done(iter: &mut RawWireIter) -> bool;

    fn npnr_context_net_iter(ctx: &Context) -> &mut RawNetIter;
    fn npnr_delete_net_iter(iter: &mut RawNetIter);
    fn npnr_inc_net_iter(iter: &mut RawNetIter);
    fn npnr_deref_net_iter_first(iter: &RawNetIter) -> libc::c_int;
    fn npnr_deref_net_iter_second(iter: &RawNetIter) -> &mut NetInfo;
    fn npnr_is_net_iter_done(iter: &RawNetIter) -> bool;

    fn npnr_context_cell_iter(ctx: &Context) -> &mut RawCellIter;
    fn npnr_delete_cell_iter(iter: &mut RawCellIter);
    fn npnr_inc_cell_iter(iter: &mut RawCellIter);
    fn npnr_deref_cell_iter_first(iter: &mut RawCellIter) -> libc::c_int;
    fn npnr_deref_cell_iter_second(iter: &mut RawCellIter) -> &mut CellInfo;
    fn npnr_is_cell_iter_done(iter: &mut RawCellIter) -> bool;
}

/// Store for the nets of a context.
pub struct Nets<'a> {
    ctx: &'a Context,
}

impl<'a> IntoIterator for &'a Nets<'a> {
    type Item = (IdString, *const NetInfo);

    type IntoIter = NetIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        NetIter {
            iter: unsafe { npnr_context_net_iter(self.ctx) },
            phantom_data: PhantomData,
        }
    }
}

pub struct Cells<'a> {
    ctx: &'a Context,
}

impl<'a> IntoIterator for &'a Cells<'a> {
    type Item = (IdString, *const CellInfo);

    type IntoIter = CellIter<'a>;

    fn into_iter(self) -> Self::IntoIter {
        CellIter {
            iter: unsafe { npnr_context_cell_iter(self.ctx) },
            phantom_data: PhantomData,
        }
    }
}

pub struct NetSinkWireIter<'a> {
    ctx: &'a Context,
    net: &'a NetInfo,
    sink: &'a PortRef,
    n: u32,
}

impl Iterator for NetSinkWireIter<'_> {
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

#[repr(C)]
struct RawDownhillIter {
    content: [u8; 0],
}

pub struct DownhillPipsIter<'a> {
    iter: &'a mut RawDownhillIter,
    phantom_data: PhantomData<&'a PipId>,
}

impl Iterator for DownhillPipsIter<'_> {
    type Item = PipId;

    fn next(&mut self) -> Option<Self::Item> {
        if unsafe { npnr_is_downhill_iter_done(self.iter) } {
            None
        } else {
            let pip = unsafe { npnr_deref_downhill_iter(self.iter) };
            unsafe { npnr_inc_downhill_iter(self.iter) };
            Some(pip)
        }
    }
}

impl Drop for DownhillPipsIter<'_> {
    fn drop(&mut self) {
        unsafe { npnr_delete_downhill_iter(self.iter) };
    }
}

#[repr(C)]
struct RawUphillIter {
    content: [u8; 0],
}

pub struct UphillPipsIter<'a> {
    iter: &'a mut RawUphillIter,
    phantom_data: PhantomData<&'a PipId>,
}

impl Iterator for UphillPipsIter<'_> {
    type Item = PipId;

    fn next(&mut self) -> Option<Self::Item> {
        if unsafe { npnr_is_uphill_iter_done(self.iter) } {
            None
        } else {
            let pip = unsafe { npnr_deref_uphill_iter(self.iter) };
            unsafe { npnr_inc_uphill_iter(self.iter) };
            Some(pip)
        }
    }
}

impl Drop for UphillPipsIter<'_> {
    fn drop(&mut self) {
        unsafe { npnr_delete_uphill_iter(self.iter) };
    }
}

#[repr(C)]
struct RawBelIter {
    content: [u8; 0],
}

pub struct BelIter<'a> {
    iter: &'a mut RawBelIter,
    phantom_data: PhantomData<&'a BelId>,
}

impl Iterator for BelIter<'_> {
    type Item = BelId;

    fn next(&mut self) -> Option<Self::Item> {
        if unsafe { npnr_is_bel_iter_done(self.iter) } {
            None
        } else {
            let pip = unsafe { npnr_deref_bel_iter(self.iter) };
            unsafe { npnr_inc_bel_iter(self.iter) };
            Some(pip)
        }
    }
}

impl Drop for BelIter<'_> {
    fn drop(&mut self) {
        unsafe { npnr_delete_bel_iter(self.iter) };
    }
}

#[repr(C)]
struct RawPipIter {
    content: [u8; 0],
}

pub struct PipIter<'a> {
    iter: &'a mut RawPipIter,
    phantom_data: PhantomData<&'a PipId>,
}

impl Iterator for PipIter<'_> {
    type Item = PipId;

    fn next(&mut self) -> Option<Self::Item> {
        if unsafe { npnr_is_pip_iter_done(self.iter) } {
            None
        } else {
            let pip = unsafe { npnr_deref_pip_iter(self.iter) };
            unsafe { npnr_inc_pip_iter(self.iter) };
            Some(pip)
        }
    }
}

impl Drop for PipIter<'_> {
    fn drop(&mut self) {
        unsafe { npnr_delete_pip_iter(self.iter) };
    }
}

#[repr(C)]
struct RawWireIter {
    content: [u8; 0],
}

pub struct WireIter<'a> {
    iter: &'a mut RawWireIter,
    phantom_data: PhantomData<&'a WireId>,
}

impl Iterator for WireIter<'_> {
    type Item = WireId;

    fn next(&mut self) -> Option<Self::Item> {
        if unsafe { npnr_is_wire_iter_done(self.iter) } {
            None
        } else {
            let pip = unsafe { npnr_deref_wire_iter(self.iter) };
            unsafe { npnr_inc_wire_iter(self.iter) };
            Some(pip)
        }
    }
}

impl Drop for WireIter<'_> {
    fn drop(&mut self) {
        unsafe { npnr_delete_wire_iter(self.iter) };
    }
}

#[repr(C)]
struct RawNetIter {
    content: [u8; 0],
}

pub struct NetIter<'a> {
    iter: &'a mut RawNetIter,
    phantom_data: PhantomData<&'a NetInfo>,
}

impl Iterator for NetIter<'_> {
    type Item = (IdString, *const NetInfo);

    fn next(&mut self) -> Option<Self::Item> {
        if unsafe { npnr_is_net_iter_done(self.iter) } {
            None
        } else {
            let s = IdString(unsafe { npnr_deref_net_iter_first(self.iter) });
            let net = unsafe { &raw const *npnr_deref_net_iter_second(self.iter) };
            unsafe { npnr_inc_net_iter(self.iter) };
            Some((s, net))
        }
    }
}

impl Drop for NetIter<'_> {
    fn drop(&mut self) {
        unsafe { npnr_delete_net_iter(self.iter) };
    }
}

#[repr(C)]
struct RawCellIter {
    content: [u8; 0],
}

pub struct CellIter<'a> {
    iter: &'a mut RawCellIter,
    phantom_data: PhantomData<&'a CellInfo>,
}

impl Iterator for CellIter<'_> {
    type Item = (IdString, *const CellInfo);

    fn next(&mut self) -> Option<Self::Item> {
        if unsafe { npnr_is_cell_iter_done(self.iter) } {
            None
        } else {
            let s = IdString(unsafe { npnr_deref_cell_iter_first(self.iter) });
            let cell = unsafe { &raw const *npnr_deref_cell_iter_second(self.iter) };
            unsafe { npnr_inc_cell_iter(self.iter) };
            Some((s, cell))
        }
    }
}

impl Drop for CellIter<'_> {
    fn drop(&mut self) {
        unsafe { npnr_delete_cell_iter(self.iter) };
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

