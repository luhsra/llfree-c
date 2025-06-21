#include "lower.h"
#include "bitfield.h"
#include "child.h"
#include "llfree.h"

#define CHILD_N LLFREE_CHILD_SIZE

static size_t child_count(lower_t *self)
{
	return div_ceil(self->frames, CHILD_N);
}

static _Atomic(child_t) *get_child(lower_t *self, size_t i)
{
	assert(i < align_up(child_count(self), LLFREE_TREE_CHILDREN));
	return &self->children[i / LLFREE_TREE_CHILDREN]
			.entries[i % LLFREE_TREE_CHILDREN];
}

size_t lower_metadata_size(size_t frames)
{
	size_t children = div_ceil(frames, CHILD_N);
	size_t trees = div_ceil(children, LLFREE_TREE_CHILDREN);
	uint64_t size_bitfields =
		align_up(children * sizeof(bitfield_t), sizeof(children_t));
	uint64_t size_children =
		align_up(trees * sizeof(children_t),
			 LL_MAX(sizeof(children_t), sizeof(bitfield_t)));
	return size_bitfields + size_children;
}

static void zero_field(bitfield_t *field)
{
	for (size_t i = 0; i < FIELD_N; ++i) {
		*((uint64_t *)&field->rows[i]) = 0;
	}
}

static void init_field(bitfield_t *field, size_t free)
{
	for (size_t i = 0; i < FIELD_N; ++i) {
		if (free >= LLFREE_ATOMIC_SIZE) {
			*((uint64_t *)&field->rows[i]) = 0;
			free -= LLFREE_ATOMIC_SIZE;
		} else if (free > 0) {
			*((uint64_t *)&field->rows[i]) = UINT64_MAX << free;
			free = 0;
		} else {
			*((uint64_t *)&field->rows[i]) = UINT64_MAX;
		}
	}
}

static void lower_clear(lower_t *self, bool free_all)
{
	assert(self != NULL);

	size_t child_c = child_count(self);

	for (size_t i = 0; i < child_c; i++) {
		size_t f = LL_MIN(CHILD_N, self->frames - (i * CHILD_N));
		size_t free = free_all ? f : 0;

		*get_child(self, i) =
			child_new((uint16_t)free, free == 0, false, false);

		if (free == 0)
			zero_field(&self->fields[i]);
		else
			init_field(&self->fields[i], free);
	}
	// Leftover children are initialized to 0
	for (size_t i = child_c; i < align_up(child_c, LLFREE_TREE_CHILDREN);
	     i++) {
		*get_child(self, i) = child_new(0, true, false, false);
	}
}

static void lower_recover(lower_t *self)
{
	for (size_t i = 0; i < child_count(self); ++i) {
		child_t child = atom_load(get_child(self, i));
		if (child.huge) {
			// if this was reserved as Huge Page set counter and all frames to 0
			atom_store(get_child(self, i),
				   child_new(0, true, false, false));
			field_init(&self->fields[i]);
		} else {
			// not a Huge Page -> count free Frames and set as counter
			uint16_t counter =
				(uint16_t)(CHILD_N -
					   field_count_ones(&self->fields[i]));
			atom_store(get_child(self, i),
				   child_new(counter, false, false, false));
		}
	}
}

llfree_result_t lower_init(lower_t *self, size_t frames, uint8_t init,
			   uint8_t *primary)
{
	self->frames = frames;
	size_t child_c = child_count(self);
	size_t bitfield_size =
		align_up(sizeof(bitfield_t) * child_c, sizeof(children_t));

	self->fields = (bitfield_t *)primary;
	self->children = (children_t *)(primary + bitfield_size);
	size_t ll_unused tree_count = div_ceil(child_c, LLFREE_TREE_CHILDREN);
	size_t ll_unused meta = lower_metadata_size(frames);
	assert((size_t)(self->children + tree_count) <=
	       (size_t)(primary + meta));

	switch (init) {
	case LLFREE_INIT_FREE:
		lower_clear(self, true);
		break;
	case LLFREE_INIT_ALLOC:
		lower_clear(self, false);
		break;
	case LLFREE_INIT_RECOVER:
	case LLFREE_INIT_NONE:
		break; // nothing to do
	case LLFREE_INIT_RECOVER_CRASH:
		lower_recover(self);
		break;
	default:
		return llfree_err(LLFREE_ERR_INIT);
	}
	return llfree_err(LLFREE_ERR_OK);
}

uint8_t *lower_metadata(lower_t *self)
{
	return (uint8_t *)self->fields;
}

static llfree_result_t get_max(lower_t *self, uint64_t frame,
			       bool allow_reclaimed, bool zeroed)
{
	assert(self != 0);

	size_t idx = child_from_frame(frame) / 2;
	assert(idx < child_count(self) / 2);
	for_offsetted(idx, LLFREE_TREE_CHILDREN / 2) {
		child_pair_t old;
		_Atomic(child_pair_t) *pair =
			(_Atomic(child_pair_t) *)get_child(self, current_i * 2);
		if (atom_update(pair, old, child_set_max, allow_reclaimed,
				zeroed)) {
			return llfree_ok(frame_from_child(current_i * 2),
					 old.first.reclaimed ||
						 old.second.reclaimed,
					 old.first.zeroed && old.second.zeroed);
		}
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

static llfree_result_t get_huge(lower_t *self, uint64_t frame,
				bool allow_reclaimed, bool zeroed)
{
	assert(self != 0);

	size_t idx = child_from_frame(frame);
	assert(idx < child_count(self));
	for_offsetted(idx, LLFREE_TREE_CHILDREN) {
		child_t old;
		_Atomic(child_t) *child = get_child(self, current_i);
		if (atom_update(child, old, child_set_huge, allow_reclaimed,
				zeroed)) {
			return llfree_ok(frame_from_child(current_i),
					 old.reclaimed, old.zeroed);
		}
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

static llfree_result_t lower_get_inner(lower_t *self, uint64_t frame,
				       llflags_t flags, bool allow_reclaimed)
{
	if (flags.order == LLFREE_MAX_ORDER)
		return get_max(self, frame, allow_reclaimed, flags.zeroed);
	if (flags.order == LLFREE_HUGE_ORDER)
		return get_huge(self, frame, allow_reclaimed, flags.zeroed);

	const size_t idx = child_from_frame(frame);
	assert(idx < child_count(self));
	for_offsetted(idx, LLFREE_TREE_CHILDREN) {
		child_t old;
		_Atomic(child_t) *child = get_child(self, current_i);
		if (atom_update(child, old, child_dec, flags.order,
				allow_reclaimed)) {
			llfree_result_t pos = field_set_next(
				&self->fields[current_i], frame, flags.order);
			if (llfree_is_ok(pos)) {
				return llfree_ok(frame_from_child(current_i) +
							 pos.frame,
						 old.reclaimed, false);
			}

			if (!atom_update(child, old, child_inc, flags.order)) {
				llfree_warn("Undo failed!");
				assert(false);
			}
		}
	}
	return llfree_err(LLFREE_ERR_MEMORY);
}

llfree_result_t lower_get(lower_t *self, const uint64_t start_frame,
			  llflags_t flags)
{
	assert(flags.order <= LLFREE_MAX_ORDER);
	assert(start_frame < self->frames);

	if (LLFREE_PREFER_INSTALLED) {
		llfree_result_t res =
			lower_get_inner(self, start_frame, flags, false);
		if (res.error != LLFREE_ERR_MEMORY)
			return res;
	}

	return lower_get_inner(self, start_frame, flags, true);
}

llfree_result_t lower_get_at(lower_t *self, uint64_t frame, llflags_t flags)
{
	assert(flags.order <= LLFREE_MAX_ORDER);
	if (frame + (1 << flags.order) > self->frames ||
	    frame % (1 << flags.order) != 0) {
		llfree_warn("invalid frame %" PRIu64 "\n", frame);
		return llfree_err(LLFREE_ERR_ADDRESS);
	}

	size_t child_idx = child_from_frame(frame);
	_Atomic(child_t) *child = get_child(self, child_idx);

	if (flags.order == LLFREE_MAX_ORDER) {
		child_pair_t old;
		_Atomic(child_pair_t) *pair = (_Atomic(child_pair_t) *)child;
		if (atom_update(pair, old, child_set_max, true, flags.zeroed)) {
			return llfree_ok(frame,
					 old.first.reclaimed ||
						 old.second.reclaimed,
					 old.first.zeroed && old.second.zeroed);
		}
		return llfree_err(LLFREE_ERR_MEMORY);
	}
	if (flags.order == LLFREE_HUGE_ORDER) {
		child_t old;
		if (atom_update(child, old, child_set_huge, true,
				flags.zeroed)) {
			return llfree_ok(frame, old.reclaimed, old.zeroed);
		}
		return llfree_err(LLFREE_ERR_MEMORY);
	}

	child_t old;
	if (atom_update(child, old, child_dec, flags.order, true)) {
		size_t field_index = frame % CHILD_N;
		bitfield_t *field = &self->fields[child_idx];
		llfree_result_t ret =
			field_toggle(field, field_index, flags.order, false);
		if (llfree_is_ok(ret))
			return llfree_ok(frame, old.reclaimed, false);

		if (!atom_update(child, old, child_inc, flags.order)) {
			llfree_warn("Undo failed!");
			assert(false);
		}
	}
	return llfree_err(LLFREE_ERR_MEMORY);
}

static llfree_result_t split_huge(child_t old, _Atomic(child_t) *child,
				  bitfield_t *field)
{
	uint64_t zero = 0;

	// synchronize multiple threads on the first row
	bool success = atom_cmp_exchange(&field->rows[0], &zero, UINT64_MAX);
	if (success) {
		llfree_debug("split huge");

		for (size_t i = 1; i < FIELD_N; ++i) {
			atom_store(&field->rows[i], UINT64_MAX);
		}

		child_t expected = child_new(0, true, false, old.zeroed);
		success = atom_cmp_exchange(child, &expected,
					    child_new(0, false, false,
						      old.zeroed));
		assert(success);
	} else {
		llfree_debug("split huge: wait");
		// another thread ist trying to breakup this HP
		// -> wait for their completion
		for (size_t i = 0; i < RETRIES; i++) {
			child_t c = atom_load(child);
			if (!c.huge)
				return llfree_err(LLFREE_ERR_OK);
		}
		llfree_warn("split huge: timeout");
		return llfree_err(LLFREE_ERR_RETRY);
	}
	return llfree_err(LLFREE_ERR_OK);
}

llfree_result_t lower_put(lower_t *self, uint64_t frame, llflags_t flags)
{
	size_t order = flags.order;
	assert(order <= LLFREE_MAX_ORDER);

	if (frame + (1 << order) > self->frames || frame % (1 << order) != 0) {
		llfree_warn("invalid frame %" PRIu64 "\n", frame);
		return llfree_err(LLFREE_ERR_ADDRESS);
	}

	const size_t child_idx = child_from_frame(frame);
	_Atomic(child_t) *child = get_child(self, child_idx);

	if (order == LLFREE_MAX_ORDER) {
		child_pair_t old;
		if (atom_update((_Atomic(child_pair_t) *)child, old,
				child_clear_max, flags.zeroed))
			return llfree_err(LLFREE_ERR_OK);

		return llfree_err(LLFREE_ERR_MEMORY);
	}
	if (order == LLFREE_HUGE_ORDER) {
		child_t old;
		if (atom_update(child, old, child_clear_huge, flags.zeroed))
			return llfree_err(LLFREE_ERR_OK);

		return llfree_err(LLFREE_ERR_ADDRESS);
	}

	bitfield_t *field = &self->fields[child_idx];

	child_t old = atom_load(child);
	if (old.huge) {
		llfree_result_t res = split_huge(old, child, field);
		if (!llfree_is_ok(res))
			return res;
	}

	size_t field_index = frame % CHILD_N;
	llfree_result_t ret = field_toggle(field, field_index, order, true);
	if (!llfree_is_ok(ret))
		return ret;

	if (!atom_update(child, old, child_inc, order)) {
		llfree_warn("Inc Failed!");
		assert(false);
	}

	return llfree_err(LLFREE_ERR_OK);
}

bool lower_is_free(lower_t *self, uint64_t frame, size_t order)
{
	assert(order == 0 || order == LLFREE_HUGE_ORDER);
	// check if outside of managed space
	if (frame >= self->frames)
		return false;

	size_t child_index = child_from_frame(frame);

	child_t child = atom_load(get_child(self, child_index));

	if (order == LLFREE_HUGE_ORDER) {
		return (!child.huge && child.free == CHILD_N);
	}

	size_t field_index = frame % CHILD_N;

	if (child.free < (1 << order))
		return false;

	return field_is_free(&self->fields[child_index], field_index);
}

size_t lower_huge(lower_t *self)
{
	assert(self != NULL);
	return child_count(self);
}

size_t lower_free_frames(lower_t *self)
{
	size_t count = 0;
	for (size_t i = 0; i < child_count(self); i++) {
		child_t child = atom_load(get_child(self, i));
		count += (size_t)child.free;
	}
	return count;
}

size_t lower_free_huge(lower_t *self)
{
	size_t count = 0;
	for (size_t i = 0; i < child_count(self); ++i) {
		child_t child = atom_load(get_child(self, i));
		if (child.free == CHILD_N)
			++count;
	}
	return count;
}

size_t lower_zeroed_huge(lower_t *self) {
	size_t count = 0;
	for (size_t i = 0; i < child_count(self); ++i) {
		child_t child = atom_load(get_child(self, i));
		if (child.free == CHILD_N && child.zeroed)
			++count;
	}
	return count;
}

size_t lower_free_at_huge(lower_t *self, uint64_t frame)
{
	assert(frame >> LLFREE_CHILD_ORDER < child_count(self));
	child_t child = atom_load(get_child(self, frame >> LLFREE_CHILD_ORDER));
	return child.free;
}

size_t lower_free_at_tree(lower_t *self, uint64_t frame)
{
	assert(frame >> LLFREE_CHILD_ORDER < child_count(self));
	size_t free = 0;
	size_t start =
		align_down(frame >> LLFREE_CHILD_ORDER, LLFREE_TREE_CHILDREN);
	for (size_t i = 0; i < LLFREE_TREE_CHILDREN; ++i) {
		child_t child = atom_load(get_child(self, start + i));
		assert(child.free <= CHILD_N);
		free += child.free;
	}
	return free;
}

llfree_result_t lower_reclaim(lower_t *self, uint64_t start_frame, bool hard,
			      bool zeroed)
{
	assert(self != 0);

	size_t idx = child_from_frame(start_frame);
	assert(idx < child_count(self));
	for_offsetted(idx, LLFREE_TREE_CHILDREN) {
		child_t old;
		_Atomic(child_t) *child = get_child(self, current_i);
		if (atom_update(child, old, child_reclaim, hard, zeroed)) {
			return llfree_ok(frame_from_child(current_i),
					 old.reclaimed, old.zeroed);
		}
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

llfree_result_t lower_return(lower_t *self, uint64_t frame, bool install)
{
	_Atomic(child_t) *child = get_child(self, child_from_frame(frame));
	child_t old;
	if (atom_update(child, old, child_return, install))
		return llfree_ok(0, false, true);
	return llfree_err(LLFREE_ERR_ADDRESS);
}

llfree_result_t lower_install(lower_t *self, uint64_t frame)
{
	_Atomic(child_t) *child = get_child(self, child_from_frame(frame));
	child_t old;
	if (atom_update(child, old, child_install))
		return llfree_ok(0, false, true);
	return llfree_err(LLFREE_ERR_ADDRESS);
}

bool lower_is_reclaimed(lower_t *self, uint64_t frame)
{
	_Atomic(child_t) *child = get_child(self, child_from_frame(frame));
	child_t c = atom_load(child);
	return c.reclaimed;
}

void lower_print(lower_t *self)
{
	llfree_info_start();
	llfree_info_cont("lower_t {\n");
	for (size_t i = 0;
	     i < align_up(child_count(self), LLFREE_TREE_CHILDREN); i++) {
		if (i % LLFREE_TREE_CHILDREN == 0)
			llfree_info_cont("\n");

		child_t ll_unused child = atom_load(get_child(self, i));
		llfree_info_cont("    %" PRIuS ": free=%" PRIuS ", huge=%d\n",
				 i, (size_t)child.free, child.huge);
	}
	llfree_info_cont("}\n");
	llfree_info_end();
}
