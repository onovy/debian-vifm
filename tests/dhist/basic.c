#include <stic.h>

#include "../../src/dhist.h"

SETUP()
{
	assert_success(dhist_set_size(10U));
}

TEARDOWN()
{
	assert_success(dhist_set_size(0U));
}

TEST(first_history_item_has_zero_position)
{
	const int dhpos = dhist_save(-1, "/", "etc", 0);
	assert_int_equal(dhpos, 0);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
