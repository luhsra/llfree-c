#include "lower.h"
#include "bitfield.h"
#include "child.h"
#include "llfree.h"

size_t lower_metadata_size(size_t frames)
{
	size_t children = div_ceil(frames, LLFREE_CHILD_SIZE);
	uint64_t size_bitfields =
		align_up(children * sizeof(bitfield_t),
			 LLFREE_TREE_CHILDREN * sizeof(child_t));
	uint64_t size_children =
		align_up(children * sizeof(child_t),
			 MAX(LLFREE_TREE_CHILDREN * sizeof(child_t),
			     sizeof(bitfield_t)));
	return size_bitfields + size_children;
}

static void lower_clear(lower_t *self, bool free_all)
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

	// we have a few more unused children, which simplifies iterating over them
	for (size_t i = self->childs_len;
	     i < align_up(self->childs_len, LLFREE_TREE_CHILDREN); i++) {
		self->children[i] = child_new(0, false);
	}

	uint64_t rest_frames = self->frames % LLFREE_CHILD_SIZE;
	if (rest_frames == 0) {
		// rest is exactly one entire child
		self->children[self->childs_len - 1] =
			child_new(free_all ? LLFREE_CHILD_SIZE : 0, !free_all);

		bitfield_t *field = &self->fields[self->childs_len - 1];
		for (uint64_t j = 0; j < FIELD_N; ++j) {
			*((uint64_t *)&field->rows[j]) = 0;
		}
		return;
	}
	// rest is less than an entire child
	self->children[self->childs_len - 1] =
		child_new(free_all ? rest_frames : 0, false);

	bitfield_t *field = &self->fields[self->childs_len - 1];
	if (free_all) {
		for (uint64_t j = 0; j < FIELD_N; ++j) {
			if (rest_frames >= LLFREE_ATOMIC_SIZE) {
				*((uint64_t *)&field->rows[j]) = 0;
				rest_frames -= LLFREE_ATOMIC_SIZE;
			} else if (rest_frames > 0) {
				*((uint64_t *)&field->rows[j]) = UINT64_MAX
								 << rest_frames;
				rest_frames = 0;
			} else {
				*((uint64_t *)&field->rows[j]) = UINT64_MAX;
			}
		}
	} else {
		for (uint64_t j = 0; j < FIELD_N; ++j) {
			*((uint64_t *)&field->rows[j]) = UINT64_MAX;
		}
	}
}

static void lower_recover(lower_t *self)
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
}

llfree_result_t lower_init(lower_t *self, size_t frames, uint8_t init,
			   uint8_t *primary)
{
	self->frames = frames;
	self->childs_len = div_ceil(self->frames, LLFREE_CHILD_SIZE);

	size_t bitfield_size = align_up(sizeof(bitfield_t) * self->childs_len,
					sizeof(child_t) * LLFREE_TREE_CHILDREN);

	self->fields = (bitfield_t *)primary;
	self->children = (_Atomic(child_t) *)(primary + bitfield_size);

	switch (init) {
	case LLFREE_INIT_FREE:
		lower_clear(self, true);
		break;
	case LLFREE_INIT_ALLOC:
		lower_clear(self, false);
		break;
	case LLFREE_INIT_RECOVER:
		break; // nothing to do
	case LLFREE_INIT_RECOVER_CRASH:
		lower_recover(self);
		break;
	default:
		return llfree_result(LLFREE_ERR_INIT);
	}
	return llfree_result(LLFREE_ERR_OK);
}

uint8_t *lower_metadata(lower_t *self)
{
	return (uint8_t *)self->fields;
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
			if (llfree_ok(pos)) {
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

static llfree_result_t split_huge(_Atomic(child_t) *child, bitfield_t *field)
{
	uint64_t zero = 0;

	// synchronize multiple threads on the first row
	bool success = atom_cmp_exchange(&field->rows[0], &zero, UINT64_MAX);
	if (success) {
		llfree_info("split huge");

		for (size_t i = 1; i < FIELD_N; ++i) {
			atom_store(&field->rows[i], UINT64_MAX);
		}

		child_t expected = child_new(0, true);
		success = atom_cmp_exchange(child, &expected,
					    child_new(0, false));
		assert(success);
	} else {
		llfree_info("split huge: wait");
		// another thread ist trying to breakup this HP
		// -> wait for their completion
		for (size_t i = 0; i < RETRIES; i++) {
			child_t c = atom_load(child);
			if (!c.huge)
				return llfree_result(LLFREE_ERR_OK);
		}
		llfree_warn("split huge: timeout");
		return llfree_result(LLFREE_ERR_RETRY);
	}
	return llfree_result(LLFREE_ERR_OK);
}

llfree_result_t lower_put(lower_t *self, uint64_t frame, size_t order)
{
	assert(order <= LLFREE_MAX_ORDER);
	if (frame + (1 << order) > self->frames || frame % (1 << order) != 0) {
		llfree_warn("invalid pfn %" PRIu64 "\n", frame);
		return llfree_result(LLFREE_ERR_ADDRESS);
	}

	const size_t child_idx = child_from_pfn(frame);
	_Atomic(child_t) *child = &self->children[child_idx];

	if (order == LLFREE_MAX_ORDER) {
		child_pair_t old = { child_new(0, true), child_new(0, true) };
		child_pair_t new = { child_new(LLFREE_CHILD_SIZE, false),
				     child_new(LLFREE_CHILD_SIZE, false) };

		if (atom_cmp_exchange((_Atomic(child_pair_t) *)child, &old,
				      new))
			return llfree_result(LLFREE_ERR_OK);

		return llfree_result(LLFREE_ERR_MEMORY);
	}
	if (order == LLFREE_HUGE_ORDER) {
		child_t old = child_new(0, true);
		child_t new = child_new(LLFREE_CHILD_SIZE, false);

		if (atom_cmp_exchange(child, &old, new))
			return llfree_result(LLFREE_ERR_OK);

		return llfree_result(LLFREE_ERR_ADDRESS);
	}

	bitfield_t *field = &self->fields[child_idx];

	child_t old = atom_load(child);
	if (old.huge) {
		llfree_result_t res = split_huge(child, field);
		if (!llfree_ok(res))
			return res;
	}

	size_t field_index = frame % LLFREE_CHILD_SIZE;
	llfree_result_t ret = field_toggle(field, field_index, order, true);
	if (!llfree_ok(ret))
		return ret;

	if (!atom_update(child, old, child_inc, order)) {
		assert(!"unreachable");
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
			  void f(void *, uint64_t, size_t))
{
	for (uint64_t i = 0; i < self->childs_len; ++i) {
		child_t child = atom_load(&self->children[i]);
		f(context, i << LLFREE_HUGE_ORDER, child.free);
	}
}
