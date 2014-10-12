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

#ifndef VIFM__IO__PRIVATE__IOETA_H__
#define VIFM__IO__PRIVATE__IOETA_H__

#include <stdint.h> /* uint64_t */

#include "../ioeta.h"

/* ioeta - private functions of Input/Output estimation */

/* Adds zero-size item to the estimation. */
void ioeta_add_item(ioeta_estim_t *estim, const char path[]);

/* Adds file to the estimation. */
void ioeta_add_file(ioeta_estim_t *estim, const char path[]);

/* Adds directory to the estimation. */
void ioeta_add_dir(ioeta_estim_t *estim, const char path[]);

/* ioeta_update_estim(e, "p", 0, 100); -- 100 bytes of current item processed.
 * ioeta_update_estim(e, "", 1, 50); -- Last 50 bytes of current item processed.
 * Might calculate speed, time, etc.  When estim is NULL, the function just
 * returns.  The path can be NULL to indicate that file name didn't change.
 * Calls progress changed notification handler. */
void ioeta_update(ioeta_estim_t *estim, const char path[], int finished,
		uint64_t bytes);

#endif /* VIFM__IO__PRIVATE__IOETA_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
