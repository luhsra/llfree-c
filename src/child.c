#include "bitfield.h"
#include "child.h"
#include "utils.h"

bool child_counter_inc(child_t *self, _void _unused v)
{
	if (self->huge == true || self->counter >= FIELDSIZE)
		return false;

	self->counter += 1;
	return true;
}

bool child_counter_dec(child_t *self, _void _unused v)
{
	if (self->huge || self->counter == 0)
		return false;

	self->counter -= 1;
	return true;
}

bool child_reserve_huge(child_t *self, _void _unused v)
{
	if (self->counter != FIELDSIZE)
		return false;

	*self = child_new(0, true);
	return true;
}

bool child_reserve_max(child_pair_t *self, _void _unused v) {

        if (self->first.counter != FIELDSIZE || self->second.counter != FIELDSIZE) {
                return false;
        }
        self->first = child_new(0, true);
        self->second = child_new(0, true);
        return true;
}
