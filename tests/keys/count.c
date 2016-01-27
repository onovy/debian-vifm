#include <stic.h>

#include <limits.h> /* INT_MAX */
#include <stdlib.h> /* free() */
#include <wchar.h> /* wchar_t */

#include "../../src/engine/keys.h"
#include "../../src/modes/modes.h"
#include "../../src/utils/str.h"

extern int last_command_count;

SETUP()
{
	add_user_keys(L"abc", L"", NORMAL_MODE, 0);
}

TEST(no_number_get_not_def)
{
	assert_false(IS_KEYS_RET_CODE(execute_keys(L"dd")));
	assert_int_equal(NO_COUNT_GIVEN, last_command_count);
}

TEST(normal_number_get_it)
{
	assert_false(IS_KEYS_RET_CODE(execute_keys(L"123dd")));
	assert_int_equal(123, last_command_count);
}

TEST(huge_number_get_intmax)
{
	assert_false(IS_KEYS_RET_CODE(execute_keys(L"999999999999dd")));
	assert_int_equal(INT_MAX, last_command_count);
}

TEST(max_count_is_handled_correctly)
{
	char *const keys = format_str("%udd", INT_MAX);
	wchar_t *const keysw = to_wide(keys);

	assert_false(IS_KEYS_RET_CODE(execute_keys(keysw)));
	assert_int_equal(INT_MAX, last_command_count);

	free(keysw);
	free(keys);
}

TEST(nops_count_not_passed)
{
	assert_false(IS_KEYS_RET_CODE(execute_keys(L"10abcdd")));
	assert_int_equal(NO_COUNT_GIVEN, last_command_count);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
