/* vifm
 * Copyright (C) 2001 Ken Steen.
 * Copyright (C) 2011 xaizek.
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

#include "file_info.h"

#include <curses.h>

#include <sys/stat.h>

#include <assert.h> /* assert() */
#include <inttypes.h> /* PRId64 */
#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t */
#include <stdio.h>
#include <string.h> /* strlen() */
#include <time.h> /* tm localtime() strftime() */

#include "../cfg/config.h"
#include "../compat/fs_limits.h"
#include "../compat/os.h"
#include "../engine/keys.h"
#include "../engine/mode.h"
#include "../int/file_magic.h"
#include "../menus/menus.h"
#include "../ui/ui.h"
#include "../utils/fs.h"
#include "../utils/fsdata.h"
#include "../utils/macros.h"
#include "../utils/str.h"
#include "../utils/utf8.h"
#include "../utils/utils.h"
#include "../filelist.h"
#include "../status.h"
#include "../types.h"
#include "modes.h"

static void leave_file_info_mode(void);
static int print_item(const char label[], const char path[], int curr_y);
static int show_file_type(FileView *view, int curr_y);
static int show_mime_type(FileView *view, int curr_y);
static void format_time(time_t t, char buf[], size_t buf_size);
static void cmd_ctrl_c(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_l(key_info_t key_info, keys_info_t *keys_info);

static FileView *view;
static int was_redraw;

static keys_add_info_t builtin_cmds[] = {
	{L"\x03", {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = cmd_ctrl_c}}},
	{L"\x0c", {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = cmd_ctrl_l}}},
	/* return */
	{L"\x0d", {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = cmd_ctrl_c}}},
	/* escape */
	{L"\x1b", {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = cmd_ctrl_c}}},
	{L"ZQ", {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = cmd_ctrl_c}}},
	{L"ZZ", {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = cmd_ctrl_c}}},
	{L"q", {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = cmd_ctrl_c}}},
};

void
init_file_info_mode(void)
{
	int ret_code;

	ret_code = add_cmds(builtin_cmds, ARRAY_LEN(builtin_cmds), FILE_INFO_MODE);
	assert(ret_code == 0);

	(void)ret_code;
}

void
enter_file_info_mode(FileView *v)
{
	vle_mode_set(FILE_INFO_MODE, VMT_PRIMARY);
	view = v;
	setup_menu();
	redraw_file_info_dialog();

	was_redraw = 0;
}

static void
leave_file_info_mode(void)
{
	vle_mode_set(NORMAL_MODE, VMT_PRIMARY);

	if(was_redraw)
	{
		update_screen(UT_FULL);
	}
	else
	{
		update_all_windows();
	}
}

void
redraw_file_info_dialog(void)
{
	const dir_entry_t *entry;
	char perm_buf[26];
	char size_buf[56];
	char buf[256];
#ifndef _WIN32
	char id_buf[26];
#endif
	int curr_y;
	uint64_t size;
	int size_not_precise;

	assert(view != NULL);

	if(resize_for_menu_like() != 0)
	{
		return;
	}

	werase(menu_win);

	entry = &view->dir_entry[view->list_pos];

	size = DCACHE_UNKNOWN;
	if(entry->type == FT_DIR)
	{
		dcache_get_of(entry, &size, NULL);
	}

	if(size == DCACHE_UNKNOWN)
	{
		size = entry->size;
	}

	size_not_precise = friendly_size_notation(size, sizeof(size_buf), size_buf);

	curr_y = 2;

	curr_y += print_item("Path: ", entry->origin, curr_y);
	curr_y += print_item("Name: ", entry->name, curr_y);

	mvwaddstr(menu_win, curr_y, 2, "Size: ");
	mvwaddstr(menu_win, curr_y, 8, size_buf);
	if(size_not_precise)
	{
		snprintf(size_buf, sizeof(size_buf), " (%" PRId64 " bytes)", size);
		waddstr(menu_win, size_buf);
	}
	curr_y += 2;

	curr_y += show_file_type(view, curr_y);
	curr_y += show_mime_type(view, curr_y);

#ifndef _WIN32
	get_perm_string(perm_buf, sizeof(perm_buf), entry->mode);
	curr_y += print_item("Permissions: ", perm_buf, curr_y);
#else
	copy_str(perm_buf, sizeof(perm_buf), attr_str_long(entry->attrs));
	curr_y += print_item("Attributes: ", perm_buf, curr_y);
#endif

	format_time(entry->mtime, buf, sizeof(buf));
	curr_y += print_item("Modified: ", buf, curr_y);

	format_time(entry->atime, buf, sizeof(buf));
	curr_y += print_item("Accessed: ", buf, curr_y);

	format_time(entry->ctime, buf, sizeof(buf));
#ifndef _WIN32
	curr_y += print_item("Changed: ", buf, curr_y);
#else
	curr_y += print_item("Created: ", buf, curr_y);
#endif

#ifndef _WIN32
	get_uid_string(entry, 0, sizeof(id_buf), id_buf);
	curr_y += print_item("Owner: ", id_buf, curr_y);

	get_gid_string(entry, 0, sizeof(id_buf), id_buf);
	curr_y += print_item("Group: ", id_buf, curr_y);
#endif

	/* Fake use after last assignment. */
	(void)curr_y;

	box(menu_win, 0, 0);
	checked_wmove(menu_win, 0, 3);
	wprint(menu_win, " File Information ");
	wrefresh(menu_win);

	was_redraw = 1;
}

/* Prints item prefixed with a label truncating the item if it's too long.
 * Returns increment for curr_y. */
static int
print_item(const char label[], const char path[], int curr_y)
{
	const int max_width = getmaxx(menu_win) - strlen(label) - 2;
	const size_t print_len = utf8_nstrsnlen(path, max_width);

	mvwaddstr(menu_win, curr_y, 2, label);
	if(path[print_len] == '\0')
	{
		wprint(menu_win, path);
	}
	else
	{
		char path_buf[PATH_MAX];
		copy_str(path_buf, MIN(sizeof(path_buf), print_len + 1), path);
		wprint(menu_win, path_buf);
	}

	return 2;
}

/* Returns increment for curr_y. */
static int
show_file_type(FileView *view, int curr_y)
{
	const dir_entry_t *entry;
	int x;
	int old_curr_y = curr_y;
	x = getmaxx(menu_win);

	entry = &view->dir_entry[view->list_pos];

	mvwaddstr(menu_win, curr_y, 2, "Type: ");
	if(entry->type == FT_LINK)
	{
		char full_path[PATH_MAX];
		char linkto[PATH_MAX + NAME_MAX];

		get_current_full_path(view, sizeof(full_path), full_path);

		mvwaddstr(menu_win, curr_y, 8, "Link");
		curr_y += 2;
		mvwaddstr(menu_win, curr_y, 2, "Link To: ");

		if(get_link_target(full_path, linkto, sizeof(linkto)) == 0)
		{
			mvwaddnstr(menu_win, curr_y, 11, linkto, x - 11);

			if(!path_exists(linkto, DEREF))
			{
				mvwaddstr(menu_win, curr_y - 2, 12, " (BROKEN)");
			}
		}
		else
		{
			mvwaddstr(menu_win, curr_y, 11, "Couldn't Resolve Link");
		}
	}
	else if(entry->type == FT_EXEC || entry->type == FT_REG)
	{
#ifdef HAVE_FILE_PROG
		char full_path[PATH_MAX];
		FILE *pipe;
		char command[1024];
		char buf[NAME_MAX];

		get_current_full_path(view, sizeof(full_path), full_path);

		/* Use the file command to get file information. */
		snprintf(command, sizeof(command), "file \"%s\" -b", full_path);

		if((pipe = popen(command, "r")) == NULL)
		{
			mvwaddstr(menu_win, curr_y, 8, "Unable to open pipe to read file");
			return 2;
		}

		if(fgets(buf, sizeof(buf), pipe) != buf)
			strcpy(buf, "Pipe read error");

		pclose(pipe);

		mvwaddnstr(menu_win, curr_y, 8, buf, x - 9);
		if(x > 9 && strlen(buf) > (size_t)(x - 9))
		{
			mvwaddnstr(menu_win, curr_y + 1, 8, buf + x - 9, x - 9);
		}
#else /* #ifdef HAVE_FILE_PROG */
		if(entry->type == FT_EXEC)
			mvwaddstr(menu_win, curr_y, 8, "Executable");
		else
			mvwaddstr(menu_win, curr_y, 8, "Regular File");
#endif /* #ifdef HAVE_FILE_PROG */
	}
	else if(entry->type == FT_DIR)
	{
		mvwaddstr(menu_win, curr_y, 8, "Directory");
	}
#ifndef _WIN32
	else if(entry->type == FT_CHAR_DEV || entry->type == FT_BLOCK_DEV)
	{
		const char *const type = (entry->type == FT_CHAR_DEV)
		                       ? "Character Device"
		                       : "Block Device";
		char full_path[PATH_MAX];
		struct stat st;

		mvwaddstr(menu_win, curr_y, 8, type);

		get_current_full_path(view, sizeof(full_path), full_path);
		if(os_stat(full_path, &st) == 0)
		{
			char info[64];

			snprintf(info, sizeof(info), "Device Id: 0x%x:0x%x", major(st.st_rdev),
					minor(st.st_rdev));

			curr_y += 2;
			mvwaddstr(menu_win, curr_y, 2, info);
		}
	}
	else if(entry->type == FT_SOCK)
	{
		mvwaddstr(menu_win, curr_y, 8, "Socket");
	}
#endif
	else if(entry->type == FT_FIFO)
	{
		mvwaddstr(menu_win, curr_y, 8, "Fifo Pipe");
	}
	else
	{
		mvwaddstr(menu_win, curr_y, 8, "Unknown");
	}
	curr_y += 2;

	return curr_y - old_curr_y;
}

/* Returns increment for curr_y. */
static int
show_mime_type(FileView *view, int curr_y)
{
	char full_path[PATH_MAX];
	const char* mimetype = NULL;

	get_current_full_path(view, sizeof(full_path), full_path);
	mimetype = get_mimetype(full_path);

	mvwaddstr(menu_win, curr_y, 2, "Mime Type: ");

	if(mimetype == NULL)
		mimetype = "Unknown";

	mvwaddstr(menu_win, curr_y, 13, mimetype);

	return 2;
}

/* Formats single time field as a string.  Writes empty string on error. */
static void
format_time(time_t t, char buf[], size_t buf_size)
{
	struct tm *const tm = localtime(&t);
	if(tm == NULL)
	{
		copy_str(buf, buf_size, "");
		return;
	}

	strftime(buf, buf_size, "%a, %d %b %Y %H:%M:%S", tm);
}

static void
cmd_ctrl_c(key_info_t key_info, keys_info_t *keys_info)
{
	leave_file_info_mode();
}

static void
cmd_ctrl_l(key_info_t key_info, keys_info_t *keys_info)
{
	redraw_file_info_dialog();
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
