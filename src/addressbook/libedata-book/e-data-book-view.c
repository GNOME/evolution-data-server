/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2006 OpenedHand Ltd
 * Copyright (C) 2009 Intel Corporation
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Ross Burton <ross@linux.intel.com>
 */

/**
 * SECTION: e-data-book-view
 * @include: libedata-book/libedata-book.h
 * @short_description: A server side object for issuing view notifications
 *
 * This class communicates with #EBookClientViews over the bus.
 *
 * Addressbook backends can automatically own a number of views requested
 * by the client, this API can be used by the backend to issue notifications
 * which will be delivered to the #EBookClientView
 **/

#include "evolution-data-server-config.h"

#include <string.h>

#include "e-data-book-view.h"

#include "e-data-book.h"
#include "e-book-backend.h"

#include "e-dbus-address-book-view.h"

/* how many items can be hold in a cache, before propagated to UI */
#define THRESHOLD_ITEMS 32

/* how long to wait until notifications are propagated to UI; in seconds */
#define THRESHOLD_SECONDS 2

struct _EDataBookViewPrivate {
	GDBusConnection *connection;
	EDBusAddressBookView *dbus_object;
	gchar *object_path;

	GWeakRef backend_weakref; /* EBookBackend * */

	EBookBackendSExp *sexp;
	EBookClientViewFlags flags;

	gboolean force_initial_notifications;
	gboolean running;
	gboolean complete;
	GMutex pending_mutex;

	GArray *adds;
	GArray *changes;
	GArray *removes;

	GHashTable *ids;

	guint flush_id;

	/* which fields is listener interested in */
	GHashTable *fields_of_interest;
	gboolean send_uids_only;
};

enum {
	PROP_0,
	PROP_BACKEND,
	PROP_CONNECTION,
	PROP_OBJECT_PATH,
	PROP_SEXP,
	PROP_N_TOTAL,
	PROP_INDICES,
	N_PROPS
};

enum {
	OBJECTS_ADDED,
	OBJECTS_MODIFIED,
	OBJECTS_REMOVED,
	LAST_SIGNAL
};

static GParamSpec *properties[N_PROPS] = { NULL, };

static guint signals[LAST_SIGNAL];

/* Forward Declarations */
static void	e_data_book_view_initable_init	(GInitableIface *iface);

G_DEFINE_TYPE_WITH_CODE (
	EDataBookView,
	e_data_book_view,
	G_TYPE_OBJECT,
	G_ADD_PRIVATE (EDataBookView)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_data_book_view_initable_init))

static guint
str_ic_hash (gconstpointer key)
{
	guint32 hash = 5381;
	const gchar *str = key;
	gint ii;

	if (str == NULL)
		return hash;

	for (ii = 0; str[ii] != '\0'; ii++)
		hash = hash * 33 + g_ascii_tolower (str[ii]);

	return hash;
}

static gboolean
str_ic_equal (gconstpointer a,
              gconstpointer b)
{
	const gchar *stra = a;
	const gchar *strb = b;
	gint ii;

	if (stra == NULL && strb == NULL)
		return TRUE;

	if (stra == NULL || strb == NULL)
		return FALSE;

	for (ii = 0; stra[ii] != '\0' && strb[ii] != '\0'; ii++) {
		if (g_ascii_tolower (stra[ii]) != g_ascii_tolower (strb[ii]))
			return FALSE;
	}

	return stra[ii] == strb[ii];
}

static void
reset_array (GArray *array)
{
	gint i = 0;
	gchar *tmp = NULL;

	/* Free stored strings */
	for (i = 0; i < array->len; i++) {
		tmp = g_array_index (array, gchar *, i);
		g_free (tmp);
	}

	/* Force the array size to 0 */
	g_array_set_size (array, 0);
}

static void
send_pending_adds (EDataBookView *view)
{
	if (view->priv->adds->len == 0)
		return;

	if ((view->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0) {
		g_signal_emit (view, signals[OBJECTS_ADDED], 0, (const gchar * const *) view->priv->adds->data);
	} else {
		e_dbus_address_book_view_emit_objects_added (
			view->priv->dbus_object,
			(const gchar * const *) view->priv->adds->data);
	}

	reset_array (view->priv->adds);
}

static void
send_pending_changes (EDataBookView *view)
{
	if (view->priv->changes->len == 0)
		return;

	if ((view->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0) {
		g_signal_emit (view, signals[OBJECTS_MODIFIED], 0, (const gchar * const *) view->priv->changes->data);
	} else {
		e_dbus_address_book_view_emit_objects_modified (
			view->priv->dbus_object,
			(const gchar * const *) view->priv->changes->data);
	}

	reset_array (view->priv->changes);
}

static void
send_pending_removes (EDataBookView *view)
{
	if (view->priv->removes->len == 0)
		return;

	if ((view->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0) {
		g_signal_emit (view, signals[OBJECTS_REMOVED], 0, (const gchar * const *) view->priv->removes->data);
	} else {
		e_dbus_address_book_view_emit_objects_removed (
			view->priv->dbus_object,
			(const gchar * const *) view->priv->removes->data);
	}

	reset_array (view->priv->removes);
}

static gboolean
pending_flush_timeout_cb (gpointer data)
{
	EDataBookView *view = data;

	g_mutex_lock (&view->priv->pending_mutex);

	view->priv->flush_id = 0;

	if (!g_source_is_destroyed (g_main_current_source ())) {
		send_pending_adds (view);
		send_pending_changes (view);
		send_pending_removes (view);
	}

	g_mutex_unlock (&view->priv->pending_mutex);

	return FALSE;
}

static void
ensure_pending_flush_timeout (EDataBookView *view)
{
	if (view->priv->flush_id > 0)
		return;

	view->priv->flush_id = e_named_timeout_add_seconds (
		THRESHOLD_SECONDS, pending_flush_timeout_cb, view);
}

static gpointer
bookview_start_thread (gpointer data)
{
	EDataBookView *view = data;

	if (view->priv->running) {
		EBookBackend *backend = e_data_book_view_ref_backend (view);

		if (backend) {
			/* To avoid race condition when one thread is starting the view, while
			   another thread wants to notify about created/modified/removed objects. */
			e_book_backend_sexp_lock (view->priv->sexp);

			e_book_backend_start_view (backend, view);

			e_book_backend_sexp_unlock (view->priv->sexp);

			g_object_unref (backend);
		}
	}

	g_object_unref (view);

	return NULL;
}

static gboolean
impl_DataBookView_start (EDBusAddressBookView *object,
                         GDBusMethodInvocation *invocation,
                         EDataBookView *view)
{
	GThread *thread;

	view->priv->running = TRUE;
	view->priv->complete = FALSE;

	thread = g_thread_new (
		NULL, bookview_start_thread, g_object_ref (view));
	g_thread_unref (thread);

	e_dbus_address_book_view_complete_start (object, invocation);

	return TRUE;
}

static gpointer
bookview_stop_thread (gpointer data)
{
	EDataBookView *view = data;

	if (!view->priv->running) {
		EBookBackend *backend = e_data_book_view_ref_backend (view);

		if (backend) {
			e_book_backend_stop_view (backend, view);
			g_object_unref (backend);
		}
	}
	g_object_unref (view);

	return NULL;
}

static gboolean
impl_DataBookView_stop (EDBusAddressBookView *object,
                        GDBusMethodInvocation *invocation,
                        EDataBookView *view)
{
	GThread *thread;

	view->priv->running = FALSE;
	view->priv->complete = FALSE;

	thread = g_thread_new (
		NULL, bookview_stop_thread, g_object_ref (view));
	g_thread_unref (thread);

	e_dbus_address_book_view_complete_stop (object, invocation);

	return TRUE;
}

static gboolean
impl_DataBookView_setFlags (EDBusAddressBookView *object,
                            GDBusMethodInvocation *invocation,
                            EBookClientViewFlags flags,
                            EDataBookView *view)
{
	view->priv->flags = flags;

	e_dbus_address_book_view_complete_set_flags (object, invocation);

	return TRUE;
}

static gboolean
impl_DataBookView_dispose (EDBusAddressBookView *object,
                           GDBusMethodInvocation *invocation,
                           EDataBookView *view)
{
	EBookBackend *backend;

	e_dbus_address_book_view_complete_dispose (object, invocation);

	backend = e_data_book_view_ref_backend (view);

	if (backend) {
		e_book_backend_stop_view (backend, view);
		view->priv->running = FALSE;
		e_book_backend_remove_view (backend, view);

		g_object_unref (backend);
	} else {
		view->priv->running = FALSE;
	}

	return TRUE;
}

static gboolean
impl_DataBookView_set_fields_of_interest (EDBusAddressBookView *object,
                                          GDBusMethodInvocation *invocation,
                                          const gchar * const *in_fields_of_interest,
                                          EDataBookView *view)
{
	gint ii;

	g_return_val_if_fail (in_fields_of_interest != NULL, TRUE);

	g_clear_pointer (&view->priv->fields_of_interest, g_hash_table_destroy);

	view->priv->send_uids_only = FALSE;

	for (ii = 0; in_fields_of_interest[ii]; ii++) {
		const gchar *field = in_fields_of_interest[ii];

		if (!*field)
			continue;

		if (strcmp (field, "x-evolution-uids-only") == 0) {
			view->priv->send_uids_only = TRUE;
			continue;
		}

		if (view->priv->fields_of_interest == NULL)
			view->priv->fields_of_interest =
				g_hash_table_new_full (
					(GHashFunc) str_ic_hash,
					(GEqualFunc) str_ic_equal,
					(GDestroyNotify) g_free,
					(GDestroyNotify) NULL);

		g_hash_table_insert (
			view->priv->fields_of_interest,
			g_strdup (field), GINT_TO_POINTER (1));
	}

	e_dbus_address_book_view_complete_set_fields_of_interest (object, invocation);

	return TRUE;
}

static gboolean
impl_DataBookView_set_sort_fields (EDBusAddressBookView *object,
				   GDBusMethodInvocation *invocation,
				   GVariant *arg_fields,
				   EDataBookView *view)
{
	GVariantIter iter;
	guint fld, st, ii;
	EBookClientViewSortFields *fields;

	fields = g_new0 (EBookClientViewSortFields, g_variant_iter_init (&iter, arg_fields) + 1);

	for (ii = 0; g_variant_iter_next (&iter, "(uu)", &fld, &st); ii++) {
		fields[ii].field = (EContactField) fld;
		fields[ii].sort_type = (EBookCursorSortType) st;
	}

	fields[ii].field = E_CONTACT_FIELD_LAST;
	fields[ii].sort_type = E_BOOK_CURSOR_SORT_ASCENDING;

	e_data_book_view_set_sort_fields (view, fields);

	e_book_client_view_sort_fields_free (fields);

	e_dbus_address_book_view_complete_set_sort_fields (object, invocation);

	return TRUE;
}

static gboolean
impl_DataBookView_dup_contacts (EDBusAddressBookView *object,
				GDBusMethodInvocation *invocation,
				guint arg_range_start,
				guint arg_range_length,
				EDataBookView *view)
{
	GPtrArray *contacts;
	const gchar *const empty_array[] = { NULL };
	gchar **vcards;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), TRUE);

	contacts = e_data_book_view_dup_contacts (view, arg_range_start, arg_range_length);

	if (contacts) {
		guint ii;

		vcards = g_new0 (gchar *, contacts->len + 1);

		for (ii = 0; ii < contacts->len; ii++) {
			EContact *contact = g_ptr_array_index (contacts, ii);

			vcards[ii] = e_vcard_to_string (E_VCARD (contact));
		}

		g_ptr_array_unref (contacts);
	} else {
		vcards = NULL;
	}

	e_dbus_address_book_view_complete_dup_contacts (object, invocation, vcards ? (const gchar * const *) vcards : empty_array);

	g_strfreev (vcards);

	return TRUE;
}

static void
data_book_view_set_backend (EDataBookView *view,
                            EBookBackend *backend)
{
	g_return_if_fail (E_IS_BOOK_BACKEND (backend));

	g_weak_ref_set (&view->priv->backend_weakref, backend);
}

static void
data_book_view_set_connection (EDataBookView *view,
                               GDBusConnection *connection)
{
	g_return_if_fail (G_IS_DBUS_CONNECTION (connection));
	g_return_if_fail (view->priv->connection == NULL);

	view->priv->connection = g_object_ref (connection);
}

static void
data_book_view_set_object_path (EDataBookView *view,
                                const gchar *object_path)
{
	g_return_if_fail (object_path != NULL);
	g_return_if_fail (view->priv->object_path == NULL);

	view->priv->object_path = g_strdup (object_path);
}

static void
data_book_view_set_sexp (EDataBookView *view,
                         EBookBackendSExp *sexp)
{
	g_return_if_fail (E_IS_BOOK_BACKEND_SEXP (sexp));
	g_return_if_fail (view->priv->sexp == NULL);

	view->priv->sexp = g_object_ref (sexp);
}

static void
data_book_view_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			data_book_view_set_backend (
				E_DATA_BOOK_VIEW (object),
				g_value_get_object (value));
			return;

		case PROP_CONNECTION:
			data_book_view_set_connection (
				E_DATA_BOOK_VIEW (object),
				g_value_get_object (value));
			return;

		case PROP_OBJECT_PATH:
			data_book_view_set_object_path (
				E_DATA_BOOK_VIEW (object),
				g_value_get_string (value));
			return;

		case PROP_SEXP:
			data_book_view_set_sexp (
				E_DATA_BOOK_VIEW (object),
				g_value_get_object (value));
			return;

		case PROP_N_TOTAL:
			e_data_book_view_set_n_total (
				E_DATA_BOOK_VIEW (object),
				g_value_get_uint (value));
			return;

		case PROP_INDICES:
			e_data_book_view_set_indices (
				E_DATA_BOOK_VIEW (object),
				g_value_get_boxed (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_book_view_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_BACKEND:
			g_value_take_object (
				value,
				e_data_book_view_ref_backend (
				E_DATA_BOOK_VIEW (object)));
			return;

		case PROP_CONNECTION:
			g_value_set_object (
				value,
				e_data_book_view_get_connection (
				E_DATA_BOOK_VIEW (object)));
			return;

		case PROP_OBJECT_PATH:
			g_value_set_string (
				value,
				e_data_book_view_get_object_path (
				E_DATA_BOOK_VIEW (object)));
			return;

		case PROP_SEXP:
			g_value_set_object (
				value,
				e_data_book_view_get_sexp (
				E_DATA_BOOK_VIEW (object)));
			return;

		case PROP_N_TOTAL:
			g_value_set_uint (
				value,
				e_data_book_view_get_n_total (
				E_DATA_BOOK_VIEW (object)));
			return;

		case PROP_INDICES:
			g_value_take_boxed (
				value,
				e_data_book_view_dup_indices (
				E_DATA_BOOK_VIEW (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
data_book_view_dispose (GObject *object)
{
	EDataBookViewPrivate *priv;

	priv = E_DATA_BOOK_VIEW (object)->priv;

	g_mutex_lock (&priv->pending_mutex);

	if (priv->flush_id > 0) {
		g_source_remove (priv->flush_id);
		priv->flush_id = 0;
	}

	g_mutex_unlock (&priv->pending_mutex);

	g_clear_object (&priv->connection);
	g_clear_object (&priv->dbus_object);
	g_clear_object (&priv->sexp);

	g_weak_ref_set (&priv->backend_weakref, NULL);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_data_book_view_parent_class)->dispose (object);
}

static void
data_book_view_finalize (GObject *object)
{
	EDataBookViewPrivate *priv;

	priv = E_DATA_BOOK_VIEW (object)->priv;

	g_free (priv->object_path);

	reset_array (priv->adds);
	reset_array (priv->changes);
	reset_array (priv->removes);
	g_array_free (priv->adds, TRUE);
	g_array_free (priv->changes, TRUE);
	g_array_free (priv->removes, TRUE);

	if (priv->fields_of_interest)
		g_hash_table_destroy (priv->fields_of_interest);

	g_mutex_clear (&priv->pending_mutex);
	g_weak_ref_clear (&priv->backend_weakref);

	g_hash_table_destroy (priv->ids);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_data_book_view_parent_class)->finalize (object);
}

static gboolean
data_book_view_initable_init (GInitable *initable,
                              GCancellable *cancellable,
                              GError **error)
{
	EDataBookView *view;

	view = E_DATA_BOOK_VIEW (initable);

	return g_dbus_interface_skeleton_export (
		G_DBUS_INTERFACE_SKELETON (view->priv->dbus_object),
		view->priv->connection,
		view->priv->object_path,
		error);
}

static void
e_data_book_view_class_init (EDataBookViewClass *class)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = data_book_view_set_property;
	object_class->get_property = data_book_view_get_property;
	object_class->dispose = data_book_view_dispose;
	object_class->finalize = data_book_view_finalize;

	/**
	 * EDataBookView:backend
	 *
	 * The backend being monitored
	 **/
	properties[PROP_BACKEND] =
		g_param_spec_object (
			"backend",
			NULL, NULL,
			E_TYPE_BOOK_BACKEND,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * EDataBookView:connection
	 *
	 * The #GDBusConnection on which to export the view interface
	 **/
	properties[PROP_CONNECTION] =
		g_param_spec_object (
			"connection",
			NULL, NULL,
			G_TYPE_DBUS_CONNECTION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * EDataBookView:object-path
	 *
	 * The object path at which to export the view interface
	 **/
	properties[PROP_OBJECT_PATH] =
		g_param_spec_string (
			"object-path",
			NULL, NULL,
			NULL,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * EDataBookView:sexp
	 *
	 * The query expression for this view
	 **/
	properties[PROP_SEXP] =
		g_param_spec_object (
			"sexp",
			NULL, NULL,
			E_TYPE_BOOK_BACKEND_SEXP,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * EDataBookView:n-total
	 *
	 * How many contacts are available in the view
	 **/
	properties[PROP_N_TOTAL] =
		g_param_spec_uint (
			"n-total", NULL, NULL,
			0, G_MAXUINT, 0,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS);

	/**
	 * EDataBookView:indices
	 *
	 * List of #EBookIndices holding indices of the contacts in the view
	 **/
	properties[PROP_INDICES] =
		g_param_spec_pointer (
			"indices", NULL, NULL,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * EDataBookView::objects-added:
	 * @view: an #EDataBookView which emitted the signal
	 * @vcards: (array zero-terminated=1): array of vCards as string
	 *
	 * Emitted when new objects are added into the @view.
	 *
	 * Note: This signal is used only when the view has
	 *    set @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY flag.
	 *
	 * Since: 3.50
	 **/
	signals[OBJECTS_ADDED] = g_signal_new (
		"objects-added",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		0,
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_STRV);

	/**
	 * EDataBookView::objects-modified:
	 * @view: an #EDataBookView which emitted the signal
	 * @vcards: (array zero-terminated=1): array of vCards as string
	 *
	 * Emitted when the objects in the @view are modified.
	 *
	 * Note: This signal is used only when the view has
	 *    set @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY flag.
	 *
	 * Since: 3.50
	 **/
	signals[OBJECTS_MODIFIED] = g_signal_new (
		"objects-modified",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		0,
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_STRV);

	/**
	 * EDataBookView::objects-removed:
	 * @view: an #EDataBookView which emitted the signal
	 * @uids: (array zero-terminated=1): array of UIDs as string
	 *
	 * Emitted when the objects are removed from the @view.
	 *
	 * Note: This signal is used only when the view has
	 *    set @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY flag.
	 *
	 * Since: 3.50
	 **/
	signals[OBJECTS_REMOVED] = g_signal_new (
		"objects-removed",
		G_OBJECT_CLASS_TYPE (object_class),
		G_SIGNAL_RUN_LAST,
		0,
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_STRV);
}

static void
e_data_book_view_initable_init (GInitableIface *iface)
{
	iface->init = data_book_view_initable_init;
}

static void
e_data_book_view_init (EDataBookView *view)
{
	view->priv = e_data_book_view_get_instance_private (view);

	g_weak_ref_init (&view->priv->backend_weakref, NULL);

	view->priv->flags = E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL;

	view->priv->dbus_object = e_dbus_address_book_view_skeleton_new ();
	g_signal_connect (
		view->priv->dbus_object, "handle-start",
		G_CALLBACK (impl_DataBookView_start), view);
	g_signal_connect (
		view->priv->dbus_object, "handle-stop",
		G_CALLBACK (impl_DataBookView_stop), view);
	g_signal_connect (
		view->priv->dbus_object, "handle-set-flags",
		G_CALLBACK (impl_DataBookView_setFlags), view);
	g_signal_connect (
		view->priv->dbus_object, "handle-dispose",
		G_CALLBACK (impl_DataBookView_dispose), view);
	g_signal_connect (
		view->priv->dbus_object, "handle-set-fields-of-interest",
		G_CALLBACK (impl_DataBookView_set_fields_of_interest), view);
	g_signal_connect (
		view->priv->dbus_object, "handle-set-sort-fields",
		G_CALLBACK (impl_DataBookView_set_sort_fields), view);
	g_signal_connect (
		view->priv->dbus_object, "handle-dup-contacts",
		G_CALLBACK (impl_DataBookView_dup_contacts), view);

	view->priv->fields_of_interest = NULL;
	view->priv->running = FALSE;
	view->priv->complete = FALSE;
	g_mutex_init (&view->priv->pending_mutex);

	/* THRESHOLD_ITEMS * 2 because we store UID and vcard */
	view->priv->adds = g_array_sized_new (
		TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS * 2);
	view->priv->changes = g_array_sized_new (
		TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS * 2);
	view->priv->removes = g_array_sized_new (
		TRUE, TRUE, sizeof (gchar *), THRESHOLD_ITEMS);

	view->priv->ids = g_hash_table_new_full (
		(GHashFunc) g_str_hash,
		(GEqualFunc) g_str_equal,
		(GDestroyNotify) g_free,
		(GDestroyNotify) NULL);

	view->priv->flush_id = 0;

	e_dbus_address_book_view_set_id (view->priv->dbus_object, (guint64) GPOINTER_TO_SIZE (view));
}

/**
 * e_data_book_view_new:
 * @backend: (type EBookBackend): an #EBookBackend
 * @sexp: an #EBookBackendSExp
 * @connection: a #GDBusConnection
 * @object_path: an object path for the view
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #EDataBookView and exports its D-Bus interface on
 * @connection at @object_path.  If an error occurs while exporting,
 * the function sets @error and returns %NULL.
 *
 * Returns: an #EDataBookView or %NULL on error
 */
EDataBookView *
e_data_book_view_new (EBookBackend *backend,
                      EBookBackendSExp *sexp,
                      GDBusConnection *connection,
                      const gchar *object_path,
                      GError **error)
{
	g_return_val_if_fail (E_IS_BOOK_BACKEND (backend), NULL);
	g_return_val_if_fail (E_IS_BOOK_BACKEND_SEXP (sexp), NULL);
	g_return_val_if_fail (G_IS_DBUS_CONNECTION (connection), NULL);
	g_return_val_if_fail (object_path != NULL, NULL);

	return g_initable_new (
		E_TYPE_DATA_BOOK_VIEW, NULL, error,
		"backend", backend,
		"connection", connection,
		"object-path", object_path,
		"sexp", sexp, NULL);
}

/**
 * e_data_book_view_ref_backend:
 * @view: an #EDataBookView
 *
 * Refs the backend that @view is querying. Unref the returned backend,
 * if not %NULL, with g_object_unref(), when no longer needed.
 *
 * Returns: (type EBookBackend) (transfer full) (nullable): The associated #EBookBackend.
 *
 * Since: 3.34
 **/
EBookBackend *
e_data_book_view_ref_backend (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	return g_weak_ref_get (&view->priv->backend_weakref);
}

/**
 * e_data_book_view_get_backend:
 * @view: an #EDataBookView
 *
 * Gets the backend that @view is querying.
 *
 * Returns: (type EBookBackend) (transfer none): The associated #EBookBackend.
 *
 * Deprecated: 3.34: Use e_data_book_view_ref_backend() instead.
 **/
EBookBackend *
e_data_book_view_get_backend (EDataBookView *view)
{
	EBookBackend *backend;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	backend = e_data_book_view_ref_backend (view);
	if (backend)
		g_object_unref (backend);

	return backend;
}

/**
 * e_data_book_view_get_sexp:
 * @view: an #EDataBookView
 *
 * Gets the s-expression used for matching contacts to @view.
 *
 * Returns: (transfer none): The #EBookBackendSExp used.
 *
 * Since: 3.8
 **/
EBookBackendSExp *
e_data_book_view_get_sexp (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	return view->priv->sexp;
}

/**
 * e_data_book_view_get_connection:
 * @view: an #EDataBookView
 *
 * Returns the #GDBusConnection on which the AddressBookView D-Bus
 * interface is exported.
 *
 * Returns: (transfer none): the #GDBusConnection
 *
 * Since: 3.8
 **/
GDBusConnection *
e_data_book_view_get_connection (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	return view->priv->connection;
}

/**
 * e_data_book_view_get_object_path:
 * @view: an #EDataBookView
 *
 * Returns the object path at which the AddressBookView D-Bus interface
 * is exported.
 *
 * Returns: the object path
 *
 * Since: 3.8
 **/
const gchar *
e_data_book_view_get_object_path (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	return view->priv->object_path;
}

/**
 * e_data_book_view_get_flags:
 * @view: an #EDataBookView
 *
 * Gets the #EBookClientViewFlags that control the behaviour of @view.
 *
 * Returns: the flags for @view.
 *
 * Since: 3.4
 **/
EBookClientViewFlags
e_data_book_view_get_flags (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), 0);

	return view->priv->flags;
}

/**
 * e_data_book_view_get_force_initial_notifications:
 * @view: an #EDataBookView
 *
 * Returns whether the @view should do initial notifications
 * even when the flags do not contain %E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL.
 * The default is %FALSE.
 *
 * Returns: value set by e_data_book_view_set_force_initial_notifications()
 *
 * Since: 3.50
 **/
gboolean
e_data_book_view_get_force_initial_notifications (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), FALSE);

	return view->priv->force_initial_notifications;
}

/**
 * e_data_book_view_set_force_initial_notifications:
 * @view: an #EDataBookView
 * @value: value to set
 *
 * Sets whether the @view should do initial notifications
 * even when the flags do not contain %E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL.
 *
 * Since: 3.50
 **/
void
e_data_book_view_set_force_initial_notifications (EDataBookView *view,
						  gboolean value)
{
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));

	view->priv->force_initial_notifications = value;
}

/**
 * e_data_book_view_is_completed:
 * @view: an #EDataBookView
 *
 * Returns: whether the @view had been completed; that is,
 *    whether e_data_book_view_notify_complete() had been called
 *    since the @view had been started.
 *
 * Since: 3.34
 **/
gboolean
e_data_book_view_is_completed (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), FALSE);

	return view->priv->complete;
}

/*
 * Queue @vcard and @id to be sent as a change notification.
 */
static void
notify_change (EDataBookView *view,
               const gchar *id,
               const gchar *vcard)
{
	gchar *utf8_vcard, *utf8_id;

	send_pending_adds (view);
	send_pending_removes (view);

	if (view->priv->changes->len == THRESHOLD_ITEMS * 2) {
		send_pending_changes (view);
	}

	if (!view->priv->send_uids_only || (view->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0) {
		utf8_vcard = e_util_utf8_make_valid (vcard);
		g_array_append_val (view->priv->changes, utf8_vcard);
	}

	utf8_id = e_util_utf8_make_valid (id);
	g_array_append_val (view->priv->changes, utf8_id);

	ensure_pending_flush_timeout (view);
}

/*
 * Queue @id to be sent as a change notification.
 */
static void
notify_remove (EDataBookView *view,
               const gchar *id)
{
	gchar *valid_id;

	send_pending_adds (view);
	send_pending_changes (view);

	if (view->priv->removes->len == THRESHOLD_ITEMS) {
		send_pending_removes (view);
	}

	valid_id = e_util_utf8_make_valid (id);
	g_array_append_val (view->priv->removes, valid_id);
	g_hash_table_remove (view->priv->ids, valid_id);

	ensure_pending_flush_timeout (view);
}

/*
 * Queue @vcard and @id to be sent as a change notification.
 */
static void
notify_add (EDataBookView *view,
            const gchar *id,
            const gchar *vcard)
{
	EBookClientViewFlags flags;
	gchar *utf8_vcard, *utf8_id;

	send_pending_changes (view);
	send_pending_removes (view);

	utf8_id = e_util_utf8_make_valid (id);

	/* Do not send contact add notifications during initial stage */
	flags = e_data_book_view_get_flags (view);
	if (view->priv->complete || view->priv->force_initial_notifications || (flags & E_BOOK_CLIENT_VIEW_FLAGS_NOTIFY_INITIAL) != 0) {
		gchar *utf8_id_copy = g_strdup (utf8_id);

		if (view->priv->adds->len == THRESHOLD_ITEMS) {
			send_pending_adds (view);
		}

		if (!view->priv->send_uids_only || (flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY) != 0) {
			utf8_vcard = e_util_utf8_make_valid (vcard);
			g_array_append_val (view->priv->adds, utf8_vcard);
		}

		g_array_append_val (view->priv->adds, utf8_id_copy);

		ensure_pending_flush_timeout (view);
	}

	g_hash_table_insert (view->priv->ids, utf8_id, GUINT_TO_POINTER (1));
}

static gboolean
id_is_in_view (EDataBookView *view,
               const gchar *id)
{
	gchar *valid_id;
	gboolean res;

	g_return_val_if_fail (view != NULL, FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	valid_id = e_util_utf8_make_valid (id);
	res = g_hash_table_lookup (view->priv->ids, valid_id) != NULL;
	g_free (valid_id);

	return res;
}

/**
 * e_data_book_view_notify_update:
 * @view: an #EDataBookView
 * @contact: an #EContact
 *
 * Notify listeners that @contact has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @view.
 **/
void
e_data_book_view_notify_update (EDataBookView *view,
                                const EContact *contact)
{
	gboolean currently_in_view, want_in_view;
	const gchar *id;
	gchar *vcard;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));
	g_return_if_fail (E_IS_CONTACT (contact));

	if (!view->priv->running)
		return;

	g_mutex_lock (&view->priv->pending_mutex);

	id = e_contact_get_const ((EContact *) contact, E_CONTACT_UID);

	currently_in_view = id_is_in_view (view, id);
	want_in_view = e_book_backend_sexp_match_contact (
		view->priv->sexp, (EContact *) contact);

	if (want_in_view) {
		vcard = e_vcard_to_string (E_VCARD (contact));

		if (currently_in_view)
			notify_change (view, id, vcard);
		else
			notify_add (view, id, vcard);

		g_free (vcard);
	} else {
		if (currently_in_view)
			notify_remove (view, id);
		/* else nothing; we're removing a card that wasn't there */
	}

	g_mutex_unlock (&view->priv->pending_mutex);
}

/**
 * e_data_book_view_notify_update_vcard:
 * @view: an #EDataBookView
 * @id: a unique id of the @vcard
 * @vcard: a plain vCard
 *
 * Notify listeners that @vcard has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @view.  This method should be preferred over
 * e_data_book_view_notify_update() when the native
 * representation of a contact is a vCard.
 **/
void
e_data_book_view_notify_update_vcard (EDataBookView *view,
                                      const gchar *id,
                                      const gchar *vcard)
{
	gboolean currently_in_view, want_in_view;
	EContact *contact;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));
	g_return_if_fail (id != NULL);
	g_return_if_fail (vcard != NULL);

	if (!view->priv->running)
		return;

	g_mutex_lock (&view->priv->pending_mutex);

	contact = e_contact_new_from_vcard_with_uid (vcard, id);
	currently_in_view = id_is_in_view (view, id);
	want_in_view = e_book_backend_sexp_match_contact (
		view->priv->sexp, contact);

	if (want_in_view) {
		if (currently_in_view)
			notify_change (view, id, vcard);
		else
			notify_add (view, id, vcard);
	} else {
		if (currently_in_view)
			notify_remove (view, id);
	}

	/* Do this last so that id is still valid when notify_ is called */
	g_object_unref (contact);

	g_mutex_unlock (&view->priv->pending_mutex);
}

/**
 * e_data_book_view_notify_update_prefiltered_vcard:
 * @view: an #EDataBookView
 * @id: the UID of this contact
 * @vcard: a plain vCard
 *
 * Notify listeners that @vcard has changed. This can
 * trigger an add, change or removal event depending on
 * whether the change causes the contact to start matching,
 * no longer match, or stay matching the query specified
 * by @view.  This method should be preferred over
 * e_data_book_view_notify_update() when the native
 * representation of a contact is a vCard.
 *
 * The important difference between this method and
 * e_data_book_view_notify_update() and
 * e_data_book_view_notify_update_vcard() is
 * that it doesn't match the contact against the book view query to see if it
 * should be included, it assumes that this has been done and the contact is
 * known to exist in the view.
 **/
void
e_data_book_view_notify_update_prefiltered_vcard (EDataBookView *view,
                                                  const gchar *id,
                                                  const gchar *vcard)
{
	gboolean currently_in_view;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));
	g_return_if_fail (id != NULL);
	g_return_if_fail (vcard != NULL);

	if (!view->priv->running)
		return;

	g_mutex_lock (&view->priv->pending_mutex);

	currently_in_view = id_is_in_view (view, id);

	if (currently_in_view)
		notify_change (view, id, vcard);
	else
		notify_add (view, id, vcard);

	g_mutex_unlock (&view->priv->pending_mutex);
}

/**
 * e_data_book_view_notify_remove:
 * @view: an #EDataBookView
 * @id: a unique contact ID
 *
 * Notify listeners that a contact specified by @id
 * was removed from @view.
 **/
void
e_data_book_view_notify_remove (EDataBookView *view,
                                const gchar *id)
{
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));
	g_return_if_fail (id != NULL);

	if (!view->priv->running)
		return;

	g_mutex_lock (&view->priv->pending_mutex);

	if (id_is_in_view (view, id))
		notify_remove (view, id);

	g_mutex_unlock (&view->priv->pending_mutex);
}

/**
 * e_data_book_view_notify_complete:
 * @view: an #EDataBookView
 * @error: the error of the query, if any
 *
 * Notifies listeners that all pending updates on @view
 * have been sent. The listener's information should now be
 * in sync with the backend's.
 **/
void
e_data_book_view_notify_complete (EDataBookView *view,
                                  const GError *error)
{
	gchar *error_name, *error_message;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));

	if (!view->priv->running)
		return;

	/* View is complete */
	view->priv->complete = TRUE;

	g_mutex_lock (&view->priv->pending_mutex);

	send_pending_adds (view);
	send_pending_changes (view);
	send_pending_removes (view);

	g_mutex_unlock (&view->priv->pending_mutex);

	if (error) {
		gchar *dbus_error_name = g_dbus_error_encode_gerror (error);

		error_name = e_util_utf8_make_valid (dbus_error_name ? dbus_error_name : "");
		error_message = e_util_utf8_make_valid (error->message);

		g_free (dbus_error_name);
	} else {
		error_name = g_strdup ("");
		error_message = g_strdup ("");
	}

	e_dbus_address_book_view_emit_complete (
		view->priv->dbus_object,
		error_name,
		error_message);

	g_free (error_name);
	g_free (error_message);

	e_util_call_malloc_trim ();
}

/**
 * e_data_book_view_notify_progress:
 * @view: an #EDataBookView
 * @percent: percent done; use -1 when not available
 * @message: a text message
 *
 * Provides listeners with a human-readable text describing the
 * current backend operation. This can be used for progress
 * reporting.
 *
 * Since: 3.2
 **/
void
e_data_book_view_notify_progress (EDataBookView *view,
                                  guint percent,
                                  const gchar *message)
{
	gchar *dbus_message = NULL;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW (view));

	if (!view->priv->running)
		return;

	e_dbus_address_book_view_emit_progress (
		view->priv->dbus_object, percent,
		e_util_ensure_gdbus_string (message, &dbus_message));

	g_free (dbus_message);
}

/**
 * e_data_book_view_get_fields_of_interest:
 * @view: an #EDataBookView
 *
 * Returns: (transfer none) (element-type utf8 gint) (nullable): Hash table of field names which
 * the listener is interested in. Backends can return fully populated objects, but the listener
 * advertised that it will use only these. Returns %NULL for all available fields.
 *
 * Note: The data pointer in the hash table has no special meaning, it's
 * only GINT_TO_POINTER(1) for easier checking. Also, field names are
 * compared case insensitively.
 **/
GHashTable *
e_data_book_view_get_fields_of_interest (EDataBookView *view)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (view), NULL);

	return view->priv->fields_of_interest;
}

/**
 * e_data_book_view_get_id:
 * @self: an #EDataBookView
 *
 * Returns an identifier of the @self. It does not change
 * for the whole life time of the @self.
 *
 * Note: This function can be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY.
 *
 * Returns: an identifier of the view
 *
 * Since: 3.50
 **/
gsize
e_data_book_view_get_id (EDataBookView *self)
{
	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (self), 0);

	return GPOINTER_TO_SIZE (self);
}

/**
 * e_data_book_view_set_sort_fields:
 * @self: an #EDataBookView
 * @fields: an array of #EBookClientViewSortFields fields to sort by
 *
 * Sets @fields to sort the view by. The default is to sort by the file-as
 * field. The contacts are always sorted in ascending order. Not every field
 * can be used for sorting, the default available fields are %E_CONTACT_FILE_AS,
 * %E_CONTACT_GIVEN_NAME and %E_CONTACT_FAMILY_NAME.
 *
 * The first sort field is used to populate indices, as returned
 * by e_data_book_view_dup_indices().
 *
 * Note: This function can be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY.
 *
 * Since: 3.50
 **/
void
e_data_book_view_set_sort_fields (EDataBookView *self,
				  const EBookClientViewSortFields *fields)
{
	EBookBackend *backend;

	g_return_if_fail (E_IS_DATA_BOOK_VIEW (self));

	if (!(self->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY))
		return;

	backend = e_data_book_view_ref_backend (self);
	if (backend)
		e_book_backend_set_view_sort_fields (backend, e_data_book_view_get_id (self), fields);

	g_clear_object (&backend);
}

/**
 * e_data_book_view_get_n_total:
 * @self: an #EDataBookView
 *
 * Returns how many contacts are available in the view.
 *
 * Note: This function can be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY.
 *
 * Returns: how many contacts are available in the view
 *
 * Since: 3.50
 **/
guint
e_data_book_view_get_n_total (EDataBookView *self)
{
	guint n_total;
	EBookBackend *backend;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (self), 0);

	if (!(self->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY))
		return 0;

	backend = e_data_book_view_ref_backend (self);
	n_total = backend ? e_book_backend_get_view_n_total (backend, e_data_book_view_get_id (self)) : 0;

	g_clear_object (&backend);

	return n_total;
}

/**
 * e_data_book_view_set_n_total:
 * @self: an #EDataBookView
 * @n_total: a value to set
 *
 * Sets how many contacts are available in the view.
 *
 * Note: This function can be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY.
 *
 * Since: 3.50
 **/
void
e_data_book_view_set_n_total (EDataBookView *self,
			      guint n_total)
{
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (self));

	if (!(self->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY))
		return;

	e_dbus_address_book_view_set_n_total (self->priv->dbus_object, n_total);
}

/**
 * e_data_book_view_dup_indices:
 * @self: an #EDataBookView
 *
 * Returns a list of #EBookIndices holding indices of the contacts
 * in the view. These are received from the first sort field set by
 * e_data_book_view_set_sort_fields(). The last item of the returned
 * array is the one with chr member being %NULL.
 *
 * Free the returned array with e_book_indices_free(), when no longer needed.
 *
 * Note: This function can be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY.
 *
 * Returns: (transfer full) (nullable): list of indices for the view,
 *    or %NULL when cannot determine
 *
 * Since: 3.50
 **/
EBookIndices *
e_data_book_view_dup_indices (EDataBookView *self)
{
	EBookIndices *indices;
	EBookBackend *backend;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (self), NULL);

	if (!(self->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY))
		return NULL;

	backend = e_data_book_view_ref_backend (self);
	indices = backend ? e_book_backend_dup_view_indices (backend, e_data_book_view_get_id (self)) : NULL;

	g_clear_object (&backend);

	return indices;
}

/**
 * e_data_book_view_set_indices:
 * @self: an #EDataBookView
 * @indices: an array of #EBookIndices
 *
 * Sets indices used by the @self. The array is terminated by an item
 * with chr member being %NULL.
 * See e_data_book_view_dup_indices() for more information.
 *
 * Note: This function can be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY.
 *
 * Since: 3.50
 **/
void
e_data_book_view_set_indices (EDataBookView *self,
			      const EBookIndices *indices)
{
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (self));
	g_return_if_fail (indices != NULL);

	if (!(self->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY))
		return;

	if (indices) {
		GVariantBuilder builder;
		guint ii;

		g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(su)"));

		for (ii = 0; indices[ii].chr != NULL; ii++) {
			g_variant_builder_add (&builder, "(su)", indices[ii].chr, indices[ii].index);
		}

		e_dbus_address_book_view_set_indices (self->priv->dbus_object, g_variant_builder_end (&builder));
	} else {
		e_dbus_address_book_view_set_indices (self->priv->dbus_object, NULL);
	}
}

/**
 * e_data_book_view_dup_contacts:
 * @self: an #EDataBookView
 * @range_start: 0-based range start to retrieve the contacts for
 * @range_length: how many contacts to retrieve
 *
 * Reads @range_length contacts from index @range_start.
 * When there are asked more than e_data_book_view_get_n_total()
 * contacts only those up to the total number of contacts are read.
 *
 * Free the returned #GPtrArray with g_ptr_array_unref(),
 * when no longer needed.
 *
 * Note: This function can be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY.
 *
 * Returns: (transfer container) (element-type EContact) (nullable): array of the read contacts,
 *    or %NULL, when not applicable or when the @range_start it out of bounds.
 *
 * Since: 3.50
 **/
GPtrArray *
e_data_book_view_dup_contacts (EDataBookView *self,
			       guint range_start,
			       guint range_length)
{
	GPtrArray *contacts;
	EBookBackend *backend;

	g_return_val_if_fail (E_IS_DATA_BOOK_VIEW (self), NULL);

	if (!(self->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY))
		return NULL;

	backend = e_data_book_view_ref_backend (self);
	contacts = backend ? e_book_backend_dup_view_contacts (backend, e_data_book_view_get_id (self), range_start, range_length) : NULL;

	g_clear_object (&backend);

	return contacts;
}

/**
 * e_data_book_view_notify_content_changed:
 * @self: an #EDataBookView
 *
 * Notifies the client side that the content of the @self changed,
 * which it should use to refresh the view data.
 *
 * Note: This function can be used only with @E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY.
 *
 * Since: 3.50
 **/
void
e_data_book_view_notify_content_changed (EDataBookView *self)
{
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (self));

	if (!(self->priv->flags & E_BOOK_CLIENT_VIEW_FLAGS_MANUAL_QUERY))
		return;

	e_dbus_address_book_view_emit_content_changed (self->priv->dbus_object);
}

/**
 * e_data_book_view_claim_contact_uid:
 * @self: an #EDataBookView
 * @uid: a contact UID
 *
 * Tells the @self, that it contains a contact with UID @uid. This is useful
 * for "manual query" view, which do not do initial notifications. It helps
 * to not send "objects-added" signal for contacts, which are already part
 * of the @self, because for them the "objects-modified" should be emitted
 * instead.
 *
 * Since: 3.50
 **/
void
e_data_book_view_claim_contact_uid (EDataBookView *self,
				    const gchar *uid)
{
	g_return_if_fail (E_IS_DATA_BOOK_VIEW (self));
	g_return_if_fail (uid != NULL);

	g_hash_table_insert (self->priv->ids, e_util_utf8_make_valid (uid), GUINT_TO_POINTER (1));
}
