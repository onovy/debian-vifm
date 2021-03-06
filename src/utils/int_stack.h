/* vifm
 * Copyright (C) 2012 xaizek.
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

#ifndef VIFM__UTILS__INT_STACK_H__
#define VIFM__UTILS__INT_STACK_H__

#include <stddef.h> /* NULL, size_t */

/* Integer stack structure. */
typedef struct {
	int *data; /* Pointer to stack data. */
	size_t len; /* Number of available elements. */
	size_t top; /* Number of used elements. */
} int_stack_t;

#define INT_STACK_INITIALIZER { NULL, 0U, 0U };


/* Checks whether stack is empty.  Returns non-zero if it is. */
int int_stack_is_empty(const int_stack_t *const stack);

/* Returns element at the top of the stack.  The stack must contain at least one
 * element. */
int int_stack_get_top(const int_stack_t *const stack);

/* Checks element at the top of the stack.  The stack may be empty.  Returns
 * non-zero if top element is val, otherwise zero is returned.  Always returns
 * zero for empty stack. */
int int_stack_top_is(const int_stack_t *const stack, const int val);

/* Set element at the top of the stack.  The stack must contain at least one
 * element. */
void int_stack_set_top(const int_stack_t *const stack, const int val);

/* Puts the val at the top of the stack.  Returns non-zero in case of error. */
int int_stack_push(int_stack_t *const stack, const int val);

/* Removes element from the top of the stack.  The stack must contain at least
 * one element */
void int_stack_pop(int_stack_t *const stack);

/* Removes element from the top of the stack until first occurrence of the
 * seq_guard value and that occurrence.  The stack may be empty. */
void int_stack_pop_seq(int_stack_t *const stack, const int seq_guard);

/* Removes all element from the stack. */
void int_stack_clear(int_stack_t *const stack);

#endif /* VIFM__UTILS__INT_STACK_H__ */

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
