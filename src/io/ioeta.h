/* vifm
 * Copyright (C) 2014 xaizek.
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

#ifndef VIFM__IO__IOETA_H__
#define VIFM__IO__IOETA_H__

#include <stddef.h> /* size_t */
#include <stdint.h> /* uint64_t */

/* ioeta - Input/Output estimation */

/* TODO: add per file progress. */

typedef struct
{
	/* Total number of items to process (T). */
	size_t total_items;

	/* Number of already processed items and index of the current item at the same
	 * time (0..T). */
	size_t current_item;

	/* Total number of bytes to process (T).  Size of directories is counted as
	 * 0. */
	uint64_t total_bytes;

	/* Number of already processed bytes. */
	uint64_t current_byte;

	/* Relative path to currently processed file. */
	char *item;

	/* Custom parameter for notification callbacks. */
	void *param;
}
ioeta_estim_t;

/* Allocates and initializes new ioeta_estim_t. */
ioeta_estim_t * ioeta_alloc(void *param);

/* Frees ioeta_estim_t.  The estim can be NULL. */
void ioeta_free(ioeta_estim_t *estim);

/* Calculates estimates for a subtree rooted at path.  Adds them up to values
 * already present in the estim.  Shallow estimation doesn't recur into
 * directories. */
void ioeta_calculate(ioeta_estim_t *estim, const char path[], int shallow);

#endif /* VIFM__IO__IOETA_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
