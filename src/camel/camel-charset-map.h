/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_CHARSET_MAP_H
#define CAMEL_CHARSET_MAP_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _CamelCharset CamelCharset;

struct _CamelCharset {
	guint mask;
	gint level;
};

void camel_charset_init (CamelCharset *c);
void camel_charset_step (CamelCharset *cc, const gchar *in, gint len);

const gchar *camel_charset_best_name (CamelCharset *charset);

/* helper function */
const gchar *camel_charset_best (const gchar *in, gint len);

const gchar *camel_charset_iso_to_windows (const gchar *isocharset);

G_END_DECLS

#endif /* CAMEL_CHARSET_MAP_H */
