/*
 * e-cal-client.c
 *
 * Copyright (C) 2011 Red Hat, Inc. (www.redhat.com)
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
 */

/**
 * SECTION: e-cal-client
 * @include: libecal/libecal.h
 * @short_description: Accessing and modifying a calendar 
 *
 * This class is the main user facing API for accessing and modifying
 * the calendar.
 **/

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>
#include <gio/gio.h>

/* Private D-Bus classes. */
#include <e-dbus-calendar.h>
#include <e-dbus-calendar-factory.h>

#include <libedataserver/e-client-private.h>

#include "e-cal-client.h"
#include "e-cal-component.h"
#include "e-cal-check-timezones.h"
#include "e-cal-enumtypes.h"
#include "e-cal-time-util.h"
#include "e-cal-enums.h"
#include "e-timezone-cache.h"

/* Set this to a sufficiently large value
 * to cover most long-running operations. */
#define DBUS_PROXY_TIMEOUT_MS (3 * 60 * 1000)  /* 3 minutes */

typedef struct _AsyncContext AsyncContext;
typedef struct _SignalClosure SignalClosure;
typedef struct _ConnectClosure ConnectClosure;
typedef struct _RunInThreadClosure RunInThreadClosure;
typedef struct _SendObjectResult SendObjectResult;

struct _ECalClientPrivate {
	EDBusCalendar *dbus_proxy;
	guint name_watcher_id;

	ECalClientSourceType source_type;
	ICalTimezone *default_zone;

	GMutex zone_cache_lock;
	GHashTable *zone_cache;

	gulong dbus_proxy_error_handler_id;
	gulong dbus_proxy_notify_handler_id;
	gulong dbus_proxy_free_busy_data_handler_id;
};

struct _AsyncContext {
	ICalComponent *in_comp;
	ICalTimezone *zone;
	GSList *comp_list;
	GSList *string_list;
	GSList *ids_list; /* ECalComponentId * */
	gchar *uid;
	gchar *rid;
	gchar *auid;
	ECalObjModType mod;
	time_t start;
	time_t end;
	guint32 opflags;
};

struct _SignalClosure {
	GWeakRef client;
	gchar *property_name;
	gchar *error_message;
	gchar **free_busy_data;
	ICalTimezone *cached_zone;
};

struct _ConnectClosure {
	ESource *source;
	GCancellable *cancellable;
	guint32 wait_for_connected_seconds;
};

struct _RunInThreadClosure {
	GTaskThreadFunc func;
	GTask *task;
};

struct _SendObjectResult {
	GSList *users;
	ICalComponent *modified_icalcomp;
};

enum {
	PROP_0,
	PROP_DEFAULT_TIMEZONE,
	PROP_SOURCE_TYPE,
	N_PROPS
};

enum {
	FREE_BUSY_DATA,
	LAST_SIGNAL
};

/* Forward Declarations */
static void	e_cal_client_initable_init
					(GInitableIface *iface);
static void	e_cal_client_async_initable_init
					(GAsyncInitableIface *iface);
static void	e_cal_client_timezone_cache_init
					(ETimezoneCacheInterface *iface);

static GParamSpec *properties[N_PROPS] = { NULL, };

static guint signals[LAST_SIGNAL];

G_DEFINE_TYPE_WITH_CODE (
	ECalClient,
	e_cal_client,
	E_TYPE_CLIENT,
	G_ADD_PRIVATE (ECalClient)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_cal_client_initable_init)
	G_IMPLEMENT_INTERFACE (
		G_TYPE_ASYNC_INITABLE,
		e_cal_client_async_initable_init)
	G_IMPLEMENT_INTERFACE (
		E_TYPE_TIMEZONE_CACHE,
		e_cal_client_timezone_cache_init))

static void
async_context_free (AsyncContext *async_context)
{
	g_clear_object (&async_context->in_comp);
	g_clear_object (&async_context->zone);

	g_slist_free_full (async_context->comp_list, g_object_unref);
	g_slist_free_full (async_context->string_list, g_free);
	g_slist_free_full (async_context->ids_list, e_cal_component_id_free);

	g_free (async_context->uid);
	g_free (async_context->rid);
	g_free (async_context->auid);

	g_slice_free (AsyncContext, async_context);
}

static void
signal_closure_free (SignalClosure *signal_closure)
{
	g_weak_ref_clear (&signal_closure->client);

	g_free (signal_closure->property_name);
	g_free (signal_closure->error_message);

	g_strfreev (signal_closure->free_busy_data);
	g_clear_object (&signal_closure->cached_zone);

	/* The ICalTimezone is cached in ECalClient's internal
	 * "zone_cache" hash table and must not be freed here. */

	g_slice_free (SignalClosure, signal_closure);
}

static void
connect_closure_free (ConnectClosure *connect_closure)
{
	g_clear_object (&connect_closure->source);

	g_slice_free (ConnectClosure, connect_closure);
}

static void
run_in_thread_closure_free (RunInThreadClosure *run_in_thread_closure)
{
	g_clear_object (&run_in_thread_closure->task);

	g_slice_free (RunInThreadClosure, run_in_thread_closure);
}

/*
 * Well-known calendar backend properties:
 * @E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS: Contains default calendar's email
 *   address suggested by the backend.
 * @E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS: Contains default alarm email
 *   address suggested by the backend.
 * @E_CAL_BACKEND_PROPERTY_DEFAULT_OBJECT: Contains iCal component string
 *   of an #ICalComponent with the default values for properties needed.
 *   Preferred way of retrieving this property is by
 *   calling e_cal_client_get_default_object().
 *
 * See also: @CLIENT_BACKEND_PROPERTY_OPENED, @CLIENT_BACKEND_PROPERTY_OPENING,
 *   @CLIENT_BACKEND_PROPERTY_ONLINE, @CLIENT_BACKEND_PROPERTY_READONLY
 *   @CLIENT_BACKEND_PROPERTY_CACHE_DIR, @CLIENT_BACKEND_PROPERTY_CAPABILITIES
 */

G_DEFINE_QUARK (e-cal-client-error-quark, e_cal_client_error)

/**
 * e_cal_client_error_to_string:
 * @code: an #ECalClientError error code
 *
 * Get localized human readable description of the given error code.
 *
 * Returns: Localized human readable description of the given error code
 *
 * Since: 3.2
 **/
const gchar *
e_cal_client_error_to_string (ECalClientError code)
{
	switch (code) {
	case E_CAL_CLIENT_ERROR_NO_SUCH_CALENDAR:
		return _("No such calendar");
	case E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND:
		return _("Object not found");
	case E_CAL_CLIENT_ERROR_INVALID_OBJECT:
		return _("Invalid object");
	case E_CAL_CLIENT_ERROR_UNKNOWN_USER:
		return _("Unknown user");
	case E_CAL_CLIENT_ERROR_OBJECT_ID_ALREADY_EXISTS:
		return _("Object ID already exists");
	case E_CAL_CLIENT_ERROR_INVALID_RANGE:
		return _("Invalid range");
	}

	return _("Unknown error");
}

/**
 * e_cal_client_error_create:
 * @code: an #ECalClientError code to create
 * @custom_msg: (nullable): custom message to use for the error; can be %NULL
 *
 * Returns: (transfer full): a new #GError containing an #E_CAL_CLIENT_ERROR of the given
 *    @code. If the @custom_msg is NULL, then the error message is the one returned
 *    from e_cal_client_error_to_string() for the @code, otherwise the given message is used.
 *    Returned pointer should be freed with g_error_free().
 *
 * Since: 3.2
 **/
GError *
e_cal_client_error_create (ECalClientError code,
			   const gchar *custom_msg)
{
	if (!custom_msg)
		custom_msg = e_cal_client_error_to_string (code);

	return g_error_new_literal (E_CAL_CLIENT_ERROR, code, custom_msg);
}

/**
 * e_cal_client_error_create_fmt:
 * @code: an #ECalClientError
 * @format: (nullable): message format, or %NULL to use the default message for the @code
 * @...: arguments for the format
 *
 * Similar as e_cal_client_error_create(), only here, instead of custom_msg,
 * is used a printf() format to create a custom message for the error.
 *
 * Returns: (transfer full): a newly allocated #GError, which should be
 *   freed with g_error_free(), when no longer needed.
 *   The #GError has set the custom message, or the default message for
 *   @code, when @format is %NULL.
 *
 * Since: 3.34
 **/
GError *
e_cal_client_error_create_fmt (ECalClientError code,
			       const gchar *format,
			       ...)
{
	GError *error;
	gchar *custom_msg;
	va_list ap;

	if (!format)
		return e_cal_client_error_create (code, NULL);

	va_start (ap, format);
	custom_msg = g_strdup_vprintf (format, ap);
	va_end (ap);

	error = e_cal_client_error_create (code, custom_msg);

	g_free (custom_msg);

	return error;
}

static gpointer
cal_client_dbus_thread (gpointer user_data)
{
	GMainContext *main_context = user_data;
	GMainLoop *main_loop;

	g_main_context_push_thread_default (main_context);

	main_loop = g_main_loop_new (main_context, FALSE);
	g_main_loop_run (main_loop);
	g_main_loop_unref (main_loop);

	g_main_context_pop_thread_default (main_context);

	g_main_context_unref (main_context);

	return NULL;
}

static gpointer
cal_client_dbus_thread_init (gpointer unused)
{
	GMainContext *main_context;

	main_context = g_main_context_new ();

	/* This thread terminates when the process itself terminates, so
	 * no need to worry about unreferencing the returned GThread. */
	g_thread_new (
		"cal-client-dbus-thread",
		cal_client_dbus_thread,
		g_main_context_ref (main_context));

	return main_context;
}

static GMainContext *
cal_client_ref_dbus_main_context (void)
{
	static GOnce cal_client_dbus_thread_once = G_ONCE_INIT;

	g_once (
		&cal_client_dbus_thread_once,
		cal_client_dbus_thread_init, NULL);

	return g_main_context_ref (cal_client_dbus_thread_once.retval);
}

static gboolean
cal_client_run_in_dbus_thread_idle_cb (gpointer user_data)
{
	RunInThreadClosure *closure = user_data;
	GTask *task;

	task = G_TASK (closure->task);

	closure->func (
		task,
		g_task_get_source_object (task),
		g_task_get_task_data (task),
		g_task_get_cancellable (task));

	return G_SOURCE_REMOVE;
}

static void
cal_client_run_in_dbus_thread (GTask *task,
                               GTaskThreadFunc func)
{
	RunInThreadClosure *closure;
	GMainContext *main_context;
	GSource *idle_source;

	main_context = cal_client_ref_dbus_main_context ();

	closure = g_slice_new0 (RunInThreadClosure);
	closure->func = func;
	closure->task = g_object_ref (task);

	idle_source = g_idle_source_new ();
	g_source_set_priority (idle_source, g_task_get_priority (task));
	g_source_set_callback (
		idle_source, cal_client_run_in_dbus_thread_idle_cb,
		closure, (GDestroyNotify) run_in_thread_closure_free);
	g_source_attach (idle_source, main_context);
	g_source_unref (idle_source);

	g_main_context_unref (main_context);
}

static gboolean
cal_client_emit_backend_died_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	EClient *client;

	client = g_weak_ref_get (&signal_closure->client);

	if (client != NULL) {
		g_signal_emit_by_name (client, "backend-died");
		g_object_unref (client);
	}

	return FALSE;
}

static gboolean
cal_client_emit_backend_error_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	EClient *client;

	client = g_weak_ref_get (&signal_closure->client);

	if (client != NULL) {
		g_signal_emit_by_name (
			client, "backend-error",
			signal_closure->error_message);
		g_object_unref (client);
	}

	return FALSE;
}

static gboolean
cal_client_emit_backend_property_changed_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	EClient *client;

	client = g_weak_ref_get (&signal_closure->client);

	if (client != NULL) {
		gchar *prop_value = NULL;

		/* XXX Despite appearances, this function does not block. */
		e_client_get_backend_property_sync (
			client,
			signal_closure->property_name,
			&prop_value, NULL, NULL);

		if (prop_value != NULL) {
			g_signal_emit_by_name (
				client,
				"backend-property-changed",
				signal_closure->property_name,
				prop_value);
			g_free (prop_value);
		}

		g_object_unref (client);
	}

	return FALSE;
}

static gboolean
cal_client_emit_free_busy_data_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	EClient *client;

	client = g_weak_ref_get (&signal_closure->client);

	if (client != NULL) {
		GSList *list = NULL;
		gchar **strv;
		gint ii;

		strv = signal_closure->free_busy_data;

		for (ii = 0; strv[ii] != NULL; ii++) {
			ECalComponent *comp;
			ICalComponent *icalcomp;
			ICalComponentKind kind;

			icalcomp = i_cal_component_new_from_string (strv[ii]);
			if (icalcomp == NULL)
				continue;

			kind = i_cal_component_isa (icalcomp);
			if (kind != I_CAL_VFREEBUSY_COMPONENT) {
				i_cal_component_free (icalcomp);
				continue;
			}

			comp = e_cal_component_new_from_icalcomponent (icalcomp);
			if (comp)
				list = g_slist_prepend (list, comp);
		}

		list = g_slist_reverse (list);

		g_signal_emit (client, signals[FREE_BUSY_DATA], 0, list);

		g_slist_free_full (list, (GDestroyNotify) g_object_unref);

		g_object_unref (client);
	}

	return FALSE;
}

static gboolean
cal_client_emit_timezone_added_idle_cb (gpointer user_data)
{
	SignalClosure *signal_closure = user_data;
	EClient *client;

	client = g_weak_ref_get (&signal_closure->client);

	if (client != NULL) {
		g_signal_emit_by_name (
			client, "timezone-added",
			signal_closure->cached_zone);
		g_object_unref (client);
	}

	return FALSE;
}

static void
cal_client_dbus_proxy_error_cb (EDBusCalendar *dbus_proxy,
                                const gchar *error_message,
                                GWeakRef *client_weak_ref)
{
	EClient *client;

	client = g_weak_ref_get (client_weak_ref);

	if (client != NULL) {
		GSource *idle_source;
		GMainContext *main_context;
		SignalClosure *signal_closure;

		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_init (&signal_closure->client, client);
		signal_closure->error_message = g_strdup (error_message);

		main_context = e_client_ref_main_context (client);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			cal_client_emit_backend_error_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, main_context);
		g_source_unref (idle_source);

		g_main_context_unref (main_context);

		g_object_unref (client);
	}
}

static void
cal_client_dbus_proxy_property_changed (EClient *client,
					const gchar *property_name,
					const GValue *value,
					gboolean is_in_main_thread)
{
	const gchar *backend_prop_name = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (property_name != NULL);

	if (g_str_equal (property_name, "alarm-email-address")) {
		backend_prop_name = E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS;
	}

	if (g_str_equal (property_name, "cache-dir")) {
		backend_prop_name = CLIENT_BACKEND_PROPERTY_CACHE_DIR;
	}

	if (g_str_equal (property_name, "cal-email-address")) {
		backend_prop_name = E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS;
	}

	if (g_str_equal (property_name, "capabilities")) {
		gchar **strv;
		gchar *csv = NULL;

		backend_prop_name = CLIENT_BACKEND_PROPERTY_CAPABILITIES;

		strv = g_value_get_boxed (value);
		if (strv != NULL) {
			csv = g_strjoinv (",", strv);
		}
		e_client_set_capabilities (client, csv);
		g_free (csv);
	}

	if (g_str_equal (property_name, "default-object")) {
		backend_prop_name = E_CAL_BACKEND_PROPERTY_DEFAULT_OBJECT;
	}

	if (g_str_equal (property_name, "online")) {
		gboolean online;

		backend_prop_name = CLIENT_BACKEND_PROPERTY_ONLINE;

		online = g_value_get_boolean (value);
		e_client_set_online (client, online);
	}

	if (g_str_equal (property_name, "revision")) {
		backend_prop_name = CLIENT_BACKEND_PROPERTY_REVISION;
	}

	if (g_str_equal (property_name, "writable")) {
		gboolean writable;

		backend_prop_name = CLIENT_BACKEND_PROPERTY_READONLY;

		writable = g_value_get_boolean (value);
		e_client_set_readonly (client, !writable);
	}

	if (backend_prop_name != NULL) {
		SignalClosure *signal_closure;

		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_init (&signal_closure->client, client);
		signal_closure->property_name = g_strdup (backend_prop_name);

		if (is_in_main_thread) {
			cal_client_emit_backend_property_changed_idle_cb (signal_closure);
			signal_closure_free (signal_closure);
		} else {
			GSource *idle_source;
			GMainContext *main_context;

			main_context = e_client_ref_main_context (client);

			idle_source = g_idle_source_new ();
			g_source_set_callback (
				idle_source,
				cal_client_emit_backend_property_changed_idle_cb,
				signal_closure,
				(GDestroyNotify) signal_closure_free);
			g_source_attach (idle_source, main_context);
			g_source_unref (idle_source);

			g_main_context_unref (main_context);
		}
	}

}

typedef struct {
	EClient *client;
	gchar *property_name;
	GValue property_value;
} IdleProxyNotifyData;

static void
idle_proxy_notify_data_free (gpointer ptr)
{
	IdleProxyNotifyData *ipn = ptr;

	if (ipn) {
		g_clear_object (&ipn->client);
		g_free (ipn->property_name);
		g_value_unset (&ipn->property_value);
		g_slice_free (IdleProxyNotifyData, ipn);
	}
}

static gboolean
cal_client_proxy_notify_idle_cb (gpointer user_data)
{
	IdleProxyNotifyData *ipn = user_data;

	g_return_val_if_fail (ipn != NULL, FALSE);

	cal_client_dbus_proxy_property_changed (ipn->client, ipn->property_name, &ipn->property_value, TRUE);

	return FALSE;
}

static void
cal_client_dbus_proxy_notify_cb (EDBusCalendar *dbus_proxy,
                                 GParamSpec *pspec,
                                 GWeakRef *client_weak_ref)
{
	EClient *client;
	GSource *idle_source;
	GMainContext *main_context;
	IdleProxyNotifyData *ipn;

	client = g_weak_ref_get (client_weak_ref);
	if (client == NULL)
		return;

	ipn = g_slice_new0 (IdleProxyNotifyData);
	ipn->client = g_object_ref (client);
	ipn->property_name = g_strdup (pspec->name);
	g_value_init (&ipn->property_value, pspec->value_type);
	g_object_get_property (G_OBJECT (dbus_proxy), pspec->name, &ipn->property_value);

	main_context = e_client_ref_main_context (client);

	idle_source = g_idle_source_new ();
	g_source_set_callback (idle_source, cal_client_proxy_notify_idle_cb,
		ipn, idle_proxy_notify_data_free);
	g_source_attach (idle_source, main_context);
	g_source_unref (idle_source);

	g_main_context_unref (main_context);
	g_object_unref (client);
}

static void
cal_client_dbus_proxy_free_busy_data_cb (EDBusCalendar *dbus_proxy,
                                         gchar **free_busy_data,
                                         EClient *client)
{
	GSource *idle_source;
	GMainContext *main_context;
	SignalClosure *signal_closure;

	signal_closure = g_slice_new0 (SignalClosure);
	g_weak_ref_init (&signal_closure->client, client);
	signal_closure->free_busy_data = g_strdupv (free_busy_data);

	main_context = e_client_ref_main_context (client);

	idle_source = g_idle_source_new ();
	g_source_set_callback (
		idle_source,
		cal_client_emit_free_busy_data_idle_cb,
		signal_closure,
		(GDestroyNotify) signal_closure_free);
	g_source_attach (idle_source, main_context);
	g_source_unref (idle_source);

	g_main_context_unref (main_context);
}

static void
cal_client_name_vanished_cb (GDBusConnection *connection,
                             const gchar *name,
                             GWeakRef *client_weak_ref)
{
	EClient *client;

	client = g_weak_ref_get (client_weak_ref);

	if (client != NULL) {
		GSource *idle_source;
		GMainContext *main_context;
		SignalClosure *signal_closure;

		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_init (&signal_closure->client, client);

		main_context = e_client_ref_main_context (client);

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			cal_client_emit_backend_died_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, main_context);
		g_source_unref (idle_source);

		g_main_context_unref (main_context);

		g_object_unref (client);
	}
}

static void
cal_client_set_source_type (ECalClient *cal_client,
                            ECalClientSourceType source_type)
{
	cal_client->priv->source_type = source_type;
}

static void
cal_client_set_property (GObject *object,
                         guint property_id,
                         const GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DEFAULT_TIMEZONE:
			e_cal_client_set_default_timezone (
				E_CAL_CLIENT (object),
				g_value_get_object (value));
			return;

		case PROP_SOURCE_TYPE:
			cal_client_set_source_type (
				E_CAL_CLIENT (object),
				g_value_get_enum (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_client_get_property (GObject *object,
                         guint property_id,
                         GValue *value,
                         GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_DEFAULT_TIMEZONE:
			g_value_set_object (
				value,
				e_cal_client_get_default_timezone (
				E_CAL_CLIENT (object)));
			return;

		case PROP_SOURCE_TYPE:
			g_value_set_enum (
				value,
				e_cal_client_get_source_type (
				E_CAL_CLIENT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cal_client_dispose (GObject *object)
{
	ECalClientPrivate *priv;

	priv = E_CAL_CLIENT (object)->priv;

	if (priv->dbus_proxy_error_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->dbus_proxy_error_handler_id);
		priv->dbus_proxy_error_handler_id = 0;
	}

	if (priv->dbus_proxy_notify_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->dbus_proxy_notify_handler_id);
		priv->dbus_proxy_notify_handler_id = 0;
	}

	if (priv->dbus_proxy_free_busy_data_handler_id > 0) {
		g_signal_handler_disconnect (
			priv->dbus_proxy,
			priv->dbus_proxy_free_busy_data_handler_id);
		priv->dbus_proxy_free_busy_data_handler_id = 0;
	}

	if (priv->dbus_proxy != NULL) {
		/* Call close() asynchronously so we don't block dispose().
		 * Also omit a callback function, so the GDBusMessage uses
		 * G_DBUS_MESSAGE_FLAGS_NO_REPLY_EXPECTED. */
		e_dbus_calendar_call_close (
			priv->dbus_proxy, NULL, NULL, NULL);
		g_object_unref (priv->dbus_proxy);
		priv->dbus_proxy = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_cal_client_parent_class)->dispose (object);
}

static void
cal_client_finalize (GObject *object)
{
	ECalClientPrivate *priv;

	priv = E_CAL_CLIENT (object)->priv;

	if (priv->name_watcher_id > 0)
		g_bus_unwatch_name (priv->name_watcher_id);

	if (priv->default_zone && priv->default_zone != i_cal_timezone_get_utc_timezone ())
		g_clear_object (&priv->default_zone);

	g_mutex_lock (&priv->zone_cache_lock);
	g_hash_table_destroy (priv->zone_cache);
	g_mutex_unlock (&priv->zone_cache_lock);

	g_mutex_clear (&priv->zone_cache_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_cal_client_parent_class)->finalize (object);
}

static void
cal_client_process_properties (ECalClient *cal_client,
			       gchar * const *props)
{
	GObject *dbus_proxy;
	GObjectClass *object_class;
	gint ii;

	g_return_if_fail (E_IS_CAL_CLIENT (cal_client));

	dbus_proxy = G_OBJECT (cal_client->priv->dbus_proxy);
	g_return_if_fail (G_IS_OBJECT (dbus_proxy));

	if (!props)
		return;

	object_class = G_OBJECT_GET_CLASS (dbus_proxy);

	for (ii = 0; props[ii]; ii++) {
		if (!(ii & 1) && props[ii + 1]) {
			GParamSpec *param;
			GVariant *expected = NULL;

			param = g_object_class_find_property (object_class, props[ii]);
			if (param) {
				#define WORKOUT(gvl, gvr) \
					if (g_type_is_a (param->value_type, G_TYPE_ ## gvl)) { \
						expected = g_variant_parse (G_VARIANT_TYPE_ ## gvr, props[ii + 1], NULL, NULL, NULL); \
					}

				WORKOUT (BOOLEAN, BOOLEAN);
				WORKOUT (STRING, STRING);
				WORKOUT (STRV, STRING_ARRAY);
				WORKOUT (UCHAR, BYTE);
				WORKOUT (INT, INT32);
				WORKOUT (UINT, UINT32);
				WORKOUT (INT64, INT64);
				WORKOUT (UINT64, UINT64);
				WORKOUT (DOUBLE, DOUBLE);

				#undef WORKOUT
			}

			/* Update the property always, even when the current value on the GDBusProxy
			   matches the expected value, because sometimes the proxy can have up-to-date
			   values, but still not propagated into EClient properties. */
			if (expected) {
				GValue value = G_VALUE_INIT;

				g_dbus_gvariant_to_gvalue (expected, &value);

				cal_client_dbus_proxy_property_changed (E_CLIENT (cal_client), param->name, &value, FALSE);

				g_value_unset (&value);
				g_variant_unref (expected);
			}
		}
	}
}

static GDBusProxy *
cal_client_get_dbus_proxy (EClient *client)
{
	ECalClientPrivate *priv;

	priv = E_CAL_CLIENT (client)->priv;

	return G_DBUS_PROXY (priv->dbus_proxy);
}

static gboolean
cal_client_get_backend_property_sync (EClient *client,
                                      const gchar *prop_name,
                                      gchar **prop_value,
                                      GCancellable *cancellable,
                                      GError **error)
{
	ECalClient *cal_client;
	EDBusCalendar *dbus_proxy;
	gchar **strv;

	cal_client = E_CAL_CLIENT (client);
	dbus_proxy = cal_client->priv->dbus_proxy;

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENED)) {
		*prop_value = g_strdup ("TRUE");
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_OPENING)) {
		*prop_value = g_strdup ("FALSE");
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_ONLINE)) {
		if (e_dbus_calendar_get_online (dbus_proxy))
			*prop_value = g_strdup ("TRUE");
		else
			*prop_value = g_strdup ("FALSE");
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_READONLY)) {
		if (e_dbus_calendar_get_writable (dbus_proxy))
			*prop_value = g_strdup ("FALSE");
		else
			*prop_value = g_strdup ("TRUE");
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CACHE_DIR)) {
		*prop_value = e_dbus_calendar_dup_cache_dir (dbus_proxy);
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_REVISION)) {
		*prop_value = e_dbus_calendar_dup_revision (dbus_proxy);
		return TRUE;
	}

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		strv = e_dbus_calendar_dup_capabilities (dbus_proxy);
		if (strv != NULL)
			*prop_value = g_strjoinv (",", strv);
		else
			*prop_value = g_strdup ("");
		g_strfreev (strv);
		return TRUE;
	}

	if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		*prop_value = e_dbus_calendar_dup_alarm_email_address (dbus_proxy);
		return TRUE;
	}

	if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS)) {
		*prop_value = e_dbus_calendar_dup_cal_email_address (dbus_proxy);
		return TRUE;
	}

	if (g_str_equal (prop_name, E_CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		*prop_value = e_dbus_calendar_dup_default_object (dbus_proxy);
		return TRUE;
	}

	g_set_error (
		error, E_CLIENT_ERROR, E_CLIENT_ERROR_NOT_SUPPORTED,
		_("Unknown calendar property “%s”"), prop_name);

	return FALSE;
}

static gboolean
cal_client_set_backend_property_sync (EClient *client,
                                      const gchar *prop_name,
                                      const gchar *prop_value,
                                      GCancellable *cancellable,
                                      GError **error)
{
	g_set_error (
		error, E_CLIENT_ERROR,
		E_CLIENT_ERROR_NOT_SUPPORTED,
		_("Cannot change value of calendar property “%s”"),
		prop_name);

	return FALSE;
}

static gboolean
cal_client_open_sync (EClient *client,
                      gboolean only_if_exists,
                      GCancellable *cancellable,
                      GError **error)
{
	ECalClient *cal_client;
	gchar **props = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	cal_client = E_CAL_CLIENT (client);

	e_dbus_calendar_call_open_sync (
		cal_client->priv->dbus_proxy, &props, cancellable, &local_error);

	cal_client_process_properties (cal_client, props);
	g_strfreev (props);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
cal_client_refresh_sync (EClient *client,
                         GCancellable *cancellable,
                         GError **error)
{
	ECalClient *cal_client;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	cal_client = E_CAL_CLIENT (client);

	e_dbus_calendar_call_refresh_sync (
		cal_client->priv->dbus_proxy, cancellable, &local_error);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static gboolean
cal_client_retrieve_properties_sync (EClient *client,
				     GCancellable *cancellable,
				     GError **error)
{
	ECalClient *cal_client;
	gchar **props = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	cal_client = E_CAL_CLIENT (client);

	e_dbus_calendar_call_retrieve_properties_sync (cal_client->priv->dbus_proxy, &props, cancellable, &local_error);

	cal_client_process_properties (cal_client, props);
	g_strfreev (props);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static void
cal_client_init_in_dbus_thread (GTask *task,
                                gpointer source_object,
                                gpointer task_data,
                                GCancellable *cancellable)
{
	ECalClientPrivate *priv;
	EDBusCalendarFactory *factory_proxy;
	GDBusConnection *connection;
	GDBusProxy *proxy;
	EClient *client;
	ESource *source;
	const gchar *uid;
	gchar *object_path = NULL;
	gchar *bus_name = NULL;
	gulong handler_id;
	GError *local_error = NULL;

	priv = E_CAL_CLIENT (source_object)->priv;

	client = E_CLIENT (source_object);
	source = e_client_get_source (client);
	uid = e_source_get_uid (source);

	connection = g_bus_get_sync (
		G_BUS_TYPE_SESSION, cancellable, &local_error);

	/* Sanity check. */
	g_return_if_fail (
		((connection != NULL) && (local_error == NULL)) ||
		((connection == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		return;
	}

	factory_proxy = e_dbus_calendar_factory_proxy_new_sync (
		connection,
		G_DBUS_PROXY_FLAGS_NONE,
		CALENDAR_DBUS_SERVICE_NAME,
		"/org/gnome/evolution/dataserver/CalendarFactory",
		cancellable, &local_error);

	/* Sanity check. */
	g_return_if_fail (
		((factory_proxy != NULL) && (local_error == NULL)) ||
		((factory_proxy == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		g_object_unref (connection);
		return;
	}

	switch (e_cal_client_get_source_type (E_CAL_CLIENT (client))) {
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
			e_dbus_calendar_factory_call_open_calendar_sync (
				factory_proxy, uid, &object_path, &bus_name,
				cancellable, &local_error);
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
			e_dbus_calendar_factory_call_open_task_list_sync (
				factory_proxy, uid, &object_path, &bus_name,
				cancellable, &local_error);
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
			e_dbus_calendar_factory_call_open_memo_list_sync (
				factory_proxy, uid, &object_path, &bus_name,
				cancellable, &local_error);
			break;
		default:
			g_return_if_reached ();
	}

	g_object_unref (factory_proxy);

	/* Sanity check. */
	g_return_if_fail (
		(((object_path != NULL) || (bus_name != NULL)) && (local_error == NULL)) ||
		(((object_path == NULL) || (bus_name == NULL)) && (local_error != NULL)));

	if (local_error) {
		g_dbus_error_strip_remote_error (local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		g_object_unref (connection);
		return;
	}

	e_client_set_bus_name (client, bus_name);

	priv->dbus_proxy = e_dbus_calendar_proxy_new_sync (
		connection,
		G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
		bus_name, object_path, cancellable, &local_error);

	g_free (object_path);
	g_free (bus_name);

	/* Sanity check. */
	g_return_if_fail (
		((priv->dbus_proxy != NULL) && (local_error == NULL)) ||
		((priv->dbus_proxy == NULL) && (local_error != NULL)));

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
		g_object_unref (connection);
		return;
	}

	/* Configure our new GDBusProxy. */

	proxy = G_DBUS_PROXY (priv->dbus_proxy);

	g_dbus_proxy_set_default_timeout (proxy, DBUS_PROXY_TIMEOUT_MS);

	priv->name_watcher_id = g_bus_watch_name_on_connection (
		connection,
		g_dbus_proxy_get_name (proxy),
		G_BUS_NAME_WATCHER_FLAGS_NONE,
		(GBusNameAppearedCallback) NULL,
		(GBusNameVanishedCallback) cal_client_name_vanished_cb,
		e_weak_ref_new (client),
		(GDestroyNotify) e_weak_ref_free);

	handler_id = g_signal_connect_data (
		proxy, "error",
		G_CALLBACK (cal_client_dbus_proxy_error_cb),
		e_weak_ref_new (client),
		(GClosureNotify) e_weak_ref_free,
		0);
	priv->dbus_proxy_error_handler_id = handler_id;

	handler_id = g_signal_connect_data (
		proxy, "notify",
		G_CALLBACK (cal_client_dbus_proxy_notify_cb),
		e_weak_ref_new (client),
		(GClosureNotify) e_weak_ref_free,
		0);
	priv->dbus_proxy_notify_handler_id = handler_id;

	handler_id = g_signal_connect_object (
		proxy, "free-busy-data",
		G_CALLBACK (cal_client_dbus_proxy_free_busy_data_cb),
		client, 0);
	priv->dbus_proxy_free_busy_data_handler_id = handler_id;

	/* Initialize our public-facing GObject properties. */
	g_object_notify (G_OBJECT (proxy), "online");
	g_object_notify (G_OBJECT (proxy), "writable");
	g_object_notify (G_OBJECT (proxy), "capabilities");

	g_object_unref (connection);
	g_task_return_boolean (task, TRUE);
}

static gboolean
cal_client_initable_init (GInitable *initable,
                          GCancellable *cancellable,
                          GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	closure = e_async_closure_new ();

	g_async_initable_init_async (
		G_ASYNC_INITABLE (initable),
		G_PRIORITY_DEFAULT, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = g_async_initable_init_finish (
		G_ASYNC_INITABLE (initable), result, error);

	e_async_closure_free (closure);

	return success;
}

static void
cal_client_initable_init_async (GAsyncInitable *initable,
                                gint io_priority,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
	GTask *task;

	task = g_task_new (initable, cancellable, callback, user_data);
	g_task_set_source_tag (task, cal_client_initable_init_async);
	g_task_set_priority (task, io_priority);

	cal_client_run_in_dbus_thread (task, cal_client_init_in_dbus_thread);

	g_object_unref (task);
}

static gboolean
cal_client_initable_init_finish (GAsyncInitable *initable,
                                 GAsyncResult *result,
                                 GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, initable), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, cal_client_initable_init_async), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

static void
cal_client_add_cached_timezone (ETimezoneCache *cache,
                                ICalTimezone *zone)
{
	ECalClientPrivate *priv;
	const gchar *tzid;

	priv = E_CAL_CLIENT (cache)->priv;

	/* XXX Apparently this function can sometimes return NULL.
	 *     I'm not sure when or why that happens, but we can't
	 *     cache the ICalTimezone if it has no tzid string. */
	tzid = i_cal_timezone_get_tzid (zone);
	if (tzid == NULL)
		return;

	g_mutex_lock (&priv->zone_cache_lock);

	/* Avoid replacing an existing cache entry.  We don't want to
	 * invalidate any ICalTimezone pointers that may have already
	 * been returned through e_timezone_cache_get_timezone(). */
	if (!g_hash_table_contains (priv->zone_cache, tzid)) {
		GSource *idle_source;
		GMainContext *main_context;
		SignalClosure *signal_closure;
		ICalTimezone *cached_zone;

		cached_zone = e_cal_util_copy_timezone (zone);

		g_hash_table_insert (
			priv->zone_cache,
			g_strdup (tzid), cached_zone);

		/* The closure's client reference will keep the
		 * internally cached ICalTimezone alive for the
		 * duration of the idle callback. */
		signal_closure = g_slice_new0 (SignalClosure);
		g_weak_ref_init (&signal_closure->client, cache);
		signal_closure->cached_zone = g_object_ref (cached_zone);

		main_context = e_client_ref_main_context (E_CLIENT (cache));

		idle_source = g_idle_source_new ();
		g_source_set_callback (
			idle_source,
			cal_client_emit_timezone_added_idle_cb,
			signal_closure,
			(GDestroyNotify) signal_closure_free);
		g_source_attach (idle_source, main_context);
		g_source_unref (idle_source);

		g_main_context_unref (main_context);
	}

	g_mutex_unlock (&priv->zone_cache_lock);
}

static ICalTimezone *
cal_client_get_cached_timezone (ETimezoneCache *cache,
                                const gchar *tzid)
{
	ECalClientPrivate *priv;
	ICalTimezone *zone = NULL;
	ICalTimezone *builtin_zone = NULL;
	ICalComponent *icalcomp, *clone;
	ICalProperty *prop;
	const gchar *builtin_tzid;

	priv = E_CAL_CLIENT (cache)->priv;

	if (g_str_equal (tzid, "UTC"))
		return i_cal_timezone_get_utc_timezone ();

	g_mutex_lock (&priv->zone_cache_lock);

	/* See if we already have it in the cache. */
	zone = g_hash_table_lookup (priv->zone_cache, tzid);

	if (zone != NULL)
		goto exit;

	/* Try to replace the original time zone with a more complete
	 * and/or potentially updated built-in time zone.  Note this also
	 * applies to TZIDs which match built-in time zones exactly: they
	 * are extracted via i_cal_timezone_get_builtin_timezone_from_tzid()
	 * below without a roundtrip to the backend. */

	builtin_tzid = e_cal_match_tzid (tzid);

	if (builtin_tzid != NULL)
		builtin_zone = i_cal_timezone_get_builtin_timezone_from_tzid (builtin_tzid);

	if (builtin_zone == NULL)
		goto exit;

	/* Use the built-in time zone *and* rename it.  Likely the caller
	 * is asking for a specific TZID because it has an event with such
	 * a TZID.  Returning an ICalTimezone with a different TZID would
	 * lead to broken VCALENDARs in the caller. */

	icalcomp = i_cal_timezone_get_component (builtin_zone);
	clone = i_cal_component_clone (icalcomp);
	g_object_unref (icalcomp);
	icalcomp = clone;

	for (prop = i_cal_component_get_first_property (icalcomp, I_CAL_ANY_PROPERTY);
	     prop;
	     g_object_unref (prop), prop = i_cal_component_get_next_property (icalcomp, I_CAL_ANY_PROPERTY)) {
		if (i_cal_property_isa (prop) == I_CAL_TZID_PROPERTY) {
			i_cal_property_set_value_from_string (prop, tzid, "NO");
			g_object_unref (prop);
			break;
		}
	}

	zone = i_cal_timezone_new ();
	if (i_cal_timezone_set_component (zone, icalcomp)) {
		tzid = i_cal_timezone_get_tzid (zone);
		g_hash_table_insert (priv->zone_cache, g_strdup (tzid), zone);
	} else {
		g_object_unref (zone);
		zone = NULL;
	}
	g_object_unref (icalcomp);

exit:
	g_mutex_unlock (&priv->zone_cache_lock);

	return zone;
}

static GList *
cal_client_list_cached_timezones (ETimezoneCache *cache)
{
	ECalClientPrivate *priv;
	GList *list;

	priv = E_CAL_CLIENT (cache)->priv;

	g_mutex_lock (&priv->zone_cache_lock);

	list = g_hash_table_get_values (priv->zone_cache);

	g_mutex_unlock (&priv->zone_cache_lock);

	return list;
}

static void
e_cal_client_class_init (ECalClientClass *class)
{
	GObjectClass *object_class;
	EClientClass *client_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = cal_client_set_property;
	object_class->get_property = cal_client_get_property;
	object_class->dispose = cal_client_dispose;
	object_class->finalize = cal_client_finalize;

	client_class = E_CLIENT_CLASS (class);
	client_class->get_dbus_proxy = cal_client_get_dbus_proxy;
	client_class->get_backend_property_sync = cal_client_get_backend_property_sync;
	client_class->set_backend_property_sync = cal_client_set_backend_property_sync;
	client_class->open_sync = cal_client_open_sync;
	client_class->refresh_sync = cal_client_refresh_sync;
	client_class->retrieve_properties_sync = cal_client_retrieve_properties_sync;

	/**
	 * ECalClient:default-timezone
	 *
	 * Timezone used to resolve DATE and floating DATE-TIME values
	 **/
	properties[PROP_DEFAULT_TIMEZONE] =
		g_param_spec_object (
			"default-timezone",
			NULL, NULL,
			I_CAL_TYPE_TIMEZONE,
			G_PARAM_READWRITE |
			G_PARAM_EXPLICIT_NOTIFY |
			G_PARAM_STATIC_STRINGS);

	/**
	 * ECalClient:source-type
	 *
	 * The iCalendar data type
	 **/
	properties[PROP_SOURCE_TYPE] =
		g_param_spec_enum (
			"source-type",
			NULL, NULL,
			E_TYPE_CAL_CLIENT_SOURCE_TYPE,
			E_CAL_CLIENT_SOURCE_TYPE_EVENTS,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY |
			G_PARAM_STATIC_STRINGS);

	g_object_class_install_properties (object_class, N_PROPS, properties);

	/**
	 * ECalClient::free-busy-data
	 * @client: A calendar client.
	 * @free_busy_ecalcomps: (type GSList<ECalComponent>):
	 **/
	signals[FREE_BUSY_DATA] = g_signal_new (
		"free-busy-data",
		G_OBJECT_CLASS_TYPE (class),
		G_SIGNAL_RUN_FIRST,
		G_STRUCT_OFFSET (ECalClientClass, free_busy_data),
		NULL, NULL, NULL,
		G_TYPE_NONE, 1,
		G_TYPE_POINTER);
}

static void
e_cal_client_initable_init (GInitableIface *iface)
{
	iface->init = cal_client_initable_init;
}

static void
e_cal_client_async_initable_init (GAsyncInitableIface *iface)
{
	iface->init_async = cal_client_initable_init_async;
	iface->init_finish = cal_client_initable_init_finish;
}

static void
e_cal_client_timezone_cache_init (ETimezoneCacheInterface *iface)
{
	iface->tzcache_add_timezone = cal_client_add_cached_timezone;
	iface->tzcache_get_timezone = cal_client_get_cached_timezone;
	iface->tzcache_list_timezones = cal_client_list_cached_timezones;
}

static void
e_cal_client_init (ECalClient *client)
{
	GHashTable *zone_cache;

	zone_cache = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);

	client->priv = e_cal_client_get_instance_private (client);
	client->priv->source_type = E_CAL_CLIENT_SOURCE_TYPE_LAST;
	client->priv->default_zone = i_cal_timezone_get_utc_timezone ();
	g_mutex_init (&client->priv->zone_cache_lock);
	client->priv->zone_cache = zone_cache;
}

/**
 * e_cal_client_connect_sync:
 * @source: an #ESource
 * @source_type: source type of the calendar
 * @wait_for_connected_seconds: timeout, in seconds, to wait for the backend to be fully connected
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Creates a new #ECalClient for @source and @source_type.  If an error
 * occurs, the function will set @error and return %FALSE.
 *
 * The @wait_for_connected_seconds argument had been added since 3.16,
 * to let the caller decide how long to wait for the backend to fully
 * connect to its (possibly remote) data store. This is required due
 * to a change in the authentication process, which is fully asynchronous
 * and done on the client side, while not every client is supposed to
 * response to authentication requests. In case the backend will not connect
 * within the set interval, then it is opened in an offline mode. A special
 * value -1 can be used to not wait for the connected state at all.
 *
 * Unlike with e_cal_client_new(), there is no need to call
 * e_client_open_sync() after obtaining the #ECalClient.
 *
 * For error handling convenience, any error message returned by this
 * function will have a descriptive prefix that includes the display
 * name of @source.
 *
 * Returns: (transfer full) (nullable): a new #ECalClient, or %NULL
 *
 * Since: 3.8
 **/
EClient *
e_cal_client_connect_sync (ESource *source,
                           ECalClientSourceType source_type,
			   guint32 wait_for_connected_seconds,
                           GCancellable *cancellable,
                           GError **error)
{
	ECalClient *client;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_SOURCE (source), NULL);
	g_return_val_if_fail (
		source_type == E_CAL_CLIENT_SOURCE_TYPE_EVENTS ||
		source_type == E_CAL_CLIENT_SOURCE_TYPE_TASKS ||
		source_type == E_CAL_CLIENT_SOURCE_TYPE_MEMOS, NULL);

	client = g_object_new (
		E_TYPE_CAL_CLIENT,
		"source", source,
		"source-type", source_type, NULL);

	g_initable_init (G_INITABLE (client), cancellable, &local_error);

	if (local_error == NULL) {
		gchar **props = NULL;

		e_dbus_calendar_call_open_sync (
			client->priv->dbus_proxy, &props, cancellable, &local_error);

		cal_client_process_properties (client, props);
		g_strfreev (props);
	}

	if (!local_error && wait_for_connected_seconds != (guint32) -1) {
		/* These errors are ignored, the book is left opened in an offline mode. */
		e_client_wait_for_connected_sync (E_CLIENT (client),
			wait_for_connected_seconds, cancellable, NULL);
	}

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		g_prefix_error (
			error,_("Unable to connect to “%s”: "),
			e_source_get_display_name (source));
		g_object_unref (client);
		return NULL;
	}

	return E_CLIENT (client);
}

static void
cal_client_connect_wait_for_connected_cb (GObject *source_object,
					   GAsyncResult *result,
					   gpointer user_data)
{
	GTask *task = user_data;

	/* These errors are ignored, the book is left opened in an offline mode. */
	e_client_wait_for_connected_finish (E_CLIENT (source_object), result, NULL);

	g_task_return_boolean (task, TRUE);

	g_object_unref (task);
}

/* Helper for e_cal_client_connect() */
static void
cal_client_connect_open_cb (GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
	GTask *task;
	gchar **props = NULL;
	ECalClient *client_object;
	GError *local_error = NULL;

	task = G_TASK (user_data);

	e_dbus_calendar_call_open_finish (
		E_DBUS_CALENDAR (source_object), &props, result, &local_error);

	client_object = g_task_get_source_object (task);
	if (client_object) {
		cal_client_process_properties (client_object, props);

		if (!local_error) {
			ConnectClosure *closure;

			closure = g_task_get_task_data (task);
			if (closure->wait_for_connected_seconds != (guint32) -1) {
				GCancellable *cancellable;

				cancellable = g_task_get_cancellable (task);
				e_client_wait_for_connected (E_CLIENT (client_object),
					closure->wait_for_connected_seconds,
					cancellable,
					cal_client_connect_wait_for_connected_cb,
					g_steal_pointer (&task));
			}
		}
	}

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
	} else if (task) {
		g_task_return_boolean (task, TRUE);
	}

	g_clear_object (&task);
	g_strfreev (props);
}

/* Helper for e_cal_client_connect() */
static void
cal_client_connect_init_cb (GObject *source_object,
                            GAsyncResult *result,
                            gpointer user_data)
{
	GTask *task;
	ECalClientPrivate *priv;
	GError *local_error = NULL;
	ECalClient *client_object;
	GCancellable *cancellable;

	task = G_TASK (user_data);

	g_async_initable_init_finish (
		G_ASYNC_INITABLE (source_object), result, &local_error);

	if (local_error != NULL) {
		g_task_return_error (task, g_steal_pointer (&local_error));
		goto exit;
	}

	client_object = g_task_get_source_object (task);
	cancellable = g_task_get_cancellable (task);

	g_return_if_fail (E_IS_CAL_CLIENT (client_object));
	priv = client_object->priv;

	e_dbus_calendar_call_open (
		priv->dbus_proxy,
		cancellable,
		cal_client_connect_open_cb,
		g_steal_pointer (&task));

exit:
	g_clear_object (&task);
}

/**
 * e_cal_client_connect:
 * @source: an #ESource
 * @source_type: source tpe of the calendar
 * @wait_for_connected_seconds: timeout, in seconds, to wait for the backend to be fully connected
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request
 *            is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously creates a new #ECalClient for @source and @source_type.
 *
 * The @wait_for_connected_seconds argument had been added since 3.16,
 * to let the caller decide how long to wait for the backend to fully
 * connect to its (possibly remote) data store. This is required due
 * to a change in the authentication process, which is fully asynchronous
 * and done on the client side, while not every client is supposed to
 * response to authentication requests. In case the backend will not connect
 * within the set interval, then it is opened in an offline mode. A special
 * value -1 can be used to not wait for the connected state at all.
 *
 * Unlike with e_cal_client_new(), there is no need to call e_client_open()
 * after obtaining the #ECalClient.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call e_cal_client_connect_finish() to get the result of the operation.
 *
 * Since: 3.8
 **/
void
e_cal_client_connect (ESource *source,
                      ECalClientSourceType source_type,
		      guint32 wait_for_connected_seconds,
                      GCancellable *cancellable,
                      GAsyncReadyCallback callback,
                      gpointer user_data)
{
	GTask *task;
	ConnectClosure *closure;
	ECalClient *client;

	g_return_if_fail (E_IS_SOURCE (source));
	g_return_if_fail (
		source_type == E_CAL_CLIENT_SOURCE_TYPE_EVENTS ||
		source_type == E_CAL_CLIENT_SOURCE_TYPE_TASKS ||
		source_type == E_CAL_CLIENT_SOURCE_TYPE_MEMOS);

	/* Two things with this: 1) instantiate the client object
	 * immediately to make sure the thread-default GMainContext
	 * gets plucked, and 2) do not call the D-Bus open() method
	 * from our designated D-Bus thread -- it may take a long
	 * time and block other clients from receiving signals. */

	closure = g_slice_new0 (ConnectClosure);
	closure->source = g_object_ref (source);
	closure->wait_for_connected_seconds = wait_for_connected_seconds;

	client = g_object_new (
		E_TYPE_CAL_CLIENT,
		"source", source,
		"source-type", source_type, NULL);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_connect);
	g_task_set_task_data (task, g_steal_pointer (&closure), (GDestroyNotify) connect_closure_free);

	g_async_initable_init_async (
		G_ASYNC_INITABLE (client),
		G_PRIORITY_DEFAULT, cancellable,
		cal_client_connect_init_cb,
		g_steal_pointer (&task));

	g_object_unref (client);
}

/**
 * e_cal_client_connect_finish:
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with e_cal_client_connect().  If an
 * error occurs in connecting to the D-Bus service, the function sets
 * @error and returns %NULL.
 *
 * For error handling convenience, any error message returned by this
 * function will have a descriptive prefix that includes the display
 * name of the #ESource passed to e_cal_client_connect().
 *
 * Returns: (transfer full) (nullable): a new #ECalClient, or %NULL
 *
 * Since: 3.8
 **/
EClient *
e_cal_client_connect_finish (GAsyncResult *result,
                             GError **error)
{
	GTask *task = (GTask *)result;
	EClient *client;

	g_return_val_if_fail (G_IS_TASK (task), NULL);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_connect), NULL);

	if (!g_task_propagate_boolean (task, error)) {
		ConnectClosure *closure;

		closure = g_task_get_task_data (task);
		g_prefix_error (
			error, _("Unable to connect to “%s”: "),
			e_source_get_display_name (closure->source));
		return NULL;
	}

	client = g_task_get_source_object (task);
	return g_object_ref (client);
}

/**
 * e_cal_client_get_source_type:
 * @client: A calendar client.
 *
 * Gets the source type of the calendar client.
 *
 * Returns: an #ECalClientSourceType value corresponding
 * to the source type of the calendar client.
 *
 * Since: 3.2
 **/
ECalClientSourceType
e_cal_client_get_source_type (ECalClient *client)
{
	g_return_val_if_fail (
		E_IS_CAL_CLIENT (client),
		E_CAL_CLIENT_SOURCE_TYPE_LAST);

	return client->priv->source_type;
}

/**
 * e_cal_client_get_local_attachment_store:
 * @client: A calendar client.
 *
 * Queries the URL where the calendar attachments are
 * serialized in the local filesystem. This enable clients
 * to operate with the reference to attachments rather than the data itself
 * unless it specifically uses the attachments for open/sending
 * operations.
 *
 * Returns: The URL where the attachments are serialized in the
 * local filesystem.
 *
 * Since: 3.2
 **/
const gchar *
e_cal_client_get_local_attachment_store (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);

	return e_dbus_calendar_get_cache_dir (client->priv->dbus_proxy);
}

/**
 * e_cal_client_set_default_timezone:
 * @client: A calendar client.
 * @zone: A timezone object.
 *
 * Sets the default timezone to use to resolve DATE and floating DATE-TIME
 * values. This will typically be from the user's timezone setting. Call this
 * before using any other object fetching functions.
 *
 * Since: 3.2
 **/
void
e_cal_client_set_default_timezone (ECalClient *client,
                                   ICalTimezone *zone)
{
	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (zone != NULL);

	if (zone == client->priv->default_zone)
		return;

	if (client->priv->default_zone != i_cal_timezone_get_utc_timezone ())
		g_clear_object (&client->priv->default_zone);

	if (zone == i_cal_timezone_get_utc_timezone ())
		client->priv->default_zone = zone;
	else
		client->priv->default_zone = e_cal_util_copy_timezone (zone);

	g_object_notify_by_pspec (G_OBJECT (client), properties[PROP_DEFAULT_TIMEZONE]);
}

/**
 * e_cal_client_get_default_timezone:
 * @client: A calendar client.
 *
 * Returns the default timezone previously set with
 * e_cal_client_set_default_timezone().  The returned pointer is owned by
 * the @client and should not be freed.
 *
 * Returns: (transfer none): an #ICalTimezone
 *
 * Since: 3.2
 **/
ICalTimezone *
e_cal_client_get_default_timezone (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);

	return client->priv->default_zone;
}

/**
 * e_cal_client_check_one_alarm_only:
 * @client: A calendar client.
 *
 * Checks if a calendar supports only one alarm per component.
 *
 * Returns: TRUE if the calendar allows only one alarm, FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_one_alarm_only (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (
		E_CLIENT (client),
		E_CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY);
}

/**
 * e_cal_client_check_save_schedules:
 * @client: A calendar client.
 *
 * Checks whether the calendar saves schedules.
 *
 * Returns: TRUE if it saves schedules, FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_save_schedules (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (
		E_CLIENT (client),
		E_CAL_STATIC_CAPABILITY_SAVE_SCHEDULES);
}

/**
 * e_cal_client_check_organizer_must_attend:
 * @client: A calendar client.
 *
 * Checks if a calendar forces organizers of meetings to be also attendees.
 *
 * Returns: TRUE if the calendar forces organizers to attend meetings,
 * FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_organizer_must_attend (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (
		E_CLIENT (client),
		E_CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND);
}

/**
 * e_cal_client_check_organizer_must_accept:
 * @client: A calendar client.
 *
 * Checks whether a calendar requires organizer to accept their attendance to
 * meetings.
 *
 * Returns: TRUE if the calendar requires organizers to accept, FALSE
 * otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_organizer_must_accept (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (
		E_CLIENT (client),
		E_CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT);
}

/**
 * e_cal_client_check_recurrences_no_master:
 * @client: A calendar client.
 *
 * Checks if the calendar has a master object for recurrences.
 *
 * Returns: TRUE if the calendar has a master object for recurrences,
 * FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_check_recurrences_no_master (ECalClient *client)
{
	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	return e_client_check_capability (
		E_CLIENT (client),
		E_CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER);
}

struct comp_instance {
	ECalComponent *comp;
	ICalTime *start;
	ICalTime *end;
};

static struct comp_instance *
comp_instance_new (void)
{
	return g_new0 (struct comp_instance, 1);
}

static void
comp_instance_free (gpointer ptr)
{
	struct comp_instance *ci = ptr;

	if (ci) {
		g_clear_object (&ci->comp);
		g_clear_object (&ci->start);
		g_clear_object (&ci->end);
		g_free (ci);
	}
}

static gboolean
add_instance_cb (ICalComponent *icomp,
		 ICalTime *start,
		 ICalTime *end,
		 gpointer user_data,
		 GCancellable *cancellable,
		 GError **error)
{
	GHashTable *instances_by_uid = user_data;
	GPtrArray *instances;
	struct comp_instance *ci;

	if (!i_cal_component_get_uid (icomp)) {
		g_warning ("%s: Received component without UID", G_STRFUNC);
		return TRUE;
	}

	ci = comp_instance_new ();

	/* add the instance to the list */
	ci->comp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (icomp));
	if (!ci->comp) {
		comp_instance_free (ci);
		return FALSE;
	}

	/* make sure we return an instance */
	if (e_cal_component_has_recurrences (ci->comp) &&
	    !e_cal_component_is_instance (ci->comp)) {
		ECalComponentDateTime *dtstart, *dtend;
		ECalComponentRange *range;

		/* Update DTSTART */
		dtstart = e_cal_component_datetime_new (start, i_cal_time_get_tzid (start));
		e_cal_component_set_dtstart (ci->comp, dtstart);

		if (!i_cal_time_is_date (start) && i_cal_time_get_tzid (start) != NULL) {
			/* convert into UTC, to have RECURRENCE-ID in a fixed timezone,
			   thus ECalComponetId can be safely compared; some servers convert
			   the value to UTC on their own too */
			ICalTimezone *utc = i_cal_timezone_get_utc_timezone ();
			ICalTime *in_utc;

			in_utc = i_cal_time_convert_to_zone (start, utc);
			if (in_utc) {
				i_cal_time_set_timezone (in_utc, utc);
				e_cal_component_datetime_take_value (dtstart, in_utc);
				e_cal_component_datetime_set_tzid (dtstart, "UTC");
			}
		}

		/* set the RECUR-ID for the instance */
		range = e_cal_component_range_new (E_CAL_COMPONENT_RANGE_SINGLE, dtstart);

		e_cal_component_set_recurid (ci->comp, range);

		e_cal_component_datetime_free (dtstart);
		e_cal_component_range_free (range);

		/* Update DTEND */
		dtend = e_cal_component_datetime_new (end, i_cal_time_get_tzid (end));
		e_cal_component_set_dtend (ci->comp, dtend);
		e_cal_component_datetime_free (dtend);
	}

	ci->start = i_cal_time_clone (start);
	ci->end = i_cal_time_clone (end);

	instances = g_hash_table_lookup (instances_by_uid, e_cal_component_get_uid (ci->comp));
	if (!instances) {
		instances = g_ptr_array_new_with_free_func (comp_instance_free);
		g_hash_table_insert (instances_by_uid, (gpointer) e_cal_component_get_uid (ci->comp), instances);
	}

	g_ptr_array_add (instances, ci);

	return TRUE;
}

/* Used from g_ptr_array_sort(); compares two struct comp_instance structures */
static gint
compare_comp_instance (gconstpointer a,
                       gconstpointer b)
{
	const struct comp_instance *cia = *((const struct comp_instance **) a);
	const struct comp_instance *cib = *((const struct comp_instance **) b);

	return i_cal_time_compare (cia->start, cib->start);
}

static time_t
convert_to_tt_with_zone (const ECalComponentDateTime *dt,
			 ECalRecurResolveTimezoneCb tz_cb,
			 gpointer tz_cb_data,
			 ICalTimezone *default_timezone)
{
	ICalTimezone *zone = NULL;
	ICalTime *value;
	time_t tt;

	value = dt ? e_cal_component_datetime_get_value (dt) : NULL;

	if (!dt || !value)
		return (time_t) 0;

	if (i_cal_time_is_utc (value)) {
		zone = i_cal_timezone_get_utc_timezone ();
	} else if (tz_cb && !i_cal_time_is_date (value) && e_cal_component_datetime_get_tzid (dt)) {
		zone = (*tz_cb) (e_cal_component_datetime_get_tzid (dt), tz_cb_data, NULL, NULL);
	}

	if (!zone)
		zone = default_timezone;

	tt = i_cal_time_as_timet_with_zone (value, zone);

	return tt;
}

static void
process_detached_instances (GHashTable *instances_by_uid,
			    GPtrArray *detached_instances,
			    ECalRecurResolveTimezoneCb tz_cb,
			    gpointer tz_cb_data,
			    ICalTimezone *default_timezone,
			    ICalTime *starttt,
			    ICalTime *endtt)
{
	struct comp_instance *ci, *cid;
	GPtrArray *unprocessed_instances = NULL;
	guint ii;

	for (ii = 0; detached_instances && ii < detached_instances->len; ii++) {
		GPtrArray *instances;
		const gchar *uid;
		gboolean processed;
		ECalComponentRange *recur_id;
		time_t d_rid, i_rid;
		guint jj;

		processed = FALSE;

		cid = g_ptr_array_index (detached_instances, ii);
		uid = e_cal_component_get_uid (cid->comp);

		recur_id = e_cal_component_get_recurid (cid->comp);

		if (!recur_id ||
		    !e_cal_component_range_get_datetime (recur_id) ||
		    !e_cal_component_datetime_get_value (e_cal_component_range_get_datetime (recur_id))) {
			e_cal_component_range_free (recur_id);
			continue;
		}

		instances = g_hash_table_lookup (instances_by_uid, uid);
		if (instances) {
			d_rid = convert_to_tt_with_zone (e_cal_component_range_get_datetime (recur_id), tz_cb, tz_cb_data, default_timezone);

			/* search for coincident instances already expanded */
			for (jj = 0; jj < instances->len; jj++) {
				ECalComponentRange *instance_recur_id;
				gint cmp;

				ci = g_ptr_array_index (instances, jj);

				instance_recur_id = e_cal_component_get_recurid (ci->comp);

				if (!instance_recur_id ||
				    !e_cal_component_range_get_datetime (instance_recur_id) ||
				    !e_cal_component_datetime_get_value (e_cal_component_range_get_datetime (instance_recur_id))) {
					/*
					 * Prevent obvious segfault by ignoring missing
					 * recurrency ids. Real problem might be elsewhere,
					 * but anything is better than crashing...
					 */
					g_warning ("UID %s: instance RECURRENCE-ID and detached instance RECURRENCE-ID cannot compare", uid);

					e_cal_component_range_free (instance_recur_id);
					continue;
				}

				i_rid = convert_to_tt_with_zone (e_cal_component_range_get_datetime (instance_recur_id), tz_cb, tz_cb_data, default_timezone);

				if (e_cal_component_range_get_kind (recur_id) == E_CAL_COMPONENT_RANGE_SINGLE && i_rid == d_rid) {
					if (i_cal_time_compare (cid->start, endtt) <= 0 && i_cal_time_compare (cid->end, starttt) >= 0) {
						g_object_unref (ci->comp);
						g_clear_object (&ci->start);
						g_clear_object (&ci->end);
						ci->comp = g_object_ref (cid->comp);
						ci->start = i_cal_time_clone (cid->start);
						ci->end = i_cal_time_clone (cid->end);
					} else {
						g_ptr_array_remove_index_fast (instances, jj);
						jj--;
					}

					processed = TRUE;
				} else {
					cmp = i_rid == d_rid ? 0 : i_rid < d_rid ? -1 : 1;
					if ((e_cal_component_range_get_kind (recur_id) == E_CAL_COMPONENT_RANGE_THISPRIOR && cmp <= 0) ||
					    (e_cal_component_range_get_kind (recur_id) == E_CAL_COMPONENT_RANGE_THISFUTURE && cmp >= 0)) {
						ECalComponent *comp;

						comp = e_cal_component_clone (cid->comp);
						e_cal_component_set_recurid (comp, instance_recur_id);

						/* replace the generated instances */
						g_object_unref (ci->comp);
						ci->comp = comp;
					}
				}

				e_cal_component_range_free (instance_recur_id);
			}

			e_cal_component_range_free (recur_id);
		}

		if (!processed && i_cal_time_compare (cid->start, endtt) <= 0 && i_cal_time_compare (cid->end, starttt) >= 0) {
			if (!unprocessed_instances)
				unprocessed_instances = g_ptr_array_new_with_free_func (comp_instance_free);
			g_ptr_array_add (unprocessed_instances, cid);
			/* steal the instance */
			detached_instances->pdata[ii] = NULL;
		}
	}

	/* add the unprocessed instances
	 * (ie, detached instances with no master object) */
	if (unprocessed_instances) {
		GPtrArray *instances;

		/* the used UID does not matter, just do not clash with other */
		instances = g_hash_table_lookup (instances_by_uid, "\n");
		g_warn_if_fail (instances == NULL);
		g_hash_table_insert (instances_by_uid, (gpointer) "\n", g_steal_pointer (&unprocessed_instances));
		g_clear_pointer (&unprocessed_instances, g_ptr_array_unref);
	}
}

static void
generate_instances (ECalClient *client,
                    time_t start,
                    time_t end,
                    GSList *objects,
                    GCancellable *cancellable,
                    ECalRecurInstanceCb cb,
                    gpointer cb_data)
{
	GHashTable *instances_by_uid; /* gchar *uid ~> GPtrArray { comp_instance * } */
	GPtrArray *detached_instances = NULL; /* comp_instance * */
	GSList *link;
	ECalClientPrivate *priv;
	ICalTimezone *default_zone;
	ICalTime *starttt, *endtt;
	GHashTableIter iter;
	gpointer value = NULL;
	guint ii;

	priv = client->priv;

	instances_by_uid = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_ptr_array_unref);

	if (priv->default_zone)
		default_zone = priv->default_zone;
	else
		default_zone = i_cal_timezone_get_utc_timezone ();

	starttt = i_cal_time_new_from_timet_with_zone (start, FALSE, i_cal_timezone_get_utc_timezone ());
	endtt = i_cal_time_new_from_timet_with_zone (end, FALSE, i_cal_timezone_get_utc_timezone ());

	for (link = objects; link && !g_cancellable_is_cancelled (cancellable); link = g_slist_next (link)) {
		ECalComponent *comp = link->data;

		if (e_cal_component_is_instance (comp)) {
			struct comp_instance *ci;
			ECalComponentDateTime *dtstart, *dtend;
			ICalTime *rid;

			/* keep the detached instances apart */
			ci = comp_instance_new ();
			ci->comp = g_object_ref (comp);

			dtstart = e_cal_component_get_dtstart (comp);
			dtend = e_cal_component_get_dtend (comp);

			if (!dtstart || !e_cal_component_datetime_get_value (dtstart)) {
				g_warn_if_reached ();

				e_cal_component_datetime_free (dtstart);
				e_cal_component_datetime_free (dtend);
				comp_instance_free (ci);

				continue;
			}

			ci->start = i_cal_time_clone (e_cal_component_datetime_get_value (dtstart));
			if (e_cal_component_datetime_get_tzid (dtstart)) {
				ICalTimezone *zone;

				zone = e_cal_client_tzlookup_cb (e_cal_component_datetime_get_tzid (dtstart), client, NULL, NULL);

				if (zone)
					i_cal_time_set_timezone (ci->start, zone);
			}

			if (dtend && e_cal_component_datetime_get_value (dtend)) {
				ci->end = i_cal_time_clone (e_cal_component_datetime_get_value (dtend));

				if (e_cal_component_datetime_get_tzid (dtend)) {
					ICalTimezone *zone;

					zone = e_cal_client_tzlookup_cb (e_cal_component_datetime_get_tzid (dtend), client, NULL, NULL);

					if (zone)
						i_cal_time_set_timezone (ci->end, zone);
				}
			} else {
				ci->end = i_cal_time_clone (ci->start);

				if (i_cal_time_is_date (e_cal_component_datetime_get_value (dtstart)))
					i_cal_time_adjust (ci->end, 1, 0, 0, 0);
			}

			e_cal_component_datetime_free (dtstart);
			e_cal_component_datetime_free (dtend);

			rid = i_cal_component_get_recurrenceid (e_cal_component_get_icalcomponent (comp));

			/* either the instance belongs to the interval or it did belong to it before it had been moved */
			if ((i_cal_time_compare (ci->start, endtt) <= 0 && i_cal_time_compare (ci->end, starttt) >= 0) ||
			    (rid && i_cal_time_compare (rid, endtt) <= 0 && i_cal_time_compare (rid, starttt) >= 0)) {
				if (!detached_instances)
					detached_instances = g_ptr_array_new_with_free_func (comp_instance_free);
				g_ptr_array_add (detached_instances, ci);
			} else {
				/* it doesn't fit to our time range, thus skip it */
				comp_instance_free (ci);
			}

			g_clear_object (&rid);
		} else {
			e_cal_recur_generate_instances_sync (
				e_cal_component_get_icalcomponent (comp), starttt, endtt, add_instance_cb, instances_by_uid,
				e_cal_client_tzlookup_cb, client,
				default_zone, cancellable, NULL);
		}
	}

	g_slist_free_full (objects, g_object_unref);

	/* Generate instances and spew them out */

	if (!g_cancellable_is_cancelled (cancellable)) {
		process_detached_instances (instances_by_uid, detached_instances,
			e_cal_client_tzlookup_cb, client, default_zone, starttt, endtt);
	}

	g_hash_table_iter_init (&iter, instances_by_uid);
	while (g_hash_table_iter_next (&iter, NULL, &value) && !g_cancellable_is_cancelled (cancellable)) {
		GPtrArray *instances = value;
		gboolean result = TRUE;

		g_ptr_array_sort (instances, compare_comp_instance);

		for (ii = 0; ii < instances->len && result; ii++) {
			struct comp_instance *ci = g_ptr_array_index (instances, ii);

			result = (* cb) (e_cal_component_get_icalcomponent (ci->comp), ci->start, ci->end, cb_data, cancellable, NULL);
		}

		if (!result)
			break;
	}

	/* Clean up */

	g_clear_pointer (&instances_by_uid, g_hash_table_unref);
	g_clear_pointer (&detached_instances, g_ptr_array_unref);
	g_clear_object (&starttt);
	g_clear_object (&endtt);
}

static GSList *
get_objects_sync (ECalClient *client,
                  time_t start,
                  time_t end,
                  const gchar *uid)
{
	GSList *objects = NULL;

	/* Generate objects */
	if (uid && *uid) {
		GError *local_error = NULL;

		e_cal_client_get_objects_for_uid_sync (
			client, uid, &objects, NULL, &local_error);

		if (local_error != NULL) {
			g_warning (
				"Failed to get recurrence objects "
				"for uid: %s\n", local_error->message);
			g_error_free (local_error);
			return NULL;
		}
	} else {
		gchar *iso_start, *iso_end;
		gchar *query;

		iso_start = isodate_from_time_t (start);
		if (!iso_start)
			return NULL;

		iso_end = isodate_from_time_t (end);
		if (!iso_end) {
			g_free (iso_start);
			return NULL;
		}

		query = g_strdup_printf (
			"(occur-in-time-range? "
			"(make-time \"%s\") (make-time \"%s\"))",
			iso_start, iso_end);
		g_free (iso_start);
		g_free (iso_end);
		if (!e_cal_client_get_object_list_as_comps_sync (
			client, query, &objects, NULL, NULL)) {
			g_free (query);
			return NULL;
		}
		g_free (query);
	}

	return objects;
}

struct get_objects_async_data {
	GCancellable *cancellable;
	ECalClient *client;
	time_t start;
	time_t end;
	ECalRecurInstanceCb cb;
	gpointer cb_data;
	GDestroyNotify destroy_cb_data;
	gchar *uid;
	gchar *rid;
	gchar *query;
	guint tries;
	void (* ready_cb) (struct get_objects_async_data *goad, GSList *objects);
};

static void
free_get_objects_async_data (struct get_objects_async_data *goad)
{
	if (!goad)
		return;

	if (goad->destroy_cb_data)
		goad->destroy_cb_data (goad->cb_data);
	g_clear_object (&goad->cancellable);
	g_clear_object (&goad->client);
	g_free (goad->query);
	g_free (goad->uid);
	g_free (goad->rid);
	g_slice_free (struct get_objects_async_data, goad);
}

static void
got_objects_for_uid_cb (GObject *source_object,
                        GAsyncResult *result,
                        gpointer user_data)
{
	struct get_objects_async_data *goad = user_data;
	GSList *objects = NULL;
	GError *local_error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (result != NULL);
	g_return_if_fail (goad != NULL);
	g_return_if_fail (goad->client == E_CAL_CLIENT (source_object));

	e_cal_client_get_objects_for_uid_finish (
		goad->client, result, &objects, &local_error);

	if (local_error != NULL) {
		if (g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_CANCELLED) ||
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			free_get_objects_async_data (goad);
			g_error_free (local_error);
			return;
		}

		g_clear_error (&local_error);
		objects = NULL;
	}

	g_return_if_fail (goad->ready_cb != NULL);

	/* takes care of the objects and goad */
	goad->ready_cb (goad, objects);
}

static void
got_object_list_as_comps_cb (GObject *source_object,
                             GAsyncResult *result,
                             gpointer user_data)
{
	struct get_objects_async_data *goad = user_data;
	GSList *objects = NULL;
	GError *local_error = NULL;

	g_return_if_fail (source_object != NULL);
	g_return_if_fail (result != NULL);
	g_return_if_fail (goad != NULL);
	g_return_if_fail (goad->client == E_CAL_CLIENT (source_object));

	e_cal_client_get_object_list_as_comps_finish (
		goad->client, result, &objects, &local_error);

	if (local_error != NULL) {
		if (g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_CANCELLED) ||
		    g_error_matches (local_error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			free_get_objects_async_data (goad);
			g_error_free (local_error);
			return;
		}

		g_clear_error (&local_error);
		objects = NULL;
	}

	g_return_if_fail (goad->ready_cb != NULL);

	/* takes care of the objects and goad */
	goad->ready_cb (goad, objects);
}

/* ready_cb may take care of both arguments, goad and objects;
 * objects can be also NULL */
static void
get_objects_async (void (*ready_cb) (struct get_objects_async_data *goad,
                                     GSList *objects),
                   struct get_objects_async_data *goad)
{
	g_return_if_fail (ready_cb != NULL);
	g_return_if_fail (goad != NULL);

	goad->ready_cb = ready_cb;

	if (goad->uid && *goad->uid) {
		e_cal_client_get_objects_for_uid (
			goad->client, goad->uid, goad->cancellable,
			got_objects_for_uid_cb, goad);
	} else {
		gchar *iso_start, *iso_end;

		iso_start = isodate_from_time_t (goad->start);
		if (!iso_start) {
			free_get_objects_async_data (goad);
			return;
		}

		iso_end = isodate_from_time_t (goad->end);
		if (!iso_end) {
			g_free (iso_start);
			free_get_objects_async_data (goad);
			return;
		}

		goad->query = g_strdup_printf (
			"(occur-in-time-range? "
			"(make-time \"%s\") (make-time \"%s\"))",
			iso_start, iso_end);

		g_free (iso_start);
		g_free (iso_end);

		e_cal_client_get_object_list_as_comps (
			goad->client, goad->query, goad->cancellable,
			got_object_list_as_comps_cb, goad);
	}
}

static void
generate_instances_got_objects_cb (struct get_objects_async_data *goad,
                                   GSList *objects)
{
	g_return_if_fail (goad != NULL);

	/* generate_instaces () frees 'objects' slist */
	if (objects)
		generate_instances (
			goad->client, goad->start, goad->end, objects,
			goad->cancellable, goad->cb, goad->cb_data);

	free_get_objects_async_data (goad);
}

/**
 * e_cal_client_generate_instances:
 * @client: A calendar client.
 * @start: Start time for query.
 * @end: End time for query.
 * @cancellable: a #GCancellable; can be %NULL
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 * @destroy_cb_data: Function to call when the processing is done, to free
 *                   @cb_data; can be %NULL.
 *
 * Does a combination of e_cal_client_get_object_list() and
 * e_cal_recur_generate_instances_sync(). Unlike
 * e_cal_client_generate_instances_sync(), this returns immediately and the
 * @cb callback is called asynchronously.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Since: 3.2
 **/
void
e_cal_client_generate_instances (ECalClient *client,
                                 time_t start,
                                 time_t end,
                                 GCancellable *cancellable,
                                 ECalRecurInstanceCb cb,
                                 gpointer cb_data,
                                 GDestroyNotify destroy_cb_data)
{
	struct get_objects_async_data *goad;
	GCancellable *use_cancellable;

	g_return_if_fail (E_IS_CAL_CLIENT (client));

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	use_cancellable = cancellable;
	if (!use_cancellable)
		use_cancellable = g_cancellable_new ();

	goad = g_slice_new0 (struct get_objects_async_data);
	goad->cancellable = g_object_ref (use_cancellable);
	goad->client = g_object_ref (client);
	goad->start = start;
	goad->end = end;
	goad->cb = cb;
	goad->cb_data = cb_data;
	goad->destroy_cb_data = destroy_cb_data;

	get_objects_async (generate_instances_got_objects_cb, goad);

	if (use_cancellable != cancellable)
		g_object_unref (use_cancellable);
}

/**
 * e_cal_client_generate_instances_sync:
 * @client: A calendar client
 * @start: Start time for query
 * @end: End time for query
 * @cancellable: a #GCancellable; can be %NULL
 * @cb: (closure cb_data) (scope call): Callback for each generated instance
 * @cb_data: Closure data for the callback
 *
 * Does a combination of e_cal_client_get_object_list() and
 * e_cal_recur_generate_instances_sync().
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unreffed
 * as soon as the callback returns.
 *
 * Since: 3.2
 **/
void
e_cal_client_generate_instances_sync (ECalClient *client,
                                      time_t start,
                                      time_t end,
				      GCancellable *cancellable,
                                      ECalRecurInstanceCb cb,
                                      gpointer cb_data)
{
	GSList *objects = NULL;

	g_return_if_fail (E_IS_CAL_CLIENT (client));

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	objects = get_objects_sync (client, start, end, NULL);
	if (!objects)
		return;

	/* generate_instaces frees 'objects' slist */
	generate_instances (client, start, end, objects, cancellable, cb, cb_data);
}

/* also frees 'instances' GSList */
static void
process_instances (ECalClient *client,
		   const gchar *uid,
		   const gchar *rid,
                   GHashTable *instances_by_uid, /* (transfer none) */
                   ECalRecurInstanceCb cb,
                   gpointer cb_data)
{
	GHashTableIter iter;
	gpointer value = NULL;
	gboolean result = TRUE;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (cb != NULL);

	g_hash_table_iter_init (&iter, instances_by_uid);
	while (result && g_hash_table_iter_next (&iter, NULL, &value)) {
		GPtrArray *instances = value;
		guint ii;

		/* now only return back the instances for the given object */
		for (ii = 0; ii < instances->len && result; ii++) {
			struct comp_instance *ci = g_ptr_array_index (instances, ii);

			if (rid && *rid) {
				gchar *instance_rid = e_cal_component_get_recurid_as_string (ci->comp);

				if (g_strcmp0 (rid, instance_rid) == 0)
					result = (* cb) (e_cal_component_get_icalcomponent (ci->comp), ci->start, ci->end, cb_data, NULL, NULL);

				g_free (instance_rid);
			} else {
				result = (* cb) (e_cal_component_get_icalcomponent (ci->comp), ci->start, ci->end, cb_data, NULL, NULL);
			}
		}
	}
}

static void
generate_instances_for_object_got_objects_cb (struct get_objects_async_data *goad,
                                              GSList *objects)
{
	GHashTable *instances_by_uid;

	g_return_if_fail (goad != NULL);

	instances_by_uid = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_ptr_array_unref);

	/* generate all instances in the given time range */
	generate_instances (
		goad->client, goad->start, goad->end, objects,
		goad->cancellable, add_instance_cb, instances_by_uid);

	/* it also frees 'instances' GSList */
	process_instances (
		goad->client, goad->uid, goad->rid, instances_by_uid,
		goad->cb, goad->cb_data);

	/* clean up */
	free_get_objects_async_data (goad);
	g_hash_table_destroy (instances_by_uid);
}

/**
 * e_cal_client_generate_instances_for_object:
 * @client: A calendar client.
 * @icalcomp: Object to generate instances from.
 * @start: Start time for query.
 * @end: End time for query.
 * @cancellable: a #GCancellable; can be %NULL
 * @cb: Callback for each generated instance.
 * @cb_data: Closure data for the callback.
 * @destroy_cb_data: Function to call when the processing is done, to
 *                   free @cb_data; can be %NULL.
 *
 * Does a combination of e_cal_client_get_object_list() and
 * e_cal_recur_generate_instances_sync(), like
 * e_cal_client_generate_instances(), but for a single object. Unlike
 * e_cal_client_generate_instances_for_object_sync(), this returns immediately
 * and the @cb callback is called asynchronously.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Since: 3.2
 **/
void
e_cal_client_generate_instances_for_object (ECalClient *client,
                                            ICalComponent *icalcomp,
                                            time_t start,
                                            time_t end,
                                            GCancellable *cancellable,
                                            ECalRecurInstanceCb cb,
                                            gpointer cb_data,
                                            GDestroyNotify destroy_cb_data)
{
	struct get_objects_async_data *goad;
	GCancellable *use_cancellable;

	g_return_if_fail (E_IS_CAL_CLIENT (client));

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	/* If the backend stores it as individual instances and does not
	 * have a master object - do not expand */
	if (!e_cal_util_component_has_recurrences (icalcomp) || e_client_check_capability (E_CLIENT (client), E_CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER)) {
		ICalTime *dtstart, *dtend;

		dtstart = i_cal_component_get_dtstart (icalcomp);
		dtend = i_cal_component_get_dtend (icalcomp);

		/* return the same instance */
		(* cb)  (icalcomp, dtstart, dtend, cb_data, NULL, NULL);

		g_clear_object (&dtstart);
		g_clear_object (&dtend);

		if (destroy_cb_data)
			destroy_cb_data (cb_data);
		return;
	}

	use_cancellable = cancellable;
	if (!use_cancellable)
		use_cancellable = g_cancellable_new ();

	goad = g_slice_new0 (struct get_objects_async_data);
	goad->cancellable = g_object_ref (use_cancellable);
	goad->client = g_object_ref (client);
	goad->start = start;
	goad->end = end;
	goad->cb = cb;
	goad->cb_data = cb_data;
	goad->destroy_cb_data = destroy_cb_data;
	goad->uid = g_strdup (i_cal_component_get_uid (icalcomp));
	goad->rid = e_cal_util_component_get_recurid_as_string (icalcomp);

	get_objects_async (generate_instances_for_object_got_objects_cb, goad);

	if (use_cancellable != cancellable)
		g_object_unref (use_cancellable);
}

/**
 * e_cal_client_generate_instances_for_object_sync:
 * @client: A calendar client
 * @icalcomp: Object to generate instances from
 * @start: Start time for query
 * @end: End time for query
 * @cancellable: a #GCancellable; can be %NULL
 * @cb: (closure cb_data) (scope call): Callback for each generated instance
 * @cb_data: Closure data for the callback
 *
 * Does a combination of e_cal_client_get_object_list() and
 * e_cal_recur_generate_instances_sync(), like
 * e_cal_client_generate_instances_sync(), but for a single object.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Since: 3.2
 **/
void
e_cal_client_generate_instances_for_object_sync (ECalClient *client,
                                                 ICalComponent *icalcomp,
                                                 time_t start,
                                                 time_t end,
						 GCancellable *cancellable,
                                                 ECalRecurInstanceCb cb,
                                                 gpointer cb_data)
{
	const gchar *uid;
	gchar *rid;
	GHashTable *instances_by_uid;

	g_return_if_fail (E_IS_CAL_CLIENT (client));

	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	/* If the backend stores it as individual instances and does not
	 * have a master object - do not expand */
	if (!e_cal_util_component_has_recurrences (icalcomp) || e_client_check_capability (E_CLIENT (client), E_CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER)) {
		ICalTime *dtstart, *dtend;

		dtstart = i_cal_component_get_dtstart (icalcomp);
		dtend = i_cal_component_get_dtend (icalcomp);

		/* return the same instance */
		(* cb)  (icalcomp, dtstart, dtend, cb_data, cancellable, NULL);

		g_clear_object (&dtstart);
		g_clear_object (&dtend);

		return;
	}

	uid = i_cal_component_get_uid (icalcomp);
	rid = e_cal_util_component_get_recurid_as_string (icalcomp);

	instances_by_uid = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_ptr_array_unref);

	/* generate all instances in the given time range */
	generate_instances (
		client, start, end,
		get_objects_sync (client, start, end, uid),
		cancellable, add_instance_cb, instances_by_uid);

	/* it also frees 'instances' GSList */
	process_instances (client, uid, rid, instances_by_uid, cb, cb_data);

	/* clean up */
	g_hash_table_destroy (instances_by_uid);
	g_free (rid);
}

/**
 * e_cal_client_generate_instances_for_uid_sync:
 * @client: A calendar client
 * @uid: A component UID to generate instances for
 * @start: Start time for query
 * @end: End time for query
 * @cancellable: a #GCancellable; can be %NULL
 * @cb: (closure cb_data) (scope call): Callback for each generated instance
 * @cb_data: Closure data for the callback
 *
 * Does a combination of e_cal_client_get_object_list() and
 * e_cal_recur_generate_instances_sync(), like
 * e_cal_client_generate_instances_sync(), but for a single object.
 *
 * The callback function should do a g_object_ref() of the calendar component
 * it gets passed if it intends to keep it around, since it will be unref'ed
 * as soon as the callback returns.
 *
 * Since: 3.48
 **/
void
e_cal_client_generate_instances_for_uid_sync (ECalClient *client,
					      const gchar *uid,
					      time_t start,
					      time_t end,
					      GCancellable *cancellable,
					      ECalRecurInstanceCb cb,
					      gpointer cb_data)
{
	GHashTable *instances_by_uid;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (uid != NULL);
	g_return_if_fail (start >= 0);
	g_return_if_fail (end >= 0);
	g_return_if_fail (cb != NULL);

	instances_by_uid = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_ptr_array_unref);

	/* generate all instances in the given time range */
	generate_instances (
		client, start, end,
		get_objects_sync (client, start, end, uid),
		cancellable, add_instance_cb, instances_by_uid);

	/* it also frees 'instances' GSList */
	process_instances (client, uid, NULL, instances_by_uid, cb, cb_data);

	g_hash_table_destroy (instances_by_uid);
}

typedef struct _ForeachTZIDCallbackData ForeachTZIDCallbackData;
struct _ForeachTZIDCallbackData {
	ECalClient *client;
	ICalComponent *icalcomp;
	GHashTable *timezone_hash;
	gboolean success;
};

/* This adds the VTIMEZONE given by the TZID parameter to the GHashTable in
 * data. */
static void
foreach_tzid_callback (ICalParameter *param,
                       gpointer cbdata)
{
	ForeachTZIDCallbackData *data = cbdata;
	const gchar *tzid;
	ICalTimezone *zone = NULL;
	ICalComponent *vtimezone_comp, *vtimezone_clone;
	gchar *vtimezone_as_string;

	/* Get the TZID string from the parameter. */
	tzid = i_cal_parameter_get_tzid (param);
	if (!tzid)
		return;

	/* Check if we've already added it to the GHashTable. */
	if (g_hash_table_lookup (data->timezone_hash, tzid))
		return;

	if (!e_cal_client_get_timezone_sync (data->client, tzid, &zone, NULL, NULL) || !zone) {
		data->success = FALSE;
		return;
	}

	/* Convert it to a string and add it to the hash. */
	vtimezone_comp = i_cal_timezone_get_component (zone);
	if (!vtimezone_comp)
		return;

	vtimezone_clone = i_cal_component_clone (vtimezone_comp);
	e_cal_util_clamp_vtimezone_by_component (vtimezone_clone, data->icalcomp);

	vtimezone_as_string = i_cal_component_as_ical_string (vtimezone_clone);

	g_hash_table_insert (data->timezone_hash, (gchar *) tzid, vtimezone_as_string);

	g_clear_object (&vtimezone_clone);
}

/* This appends the value string to the GString given in data. */
static void
append_timezone_string (gpointer key,
                        gpointer value,
                        gpointer data)
{
	GString *vcal_string = data;

	g_string_append (vcal_string, value);
}

/**
 * e_cal_client_get_component_as_string:
 * @client: A calendar client.
 * @icalcomp: A calendar component object.
 *
 * Gets a calendar component as an iCalendar string, with a toplevel
 * VCALENDAR component and all VTIMEZONEs needed for the component.
 *
 * Returns: (nullable): the component as a complete iCalendar string, or NULL on
 * failure. The string should be freed with g_free().
 *
 * Since: 3.2
 **/
gchar *
e_cal_client_get_component_as_string (ECalClient *client,
                                      ICalComponent *icalcomp)
{
	GHashTable *timezone_hash;
	GString *vcal_string;
	ForeachTZIDCallbackData cbdata;
	gchar *obj_string;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), NULL);
	g_return_val_if_fail (icalcomp != NULL, NULL);

	timezone_hash = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

	/* Add any timezones needed to the hash. We use a hash since we only
	 * want to add each timezone once at most. */
	cbdata.client = client;
	cbdata.icalcomp = icalcomp;
	cbdata.timezone_hash = timezone_hash;
	cbdata.success = TRUE;
	i_cal_component_foreach_tzid (icalcomp, foreach_tzid_callback, &cbdata);
	if (!cbdata.success) {
		g_hash_table_destroy (timezone_hash);
		return NULL;
	}

	/* Create the start of a VCALENDAR, to add the VTIMEZONES to,
	 * and remember its length so we know if any VTIMEZONEs get added. */
	vcal_string = g_string_new (NULL);
	g_string_append (
		vcal_string,
		"BEGIN:VCALENDAR\r\n"
		"PRODID:-//Ximian//NONSGML Evolution Calendar//EN\r\n"
		"VERSION:2.0\r\n"
		"METHOD:PUBLISH\r\n");

	/* Now concatenate all the timezone strings. This also frees the
	 * timezone strings as it goes. */
	g_hash_table_foreach (timezone_hash, append_timezone_string, vcal_string);

	/* Get the string for the VEVENT/VTODO. */
	obj_string = i_cal_component_as_ical_string (icalcomp);

	/* If there were any timezones to send, create a complete VCALENDAR,
	 * else just send the VEVENT/VTODO string. */
	g_string_append (vcal_string, obj_string);
	g_string_append (vcal_string, "END:VCALENDAR\r\n");
	g_free (obj_string);

	g_hash_table_destroy (timezone_hash);

	return g_string_free (vcal_string, FALSE);
}

/* Helper for e_cal_client_get_default_object() */
static void
cal_client_get_default_object_thread (GTask *task,
                                      gpointer source_object,
                                      gpointer task_data,
                                      GCancellable *cancellable)
{
	ICalComponent *icalcomp = NULL;
	GError *local_error = NULL;

	if (!e_cal_client_get_default_object_sync (
		E_CAL_CLIENT (source_object),
		&icalcomp,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&icalcomp), g_object_unref);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_get_default_object:
 * @client: an #ECalClient
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Retrives an #ICalComponent from the backend that contains the default
 * values for properties needed. The call is finished
 * by e_cal_client_get_default_object_finish() from the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_default_object (ECalClient *client,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_CAL_CLIENT (client));

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_default_object);

	g_task_run_in_thread (task, cal_client_get_default_object_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_default_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_icalcomp: (out) (transfer full): Return value for the default calendar object.
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_default_object() and
 * sets @out_icalcomp to an #ICalComponent from the backend that contains
 * the default values for properties needed. This @out_icalcomp should be
 * freed with g_object_unref(), when no longer needed.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_default_object_finish (ECalClient *client,
                                        GAsyncResult *result,
                                        ICalComponent **out_icalcomp,
                                        GError **error)
{
	ICalComponent *icalcomp;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_default_object), FALSE);

	icalcomp = g_task_propagate_pointer (G_TASK (result), error);
	if (!icalcomp)
		return FALSE;

	if (out_icalcomp != NULL)
		*out_icalcomp = g_steal_pointer (&icalcomp);
	else
		g_clear_object (&icalcomp);

	return TRUE;
}

/**
 * e_cal_client_get_default_object_sync:
 * @client: an #ECalClient
 * @out_icalcomp: (out) (transfer full): Return value for the default calendar object.
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Retrives an #ICalComponent from the backend that contains the default
 * values for properties needed. This @out_icalcomp should be freed with
 * g_object_unref(), when no longer needed.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_default_object_sync (ECalClient *client,
                                      ICalComponent **out_icalcomp,
                                      GCancellable *cancellable,
                                      GError **error)
{
	ICalComponent *icalcomp = NULL;
	gchar *string;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (out_icalcomp != NULL, FALSE);

	string = e_dbus_calendar_dup_default_object (client->priv->dbus_proxy);
	if (string != NULL) {
		icalcomp = i_cal_parser_parse_string (string);
		g_free (string);
	}

	if (icalcomp == NULL) {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	if (i_cal_component_get_uid (icalcomp) != NULL) {
		gchar *new_uid;

		/* Make sure the UID is always unique. */
		new_uid = e_util_generate_uid ();
		i_cal_component_set_uid (icalcomp, new_uid);
		g_free (new_uid);
	}

	*out_icalcomp = icalcomp;

	return TRUE;
}

/* Helper for e_cal_client_get_object() */
static void
cal_client_get_object_thread (GTask *task,
                              gpointer source_object,
                              gpointer task_data,
                              GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;
	ICalComponent *icalcomp = NULL;

	if (!e_cal_client_get_object_sync (
		E_CAL_CLIENT (source_object),
		async_context->uid,
		async_context->rid,
		&icalcomp,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&icalcomp), g_object_unref);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_get_object:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component.
 * @rid: (nullable): Recurrence identifier.
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Queries a calendar for a calendar component object based on its unique
 * identifier. The call is finished by e_cal_client_get_object_finish()
 * from the @callback.
 *
 * Use e_cal_client_get_objects_for_uid() to get list of all
 * objects for the given uid, which includes master object and
 * all detached instances.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_object (ECalClient *client,
                         const gchar *uid,
                         const gchar *rid,
                         GCancellable *cancellable,
                         GAsyncReadyCallback callback,
                         gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (uid != NULL);
	/* rid is optional */

	async_context = g_slice_new0 (AsyncContext);
	async_context->uid = g_strdup (uid);
	async_context->rid = g_strdup (rid);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_object);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_get_object_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_icalcomp: (out) (transfer full): Return value for the calendar component object.
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_object() and
 * sets @out_icalcomp to queried component. This function always returns
 * master object for a case of @rid being NULL or an empty string.
 * This component should be freed with g_object_unref(), when no longer needed.
 *
 * Use e_cal_client_get_objects_for_uid() to get list of all
 * objects for the given uid, which includes master object and
 * all detached instances.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_finish (ECalClient *client,
                                GAsyncResult *result,
                                ICalComponent **out_icalcomp,
                                GError **error)
{
	ICalComponent *icalcomp;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_object), FALSE);

	icalcomp = g_task_propagate_pointer (G_TASK (result), error);
	if (!icalcomp)
		return FALSE;

	if (out_icalcomp != NULL)
		*out_icalcomp = g_steal_pointer (&icalcomp);
	else
		g_clear_object (&icalcomp);

	return TRUE;
}

/**
 * e_cal_client_get_object_sync:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component.
 * @rid: (nullable): Recurrence identifier.
 * @out_icalcomp: (out) (transfer full): Return value for the calendar component object.
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Queries a calendar for a calendar component object based
 * on its unique identifier. This function always returns
 * master object for a case of @rid being NULL or an empty string.
 * This component should be freed with g_object_unref(),
 * when no longer needed.
 *
 * Use e_cal_client_get_objects_for_uid_sync() to get list of all
 * objects for the given uid, which includes master object and
 * all detached instances.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_sync (ECalClient *client,
                              const gchar *uid,
                              const gchar *rid,
                              ICalComponent **out_icalcomp,
                              GCancellable *cancellable,
                              GError **error)
{
	ICalComponent *icalcomp = NULL;
	ICalComponentKind kind;
	gchar *utf8_uid;
	gchar *utf8_rid;
	gchar *string = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_icalcomp != NULL, FALSE);

	*out_icalcomp = NULL;

	if (rid == NULL)
		rid = "";

	utf8_uid = e_util_utf8_make_valid (uid);
	utf8_rid = e_util_utf8_make_valid (rid);

	e_dbus_calendar_call_get_object_sync (
		client->priv->dbus_proxy, utf8_uid, utf8_rid,
		&string, cancellable, &local_error);

	g_free (utf8_uid);
	g_free (utf8_rid);

	/* Sanity check. */
	g_return_val_if_fail (
		((string != NULL) && (local_error == NULL)) ||
		((string == NULL) && (local_error != NULL)), FALSE);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	icalcomp = i_cal_parser_parse_string (string);

	g_free (string);

	if (icalcomp == NULL) {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	switch (e_cal_client_get_source_type (client)) {
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
			kind = I_CAL_VEVENT_COMPONENT;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
			kind = I_CAL_VTODO_COMPONENT;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
			kind = I_CAL_VJOURNAL_COMPONENT;
			break;
		default:
			g_warn_if_reached ();
			kind = I_CAL_VEVENT_COMPONENT;
			break;
	}

	if (i_cal_component_isa (icalcomp) == kind) {
		*out_icalcomp = icalcomp;
		icalcomp = NULL;

	} else if (i_cal_component_isa (icalcomp) == I_CAL_VCALENDAR_COMPONENT) {
		ICalComponent *subcomponent;

		for (subcomponent = i_cal_component_get_first_component (icalcomp, kind);
		     subcomponent != NULL;
		     g_object_unref (subcomponent), subcomponent = i_cal_component_get_next_component (icalcomp, kind)) {
			ICalTime *recurrenceid;

			if (i_cal_component_get_uid (subcomponent) == NULL)
				continue;

			recurrenceid = i_cal_component_get_recurrenceid (subcomponent);

			if (!recurrenceid ||
			    i_cal_time_is_null_time (recurrenceid) ||
			    !i_cal_time_is_valid_time (recurrenceid)) {
				g_clear_object (&recurrenceid);
				break;
			}
		}

		if (subcomponent == NULL)
			subcomponent = i_cal_component_get_first_component (icalcomp, kind);
		if (subcomponent != NULL) {
			ICalComponent *clone;

			clone = i_cal_component_clone (subcomponent);
			g_object_unref (subcomponent);
			subcomponent = clone;
		}

		/* XXX Shouldn't we set an error if this is still NULL? */
		*out_icalcomp = subcomponent;
	}

	g_clear_object (&icalcomp);

	return TRUE;
}

/* Helper for e_cal_client_get_objects_for_uid() */
static void
cal_client_get_objects_for_uid_thread (GTask *task,
                                       gpointer source_object,
                                       gpointer task_data,
                                       GCancellable *cancellable)
{
	const gchar *uid = task_data;
	GError *local_error = NULL;
	GSList *ecalcomps = NULL;

	if (!e_cal_client_get_objects_for_uid_sync (
		E_CAL_CLIENT (source_object),
		uid,
		&ecalcomps,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&ecalcomps), (GDestroyNotify) e_util_free_object_slist);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_get_objects_for_uid:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Queries a calendar for all calendar components with the given unique
 * ID. This will return any recurring event and all its detached recurrences.
 * For non-recurring events, it will just return the object with that ID.
 * The call is finished by e_cal_client_get_objects_for_uid_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_objects_for_uid (ECalClient *client,
                                  const gchar *uid,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (uid != NULL);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_objects_for_uid);
	g_task_set_task_data (task, g_strdup (uid), g_free);

	g_task_run_in_thread (task, cal_client_get_objects_for_uid_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_objects_for_uid_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_ecalcomps: (out) (transfer full) (element-type ECalComponent):
 *                 Return location for the list of objects obtained from the
 *                 backend
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_objects_for_uid() and
 * sets @out_ecalcomps to a list of #ECalComponent<!-- -->s corresponding to
 * found components for a given uid of the same type as this client.
 * This list should be freed with e_client_util_free_object_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_objects_for_uid_finish (ECalClient *client,
                                         GAsyncResult *result,
                                         GSList **out_ecalcomps,
                                         GError **error)
{
	GSList *ecalcomps;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_objects_for_uid), FALSE);

	ecalcomps = g_task_propagate_pointer (G_TASK (result), error);
	if (!ecalcomps)
		return FALSE;

	if (out_ecalcomps != NULL)
		*out_ecalcomps = g_steal_pointer (&ecalcomps);
	else
		g_clear_pointer (&ecalcomps, e_util_free_object_slist);

	return TRUE;
}

/**
 * e_cal_client_get_objects_for_uid_sync:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @out_ecalcomps: (out) (transfer full) (element-type ECalComponent):
 *                 Return location for the list of objects obtained from the
 *                 backend
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Queries a calendar for all calendar components with the given unique
 * ID. This will return any recurring event and all its detached recurrences.
 * For non-recurring events, it will just return the object with that ID.
 * This list should be freed with e_client_util_free_object_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_objects_for_uid_sync (ECalClient *client,
                                       const gchar *uid,
                                       GSList **out_ecalcomps,
                                       GCancellable *cancellable,
                                       GError **error)
{
	ICalComponent *icalcomp;
	ICalComponentKind kind;
	gchar *utf8_uid;
	gchar *string = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_ecalcomps != NULL, FALSE);

	*out_ecalcomps = NULL;

	utf8_uid = e_util_utf8_make_valid (uid);

	e_dbus_calendar_call_get_object_sync (
		client->priv->dbus_proxy, utf8_uid, "",
		&string, cancellable, &local_error);

	g_free (utf8_uid);

	/* Sanity check. */
	g_return_val_if_fail (
		((string != NULL) && (local_error == NULL)) ||
		((string == NULL) && (local_error != NULL)), FALSE);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	icalcomp = i_cal_parser_parse_string (string);

	g_free (string);

	if (icalcomp == NULL) {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	switch (e_cal_client_get_source_type (client)) {
		case E_CAL_CLIENT_SOURCE_TYPE_EVENTS:
			kind = I_CAL_VEVENT_COMPONENT;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_TASKS:
			kind = I_CAL_VTODO_COMPONENT;
			break;
		case E_CAL_CLIENT_SOURCE_TYPE_MEMOS:
			kind = I_CAL_VJOURNAL_COMPONENT;
			break;
		default:
			g_warn_if_reached ();
			kind = I_CAL_VEVENT_COMPONENT;
			break;
	}

	if (i_cal_component_isa (icalcomp) == kind) {
		ECalComponent *comp;

		comp = e_cal_component_new_from_icalcomponent (icalcomp);
		icalcomp = NULL;

		*out_ecalcomps = g_slist_append (NULL, comp);

	} else if (i_cal_component_isa (icalcomp) == I_CAL_VCALENDAR_COMPONENT) {
		GSList *tmp = NULL;
		ICalComponent *subcomponent;

		for (subcomponent = i_cal_component_get_first_component (icalcomp, kind);
		     subcomponent;
		     g_object_unref (subcomponent), subcomponent = i_cal_component_get_next_component (icalcomp, kind)) {
			ECalComponent *comp;

			comp = e_cal_component_new_from_icalcomponent (i_cal_component_clone (subcomponent));
			if (comp)
				tmp = g_slist_prepend (tmp, comp);
		}

		*out_ecalcomps = g_slist_reverse (tmp);
	}

	g_clear_object (&icalcomp);

	return TRUE;
}

/* Helper for e_cal_client_get_object_list() */
static void
cal_client_get_object_list_thread (GTask *task,
                                   gpointer source_object,
                                   gpointer task_data,
                                   GCancellable *cancellable)
{
	const gchar *sexp = task_data;
	GError *local_error = NULL;
	GSList *icalcomps = NULL;

	if (!e_cal_client_get_object_list_sync (
		E_CAL_CLIENT (source_object),
		sexp,
		&icalcomps,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&icalcomps), (GDestroyNotify) e_util_free_object_slist);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_get_object_list:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @sexp argument, returning matching objects as a list of #ICalComponent-s.
 * The call is finished by e_cal_client_get_object_list_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_object_list (ECalClient *client,
                              const gchar *sexp,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (sexp != NULL);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_object_list);
	g_task_set_task_data (task, g_strdup (sexp), g_free);

	g_task_run_in_thread (task, cal_client_get_object_list_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_object_list_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_icalcomps: (out) (element-type ICalComponent): list of matching
 *                 #ICalComponent<!-- -->s
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_object_list() and
 * sets @out_icalcomps to a matching list of #ICalComponent-s.
 * This list should be freed with e_client_util_free_object_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_list_finish (ECalClient *client,
                                     GAsyncResult *result,
                                     GSList **out_icalcomps,
                                     GError **error)
{
	GSList *icalcomps;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_object_list), FALSE);

	icalcomps = g_task_propagate_pointer (G_TASK (result), error);
	if (!icalcomps)
		return FALSE;

	if (out_icalcomps != NULL)
		*out_icalcomps = g_steal_pointer (&icalcomps);
	else
		g_clear_pointer (&icalcomps, e_util_free_object_slist);

	return TRUE;
}

/**
 * e_cal_client_get_object_list_sync:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query
 * @out_icalcomps: (out) (element-type ICalComponent): list of matching
 *                 #ICalComponent<!-- -->s
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @sexp argument. The objects will be returned in the @out_icalcomps
 * argument, which is a list of #ICalComponent.
 * This list should be freed with e_client_util_free_object_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_list_sync (ECalClient *client,
                                   const gchar *sexp,
                                   GSList **out_icalcomps,
                                   GCancellable *cancellable,
                                   GError **error)
{
	GSList *tmp = NULL;
	gchar *utf8_sexp;
	gchar **strv = NULL;
	gint ii;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (out_icalcomps != NULL, FALSE);

	*out_icalcomps = NULL;

	utf8_sexp = e_util_utf8_make_valid (sexp);

	e_dbus_calendar_call_get_object_list_sync (
		client->priv->dbus_proxy, utf8_sexp,
		&strv, cancellable, &local_error);

	g_free (utf8_sexp);

	/* Sanity check. */
	g_return_val_if_fail (
		((strv != NULL) && (local_error == NULL)) ||
		((strv == NULL) && (local_error != NULL)), FALSE);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	for (ii = 0; strv[ii] != NULL; ii++) {
		ICalComponent *icalcomp;

		icalcomp = i_cal_component_new_from_string (strv[ii]);
		if (icalcomp == NULL)
			continue;

		tmp = g_slist_prepend (tmp, icalcomp);
	}

	*out_icalcomps = g_slist_reverse (tmp);

	g_strfreev (strv);

	return TRUE;
}

/* Helper for e_cal_client_get_object_list_as_comps() */
static void
cal_client_get_object_list_as_comps_thread (GTask *task,
                                            gpointer source_object,
                                            gpointer task_data,
                                            GCancellable *cancellable)
{
	const gchar *sexp = task_data;
	GError *local_error = NULL;
	GSList *ecalcomps = NULL;

	if (!e_cal_client_get_object_list_as_comps_sync (
		E_CAL_CLIENT (source_object),
		sexp,
		&ecalcomps,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}


	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&ecalcomps), (GDestroyNotify) e_util_free_object_slist);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_get_object_list_as_comps:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @sexp argument, returning matching objects as a list of #ECalComponent-s.
 * The call is finished by e_cal_client_get_object_list_as_comps_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_object_list_as_comps (ECalClient *client,
                                       const gchar *sexp,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (sexp != NULL);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_object_list_as_comps);
	g_task_set_task_data (task, g_strdup (sexp), g_free);

	g_task_run_in_thread (task, cal_client_get_object_list_as_comps_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_object_list_as_comps_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_ecalcomps: (out) (element-type ECalComponent): list of matching
 *                 #ECalComponent<!-- -->s
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_object_list_as_comps() and
 * sets @out_ecalcomps to a matching list of #ECalComponent-s.
 * This list should be freed with e_client_util_free_object_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_list_as_comps_finish (ECalClient *client,
                                              GAsyncResult *result,
                                              GSList **out_ecalcomps,
                                              GError **error)
{
	GSList *ecalcomps;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_object_list_as_comps), FALSE);

	ecalcomps = g_task_propagate_pointer (G_TASK (result), error);
	if (!ecalcomps)
		return FALSE;

	if (out_ecalcomps != NULL)
		*out_ecalcomps = g_steal_pointer (&ecalcomps);
	else
		g_clear_pointer (&ecalcomps, e_util_free_object_slist);

	return TRUE;
}

/**
 * e_cal_client_get_object_list_as_comps_sync:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query
 * @out_ecalcomps: (out) (element-type ECalComponent): list of matching
 *                 #ECalComponent<!-- -->s
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Gets a list of objects from the calendar that match the query specified
 * by the @sexp argument. The objects will be returned in the @out_ecalcomps
 * argument, which is a list of #ECalComponent.
 * This list should be freed with e_client_util_free_object_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_object_list_as_comps_sync (ECalClient *client,
                                            const gchar *sexp,
                                            GSList **out_ecalcomps,
                                            GCancellable *cancellable,
                                            GError **error)
{
	GSList *list = NULL;
	GSList *link;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (out_ecalcomps != NULL, FALSE);

	*out_ecalcomps = NULL;

	success = e_cal_client_get_object_list_sync (
		client, sexp, &list, cancellable, error);

	if (!success) {
		g_warn_if_fail (list == NULL);
		return FALSE;
	}

	/* Convert the ICalComponent list to an ECalComponent list. */
	for (link = list; link != NULL; link = g_slist_next (link)) {
		ECalComponent *comp;
		ICalComponent *icalcomp = link->data;

		/* This takes ownership of the ICalComponent. */
		comp = e_cal_component_new_from_icalcomponent (icalcomp);
		if (comp)
			*out_ecalcomps = g_slist_prepend (*out_ecalcomps, comp);

	}

	g_slist_free (list);

	*out_ecalcomps = g_slist_reverse (*out_ecalcomps);

	return TRUE;
}

/* Helper for e_cal_client_get_free_busy() */
static void
cal_client_get_free_busy_thread (GTask *task,
                                 gpointer source_object,
                                 gpointer task_data,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;
	GSList *freebusy = NULL;

	if (!e_cal_client_get_free_busy_sync (
		E_CAL_CLIENT (source_object),
		async_context->start,
		async_context->end,
		async_context->string_list,
		&freebusy,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&freebusy), (GDestroyNotify) e_util_free_object_slist);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_get_free_busy:
 * @client: an #ECalClient
 * @start: Start time for query
 * @end: End time for query
 * @users: (element-type utf8): List of users to retrieve free/busy information for
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Begins retrieval of free/busy information from the calendar server
 * as a list of #ECalComponent-s. Connect to "free-busy-data" signal
 * to receive chunks of free/busy components.
 * The call is finished by e_cal_client_get_free_busy_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_free_busy (ECalClient *client,
                            time_t start,
                            time_t end,
                            const GSList *users,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	AsyncContext *async_context;
	GTask *task;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (start > 0);
	g_return_if_fail (end > 0);

	async_context = g_slice_new0 (AsyncContext);
	async_context->start = start;
	async_context->end = end;
	async_context->string_list = g_slist_copy_deep (
		(GSList *) users, (GCopyFunc) g_strdup, NULL);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_free_busy);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_get_free_busy_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_free_busy_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_freebusy: (out) (element-type ECalComponent): a #GSList of #ECalComponent-s with overall returned Free/Busy data
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_free_busy().
 * The @out_freebusy contains all VFREEBUSY #ECalComponent-s, which could be also
 * received by "free-busy-data" signal. The client is responsible to do a merge of
 * the components between this complete list and those received through the signal.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_free_busy_finish (ECalClient *client,
                                   GAsyncResult *result,
				   GSList **out_freebusy,
                                   GError **error)
{
	GSList *freebusy;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_free_busy), FALSE);

	freebusy = g_task_propagate_pointer (G_TASK (result), error);
	if (!freebusy)
		return FALSE;

	if (out_freebusy != NULL)
		*out_freebusy = g_steal_pointer (&freebusy);
	else
		g_clear_pointer (&freebusy, e_util_free_object_slist);

	return TRUE;
}

/**
 * e_cal_client_get_free_busy_sync:
 * @client: an #ECalClient
 * @start: Start time for query
 * @end: End time for query
 * @users: (element-type utf8): List of users to retrieve free/busy information for
 * @out_freebusy: (out) (element-type ECalComponent): a #GSList of #ECalComponent-s with overall returned Free/Busy data
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Gets free/busy information from the calendar server.
 * The @out_freebusy contains all VFREEBUSY #ECalComponent-s, which could be also
 * received by "free-busy-data" signal. The client is responsible to do a merge of
 * the components between this complete list and those received through the signal.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_free_busy_sync (ECalClient *client,
                                 time_t start,
                                 time_t end,
                                 const GSList *users,
				 GSList **out_freebusy,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gchar **strv, **freebusy_strv = NULL;
	gint ii = 0;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (start > 0, FALSE);
	g_return_val_if_fail (end > 0, FALSE);

	strv = g_new0 (gchar *, g_slist_length ((GSList *) users) + 1);
	while (users != NULL) {
		strv[ii++] = e_util_utf8_make_valid (users->data);
		users = g_slist_next (users);
	}

	e_dbus_calendar_call_get_free_busy_sync (
		client->priv->dbus_proxy,
		(gint64) start, (gint64) end,
		(const gchar * const *) strv,
		&freebusy_strv,
		cancellable, &local_error);

	g_strfreev (strv);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	if (out_freebusy) {
		*out_freebusy = NULL;

		for (ii = 0; freebusy_strv && freebusy_strv[ii] != NULL; ii++) {
			ECalComponent *comp;

			comp = e_cal_component_new_from_string (freebusy_strv[ii]);
			if (comp)
				*out_freebusy = g_slist_prepend (*out_freebusy, comp);
		}

		*out_freebusy = g_slist_reverse (*out_freebusy);
	}

	g_strfreev (freebusy_strv);

	return TRUE;
}

static gchar *
e_cal_client_sanitize_comp_as_string (ICalComponent *icomp)
{
	gchar *utf8_string;
	gchar *ical_string;

	if (i_cal_component_count_errors (icomp) > 0) {
		ICalComponent *clone;

		clone = i_cal_component_clone (icomp);

		i_cal_component_strip_errors (clone);

		ical_string = i_cal_component_as_ical_string (clone);

		g_clear_object (&clone);
	} else {
		ical_string = i_cal_component_as_ical_string (icomp);
	}

	utf8_string = e_util_utf8_make_valid (ical_string);

	g_free (ical_string);

	return utf8_string;
}

/* Helper for e_cal_client_create_object() */
static void
cal_client_create_object_thread (GTask *task,
                                 gpointer source_object,
                                 gpointer task_data,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;
	gchar *uid = NULL;

	if (!e_cal_client_create_object_sync (
		E_CAL_CLIENT (source_object),
		async_context->in_comp,
		async_context->opflags,
		&uid,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&uid), g_free);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_create_object:
 * @client: an #ECalClient
 * @icalcomp: The component to create
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests the calendar backend to create the object specified by the @icalcomp
 * argument. Some backends would assign a specific UID to the newly created object,
 * but this function does not modify the original @icalcomp if its UID changes.
 * The call is finished by e_cal_client_create_object_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_create_object (ECalClient *client,
                            ICalComponent *icalcomp,
			    ECalOperationFlags opflags,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (icalcomp != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->in_comp = i_cal_component_clone (icalcomp);
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_create_object);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_create_object_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_create_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_uid: (out) (optional): Return value for the UID assigned to the new component
 *           by the calendar backend
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_create_object() and
 * sets @out_uid to newly assigned UID for the created object.
 * This @out_uid should be freed with g_free().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_create_object_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   gchar **out_uid,
                                   GError **error)
{
	gchar *uid;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_create_object), FALSE);

	uid = g_task_propagate_pointer (G_TASK (result), error);
	if (!uid)
		return FALSE;

	if (out_uid != NULL)
		*out_uid = g_steal_pointer (&uid);
	else
		g_clear_pointer (&uid, g_free);

	return TRUE;
}

/**
 * e_cal_client_create_object_sync:
 * @client: an #ECalClient
 * @icalcomp: The component to create
 * @opflags: bit-or of #ECalOperationFlags
 * @out_uid: (out) (nullable) (optional): Return value for the UID assigned to the new component
 *           by the calendar backend
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Requests the calendar backend to create the object specified by the
 * @icalcomp argument. Some backends would assign a specific UID to the newly
 * created object, in those cases that UID would be returned in the @out_uid
 * argument. This function does not modify the original @icalcomp if its UID
 * changes.  Returned @out_uid should be freed with g_free().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_create_object_sync (ECalClient *client,
                                 ICalComponent *icalcomp,
				 ECalOperationFlags opflags,
                                 gchar **out_uid,
                                 GCancellable *cancellable,
                                 GError **error)
{
	GSList link = { icalcomp, NULL };
	GSList *string_list = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	success = e_cal_client_create_objects_sync (
		client, &link, opflags, &string_list, cancellable, error);

	/* Sanity check. */
	g_return_val_if_fail (
		(success && (string_list != NULL)) ||
		(!success && (string_list == NULL)), FALSE);

	if (out_uid != NULL && string_list != NULL)
		*out_uid = g_strdup (string_list->data);
	else if (out_uid)
		*out_uid = NULL;

	g_slist_free_full (string_list, (GDestroyNotify) g_free);

	return success;
}

/* Helper for e_cal_client_create_objects() */
static void
cal_client_create_objects_thread (GTask *task,
                                  gpointer source_object,
                                  gpointer task_data,
                                  GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;
	GSList *uids = NULL;

	if (!e_cal_client_create_objects_sync (
		E_CAL_CLIENT (source_object),
		async_context->comp_list,
		async_context->opflags,
		&uids,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&uids), (GDestroyNotify) e_util_free_string_slist);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_create_objects:
 * @client: an #ECalClient
 * @icalcomps: (element-type ICalComponent): The components to create
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests the calendar backend to create the objects specified by the @icalcomps
 * argument. Some backends would assign a specific UID to the newly created object,
 * but this function does not modify the original @icalcomps if their UID changes.
 * The call is finished by e_cal_client_create_objects_finish() from
 * the @callback.
 *
 * Since: 3.6
 **/
void
e_cal_client_create_objects (ECalClient *client,
                             GSList *icalcomps,
			     ECalOperationFlags opflags,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (icalcomps != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->comp_list = g_slist_copy_deep (
		icalcomps, (GCopyFunc) i_cal_component_clone, NULL);
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_create_objects);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_create_objects_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_create_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_uids: (out) (optional) (element-type utf8): Return value for the UIDs assigned
 *            to the new components by the calendar backend
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_create_objects() and
 * sets @out_uids to newly assigned UIDs for the created objects.
 * This @out_uids should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_create_objects_finish (ECalClient *client,
                                    GAsyncResult *result,
                                    GSList **out_uids,
                                    GError **error)
{
	GSList *uids;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_create_objects), FALSE);

	uids = g_task_propagate_pointer (G_TASK (result), error);
	if (!uids)
		return FALSE;

	if (out_uids != NULL)
		*out_uids = g_steal_pointer (&uids);
	else
		g_clear_pointer (&uids, e_util_free_string_slist);

	return TRUE;
}

/**
 * e_cal_client_create_objects_sync:
 * @client: an #ECalClient
 * @icalcomps: (element-type ICalComponent): The components to create
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @out_uids: (out) (optional) (element-type utf8): Return value for the UIDs assigned
 *            to the new components by the calendar backend
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Requests the calendar backend to create the objects specified by the
 * @icalcomps argument. Some backends would assign a specific UID to the
 * newly created objects, in those cases these UIDs would be returned in
 * the @out_uids argument. This function does not modify the original
 * @icalcomps if their UID changes.  Returned @out_uids should be freed
 * with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_create_objects_sync (ECalClient *client,
                                  GSList *icalcomps,
				  ECalOperationFlags opflags,
                                  GSList **out_uids,
                                  GCancellable *cancellable,
                                  GError **error)
{
	gchar **strv;
	gchar **uids = NULL;
	gint ii = 0;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomps != NULL, FALSE);

	strv = g_new0 (gchar *, g_slist_length (icalcomps) + 1);
	while (icalcomps != NULL) {
		ICalComponent *icomp = icalcomps->data;

		strv[ii++] = e_cal_client_sanitize_comp_as_string (icomp);

		icalcomps = g_slist_next (icalcomps);
	}

	e_dbus_calendar_call_create_objects_sync (
		client->priv->dbus_proxy,
		(const gchar * const *) strv,
		opflags, &uids, cancellable, &local_error);

	g_strfreev (strv);

	/* Sanity check. */
	g_return_val_if_fail (
		((uids != NULL) && (local_error == NULL)) ||
		((uids == NULL) && (local_error != NULL)), FALSE);

	if (uids && out_uids) {
		GSList *tmp = NULL;

		/* Steal the string array elements. */
		for (ii = 0; uids[ii] != NULL; ii++) {
			tmp = g_slist_prepend (tmp, uids[ii]);
			uids[ii] = NULL;
		}

		*out_uids = g_slist_reverse (tmp);
	} else if (out_uids) {
		*out_uids = NULL;
	}

	g_strfreev (uids);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for e_cal_client_modify_object() */
static void
cal_client_modify_object_thread (GTask *task,
                                 gpointer source_object,
                                 gpointer task_data,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;

	if (!e_cal_client_modify_object_sync (
		E_CAL_CLIENT (source_object),
		async_context->in_comp,
		async_context->mod,
		async_context->opflags,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_modify_object:
 * @client: an #ECalClient
 * @icalcomp: Component to modify
 * @mod: Type of modification
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests the calendar backend to modify an existing object. If the object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (#E_CAL_OBJ_MOD_ALL), a single instance (#E_CAL_OBJ_MOD_THIS),
 * or a specific set of instances (#E_CAL_OBJ_MOD_THIS_AND_PRIOR and
 * #E_CAL_OBJ_MOD_THIS_AND_FUTURE).
 *
 * The call is finished by e_cal_client_modify_object_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_modify_object (ECalClient *client,
                            ICalComponent *icalcomp,
                            ECalObjModType mod,
			    ECalOperationFlags opflags,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (icalcomp != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->in_comp = i_cal_component_clone (icalcomp);
	async_context->mod = mod;
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_modify_object);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_modify_object_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_modify_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_modify_object().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_modify_object_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_modify_object), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_cal_client_modify_object_sync:
 * @client: an #ECalClient
 * @icalcomp: Component to modify
 * @mod: Type of modification
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Requests the calendar backend to modify an existing object. If the object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (#E_CAL_OBJ_MOD_ALL), a single instance (#E_CAL_OBJ_MOD_THIS),
 * or a specific set of instances (#E_CAL_OBJ_MOD_THIS_AND_PRIOR and
 * #E_CAL_OBJ_MOD_THIS_AND_FUTURE).
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_modify_object_sync (ECalClient *client,
                                 ICalComponent *icalcomp,
                                 ECalObjModType mod,
				 ECalOperationFlags opflags,
                                 GCancellable *cancellable,
                                 GError **error)
{
	GSList link = { icalcomp, NULL };

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	return e_cal_client_modify_objects_sync (
		client, &link, mod, opflags, cancellable, error);
}

/* Helper for e_cal_client_modify_objects() */
static void
cal_client_modify_objects_thread (GTask *task,
                                  gpointer source_object,
                                  gpointer task_data,
                                  GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;

	if (!e_cal_client_modify_objects_sync (
		E_CAL_CLIENT (source_object),
		async_context->comp_list,
		async_context->mod,
		async_context->opflags,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_modify_objects:
 * @client: an #ECalClient
 * @icalcomps: (element-type ICalComponent): Components to modify
 * @mod: Type of modification
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests the calendar backend to modify existing objects. If an object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (#E_CAL_OBJ_MOD_ALL), a single instance (#E_CAL_OBJ_MOD_THIS),
 * or a specific set of instances (#E_CAL_OBJ_MOD_THIS_AND_PRIOR and
 * #E_CAL_OBJ_MOD_THIS_AND_FUTURE).
 *
 * The call is finished by e_cal_client_modify_objects_finish() from
 * the @callback.
 *
 * Since: 3.6
 **/
void
e_cal_client_modify_objects (ECalClient *client,
                             GSList *icalcomps,
                             ECalObjModType mod,
			     ECalOperationFlags opflags,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (icalcomps != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->comp_list = g_slist_copy_deep (
		icalcomps, (GCopyFunc) i_cal_component_clone, NULL);
	async_context->mod = mod;
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_modify_objects);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_modify_objects_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_modify_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_modify_objects().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_modify_objects_finish (ECalClient *client,
                                    GAsyncResult *result,
                                    GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_modify_objects), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_cal_client_modify_objects_sync:
 * @client: an #ECalClient
 * @icalcomps: (element-type ICalComponent): Components to modify
 * @mod: Type of modification
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Requests the calendar backend to modify existing objects. If an object
 * does not exist on the calendar, an error will be returned.
 *
 * For recurrent appointments, the @mod argument specifies what to modify,
 * if all instances (#E_CAL_OBJ_MOD_ALL), a single instance (#E_CAL_OBJ_MOD_THIS),
 * or a specific set of instances (#E_CAL_OBJ_MOD_THIS_AND_PRIOR and
 * #E_CAL_OBJ_MOD_THIS_AND_FUTURE).
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_modify_objects_sync (ECalClient *client,
                                  GSList *icalcomps,
                                  ECalObjModType mod,
				  ECalOperationFlags opflags,
                                  GCancellable *cancellable,
                                  GError **error)
{
	GFlagsClass *flags_class;
	GFlagsValue *flags_value;
	GString *mod_flags;
	gchar **strv;
	gint ii = 0;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomps != NULL, FALSE);

	mod_flags = g_string_new (NULL);
	flags_class = g_type_class_ref (E_TYPE_CAL_OBJ_MOD_TYPE);
	for (flags_value = g_flags_get_first_value (flags_class, mod);
	     flags_value && mod;
	     flags_value = g_flags_get_first_value (flags_class, mod)) {
		if (mod_flags->len > 0)
			g_string_append_c (mod_flags, ':');
		g_string_append (mod_flags, flags_value->value_nick);
		mod &= ~flags_value->value;
	}

	strv = g_new0 (gchar *, g_slist_length (icalcomps) + 1);
	while (icalcomps != NULL) {
		ICalComponent *icomp = icalcomps->data;

		strv[ii++] = e_cal_client_sanitize_comp_as_string (icomp);

		icalcomps = g_slist_next (icalcomps);
	}

	e_dbus_calendar_call_modify_objects_sync (
		client->priv->dbus_proxy,
		(const gchar * const *) strv,
		mod_flags->str, opflags, cancellable, &local_error);

	g_strfreev (strv);

	g_type_class_unref (flags_class);
	g_string_free (mod_flags, TRUE);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for e_cal_client_remove_object() */
static void
cal_client_remove_object_thread (GTask *task,
                                 gpointer source_object,
                                 gpointer task_data,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;

	if (!e_cal_client_remove_object_sync (
		E_CAL_CLIENT (source_object),
		async_context->uid,
		async_context->rid,
		async_context->mod,
		async_context->opflags,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_remove_object:
 * @client: an #ECalClient
 * @uid: UID of the object to remove
 * @rid: (nullable): Recurrence ID of the specific recurrence to remove
 * @mod: Type of the removal
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * This function allows the removal of instances of a recurrent
 * appointment. By using a combination of the @uid, @rid and @mod
 * arguments, you can remove specific instances. If what you want
 * is to remove all instances, use %NULL @rid and #E_CAL_OBJ_MOD_ALL
 * for the @mod.
 *
 * The call is finished by e_cal_client_remove_object_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_remove_object (ECalClient *client,
                            const gchar *uid,
                            const gchar *rid,
                            ECalObjModType mod,
			    ECalOperationFlags opflags,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (uid != NULL);
	/* rid is optional */

	async_context = g_slice_new0 (AsyncContext);
	async_context->uid = g_strdup (uid);
	async_context->rid = g_strdup (rid);
	async_context->mod = mod;
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_remove_object);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_remove_object_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_remove_object_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_remove_object().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_remove_object_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_remove_object), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_cal_client_remove_object_sync:
 * @client: an #ECalClient
 * @uid: UID of the object to remove
 * @rid: (nullable): Recurrence ID of the specific recurrence to remove
 * @mod: Type of the removal
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * This function allows the removal of instances of a recurrent
 * appointment. By using a combination of the @uid, @rid and @mod
 * arguments, you can remove specific instances. If what you want
 * is to remove all instances, use %NULL @rid and #E_CAL_OBJ_MOD_ALL
 * for the @mod.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_remove_object_sync (ECalClient *client,
                                 const gchar *uid,
                                 const gchar *rid,
                                 ECalObjModType mod,
				 ECalOperationFlags opflags,
                                 GCancellable *cancellable,
                                 GError **error)
{
	ECalComponentId *id;
	GSList *link;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	id = e_cal_component_id_new (uid, rid);
	link = g_slist_prepend (NULL, id);

	success = e_cal_client_remove_objects_sync (client, link, mod, opflags, cancellable, error);

	g_slist_free_full (link, e_cal_component_id_free);

	return success;
}

/* Helper for e_cal_client_remove_objects() */
static void
cal_client_remove_objects_thread (GTask *task,
                                  gpointer source_object,
                                  gpointer task_data,
                                  GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;

	if (!e_cal_client_remove_objects_sync (
		E_CAL_CLIENT (source_object),
		async_context->ids_list,
		async_context->mod,
		async_context->opflags,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_remove_objects:
 * @client: an #ECalClient
 * @ids: (element-type ECalComponentId): A list of #ECalComponentId objects
 * identifying the objects to remove
 * @mod: Type of the removal
 * @opflags: bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * This function allows the removal of instances of recurrent appointments.
 * #ECalComponentId objects can identify specific instances (if rid is not
 * %NULL).  If what you want is to remove all instances, use a %NULL rid in
 * the #ECalComponentId and #E_CAL_OBJ_MOD_ALL for the @mod.
 *
 * The call is finished by e_cal_client_remove_objects_finish() from
 * the @callback.
 *
 * Since: 3.6
 **/
void
e_cal_client_remove_objects (ECalClient *client,
                             const GSList *ids,
                             ECalObjModType mod,
			     ECalOperationFlags opflags,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (ids != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->ids_list = g_slist_copy_deep (
		(GSList *) ids, (GCopyFunc) e_cal_component_id_copy, NULL);
	async_context->mod = mod;
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_remove_objects);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_remove_objects_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_remove_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_remove_objects().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_remove_objects_finish (ECalClient *client,
                                    GAsyncResult *result,
                                    GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_remove_objects), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_cal_client_remove_objects_sync:
 * @client: an #ECalClient
 * @ids: (element-type ECalComponentId): a list of #ECalComponentId objects
 *       identifying the objects to remove
 * @mod: Type of the removal
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * This function allows the removal of instances of recurrent
 * appointments. #ECalComponentId objects can identify specific instances
 * (if rid is not %NULL).  If what you want is to remove all instances, use
 * a %NULL rid in the #ECalComponentId and #E_CAL_OBJ_MOD_ALL for the @mod.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.6
 **/
gboolean
e_cal_client_remove_objects_sync (ECalClient *client,
                                  const GSList *ids,
                                  ECalObjModType mod,
				  ECalOperationFlags opflags,
                                  GCancellable *cancellable,
                                  GError **error)
{
	GVariantBuilder builder;
	GFlagsClass *flags_class;
	GFlagsValue *flags_value;
	GString *mod_flags;
	guint n_valid_uids = 0;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (ids != NULL, FALSE);

	mod_flags = g_string_new (NULL);
	flags_class = g_type_class_ref (E_TYPE_CAL_OBJ_MOD_TYPE);
	for (flags_value = g_flags_get_first_value (flags_class, mod);
	     flags_value && mod;
	     flags_value = g_flags_get_first_value (flags_class, mod)) {
		if (mod_flags->len > 0)
			g_string_append_c (mod_flags, ':');
		g_string_append (mod_flags, flags_value->value_nick);
		mod &= ~flags_value->value;
	}

	g_variant_builder_init (&builder, G_VARIANT_TYPE ("a(ss)"));
	while (ids != NULL) {
		ECalComponentId *id = ids->data;
		const gchar *uid, *rid;
		gchar *utf8_uid;
		gchar *utf8_rid;

		ids = g_slist_next (ids);

		uid = e_cal_component_id_get_uid (id);
		rid = e_cal_component_id_get_rid (id);

		if (!uid)
			continue;

		/* Reject empty UIDs with an OBJECT_NOT_FOUND error for
		 * backward-compatibility, even though INVALID_ARG might
		 * be more appropriate. */
		if (!*uid) {
			local_error = g_error_new_literal (
				E_CAL_CLIENT_ERROR,
				E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND,
				e_cal_client_error_to_string (
				E_CAL_CLIENT_ERROR_OBJECT_NOT_FOUND));
			n_valid_uids = 0;
			break;
		}

		utf8_uid = e_util_utf8_make_valid (uid);
		if (rid)
			utf8_rid = e_util_utf8_make_valid (rid);
		else
			utf8_rid = g_strdup ("");

		g_variant_builder_add (&builder, "(ss)", utf8_uid, utf8_rid);

		g_free (utf8_uid);
		g_free (utf8_rid);

		n_valid_uids++;
	}

	if (n_valid_uids > 0) {
		e_dbus_calendar_call_remove_objects_sync (
			client->priv->dbus_proxy,
			g_variant_builder_end (&builder),
			mod_flags->str, opflags, cancellable, &local_error);
	} else {
		g_variant_builder_clear (&builder);
	}

	g_type_class_unref (flags_class);
	g_string_free (mod_flags, TRUE);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for e_cal_client_receive_objects() */
static void
cal_client_receive_objects_thread (GTask *task,
                                   gpointer source_object,
                                   gpointer task_data,
                                   GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;

	if (!e_cal_client_receive_objects_sync (
		E_CAL_CLIENT (source_object),
		async_context->in_comp,
		async_context->opflags,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_receive_objects:
 * @client: an #ECalClient
 * @icalcomp: An #ICalComponent
 * @opflags: bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Makes the backend receive the set of iCalendar objects specified in the
 * @icalcomp argument. This is used for iTIP confirmation/cancellation
 * messages for scheduled meetings.
 *
 * The call is finished by e_cal_client_receive_objects_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_receive_objects (ECalClient *client,
                              ICalComponent *icalcomp,
			      ECalOperationFlags opflags,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (icalcomp != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->in_comp = i_cal_component_clone (icalcomp);
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_receive_objects);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_receive_objects_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_receive_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_receive_objects().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_receive_objects_finish (ECalClient *client,
                                     GAsyncResult *result,
                                     GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_receive_objects), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_cal_client_receive_objects_sync:
 * @client: an #ECalClient
 * @icalcomp: An #ICalComponent
 * @opflags: bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Makes the backend receive the set of iCalendar objects specified in the
 * @icalcomp argument. This is used for iTIP confirmation/cancellation
 * messages for scheduled meetings.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_receive_objects_sync (ECalClient *client,
                                   ICalComponent *icalcomp,
				   ECalOperationFlags opflags,
                                   GCancellable *cancellable,
                                   GError **error)
{
	gchar *ical_string;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);

	ical_string = e_cal_client_sanitize_comp_as_string (icalcomp);

	e_dbus_calendar_call_receive_objects_sync (
		client->priv->dbus_proxy, ical_string, opflags,
		cancellable, &local_error);

	g_free (ical_string);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

static void
send_object_result_free (SendObjectResult *res)
{
	if (!res)
		return;

	g_clear_pointer (&res->users, e_util_free_string_slist);
	g_clear_object (&res->modified_icalcomp);
	g_free (res);
}

/* Helper for e_cal_client_send_objects() */
static void
cal_client_send_objects_thread (GTask *task,
                                gpointer source_object,
                                gpointer task_data,
                                GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;
	SendObjectResult *result = g_new0 (SendObjectResult, 1);

	if (!e_cal_client_send_objects_sync (
		E_CAL_CLIENT (source_object),
		async_context->in_comp,
		async_context->opflags,
		&result->users,
		&result->modified_icalcomp,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&result), (GDestroyNotify) send_object_result_free);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));

	g_clear_pointer (&result, send_object_result_free);
}

/**
 * e_cal_client_send_objects:
 * @client: an #ECalClient
 * @icalcomp: An #ICalComponent to be sent
 * @opflags: bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Requests a calendar backend to send meeting information stored in @icalcomp.
 * The backend can modify this component and request a send to particular users.
 * The call is finished by e_cal_client_send_objects_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_send_objects (ECalClient *client,
                           ICalComponent *icalcomp,
			   ECalOperationFlags opflags,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (icalcomp != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->in_comp = i_cal_component_clone (icalcomp);
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_send_objects);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_send_objects_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_send_objects_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_users: (out) (transfer full) (element-type utf8): List of users to send
 *             the @out_modified_icalcomp to
 * @out_modified_icalcomp: (out) (transfer full): Return value for the #ICalComponent to be sent
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_send_objects() and
 * populates @out_users with a list of users to send @out_modified_icalcomp to.
 *
 * The @out_users list should be freed with e_client_util_free_string_slist()
 * and the @out_modified_icalcomp should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_send_objects_finish (ECalClient *client,
                                  GAsyncResult *result,
                                  GSList **out_users,
                                  ICalComponent **out_modified_icalcomp,
                                  GError **error)
{
	SendObjectResult *send_res;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_send_objects), FALSE);

	send_res = g_task_propagate_pointer (G_TASK (result), error);
	if (!send_res)
		return FALSE;

	if (out_users != NULL)
		*out_users = g_steal_pointer (&send_res->users);

	if (out_modified_icalcomp != NULL)
		*out_modified_icalcomp = g_steal_pointer (&send_res->modified_icalcomp);

	g_clear_pointer (&send_res, send_object_result_free);
	return TRUE;
}

/**
 * e_cal_client_send_objects_sync:
 * @client: an #ECalClient
 * @icalcomp: An #ICalComponent to be sent
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @out_users: (out) (transfer full) (element-type utf8): List of users to send the
 *             @out_modified_icalcomp to
 * @out_modified_icalcomp: (out) (transfer full): Return value for the #ICalComponent to be sent
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Requests a calendar backend to send meeting information stored in @icalcomp.
 * The backend can modify this component and request a send to users in the
 * @out_users list.
 *
 * The @out_users list should be freed with e_client_util_free_string_slist()
 * and the @out_modified_icalcomp should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_send_objects_sync (ECalClient *client,
                                ICalComponent *icalcomp,
				ECalOperationFlags opflags,
                                GSList **out_users,
                                ICalComponent **out_modified_icalcomp,
                                GCancellable *cancellable,
                                GError **error)
{
	gchar *ical_string;
	gchar **users = NULL;
	gchar *out_ical_string = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);
	g_return_val_if_fail (out_users != NULL, FALSE);
	g_return_val_if_fail (out_modified_icalcomp != NULL, FALSE);

	ical_string = e_cal_client_sanitize_comp_as_string (icalcomp);

	e_dbus_calendar_call_send_objects_sync (
		client->priv->dbus_proxy, ical_string, opflags,
		&users, &out_ical_string, cancellable, &local_error);

	g_free (ical_string);

	/* Sanity check. */
	g_return_val_if_fail (
		((out_ical_string != NULL) && (local_error == NULL)) ||
		((out_ical_string == NULL) && (local_error != NULL)), FALSE);

	if (local_error != NULL) {
		g_warn_if_fail (users == NULL);
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	icalcomp = i_cal_parser_parse_string (out_ical_string);

	g_free (out_ical_string);

	if (icalcomp != NULL) {
		*out_modified_icalcomp = icalcomp;
	} else {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		g_strfreev (users);
		return FALSE;
	}

	if (users != NULL) {
		GSList *tmp = NULL;
		gint ii;

		for (ii = 0; users[ii] != NULL; ii++) {
			tmp = g_slist_prepend (tmp, users[ii]);
			users[ii] = NULL;
		}

		*out_users = g_slist_reverse (tmp);
	}

	g_strfreev (users);

	return TRUE;
}

/* Helper for e_cal_client_get_attachment_uris() */
static void
cal_client_get_attachment_uris_thread (GTask *task,
                                       gpointer source_object,
                                       gpointer task_data,
                                       GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;
	GSList *string_list = NULL;

	if (!e_cal_client_get_attachment_uris_sync (
		E_CAL_CLIENT (source_object),
		async_context->uid,
		async_context->rid,
		&string_list,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, g_steal_pointer (&string_list), (GDestroyNotify) e_util_free_string_slist);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_get_attachment_uris:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @rid: (nullable): Recurrence identifier
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Queries a calendar for a specified component's object attachment uris.
 * The call is finished by e_cal_client_get_attachment_uris_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_attachment_uris (ECalClient *client,
                                  const gchar *uid,
                                  const gchar *rid,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_CAL_CLIENT (client));
	g_return_if_fail (uid != NULL);
	/* rid is optional */

	async_context = g_slice_new0 (AsyncContext);
	async_context->uid = g_strdup (uid);
	async_context->rid = g_strdup (rid);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_attachment_uris);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_get_attachment_uris_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_attachment_uris_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_attachment_uris: (out) (element-type utf8): Return location for the
 *                       list of attachment URIs
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_attachment_uris() and
 * sets @out_attachment_uris to uris for component's attachments.
 * The list should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_attachment_uris_finish (ECalClient *client,
                                         GAsyncResult *result,
                                         GSList **out_attachment_uris,
                                         GError **error)
{
	GSList *attachment_uris;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_attachment_uris), FALSE);

	attachment_uris = g_task_propagate_pointer (G_TASK (result), error);
	if (!attachment_uris)
		return FALSE;

	if (out_attachment_uris != NULL)
		*out_attachment_uris = g_steal_pointer (&attachment_uris);
	else
		g_slist_free_full (attachment_uris, g_free);

	return TRUE;
}

/**
 * e_cal_client_get_attachment_uris_sync:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @rid: (nullable): Recurrence identifier
 * @out_attachment_uris: (out) (element-type utf8): Return location for the
 *                       list of attachment URIs
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Queries a calendar for a specified component's object attachment URIs.
 * The list should be freed with e_client_util_free_string_slist().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_attachment_uris_sync (ECalClient *client,
                                       const gchar *uid,
                                       const gchar *rid,
                                       GSList **out_attachment_uris,
                                       GCancellable *cancellable,
                                       GError **error)
{
	gchar *utf8_uid;
	gchar *utf8_rid;
	gchar **uris = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (out_attachment_uris != NULL, FALSE);

	if (rid == NULL)
		rid = "";

	utf8_uid = e_util_utf8_make_valid (uid);
	utf8_rid = e_util_utf8_make_valid (rid);

	e_dbus_calendar_call_get_attachment_uris_sync (
		client->priv->dbus_proxy, utf8_uid, utf8_rid,
		&uris, cancellable, &local_error);

	g_free (utf8_uid);
	g_free (utf8_rid);

	/* Sanity check. */
	g_return_val_if_fail (
		((uris != NULL) && (local_error == NULL)) ||
		((uris == NULL) && (local_error != NULL)), FALSE);

	if (uris != NULL) {
		GSList *tmp = NULL;
		gint ii;

		for (ii = 0; uris[ii] != NULL; ii++) {
			tmp = g_slist_prepend (tmp, uris[ii]);
			uris[ii] = NULL;
		}

		*out_attachment_uris = g_slist_reverse (tmp);

		g_free (uris);
	}

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for e_cal_client_discard_alarm() */
static void
cal_client_discard_alarm_thread (GTask *task,
                                 gpointer source_object,
                                 gpointer task_data,
                                 GCancellable *cancellable)
{
	AsyncContext *async_context = task_data;
	GError *local_error = NULL;

	if (!e_cal_client_discard_alarm_sync (
		E_CAL_CLIENT (source_object),
		async_context->uid,
		async_context->rid,
		async_context->auid,
		async_context->opflags,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_discard_alarm:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @rid: (nullable): Recurrence identifier
 * @auid: Alarm identifier to discard
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Discards alarm @auid from a given component identified by @uid and @rid.
 * The call is finished by e_cal_client_discard_alarm_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_discard_alarm (ECalClient *client,
                            const gchar *uid,
                            const gchar *rid,
                            const gchar *auid,
			    ECalOperationFlags opflags,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GTask *task;
	AsyncContext *async_context;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (uid != NULL);
	/* rid is optional */
	g_return_if_fail (auid != NULL);

	async_context = g_slice_new0 (AsyncContext);
	async_context->uid = g_strdup (uid);
	async_context->rid = g_strdup (rid);
	async_context->auid = g_strdup (auid);
	async_context->opflags = opflags;

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_discard_alarm);
	g_task_set_task_data (task, g_steal_pointer (&async_context), (GDestroyNotify) async_context_free);

	g_task_run_in_thread (task, cal_client_discard_alarm_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_discard_alarm_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_discard_alarm().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_discard_alarm_finish (ECalClient *client,
                                   GAsyncResult *result,
                                   GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_discard_alarm), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_cal_client_discard_alarm_sync:
 * @client: an #ECalClient
 * @uid: Unique identifier for a calendar component
 * @rid: (nullable): Recurrence identifier
 * @auid: Alarm identifier to discard
 * @opflags: (type ECalOperationFlags): bit-or of #ECalOperationFlags
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Discards alarm @auid from a given component identified by @uid and @rid.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_discard_alarm_sync (ECalClient *client,
                                 const gchar *uid,
                                 const gchar *rid,
                                 const gchar *auid,
				 ECalOperationFlags opflags,
                                 GCancellable *cancellable,
                                 GError **error)
{
	gchar *utf8_uid;
	gchar *utf8_rid;
	gchar *utf8_auid;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);
	g_return_val_if_fail (auid != NULL, FALSE);

	if (rid == NULL)
		rid = "";

	utf8_uid = e_util_utf8_make_valid (uid);
	utf8_rid = e_util_utf8_make_valid (rid);
	utf8_auid = e_util_utf8_make_valid (auid);

	e_dbus_calendar_call_discard_alarm_sync (
		client->priv->dbus_proxy,
		utf8_uid, utf8_rid, utf8_auid, opflags,
		cancellable, &local_error);

	g_free (utf8_uid);
	g_free (utf8_rid);
	g_free (utf8_auid);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

/* Helper for e_cal_client_get_view() */
static void
cal_client_get_view_in_dbus_thread (GTask *task,
                                    gpointer source_object,
                                    gpointer task_data,
                                    GCancellable *cancellable)
{
	ECalClient *client = E_CAL_CLIENT (source_object);
	const gchar *utf8_sexp = task_data;
	gchar *object_path = NULL;
	GError *local_error = NULL;

	e_dbus_calendar_call_get_view_sync (
		client->priv->dbus_proxy, utf8_sexp,
		&object_path, cancellable, &local_error);

	/* Sanity check. */
	g_return_if_fail (
		((object_path != NULL) && (local_error == NULL)) ||
		((object_path == NULL) && (local_error != NULL)));

	if (object_path != NULL) {
		GDBusConnection *connection;
		ECalClientView *client_view;

		connection = g_dbus_proxy_get_connection (
			G_DBUS_PROXY (client->priv->dbus_proxy));

		client_view = g_initable_new (
			E_TYPE_CAL_CLIENT_VIEW,
			cancellable, &local_error,
			"client", client,
			"connection", connection,
			"object-path", object_path,
			NULL);
		g_clear_pointer (&object_path, g_free);

		/* Sanity check. */
		g_return_if_fail (
			((client_view != NULL) && (local_error == NULL)) ||
			((client_view == NULL) && (local_error != NULL)));

		g_task_return_pointer (task, g_steal_pointer (&client_view), g_object_unref);
	} else {
		g_dbus_error_strip_remote_error (local_error);
		g_task_return_error (task, g_steal_pointer (&local_error));
	}
}

/**
 * e_cal_client_get_view:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query.
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Query @client with @sexp, creating an #ECalClientView.
 * The call is finished by e_cal_client_get_view_finish()
 * from the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_view (ECalClient *client,
                       const gchar *sexp,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (sexp != NULL);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_view);
	g_task_set_task_data (task, e_util_utf8_make_valid (sexp), g_free);

	cal_client_run_in_dbus_thread (task, cal_client_get_view_in_dbus_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_view_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_view: (out) (transfer full): an #ECalClientView
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_view().
 * If successful, then the @out_view is set to newly allocated #ECalClientView,
 * which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_view_finish (ECalClient *client,
                              GAsyncResult *result,
                              ECalClientView **out_view,
                              GError **error)
{
	ECalClientView *view;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_view), FALSE);

	view = g_task_propagate_pointer (G_TASK (result), error);
	if (!view)
		return FALSE;

	if (out_view != NULL)
		*out_view = g_steal_pointer (&view);

	g_clear_object (&view);
	return TRUE;
}

/**
 * e_cal_client_get_view_sync:
 * @client: an #ECalClient
 * @sexp: an S-expression representing the query.
 * @out_view: (out): an #ECalClientView
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Query @client with @sexp, creating an #ECalClientView.
 * If successful, then the @out_view is set to newly allocated #ECalClientView,
 * which should be freed with g_object_unref().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_view_sync (ECalClient *client,
                            const gchar *sexp,
                            ECalClientView **out_view,
                            GCancellable *cancellable,
                            GError **error)
{
	EAsyncClosure *closure;
	GAsyncResult *result;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (sexp != NULL, FALSE);
	g_return_val_if_fail (out_view != NULL, FALSE);

	closure = e_async_closure_new ();

	e_cal_client_get_view (
		client, sexp, cancellable,
		e_async_closure_callback, closure);

	result = e_async_closure_wait (closure);

	success = e_cal_client_get_view_finish (
		client, result, out_view, error);

	e_async_closure_free (closure);

	return success;
}

/* Helper for e_cal_client_get_timezone() */
static void
cal_client_get_timezone_thread (GTask *task,
                                gpointer source_object,
                                gpointer task_data,
                                GCancellable *cancellable)
{
	GError *local_error = NULL;
	const gchar *tzid = task_data;
	ICalTimezone *zone = NULL;

	if (!e_cal_client_get_timezone_sync (
		E_CAL_CLIENT (source_object),
		tzid,
		&zone,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_pointer (task, zone, NULL);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_get_timezone:
 * @client: an #ECalClient
 * @tzid: ID of the timezone to retrieve
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Retrieves a timezone object from the calendar backend.
 * The call is finished by e_cal_client_get_timezone_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_get_timezone (ECalClient *client,
                           const gchar *tzid,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (tzid != NULL);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_get_timezone);
	g_task_set_task_data (task, g_strdup (tzid), g_free);

	g_task_run_in_thread (task, cal_client_get_timezone_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_get_timezone_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @out_zone: (out) (transfer none): Return value for the timezone
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_get_timezone() and
 * sets @out_zone to a retrieved timezone object from the calendar backend.
 * This object is owned by the @client, thus do not free it.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_timezone_finish (ECalClient *client,
                                  GAsyncResult *result,
                                  ICalTimezone **out_zone,
                                  GError **error)
{
	ICalTimezone *zone;

	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_get_timezone), FALSE);

	zone = g_task_propagate_pointer (G_TASK (result), error);
	if (!zone)
		return FALSE;

	if (out_zone != NULL)
		*out_zone = g_steal_pointer (&zone);

	return TRUE;
}

/**
 * e_cal_client_get_timezone_sync:
 * @client: an #ECalClient
 * @tzid: ID of the timezone to retrieve
 * @out_zone: (out) (transfer none): Return value for the timezone
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Retrieves a timezone object from the calendar backend.
 * This object is owned by the @client, thus do not free it.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_get_timezone_sync (ECalClient *client,
                                const gchar *tzid,
                                ICalTimezone **out_zone,
                                GCancellable *cancellable,
                                GError **error)
{
	ICalComponent *icalcomp;
	ICalTimezone *zone;
	gchar *utf8_tzid;
	gchar *string = NULL;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (tzid != NULL, FALSE);
	g_return_val_if_fail (out_zone != NULL, FALSE);

	zone = e_timezone_cache_get_timezone (
		E_TIMEZONE_CACHE (client), tzid);
	if (zone != NULL) {
		*out_zone = zone;
		return TRUE;
	}

	utf8_tzid = e_util_utf8_make_valid (tzid);

	e_dbus_calendar_call_get_timezone_sync (
		client->priv->dbus_proxy, utf8_tzid,
		&string, cancellable, &local_error);

	g_free (utf8_tzid);

	/* Sanity check. */
	g_return_val_if_fail (
		((string != NULL) && (local_error == NULL)) ||
		((string == NULL) && (local_error != NULL)), FALSE);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	icalcomp = i_cal_parser_parse_string (string);

	g_free (string);

	if (icalcomp == NULL) {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return FALSE;
	}

	zone = i_cal_timezone_new ();
	if (!i_cal_timezone_set_component (zone, icalcomp)) {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		g_object_unref (icalcomp);
		g_object_unref (zone);
		return FALSE;
	}

	/* Add the timezone to the cache directly,
	 * otherwise we'd have to free this struct
	 * and fetch the cached copy. */
	g_mutex_lock (&client->priv->zone_cache_lock);
	if (g_hash_table_lookup (client->priv->zone_cache, tzid)) {
		/* It can be that another thread already filled the zone into the cache,
		   thus deal with it properly, because that other zone can be used by that
		   other thread. */
		g_object_unref (zone);
		zone = g_hash_table_lookup (client->priv->zone_cache, tzid);
	} else {
		g_hash_table_insert (
			client->priv->zone_cache, g_strdup (tzid), zone);
	}
	g_mutex_unlock (&client->priv->zone_cache_lock);

	g_object_unref (icalcomp);

	*out_zone = zone;

	return TRUE;
}

/* Helper for e_cal_client_add_timezone() */
static void
cal_client_add_timezone_thread (GTask *task,
                                gpointer source_object,
                                gpointer task_data,
                                GCancellable *cancellable)
{
	GError *local_error = NULL;
	ICalTimezone *zone = task_data;

	if (!e_cal_client_add_timezone_sync (
		E_CAL_CLIENT (source_object),
		zone,
		cancellable, &local_error)) {

		if (!local_error)
			local_error = g_error_new_literal (
				E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Unknown error"));
	}

	if (!local_error)
		g_task_return_boolean (task, TRUE);
	else
		g_task_return_error (task, g_steal_pointer (&local_error));
}

/**
 * e_cal_client_add_timezone:
 * @client: an #ECalClient
 * @zone: The timezone to add
 * @cancellable: a #GCancellable; can be %NULL
 * @callback: callback to call when a result is ready
 * @user_data: user data for the @callback
 *
 * Add a VTIMEZONE object to the given calendar client.
 * The call is finished by e_cal_client_add_timezone_finish() from
 * the @callback.
 *
 * Since: 3.2
 **/
void
e_cal_client_add_timezone (ECalClient *client,
                           ICalTimezone *zone,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	GTask *task;

	g_return_if_fail (E_IS_CAL_CLIENT (client));
	g_return_if_fail (zone != NULL);

	task = g_task_new (client, cancellable, callback, user_data);
	g_task_set_source_tag (task, e_cal_client_add_timezone);
	g_task_set_task_data (task, e_cal_util_copy_timezone (zone), g_object_unref);

	if (zone == i_cal_timezone_get_utc_timezone ())
		g_task_return_boolean (task, TRUE);
	else
		g_task_run_in_thread (task, cal_client_add_timezone_thread);

	g_object_unref (task);
}

/**
 * e_cal_client_add_timezone_finish:
 * @client: an #ECalClient
 * @result: a #GAsyncResult
 * @error: a #GError to set an error, if any
 *
 * Finishes previous call of e_cal_client_add_timezone().
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_add_timezone_finish (ECalClient *client,
                                  GAsyncResult *result,
                                  GError **error)
{
	g_return_val_if_fail (g_task_is_valid (result, client), FALSE);
	g_return_val_if_fail (g_async_result_is_tagged (result, e_cal_client_add_timezone), FALSE);

	return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * e_cal_client_add_timezone_sync:
 * @client: an #ECalClient
 * @zone: The timezone to add
 * @cancellable: a #GCancellable; can be %NULL
 * @error: a #GError to set an error, if any
 *
 * Add a VTIMEZONE object to the given calendar client.
 *
 * Returns: %TRUE if successful, %FALSE otherwise.
 *
 * Since: 3.2
 **/
gboolean
e_cal_client_add_timezone_sync (ECalClient *client,
                                ICalTimezone *zone,
                                GCancellable *cancellable,
                                GError **error)
{
	ICalComponent *icalcomp;
	gchar *zone_str;
	gchar *utf8_zone_str;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_CLIENT (client), FALSE);
	g_return_val_if_fail (zone != NULL, FALSE);

	if (zone == i_cal_timezone_get_utc_timezone ())
		return TRUE;

	icalcomp = i_cal_timezone_get_component (zone);
	if (icalcomp == NULL) {
		g_propagate_error (
			error, e_client_error_create (
			E_CLIENT_ERROR_INVALID_ARG, NULL));
		return FALSE;
	}

	zone_str = i_cal_component_as_ical_string (icalcomp);
	utf8_zone_str = e_util_utf8_make_valid (zone_str);

	e_dbus_calendar_call_add_timezone_sync (
		client->priv->dbus_proxy, utf8_zone_str,
		cancellable, &local_error);

	g_free (zone_str);
	g_free (utf8_zone_str);
	g_object_unref (icalcomp);

	if (local_error != NULL) {
		g_dbus_error_strip_remote_error (local_error);
		g_propagate_error (error, local_error);
		return FALSE;
	}

	return TRUE;
}

