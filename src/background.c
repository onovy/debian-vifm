/* vifm Copyright (C) 2001 Ken Steen.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA
 */

#include<string.h>
#include<unistd.h>
#include<time.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/wait.h>
#include<fcntl.h>

#include"background.h" 
#include"menus.h"
#include"status.h"
#include"utils.h"

struct Jobs_List *jobs = NULL;
struct Finished_Jobs *fjobs = NULL;

static void 
add_background_job(pid_t pid, char *cmd, int fd) 
{
	Jobs_List *new;

	new = (Jobs_List *)malloc(sizeof(Jobs_List)); 
	new->pid = pid;
	new->cmd = strdup(cmd);
	new->next = jobs;
	new->fd = fd; 
	new->error_buf = (char *)calloc(1, sizeof(char));
	new->running = 1;
	new->stopped = 0;
	jobs = new;
}

void 
add_finished_job(pid_t pid, int status)
{
	Finished_Jobs *new;

	new = (Finished_Jobs *)malloc(sizeof(Finished_Jobs)); 
	new->pid = pid;
	new->remove = 0;
	new->next = fjobs;
	fjobs = new;
}

void
check_background_jobs(void)
{
	Jobs_List *p = jobs;
	Jobs_List *prev = 0;
	Finished_Jobs *fj = NULL;
	sigset_t new_mask;
	fd_set ready;
	int maxfd;
	int nread;
	struct timeval ts;


	if (!p)
		return;

	/*
	 * SIGCHLD  needs to be blocked anytime the Finished_Jobs list 
	 * is accessed from anywhere except the received_sigchld().
	 */
	sigemptyset(&new_mask);
	sigaddset(&new_mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &new_mask, NULL);

	fj = fjobs;

	ts.tv_sec = 0;
	ts.tv_usec = 100;

	while (p)
	{
		/* Mark any finished jobs */
		while (fj)
		{
			if (p->pid == fj->pid)
			{
				p->running = 0;
				fj->remove = 1;
			}
			fj = fj->next;
		}

		/* Setup pipe for reading */

		FD_ZERO(&ready);
		maxfd = 0;
		FD_SET(p->fd, &ready);
		maxfd = (p->fd > maxfd ? p->fd : maxfd);

		if ((select(maxfd +1, &ready, NULL, NULL, &ts) > 0))
		{
			char buf[256];

			nread = read(p->fd, buf, sizeof(buf) -1);

			if (nread)
			{
				p->error_buf = (char *) realloc(p->error_buf, 
						sizeof(buf));

				strncat(p->error_buf, buf, sizeof(buf) - 1);
			}
			if (strlen(p->error_buf) > 1)
			{
				show_error_msg(" Background Process Error ", p->error_buf);
				my_free(p->error_buf);
				p->error_buf = (char *) calloc(1, sizeof(char));
			}
		}

		/* Remove any finished jobs. */
		if (!p->running)
		{
			Jobs_List *j = p;
			if (prev)
				prev->next = p->next;
			else 
				jobs = p->next;

			p = p->next;
			my_free(j->cmd);

			if (strlen(j->error_buf))
				my_free(j->error_buf);

			my_free(j);
		}
		else
		{
			prev = p;
			p = p->next;
		}
	}
	 
	/* Clean up Finished Jobs list */
	fj = fjobs;
	if (fj)
	{
		Finished_Jobs *prev = 0;
		while (fj)
		{
			if (fj->remove)
			{
				Finished_Jobs *j = fj;

				if (prev)
					prev->next = fj->next;
				else
					fjobs = fj->next;

				fj = fj->next;
				my_free(j);
			}
			else
			{
				prev = fj;
				fj = fj->next;
			}
		}
	}

	/* Unblock SIGCHLD signal */
	sigprocmask(SIG_UNBLOCK, &new_mask, NULL);
}

int 
start_background_job(char *cmd)
{ 
	pid_t pid;
	char *args[4];
	int error_pipe[2];

	if (pipe(error_pipe) != 0)
	{
		show_error_msg(" File pipe error", 
				"Error creating pipe in background.c line 84");
		return -1;
	}

	if ((pid = fork()) == -1)
		return -1;

	if (pid == 0)
	{
		int nullfd;
		close(2);        /* Close stderr */
		dup(error_pipe[1]);  /* Redirect stderr to write end of pipe. */
		close(error_pipe[0]); /* Close read end of pipe. */
		close(0); /* Close stdin */
		close(1); /* Close stdout */ 

		/* Send stdout, stdin to /dev/null */
		if ((nullfd = open("/dev/null", O_RDONLY)) != -1)
		{
			if (dup2(nullfd, 0) == -1)
				;
			if (dup2(nullfd, 1) == -1)
				;
		}

		args[0] = "sh";
		args[1] = "-c";
		args[2] = cmd;
		args[3] = NULL;

		execvp(args[0], args);
		exit(-1);
	}
	else
	{
		close(error_pipe[1]); /* Close write end of pipes. */

		add_background_job(pid, cmd, error_pipe[0]);
	}
	return 0;
}
