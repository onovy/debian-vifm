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



#include<ncurses.h>
#include<unistd.h> /* getcwd */
#include<string.h> /* strncpy */
#include<sys/stat.h> /* stat */
#include<unistd.h> /* stat */

#include"bookmarks.h"
#include"commands.h"
#include"config.h"
#include"filelist.h"
#include"filetype.h"
#include"keys.h"
#include"signals.h"
#include"status.h"
#include"ui.h"
#include"utils.h"


Config cfg;
Status curr_stats;



static void
show_help_msg(void)
{
	system("clear");
	printf("vifm usage:\n\n");
	printf("	To start in a specific directory give the directory path.\n\n");
	printf("		vifm /path/to/start/dir/one\n");
	printf("		or\n");
	printf("		vifm /path/to/start/dir/one  /path/to/start/dir/two\n\n");
	printf("	If no path is given vifm will start in the current working directory.\n\n");
	printf("	vifm --version \n");
	printf("		show version number and quit.\n\n");
	printf("	vifm --help\n");
	printf("		show this help message and quit.\n\n");

}

int
main(int argc, char *argv[])
{
	char dir[PATH_MAX];
	char config_dir[PATH_MAX];
	char *console = NULL;
	int x;
	int side = 0;
	struct stat stat_buf;

	getcwd(dir, sizeof(dir));

	/* Window initializations */
	rwin.curr_line = 0;
	rwin.top_line = 0;
	rwin.list_rows = 0;
	rwin.list_pos = 0;
	rwin.selected_filelist = NULL;
	rwin.history_num = 0;
	rwin.invert = 0;

	lwin.curr_line = 0;
	lwin.top_line = 0;
	lwin.list_rows = 0;
	lwin.list_pos = 0;
	lwin.selected_filelist = NULL;
	lwin.history_num = 0;
	lwin.invert = 0;

	/* These need to be initialized before reading the configuration file */
	cfg.command_num = 0;
	cfg.filetypes_num = 0;
	cfg.nmapped_num = 0;
	cfg.vim_filter = 0;
	cfg.use_custom_colors = 0;
	command_list = NULL;
	filetypes = NULL;

	cfg.search_history_len = 15;
	cfg.search_history_num = -1;
	cfg.search_history = (char **)calloc(cfg.search_history_len, sizeof(char*));
	cfg.cmd_history_len = 15;
	cfg.cmd_history_num = -1;
	cfg.cmd_history = (char **)calloc(cfg.cmd_history_len, sizeof(char *));
	cfg.auto_execute = 0;


	init_config();
	set_config_dir();
	read_config_file();

	/* Misc configuration */

	lwin.prev_invert = lwin.invert;
	lwin.hide_dot = 1;
	strncpy(lwin.regexp, "\\..~$", sizeof(lwin.regexp));
	rwin.prev_invert = rwin.invert;
	rwin.hide_dot = 1;
	strncpy(rwin.regexp, "\\..~$", sizeof(rwin.regexp));
	cfg.timer = 10;
	curr_stats.yanked_files = NULL;
	curr_stats.num_yanked_files = 0;
	curr_stats.need_redraw = 0;
	curr_stats.getting_input = 0;
	curr_stats.menu = 0;
	curr_stats.redraw_menu = 0;
	curr_stats.is_updir = 0;
	curr_stats.last_char = 0;
	curr_stats.is_console = 0;
	curr_stats.search = 0;
	curr_stats.save_msg = 0;

	snprintf(config_dir, sizeof(config_dir), "%s/vifmrc", cfg.config_dir);
	if(stat(config_dir, &stat_buf) == 0)
		curr_stats.config_file_mtime = stat_buf.st_mtime;
	else
		curr_stats.config_file_mtime = 0;


	/* Check if running in X */
	console = getenv("DISPLAY");
	if(!console || !*console)
		curr_stats.is_console = 1;


	/* Setup the ncurses interface. */
	if(!setup_ncurses_interface())
		return -1;

	/* Load the initial directory */
	snprintf(rwin.curr_dir, sizeof(rwin.curr_dir), dir);
	snprintf(lwin.curr_dir, sizeof(lwin.curr_dir), dir);
	rwin.dir_entry = (dir_entry_t *)malloc(sizeof(dir_entry_t));
	lwin.dir_entry = (dir_entry_t *)malloc(sizeof(dir_entry_t));
	rwin.dir_entry[0].name = malloc(sizeof("../") +1);
	lwin.dir_entry[0].name = malloc(sizeof("../") +1);
	strcpy(rwin.dir_entry[0].name, "../");
	strcpy(lwin.dir_entry[0].name, "../");
	change_directory(&rwin, dir);
	change_directory(&lwin, dir);
	other_view = &lwin;
	curr_view = &rwin;

	/* Get Command Line Arguments */
	for(x = 1; x < argc; x++)
	{
		if(argv[x] != NULL)
		{
				if(!strcmp(argv[x], "-f"))
				{
					cfg.vim_filter = 1;
				}
				else if(!strcmp(argv[x], "--version"))
				{
					endwin();
					system("clear");
					printf("vifm %.1f\n\n", VERSION);
					exit(0);
				}
				else if(!strcmp(argv[x], "--help"))
				{
					endwin();
					show_help_msg();
					exit(0);
				}
				else if(is_dir(argv[x]))
				{
					if(side)
						snprintf(rwin.curr_dir, sizeof(rwin.curr_dir), argv[x]);
					else
					{
						snprintf(lwin.curr_dir, sizeof(lwin.curr_dir), argv[x]);
						side++;
					}
				}
				else
				{
					endwin();
					show_help_msg();
					exit(0);
				}
		}
	}


	load_dir_list(&rwin, 0);
	mvwaddstr(rwin.win, rwin.curr_line, 0, "*"); 
	wrefresh(rwin.win);

	/* This is needed for the sort_dir_list() which uses curr_view */
	switch_views();

	load_dir_list(&lwin, 0);
	moveto_list_pos(&lwin, 0);
	doupdate();
	setup_signals();

	werase(status_bar);
	wnoutrefresh(status_bar);



	/* Need to wait until both lists are loaded before changing one of the
	 * lists to show the file stats.  This is only used for starting vifm 
	 * from the vifm.vim script
	 */
	
	if(cfg.vim_filter)
		cfg.show_full = 1;

	/* Enter the main loop. */
	main_key_press_cb(curr_view);

	return 0;
}

