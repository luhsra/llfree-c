#include "lower.h"
#include "bitfield.h"
#include "child.h"
#include "llfree.h"

#define CHILD_N LLFREE_CHILD_SIZE

static size_t child_count(const lower_t *self)
{
	return div_ceil(self->frames, CHILD_N);
}
static size_t children_count(const lower_t *self)
{
	return div_ceil(child_count(self), LLFREE_TREE_CHILDREN);
}

static _Atomic(child_t) *get_child(const lower_t *self, size_t i)
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

		*get_child(self, i) = child_new((uint16_t)free, free == 0);

		if (free == 0)
			zero_field(&self->fields[i]);
		else
			init_field(&self->fields[i], free);
	}
	/* Leftover children are initialized to 0 */
	for (size_t i = child_c; i < align_up(child_c, LLFREE_TREE_CHILDREN);
	     i++) {
		*get_child(self, i) = child_new(0, true);
	}
}

static void lower_recover(lower_t *self)
{
	for (size_t i = 0; i < child_count(self); ++i) {
		child_t child = atom_load(get_child(self, i));
		if (child.huge) {
			atom_store(get_child(self, i), child_new(0, true));
			field_init(&self->fields[i]);
		} else {
			uint16_t counter =
				(uint16_t)(CHILD_N -
					   field_count_ones(&self->fields[i]));
			atom_store(get_child(self, i),
				   child_new(counter, false));
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
	case LLFREE_INIT_NONE:
		break;
	case LLFREE_INIT_RECOVER:
		lower_recover(self);
		break;
	default:
		return llfree_err(LLFREE_ERR_INIT);
	}
	return llfree_err(LLFREE_ERR_OK);
}

uint8_t *lower_metadata(const lower_t *self)
{
	return (uint8_t *)self->fields;
}

static llfree_result_t get_max(lower_t *self, frame_id_t frame)
{
	assert(self != 0);

	size_t idx = child_from_frame(frame).value / 2;
	assert(idx < child_count(self) / 2);
	for_offsetted(idx, LLFREE_TREE_CHILDREN / 2, current_i) {
		child_pair_t old;
		_Atomic(child_pair_t) *pair =
			(_Atomic(child_pair_t) *)get_child(self, current_i * 2);
		if (atom_update(pair, old, child_set_max)) {
			return llfree_ok(
				frame_from_child(huge_id(current_i * 2)), 0);
		}
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

static llfree_result_t get_huge(lower_t *self, frame_id_t frame)
{
	assert(self != 0);

	size_t idx = child_from_frame(frame).value;
	assert(idx < child_count(self));
	for_offsetted(idx, LLFREE_TREE_CHILDREN, current_i) {
		child_t old;
		_Atomic(child_t) *child = get_child(self, current_i);
		if (atom_update(child, old, child_set_huge)) {
			return llfree_ok(frame_from_child(huge_id(current_i)),
					 0);
		}
	}

	return llfree_err(LLFREE_ERR_MEMORY);
}

static llfree_result_t lower_get_at(lower_t *self, frame_id_t frame,
				    size_t order)
{
	if (frame.value + (1u << order) > self->frames ||
	    frame.value % (1u << order) != 0) {
		llfree_warn("invalid frame %" PRIu64 "\n", frame.value);
		return llfree_err(LLFREE_ERR_ADDRESS);
	}
	size_t child_idx = child_from_frame(frame).value;
	_Atomic(child_t) *child = get_child(self, child_idx);
	if (order == LLFREE_MAX_ORDER) {
		child_pair_t old;
		_Atomic(child_pair_t) *pair = (_Atomic(child_pair_t) *)child;
		if (atom_update(pair, old, child_set_max))
			return llfree_ok(frame, 0);
		return llfree_err(LLFREE_ERR_MEMORY);
	}
	if (order == LLFREE_HUGE_ORDER) {
		child_t old;
		if (atom_update(child, old, child_set_huge))
			return llfree_ok(frame, 0);
		return llfree_err(LLFREE_ERR_MEMORY);
	}
	child_t old;
	if (atom_update(child, old, child_dec, order)) {
		bitfield_t *field = &self->fields[child_idx];
		llfree_result_t ret = field_toggle(field, frame.value % CHILD_N,
						   order, false);
		if (llfree_is_ok(ret))
			return llfree_ok(frame, 0);
		if (!atom_update(child, old, child_inc, order)) {
			llfree_warn("Undo failed!");
			assert(false);
		}
	}
	return llfree_err(LLFREE_ERR_MEMORY);
}

llfree_result_t lower_get(lower_t *self, const frame_id_t start_frame,
			  size_t order, frame_id_optional_t frame)
{
	assert(order <= LLFREE_MAX_ORDER);
	if (!unlikely(frame.present)) {
		assert(start_frame.value < self->frames);
		if (order == LLFREE_MAX_ORDER)
			return get_max(self, start_frame);
		if (order == LLFREE_HUGE_ORDER)
			return get_huge(self, start_frame);

		const size_t idx = child_from_frame(start_frame).value;
		assert(idx < child_count(self));
		for_offsetted(idx, LLFREE_TREE_CHILDREN, current_i) {
			child_t old;
			_Atomic(child_t) *child = get_child(self, current_i);
			if (atom_update(child, old, child_dec, order)) {
				llfree_result_t pos =
					field_set_next(&self->fields[current_i],
						       start_frame, order);
				if (llfree_is_ok(pos)) {
					frame_id_t offset = frame_from_child(
						huge_id(current_i));
					return llfree_ok(
						frame_id(offset.value +
							 pos.frame.value),
						0);
				}

				if (!atom_update(child, old, child_inc,
						 order)) {
					llfree_warn("Undo failed!");
					assert(false);
				}
			}
		}
		return llfree_err(LLFREE_ERR_MEMORY);
	}
	return lower_get_at(self, frame.value, order);
}

static llfree_result_t split_huge(child_t old, _Atomic(child_t) *child,
				  bitfield_t *field)
{
	(void)old;
	uint64_t zero = 0;

	bool success = atom_cmp_exchange(&field->rows[0], &zero, UINT64_MAX);
	if (success) {
		llfree_debug("split huge");

		for (size_t i = 1; i < FIELD_N; ++i) {
			atom_store(&field->rows[i], UINT64_MAX);
		}

		child_t expected = child_new(0, true);
		success = atom_cmp_exchange(child, &expected,
					    child_new(0, false));
		assert(success);
	} else {
		llfree_debug("split huge: wait");
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

llfree_result_t lower_put(lower_t *self, frame_id_t frame, size_t order)
{
	assert(order <= LLFREE_MAX_ORDER);

	if (frame.value + (1 << order) > self->frames ||
	    frame.value % (1 << order) != 0) {
		llfree_warn("invalid frame %" PRIu64 "\n", frame.value);
		return llfree_err(LLFREE_ERR_ADDRESS);
	}

	const size_t child_idx = child_from_frame(frame).value;
	_Atomic(child_t) *child = get_child(self, child_idx);

	if (order == LLFREE_MAX_ORDER) {
		child_pair_t old;
		if (atom_update((_Atomic(child_pair_t) *)child, old,
				child_clear_max))
			return llfree_err(LLFREE_ERR_OK);
		return llfree_err(LLFREE_ERR_MEMORY);
	}
	if (order == LLFREE_HUGE_ORDER) {
		child_t old;
		if (atom_update(child, old, child_clear_huge))
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

	size_t field_index = frame.value % CHILD_N;
	llfree_result_t ret = field_toggle(field, field_index, order, true);
	if (!llfree_is_ok(ret))
		return ret;

	if (!atom_update(child, old, child_inc, order)) {
		llfree_warn("Inc Failed!");
		assert(false);
	}

	return llfree_err(LLFREE_ERR_OK);
}

ll_stats_t lower_stats(const lower_t *self)
{
	assert(self != NULL);
	ll_stats_t stats = {
		.free_frames = 0,
		.free_huge = 0,
		.free_trees = 0,
	};
	for (size_t i = 0; i < children_count(self); ++i) {
		size_t free_frames = 0;
		for (size_t j = 0; j < LLFREE_TREE_CHILDREN; ++j) {
			child_t child =
				atom_load(&self->children[i].entries[j]);
			stats.free_frames += (size_t)child.free;
			stats.free_huge += (child.free == CHILD_N);
			free_frames += (size_t)child.free;
		}
		stats.free_trees += free_frames == LLFREE_TREE_SIZE;
	}
	return stats;
}

ll_stats_t lower_stats_at(const lower_t *self, frame_id_t frame, size_t order)
{
	assert(self != NULL);
	ll_stats_t stats = { 0, 0, 0 };

	if (frame.value >= self->frames) {
		llfree_warn("invalid frame %" PRIu64 "\n", frame.value);
		return stats;
	}

	if (order == 0) {
		size_t i = child_from_frame(frame).value;
		child_t child = atom_load(get_child(self, i));
		if (child.free == CHILD_N) {
			stats.free_frames = CHILD_N;
			stats.free_huge = 1;
		} else if (child.free > 0) {
			stats.free_frames = field_is_free(
				&self->fields[i], frame.value % CHILD_N);
			stats.free_huge = 0;
		}
	} else if (order == LLFREE_HUGE_ORDER) {
		size_t i = child_from_frame(frame).value;
		child_t child = atom_load(get_child(self, i));
		stats.free_frames = (size_t)child.free;
		stats.free_huge = (child.free == CHILD_N);
	} else if (order == LLFREE_TREE_ORDER) {
		size_t start = align_down(frame.value >> LLFREE_CHILD_ORDER,
					  LLFREE_TREE_CHILDREN);
		for (size_t i = 0; i < LLFREE_TREE_CHILDREN; ++i) {
			child_t child = atom_load(get_child(self, start + i));
			stats.free_frames += (size_t)child.free;
			stats.free_huge += (child.free == CHILD_N);
		}
		stats.free_trees = stats.free_frames == LLFREE_TREE_SIZE;
	} else {
		llfree_warn("invalid order %zu\n", order);
	}
	return stats;
}

void lower_print(const lower_t *self)
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
