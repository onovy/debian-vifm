/* vifm
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

#include "utf8.h"

#include <assert.h> /* assert() */
#include <stddef.h> /* size_t wchar_t */
#include <stdlib.h> /* malloc() */
#include <string.h> /* strlen() */

#include "../compat/reallocarray.h"
#include "macros.h"
#include "utils.h"

static size_t guess_char_width(char c);
static wchar_t utf8_char_to_wchar(const char str[], size_t char_width);
static size_t chrsw(const char str[], size_t char_width);
static size_t utf8_narrowed_len(const wchar_t utf16[]);

size_t
utf8_chrw(const char str[])
{
	const size_t expected = guess_char_width(str[0]);
	if(expected == 2 && (str[1] & 0xc0) == 0x80)
		return 2;
	else if(expected == 3 && (str[1] & 0xc0) == 0x80 && (str[2] & 0xc0) == 0x80)
		return 3;
	else if(expected == 4 && (str[1] & 0xc0) == 0x80 && (str[2] & 0xc0) == 0x80 &&
			(str[3] & 0xc0) == 0x80)
		return 4;
	else if(str[0] == '\0')
		return 0;
	return 1;
}

/* Determines width of a utf-8 characted by its first byte. */
static size_t
guess_char_width(char c)
{
	if((c & 0xe0) == 0xc0)
		return 2;
	else if((c & 0xf0) == 0xe0)
		return 3;
	else if((c & 0xf8) == 0xf0)
		return 4;
	return 1;
}

size_t
utf8_strsnlen(const char str[], size_t max_screen_width)
{
	size_t width = 0;
	while(*str != '\0' && max_screen_width != 0)
	{
		size_t char_width = utf8_chrw(str);
		size_t char_screen_width = chrsw(str, char_width);
		if(char_screen_width > max_screen_width)
		{
			break;
		}
		max_screen_width -= char_screen_width;
		width += char_width;
		str += char_width;
	}
	return width;
}

size_t
utf8_nstrlen(const char str[])
{
	size_t length_left = strlen(str);
	size_t length = 0;
	while(length_left != '\0')
	{
		const size_t char_width = utf8_chrw(str);
		if(char_width > length_left)
		{
			break;
		}

		++length;
		str += char_width;
		length_left -= char_width;
	}
	return length;
}

size_t
utf8_nstrsnlen(const char str[], size_t max_screen_width)
{
	size_t length_left = strlen(str);
	size_t length = 0;
	while(length_left != 0 && max_screen_width > 0)
	{
		size_t char_screen_width;
		const size_t char_width = utf8_chrw(str);
		if(char_width > length_left)
		{
			break;
		}

		char_screen_width = chrsw(str, char_width);
		if(char_screen_width > max_screen_width)
		{
			break;
		}

		length += char_width;
		max_screen_width -= char_screen_width;
		str += char_width;
		length_left -= char_width;
	}
	return length;
}

/* Converts one utf-8 encoded character to wide character form. */
static wchar_t
utf8_char_to_wchar(const char str[], size_t char_width)
{
	/* First mask is a fake one, to omit decrementing of char_width. */
	static const int masks[] = { 0x00, 0xff, 0x1f, 0x0f, 0x07 };

	wchar_t result;

	assert(char_width != 0 && "There are no zero width utf-8 characters.");
	assert(char_width < ARRAY_LEN(masks) && "Too long utf-8 character.");

	result = *str&masks[char_width];
	while(--char_width != 0)
	{
		result = (result << 6)|(*++str&0x3f);
	}

	return result;
}

size_t
utf8_strsw(const char str[])
{
	size_t length = 0;
	while(*str != '\0')
	{
		const size_t char_width = utf8_chrw(str);
		const size_t char_screen_width = chrsw(str, char_width);
		str += char_width;
		length += char_screen_width;
	}
	return length;
}

size_t
utf8_strsw_with_tabs(const char str[], int tab_stops)
{
	size_t length = 0U;

	assert(tab_stops > 0 && "Non-positive number of tab stops.");

	while(*str != '\0')
	{
		size_t char_screen_width;
		const size_t char_width = utf8_chrw(str);

		if(char_width == 1 && *str == '\t')
		{
			char_screen_width = tab_stops - length%tab_stops;
		}
		else
		{
			char_screen_width = chrsw(str, char_width);
		}

		str += char_width;
		length += char_screen_width;
	}
	return length;
}

size_t
utf8_chrsw(const char str[])
{
	return chrsw(str, utf8_chrw(str));
}

/* Returns width of the character in the terminal. */
static size_t
chrsw(const char str[], size_t char_width)
{
	const wchar_t wide = utf8_char_to_wchar(str, char_width);
	const size_t result = vifm_wcwidth(wide);
	return (result == (size_t)-1) ? 1 : result;
}

size_t
utf8_stro(const char str[])
{
	size_t overhead = 0;
	while(*str != '\0')
	{
		size_t char_width = utf8_chrw(str);
		str += char_width;
		overhead += char_width - 1;
	}
	return overhead;
}

size_t
utf8_strso(const char str[])
{
	size_t overhead = 0;
	while(*str != '\0')
	{
		const size_t char_width = utf8_chrw(str);
		const size_t char_screen_width = chrsw(str, char_width);
		str += char_width;
		overhead += (char_width - 1) - (char_screen_width - 1);
	}
	return overhead;
}

wchar_t *
utf8_to_utf16(const char utf8[])
{
	size_t size = utf8_widen_len(utf8);

	wchar_t *const utf16 = reallocarray(NULL, size + 1, sizeof(wchar_t));

	wchar_t *t = utf16;
	const char *p = utf8;
	unsigned char c;
	while((c = *p++) != '\0')
	{
		wchar_t wc;
		if(c < 0x80)
		{
			wc = c;
		}
		else if((c & 0xe0) == 0xc0)
		{
			wc = ((c & 0x1f) << 6) | ((*p++) & 0x3f);
		}
		else if((c & 0xf0) == 0xe0)
		{
			wc = ((c & 0x0f) << 12) | ((p[0] & 0x3f) << 6) | (p[1] & 0x3f);
			p += 2;
		}
		else
		{
			const unsigned int r32 = ((c & 0x07) << 18)
			                       | ((p[0] & 0x3f) << 12)
			                       | ((p[1] & 0x3f) << 6)
			                       | (p[2] & 0x3f);
			p += 3;
			*t++ = 0xd800 | (((r32 - 0x10000) >> 10) & 0x3ff);
			wc = 0xdc00 | (r32 & 0x3ff);
		}
		*t++ = wc;
	}
	*t = 0;

	return utf16;
}

wchar_t
utf8_first_char(const char utf8[])
{
	/* This code is a copy of utf8_to_utf16() loop body, which is hard to share
	 * between these two functions.  Luckily this shouldn't require much updates
	 * in the future. */

	unsigned char c = *utf8++;
	wchar_t wc;

	if(c < 0x80)
	{
		wc = c;
	}
	else if((c & 0xe0) == 0xc0)
	{
		wc = ((c & 0x1f) << 6) | (utf8[0] & 0x3f);
	}
	else if((c & 0xf0) == 0xe0)
	{
		wc = ((c & 0x0f) << 12) | ((utf8[0] & 0x3f) << 6) | (utf8[1] & 0x3f);
	}
	else
	{
		const unsigned int r32 = ((c & 0x07) << 18)
		                       | ((utf8[0] & 0x3f) << 12)
		                       | ((utf8[1] & 0x3f) << 6)
		                       | (utf8[2] & 0x3f);
		wc = 0xd800 | (((r32 - 0x10000) >> 10) & 0x3ff);
	}

	return wc;
}

size_t
utf8_widen_len(const char utf8[])
{
	size_t size = 0;
	const char *p = utf8;
	unsigned char c;
	while((c = *p++) != '\0')
	{
		++size;
		if(c < 0x80)
		{
			/* Do nothing. */
		}
		else if((c&0xe0) == 0xc0)
		{
			++p;
		}
		else if((c&0xf0) == 0xe0)
		{
			p += 2;
		}
		else
		{
			p += 3;
			/* Surrogate. */
			++size;
		}
	}
	return size;
}

char *
utf8_from_utf16(const wchar_t utf16[])
{
	const size_t size = utf8_narrowed_len(utf16);

	char *const utf8 = malloc(size + 1);

	const wchar_t *p = utf16;
	char *t = utf8;
	unsigned short int c;
	while((c = *p++) != 0)
	{
		if(c < 0x80)
		{
			/* 7 bit (ascii). */
			*t++ = (char)c;
		}
		else if(c < 0x0800)
		{
			/* 11 bit. */
			*t++ = (char)(0xc0 | (c >> 6));
			*t++ = (char)(0x80 | (c & 0x3f));
		}
		else if((c&0xf8) != 0xd8)
		{
			/* 16 bit. */
			*t++ = (char)(0xe0 | (c >> 12));
			*t++ = (char)(0x80 | ((c >> 6) & 0x3f));
			*t++ = (char)(0x80 | (c & 0x3f));
		}
		else
		{
			/* 21 bit - surrogate pair. */
			const unsigned short int c1 = (*p ? *p++ : 0xdc);
			/* utf-32 character. */
			const unsigned int d = (((c & 0x3ff) << 10) | (c1 & 0x3ff)) + 0x10000;
			*t++ = (char)(0xf0 | ((d >> 18) & 0x7));
			*t++ = (char)(0x80 | ((d >> 12) & 0x3f));
			*t++ = (char)(0x80 | ((d >> 6) & 0x3f));
			*t++ = (char)(0x80 | (d & 0x3f));
		}
	}
	*t = '\0';

	return utf8;
}

/* Calculate how many utf8 chars are needed to store given utf-16 string.
 * Returns the number. */
static size_t
utf8_narrowed_len(const wchar_t utf16[])
{
	const wchar_t *p = utf16;
	size_t len = 0;
	unsigned short int c;
	while((c = *p++) != 0)
	{
		if(c < 0x80)
		{
			++len;
		}
		else if(c < 0x0800)
		{
			len += 2;
		}
		else if((c & 0xf8) != 0xd8)
		{
			len += 3;
		}
		else
		{
			++p;
			len += 4;
		}
	}
	return len;
}

size_t
utf8_strcpy(char dst[], const char src[], size_t dst_len)
{
	const size_t len = dst_len;
	if(dst_len == 0U)
	{
		return 0U;
	}

	while(*src != '\0' && dst_len > 1U)
	{
		size_t char_width = utf8_chrw(src);
		if(char_width >= dst_len)
		{
			break;
		}
		while(char_width-- != 0)
		{
			*dst++ = *src++;
			--dst_len;
		}
	}

	*dst = '\0';
	return len - (dst_len - 1U);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
