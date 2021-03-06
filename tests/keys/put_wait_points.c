#include <stic.h>

#include "../../src/engine/keys.h"
#include "../../src/modes/modes.h"

SETUP()
{
	add_user_keys(L"abcdef", L"k", NORMAL_MODE, 0);
}

TEST(abcdef)
{
	assert_int_equal(KEYS_WAIT, execute_keys(L"a"));
	assert_int_equal(KEYS_WAIT, execute_keys(L"ab"));
	assert_int_equal(KEYS_WAIT, execute_keys(L"abc"));
	assert_int_equal(KEYS_WAIT, execute_keys(L"abcd"));
	assert_int_equal(KEYS_WAIT, execute_keys(L"abcde"));
	assert_false(IS_KEYS_RET_CODE(execute_keys(L"abcdef")));
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0: */
/* vim: set cinoptions+=t0 filetype=c : */
