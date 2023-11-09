#include "lower.h"
#include "bitfield.h"
#include "child.h"

void lower_init(lower_t *self, uint64_t offset, size_t len, uint8_t init)
{
	assert(offset % (1 << (LLFREE_MAX_ORDER - LLFREE_HUGE_ORDER)) == 0);

	self->offset = offset;
	self->frames = len;

	self->childs_len = div_ceil(self->frames, LLFREE_CHILD_SIZE);

	if (init == LLFREE_INIT_VOLATILE) {
		self->fields =
			llfree_ext_alloc(LLFREE_CACHE_SIZE,
					 sizeof(bitfield_t) * self->childs_len);
		assert(self->fields != NULL);

		self->children = llfree_ext_alloc(
			LLFREE_CACHE_SIZE, sizeof(child_t) * self->childs_len);
		assert(self->children != NULL);

	} else {
		// Layout in persistent Memory:
		// |---------------+----------+-----------+----------|
		// |     Frames    | children | bitfields | metadata |
		// |---------------+----------+-----------+----------|

		uint64_t size_bitfields = self->childs_len * sizeof(bitfield_t);
		size_bitfields = align_up(
			size_bitfields, LLFREE_TREE_CHILDREN * sizeof(child_t));
		uint64_t size_children = self->childs_len * sizeof(child_t);
		// round up to get complete cacheline
		size_children =
			align_up(size_children,
				 MAX(LLFREE_TREE_CHILDREN * sizeof(child_t),
				     sizeof(bitfield_t)));

		uint64_t metadata_bytes = size_bitfields + size_children;
		uint64_t metadata_pages =
			div_ceil(metadata_bytes, LLFREE_FRAME_SIZE);

		assert(metadata_pages < self->frames);

		self->frames -= metadata_pages;
		self->childs_len = div_ceil(self->frames, LLFREE_CHILD_SIZE);

		uint64_t metadata_start =
			(self->offset + self->frames) * LLFREE_FRAME_SIZE;

		self->children = (_Atomic(child_t) *)metadata_start;
		self->fields = (bitfield_t *)(metadata_start + size_children);
	}
}

void lower_clear(lower_t *self, bool free_all)
{
	assert(self != NULL);

	for (size_t i = 0; i < self->childs_len - 1; i++) {
		self->children[i] =
			child_new(free_all ? LLFREE_CHILD_SIZE : 0, !free_all);

		bitfield_t *field = &self->fields[i];
		for (uint64_t j = 0; j < FIELD_N; ++j) {
			*((uint64_t *)&field->rows[j]) = 0;
		}
	}

	uint64_t rest_frames = self->frames % LLFREE_CHILD_SIZE;
	if (rest_frames == 0)
		rest_frames = LLFREE_CHILD_SIZE;

	self->children[self->childs_len - 1] =
		free_all ? child_new(rest_frames, false) :
			   child_new(0, rest_frames == LLFREE_CHILD_SIZE);

	// we have a few more unused children, which simplifies iterating over them
	for (size_t i = self->childs_len;
	     i < align_up(self->childs_len, LLFREE_TREE_CHILDREN); i++) {
		self->children[i] = child_new(0, false);
	}

	bitfield_t *field = &self->fields[self->childs_len - 1];
	uint64_t val = free_all ? 0 : UINT64_MAX;
	for (uint64_t j = 0; j < FIELD_N; ++j) {
		if (rest_frames >= LLFREE_ATOMIC_SIZE) {
			*((uint64_t *)&field->rows[j]) = val;
			rest_frames -= LLFREE_ATOMIC_SIZE;
		} else if (rest_frames > 0) {
			val = free_all ? UINT64_MAX << rest_frames : UINT64_MAX;
			*((uint64_t *)&field->rows[j]) = val;
			rest_frames = 0;
		} else {
			*((uint64_t *)&field->rows[j]) = UINT64_MAX;
		}
	}
}

llfree_result_t lower_recover(lower_t *self)
{
	for (size_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->children[i]);
		if (child.huge) {
			// if this was reserved as Huge Page set counter and all frames to 0
			atom_store(&self->children[i], child_new(0, true));
			field_init(&self->fields[i]);
		} else {
			// not a Huge Page -> count free Frames and set as counter
			uint16_t counter = LLFREE_CHILD_SIZE -
					   field_count_ones(&self->fields[i]);
			atom_store(&self->children[i],
				   child_new(counter, false));
		}
	}

	return llfree_result(LLFREE_ERR_OK);
}

static llfree_result_t get_max(lower_t *self, uint64_t pfn)
{
	assert(self != 0);

	size_t idx = child_from_pfn(pfn) / 2;
	assert(idx < self->childs_len / 2);
	for_offsetted(idx, LLFREE_TREE_CHILDREN / 2) {
		child_pair_t old;
		if (atom_update((_Atomic(child_pair_t) *)&self
					->children[current_i * 2],
				old, child_reserve_max)) {
			return llfree_result(pfn_from_child(current_i * 2));
		}
	}

	return llfree_result(LLFREE_ERR_MEMORY);
}

static llfree_result_t get_huge(lower_t *self, uint64_t pfn)
{
	assert(self != 0);

	size_t idx = child_from_pfn(pfn);
	assert(idx < self->childs_len);
	for_offsetted(idx, LLFREE_TREE_CHILDREN) {
		child_t old;
		if (atom_update(&self->children[current_i], old,
				child_reserve_huge)) {
			return llfree_result(pfn_from_child(current_i));
		}
	}

	return llfree_result(LLFREE_ERR_MEMORY);
}

llfree_result_t lower_get(lower_t *self, const uint64_t start_frame,
			  const size_t order)
{
	assert(order <= LLFREE_MAX_ORDER);
	assert(start_frame < self->frames);

	if (order == LLFREE_MAX_ORDER)
		return get_max(self, start_frame);
	if (order == LLFREE_HUGE_ORDER)
		return get_huge(self, start_frame);

	const size_t idx = child_from_pfn(start_frame);
	assert(idx < self->childs_len);
	for_offsetted(idx, LLFREE_TREE_CHILDREN) {
		child_t old;
		if (atom_update(&self->children[current_i], old, child_dec,
				order)) {
			llfree_result_t pos = field_set_next(
				&self->fields[current_i], start_frame, order);
			if (llfree_result_ok(pos)) {
				return llfree_result(pfn_from_child(current_i) +
						     pos.val);
			}

			if (!atom_update(&self->children[current_i], old,
					 child_inc, order)) {
				return llfree_result(LLFREE_ERR_CORRUPT);
			}
		}
	}

	return llfree_result(LLFREE_ERR_MEMORY);
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
		llfree_info("wait");
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

llfree_result_t lower_put(lower_t *self, uint64_t frame, size_t order)
{
	assert(order <= LLFREE_MAX_ORDER);
	if (frame + (1 << order) > self->frames || frame % (1 << order) != 0)
		return llfree_result(LLFREE_ERR_ADDRESS);

	const size_t child_idx = child_from_pfn(frame);
	_Atomic(child_t) *child = &self->children[child_idx];

	if (order == LLFREE_MAX_ORDER) {
		child_pair_t old = { child_new(0, true), child_new(0, true) };
		child_pair_t new = { child_new(LLFREE_CHILD_SIZE, false),
				     child_new(LLFREE_CHILD_SIZE, false) };
		return atom_cmp_exchange((_Atomic(child_pair_t) *)child, &old,
					 new) ?
			       llfree_result(LLFREE_ERR_OK) :
			       llfree_result(LLFREE_ERR_ADDRESS);
	}
	if (order == LLFREE_HUGE_ORDER) {
		child_t old = child_new(0, true);
		child_t new = child_new(LLFREE_CHILD_SIZE, false);
		return atom_cmp_exchange(child, &old, new) ?
			       llfree_result(LLFREE_ERR_OK) :
			       llfree_result(LLFREE_ERR_ADDRESS);
	}

	bitfield_t *field = &self->fields[child_idx];

	child_t old = atom_load(child);
	if (old.huge) {
		split_huge(child, field);
	}

	size_t field_index = frame % LLFREE_CHILD_SIZE;
	llfree_result_t ret = field_toggle(field, field_index, order, true);
	if (!llfree_result_ok(ret))
		return ret;

	if (!atom_update(child, old, child_inc, order)) {
		assert(!"should never be possible");
		return llfree_result(LLFREE_ERR_CORRUPT);
	}

	return llfree_result(LLFREE_ERR_OK);
}

bool lower_is_free(lower_t *self, uint64_t frame, size_t order)
{
	assert(order == 0 || order == LLFREE_HUGE_ORDER);
	// check if outside of managed space
	if (frame >= self->frames)
		return false;

	size_t child_index = child_from_pfn(frame);

	child_t child = atom_load(&self->children[child_index]);

	if (order == LLFREE_HUGE_ORDER) {
		return (!child.huge && child.free == LLFREE_CHILD_SIZE);
	}

	size_t field_index = frame % LLFREE_CHILD_SIZE;

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

#ifdef STD
void lower_print(lower_t *self)
{
	printf("lower_t {\n");
	for (size_t i = 0; i < align_up(self->childs_len, LLFREE_TREE_CHILDREN);
	     i++) {
		if (i % LLFREE_TREE_CHILDREN == 0)
			printf("\n");

		child_t child = atom_load(&self->children[i]);
		printf("    %6ju: free=%u, huge=%d\n", i, child.free,
		       child.huge);
	}
	printf("}\n");
}
#endif

void lower_drop(lower_t *self)
{
	assert(self != NULL);
	_Atomic(child_t) *start_data =
		(void *)((self->offset + self->frames) * LLFREE_FRAME_SIZE);
	if (self->children != start_data) {
		llfree_ext_free(LLFREE_CACHE_SIZE,
				sizeof(child_t) * self->childs_len,
				self->children);
		llfree_ext_free(LLFREE_CACHE_SIZE,
				sizeof(bitfield_t) * self->childs_len,
				self->fields);
	}
}

size_t lower_free_huge(lower_t *self)
{
	size_t count = 0;
	for (size_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->children[i]);
		if (child.free == LLFREE_CHILD_SIZE)
			++count;
	}
	return count;
}

void lower_for_each_child(const lower_t *self, void *context,
			  void f(void *, uint64_t, uint64_t))
{
	for (uint64_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->children[i]);
		f(context, self->offset + i * (1 << LLFREE_HUGE_ORDER),
		  child.free);
	}
}
