use core::ops::Range;
use core::ptr::{null, null_mut};
use core::sync::atomic::{AtomicPtr, AtomicUsize, Ordering};

use log::{error, warn};

use super::{Alloc, Error, Result, Size, MIN_PAGES};
use crate::util::Page;

/// Simple volatile 4K page allocator that uses CPU-local linked lists.
/// During initialization allocators memory is split into pages
/// and evenly distributed to the cores.
/// The linked lists are build directly within the pages,
/// storing the next pointers at the beginning of the free pages.
///
/// No extra load balancing is made, if a core runs out of memory,
/// the allocation fails.
#[repr(align(64))]
pub struct ListLocalAlloc {
    memory: Range<*const Page>,
    /// CPU local metadata
    local: Vec<Local>,
    pages: usize,
}

impl ListLocalAlloc {
    pub fn new() -> Self {
        Self {
            memory: null()..null(),
            local: Vec::new(),
            pages: 0,
        }
    }
}

unsafe impl Send for ListLocalAlloc {}
unsafe impl Sync for ListLocalAlloc {}

impl Alloc for ListLocalAlloc {
    #[cold]
    fn init(&mut self, cores: usize, memory: &mut [Page], _overwrite: bool) -> Result<()> {
        warn!(
            "initializing c={cores} {:?} {}",
            memory.as_ptr_range(),
            memory.len()
        );
        if memory.len() < cores * MIN_PAGES {
            error!("Not enough memory {} < {}", memory.len(), cores * MIN_PAGES);
            return Err(Error::Memory);
        }

        let begin = memory.as_ptr() as usize;
        let pages = memory.len();

        // build core local free lists
        let mut local = Vec::with_capacity(cores);
        let p_core = pages / cores;
        for core in 0..cores {
            let l = Local::new();
            // build linked list
            for i in core * p_core + 1..(core + 1) * p_core {
                memory[i - 1]
                    .cast_mut::<Node>()
                    .set((begin + i * Page::SIZE) as *mut _);
            }
            memory[(core + 1) * p_core - 1]
                .cast_mut::<Node>()
                .set(null_mut());
            l.next.set((begin + core * p_core * Page::SIZE) as *mut _);
            local.push(l);
        }

        self.pages = p_core * cores;
        self.memory = memory[..p_core * cores].as_ptr_range();
        self.local = local;
        Ok(())
    }

    #[inline(never)]
    fn get(&self, core: usize, size: Size) -> Result<u64> {
        if size != Size::L0 {
            error!("{size:?} not supported");
            return Err(Error::Memory);
        }

        let local = &self.local[core];
        if let Some(node) = local.next.pop() {
            local.counter.fetch_add(1, Ordering::Relaxed);
            let addr = node as *mut _ as u64;
            debug_assert!(addr % Page::SIZE as u64 == 0 && self.memory.contains(&(addr as _)));
            Ok(addr)
        } else {
            error!("No memory");
            Err(Error::Memory)
        }
    }

    #[inline(never)]
    fn put(&self, core: usize, addr: u64) -> Result<()> {
        if addr % Page::SIZE as u64 != 0 || !self.memory.contains(&(addr as _)) {
            error!("invalid addr");
            return Err(Error::Address);
        }

        let local = &self.local[core];
        local.next.push(unsafe { &mut *(addr as *mut Node) });
        local.counter.fetch_sub(1, Ordering::Relaxed);
        Ok(())
    }

    fn pages(&self) -> usize {
        self.pages
    }

    #[cold]
    fn allocated_pages(&self) -> usize {
        let mut pages = 0;
        for local in &self.local {
            pages += local.counter.load(Ordering::Relaxed);
        }
        pages
    }
}

#[repr(align(64))]
struct Local {
    next: Node,
    counter: AtomicUsize,
}

impl Local {
    fn new() -> Self {
        Self {
            next: Node::new(),
            counter: AtomicUsize::new(0),
        }
    }
}

struct Node(AtomicPtr<Node>);

impl Node {
    fn new() -> Self {
        Self(AtomicPtr::new(null_mut()))
    }
    fn set(&self, v: *mut Node) {
        self.0.store(v, Ordering::Relaxed);
    }
    fn push(&self, v: &mut Node) {
        let next = self.0.load(Ordering::Relaxed);
        v.0.store(next, Ordering::Relaxed);
        self.0.store(v, Ordering::Relaxed);
    }
    fn pop(&self) -> Option<&mut Node> {
        let curr = self.0.load(Ordering::Relaxed);
        if !curr.is_null() {
            let curr = unsafe { &mut *curr };
            let next = curr.0.load(Ordering::Relaxed);
            self.0.store(next, Ordering::Relaxed);
            Some(curr)
        } else {
            None
        }
    }
}
