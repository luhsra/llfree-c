use core::cmp::Ordering;
use core::fmt;

use bitfield_struct::bitfield;
use log::error;

use crate::atomic::AtomicValue;
use crate::Size;

#[bitfield(u64)]
pub struct Entry {
    /// Number of subtrees where no page is allocated.
    #[bits(20)]
    pub empty: usize,
    /// Number of subtrees where at least one l0 page is allocated.
    #[bits(20)]
    pub partial_l0: usize,
    /// Number of subtrees where at least one l1 page is allocated.
    #[bits(20)]
    pub partial_l1: usize,
    #[bits(4)]
    _p: usize,
}

impl AtomicValue for Entry {
    type V = u64;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Change {
    None,
    IncEmpty,
    IncPartialL0,
    IncPartialL1,
    DecPartialL0,
    DecPartialL1,
}
impl Change {
    #[inline]
    pub fn p_inc(huge: bool) -> Change {
        if huge {
            Change::IncPartialL1
        } else {
            Change::IncPartialL0
        }
    }
    #[inline]
    pub fn p_dec(huge: bool) -> Change {
        if huge {
            Change::DecPartialL1
        } else {
            Change::DecPartialL0
        }
    }
}

impl Entry {
    #[inline]
    pub fn dec_partial(self, huge: bool) -> Option<Self> {
        if !huge && self.partial_l0() > 0 {
            Some(self.with_partial_l0(self.partial_l0() - 1))
        } else if huge && self.partial_l1() > 0 {
            Some(self.with_partial_l1(self.partial_l1() - 1))
        } else {
            None
        }
    }
    #[inline]
    pub fn dec_empty(self) -> Option<Self> {
        (self.empty() > 0).then(|| self.with_empty(self.empty() - 1))
    }
    #[inline]
    pub fn change(self, dec: Change) -> Option<Self> {
        match dec {
            Change::IncEmpty => Some(self.with_empty(self.empty() + 1)),
            Change::IncPartialL0 => Some(self.with_partial_l0(self.partial_l0() + 1)),
            Change::IncPartialL1 => Some(self.with_partial_l1(self.partial_l1() + 1)),
            Change::DecPartialL0 if self.partial_l0() > 0 => Some(
                self.with_partial_l0(self.partial_l0() - 1)
                    .with_empty(self.empty() + 1),
            ),
            Change::DecPartialL1 if self.partial_l1() > 0 => Some(
                self.with_partial_l1(self.partial_l1() - 1)
                    .with_empty(self.empty() + 1),
            ),
            _ => None,
        }
    }
}

impl fmt::Debug for Entry {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Entry")
            .field("empty", &self.empty())
            .field("partial_l0", &self.partial_l0())
            .field("partial_l1", &self.partial_l1())
            .finish()
    }
}

#[bitfield(u64)]
pub struct Entry3 {
    /// Number of free 4K pages.
    #[bits(20)]
    pub free: usize,
    /// Metadata for the higher level allocators.
    #[bits(42)]
    pub idx: usize,
    /// If this subtree is reserved by a CPU.
    pub reserved: bool,
    /// If this subtree contains allocated huge pages.
    pub huge: bool,
}

impl Default for Entry3 {
    fn default() -> Self {
        Self::new()
    }
}

impl AtomicValue for Entry3 {
    type V = u64;
}

impl Entry3 {
    pub const IDX_MAX: usize = (1 << 41) - 1;

    #[inline]
    pub fn empty(span: usize) -> Entry3 {
        Entry3::new().with_free(span)
    }
    /// Creates a new entry referring to a level 2 page table.
    #[inline]
    pub fn new_table(pages: usize, size: Size, reserved: bool) -> Entry3 {
        Entry3::new()
            .with_free(pages)
            .with_huge(size == Size::L1)
            .with_reserved(reserved)
    }
    /// Decrements the free pages counter.
    #[inline]
    pub fn dec(self, page_span: usize, span: usize) -> Option<Entry3> {
        let huge = page_span > 1;
        if (self.huge() == huge || self.free() == span) && self.free() >= page_span
        {
            Some(
                self.with_free(self.free() - page_span)
                    .with_huge(huge)
                    .with_reserved(true),
            )
        } else {
            None
        }
    }
    /// Increments the free pages counter.
    #[inline]
    pub fn inc(self, page_span: usize, max: usize) -> Option<Entry3> {
        let huge = page_span > 1;
        if self.huge() != huge {
            return None;
        }
        let pages = self.free() + page_span;
        match pages.cmp(&max) {
            Ordering::Greater => None,
            Ordering::Equal => Some(self.with_free(max).with_huge(false)),
            Ordering::Less => Some(self.with_free(pages)),
        }
    }
    /// Increments the free pages counter and checks for `idx` to match.
    #[inline]
    pub fn inc_idx(self, page_span: usize, idx: usize, max: usize) -> Option<Entry3> {
        let huge = page_span > 1;
        if self.huge() != huge || self.idx() != idx {
            return None;
        }
        let pages = self.free() + page_span;
        match pages.cmp(&max) {
            Ordering::Greater => None,
            Ordering::Equal => Some(self.with_free(max).with_huge(false)),
            Ordering::Less => Some(self.with_free(pages)),
        }
    }
    /// Reserves this entry.
    #[inline]
    pub fn reserve(self, page_span: usize, span: usize) -> Option<Entry3> {
        let huge = page_span > 1;
        if !self.reserved()
            && (self.free() >= span || self.huge() == huge)
            && self.free() >= page_span
        {
            Some(self.with_free(0).with_huge(huge).with_reserved(true))
        } else {
            None
        }
    }
    /// Reserves this entry if it is partially filled.
    #[inline]
    pub fn reserve_partial(self, huge: bool, min: usize, span: usize) -> Option<Entry3> {
        if !self.reserved()
            && self.free() > min
            && self.free() < span
            && self.huge() == huge
        {
            Some(self.with_huge(huge).with_reserved(true).with_free(0))
        } else {
            None
        }
    }
    /// Reserves this entry if it is completely empty.
    #[inline]
    pub fn reserve_empty(self, huge: bool, span: usize) -> Option<Entry3> {
        if !self.reserved() && self.free() == span {
            Some(self.with_huge(huge).with_reserved(true).with_free(0))
        } else {
            None
        }
    }
    /// Clears the reserve flag of this entry.
    #[inline]
    pub fn unreserve(self) -> Option<Entry3> {
        if self.reserved() {
            Some(self.with_reserved(false))
        } else {
            None
        }
    }
    /// Add the pages from the `other` entry to the reserved `self` entry and unreserve it.
    /// `self` is the entry in the global array / table.
    #[inline]
    pub fn unreserve_add(
        self,
        huge: bool,
        other: Entry3,
        max: usize,
        span: usize,
    ) -> Option<Entry3> {
        let pages = self.free() + other.free();
        if self.reserved()
            && pages <= max
            && (self.huge() == huge || self.free() == span)
            && (other.huge() == huge || other.free() == span)
        {
            Some(
                self.with_free(pages)
                    .with_huge(huge && pages < max)
                    .with_reserved(false),
            )
        } else {
            error!("{self:?} + {other:?}, {pages} <= {max}");
            None
        }
    }
}

impl fmt::Debug for Entry3 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Entry3")
            .field("free", &self.free())
            .field("idx", &self.idx())
            .field("reserved", &self.reserved())
            .field("huge", &self.huge())
            .finish()
    }
}

#[bitfield(u64)]
pub struct Entry2 {
    /// Number of free pages.
    #[bits(54)]
    pub free: usize,
    /// Index of the level one page table.
    #[bits(9)]
    pub i1: usize,
    /// If this is allocated as large page.
    pub page: bool,
}

impl AtomicValue for Entry2 {
    type V = u64;
}

impl Entry2 {
    /// Creates a new entry referencing a level one page table.
    #[inline]
    pub fn new_table(pages: usize, i1: usize) -> Self {
        Self::new().with_free(pages).with_i1(i1)
    }
    #[inline]
    pub fn mark_huge(self, span: usize) -> Option<Self> {
        if !self.page() && self.free() == span {
            Some(Entry2::new().with_page(true))
        } else {
            None
        }
    }
    /// Decrement the free pages counter.
    #[inline]
    pub fn dec(self, i1: usize) -> Option<Self> {
        if !self.page() && self.i1() == i1 && self.free() > 0 {
            Some(self.with_free(self.free() - 1))
        } else {
            None
        }
    }
    /// Increments the free pages counter.
    #[inline]
    pub fn inc_partial(self, i1: usize, span: usize) -> Option<Self> {
        if !self.page()
            && i1 == self.i1()
            && self.free() > 0 // is the child pt already initialized?
            && self.free() < span
        {
            Some(self.with_free(self.free() + 1))
        } else {
            None
        }
    }
    /// Increments the free pages counter.
    #[inline]
    pub fn inc(self, span: usize) -> Option<Self> {
        if !self.page() && self.free() < span {
            Some(self.with_free(self.free() + 1))
        } else {
            None
        }
    }
}

impl fmt::Debug for Entry2 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Entry2")
            .field("free", &self.free())
            .field("i1", &self.i1())
            .field("page", &self.page())
            .finish()
    }
}

#[bitfield(u16)]
pub struct SmallEntry2 {
    /// Number of free pages.
    #[bits(15)]
    pub free: usize,
    /// If this is allocated as large page.
    pub page: bool,
}

impl AtomicValue for SmallEntry2 {
    type V = u16;
}

impl SmallEntry2 {
    /// Creates a new entry referencing a level one page table.
    #[inline]
    pub fn new_table(pages: usize) -> Self {
        Self::new().with_free(pages)
    }
    #[inline]
    pub fn mark_huge(self, span: usize) -> Option<Self> {
        if !self.page() && self.free() == span {
            Some(Self::new().with_page(true))
        } else {
            None
        }
    }
    /// Decrement the free pages counter.
    #[inline]
    pub fn dec(self) -> Option<Self> {
        if !self.page() && self.free() > 0 {
            Some(self.with_free(self.free() - 1))
        } else {
            None
        }
    }
    /// Increments the free pages counter.
    #[inline]
    pub fn inc_partial(self, span: usize) -> Option<Self> {
        if !self.page()
            && self.free() > 0 // is the child pt already initialized?
            && self.free() < span
        {
            Some(self.with_free(self.free() + 1))
        } else {
            None
        }
    }
    /// Increments the free pages counter.
    #[inline]
    pub fn inc(self, span: usize) -> Option<Self> {
        if !self.page() && self.free() < span {
            Some(self.with_free(self.free() + 1))
        } else {
            None
        }
    }
}

impl fmt::Debug for SmallEntry2 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Entry2")
            .field("free", &self.free())
            .field("page", &self.page())
            .finish()
    }
}

/// Level 1 page table entry
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
#[repr(u64)]
pub enum Entry1 {
    Empty = 0,
    Page = 1,
}

impl AtomicValue for Entry1 {
    type V = u64;
}

impl From<u64> for Entry1 {
    fn from(v: u64) -> Self {
        match v {
            0 => Entry1::Empty,
            _ => Entry1::Page,
        }
    }
}

impl From<Entry1> for u64 {
    fn from(v: Entry1) -> u64 {
        v as u64
    }
}

#[cfg(all(test, feature = "std"))]
mod test {
    use crate::table::{ATable, PT_LEN};

    #[test]
    fn pt() {
        type Table = ATable<u64, PT_LEN>;

        let pt: Table = Table::empty();
        pt.cas(0, 0, 42).unwrap();
        pt.update(0, |v| Some(v + 1)).unwrap();
        assert_eq!(pt.get(0), 43);
    }
}
