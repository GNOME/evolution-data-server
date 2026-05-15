/*
 * SPDX-FileCopyrightText: (C) 2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Srinivsa Ragavan <sragavan@novell.com>
 */

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <string.h>

#include "camel-local-private.h"

gint
camel_local_frompos_sort (gpointer enc,
                          gint len1,
                          gpointer data1,
                          gint len2,
                          gpointer data2)
{
	static gchar *sa1 = NULL, *sa2 = NULL;
	static gint l1 = 0, l2 = 0;
	gint a1, a2;

	if (l1 < len1 + 1) {
		sa1 = g_realloc (sa1, len1 + 1);
		l1 = len1 + 1;
	}
	if (l2 < len2 + 1) {
		sa2 = g_realloc (sa2, len2 + 1);
		l2 = len2 + 1;
	}

	if (sa1) {
		strncpy (sa1, data1, len1); sa1[len1] = 0;
		a1 = strtoul (sa1, NULL, 10);
	} else {
		a1 = 0;
	}

	if (sa2) {
		strncpy (sa2, data2, len2); sa2[len2] = 0;
		a2 = strtoul (sa2, NULL, 10);
	} else {
		a2 = 0;
	}

	return a1 - a2;
}
