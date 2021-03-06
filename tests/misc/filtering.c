#include <stic.h>

#include <stdlib.h> /* free() */
#include <string.h> /* strdup() */

#include "../../src/cfg/config.h"
#include "../../src/ui/ui.h"
#include "../../src/utils/dynarray.h"
#include "../../src/filtering.h"

#define assert_hidden(view, name, dir) \
	assert_false(file_is_visible(&view, name, dir))

#define assert_visible(view, name, dir) \
	assert_true(file_is_visible(&view, name, dir))

SETUP()
{
	cfg.slow_fs_list = strdup("");

	cfg.filter_inverted_by_default = 1;

	lwin.list_rows = 7;
	lwin.list_pos = 2;
	lwin.dir_entry = dynarray_cextend(NULL,
			lwin.list_rows*sizeof(*lwin.dir_entry));
	lwin.dir_entry[0].name = strdup("with(round)");
	lwin.dir_entry[0].origin = &lwin.curr_dir[0];
	lwin.dir_entry[1].name = strdup("with[square]");
	lwin.dir_entry[1].origin = &lwin.curr_dir[0];
	lwin.dir_entry[2].name = strdup("with{curly}");
	lwin.dir_entry[2].origin = &lwin.curr_dir[0];
	lwin.dir_entry[3].name = strdup("with<angle>");
	lwin.dir_entry[3].origin = &lwin.curr_dir[0];
	lwin.dir_entry[4].name = strdup("withSPECS+*^$?|\\");
	lwin.dir_entry[4].origin = &lwin.curr_dir[0];
	lwin.dir_entry[5].name = strdup("with....dots");
	lwin.dir_entry[5].origin = &lwin.curr_dir[0];
	lwin.dir_entry[6].name = strdup("withnonodots");
	lwin.dir_entry[6].origin = &lwin.curr_dir[0];

	lwin.dir_entry[0].selected = 1;
	lwin.dir_entry[1].selected = 1;
	lwin.dir_entry[2].selected = 1;
	lwin.dir_entry[3].selected = 1;
	lwin.dir_entry[4].selected = 1;
	lwin.dir_entry[5].selected = 1;
	lwin.dir_entry[6].selected = 0;
	lwin.selected_files = 6;

	filter_init(&lwin.manual_filter, FILTER_DEF_CASE_SENSITIVITY);
	filter_init(&lwin.auto_filter, FILTER_DEF_CASE_SENSITIVITY);
	lwin.invert = cfg.filter_inverted_by_default;

	lwin.column_count = 1;

	rwin.list_rows = 8;
	rwin.list_pos = 2;
	rwin.dir_entry = dynarray_cextend(NULL,
			rwin.list_rows*sizeof(*rwin.dir_entry));
	rwin.dir_entry[0].name = strdup("dir1.d");
	rwin.dir_entry[0].origin = &rwin.curr_dir[0];
	rwin.dir_entry[1].name = strdup("dir2.d");
	rwin.dir_entry[1].origin = &rwin.curr_dir[0];
	rwin.dir_entry[2].name = strdup("dir3.d");
	rwin.dir_entry[2].origin = &rwin.curr_dir[0];
	rwin.dir_entry[3].name = strdup("file1.d");
	rwin.dir_entry[3].origin = &rwin.curr_dir[0];
	rwin.dir_entry[4].name = strdup("file2.d");
	rwin.dir_entry[4].origin = &rwin.curr_dir[0];
	rwin.dir_entry[5].name = strdup("file3.d");
	rwin.dir_entry[5].origin = &rwin.curr_dir[0];
	rwin.dir_entry[6].name = strdup("withnonodots");
	rwin.dir_entry[6].origin = &rwin.curr_dir[0];
	rwin.dir_entry[7].name = strdup("somedir");
	rwin.dir_entry[7].origin = &rwin.curr_dir[0];

	rwin.dir_entry[0].selected = 0;
	rwin.dir_entry[1].selected = 0;
	rwin.dir_entry[2].selected = 0;
	rwin.dir_entry[3].selected = 0;
	rwin.dir_entry[4].selected = 0;
	rwin.dir_entry[5].selected = 0;
	rwin.dir_entry[6].selected = 0;
	rwin.dir_entry[7].selected = 0;
	rwin.selected_files = 0;

	filter_init(&rwin.manual_filter, FILTER_DEF_CASE_SENSITIVITY);
	filter_init(&rwin.auto_filter, FILTER_DEF_CASE_SENSITIVITY);
	rwin.invert = cfg.filter_inverted_by_default;

	rwin.column_count = 1;
}

static void
cleanup_view(FileView *view)
{
	int i;

	for(i = 0; i < view->list_rows; i++)
		free(view->dir_entry[i].name);
	dynarray_free(view->dir_entry);
	filter_dispose(&view->manual_filter);
	filter_dispose(&view->auto_filter);
}

TEARDOWN()
{
	free(cfg.slow_fs_list);
	cfg.slow_fs_list = NULL;

	cleanup_view(&lwin);
	cleanup_view(&rwin);
}

TEST(filtering)
{
	assert_int_equal(7, lwin.list_rows);
	filter_selected_files(&lwin);
	assert_int_equal(1, lwin.list_rows);

	assert_string_equal("withnonodots", lwin.dir_entry[0].name);
	assert_visible(lwin, lwin.dir_entry[0].name, 0);
}

TEST(filtering_file_does_not_filter_dir)
{
	char *const name = strdup(rwin.dir_entry[6].name);

	rwin.dir_entry[6].selected = 1;
	rwin.selected_files = 1;

	assert_int_equal(8, rwin.list_rows);
	filter_selected_files(&rwin);
	assert_int_equal(7, rwin.list_rows);

	assert_hidden(rwin, name, 0);
	assert_visible(rwin, name, 1);

	free(name);
}

TEST(filtering_dir_does_not_filter_file)
{
	char *const name = strdup(rwin.dir_entry[6].name);

	rwin.dir_entry[6].selected = 1;
	rwin.dir_entry[6].type = FT_DIR;
	rwin.selected_files = 1;

	assert_int_equal(8, rwin.list_rows);
	filter_selected_files(&rwin);
	assert_int_equal(7, rwin.list_rows);

	assert_hidden(rwin, name, 1);
	assert_visible(rwin, name, 0);

	free(name);
}

TEST(filtering_files_does_not_filter_dirs)
{
	(void)filter_set(&rwin.manual_filter, "^.*\\.d$");

	assert_visible(rwin, rwin.dir_entry[0].name, 1);
	assert_visible(rwin, rwin.dir_entry[1].name, 1);
	assert_visible(rwin, rwin.dir_entry[2].name, 1);
	assert_hidden(rwin, rwin.dir_entry[3].name, 0);
	assert_hidden(rwin, rwin.dir_entry[4].name, 0);
	assert_hidden(rwin, rwin.dir_entry[5].name, 0);
	assert_visible(rwin, rwin.dir_entry[6].name, 0);
	assert_visible(rwin, rwin.dir_entry[7].name, 1);
	assert_int_equal(8, rwin.list_rows);
}

TEST(filtering_dirs_does_not_filter_files)
{
	(void)filter_set(&rwin.manual_filter, "^.*\\.d/$");

	assert_hidden(rwin, rwin.dir_entry[0].name, 1);
	assert_hidden(rwin, rwin.dir_entry[1].name, 1);
	assert_hidden(rwin, rwin.dir_entry[2].name, 1);
	assert_visible(rwin, rwin.dir_entry[3].name, 0);
	assert_visible(rwin, rwin.dir_entry[4].name, 0);
	assert_visible(rwin, rwin.dir_entry[5].name, 0);
	assert_visible(rwin, rwin.dir_entry[6].name, 0);
	assert_visible(rwin, rwin.dir_entry[7].name, 1);
	assert_int_equal(8, rwin.list_rows);
}

TEST(filtering_files_and_dirs)
{
	(void)filter_set(&rwin.manual_filter, "^.*\\.d/?$");

	assert_hidden(rwin, rwin.dir_entry[0].name, 1);
	assert_hidden(rwin, rwin.dir_entry[1].name, 1);
	assert_hidden(rwin, rwin.dir_entry[2].name, 1);
	assert_hidden(rwin, rwin.dir_entry[3].name, 0);
	assert_hidden(rwin, rwin.dir_entry[4].name, 0);
	assert_hidden(rwin, rwin.dir_entry[5].name, 0);
	assert_visible(rwin, rwin.dir_entry[6].name, 0);
	assert_visible(rwin, rwin.dir_entry[7].name, 1);
	assert_int_equal(8, rwin.list_rows);
}

/* vim: set tabstop=2 softtabstop=2 shiftwidth=2 noexpandtab cinoptions-=(0 : */
/* vim: set cinoptions+=t0 filetype=c : */
