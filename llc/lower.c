#include "lower.h"
#include "bitfield.h"
#include "child.h"
#include "llc.h"
#include "utils.h"
#include <assert.h>
#include <stdint.h>

void lower_init(lower_t *const self, uint64_t offset, size_t len, uint8_t init)
{
	self->offset = offset;
	self->frames = len;

	self->childs_len = div_ceil(self->frames, FIELDSIZE);

	if (init == VOLATILE) {
		self->fields = llc_ext_alloc(
			CACHESIZE, sizeof(bitfield_t) * self->childs_len);
		assert(self->fields != NULL);

		self->childs = llc_ext_alloc(
			CACHESIZE, sizeof(child_t) * self->childs_len);
		assert(self->childs != NULL);

	} else {
		// Layout in persistent Memory:
		// |-----------------------+----------+-----------+-----------+----------|
		// |     Managed Frames    | children | bitfields | (padding) | metadata |
		// |-----------------------+----------+-----------+-----------+----------|

		uint64_t bitfield_bytes = self->childs_len * sizeof(bitfield_t);

		uint64_t child_bytes = self->childs_len * sizeof(child_t);
		// round up to get complete cacheline
		child_bytes = div_ceil(child_bytes, CACHESIZE) * CACHESIZE;

		uint64_t meta_bytes = CACHESIZE;

		uint64_t metadata_bytes =
			bitfield_bytes + child_bytes + meta_bytes;
		uint64_t metadata_pages = div_ceil(metadata_bytes, PAGESIZE);

		assert(metadata_pages < self->frames);

		self->frames -= metadata_pages;
		self->childs_len = div_ceil(self->frames, FIELDSIZE);

		uint64_t metadata_start =
			(self->offset + self->frames) * PAGESIZE;

		self->childs = (_Atomic(child_t) *)metadata_start;
		self->fields = (bitfield_t *)(metadata_start + child_bytes);

		assert((uint64_t)&self->childs[0] % CACHESIZE == 0);
		assert((uint64_t)&self->fields[0] % CACHESIZE == 0);
		assert((uint64_t)&self->childs[self->childs_len] <
		       (self->offset + len) * PAGESIZE);
		assert((uint64_t)&self->childs[self->childs_len] >
		       self->offset * PAGESIZE);
		assert((uint64_t)&self->fields[self->childs_len] <
		       (self->offset + len) * PAGESIZE);
		assert((uint64_t)&self->fields[self->childs_len] >
		       self->offset * PAGESIZE);
	}
}

result_t lower_clear(lower_t *self, bool all_free)
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
	uint64_t rest_frames = self->frames % FIELDSIZE;
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

static result_t get_huge(lower_t *self, uint64_t pfn)
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
			return result(pfn_from_child(current_i));
		}
	}

	return result(ERR_MEMORY);
}

result_t lower_get(lower_t *self, const uint64_t start_pfn, const size_t order)
{
	assert(order == 0 || order == HP_ORDER);
	assert(start_pfn < self->frames);

	if (order == HP_ORDER)
		return get_huge(self, start_pfn);

	const size_t start_idx = child_from_pfn(start_pfn);

	for_offsetted(start_idx, CHILDS_PER_TREE)
	{
		if (current_i >= self->childs_len)
			continue;

		child_t old;
		if (atom_update(&self->childs[current_i], old, VOID,
				child_counter_dec)) {
			result_t pos = field_set_next(&self->fields[current_i],
						      start_pfn);
			if (!result_ok(pos))
				return result(ERR_CORRUPTION); // TODO: undo

			return result(pfn_from_child(current_i) + pos.val);
		}
	}

	return result(ERR_MEMORY);
}

static void split_huge(_Atomic(child_t) *child, bitfield_t *field)
{
	uint64_t zero = 0;

	// synchronize multiple threads on the first row
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

result_t lower_put(lower_t *self, uint64_t frame, size_t order)
{
	assert(order == 0 || order == HP_ORDER);
	if (frame >= self->frames)
		return result(ERR_ADDRESS);

	const size_t child_idx = child_from_pfn(frame);
	_Atomic(child_t) *child = &self->childs[child_idx];
	bitfield_t *field = &self->fields[child_idx];

	if (order == HP_ORDER) {
		child_t old = child_new(0, true);
		child_t new = child_new(CHILDSIZE, false);
		return atom_cmp_exchange(child, &old, new) ?
			       result(ERR_OK) :
			       result(ERR_ADDRESS);
	}

	child_t old = atom_load(child);
	if (old.huge) {
		split_huge(child, field);
	}

	size_t field_index = frame % FIELDSIZE;
	result_t ret = field_reset_bit(field, field_index);
	if (!result_ok(ret))
		return ret;

	if (!atom_update(child, old, VOID, child_counter_inc)) {
		assert(!"should never be possible");
		return result(ERR_CORRUPTION);
	}

	return result(ERR_OK);
}

bool lower_is_free(lower_t *self, uint64_t frame, size_t order)
{
	assert(order == 0 || order == HP_ORDER);
	// check if outside of managed space
	if (frame >= self->frames)
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

size_t lower_free_frames(lower_t *self)
{
	size_t counter = 0;
	for (size_t i = 0; i < self->childs_len; i++) {
		child_t child = atom_load(&self->childs[i]);
		counter += child.counter;
	}
	return counter;
};

void lower_print(lower_t *self)
{
	printf("\n-------------------------------------\nLOWER ALLOCATOR\n"
	       "childs\n%lu/%lu frames are allocated\n%lu/%lu Huge Frames are "
	       "free\nChilds:\n",
	       lower_free_frames(self), self->frames, lower_free_huge(self),
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

void lower_drop(lower_t *self)
{
	assert(self != NULL);
	_Atomic(child_t) *start_data =
		(void *)((self->offset + self->frames) * PAGESIZE);
	if (self->childs != start_data) {
		llc_ext_free(CACHESIZE, sizeof(child_t) * self->childs_len,
			     self->childs);
		llc_ext_free(CACHESIZE, sizeof(bitfield_t) * self->childs_len,
			     self->fields);
	}
}

size_t lower_free_huge(lower_t *self)
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
	for (uint64_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->childs[i]);
		f(context, self->offset + i * (1 << HP_ORDER), child.counter);
	}
}
