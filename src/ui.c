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

#include<signal.h>  /* signal() */
#include<stdlib.h> /* malloc */
#include<sys/stat.h> /* stat */
#include<dirent.h> /* DIR */
#include<pwd.h> /* getpwent() */
#include<string.h>
#include<time.h>
#include<termios.h> /* struct winsize */
#include<sys/ioctl.h>

#include "config.h" /* for menu colors */
#include "file_info.h"
#include "filelist.h"
#include "keys.h"
#include "menus.h"
#include "signals.h"
#include "status.h"
#include "ui.h"  
#include "utils.h"


static void
finish(char *message)
{
	endwin();
	write_config_file();
	system("clear");
	printf("%s", message);
	exit(0);
}

void
update_pos_window(FileView *view)
{
	char buf[13];

	if(curr_stats.freeze)
		return;
	werase(pos_win);
	snprintf(buf, sizeof(buf), "%d-%d ", view->list_pos +1, view->list_rows);
	mvwaddstr(pos_win, 0, 13 - strlen(buf), buf);
	wnoutrefresh(pos_win);
}

void
write_stat_win(char *message)
{
	werase(stat_win);
	wprintw(stat_win, "%s", message);
	wnoutrefresh(stat_win);
}

void
update_stat_window(FileView *view)
{
	char name_buf[20];
	char perm_buf[26];
	char size_buf[56];
	char uid_buf[26];
	struct passwd *pwd_buf;
	int x, y;

	getmaxyx(stat_win, y, x);
	snprintf(name_buf, sizeof(name_buf), "%s", get_current_file_name(view));
	describe_file_size(size_buf, sizeof(size_buf), view);
	
	if((pwd_buf = getpwuid(view->dir_entry[view->list_pos].uid)) == NULL)
	{
		snprintf (uid_buf, sizeof(uid_buf), "  %d", 
				(int) view->dir_entry[view->list_pos].uid);
	}
	else
	{
		snprintf(uid_buf, sizeof(uid_buf), "  %s", pwd_buf->pw_name);
	}
	get_perm_string(perm_buf, sizeof(perm_buf), 
			view->dir_entry[view->list_pos].mode);
	werase(stat_win);

	mvwaddstr(stat_win, 0, 2, name_buf);
	mvwaddstr(stat_win, 0, 20, size_buf);
	mvwaddstr(stat_win, 0, 36, perm_buf);
	mvwaddstr(stat_win, 0, 46, uid_buf);
	snprintf(name_buf, sizeof(name_buf), "%d %s filtered", 
			view->filtered, view->filtered == 1 ? "file" : "files");


	if(view->filtered > 0)
		mvwaddstr(stat_win, 0, x - (strlen(name_buf) +2) , name_buf);  

	wnoutrefresh(stat_win);
	update_pos_window(view);
}

void
status_bar_message(char *message)
{
	werase(status_bar);
	wprintw(status_bar, "%s", message);
	wnoutrefresh(status_bar);
}


int
setup_ncurses_interface()
{
	int screen_x, screen_y;
	int x, y;

	initscr();
	noecho();
	nonl();
	raw();

	getmaxyx(stdscr, screen_y, screen_x);
	/* screen is too small to be useful*/
	if(screen_y < 10)
		finish("Terminal is too small to run vifm\n");
	if(screen_x < 30)
		finish("Terminal is too small to run vifm\n");

	/* User did not want colors. */
	if(cfg.use_color == 0)
		cfg.use_color = 0;
	else
		cfg.use_color = has_colors();

	if(cfg.use_color)
	{
		int i;

		start_color();
		// Changed for pdcurses
		use_default_colors();

		for (i = 0; i < MAXNUM_COLOR; ++i)
		{
			init_pair(i, cfg.color[i].fg, cfg.color[i].bg);
		}
	}
	
	werase(stdscr);

	menu_win = newwin(screen_y - 1, screen_x , 0, 0);
	wbkgdset(menu_win, COLOR_PAIR(WIN_COLOR));
	werase(menu_win);

	sort_win = newwin(14, 30, (screen_y -12)/2, (screen_x -30)/2);
	wbkgdset(sort_win, COLOR_PAIR(WIN_COLOR));
	werase(sort_win);

	change_win = newwin(20, 30, (screen_y -20)/2, (screen_x -30)/2);
	wbkgdset(change_win, COLOR_PAIR(WIN_COLOR));
	werase(change_win);

	error_win = newwin(10, screen_x -2, (screen_y -10)/2, 1);
	wbkgdset(error_win, COLOR_PAIR(WIN_COLOR));
	werase(error_win);

	lborder = newwin(screen_y - 2, 1, 0, 0);

	if(cfg.use_color)
		wbkgdset(lborder, COLOR_PAIR(BORDER_COLOR));
	else
		wbkgdset(lborder, A_REVERSE);

	werase(lborder);

	if (curr_stats.number_of_windows == 1)
		lwin.title = newwin(0, screen_x -2, 0, 1);
	else
		lwin.title = newwin(0, screen_x/2 -1, 0, 1);
		
	if(cfg.use_color)
	{
		wattrset(lwin.title, A_BOLD);
		wbkgdset(lwin.title, COLOR_PAIR(BORDER_COLOR));
	}
	else
		wbkgdset(lwin.title, A_REVERSE);

	werase(lwin.title);

	if (curr_stats.number_of_windows == 1)
		lwin.win = newwin(screen_y - 3, screen_x -2, 1, 1);
	else
		lwin.win = newwin(screen_y - 3, screen_x/2 -2, 1, 1);

	keypad(lwin.win, TRUE);
	wbkgdset(lwin.win, COLOR_PAIR(WIN_COLOR));
	wattrset(lwin.win, A_BOLD);
	wattron(lwin.win, A_BOLD);
	werase(lwin.win);
	getmaxyx(lwin.win, y, x);
	lwin.window_rows = y -1;
	lwin.window_width = x -1;

	mborder = newwin(screen_y, 2, 0, screen_x/2 -1);

	if(cfg.use_color)
		wbkgdset(mborder, COLOR_PAIR(BORDER_COLOR));
	else
		wbkgdset(mborder, A_REVERSE);

	werase(mborder);

	if (curr_stats.number_of_windows == 1)
		rwin.title = newwin(0, screen_x -2  , 0, 1);
	else
		rwin.title = newwin(1, screen_x/2 -1  , 0, screen_x/2 +1);

	if(cfg.use_color)
	{
		wbkgdset(rwin.title, COLOR_PAIR(BORDER_COLOR));
		wattrset(rwin.title, A_BOLD);
		wattroff(rwin.title, A_BOLD);
	}
	else
		wbkgdset(rwin.title, A_REVERSE);

	werase(rwin.title);

	if (curr_stats.number_of_windows == 1)
		rwin.win = newwin(screen_y - 3, screen_x -2 , 1, 1);
	else
		rwin.win = newwin(screen_y - 3, screen_x/2 -2 , 1, screen_x/2 +1);

	keypad(rwin.win, TRUE);
	wattrset(rwin.win, A_BOLD);
	wattron(rwin.win, A_BOLD);
	wbkgdset(rwin.win, COLOR_PAIR(WIN_COLOR));
	werase(rwin.win);
	getmaxyx(rwin.win, y, x);
	rwin.window_rows = y - 1;
	rwin.window_width = x -1;


	/*
	if(screen_x % 2)
		rborder = newwin(screen_y - 2, 2, 0, screen_x -1);
	else
	*/
		rborder = newwin(screen_y - 2, 1, 0, screen_x -1);


	if(cfg.use_color)
		wbkgdset(rborder, COLOR_PAIR(BORDER_COLOR));
	else
		wbkgdset(rborder, A_REVERSE);


	werase(rborder);

	stat_win = newwin(1, screen_x, screen_y -2, 0);

	if(cfg.use_color)
		wbkgdset(stat_win, COLOR_PAIR(BORDER_COLOR));
	else
		wbkgdset(stat_win, A_REVERSE);

	werase(stat_win);

	status_bar = newwin(1, screen_x - 19, screen_y -1, 0);
	keypad(status_bar, TRUE);
	wattrset(status_bar, A_BOLD);
	wattron(status_bar, A_BOLD);
	wbkgdset(status_bar, COLOR_PAIR(STATUS_BAR_COLOR));
	werase(status_bar);

	pos_win = newwin(1, 13, screen_y - 1, screen_x -13);
	wattrset(pos_win, A_BOLD);
	wattron(pos_win, A_BOLD);
	wbkgdset(pos_win, COLOR_PAIR(STATUS_BAR_COLOR));
	werase(pos_win);

	num_win = newwin(1, 6, screen_y - 1, screen_x -19);
	wattrset(num_win, A_BOLD);
	wattron(num_win, A_BOLD);
	wbkgdset(num_win, COLOR_PAIR(STATUS_BAR_COLOR));
	werase(num_win);


	wnoutrefresh(lwin.title);
	wnoutrefresh(lwin.win);
	wnoutrefresh(rwin.win);
	wnoutrefresh(rwin.title);
	wnoutrefresh(stat_win);
	wnoutrefresh(status_bar);
	wnoutrefresh(pos_win);
	wnoutrefresh(num_win);
	wnoutrefresh(lborder);
	wnoutrefresh(mborder);
	wnoutrefresh(rborder);

	return 1;
}

void
redraw_window(void)
{
	int screen_x, screen_y;
	int x, y;
	struct winsize ws;

	curr_stats.freeze = 1;

	ioctl(0, TIOCGWINSZ, &ws);
	
	// changed for pdcurses
	resize_term(ws.ws_row, ws.ws_col);

	getmaxyx(stdscr, screen_y, screen_x);

	if (screen_y < 10)
		finish("Terminal is too small to run vifm\n");
	if (screen_x < 30)
		finish("Terminal is too small to run vifm\n");

	wclear(stdscr);
	wclear(lwin.title);
	wclear(lwin.win);
	wclear(rwin.title);
	wclear(rwin.win);
	wclear(stat_win);
	wclear(status_bar);
	wclear(pos_win);
	wclear(num_win);
	wclear(rborder);
	wclear(mborder);
	wclear(lborder);

	wclear(change_win);
	wclear(sort_win);
	
	wresize(stdscr, screen_y, screen_x);
	mvwin(sort_win, (screen_y - 14)/2, (screen_x -30)/2);
	mvwin(change_win, (screen_y - 10)/2, (screen_x -30)/2);
	wresize(menu_win, screen_y - 1, screen_x);
	wresize(error_win, (screen_y -10)/2, screen_x -2);
	mvwin(error_win, (screen_y -10)/2, 1);
	wresize(lborder, screen_y -2, 1);

	if (curr_stats.number_of_windows == 1)
	{
		wresize(lwin.title, 1, screen_x -1);
		wresize(lwin.win, screen_y -3, screen_x -2);
		getmaxyx(lwin.win, y, x);
		lwin.window_width = x -1;
		lwin.window_rows = y -1;

		wresize(rwin.title, 1, screen_x -1);
		mvwin(rwin.title, 0, 1);
		wresize(rwin.win, screen_y -3, screen_x -2);
		mvwin(rwin.win, 1, 1);
		getmaxyx(rwin.win, y, x);
		rwin.window_width = x -1;
		rwin.window_rows = y -1;
	}
	else
	{
		wresize(lwin.title, 1, screen_x/2 -2);
		wresize(lwin.win, screen_y -3, screen_x/2 -2);
		getmaxyx(lwin.win, y, x);
		lwin.window_width = x -1;
		lwin.window_rows = y -1;

		mvwin(mborder, 0, screen_x/2 -1);
		wresize(mborder, screen_y, 2);

		wresize(rwin.title, 1, screen_x/2 -2);
		mvwin(rwin.title, 0, screen_x/2 +1);

		wresize(rwin.win, screen_y -3, screen_x/2 -2);
		mvwin(rwin.win, 1, screen_x/2 +1);
		getmaxyx(rwin.win, y, x);
		rwin.window_width = x -1;
		rwin.window_rows = y -1;
	}



	/* For FreeBSD */
	keypad(lwin.win, TRUE);


	/* For FreeBSD */
	keypad(rwin.win, TRUE);

	if (screen_x % 2)
	{
		wresize(rborder, screen_y -2, 2);
		mvwin(rborder, 0, screen_x -2);
	}
	else
	{
		wresize(rborder, screen_y -2, 1);
		mvwin(rborder, 0, screen_x -1);
	}

	wresize(stat_win, 1, screen_x);
	mvwin(stat_win, screen_y -2, 0);
	wresize(status_bar, 1, screen_x -19);

	/* For FreeBSD */
	keypad(status_bar, TRUE);

	mvwin(status_bar, screen_y -1, 0);
	wresize(pos_win, 1, 13);
	mvwin(pos_win, screen_y -1, screen_x -13);

	wresize(num_win, 1, 6);
	mvwin(num_win, screen_y -1, screen_x -19);

	curs_set(0);

	change_directory(&rwin, rwin.curr_dir);
	load_dir_list(&rwin, 0);
	change_directory(&lwin, lwin.curr_dir);
	load_dir_list(&lwin, 0);
	change_directory(curr_view, curr_view->curr_dir);

	update_stat_window(curr_view);

	if (curr_view->selected_files)
	{
		char status_buf[24];
		snprintf(status_buf, sizeof(status_buf), "%d %s Selected",
				curr_view->selected_files, 
				curr_view->selected_files == 1 ? "File" : "Files");
		status_bar_message(status_buf);
	}
	else
		status_bar_message(" ");

	
	update_pos_window(curr_view);

	update_all_windows();

	moveto_list_pos(curr_view, curr_view->list_pos);
	wrefresh(curr_view->win);

	curr_stats.freeze = 0;
	curr_stats.need_redraw = 0;

}


