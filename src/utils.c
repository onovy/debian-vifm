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
#include <unistd.h>
#include<stdlib.h>
#include<signal.h>
#include<string.h>
#include<sys/stat.h>

#include "ui.h"
#include "status.h"

/* Checks for a NULL pointer before calling free() */
void
my_free(void *stuff)
{
	if(stuff != NULL)
		free(stuff);
}

int
is_dir(char *file)
{
	struct stat statbuf;
	stat(file, &statbuf);

	if(S_ISDIR(statbuf.st_mode))
		return 1;
	else
		return 0;
}

void *
duplicate (void *stuff, int size)
{
  void *new_stuff = (void *) malloc (size);
  memcpy (new_stuff, stuff, size);
  return new_stuff;
}

/*
 * Escape the filename for the purpose of inserting it into the shell.
 */
char *
escape_filename(const char *string, int quote_percent)
{
    char *ret, *dup;

    dup = ret = (char *)malloc (strlen (string) * 2 + 2 + 1);

		if (*string == '-')
		{
			*dup++ = '.';
			*dup++ = '/';
		}

    for (; *string; string++, dup++)
		{
			switch (*string) 
			{
				case '%':
					if (quote_percent)
						*dup++ = '%';
					break;
				case '\'':
				case '\\':
				case '\r':
				case '\n':
				case '\t':
				case '"':
				case ';':
				case ' ':
				case '?':
				case '|':
				case '[':
				case ']':
				case '{':
				case '}':
				case '<':
				case '>':
				case '`':
				case '!':
				case '$':
				case '&':
				case '*':
				case '(':
				case ')':
						*dup++ = '\\';
						break;
				case '~':
				case '#':
						if (dup == ret)
							*dup++ = '\\';
						break;
		}
		*dup = *string;
  }
  *dup = '\0';
  return ret;
}

void
chomp(char *text)
{
	if(text[strlen(text) -1] == '\n')
		text[strlen(text) -1] = '\0';
}



void
update_term_title(char *title)
{


	/* Check if running in console */ 
	if(curr_stats.is_console)
		return;

	/* This length is arbitrary and should be changed. 
	while(strlen(title) +17 > 50)
	{
		str = strchr(title, '/');
		str++;
	title = strdup(str);
	}
	*/

/*	snprintf(buf, sizeof(buf), "%c]2;vifm - %s%c", 0x1b, title, 0x07);

	printf(buf);
	my_free(title);
	fflush(stdout);
	*/
}

void
clear_term_title(void)
{
	char buf[50] = "";


	/* Check if running in console */ 
	if(curr_stats.is_console)
		return;

	snprintf(buf, sizeof(buf), "%c]2;%c", 0x1b, 0x07);

	printf(buf);
	fflush(stdout);
}
