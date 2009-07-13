/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-list.h
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
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifndef _E_SOURCE_LIST_H_
#define _E_SOURCE_LIST_H_

#include <libxml/tree.h>
#include <gconf/gconf-client.h>

#include "e-source-group.h"

G_BEGIN_DECLS

#define E_TYPE_SOURCE_LIST			(e_source_list_get_type ())
#define E_SOURCE_LIST(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_SOURCE_LIST, ESourceList))
#define E_SOURCE_LIST_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_SOURCE_LIST, ESourceListClass))
#define E_IS_SOURCE_LIST(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_SOURCE_LIST))
#define E_IS_SOURCE_LIST_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_SOURCE_LIST))

typedef struct _ESourceList        ESourceList;
typedef struct _ESourceListPrivate ESourceListPrivate;
typedef struct _ESourceListClass   ESourceListClass;

struct _ESourceList {
	GObject parent;

	ESourceListPrivate *priv;
};

struct _ESourceListClass {
	GObjectClass parent_class;

	/* Signals.  */

	void (* changed) (ESourceList *source_list);

	void (* group_removed) (ESourceList *source_list, ESourceGroup *group);
	void (* group_added) (ESourceList *source_list, ESourceGroup *group);
};

GType    e_source_list_get_type (void);

ESourceList *e_source_list_new            (void);
ESourceList *e_source_list_new_for_gconf  (GConfClient *client,
					   const gchar  *path);
ESourceList *e_source_list_new_for_gconf_default  (const gchar  *path);

GSList       *e_source_list_peek_groups         (ESourceList *list);
ESourceGroup *e_source_list_peek_group_by_uid   (ESourceList *list,
						 const gchar  *uid);
#ifndef EDS_DISABLE_DEPRECATED
ESourceGroup *e_source_list_peek_group_by_name  (ESourceList *list,
						 const gchar *name);
#endif
ESourceGroup *e_source_list_peek_group_by_base_uri (ESourceList *list, const gchar *base_uri);
ESourceGroup *e_source_list_peek_group_by_properties (ESourceList *list, const gchar *property_name, ...);

ESource      *e_source_list_peek_source_by_uid  (ESourceList *list,
						 const gchar  *uid);
ESource      *e_source_list_peek_source_any     (ESourceList *list);

gboolean  e_source_list_add_group             (ESourceList  *list,
					       ESourceGroup *group,
					       gint           position);
gboolean  e_source_list_remove_group          (ESourceList  *list,
					       ESourceGroup *group);
gboolean  e_source_list_remove_group_by_uid   (ESourceList  *list,
					       const gchar   *uid);

ESourceGroup *e_source_list_ensure_group (ESourceList *list, const gchar *name, const gchar *base_uri, gboolean ret_it);
gboolean e_source_list_remove_group_by_base_uri (ESourceList *list, const gchar *base_uri);

gboolean  e_source_list_remove_source_by_uid  (ESourceList  *list,
					       const gchar   *uidj);

gboolean  e_source_list_sync  (ESourceList  *list,
			       GError      **error);

gboolean e_source_list_is_gconf_updated (ESourceList *list);

G_END_DECLS

#endif /* _E_SOURCE_LIST_H_ */
