/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-combo-box.h
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef _E_SOURCE_COMBO_BOX_H_
#define _E_SOURCE_COMBO_BOX_H_

#include <gtk/gtk.h>
#include <libedataserver/e-source-list.h>

#define E_TYPE_SOURCE_COMBO_BOX \
	(e_source_combo_box_get_type ())
#define E_SOURCE_COMBO_BOX(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_COMBO_BOX, ESourceComboBox))
#define E_SOURCE_COMBO_BOX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_COMBO_BOX, ESourceComboBoxClass))
#define E_IS_SOURCE_COMBO_BOX(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_SOURCE_COMBO_BOX))
#define E_IS_SOURCE_COMBO_BOX_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE ((cls), E_TYPE_SOURCE_COMBO_BOX))
#define E_SOURCE_COMBO_BOX_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_COMBO_BOX, ESourceComboBox))

G_BEGIN_DECLS

typedef struct _ESourceComboBox ESourceComboBox;
typedef struct _ESourceComboBoxClass ESourceComboBoxClass;
typedef struct _ESourceComboBoxPrivate ESourceComboBoxPrivate;

/**
 * ESourceComboBox:
 *
 * Since: 2.22
 **/
struct _ESourceComboBox {
	GtkComboBox parent;

	ESourceComboBoxPrivate *priv;
};

struct _ESourceComboBoxClass {
	GtkComboBoxClass parent_class;
};

GType		e_source_combo_box_get_type	(void);
GtkWidget *	e_source_combo_box_new		(ESourceList *source_list);
ESourceList *	e_source_combo_box_get_source_list
						(ESourceComboBox *source_combo_box);
void		e_source_combo_box_set_source_list
						(ESourceComboBox *source_combo_box,
						 ESourceList *source_list);
ESource *	e_source_combo_box_get_active
						(ESourceComboBox *source_combo_box);
void		e_source_combo_box_set_active
						(ESourceComboBox *source_combo_box,
						 ESource *source);
const gchar *	e_source_combo_box_get_active_uid
						(ESourceComboBox *source_combo_box);
void		e_source_combo_box_set_active_uid
						(ESourceComboBox *source_combo_box,
						 const gchar *uid);

G_END_DECLS

#endif /* _E_SOURCE_COMBO_BOX_H_ */
