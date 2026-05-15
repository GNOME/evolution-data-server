/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#include "e-uid.h"

#include "e-data-server-util.h"

/**
 * e_uid_new:
 *
 * Generate a new unique string for use e.g. in account lists.
 *
 * Returns: The newly generated UID.  The caller should free the string
 * when it's done with it.
 *
 * Deprecated: 3.26: Use e_util_generate_uid() instead.
 **/
gchar *
e_uid_new (void)
{
	return e_util_generate_uid ();
}
