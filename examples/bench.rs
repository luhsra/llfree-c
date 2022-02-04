#![cfg(all(feature = "thread", feature = "logger"))]

use core::fmt;
use std::any::type_name;
use std::fs::File;
use std::io::Write;
use std::sync::{Arc, Barrier};
use std::time::Instant;

use clap::Parser;
use log::warn;

use nvalloc::alloc::array_aligned::ArrayAlignedAlloc;
use nvalloc::alloc::array_atomic::ArrayAtomicAlloc;
use nvalloc::alloc::array_packed::ArrayPackedAlloc;
use nvalloc::alloc::list_local::ListLocalAlloc;
use nvalloc::alloc::list_locked::ListLockedAlloc;
use nvalloc::alloc::table::TableAlloc;
use nvalloc::alloc::{Alloc, Size, MIN_PAGES};
use nvalloc::mmap::MMap;
use nvalloc::table::Table;
use nvalloc::util::Page;
use nvalloc::{thread, util};

/// Benchmarking the allocators against each other.
#[derive(Parser, Debug)]
#[clap(about, version, author)]
struct Args {
    #[clap(short, long)]
    realloc: bool,
    #[clap(short, long, default_value = "1")]
    threads: Vec<usize>,
    #[clap(short, long, default_value = "bench/out/bench.csv")]
    outfile: String,
    #[clap(long)]
    dax: Option<String>,
    #[clap(short, long, default_value_t = 1)]
    iterations: usize,
    #[clap(short, long, default_value_t = 0)]
    size: usize,
    #[clap(long, default_value_t = 1)]
    cpu_stride: usize,
    /// Memory in GiB
    #[clap(short, long, default_value_t = 16)]
    memory: usize,
}

fn main() {
    let Args {
        realloc,
        threads,
        outfile,
        dax,
        iterations,
        size,
        cpu_stride,
        memory,
    } = Args::parse();

    util::logging();

    for &thread in &threads {
        assert!(thread >= 1);
        assert!(thread * cpu_stride <= std::thread::available_parallelism().unwrap().get());
    }
    let max_threads = threads.iter().copied().max().unwrap();
    let thread_pages = (memory * Table::span(2)) / max_threads;
    assert!(
        thread_pages >= MIN_PAGES,
        "{} !=> {}",
        thread_pages,
        MIN_PAGES
    );

    unsafe { nvalloc::thread::CPU_STRIDE = cpu_stride };

    let mut outfile = File::create(outfile).unwrap();
    writeln!(outfile, "alloc,threads,iteration,allocs,{}", Perf::header()).unwrap();

    let size = match size {
        0 => Size::L0,
        1 => Size::L1,
        2 => Size::L2,
        _ => panic!("`size` has to be 0, 1 or 2"),
    };
    let allocs = thread_pages / 2 / Table::span(size as _);
    warn!("Allocs: {allocs} of size {size:?}");

    let mut mapping = mapping(0x1000_0000_0000, memory * Table::span(2), dax).unwrap();

    // Warmup
    for page in &mut mapping[..] {
        *page.cast::<usize>() = 1;
    }

    for threads in threads {
        for i in 0..iterations {
            let mapping = &mut mapping[..thread_pages * threads];

            bench_alloc::<TableAlloc>(mapping, size, threads, realloc, i, &mut outfile);
            bench_alloc::<ArrayAlignedAlloc>(mapping, size, threads, realloc, i, &mut outfile);
            bench_alloc::<ArrayPackedAlloc>(mapping, size, threads, realloc, i, &mut outfile);
            bench_alloc::<ArrayAtomicAlloc>(mapping, size, threads, realloc, i, &mut outfile);

            if size == Size::L0 {
                bench_alloc::<ListLocalAlloc>(mapping, Size::L0, threads, realloc, i, &mut outfile);

                if threads <= 24 {
                    bench_alloc::<ListLockedAlloc>(
                        mapping,
                        Size::L0,
                        threads,
                        realloc,
                        i,
                        &mut outfile,
                    );
                }
            }
        }
    }
}

fn mapping<'a>(begin: usize, length: usize, dax: Option<String>) -> Result<MMap<'a, Page>, ()> {
    #[cfg(target_os = "linux")]
    if length > 0 {
        if let Some(file) = dax {
            warn!(
                "MMap file {file} l={}G ({:x})",
                (length * std::mem::size_of::<Page>()) >> 30,
                length * std::mem::size_of::<Page>()
            );
            let f = std::fs::OpenOptions::new()
                .read(true)
                .write(true)
                .open(file)
                .unwrap();
            return MMap::dax(begin, length, f);
        }
    }
    MMap::anon(begin, length)
}

fn bench_alloc<A: Alloc>(
    mapping: &mut [Page],
    size: Size,
    threads: usize,
    realloc: bool,
    i: usize,
    out: &mut dyn Write,
) {
    let name = type_name::<A>();
    let name = name.rsplit_once(':').map(|s| s.1).unwrap_or(name);
    let name = name.strip_suffix("Alloc").unwrap_or(name);

    warn!("\n\n>>> bench t={threads} {size:?} {name}\n");

    // Allocate half the memory
    let allocs = mapping.len() / threads / 2 / Table::span(size as _);

    let timer = Instant::now();
    A::init(threads, mapping).unwrap();
    let init = timer.elapsed().as_millis();
    warn!("init time {init}ms");

    let barrier = Arc::new(Barrier::new(threads));

    let mut perf = if realloc {
        let perfs = thread::parallel(threads as _, move |t| {
            reallocate::<A>(t, allocs, size, barrier)
        });
        assert_eq!(
            A::instance().allocated_pages(),
            threads * allocs * Table::span(size as _)
        );
        Perf::avg(perfs.into_iter()).unwrap()
    } else {
        let perfs = thread::parallel(threads as _, move |t| {
            bulk_alloc::<A>(t, allocs, size, barrier)
        });
        assert_eq!(A::instance().allocated_pages(), 0);
        Perf::avg(perfs.into_iter()).unwrap()
    };

    A::destroy();

    perf.init = init;
    warn!("{perf:#?}");
    writeln!(out, "{name},{threads},{i},{allocs},{perf}").unwrap();
}

fn reallocate<A: Alloc>(t: usize, allocs: usize, size: Size, barrier: Arc<Barrier>) -> Perf {
    thread::pin(t);
    for _ in 0..allocs {
        A::instance().get(t, size).unwrap();
    }

    barrier.wait();
    let timer = Instant::now();
    for _ in 0..allocs {
        let page = A::instance().get(t, size).unwrap();
        A::instance().put(t, page).unwrap();
    }

    let realloc = timer.elapsed().as_nanos() / allocs as u128;
    Perf {
        get_min: realloc,
        get_avg: realloc,
        get_max: realloc,
        put_min: realloc,
        put_avg: realloc,
        put_max: realloc,
        init: 0,
        total: timer.elapsed().as_millis(),
    }
}

fn bulk_alloc<A: Alloc>(t: usize, allocs: usize, size: Size, barrier: Arc<Barrier>) -> Perf {
    thread::pin(t);

    barrier.wait();
    let mut pages = Vec::with_capacity(allocs);
    let t1 = Instant::now();
    for _ in 0..allocs {
        pages.push(A::instance().get(t, size).unwrap());
    }
    let get = t1.elapsed().as_nanos() / allocs as u128;

    barrier.wait();
    let t2 = Instant::now();
    for page in pages {
        A::instance().put(t, page).unwrap();
    }
    let put = t2.elapsed().as_nanos() / allocs as u128;

    Perf {
        get_min: get,
        get_avg: get,
        get_max: get,
        put_min: put,
        put_avg: put,
        put_max: put,
        init: 0,
        total: t1.elapsed().as_millis(),
    }
}

#[derive(Debug)]
struct Perf {
    get_min: u128,
    get_avg: u128,
    get_max: u128,
    put_min: u128,
    put_avg: u128,
    put_max: u128,
    init: u128,
    total: u128,
}

impl Default for Perf {
    fn default() -> Self {
        Self {
            get_min: u128::MAX,
            get_avg: 0,
            get_max: 0,
            put_min: u128::MAX,
            put_avg: 0,
            put_max: 0,
            init: 0,
            total: 0,
        }
    }
}

impl Perf {
    fn avg(iter: impl Iterator<Item = Perf>) -> Option<Perf> {
        let mut res = Perf::default();
        let mut counter = 0;
        for p in iter {
            res.get_min = res.get_min.min(p.get_min);
            res.get_avg += p.get_avg;
            res.get_max = res.get_max.max(p.get_max);
            res.put_min = res.put_min.min(p.put_min);
            res.put_avg += p.put_avg;
            res.put_max = res.put_max.max(p.put_max);
            res.init += p.init;
            res.total += p.total;
            counter += 1;
        }
        if counter > 0 {
            res.get_avg /= counter;
            res.put_avg /= counter;
            res.init /= counter as u128;
            res.total /= counter as u128;
            Some(res)
        } else {
            None
        }
    }
    fn header() -> &'static str {
        "get_min,get_avg,get_max,put_min,put_avg,put_max,init,total"
    }
}

impl fmt::Display for Perf {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "{},{},{},{},{},{},{},{}",
            self.get_min,
            self.get_avg,
            self.get_max,
            self.put_min,
            self.put_avg,
            self.put_max,
            self.init,
            self.total
        )
    }
}
