/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source.h
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

#ifndef _E_SOURCE_H_
#define _E_SOURCE_H_

#include <glib-object.h>
#include <libxml/tree.h>

G_BEGIN_DECLS

#define E_TYPE_SOURCE			(e_source_get_type ())
#define E_SOURCE(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_SOURCE, ESource))
#define E_SOURCE_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_SOURCE, ESourceClass))
#define E_IS_SOURCE(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_SOURCE))
#define E_IS_SOURCE_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((obj), E_TYPE_SOURCE))

typedef struct _ESource        ESource;
typedef struct _ESourcePrivate ESourcePrivate;
typedef struct _ESourceClass   ESourceClass;

#include "e-source-group.h"

struct _ESource {
	GObject parent;

	ESourcePrivate *priv;
};

struct _ESourceClass {
	GObjectClass parent_class;

	/* Signals.  */
	void (* changed) (ESource *source);
};

GType    e_source_get_type (void);

ESource *e_source_new                (const gchar   *name,
				      const gchar   *relative_uri);
ESource *e_source_new_with_absolute_uri(const gchar   *name,
					const gchar   *absolute_uri);
ESource *e_source_new_from_xml_node  (xmlNodePtr    node);
ESource *e_source_new_from_standalone_xml (const gchar *xml);

ESource *e_source_copy (ESource *source);

gboolean  e_source_update_from_xml_node  (ESource    *source,
					  xmlNodePtr  node,
					  gboolean   *changed_return);

gchar *e_source_uid_from_xml_node  (xmlNodePtr node);

void  e_source_set_group         (ESource      *source,
				  ESourceGroup *group);
void  e_source_set_name          (ESource      *source,
				  const gchar   *name);
void  e_source_set_relative_uri  (ESource      *source,
				  const gchar   *relative_uri);
void  e_source_set_absolute_uri  (ESource      *source,
				  const gchar   *absolute_uri);
void  e_source_set_color_spec    (ESource      *source,
				  const gchar  *color_spec);
void  e_source_set_readonly      (ESource      *source,
				  gboolean      readonly);
#ifndef EDS_DISABLE_DEPRECATED
void  e_source_set_color         (ESource      *source,
				  guint32       color);
void  e_source_unset_color       (ESource      *source);
#endif

ESourceGroup *e_source_peek_group         (ESource *source);
const gchar   *e_source_peek_uid           (ESource *source);
const gchar   *e_source_peek_name          (ESource *source);
const gchar   *e_source_peek_relative_uri  (ESource *source);
const gchar   *e_source_peek_absolute_uri  (ESource *source);
const gchar   *e_source_peek_color_spec    (ESource *source);
gboolean      e_source_get_readonly       (ESource *source);
#ifndef EDS_DISABLE_DEPRECATED
gboolean      e_source_get_color          (ESource *source,
					   guint32 *color_return);
#endif

gchar *e_source_get_uri  (ESource *source);

void  e_source_dump_to_xml_node  (ESource    *source,
				  xmlNodePtr  parent_node);
gchar *e_source_to_standalone_xml (ESource *source);

const gchar *e_source_get_property     (ESource *source,
					const gchar *property);
void         e_source_set_property     (ESource *source,
					const gchar *property,
					const gchar *value);
void         e_source_foreach_property (ESource *source,
					GHFunc func,
					gpointer data);

gchar *e_source_get_duped_property (ESource *source, const gchar *property);
gchar *e_source_build_absolute_uri (ESource *source);

gboolean e_source_equal (ESource *a, ESource *b);
gboolean e_source_xmlstr_equal (const gchar *a, const gchar *b);

G_END_DECLS

#endif /* _E_SOURCE_H_ */
