//! Global allocator for the PSP build, forwarding to the C++ half's heap.
//!
//! McpeGen is around 190 KB of noise tables, which is why the C++ builds it with
//! `new` and drops it in worldGenFree. Keeping that shape means the port needs
//! an allocator, and the only heap on the console is the one pspsdk already
//! manages. See src/rs/rs.cpp for the other side.

use core::alloc::{GlobalAlloc, Layout};

extern "C" {
    fn ds_alloc(align: i32, size: i32) -> *mut u8;
    fn ds_free(ptr: *mut u8);
}

struct CHeap;

unsafe impl GlobalAlloc for CHeap {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        ds_alloc(layout.align() as i32, layout.size() as i32)
    }

    unsafe fn dealloc(&self, ptr: *mut u8, _layout: Layout) {
        ds_free(ptr)
    }
}

#[global_allocator]
static HEAP: CHeap = CHeap;
