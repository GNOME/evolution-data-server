/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-group.h
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

#ifndef _E_SOURCE_GROUP_H_
#define _E_SOURCE_GROUP_H_

#include <glib-object.h>
#include <libxml/tree.h>

G_BEGIN_DECLS

#define E_TYPE_SOURCE_GROUP			(e_source_group_get_type ())
#define E_SOURCE_GROUP(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_SOURCE_GROUP, ESourceGroup))
#define E_SOURCE_GROUP_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_SOURCE_GROUP, ESourceGroupClass))
#define E_IS_SOURCE_GROUP(obj)			(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_SOURCE_GROUP))
#define E_IS_SOURCE_GROUP_CLASS(klass)		(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_SOURCE_GROUP))

typedef struct _ESourceGroup        ESourceGroup;
typedef struct _ESourceGroupPrivate ESourceGroupPrivate;
typedef struct _ESourceGroupClass   ESourceGroupClass;

#include "e-source.h"

struct _ESourceGroup {
	GObject parent;

	ESourceGroupPrivate *priv;
};

struct _ESourceGroupClass {
	GObjectClass parent_class;

	/* Signals.  */

	void (* changed) (ESourceGroup *group);

	void (* source_removed) (ESourceGroup *source_list, ESource *source);
	void (* source_added)   (ESourceGroup *source_list, ESource *source);
};

GType    e_source_group_get_type (void);

ESourceGroup *e_source_group_new              (const gchar *name,
					       const gchar *base_uri);
ESourceGroup *e_source_group_new_from_xml     (const gchar *xml);
ESourceGroup *e_source_group_new_from_xmldoc  (xmlDocPtr   doc);

gboolean  e_source_group_update_from_xml     (ESourceGroup *group,
					      const gchar   *xml,
					      gboolean     *changed_return);
gboolean  e_source_group_update_from_xmldoc  (ESourceGroup *group,
					      xmlDocPtr     doc,
					      gboolean     *changed_return);

gchar *e_source_group_uid_from_xmldoc  (xmlDocPtr doc);

void  e_source_group_set_name      (ESourceGroup *group,
				    const gchar   *name);
void  e_source_group_set_base_uri  (ESourceGroup *group,
				    const gchar   *base_uri);

void e_source_group_set_readonly (ESourceGroup *group,
				  gboolean      readonly);

const gchar *e_source_group_peek_uid       (ESourceGroup *group);
const gchar *e_source_group_peek_name      (ESourceGroup *group);
const gchar *e_source_group_peek_base_uri  (ESourceGroup *group);
gboolean    e_source_group_get_readonly   (ESourceGroup *group);

GSList  *e_source_group_peek_sources        (ESourceGroup *group);
ESource *e_source_group_peek_source_by_uid  (ESourceGroup *group,
					     const gchar   *source_uid);
ESource *e_source_group_peek_source_by_name (ESourceGroup *group,
					     const gchar   *source_name);

gboolean  e_source_group_add_source            (ESourceGroup *group,
						ESource      *source,
						gint           position);
gboolean  e_source_group_remove_source         (ESourceGroup *group,
						ESource      *source);
gboolean  e_source_group_remove_source_by_uid  (ESourceGroup *group,
						const gchar   *uid);

gchar *e_source_group_get_property     (ESourceGroup *source,
					      const gchar *property);
void         e_source_group_set_property     (ESourceGroup *source,
					      const gchar *property,
					      const gchar *value);
void         e_source_group_foreach_property (ESourceGroup *source,
					      GHFunc func,
					      gpointer data);

gchar *e_source_group_to_xml (ESourceGroup *group);

gboolean e_source_group_equal (ESourceGroup *a, ESourceGroup *b);
gboolean e_source_group_xmlstr_equal (const gchar *a, const gchar *b);

G_END_DECLS

#endif /* _E_SOURCE_GROUP_H_ */
