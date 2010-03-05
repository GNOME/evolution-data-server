/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-selector.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifndef E_SOURCE_SELECTOR_H
#define E_SOURCE_SELECTOR_H

#include <gtk/gtk.h>
#include "libedataserver/e-source-list.h"

/* Standard GObject macros */
#define E_TYPE_SOURCE_SELECTOR \
	(e_source_selector_get_type ())
#define E_SOURCE_SELECTOR(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_SELECTOR, ESourceSelector))
#define E_SOURCE_SELECTOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_SELECTOR, ESourceSelectorClass))
#define E_IS_SOURCE_SELECTOR(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_SELECTOR))
#define E_IS_SOURCE_SELECTOR_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_SELECTOR))
#define E_SOURCE_SELECTOR_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_SELECTOR, ESourceSelectorClass))

G_BEGIN_DECLS

typedef struct _ESourceSelector ESourceSelector;
typedef struct _ESourceSelectorClass ESourceSelectorClass;
typedef struct _ESourceSelectorPrivate ESourceSelectorPrivate;

struct _ESourceSelector {
	GtkTreeView parent;
	ESourceSelectorPrivate *priv;
};

struct _ESourceSelectorClass {
	GtkTreeViewClass parent_class;

	void		(*selection_changed)	(ESourceSelector *selector);
	void		(*primary_selection_changed)
						(ESourceSelector *selector);
	gboolean	(*popup_event)		(ESourceSelector *selector,
						 ESource *primary,
						 GdkEventButton *event);
	gboolean	(*data_dropped)		(ESourceSelector *selector,
						 GtkSelectionData *data,
						 ESource *destination,
						 GdkDragAction action,
						 guint target_info);

	gpointer padding1;
	gpointer padding2;
	gpointer padding3;
};

GType		e_source_selector_get_type	(void);
GtkWidget *	e_source_selector_new		(ESourceList *list);
ESourceList *	e_source_selector_get_source_list
						(ESourceSelector *selector);
void		e_source_selector_select_source	(ESourceSelector *selector,
						 ESource *source);
void		e_source_selector_unselect_source
						(ESourceSelector *selector,
						 ESource *source);
void		e_source_selector_select_exclusive
						(ESourceSelector *selector,
						 ESource *source);
gboolean	e_source_selector_source_is_selected
						(ESourceSelector *selector,
						 ESource *source);
GSList *	e_source_selector_get_selection	(ESourceSelector *selector);
void		e_source_selector_free_selection(GSList *list);
void		e_source_selector_show_selection(ESourceSelector *selector,
						 gboolean show);
gboolean	e_source_selector_selection_shown
						(ESourceSelector *selector);
void		e_source_selector_set_select_new(ESourceSelector *selector,
						 gboolean state);
void		e_source_selector_edit_primary_selection
						(ESourceSelector *selector);
ESource *	e_source_selector_peek_primary_selection
						(ESourceSelector *selector);
void		e_source_selector_set_primary_selection
						(ESourceSelector *selector,
						 ESource *source);
ESourceGroup *	e_source_selector_get_primary_source_group
						(ESourceSelector *selector);

G_END_DECLS

#endif /* E_SOURCE_SELECTOR_H */
