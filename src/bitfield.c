#include "bitfield.h"
#include "utils.h"
#include <stddef.h>
#include <stdint.h>

/// Helping struct to store the position of a bit in a bitfield.
typedef struct pos {
	/// 0 <= row_number < N
	size_t row_index;
	/// 0 <= bit_number < sizeof(uint64_t)
	size_t bit_index;
} pos_t;

/// Translates the index of the bit to the position in the field.
static pos_t get_pos(uint64_t index)
{
	index = index & (FIELDSIZE - 1);

	pos_t pos = { index / ATOMICSIZE, index % ATOMICSIZE };
	return pos;
}

void field_init(bitfield_t *self)
{
	for (uint64_t i = 0; i < FIELD_N; ++i) {
		self->rows[i] = 0;
	}
}

/// Finds the position of the first 0 in the value
static result_t find_unset(uint64_t val)
{
	// If x is 0, the result is undefined and there are no unset bits
	if (~val == 0)
		return result(ERR_MEMORY);

	int ret = trailing_zeros(~val);
	assert(ret >= 0 && ret < 64 && "out of bounds");
	return result(ret);
}

struct f_pair {
	size_t *pos;
	size_t order;
};

static bool find_in_row(uint64_t *row, struct f_pair pair)
{
	result_t res = find_unset(*row);
	if (result_ok(res)) {
		*pair.pos = res.val;
		*row |= (1ul << res.val);
		return true;
	}
	return false;
}

/// Set the first aligned 2^`order` zero bits, returning the bit offset
///
/// - See <https://graphics.stanford.edu/~seander/bithacks.html#ZeroInWord>
result_t first_zeros_aligned(uint64_t *v, size_t order)
{
	result_t off = result(ERR_MEMORY);
	switch (order) {
	case 0: {
		off = find_unset(*v);
		if (result_ok(off))
			*v |= 1 << off.val;
	}
	case 1: {
		uint64_t mask = 0xaaaaaaaaaaaaaaaallu;
		uint64_t or = (*v | (*v >> 1)) | mask;
		off = find_unset(or);
		if (result_ok(off))
			*v |= 0b11 << off.val;
	}
	case 2: {
		uint64_t mask = 0x1111111111111111llu;
		off = find_unset((((*v - mask) & !*v) >> 3) & mask);
		if (result_ok(off))
			*v |= 0b1111 << off.val;
	}
	case 3: {
		uint64_t mask = 0x0101010101010101llu;
		off = find_unset((((*v - mask) & !*v) >> 7) & mask);
		if (result_ok(off))
			*v |= 0xff << off.val;
	}
	case 4: {
		uint64_t mask = 0x0001000100010001llu;
		off = find_unset((((*v - mask) & !*v) >> 15) & mask);
		if (result_ok(off))
			*v |= 0xffff << off.val;
	}
	case 5: {
		uint64_t mask = 0xffffffffllu;
		if ((uint32_t)*v == 0) {
			off = result(0);
			*v |= mask;
		} else if (*v >> 32 == 0) {
			off = result(32);
			*v |= mask << 32;
		}
	}
	case 6: {
		if (v == 0) {
			off = result(0);
			*v = UINT64_MAX;
		}
	}
	// All other orders are handled differently
	default:
		assert(false);
		break;
	}

	return off;
}

result_t field_set_next(bitfield_t *field, uint64_t start_pfn, size_t order)
{
	size_t num_frames = 1 << order;
	assert(num_frames < FIELDSIZE);

	uint64_t row = row_from_pfn(start_pfn) % FIELD_N;

	if (num_frames <= ATOMICSIZE) {
		for_offsetted(row, FIELD_N)
		{
			size_t pos = 0;
			struct f_pair pair = { &pos, order };
			uint64_t old;
			if (atom_update(&field->rows[current_i], old, pair,
					find_in_row)) {
				return result(current_i *
						      (sizeof(uint64_t) * 8) +
					      pos);
			}
		}
		return result(ERR_MEMORY);
	}

	size_t entries = num_frames / ATOMICSIZE;
	for_offsetted(row / entries, FIELD_N / entries)
	{
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
			break; // check next slot
		}
	}

	return result(ERR_MEMORY);
}

result_t field_reset_bit(bitfield_t *field, size_t index)
{
	assert(0 <= index && index < FIELDSIZE);

	pos_t pos = get_pos(index);

	// 11...101...11 -> zero at the bit-position
	uint64_t mask = ~(1ull << pos.bit_index);

	uint64_t before = atom_and(&field->rows[pos.row_index], mask);

	if ((before & mask) == before) { // bit were already reset
		return result(ERR_ADDRESS);
	}

	return result(ERR_OK);
}

int field_count_bits(bitfield_t *field)
{
	int counter = 0;
	for (size_t i = 0; i < FIELD_N; i++) {
		uint64_t row = atom_load(&field->rows[i]);
		counter += count_ones(row);
	}

	assert(0 <= counter && counter <= FIELDSIZE);
	return counter;
}

bool field_is_free(bitfield_t *self, size_t index)
{
	assert(0 <= index && index < FIELDSIZE);
	pos_t pos = get_pos(index);

	uint64_t row = atom_load(&self->rows[pos.row_index]);
	uint64_t mask = 1ul << pos.bit_index;

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
