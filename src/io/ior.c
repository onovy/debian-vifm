/* vifm
 * Copyright (C) 2013 xaizek.
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

#include "ior.h"

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <sys/stat.h> /* stat chmod() */
#include <unistd.h> /* lstat() unlink() */

#include <errno.h> /* EEXIST EISDIR ENOTEMPTY EXDEV errno */
#include <stddef.h> /* NULL */
#include <stdio.h> /* removee() snprintf() */
#include <stdlib.h> /* free() */
#include <string.h> /* strlen() */

#include "../utils/fs.h"
#include "../utils/fs_limits.h"
#include "../utils/log.h"
#include "../utils/path.h"
#include "../utils/str.h"
#include "../background.h"
#include "../ui.h"
#include "private/ioeta.h"
#include "private/traverser.h"
#include "ioc.h"
#include "iop.h"

static VisitResult rm_visitor(const char full_path[], VisitAction action,
		void *param);
static VisitResult cp_visitor(const char full_path[], VisitAction action,
		void *param);
static int is_file(const char path[]);
static VisitResult mv_visitor(const char full_path[], VisitAction action,
		void *param);
static VisitResult cp_mv_visitor(const char full_path[], VisitAction action,
		void *param, int cp);

int
ior_rm(io_args_t *const args)
{
	const char *const path = args->arg1.path;
	return traverse(path, &rm_visitor, args);
}

/* Implementation of traverse() visitor for subtree removal.  Returns 0 on
 * success, otherwise non-zero is returned. */
static VisitResult
rm_visitor(const char full_path[], VisitAction action, void *param)
{
	const io_args_t *const rm_args = param;
	VisitResult result = VR_OK;

	if(rm_args->cancellable && ui_cancellation_requested())
	{
		return VR_CANCELLED;
	}

	switch(action)
	{
		case VA_DIR_ENTER:
			/* Do nothing, directories are removed on leaving them. */
			result = VR_OK;
			break;
		case VA_FILE:
			{
				io_args_t args =
				{
					.arg1.path = full_path,

					.cancellable = rm_args->cancellable,
					.estim = rm_args->estim,
				};

				result = iop_rmfile(&args);
				break;
			}
		case VA_DIR_LEAVE:
			{
				io_args_t args =
				{
					.arg1.path = full_path,

					.cancellable = rm_args->cancellable,
					.estim = rm_args->estim,
				};

				result = iop_rmdir(&args);
				break;
			}
	}

	return result;
}

int
ior_cp(io_args_t *const args)
{
	const char *const src = args->arg1.src;
	const char *const dst = args->arg2.dst;

	if(is_in_subtree(dst, src))
	{
		return 1;
	}

	if(args->arg3.crs == IO_CRS_REPLACE_ALL)
	{
		io_args_t rm_args =
		{
			.arg1.path = dst,

			.cancellable = args->cancellable,
			.estim = args->estim,
		};
		const int result = ior_rm(&rm_args);
		if(result != 0)
		{
			return result;
		}
	}

	return traverse(src, &cp_visitor, args);
}

/* Implementation of traverse() visitor for subtree copying.  Returns 0 on
 * success, otherwise non-zero is returned. */
static VisitResult
cp_visitor(const char full_path[], VisitAction action, void *param)
{
	return cp_mv_visitor(full_path, action, param, 1);
}

int
ior_mv(io_args_t *const args)
{
	const char *const src = args->arg1.src;
	const char *const dst = args->arg2.dst;
	const IoCrs crs = args->arg3.crs;

	if(crs == IO_CRS_FAIL && path_exists(dst))
	{
		return 1;
	}

	if(crs == IO_CRS_APPEND_TO_FILES)
	{
		if(!is_file(src) || !is_file(dst))
		{
			return 1;
		}
	}

	if(rename(src, dst) == 0)
	{
		ioeta_update(args->estim, src, 1, 0);
		return 0;
	}

	switch(errno)
	{
		case EXDEV:
			{
				const int result = ior_cp(args);
				return (result == 0) ? ior_rm(args) : result;
			}
		case EISDIR:
		case ENOTEMPTY:
		case EEXIST:
			if(crs == IO_CRS_REPLACE_ALL)
			{
				io_args_t rm_args =
				{
					.arg1.path = dst,

					.cancellable = args->cancellable,
					.estim = args->estim,
				};

				const int error = ior_rm(&rm_args);
				if(error != 0)
				{
					return error;
				}

				return rename(src, dst);
			}
			else if(crs == IO_CRS_REPLACE_FILES)
			{
#ifdef _WIN32
				/* rename() on Windows doesn't replace files. */
				if(is_file(dst))
				{
					io_args_t rm_args =
					{
						.arg1.path = dst,

						.cancellable = args->cancellable,
						.estim = args->estim,
					};

					const int error = iop_rmfile(&rm_args);
					if(error != 0)
					{
						return error;
					}
				}
#endif

				return traverse(src, &mv_visitor, args);
			}
			/* Break is intentionally omitted. */

		default:
			return errno;
	}
}

/* Checks that path points to a file or symbolic link.  Returns non-zero if so,
 * otherwise zero is returned. */
static int
is_file(const char path[])
{
	return !is_dir(path)
	    || (is_symlink(path) && get_symlink_type(path) != SLT_UNKNOWN);
}

/* Implementation of traverse() visitor for subtree moving.  Returns 0 on
 * success, otherwise non-zero is returned. */
static VisitResult
mv_visitor(const char full_path[], VisitAction action, void *param)
{
	return cp_mv_visitor(full_path, action, param, 0);
}

/* Generic implementation of traverse() visitor for subtree copying/moving.
 * Returns 0 on success, otherwise non-zero is returned. */
static VisitResult
cp_mv_visitor(const char full_path[], VisitAction action, void *param, int cp)
{
	const io_args_t *const cp_args = param;
	const char *dst_full_path;
	char *free_me = NULL;
	VisitResult result = VR_OK;
	const char *rel_part;

	if(cp_args->cancellable && ui_cancellation_requested())
	{
		return VR_CANCELLED;
	}

	/* TODO: come up with something better than this. */
	rel_part = full_path + strlen(cp_args->arg1.src);
	dst_full_path = (rel_part[0] == '\0')
	              ? cp_args->arg2.dst
	              : (free_me = format_str("%s/%s", cp_args->arg2.dst, rel_part));

	switch(action)
	{
		case VA_DIR_ENTER:
			if(cp_args->arg3.crs != IO_CRS_REPLACE_FILES || !is_dir(dst_full_path))
			{
				io_args_t args =
				{
					.arg1.path = dst_full_path,

					/* Temporary fake rights so we can add files to the directory. */
					.arg3.mode = 0700,

					.cancellable = cp_args->cancellable,
					.estim = cp_args->estim,
				};

				result = (iop_mkdir(&args) == 0) ? VR_OK : VR_ERROR;
			}
			else
			{
				result = VR_SKIP_DIR_LEAVE;
			}
			break;
		case VA_FILE:
			{
				io_args_t args =
				{
					.arg1.src = full_path,
					.arg2.dst = dst_full_path,
					.arg3.crs = cp_args->arg3.crs,

					.cancellable = cp_args->cancellable,
					.estim = cp_args->estim,
				};

				result = ((cp ? iop_cp(&args) : ior_mv(&args)) == 0) ? VR_OK : VR_ERROR;
				break;
			}
		case VA_DIR_LEAVE:
			{
#ifndef _WIN32
				{
					struct stat st;

					if(lstat(full_path, &st) == 0)
					{
						result = (chmod(dst_full_path, st.st_mode & 07777) == 0)
						       ? VR_OK
						       : VR_ERROR;
					}
					else
					{
						result = VR_ERROR;
					}
				}
#else
				result = VR_OK;
#endif
				break;
			}
	}

	free(free_me);

	return result;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
