/* vifm
 * Copyright (C) 2012 xaizek.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "builtin_functions.h"

#include <sys/types.h> /* mode_t */

#include <assert.h> /* assert() */
#include <stddef.h> /* NULL size_t */
#include <stdlib.h> /* free() */
#include <string.h> /* strcmp() strdup() strpbrk() */

#include "engine/functions.h"
#include "engine/var.h"
#include "utils/macros.h"
#include "utils/path.h"
#include "utils/utils.h"
#include "macros.h"
#include "types.h"
#include "ui.h"

static var_t executable_builtin(const call_info_t *call_info);
static var_t expand_builtin(const call_info_t *call_info);
static var_t filetype_builtin(const call_info_t *call_info);
static int get_fnum(const char position[]);
static var_t has_builtin(const call_info_t *call_info);

static const function_t functions[] =
{
	{ "executable", 1, &executable_builtin },
	{ "expand",     1, &expand_builtin },
	{ "filetype",   1, &filetype_builtin },
	{ "has",        1, &has_builtin },
};

void
init_builtin_functions(void)
{
	size_t i;
	for(i = 0; i < ARRAY_LEN(functions); i++)
	{
		int result = function_register(&functions[i]);
		assert(result == 0 && "Builtin function registration error");
	}
}

/* Checks whether executable exists at absolute path orin directories listed in
 * $PATH when path isn't absolute.  Checks for various executable extensions on
 * Windows.  Returns boolean value describing result of the check. */
static var_t
executable_builtin(const call_info_t *call_info)
{
	int exists;
	char *str_val;

	str_val = var_to_string(call_info->argv[0]);

	if(strpbrk(str_val, PATH_SEPARATORS) != NULL)
	{
		exists = executable_exists(str_val);
	}
	else
	{
		exists = (find_cmd_in_path(str_val, 0UL, NULL) == 0);
	}

	free(str_val);

	return var_from_bool(exists);
}

/* Returns string after expanding expression. */
static var_t
expand_builtin(const call_info_t *call_info)
{
	var_t result;
	var_val_t var_val;
	char *str_val;
	char *env_expanded_str_val;

	str_val = var_to_string(call_info->argv[0]);
	env_expanded_str_val = expand_envvars(str_val, 0);
	var_val.string = expand_macros(env_expanded_str_val, NULL, NULL, 0);
	free(env_expanded_str_val);
	free(str_val);

	result = var_new(VTYPE_STRING, var_val);
	free(var_val.string);

	return result;
}

/* Gets string representation of file type.  Returns the string. */
static var_t
filetype_builtin(const call_info_t *call_info)
{
	char *str_val = var_to_string(call_info->argv[0]);
	const int fnum = get_fnum(str_val);
	var_val_t var_val = { .string = "" };
	free(str_val);

	if(fnum >= 0)
	{
		const FileType type = curr_view->dir_entry[fnum].type;
		var_val.const_string = get_type_str(type);
	}
	return var_new(VTYPE_STRING, var_val);
}

/* Returns file type from position or -1 if the position has wrong value. */
static int
get_fnum(const char position[])
{
	if(strcmp(position, ".") == 0)
	{
		return curr_view->list_pos;
	}
	else
	{
		return -1;
	}
}

/* Allows examining internal parameters from scripts to e.g. figure out
 * environment in which application is running. */
static var_t
has_builtin(const call_info_t *call_info)
{
	var_t result;

	char *const str_val = var_to_string(call_info->argv[0]);

	if(strcmp(str_val, "unix") == 0)
	{
		result = var_from_bool(get_env_type() == ET_UNIX);
	}
	else if(strcmp(str_val, "win") == 0)
	{
		result = var_from_bool(get_env_type() == ET_WIN);
	}
	else
	{
		result = var_false();
	}

	free(str_val);

	return result;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
