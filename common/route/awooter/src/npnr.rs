use core::slice;
use std::pin::Pin;
use std::{collections::HashMap, ffi::CStr, sync::Mutex};

use cxx::CxxVector;
use libc::c_char;

mod ffi;
pub use ffi::base::*;
pub use ffi::ecp5::*;

macro_rules! log_info {
    ($($t:tt)*) => {
        let s = std::ffi::CString::new(format!($($t)*)).unwrap();
        unsafe { crate::npnr::npnr_log_info(s.as_ptr().cast()); }
    };
}

macro_rules! log_error {
    ($($t:tt)*) => {
        let s = std::ffi::CString::new(format!($($t)*)).unwrap();
        unsafe { crate::npnr::npnr_log_error(s.as_ptr().cast()); }
    };
}

impl NetInfo {
    pub fn driver(&self) -> Option<&PortRef> {
        unsafe { npnr_netinfo_driver(self).as_ref() }
    }

    pub fn is_global(&self) -> bool {
        unsafe { npnr_netinfo_is_global(self) }
    }

    pub fn index(&self) -> NetIndex {
        unsafe { npnr_netinfo_udata(self) }
    }
}

#[repr(transparent)]
#[derive(Clone, Copy, PartialEq, Eq, Hash)]
pub struct NetIndex(i32);

impl NetIndex {
    pub fn into_inner(self) -> i32 {
        self.0
    }
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

impl PipId {
    pub fn null() -> Self {
        unsafe { npnr_pipid_null() }
    }
}

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

// Almost certainly not!
unsafe impl Send for Context {}
unsafe impl Sync for Context {}

impl Context {
    /// Return the cell the given bel is bound to, or nullptr if the bel is not bound.
    pub fn bound_bel_cell(&self, bel: BelId) -> Option<&CellInfo> {
        unsafe { npnr_context_get_bound_bel_cell(self, bel).as_ref() }
    }

    // TODO: Should this be a Duration? Does that even make sense?
    pub fn estimate_delay(&self, src: WireId, dst: WireId) -> f32 {
        unsafe { npnr_context_estimate_delay(self, &src, &dst) as f32 }
    }

    pub fn pip_delay(&self, pip: PipId) -> f32 {
        unsafe { npnr_context_get_pip_delay(self, &pip) }
    }

    pub fn wire_delay(&self, wire: WireId) -> f32 {
        unsafe { npnr_context_get_wire_delay(self, &wire) }
    }

    pub fn delay_epsilon(&self) -> f32 {
        unsafe { npnr_context_delay_epsilon(self) }
    }

    pub fn sink_wires(&self, net: &NetInfo, sink: &PortRef) -> Vec<WireId> {
        let mut v = Vec::new();
        let mut n = 0;
        loop {
            let wire = unsafe { self.sink_wire(net, sink, n) };
            if wire.is_null() {
                break;
            }
            n += 1;
            v.push(wire);
        }
        v
    }

    pub fn wires_leaking(&self) -> Vec<WireId> {
        let mut wires = vec![];
        let len = unsafe { npnr_context_get_wires_leak(self, &mut wires) };
        assert_eq!(len, wires.len()); // Sanity check that Rust and C++ are on speaking terms
        wires
    }

    pub fn pips_leaking(&self) -> Vec<PipId> {
        let mut pips = vec![];
        let len = unsafe { npnr_context_get_pips_leak(self, &mut pips) };
        assert_eq!(len, pips.len()); // Sanity check that Rust and C++ are on speaking terms
        pips
    }

    pub fn get_downhill_pips(&self, wire: WireId) -> DownhillPipsIter {
        let iter = unsafe { npnr_context_get_pips_downhill(self, wire) };
        DownhillPipsIter {
            iter,
            phantom_data: Default::default(),
        }
    }

    pub fn get_uphill_pips(&self, wire: WireId) -> UphillPipsIter {
        let iter = unsafe { npnr_context_get_pips_uphill(self, wire) };
        UphillPipsIter {
            iter,
            phantom_data: Default::default(),
        }
    }

    pub fn debug(&self) -> bool {
        unsafe { npnr_context_debug(self) }
    }

    pub fn id(&self, s: &str) -> IdString {
        let s = std::ffi::CString::new(s).unwrap();
        unsafe { npnr_context_id(self, s.as_ptr()) }
    }

    pub fn name_of(&self, s: IdString) -> &str {
        unsafe { CStr::from_ptr(npnr_context_name_of(self, s)) }
            .to_str()
            .unwrap()
    }

    pub fn name_of_pip(&self, pip: PipId) -> &str {
        unsafe { CStr::from_ptr(npnr_context_name_of_pip(self, pip)) }
            .to_str()
            .unwrap()
    }

    pub fn name_of_wire(&self, wire: WireId) -> String {
        static MUTEX: Mutex<()> = Mutex::new(());
        let _lock = MUTEX.lock().unwrap();
        unsafe { CStr::from_ptr(npnr_context_name_of_wire(self, wire)) }
            .to_str()
            .unwrap()
            .to_owned()
    }

    pub fn verbose(&self) -> bool {
        unsafe { npnr_context_verbose(self) }
    }
}

extern "C-unwind" {
    pub fn npnr_log_info(format: *const c_char);
    pub fn npnr_log_error(format: *const c_char);

    fn npnr_belid_null() -> BelId;
    fn npnr_wireid_null() -> WireId;
    fn npnr_pipid_null() -> PipId;

    #[allow(improper_ctypes)] // &mut to a Vec is acceptable with cxx's help
    pub fn npnr_nets_names(nets: &NetDict, names: &mut Vec<IdString>) -> usize;
    pub fn npnr_netinfo_udata_set(net: Pin<&mut NetInfo>, value: i32);
    #[allow(improper_ctypes)] // &mut to a Vec is acceptable with cxx's help
    fn npnr_context_get_wires_leak(ctx: &Context, wires: &mut Vec<WireId>) -> usize;
    #[allow(improper_ctypes)] // &mut to a Vec is acceptable with cxx's help
    fn npnr_context_get_pips_leak(ctx: &Context, pips: &mut Vec<PipId>) -> usize;

    fn npnr_context_get_bound_bel_cell(ctx: *const Context, bel: BelId) -> *const CellInfo;
    pub fn npnr_context_nets(ctx: *mut Context) -> *mut NetDict;
    fn npnr_context_estimate_delay(ctx: *const Context, src: &WireId, dst: &WireId) -> f32;
    fn npnr_context_delay_epsilon(ctx: *const Context) -> f32;
    fn npnr_context_get_pip_delay(ctx: *const Context, pip: &PipId) -> f32;
    fn npnr_context_get_wire_delay(ctx: *const Context, wire: &WireId) -> f32;
    fn npnr_context_debug(ctx: *const Context) -> bool;
    fn npnr_context_id(ctx: *const Context, s: *const c_char) -> IdString;
    fn npnr_context_name_of(ctx: *const Context, s: IdString) -> *const libc::c_char;
    fn npnr_context_name_of_pip(ctx: *const Context, pip: PipId) -> *const libc::c_char;
    fn npnr_context_name_of_wire(ctx: *const Context, wire: WireId) -> *const libc::c_char;
    fn npnr_context_verbose(ctx: *const Context) -> bool;

    fn npnr_context_get_pips_downhill(ctx: *const Context, wire: WireId) -> *mut RawDownhillIter;
    fn npnr_delete_downhill_iter(iter: *mut RawDownhillIter);
    fn npnr_context_get_pips_uphill(ctx: *const Context, wire: WireId) -> *mut RawUphillIter;
    fn npnr_delete_uphill_iter(iter: *mut RawUphillIter);

    #[allow(improper_ctypes)] // &mut to a Vec is acceptable with cxx's help
    pub fn npnr_netinfo_users_leak(net: Pin<&mut NetInfo>, ports: &mut Vec<PortRef>) -> usize;
    fn npnr_netinfo_driver(net: *const NetInfo) -> *mut PortRef;
    fn npnr_netinfo_is_global(net: *const NetInfo) -> bool;
    fn npnr_netinfo_udata(net: *const NetInfo) -> NetIndex;

    fn npnr_portref_cell(port: *const PortRef) -> *mut CellInfo;

    fn npnr_inc_downhill_iter(iter: *mut RawDownhillIter);
    fn npnr_deref_downhill_iter(iter: *mut RawDownhillIter) -> PipId;
    fn npnr_is_downhill_iter_done(iter: *mut RawDownhillIter) -> bool;
    fn npnr_inc_uphill_iter(iter: *mut RawUphillIter);
    fn npnr_deref_uphill_iter(iter: *mut RawUphillIter) -> PipId;
    fn npnr_is_uphill_iter_done(iter: *mut RawUphillIter) -> bool;
}

/// Store for the nets of a context.
pub struct Nets {
    pub nets: HashMap<IdString, cxx::UniquePtr<NetInfo>>,
    pub users: HashMap<IdString, Vec<PortRef>>,
    pub index_to_net: Vec<IdString>,
}

unsafe impl Send for Nets {}
unsafe impl Sync for Nets {}

impl Nets {
    /// Create a new store for the nets of a context.
    ///
    /// Note that this leaks memory created by nextpnr; the intention is this is called once.

    /// Find net users given a net's name.
    pub fn users_by_name(&self, net: IdString) -> Option<&Vec<PortRef>> {
        self.users.get(&net)
    }

    /// Return the number of nets in the store.
    pub fn len(&self) -> usize {
        self.nets.len()
    }

    pub fn name_from_index(&self, index: NetIndex) -> IdString {
        self.index_to_net[index.0 as usize]
    }

    pub fn get_net(&self, index: NetIndex) -> Option<&NetInfo> {
        self.nets
            .get(&self.name_from_index(index))
            .and_then(|ptr| ptr.as_ref())
    }

    pub fn get_net_mut(&mut self, index: NetIndex) -> Option<Pin<&mut NetInfo>> {
        self.nets
            .get_mut(&self.name_from_index(index))
            .and_then(|ptr| ptr.as_mut())
    }
}

pub struct NetSinkWireIter<'a> {
    ctx: &'a Context,
    net: &'a NetInfo,
    sink: &'a PortRef,
    n: u32,
}

impl<'a> Iterator for NetSinkWireIter<'a> {
    type Item = WireId;

    fn next(&mut self) -> Option<Self::Item> {
        let item = unsafe { self.ctx.sink_wire(self.net, self.sink, self.n as usize) };
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
    iter: *mut RawDownhillIter,
    phantom_data: std::marker::PhantomData<&'a PipId>,
}

impl<'a> Iterator for DownhillPipsIter<'a> {
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

impl<'a> Drop for DownhillPipsIter<'a> {
    fn drop(&mut self) {
        unsafe { npnr_delete_downhill_iter(self.iter) };
    }
}

#[repr(C)]
struct RawUphillIter {
    content: [u8; 0],
}

pub struct UphillPipsIter<'a> {
    iter: *mut RawUphillIter,
    phantom_data: std::marker::PhantomData<&'a PipId>,
}

impl<'a> Iterator for UphillPipsIter<'a> {
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

impl<'a> Drop for UphillPipsIter<'a> {
    fn drop(&mut self) {
        unsafe { npnr_delete_uphill_iter(self.iter) };
    }
}
