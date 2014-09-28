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

#define CP_HELP "cp /usr/local/share/vifm/vifm-%.1f.help.txt ~/.vifm"
#define CP_RC "cp /usr/local/share/vifm/vifmrc%.1f ~/.vifm"

#include<stdio.h> /* FILE */
#include<stdlib.h> /* getenv */
#include<unistd.h> /* chdir */
#include<sys/stat.h> /* mkdir */
#include<string.h>
#include<utils.h>
#include<ctype.h> /* isalnum */

#include"bookmarks.h"
#include"fileops.h"
#include"filetype.h"
#include"registers.h"
#include"status.h"
#include"commands.h"
#include"config.h"

#define MAX_LEN 1024
#define DEFAULT_FILENAME_FILTER "\\.o$"

void
init_config(void)
{
	int i;

	/* init bookmarks */
	for (i = 0; i < NUM_BOOKMARKS; ++i)
	{
		bookmarks[i].directory = NULL;
		bookmarks[i].file = NULL;
	}
	cfg.num_bookmarks = 0;
	for ( i = 0; i < NUM_REGISTERS; ++i)
	{
		reg[i].name = valid_registers[i];
		reg[i].num_files = 0;
		reg[i].files = NULL;
		reg[i].deleted = 0;
	}
}


static void
create_help_file(void)
{
	char command[PATH_MAX];

	snprintf(command, sizeof(command), CP_HELP, VERSION);
	file_exec(command);
}

static void
create_rc_file(void)
{
	char command[PATH_MAX];

	snprintf(command, sizeof(command), CP_RC, VERSION);
	file_exec(command);
	add_bookmark('H', getenv("HOME"), "../");
	add_bookmark('z', cfg.config_dir, "../");
}

/* This is just a safety check so that vifm will still load and run if
 * the configuration file is not present.
 */
static void
load_default_configuration(void)
{
	cfg.use_trash = 1;
	cfg.use_color = 1;
	cfg.vi_command = strdup("vim");
	cfg.use_screen = 0;
	cfg.history_len = 15;
	cfg.use_vim_help = 0;
	strncpy(lwin.regexp, "\\..~$", sizeof(lwin.regexp) -1);

	/*
	lwin.filename_filter = (char *)realloc(lwin.filename_filter, 
			strlen("\\.o$") +1);
	strncpy(lwin.filename_filter, "\\.o$", sizeof(lwin.filename_filter));
	lwin.prev_filter = (char *)realloc(lwin.prev_filter, 
			strlen("\\.o$") +1);
	strncpy(lwin.prev_filter, "\\.o$", sizeof(lwin.prev_filter));
	*/

 	lwin.filename_filter = (char *)realloc(lwin.filename_filter, 
 			strlen(DEFAULT_FILENAME_FILTER) +1);
 	strcpy(lwin.filename_filter, DEFAULT_FILENAME_FILTER);
  	lwin.prev_filter = (char *)realloc(lwin.prev_filter, 
 			strlen(DEFAULT_FILENAME_FILTER) +1);
 	strcpy(lwin.prev_filter, DEFAULT_FILENAME_FILTER);

	lwin.invert = TRUE;
	strncpy(rwin.regexp, "\\..~$", sizeof(rwin.regexp) -1);

	/*
	rwin.filename_filter = (char *)realloc(rwin.filename_filter, 
			strlen("\\.o$") +1);
	strncpy(rwin.filename_filter, "\\.o$", sizeof(rwin.filename_filter));
	rwin.prev_filter = (char *)realloc(rwin.prev_filter, 
			strlen("\\.o$") +1);
	strncpy(rwin.prev_filter, "\\.o$", sizeof(rwin.prev_filter));
	*/

  rwin.filename_filter = (char *)realloc(rwin.filename_filter, 
 			strlen(DEFAULT_FILENAME_FILTER) +1);
 	strcpy(rwin.filename_filter, DEFAULT_FILENAME_FILTER);
 	rwin.prev_filter = (char *)realloc(rwin.prev_filter, 
 			strlen(DEFAULT_FILENAME_FILTER) +1);
 	strcpy(rwin.prev_filter, DEFAULT_FILENAME_FILTER);

	rwin.invert = TRUE;
	lwin.sort_type = SORT_BY_NAME;
	rwin.sort_type = SORT_BY_NAME;

	cfg.color[MENU_COLOR].fg = COLOR_CYAN,
	cfg.color[MENU_COLOR].bg = COLOR_BLACK;
	cfg.color[BORDER_COLOR].fg = COLOR_BLACK,
	cfg.color[BORDER_COLOR].bg = COLOR_WHITE;
	cfg.color[WIN_COLOR].fg = COLOR_WHITE,
	cfg.color[WIN_COLOR].bg = COLOR_BLACK;
	cfg.color[STATUS_BAR_COLOR].fg = COLOR_WHITE,
	cfg.color[STATUS_BAR_COLOR].bg = COLOR_BLACK;
	cfg.color[CURR_LINE_COLOR].fg = COLOR_WHITE;
	cfg.color[CURR_LINE_COLOR].bg = COLOR_BLUE;
	cfg.color[DIRECTORY_COLOR].fg = COLOR_CYAN,
	cfg.color[DIRECTORY_COLOR].bg = COLOR_BLACK;
	cfg.color[LINK_COLOR].fg = COLOR_YELLOW,
	cfg.color[LINK_COLOR].bg = COLOR_BLACK;
	cfg.color[SOCKET_COLOR].fg = COLOR_BLUE,
	cfg.color[SOCKET_COLOR].bg = COLOR_BLACK;
	cfg.color[DEVICE_COLOR].fg = COLOR_RED,
	cfg.color[DEVICE_COLOR].bg = COLOR_BLACK;
	cfg.color[EXECUTABLE_COLOR].fg = COLOR_GREEN,
	cfg.color[EXECUTABLE_COLOR].bg = COLOR_BLACK;
	cfg.color[SELECTED_COLOR].fg = COLOR_MAGENTA,
	cfg.color[SELECTED_COLOR].bg = COLOR_WHITE;
	cfg.color[CURRENT_COLOR].fg = COLOR_BLACK,
	cfg.color[CURRENT_COLOR].bg = COLOR_WHITE;

}

void
set_config_dir(void)
{
	char *home_dir = getenv("HOME");

	if(home_dir)
	{
		FILE *f;
		char help_file[PATH_MAX];
		char rc_file[PATH_MAX];

		snprintf(rc_file, sizeof(rc_file), "%s/.vifm/vifmrc%.1f", 
				home_dir, VERSION);
		snprintf(help_file, sizeof(help_file), "%s/.vifm/vifm-%.1f.help_txt", 
				home_dir, VERSION);
		snprintf(cfg.config_dir, sizeof(cfg.config_dir), "%s/.vifm", home_dir);
		snprintf(cfg.trash_dir, sizeof(cfg.trash_dir), "%s/.vifm/Trash", home_dir);

		if(chdir(cfg.config_dir))
		{
			if(mkdir(cfg.config_dir, 0777))
				return;
			if(mkdir(cfg.trash_dir, 0777))
				return;
			if((f = fopen(help_file, "r")) == NULL)
				create_help_file();
			if((f = fopen(rc_file, "r")) == NULL)
				create_rc_file();
		}

	}
}

/*
 * convert possible <color_name> to <int>
 */
int
colname2int(char col[])
{
 /* test if col[] is a number... */
	 if (isdigit(col[0]))
	   return atoi(col);

 /* otherwise convert */
 if(!strcmp(col, "black"))
   return 0;
 if(!strcmp(col, "red"))
   return 1;
 if(!strcmp(col, "green"))
   return 2;
 if(!strcmp(col, "yellow"))
   return 3;
 if(!strcmp(col, "blue"))
   return 4;
 if(!strcmp(col, "magenta"))
   return 5;
 if(!strcmp(col, "cyan"))
   return 6;
 if(!strcmp(col, "white"))
   return 7;
 /* return default color */
 return -1;
}



/*
 * add color
 */
void
add_color(char s1[], char s2[], char s3[])
{
 	int fg, bg;

  fg = colname2int(s2);
  bg = colname2int(s3);

 if(!strcmp(s1, "MENU"))
 {
		cfg.color[MENU_COLOR].fg = fg;
		cfg.color[MENU_COLOR].bg = bg;
	}
	if(!strcmp(s1, "BORDER"))
	{
		cfg.color[BORDER_COLOR].fg = fg;
		cfg.color[BORDER_COLOR].bg = bg;
	}
	if(!strcmp(s1, "WIN"))
	{
			cfg.color[WIN_COLOR].fg = fg;
			cfg.color[WIN_COLOR].bg = bg;
	}
	if(!strcmp(s1, "STATUS_BAR"))
	{
		 cfg.color[STATUS_BAR_COLOR].fg = fg;
		 cfg.color[STATUS_BAR_COLOR].bg = bg;
	 }
	 if(!strcmp(s1, "CURR_LINE"))
	 {
			cfg.color[CURR_LINE_COLOR].fg = fg;
			cfg.color[CURR_LINE_COLOR].bg = bg;
	 }
	 if(!strcmp(s1, "DIRECTORY"))
	 {
		 cfg.color[DIRECTORY_COLOR].fg = fg;
		 cfg.color[DIRECTORY_COLOR].bg = bg;
	 }
	 if(!strcmp(s1, "LINK"))
	 {
			cfg.color[LINK_COLOR].fg = fg;
			cfg.color[LINK_COLOR].bg = bg;
		}
	if(!strcmp(s1, "SOCKET"))
	{
		 cfg.color[SOCKET_COLOR].fg = fg;
		 cfg.color[SOCKET_COLOR].bg = bg;
	 }
	 if(!strcmp(s1, "DEVICE"))
	 {
			cfg.color[DEVICE_COLOR].fg = fg;
			cfg.color[DEVICE_COLOR].bg = bg;
	 }
	 if(!strcmp(s1, "EXECUTABLE"))
	 {
		 cfg.color[EXECUTABLE_COLOR].fg = fg;
		 cfg.color[EXECUTABLE_COLOR].bg = bg;
		}
		if(!strcmp(s1, "SELECTED"))
		{
			 cfg.color[SELECTED_COLOR].fg = fg;
			 cfg.color[SELECTED_COLOR].bg = bg;
		 }
		if(!strcmp(s1, "CURRENT"))
		{
			 cfg.color[CURRENT_COLOR].fg = fg;
			 cfg.color[CURRENT_COLOR].bg = bg;
		 }
}


/*
 * write colors back to config file.
 */
void
write_colors(FILE *fp)
{


	 fprintf(fp, "\n# The standard ncurses colors are:\n");
	 fprintf(fp, "# BLACK 0\n");
	 fprintf(fp, "# RED 1\n");
	 fprintf(fp, "# GREEN 2\n");
	 fprintf(fp, "# YELLOW 3\n");
	 fprintf(fp, "# BLUE 4\n");
	 fprintf(fp, "# MAGENTA 5\n");
	 fprintf(fp, "# CYAN 6\n");
	 fprintf(fp, "# WHITE 7\n");
	 fprintf(fp, "# COLOR=Window_name=foreground_color=background_color\n\n");

	 fprintf(fp, "COLOR=MENU=%d=%d\n",
			   cfg.color[MENU_COLOR].fg,
			   cfg.color[MENU_COLOR].bg);
	 fprintf(fp, "COLOR=BORDER=%d=%d\n",
			    cfg.color[BORDER_COLOR].fg,
			    cfg.color[BORDER_COLOR].bg);
	 fprintf(fp, "COLOR=WIN=%d=%d\n",
			    cfg.color[WIN_COLOR].fg,
			    cfg.color[WIN_COLOR].bg);
	 fprintf(fp, "COLOR=STATUS_BAR=%d=%d\n",
			    cfg.color[STATUS_BAR_COLOR].fg,
			    cfg.color[STATUS_BAR_COLOR].bg);
	 fprintf(fp, "COLOR=CURR_LINE=%d=%d\n",
			    cfg.color[CURR_LINE_COLOR].fg,
			    cfg.color[CURR_LINE_COLOR].bg);
	 fprintf(fp, "COLOR=DIRECTORY=%d=%d\n",
			    cfg.color[DIRECTORY_COLOR].fg,
			    cfg.color[DIRECTORY_COLOR].bg);
	 fprintf(fp, "COLOR=LINK=%d=%d\n",
			    cfg.color[LINK_COLOR].fg,
			    cfg.color[LINK_COLOR].bg);
	 fprintf(fp, "COLOR=SOCKET=%d=%d\n",
			    cfg.color[SOCKET_COLOR].fg,
			    cfg.color[SOCKET_COLOR].bg);
	 fprintf(fp, "COLOR=DEVICE=%d=%d\n",
			    cfg.color[DEVICE_COLOR].fg,
			    cfg.color[DEVICE_COLOR].bg);
	 fprintf(fp, "COLOR=EXECUTABLE=%d=%d\n",
			    cfg.color[EXECUTABLE_COLOR].fg,
			    cfg.color[EXECUTABLE_COLOR].bg);
	 fprintf(fp, "COLOR=SELECTED=%d=%d\n",
			    cfg.color[SELECTED_COLOR].fg,
			    cfg.color[SELECTED_COLOR].bg);
	 fprintf(fp, "COLOR=CURRENT=%d=%d\n",
			    cfg.color[CURRENT_COLOR].fg,
			    cfg.color[CURRENT_COLOR].bg);
}
		

int
read_config_file(void)
{
	FILE *fp;
	char config_file[PATH_MAX];
	char line[MAX_LEN];
	char *s1 = NULL;
	char *s2 = NULL;
	char *s3 = NULL;
	char *sx = NULL;
	int args;


	snprintf(config_file, sizeof(config_file), "%s/vifmrc%.1f", cfg.config_dir, VERSION);

	if((fp = fopen(config_file, "r")) == NULL)
	{
		fprintf(stdout, "Unable to find configuration file.\n Using defaults.");
		load_default_configuration();
		return 0;
	}

	while(fgets(line, MAX_LEN, fp))
	{
		args = 0;
		if(line[0] == '#')
			continue;

		if((sx = s1 = strchr(line, '=')) != NULL)
		{
			s1++;
			chomp(s1);
			*sx = '\0';
			args = 1;
		}
		else
			continue;
		if((sx = s2 = strchr(s1, '=')) != NULL)
		{
			s2++;
			chomp(s2);
			*sx = '\0';
			args = 2;
		}
		/* COMMAND is handled here so that the command can have an '=' */
		if(!strcmp(line, "COMMAND"))
			add_command(s1, s2);
		else if((args == 2) && ((sx = s3 = strchr(s2, '=')) != NULL))
		{
			s3++;
			chomp(s3);
			*sx = '\0';
			args = 3;
		}
		if(args == 1)
		{
			if(!strcmp(line, "VI_COMMAND"))
			{
				if(cfg.vi_command != NULL)
					free(cfg.vi_command);

				cfg.vi_command = strdup(s1);
				continue;
			}

			if(!strcmp(line, "USE_TRASH"))
			{
				cfg.use_trash = atoi(s1);
				continue;
			}
			if(!strcmp(line, "USE_ONE_WINDOW"))
			{
				cfg.show_one_window = atoi(s1);
				continue;
			}
			if(!strcmp(line, "USE_SCREEN"))
			{
				cfg.use_screen = atoi(s1);
				continue;
			}
			if(!strcmp(line, "USE_COLOR"))
			{
				cfg.use_color = atoi(s1);
				continue;
			}
			if(!strcmp(line, "HISTORY_LENGTH"))
			{
				cfg.history_len = atoi(s1);
				continue;
			}
			if(!strcmp(line, "LEFT_WINDOW_SORT_TYPE"))
			{
				lwin.sort_type = atoi(s1);
				continue;
			}
			if(!strcmp(line, "RIGHT_WINDOW_SORT_TYPE"))
			{
				rwin.sort_type = atoi(s1);
				continue;
			}
			if(!strcmp(line, "LWIN_FILTER"))
			{
				lwin.filename_filter = (char *)realloc(lwin.filename_filter, 
				strlen(s1) +1);
				strncpy(lwin.filename_filter, s1, strlen(s1));
				lwin.prev_filter = (char *)realloc(lwin.prev_filter, 
					strlen(s1) +1);
				strncpy(lwin.prev_filter, s1, strlen(s1));
				continue;
			}
			if(!strcmp(line, "LWIN_INVERT"))
			{
				lwin.invert = atoi(s1);
				continue;
			}
			if(!strcmp(line, "RWIN_FILTER"))
			{
				rwin.filename_filter = (char *)realloc(rwin.filename_filter, 
				strlen(s1) +1);
				strncpy(rwin.filename_filter, s1, strlen(s1));
				rwin.prev_filter = (char *)realloc(rwin.prev_filter, 
					strlen(s1) +1);
				strncpy(rwin.prev_filter, s1, strlen(s1));
				continue;
			}
			if(!strcmp(line, "RWIN_INVERT"))
			{
				rwin.invert = atoi(s1);
				continue;
			}
			if(!strcmp(line, "USE_VIM_HELP"))
			{
				cfg.use_vim_help = atoi(s1);
				continue;
			}
			/*
			if(!strcmp(line, "USE_AUTOMATIC_UPDATE"))
			{
				cfg.auto_update = atoi(s1);
				continue;
			}
			*/
			if(!strcmp(line, "RUN_EXECUTABLE"))
			{
				cfg.auto_execute = atoi(s1);
				continue;
			}
		}
		if(args == 3)
		{
			if(!strcmp(line, "FILETYPE"))
			{
				add_filetype(s1, s2, s3);
				continue;
			}
			if(!strcmp(line, "BOOKMARKS"))
			{
				if(isalnum(*s1))
					add_bookmark(*s1, s2, s3);
				continue;
			}
			if(!strcmp(line, "COLOR"))
			{
				add_color(s1, s2, s3);
				continue;
			}
		}
	}


	fclose(fp);

	return 1;
}

void
write_config_file(void)
{
	FILE *fp;
	int x;
	char config_file[PATH_MAX];
	curr_stats.getting_input = 1;

	snprintf(config_file, sizeof(config_file), "%s/vifmrc%.1f", cfg.config_dir,
			VERSION);

	if((fp = fopen(config_file, "w")) == NULL)
		return;

	fprintf(fp, "# You can edit this file by hand.\n");
	fprintf(fp, "# The # character at the beginning of a line comments out the line.\n");
	fprintf(fp, "# Blank lines are ignored.\n");
	fprintf(fp, "# The basic format for each item is shown with an example.\n");
	fprintf(fp,
		"# The '=' character is used to separate fields within a single line.\n");
	fprintf(fp, "# Most settings are true = 1 or false = 0.\n");
	
	fprintf(fp, "\n# This is the actual command used to start vi.  The default is vi.\n");
	fprintf(fp,
			"# If you would like to use another vi clone such as Vim, Elvis, or Vile\n");
	fprintf(fp, "# you will need to change this setting.\n");
	fprintf(fp, "\nVI_COMMAND=%s", cfg.vi_command);
	fprintf(fp, "\n# VI_COMMAND=vim");
	fprintf(fp, "\n# VI_COMMAND=elvis -G termcap");
	fprintf(fp, "\n# VI_COMMAND=vile");
	fprintf(fp, "\n");
	fprintf(fp, "\n# Trash Directory\n");
	fprintf(fp, "# The default is to move files that are deleted with dd or :d to\n");
	fprintf(fp, "# the trash directory.  1 means use the trash directory 0 means\n");
	fprintf(fp, "# just use rm.  If you change this you will not be able to move\n");
	fprintf(fp, "# files by deleting them and then using p to put the file in the new location.\n");
	fprintf(fp, "# I recommend not changing this until you are familiar with vifm.\n");
	fprintf(fp, "# This probably shouldn't be an option.\n");
	fprintf(fp, "\nUSE_TRASH=%d\n", cfg.use_trash);

	fprintf(fp, "\n# Show only one Window\n");
	fprintf(fp, "# If you would like to start vifm with only one window set this to 1\n");
	fprintf(fp, "\nUSE_ONE_WINDOW=%d\n", curr_stats.number_of_windows == 1 ? 1 : 0);

	fprintf(fp, "\n# Screen configuration.  If you would like to use vifm with\n"); 
	fprintf(fp, "# the screen program set this to 1.\n");
	fprintf(fp, "\nUSE_SCREEN=%d\n", cfg.use_screen);

	fprintf(fp, "\n# 1 means use color if the terminal supports it.\n");
	fprintf(fp, "# 0 means don't use color even if supported.\n");
	fprintf(fp, "\nUSE_COLOR=%d\n", cfg.use_color);


	fprintf(fp, "\n# This is how many files to show in the directory history menu.\n");
	fprintf(fp, "\nHISTORY_LENGTH=%d\n", cfg.history_len);

	fprintf(fp, "\n# The sort type is how the files will be sorted in the file listing.\n");
	fprintf(fp, "# Sort by File Extension = 0\n");
	fprintf(fp, "# Sort by File Name = 1\n");
	fprintf(fp, "# Sort by Group ID = 2\n");
	fprintf(fp, "# Sort by Group Name = 3\n");
	fprintf(fp, "# Sort by Mode = 4\n");
	fprintf(fp, "# Sort by Owner ID = 5\n");
	fprintf(fp, "# Sort by Owner Name = 6\n");
	fprintf(fp, "# Sort by Size = 7\n");
	fprintf(fp, "# Sort by Time Accessed =8\n");
	fprintf(fp, "# Sort by Time Changed =9\n");
	fprintf(fp, "# Sort by Time Modified =10\n");
	fprintf(fp, "# This can be set with the :sort command in vifm.\n");
	fprintf(fp, "\nLEFT_WINDOW_SORT_TYPE=%d\n", lwin.sort_type);
	fprintf(fp, "\nRIGHT_WINDOW_SORT_TYPE=%d\n", rwin.sort_type);

	fprintf(fp, "\n# The regular expression used to filter files out of\n");
	fprintf(fp, "# the directory listings.\n");
	fprintf(fp, "# LWIN_FILTER=\\.o$ and LWIN_INVERT=1 would filter out all\n");
	fprintf(fp, "# of the .o files from the directory listing. LWIN_INVERT=0\n");
	fprintf(fp, "# would show only the .o files\n");
	fprintf(fp, "\nLWIN_FILTER=%s\n", lwin.filename_filter);
	fprintf(fp, "LWIN_INVERT=%d\n", lwin.invert);
	fprintf(fp, "RWIN_FILTER=%s\n", rwin.filename_filter);
	fprintf(fp, "RWIN_INVERT=%d\n", rwin.invert);

	fprintf(fp, "\n# If you installed the vim.txt help file change this to 1.\n");
	fprintf(fp, "# If would rather use a plain text help file set this to 0.\n");
	fprintf(fp, "\nUSE_VIM_HELP=%d\n", cfg.use_vim_help);

	/* Removed
	fprintf(fp, "\n# Setting this to 1 will result in the file list being \n");
	fprintf(fp, "# reloaded if it has been changed.  If you turn this off\n");
	fprintf(fp, "# you can use CTRL-L to refresh the screen.\n");
	fprintf(fp, "\nUSE_AUTOMATIC_UPDATE=%d\n", cfg.auto_update);
	*/

	write_colors(fp);

	fprintf(fp, "\n# If you would like to run an executable file when you \n");
	fprintf(fp, "# press return on the file name set this to 1.\n");
	fprintf(fp, "\nRUN_EXECUTABLE=%d\n", cfg.auto_execute);

	fprintf(fp, "\n# BOOKMARKS=mark=/full/directory/path=filename\n\n");
	for(x = 0; x < NUM_BOOKMARKS; x++)
	{
		if (is_bookmark(x))
		{
			fprintf(fp, "BOOKMARKS=%c=%s=%s\n",
					index2mark(x),
					bookmarks[x].directory, bookmarks[x].file);
		}
	}

	fprintf(fp, "\n# COMMAND=command_name=action\n");
	fprintf(fp, "# The following macros can be used in a command\n");
	fprintf(fp, "# %%a is replaced with the user arguments.\n");
	fprintf(fp, "# %%f the current selected file, or files.\n");
	fprintf(fp, "# %%F the current selected file, or files in the other directoy.\n");
	fprintf(fp, "# %%d the current directory name.\n");
	fprintf(fp, "# %%D the other window directory name.\n\n");
	fprintf(fp, "# %%m run the command in a menu window\n");
	fprintf(fp, "# %%s run the command in a new screen region\n");
	for(x = 0; x < cfg.command_num; x++)
	{
		fprintf(fp, "COMMAND=%s=%s\n", command_list[x].name, 
				command_list[x].action);
	}

	fprintf(fp, "\n# The file type is for the default programs to be used with\n");
	fprintf(fp, "# a file extension. \n");
	fprintf(fp, "# FILETYPE=description=extension1,extension2=defaultprogram, program2\n");
	fprintf(fp, "# FILETYPE=Web=html,htm,shtml=links,mozilla,elvis\n");
	fprintf(fp, "# would set links as the default program for .html .htm .shtml files\n");
	fprintf(fp, "# The other programs for the file type can be accessed with the :file command\n");
	fprintf(fp, "# The command macros %%f, %%F, %%d, %%F may be used in the commands.\n");
	fprintf(fp, "# The %%a macro is ignored.  To use a %% you must put %%%%.\n\n");
	for(x = 0; x < cfg.filetypes_num; x++)
	{
		fprintf(fp, "FILETYPE=%s=%s=%s\n", filetypes[x].type, filetypes[x].ext, filetypes[x].programs);
	}

	fclose(fp);
	curr_stats.getting_input = 0;
}
