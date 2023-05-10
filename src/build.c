#include "build.h"

#include <stddef.h>
#include <string.h>

#include <fts.h>
#include <sys/stat.h>
#include <pthread.h>

#include "util.h"

static struct arraylist ext_files(char *dir, struct arraylist const *exts)
{
	unsigned long fts_opts = FTS_LOGICAL | FTS_COMFOLLOW | FTS_NOCHDIR;
	char *const fts_dirs[] = {dir, NULL};
	FTS *fts_p = fts_open(fts_dirs, fts_opts, NULL);

	if (!fts_p)
		log_fail("error on fts_open()");

	struct arraylist files = arraylist_create();

	if (fts_children(fts_p, 0))
		return files;

	FTSENT *fts_ent;
	while (fts_ent = fts_read(fts_p)) {
		if (fts_ent->fts_info != FTS_F)
			continue;

		for (size_t i = 0; i < exts->size; ++i) {
			if (!strcmp(exts->data[i], file_ext(fts_ent->fts_path))) {
				arraylist_add(&files, fts_ent->fts_path, fts_ent->fts_pathlen);
				break;
			}
		}
	}

	fts_close(fts_p);
	return files;
}

struct build_info build_info_get(struct conf const *conf)
{
	struct build_info info = {
		.srcs = ext_files(conf->proj.src_dir, &conf->proj.src_exts),
		.hdrs = ext_files(conf->proj.inc_dir, &conf->proj.hdr_exts),
		.objs = arraylist_create(),
	};

	for (size_t i = 0; i < info.srcs.size; ++i) {
	}

	return info;
}

void build_info_destroy(struct build_info *info)
{
	arraylist_destroy(&info->srcs);
	arraylist_destroy(&info->hdrs);
	arraylist_destroy(&info->objs);
}

void build_prune(struct build_info *info)
{
}

void build_compile(struct conf const *conf, struct build_info const *info)
{
}

void build_link(struct conf const *conf, struct build_info const *info)
{
}
