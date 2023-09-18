#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>

#include "bitfield.h"
#include "child.h"
#include "utils.h"
#include <stdio.h>

bool child_counter_inc(child_t *self, _void v)
{
	(void)v;
	assert(self != NULL);

	// If reserved as HP the counter should not be touched
	// cannot increase the counter over the maximum number of free fields
	if (self->huge == true || self->counter >= FIELDSIZE)
		return false;

	self->counter += 1;
	return true;
}

bool child_counter_dec(child_t *self, _void v)
{
	(void)v;
	assert(self != NULL);
	// If reserved as HP the counter an should not be touched
	if (self->huge || self->counter == 0)
		return false;

	self->counter -= 1;
	return true;
}

bool child_free_huge(child_t *self, _void v)
{
	(void)v;
	assert(self != NULL);

	// check if child is marked as HP || somehow pages are free
	if (self->huge == false || self->counter != 0)
		return false;

	*self = child_new(CHILDSIZE, false);
	return true;
}

bool child_reserve_huge(child_t *self, _void v)
{
	(void)v;
	assert(self != NULL);

	// check if already reserved as HP or if not all frames are free
	if (self->huge == true || self->counter != FIELDSIZE)
		return false;

	*self = child_new(0, true);
	return true;
}
