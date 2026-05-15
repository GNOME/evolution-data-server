/*
 * SPDX-FileCopyrightText: (C) 2013 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef CURSOR_SEARCH_H
#define CURSOR_SEARCH_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CURSOR_TYPE_SEARCH            (cursor_search_get_type())
#define CURSOR_SEARCH(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CURSOR_TYPE_SEARCH, CursorSearch))
#define CURSOR_SEARCH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CURSOR_TYPE_SEARCH, CursorSearchClass))
#define CURSOR_IS_SEARCH(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CURSOR_TYPE_SEARCH))
#define CURSOR_IS_SEARCH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CURSOR_TYPE_SEARCH))
#define CURSOR_SEARCH_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), CURSOR_SEARCH, CursorSearchClass))

typedef struct _CursorSearch         CursorSearch;
typedef struct _CursorSearchPrivate  CursorSearchPrivate;
typedef struct _CursorSearchClass    CursorSearchClass;

struct _CursorSearch
{
	GtkSearchEntry parent_instance;

	CursorSearchPrivate *priv;
};

struct _CursorSearchClass
{
	GtkSearchEntryClass parent_class;
};

GType        cursor_search_get_type  (void) G_GNUC_CONST;
GtkWidget   *cursor_search_new       (void);
const gchar *cursor_search_get_sexp  (CursorSearch *search);

G_END_DECLS

#endif /* CURSOR_SEARCH_H */
