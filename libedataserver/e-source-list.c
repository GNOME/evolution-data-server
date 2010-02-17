/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* e-source-list.c
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include "e-source-list.h"

struct _ESourceListPrivate {
	GConfClient *gconf_client;
	gchar *gconf_path;

	gint gconf_notify_id;

	GSList *groups;

	gboolean ignore_group_changed;
	gint sync_idle_id;
};

/* Signals.  */

enum {
	CHANGED,
	GROUP_REMOVED,
	GROUP_ADDED,
	LAST_SIGNAL
};
static guint signals[LAST_SIGNAL] = { 0 };

/* Forward declarations.  */

static gboolean  sync_idle_callback      (ESourceList  *list);
static void      group_changed_callback  (ESourceGroup *group,
					  ESourceList  *list);
static void      conf_changed_callback   (GConfClient  *client,
					  guint  connection_id,
					  GConfEntry   *entry,
					  ESourceList  *list);

/* Utility functions.  */

static void
load_from_gconf (ESourceList *list)
{
	GSList *conf_list, *p, *q;
	GSList *new_groups_list;
	GHashTable *new_groups_hash;
	gboolean changed = FALSE;
	gint pos;

	conf_list = gconf_client_get_list (list->priv->gconf_client,
					   list->priv->gconf_path,
					   GCONF_VALUE_STRING, NULL);

	new_groups_list = NULL;
	new_groups_hash = g_hash_table_new (g_direct_hash, g_direct_equal);

	for (p = conf_list, pos = 0; p != NULL; p = p->next, pos++) {
		const xmlChar *xml = p->data;
		xmlDocPtr xmldoc;
		gchar *group_uid;
		ESourceGroup *existing_group;

		xmldoc = xmlParseDoc (xml);
		if (xmldoc == NULL)
			continue;

		group_uid = e_source_group_uid_from_xmldoc (xmldoc);
		if (group_uid == NULL) {
			xmlFreeDoc (xmldoc);
			continue;
		}

		existing_group = e_source_list_peek_group_by_uid (list, group_uid);
		if (g_hash_table_lookup (new_groups_hash, existing_group) != NULL) {
			xmlFreeDoc (xmldoc);
			g_free (group_uid);
			continue;
		}

		if (existing_group == NULL) {
			ESourceGroup *new_group = e_source_group_new_from_xmldoc (xmldoc);

			if (new_group != NULL) {
				g_signal_connect (new_group, "changed", G_CALLBACK (group_changed_callback), list);
				new_groups_list = g_slist_prepend (new_groups_list, new_group);

				g_hash_table_insert (new_groups_hash, new_group, new_group);
				g_signal_emit (list, signals[GROUP_ADDED], 0, new_group);
				changed = TRUE;
			}
		} else {
			gboolean group_changed;

			list->priv->ignore_group_changed ++;

			if (e_source_group_update_from_xmldoc (existing_group, xmldoc, &group_changed)) {
				new_groups_list = g_slist_prepend (new_groups_list, existing_group);
				g_object_ref (existing_group);
				g_hash_table_insert (new_groups_hash, existing_group, existing_group);

				if (group_changed)
					changed = TRUE;
			}

			list->priv->ignore_group_changed --;
		}

		xmlFreeDoc (xmldoc);
		g_free (group_uid);
	}

	new_groups_list = g_slist_reverse (new_groups_list);

	g_slist_foreach (conf_list, (GFunc) g_free, NULL);
	g_slist_free (conf_list);

	/* Emit "group_removed" and disconnect the "changed" signal for all the
	   groups that we haven't found in the new list.  Also, check if the
	   order has changed.  */
	q = new_groups_list;
	for (p = list->priv->groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);

		if (g_hash_table_lookup (new_groups_hash, group) == NULL) {
			changed = TRUE;
			g_signal_emit (list, signals[GROUP_REMOVED], 0, group);
			g_signal_handlers_disconnect_by_func (group, group_changed_callback, list);
		}

		if (!changed && q != NULL) {
			if (q->data != p->data)
				changed = TRUE;
			q = q->next;
		}
	}

	g_hash_table_destroy (new_groups_hash);

	/* Replace the original group list with the new one.  */

	g_slist_foreach (list->priv->groups, (GFunc) g_object_unref, NULL);
	g_slist_free (list->priv->groups);

	list->priv->groups = new_groups_list;

	/* FIXME if the order changes, the function doesn't notice.  */

	if (changed)
		g_signal_emit (list, signals[CHANGED], 0);
}

static void
remove_group (ESourceList *list,
	      ESourceGroup *group)
{
	list->priv->groups = g_slist_remove (list->priv->groups, group);

	g_signal_emit (list, signals[GROUP_REMOVED], 0, group);
	g_object_unref (group);

	g_signal_emit (list, signals[CHANGED], 0);
}

/* Callbacks.  */

static gboolean
sync_idle_callback (ESourceList *list)
{
	GError *error = NULL;

	if (!e_source_list_sync (list, &error)) {
		g_warning ("Cannot update \"%s\": %s", list->priv->gconf_path, error ? error->message : "Unknown error");
		g_error_free (error);
	}

	list->priv->sync_idle_id= 0;

	return FALSE;
}

static void
group_changed_callback (ESourceGroup *group,
			ESourceList *list)
{
	if (!list->priv->ignore_group_changed)
		g_signal_emit (list, signals[CHANGED], 0);

	if (list->priv->sync_idle_id == 0)
		list->priv->sync_idle_id = g_idle_add ((GSourceFunc) sync_idle_callback, list);
}

static void
conf_changed_callback (GConfClient *client,
		       guint connection_id,
		       GConfEntry *entry,
		       ESourceList *list)
{
	load_from_gconf (list);
}

/* GObject methods.  */

G_DEFINE_TYPE (ESourceList, e_source_list, G_TYPE_OBJECT)

static void
impl_dispose (GObject *object)
{
	ESourceListPrivate *priv = E_SOURCE_LIST (object)->priv;

	if (priv->sync_idle_id != 0) {
		GError *error = NULL;

		g_source_remove (priv->sync_idle_id);
		priv->sync_idle_id = 0;

		if (!e_source_list_sync (E_SOURCE_LIST (object), &error))
			g_warning ("Could not update \"%s\": %s",
				   priv->gconf_path, error->message);
	}

	if (priv->groups != NULL) {
		GSList *p;

		for (p = priv->groups; p != NULL; p = p->next)
			g_object_unref (p->data);

		g_slist_free (priv->groups);
		priv->groups = NULL;
	}

	if (priv->gconf_client != NULL) {
		if (priv->gconf_notify_id != 0) {
			gconf_client_notify_remove (priv->gconf_client,
						    priv->gconf_notify_id);
			priv->gconf_notify_id = 0;
		}

		g_object_unref (priv->gconf_client);
		priv->gconf_client = NULL;
	} else {
		g_assert_not_reached ();
	}

	(* G_OBJECT_CLASS (e_source_list_parent_class)->dispose) (object);
}

static void
impl_finalize (GObject *object)
{
	ESourceListPrivate *priv = E_SOURCE_LIST (object)->priv;

	if (priv->gconf_notify_id != 0) {
		gconf_client_notify_remove (priv->gconf_client,
					    priv->gconf_notify_id);
		priv->gconf_notify_id = 0;
	}

	g_free (priv->gconf_path);
	g_free (priv);

	(* G_OBJECT_CLASS (e_source_list_parent_class)->finalize) (object);
}

/* Initialization.  */

static void
e_source_list_class_init (ESourceListClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);

	object_class->dispose  = impl_dispose;
	object_class->finalize = impl_finalize;

	signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceListClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);

	signals[GROUP_REMOVED] =
		g_signal_new ("group_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceListClass, group_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);

	signals[GROUP_ADDED] =
		g_signal_new ("group_added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (ESourceListClass, group_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE, 1,
			      G_TYPE_POINTER);
}

static void
e_source_list_init (ESourceList *source_list)
{
	ESourceListPrivate *priv;

	priv = g_new0 (ESourceListPrivate, 1);

	source_list->priv = priv;
}

/* Public methods.  */

ESourceList *
e_source_list_new (void)
{
	ESourceList *list = g_object_new (e_source_list_get_type (), NULL);

	return list;
}

ESourceList *
e_source_list_new_for_gconf (GConfClient *client,
			     const gchar *path)
{
	ESourceList *list;

	g_return_val_if_fail (GCONF_IS_CLIENT (client), NULL);
	g_return_val_if_fail (path != NULL, NULL);

	list = g_object_new (e_source_list_get_type (), NULL);

	list->priv->gconf_path = g_strdup (path);
	list->priv->gconf_client = client;
	g_object_ref (client);

	gconf_client_add_dir (client, path, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	list->priv->gconf_notify_id
		= gconf_client_notify_add (client, path,
					   (GConfClientNotifyFunc) conf_changed_callback, list,
					   NULL, NULL);
	load_from_gconf (list);

	return list;
}

ESourceList *
e_source_list_new_for_gconf_default (const gchar  *path)
{
	ESourceList *list;

	g_return_val_if_fail (path != NULL, NULL);

	list = g_object_new (e_source_list_get_type (), NULL);

	list->priv->gconf_path = g_strdup (path);
	list->priv->gconf_client = gconf_client_get_default ();

	gconf_client_add_dir (list->priv->gconf_client, path, GCONF_CLIENT_PRELOAD_ONELEVEL, NULL);

	list->priv->gconf_notify_id
		= gconf_client_notify_add (list->priv->gconf_client, path,
					   (GConfClientNotifyFunc) conf_changed_callback, list,
					   NULL, NULL);
	load_from_gconf (list);

	return list;
}

GSList *
e_source_list_peek_groups (ESourceList *list)
{
	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);

	return list->priv->groups;
}

ESourceGroup *
e_source_list_peek_group_by_uid (ESourceList *list,
				 const gchar *uid)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	for (p = list->priv->groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);

		if (strcmp (e_source_group_peek_uid (group), uid) == 0)
			return group;
	}

	return NULL;
}

#ifndef EDS_DISABLE_DEPRECATED
/**
 * Note: This function isn't safe with respect of localized names,
 * use e_source_list_peek_group_by_base_uri instead.
 **/
ESourceGroup *
e_source_list_peek_group_by_name (ESourceList *list,
				  const gchar *name)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);
	g_return_val_if_fail (name != NULL, NULL);

	for (p = list->priv->groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);

		if (strcmp (e_source_group_peek_name (group), name) == 0)
			return group;
	}

	return NULL;
}
#endif

/**
 * e_source_list_peek_group_by_base_uri:
 * Returns the first group which base uri begins with a base_uri.
 **/
ESourceGroup *
e_source_list_peek_group_by_base_uri (ESourceList *list, const gchar *base_uri)
{
	GSList *p;
	gint len;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);
	g_return_val_if_fail (base_uri != NULL, NULL);

	len = strlen (base_uri);

	for (p = list->priv->groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		const gchar *buri = e_source_group_peek_base_uri (group);

		if (buri && g_ascii_strncasecmp (buri, base_uri, len) == 0)
			return group;
	}

	return NULL;
}

struct property_check_struct {
	ESourceGroup *group;
	gboolean same;
};

static void
check_group_property (const gchar *property_name, const gchar *property_value, struct property_check_struct *pcs)
{
	gchar *value;

	g_return_if_fail (property_name != NULL);
	g_return_if_fail (property_value != NULL);
	g_return_if_fail (pcs != NULL);
	g_return_if_fail (pcs->group != NULL);

	value = e_source_group_get_property (pcs->group, property_name);
	pcs->same = pcs->same && value && g_ascii_strcasecmp (property_value, value) == 0;
	g_free (value);
}

/**
 * e_source_list_peek_group_by_properties:
 * Peeks group by its properties. Parameters are pairs of strings
 * property_name, property_value, terminated by NULL! ESourceGroup
 * is returned only if matches all the properties. Values are compared
 * case insensitively.
 **/
ESourceGroup *
e_source_list_peek_group_by_properties (ESourceList *list, const gchar *property_name, ...)
{
	GSList *p;
	va_list ap;
	GHashTable *props;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);
	g_return_val_if_fail (property_name != NULL, NULL);

	props = g_hash_table_new (g_str_hash, g_str_equal);

	va_start (ap, property_name);
	while (property_name) {
		const gchar *value = va_arg (ap, const gchar *);

		if (!value)
			break;

		g_hash_table_insert (props, (gpointer)property_name, (gpointer)value);
		property_name = va_arg (ap, const gchar *);
	}
	va_end (ap);

	for (p = list->priv->groups; p != NULL; p = p->next) {
		struct property_check_struct pcs;

		pcs.group = E_SOURCE_GROUP (p->data);
		pcs.same = TRUE;

		g_hash_table_foreach (props, (GHFunc) check_group_property, &pcs);

		if (pcs.same) {
			g_hash_table_unref (props);
			return pcs.group;
		}
	}

	g_hash_table_unref (props);

	return NULL;
}

ESource *
e_source_list_peek_source_by_uid (ESourceList *list,
				  const gchar *uid)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	for (p = list->priv->groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		ESource *source;

		source = e_source_group_peek_source_by_uid (group, uid);
		if (source)
			return source;
	}

	return NULL;
}

ESource *
e_source_list_peek_source_any (ESourceList *list)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);

	for (p = list->priv->groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		GSList *sources;

		sources = e_source_group_peek_sources (group);
		if (sources && sources->data)
			return E_SOURCE (sources->data);
	}

	return NULL;
}

gboolean
e_source_list_add_group (ESourceList *list,
			 ESourceGroup *group,
			 gint position)
{
	g_return_val_if_fail (E_IS_SOURCE_LIST (list), FALSE);
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);

	if (e_source_list_peek_group_by_uid (list, e_source_group_peek_uid (group)) != NULL)
		return FALSE;

	list->priv->groups = g_slist_insert (list->priv->groups, group, position);
	g_object_ref (group);

	g_signal_connect (group, "changed", G_CALLBACK (group_changed_callback), list);

	g_signal_emit (list, signals[GROUP_ADDED], 0, group);
	g_signal_emit (list, signals[CHANGED], 0);

	return TRUE;
}

gboolean
e_source_list_remove_group (ESourceList *list,
			    ESourceGroup *group)
{
	g_return_val_if_fail (E_IS_SOURCE_LIST (list), FALSE);
	g_return_val_if_fail (E_IS_SOURCE_GROUP (group), FALSE);

	if (e_source_list_peek_group_by_uid (list, e_source_group_peek_uid (group)) == NULL)
		return FALSE;

	remove_group (list, group);
	return TRUE;
}

gboolean
e_source_list_remove_group_by_uid (ESourceList *list,
				    const gchar *uid)
{
	ESourceGroup *group;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	group = e_source_list_peek_group_by_uid (list, uid);
	if (group== NULL)
		return FALSE;

	remove_group (list, group);
	return TRUE;
}

/**
 * e_source_list_ensure_group:
 * Ensures group with the @base_uri will exists in the @list and its name will be @name.
 * If ret_it will be TRUE the group will be also returned, in that case caller should
 * g_object_unref the group. Otherwise it returns NULL.
 **/
ESourceGroup *
e_source_list_ensure_group (ESourceList *list, const gchar *name, const gchar *base_uri, gboolean ret_it)
{
	ESourceGroup *group;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), NULL);
	g_return_val_if_fail (name != NULL, NULL);
	g_return_val_if_fail (base_uri != NULL, NULL);

	group = e_source_list_peek_group_by_base_uri (list, base_uri);
	if (group) {
		e_source_group_set_name (group, name);
		if (ret_it)
			g_object_ref (group);
		else
			group = NULL;
	} else {
		group = e_source_group_new (name, base_uri);

		if (!e_source_list_add_group (list, group, -1)) {
			g_warning ("Could not add source group %s with base uri %s to a source list", name, base_uri);
			g_object_unref (group);
			group = NULL;
		} else {
			/* save it now */
			e_source_list_sync (list, NULL);

			if (!ret_it) {
				g_object_unref (group);
				group = NULL;
			}
		}
	}

	return group;
}

/**
 * e_source_list_remove_group_by_base_uri:
 * Removes group with given base_uri.
 * Returns TRUE if group was found.
 **/
gboolean
e_source_list_remove_group_by_base_uri (ESourceList *list, const gchar *base_uri)
{
	ESourceGroup *group;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), FALSE);
	g_return_val_if_fail (base_uri != NULL, FALSE);

	group = e_source_list_peek_group_by_base_uri (list, base_uri);
	if (group == NULL)
		return FALSE;

	remove_group (list, group);
	return TRUE;
}

gboolean
e_source_list_remove_source_by_uid (ESourceList *list,
				     const gchar *uid)
{
	GSList *p;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	for (p = list->priv->groups; p != NULL; p = p->next) {
		ESourceGroup *group = E_SOURCE_GROUP (p->data);
		ESource *source;

		source = e_source_group_peek_source_by_uid (group, uid);
		if (source)
			return e_source_group_remove_source_by_uid (group, uid);
	}

	return FALSE;
}

gboolean
e_source_list_sync (ESourceList *list,
		    GError **error)
{
	GSList *conf_list;
	GSList *p;
	gboolean retval;

	g_return_val_if_fail (E_IS_SOURCE_LIST (list), FALSE);

	conf_list = NULL;
	for (p = list->priv->groups; p != NULL; p = p->next)
		conf_list = g_slist_prepend (conf_list, e_source_group_to_xml (E_SOURCE_GROUP (p->data)));
	conf_list = g_slist_reverse (conf_list);

	if (!e_source_list_is_gconf_updated (list))
		retval = gconf_client_set_list (list->priv->gconf_client,
						list->priv->gconf_path,
						GCONF_VALUE_STRING,
						conf_list,
						error);
	else
		retval = TRUE;

	g_slist_foreach (conf_list, (GFunc) g_free, NULL);
	g_slist_free (conf_list);

	return retval;
}

gboolean
e_source_list_is_gconf_updated (ESourceList *list)
{
	gchar *source_group_xml = NULL;
	gchar *gconf_xml = NULL;
	gchar *group_uid = NULL;
	GSList *conf_list = NULL, *temp = NULL, *p = NULL;
	xmlDocPtr xmldoc;
	ESourceGroup *group = NULL;
	GSList *groups = NULL;
	gboolean conf_to_list = TRUE, list_to_conf = TRUE;

	g_return_val_if_fail (list != NULL, FALSE);

	conf_list = gconf_client_get_list (list->priv->gconf_client,
					   list->priv->gconf_path,
					   GCONF_VALUE_STRING, NULL);

	/* From conf to list */

	for (temp = conf_list; temp != NULL; temp = temp->next) {
		gconf_xml = (gchar *)temp->data;
		xmldoc = xmlParseDoc ((const xmlChar *)gconf_xml);

		if (xmldoc == NULL)
			continue;

		group_uid = e_source_group_uid_from_xmldoc (xmldoc);
		group = e_source_list_peek_group_by_uid (list, group_uid);
		g_free (group_uid);
		xmlFreeDoc (xmldoc);

		if (group) {
			source_group_xml = e_source_group_to_xml (group);
			if (e_source_group_xmlstr_equal (gconf_xml, source_group_xml)) {
				g_free (source_group_xml);
				continue;
			}
			else {
				conf_to_list  = FALSE;
				g_free (source_group_xml);
				break;
			}
		} else {
			conf_to_list = FALSE;
			break;
		}
	}

	/* If there is mismatch, free the conf_list and return FALSE */
	if (!conf_to_list) {
		for (p = conf_list; p != NULL; p = p->next) {
			gconf_xml = (gchar *) p->data;
			g_free (gconf_xml);
		}
		g_slist_free (conf_list);
		return FALSE;
	}

	groups = e_source_list_peek_groups (list);

	/* From list to conf */

	for (p = groups; p != NULL; p = p->next) {
		group = E_SOURCE_GROUP (p->data);
		source_group_xml = e_source_group_to_xml (group);

		for (temp = conf_list; temp != NULL; temp = temp->next) {
			gconf_xml = (gchar *)temp->data;
			if (!e_source_group_xmlstr_equal (gconf_xml, source_group_xml))
				continue;
			else
				break;
		}
		g_free (source_group_xml);

		if (!temp) {
			list_to_conf = FALSE;
			break;
		}

	}

	for (p = conf_list; p != NULL; p = p->next) {
		gconf_xml = (gchar *) p->data;
		g_free (gconf_xml);
	}
	g_slist_free (conf_list);

	/* If there is mismatch return FALSE */
	if (!list_to_conf)
		return FALSE;

	return TRUE;
}
