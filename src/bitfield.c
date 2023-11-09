#include "bitfield.h"

/// Helping struct to store the position of a bit in a bitfield.
typedef struct pos {
	/// 0 <= row_number < N
	size_t row;
	/// 0 <= bit_number < sizeof(uint64_t)
	size_t bit;
} pos_t;

/// Translates the index of the bit to the position in the field.
static pos_t get_pos(uint64_t index)
{
	index = index % CHILD_SIZE;
	pos_t pos = { index / ATOMIC_SIZE, index % ATOMIC_SIZE };
	return pos;
}

void field_init(bitfield_t *self)
{
	for (uint64_t i = 0; i < FIELD_N; ++i) {
		self->rows[i] = 0;
	}
}

/// Set the first aligned 2^`order` zero bits, returning the bit offset
///
/// - See <https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord>
bool first_zeros_aligned(uint64_t *v, size_t order, size_t *pos)
{
	uint64_t mask;
	int p = -1;
	switch (order) {
	case 0:
		p = trailing_zeros(~(*v));
		if (p >= 0)
			*v |= 1llu << p;
		break;
	case 1:
		mask = 0xaaaaaaaaaaaaaaaallu;
		p = trailing_zeros(~((*v | (*v >> 1)) | mask));
		if (p >= 0)
			*v |= 0b11llu << p;
		break;
	case 2:
		mask = 0x1111111111111111llu;
		p = trailing_zeros((((*v - mask) & ~*v) >> 3) & mask);
		if (p >= 0)
			*v |= 0b1111llu << p;
		break;
	case 3:
		mask = 0x0101010101010101llu;
		p = trailing_zeros((((*v - mask) & ~*v) >> 7) & mask);
		if (p >= 0)
			*v |= 0xffllu << p;
		break;
	case 4:
		mask = 0x0001000100010001llu;
		p = trailing_zeros((((*v - mask) & ~*v) >> 15) & mask);
		if (p >= 0)
			*v |= 0xffffllu << p;
		break;
	case 5:
		mask = 0xffffffffllu;
		if ((uint32_t)*v == 0) {
			p = 0;
			*v |= mask;
		} else if (*v >> 32 == 0) {
			p = 32;
			*v |= mask << 32;
		}
		break;
	case 6:
		if (*v == 0) {
			p = 0;
			*v = UINT64_MAX;
		}
		break;
	default:
		// All other orders are handled differently
		assert(false);
		break;
	}
	if (p >= 0) {
		*pos = p;
		return true;
	}
	return false;
}

result_t field_set_next(bitfield_t *field, uint64_t start_pfn, size_t order)
{
	size_t num_frames = 1 << order;
	assert(num_frames < CHILD_SIZE);

	uint64_t row = row_from_pfn(start_pfn) % FIELD_N;

	if (num_frames <= ATOMIC_SIZE) {
		for_offsetted(row, FIELD_N)
		{
			size_t pos = 0;
			uint64_t old;
			if (atom_update(&field->rows[current_i], old,
					first_zeros_aligned, order, &pos)) {
				return result(current_i * ATOMIC_SIZE + pos);
			}
		}
		return result(ERR_MEMORY);
	}

	size_t entries = num_frames / ATOMIC_SIZE;
	for_offsetted(row / entries, FIELD_N / entries)
	{
		bool failed = false;
		for (size_t i = 0; i < entries; i++) {
			size_t idx = current_i * entries + i;
			uint64_t old = 0;
			if (atom_cmp_exchange(&field->rows[idx], &old,
					      UINT64_MAX)) {
				continue;
			}

			// Undo changes
			for (size_t j = 0; j < i; j++) {
				size_t idx = current_i * entries + j;
				uint64_t old = UINT64_MAX;
				if (!atom_cmp_exchange(&field->rows[idx], &old,
						       0)) {
					return result(ERR_CORRUPTION);
				}
			}
			failed = true;
			break; // check next slot
		}
		if (!failed) {
			// Success, we have updated all rows
			return result(current_i * entries * ATOMIC_SIZE);
		}
	}

	return result(ERR_MEMORY);
}

static bool row_toggle(uint64_t *row, uint64_t mask, bool expected)
{
	if (expected) {
		if ((*row & mask) == mask) {
			*row &= ~mask;
			return true;
		}
	} else {
		if ((*row & mask) == 0) {
			*row |= mask;
			return true;
		}
	}
	return false;
}

result_t field_toggle(bitfield_t *field, size_t index, size_t order,
		      bool expected)
{
	assert(0 <= index && index < CHILD_SIZE);

	pos_t pos = get_pos(index);
	size_t num_frames = 1 << order;

	if (num_frames > ATOMIC_SIZE) {
		size_t num_entries = (1 << order) / ATOMIC_SIZE;
		for (size_t i = 0; i < num_entries; i++) {
			uint64_t mask = expected ? UINT64_MAX : 0;
			uint64_t old = mask;
			if (atom_cmp_exchange(&field->rows[pos.row + i], &old,
					      ~mask)) {
				continue;
			}
			return result(ERR_ADDRESS);
		}
		return result(ERR_OK);
	}

	uint64_t mask = ((UINT64_MAX >> (ATOMIC_SIZE - num_frames)) << pos.bit);
	uint64_t old;
	if (atom_update(&field->rows[pos.row], old, row_toggle, mask,
			expected)) {
		return result(ERR_OK);
	}
	return result(ERR_ADDRESS);
}

size_t field_count_ones(bitfield_t *field)
{
	size_t counter = 0;
	for (size_t i = 0; i < FIELD_N; i++) {
		uint64_t row = atom_load(&field->rows[i]);
		counter += count_ones(row);
	}
	assert(0 <= counter && counter <= CHILD_SIZE);
	return counter;
}

bool field_is_free(bitfield_t *self, size_t index)
{
	assert(0 <= index && index < CHILD_SIZE);
	pos_t pos = get_pos(index);

	uint64_t row = atom_load(&self->rows[pos.row]);
	uint64_t mask = 1ul << pos.bit;

	return (row & mask) == 0;
}

void field_print(bitfield_t *field)
{
	printf("Field in HEX: MSB to LSB\n");
	for (size_t i = 0; i < FIELD_N; i++) {
		uint16_t *s = (uint16_t *)&(field->rows[i]);
		printf("%04X %04X %04X %04X\n", s[3], s[2], s[1], s[0]);
	}
}
