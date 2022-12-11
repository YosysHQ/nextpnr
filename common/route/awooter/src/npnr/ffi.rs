
// arch-neutral ffi types
pub mod base {
    #[repr(C)]
    pub enum PlaceStrength {
        None = 0,
        Weak = 1,
        Strong = 2,
        Placer = 3,
        Fixed = 4,
        Locked = 5,
        User = 6
    }

    #[repr(C)]
    #[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
    pub struct Loc {
        pub x: libc::c_int,
        pub y: libc::c_int,
        pub z: libc::c_int,
    }
}

pub mod ecp5 {
    pub use super::base::*;

    #[cxx::bridge]
    pub mod ecp5_bridge {
        // C++ types and signatures exposed to Rust.
        unsafe extern "C++" {
            include!("awooter.h");

            pub type Context;
            pub type Location;
            pub type NetInfo;
            pub type CellInfo;
            type DelayQuad;

            /* Context member functions */
            fn check(self: &Context);

            #[rust_name = "grid_dim_x"]
            fn getGridDimX(self: &Context) -> i32;
            #[rust_name = "grid_dim_y"]
            fn getGridDimY(self: &Context) -> i32;
        }
    }

    pub use ecp5_bridge::*;
}
