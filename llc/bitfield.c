#include "bitfield.h"
#include "utils.h"

// Helping struct to store the position of a bit in a bitfield.
typedef struct pos {
	size_t row_index; // 0 <= row_number < N
	size_t bit_index; // 0 <= bit_number < sizeof(uint64_t)
} pos_t;

//Translates the index of the bit to the position in the field.
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

/**
 * @brief finds the position of the first 0 in the bitfield
 * @param field Pointer to the bitfield
 * @param pos Pointer to a struct the position will be Stored in
 * @return  ERR_OK on success
 *          ERR_MEMORY if no unset Bit was found.
 */
static result_t find_unset(uint64_t val)
{
	// If x is 0, the result is undefined and there are no unset bits
	if (~val == 0)
		return result(ERR_MEMORY);

	int ret = trailing_zeros(~val);
	assert(ret >= 0 && ret < 64 && "out of bounds");
	return result(ret);
}

static bool find_in_row(uint64_t *row, size_t *pos)
{
	result_t res = find_unset(*row);
	if (result_ok(res)) {
		*pos = res.val;
		*row |= (1ul << res.val);
		return true;
	}
	return false;
}

result_t field_set_next(bitfield_t *field, const uint64_t pfn)
{
	uint64_t row = atomic_from_pfn(pfn) % FIELD_N;

	for_offsetted(row, FIELD_N)
	{
		size_t pos = 0;
		uint64_t old;
		if (atom_update(&field->rows[current_i], old, &pos,
				find_in_row)) {
			return result(current_i * (sizeof(uint64_t) * 8) + pos);
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

	uint64_t before = atomic_fetch_and(&field->rows[pos.row_index], mask);

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
