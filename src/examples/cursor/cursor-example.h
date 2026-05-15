/*
 * SPDX-FileCopyrightText: (C) 2013 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef CURSOR_EXAMPLE_H
#define CURSOR_EXAMPLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CURSOR_TYPE_EXAMPLE            (cursor_example_get_type())
#define CURSOR_EXAMPLE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CURSOR_TYPE_EXAMPLE, CursorExample))
#define CURSOR_EXAMPLE_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CURSOR_TYPE_EXAMPLE, CursorExampleClass))
#define CURSOR_IS_EXAMPLE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CURSOR_TYPE_EXAMPLE))
#define CURSOR_IS_EXAMPLE_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CURSOR_TYPE_EXAMPLE))
#define CURSOR_EXAMPLE_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), CURSOR_EXAMPLE, CursorExampleClass))

typedef struct _CursorExample         CursorExample;
typedef struct _CursorExamplePrivate  CursorExamplePrivate;
typedef struct _CursorExampleClass    CursorExampleClass;

struct _CursorExample
{
	GtkWindow parent_instance;

	CursorExamplePrivate *priv;
};

struct _CursorExampleClass
{
	GtkWindowClass parent_class;
};

GType       cursor_example_get_type (void) G_GNUC_CONST;
GtkWidget  *cursor_example_new      (const gchar   *vcard_path);

G_END_DECLS

#endif /* CURSOR_EXAMPLE_H */
