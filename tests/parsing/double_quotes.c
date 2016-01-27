#include <stic.h>

#include <stdlib.h> /* free() */

#include "../../src/engine/parsing.h"
#include "../../src/engine/var.h"

#include "asserts.h"

TEST(empty_ok)
{
	ASSERT_OK("\"\"", "");
}

TEST(simple_ok)
{
	ASSERT_OK("\"test\"", "test");
}

TEST(concatenation)
{
	ASSERT_OK("\"NV\".\"AR\"", "NVAR");
	ASSERT_OK("\"NV\" .\"AR\"", "NVAR");
	ASSERT_OK("\"NV\". \"AR\"", "NVAR");
	ASSERT_OK("\"NV\" . \"AR\"", "NVAR");
}

TEST(double_quote_escaping_ok)
{
	ASSERT_OK("\"\\\"\"", "\"");
}

TEST(special_chars_ok)
{
	ASSERT_OK("\"\\t\"", "\t");
}

TEST(spaces_ok)
{
	ASSERT_OK("\" s y \"", " s y ");
}

TEST(dot_ok)
{
	ASSERT_OK("\"a . c\"", "a . c");
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
