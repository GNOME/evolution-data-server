/*
 * SPDX-FileCopyrightText: (C) 2013 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#ifndef CURSOR_NAVIGATOR_H
#define CURSOR_NAVIGATOR_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define CURSOR_TYPE_NAVIGATOR            (cursor_navigator_get_type())
#define CURSOR_NAVIGATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), CURSOR_TYPE_NAVIGATOR, CursorNavigator))
#define CURSOR_NAVIGATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), CURSOR_TYPE_NAVIGATOR, CursorNavigatorClass))
#define CURSOR_IS_NAVIGATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), CURSOR_TYPE_NAVIGATOR))
#define CURSOR_IS_NAVIGATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), CURSOR_TYPE_NAVIGATOR))
#define CURSOR_NAVIGATOR_GET_CLASS(o)    (G_TYPE_INSTANCE_GET_CLASS ((o), CURSOR_NAVIGATOR, CursorNavigatorClass))

typedef struct _CursorNavigator         CursorNavigator;
typedef struct _CursorNavigatorPrivate  CursorNavigatorPrivate;
typedef struct _CursorNavigatorClass    CursorNavigatorClass;

struct _CursorNavigator
{
	GtkScale parent_instance;

	CursorNavigatorPrivate *priv;
};

struct _CursorNavigatorClass
{
	GtkScaleClass parent_class;
};

GType                cursor_navigator_get_type        (void) G_GNUC_CONST;
CursorNavigator     *cursor_navigator_new             (void);
void                 cursor_navigator_set_alphabet    (CursorNavigator     *navigator,
						       const gchar * const *alphabet);
const gchar * const *cursor_navigator_get_alphabet    (CursorNavigator     *navigator);
void                 cursor_navigator_set_index       (CursorNavigator     *navigator,
						       gint                 index);
gint                 cursor_navigator_get_index       (CursorNavigator     *navigator);

G_END_DECLS

#endif /* CURSOR_NAVIGATOR_H */
