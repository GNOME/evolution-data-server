/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          JP Rosevear <jpr@ximian.com>
 *          Rodrigo Moya <rodrigo@ximian.com>    
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <libxml/parser.h>
#include <libxml/parserInternals.h>
#include <libxml/xmlmemory.h>

#include "e-cal-backend.h"



/* A category that exists in some of the objects of the calendar */
typedef struct {
	/* Category name, also used as the key in the categories hash table */
	char *name;

	/* Number of objects that have this category */
	int refcount;
} ECalBackendCategory;

/* Private part of the CalBackend structure */
struct _ECalBackendPrivate {
	/* The source for this backend */
	ESource *source;

	/* URI, from source. This is cached, since we return const. */
	char *uri;

	/* The kind of components for this backend */
	icalcomponent_kind kind;
	
	/* List of Cal objects */
	GMutex *clients_mutex;
	GList *clients;

	GMutex *queries_mutex;
	EList *queries;
	
	/* Hash table of live categories, temporary hash of
	 * added/removed categories, and idle handler for sending
	 * category_changed.
	 */
	GHashTable *categories;
	GHashTable *changed_categories;
	guint category_idle_id;

	/* ECalBackend to pass notifications on to */
	ECalBackend *notification_proxy;
};

/* Property IDs */
enum props {
	PROP_0,
	PROP_SOURCE,
	PROP_URI,
	PROP_KIND
};

/* Signal IDs */
enum {
	LAST_CLIENT_GONE,
	OPENED,
	REMOVED,
	LAST_SIGNAL
};
static guint e_cal_backend_signals[LAST_SIGNAL];

static void e_cal_backend_class_init (ECalBackendClass *class);
static void e_cal_backend_init (ECalBackend *backend);
static void e_cal_backend_finalize (GObject *object);

static void notify_categories_changed (ECalBackend *backend);

#define CLASS(backend) (E_CAL_BACKEND_CLASS (G_OBJECT_GET_CLASS (backend)))

static GObjectClass *parent_class;



/**
 * e_cal_backend_get_type:
 * @void:
 *
 * Registers the #ECalBackend class if necessary, and returns the type ID
 * associated to it.
 *
 * Return value: The type ID of the #ECalBackend class.
 **/
GType
e_cal_backend_get_type (void)
{
	static GType e_cal_backend_type = 0;

	if (!e_cal_backend_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackendClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_class_init,
                        NULL, NULL,
                        sizeof (ECalBackend),
                        0,
                        (GInstanceInitFunc) e_cal_backend_init,
                };
		e_cal_backend_type = g_type_register_static (G_TYPE_OBJECT, "ECalBackend", &info, 0);
	}

	return e_cal_backend_type;
}

static void
e_cal_backend_set_property (GObject *object, guint property_id, const GValue *value, GParamSpec *pspec)
{
	ECalBackend *backend;
	ECalBackendPrivate *priv;
	
	backend = E_CAL_BACKEND (object);
	priv = backend->priv;
	
	switch (property_id) {
	case PROP_SOURCE:
		{
			ESource *new_source;

			new_source = g_value_get_object (value);
			if (new_source)
				g_object_ref (new_source);

			if (priv->source)
				g_object_unref (priv->source);

			priv->source = new_source;

			/* Cache the URI */
			if (new_source) {
				g_free (priv->uri);
				priv->uri = e_source_get_uri (priv->source);
			}
		}
		break;
	case PROP_URI:
		if (!priv->source) {
			g_free (priv->uri);
			priv->uri = g_value_dup_string (value);
		}
		break;
	case PROP_KIND:
		priv->kind = g_value_get_ulong (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

static void
e_cal_backend_get_property (GObject *object, guint property_id, GValue *value, GParamSpec *pspec)
{
 	ECalBackend *backend;
	ECalBackendPrivate *priv;
	
	backend = E_CAL_BACKEND (object);
	priv = backend->priv;

	switch (property_id) {
	case PROP_SOURCE:
		g_value_set_object (value, e_cal_backend_get_source (backend));
		break;
	case PROP_URI:
		g_value_set_string (value, e_cal_backend_get_uri (backend));
		break;
	case PROP_KIND:
		g_value_set_ulong (value, e_cal_backend_get_kind (backend));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
		break;
	}
}

/* Class initialization function for the calendar backend */
static void
e_cal_backend_class_init (ECalBackendClass *class)
{
	GObjectClass *object_class;

	parent_class = (GObjectClass *) g_type_class_peek_parent (class);

	object_class = (GObjectClass *) class;

	object_class->set_property = e_cal_backend_set_property;
	object_class->get_property = e_cal_backend_get_property;
	object_class->finalize = e_cal_backend_finalize;

	g_object_class_install_property (object_class, PROP_SOURCE, 
					 g_param_spec_object ("source", NULL, NULL, E_TYPE_SOURCE,
							      G_PARAM_READABLE | G_PARAM_WRITABLE
							      | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_URI, 
					 g_param_spec_string ("uri", NULL, NULL, "",
							      G_PARAM_READABLE | G_PARAM_WRITABLE
							      | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class, PROP_KIND, 
					 g_param_spec_ulong ("kind", NULL, NULL, 
							     ICAL_NO_COMPONENT, ICAL_XLICMIMEPART_COMPONENT, 
							     ICAL_NO_COMPONENT,
							     G_PARAM_READABLE | G_PARAM_WRITABLE
							     | G_PARAM_CONSTRUCT_ONLY));	
	e_cal_backend_signals[LAST_CLIENT_GONE] =
		g_signal_new ("last_client_gone",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalBackendClass, last_client_gone),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE, 0);
	e_cal_backend_signals[OPENED] =
		g_signal_new ("opened",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalBackendClass, opened),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__ENUM,
			      G_TYPE_NONE, 1,
			      G_TYPE_INT);
	e_cal_backend_signals[REMOVED] =
		g_signal_new ("removed",
			      G_TYPE_FROM_CLASS (class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (ECalBackendClass, removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__ENUM,
			      G_TYPE_NONE, 1,
			      G_TYPE_INT);

	class->last_client_gone = NULL;
	class->opened = NULL;
	class->obj_updated = NULL;

	class->get_cal_address = NULL;
	class->get_alarm_email_address = NULL;
	class->get_static_capabilities = NULL;
	class->open = NULL;
	class->is_loaded = NULL;
	class->is_read_only = NULL;
	class->start_query = NULL;
	class->get_mode = NULL;
	class->set_mode = NULL;	
	class->get_object = NULL;
	class->get_default_object = NULL;
	class->get_object_list = NULL;
	class->get_free_busy = NULL;
	class->get_changes = NULL;
	class->discard_alarm = NULL;
	class->create_object = NULL;
	class->modify_object = NULL;
	class->remove_object = NULL;
	class->receive_objects = NULL;
	class->send_objects = NULL;
	class->get_timezone = NULL;
	class->add_timezone = NULL;
	class->set_default_timezone = NULL;
}

/* Object initialization func for the calendar backend */
void
e_cal_backend_init (ECalBackend *backend)
{
	ECalBackendPrivate *priv;

	priv = g_new0 (ECalBackendPrivate, 1);
	backend->priv = priv;

	priv->clients = NULL;
	priv->clients_mutex = g_mutex_new ();

	/* FIXME bonobo_object_ref/unref? */
	priv->queries = e_list_new((EListCopyFunc) g_object_ref, (EListFreeFunc) g_object_unref, NULL);
	priv->queries_mutex = g_mutex_new ();
	
	priv->categories = g_hash_table_new (g_str_hash, g_str_equal);
	priv->changed_categories = g_hash_table_new (g_str_hash, g_str_equal);
}

/* Used from g_hash_table_foreach(), frees a ECalBackendCategory structure */
static void
free_category_cb (gpointer key, gpointer value, gpointer data)
{
	ECalBackendCategory *c = value;

	g_free (c->name);
	g_free (c);
}

static gboolean
prune_changed_categories (gpointer key, gpointer value, gpointer data)
{
	ECalBackendCategory *c = value;

	if (!c->refcount)
		free_category_cb (key, value, data);
	return TRUE;
}

void
e_cal_backend_finalize (GObject *object)
{
	ECalBackend *backend = (ECalBackend *)object;
	ECalBackendPrivate *priv;

	priv = backend->priv;

	g_assert (priv->clients == NULL);

	g_object_unref (priv->queries);

	g_hash_table_foreach_remove (priv->changed_categories, prune_changed_categories, NULL);
	g_hash_table_destroy (priv->changed_categories);

	g_hash_table_foreach (priv->categories, free_category_cb, NULL);
	g_hash_table_destroy (priv->categories);

	g_mutex_free (priv->clients_mutex);
	g_mutex_free (priv->queries_mutex);

	if (priv->category_idle_id)
		g_source_remove (priv->category_idle_id);

	g_free (priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}



ESource *
e_cal_backend_get_source (ECalBackend *backend)
{
	ECalBackendPrivate *priv;
	
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	priv = backend->priv;
	
	return priv->source;
}

/**
 * e_cal_backend_get_uri:
 * @backend: A calendar backend.
 *
 * Queries the URI of a calendar backend, which must already have an open
 * calendar.
 *
 * Return value: The URI where the calendar is stored.
 **/
const char *
e_cal_backend_get_uri (ECalBackend *backend)
{
	ECalBackendPrivate *priv;
	
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	priv = backend->priv;
	
	return priv->uri;
}

icalcomponent_kind
e_cal_backend_get_kind (ECalBackend *backend)
{
	ECalBackendPrivate *priv;
	
	g_return_val_if_fail (backend != NULL, ICAL_NO_COMPONENT);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), ICAL_NO_COMPONENT);

	priv = backend->priv;
	
	return priv->kind;
}

static void
cal_destroy_cb (gpointer data, GObject *where_cal_was)
{
	ECalBackend *backend = E_CAL_BACKEND (data);

	e_cal_backend_remove_client (backend, (EDataCal *) where_cal_was);
}

static void
listener_died_cb (gpointer cnx, gpointer data)
{
	EDataCal *cal = E_DATA_CAL (data);

	e_cal_backend_remove_client (e_data_cal_get_backend (cal), cal);
}

static void
last_client_gone (ECalBackend *backend)
{
	g_signal_emit (backend, e_cal_backend_signals[LAST_CLIENT_GONE], 0);
}

void
e_cal_backend_add_client (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendPrivate *priv;
	
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = backend->priv;
	
	bonobo_object_set_immortal (BONOBO_OBJECT (cal), TRUE);

	g_object_weak_ref (G_OBJECT (cal), cal_destroy_cb, backend);

	ORBit_small_listen_for_broken (e_data_cal_get_listener (cal), G_CALLBACK (listener_died_cb), cal);

	g_mutex_lock (priv->clients_mutex);
	priv->clients = g_list_append (priv->clients, cal);
	g_mutex_unlock (priv->clients_mutex);

	/* Tell the new client about the list of categories.
	 * (Ends up telling all the other clients too, but *shrug*.)
	 */
	/* FIXME This doesn't seem right at all */
	notify_categories_changed (backend);
}

void
e_cal_backend_remove_client (ECalBackend *backend, EDataCal *cal)
{
	ECalBackendPrivate *priv;
	
	/* XXX this needs a bit more thinking wrt the mutex - we
	   should be holding it when we check to see if clients is
	   NULL */
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (cal != NULL);
	g_return_if_fail (E_IS_DATA_CAL (cal));

	priv = backend->priv;

	/* Disconnect */
	g_mutex_lock (priv->clients_mutex);
	priv->clients = g_list_remove (priv->clients, cal);
	g_mutex_unlock (priv->clients_mutex);

	/* When all clients go away, notify the parent factory about it so that
	 * it may decide whether to kill the backend or not.
	 */
	if (!priv->clients)
		last_client_gone (backend);
}

void
e_cal_backend_add_query (ECalBackend *backend, EDataCalView *query)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_mutex_lock (backend->priv->queries_mutex);

	e_list_append (backend->priv->queries, query);
	
	g_mutex_unlock (backend->priv->queries_mutex);
}

EList *
e_cal_backend_get_queries (ECalBackend *backend)
{
	g_return_val_if_fail (backend != NULL, NULL);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);

	return g_object_ref (backend->priv->queries);
}


/**
 * e_cal_backend_get_cal_address:
 * @backend: A calendar backend.
 *
 * Queries the cal address associated with a calendar backend, which
 * must already have an open calendar.
 *
 * Return value: The cal address associated with the calendar.
 **/
void
e_cal_backend_get_cal_address (ECalBackend *backend, EDataCal *cal)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_cal_address != NULL);
	(* CLASS (backend)->get_cal_address) (backend, cal);
}

void
e_cal_backend_get_alarm_email_address (ECalBackend *backend, EDataCal *cal)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_alarm_email_address != NULL);
	(* CLASS (backend)->get_alarm_email_address) (backend, cal);
}

void
e_cal_backend_get_ldap_attribute (ECalBackend *backend, EDataCal *cal)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_ldap_attribute != NULL);
	(* CLASS (backend)->get_ldap_attribute) (backend, cal);
}

void
e_cal_backend_get_static_capabilities (ECalBackend *backend, EDataCal *cal)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_static_capabilities != NULL);
	(* CLASS (backend)->get_static_capabilities) (backend, cal);
}

/**
 * e_cal_backend_open:
 * @backend: A calendar backend.
 * @uristr: URI that contains the calendar data.
 * @only_if_exists: Whether the calendar should be opened only if it already
 * exists.  If FALSE, a new calendar will be created when the specified @uri
 * does not exist.
 * @username: User name to use for authentication (if needed).
 * @password: Password for @username.
 *
 * Opens a calendar backend with data from a calendar stored at the specified
 * URI.
 *
 * Return value: An operation status code.
 **/
void
e_cal_backend_open (ECalBackend *backend, EDataCal *cal, gboolean only_if_exists,
		    const char *username, const char *password)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->open != NULL);
	(* CLASS (backend)->open) (backend, cal, only_if_exists, username, password);
}

void
e_cal_backend_remove (ECalBackend *backend, EDataCal *cal)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->remove != NULL);
	(* CLASS (backend)->remove) (backend, cal);
}

/**
 * e_cal_backend_is_loaded:
 * @backend: A calendar backend.
 * 
 * Queries whether a calendar backend has been loaded yet.
 * 
 * Return value: TRUE if the backend has been loaded with data, FALSE
 * otherwise.
 **/
gboolean
e_cal_backend_is_loaded (ECalBackend *backend)
{
	gboolean result;

	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	g_assert (CLASS (backend)->is_loaded != NULL);
	result = (* CLASS (backend)->is_loaded) (backend);

	return result;
}

/**
 * e_cal_backend_is_read_only
 * @backend: A calendar backend.
 *
 * Queries whether a calendar backend is read only or not.
 *
 * Return value: TRUE if the calendar is read only, FALSE otherwise.
 */
void
e_cal_backend_is_read_only (ECalBackend *backend, EDataCal *cal)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->is_read_only != NULL);
	(* CLASS (backend)->is_read_only) (backend, cal);
}

void 
e_cal_backend_start_query (ECalBackend *backend, EDataCalView *query)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->start_query != NULL);
	(* CLASS (backend)->start_query) (backend, query);
}

/**
 * e_cal_backend_get_mode:
 * @backend: A calendar backend. 
 * 
 * Queries whether a calendar backend is connected remotely.
 * 
 * Return value: The current mode the calendar is in
 **/
CalMode
e_cal_backend_get_mode (ECalBackend *backend)
{
	CalMode result;

	g_return_val_if_fail (backend != NULL, FALSE);
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), FALSE);

	g_assert (CLASS (backend)->get_mode != NULL);
	result = (* CLASS (backend)->get_mode) (backend);

	return result;
}


/**
 * e_cal_backend_set_mode:
 * @backend: A calendar backend
 * @mode: Mode to change to
 * 
 * Sets the mode of the calendar
 * 
 **/
void
e_cal_backend_set_mode (ECalBackend *backend, CalMode mode)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->set_mode != NULL);
	(* CLASS (backend)->set_mode) (backend, mode);
}

void
e_cal_backend_get_default_object (ECalBackend *backend, EDataCal *cal)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_default_object != NULL);
	(* CLASS (backend)->get_default_object) (backend, cal);
}

/**
 * e_cal_backend_get_object:
 * @backend: A calendar backend.
 * @uid: Unique identifier for a calendar object.
 * @rid: ID for the object's recurrence to get.
 *
 * Queries a calendar backend for a calendar object based on its unique
 * identifier and its recurrence ID (if a recurrent appointment).
 *
 * Return value: The string representation of a complete calendar wrapping the
 * the sought object, or NULL if no object had the specified UID.
 **/
void
e_cal_backend_get_object (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);

	g_assert (CLASS (backend)->get_object != NULL);
	(* CLASS (backend)->get_object) (backend, cal, uid, rid);
}

/**
 * e_cal_backend_get_object_list:
 * @backend: 
 * @type: 
 * 
 * 
 * 
 * Return value: 
 **/
void
e_cal_backend_get_object_list (ECalBackend *backend, EDataCal *cal, const char *sexp)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	g_assert (CLASS (backend)->get_object_list != NULL);
	return (* CLASS (backend)->get_object_list) (backend, cal, sexp);
}

/**
 * e_cal_backend_get_free_busy:
 * @backend: A calendar backend.
 * @users: List of users to get free/busy information for.
 * @start: Start time for query.
 * @end: End time for query.
 * 
 * Gets a free/busy object for the given time interval
 * 
 * Return value: a list of CalObj's
 **/
void
e_cal_backend_get_free_busy (ECalBackend *backend, EDataCal *cal, GList *users, time_t start, time_t end)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (start != -1 && end != -1);
	g_return_if_fail (start <= end);

	g_assert (CLASS (backend)->get_free_busy != NULL);
	(* CLASS (backend)->get_free_busy) (backend, cal, users, start, end);
}

/**
 * e_cal_backend_get_changes:
 * @backend: A calendar backend
 * @change_id: A unique uid for the callers change list
 * 
 * Builds a sequence of objects and the type of change that occurred on them since
 * the last time the give change_id was seen
 * 
 * Return value: A list of the objects that changed and the type of change
 **/
void
e_cal_backend_get_changes (ECalBackend *backend, EDataCal *cal, const char *change_id) 
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (change_id != NULL);

	g_assert (CLASS (backend)->get_changes != NULL);
	(* CLASS (backend)->get_changes) (backend, cal, change_id);
}

/**
 * e_cal_backend_discard_alarm
 * @backend: A calendar backend.
 * @uid: UID of the component to discard the alarm from.
 * @auid: Alarm ID.
 *
 * Discards an alarm from the given component. This allows the specific backend
 * to do whatever is needed to really discard the alarm.
 *
 **/
void
e_cal_backend_discard_alarm (ECalBackend *backend, EDataCal *cal, const char *uid, const char *auid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (auid != NULL);

	g_assert (CLASS (backend)->discard_alarm != NULL);
	(* CLASS (backend)->discard_alarm) (backend, cal, uid, auid);
}

void
e_cal_backend_create_object (ECalBackend *backend, EDataCal *cal, const char *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	g_assert (CLASS (backend)->create_object != NULL);
	(* CLASS (backend)->create_object) (backend, cal, calobj);
}

void
e_cal_backend_modify_object (ECalBackend *backend, EDataCal *cal, const char *calobj, CalObjModType mod)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	g_assert (CLASS (backend)->modify_object != NULL);
	(* CLASS (backend)->modify_object) (backend, cal, calobj, mod);
}

/**
 * e_cal_backend_remove_object:
 * @backend: A calendar backend.
 * @uid: Unique identifier of the object to remove.
 * @rid: A recurrence ID.
 * 
 * Removes an object in a calendar backend.  The backend will notify all of its
 * clients about the change.
 * 
 **/
void
e_cal_backend_remove_object (ECalBackend *backend, EDataCal *cal, const char *uid, const char *rid, CalObjModType mod)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (uid != NULL);

	g_assert (CLASS (backend)->remove_object != NULL);
	(* CLASS (backend)->remove_object) (backend, cal, uid, rid, mod);
}

void
e_cal_backend_receive_objects (ECalBackend *backend, EDataCal *cal, const char *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	g_assert (CLASS (backend)->receive_objects != NULL);
	(* CLASS (backend)->receive_objects) (backend, cal, calobj);
}

void
e_cal_backend_send_objects (ECalBackend *backend, EDataCal *cal, const char *calobj)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (calobj != NULL);

	g_assert (CLASS (backend)->send_objects != NULL);
	(* CLASS (backend)->send_objects) (backend, cal, calobj);
}

/**
 * e_cal_backend_get_timezone:
 * @backend: A calendar backend.
 * @tzid: Unique identifier of a VTIMEZONE object. Note that this must not be
 * NULL.
 * 
 * Returns the icaltimezone* corresponding to the TZID, or NULL if the TZID
 * can't be found.
 * 
 * Returns: The icaltimezone* corresponding to the given TZID, or NULL.
 **/
void
e_cal_backend_get_timezone (ECalBackend *backend, EDataCal *cal, const char *tzid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzid != NULL);

	g_assert (CLASS (backend)->get_timezone != NULL);
	(* CLASS (backend)->get_timezone) (backend, cal, tzid);
}

/**
 * e_cal_backend_set_default_timezone:
 * @backend: A calendar backend.
 * @tzid: The TZID identifying the timezone.
 * 
 * Sets the default timezone for the calendar, which is used to resolve
 * DATE and floating DATE-TIME values.
 * 
 * Returns: TRUE if the VTIMEZONE data for the timezone was found, or FALSE if
 * not.
 **/
void
e_cal_backend_set_default_timezone (ECalBackend *backend, EDataCal *cal, const char *tzid)
{
	g_return_if_fail (backend != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzid != NULL);

	g_assert (CLASS (backend)->set_default_timezone != NULL);
	(* CLASS (backend)->set_default_timezone) (backend, cal, tzid);
}

/**
 * e_cal_backend_add_timezone
 * @backend: A calendar backend.
 * @tzobj: The timezone object, in a string.
 *
 * Add a timezone object to the given backend.
 *
 * Returns: TRUE if successful, or FALSE if not.
 */
void
e_cal_backend_add_timezone (ECalBackend *backend, EDataCal *cal, const char *tzobj)
{
	g_return_if_fail (E_IS_CAL_BACKEND (backend));
	g_return_if_fail (tzobj != NULL);
	g_return_if_fail (CLASS (backend)->add_timezone != NULL);

	(* CLASS (backend)->add_timezone) (backend, cal, tzobj);
}

icaltimezone *
e_cal_backend_internal_get_default_timezone (ECalBackend *backend)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);
	g_return_val_if_fail (CLASS (backend)->internal_get_default_timezone != NULL, NULL);

	return (* CLASS (backend)->internal_get_default_timezone) (backend);
}

icaltimezone *
e_cal_backend_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	g_return_val_if_fail (E_IS_CAL_BACKEND (backend), NULL);
	g_return_val_if_fail (tzid != NULL, NULL);
	g_return_val_if_fail (CLASS (backend)->internal_get_timezone != NULL, NULL);

	return (* CLASS (backend)->internal_get_timezone) (backend, tzid);
}

void
e_cal_backend_set_notification_proxy (ECalBackend *backend, ECalBackend *proxy)
{
	ECalBackendPrivate *priv;

	g_return_if_fail (E_IS_CAL_BACKEND (backend));

	priv = backend->priv;

	priv->notification_proxy = proxy;
}

/**
 * e_cal_backend_notify_object_created:
 * @backend: A calendar backend.
 * @calobj: iCalendar representation of new object
 *
 * Notifies each of the backend's listeners about a new object.
 *
 * cal_notify_object_created() calls this for you. You only need to
 * call e_cal_backend_notify_object_created() yourself to report objects
 * created by non-PCS clients.
 **/
void
e_cal_backend_notify_object_created (ECalBackend *backend, const char *calobj)
{
	ECalBackendPrivate *priv;
	EList *queries;
	EIterator *iter;
	EDataCalView *query;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_created (priv->notification_proxy, calobj);
		return;
	}

	queries = e_cal_backend_get_queries (backend);
	iter = e_list_get_iterator (queries);

	while (e_iterator_is_valid (iter)) {
		query = QUERY (e_iterator_get (iter));

		bonobo_object_ref (query);
		if (e_data_cal_view_object_matches (query, calobj))		
			e_data_cal_view_notify_objects_added_1 (query, calobj);
		bonobo_object_unref (query);

		e_iterator_next (iter);
	}
	g_object_unref (iter);
	g_object_unref (queries);
}

/**
 * e_cal_backend_notify_object_modified:
 * @backend: A calendar backend.
 * @old_object: iCalendar representation of the original form of the object
 * @object: iCalendar representation of the new form of the object
 *
 * Notifies each of the backend's listeners about a modified object.
 *
 * cal_notify_object_modified() calls this for you. You only need to
 * call e_cal_backend_notify_object_modified() yourself to report objects
 * modified by non-PCS clients.
 **/
void
e_cal_backend_notify_object_modified (ECalBackend *backend, 
				    const char *old_object, const char *object)
{
	ECalBackendPrivate *priv;
	EList *queries;
	EIterator *iter;
	EDataCalView *query;
	gboolean old_match, new_match;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_modified (priv->notification_proxy, old_object, object);
		return;
	}

	queries = e_cal_backend_get_queries (backend);
	iter = e_list_get_iterator (queries);

	while (e_iterator_is_valid (iter)) {
		query = QUERY (e_iterator_get (iter));
		
		bonobo_object_ref (query);

		old_match = e_data_cal_view_object_matches (query, old_object);
		new_match = e_data_cal_view_object_matches (query, object);
		if (old_match && new_match)
			e_data_cal_view_notify_objects_modified_1 (query, object);
		else if (new_match)
			e_data_cal_view_notify_objects_added_1 (query, object);
		else if (old_match) {
			icalcomponent *comp;

			comp = icalcomponent_new_from_string ((char *)old_object);
			e_data_cal_view_notify_objects_removed_1 (query, icalcomponent_get_uid (comp));
			icalcomponent_free (comp);
		}

		bonobo_object_unref (query);

		e_iterator_next (iter);
	}
	g_object_unref (iter);
	g_object_unref (queries);
}

/**
 * e_cal_backend_notify_object_removed:
 * @backend: A calendar backend.
 * @uid: the UID of the removed object
 * @old_object: iCalendar representation of the removed object
 *
 * Notifies each of the backend's listeners about a removed object.
 *
 * cal_notify_object_removed() calls this for you. You only need to
 * call e_cal_backend_notify_object_removed() yourself to report objects
 * removed by non-PCS clients.
 **/
void
e_cal_backend_notify_object_removed (ECalBackend *backend, const char *uid,
				   const char *old_object)
{
	ECalBackendPrivate *priv;
	EList *queries;
	EIterator *iter;
	EDataCalView *query;

	priv = backend->priv;

	if (priv->notification_proxy) {
		e_cal_backend_notify_object_removed (priv->notification_proxy, uid, old_object);
		return;
	}

	queries = e_cal_backend_get_queries (backend);
	iter = e_list_get_iterator (queries);

	while (e_iterator_is_valid (iter)) {
		query = QUERY (e_iterator_get (iter));

		bonobo_object_ref (query);
		if (e_data_cal_view_object_matches (query, old_object))
			e_data_cal_view_notify_objects_removed_1 (query, uid);
		bonobo_object_unref (query);

		e_iterator_next (iter);
	}
	g_object_unref (iter);
	g_object_unref (queries);
}

/**
 * e_cal_backend_notify_mode:
 * @backend: A calendar backend.
 * @status: Status of the mode set
 * @mode: the current mode
 *
 * Notifies each of the backend's listeners about the results of a
 * setMode call.
 **/
void
e_cal_backend_notify_mode (ECalBackend *backend,
			 GNOME_Evolution_Calendar_CalListener_SetModeStatus status, 
			 GNOME_Evolution_Calendar_CalMode mode)
{
	ECalBackendPrivate *priv = backend->priv;
	GList *l;

	if (priv->notification_proxy) {
		e_cal_backend_notify_mode (priv->notification_proxy, status, mode);
		return;
	}

	for (l = priv->clients; l; l = l->next)
		e_data_cal_notify_mode (l->data, status, mode);
}

/**
 * e_cal_backend_notify_error:
 * @backend: A calendar backend.
 * @message: Error message
 *
 * Notifies each of the backend's listeners about an error
 **/
void
e_cal_backend_notify_error (ECalBackend *backend, const char *message)
{
	ECalBackendPrivate *priv = backend->priv;
	GList *l;

	if (priv->notification_proxy) {
		e_cal_backend_notify_error (priv->notification_proxy, message);
		return;
	}

	for (l = priv->clients; l; l = l->next)
		e_data_cal_notify_error (l->data, message);
}

static void
add_category_cb (gpointer name, gpointer category, gpointer data)
{
	GNOME_Evolution_Calendar_StringSeq *seq = data;

	seq->_buffer[seq->_length++] = CORBA_string_dup (name);
}

static void
notify_categories_changed (ECalBackend *backend)
{
	ECalBackendPrivate *priv = backend->priv;
	GNOME_Evolution_Calendar_StringSeq *seq;
	GList *l;

	/* Build the sequence of category names */
	seq = GNOME_Evolution_Calendar_StringSeq__alloc ();
	seq->_length = 0;
	seq->_maximum = g_hash_table_size (priv->categories);
	seq->_buffer = CORBA_sequence_CORBA_string_allocbuf (seq->_maximum);
	CORBA_sequence_set_release (seq, TRUE);

	g_hash_table_foreach (priv->categories, add_category_cb, seq);

	/* Notify the clients */
	for (l = priv->clients; l; l = l->next)
		e_data_cal_notify_categories_changed (l->data, seq);

	CORBA_free (seq);
}

static gboolean
idle_notify_categories_changed (gpointer data)
{
	ECalBackend *backend = E_CAL_BACKEND (data);
	ECalBackendPrivate *priv = backend->priv;

	if (g_hash_table_size (priv->changed_categories)) {
		notify_categories_changed (backend);
		g_hash_table_foreach_remove (priv->changed_categories, prune_changed_categories, NULL);
	}

	priv->category_idle_id = 0;
	
	return FALSE;
}

/**
 * e_cal_backend_ref_categories:
 * @backend: A calendar backend
 * @categories: a list of categories
 *
 * Adds 1 to the refcount of each of the named categories. If any of
 * the categories are new, clients will be notified of the updated
 * category list at idle time.
 **/
void
e_cal_backend_ref_categories (ECalBackend *backend, GSList *categories)
{
	ECalBackendPrivate *priv;
	ECalBackendCategory *c;
	const char *name;

	priv = backend->priv;

	while (categories) {
		name = categories->data;
		c = g_hash_table_lookup (priv->categories, name);

		if (c)
			c->refcount++;
		else {
			/* See if it was recently removed */

			c = g_hash_table_lookup (priv->changed_categories, name);
			if (c && c->refcount == 0) {
				/* Move it back to the set of live categories */
				g_hash_table_remove (priv->changed_categories, c->name);

				c->refcount = 1;
				g_hash_table_insert (priv->categories, c->name, c);
			} else {
				/* Create a new category */
				c = g_new (ECalBackendCategory, 1);
				c->name = g_strdup (name);
				c->refcount = 1;
				g_hash_table_insert (priv->categories, c->name, c);
				g_hash_table_insert (priv->changed_categories, c->name, c);
			}
		}

		categories = categories->next;
	}

	if (g_hash_table_size (priv->changed_categories) &&
	    !priv->category_idle_id)
		priv->category_idle_id = g_idle_add (idle_notify_categories_changed, backend);
}

/**
 * e_cal_backend_unref_categories:
 * @backend: A calendar backend
 * @categories: a list of categories
 *
 * Subtracts 1 from the refcount of each of the named categories. If
 * any of the refcounts go down to 0, clients will be notified of the
 * updated category list at idle time.
 **/
void
e_cal_backend_unref_categories (ECalBackend *backend, GSList *categories)
{
	ECalBackendPrivate *priv;
	ECalBackendCategory *c;
	const char *name;

	priv = backend->priv;

	while (categories) {
		name = categories->data;
		c = g_hash_table_lookup (priv->categories, name);

		if (c) {
			g_assert (c != NULL);
			g_assert (c->refcount > 0);

			c->refcount--;

			if (c->refcount == 0) {
				g_hash_table_remove (priv->categories, c->name);
				g_hash_table_insert (priv->changed_categories, c->name, c);
			}
		}

		categories = categories->next;
	}

	if (g_hash_table_size (priv->changed_categories) &&
	    !priv->category_idle_id)
		priv->category_idle_id = g_idle_add (idle_notify_categories_changed, backend);
}
