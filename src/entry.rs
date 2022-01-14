use core::fmt;
use std::cmp::Ordering;

use bitfield_struct::bitfield;

use crate::table::Table;
use crate::Size;

#[bitfield(u64)]
pub struct Entry {
    #[bits(20)]
    pub full: usize,
    #[bits(20)]
    pub partial_l0: usize,
    #[bits(20)]
    pub partial_l1: usize,
    #[bits(4)]
    _p: usize,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Dec {
    None,
    Full,
    FullPartialL0,
    FullPartialL1,
    PartialL0,
    PartialL1,
}

impl Entry {
    pub fn reserve_partial(self, size: Size, max: usize) -> Option<Self> {
        if self.full() < max {
            match size {
                Size::L0 if self.partial_l0() > 0 => Some(
                    self.with_full(self.full() + 1)
                        .with_partial_l0(self.partial_l0() - 1),
                ),
                Size::L1 if self.partial_l1() > 0 => Some(
                    self.with_full(self.full() + 1)
                        .with_partial_l1(self.partial_l1() - 1),
                ),
                _ => None,
            }
        } else {
            None
        }
    }
    pub fn inc_full(self, max: usize) -> Option<Self> {
        if self.full() < max {
            Some(self.with_full(self.full() + 1))
        } else {
            None
        }
    }
    pub fn dec(self, dec: Dec) -> Option<Self> {
        match dec {
            Dec::Full if self.full() > 0 => Some(self.with_full(self.full() - 1)),
            Dec::PartialL0 if self.partial_l0() > 0 => {
                Some(self.with_partial_l0(self.partial_l0() - 1))
            }
            Dec::PartialL1 if self.partial_l1() > 0 => {
                Some(self.with_partial_l1(self.partial_l1() - 1))
            }
            Dec::FullPartialL0 if self.full() > 0 => Some(
                self.with_full(self.full() - 1)
                    .with_partial_l0(self.partial_l0() + 1),
            ),
            Dec::FullPartialL1 if self.full() > 0 => Some(
                self.with_full(self.full() - 1)
                    .with_partial_l1(self.partial_l1() + 1),
            ),
            _ => None,
        }
    }
}

impl fmt::Debug for Entry {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Entry")
            .field("full", &self.full())
            .field("partial_l0", &self.partial_l0())
            .field("partial_l1", &self.partial_l1())
            .finish()
    }
}

#[bitfield(u64)]
pub struct Entry3 {
    #[bits(20)]
    pub pages: usize,
    #[bits(2)]
    pub size_n: u8,
    pub reserved: bool,
    #[bits(41)]
    pub idx: usize,
}

impl Entry3 {
    #[inline(always)]
    pub fn new_giant() -> Entry3 {
        Entry3::new().with_size_n(3)
    }
    #[inline(always)]
    pub fn new_table(pages: usize, size: Size, reserved: bool) -> Entry3 {
        Entry3::new()
            .with_pages(pages)
            .with_size_n(size as u8 + 1)
            .with_reserved(reserved)
    }
    pub fn size(self) -> Option<Size> {
        let s = self.size_n();
        match s {
            1..=3 => Some(unsafe { std::mem::transmute(s - 1) }),
            _ => None,
        }
    }
    /// Increments the pages counter and sets the size and reserved bits.
    #[inline(always)]
    pub fn inc(self, size: Size, max: usize) -> Option<Entry3> {
        if self.size_n() == 3 || (self.pages() != 0 && self.size() != Some(size)) {
            return None;
        }

        let pages = self.pages() + Table::span(size as usize);
        if pages <= Table::span(2) && pages <= max {
            Some(
                self.with_pages(pages)
                    .with_size_n(size as u8 + 1)
                    .with_reserved(true),
            )
        } else {
            None
        }
    }
    /// Increments the pages counter and sets the size and reserved bits.
    /// Requires the current entry to not be reserved.
    #[inline(always)]
    pub fn inc_reserve(self, size: Size, max: usize) -> Option<Entry3> {
        if self.reserved() || self.size_n() == 3 || (self.pages() != 0 && self.size() != Some(size))
        {
            return None;
        }

        let pages = self.pages() + Table::span(size as usize);
        if pages <= Table::span(2) && pages <= max {
            Some(
                self.with_pages(pages)
                    .with_size_n(size as u8 + 1)
                    .with_reserved(true),
            )
        } else {
            None
        }
    }
    #[inline(always)]
    pub fn dec(self, size: Size) -> Option<Entry3> {
        if self.size_n() == 3 || self.size() != Some(size) {
            return None;
        }

        match self.pages().cmp(&Table::span(size as _)) {
            Ordering::Less => None,
            Ordering::Equal => Some(self.with_pages(0).with_size_n(0)),
            Ordering::Greater => Some(self.with_pages(self.pages() - Table::span(size as usize))),
        }
    }
    #[inline(always)]
    pub fn dec_idx(self, size: Size, idx: usize) -> Option<Entry3> {
        if self.size_n() == 3 || self.size() != Some(size) || self.idx() != idx {
            return None;
        }

        match self.pages().cmp(&Table::span(size as _)) {
            Ordering::Less => None,
            Ordering::Equal => Some(self.with_pages(0).with_size_n(0)),
            Ordering::Greater => Some(self.with_pages(self.pages() - Table::span(size as usize))),
        }
    }
    #[inline(always)]
    pub fn reserve(self, size: Size, max: usize) -> Option<Entry3> {
        if self.reserved() || self.size_n() == 3 || (self.pages() != 0 && self.size() != Some(size))
        {
            return None;
        }

        let pages = self.pages() + Table::span(size as usize);
        if pages <= Table::span(2) && pages <= max {
            Some(self.with_reserved(true))
        } else {
            None
        }
    }
    /// Sets the size and reserved bits and the page counter to it's max value.
    #[inline(always)]
    pub fn reserve_fill(self, size: Size, max: usize) -> Option<Entry3> {
        if self.size_n() == 3 || (self.pages() != 0 && self.size() != Some(size)) {
            return None;
        }

        let max = max.min(Table::span(2));
        if self.pages() + Table::span(size as usize) <= max {
            Some(
                self.with_pages(max)
                    .with_size_n(size as u8 + 1)
                    .with_reserved(true),
            )
        } else {
            None
        }
    }
    #[inline(always)]
    pub fn reserve_inc_partial(self, size: Size, max: usize) -> Option<Entry3> {
        if self.reserved() || self.pages() == 0 || self.size() != Some(size) {
            return None;
        }

        let pages = self.pages() + Table::span(size as _);
        if pages <= Table::span(2) && pages <= max {
            Some(self.with_reserved(true).with_pages(pages))
        } else {
            None
        }
    }
    #[inline(always)]
    pub fn reserve_inc_empty(self, size: Size, max: usize) -> Option<Entry3> {
        if !self.reserved()
            && self.size_n() != 3
            && self.pages() == 0
            && Table::span(size as usize) <= max
        {
            Some(
                self.with_size_n(size as u8 + 1)
                    .with_reserved(true)
                    .with_pages(Table::span(size as _)),
            )
        } else {
            None
        }
    }
    #[inline(always)]
    pub fn unreserve(self) -> Option<Entry3> {
        if self.size_n() != 3 && self.reserved() {
            Some(self.with_reserved(false).with_idx(0))
        } else {
            None
        }
    }
    /// Clear reserve flag and own free pages from `other`.
    #[inline(always)]
    pub fn unreserve_sub(self, other: Entry3, max: usize) -> Option<Entry3> {
        if self.reserved() && self.size_n() != 3 && self.size_n() == other.size_n() {
            let pages = other.pages() - (max.min(Table::span(2)) - self.pages());
            Some(self.with_pages(pages).with_reserved(false).with_idx(0))
        } else {
            None
        }
    }
}

impl fmt::Debug for Entry3 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Entry3")
            .field("pages", &self.pages())
            .field("size", &self.size())
            .field("reserved", &self.reserved())
            .field("idx", &self.idx())
            .finish()
    }
}

#[bitfield(u64)]
pub struct Entry2 {
    #[bits(10)]
    pub pages: usize,
    #[bits(9)]
    pub i1: usize,
    pub page: bool,
    pub giant: bool,
    #[bits(43)]
    _p: u64,
}

impl Entry2 {
    #[inline(always)]
    pub fn new_table(pages: usize, i1: usize) -> Self {
        Self::new().with_pages(pages).with_i1(i1)
    }
    #[inline(always)]
    pub fn mark_huge(self) -> Option<Self> {
        if !self.giant() && !self.page() && self.pages() == 0 {
            Some(Entry2::new().with_page(true))
        } else {
            None
        }
    }
    #[inline(always)]
    pub fn inc(self, i1: usize) -> Option<Self> {
        if !self.page() && !self.giant() && self.i1() == i1 && self.pages() < Table::LEN {
            Some(self.with_pages(self.pages() + 1))
        } else {
            None
        }
    }
    #[inline(always)]
    pub fn dec(self, i1: usize) -> Option<Self> {
        if !self.giant()
            && !self.page()
            && i1 == self.i1()
            && self.pages() > 0
            && self.pages() < Table::LEN
        {
            Some(self.with_pages(self.pages() - 1))
        } else {
            None
        }
    }
}

impl fmt::Debug for Entry2 {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Entry2")
            .field("pages", &self.pages())
            .field("i1", &self.i1())
            .field("page", &self.page())
            .field("giant", &self.giant())
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

impl From<u64> for Entry1 {
    fn from(v: u64) -> Self {
        assert!(v <= Self::Page as u64);
        unsafe { std::mem::transmute(v) }
    }
}

impl From<Entry1> for u64 {
    fn from(v: Entry1) -> u64 {
        v as u64
    }
}

#[cfg(test)]
mod test {
    use crate::table::Table;

    #[test]
    fn pt() {
        let pt: Table<u64> = Table::empty();
        pt.cas(0, 0, 42).unwrap();
        pt.update(0, |v| Some(v + 1)).unwrap();
        assert_eq!(pt.get(0), 43);
    }
}
