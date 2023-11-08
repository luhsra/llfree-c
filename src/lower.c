#include "lower.h"
#include "bitfield.h"
#include "child.h"

void lower_init(lower_t *self, uint64_t offset, size_t len, uint8_t init)
{
	self->offset = offset;
	self->frames = len;

	self->childs_len = div_ceil(self->frames, CHILD_SIZE);

	if (init == INIT_VOLATILE) {
		self->fields = llc_ext_alloc(
			CACHE_SIZE, sizeof(bitfield_t) * self->childs_len);
		assert(self->fields != NULL);

		self->children = llc_ext_alloc(
			CACHE_SIZE, sizeof(child_t) * self->childs_len);
		assert(self->children != NULL);

	} else {
		// Layout in persistent Memory:
		// |---------------+----------+-----------+----------|
		// |     Frames    | children | bitfields | metadata |
		// |---------------+----------+-----------+----------|

		uint64_t size_bitfields = self->childs_len * sizeof(bitfield_t);
		size_bitfields = align_up(size_bitfields,
					  TREE_CHILDREN * sizeof(child_t));
		uint64_t size_children = self->childs_len * sizeof(child_t);
		// round up to get complete cacheline
		size_children = align_up(size_children,
					 MAX(CACHE_SIZE, sizeof(bitfield_t)));

		uint64_t metadata_bytes = size_bitfields + size_children;
		uint64_t metadata_pages = div_ceil(metadata_bytes, FRAME_SIZE);

		assert(metadata_pages < self->frames);

		self->frames -= metadata_pages;
		self->childs_len = div_ceil(self->frames, CHILD_SIZE);

		uint64_t metadata_start =
			(self->offset + self->frames) * FRAME_SIZE;

		self->children = (_Atomic(child_t) *)metadata_start;
		self->fields = (bitfield_t *)(metadata_start + size_children);
	}
}

void lower_clear(lower_t *self, bool free_all)
{
	assert(self != NULL);

	for (size_t i = 0; i < self->childs_len - 1; i++) {
		self->children[i] =
			child_new(free_all ? CHILD_SIZE : 0, !free_all);
		bitfield_t *field = &self->fields[i];
		for (uint64_t j = 0; j < FIELD_N; ++j) {
			*((uint64_t *)&field->rows[j]) = 0;
		}
	}

	uint64_t rest_frames = self->frames % CHILD_SIZE;
	if (rest_frames == 0)
		rest_frames = CHILD_SIZE;

	self->children[self->childs_len - 1] =
		free_all ? child_new(rest_frames, false) :
			   child_new(0, rest_frames == CHILD_SIZE);

	bitfield_t *field = &self->fields[self->childs_len - 1];
	uint64_t val = free_all ? 0 : UINT64_MAX;
	for (uint64_t j = 0; j < FIELD_N; ++j) {
		if (rest_frames >= ATOMIC_SIZE) {
			*((uint64_t *)&field->rows[j]) = val;
			rest_frames -= ATOMIC_SIZE;
		} else if (rest_frames > 0) {
			val = free_all ? UINT64_MAX << rest_frames : UINT64_MAX;
			*((uint64_t *)&field->rows[j]) = val;
			rest_frames = 0;
		} else {
			*((uint64_t *)&field->rows[j]) = UINT64_MAX;
		}
	}
}

result_t lower_recover(lower_t *self)
{
	for (size_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->children[i]);
		if (child.huge) {
			// if this was reserved as Huge Page set counter and all frames to 0
			atom_store(&self->children[i], child_new(0, true));
			field_init(&self->fields[i]);
		} else {
			// not a Huge Page -> count free Frames and set as counter
			uint16_t counter =
				CHILD_SIZE - field_count_bits(&self->fields[i]);
			atom_store(&self->children[i],
				   child_new(counter, false));
		}
	}

	return result(ERR_OK);
}

static result_t get_max(lower_t *self, uint64_t pfn)
{
	assert(self != 0);

	size_t idx = child_from_pfn(pfn) / 2;
	for_offsetted(idx, TREE_CHILDREN / 2)
	{
		if (current_i * 2 + 1 >= self->childs_len)
			continue;

		child_pair_t old;
		if (atom_update((_Atomic(child_pair_t) *)&self
					->children[current_i * 2],
				old, child_reserve_max)) {
			return result(pfn_from_child(current_i * 2));
		}
	}

	return result(ERR_MEMORY);
}

static result_t get_huge(lower_t *self, uint64_t pfn)
{
	assert(self != 0);

	size_t idx = child_from_pfn(pfn);
	for_offsetted(idx, TREE_CHILDREN)
	{
		if (current_i >= self->childs_len)
			continue;

		child_t old;
		if (atom_update(&self->children[current_i], old,
				child_reserve_huge)) {
			return result(pfn_from_child(current_i));
		}
	}

	return result(ERR_MEMORY);
}

result_t lower_get(lower_t *self, const uint64_t start_frame,
		   const size_t order)
{
	assert(order <= MAX_ORDER);
	assert(start_frame < self->frames);

	if (order == MAX_ORDER)
		return get_max(self, start_frame);
	if (order == HP_ORDER)
		return get_huge(self, start_frame);

	const size_t start_idx = child_from_pfn(start_frame);

	for_offsetted(start_idx, TREE_CHILDREN)
	{
		if (current_i >= self->childs_len)
			continue;

		child_t old;
		if (atom_update(&self->children[current_i], old, child_dec,
				order)) {
			result_t pos = field_set_next(&self->fields[current_i],
						      start_frame, order);
			if (result_ok(pos)) {
				return result(pfn_from_child(current_i) +
					      pos.val);
			}

			if (!atom_update(&self->children[current_i], old,
					 child_inc, order)) {
				return result(ERR_CORRUPTION);
			}
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
	assert(order <= MAX_ORDER);
	if (frame + (1 << order) > self->frames || frame % (1 << order) != 0)
		return result(ERR_ADDRESS);

	const size_t child_idx = child_from_pfn(frame);
	_Atomic(child_t) *child = &self->children[child_idx];

	if (order == MAX_ORDER) {
		child_pair_t old = { child_new(0, true), child_new(0, true) };
		child_pair_t new = { child_new(CHILD_SIZE, false),
				     child_new(CHILD_SIZE, false) };
		return atom_cmp_exchange((_Atomic(child_pair_t) *)child, &old,
					 new) ?
			       result(ERR_OK) :
			       result(ERR_ADDRESS);
	}
	if (order == HP_ORDER) {
		child_t old = child_new(0, true);
		child_t new = child_new(CHILD_SIZE, false);
		return atom_cmp_exchange(child, &old, new) ?
			       result(ERR_OK) :
			       result(ERR_ADDRESS);
	}

	bitfield_t *field = &self->fields[child_idx];

	child_t old = atom_load(child);
	if (old.huge) {
		split_huge(child, field);
	}

	size_t field_index = frame % CHILD_SIZE;
	result_t ret = field_toggle(field, field_index, order, true);
	if (!result_ok(ret))
		return ret;

	if (!atom_update(child, old, child_inc, order)) {
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

	child_t child = atom_load(&self->children[child_index]);

	if (order == HP_ORDER) {
		return (!child.huge && child.free == CHILD_SIZE);
	}

	size_t field_index = frame % CHILD_SIZE;

	if (child.free < (1 << order))
		return false;

	return field_is_free(&self->fields[child_index], field_index);
}

size_t lower_free_frames(lower_t *self)
{
	size_t counter = 0;
	for (size_t i = 0; i < self->childs_len; i++) {
		child_t child = atom_load(&self->children[i]);
		counter += child.free;
	}
	return counter;
};

void lower_print(lower_t *self)
{
	printf("\n-------------------------------------\nLOWER ALLOCATOR\n"
	       "childs\n%ju/%ju frames are allocated\n%ju/%ju Huge Frames are "
	       "free\nChilds:\n",
	       lower_free_frames(self), self->frames, lower_free_huge(self),
	       self->childs_len);
	if (self->childs_len > 20)
		printf("There are over 20 Childs. Print will only contain first and last "
		       "10\n\n");
	printf("Nr:\t\t");
	for (size_t i = 0; i < self->childs_len; ++i) {
		if (i < 10 || i >= self->childs_len - 10)
			printf("%ju\t", i);
	}
	printf("\nHp?:\t\t");
	for (size_t i = 0; i < self->childs_len; ++i) {
		if (i < 10 || i >= self->childs_len - 10) {
			child_t child = atom_load(&self->children[i]);
			printf("%d\t", child.huge);
		}
	}
	printf("\nfree:\t\t");
	for (size_t i = 0; i < self->childs_len; ++i) {
		if (i < 10 || i >= self->childs_len - 10) {
			child_t child = atom_load(&self->children[i]);
			printf("%d\t", child.free);
		}
	}
	printf("\n");
}

void lower_drop(lower_t *self)
{
	assert(self != NULL);
	_Atomic(child_t) *start_data =
		(void *)((self->offset + self->frames) * FRAME_SIZE);
	if (self->children != start_data) {
		llc_ext_free(CACHE_SIZE, sizeof(child_t) * self->childs_len,
			     self->children);
		llc_ext_free(CACHE_SIZE, sizeof(bitfield_t) * self->childs_len,
			     self->fields);
	}
}

size_t lower_free_huge(lower_t *self)
{
	size_t count = 0;
	for (size_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->children[i]);
		if (child.free == CHILD_SIZE)
			++count;
	}
	return count;
}

void lower_for_each_child(const lower_t *self, void *context,
			  void f(void *, uint64_t, uint64_t))
{
	for (uint64_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->children[i]);
		f(context, self->offset + i * (1 << HP_ORDER), child.free);
	}
}
