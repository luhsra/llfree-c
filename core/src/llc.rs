use crate::{Error, PFNRange, Result, PFN};
use core::ops::Range;
use core::{
    ffi::{c_char, CStr},
    fmt,
};

use super::{Alloc, Init};

use core::ffi::c_void;

/// C implementation of LLFree
///
/// Only containts an opaque pointer, that is passed to the C functions.
///
/// TODO: As the Alloc trait is already dyn (opaque), we could just use
/// LLC directly as opaque type, but then default cannot be implemented anymore...
#[repr(transparent)]
pub struct LLC {
    raw: *mut c_void,
}

unsafe impl Send for LLC {}
unsafe impl Sync for LLC {}

impl Alloc for LLC {
    fn init(&mut self, cores: usize, area: Range<PFN>, init: Init, free_all: bool) -> Result<()> {
        let ret = unsafe {
            llc_init(
                self.raw,
                cores as _,
                area.start.0 as _,
                area.len() as _,
                init as usize as _,
                free_all as _,
            )
        };
        to_result(ret).map(|_| ())
    }

    fn get(&self, core: usize, order: usize) -> Result<PFN> {
        let ret = unsafe { llc_get(self.raw, core as _, order as _) };
        Ok(PFN(to_result(ret)? as _))
    }

    fn put(&self, core: usize, frame: PFN, order: usize) -> Result<()> {
        let ret = unsafe { llc_put(self.raw, core as _, frame.0 as _, order as _) };
        to_result(ret).map(|_| ())
    }

    fn is_free(&self, frame: PFN, order: usize) -> bool {
        let ret = unsafe { llc_is_free(self.raw, frame.0 as _, order as _) };
        ret != 0
    }

    fn frames(&self) -> usize {
        unsafe { llc_frames(self.raw) as _ }
    }

    fn free_frames(&self) -> usize {
        unsafe { llc_free_frames(self.raw) as _ }
    }

    // Optional functions ...
}

impl Drop for LLC {
    fn drop(&mut self) {
        unsafe { llc_drop(self.raw) }
    }
}

/// Converting return codes to errors
fn to_result(code: i64) -> Result<u64> {
    if code >= 0 {
        Ok(code as _)
    } else {
        match code {
            -1 => Err(Error::Memory),
            -2 => Err(Error::Retry),
            -3 => Err(Error::Address),
            -4 => Err(Error::Initialization),
            -5 => Err(Error::Corruption),
            _ => unreachable!("invalid return code"),
        }
    }
}

impl Default for LLC {
    fn default() -> Self {
        Self {
            raw: unsafe { llc_default() },
        }
    }
}

impl fmt::Debug for LLC {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        // wrapper function that is called by the c implementation
        extern "C" fn writer(arg: *mut c_void, msg: *const c_char) {
            let f = unsafe { &mut *arg.cast::<fmt::Formatter<'_>>() };
            let c_str = unsafe { CStr::from_ptr(msg) };
            write!(f, "{}", c_str.to_str().unwrap()).unwrap();
        }

        unsafe { llc_debug(self.raw, writer, (f as *mut fmt::Formatter).cast()) };

        Ok(())
    }
}

#[link(name = "llc", kind = "static")]
extern "C" {
    /// Creates the allocator and returns a pointer to its data that is passed into all other functions
    fn llc_default() -> *mut c_void;

    /// Initializes the allocator for the given memory region, returning 0 on success or a negative error code
    fn llc_init(
        this: *mut c_void,
        cores: u64,
        start_pfn: u64,
        len: u64,
        init: u8,
        free_all: u8,
    ) -> i64;

    /// Destructs the allocator
    fn llc_drop(this: *mut c_void);

    /// Allocates a frame and returns its address, or a negative error code
    fn llc_get(this: *const c_void, core: u64, order: u64) -> i64;

    /// Frees a frame, returning 0 on success or a negative error code
    fn llc_put(this: *const c_void, core: u64, frame: u64, order: u64) -> i64;

    /// Checks if a frame is allocated, returning 0 if not
    fn llc_is_free(this: *const c_void, frame: u64, order: u64) -> u8;

    /// Returns the total number of frames the allocator can allocate
    fn llc_frames(this: *const c_void) -> u64;

    /// Returns number of currently free frames
    fn llc_free_frames(this: *const c_void) -> u64;

    /// Prints the allocators state for debugging
    fn llc_debug(
        this: *const c_void,
        writer: extern "C" fn(*mut c_void, *const c_char),
        arg: *mut c_void,
    );
}

#[cfg(test)]
mod test {
    use super::LLC;

    #[test]
    fn test_debug() {
        let alloc = LLC::default();
        println!("{alloc:?}");
    }
}
