/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1996-2002
 *	Sleepycat Software.  All rights reserved.
 */

#include "db_config.h"

#ifndef lint
static const gchar revid[] = "$Id$";
#endif /* not lint */

#ifndef NO_SYSTEM_INCLUDES
#include <sys/types.h>

#include <stdio.h>
#endif

#include "db_int.h"

/*
 * snprintf --
 *	Bounded version of sprintf.
 *
 * PUBLIC: #ifndef HAVE_SNPRINTF
 * PUBLIC: gint snprintf __P((gchar *, size_t, const gchar *, ...));
 * PUBLIC: #endif
 */
#ifndef HAVE_SNPRINTF
gint
#ifdef __STDC__
snprintf(gchar *str, size_t n, const gchar *fmt, ...)
#else
snprintf(str, n, fmt, va_alist)
	gchar *str;
	size_t n;
	const gchar *fmt;
	va_dcl
#endif
{
	static gint ret_charpnt = -1;
	va_list ap;
	gint len;

	COMPQUIET(n, 0);

	/*
	 * Some old versions of sprintf return a pointer to the first argument
	 * instead of a character count.  Assume the return value of snprintf,
	 * vsprintf, etc. will be the same as sprintf, and check the easy one.
	 *
	 * We do this test at run-time because it's not a test we can do in a
	 * cross-compilation environment.
	 */
	if (ret_charpnt == -1) {
		gchar buf[10];

		ret_charpnt =
		    sprintf(buf, "123") != 3 ||
		    sprintf(buf, "123456789") != 9 ||
		    sprintf(buf, "1234") != 4;
	}

#ifdef __STDC__
	va_start(ap, fmt);
#else
	va_start(ap);
#endif
	len = vsprintf(str, fmt, ap);
	va_end(ap);
	return (ret_charpnt ? (int)strlen(str) : len);
}
#endif
