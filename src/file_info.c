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

#include<stdio.h>
#include<sys/time.h>
#include<time.h>
#include <unistd.h>
#include<stdlib.h>
#include<signal.h>
#include<string.h>
#include<sys/stat.h>
#include<pwd.h>
#include<grp.h>

#include "config.h"
#include "filelist.h"
#include "ui.h"
#include "utils.h"


void
describe_file_size (char* buf, int buf_size, FileView *view)
{
	if (view->dir_entry[view->list_pos].size < 10240)	/* less than 10K */
 	{
 		snprintf (buf, buf_size, "%5d  bytes", 
				(int) view->dir_entry[view->list_pos].size);
 	}
 	else if (view->dir_entry[view->list_pos].size < 1048576)/*less than a meg */
 	{
 		snprintf (buf, buf_size, "%5.2f Kbytes",
 			((float) view->dir_entry[view->list_pos].size  / 1024.0));
 	}
 	else			/* more than a meg */
 	{
 		snprintf (buf, buf_size, "%5.2f Mbytes",
 			((float) view->dir_entry[view->list_pos].size / 1048576.0));
 	}
}

void
get_perm_string (char * buf, int len, mode_t mode)
{
  char *perm_sets[] =
    { "---", "--x", "-w-", "-wx", "r--", "r-x", "rw-", "rwx" };
  int u, g, o;

  u = (mode & S_IRWXU) >> 6;
  g = (mode & S_IRWXG) >> 3;
  o = (mode & S_IRWXO);

  snprintf (buf, len, "-%s%s%s", perm_sets[u], perm_sets[g], perm_sets[o]);

  if (S_ISLNK (mode))
    buf[0] = 'l';
  else if (S_ISDIR (mode))
    buf[0] = 'd';
  else if (S_ISBLK (mode))
    buf[0] = 'b';
  else if (S_ISCHR (mode))
    buf[0] = 'c';
  else if (S_ISFIFO (mode))
    buf[0] = 'f';
  else if (S_ISSOCK (mode))
    buf[0] = 's';

  if (mode & S_ISVTX)
    buf[9] = (buf[9] == '-') ? 'T' : 't';
  if (mode & S_ISGID)
    buf[6] = (buf[6] == '-') ? 'S' : 's';
  if (mode & S_ISUID)
    buf[3] = (buf[3] == '-') ? 'S' : 's';
}

void
show_full_file_properties(FileView *view)
{
	char name_buf[20];
	char perm_buf[26];
	char size_buf[56];
	char uid_buf[26];
	char buf[256];
	struct passwd *pwd_buf;
  	struct group *grp_buf;
	struct tm *tm_ptr;
	int x, y;
	
	if(view->win == lwin.win)
		other_view = &rwin;
	else
		other_view = &lwin;

	werase(other_view->title);
	werase(other_view->win);
	werase(stat_win);
	wnoutrefresh(other_view->title);


	snprintf(name_buf, sizeof(name_buf), view->dir_entry[view->list_pos].name);
	describe_file_size(size_buf, sizeof(size_buf), view);
	
	if((pwd_buf = getpwuid(view->dir_entry[view->list_pos].uid)) == NULL)
	{
		snprintf (uid_buf, sizeof(uid_buf), "%d", 
				(int) view->dir_entry[view->list_pos].uid);
	}
	else
	{
		snprintf(uid_buf, sizeof(uid_buf), "%s", pwd_buf->pw_name);
	}
	get_perm_string(perm_buf, sizeof(perm_buf), 
			view->dir_entry[view->list_pos].mode);

	mvwaddstr(other_view->win, 2, 2, "File: ");
	mvwaddstr(other_view->win, 2, 8, name_buf);
	mvwaddstr(other_view->win, 4, 2, "Size: ");
	mvwaddstr(other_view->win, 4, 8, size_buf);

	mvwaddstr(other_view->win, 6, 2, "Type: ");
	if(S_ISLNK(view->dir_entry[view->list_pos].mode))
	{
		char linkto[PATH_MAX +NAME_MAX];
		int len;
		char *filename = strdup(view->dir_entry[view->list_pos].name);
		len = strlen(filename);
		if (filename[len - 1] == '/')
			filename[len - 1] = '\0';

	  	mvwaddstr(other_view->win, 6, 8, "Link");
		len = readlink (filename, linkto, sizeof (linkto));

		mvwaddstr(other_view->win, 7, 2, "Link To: ");
		if (len > 0)
		{
			linkto[len] = '\0';
			mvwaddstr(other_view->win, 7, 11, linkto);
		}
	  	else
			mvwaddstr(other_view->win, 7, 11, "Couldn't Resolve Link");
	}
 	else if (S_ISREG (view->dir_entry[view->list_pos].mode))
    {
		if((S_IXUSR &view->dir_entry[view->list_pos].mode)
				|| (S_IXGRP &view->dir_entry[view->list_pos].mode)
				|| (S_IXOTH &view->dir_entry[view->list_pos].mode))

			mvwaddstr(other_view->win, 6, 8, "Executable");
		else
			mvwaddstr(other_view->win, 6, 8, "Regular File");
    }
	else if (S_ISDIR (view->dir_entry[view->list_pos].mode))
	{
	  mvwaddstr(other_view->win, 6, 8, "Directory");
	}
	else if (S_ISCHR (view->dir_entry[view->list_pos].mode))
	{
	  mvwaddstr(other_view->win, 6, 8, "Character Device");
	}
	else if (S_ISBLK (view->dir_entry[view->list_pos].mode))
	{
	  mvwaddstr(other_view->win, 6, 8, "Block Device");
	}
	else if (S_ISFIFO (view->dir_entry[view->list_pos].mode))
	{
	  mvwaddstr(other_view->win, 6, 8, "Fifo Pipe");
	}
    else if (S_ISSOCK (view->dir_entry[view->list_pos].mode))
    {
	  mvwaddstr(other_view->win, 6, 8, "Socket");
    }
  	else
    {
	  mvwaddstr(other_view->win, 6, 8, "Unknown");
    }

	mvwaddstr(other_view->win, 8, 2, "Permissions: ");
	mvwaddstr(other_view->win, 8, 15, perm_buf);
	mvwaddstr(other_view->win, 10, 2, "Modified: ");
	tm_ptr = localtime(&view->dir_entry[view->list_pos].mtime);
  	strftime (buf, sizeof (buf), "%a %b %d %I:%M %p", tm_ptr);
	mvwaddstr(other_view->win, 10, 13, buf);
	mvwaddstr(other_view->win, 12, 2, "Accessed: ");
	tm_ptr = localtime(&view->dir_entry[view->list_pos].atime);
  	strftime (buf, sizeof (buf), "%a %b %d %I:%M %p", tm_ptr);
	mvwaddstr(other_view->win, 12, 13, buf);
	mvwaddstr(other_view->win, 14, 2, "Changed: ");
	tm_ptr = localtime(&view->dir_entry[view->list_pos].ctime);
  	strftime (buf, sizeof (buf), "%a %b %d %I:%M %p", tm_ptr);
	mvwaddstr(other_view->win, 14, 13, buf);
	mvwaddstr(other_view->win, 16, 2, "Owner: ");
	mvwaddstr(other_view->win, 16, 10, uid_buf);
	mvwaddstr(other_view->win, 18, 2, "Group: ");
	if((grp_buf = getgrgid(view->dir_entry[view->list_pos].gid)) != NULL)
	{
		mvwaddstr(other_view->win, 18, 10, grp_buf->gr_name);
	}
	wnoutrefresh(other_view->win);
	update_pos_window(view);
	getmaxyx(stat_win, y, x);
	snprintf(name_buf, sizeof(name_buf), "%d files filtered", view->filtered);
	if(view->filtered > 0)
		mvwaddstr(stat_win, 0, x - (strlen(name_buf) + 2), name_buf);
	wnoutrefresh(stat_win);

}


