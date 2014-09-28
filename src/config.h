/* vifm
 * Copyright (C) 2001 Ken Steen.
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include<limits.h>
#include<ncurses.h>

#define MAXNUM_COLOR 12

#define MENU_COLOR 0
#define BORDER_COLOR 1
#define WIN_COLOR 2
#define	STATUS_BAR_COLOR 3
#define CURR_LINE_COLOR 4
#define DIRECTORY_COLOR 5
#define LINK_COLOR 6
#define SOCKET_COLOR 7
#define DEVICE_COLOR 8
#define EXECUTABLE_COLOR 9
#define SELECTED_COLOR 10
#define CURRENT_COLOR 11

typedef struct _Col_attr {
	int fg;
	int bg;
	/* future addition for color attribute[s]:
	 * int attr;
	 * int dim;
	 */
} Col_attr;


typedef struct _Config {
	char config_dir[PATH_MAX];
	char trash_dir[PATH_MAX];
	char *vi_command;
	int num_bookmarks;
	int use_trash;
	int use_color;
	int vim_filter;
	int use_screen;
	int show_full;
	int use_vim_help;
	int command_num;
	int filetypes_num;
	int history_len;
	int nmapped_num;
	int screen_num;
	int timer;
	char **search_history;
	int search_history_len;
	int search_history_num;
	char **cmd_history;
	int cmd_history_len;
	int cmd_history_num;
	int auto_execute;
	Col_attr color[MAXNUM_COLOR];
	int use_custom_colors;
} Config;

extern Config cfg;

int read_config_file(void);
void write_config_file(void);
void set_config_dir(void);
void init_config(void);

#endif
