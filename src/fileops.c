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
#include<unistd.h>
#include<errno.h> /* errno */
#include<sys/wait.h> /* waitpid() */
#include<sys/types.h> /* waitpid() */
#include<sys/stat.h> /* stat */
#include<stdio.h>
#include<string.h>

#include"background.h"
#include"commands.h"
#include"config.h"
#include"filelist.h"
#include"fileops.h"
#include"filetype.h"
#include"menus.h"
#include"status.h"
#include"ui.h"
#include"utils.h"

int 
my_system(char *command)
{
	int pid;
	int status;
	extern char **environ;

	if(command == 0)
		return 1;

	pid = fork();
	if(pid == -1)
		return -1;
	if(pid == 0)
	{
		char *args[4];

		args[0] = "sh";
		args[1] = "-c";
		args[2] = command;
		args[3] = 0;
		execve("/bin/sh", args, environ);
		exit(127);
	}
	do
	{
		if(waitpid(pid, &status, 0) == -1)
		{
			if(errno != EINTR)
				return -1;
		}
		else
			return status;
	}while(1);
}

static int
execute(char **args)
{
	int pid;

	if((pid = fork()) == 0)
	{
		/* Run as a separate session */
		setsid();
		close(0);
		execvp(args[0], args);
		exit(127);
	}

	return pid;
}

void
yank_selected_files(FileView *view)
{
	int x;
	size_t namelen;
	int old_list = curr_stats.num_yanked_files;



	if(curr_stats.yanked_files)
	{
		for(x = 0; x < old_list; x++)
		{
			if(curr_stats.yanked_files[x])
			{
				my_free(curr_stats.yanked_files[x]);
				curr_stats.yanked_files[x] = NULL;
			}
		}
		my_free(curr_stats.yanked_files);
		curr_stats.yanked_files = NULL;
	}

	curr_stats.yanked_files = (char **)calloc(view->selected_files, 
			sizeof(char *));

	for(x = 0; x < view->selected_files; x++)
	{
		if(view->selected_filelist[x])
		{
		namelen = strlen(view->selected_filelist[x]);
		curr_stats.yanked_files[x] = malloc(namelen +1);
		strcpy(curr_stats.yanked_files[x], view->selected_filelist[x]);
		}
		else
		{
			x--;
			break;
		}
	}
	curr_stats.num_yanked_files = x;
	strncpy(curr_stats.yanked_files_dir, view->curr_dir,
			sizeof(curr_stats.yanked_files_dir) -1);
}

/* execute command. */
int
file_exec(char *command)
{
	char *args[4];
	pid_t pid;

	args[0] = "sh";
	args[1] = "-c";
	args[2] = command;
	args[3] = NULL;

	pid = execute(args);
	return pid;
}

void
view_file(FileView *view)
{
	char command[PATH_MAX + 5] = "";
	char *filename = get_current_file_name(view);

	snprintf(command, sizeof(command), "%s \"%s\"", cfg.vi_command, filename);
	shellout(command, 0);
	curs_set(0);
}




void
handle_file(FileView *view)
{
	if(DIRECTORY == view->dir_entry[view->list_pos].type)
	{
		char *filename = get_current_file_name(view);
		change_directory(view, filename);
		load_dir_list(view, 0);
		moveto_list_pos(view, view->curr_line);
		return;
	}
	if(EXECUTABLE == view->dir_entry[view->list_pos].type)
	{
		if(cfg.auto_execute)
		{
			char buf[NAME_MAX];
			snprintf(buf, sizeof(buf), "./%s", get_current_file_name(view));
			shellout(buf, 1);
			return;
		}
		else
			view_file(view);
	}
	if(REGULAR == view->dir_entry[view->list_pos].type)
	{
		char *program = NULL;
		if(cfg.vim_filter)
		{
			FILE *fp;
			char filename[PATH_MAX] = "";

			snprintf(filename, sizeof(filename), "%s/vimfiles", cfg.config_dir);
 			fp = fopen(filename, "w");
			snprintf(filename, sizeof(filename), "%s/%s",
					view->curr_dir,
					view->dir_entry[view->list_pos].name);
			endwin();
			fprintf(fp, filename);
			fclose(fp);
			exit(0);

		}
		else if((program = get_default_program_for_file(
					view->dir_entry[view->list_pos].name)) != NULL)
		{
			if(strchr(program, '%'))
			{
				char *command = expand_macros(view, program, NULL);
				shellout(command, 0);
				my_free(command);
				return;
			}
			else
			{
				char buf[NAME_MAX *2];
				char *temp = escape_filename(view->dir_entry[view->list_pos].name, 0);

				snprintf(buf, sizeof(buf), "%s %s", program, temp); 
				shellout(buf, 0);
				my_free(program);
				my_free(temp);
				return;
			}
		}
		else /* vi is set as the default for any extension without a program */
			view_file(view);

		return;
	}
	if(LINK == view->dir_entry[view->list_pos].type)
	{
		char linkto[PATH_MAX +NAME_MAX];
		int len;
		char *filename = strdup(view->dir_entry[view->list_pos].name);
		len = strlen(filename);
		if (filename[len - 1] == '/')
			filename[len - 1] = '\0';

		len = readlink (filename, linkto, sizeof (linkto));

		if (len > 0)
		{
			struct stat s;
			int is_dir = 0;
			int is_file = 0;
			char *dir = NULL;
			char *file = NULL;
			char *link_dup = strdup(linkto);
			linkto[len] = '\0';
			lstat(linkto, &s);
			
			if((s.st_mode & S_IFMT) == S_IFDIR)
			{
				is_dir = 1;
				dir = strdup(linkto);
			}
			else
			{
				int x;
				for(x = strlen(linkto); x > 0; x--)
				{
					if(linkto[x] == '/')
					{
						linkto[x] = '\0';
						lstat(linkto, &s);
						if((s.st_mode & S_IFMT) == S_IFDIR)
						{
							is_dir = 1;
							dir = strdup(linkto);
							break;
						}
					}
				}
				if((file = strrchr(link_dup, '/')))
				{
					file++;
					is_file = 1;
				}
			}
			if(is_dir)
			{
				change_directory(view, dir);
				load_dir_list(view, 0);

				if(is_file)
				{
					int pos = find_file_pos_in_list(view, file);
					if(pos >= 0)
						moveto_list_pos(view, pos);

				}
				else
				{
					moveto_list_pos(view, 0);
				}
			}
			else
			{
				int pos = find_file_pos_in_list(view, link_dup);
				if(pos >= 0)
					moveto_list_pos(view, pos);
			}
			my_free(link_dup);
		}
	  	else
			status_bar_message("Couldn't Resolve Link");
	}
}


int
pipe_and_capture_errors(char *command)
{
  int file_pipes[2];
  int pid;
	int nread;
	int error = 0;
  char *args[4];

  if (pipe (file_pipes) != 0)
      return 1;

  if ((pid = fork ()) == -1)
      return 1;

  if (pid == 0)
    {
			close(1);
			close(2);
			dup(file_pipes[1]);
      close (file_pipes[0]);
      close (file_pipes[1]);

      args[0] = "sh";
      args[1] = "-c";
      args[2] = command;
      args[3] = NULL;
      execvp (args[0], args);
      exit (127);
    }
  else
    {
			char buf[1024];
      close (file_pipes[1]);
			while((nread = read(*file_pipes, buf, sizeof(buf) -1)) > 0)
			{
				buf[nread] = '\0';
				error = nread;
			}
			if(error > 1)
			{
				char title[strlen(command) +4];
				snprintf(title, sizeof(title), " %s ", command);
				show_error_msg(title, buf);
				return 1;
			}
    }
	return 0;
}

void
delete_file(FileView *view)
{
	char buf[256];
	int x;

	if(!view->selected_files)
	{
		view->dir_entry[view->list_pos].selected = 1;
		view->selected_files = 1;
	}

	get_all_selected_files(view);
	yank_selected_files(view);
	strncpy(curr_stats.yanked_files_dir, cfg.trash_dir,
			sizeof(curr_stats.yanked_files_dir) -1);
	for(x = 0; x < view->selected_files; x++)
	{
		if(!strcmp("../", view->selected_filelist[x]))
		{
			status_bar_message("You cannot delete the ../ directory");
			continue;
		}

		if(cfg.use_trash)
				snprintf(buf, sizeof(buf), "mv \"%s\" %s",
					view->selected_filelist[x],  cfg.trash_dir);
		else
			snprintf(buf, sizeof(buf), "rm -fr '%s'",
					 view->selected_filelist[x]);

		start_background_job(buf);
	}
	free_selected_file_array(view);
	view->selected_files = 0;

	load_dir_list(view, 1);

	moveto_list_pos(view, view->list_pos);
	/*
	char *buf = NULL;
	char *files = expand_macros(view, "%f", NULL);

	buf = (char *)malloc(strlen(files) + PATH_MAX);

	if(cfg.use_trash)
		snprintf(buf, strlen(files) + PATH_MAX, "mv %s %s &", files,  cfg.trash_dir);
	else
		snprintf(buf, strlen(files) + 24, "rm -fr %s &", files);

	start_background_job(buf);

	my_free(files);
	my_free(buf);

	load_dir_list(view, 1);

	moveto_list_pos(view, view->list_pos);
	*/
}
