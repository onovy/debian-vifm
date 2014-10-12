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

#include "cmds.h"

#include <assert.h> /* assert() */
#include <ctype.h> /* isalpha() isdigit() isspace() */
#include <stddef.h> /* NULL size_t */
#include <stdio.h>
#include <stdlib.h> /* calloc() malloc() free() realloc() */
#include <string.h>

#include "../utils/log.h"
#include "../utils/macros.h"
#include "../utils/str.h"
#include "../utils/string_array.h"
#include "../utils/test_helpers.h"
#include "../utils/utils.h"
#include "completion.h"

#define MAX_CMD_RECURSION 16
#define MAX_CMD_NAME_LEN 256
#define INVALID_MARK -4096

typedef enum
{
	BUILTIN_ABBR,
	BUILTIN_CMD,
	USER_CMD,
}CMD_TYPE;

typedef struct cmd_t
{
	char *name;
	int id;
	CMD_TYPE type;
	int passed;

	cmd_handler handler;
	char *cmd;

	int range;
	int cust_sep;
	int emark;
	int qmark;
	int expand;
	int min_args, max_args;
	int regexp;
	int select;
	int bg;
	int quote;

	struct cmd_t *next;
}cmd_t;

typedef struct
{
	cmd_t head;
	cmd_add_t user_cmd_handler;
	cmd_handler command_handler;
	int udf_count;
}inner_t;

/* List of characters, which are treated as range separators. */
static const char *RANGE_SEPARATORS = ",;";

static inner_t *inner;
static cmds_conf_t *cmds_conf;

static const char * correct_limit(const char cmd[], cmd_info_t *cmd_info);
static int udf_is_ambiguous(const char name[]);
static const char * parse_tail(cmd_t *cur, const char cmd[],
		cmd_info_t *cmd_info);
static const char * get_cmd_name(const char cmd[], char buf[], size_t buf_len);
static void init_cmd_info(cmd_info_t *cmd_info);
static const char * skip_prefix_commands(const char cmd[]);
static cmd_t * find_cmd(const char name[]);
static const char * parse_range(const char cmd[], cmd_info_t *cmd_info);
static const char * parse_range_elem(const char cmd[], cmd_info_t *cmd_info,
		char last_sep);
static int complete_cmd_args(cmd_t *cur, const char args[],
		cmd_info_t *cmd_info, void *arg);
static void complete_cmd_name(const char cmd_name[], int user_only);
TSTATIC int add_builtin_cmd(const char name[], int abbr, const cmd_add_t *conf);
static int comclear_cmd(const cmd_info_t *cmd_info);
static int command_cmd(const cmd_info_t *cmd_info);
static const char * get_user_cmd_name(const char cmd[], char buf[],
		size_t buf_len);
static int is_correct_name(const char name[]);
static cmd_t * insert_cmd(cmd_t *after);
static int delcommand_cmd(const cmd_info_t *cmd_info);
TSTATIC char ** dispatch_line(const char args[], int *count, char sep,
		int regexp, int quotes, int *last_arg, int *last_begin, int *last_end);
static int is_separator(char c, char sep);
TSTATIC void unescape(char s[], int regexp);

void
init_cmds(int udf, cmds_conf_t *conf)
{
	static cmd_add_t commands[] = {
		{
			.name = "comclear",   .abbr = "comc", .handler = comclear_cmd,   .id = COMCLEAR_CMD_ID,   .quote = 0,
			.range = 0,           .emark = 0,     .qmark = 0,                .regexp = 0,             .select = 0,
			.expand = 0,          .bg = 0,        .min_args = 0,             .max_args = 0,
		}, {
			.name = "command",    .abbr = "com",  .handler = command_cmd,    .id = COMMAND_CMD_ID,    .quote = 0,
			.range = 0,           .emark = 1,     .qmark = 0,                .regexp = 0,             .select = 0,
			.expand = 0,          .bg = 0,        .min_args = 0,             .max_args = NOT_DEF,
		}, {
			.name = "delcommand", .abbr = "delc", .handler = delcommand_cmd, .id = DELCOMMAND_CMD_ID, .quote = 0,
			.range = 0,           .emark = 1,     .qmark = 0,                .regexp = 0,             .select = 0,
			.expand = 0,          .bg = 0,        .min_args = 1,             .max_args = 1,
		}
	};

	cmds_conf = conf;
	inner = conf->inner;

	if(inner == NULL)
	{
		assert(conf->complete_args != NULL);
		assert(conf->swap_range != NULL);
		assert(conf->resolve_mark != NULL);
		assert(conf->expand_macros != NULL);
		assert(conf->expand_envvars != NULL);
		assert(conf->post != NULL);
		assert(conf->select_range != NULL);
		assert(conf->skip_at_beginning != NULL);
		conf->inner = calloc(1, sizeof(inner_t));
		assert(conf->inner != NULL);
		inner = conf->inner;

		if(udf)
			add_builtin_commands(commands, ARRAY_LEN(commands));
	}
}

void
reset_cmds(void)
{
	cmd_t *cur = inner->head.next;

	while(cur != NULL)
	{
		cmd_t *next = cur->next;
		free(cur->cmd);
		free(cur->name);
		free(cur);
		cur = next;
	}

	inner->head.next = NULL;
	inner->user_cmd_handler.handler = NULL;

	free(inner);
	cmds_conf->inner = NULL;
}

int
execute_cmd(const char cmd[])
{
	cmd_info_t cmd_info;
	char cmd_name[MAX_CMD_NAME_LEN];
	cmd_t *cur;
	const char *args;
	int execution_code;
	size_t last_arg_len;
	char *last_arg;
	int last_end;
	cmds_conf_t *cc = cmds_conf;

	init_cmd_info(&cmd_info);
	cmd = parse_range(cmd, &cmd_info);
	if(cmd == NULL)
	{
		if(cmd_info.end == INVALID_MARK)
			return CMDS_ERR_INVALID_RANGE;
		else
			return CMDS_ERR_INVALID_CMD;
	}

	if(*cmd != '\0' && cmd_info.end < cmd_info.begin)
	{
		int t;

		if(!cc->swap_range())
			return CMDS_ERR_INVALID_RANGE;

		t = cmd_info.end;
		cmd_info.end = cmd_info.begin;
		cmd_info.begin = t;
	}

	cmd = get_cmd_name(cmd, cmd_name, sizeof(cmd_name));
	if(udf_is_ambiguous(cmd_name))
		return CMDS_ERR_UDF_IS_AMBIGUOUS;

	cur = inner->head.next;
	while(cur != NULL && strcmp(cur->name, cmd_name) < 0)
		cur = cur->next;

	if(cur == NULL || strncmp(cmd_name, cur->name, strlen(cmd_name)) != 0)
		return CMDS_ERR_INVALID_CMD;

	args = parse_tail(cur, cmd, &cmd_info);

	cmd_info.raw_args = strdup(args);

	/* Set background flag and remove background mark from raw arguments, when
	 * command supports backgrounding. */
	last_arg = get_last_argument(cmd_info.raw_args, &last_arg_len);
	if(cur->bg && *last_arg == '&' && *skip_whitespace(last_arg + 1) == '\0')
	{
		cmd_info.bg = 1;
		*last_arg = '\0';
	}

	if(cur->select)
		cc->select_range(cur->id, &cmd_info);

	if(cur->expand)
	{
		char *p = NULL;
		if(cur->expand & 1)
			cmd_info.args = cc->expand_macros(cmd_info.raw_args, cur->expand & 4,
					&cmd_info.usr1, &cmd_info.usr2);
		if(cur->expand & 2)
		{
			p = cmd_info.args;
			cmd_info.args = cc->expand_envvars(p ? p : cmd_info.raw_args);
		}
		free(p);
	}
	else
	{
		cmd_info.args = strdup(cmd_info.raw_args);
	}
	cmd_info.argv = dispatch_line(cmd_info.args, &cmd_info.argc, cmd_info.sep,
			cur->regexp, cur->quote, NULL, NULL, &last_end);
	cmd_info.args[last_end] = '\0';

	if((cmd_info.begin != NOT_DEF || cmd_info.end != NOT_DEF) && !cur->range)
	{
		execution_code = CMDS_ERR_NO_RANGE_ALLOWED;
	}
	else if(cmd_info.argc < 0)
	{
		execution_code = CMDS_ERR_INVALID_ARG;
	}
	else if(cmd_info.emark && !cur->emark)
	{
		execution_code = CMDS_ERR_NO_BANG_ALLOWED;
	}
	else if(cmd_info.qmark && !cur->qmark)
	{
		execution_code = CMDS_ERR_NO_QMARK_ALLOWED;
	}
	else if(cmd_info.qmark && cur->qmark == 1 && *cmd_info.args != '\0')
	{
		execution_code = CMDS_ERR_TRAILING_CHARS;
	}
	else if(cmd_info.argc < cur->min_args)
	{
		execution_code = CMDS_ERR_TOO_FEW_ARGS;
	}
	else if(cmd_info.argc > cur->max_args && cur->max_args != NOT_DEF)
	{
		execution_code = CMDS_ERR_TRAILING_CHARS;
	}
	else if(cur->passed > MAX_CMD_RECURSION)
	{
		execution_code = CMDS_ERR_LOOP;
	}
	else
	{
		cur->passed++;

		if(cur->type != BUILTIN_CMD && cur->type != BUILTIN_ABBR)
		{
			cmd_info.cmd = cur->cmd;
			execution_code = inner->user_cmd_handler.handler(&cmd_info);
		}
		else
		{
			execution_code = cur->handler(&cmd_info);
		}

		cc->post(cur->id);
		cur->passed--;
	}

	free(cmd_info.raw_args);
	free(cmd_info.args);
	free_string_array(cmd_info.argv, cmd_info.argc);

	return execution_code;
}

static const char *
correct_limit(const char cmd[], cmd_info_t *cmd_info)
{
	while(*cmd == '+' || *cmd == '-')
	{
		int n = 1;
		int plus = *cmd == '+';
		cmd++;
		if(isdigit(*cmd))
		{
			char *p;
			n = strtol(cmd, &p, 10);
			cmd = p;
		}
		if(plus)
			cmd_info->end += n;
		else
			cmd_info->end -= n;
	}
	if(cmd_info->end < 0)
		cmd_info->end = 0;
	if(cmd_info->end > cmds_conf->end)
		cmd_info->end = cmds_conf->end;

	return cmd;
}

static int
udf_is_ambiguous(const char name[])
{
	size_t len;
	int count;
	cmd_t *cur;

	len = strlen(name);
	count = 0;
	cur = inner->head.next;
	while(cur != NULL)
	{
		int cmp;

		cmp = strncmp(cur->name, name, len);
		if(cmp == 0)
		{
			if(cur->name[len] == '\0')
				return 0;
			if(cur->type == USER_CMD)
			{
				char c = cur->name[strlen(cur->name) - 1];
				if(c != '!' && c != '?')
					count++;
			}
		}
		else if(cmp > 0)
		{
			break;
		}

		cur = cur->next;
	}
	return (count > 1);
}

static const char *
parse_tail(cmd_t *cur, const char cmd[], cmd_info_t *cmd_info)
{
	if(*cmd == '!' && (!cur->cust_sep || cur->emark))
	{
		cmd_info->emark = 1;
		cmd++;
	}
	else if(*cmd == '?' && (!cur->cust_sep || cur->qmark))
	{
		cmd_info->qmark = 1;
		cmd++;
	}

	if(*cmd != '\0' && !isspace(*cmd))
	{
		if(cur->cust_sep)
		{
			cmd_info->sep = isspace(*cmd) ? ' ' : *cmd;
		}
		return cmd;
	}

	while(is_separator(*cmd, cmd_info->sep))
	{
		++cmd;
	}

	return cmd;
}

int
get_cmd_id(const char cmd[])
{
	cmd_info_t info;
	return get_cmd_info(cmd, &info);
}

static void
init_cmd_info(cmd_info_t *cmd_info)
{
	cmd_info->begin = NOT_DEF;
	cmd_info->end = NOT_DEF;
	cmd_info->emark = 0;
	cmd_info->qmark = 0;
	cmd_info->raw_args = NULL;
	cmd_info->args = NULL;
	cmd_info->argc = 0;
	cmd_info->argv = NULL;
	cmd_info->cmd = NULL;
	cmd_info->sep = ' ';
	cmd_info->bg = 0;
	cmd_info->usr1 = 0;
	cmd_info->usr2 = 0;
}

/* Returns command id */
int
get_cmd_info(const char cmd[], cmd_info_t *info)
{
	cmd_info_t cmd_info;
	char cmd_name[MAX_CMD_NAME_LEN];
	cmd_t *cur;
	size_t len;

	init_cmd_info(&cmd_info);

	cmd = parse_range(cmd, &cmd_info);
	if(cmd == NULL)
		return CMDS_ERR_INVALID_CMD;

	cmd = get_cmd_name(cmd, cmd_name, sizeof(cmd_name));
	cur = inner->head.next;
	while(cur != NULL && strcmp(cur->name, cmd_name) < 0)
		cur = cur->next;

	len = strlen(cmd_name);
	if(cur == NULL || strncmp(cmd_name, cur->name, len) != 0)
		return -1;

	if(parse_tail(cur, cmd, &cmd_info) == NULL)
		return -1;

	*info = cmd_info;
	return cur->id;
}

int
complete_cmd(const char cmd[], void *arg)
{
	cmd_info_t cmd_info;
	const char *begin, *cmd_name_pos;
	size_t prefix_len;

	begin = cmd;
	cmd = skip_prefix_commands(begin);
	prefix_len = cmd - begin;

	init_cmd_info(&cmd_info);

	cmd_name_pos = parse_range(cmd, &cmd_info);
	if(cmd_name_pos != NULL)
	{
		char cmd_name[MAX_CMD_NAME_LEN];
		const char *args;
		cmd_t *cur;

		args = get_cmd_name(cmd_name_pos, cmd_name, sizeof(cmd_name));
		cur = find_cmd(cmd_name);

		if(*args == '\0' && strcmp(cmd_name, "!") != 0)
		{
			complete_cmd_name(cmd_name, 0);
			prefix_len += cmd_name_pos - cmd;
		}
		else
		{
			prefix_len += args - cmd;
			prefix_len += complete_cmd_args(cur, args, &cmd_info, arg);
		}
	}

	return prefix_len;
}

/* Skips prefix commands (which can be followed by an arbitrary command) at the
 * beginning of command-line. */
static const char *
skip_prefix_commands(const char cmd[])
{
	cmd_info_t cmd_info;
	const char *cmd_name_pos;

	init_cmd_info(&cmd_info);

	cmd_name_pos = parse_range(cmd, &cmd_info);
	if(cmd_name_pos != NULL)
	{
		char cmd_name[MAX_CMD_NAME_LEN];
		const char *args;
		cmd_t *cur;

		args = get_cmd_name(cmd_name_pos, cmd_name, sizeof(cmd_name));
		cur = find_cmd(cmd_name);
		while(cur != NULL && *args != '\0')
		{
			int offset = cmds_conf->skip_at_beginning(cur->id, args);
			if(offset >= 0)
			{
				int delta = (args - cmd) + offset;
				cmd += delta;
				init_cmd_info(&cmd_info);
				cmd_name_pos = parse_range(cmd, &cmd_info);
				if(cmd_name_pos == NULL)
				{
					break;
				}
				args = get_cmd_name(cmd_name_pos, cmd_name, sizeof(cmd_name));
			}
			else
			{
				break;
			}
			cur = find_cmd(cmd_name);
		}
	}
	return cmd;
}

static cmd_t *
find_cmd(const char name[])
{
	cmd_t *cmd;

	cmd = inner->head.next;
	while(cmd != NULL && strcmp(cmd->name, name) < 0)
	{
		cmd = cmd->next;
	}

	if(cmd != NULL && strncmp(name, cmd->name, strlen(name)) != 0)
	{
		cmd = NULL;
	}

	return cmd;
}

/* Parses whole command range (e.g. "<val>;+<val>,,-<val>").  Returns advanced
 * value of cmd when parsing is successful, otherwise NULL is returned. */
static const char *
parse_range(const char cmd[], cmd_info_t *cmd_info)
{
	char last_sep;

	cmd = skip_whitespace(cmd);

	if(isalpha(*cmd) || *cmd == '!' || *cmd == '\0')
	{
		return cmd;
	}

	last_sep = '\0';
	while(*cmd != '\0')
	{
		cmd_info->begin = cmd_info->end;

		cmd = parse_range_elem(cmd, cmd_info, last_sep);
		if(cmd == NULL)
		{
			return NULL;
		}

		cmd = correct_limit(cmd, cmd_info);

		if(cmd_info->begin == NOT_DEF)
			cmd_info->begin = cmd_info->end;

		cmd = skip_whitespace(cmd);

		if(!char_is_one_of(RANGE_SEPARATORS, *cmd))
		{
			break;
		}

		last_sep = *cmd;
		cmd++;

		cmd = skip_whitespace(cmd);
	}

	return cmd;
}

/* Parses single element of a command range (e.g. any of <el> in
 * "<el>;<el>,<el>").  Returns advanced value of cmd when parsing is successful,
 * otherwise NULL is returned. */
static const char *
parse_range_elem(const char cmd[], cmd_info_t *cmd_info, char last_sep)
{
	if(cmd[0] == '%')
	{
		cmd_info->begin = cmds_conf->begin;
		cmd_info->end = cmds_conf->end;
		cmd++;
	}
	else if(cmd[0] == '$')
	{
		cmd_info->end = cmds_conf->end;
		cmd++;
	}
	else if(cmd[0] == '.')
	{
		cmd_info->end = cmds_conf->current;
		cmd++;
	}
	else if(char_is_one_of(RANGE_SEPARATORS, *cmd))
	{
		cmd_info->end = cmds_conf->current;
	}
	else if(isalpha(*cmd))
	{
		cmd_info->end = cmds_conf->current;
	}
	else if(isdigit(*cmd))
	{
		char *p;
		cmd_info->end = strtol(cmd, &p, 10) - 1;
		if(cmd_info->end < cmds_conf->begin)
			cmd_info->end = cmds_conf->begin;
		cmd = p;
	}
	else if(*cmd == '\'')
	{
		char mark;
		cmd++;
		mark = *cmd++;
		cmd_info->end = cmds_conf->resolve_mark(mark);
		if(cmd_info->end < 0)
		{
			cmd_info->end = INVALID_MARK;
			return NULL;
		}
	}
	else if(*cmd == '+' || *cmd == '-')
	{
		/* Do nothing after semicolon, because in this case +/- are adjusting not
		 * base current cursor position, but base end of the range. */
		if(last_sep != ';')
		{
			cmd_info->end = cmds_conf->current;
		}
	}
	else
	{
		return NULL;
	}

	return cmd;
}

static const char *
get_cmd_name(const char cmd[], char buf[], size_t buf_len)
{
	const char *t;
	size_t len;

	assert(buf_len != 0 && "The buffer is expected to be of size > 0.");

	if(cmd[0] == '!')
	{
		strcpy(buf, "!");
		cmd = skip_whitespace(cmd + 1);
		return cmd;
	}

	t = cmd;
	while(isalpha(*t))
		t++;

	len = MIN(t - cmd, buf_len - 1);
	strncpy(buf, cmd, len);
	buf[len] = '\0';
	if(*t == '?' || *t == '!')
	{
		int cmp;
		cmd_t *cur;

		cur = inner->head.next;
		while(cur != NULL && (cmp = strncmp(cur->name, buf, len)) <= 0)
		{
			if(cmp == 0 && cur->type == USER_CMD &&
					cur->name[strlen(cur->name) - 1] == *t)
			{
				strncpy(buf, cur->name, buf_len);
				break;
			}
			cur = cur->next;
		}
		if(cur != NULL && strncmp(cur->name, buf, len) == 0)
			t++;
	}

	return t;
}

/* Returns offset at which completion was done. */
static int
complete_cmd_args(cmd_t *cur, const char args[], cmd_info_t *cmd_info,
		void *arg)
{
	const char *tmp_args = args;
	int result = 0;

	if(cur == NULL || (cur->id >= NO_COMPLETION_BOUNDARY && cur->id < 0))
		return 0;

	args = parse_tail(cur, tmp_args, cmd_info);
	args = skip_whitespace(args);
	result += args - tmp_args;

	if(cur->id == COMMAND_CMD_ID || cur->id == DELCOMMAND_CMD_ID)
	{
		const char *arg;

		arg = strrchr(args, ' ');
		arg = (arg == NULL) ? args : (arg + 1);

		complete_cmd_name(arg, 1);
		result += arg - args;
	}
	else
	{
		int argc;
		char **argv;
		int last_arg = 0;

		argv = dispatch_line(args, &argc, ' ', 0, 1, &last_arg, NULL, NULL);
		result += cmds_conf->complete_args(cur->id, args, argc, argv, last_arg,
				arg);
		free_string_array(argv, argc);
	}
	return result;
}

static void
complete_cmd_name(const char cmd_name[], int user_only)
{
	cmd_t *cur;
	size_t len;

	cur = inner->head.next;
	while(cur != NULL && strcmp(cur->name, cmd_name) < 0)
		cur = cur->next;

	len = strlen(cmd_name);
	while(cur != NULL && strncmp(cur->name, cmd_name, len) == 0)
	{
		if(cur->type == BUILTIN_ABBR)
			;
		else if(cur->type != USER_CMD && user_only)
			;
		else if(cur->name[0] == '\0')
			;
		else
			vle_compl_add_match(cur->name);
		cur = cur->next;
	}

	vle_compl_add_last_match(cmd_name);
}

void
add_builtin_commands(const cmd_add_t *cmds, int count)
{
	int i;
	for(i = 0; i < count; i++)
	{
		int ret_code;
		assert(cmds[i].min_args >= 0);
		assert(cmds[i].max_args == NOT_DEF ||
				cmds[i].min_args <= cmds[i].max_args);
		ret_code = add_builtin_cmd(cmds[i].name, 0, &cmds[i]);
		assert(ret_code == 0);
		if(cmds[i].abbr != NULL)
		{
			size_t full_len, short_len;
			char buf[strlen(cmds[i].name) + 1];
			assert(starts_with(cmds[i].name, cmds[i].abbr) &&
					"Abbreviation is consistent with full command");
			strcpy(buf, cmds[i].name);
			full_len = strlen(buf);
			short_len = strlen(cmds[i].abbr);
			while(full_len > short_len)
			{
				buf[--full_len] = '\0';
				ret_code = add_builtin_cmd(buf, 1, &cmds[i]);
				assert(ret_code == 0);
			}
		}
	}
}

/* Returns non-zero on error */
TSTATIC int
add_builtin_cmd(const char name[], int abbr, const cmd_add_t *conf)
{
	int i;
	int cmp;
	cmd_t *new;
	cmd_t *cur = &inner->head;

	if(strcmp(name, "<USERCMD>") == 0)
	{
		if(inner->user_cmd_handler.handler != NULL)
			return -1;
		inner->user_cmd_handler = *conf;
		return 0;
	}

	if(strcmp(name, "!") != 0)
		for(i = 0; name[i] != '\0'; i++)
			if(!isalpha(name[i]))
				return -1;

	cmp = -1;
	while(cur->next != NULL && (cmp = strcmp(cur->next->name, name)) < 0)
		cur = cur->next;

	/* command with the same name already exists */
	if(cmp == 0)
	{
		if(strncmp(name, "command", strlen(name)) == 0)
		{
			inner->command_handler = conf->handler;
			return 0;
		}
		return -1;
	}

	if((new = insert_cmd(cur)) == NULL)
		return -1;
	new->name = strdup(name);
	new->id = conf->id;
	new->handler = conf->handler;
	new->type = abbr ? BUILTIN_ABBR : BUILTIN_CMD;
	new->passed = 0;
	new->range = conf->range;
	new->cust_sep = conf->cust_sep;
	new->emark = conf->emark;
	new->qmark = conf->qmark;
	new->expand = conf->expand;
	new->min_args = conf->min_args;
	new->max_args = conf->max_args;
	new->regexp = conf->regexp;
	new->select = conf->select;
	new->bg = conf->bg;
	new->quote = conf->quote;
	new->cmd = NULL;

	return 0;
}

static int
comclear_cmd(const cmd_info_t *cmd_info)
{
	cmd_t *cur = &inner->head;

	while(cur->next != NULL)
	{
		if(cur->next->type == USER_CMD)
		{
			cmd_t *this = cur->next;
			cur->next = this->next;

			free(this->cmd);
			free(this->name);
			free(this);
		}
		else
		{
			cur = cur->next;
		}
	}
	inner->udf_count = 0;
	return 0;
}

static int
command_cmd(const cmd_info_t *cmd_info)
{
	int cmp;
	char cmd_name[MAX_CMD_NAME_LEN];
	const char *args;
	cmd_t *new, *cur;

	if(cmd_info->argc < 2)
	{
		if(inner->command_handler != NULL)
			return inner->command_handler(cmd_info);
		else
			return CMDS_ERR_TOO_FEW_ARGS;
	}

	args = get_user_cmd_name(cmd_info->args, cmd_name, sizeof(cmd_name));
	args = skip_whitespace(args);
	if(args[0] == '\0')
		return CMDS_ERR_TOO_FEW_ARGS;
	else if(!is_correct_name(cmd_name))
		return CMDS_ERR_INCORRECT_NAME;

	cmp = -1;
	cur = &inner->head;
	while(cur->next != NULL && (cmp = strcmp(cur->next->name, cmd_name)) < 0)
		cur = cur->next;

	if(cmp == 0)
	{
		cur = cur->next;
		if(cur->type == BUILTIN_CMD)
			return CMDS_ERR_NO_BUILTIN_REDEFINE;
		if(!cmd_info->emark)
			return CMDS_ERR_NEED_BANG;
		free(cur->name);
		free(cur->cmd);
    new = cur;
	}
	else
	{
		if((new = insert_cmd(cur)) == NULL)
		{
			return CMDS_ERR_NO_MEM;
		}
	}

	new->name = strdup(cmd_name);
	new->id = USER_CMD_ID;
	new->type = USER_CMD;
	new->passed = 0;
	new->cmd = strdup(args);
	new->range = inner->user_cmd_handler.range;
	new->cust_sep = inner->user_cmd_handler.cust_sep;
	new->emark = inner->user_cmd_handler.emark;
	new->qmark = inner->user_cmd_handler.qmark;
	new->expand = inner->user_cmd_handler.expand;
	new->min_args = inner->user_cmd_handler.min_args;
	new->max_args = inner->user_cmd_handler.max_args;
	new->regexp = inner->user_cmd_handler.regexp;
	new->select = inner->user_cmd_handler.select;
	new->bg = inner->user_cmd_handler.bg;
	new->quote = inner->user_cmd_handler.quote;

	inner->udf_count++;
	return 0;
}

static const char *
get_user_cmd_name(const char cmd[], char buf[], size_t buf_len)
{
	const char *t;
	size_t len;

	t = skip_non_whitespace(cmd);

	len = MIN(t - cmd, buf_len);
	strncpy(buf, cmd, len);
	buf[len] = '\0';
	return t;
}

static int
is_correct_name(const char name[])
{
	if(strcmp(name, "!") == 0)
		return 0;

	if(strcmp(name, "?") == 0)
		return 0;

	while(name[0] != '\0')
	{
		if(!isalpha(name[0]))
		{
			if(name[1] != '\0')
				return 0;
			else if(name[0] != '!' && name[0] != '?')
				return 0;
		}
		name++;
	}
	return 1;
}

static cmd_t *
insert_cmd(cmd_t *after)
{
	cmd_t *new;
  new = malloc(sizeof(*new));
	if(new == NULL)
		return NULL;
	new->next = after->next;
	after->next = new;
	return new;
}

static int
delcommand_cmd(const cmd_info_t *cmd_info)
{
	int cmp;
	cmd_t *cur;
	cmd_t *cmd;

	cmp = -1;
	cur = &inner->head;
	while(cur->next != NULL &&
			(cmp = strcmp(cur->next->name, cmd_info->argv[0])) < 0)
		cur = cur->next;

	if(cur->next == NULL || cmp != 0)
		return CMDS_ERR_NO_SUCH_UDF;

	cmd = cur->next;
	cur->next = cmd->next;
	free(cmd->name);
	free(cmd->cmd);
	free(cmd);

	inner->udf_count--;
	return 0;
}

char *
get_last_argument(const char cmd[], size_t *len)
{
	int argc;
	char **argv;
	int last_start = 0;
	int last_end = 0;

	argv = dispatch_line(cmd, &argc, ' ', 0, 1, NULL, &last_start, &last_end);
	*len = last_end - last_start;
	free_string_array(argv, argc);
	return (char *)cmd + last_start;
}

/* Splits argument string into array of strings.  Returns NULL if no arguments
 * are found or an error occurred.  Always sets *count (to negative value on
 * unmatched quotes and to zero on all other errors). */
TSTATIC char **
dispatch_line(const char args[], int *count, char sep, int regexp, int quotes,
		int *last_pos, int *last_begin, int *last_end)
{
	char *cmdstr;
	int len;
	int i;
	int st;
	const char *args_beg;
	char** params;

	enum { BEGIN, NO_QUOTING, S_QUOTING, D_QUOTING, R_QUOTING, ARG, QARG } state;

	if(last_pos != NULL)
		*last_pos = 0;
	if(last_begin != NULL)
		*last_begin = 0;
	if(last_end != NULL)
		*last_end = 0;

	*count = 0;
	params = NULL;

	args_beg = args;
	if(sep == ' ')
	{
		args = skip_whitespace(args);
	}
	cmdstr = strdup(args);
	len = strlen(cmdstr);
	for(i = 0, st = 0, state = BEGIN; i <= len; ++i)
	{
		const int prev_state = state;
		switch(state)
		{
			case BEGIN:
				if(sep == ' ' && cmdstr[i] == '\'' && quotes)
				{
					st = i + 1;
					state = S_QUOTING;
				}
				else if(sep == ' ' && cmdstr[i] == '"' && quotes)
				{
					st = i + 1;
					state = D_QUOTING;
				}
				else if(sep == ' ' && cmdstr[i] == '/' && regexp)
				{
					st = i + 1;
					state = R_QUOTING;
				}
				else if(!is_separator(cmdstr[i], sep))
				{
					st = i;
					state = NO_QUOTING;
				}
				else if(sep != ' ' && i > 0 && is_separator(cmdstr[i - 1], sep))
				{
					st = i--;
					state = NO_QUOTING;
				}
				if(state != BEGIN && cmdstr[i] != '\0' && last_begin != NULL)
				{
					*last_begin = i;
				}
				break;
			case NO_QUOTING:
				if(cmdstr[i] == '\0' || is_separator(cmdstr[i], sep))
				{
					state = ARG;
				}
				else if(cmdstr[i] == '\\' && cmdstr[i + 1] != '\0')
				{
					i++;
				}
				break;
			case S_QUOTING:
				if(cmdstr[i] == '\'')
				{
					if(cmdstr[i + 1] == '\'')
					{
						i++;
					}
					else
					{
						state = QARG;
					}
				}
				break;
			case D_QUOTING:
				if(cmdstr[i] == '"')
				{
					state = QARG;
				}
				else if(cmdstr[i] == '\\' && cmdstr[i + 1] != '\0')
				{
					i++;
				}
				break;
			case R_QUOTING:
				if(cmdstr[i] == '/')
					state = QARG;
				else if(cmdstr[i] == '\\' && cmdstr[i + 1] == '/')
				{
						i++;
				}
				break;

			case ARG:
			case QARG:
				assert(0 && "Dispatch line state machine is broken");
		}
		if(state == ARG || state == QARG)
		{
			char *last_arg;
			const char c = cmdstr[i];
			/* found another argument */
			cmdstr[i] = '\0';
			if(last_end != NULL)
				*last_end = (args - args_beg) + ((state == ARG) ? i : (i + 1));

			*count = add_to_string_array(&params, *count, 1, &cmdstr[st]);
			if(*count == 0)
			{
				break;
			}
			last_arg = params[*count - 1];

			cmdstr[i] = c;
			switch(prev_state)
			{
				case NO_QUOTING: unescape(last_arg, sep != ' ');    break;
				case  S_QUOTING: expand_squotes_escaping(last_arg); break;
				case  D_QUOTING: expand_dquotes_escaping(last_arg); break;
				case  R_QUOTING: unescape(last_arg, 1);             break;
			}
			state = BEGIN;
		}
	}

	free(cmdstr);

	if(*count == 0 || (state != BEGIN && state != NO_QUOTING) ||
			put_into_string_array(&params, *count, NULL) != *count + 1)
	{
		free_string_array(params, *count);
		*count = (state == S_QUOTING || state == D_QUOTING) ? -1 : 0;
		return NULL;
	}

	if(last_pos != NULL)
	{
		*last_pos = (args - args_beg) + st;
	}

	return params;
}

/* Checks whether character is command separator.  Special case is space as a
 * separator, which means that any whitespace can be used as separators.
 * Returns non-zero if c is counted to be a separator, otherwise zero is
 * returned. */
static int
is_separator(char c, char sep)
{
	return (sep == ' ') ? isspace(c) : (c == sep);
}

TSTATIC void
unescape(char s[], int regexp)
{
	char *p;

	p = s;
	while(s[0] != '\0')
	{
		if(s[0] == '\\' && (!regexp || s[1] == '/'))
			s++;
		*p++ = s[0];
		if(s[0] != '\0')
		{
			s++;
		}
	}
	*p = '\0';
}

char **
list_udf(void)
{
	char **list;
	char **p;
	cmd_t *cur;

	if((list = malloc(sizeof(*list)*(inner->udf_count*2 + 1))) == NULL)
		return NULL;

	p = list;

	cur = inner->head.next;
	while(cur != NULL)
	{
		if(cur->type == USER_CMD)
		{
			*p++ = strdup(cur->name);
			*p++ = strdup(cur->cmd);
		}
		cur = cur->next;
	}

	*p = NULL;

	return list;
}

char *
list_udf_content(const char beginning[])
{
	size_t len;
	cmd_t *cur;
	char *content = NULL;
	size_t content_len = 0;

	cur = inner->head.next;
	len = strlen(beginning);
	while(cur != NULL)
	{
		void *ptr;
		size_t new_size;

		if(strncmp(cur->name, beginning, len) != 0 || cur->type != USER_CMD)
		{
			cur = cur->next;
			continue;
		}

		if(content == NULL)
		{
			content = strdup("Command -- Action");
			content_len = strlen(content);
		}
		new_size = content_len + 1 + strlen(cur->name) + 10 + strlen(cur->cmd) + 1;
		ptr = realloc(content, new_size);
		if(ptr != NULL)
		{
			content = ptr;
			content_len += sprintf(content + content_len, "\n%-*s %s", 10, cur->name,
					cur->cmd);
		}
		cur = cur->next;
	}

	return content;
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 : */
