#![allow(unused)]

// arch-neutral ffi types
pub mod base {

    // nextpnr_base_types.h
    #[repr(C)]
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub enum PlaceStrength {
        None = 0,
        Weak = 1,
        Strong = 2,
        Placer = 3,
        Fixed = 4,
        Locked = 5,
        User = 6,
    }

    unsafe impl cxx::ExternType for PlaceStrength {
        type Id = cxx::type_id!("PlaceStrength");
        type Kind = cxx::kind::Trivial;
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub struct Loc {
        pub x: libc::c_int,
        pub y: libc::c_int,
        pub z: libc::c_int,
    }

    unsafe impl cxx::ExternType for Loc {
        type Id = cxx::type_id!("Loc");
        type Kind = cxx::kind::Trivial;
    }

    impl From<(i32, i32)> for Loc {
        fn from((x, y): (i32, i32)) -> Self {
            Self { x, y, z: 0 }
        }
    }

    // idstring.h
    #[repr(C)]
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub struct IdString(libc::c_int);

    unsafe impl cxx::ExternType for IdString {
        type Id = cxx::type_id!("IdString");
        type Kind = cxx::kind::Trivial;
    }
}

// TODO: Fix hardcoding by using bindgen
pub mod ecp5 {
    use super::*;

    #[derive(Copy, Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
    #[repr(C)]
    pub struct Location {
        x: i16,
        y: i16,
    }

    unsafe impl cxx::ExternType for Location {
        type Id = cxx::type_id!("Location");
        type Kind = cxx::kind::Trivial;
    }

    #[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
    #[repr(C)]
    pub struct PipId {
        location: Location,
        index: i32,
    }

    unsafe impl cxx::ExternType for PipId {
        type Id = cxx::type_id!("PipId");
        type Kind = cxx::kind::Trivial;
    }

    #[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
    #[repr(C)]
    pub struct BelId {
        location: Location,
        index: i32,
    }

    unsafe impl cxx::ExternType for BelId {
        type Id = cxx::type_id!("BelId");
        type Kind = cxx::kind::Trivial;
    }

    #[derive(Copy, Clone, Debug, Hash, PartialEq, Eq, PartialOrd, Ord)]
    #[repr(C)]
    pub struct WireId {
        pub location: Location,
        index: i32,
    }

    unsafe impl cxx::ExternType for WireId {
        type Id = cxx::type_id!("WireId");
        type Kind = cxx::kind::Trivial;
    }

    #[derive(Copy, Clone, Debug, Hash, PartialEq, Eq)]
    #[repr(C)]
    pub struct PortRef {
        cell: *mut CellInfo,
        port: base::IdString,
        budget: i32, // delay_t
    }

    unsafe impl cxx::ExternType for PortRef {
        type Id = cxx::type_id!("PortRef");
        type Kind = cxx::kind::Trivial;
    }

    #[cxx::bridge]
    pub mod ecp5_bridge {
        // C++ types and signatures exposed to Rust.
        unsafe extern "C++" {
            include!("awooter.h");

            pub type Context;
            pub type NetInfo;
            pub type CellInfo;
            pub type PortRef = crate::npnr::ffi::ecp5::PortRef;

            pub type NetDict;

            type DelayQuad;

            // base
            type IdString = crate::npnr::ffi::base::IdString;
            type Loc = crate::npnr::ffi::base::Loc;
            type PlaceStrength = crate::npnr::ffi::base::PlaceStrength;

            // arch
            type BelId = crate::npnr::ffi::ecp5::BelId;
            type PipId = crate::npnr::ffi::ecp5::PipId;
            type WireId = crate::npnr::ffi::ecp5::WireId;
            type Location = crate::npnr::ffi::ecp5::Location;

            /* Context member functions */
            fn check(self: &Context);

            #[rust_name = "grid_dim_x"]
            fn getGridDimX(self: &Context) -> i32;
            #[rust_name = "grid_dim_y"]
            fn getGridDimY(self: &Context) -> i32;

            /* Context: bel functions */
            /// Bind a given bel to a given cell with the given strength.
            #[rust_name = "bind_bel"]
            unsafe fn bindBel(
                self: Pin<&mut Context>,
                bel: BelId,
                cell: *mut CellInfo,
                strength: PlaceStrength,
            );
            /// Unbind a bel.
            #[rust_name = "unbind_bel"]
            fn unbindBel(self: Pin<&mut Context>, bel: BelId);
            /// Returns true if the bel is available.
            /// A bel can be unavailable because it is bound, or because it is exclusive to some other resource that is bound.
            #[rust_name = "check_bel_avail"]
            fn checkBelAvail(self: &Context, bel: BelId) -> bool;

            /* Context: pip functions */

            /// Get the destination wire for a pip.
            #[rust_name = "pip_dst_wire"]
            fn getPipDstWire(self: &Context, pip: PipId) -> WireId;
            /// Get the source wire for a pip.
            #[rust_name = "pip_src_wire"]
            fn getPipSrcWire(self: &Context, pip: PipId) -> WireId;
            #[rust_name = "pip_location"]
            fn getPipLocation(self: &Context, pip: PipId) -> Loc;
            #[rust_name = "pip_avail_for_net"]
            unsafe fn checkPipAvailForNet(self: &Context, pip: PipId, net: *mut NetInfo) -> bool;

            /// Bid a pip to a net. This also binds the destination wire of that pip.
            #[rust_name = "bind_pip"]
            unsafe fn bindPip(
                self: Pin<&mut Context>,
                pip: PipId,
                cell: *mut NetInfo,
                strength: PlaceStrength,
            );
            /// Unbind a pip and the wire driven by that pip.
            #[rust_name = "unbind_pip"]
            fn unbindPip(self: Pin<&mut Context>, pip: PipId);

            /* Context: wire functions */
            /// Bind a wire to a net. This method must be used when binding a wire that is driven by a bel pin. Use bindPip() when binding a wire that is driven by a pip.
            #[rust_name = "bind_wire"]
            unsafe fn bindWire(
                self: Pin<&mut Context>,
                wire: WireId,
                cell: *mut NetInfo,
                strength: PlaceStrength,
            );
            /// Unbind a wire. For wires that are driven by a pip, this will also unbind the driving pip.
            #[rust_name = "unbind_wire"]
            fn unbindWire(self: Pin<&mut Context>, wire: WireId);
            #[rust_name = "source_wire"]
            unsafe fn getNetinfoSourceWire(self: &Context, net: *const NetInfo) -> WireId;
            #[rust_name = "sink_wire"]
            unsafe fn getNetinfoSinkWire(
                self: &Context,
                net: *const NetInfo,
                sink: &PortRef,
                n: usize,
            ) -> WireId;

            /* CellInfo member functions */
            #[rust_name = "location"]
            fn getLocation(self: &CellInfo) -> Loc;

            /* NetDict member functions. Note this type is actually dict<IdString, UniquePtr<NetInfo>> */
            #[cxx_name = "at"]
            fn get_net(self: &NetDict, key: &IdString) -> &UniquePtr<NetInfo>;
            #[cxx_name = "at"]
            fn move_net(self: Pin<&mut NetDict>, key: &IdString) -> &mut UniquePtr<NetInfo>;
            fn size(self: &NetDict) -> usize;
        }
    }

    pub use ecp5_bridge::{CellInfo, Context, NetDict, NetInfo};
}
