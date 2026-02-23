/* aiori-LOCUSTA.c -- IOR AIORI backend for locusta */

#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <locustaclient.h>

#include "ior.h"
#include "aiori.h"

static aiori_xfer_hint_t *hints = NULL;

/* locusta は fd レスなのでパスだけ保持 */
struct Locusta_File {
	char *path;
};

struct locusta_option {
	char *runtime_dir; /* カンマ区切りで複数ディレクトリ指定 */
};

static option_help *
Locusta_options(aiori_mod_opt_t **init_backend_options,
	aiori_mod_opt_t *init_values)
{
	struct locusta_option *o = malloc(sizeof(*o));

	if (init_values != NULL)
		memcpy(o, init_values, sizeof(*o));
	else
		memset(o, 0, sizeof(*o));

	*init_backend_options = (aiori_mod_opt_t *)o;

	option_help h[] = {
	    {0, "locusta.runtime_dir",
		"comma-separated runtime directories", OPTION_OPTIONAL_ARGUMENT,
		's', &o->runtime_dir},
	    LAST_OPTION
	};
	option_help *help = malloc(sizeof(h));
	memcpy(help, h, sizeof(h));

	return (help);
}

static void
Locusta_xfer_hints(aiori_xfer_hint_t *params)
{
	hints = params;
}

static void
Locusta_initialize(aiori_mod_opt_t *options)
{
	struct locusta_option *o = (struct locusta_option *)options;
	const char *dir_str = NULL;

	/* オプションから取得、なければ環境変数 */
	if (o != NULL && o->runtime_dir != NULL)
		dir_str = o->runtime_dir;
	else
		dir_str = getenv("LOCUSTA_RUNTIME_DIRS");

	if (dir_str == NULL || dir_str[0] == '\0') {
		ERR("locusta: runtime_dir not specified. "
		    "Use --locusta.runtime_dir or LOCUSTA_RUNTIME_DIRS env");
	}

	/* カンマ区切りをパース */
	char *buf = strdup(dir_str);
	const char **dirs = NULL;
	size_t num_dirs = 0;
	size_t cap = 8;

	dirs = malloc(sizeof(char *) * cap);
	char *tok = strtok(buf, ",");
	while (tok != NULL) {
		if (num_dirs >= cap) {
			cap *= 2;
			dirs = realloc(dirs, sizeof(char *) * cap);
		}
		dirs[num_dirs++] = tok;
		tok = strtok(NULL, ",");
	}

	if (locusta_init(dirs, num_dirs) != 0) {
		free(dirs);
		free(buf);
		ERR("locusta_init failed");
	}

	free(dirs);
	free(buf);
}

static void
Locusta_finalize(aiori_mod_opt_t *options)
{
	locusta_term();
}

static aiori_fd_t *
Locusta_create(char *fn, int flags, aiori_mod_opt_t *param)
{
	struct Locusta_File *lf;

	if (hints->dryRun)
		return (NULL);

	if (locusta_create(fn, 0100644) != 0)
		ERR("locusta_create failed");

	lf = malloc(sizeof(*lf));
	lf->path = strdup(fn);

	return ((aiori_fd_t *)lf);
}

static aiori_fd_t *
Locusta_open(char *fn, int flags, aiori_mod_opt_t *param)
{
	struct Locusta_File *lf;

	if (hints->dryRun)
		return (NULL);

	/* locusta は明示的 open 不要、パスだけ保持 */
	lf = malloc(sizeof(*lf));
	lf->path = strdup(fn);

	return ((aiori_fd_t *)lf);
}

static IOR_offset_t
Locusta_xfer(int access, aiori_fd_t *fd, IOR_size_t *buffer,
	IOR_offset_t len, IOR_offset_t offset, aiori_mod_opt_t *param)
{
	struct Locusta_File *lf = (struct Locusta_File *)fd;
	ssize_t r;

	if (hints->dryRun)
		return (len);

	switch (access) {
	case WRITE:
		r = locusta_pwrite(lf->path, buffer, len, offset);
		break;
	default:
		r = locusta_pread(lf->path, buffer, len, offset);
		if (r != (ssize_t)len) {
			fprintf(stderr,
			    "[LOCUSTA] pread: path=%s off=%lld len=%lld r=%zd\n",
			    lf->path, (long long)offset, (long long)len, r);
		}
		break;
	}

	return (r);
}

static void
Locusta_close(aiori_fd_t *fd, aiori_mod_opt_t *param)
{
	struct Locusta_File *lf = (struct Locusta_File *)fd;

	if (hints->dryRun)
		return;

	locusta_fsync();
	free(lf->path);
	free(lf);
}

static void
Locusta_delete(char *fn, aiori_mod_opt_t *param)
{
	if (hints->dryRun)
		return;

	locusta_unlink(fn);
}

static char *
Locusta_version(void)
{
	return ((char *)"locusta 0.1.0");
}

static void
Locusta_fsync(aiori_fd_t *fd, aiori_mod_opt_t *param)
{
	locusta_fsync();
}

static IOR_offset_t
Locusta_get_file_size(aiori_mod_opt_t *param, char *fn)
{
	uint32_t mode;
	uint64_t size;

	if (hints->dryRun)
		return (0);

	if (locusta_stat(fn, &mode, &size) != 0)
		return (-1);

	return ((IOR_offset_t)size);
}

static int
Locusta_statfs(const char *fn, ior_aiori_statfs_t *st,
	aiori_mod_opt_t *param)
{
	/* スタブ: locusta は statfs 未対応 */
	if (st != NULL)
		memset(st, 0, sizeof(*st));
	return (0);
}

static int
Locusta_mkdir(const char *fn, mode_t mode, aiori_mod_opt_t *param)
{
	if (hints->dryRun)
		return (0);

	return (locusta_mkdir(fn, (uint32_t)mode));
}

static int
Locusta_rmdir(const char *fn, aiori_mod_opt_t *param)
{
	if (hints->dryRun)
		return (0);

	return (locusta_rmdir(fn));
}

static int
Locusta_access(const char *fn, int mode, aiori_mod_opt_t *param)
{
	uint32_t fmode;
	uint64_t size;

	if (hints->dryRun)
		return (0);

	/* stat で代替 */
	return (locusta_stat(fn, &fmode, &size));
}

static int
Locusta_stat(const char *fn, struct stat *buf, aiori_mod_opt_t *param)
{
	uint32_t mode;
	uint64_t size;

	if (hints->dryRun)
		return (0);

	if (locusta_stat(fn, &mode, &size) != 0)
		return (-1);

	if (buf != NULL) {
		memset(buf, 0, sizeof(*buf));
		buf->st_mode = (mode_t)mode;
		buf->st_size = (off_t)size;
	}

	return (0);
}

static void
Locusta_sync(aiori_mod_opt_t *param)
{
	/* locusta は同期書き込みのため no-op */
}

ior_aiori_t locusta_aiori = {
	.name = "LOCUSTA",
	.name_legacy = NULL,
	.create = Locusta_create,
	.open = Locusta_open,
	.xfer_hints = Locusta_xfer_hints,
	.xfer = Locusta_xfer,
	.close = Locusta_close,
	.remove = Locusta_delete,
	.get_version = Locusta_version,
	.fsync = Locusta_fsync,
	.get_file_size = Locusta_get_file_size,
	.statfs = Locusta_statfs,
	.mkdir = Locusta_mkdir,
	.rmdir = Locusta_rmdir,
	.access = Locusta_access,
	.stat = Locusta_stat,
	.initialize = Locusta_initialize,
	.finalize = Locusta_finalize,
	.get_options = Locusta_options,
	.sync = Locusta_sync,
	.enable_mdtest = true,
};
