/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1997-2002
 *	Sleepycat Software.  All rights reserved.
 */

#include "db_config.h"

#ifndef lint
static const gchar revid[] = "$Id$";
#endif /* not lint */

#ifndef NO_SYSTEM_INCLUDES
#include <sys/types.h>

#include <string.h>
#include <unistd.h>
#endif

#include "db_int.h"

/*
 * __os_region_unlink --
 *	Remove a shared memory object file.
 *
 * PUBLIC: gint __os_region_unlink __P((DB_ENV *, const gchar *));
 */
gint
__os_region_unlink(dbenv, path)
	DB_ENV *dbenv;
	const gchar *path;
{
#ifdef HAVE_QNX
	gint ret;
	gchar *newname;

	if ((ret = __os_shmname(dbenv, path, &newname)) != 0)
		goto err;

	if ((ret = shm_unlink(newname)) != 0) {
		ret = __os_get_errno();
		if (ret != ENOENT)
			__db_err(dbenv, "shm_unlink: %s: %s",
			    newname, strerror(ret));
	}
err:
	if (newname != NULL)
		__os_free(dbenv, newname);
	return (ret);
#else
	if (F_ISSET(dbenv, DB_ENV_OVERWRITE))
		(void)__db_overwrite(dbenv, path);

	return (__os_unlink(dbenv, path));
#endif
}

/*
 * __os_unlink --
 *	Remove a file.
 *
 * PUBLIC: gint __os_unlink __P((DB_ENV *, const gchar *));
 */
gint
__os_unlink(dbenv, path)
	DB_ENV *dbenv;
	const gchar *path;
{
	gint ret;

retry:	ret = DB_GLOBAL(j_unlink) != NULL ?
	    DB_GLOBAL(j_unlink)(path) :
#ifdef HAVE_VXWORKS
	    unlink((gchar *)path);
#else
	    unlink(path);
#endif
	if (ret == -1) {
		if ((ret = __os_get_errno()) == EINTR)
			goto retry;
		/*
		 * XXX
		 * We really shouldn't be looking at this value ourselves,
		 * but ENOENT usually signals that a file is missing, and
		 * we attempt to unlink things (such as v. 2.x environment
		 * regions, in DB_ENV->remove) that we're expecting not to
		 * be there.  Reporting errors in these cases is annoying.
		 */
#ifdef HAVE_VXWORKS
		/*
		 * XXX
		 * The results of unlink are file system driver specific
		 * on VxWorks.  In the case of removing a file that did
		 * not exist, some, at least, return an error, but with
		 * an errno of 0, not ENOENT.
		 *
		 * Code below falls through to original if-statement only
		 * we didn't get a "successful" error.
		 */
		if (ret != 0)
		/* FALLTHROUGH */
#endif
		if (ret != ENOENT)
			__db_err(dbenv, "unlink: %s: %s", path, strerror(ret));
	}

	return (ret);
}
