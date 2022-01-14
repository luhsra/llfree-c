use std::alloc::Layout;
use std::arch::asm;
use std::marker::PhantomData;
use std::sync::atomic::{AtomicU64, AtomicUsize, Ordering};

/// Correctly sized and aligned page.
#[derive(Clone)]
#[repr(align(0x1000))]
pub struct Page {
    _data: [u8; Page::SIZE],
}
const _: () = assert!(Layout::new::<Page>().size() == Page::SIZE);
const _: () = assert!(Layout::new::<Page>().align() == Page::SIZE);
impl Page {
    pub const SIZE_BITS: usize = 12; // 2^12 => 4KiB
    pub const SIZE: usize = 1 << Page::SIZE_BITS;
    pub const fn new() -> Self {
        Self {
            _data: [0; Page::SIZE],
        }
    }
    pub fn cast<T>(&mut self) -> &mut T {
        unsafe { std::mem::transmute(self) }
    }
}

pub const fn align_up(v: usize, align: usize) -> usize {
    (v + align - 1) & !(align - 1)
}

pub const fn align_down(v: usize, align: usize) -> usize {
    v & !(align - 1)
}

pub struct Atomic<T: From<u64> + Into<u64>>(AtomicU64, PhantomData<T>);

impl<T: From<u64> + Into<u64>> Atomic<T> {
    pub fn new(v: T) -> Self {
        Self(AtomicU64::new(v.into()), PhantomData)
    }
    pub fn compare_exchange(&self, current: T, new: T) -> Result<T, T> {
        match self.0.compare_exchange(
            current.into(),
            new.into(),
            Ordering::SeqCst,
            Ordering::SeqCst,
        ) {
            Ok(v) => Ok(T::from(v)),
            Err(v) => Err(T::from(v)),
        }
    }
    pub fn fetch_update<F: FnMut(T) -> Option<T>>(&self, mut f: F) -> Result<T, T> {
        match self
            .0
            .fetch_update(Ordering::SeqCst, Ordering::SeqCst, |v| {
                f(T::from(v)).map(T::into)
            }) {
            Ok(v) => Ok(T::from(v)),
            Err(v) => Err(T::from(v)),
        }
    }
    pub fn load(&self) -> T {
        T::from(self.0.load(Ordering::SeqCst))
    }
    pub fn store(&self, v: T) {
        self.0.store(v.into(), Ordering::SeqCst)
    }
    pub fn swap(&self, v: T) -> T {
        self.0.swap(v.into(), Ordering::SeqCst).into()
    }
}

/// Simple atomic stack with atomic entries.
pub struct AtomicStack {
    data: Vec<AtomicU64>,
    i: AtomicUsize,
}

unsafe impl Send for AtomicStack {}
unsafe impl Sync for AtomicStack {}

impl AtomicStack {
    pub fn new(capacity: usize) -> Self {
        let mut data = Vec::with_capacity(capacity);
        data.resize_with(capacity, || AtomicU64::new(0));
        Self {
            data,
            i: AtomicUsize::new(0),
        }
    }
    pub fn push(&self, v: u64) -> Result<(), ()> {
        let i = self.i.fetch_add(1, Ordering::SeqCst);
        if i < self.data.len() {
            self.data[i].store(v, Ordering::SeqCst);
            Ok(())
        } else {
            self.i.fetch_sub(1, Ordering::SeqCst);
            Err(())
        }
    }
    pub fn pop(&self) -> Result<u64, ()> {
        let mut i = self.i.load(Ordering::SeqCst);
        loop {
            if i == 0 {
                return Err(());
            }
            let val = self.data[i - 1].load(Ordering::SeqCst);

            // Check if index is still the same
            match self
                .i
                .compare_exchange(i, i - 1, Ordering::SeqCst, Ordering::SeqCst)
            {
                Ok(_) => return Ok(val),
                Err(j) => i = j,
            }
        }
    }
}

#[cfg(any(test, feature = "logger"))]
pub fn logging() {
    use std::io::Write;
    use std::panic::{self, Location};
    use std::thread::ThreadId;

    use log::{Level, Record};

    #[cfg(any(test, feature = "thread"))]
    let core = {
        use crate::thread::PINNED;
        PINNED.with(|p| p.load(Ordering::SeqCst))
    };
    #[cfg(not(any(test, feature = "thread")))]
    let core = 0usize;

    let _ = env_logger::Builder::from_env(env_logger::Env::default().default_filter_or("warn"))
        .format(move |buf, record| {
            let color = match record.level() {
                log::Level::Error => "\x1b[91m",
                log::Level::Warn => "\x1b[93m",
                log::Level::Info => "\x1b[90m",
                log::Level::Debug => "\x1b[90m",
                log::Level::Trace => "\x1b[90m",
            };

            writeln!(
                buf,
                "{}[{:5} {:02?}@{:02?} {}:{}] {}\x1b[0m",
                color,
                record.level(),
                unsafe { std::mem::transmute::<ThreadId, u64>(std::thread::current().id()) },
                core,
                record.file().unwrap_or_default(),
                record.line().unwrap_or_default(),
                record.args()
            )
        })
        .try_init();

    panic::set_hook(Box::new(|info| {
        if let Some(args) = info.message() {
            log::logger().log(
                &Record::builder()
                    .args(*args)
                    .level(Level::Error)
                    .file(info.location().map(Location::file))
                    .line(info.location().map(Location::line))
                    .build(),
            );
        } else if let Some(&payload) = info.payload().downcast_ref::<&'static str>() {
            log::logger().log(
                &Record::builder()
                    .args(format_args!("{}", payload))
                    .level(Level::Error)
                    .file(info.location().map(Location::file))
                    .line(info.location().map(Location::line))
                    .build(),
            );
        } else {
            log::logger().log(
                &Record::builder()
                    .args(format_args!("panic!"))
                    .level(Level::Error)
                    .file(info.location().map(Location::file))
                    .line(info.location().map(Location::line))
                    .build(),
            )
        }

        log::logger().flush();
    }));
}

/// Executes CLWB (cache-line write back) for the given address.
///
/// # Safety
/// Directly executes an asm instruction.
#[cfg(target_arch = "x86_64")]
#[inline(always)]
pub unsafe fn _mm_clwb(addr: *const ()) {
    asm!("clwb [rax]", in("rax") addr);
}

/// Executes RDTSC (read time-stamp counter) and returns the current cycle count.
///
/// # Safety
/// Directly executes an asm instruction.
#[cfg(target_arch = "x86_64")]
#[inline(always)]
unsafe fn time_stamp_counter() -> u64 {
    let mut lo: u32;
    let mut hi: u32;
    asm!("rdtsc", out("eax") lo, out("edx") hi);
    lo as u64 | (hi as u64) << 32
}

/// Reads the `CNTVCT_EL0` register and returns the current cycle count.
///
/// # Safety
/// Directly executes an asm instruction.
#[cfg(target_arch = "aarch64")]
#[inline(always)]
unsafe fn time_stamp_counter() -> u64 {
    let mut val: u64;
    asm!("mrs {}, cntvct_e10", out(reg) val);
    val
}

#[derive(Debug, Clone, Copy)]
#[repr(transparent)]
pub struct Cycles(u64);

impl Cycles {
    pub fn now() -> Self {
        Self(unsafe { time_stamp_counter() })
    }
    pub fn elapsed(self) -> u64 {
        unsafe { time_stamp_counter() }.wrapping_sub(self.0)
    }
}

#[cfg(test)]
mod test {
    use std::sync::{Arc, Barrier};

    use super::AtomicStack;
    use crate::thread;

    #[cfg(target_arch = "x86_64")]
    #[test]
    #[ignore]
    fn clwb() {
        let mut data = Box::new(43_u64);
        *data = 44;
        unsafe { super::_mm_clwb(data.as_ref() as *const _ as _) };
    }

    #[test]
    fn atomic_stack() {
        let stack = AtomicStack::new(10);
        stack.push(10).unwrap();
        stack.push(1).unwrap();
        assert_eq!(stack.pop(), Ok(1));
        assert_eq!(stack.pop(), Ok(10));
        assert_eq!(stack.pop(), Err(()));

        const THREADS: usize = 4;
        const N: usize = 10;
        let shared = Arc::new(AtomicStack::new(THREADS * 4));
        let clone = shared.clone();
        let barrier = Arc::new(Barrier::new(THREADS));
        thread::parallel(THREADS, move |t| {
            shared.push(t as u64).unwrap();
            barrier.wait();
            for i in 0..N {
                let v = shared.pop().unwrap();
                shared.push(v + i as u64).unwrap();
            }
        });

        let mut count = 0;
        let mut sum = 0;
        while let Ok(v) = clone.pop() {
            count += 1;
            sum += v;
            println!("{v}")
        }
        assert_eq!(count, 4);
        assert_eq!(
            sum,
            ((1..THREADS).sum::<usize>() + THREADS * (1..N).sum::<usize>()) as u64
        )
    }
}
