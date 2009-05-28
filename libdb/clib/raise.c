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
#include <signal.h>
#include <unistd.h>
#endif

/*
 * raise --
 *	Send a signal to the current process.
 *
 * PUBLIC: #ifndef HAVE_RAISE
 * PUBLIC: gint raise __P((int));
 * PUBLIC: #endif
 */
gint
raise(s)
	gint s;
{
	/*
	 * Do not use __os_id(), as it may not return the process ID -- any
	 * system with kill(3) probably has getpid(3).
	 */
	return (kill(getpid(), s));
}
