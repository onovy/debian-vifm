/* vifm
 * Copyright (C) 2015 xaizek.
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

#include "more.h"

#include <curses.h>

#include <assert.h> /* assert() */
#include <limits.h> /* INT_MAX */
#include <stddef.h> /* NULL size_t */
#include <string.h> /* strdup() */

#include "../cfg/config.h"
#include "../compat/reallocarray.h"
#include "../engine/keys.h"
#include "../engine/mode.h"
#include "../menus/menus.h"
#include "../ui/ui.h"
#include "../utils/macros.h"
#include "../utils/str.h"
#include "../utils/utf8.h"
#include "cmdline.h"
#include "modes.h"

/* Provide more readable definitions of key codes. */

#define WK_CTRL_c L"\x03"
#define WK_CTRL_j L"\x0a"
#define WK_CTRL_l L"\x0c"
#define WK_CTRL_m L"\x0d"

#define WK_COLON L":"
#define WK_ESCAPE L"\x1b"
#define WK_RETURN WK_CTRL_m
#define WK_SPACE L" "

#define WK_G L"G"
#define WK_b L"b"
#define WK_d L"d"
#define WK_f L"f"
#define WK_g L"g"
#define WK_j L"j"
#define WK_k L"k"
#define WK_q L"q"
#define WK_u L"u"

static void calc_vlines_wrapped(void);
static void leave_more_mode(void);
static const char * get_text_beginning(void);
static void draw_all(const char text[]);
static void cmd_leave(key_info_t key_info, keys_info_t *keys_info);
static void cmd_ctrl_l(key_info_t key_info, keys_info_t *keys_info);
static void cmd_colon(key_info_t key_info, keys_info_t *keys_info);
static void cmd_bottom(key_info_t key_info, keys_info_t *keys_info);
static void cmd_top(key_info_t key_info, keys_info_t *keys_info);
static void cmd_down_line(key_info_t key_info, keys_info_t *keys_info);
static void cmd_up_line(key_info_t key_info, keys_info_t *keys_info);
static void cmd_down_screen(key_info_t key_info, keys_info_t *keys_info);
static void cmd_up_screen(key_info_t key_info, keys_info_t *keys_info);
static void cmd_down_page(key_info_t key_info, keys_info_t *keys_info);
static void cmd_up_page(key_info_t key_info, keys_info_t *keys_info);
static void goto_vline(int line);
static void goto_vline_below(int by);
static void goto_vline_above(int by);

/* Whether UI was redrawn while this mode was active. */
static int was_redraw;

/* Text displayed by the mode. */
static char *text;
/* (first virtual line, screen width, offset in text) triples per real line. */
static int (*data)[3];

/* Number of virtual lines. */
static int nvlines;

/* Current real line number. */
static int curr_line;
/* Current virtual line number. */
static int curr_vline;

/* Width of the area text is printed on. */
static int viewport_width;
/* Height of the area text is printed on. */
static int viewport_height;

/* List of builtin keys. */
static keys_add_info_t builtin_keys[] = {
	{WK_CTRL_c, {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_leave}}},
	{WK_CTRL_j, {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_down_line}}},
	{WK_CTRL_l, {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_ctrl_l}}},

	{WK_COLON,  {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_colon}}},
	{WK_ESCAPE, {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_leave}}},
	{WK_RETURN, {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_leave}}},
	{WK_SPACE,  {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_down_screen}}},

	{WK_G,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_bottom}}},
	{WK_b,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_up_screen}}},
	{WK_d,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_down_page}}},
	{WK_f,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_down_screen}}},
	{WK_g,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_top}}},
	{WK_j,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_down_line}}},
	{WK_k,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_up_line}}},
	{WK_q,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_leave}}},
	{WK_u,      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_up_page}}},

#ifdef ENABLE_EXTENDED_KEYS
	{{KEY_BACKSPACE}, {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_up_line}}},
	{{KEY_DOWN},      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_down_line}}},
	{{KEY_UP},        {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_up_line}}},
	{{KEY_HOME},      {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_top}}},
	{{KEY_END},       {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_bottom}}},
	{{KEY_NPAGE},     {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_down_screen}}},
	{{KEY_PPAGE},     {BUILTIN_KEYS, FOLLOWED_BY_NONE, {.handler = &cmd_up_screen}}},
#endif /* ENABLE_EXTENDED_KEYS */
};

void
modmore_init(void)
{
	int ret_code;

	ret_code = add_cmds(builtin_keys, ARRAY_LEN(builtin_keys), MORE_MODE);
	assert(ret_code == 0 && "Failed to initialize more mode keys.");

	(void)ret_code;
}

void
modmore_enter(const char txt[])
{
	text = strdup(txt);
	curr_line = 0;
	curr_vline = 0;

	vle_mode_set(MORE_MODE, VMT_PRIMARY);

	modmore_redraw();
	was_redraw = 0;
}

/* Recalculates virtual lines of a view with line wrapping. */
static void
calc_vlines_wrapped(void)
{
	const char *p;
	char *q;

	int i;
	const int nlines = count_lines(text, INT_MAX);

	data = reallocarray(NULL, nlines, sizeof(*data));

	nvlines = 0;

	p = text;
	q = text - 1;

	for(i = 0; i < nlines; ++i)
	{
		char saved_char;
		q = until_first(q + 1, '\n');
		saved_char = *q;
		*q = '\0';

		data[i][0] = nvlines++;
		data[i][1] = utf8_strsw_with_tabs(p, cfg.tab_stop);
		data[i][2] = p - text;
		nvlines += data[i][1]/viewport_width;

		*q = saved_char;
		p = q + 1;
	}
}

/* Quits the mode restoring previously active one. */
static void
leave_more_mode(void)
{
	update_string(&text, NULL);
	free(data);
	data = NULL;

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
modmore_redraw(void)
{
	if(resize_for_menu_like() != 0)
	{
		return;
	}
	wresize(status_bar, 1, getmaxx(stdscr));

	viewport_width = getmaxx(menu_win);
	viewport_height = getmaxy(menu_win);
	calc_vlines_wrapped();
	goto_vline(curr_vline);

	draw_all(get_text_beginning());

	was_redraw = 1;
}

/* Retrieves beginning of the text that should be displayed.  Returns the
 * beginning. */
static const char *
get_text_beginning(void)
{
	int skipped = 0;
	const char *text_piece = text + data[curr_line][2];
	/* Skip invisible virtual lines (those that are above current one). */
	while(skipped < curr_vline - data[curr_line][0])
	{
		text_piece += utf8_strsnlen(text_piece, viewport_width);
		++skipped;
	}
	return text_piece;
}

/* Draws all components of the mode onto the screen. */
static void
draw_all(const char text[])
{
	int attr;

	/* Clean up everything. */
	werase(menu_win);
	werase(status_bar);

	/* Draw the text. */
	checked_wmove(menu_win, 0, 0);
	wprint(menu_win, text);

	/* Draw status line. */
	attr = cfg.cs.color[CMD_LINE_COLOR].attr;
	wattron(status_bar, COLOR_PAIR(cfg.cs.pair[CMD_LINE_COLOR]) | attr);
	checked_wmove(status_bar, 0, 0);
	mvwprintw(status_bar, 0, 0, "-- More -- %d-%d/%d", curr_vline + 1,
			MIN(nvlines, curr_vline + viewport_height), nvlines);

	/* Inform curses of the changes. */
	wnoutrefresh(menu_win);
	wnoutrefresh(status_bar);

	/* Apply all changes. */
	doupdate();

	was_redraw = 1;
}

/* Leaves the mode. */
static void
cmd_leave(key_info_t key_info, keys_info_t *keys_info)
{
	leave_more_mode();
}

/* Redraws the mode. */
static void
cmd_ctrl_l(key_info_t key_info, keys_info_t *keys_info)
{
	modmore_redraw();
}

/* Switches to command-line mode. */
static void
cmd_colon(key_info_t key_info, keys_info_t *keys_info)
{
	leave_more_mode();
	enter_cmdline_mode(CLS_COMMAND, "", NULL);
}

/* Navigate to the bottom. */
static void
cmd_bottom(key_info_t key_info, keys_info_t *keys_info)
{
	goto_vline(INT_MAX);
}

/* Navigate to the top. */
static void
cmd_top(key_info_t key_info, keys_info_t *keys_info)
{
	goto_vline(0);
}

/* Go one line below. */
static void
cmd_down_line(key_info_t key_info, keys_info_t *keys_info)
{
	goto_vline(curr_vline + 1);
}

/* Go one line above. */
static void
cmd_up_line(key_info_t key_info, keys_info_t *keys_info)
{
	goto_vline(curr_vline - 1);
}

/* Go one screen below. */
static void
cmd_down_screen(key_info_t key_info, keys_info_t *keys_info)
{
	goto_vline(curr_vline + viewport_height);
}

/* Go one screen above. */
static void
cmd_up_screen(key_info_t key_info, keys_info_t *keys_info)
{
	goto_vline(curr_vline - viewport_height);
}

/* Go one page (half of the screen) below. */
static void
cmd_down_page(key_info_t key_info, keys_info_t *keys_info)
{
	goto_vline(curr_vline + viewport_height/2);
}

/* Go one page (half of the screen) above. */
static void
cmd_up_page(key_info_t key_info, keys_info_t *keys_info)
{
	goto_vline(curr_vline - viewport_height/2);
}

/* Navigates to the specified virtual line taking care of values that are out of
 * range. */
static void
goto_vline(int line)
{
	const int max_vline = nvlines - viewport_height;

	if(line > max_vline)
	{
		line = max_vline;
	}
	if(line < 0)
	{
		line = 0;
	}

	if(curr_vline == line)
	{
		return;
	}

	if(line > curr_vline)
	{
		goto_vline_below(line - curr_vline);
	}
	else
	{
		goto_vline_above(curr_vline - line);
	}

	modmore_redraw();
}

/* Navigates by virtual lines below. */
static void
goto_vline_below(int by)
{
	while(by-- > 0)
	{
		const int height = MAX(DIV_ROUND_UP(data[curr_line][1], viewport_width), 1);
		if(curr_vline + 1 >= data[curr_line][0] + height)
		{
			++curr_line;
		}

		++curr_vline;
	}
}

/* Navigates by virtual lines above. */
static void
goto_vline_above(int by)
{
	while(by-- > 0)
	{
		if(curr_vline - 1 < data[curr_line][0])
		{
			--curr_line;
		}

		--curr_vline;
	}
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */