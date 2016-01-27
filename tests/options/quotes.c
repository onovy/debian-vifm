#include <stic.h>

#include "../../src/engine/options.h"

extern const char *value;

TEST(no_quotes)
{
	value = NULL;
	set_options("fusehome=a\\ b", OPT_GLOBAL);
	assert_string_equal("a b", value);

	value = NULL;
	set_options("fh=a\\ b\\ c", OPT_GLOBAL);
	assert_string_equal("a b c", value);

	set_options("so=name", OPT_GLOBAL);
}

TEST(single_quotes)
{
	set_options("fusehome='a b'", OPT_GLOBAL);
	assert_string_equal("a b", value);
}

TEST(double_quotes)
{
	set_options("fusehome=\"a b\"", OPT_GLOBAL);
	assert_string_equal("a b", value);

	set_options("fusehome=\"a \\\" b\"", OPT_GLOBAL);
	assert_string_equal("a \" b", value);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0: */
/* vim: set cinoptions+=t0 filetype=c : */
