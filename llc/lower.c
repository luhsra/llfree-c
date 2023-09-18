#include "lower.h"
#include "bitfield.h"
#include "child.h"
#include "local.h"
#include "utils.h"
#include <assert.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <stdio.h>

void lower_init_default(lower_t *const self, uint64_t start_frame_adr,
			size_t len, uint8_t init)
{
	self->start_frame_adr = start_frame_adr;
	self->length = len;

	self->childs_len = div_ceil(self->length, FIELDSIZE);

	if (init == VOLATILE) {
		self->fields = aligned_alloc(
			CACHESIZE, sizeof(bitfield_t) * self->childs_len);
		assert(self->fields != NULL);

		self->childs = aligned_alloc(
			CACHESIZE, sizeof(child_t) * self->childs_len);
		assert(self->childs != NULL);

	} else {
		// Layout in persistent Memory:
		// |-----------------------------------------------------------------------|
		// |      Managed Frames     | childs |  bitfields  | (padding) | metadata |
		// |-----------------------------------------------------------------------|
		// padding is unused memory of a size smaller than a Frame.
		// its clippings, because we can not hand out less than a full frame.

		uint64_t bytes_for_Bitfields =
			self->childs_len * sizeof(bitfield_t);

		uint64_t bytes_for_childs = self->childs_len * sizeof(child_t);
		// round up to get complete cacheline
		bytes_for_childs =
			div_ceil(bytes_for_childs, CACHESIZE) * CACHESIZE;

		uint64_t bytes_for_meta = CACHESIZE;
		// round up to get complete cacheline

		uint64_t memory_for_control_structures =
			bytes_for_Bitfields + bytes_for_childs + bytes_for_meta;

		uint64_t pages_needed =
			div_ceil(memory_for_control_structures, PAGESIZE);
		assert(pages_needed < self->length);
		self->length -= pages_needed;
		self->childs_len = div_ceil(self->length, FIELDSIZE);

		uint64_t start_data =
			(self->start_frame_adr + self->length * PAGESIZE);

		self->childs = (_Atomic(child_t) *)start_data;
		self->fields = (bitfield_t *)(start_data + bytes_for_childs);

		assert((uint64_t)&self->childs[0] % CACHESIZE == 0);
		assert((uint64_t)&self->fields[0] % CACHESIZE == 0);
		assert((uint64_t)&self->childs[self->childs_len] <
		       self->start_frame_adr + len * PAGESIZE);
		assert((uint64_t)&self->childs[self->childs_len] >
		       self->start_frame_adr);
		assert((uint64_t)&self->fields[self->childs_len] <
		       self->start_frame_adr + len * PAGESIZE);
		assert((uint64_t)&self->fields[self->childs_len] >
		       self->start_frame_adr);

		//printf("region\tstart\tend\n");
		//printf("memory\t%lX\t%lX\n", self->start_frame_adr, self->start_frame_adr + len * PAGESIZE);
		//printf("childs\t%lX\t%lX\n", (uint64_t)&self->childs[0], (uint64_t)&self->childs[self->num_of_childs] + sizeof(child_t));
		//printf("fields\t%lX\t%lX\n", (uint64_t)&self->fields[0], (uint64_t)&self->fields[self->num_of_childs] + sizeof(bitfield_t));
	}
}

result_t lower_init(lower_t const *const self, bool all_free)
{
	assert(self != NULL);

	uint64_t *field = (uint64_t *)&self->fields[0];
	for (size_t i = 0; i < self->childs_len - 1; i++) {
		self->childs[i] =
			child_new(all_free ? FIELDSIZE : 0, !all_free);
		for (uint64_t j = 0; j < 8; ++j) {
			field[i * 8 + j] = 0;
		}
	}

	uint64_t last_child_idx = self->childs_len - 1;
	uint64_t rest_frames = self->length % FIELDSIZE;
	if (rest_frames == 0)
		rest_frames = FIELDSIZE;

	self->childs[self->childs_len - 1] =
		all_free ? child_new(rest_frames, false) :
			   child_new(0, rest_frames == FIELDSIZE);

	uint64_t val = all_free ? 0 : UINT64_MAX;
	for (uint64_t j = 0; j < 8; ++j) {
		if (rest_frames >= ATOMICSIZE) {
			field[last_child_idx * 8 + j] = val;
			rest_frames -= ATOMICSIZE;
			continue;
		}
		if (rest_frames > 0) {
			val = all_free ? UINT64_MAX << rest_frames : UINT64_MAX;
			field[last_child_idx * 8 + j] = val;
			rest_frames = 0;
			continue;
		}
		field[last_child_idx * 8 + j] = UINT64_MAX;
	}
	return result(ERR_OK);
}

result_t lower_recover(lower_t *self)
{
	for (size_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->childs[i]);
		if (child.huge) {
			// if this was reserved as Huge Page set counter and all frames to 0
			atom_store(&self->childs[i], child_new(0, true));
			field_init(&self->fields[i]);
		} else {
			// not a Huge Page -> count free Frames and set as counter
			uint16_t counter =
				FIELDSIZE - field_count_bits(&self->fields[i]);
			atom_store(&self->childs[i], child_new(counter, false));
		}
	}

	return result(ERR_OK);
}

result_t get_HP(lower_t const *const self, uint64_t pfn)
{
	assert(self != 0);

	size_t idx = child_from_pfn(pfn);

	for_offsetted(idx, CHILDS_PER_TREE)
	{
		if (current_i >= self->childs_len)
			continue;

		child_t old;
		if (atom_update(&self->childs[current_i], old, VOID,
				child_reserve_huge)) {
			return result(pfn_from_child(current_i) +
				      self->start_frame_adr);
		}
	}

	return result(ERR_MEMORY);
}

/**
 * @brief searches and reserves a frame in Bitfield with with index
 *
 * @param self pointer to lower
 * @param idx index of the Bitfield that is searched
 * @return address of the reserved frame on success
 *         ERR_MEMORY if no free Frame were found
 */
static int64_t reserve_in_Bitfield(const lower_t *self,
				   const uint64_t child_idx, const uint64_t pfn)
{
	result_t pos = field_set_Bit(&self->fields[child_idx], pfn);
	if (result_ok(pos)) {
		// found and reserved a frame
		return pfn_from_child(child_idx) + pos.val +
		       self->start_frame_adr;
	}
	return ERR_MEMORY;
}

result_t lower_get(lower_t const *const self, const uint64_t pfn,
		   const size_t order)
{
	assert(order == 0 || order == HP_ORDER);
	assert(pfn < self->length);

	if (order == HP_ORDER)
		return get_HP(self, pfn);

	const size_t start_idx = child_from_pfn(pfn);

	for_offsetted(start_idx, CHILDS_PER_TREE)
	{
		if (current_i >= self->childs_len)
			continue;

		child_t old;
		if (atom_update(&self->childs[current_i], old, VOID,
				child_counter_dec)) {
			int64_t pfn_adr =
				reserve_in_Bitfield(self, current_i, pfn);

			assert(pfn_adr >= 0 &&
			       "because of the counter in child there must "
			       "always be a frame left");
			if (pfn_adr == ERR_MEMORY)
				return result(ERR_CORRUPTION);
			assert(tree_from_pfn(pfn_adr - self->start_frame_adr) ==
			       tree_from_pfn(pfn));
			return result(pfn_adr);
		}
	}

	return result(ERR_MEMORY);
}

void convert_huge_to_regular(_Atomic(child_t) *child, bitfield_t *field)
{
	uint64_t zero = 0;
	bool success = atom_cmp_exchange(&field->rows[0], &zero, UINT64_MAX);

	if (success) {
		for (size_t i = 1; i < FIELD_N; ++i) {
			atom_store(&field->rows[i], UINT64_MAX);
		}

		child_t expected = child_new(0, true);
		success = atom_cmp_exchange(child, &expected,
					    child_new(0, false));
		assert(success);
	} else {
		info("wait");
		// another thread ist trying to breakup this HP
		// -> wait for their completion
		while (({
			child_t c = atom_load(child);
			c.huge;
		})) {
			spin_wait();
		}
	}
}

result_t lower_put(lower_t const *const self, uint64_t frame_adr, size_t order)
{
	assert(order == 0 || order == HP_ORDER);

	// check if outside of managed space
	if (frame_adr >= self->start_frame_adr + self->length ||
	    frame_adr < self->start_frame_adr)
		return result(ERR_ADDRESS);
	uint64_t frame_number = frame_adr - self->start_frame_adr;
	const size_t child_index = child_from_pfn(frame_number);
	_Atomic(child_t) *child = &self->childs[child_index];
	bitfield_t *field = &self->fields[child_index];

	child_t old;
	if (order == HP_ORDER) {
		return atom_update(child, old, VOID, child_free_huge) ?
			       result(ERR_OK) :
			       result(ERR_ADDRESS);
	}

	old = atom_load(child);
	if (old.huge) {
		convert_huge_to_regular(child, field);
	}

	size_t field_index = (frame_number) % FIELDSIZE;
	result_t ret = field_reset_bit(field, field_index);
	if (!result_ok(ret))
		return ret;

	if (!atom_update(child, old, VOID, child_counter_inc)) {
		assert(!"should never be possible");
	}

	return result(ERR_OK);
}

bool lower_is_free(lower_t const *const self, uint64_t frame, size_t order)
{
	assert(order == 0 || order == HP_ORDER);

	// check if outside of managed space
	if (frame >= self->start_frame_adr + self->length ||
	    frame < self->start_frame_adr)
		return false;

	size_t child_index = child_from_pfn(frame);

	child_t child = atom_load(&self->childs[child_index]);

	if (order == HP_ORDER) {
		return (!child.huge && child.counter == FIELDSIZE);
	}

	size_t field_index = frame % FIELDSIZE;

	if (child.counter < (1 << order))
		return false;

	return field_is_free(&self->fields[child_index], field_index);
}

size_t lower_allocated_frames(lower_t const *const self)
{
	size_t counter = self->length;
	for (size_t i = 0; i < self->childs_len; i++) {
		child_t child = atom_load(&self->childs[i]);
		counter -= child.counter;
	}
	return counter;
};

void lower_print(lower_t const *const self)
{
	printf("\n-------------------------------------\nLOWER ALLOCATOR\n"
	       "childs\n%lu/%lu frames are allocated\n%lu/%lu Huge Frames are "
	       "free\nChilds:\n",
	       lower_allocated_frames(self), self->length, lower_free_HPs(self),
	       self->childs_len);
	if (self->childs_len > 20)
		printf("There are over 20 Childs. Print will only contain first and last "
		       "10\n\n");
	printf("Nr:\t\t");
	for (size_t i = 0; i < self->childs_len; ++i) {
		if (i < 10 || i >= self->childs_len - 10)
			printf("%lu\t", i);
	}
	printf("\nHp?:\t\t");
	for (size_t i = 0; i < self->childs_len; ++i) {
		if (i < 10 || i >= self->childs_len - 10) {
			child_t child = atom_load(&self->childs[i]);
			printf("%d\t", child.huge);
		}
	}
	printf("\nfree:\t\t");
	for (size_t i = 0; i < self->childs_len; ++i) {
		if (i < 10 || i >= self->childs_len - 10) {
			child_t child = atom_load(&self->childs[i]);
			printf("%d\t", child.counter);
		}
	}
	printf("\n");
}

void lower_drop(lower_t const *const self)
{
	assert(self != NULL);
	_Atomic(child_t) *start_data =
		(_Atomic(child_t) *)(self->start_frame_adr +
				     self->length * PAGESIZE);
	if (self->childs != start_data) {
		free(self->childs);
		free(self->fields);
	}
}

size_t lower_free_HPs(lower_t const *const self)
{
	size_t count = 0;
	for (size_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->childs[i]);
		if (child.counter == CHILDSIZE)
			++count;
	}
	return count;
}

void lower_for_each_child(const lower_t *self, void *context,
			  void f(void *, uint64_t, uint64_t))
{
	uint64_t pfn = self->start_frame_adr;
	for (uint64_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->childs[i]);
		pfn += 512;
		f(context, pfn, child.counter);
	}
}
