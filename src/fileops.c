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

#include "fileops.h"

#include <regex.h>

#include <pthread.h>

#include <dirent.h> /* DIR dirent opendir() readdir() closedir() */
#include <fcntl.h>
#include <sys/stat.h> /* stat */
#include <sys/types.h> /* waitpid() */
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include <unistd.h>

#include <assert.h> /* assert() */
#include <ctype.h> /* isdigit() tolower() */
#include <errno.h> /* errno */
#include <stddef.h> /* NULL size_t */
#include <stdint.h> /* uint64_t */
#include <stdio.h> /* snprintf() */
#include <stdlib.h> /* free() malloc() strtol() */
#include <string.h> /* memcmp() memset() strcpy() strdup() strerror() */

#include "cfg/config.h"
#include "io/ioeta.h"
#include "io/ionotif.h"
#include "menus/menus.h"
#include "modes/cmdline.h"
#ifdef _WIN32
#include "utils/env.h"
#endif
#include "utils/fs.h"
#include "utils/fs_limits.h"
#include "utils/macros.h"
#include "utils/path.h"
#include "utils/str.h"
#include "utils/string_array.h"
#include "utils/tree.h"
#include "utils/test_helpers.h"
#include "utils/utils.h"
#include "background.h"
#include "commands_completion.h"
#include "filelist.h"
#include "ops.h"
#include "registers.h"
#include "running.h"
#include "status.h"
#include "trash.h"
#include "types.h"
#include "ui.h"
#include "undo.h"

static char rename_file_ext[NAME_MAX];

static struct
{
	registers_t *reg;
	FileView *view;
	int force_move;
	int x, y;
	char *name;
	int skip_all;      /* Skip all conflicting files/directories. */
	int overwrite_all;
	int append;        /* Whether we're appending ending of a file or not. */
	int allow_merge;
	int merge;         /* Merge conflicting directory once. */
	int merge_all;     /* Merge all conflicting directories. */
	int link;          /* 0 - no, 1 - absolute, 2 - relative */
	ops_t *ops;
}
put_confirm;

typedef struct
{
	char **list;
	int nlines;
	int move;
	int force;
	char **sel_list;
	size_t sel_list_len;
	char src[PATH_MAX];
	char path[PATH_MAX];
	int from_file;
	int from_trash;
}
bg_args_t;

/* Arguments pack for dir_size_bg() background function. */
typedef struct
{
	char *path; /* Full path to directory to process, will be freed. */
	int force;  /* Whether cached values should be ignored. */
}
dir_size_args_t;

static void io_progress_changed(const io_progress_t *const progress);
static void format_pretty_path(const char base_dir[], const char path[],
		char pretty[], size_t pretty_size);
static int prepare_register(int reg);
static void delete_files_in_bg(void *arg);
static void delete_files_bg_i(const char curr_dir[], char *list[], int count,
		int use_trash);
TSTATIC int is_name_list_ok(int count, int nlines, char *list[], char *files[]);
TSTATIC int is_rename_list_ok(char *files[], int *is_dup, int len,
		char *list[]);
TSTATIC const char * add_to_name(const char filename[], int k);
TSTATIC int check_file_rename(const char dir[], const char old[],
		const char new[], SignalType signal_type);
#ifndef _WIN32
static int complete_owner(const char str[], void *arg);
#endif
static int is_file_name_changed(const char old[], const char new[]);
static void change_owner_cb(const char new_owner[]);
#ifndef _WIN32
static int complete_group(const char str[], void *arg);
#endif
static int complete_filename(const char str[], void *arg);
static void put_confirm_cb(const char dest_name[]);
static void prompt_what_to_do(const char src_name[]);
TSTATIC const char * gen_clone_name(const char normal_name[]);
static void clone_file(FileView* view, const char filename[], const char path[],
		const char clone[], ops_t *ops);
static void put_decide_cb(const char dest_name[]);
static void put_continue(int force);
static int is_dir_entry(const char full_path[], const struct dirent* dentry);
static int initiate_put_files_from_register(FileView *view, OPS op,
		const char descr[], int reg_name, int force_move, int link);
static void reset_put_confirm(OPS main_op, const char descr[],
		const char base_dir[]);
static int put_files_from_register_i(FileView *view, int start);
static int mv_file(const char src[], const char src_path[], const char dst[],
		const char path[], int tmpfile_num, int cancellable, ops_t *ops);
static int cp_file(const char src_dir[], const char dst_dir[], const char src[],
		const char dst[], int type, int cancellable, ops_t *ops);
static int have_read_access(FileView *view);
static char ** edit_list(size_t count, char **orig, int *nlines,
		int ignore_change);
static int edit_file(const char filepath[], int force_changed);
static ops_t * get_ops(OPS main_op, const char descr[], const char base_dir[]);
static void progress_msg(const char text[], int ready, int total);
static void cpmv_in_bg(void *arg);
static void general_prepare_for_bg_task(FileView *view, bg_args_t *args);
static const char * get_cancellation_suffix(void);
static void update_dir_entry_size(const FileView *view, int index, int force);
static void start_dir_size_calc(const char path[], int force);
static void * dir_size_bg(void *arg);
static uint64_t calc_dirsize(const char path[], int force_update);
static void set_dir_size(const char path[], uint64_t size);

void
init_fileops(void)
{
	ionotif_register(&io_progress_changed);
}

/* I/O operation update callback. */
static void
io_progress_changed(const io_progress_t *const state)
{
	enum { PRECISION = 10 };

	static int prev_progress = -1;

	const ioeta_estim_t *const estim = state->estim;
	ops_t *const ops = estim->param;

	char current_size_str[16];
	char total_size_str[16];
	int progress;
	char pretty_path[PATH_MAX];

	if(state->stage == IO_PS_ESTIMATING)
	{
		progress = estim->total_items/PRECISION;
	}
	else if(estim->total_bytes == 0)
	{
		progress = 0;
	}
	else if(prev_progress >= 100*PRECISION &&
			estim->current_byte == estim->total_bytes)
	{
		/* Special handling for unknown total size. */
		++prev_progress;
		if(prev_progress%PRECISION != 0)
		{
			return;
		}
		progress = -1;
	}
	else
	{
		progress = (estim->current_byte*100*PRECISION)/estim->total_bytes;
	}

	if(progress == prev_progress)
	{
		return;
	}
	else if(progress >= 0)
	{
		prev_progress = progress;
	}

	(void)friendly_size_notation(estim->total_bytes, sizeof(total_size_str),
			total_size_str);

	format_pretty_path(ops->base_dir, estim->item, pretty_path,
			sizeof(pretty_path));

	switch(state->stage)
	{
		case IO_PS_ESTIMATING:
			ui_sb_quick_msgf("%s: estimating... %d; %s %s", ops_describe(ops),
					estim->total_items, total_size_str, pretty_path);
			break;
		case IO_PS_IN_PROGRESS:
			(void)friendly_size_notation(estim->current_byte,
					sizeof(current_size_str), current_size_str);

			if(progress < 0)
			{
				/* Simplified message for unknown total size. */
				ui_sb_quick_msgf("%s: %d of %d; %s %s", ops_describe(ops),
						estim->current_item + 1, estim->total_items,
						total_size_str, pretty_path);
			}
			else
			{
				ui_sb_quick_msgf("%s: %d of %d; %s/%s (%2d%%) %s", ops_describe(ops),
						estim->current_item + 1, estim->total_items,
						current_size_str, total_size_str, progress/PRECISION, pretty_path);
			}
			break;
	}
}

/* Pretty prints path shortening it by skipping base directory path if
 * possible, otherwise fallbacks to the full path. */
static void
format_pretty_path(const char base_dir[], const char path[], char pretty[],
		size_t pretty_size)
{
	if(!path_starts_with(path, base_dir))
	{
		copy_str(pretty, pretty_size, path);
		return;
	}

	copy_str(pretty, pretty_size, skip_char(path + strlen(base_dir), '/'));
}

/* returns new value for save_msg */
int
yank_files(FileView *view, int reg, int count, int *indexes)
{
	int yanked;

	if(count > 0)
	{
		capture_files_at(view, count, indexes);
	}
	else
	{
		capture_target_files(view);
	}

	yank_selected_files(view, reg);
	yanked = view->selected_files;
	free_file_capture(view);
	recount_selected_files(view);

	if(count == 0)
	{
		clean_selected_files(view);
		redraw_view(view);
	}

	status_bar_messagef("%d %s yanked", yanked, yanked == 1 ? "file" : "files");

	return yanked;
}

void
yank_selected_files(FileView *view, int reg)
{
	int x;

	reg = prepare_register(reg);

	for(x = 0; x < view->selected_files; x++)
	{
		char buf[PATH_MAX];
		if(view->selected_filelist[x] == NULL)
			break;

		snprintf(buf, sizeof(buf), "%s%s%s", view->curr_dir,
				ends_with_slash(view->curr_dir) ? "" : "/", view->selected_filelist[x]);
		chosp(buf);
		append_to_register(reg, buf);
	}
	update_unnamed_reg(reg);
}

/* buf should be at least COMMAND_GROUP_INFO_LEN characters length */
static void
get_group_file_list(char **list, int count, char *buf)
{
	size_t len;
	int i;

	len = strlen(buf);
	for(i = 0; i < count && len < COMMAND_GROUP_INFO_LEN; i++)
	{
		if(buf[len - 2] != ':')
		{
			strncat(buf, ", ", COMMAND_GROUP_INFO_LEN - len - 1);
			len = strlen(buf);
		}
		strncat(buf, list[i], COMMAND_GROUP_INFO_LEN - len - 1);
		len = strlen(buf);
	}
}

/* returns new value for save_msg */
int
delete_files(FileView *view, int reg, int count, int *indexes, int use_trash)
{
	char buf[MAX(COMMAND_GROUP_INFO_LEN, 8 + PATH_MAX*2)];
	int i;
	int sel_len;
	ops_t *ops;
	char *dst_hint;

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
	{
		return 0;
	}

	use_trash = use_trash && cfg.use_trash;

	if(use_trash && is_under_trash(view->curr_dir))
	{
		show_error_msg("Can't perform deletion",
				"Current directory is under Trash directory");
		return 0;
	}

	if(count > 0)
	{
		capture_files_at(view, count, indexes);
	}
	else
	{
		capture_target_files(view);
	}

	if(use_trash)
	{
		reg = prepare_register(reg);
	}

	snprintf(buf, sizeof(buf), "%celete in %s: ", use_trash ? 'd' : 'D',
			replace_home_part(view->curr_dir));

	get_group_file_list(view->selected_filelist, view->selected_files, buf);
	cmd_group_begin(buf);

	if(vifm_chdir(curr_view->curr_dir) != 0)
	{
		show_error_msg("Directory return", "Can't chdir() to current directory");
		return 1;
	}

	ops = get_ops(OP_REMOVE, use_trash ? "deleting" : "Deleting", view->curr_dir);

	ui_cancellation_reset();

	sel_len = view->selected_files;

	dst_hint = use_trash ? pick_trash_dir(view->curr_dir) : NULL;

	for(i = 0; i < sel_len && !ui_cancellation_requested(); ++i)
	{
		char full_buf[PATH_MAX];
		const char *const fname = view->selected_filelist[i];

		snprintf(full_buf, sizeof(full_buf), "%s/%s", view->curr_dir, fname);
		chosp(full_buf);

		ops_enqueue(ops, full_buf, dst_hint);
	}

	free(dst_hint);

	for(i = 0; i < sel_len && !ui_cancellation_requested(); ++i)
	{
		const char *const fname = view->selected_filelist[i];
		char full_buf[PATH_MAX];
		int result;

		if(is_parent_dir(fname))
		{
			show_error_msg("Can't perform deletion",
					"You cannot delete the ../ directory");
			continue;
		}

		snprintf(full_buf, sizeof(full_buf), "%s/%s", view->curr_dir, fname);
		chosp(full_buf);

		progress_msg("Deleting files", i, sel_len);
		if(use_trash)
		{
			if(!is_trash_directory(full_buf))
			{
				char *const dest = gen_trash_name(view->curr_dir, fname);
				if(dest != NULL)
				{
					result = perform_operation(OP_MOVE, ops, NULL, full_buf, dest);
					/* For some reason "rm" sometimes returns 0 on cancellation. */
					if(path_exists(full_buf))
					{
						result = -1;
					}
					if(result == 0)
					{
						add_operation(OP_MOVE, NULL, NULL, full_buf, dest);
						append_to_register(reg, dest);
					}
					free(dest);
				}
				else
				{
					show_error_msgf("No trash directory is available",
							"Either correct trash directory paths or prune files.  "
							"Deletion failed on: %s", fname);
					result = -1;
				}
			}
			else
			{
				show_error_msg("Can't perform deletion",
						"You cannot delete trash directory to trash");
				result = -1;
			}
		}
		else
		{
			result = perform_operation(OP_REMOVE, ops, NULL, full_buf, NULL);
			/* For some reason "rm" sometimes returns 0 on cancellation. */
			if(path_exists(full_buf))
			{
				result = -1;
			}
			if(result == 0)
			{
				add_operation(OP_REMOVE, NULL, NULL, full_buf, "");
			}
		}

		if(result == 0 && strcmp(view->dir_entry[view->list_pos].name, fname) == 0)
		{
			if(view->list_pos + 1 < view->list_rows)
			{
				view->list_pos++;
			}
		}

		ops_advance(ops, result == 0);
	}
	free_file_capture(view);

	update_unnamed_reg(reg);

	cmd_group_end();

	ui_view_reset_selection_and_reload(view);

	status_bar_messagef("%d %s deleted%s", ops->succeeded,
			(ops->succeeded == 1) ? "file" : "files", get_cancellation_suffix());

	ops_free(ops);

	return 1;
}

/* Transforms "A-"Z register to "a-"z or clears the reg.  So that for "A-"Z new
 * values will be appended to "a-"z, for other registers old values will be
 * removed.  Returns possibly modified value of the reg parameter. */
static int
prepare_register(int reg)
{
	if(reg >= 'A' && reg <= 'Z')
	{
		reg += 'a' - 'A';
	}
	else
	{
		clear_register(reg);
	}
	return reg;
}

static void
delete_files_bg_i(const char curr_dir[], char *list[], int count, int use_trash)
{
	int i;
	for(i = 0; i < count; i++)
	{
		const char *const fname = list[i];
		char full_buf[PATH_MAX];

		if(is_parent_dir(fname))
		{
			continue;
		}

		snprintf(full_buf, sizeof(full_buf), "%s/%s", curr_dir, fname);
		chosp(full_buf);

		if(use_trash)
		{
			if(!is_trash_directory(full_buf))
			{
				char *const trash_name = gen_trash_name(curr_dir, fname);
				const char *const dest = (trash_name != NULL) ? trash_name : fname;
				(void)perform_operation(OP_MOVE, NULL, (void *)1, full_buf, dest);
				free(trash_name);
			}
		}
		else
		{
			(void)perform_operation(OP_REMOVE, NULL, (void *)1, full_buf, NULL);
		}
		inner_bg_next();
	}
}

int
delete_files_bg(FileView *view, int use_trash)
{
	char task_desc[COMMAND_GROUP_INFO_LEN];
	int i;
	bg_args_t *args;

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
		return 0;

	args = malloc(sizeof(*args));
	args->from_trash = cfg.use_trash && use_trash;

	if(args->from_trash && is_under_trash(view->curr_dir))
	{
		show_error_msg("Can't perform deletion",
				"Current directory is under Trash directory");
		free(args);
		return 0;
	}

	args->list = NULL;
	args->nlines = 0;
	args->move = 0;
	args->force = 0;

	capture_target_files(view);

	i = view->list_pos;
	while(i < view->list_rows - 1 && view->dir_entry[i].selected)
		i++;

	view->list_pos = i;

	general_prepare_for_bg_task(view, args);

	snprintf(task_desc, sizeof(task_desc), "%celete in %s: ",
			args->from_trash ? 'd' : 'D', replace_home_part(view->curr_dir));

	get_group_file_list(view->selected_filelist, view->selected_files, task_desc);

	if(bg_execute(task_desc, args->sel_list_len, &delete_files_in_bg, args) != 0)
	{
		free_string_array(args->sel_list, args->sel_list_len);
		free(args);

		show_error_msg("Can't perform deletion",
				"Failed to initiate background operation");
	}
	return 0;
}

/* Entry point for a background task that deletes files. */
static void
delete_files_in_bg(void *arg)
{
	bg_args_t *const args = arg;

	delete_files_bg_i(args->src, args->sel_list, args->sel_list_len,
			args->from_trash);

	free_string_array(args->sel_list, args->sel_list_len);
	free(args);
}

static void
rename_file_cb(const char new_name[])
{
	char *filename = get_current_file_name(curr_view);
	char buf[MAX(COMMAND_GROUP_INFO_LEN, 10 + NAME_MAX + 1)];
	char new[strlen(new_name) + 1 + strlen(rename_file_ext) + 1 + 1];
	size_t len;
	int mv_res;
	char **filename_ptr;

	if(is_null_or_empty(new_name))
	{
		return;
	}

	if(contains_slash(new_name))
	{
		status_bar_error("Name can not contain slash");
		curr_stats.save_msg = 1;
		return;
	}

	len = strlen(filename);
	snprintf(new, sizeof(new), "%s%s%s%s", new_name,
			(rename_file_ext[0] == '\0') ? "" : ".", rename_file_ext,
			(filename[len - 1] == '/') ? "/" : "");

	if(check_file_rename(curr_view->curr_dir, filename, new, ST_DIALOG) <= 0)
	{
		return;
	}

	snprintf(buf, sizeof(buf), "rename in %s: %s to %s",
			replace_home_part(curr_view->curr_dir), filename, new);
	cmd_group_begin(buf);
	mv_res = mv_file(filename, curr_view->curr_dir, new, curr_view->curr_dir, 0,
			1, NULL);
	cmd_group_end();
	if(mv_res != 0)
	{
		show_error_msg("Rename Error", "Rename operation failed");
		return;
	}

	/* Rename file in internal structures for correct positioning of cursor after
	 * reloading, as cursor will be positioned on the file with the same name.
	 * TODO: maybe create a function in ui or filelist to do this. */
	filename_ptr = &curr_view->dir_entry[curr_view->list_pos].name;
	(void)replace_string(filename_ptr, new);

	ui_view_schedule_reload(curr_view);
}

static int
complete_filename_only(const char str[], void *arg)
{
	filename_completion(str, CT_FILE_WOE);
	return 0;
}

void
rename_current_file(FileView *view, int name_only)
{
	const char *const old = get_current_file_name(view);
	char filename[strlen(old) + 1];

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
		return;

	snprintf(filename, sizeof(filename), "%s", old);
	if(is_parent_dir(filename))
	{
		show_error_msg("Rename error",
				"You can't rename parent directory this way");
		return;
	}

	chosp(filename);

	if(name_only)
	{
		copy_str(rename_file_ext, sizeof(rename_file_ext), cut_extension(filename));
	}
	else
	{
		rename_file_ext[0] = '\0';
	}

	clean_selected_files(view);
	enter_prompt_mode(L"New name: ", filename, rename_file_cb,
			complete_filename_only, 1);
}

TSTATIC int
is_name_list_ok(int count, int nlines, char *list[], char *files[])
{
	int i;

	if(nlines < count)
	{
		status_bar_errorf("Not enough file names (%d/%d)", nlines, count);
		curr_stats.save_msg = 1;
		return 0;
	}

	if(nlines > count)
	{
		status_bar_errorf("Too many file names (%d/%d)", nlines, count);
		curr_stats.save_msg = 1;
		return 0;
	}

	for(i = 0; i < count; i++)
	{
		chomp(list[i]);

		if(files != NULL)
		{
			char *file_s = find_slashr(files[i]);
			char *list_s = find_slashr(list[i]);
			if(list_s != NULL || file_s != NULL)
			{
				if(list_s - list[i] != file_s - files[i] ||
						strnoscmp(files[i], list[i], list_s - list[i]) != 0)
				{
					if(file_s == NULL)
						status_bar_errorf("Name \"%s\" contains slash", list[i]);
					else
						status_bar_errorf("Won't move \"%s\" file", files[i]);
					curr_stats.save_msg = 1;
					return 0;
				}
			}
		}

		if(list[i][0] != '\0' && is_in_string_array(list, i, list[i]))
		{
			status_bar_errorf("Name \"%s\" duplicates", list[i]);
			curr_stats.save_msg = 1;
			return 0;
		}

		if(list[i][0] == '\0')
			continue;
	}

	return 1;
}

/* Returns number of renamed files. */
static int
perform_renaming(FileView *view, char **files, int *is_dup, int len,
		char **list)
{
	char buf[MAX(10 + NAME_MAX, COMMAND_GROUP_INFO_LEN) + 1];
	size_t buf_len;
	int i;
	int renamed = 0;

	buf_len = snprintf(buf, sizeof(buf), "rename in %s: ",
			replace_home_part(view->curr_dir));

	for(i = 0; i < len && buf_len < COMMAND_GROUP_INFO_LEN; i++)
	{
		if(buf[buf_len - 2] != ':')
		{
			strncat(buf, ", ", sizeof(buf) - buf_len - 1);
			buf_len = strlen(buf);
		}
		buf_len += snprintf(buf + buf_len, sizeof(buf) - buf_len, "%s to %s",
				files[i], list[i]);
	}

	cmd_group_begin(buf);

	for(i = 0; i < len; i++)
	{
		const char *unique_name;

		if(list[i][0] == '\0')
			continue;
		if(strcmp(list[i], files[i]) == 0)
			continue;
		if(!is_dup[i])
			continue;

		unique_name = make_name_unique(files[i]);
		if(mv_file(files[i], view->curr_dir, unique_name, view->curr_dir, 2, 1,
				NULL) != 0)
		{
			cmd_group_end();
			if(!last_cmd_group_empty())
				undo_group();
			status_bar_error("Temporary rename error");
			curr_stats.save_msg = 1;
			return 0;
		}
		(void)replace_string(&files[i], unique_name);
	}

	for(i = 0; i < len; i++)
	{
		if(list[i][0] == '\0')
			continue;
		if(strcmp(list[i], files[i]) == 0)
			continue;

		if(mv_file(files[i], view->curr_dir, list[i], view->curr_dir,
				is_dup[i] ? 1 : 0, 1, NULL) == 0)
		{
			int pos;

			renamed++;

			pos = find_file_pos_in_list(view, files[i]);
			if(pos == view->list_pos)
			{
				(void)replace_string(&view->dir_entry[pos].name, list[i]);
			}
		}
	}

	cmd_group_end();

	return renamed;
}

static void
rename_files_ind(FileView *view, char **files, int *is_dup, int len)
{
	char **list;
	int nlines;

	if(len == 0 || (list = edit_list(len, files, &nlines, 0)) == NULL)
	{
		status_bar_message("0 files renamed");
		curr_stats.save_msg = 1;
		return;
	}

	if(is_name_list_ok(len, nlines, list, files) &&
			is_rename_list_ok(files, is_dup, len, list))
	{
		const int renamed = perform_renaming(view, files, is_dup, len, list);
		if(renamed >= 0)
		{
			status_bar_messagef("%d file%s renamed", renamed,
					(renamed == 1) ? "" : "s");
			curr_stats.save_msg = 1;
		}
	}

	free_string_array(list, nlines);
}

static char **
add_files_to_list(const char *path, char **files, int *len)
{
	DIR* dir;
	struct dirent* dentry;
	const char* slash = "";

	if(!is_dir(path))
	{
		*len = add_to_string_array(&files, *len, 1, path);
		return files;
	}

	dir = opendir(path);
	if(dir == NULL)
		return files;

	if(path[strlen(path) - 1] != '/')
		slash = "/";

	while((dentry = readdir(dir)) != NULL)
	{
		if(!is_builtin_dir(dentry->d_name))
		{
			char buf[PATH_MAX];
			snprintf(buf, sizeof(buf), "%s%s%s", path, slash, dentry->d_name);
			files = add_files_to_list(buf, files, len);
		}
	}

	closedir(dir);
	return files;
}

int
rename_files(FileView *view, char **list, int nlines, int recursive)
{
	char **files = NULL;
	int len;
	int i;
	int *is_dup;

	if(recursive && nlines != 0)
	{
		status_bar_error("Recursive rename doesn't accept list of new names");
		return 1;
	}

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
		return 0;

	if(view->selected_files == 0)
	{
		view->dir_entry[view->list_pos].selected = 1;
		view->selected_files = 1;
	}

	len = 0;
	for(i = 0; i < view->list_rows; i++)
	{
		if(!view->dir_entry[i].selected)
			continue;
		if(is_parent_dir(view->dir_entry[i].name))
			continue;
		if(recursive)
		{
			files = add_files_to_list(view->dir_entry[i].name, files, &len);
		}
		else
		{
			len = add_to_string_array(&files, len, 1, view->dir_entry[i].name);
			chosp(files[len - 1]);
		}
	}

	is_dup = calloc(len, sizeof(*is_dup));
	if(is_dup == NULL)
	{
		free_string_array(files, len);
		show_error_msg("Memory Error", "Unable to allocate enough memory");
		return 0;
	}

	if(nlines == 0)
	{
		rename_files_ind(view, files, is_dup, len);
	}
	else
	{
		int renamed = -1;

		if(is_name_list_ok(len, nlines, list, files) &&
				is_rename_list_ok(files, is_dup, len, list))
			renamed = perform_renaming(view, files, is_dup, len, list);

		if(renamed >= 0)
			status_bar_messagef("%d file%s renamed", renamed,
					(renamed == 1) ? "" : "s");
	}

	free_string_array(files, len);
	free(is_dup);

	clean_selected_files(view);
	redraw_view(view);
	curr_stats.save_msg = 1;
	return 1;
}

/* Checks rename correctness and forms an array of duplication marks.
 * Directory names in files array should be without trailing slash. */
TSTATIC int
is_rename_list_ok(char *files[], int *is_dup, int len, char *list[])
{
	int i;
	for(i = 0; i < len; i++)
	{
		int j;

		const int check_result =
			check_file_rename(curr_view->curr_dir, files[i], list[i], ST_NONE);
		if(check_result < 0)
		{
			continue;
		}

		for(j = 0; j < len; j++)
		{
			if(strcmp(list[i], files[j]) == 0 && !is_dup[j])
			{
				is_dup[j] = 1;
				break;
			}
		}
		if(j >= len && check_result == 0)
		{
			break;
		}
	}
	return i >= len;
}

static void
make_undo_string(FileView *view, char *buf, int nlines, char **list)
{
	int i;
	size_t len = strlen(buf);
	for(i = 0; i < view->selected_files && len < COMMAND_GROUP_INFO_LEN; i++)
	{
		if(buf[len - 2] != ':')
		{
			strncat(buf, ", ", COMMAND_GROUP_INFO_LEN - len - 1);
			len = strlen(buf);
		}
		strncat(buf, view->selected_filelist[i], COMMAND_GROUP_INFO_LEN - len - 1);
		len = strlen(buf);
		if(nlines > 0)
		{
			strncat(buf, " to ", COMMAND_GROUP_INFO_LEN - len - 1);
			len = strlen(buf);
			strncat(buf, list[i], COMMAND_GROUP_INFO_LEN - len - 1);
			len = strlen(buf);
		}
	}
}

/* Returns number of digets in passed number. */
static int
count_digits(int number)
{
	int result = 0;
	while(number != 0)
	{
		number /= 10;
		result++;
	}
	return MAX(1, result);
}

/* Returns pointer to a statically allocated buffer */
TSTATIC const char *
add_to_name(const char filename[], int k)
{
	static char result[NAME_MAX];
	char format[16];
	char *b, *e;
	int i, n;

	if((b = strpbrk(filename, "0123456789")) == NULL)
	{
		copy_str(result, sizeof(result), filename);
		return result;
	}

	n = 0;
	while(b[n] == '0' && isdigit(b[n + 1]))
	{
		n++;
	}

	if(b != filename && b[-1] == '-')
	{
		b--;
	}

	i = strtol(b, &e, 10);

	if(i + k < 0)
	{
		n++;
	}

	snprintf(result, b - filename + 1, "%s", filename);
	snprintf(format, sizeof(format), "%%0%dd%%s", n + count_digits(i));
	snprintf(result + (b - filename), sizeof(result) - (b - filename), format,
			i + k, e);

	return result;
}

/* Returns new value for save_msg flag. */
int
incdec_names(FileView *view, int k)
{
	size_t names_len;
	char **names;
	size_t tmp_len = 0;
	char **tmp_names = NULL;
	char buf[MAX(NAME_MAX, COMMAND_GROUP_INFO_LEN)];
	int i;
	int err = 0;
	int renames = 0;

	capture_target_files(view);
	names_len = view->selected_files;
	names = copy_string_array(view->selected_filelist, names_len);

	snprintf(buf, sizeof(buf), "<c-a> in %s: ",
			replace_home_part(view->curr_dir));
	make_undo_string(view, buf, 0, NULL);

	if(!view->user_selection)
		clean_selected_files(view);

	for(i = 0; i < names_len; i++)
	{
		if(strpbrk(names[i], "0123456789") == NULL)
		{
			remove_from_string_array(names, names_len--, i--);
			continue;
		}
		chosp(names[i]);
		tmp_len = add_to_string_array(&tmp_names, tmp_len, 1,
				make_name_unique(names[i]));
	}

	for(i = 0; i < names_len; i++)
	{
		const char *p = add_to_name(names[i], k);
#ifndef _WIN32
		if(is_in_string_array(names, names_len, p))
#else
		if(is_in_string_array_case(names, names_len, p))
#endif
			continue;
		if(check_file_rename(view->curr_dir, names[i], p, ST_STATUS_BAR) != 0)
			continue;

		err = -1;
		break;
	}

	cmd_group_begin(buf);
	for(i = 0; i < names_len && !err; i++)
	{
		if(mv_file(names[i], view->curr_dir, tmp_names[i], view->curr_dir, 4, 1,
				NULL) != 0)
		{
			err = 1;
			break;
		}
		renames++;
	}
	for(i = 0; i < names_len && !err; i++)
	{
		if(mv_file(tmp_names[i], view->curr_dir, add_to_name(names[i], k),
				view->curr_dir, 3, 1, NULL) != 0)
		{
			err = 1;
			break;
		}
		renames++;
	}
	cmd_group_end();

	free_string_array(names, names_len);
	free_string_array(tmp_names, tmp_len);

	if(err)
	{
		if(err > 0 && !last_cmd_group_empty())
			undo_group();
	}
	else if(view->dir_entry[view->list_pos].selected || !view->user_selection)
	{
		char **filename = &view->dir_entry[view->list_pos].name;
		(void)replace_string(filename, add_to_name(*filename, k));
	}

	clean_selected_files(view);
	if(renames > 0)
	{
		ui_view_schedule_full_reload(view);
	}

	if(err > 0)
	{
		status_bar_error("Rename error");
	}
	else if(err == 0)
	{
		status_bar_messagef("%d file%s renamed", names_len,
				(names_len == 1) ? "" : "s");
	}

	return 1;
}

/* Returns value > 0 if rename is correct, < 0 if rename isn't needed and 0
 * when rename operation should be aborted. silent parameter controls whether
 * error dialog or status bar message should be shown, 0 means dialog. */
TSTATIC int
check_file_rename(const char dir[], const char old[], const char new[],
		SignalType signal_type)
{
	if(!is_file_name_changed(old, new))
	{
		return -1;
	}

	if(path_exists_at(dir, new) && stroscmp(old, new) != 0)
	{
		switch(signal_type)
		{
			case ST_STATUS_BAR:
				status_bar_errorf("File \"%s\" already exists", new);
				curr_stats.save_msg = 1;
				break;
			case ST_DIALOG:
				show_error_msg("File exists",
						"That file already exists. Will not overwrite.");
				break;

			default:
				assert(signal_type == ST_NONE && "Unhandled signaling type");
				break;
		}
		return 0;
	}

	return 1;
}

/* Checks whether file name change was performed.  Returns non-zero if change is
 * detected, otherwise zero is returned. */
static int
is_file_name_changed(const char old[], const char new[])
{
	/* Empty new name means reuse of the old name (rename cancellation).  Names
	 * are always compared in a case sensitive way, so that changes in case of
	 * letters triggers rename operation even for systems where paths are case
	 * insensitive. */
	return (new[0] != '\0' && strcmp(old, new) != 0);
}

#ifndef _WIN32
void
chown_files(int u, int g, uid_t uid, gid_t gid)
{
	char buf[COMMAND_GROUP_INFO_LEN + 1];
	int i;
	int sel_len;

	ui_cancellation_reset();

	snprintf(buf, sizeof(buf), "ch%s in %s: ", ((u && g) || u) ? "own" : "grp",
			replace_home_part(curr_view->curr_dir));

	get_group_file_list(curr_view->saved_selection, curr_view->nsaved_selection,
			buf);
	cmd_group_begin(buf);

	sel_len = curr_view->nsaved_selection;
	for(i = 0; i < sel_len && !ui_cancellation_requested(); i++)
	{
		char *filename = curr_view->saved_selection[i];
		int pos = find_file_pos_in_list(curr_view, filename);

		if(u && perform_operation(OP_CHOWN, NULL, (void *)(long)uid, filename,
					NULL) == 0)
			add_operation(OP_CHOWN, (void *)(long)uid,
					(void *)(long)curr_view->dir_entry[pos].uid, filename, "");
		if(g && perform_operation(OP_CHGRP, NULL, (void *)(long)gid, filename,
					NULL) == 0)
			add_operation(OP_CHGRP, (void *)(long)gid,
					(void *)(long)curr_view->dir_entry[pos].gid, filename, "");
	}
	cmd_group_end();

	load_dir_list(curr_view, 1);
	move_to_list_pos(curr_view, curr_view->list_pos);
}
#endif

void
change_owner(void)
{
	if(curr_view->selected_filelist == 0)
	{
		curr_view->dir_entry[curr_view->list_pos].selected = 1;
		curr_view->selected_files = 1;
	}
	clean_selected_files(curr_view);
#ifndef _WIN32
	enter_prompt_mode(L"New owner: ", "", change_owner_cb, &complete_owner, 0);
#else
	enter_prompt_mode(L"New owner: ", "", change_owner_cb, NULL, 0);
#endif
}

#ifndef _WIN32
static int
complete_owner(const char str[], void *arg)
{
	complete_user_name(str);
	return 0;
}
#endif

static void
change_owner_cb(const char new_owner[])
{
#ifndef _WIN32
	uid_t uid;

	if(is_null_or_empty(new_owner))
	{
		return;
	}

	if(get_uid(new_owner, &uid) != 0)
	{
		status_bar_errorf("Invalid user name: \"%s\"", new_owner);
		curr_stats.save_msg = 1;
		return;
	}

	chown_files(1, 0, uid, 0);
#endif
}

static void
change_group_cb(const char new_group[])
{
#ifndef _WIN32
	gid_t gid;

	if(is_null_or_empty(new_group))
	{
		return;
	}

	if(get_gid(new_group, &gid) != 0)
	{
		status_bar_errorf("Invalid group name: \"%s\"", new_group);
		curr_stats.save_msg = 1;
		return;
	}

	chown_files(0, 1, 0, gid);
#endif
}

void
change_group(void)
{
	if(curr_view->selected_filelist == 0)
	{
		curr_view->dir_entry[curr_view->list_pos].selected = 1;
		curr_view->selected_files = 1;
	}
	clean_selected_files(curr_view);
#ifndef _WIN32
	enter_prompt_mode(L"New group: ", "", change_group_cb, &complete_group, 0);
#else
	enter_prompt_mode(L"New group: ", "", change_group_cb, NULL, 0);
#endif
}

#ifndef _WIN32
static int
complete_group(const char str[], void *arg)
{
	complete_group_name(str);
	return 0;
}
#endif

static void
change_link_cb(const char new_target[])
{
	char buf[MAX(COMMAND_GROUP_INFO_LEN, PATH_MAX)];
	char linkto[PATH_MAX];
	const char *filename;

	if(is_null_or_empty(new_target))
	{
		return;
	}

	curr_stats.confirmed = 1;

	filename = curr_view->dir_entry[curr_view->list_pos].name;
	if(get_link_target(filename, linkto, sizeof(linkto)) != 0)
	{
		show_error_msg("Error", "Can't read link");
		return;
	}

	snprintf(buf, sizeof(buf), "cl in %s: on %s from \"%s\" to \"%s\"",
			replace_home_part(curr_view->curr_dir), filename, linkto, new_target);
	cmd_group_begin(buf);

	snprintf(buf, sizeof(buf), "%s/%s", curr_view->curr_dir, filename);
	chosp(buf);

	if(perform_operation(OP_REMOVESL, NULL, NULL, buf, NULL) == 0)
		add_operation(OP_REMOVESL, NULL, NULL, buf, linkto);
	if(perform_operation(OP_SYMLINK2, NULL, NULL, new_target, buf) == 0)
		add_operation(OP_SYMLINK2, NULL, NULL, new_target, buf);

	cmd_group_end();
}

int
change_link(FileView *view)
{
	char linkto[PATH_MAX];

	if(!symlinks_available())
	{
		show_error_msg("Symbolic Links Error",
				"Your OS doesn't support symbolic links");
		return 0;
	}

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
		return 0;

	if(view->dir_entry[view->list_pos].type != LINK)
	{
		status_bar_error("File isn't a symbolic link");
		return 1;
	}

	if(get_link_target(view->dir_entry[view->list_pos].name, linkto,
			sizeof(linkto)) != 0)
	{
		show_error_msg("Error", "Can't read link");
		return 0;
	}

	enter_prompt_mode(L"Link target: ", linkto, change_link_cb,
			&complete_filename, 0);
	return 0;
}

static int
complete_filename(const char str[], void *arg)
{
	const char *name_begin = after_last(str, '/');
	filename_completion(str, CT_ALL_WOE);
	return name_begin - str;
}

static void
prompt_dest_name(const char *src_name)
{
	wchar_t buf[256];

	vifm_swprintf(buf, ARRAY_LEN(buf), L"New name for %" WPRINTF_MBSTR L": ",
			src_name);
	enter_prompt_mode(buf, src_name, put_confirm_cb, NULL, 0);
}

/* The force argument enables overwriting/replacing/merging.  Returns 0 on
 * success, otherwise non-zero is returned. */
static int
put_next(const char dest_name[], int force)
{
	char *filename;
	struct stat src_st;
	char src_buf[PATH_MAX], dst_buf[PATH_MAX];
	int from_trash;
	int op;
	int move;
	int success;
	int merge;

	/* TODO: refactor this function (put_next()) */

	if(ui_cancellation_requested())
	{
		return -1;
	}

	force = force || put_confirm.overwrite_all || put_confirm.merge_all;
	merge = put_confirm.merge || put_confirm.merge_all;

	filename = put_confirm.reg->files[put_confirm.x];
	chosp(filename);
	if(lstat(filename, &src_st) != 0)
	{
		/* File isn't there, assume that it's fine and don't error in this case. */
		return 0;
	}

	from_trash = is_under_trash(filename);
	move = from_trash || put_confirm.force_move;

	copy_str(src_buf, sizeof(src_buf), filename);

	if(dest_name[0] == '\0')
	{
		if(from_trash)
		{
			dest_name = get_real_name_from_trash_name(src_buf);
		}
		else
		{
			dest_name = find_slashr(src_buf) + 1;
		}
	}

	snprintf(dst_buf, sizeof(dst_buf), "%s/%s", put_confirm.view->curr_dir,
			dest_name);
	chosp(dst_buf);

	if(!put_confirm.append && path_exists(dst_buf))
	{
		if(force)
		{
			struct stat dst_st;
			if(lstat(dst_buf, &dst_st) == 0 && (!merge ||
					S_ISDIR(dst_st.st_mode) != S_ISDIR(src_st.st_mode)))
			{
				if(perform_operation(OP_REMOVESL, NULL, NULL, dst_buf, NULL) != 0)
				{
					return 0;
				}

				/* Schedule view update to reflect changes in UI. */
				ui_view_schedule_reload(put_confirm.view);
			}
			else if(!cfg.use_system_calls && get_env_type() == ET_UNIX)
			{
				remove_last_path_component(dst_buf);
			}
		}
		else if(put_confirm.skip_all)
		{
			return 0;
		}
		else
		{
			struct stat dst_st;
			put_confirm.allow_merge = lstat(dst_buf, &dst_st) == 0 &&
					S_ISDIR(dst_st.st_mode) && S_ISDIR(src_st.st_mode);
			prompt_what_to_do(dest_name);
			return 1;
		}
	}

	if(put_confirm.link)
	{
		op = OP_SYMLINK;
		if(put_confirm.link == 2)
		{
			copy_str(src_buf, sizeof(src_buf),
					make_rel_path(filename, put_confirm.view->curr_dir));
		}
	}
	else if(put_confirm.append)
	{
		op = move ? OP_MOVEA : OP_COPYA;
		put_confirm.append = 0;
	}
	else if(move)
	{
		op = merge ? OP_MOVEF : OP_MOVE;
	}
	else
	{
		op = merge ? OP_COPYF : OP_COPY;
	}

	progress_msg("Putting files", put_confirm.x, put_confirm.reg->num_files);

	/* Merging directory on move requires special handling as it can't be done by
	 * "mv" itself. */
	if(move && merge)
	{
		DIR *dir;

		success = 1;

		cmd_group_continue();

		if((dir = opendir(src_buf)) != NULL)
		{
			struct dirent *d;
			while((d = readdir(dir)) != NULL)
			{
				if(!is_builtin_dir(d->d_name))
				{
					char src_path[PATH_MAX];
					char dst_path[PATH_MAX];
					snprintf(src_path, sizeof(src_path), "%s/%s", src_buf, d->d_name);
					snprintf(dst_path, sizeof(dst_path), "%s/%s/%s",
							put_confirm.view->curr_dir, dest_name, d->d_name);
					if(perform_operation(OP_MOVEF, put_confirm.ops, NULL, src_path,
								dst_path) != 0)
					{
						success = 0;
						break;
					}
					add_operation(OP_MOVEF, put_confirm.ops, NULL, src_path, dst_path);
				}
			}
			closedir(dir);
		}
		else
		{
			success = 0;
		}

		if(success)
		{
			success = (perform_operation(OP_RMDIR, put_confirm.ops, NULL, src_buf,
						NULL) == 0);
			if(success)
			{
				add_operation(OP_RMDIR, NULL, NULL, src_buf, "");
			}
		}

		cmd_group_end();
	}
	else
	{
		success = (perform_operation(op, put_confirm.ops, NULL, src_buf,
					dst_buf) == 0);
	}

	ops_advance(put_confirm.ops, success);

	if(success)
	{
		char *msg, *p;
		size_t len;

		/* For some reason "mv" sometimes returns 0 on cancellation. */
		if(!path_exists(dst_buf))
		{
			return -1;
		}

		cmd_group_continue();

		msg = replace_group_msg(NULL);
		len = strlen(msg);
		p = realloc(msg, COMMAND_GROUP_INFO_LEN);
		if(p == NULL)
			len = COMMAND_GROUP_INFO_LEN;
		else
			msg = p;

		snprintf(msg + len, COMMAND_GROUP_INFO_LEN - len, "%s%s",
				(msg[len - 2] != ':') ? ", " : "", dest_name);
		replace_group_msg(msg);
		free(msg);

		if(!(move && merge))
		{
			add_operation(op, NULL, NULL, src_buf, dst_buf);
		}

		cmd_group_end();
		put_confirm.y++;
		if(move)
		{
			free(put_confirm.reg->files[put_confirm.x]);
			put_confirm.reg->files[put_confirm.x] = NULL;
		}
	}

	return 0;
}

static void
put_confirm_cb(const char dest_name[])
{
	if(!is_null_or_empty(dest_name) && put_next(dest_name, 0) == 0)
	{
		put_confirm.x++;
		curr_stats.save_msg = put_files_from_register_i(put_confirm.view, 0);
	}
}

static void
put_decide_cb(const char choice[])
{
	if(is_null_or_empty(choice) || strcmp(choice, "r") == 0)
	{
		prompt_dest_name(put_confirm.name);
	}
	else if(strcmp(choice, "s") == 0 || strcmp(choice, "S") == 0)
	{
		if(strcmp(choice, "S") == 0)
		{
			put_confirm.skip_all = 1;
		}

		put_confirm.x++;
		curr_stats.save_msg = put_files_from_register_i(put_confirm.view, 0);
	}
	else if(strcmp(choice, "o") == 0)
	{
		put_continue(1);
	}
	else if(strcmp(choice, "p") == 0 && cfg.use_system_calls &&
			!is_dir(put_confirm.name))
	{
		put_confirm.append = 1;
		put_continue(0);
	}
	else if(strcmp(choice, "O") == 0)
	{
		put_confirm.overwrite_all = 1;
		put_continue(1);
	}
	else if(put_confirm.allow_merge && strcmp(choice, "m") == 0)
	{
		put_confirm.merge = 1;
		put_continue(1);
	}
	else if(put_confirm.allow_merge && strcmp(choice, "M") == 0)
	{
		put_confirm.merge_all = 1;
		put_continue(1);
	}
	else
	{
		prompt_what_to_do(put_confirm.name);
	}
}

/* Continues putting files. */
static void
put_continue(int force)
{
	if(put_next("", force) == 0)
	{
		++put_confirm.x;
		curr_stats.save_msg = put_files_from_register_i(put_confirm.view, 0);
	}
}

/* Prompt user for conflict resolution strategy about given filename. */
static void
prompt_what_to_do(const char src_name[])
{
	wchar_t buf[NAME_MAX];

	(void)replace_string(&put_confirm.name, src_name);
	vifm_swprintf(buf, ARRAY_LEN(buf), L"Name conflict for %" WPRINTF_MBSTR
			L". [r]ename/[s]kip/[S]kip all%" WPRINTF_MBSTR
			"/[o]verwrite/[O]verwrite all"
			"%" WPRINTF_MBSTR "%" WPRINTF_MBSTR ": ",
			src_name,
			(cfg.use_system_calls && !is_dir(src_name)) ? "/[a]ppend the end" : "",
			put_confirm.allow_merge ? "/[m]erge" : "",
			put_confirm.allow_merge ? "/[M]erge all" : "");
	enter_prompt_mode(buf, "", put_decide_cb, NULL, 0);
}

/* Returns new value for save_msg flag. */
int
put_files_from_register(FileView *view, int reg_name, int force_move)
{
	if(force_move)
	{
		return initiate_put_files_from_register(view, OP_MOVE, "Putting", reg_name,
				force_move, 0);
	}
	else
	{
		return initiate_put_files_from_register(view, OP_COPY, "putting", reg_name,
				force_move, 0);
	}
}

TSTATIC const char *
gen_clone_name(const char normal_name[])
{
	static char result[NAME_MAX];

	char extension[NAME_MAX];
	int i;
	size_t len;
	char *p;

	copy_str(result, sizeof(result), normal_name);
	chosp(result);

	copy_str(extension, sizeof(extension), cut_extension(result));

	len = strlen(result);
	i = 1;
	if(result[len - 1] == ')' && (p = strrchr(result, '(')) != NULL)
	{
		char *t;
		long l;
		if((l = strtol(p + 1, &t, 10)) > 0 && t[1] == '\0')
		{
			len = p - result;
			i = l + 1;
		}
	}

	do
	{
		snprintf(result + len, sizeof(result) - len, "(%d)%s%s", i++,
				(extension[0] == '\0') ? "" : ".", extension);
	}
	while(path_exists(result));

	return result;
}

static int
is_clone_list_ok(int count, char **list)
{
	int i;
	for(i = 0; i < count; i++)
	{
		if(path_exists(list[i]))
		{
			status_bar_errorf("File \"%s\" already exists", list[i]);
			return 0;
		}
	}
	return 1;
}

static int
is_dir_path(FileView *view, const char *path, char *buf)
{
	strcpy(buf, view->curr_dir);

	if(path[0] == '/' || path[0] == '~')
	{
		char *expanded_path = expand_tilde(path);
		strcpy(buf, expanded_path);
		free(expanded_path);
	}
	else
	{
		strcat(buf, "/");
		strcat(buf, path);
	}

	if(is_dir(buf))
		return 1;

	strcpy(buf, view->curr_dir);
	return 0;
}

/* returns new value for save_msg */
int
clone_files(FileView *view, char **list, int nlines, int force, int copies)
{
	int i;
	char buf[COMMAND_GROUP_INFO_LEN + 1];
	char path[PATH_MAX];
	int with_dir = 0;
	int from_file;
	char **sel;
	int sel_len;
	ops_t *ops;

	if(!have_read_access(view))
		return 0;

	if(nlines == 1)
	{
		if((with_dir = is_dir_path(view, list[0], path)))
			nlines = 0;
	}
	else
	{
		strcpy(path, view->curr_dir);
	}
	if(!check_if_dir_writable(with_dir ? DR_DESTINATION : DR_CURRENT, path))
		return 0;

	capture_target_files(view);

	from_file = nlines < 0;
	if(from_file)
	{
		list = edit_list(view->selected_files, view->selected_filelist, &nlines, 0);
		if(list == NULL)
		{
			free_file_capture(view);
			return 0;
		}
	}

	if(nlines > 0 &&
			(!is_name_list_ok(view->selected_files, nlines, list, NULL) ||
			(!force && !is_clone_list_ok(nlines, list))))
	{
		clean_selected_files(view);
		redraw_view(view);
		if(from_file)
			free_string_array(list, nlines);
		return 1;
	}

	if(with_dir)
		snprintf(buf, sizeof(buf), "clone in %s to %s: ", view->curr_dir, list[0]);
	else
		snprintf(buf, sizeof(buf), "clone in %s: ", view->curr_dir);
	make_undo_string(view, buf, nlines, list);

	sel_len = view->selected_files;
	sel = copy_string_array(view->selected_filelist, sel_len);
	if(!view->user_selection)
	{
		erase_selection(view);
	}

	ops = get_ops(OP_COPY, "Cloning", view->curr_dir);

	ui_cancellation_reset();

	for(i = 0; i < sel_len && !ui_cancellation_requested(); ++i)
	{
		ops_enqueue(ops, sel[i], path);
	}

	cmd_group_begin(buf);
	for(i = 0; i < sel_len && !ui_cancellation_requested(); i++)
	{
		int j;
		const char * clone_name;
		if(nlines > 0)
		{
			clone_name = list[i];
		}
		else
		{
			clone_name = path_exists_at(path, sel[i]) ? gen_clone_name(sel[i]) :
				sel[i];
		}

		progress_msg("Cloning files", i, sel_len);

		for(j = 0; j < copies; j++)
		{
			if(path_exists_at(path, clone_name))
			{
				clone_name = gen_clone_name((nlines > 0) ? list[i] : sel[i]);
			}
			clone_file(view, sel[i], path, clone_name, ops);
		}

		if(find_file_pos_in_list(view, sel[i]) == view->list_pos)
		{
			free(view->dir_entry[view->list_pos].name);
			view->dir_entry[view->list_pos].name = malloc(strlen(clone_name) + 2);
			strcpy(view->dir_entry[view->list_pos].name, clone_name);
			if(ends_with_slash(sel[i]))
				strcat(view->dir_entry[view->list_pos].name, "/");
		}

		ops_advance(ops, 1);
	}
	cmd_group_end();
	free_file_capture(view);
	free_string_array(sel, sel_len);

	clean_selected_files(view);
	ui_views_reload_filelists();
	if(from_file)
	{
		free_string_array(list, nlines);
	}

	ops_free(ops);

	return 0;
}

/* Clones single file/directory named filaneme to directory specified by the
 * path under name in the clone. */
static void
clone_file(FileView* view, const char filename[], const char path[],
		const char clone[], ops_t *ops)
{
	char full[PATH_MAX];
	char clone_name[PATH_MAX];

	if(stroscmp(filename, "./") == 0)
		return;
	if(is_parent_dir(filename))
		return;

	snprintf(clone_name, sizeof(clone_name), "%s/%s", path, clone);
	chosp(clone_name);
	if(path_exists(clone_name))
	{
		if(perform_operation(OP_REMOVESL, NULL, NULL, clone_name, NULL) != 0)
		{
			return;
		}
	}

	snprintf(full, sizeof(full), "%s/%s", view->curr_dir, filename);
	chosp(full);

	if(perform_operation(OP_COPY, ops, NULL, full, clone_name) == 0)
	{
		add_operation(OP_COPY, NULL, NULL, full, clone_name);
	}
}

/* Uses dentry to check file type and fallbacks to lstat() if dentry contains
 * unknown type. */
static int
is_dir_entry(const char full_path[], const struct dirent* dentry)
{
#ifndef _WIN32
	struct stat s;
	if(dentry->d_type != DT_UNKNOWN)
	{
		return dentry->d_type == DT_DIR;
	}
	if(lstat(full_path, &s) == 0 && s.st_ino != 0)
	{
		return (s.st_mode&S_IFMT) == S_IFDIR;
	}
	return 0;
#else
	return is_dir(full_path);
#endif
}

int
put_links(FileView *view, int reg_name, int relative)
{
	return initiate_put_files_from_register(view, OP_SYMLINK, "Symlinking",
		reg_name, 0, relative ? 2 : 1);
}

/* Performs preparations necessary for putting files/links.  Returns new value
 * for save_msg flag. */
static int
initiate_put_files_from_register(FileView *view, OPS op, const char descr[],
		int reg_name, int force_move, int link)
{
	registers_t *reg;
	int i;

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
	{
		return 0;
	}

	reg = find_register(tolower(reg_name));

	if(reg == NULL || reg->num_files < 1)
	{
		status_bar_error("Register is empty");
		return 1;
	}

	reset_put_confirm(op, descr, view->curr_dir);

	put_confirm.force_move = force_move;
	put_confirm.link = link;
	put_confirm.reg = reg;
	put_confirm.view = view;

	for(i = 0; i < reg->num_files; ++i)
	{
		ops_enqueue(put_confirm.ops, reg->files[i], view->curr_dir);
	}

	return put_files_from_register_i(view, 1);
}

/* Resets state of global put_confirm variable in this module. */
static void
reset_put_confirm(OPS main_op, const char descr[], const char base_dir[])
{
	ops_free(put_confirm.ops);

	memset(&put_confirm, 0, sizeof(put_confirm));

	put_confirm.ops = get_ops(main_op, descr, base_dir);
}

/* Returns new value for save_msg flag. */
static int
put_files_from_register_i(FileView *view, int start)
{
	if(start)
	{
		char buf[MAX(COMMAND_GROUP_INFO_LEN, PATH_MAX + NAME_MAX*2 + 4)];
		const char *op = "UNKNOWN";
		const int from_trash = is_under_trash(put_confirm.reg->files[0]);
		if(put_confirm.link == 0)
			op = (put_confirm.force_move || from_trash) ? "Put" : "put";
		else if(put_confirm.link == 1)
			op = "put absolute links";
		else if(put_confirm.link == 2)
			op = "put relative links";
		snprintf(buf, sizeof(buf), "%s in %s: ", op,
				replace_home_part(view->curr_dir));
		cmd_group_begin(buf);
		cmd_group_end();
	}

	if(vifm_chdir(view->curr_dir) != 0)
	{
		show_error_msg("Directory Return", "Can't chdir() to current directory");
		return 1;
	}

	ui_cancellation_reset();

	while(put_confirm.x < put_confirm.reg->num_files)
	{
		const int put_result = put_next("", 0);
		if(put_result > 0)
		{
			/* In this case put_next() takes care of interacting with a user. */
			return 0;
		}
		else if(put_result < 0)
		{
			status_bar_messagef("%d file%s inserted%s", put_confirm.y,
					(put_confirm.y == 1) ? "" : "s", get_cancellation_suffix());
			return 1;
		}
		put_confirm.x++;
	}

	pack_register(put_confirm.reg->name);

	status_bar_messagef("%d file%s inserted%s", put_confirm.y,
			(put_confirm.y == 1) ? "" : "s", get_cancellation_suffix());

	ui_view_schedule_reload(put_confirm.view);

	return 1;
}

/* off can be NULL */
static const char *
substitute_regexp(const char *src, const char *sub, const regmatch_t *matches,
		int *off)
{
	static char buf[NAME_MAX];
	char *dst = buf;
	int i;

	for(i = 0; i < matches[0].rm_so; i++)
		*dst++ = src[i];

	while(*sub != '\0')
	{
		if(*sub == '\\')
		{
			if(sub[1] == '\0')
				break;
			else if(isdigit(sub[1]))
			{
				int n = sub[1] - '0';
				for(i = matches[n].rm_so; i < matches[n].rm_eo; i++)
					*dst++ = src[i];
				sub += 2;
				continue;
			}
			else
				sub++;
		}
		*dst++ = *sub++;
	}
	if(off != NULL)
		*off = dst - buf;

	for(i = matches[0].rm_eo; src[i] != '\0'; i++)
		*dst++ = src[i];

	*dst = '\0';

	return buf;
}

/* Returns pointer to a statically allocated buffer. */
static const char *
gsubstitute_regexp(regex_t *re, const char src[], const char sub[],
		regmatch_t matches[])
{
	static char buf[NAME_MAX];
	int off = 0;

	copy_str(buf, sizeof(buf), src);
	do
	{
		int i;
		for(i = 0; i < 10; i++)
		{
			matches[i].rm_so += off;
			matches[i].rm_eo += off;
		}

		src = substitute_regexp(buf, sub, matches, &off);
		copy_str(buf, sizeof(buf), src);

		if(matches[0].rm_eo == matches[0].rm_so)
			break;
	}
	while(regexec(re, buf + off, 10, matches, 0) == 0);

	return buf;
}

const char *
substitute_in_name(const char name[], const char pattern[], const char sub[],
		int glob)
{
	static char buf[PATH_MAX];
	regex_t re;
	regmatch_t matches[10];
	const char *dst;

	copy_str(buf, sizeof(buf), name);

	if(regcomp(&re, pattern, REG_EXTENDED) != 0)
	{
		regfree(&re);
		return buf;
	}

	if(regexec(&re, name, ARRAY_LEN(matches), matches, 0) != 0)
	{
		regfree(&re);
		return buf;
	}

	if(glob && pattern[0] != '^')
		dst = gsubstitute_regexp(&re, name, sub, matches);
	else
		dst = substitute_regexp(name, sub, matches, NULL);
	copy_str(buf, sizeof(buf), dst);

	regfree(&re);
	return buf;
}

static int
change_in_names(FileView *view, char c, const char *pattern, const char *sub,
		char **dest)
{
	int i, j;
	int n;
	char buf[COMMAND_GROUP_INFO_LEN + 1];
	size_t len;

	len = snprintf(buf, sizeof(buf), "%c/%s/%s/ in %s: ", c, pattern, sub,
			replace_home_part(view->curr_dir));

	for(i = 0; i < view->selected_files && len < COMMAND_GROUP_INFO_LEN; i++)
	{
		if(!view->dir_entry[i].selected)
			continue;
		if(is_parent_dir(view->dir_entry[i].name))
			continue;

		if(buf[len - 2] != ':')
		{
			strncat(buf, ", ", sizeof(buf) - len - 1);
			len = strlen(buf);
		}
		strncat(buf, view->dir_entry[i].name, sizeof(buf) - len - 1);
		len = strlen(buf);
	}
	cmd_group_begin(buf);
	n = 0;
	j = -1;
	for(i = 0; i < view->list_rows; i++)
	{
		char buf[NAME_MAX];

		if(!view->dir_entry[i].selected || is_parent_dir(view->dir_entry[i].name))
		{
			continue;
		}

		copy_str(buf, sizeof(buf), view->dir_entry[i].name);
		chosp(buf);
		j++;
		if(strcmp(buf, dest[j]) == 0)
			continue;

		if(i == view->list_pos)
		{
			(void)replace_string(&view->dir_entry[i].name, dest[j]);
		}

		if(mv_file(buf, view->curr_dir, dest[j], view->curr_dir, 0, 1, NULL) == 0)
		{
			n++;
		}
	}
	cmd_group_end();
	free_string_array(dest, j + 1);
	status_bar_messagef("%d file%s renamed", n, (n == 1) ? "" : "s");
	return 1;
}

/* Returns new value for save_msg flag. */
int
substitute_in_names(FileView *view, const char *pattern, const char *sub,
		int ic, int glob)
{
	int i;
	regex_t re;
	char **dest = NULL;
	int n = 0;
	int cflags;
	int err;

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
		return 0;

	if(view->selected_files == 0)
	{
		view->dir_entry[view->list_pos].selected = 1;
		view->selected_files = 1;
	}

	if(ic == 0)
		cflags = get_regexp_cflags(pattern);
	else if(ic > 0)
		cflags = REG_EXTENDED | REG_ICASE;
	else
		cflags = REG_EXTENDED;
	if((err = regcomp(&re, pattern, cflags)) != 0)
	{
		status_bar_errorf("Regexp error: %s", get_regexp_error(err, &re));
		regfree(&re);
		return 1;
	}

	for(i = 0; i < view->list_rows; i++)
	{
		char buf[NAME_MAX];
		const char *dst;
		regmatch_t matches[10];
		struct stat st;

		if(!view->dir_entry[i].selected || is_parent_dir(view->dir_entry[i].name))
		{
			continue;
		}

		copy_str(buf, sizeof(buf), view->dir_entry[i].name);
		chosp(buf);
		if(regexec(&re, buf, ARRAY_LEN(matches), matches, 0) != 0)
		{
			view->dir_entry[i].selected = 0;
			view->selected_files--;
			continue;
		}
		if(glob)
			dst = gsubstitute_regexp(&re, buf, sub, matches);
		else
			dst = substitute_regexp(buf, sub, matches, NULL);
		if(strcmp(buf, dst) == 0)
		{
			view->dir_entry[i].selected = 0;
			view->selected_files--;
			continue;
		}
		n = add_to_string_array(&dest, n, 1, dst);
		if(is_in_string_array(dest, n - 1, dst))
		{
			regfree(&re);
			free_string_array(dest, n);
			status_bar_errorf("Name \"%s\" duplicates", dst);
			return 1;
		}
		if(dst[0] == '\0')
		{
			regfree(&re);
			free_string_array(dest, n);
			status_bar_errorf("Destination name of \"%s\" is empty", buf);
			return 1;
		}
		if(contains_slash(dst))
		{
			regfree(&re);
			free_string_array(dest, n);
			status_bar_errorf("Destination name \"%s\" contains slash", dst);
			return 1;
		}
		if(lstat(dst, &st) == 0)
		{
			regfree(&re);
			free_string_array(dest, n);
			status_bar_errorf("File \"%s\" already exists", dst);
			return 1;
		}
	}
	regfree(&re);

	return change_in_names(view, 's', pattern, sub, dest);
}

static const char *
substitute_tr(const char *name, const char *pattern, const char *sub)
{
	static char buf[NAME_MAX];
	char *p = buf;
	while(*name != '\0')
	{
		const char *t = strchr(pattern, *name);
		if(t != NULL)
			*p++ = sub[t - pattern];
		else
			*p++ = *name;
		name++;
	}
	*p = '\0';
	return buf;
}

int
tr_in_names(FileView *view, const char *pattern, const char *sub)
{
	int i;
	char **dest = NULL;
	int n = 0;

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
		return 0;

	if(view->selected_files == 0)
	{
		view->dir_entry[view->list_pos].selected = 1;
		view->selected_files = 1;
	}

	for(i = 0; i < view->list_rows; i++)
	{
		char buf[NAME_MAX];
		const char *dst;
		struct stat st;

		if(!view->dir_entry[i].selected || is_parent_dir(view->dir_entry[i].name))
		{
			continue;
		}

		copy_str(buf, sizeof(buf), view->dir_entry[i].name);
		chosp(buf);
		dst = substitute_tr(buf, pattern, sub);
		if(strcmp(buf, dst) == 0)
		{
			view->dir_entry[i].selected = 0;
			view->selected_files--;
			continue;
		}
		n = add_to_string_array(&dest, n, 1, dst);
		if(is_in_string_array(dest, n - 1, dst))
		{
			free_string_array(dest, n);
			status_bar_errorf("Name \"%s\" duplicates", dst);
			return 1;
		}
		if(dst[0] == '\0')
		{
			free_string_array(dest, n);
			status_bar_errorf("Destination name of \"%s\" is empty", buf);
			return 1;
		}
		if(contains_slash(dst))
		{
			free_string_array(dest, n);
			status_bar_errorf("Destination name \"%s\" contains slash", dst);
			return 1;
		}
		if(lstat(dst, &st) == 0)
		{
			free_string_array(dest, n);
			status_bar_errorf("File \"%s\" already exists", dst);
			return 1;
		}
	}

	return change_in_names(view, 't', pattern, sub, dest);
}

static void
str_tolower(char *str)
{
	while(*str != '\0')
	{
		*str = tolower(*str);
		str++;
	}
}

static void
str_toupper(char *str)
{
	while(*str != '\0')
	{
		*str = toupper(*str);
		str++;
	}
}

int
change_case(FileView *view, int toupper, int count, int indexes[])
{
	int i;
	char **dest = NULL;
	int n = 0, k;
	char buf[COMMAND_GROUP_INFO_LEN + 1];

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
	{
		return 0;
	}

	if(count > 0)
	{
		capture_files_at(view, count, indexes);
	}
	else
	{
		capture_target_files(view);
	}

	if(view->selected_files == 0)
	{
		status_bar_message("0 files renamed");
		return 1;
	}

	for(i = 0; i < view->selected_files; i++)
	{
		char buf[NAME_MAX];
		struct stat st;

		chosp(view->selected_filelist[i]);
		copy_str(buf, sizeof(buf), view->selected_filelist[i]);
		if(toupper)
			str_toupper(buf);
		else
			str_tolower(buf);

		n = add_to_string_array(&dest, n, 1, buf);

		if(is_in_string_array(dest, n - 1, buf))
		{
			free_string_array(dest, n);
			free_file_capture(view);
			view->selected_files = 0;
			status_bar_errorf("Name \"%s\" duplicates", buf);
			return 1;
		}
		if(strcmp(dest[i], buf) == 0)
			continue;
		if(lstat(buf, &st) == 0)
		{
			free_string_array(dest, n);
			free_file_capture(view);
			view->selected_files = 0;
			status_bar_errorf("File \"%s\" already exists", buf);
			return 1;
		}
	}

	snprintf(buf, sizeof(buf), "g%c in %s: ", toupper ? 'U' : 'u',
			replace_home_part(view->curr_dir));

	get_group_file_list(view->selected_filelist, view->selected_files, buf);
	cmd_group_begin(buf);
	k = 0;
	for(i = 0; i < n; i++)
	{
		int pos;
		if(strcmp(dest[i], view->selected_filelist[i]) == 0)
			continue;
		pos = find_file_pos_in_list(view, view->selected_filelist[i]);
		if(pos == view->list_pos)
		{
			(void)replace_string(&view->dir_entry[pos].name, dest[i]);
		}
		if(mv_file(view->selected_filelist[i], view->curr_dir, dest[i],
				view->curr_dir, 0, 1, NULL) == 0)
		{
			k++;
		}
	}
	cmd_group_end();

	free_file_capture(view);
	view->selected_files = 0;
	free_string_array(dest, n);
	status_bar_messagef("%d file%s renamed", k, (k == 1) ? "" : "s");
	return 1;
}

static int
is_copy_list_ok(const char *dst, int count, char **list)
{
	int i;
	for(i = 0; i < count; i++)
	{
		if(path_exists_at(dst, list[i]))
		{
			status_bar_errorf("File \"%s\" already exists", list[i]);
			return 0;
		}
	}
	return 1;
}

static int
cpmv_prepare(FileView *view, char ***list, int *nlines, int move, int type,
		int force, char *buf, size_t buf_len, char *path, int *from_file,
		int *from_trash)
{
	int error = 0;

	if(move && !check_if_dir_writable(DR_CURRENT, view->curr_dir))
		return -1;

	if(move == 0 && type == 0 && !have_read_access(view))
		return -1;

	if(*nlines == 1)
	{
		if(is_dir_path(other_view, (*list)[0], path))
			*nlines = 0;
	}
	else
	{
		strcpy(path, other_view->curr_dir);
	}
	if(!check_if_dir_writable(DR_DESTINATION, path))
		return -1;

	capture_target_files(view);

	*from_file = *nlines < 0;
	if(*from_file)
	{
		*list = edit_list(view->selected_files, view->selected_filelist, nlines, 1);
		if(*list == NULL)
		{
			return -1;
		}
	}

	if(*nlines > 0 &&
			(!is_name_list_ok(view->selected_files, *nlines, *list, NULL) ||
			(!is_copy_list_ok(path, *nlines, *list) && !force)))
		error = 1;
	if(*nlines == 0 && !force &&
			!is_copy_list_ok(path, view->selected_files, view->selected_filelist))
		error = 1;
	if(error)
	{
		clean_selected_files(view);
		redraw_view(view);
		if(*from_file)
			free_string_array(*list, *nlines);
		return 1;
	}

	if(move)
		strcpy(buf, "move");
	else if(type == 0)
		strcpy(buf, "copy");
	else if(type == 1)
		strcpy(buf, "alink");
	else
		strcpy(buf, "rlink");
	snprintf(buf + strlen(buf), buf_len - strlen(buf), " from %s to ",
			replace_home_part(view->curr_dir));
	snprintf(buf + strlen(buf), buf_len - strlen(buf), "%s: ",
			replace_home_part(path));
	make_undo_string(view, buf, *nlines, *list);

	if(move)
	{
		int i = view->list_pos;
		while(i < view->list_rows - 1 && view->dir_entry[i].selected)
			i++;
		view->list_pos = i;
	}

	*from_trash = is_under_trash(view->curr_dir);
	return 0;
}

static int
have_read_access(FileView *view)
{
	int i;

#ifdef _WIN32
	if(is_unc_path(view->curr_dir))
		return 1;
#endif

	for(i = 0; i < view->list_rows; i++)
	{
		if(!view->dir_entry[i].selected)
			continue;
		if(access(view->dir_entry[i].name, R_OK) != 0)
		{
			show_error_msgf("Access denied",
					"You don't have read permissions on \"%s\"", view->dir_entry[i].name);
			clean_selected_files(view);
			redraw_view(view);
			return 0;
		}
	}
	return 1;
}

/* Prompts user with a file containing lines from orig array of length count and
 * returns modified list of strings of length *nlines or NULL on error.  The
 * ignore_change parameter makes function think that file is always changed. */
static char **
edit_list(size_t count, char **orig, int *nlines, int ignore_change)
{
	char rename_file[PATH_MAX];
	char **list = NULL;

	generate_tmp_file_name("vifm.rename", rename_file, sizeof(rename_file));

	if(write_file_of_lines(rename_file, orig, count) != 0)
	{
		show_error_msgf("Error Getting List Of Renames",
				"Can't create temporary file \"%s\": %s", rename_file, strerror(errno));
		return NULL;
	}

	if(edit_file(rename_file, ignore_change) > 0)
	{
		list = read_file_of_lines(rename_file, nlines);
		if(list == NULL)
		{
			show_error_msgf("Error Getting List Of Renames",
					"Can't open temporary file \"%s\": %s", rename_file, strerror(errno));
		}
	}

	unlink(rename_file);
	return list;
}

/* Edits the filepath in the editor checking whether it was changed.  Returns
 * negative value on error, zero when no changes were detected and positive
 * number otherwise. */
static int
edit_file(const char filepath[], int force_changed)
{
	struct stat st_before, st_after;

	if(!force_changed && stat(filepath, &st_before) != 0)
	{
		show_error_msgf("Error Editing File",
				"Could not stat file \"%s\" before edit: %s", filepath,
				strerror(errno));
		return -1;
	}

	if(view_file(filepath, -1, -1, 0) != 0)
	{
		show_error_msgf("Error Editing File", "Editing of file \"%s\" failed.",
				filepath);
		return -1;
	}

	if(!force_changed && stat(filepath, &st_after) != 0)
	{
		show_error_msgf("Error Editing File",
				"Could not stat file \"%s\" after edit: %s", filepath, strerror(errno));
		return -1;
	}

	return force_changed || memcmp(&st_after.st_mtime, &st_before.st_mtime,
			sizeof(st_after.st_mtime)) != 0;
}

int
cpmv_files(FileView *view, char **list, int nlines, int move, int type,
		int force)
{
	int i;
	char buf[COMMAND_GROUP_INFO_LEN + 1];
	char path[PATH_MAX];
	int from_file;
	int from_trash;
	char **sel;
	int sel_len;
	ops_t *ops;

	if(!move && type != 0 && !symlinks_available())
	{
		show_error_msg("Symbolic Links Error",
				"Your OS doesn't support symbolic links");
		return 0;
	}

	i = cpmv_prepare(view, &list, &nlines, move, type, force, buf, sizeof(buf),
			path, &from_file, &from_trash);
	if(i != 0)
		return i > 0;

	if(pane_in_dir(curr_view, path) && force)
	{
		show_error_msg("Operation Error",
				"Forcing overwrite when destination and source is same directory will "
				"lead to losing data");
		return 0;
	}

	sel_len = view->selected_files;
	sel = copy_string_array(view->selected_filelist, sel_len);
	if(!view->user_selection)
	{
		/* Clean selection so that it won't get stored for gs command. */
		erase_selection(view);
	}

	if(type == 0)
	{
		ops = get_ops(move ? OP_MOVE : OP_COPY, move ? "Moving" : "Copying",
				view->curr_dir);
	}
	else
	{
		ops = get_ops(OP_SYMLINK, "Linking", view->curr_dir);
	}

	ui_cancellation_reset();

	for(i = 0; i < sel_len && !ui_cancellation_requested(); i++)
	{
		char src_full[PATH_MAX];
		snprintf(src_full, sizeof(src_full), "%s/%s", view->curr_dir, sel[i]);
		chosp(src_full);

		ops_enqueue(ops, src_full, path);
	}

	cmd_group_begin(buf);
	for(i = 0; i < sel_len && !ui_cancellation_requested(); i++)
	{
		char dst_full[PATH_MAX];
		const char *dst = (nlines > 0) ? list[i] : sel[i];
		int success;

		if(from_trash && nlines <= 0)
		{
			char src_full[PATH_MAX];
			snprintf(src_full, sizeof(src_full), "%s/%s", view->curr_dir, dst);
			chosp(src_full);
			dst = get_real_name_from_trash_name(src_full);
		}

		snprintf(dst_full, sizeof(dst_full), "%s/%s", path, dst);
		if(path_exists(dst_full) && !from_trash)
		{
			(void)perform_operation(OP_REMOVESL, NULL, NULL, dst_full, NULL);
		}

		if(move)
		{
			progress_msg("Moving files", i, sel_len);

			success = mv_file(sel[i], view->curr_dir, dst, path, 0, 1, ops) == 0;

			if(!success)
			{
				view->list_pos = find_file_pos_in_list(view, sel[i]);
			}
		}
		else
		{
			if(type == 0)
			{
				progress_msg("Copying files", i, sel_len);
			}

			success = cp_file(view->curr_dir, path, sel[i], dst, type, 1, ops) == 0;
		}

		ops_advance(ops, success);
	}
	cmd_group_end();

	free_string_array(sel, sel_len);
	free_file_capture(view);
	clean_selected_files(view);
	ui_views_reload_filelists();
	if(from_file)
	{
		free_string_array(list, nlines);
	}

	status_bar_messagef("%d file%s successfully processed%s", ops->succeeded,
			(ops->succeeded == 1) ? "" : "s", get_cancellation_suffix());

	ops_free(ops);

	return 1;
}

/* Allocates opt_t structure and configures it as needed.  Returns pointer to
 * newly allocated structure, which should be freed by ops_free(). */
static ops_t *
get_ops(OPS main_op, const char descr[], const char base_dir[])
{
	ops_t *const ops = ops_alloc(main_op, descr, base_dir);
	if(cfg.use_system_calls)
	{
		ops->estim = ioeta_alloc(ops);
	}
	return ops;
}

/* Displays simple operation progress message.  The ready is zero based. */
static void
progress_msg(const char text[], int ready, int total)
{
	if(!cfg.use_system_calls)
	{
		char msg[strlen(text) + 32];

		sprintf(msg, "%s %d/%d", text, ready + 1, total);
		show_progress(msg, 1);
		curr_stats.save_msg = 2;
	}
}

static int
cpmv_files_bg_i(char **list, int nlines, int move, int force, char **sel_list,
		int sel_list_len, int from_trash, const char *src, const char *path)
{
	int i;
	for(i = 0; i < sel_list_len; i++)
	{
		char dst_full[PATH_MAX];
		const char *dst = (nlines > 0) ? list[i] : sel_list[i];
		if(from_trash)
		{
			char src_full[PATH_MAX];
			snprintf(src_full, sizeof(src_full), "%s/%s", src, dst);
			chosp(src_full);
			dst = get_real_name_from_trash_name(src_full);
		}

		snprintf(dst_full, sizeof(dst_full), "%s/%s", path, dst);
		if(path_exists(dst_full) && !from_trash)
		{
			perform_operation(OP_REMOVESL, NULL, (void *)1, dst_full, NULL);
		}

		if(move)
		{
			(void)mv_file(sel_list[i], src, dst, path, -1, 0, NULL);
		}
		else
		{
			(void)cp_file(src, path, sel_list[i], dst, -1, 0, NULL);
		}

		inner_bg_next();
	}
	return 0;
}

static int
mv_file(const char src[], const char src_path[], const char dst[],
		const char path[], int tmpfile_num, int cancellable, ops_t *ops)
{
	char full_src[PATH_MAX], full_dst[PATH_MAX];
	int op;
	int result;

	snprintf(full_src, sizeof(full_src), "%s/%s", src_path, src);
	chosp(full_src);
	snprintf(full_dst, sizeof(full_dst), "%s/%s", path, dst);
	chosp(full_dst);

	/* compare case sensitive strings even on Windows to let user rename file
	 * changing only case of some characters */
	if(strcmp(full_src, full_dst) == 0)
		return 0;

	if(tmpfile_num <= 0)
		op = OP_MOVE;
	else if(tmpfile_num == 1)
		op = OP_MOVETMP1;
	else if(tmpfile_num == 2)
		op = OP_MOVETMP2;
	else if(tmpfile_num == 3)
		op = OP_MOVETMP3;
	else if(tmpfile_num == 4)
		op = OP_MOVETMP4;
	else
		op = OP_NONE;

	result = perform_operation(op, ops, cancellable ? NULL : (void *)1, full_src,
			full_dst);
	if(result == 0 && tmpfile_num >= 0)
		add_operation(op, NULL, NULL, full_src, full_dst);
	return result;
}

/* type:
 *  <= 0 - copy
 *  1 - absolute symbolic links
 *  2 - relative symbolic links
 */
static int
cp_file(const char src_dir[], const char dst_dir[], const char src[],
		const char dst[], int type, int cancellable, ops_t *ops)
{
	char full_src[PATH_MAX], full_dst[PATH_MAX];
	int op;
	int result;

	snprintf(full_src, sizeof(full_src), "%s/%s", src_dir, src);
	chosp(full_src);
	snprintf(full_dst, sizeof(full_dst), "%s/%s", dst_dir, dst);
	chosp(full_dst);

	if(strcmp(full_src, full_dst) == 0)
		return 0;

	if(type <= 0)
	{
		op = OP_COPY;
	}
	else
	{
		op = OP_SYMLINK;
		if(type == 2)
		{
			snprintf(full_src, sizeof(full_src), "%s", make_rel_path(full_src,
					dst_dir));
		}
	}

	result = perform_operation(op, ops, cancellable ? NULL : (void *)1, full_src,
			full_dst);
	if(result == 0 && type >= 0)
		add_operation(op, NULL, NULL, full_src, full_dst);
	return result;
}

int
cpmv_files_bg(FileView *view, char **list, int nlines, int move, int force)
{
	int i;
	char task_desc[COMMAND_GROUP_INFO_LEN];
	bg_args_t *args = malloc(sizeof(*args));

	args->list = NULL;
	args->nlines = nlines;
	args->move = move;
	args->force = force;

	i = cpmv_prepare(view, &list, &args->nlines, move, 0, force, task_desc,
			sizeof(task_desc), args->path, &args->from_file, &args->from_trash);
	if(i != 0)
	{
		free(args);
		return i > 0;
	}

	args->list = args->from_file ? list : copy_string_array(list, nlines);

	general_prepare_for_bg_task(view, args);

	if(bg_execute(task_desc, args->sel_list_len, &cpmv_in_bg, args) != 0)
	{
		free_string_array(args->list, args->nlines);
		free_string_array(args->sel_list, args->sel_list_len);
		free(args);
	}

	return 0;
}

/* Entry point for a background task that copies/moves files. */
static void
cpmv_in_bg(void *arg)
{
	bg_args_t *const args = arg;

	cpmv_files_bg_i(args->list, args->nlines, args->move, args->force,
			args->sel_list, args->sel_list_len, args->from_trash, args->src,
			args->path);

	free_string_array(args->list, args->nlines);
	free_string_array(args->sel_list, args->sel_list_len);
	free(args);
}

/* Fills basic fields of the args structure. */
static void
general_prepare_for_bg_task(FileView *view, bg_args_t *args)
{
	/* Steal captured file list from the view. */
	args->sel_list = view->selected_filelist;
	args->sel_list_len = view->selected_files;
	view->selected_filelist = NULL;

	free_file_capture(view);
	ui_view_reset_selection_and_reload(view);

	copy_str(args->src, sizeof(args->src), view->curr_dir);
}

static void
go_to_first_file(FileView *view, char **names, int count)
{
	int i;

	load_saving_pos(view, 1);

	for(i = 0; i < view->list_rows; i++)
	{
		char name[PATH_MAX];
		snprintf(name, sizeof(name), "%s", view->dir_entry[i].name);
		chosp(name);
		if(is_in_string_array(names, count, name))
		{
			view->list_pos = i;
			break;
		}
	}
	redraw_view(view);
}

void
make_dirs(FileView *view, char **names, int count, int create_parent)
{
	char buf[COMMAND_GROUP_INFO_LEN + 1];
	int i;
	int n;
	void *cp;

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
	{
		return;
	}

	cp = (void *)(size_t)create_parent;

	for(i = 0; i < count; i++)
	{
		struct stat st;
		if(is_in_string_array(names, i, names[i]))
		{
			status_bar_errorf("Name \"%s\" duplicates", names[i]);
			return;
		}
		if(names[i][0] == '\0')
		{
			status_bar_errorf("Name #%d is empty", i + 1);
			return;
		}
		if(lstat(names[i], &st) == 0)
		{
			status_bar_errorf("File \"%s\" already exists", names[i]);
			return;
		}
	}

	ui_cancellation_reset();

	snprintf(buf, sizeof(buf), "mkdir in %s: ",
			replace_home_part(view->curr_dir));

	get_group_file_list(names, count, buf);
	cmd_group_begin(buf);
	n = 0;
	for(i = 0; i < count && !ui_cancellation_requested(); i++)
	{
		char full[PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", view->curr_dir, names[i]);
		if(perform_operation(OP_MKDIR, NULL, cp, full, NULL) == 0)
		{
			add_operation(OP_MKDIR, cp, NULL, full, "");
			n++;
		}
		else if(i == 0)
		{
			i--;
			names++;
			count--;
		}
	}
	cmd_group_end();

	if(count > 0)
	{
		if(create_parent)
		{
			for(i = 0; i < count; i++)
			{
				break_at(names[i], '/');
			}
		}
		go_to_first_file(view, names, count);
	}

	status_bar_messagef("%d director%s created", n, (n == 1) ? "y" : "ies",
			get_cancellation_suffix());
}

int
make_files(FileView *view, char **names, int count)
{
	int i;
	int n;
	char buf[COMMAND_GROUP_INFO_LEN + 1];

	if(!check_if_dir_writable(DR_CURRENT, view->curr_dir))
		return 0;

	for(i = 0; i < count; i++)
	{
		struct stat st;
		if(is_in_string_array(names, i, names[i]))
		{
			status_bar_errorf("Name \"%s\" duplicates", names[i]);
			return 1;
		}
		if(names[i][0] == '\0')
		{
			status_bar_errorf("Name #%d is empty", i + 1);
			return 1;
		}
		if(contains_slash(names[i]))
		{
			status_bar_errorf("Name \"%s\" contains slash", names[i]);
			return 1;
		}
		if(lstat(names[i], &st) == 0)
		{
			status_bar_errorf("File \"%s\" already exists", names[i]);
			return 1;
		}
	}

	snprintf(buf, sizeof(buf), "touch in %s: ",
			replace_home_part(view->curr_dir));

	get_group_file_list(names, count, buf);
	cmd_group_begin(buf);
	n = 0;
	for(i = 0; i < count && !ui_cancellation_requested(); i++)
	{
		char full[PATH_MAX];
		snprintf(full, sizeof(full), "%s/%s", view->curr_dir, names[i]);
		if(perform_operation(OP_MKFILE, NULL, NULL, full, NULL) == 0)
		{
			add_operation(OP_MKFILE, NULL, NULL, full, "");
			n++;
		}
	}
	cmd_group_end();

	if(n > 0)
		go_to_first_file(view, names, count);

	status_bar_messagef("%d file%s created%s", n, (n == 1) ? "" : "s",
			get_cancellation_suffix());
	return 1;
}

int
restore_files(FileView *view)
{
	int i;
	int m = 0;
	int n = view->selected_files;

	i = view->list_pos;
	while(i < view->list_rows - 1 && view->dir_entry[i].selected)
	{
		i++;
	}
	view->list_pos = i;

	ui_cancellation_reset();

	cmd_group_begin("restore: ");
	cmd_group_end();
	for(i = 0; i < view->list_rows && !ui_cancellation_requested(); i++)
	{
		if(view->dir_entry[i].selected)
		{
			char full_path[PATH_MAX];
			snprintf(full_path, sizeof(full_path), "%s/%s", view->curr_dir,
					view->dir_entry[i].name);
			chosp(full_path);

			if(restore_from_trash(full_path) == 0)
			{
				m++;
			}
		}
	}

	ui_view_schedule_reload(view);

	status_bar_messagef("Restored %d of %d%s", m, n, get_cancellation_suffix());
	return 1;
}

/* Provides different suffixes depending on whether cancellation was requested
 * or not.  Returns pointer to a string literal. */
static const char *
get_cancellation_suffix(void)
{
	return ui_cancellation_requested() ? " (cancelled)" : "";
}

int
check_if_dir_writable(DirRole dir_role, const char *path)
{
	if(is_dir_writable(path))
		return 1;

	if(dir_role == DR_DESTINATION)
		show_error_msg("Operation error", "Destination directory is not writable");
	else
		show_error_msg("Operation error", "Current directory is not writable");
	return 0;
}

void
calculate_size(const FileView *view, int force)
{
	int i;

	if(!view->dir_entry[view->list_pos].selected && view->user_selection)
	{
		update_dir_entry_size(view, view->list_pos, force);
		return;
	}

	for(i = 0; i < view->list_rows; i++)
	{
		const dir_entry_t *const entry = &view->dir_entry[i];

		if(entry->selected && entry->type == DIRECTORY)
		{
			update_dir_entry_size(view, i, force);
		}
	}
}

/* Initiates background size calculation for view entry. */
static void
update_dir_entry_size(const FileView *view, int index, int force)
{
	char full_path[PATH_MAX];
	const dir_entry_t *const entry = &view->dir_entry[index];

	snprintf(full_path, sizeof(full_path), "%s/%s", view->curr_dir, entry->name);
	start_dir_size_calc(full_path, force);
}

/* Initiates background directory size calculation. */
static void
start_dir_size_calc(const char path[], int force)
{
	pthread_t id;
	dir_size_args_t *dir_size;

	dir_size = malloc(sizeof(*dir_size));
	dir_size->path = strdup(path);
	dir_size->force = force;

	pthread_create(&id, NULL, dir_size_bg, dir_size);
}

/* Entry point for a background task that calculates size of a directory. */
static void *
dir_size_bg(void *arg)
{
	dir_size_args_t *const dir_size = arg;

	(void)calc_dirsize(dir_size->path, dir_size->force);

	remove_last_path_component(dir_size->path);
	if(path_starts_with(lwin.curr_dir, dir_size->path))
	{
		ui_view_schedule_redraw(&lwin);
	}
	if(path_starts_with(rwin.curr_dir, dir_size->path))
	{
		ui_view_schedule_redraw(&rwin);
	}

	free(dir_size->path);
	free(dir_size);
	return NULL;
}

/* Calculates size of a directory possibly using cache of known sizes.  Returns
 * size of a directory or zero on error. */
static uint64_t
calc_dirsize(const char path[], int force_update)
{
	DIR* dir;
	struct dirent* dentry;
	const char* slash = "";
	uint64_t size;

	dir = opendir(path);
	if(dir == NULL)
		return 0;

	if(path[strlen(path) - 1] != '/')
		slash = "/";

	size = 0;
	while((dentry = readdir(dir)) != NULL)
	{
		char buf[PATH_MAX];

		if(is_builtin_dir(dentry->d_name))
		{
			continue;
		}

		snprintf(buf, sizeof(buf), "%s%s%s", path, slash, dentry->d_name);
		if(is_dir_entry(buf, dentry))
		{
			uint64_t dir_size = 0;
			if(tree_get_data(curr_stats.dirsize_cache, buf, &dir_size) != 0
					|| force_update)
				dir_size = calc_dirsize(buf, force_update);
			size += dir_size;
		}
		else
		{
			size += get_file_size(buf);
		}
	}

	closedir(dir);

	set_dir_size(path, size);
	return size;
}

/* Updates cached directory size in a thread-safe way. */
static void
set_dir_size(const char path[], uint64_t size)
{
	static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

	pthread_mutex_lock(&mutex);
	tree_set_data(curr_stats.dirsize_cache, path, size);
	pthread_mutex_unlock(&mutex);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
