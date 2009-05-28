/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1999-2002
 *	Sleepycat Software.  All rights reserved.
 */

#include "db_config.h"

#ifndef lint
static const gchar revid[] = "$Id$";
#endif /* not lint */

#ifndef NO_SYSTEM_INCLUDES
#include <sys/types.h>
#endif

#include "db_int.h"

/*
 * EXTERN: gint db_env_set_func_close __P((gint (*)(int)));
 */
gint
db_env_set_func_close(func_close)
	gint (*func_close) __P((int));
{
	DB_GLOBAL(j_close) = func_close;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_dirfree __P((void (*)(gchar **, int)));
 */
gint
db_env_set_func_dirfree(func_dirfree)
	void (*func_dirfree) __P((gchar **, int));
{
	DB_GLOBAL(j_dirfree) = func_dirfree;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_dirlist
 * EXTERN:     __P((gint (*)(const gchar *, gchar ***, gint *)));
 */
gint
db_env_set_func_dirlist(func_dirlist)
	gint (*func_dirlist) __P((const gchar *, gchar ***, gint *));
{
	DB_GLOBAL(j_dirlist) = func_dirlist;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_exists __P((gint (*)(const gchar *, gint *)));
 */
gint
db_env_set_func_exists(func_exists)
	gint (*func_exists) __P((const gchar *, gint *));
{
	DB_GLOBAL(j_exists) = func_exists;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_free __P((void (*)(gpointer)));
 */
gint
db_env_set_func_free(func_free)
	void (*func_free) __P((gpointer));
{
	DB_GLOBAL(j_free) = func_free;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_fsync __P((gint (*)(int)));
 */
gint
db_env_set_func_fsync(func_fsync)
	gint (*func_fsync) __P((int));
{
	DB_GLOBAL(j_fsync) = func_fsync;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_ioinfo __P((gint (*)(const gchar *,
 * EXTERN:     int, u_int32_t *, u_int32_t *, u_int32_t *)));
 */
gint
db_env_set_func_ioinfo(func_ioinfo)
	gint (*func_ioinfo)
	    __P((const gchar *, int, u_int32_t *, u_int32_t *, u_int32_t *));
{
	DB_GLOBAL(j_ioinfo) = func_ioinfo;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_malloc __P((gpointer (*)(size_t)));
 */
gint
db_env_set_func_malloc(func_malloc)
	gpointer (*func_malloc) __P((size_t));
{
	DB_GLOBAL(j_malloc) = func_malloc;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_map
 * EXTERN:     __P((gint (*)(gchar *, size_t, int, int, gpointer *)));
 */
gint
db_env_set_func_map(func_map)
	gint (*func_map) __P((gchar *, size_t, int, int, gpointer *));
{
	DB_GLOBAL(j_map) = func_map;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_open __P((gint (*)(const gchar *, int, ...)));
 */
gint
db_env_set_func_open(func_open)
	gint (*func_open) __P((const gchar *, int, ...));
{
	DB_GLOBAL(j_open) = func_open;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_read __P((ssize_t (*)(int, gpointer , size_t)));
 */
gint
db_env_set_func_read(func_read)
	ssize_t (*func_read) __P((int, gpointer , size_t));
{
	DB_GLOBAL(j_read) = func_read;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_realloc __P((gpointer (*)(gpointer , size_t)));
 */
gint
db_env_set_func_realloc(func_realloc)
	gpointer (*func_realloc) __P((gpointer , size_t));
{
	DB_GLOBAL(j_realloc) = func_realloc;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_rename
 * EXTERN:     __P((gint (*)(const gchar *, const gchar *)));
 */
gint
db_env_set_func_rename(func_rename)
	gint (*func_rename) __P((const gchar *, const gchar *));
{
	DB_GLOBAL(j_rename) = func_rename;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_seek
 * EXTERN:     __P((gint (*)(int, size_t, db_pgno_t, u_int32_t, int, int)));
 */
gint
db_env_set_func_seek(func_seek)
	gint (*func_seek) __P((int, size_t, db_pgno_t, u_int32_t, int, int));
{
	DB_GLOBAL(j_seek) = func_seek;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_sleep __P((gint (*)(u_long, u_long)));
 */
gint
db_env_set_func_sleep(func_sleep)
	gint (*func_sleep) __P((u_long, u_long));
{
	DB_GLOBAL(j_sleep) = func_sleep;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_unlink __P((gint (*)(const gchar *)));
 */
gint
db_env_set_func_unlink(func_unlink)
	gint (*func_unlink) __P((const gchar *));
{
	DB_GLOBAL(j_unlink) = func_unlink;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_unmap __P((gint (*)(gpointer , size_t)));
 */
gint
db_env_set_func_unmap(func_unmap)
	gint (*func_unmap) __P((gpointer , size_t));
{
	DB_GLOBAL(j_unmap) = func_unmap;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_write
 * EXTERN:     __P((ssize_t (*)(int, gconstpointer , size_t)));
 */
gint
db_env_set_func_write(func_write)
	ssize_t (*func_write) __P((int, gconstpointer , size_t));
{
	DB_GLOBAL(j_write) = func_write;
	return (0);
}

/*
 * EXTERN: gint db_env_set_func_yield __P((gint (*)(void)));
 */
gint
db_env_set_func_yield(func_yield)
	gint (*func_yield) __P((void));
{
	DB_GLOBAL(j_yield) = func_yield;
	return (0);
}
