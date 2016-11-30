/*
 * Evolution calendar - caldav backend
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Christian Kellner <gicmo@gnome.org>
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>

/* LibXML2 includes */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

/* LibSoup includes */
#include <libsoup/soup.h>

#include <libedataserver/libedataserver.h>

#include "e-cal-backend-caldav.h"

#define d(x)

#define E_CAL_BACKEND_CALDAV_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND_CALDAV, ECalBackendCalDAVPrivate))

#define CALDAV_CTAG_KEY "CALDAV_CTAG"
#define CALDAV_MAX_MULTIGET_AMOUNT 100 /* what's the maximum count of items to fetch within a multiget request */
#define LOCAL_PREFIX "file://"

/* in seconds */
#define DEFAULT_REFRESH_TIME 60

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

typedef enum {

	SLAVE_SHOULD_SLEEP,
	SLAVE_SHOULD_WORK,
	SLAVE_SHOULD_WORK_NO_CTAG_CHECK,
	SLAVE_SHOULD_DIE

} SlaveCommand;

/* Private part of the ECalBackendHttp structure */
struct _ECalBackendCalDAVPrivate {

	/* The local disk cache */
	ECalBackendStore *store;

	/* should we sync for offline mode? */
	gboolean do_offline;

	/* TRUE after caldav_open */
	gboolean loaded;
	/* TRUE when server reachable */
	gboolean opened;

	/* lock to indicate a busy state */
	GMutex busy_lock;

	/* cond to synch threads */
	GCond cond;

	/* cond to know the slave gone */
	GCond slave_gone_cond;

	/* BG synch thread */
	const GThread *synch_slave; /* just for a reference, whether thread exists */
	SlaveCommand slave_cmd;
	gboolean slave_busy; /* whether is slave working */

	/* The main soup session  */
	SoupSession *session;

	/* clandar uri */
	gchar *uri;

	/* Authentication info */
	ENamedParameters *credentials;
	gboolean auth_required;

	/* support for 'getctag' extension */
	gboolean ctag_supported;
	gchar *ctag_to_store;

	/* TRUE when 'calendar-schedule' supported on the server */
	gboolean calendar_schedule;
	/* with 'calendar-schedule' supported, here's an outbox url
	 * for queries of free/busy information */
	gchar *schedule_outbox_url;

	/* "Temporary hack" to indicate it's talking to a google calendar.
	 * The proper solution should be to subclass whole backend and change only
	 * necessary parts in it, but this will give us more freedom, as also direct
	 * caldav calendars can profit from this. */
	gboolean is_google;

	/* The iCloud.com requires timezone IDs as locations */
	gboolean is_icloud;

	/* set to true if thread for ESource::changed is invoked */
	gboolean updating_source;

	guint refresh_id;

	/* If we fail to obtain an OAuth2 access token,
	 * soup_authenticate_bearer() stashes an error
	 * here to be claimed in caldav_credentials_required_sync().
	 * This lets us propagate a more useful error
	 * message than a generic SOUP_STATUS_UNAUTHORIZED description. */
	GError *bearer_auth_error;
	GMutex bearer_auth_error_lock;
	ESoupAuthBearer *using_bearer_auth;
};

/* Forward Declarations */
static void	e_caldav_backend_initable_init
				(GInitableIface *interface);

G_DEFINE_TYPE_WITH_CODE (
	ECalBackendCalDAV,
	e_cal_backend_caldav,
	E_TYPE_CAL_BACKEND_SYNC,
	G_IMPLEMENT_INTERFACE (
		G_TYPE_INITABLE,
		e_caldav_backend_initable_init))

/* ************************************************************************* */
/* Debugging */

#define DEBUG_MESSAGE "message"
#define DEBUG_MESSAGE_HEADER "message:header"
#define DEBUG_MESSAGE_BODY "message:body"
#define DEBUG_SERVER_ITEMS "items"
#define DEBUG_ATTACHMENTS "attachments"

static gboolean open_calendar_wrapper (ECalBackendCalDAV *cbdav,
				       GCancellable *cancellable,
				       GError **error,
				       gboolean first_attempt,
				       gboolean *know_unreachable,
				       gchar **out_certificate_pem,
				       GTlsCertificateFlags *out_certificate_errors);

static void convert_to_inline_attachment (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp);
static void convert_to_url_attachment (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp);
static void remove_cached_attachment (ECalBackendCalDAV *cbdav, const gchar *uid);

static gboolean caldav_debug_all = FALSE;
static GHashTable *caldav_debug_table = NULL;

static void
add_debug_key (const gchar *start,
               const gchar *end)
{
	gchar *debug_key;
	gchar *debug_value;

	if (start == end) {
		return;
	}

	debug_key = debug_value = g_strndup (start, end - start);

	debug_key = g_strchug (debug_key);
	debug_key = g_strchomp (debug_key);

	if (strlen (debug_key) == 0) {
		g_free (debug_value);
		return;
	}

	g_hash_table_insert (
		caldav_debug_table,
		debug_key,
		debug_value);

	d (g_debug ("Adding %s to enabled debugging keys", debug_key));
}

static gpointer
caldav_debug_init_once (gpointer data)
{
	const gchar *dbg;

	dbg = g_getenv ("CALDAV_DEBUG");

	if (dbg) {
		const gchar *ptr;

		d (g_debug ("Got debug env variable: [%s]", dbg));

		caldav_debug_table = g_hash_table_new (
			g_str_hash,
			g_str_equal);

		ptr = dbg;

		while (*ptr != '\0') {
			if (*ptr == ',' || *ptr == ':') {

				add_debug_key (dbg, ptr);

				if (*ptr == ',') {
					dbg = ptr + 1;
				}
			}

			ptr++;
		}

		if (ptr - dbg > 0) {
			add_debug_key (dbg, ptr);
		}

		if (g_hash_table_lookup (caldav_debug_table, "all")) {
			caldav_debug_all = TRUE;
			g_hash_table_destroy (caldav_debug_table);
			caldav_debug_table = NULL;
		}
	}

	return NULL;
}

static void
caldav_debug_init (void)
{
	static GOnce debug_once = G_ONCE_INIT;

	g_once (
		&debug_once,
		caldav_debug_init_once,
		NULL);
}

static gboolean
caldav_debug_show (const gchar *component)
{
	if (G_UNLIKELY (caldav_debug_all)) {
		return TRUE;
	} else if (G_UNLIKELY (caldav_debug_table != NULL) &&
		   g_hash_table_lookup (caldav_debug_table, component)) {
		return TRUE;
	}

	return FALSE;
}

#define DEBUG_MAX_BODY_SIZE (100 * 1024 * 1024)

static void
caldav_debug_setup (SoupSession *session)
{
	SoupLogger *logger;
	SoupLoggerLogLevel level;

	if (caldav_debug_show (DEBUG_MESSAGE_BODY))
		level = SOUP_LOGGER_LOG_BODY;
	else if (caldav_debug_show (DEBUG_MESSAGE_HEADER))
		level = SOUP_LOGGER_LOG_HEADERS;
	else
		level = SOUP_LOGGER_LOG_MINIMAL;

	logger = soup_logger_new (level, DEBUG_MAX_BODY_SIZE);
	soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
	g_object_unref (logger);
}

/* TODO Do not replicate this in every backend */
static icaltimezone *
resolve_tzid (const gchar *tzid,
              gpointer user_data)
{
	ETimezoneCache *timezone_cache;

	timezone_cache = E_TIMEZONE_CACHE (user_data);

	return e_timezone_cache_get_timezone (timezone_cache, tzid);
}

static gboolean
put_component_to_store (ECalBackendCalDAV *cbdav,
                        ECalComponent *comp)
{
	time_t time_start, time_end;

	e_cal_util_get_component_occur_times (
		comp, &time_start, &time_end,
		resolve_tzid, cbdav,  icaltimezone_get_utc_timezone (),
		e_cal_backend_get_kind (E_CAL_BACKEND (cbdav)));

	return e_cal_backend_store_put_component_with_time_range (
		cbdav->priv->store, comp, time_start, time_end);
}

static ECalBackendSyncClass *parent_class = NULL;

static void caldav_source_changed_cb (ESource *source, ECalBackendCalDAV *cbdav);

static gboolean remove_comp_from_cache (ECalBackendCalDAV *cbdav, const gchar *uid, const gchar *rid);
static gboolean put_comp_to_cache (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp, const gchar *href, const gchar *etag);
static void put_server_comp_to_cache (ECalBackendCalDAV *cbdav, icalcomponent *icomp, const gchar *href, const gchar *etag, GTree *c_uid2complist);

/* ************************************************************************* */
/* Misc. utility functions */

static void
caldav_ensure_bearer_auth_usage (ECalBackendCalDAV *cbdav,
				 ESoupAuthBearer *bearer)
{
	SoupSessionFeature *feature;
	SoupURI *soup_uri;
	ESourceWebdav *extension;
	ESource *source;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav));

	source = e_backend_get_source (E_BACKEND (cbdav));

	/* Preload the SoupAuthManager with a valid "Bearer" token
	 * when using OAuth 2.0. This avoids an extra unauthorized
	 * HTTP round-trip, which apparently Google doesn't like. */

	feature = soup_session_get_feature (cbdav->priv->session, SOUP_TYPE_AUTH_MANAGER);

	if (!soup_session_feature_has_feature (feature, E_TYPE_SOUP_AUTH_BEARER)) {
		/* Add the "Bearer" auth type to support OAuth 2.0. */
		soup_session_feature_add_feature (feature, E_TYPE_SOUP_AUTH_BEARER);
	}

	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	soup_uri = e_source_webdav_dup_soup_uri (extension);

	soup_auth_manager_use_auth (
		SOUP_AUTH_MANAGER (feature),
		soup_uri, SOUP_AUTH (bearer));

	soup_uri_free (soup_uri);
}

static gboolean
caldav_setup_bearer_auth (ECalBackendCalDAV *cbdav,
			  ESoupAuthBearer *bearer,
			  GCancellable *cancellable,
			  GError **error)
{
	ESource *source;
	gchar *access_token = NULL;
	gint expires_in_seconds = -1;
	gboolean success = FALSE;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);
	g_return_val_if_fail (E_IS_SOUP_AUTH_BEARER (bearer), FALSE);

	source = e_backend_get_source (E_BACKEND (cbdav));

	success = e_util_get_source_oauth2_access_token_sync (source, cbdav->priv->credentials,
		&access_token, &expires_in_seconds, cancellable, error);

	if (success) {
		e_soup_auth_bearer_set_access_token (bearer, access_token, expires_in_seconds);
		caldav_ensure_bearer_auth_usage (cbdav, bearer);
	}

	g_free (access_token);

	return success;
}

static gboolean
caldav_maybe_prepare_bearer_auth (ECalBackendCalDAV *cbdav,
				  GCancellable *cancellable,
				  GError **error)
{
	ESource *source;
	gchar *auth_method = NULL;
	gboolean success;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);

	source = e_backend_get_source (E_BACKEND (cbdav));

	if (e_source_has_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION)) {
		ESourceAuthentication *extension;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
		auth_method = e_source_authentication_dup_method (extension);
	} else {
		return TRUE;
	}

	if (g_strcmp0 (auth_method, "OAuth2") != 0 && g_strcmp0 (auth_method, "Google") != 0) {
		g_free (auth_method);
		return TRUE;
	}

	g_free (auth_method);

	if (cbdav->priv->using_bearer_auth) {
		success = caldav_setup_bearer_auth (cbdav, cbdav->priv->using_bearer_auth, cancellable, error);
	} else {
		ESourceWebdav *extension;
		SoupAuth *soup_auth;
		SoupURI *soup_uri;

		extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
		soup_uri = e_source_webdav_dup_soup_uri (extension);

		soup_auth = g_object_new (
			E_TYPE_SOUP_AUTH_BEARER,
			SOUP_AUTH_HOST, soup_uri->host, NULL);

		success = caldav_setup_bearer_auth (cbdav, E_SOUP_AUTH_BEARER (soup_auth), cancellable, error);
		if (success)
			cbdav->priv->using_bearer_auth = g_object_ref (soup_auth);

		g_object_unref (soup_auth);
		soup_uri_free (soup_uri);
	}

	return success;
}

static void
update_slave_cmd (ECalBackendCalDAVPrivate *priv,
                  SlaveCommand slave_cmd)
{
	g_return_if_fail (priv != NULL);

	if (priv->slave_cmd == SLAVE_SHOULD_DIE)
		return;

	priv->slave_cmd = slave_cmd;
}

#define X_E_CALDAV "X-EVOLUTION-CALDAV-"
#define X_E_CALDAV_ATTACHMENT_NAME X_E_CALDAV "ATTACHMENT-NAME"

static void
icomp_x_prop_set (icalcomponent *comp,
                  const gchar *key,
                  const gchar *value)
{
	icalproperty *xprop;

	/* Find the old one first */
	xprop = icalcomponent_get_first_property (comp, ICAL_X_PROPERTY);

	while (xprop) {
		const gchar *str = icalproperty_get_x_name (xprop);

		if (!strcmp (str, key)) {
			if (value) {
				icalproperty_set_value_from_string (xprop, value, "NO");
			} else {
				icalcomponent_remove_property (comp, xprop);
				icalproperty_free (xprop);
			}
			break;
		}

		xprop = icalcomponent_get_next_property (comp, ICAL_X_PROPERTY);
	}

	if (!xprop && value) {
		xprop = icalproperty_new_x (value);
		icalproperty_set_x_name (xprop, key);
		icalcomponent_add_property (comp, xprop);
	}
}

static gchar *
icomp_x_prop_get (icalcomponent *comp,
                  const gchar *key)
{
	icalproperty *xprop;

	/* Find the old one first */
	xprop = icalcomponent_get_first_property (comp, ICAL_X_PROPERTY);

	while (xprop) {
		const gchar *str = icalproperty_get_x_name (xprop);

		if (!strcmp (str, key)) {
			break;
		}

		xprop = icalcomponent_get_next_property (comp, ICAL_X_PROPERTY);
	}

	if (xprop) {
		return icalproperty_get_value_as_string_r (xprop);
	}

	return NULL;
}

/* passing NULL as 'href' removes the property */
static void
ecalcomp_set_href (ECalComponent *comp,
                   const gchar *href)
{
	icalcomponent *icomp;

	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_if_fail (icomp != NULL);

	icomp_x_prop_set (icomp, X_E_CALDAV "HREF", href);
}

static gchar *
ecalcomp_get_href (ECalComponent *comp)
{
	icalcomponent *icomp;
	gchar          *str;

	str = NULL;
	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_val_if_fail (icomp != NULL, NULL);

	str = icomp_x_prop_get (icomp, X_E_CALDAV "HREF");

	return str;
}

/* passing NULL as 'etag' removes the property */
static void
ecalcomp_set_etag (ECalComponent *comp,
                   const gchar *etag)
{
	icalcomponent *icomp;

	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_if_fail (icomp != NULL);

	icomp_x_prop_set (icomp, X_E_CALDAV "ETAG", etag);
}

static gchar *
ecalcomp_get_etag (ECalComponent *comp)
{
	icalcomponent *icomp;
	gchar          *str;

	str = NULL;
	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_val_if_fail (icomp != NULL, NULL);

	str = icomp_x_prop_get (icomp, X_E_CALDAV "ETAG");

	/* libical 0.48 escapes quotes, thus unescape them */
	if (str && strchr (str, '\\')) {
		gint ii, jj;

		for (ii = 0, jj = 0; str[ii]; ii++) {
			if (str[ii] == '\\') {
				ii++;
				if (!str[ii])
					break;
			}

			str[jj] = str[ii];
			jj++;
		}

		str[jj] = 0;
	}

	return str;
}

/*typedef enum {
 *
	/ * object is in synch,
	 * now isnt that ironic? :) * /
	ECALCOMP_IN_SYNCH = 0,
 *
	/ * local changes * /
	ECALCOMP_LOCALLY_CREATED,
	ECALCOMP_LOCALLY_DELETED,
	ECALCOMP_LOCALLY_MODIFIED
 *
} ECalCompSyncState;
 *
/ * oos = out of synch * /
static void
ecalcomp_set_synch_state (ECalComponent *comp,
 *                        ECalCompSyncState state)
{
	icalcomponent *icomp;
	gchar          *state_string;
 *
	icomp = e_cal_component_get_icalcomponent (comp);
 *
	state_string = g_strdup_printf ("%d", state);
 *
	icomp_x_prop_set (icomp, X_E_CALDAV "ETAG", state_string);
 *
	g_free (state_string);
}*/

static gchar *
ecalcomp_gen_href (ECalComponent *comp)
{
	gchar *href, *uid, *tmp;
	icalcomponent *icomp;

	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_val_if_fail (icomp != NULL, NULL);

	uid = g_strdup (icalcomponent_get_uid (icomp));
	if (!uid || !*uid) {
		g_free (uid);
		uid = e_cal_component_gen_uid ();

		tmp = uid ? strchr (uid, '@') : NULL;
		if (tmp)
			*tmp = '\0';

		tmp = NULL;
	} else
		tmp = isodate_from_time_t (time (NULL));

	/* quite long, but ensures uniqueness quite well, without using UUIDs */
	href = g_strconcat (uid ? uid : "no-uid", tmp ? "-" : "", tmp ? tmp : "", ".ics", NULL);

	g_free (tmp);
	g_free (uid);

	icomp_x_prop_set (icomp, X_E_CALDAV "HREF", href);

	return g_strdelimit (href, " /'\"`&();|<>$%{}!\\:*?#@", '_');
}

/* ensure etag is quoted (to workaround potential server bugs) */
static gchar *
quote_etag (const gchar *etag)
{
	gchar *ret;

	if (etag && (strlen (etag) < 2 || etag[strlen (etag) - 1] != '\"')) {
		ret = g_strdup_printf ("\"%s\"", etag);
	} else {
		ret = g_strdup (etag);
	}

	return ret;
}

/* ************************************************************************* */

static gboolean
status_code_to_result (SoupMessage *message,
                       ECalBackendCalDAV *cbdav,
                       gboolean is_opening,
                       GError **perror)
{
	ECalBackendCalDAVPrivate *priv;
	gchar *uri;

	g_return_val_if_fail (cbdav != NULL, FALSE);
	g_return_val_if_fail (message != NULL, FALSE);

	priv = cbdav->priv;

	if (SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		return TRUE;
	}

	if (perror && *perror)
		return FALSE;

	switch (message->status_code) {
	case SOUP_STATUS_CANT_RESOLVE:
	case SOUP_STATUS_CANT_RESOLVE_PROXY:
	case SOUP_STATUS_CANT_CONNECT:
	case SOUP_STATUS_CANT_CONNECT_PROXY:
		g_propagate_error (
			perror,
			e_data_cal_create_error_fmt (
				OtherError,
				_("Server is unreachable (%s)"),
					message->reason_phrase && *message->reason_phrase ? message->reason_phrase :
					(soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : _("Unknown error"))));
		if (priv) {
			priv->opened = FALSE;
			e_cal_backend_set_writable (
				E_CAL_BACKEND (cbdav), FALSE);
		}
		break;
	case SOUP_STATUS_NOT_FOUND:
		if (is_opening)
			g_propagate_error (perror, EDC_ERROR (NoSuchCal));
		else
			g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
		break;

	case SOUP_STATUS_FORBIDDEN:
		if (cbdav->priv->using_bearer_auth && message->response_body &&
		    message->response_body->data && message->response_body->length) {
			gchar *body = g_strndup (message->response_body->data, message->response_body->length);

			/* Do not localize this string, it is returned by the server. */
			if (body && (e_util_strstrcase (body, "Daily Limit") ||
			    e_util_strstrcase (body, "https://console.developers.google.com/"))) {
				/* Special-case this condition and provide this error up to the UI. */
				g_propagate_error (perror,
					e_data_cal_create_error_fmt (OtherError, _("Failed to login to the server: %s"), body));
			} else {
				g_propagate_error (perror, EDC_ERROR (AuthenticationRequired));
			}

			g_free (body);
		} else {
			g_propagate_error (perror, EDC_ERROR (AuthenticationRequired));
		}
		break;

	case SOUP_STATUS_UNAUTHORIZED:
		if (priv && priv->auth_required)
			g_propagate_error (perror, EDC_ERROR (AuthenticationFailed));
		else
			g_propagate_error (perror, EDC_ERROR (AuthenticationRequired));
		break;

	case SOUP_STATUS_SSL_FAILED:
		g_propagate_error (
			perror,
			e_data_cal_create_error_fmt ( OtherError,
			_("Failed to connect to a server using SSL/TLS: %s"),
			message->reason_phrase && *message->reason_phrase ? message->reason_phrase :
			(soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : _("Unknown error"))));
		if (is_opening && perror && *perror) {
			(*perror)->domain = SOUP_HTTP_ERROR;
			(*perror)->code = SOUP_STATUS_SSL_FAILED;
		}
		break;

	default:
		d (g_debug ("CalDAV:%s: Unhandled status code %d\n", G_STRFUNC, status_code));
		uri = soup_uri_to_string (soup_message_get_uri (message), FALSE);
		g_propagate_error (
			perror,
			e_data_cal_create_error_fmt (
				OtherError,
				_("Unexpected HTTP status code %d returned (%s) for URI: %s"),
					message->status_code,
					message->reason_phrase && *message->reason_phrase ? message->reason_phrase :
					(soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : _("Unknown error")),
					uri ? uri : "[null]"));
		g_free (uri);
		break;
	}

	return FALSE;
}

/* !TS, call with lock held */
static gboolean
check_state (ECalBackendCalDAV *cbdav,
             gboolean *online,
             GError **perror)
{
	*online = FALSE;

	if (!cbdav->priv->loaded) {
		g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("CalDAV backend is not loaded yet")));
		return FALSE;
	}

	if (!e_backend_get_online (E_BACKEND (cbdav))) {

		if (!cbdav->priv->do_offline) {
			g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
			return FALSE;
		}

	} else {
		*online = TRUE;
	}

	return TRUE;
}

/* ************************************************************************* */
/* XML Parsing code */

static xmlXPathObjectPtr
xpath_eval (xmlXPathContextPtr ctx,
            const gchar *format,
            ...)
{
	xmlXPathObjectPtr  result;
	va_list            args;
	gchar              *expr;

	if (ctx == NULL) {
		return NULL;
	}

	va_start (args, format);
	expr = g_strdup_vprintf (format, args);
	va_end (args);

	result = xmlXPathEvalExpression ((xmlChar *) expr, ctx);
	g_free (expr);

	if (result == NULL) {
		return NULL;
	}

	if (result->type == XPATH_NODESET &&
	    xmlXPathNodeSetIsEmpty (result->nodesetval)) {
		xmlXPathFreeObject (result);
		return NULL;
	}

	return result;
}

#if 0
static gboolean
parse_status_node (xmlNodePtr node,
                   guint *status_code)
{
	xmlChar  *content;
	gboolean  res;

	content = xmlNodeGetContent (node);

	res = soup_headers_parse_status_line (
		(gchar *) content,
		NULL,
		status_code,
		NULL);
	xmlFree (content);

	return res;
}
#endif

static gchar *
xp_object_get_string (xmlXPathObjectPtr result)
{
	gchar *ret = NULL;

	if (result == NULL)
		return ret;

	if (result->type == XPATH_STRING) {
		ret = g_strdup ((gchar *) result->stringval);
	}

	xmlXPathFreeObject (result);
	return ret;
}

/* like get_string but will quote the etag if necessary */
static gchar *
xp_object_get_etag (xmlXPathObjectPtr result)
{
	gchar *ret = NULL;
	gchar *str;

	if (result == NULL)
		return ret;

	if (result->type == XPATH_STRING) {
		str = (gchar *) result->stringval;

		ret = quote_etag (str);
	}

	xmlXPathFreeObject (result);
	return ret;
}

static guint
xp_object_get_status (xmlXPathObjectPtr result)
{
	gboolean res;
	guint    ret = 0;

	if (result == NULL)
		return ret;

	if (result->type == XPATH_STRING) {
		res = soup_headers_parse_status_line (
			(gchar *) result->stringval,
			NULL,
			&ret,
			NULL);

		if (!res) {
			ret = 0;
		}
	}

	xmlXPathFreeObject (result);
	return ret;
}

#if 0
static gint
xp_object_get_number (xmlXPathObjectPtr result)
{
	gint ret = -1;

	if (result == NULL)
		return ret;

	if (result->type == XPATH_STRING) {
		ret = result->boolval;
	}

	xmlXPathFreeObject (result);
	return ret;
}
#endif

/*** *** *** *** *** *** */
#define XPATH_HREF "string(/D:multistatus/D:response[%d]/D:href)"
#define XPATH_STATUS "string(/D:multistatus/D:response[%d]/D:propstat/D:status)"
#define XPATH_GETETAG_STATUS "string(/D:multistatus/D:response[%d]/D:propstat/D:prop/D:getetag/../../D:status)"
#define XPATH_GETETAG "string(/D:multistatus/D:response[%d]/D:propstat/D:prop/D:getetag)"
#define XPATH_CALENDAR_DATA "string(/D:multistatus/D:response[%d]/D:propstat/D:prop/C:calendar-data)"
#define XPATH_GETCTAG_STATUS "string(/D:multistatus/D:response/D:propstat/D:prop/CS:getctag/../../D:status)"
#define XPATH_GETCTAG "string(/D:multistatus/D:response/D:propstat/D:prop/CS:getctag)"
#define XPATH_OWNER_STATUS "string(/D:multistatus/D:response/D:propstat/D:prop/D:owner/D:href/../../../D:status)"
#define XPATH_OWNER "string(/D:multistatus/D:response/D:propstat/D:prop/D:owner/D:href)"
#define XPATH_SCHEDULE_OUTBOX_URL_STATUS "string(/D:multistatus/D:response/D:propstat/D:prop/C:schedule-outbox-URL/D:href/../../../D:status)"
#define XPATH_SCHEDULE_OUTBOX_URL "string(/D:multistatus/D:response/D:propstat/D:prop/C:schedule-outbox-URL/D:href)"

typedef struct _CalDAVObject CalDAVObject;

struct _CalDAVObject {

	gchar *href;
	gchar *etag;

	guint status;

	gchar *cdata;
};

static void
caldav_object_free (CalDAVObject *object,
                    gboolean free_object_itself)
{
	g_free (object->href);
	g_free (object->etag);
	g_free (object->cdata);

	if (free_object_itself) {
		g_free (object);
	}
}

static gboolean
parse_report_response (SoupMessage *soup_message,
                       CalDAVObject **objs,
                       gint *len)
{
	xmlXPathContextPtr xpctx;
	xmlXPathObjectPtr  result;
	xmlDocPtr          doc;
	gint                i, n;
	gboolean           res;

	g_return_val_if_fail (soup_message != NULL, FALSE);
	g_return_val_if_fail (objs != NULL || len != NULL, FALSE);

	res = TRUE;
	doc = xmlReadMemory (
		soup_message->response_body->data,
		soup_message->response_body->length,
		"response.xml",
		NULL,
		0);

	if (doc == NULL) {
		return FALSE;
	}

	xpctx = xmlXPathNewContext (doc);

	xmlXPathRegisterNs (
		xpctx, (xmlChar *) "D",
		(xmlChar *) "DAV:");

	xmlXPathRegisterNs (
		xpctx, (xmlChar *) "C",
		(xmlChar *) "urn:ietf:params:xml:ns:caldav");

	result = xpath_eval (xpctx, "/D:multistatus/D:response");

	if (result == NULL || result->type != XPATH_NODESET) {
		*len = 0;
		res = FALSE;
		goto out;
	}

	n = xmlXPathNodeSetGetLength (result->nodesetval);
	*len = n;

	*objs = g_new0 (CalDAVObject, n);

	for (i = 0; i < n; i++) {
		CalDAVObject *object;
		xmlXPathObjectPtr xpres;

		object = *objs + i;
		/* see if we got a status child in the response element */

		xpres = xpath_eval (xpctx, XPATH_HREF, i + 1);
		/* use full path from a href, to let calendar-multiget work properly */
		object->href = xp_object_get_string (xpres);

		xpres = xpath_eval (xpctx,XPATH_STATUS , i + 1);
		object->status = xp_object_get_status (xpres);

		if (object->status && object->status != 200) {
			continue;
		}

		xpres = xpath_eval (xpctx, XPATH_GETETAG_STATUS, i + 1);
		object->status = xp_object_get_status (xpres);

		if (object->status != 200) {
			continue;
		}

		xpres = xpath_eval (xpctx, XPATH_GETETAG, i + 1);
		object->etag = xp_object_get_etag (xpres);

		xpres = xpath_eval (xpctx, XPATH_CALENDAR_DATA, i + 1);
		object->cdata = xp_object_get_string (xpres);
	}

out:
	if (result != NULL)
		xmlXPathFreeObject (result);
	xmlXPathFreeContext (xpctx);
	xmlFreeDoc (doc);
	return res;
}

/* returns whether was able to read the xpath_value from the server's response; *value contains the result */
static gboolean
parse_propfind_response (SoupMessage *message,
                         const gchar *xpath_status,
                         const gchar *xpath_value,
                         gchar **value)
{
	xmlXPathContextPtr xpctx;
	xmlDocPtr          doc;
	gboolean           res = FALSE;

	g_return_val_if_fail (message != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	doc = xmlReadMemory (
		message->response_body->data,
		message->response_body->length,
		"response.xml",
		NULL,
		0);

	if (doc == NULL) {
		return FALSE;
	}

	xpctx = xmlXPathNewContext (doc);
	xmlXPathRegisterNs (xpctx, (xmlChar *) "D", (xmlChar *) "DAV:");
	xmlXPathRegisterNs (xpctx, (xmlChar *) "C", (xmlChar *) "urn:ietf:params:xml:ns:caldav");
	xmlXPathRegisterNs (xpctx, (xmlChar *) "CS", (xmlChar *) "http://calendarserver.org/ns/");

	if (xpath_status == NULL || xp_object_get_status (xpath_eval (xpctx, xpath_status)) == 200) {
		gchar *txt = xp_object_get_string (xpath_eval (xpctx, xpath_value));

		if (txt && *txt) {
			gint len = strlen (txt);

			if (*txt == '\"' && len > 2 && txt[len - 1] == '\"') {
				/* dequote */
				*value = g_strndup (txt + 1, len - 2);
			} else {
				*value = txt;
				txt = NULL;
			}

			res = (*value) != NULL;
		}

		g_free (txt);
	}

	xmlXPathFreeContext (xpctx);
	xmlFreeDoc (doc);

	return res;
}

/* ************************************************************************* */
/* Authentication helpers for libsoup */

static void
soup_authenticate_bearer (SoupSession *session,
                          SoupMessage *message,
                          SoupAuth *auth,
                          ECalBackendCalDAV *cbdav)
{
	GError *local_error = NULL;

	caldav_setup_bearer_auth (cbdav, E_SOUP_AUTH_BEARER (auth), NULL, &local_error);

	/* Stash the error to be picked up by caldav_credentials_required_sync().
	 * There's no way to explicitly propagate a GError directly
	 * through libsoup, so we have to work around it. */
	if (local_error != NULL) {
		g_mutex_lock (&cbdav->priv->bearer_auth_error_lock);

		/* Warn about an unclaimed error before we clear it.
		 * This is just to verify the errors we set here are
		 * actually making it back to the user. */
		g_warn_if_fail (cbdav->priv->bearer_auth_error == NULL);
		g_clear_error (&cbdav->priv->bearer_auth_error);

		g_propagate_error (
			&cbdav->priv->bearer_auth_error, local_error);

		g_mutex_unlock (&cbdav->priv->bearer_auth_error_lock);
	}
}

static void
soup_authenticate (SoupSession *session,
                   SoupMessage *msg,
                   SoupAuth *auth,
                   gboolean retrying,
                   gpointer data)
{
	ECalBackendCalDAV *cbdav;
	ESourceAuthentication *auth_extension;
	ESource *source;
	const gchar *extension_name;

	cbdav = E_CAL_BACKEND_CALDAV (data);

	source = e_backend_get_source (E_BACKEND (data));
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	if (E_IS_SOUP_AUTH_BEARER (auth)) {
		g_object_ref (auth);
		g_warn_if_fail ((gpointer) cbdav->priv->using_bearer_auth == (gpointer) auth);
		g_clear_object (&cbdav->priv->using_bearer_auth);
		cbdav->priv->using_bearer_auth = E_SOUP_AUTH_BEARER (auth);
	}

	if (retrying)
		return;

	if (cbdav->priv->using_bearer_auth) {
		soup_authenticate_bearer (session, msg, auth, cbdav);

	/* do not send same password twice, but keep it for later use */
	} else {
		gchar *auth_user;
		const gchar *username;

		auth_user = e_source_authentication_dup_user (auth_extension);

		username = cbdav->priv->credentials ? e_named_parameters_get (cbdav->priv->credentials, E_SOURCE_CREDENTIAL_USERNAME) : NULL;
		if (!username || !*username)
			username = auth_user;

		if (!username || !*username || !cbdav->priv->credentials ||
		    !e_named_parameters_exists (cbdav->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD))
			soup_message_set_status (msg, SOUP_STATUS_FORBIDDEN);
		else
			soup_auth_authenticate (auth, username, e_named_parameters_get (cbdav->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD));

		g_free (auth_user);
	}
}

/* ************************************************************************* */
/* direct CalDAV server access functions */

static void
redirect_handler (SoupMessage *msg,
                  gpointer user_data)
{
	if (SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
		SoupSession *soup_session = user_data;
		SoupURI *new_uri;
		const gchar *new_loc;

		new_loc = soup_message_headers_get_list (msg->response_headers, "Location");
		if (!new_loc)
			return;

		new_uri = soup_uri_new_with_base (soup_message_get_uri (msg), new_loc);
		if (!new_uri) {
			soup_message_set_status_full (
				msg,
				SOUP_STATUS_MALFORMED,
				_("Invalid Redirect URL"));
			return;
		}

		if (new_uri->host && g_str_has_suffix (new_uri->host, "yahoo.com")) {
			/* yahoo! returns port 7070, which is unreachable;
			 * it also requires https being used (below call resets port as well) */
			soup_uri_set_scheme (new_uri, SOUP_URI_SCHEME_HTTPS);
		}

		soup_message_set_uri (msg, new_uri);
		soup_session_requeue_message (soup_session, msg);

		soup_uri_free (new_uri);
	}
}

static void
send_and_handle_redirection (ECalBackendCalDAV *cbdav,
                             SoupMessage *msg,
                             gchar **new_location,
                             GCancellable *cancellable,
                             GError **error)
{
	gchar *old_uri = NULL;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav));
	g_return_if_fail (msg != NULL);

	if (new_location)
		old_uri = soup_uri_to_string (soup_message_get_uri (msg), FALSE);

	e_soup_ssl_trust_connect (msg, e_backend_get_source (E_BACKEND (cbdav)));

	if (cbdav->priv->using_bearer_auth &&
	    e_soup_auth_bearer_is_expired (cbdav->priv->using_bearer_auth)) {
		GError *local_error = NULL;

		if (!caldav_setup_bearer_auth (cbdav, cbdav->priv->using_bearer_auth, cancellable, &local_error)) {
			if (local_error) {
				soup_message_set_status_full (msg, SOUP_STATUS_BAD_REQUEST, local_error->message);
				g_propagate_error (error, local_error);
			} else {
				soup_message_set_status (msg, SOUP_STATUS_BAD_REQUEST);
			}
			return;
		}
	}

	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	soup_message_add_header_handler (msg, "got_body", "Location", G_CALLBACK (redirect_handler), cbdav->priv->session);
	soup_message_headers_append (msg->request_headers, "Connection", "close");
	soup_session_send_message (cbdav->priv->session, msg);

	if (new_location) {
		gchar *new_loc = soup_uri_to_string (soup_message_get_uri (msg), FALSE);

		if (new_loc && old_uri && !g_str_equal (new_loc, old_uri))
			*new_location = new_loc;
		else
			g_free (new_loc);
	}

	g_free (old_uri);

	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code))
		e_backend_ensure_source_status_connected (E_BACKEND (cbdav));
}

static gchar *
caldav_generate_uri (ECalBackendCalDAV *cbdav,
                     const gchar *target)
{
	gchar *uri;
	const gchar *slash;

	slash = strrchr (target, '/');
	if (slash)
		target = slash + 1;

	/* uri *have* trailing slash already */
	uri = g_strconcat (cbdav->priv->uri, target, NULL);

	return uri;
}

static gboolean
caldav_server_open_calendar (ECalBackendCalDAV *cbdav,
                             gboolean *server_unreachable,
			     gchar **out_certificate_pem,
			     GTlsCertificateFlags *out_certificate_errors,
                             GCancellable *cancellable,
                             GError **perror)
{
	SoupMessage *message;
	const gchar *header;
	gboolean calendar_access;
	gboolean put_allowed;
	gboolean delete_allowed;
	ESource *source;

	g_return_val_if_fail (cbdav != NULL, FALSE);
	g_return_val_if_fail (server_unreachable != NULL, FALSE);

	message = soup_message_new (SOUP_METHOD_OPTIONS, cbdav->priv->uri);
	if (message == NULL) {
		g_propagate_error (perror, EDC_ERROR (NoSuchCal));
		return FALSE;
	}

	soup_message_headers_append (
		message->request_headers,
		"User-Agent", "Evolution/" VERSION);

	source = e_backend_get_source (E_BACKEND (cbdav));
	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

	send_and_handle_redirection (cbdav, message, NULL, cancellable, perror);

	if (!SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

		switch (message->status_code) {
		case SOUP_STATUS_CANT_RESOLVE:
		case SOUP_STATUS_CANT_RESOLVE_PROXY:
		case SOUP_STATUS_CANT_CONNECT:
		case SOUP_STATUS_CANT_CONNECT_PROXY:
			*server_unreachable = TRUE;
			break;
		case SOUP_STATUS_SSL_FAILED:
			if (out_certificate_pem && out_certificate_errors) {
				GTlsCertificate *certificate = NULL;

				g_object_get (G_OBJECT (message),
					"tls-certificate", &certificate,
					"tls-errors", out_certificate_errors,
					NULL);

				if (certificate) {
					g_object_get (certificate, "certificate-pem", out_certificate_pem, NULL);
					g_object_unref (certificate);
				}
			}
			break;
		}

		status_code_to_result (message, cbdav, TRUE, perror);

		g_object_unref (message);
		return FALSE;
	}

	/* parse the dav header, we are intreseted in the
	 * calendar-access bit only at the moment */
	header = soup_message_headers_get_list (message->response_headers, "DAV");
	if (header) {
		calendar_access = soup_header_contains (header, "calendar-access");
		cbdav->priv->calendar_schedule = soup_header_contains (header, "calendar-schedule");
	} else {
		calendar_access = FALSE;
		cbdav->priv->calendar_schedule = FALSE;
	}

	/* parse the Allow header and look for PUT, DELETE at the
	 * moment (maybe we should check more here, for REPORT eg) */
	header = soup_message_headers_get_list (message->response_headers, "Allow");
	if (header) {
		/* The POST added for FastMail servers, which doesn't advertise PUT on collections. */
		put_allowed = soup_header_contains (header, "PUT") || soup_header_contains (header, "POST");
		delete_allowed = soup_header_contains (header, "DELETE");
	} else
		put_allowed = delete_allowed = FALSE;

	g_object_unref (message);

	if (calendar_access) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);
		e_cal_backend_set_writable (
			E_CAL_BACKEND (cbdav),
			put_allowed && delete_allowed);
		return TRUE;
	}

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
	g_propagate_error (perror, EDC_ERROR (PermissionDenied));
	return FALSE;
}

static gpointer
caldav_unref_thread (gpointer cbdav)
{
	g_object_unref (cbdav);

	return NULL;
}

static void
caldav_unref_in_thread (ECalBackendCalDAV *cbdav)
{
	GThread *thread;

	g_return_if_fail (cbdav != NULL);

	thread = g_thread_new (NULL, caldav_unref_thread, cbdav);
	g_thread_unref (thread);
}

static gboolean
caldav_credentials_required_sync (ECalBackendCalDAV *cbdav,
				  gboolean ref_cbdav,
				  gboolean first_attempt,
				  GCancellable *cancellable,
				  GError **error)
{
	gboolean success = TRUE;

	if (ref_cbdav)
		g_object_ref (cbdav);

	/* This function is called when we receive a 4xx response code for
	 * authentication failures.  If we're using Bearer authentication,
	 * there should be a GError available.  Return the GError to avoid
	 * inappropriately prompting for a password. */
	g_mutex_lock (&cbdav->priv->bearer_auth_error_lock);
	if (cbdav->priv->bearer_auth_error != NULL) {
		g_propagate_error (error, cbdav->priv->bearer_auth_error);
		cbdav->priv->bearer_auth_error = NULL;
		success = FALSE;
	}
	g_mutex_unlock (&cbdav->priv->bearer_auth_error_lock);

	if (success) {
		success = e_backend_credentials_required_sync (E_BACKEND (cbdav),
			(first_attempt || !cbdav->priv->credentials ||
			 !e_named_parameters_exists (cbdav->priv->credentials, E_SOURCE_CREDENTIAL_PASSWORD))
			? E_SOURCE_CREDENTIALS_REASON_REQUIRED :
			E_SOURCE_CREDENTIALS_REASON_REJECTED,
			NULL, 0, NULL, cancellable, error);
	}

	if (ref_cbdav)
		caldav_unref_in_thread (cbdav);

	return success;
}

static gconstpointer
compat_libxml_output_buffer_get_content (xmlOutputBufferPtr buf,
                                         gsize *out_len)
{
#ifdef LIBXML2_NEW_BUFFER
	*out_len = xmlOutputBufferGetSize (buf);
	return xmlOutputBufferGetContent (buf);
#else
	*out_len = buf->buffer->use;
	return buf->buffer->content;
#endif
}

/* Returns whether calendar changed on the server. This works only when server
 * supports 'getctag' extension. */
static gboolean
check_calendar_changed_on_server (ECalBackendCalDAV *cbdav,
				  gboolean save_ctag,
				  GCancellable *cancellable)
{
	xmlOutputBufferPtr	  buf;
	SoupMessage              *message;
	xmlDocPtr		  doc;
	xmlNodePtr		  root, node;
	xmlNsPtr		  ns, nsdav;
	gconstpointer		  buf_content;
	gsize			  buf_size;
	gboolean		  result = TRUE;

	g_return_val_if_fail (cbdav != NULL, TRUE);

	/* no support for 'getctag', thus update cache */
	if (!cbdav->priv->ctag_supported)
		return TRUE;

	/* Prepare the soup message */
	message = soup_message_new ("PROPFIND", cbdav->priv->uri);
	if (message == NULL)
		return FALSE;

	doc = xmlNewDoc ((xmlChar *) "1.0");
	root = xmlNewDocNode (doc, NULL, (xmlChar *) "propfind", NULL);
	xmlDocSetRootElement (doc, root);
	nsdav = xmlNewNs (root, (xmlChar *) "DAV:", NULL);
	ns = xmlNewNs (root, (xmlChar *) "http://calendarserver.org/ns/", (xmlChar *) "CS");

	node = xmlNewTextChild (root, nsdav, (xmlChar *) "prop", NULL);
	node = xmlNewTextChild (node, nsdav, (xmlChar *) "getctag", NULL);
	xmlSetNs (node, ns);

	buf = xmlAllocOutputBuffer (NULL);
	xmlNodeDumpOutput (buf, doc, root, 0, 1, NULL);
	xmlOutputBufferFlush (buf);

	soup_message_headers_append (
		message->request_headers,
		"User-Agent", "Evolution/" VERSION);
	soup_message_headers_append (
		message->request_headers,
		"Depth", "0");

	buf_content = compat_libxml_output_buffer_get_content (buf, &buf_size);
	soup_message_set_request (
		message,
		"application/xml",
		SOUP_MEMORY_COPY,
		buf_content, buf_size);

	/* Send the request now */
	send_and_handle_redirection (cbdav, message, NULL, cancellable, NULL);

	/* Clean up the memory */
	xmlOutputBufferClose (buf);
	xmlFreeDoc (doc);

	/* Check the result */
	if (message->status_code == SOUP_STATUS_UNAUTHORIZED || message->status_code == SOUP_STATUS_FORBIDDEN) {
		caldav_credentials_required_sync (cbdav, TRUE, FALSE, NULL, NULL);
	} else if (message->status_code != SOUP_STATUS_MULTI_STATUS) {
		/* does not support it, but report calendar changed to update cache */
		cbdav->priv->ctag_supported = FALSE;
	} else {
		gchar *ctag = NULL;

		if (parse_propfind_response (message, XPATH_GETCTAG_STATUS, XPATH_GETCTAG, &ctag)) {
			const gchar *my_ctag;

			my_ctag = e_cal_backend_store_get_key_value (
				cbdav->priv->store, CALDAV_CTAG_KEY);

			if (ctag && my_ctag && g_str_equal (ctag, my_ctag)) {
				/* ctag is same, no change in the calendar */
				result = FALSE;
			} else if (save_ctag) {
				/* do not store ctag now, do it rather after complete sync */
				g_free (cbdav->priv->ctag_to_store);
				cbdav->priv->ctag_to_store = ctag;
				ctag = NULL;
			}

			g_free (ctag);
		} else {
			cbdav->priv->ctag_supported = FALSE;
		}
	}

	g_object_unref (message);

	return result;
}

/* only_hrefs is a list of requested objects to fetch; it has precedence from
 * start_time/end_time, which are used only when both positive.
 * Times are supposed to be in UTC, if set.
 */
static gboolean
caldav_server_list_objects (ECalBackendCalDAV *cbdav,
                            CalDAVObject **objs,
                            gint *len,
                            GSList *only_hrefs,
                            time_t start_time,
                            time_t end_time,
			    GCancellable *cancellable)
{
	xmlOutputBufferPtr   buf;
	SoupMessage         *message;
	xmlNodePtr           node;
	xmlNodePtr           sn;
	xmlNodePtr           root;
	xmlDocPtr            doc;
	xmlNsPtr             nsdav;
	xmlNsPtr             nscd;
	gconstpointer        buf_content;
	gsize                buf_size;
	gboolean             result;

	/* Allocate the soup message */
	message = soup_message_new ("REPORT", cbdav->priv->uri);
	if (message == NULL)
		return FALSE;

	/* Maybe we should just do a g_strdup_printf here? */
	/* Prepare request body */
	doc = xmlNewDoc ((xmlChar *) "1.0");
	if (!only_hrefs)
		root = xmlNewDocNode (doc, NULL, (xmlChar *) "calendar-query", NULL);
	else
		root = xmlNewDocNode (doc, NULL, (xmlChar *) "calendar-multiget", NULL);
	nscd = xmlNewNs (root, (xmlChar *) "urn:ietf:params:xml:ns:caldav", (xmlChar *) "C");
	xmlSetNs (root, nscd);
	xmlDocSetRootElement (doc, root);

	/* Add webdav tags */
	nsdav = xmlNewNs (root, (xmlChar *) "DAV:", (xmlChar *) "D");
	node = xmlNewTextChild (root, nsdav, (xmlChar *) "prop", NULL);
	xmlNewTextChild (node, nsdav, (xmlChar *) "getetag", NULL);
	if (only_hrefs) {
		GSList *l;

		xmlNewTextChild (node, nscd, (xmlChar *) "calendar-data", NULL);
		for (l = only_hrefs; l; l = l->next) {
			if (l->data) {
				xmlNewTextChild (root, nsdav, (xmlChar *) "href", (xmlChar *) l->data);
			}
		}
	} else {
		node = xmlNewTextChild (root, nscd, (xmlChar *) "filter", NULL);
		node = xmlNewTextChild (node, nscd, (xmlChar *) "comp-filter", NULL);
		xmlSetProp (node, (xmlChar *) "name", (xmlChar *) "VCALENDAR");

		sn = xmlNewTextChild (node, nscd, (xmlChar *) "comp-filter", NULL);
		switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbdav))) {
			default:
			case ICAL_VEVENT_COMPONENT:
				xmlSetProp (sn, (xmlChar *) "name", (xmlChar *) "VEVENT");
				break;
			case ICAL_VJOURNAL_COMPONENT:
				xmlSetProp (sn, (xmlChar *) "name", (xmlChar *) "VJOURNAL");
				break;
			case ICAL_VTODO_COMPONENT:
				xmlSetProp (sn, (xmlChar *) "name", (xmlChar *) "VTODO");
				break;
		}

		if (start_time > 0 || end_time > 0) {
			gchar *tmp;

			sn = xmlNewTextChild (sn, nscd, (xmlChar *) "time-range", NULL);

			if (start_time > 0) {
				tmp = isodate_from_time_t (start_time);
				xmlSetProp (sn, (xmlChar *) "start", (xmlChar *) tmp);
				g_free (tmp);
			}

			if (end_time > 0) {
				tmp = isodate_from_time_t (end_time);
				xmlSetProp (sn, (xmlChar *) "end", (xmlChar *) tmp);
				g_free (tmp);
			}
		}
	}

	buf = xmlAllocOutputBuffer (NULL);
	xmlNodeDumpOutput (buf, doc, root, 0, 1, NULL);
	xmlOutputBufferFlush (buf);

	/* Prepare the soup message */
	soup_message_headers_append (
		message->request_headers,
		"User-Agent", "Evolution/" VERSION);
	soup_message_headers_append (
		message->request_headers,
		"Depth", "1");

	buf_content = compat_libxml_output_buffer_get_content (buf, &buf_size);
	soup_message_set_request (
		message,
		"application/xml",
		SOUP_MEMORY_COPY,
		buf_content, buf_size);

	/* Send the request now */
	send_and_handle_redirection (cbdav, message, NULL, cancellable, NULL);

	/* Clean up the memory */
	xmlOutputBufferClose (buf);
	xmlFreeDoc (doc);

	/* Check the result */
	if (message->status_code != SOUP_STATUS_MULTI_STATUS) {
		switch (message->status_code) {
		case SOUP_STATUS_CANT_RESOLVE:
		case SOUP_STATUS_CANT_RESOLVE_PROXY:
		case SOUP_STATUS_CANT_CONNECT:
		case SOUP_STATUS_CANT_CONNECT_PROXY:
			cbdav->priv->opened = FALSE;
			update_slave_cmd (cbdav->priv, SLAVE_SHOULD_SLEEP);
			e_cal_backend_set_writable (
				E_CAL_BACKEND (cbdav), FALSE);
			break;
		case SOUP_STATUS_UNAUTHORIZED:
		case SOUP_STATUS_FORBIDDEN:
			caldav_credentials_required_sync (cbdav, TRUE, FALSE, NULL, NULL);
			break;
		default:
			g_warning ("Server did not response with SOUP_STATUS_MULTI_STATUS, but with code %d (%s)", message->status_code, soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : "Unknown code");
			break;
		}

		g_object_unref (message);
		return FALSE;
	}

	/* Parse the response body */
	result = parse_report_response (message, objs, len);

	g_object_unref (message);
	return result;
}

static gboolean
caldav_server_download_attachment (ECalBackendCalDAV *cbdav,
                                   const gchar *attachment_uri,
                                   gchar **content,
                                   gsize *len,
                                   GError **error)
{
	SoupMessage *message;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);
	g_return_val_if_fail (attachment_uri != NULL, FALSE);
	g_return_val_if_fail (content != NULL, FALSE);
	g_return_val_if_fail (len != NULL, FALSE);

	message = soup_message_new (SOUP_METHOD_GET, attachment_uri);
	if (message == NULL) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return FALSE;
	}

	soup_message_headers_append (message->request_headers, "User-Agent", "Evolution/" VERSION);
	send_and_handle_redirection (cbdav, message, NULL, NULL, NULL);

	if (!SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		status_code_to_result (message, cbdav, FALSE, error);

		if (message->status_code == SOUP_STATUS_UNAUTHORIZED || message->status_code == SOUP_STATUS_FORBIDDEN)
			caldav_credentials_required_sync (cbdav, FALSE, FALSE, NULL, NULL);

		g_object_unref (message);
		return FALSE;
	}

	*len = message->response_body->length;
	*content = g_memdup (message->response_body->data, *len);

	g_object_unref (message);

	return TRUE;
}

static gboolean
caldav_server_get_object (ECalBackendCalDAV *cbdav,
                          CalDAVObject *object,
                          GCancellable *cancellable,
                          GError **perror)
{
	SoupMessage              *message;
	const gchar               *hdr;
	gchar                     *uri;

	g_return_val_if_fail (object != NULL && object->href != NULL, FALSE);

	uri = caldav_generate_uri (cbdav, object->href);
	message = soup_message_new (SOUP_METHOD_GET, uri);
	if (message == NULL) {
		g_free (uri);
		g_propagate_error (perror, EDC_ERROR (NoSuchCal));
		return FALSE;
	}

	soup_message_headers_append (
		message->request_headers,
		"User-Agent", "Evolution/" VERSION);

	send_and_handle_redirection (cbdav, message, NULL, cancellable, perror);

	if (!SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		status_code_to_result (message, cbdav, FALSE, perror);

		if (message->status_code == SOUP_STATUS_UNAUTHORIZED || message->status_code == SOUP_STATUS_FORBIDDEN)
			caldav_credentials_required_sync (cbdav, FALSE, FALSE, NULL, NULL);
		else if (message->status_code != SOUP_STATUS_NOT_FOUND)
			g_warning ("Could not fetch object '%s' from server, status:%d (%s)", uri, message->status_code, soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : "Unknown code");
		g_object_unref (message);
		g_free (uri);
		return FALSE;
	}

	hdr = soup_message_headers_get_list (message->response_headers, "Content-Type");

	if (hdr == NULL || g_ascii_strncasecmp (hdr, "text/calendar", 13)) {
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		g_object_unref (message);
		g_warning ("Object to fetch '%s' not of type text/calendar", uri);
		g_free (uri);
		return FALSE;
	}

	hdr = soup_message_headers_get_list (message->response_headers, "ETag");

	if (hdr != NULL) {
		g_free (object->etag);
		object->etag = quote_etag (hdr);
	} else if (!object->etag) {
		g_warning ("UUHH no ETag, now that's bad! (at '%s')", uri);
	}
	g_free (uri);

	g_free (object->cdata);
	object->cdata = g_strdup (message->response_body->data);

	g_object_unref (message);

	return TRUE;
}

static void
caldav_post_freebusy (ECalBackendCalDAV *cbdav,
                      const gchar *url,
                      gchar **post_fb,
                      GCancellable *cancellable,
                      GError **error)
{
	SoupMessage *message;

	message = soup_message_new (SOUP_METHOD_POST, url);
	if (message == NULL) {
		g_propagate_error (error, EDC_ERROR (NoSuchCal));
		return;
	}

	soup_message_headers_append (message->request_headers, "User-Agent", "Evolution/" VERSION);
	soup_message_set_request (
		message,
		"text/calendar; charset=utf-8",
		SOUP_MEMORY_COPY,
		*post_fb, strlen (*post_fb));

	send_and_handle_redirection (cbdav, message, NULL, cancellable, error);

	if (!SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		status_code_to_result (message, cbdav, FALSE, error);
		if (message->status_code == SOUP_STATUS_UNAUTHORIZED || message->status_code == SOUP_STATUS_FORBIDDEN)
			caldav_credentials_required_sync (cbdav, FALSE, FALSE, NULL, NULL);
		else
			g_warning ("Could not post free/busy request to '%s', status:%d (%s)", url, message->status_code, soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : "Unknown code");

		g_object_unref (message);

		return;
	}

	g_free (*post_fb);
	*post_fb = g_strdup (message->response_body->data);

	g_object_unref (message);
}

static gchar *
caldav_gen_file_from_uid (ECalBackendCalDAV *cbdav,
			  const gchar *uid)
{
	gchar *filename, *res;

	if (!uid)
		return NULL;

	filename = g_strconcat (uid, ".ics", NULL);
	res = soup_uri_encode (filename, NULL);
	g_free (filename);

	return res;
}

static gchar *
caldav_gen_file_from_uid_cal (ECalBackendCalDAV *cbdav,
                              icalcomponent *icalcomp)
{
	icalcomponent_kind my_kind;
	const gchar *uid = NULL;

	g_return_val_if_fail (cbdav != NULL, NULL);
	g_return_val_if_fail (icalcomp != NULL, NULL);

	my_kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbdav));
	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;

		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icalcomp, my_kind)) {
			uid = icalcomponent_get_uid (subcomp);
			if (uid && *uid)
				break;
		}
	} else if (icalcomponent_isa (icalcomp) == my_kind) {
		uid = icalcomponent_get_uid (icalcomp);
	}

	return caldav_gen_file_from_uid (cbdav, uid);
}

static gboolean
caldav_server_put_object (ECalBackendCalDAV *cbdav,
                          CalDAVObject *object,
                          icalcomponent *icalcomp,
                          GCancellable *cancellable,
                          GError **perror)
{
	SoupMessage              *message;
	const gchar               *hdr;
	gchar                     *uri;

	hdr = NULL;

	g_return_val_if_fail (object != NULL && object->cdata != NULL, FALSE);

	uri = caldav_generate_uri (cbdav, object->href);
	message = soup_message_new (SOUP_METHOD_PUT, uri);
	g_free (uri);
	if (message == NULL) {
		g_propagate_error (perror, EDC_ERROR (NoSuchCal));
		return FALSE;
	}

	soup_message_headers_append (
		message->request_headers,
		"User-Agent", "Evolution/" VERSION);

	/* For new items we use the If-None-Match so we don't
	 * acidently override resources, for item updates we
	 * use the If-Match header to avoid the Lost-update
	 * problem */
	if (object->etag == NULL) {
		soup_message_headers_append (message->request_headers, "If-None-Match", "*");
	} else {
		soup_message_headers_append (
			message->request_headers,
			"If-Match", object->etag);
	}

	soup_message_set_request (
		message,
		"text/calendar; charset=utf-8",
		SOUP_MEMORY_COPY,
		object->cdata,
		strlen (object->cdata));

	uri = NULL;
	send_and_handle_redirection (cbdav, message, &uri, cancellable, perror);

	if (uri) {
		gchar *file = strrchr (uri, '/');

		/* there was a redirect, update href properly */
		if (file) {
			gchar *decoded;

			g_free (object->href);

			decoded = soup_uri_decode (file + 1);
			object->href = soup_uri_encode (decoded ? decoded : (file + 1), NULL);

			g_free (decoded);
		}

		g_free (uri);
	}

	if (status_code_to_result (message, cbdav, FALSE, perror)) {
		GError *local_error = NULL;

		hdr = soup_message_headers_get_list (message->response_headers, "ETag");
		if (hdr != NULL) {
			g_free (object->etag);
			object->etag = quote_etag (hdr);
		}

		/* "201 Created" can contain a Location with a link where the component was saved */
		hdr = soup_message_headers_get_list (message->response_headers, "Location");
		if (hdr) {
			/* reflect possible href change */
			gchar *file = strrchr (hdr, '/');

			if (file) {
				gchar *decoded;

				g_free (object->href);

				decoded = soup_uri_decode (file + 1);
				object->href = soup_uri_encode (decoded ? decoded : (file + 1), NULL);

				g_free (decoded);
			}
		}

		if (!caldav_server_get_object (cbdav, object, cancellable, &local_error)) {
			if (g_error_matches (local_error, E_DATA_CAL_ERROR, ObjectNotFound)) {
				gchar *file;

				/* OK, the event was properly created, but cannot be found on the place
				 * where it was PUT - why didn't server tell us where it saved it? */
				g_clear_error (&local_error);

				/* try whether it's saved as its UID.ics file */
				file = caldav_gen_file_from_uid_cal (cbdav, icalcomp);
				if (file) {
					g_free (object->href);
					object->href = file;

					if (!caldav_server_get_object (cbdav, object, cancellable, &local_error)) {
						if (g_error_matches (local_error, E_DATA_CAL_ERROR, ObjectNotFound)) {
							g_clear_error (&local_error);

							/* not sure what can happen, but do not need to guess for ever,
							 * thus report success and update the calendar to get fresh info */
							update_slave_cmd (cbdav->priv, SLAVE_SHOULD_WORK);
							g_cond_signal (&cbdav->priv->cond);
						}
					}
				}
			}
		}

		if (!local_error) {
			icalcomponent *use_comp = NULL;

			if (object->cdata) {
				/* maybe server also modified component, thus rather store the server's */
				use_comp = icalparser_parse_string (object->cdata);
			}

			if (!use_comp)
				use_comp = icalcomp;

			put_comp_to_cache (cbdav, use_comp, object->href, object->etag);

			if (use_comp != icalcomp)
				icalcomponent_free (use_comp);
		} else {
			g_propagate_error (perror, local_error);
		}
	}

	if (message->status_code == SOUP_STATUS_UNAUTHORIZED || message->status_code == SOUP_STATUS_FORBIDDEN) {
		caldav_credentials_required_sync (cbdav, FALSE, FALSE, NULL, NULL);
	}

	g_object_unref (message);

	return TRUE;
}

static void
caldav_server_delete_object (ECalBackendCalDAV *cbdav,
                             CalDAVObject *object,
                             GCancellable *cancellable,
                             GError **perror)
{
	SoupMessage              *message;
	gchar                     *uri;

	g_return_if_fail (object != NULL && object->href != NULL);

	uri = caldav_generate_uri (cbdav, object->href);
	message = soup_message_new (SOUP_METHOD_DELETE, uri);
	g_free (uri);
	if (message == NULL) {
		g_propagate_error (perror, EDC_ERROR (NoSuchCal));
		return;
	}

	soup_message_headers_append (
		message->request_headers,
		"User-Agent", "Evolution/" VERSION);

	if (object->etag != NULL) {
		soup_message_headers_append (
			message->request_headers,
			"If-Match", object->etag);
	}

	send_and_handle_redirection (cbdav, message, NULL, cancellable, perror);

	status_code_to_result (message, cbdav, FALSE, perror);

	if (message->status_code == SOUP_STATUS_UNAUTHORIZED || message->status_code == SOUP_STATUS_FORBIDDEN)
		caldav_credentials_required_sync (cbdav, FALSE, FALSE, NULL, NULL);

	g_object_unref (message);
}

static gboolean
caldav_receive_schedule_outbox_url (ECalBackendCalDAV *cbdav,
                                    GCancellable *cancellable,
                                    GError **error)
{
	SoupMessage *message;
	xmlOutputBufferPtr buf;
	xmlDocPtr doc;
	xmlNodePtr root, node;
	xmlNsPtr nsdav;
	gconstpointer buf_content;
	gsize buf_size;
	gchar *owner = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);
	g_return_val_if_fail (cbdav->priv->schedule_outbox_url == NULL, TRUE);

	/* Prepare the soup message */
	message = soup_message_new ("PROPFIND", cbdav->priv->uri);
	if (message == NULL)
		return FALSE;

	doc = xmlNewDoc ((xmlChar *) "1.0");
	root = xmlNewDocNode (doc, NULL, (xmlChar *) "propfind", NULL);
	xmlDocSetRootElement (doc, root);
	nsdav = xmlNewNs (root, (xmlChar *) "DAV:", NULL);

	node = xmlNewTextChild (root, nsdav, (xmlChar *) "prop", NULL);
	xmlNewTextChild (node, nsdav, (xmlChar *) "owner", NULL);

	buf = xmlAllocOutputBuffer (NULL);
	xmlNodeDumpOutput (buf, doc, root, 0, 1, NULL);
	xmlOutputBufferFlush (buf);

	soup_message_headers_append (message->request_headers, "User-Agent", "Evolution/" VERSION);
	soup_message_headers_append (message->request_headers, "Depth", "0");

	buf_content = compat_libxml_output_buffer_get_content (buf, &buf_size);
	soup_message_set_request (
		message,
		"application/xml",
		SOUP_MEMORY_COPY,
		buf_content, buf_size);

	/* Send the request now */
	send_and_handle_redirection (cbdav, message, NULL, cancellable, error);

	/* Clean up the memory */
	xmlOutputBufferClose (buf);
	xmlFreeDoc (doc);

	/* Check the result */
	if (message->status_code == SOUP_STATUS_MULTI_STATUS && parse_propfind_response (message, XPATH_OWNER_STATUS, XPATH_OWNER, &owner) && owner && *owner) {
		xmlNsPtr nscd;
		SoupURI *suri;

		g_object_unref (message);

		/* owner is a full path to the user's URL, thus change it in
		 * calendar's uri when asking for schedule-outbox-URL */
		suri = soup_uri_new (cbdav->priv->uri);
		soup_uri_set_path (suri, owner);
		g_free (owner);
		owner = soup_uri_to_string (suri, FALSE);
		soup_uri_free (suri);

		message = soup_message_new ("PROPFIND", owner);
		if (message == NULL) {
			g_free (owner);
			return FALSE;
		}

		doc = xmlNewDoc ((xmlChar *) "1.0");
		root = xmlNewDocNode (doc, NULL, (xmlChar *) "propfind", NULL);
		xmlDocSetRootElement (doc, root);
		nsdav = xmlNewNs (root, (xmlChar *) "DAV:", NULL);
		nscd = xmlNewNs (root, (xmlChar *) "urn:ietf:params:xml:ns:caldav", (xmlChar *) "C");

		node = xmlNewTextChild (root, nsdav, (xmlChar *) "prop", NULL);
		xmlNewTextChild (node, nscd, (xmlChar *) "schedule-outbox-URL", NULL);

		buf = xmlAllocOutputBuffer (NULL);
		xmlNodeDumpOutput (buf, doc, root, 0, 1, NULL);
		xmlOutputBufferFlush (buf);

		soup_message_headers_append (message->request_headers, "User-Agent", "Evolution/" VERSION);
		soup_message_headers_append (message->request_headers, "Depth", "0");

		buf_content = compat_libxml_output_buffer_get_content (buf, &buf_size);
		soup_message_set_request (
			message,
			"application/xml",
			SOUP_MEMORY_COPY,
			buf_content, buf_size);

		/* Send the request now */
		send_and_handle_redirection (cbdav, message, NULL, cancellable, error);

		if (message->status_code == SOUP_STATUS_MULTI_STATUS && parse_propfind_response (message, XPATH_SCHEDULE_OUTBOX_URL_STATUS, XPATH_SCHEDULE_OUTBOX_URL, &cbdav->priv->schedule_outbox_url)) {
			if (!*cbdav->priv->schedule_outbox_url) {
				g_free (cbdav->priv->schedule_outbox_url);
				cbdav->priv->schedule_outbox_url = NULL;
			} else {
				/* make it a full URI */
				suri = soup_uri_new (cbdav->priv->uri);
				soup_uri_set_path (suri, cbdav->priv->schedule_outbox_url);
				g_free (cbdav->priv->schedule_outbox_url);
				cbdav->priv->schedule_outbox_url = soup_uri_to_string (suri, FALSE);
				soup_uri_free (suri);
			}
		}

		/* Clean up the memory */
		xmlOutputBufferClose (buf);
		xmlFreeDoc (doc);
	} else if (message->status_code == SOUP_STATUS_UNAUTHORIZED || message->status_code == SOUP_STATUS_FORBIDDEN) {
		caldav_credentials_required_sync (cbdav, FALSE, FALSE, NULL, NULL);
	}

	if (message)
		g_object_unref (message);

	g_free (owner);

	return cbdav->priv->schedule_outbox_url != NULL;
}

/* ************************************************************************* */
/* Synchronization foo */

static gboolean extract_timezones (ECalBackendCalDAV *cbdav, icalcomponent *icomp);

struct cache_comp_list
{
	GSList *slist;
};

static gboolean
remove_complist_from_cache_and_notify_cb (gpointer key,
                                          gpointer value,
                                          gpointer data)
{
	GSList *l;
	struct cache_comp_list *ccl = value;
	ECalBackendCalDAV *cbdav = data;

	for (l = ccl->slist; l; l = l->next) {
		ECalComponent *old_comp = l->data;
		ECalComponentId *id;

		id = e_cal_component_get_id (old_comp);
		if (!id) {
			continue;
		}

		if (e_cal_backend_store_remove_component (cbdav->priv->store, id->uid, id->rid)) {
			e_cal_backend_notify_component_removed ((ECalBackend *) cbdav, id, old_comp, NULL);
		}

		e_cal_component_free_id (id);
	}
	remove_cached_attachment (cbdav, (const gchar *) key);

	return FALSE;
}

static void
free_comp_list (gpointer cclist)
{
	struct cache_comp_list *ccl = cclist;

	g_return_if_fail (ccl != NULL);

	g_slist_foreach (ccl->slist, (GFunc) g_object_unref, NULL);
	g_slist_free (ccl->slist);
	g_free (ccl);
}

#define etags_match(_tag1, _tag2) ((_tag1 == _tag2) ? TRUE : \
				   g_str_equal (_tag1 != NULL ? _tag1 : "", \
						_tag2 != NULL ? _tag2 : ""))

/* start_time/end_time is an interval for checking changes. If both greater than zero,
 * only the interval is checked and the removed items are not notified, as they can
 * be still there.
*/
static void
caldav_synchronize_cache (ECalBackendCalDAV *cbdav,
			  time_t start_time,
			  time_t end_time,
			  gboolean can_check_ctag,
			  GCancellable *cancellable)
{
	CalDAVObject *sobjs, *object;
	GSList *c_objs, *c_iter; /* list of all items known from our cache */
	GTree *c_uid2complist;  /* cache components list (with detached instances) sorted by (master's) uid */
	GHashTable *c_href2uid; /* connection between href and a (master's) uid */
	GSList *hrefs_to_update, *htu; /* list of href-s to update */
	gint i, len;

	/* intentionally do server-side checking first, and then the bool test,
	   to store actual ctag value first, and then update the content, to not
	   do it again the next time this function is called */
	if (!check_calendar_changed_on_server (cbdav, start_time == (time_t) 0, cancellable) && can_check_ctag) {
		/* no changes on the server, no update required */
		return;
	}

	len = 0;
	sobjs = NULL;

	/* get list of server objects */
	if (!caldav_server_list_objects (cbdav, &sobjs, &len, NULL, start_time, end_time, cancellable))
		return;

	c_objs = e_cal_backend_store_get_components (cbdav->priv->store);

	if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
		printf ("CalDAV - found %d objects on the server, locally stored %d objects\n", len, g_slist_length (c_objs)); fflush (stdout);
	}

	/* do not store changes in cache immediately - makes things significantly quicker */
	e_cal_backend_store_freeze_changes (cbdav->priv->store);

	c_uid2complist = g_tree_new_full ((GCompareDataFunc) g_strcmp0, NULL, g_free, free_comp_list);
	c_href2uid = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	/* fill indexed hash and tree with cached components */
	for (c_iter = c_objs; c_iter; c_iter = g_slist_next (c_iter)) {
		ECalComponent *ccomp = E_CAL_COMPONENT (c_iter->data);
		const gchar *uid = NULL;
		struct cache_comp_list *ccl;
		gchar *href;

		e_cal_component_get_uid (ccomp, &uid);
		if (!uid) {
			g_warning ("broken component with NULL Id");
			continue;
		}

		href = ecalcomp_get_href (ccomp);

		if (href == NULL) {
			g_warning ("href of object NULL :(");
			continue;
		}

		ccl = g_tree_lookup (c_uid2complist, uid);
		if (ccl) {
			ccl->slist = g_slist_prepend (ccl->slist, g_object_ref (ccomp));
		} else {
			ccl = g_new0 (struct cache_comp_list, 1);
			ccl->slist = g_slist_append (NULL, g_object_ref (ccomp));

			/* make a copy, which will be used in the c_href2uid too */
			uid = g_strdup (uid);

			g_tree_insert (c_uid2complist, (gpointer) uid, ccl);
		}

		if (g_hash_table_lookup (c_href2uid, href) == NULL) {
			/* uid is from a component or c_uid2complist key, thus will not be
			 * freed before a removal from c_uid2complist, thus do not duplicate it,
			 * rather save memory */
			g_hash_table_insert (c_href2uid, href, (gpointer) uid);
		} else {
			g_free (href);
		}
	}

	/* clear it now, we do not need it later */
	g_slist_foreach (c_objs, (GFunc) g_object_unref, NULL);
	g_slist_free (c_objs);
	c_objs = NULL;

	hrefs_to_update = NULL;

	/* see if we have to update or add some objects */
	for (i = 0, object = sobjs; i < len && cbdav->priv->slave_cmd == SLAVE_SHOULD_WORK; i++, object++) {
		ECalComponent *ccomp = NULL;
		gchar *etag = NULL;
		const gchar *uid;
		struct cache_comp_list *ccl;

		if (object->status != 200) {
			/* just continue here, so that the object
			 * doesnt get removed from the cobjs list
			 * - therefore it will be removed */
			continue;
		}

		uid = g_hash_table_lookup (c_href2uid, object->href);
		if (uid) {
			ccl = g_tree_lookup (c_uid2complist, uid);
			if (ccl) {
				GSList *sl;
				for (sl = ccl->slist; sl && !etag; sl = sl->next) {
					ccomp = sl->data;
					if (ccomp)
						etag = ecalcomp_get_etag (ccomp);
				}

				if (!etag)
					ccomp = NULL;
			}
		}

		if (!etag || !etags_match (etag, object->etag)) {
			hrefs_to_update = g_slist_prepend (hrefs_to_update, object->href);
		} else if (uid && ccl) {
			/* all components cover by this uid are up-to-date */
			GSList *p;

			for (p = ccl->slist; p; p = p->next) {
				g_object_unref (p->data);
			}

			g_slist_free (ccl->slist);
			ccl->slist = NULL;
		}

		g_free (etag);
	}

	/* free hash table, as it is not used anymore */
	g_hash_table_destroy (c_href2uid);
	c_href2uid = NULL;

	if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
		printf ("CalDAV - recognized %d items to update\n", g_slist_length (hrefs_to_update)); fflush (stdout);
	}

	htu = hrefs_to_update;
	while (htu && cbdav->priv->slave_cmd == SLAVE_SHOULD_WORK) {
		gint count = 0;
		GSList *to_fetch = NULL;

		while (count < CALDAV_MAX_MULTIGET_AMOUNT && htu) {
			to_fetch = g_slist_prepend (to_fetch, htu->data);
			htu = htu->next;
			count++;
		}

		if (to_fetch && cbdav->priv->slave_cmd == SLAVE_SHOULD_WORK) {
			CalDAVObject *up_sobjs = NULL;

			if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
				printf ("CalDAV - going to fetch %d items\n", g_slist_length (to_fetch)); fflush (stdout);
			}

			count = 0;
			if (!caldav_server_list_objects (cbdav, &up_sobjs, &count, to_fetch, 0, 0, cancellable)) {
				fprintf (stderr, "CalDAV - failed to retrieve bunch of items\n"); fflush (stderr);
				break;
			}

			if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
				printf ("CalDAV - fetched bunch of %d items\n", count); fflush (stdout);
			}

			/* we are going to update cache */
			/* they are downloaded, so process them */
			for (i = 0, object = up_sobjs; i < count /*&& cbdav->priv->slave_cmd == SLAVE_SHOULD_WORK */; i++, object++) {
				if (object->status == 200 && object->href && object->etag && object->cdata && *object->cdata) {
					icalcomponent *icomp = icalparser_parse_string (object->cdata);

					if (icomp) {
						put_server_comp_to_cache (cbdav, icomp, object->href, object->etag, c_uid2complist);
						icalcomponent_free (icomp);
					}
				}

				/* these free immediately */
				caldav_object_free (object, FALSE);
			}

			/* cache update done for fetched items */
			g_free (up_sobjs);
		}

		/* do not free 'data' itself, it's part of 'sobjs' */
		g_slist_free (to_fetch);
	}

	/* if not interrupted and not using the time range... */
	if (cbdav->priv->slave_cmd == SLAVE_SHOULD_WORK && (!start_time || !end_time)) {
		/* ...remove old (not on server anymore) items from our cache and notify of a removal */
		g_tree_foreach (c_uid2complist, remove_complist_from_cache_and_notify_cb, cbdav);
	}

	if (cbdav->priv->ctag_to_store) {
		/* store only when wasn't interrupted */
		if (cbdav->priv->slave_cmd == SLAVE_SHOULD_WORK && start_time == 0 && end_time == 0) {
			e_cal_backend_store_put_key_value (cbdav->priv->store, CALDAV_CTAG_KEY, cbdav->priv->ctag_to_store);
		}

		g_free (cbdav->priv->ctag_to_store);
		cbdav->priv->ctag_to_store = NULL;
	}

	/* save cache changes to disk finally */
	e_cal_backend_store_thaw_changes (cbdav->priv->store);

	for (i = 0, object = sobjs; i < len; i++, object++) {
		caldav_object_free (object, FALSE);
	}

	g_tree_destroy (c_uid2complist);
	g_slist_free (hrefs_to_update);
	g_free (sobjs);
}

static void
check_server_tweaks (ECalBackendCalDAV *cbdav)
{
	SoupURI *suri;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav));

	cbdav->priv->is_google = FALSE;
	cbdav->priv->is_icloud = FALSE;

	g_return_if_fail (cbdav->priv->uri);

	suri = soup_uri_new (cbdav->priv->uri);
	g_return_if_fail (suri != NULL);

	cbdav->priv->is_google = suri->host && (
		g_ascii_strcasecmp (suri->host, "www.google.com") == 0 ||
		g_ascii_strcasecmp (suri->host, "apidata.googleusercontent.com") == 0);

	cbdav->priv->is_icloud = suri->host && e_util_utf8_strstrcase (suri->host, ".icloud.com");

	soup_uri_free (suri);
}

static void
time_to_refresh_caldav_calendar_cb (ESource *source,
                                    gpointer user_data)
{
	ECalBackendCalDAV *cbdav = user_data;

	g_return_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav));

	g_cond_signal (&cbdav->priv->cond);
}

/* ************************************************************************* */

static gpointer
caldav_synch_slave_loop (gpointer data)
{
	ECalBackendCalDAV *cbdav;
	time_t now;
	icaltimezone *utc = icaltimezone_get_utc_timezone ();
	gboolean know_unreachable;

	cbdav = E_CAL_BACKEND_CALDAV (data);

	g_mutex_lock (&cbdav->priv->busy_lock);

	know_unreachable = !cbdav->priv->opened;

	while (cbdav->priv->slave_cmd != SLAVE_SHOULD_DIE) {
		gboolean can_check_ctag = TRUE;

		if (cbdav->priv->slave_cmd == SLAVE_SHOULD_SLEEP) {
			/* just sleep until we get woken up again */
			g_cond_wait (&cbdav->priv->cond, &cbdav->priv->busy_lock);

			/* This means to honor SLAVE_SHOULD_SLEEP only if the backend is opened */
			if (cbdav->priv->slave_cmd == SLAVE_SHOULD_DIE ||
			    cbdav->priv->opened) {
				/* check if we should die, work or sleep again */
				continue;
			}
		}

		/* Ok here we go, do some real work
		 * Synch it baby one more time ...
		 */
		cbdav->priv->slave_busy = TRUE;
		if (cbdav->priv->slave_cmd == SLAVE_SHOULD_WORK_NO_CTAG_CHECK) {
			cbdav->priv->slave_cmd = SLAVE_SHOULD_WORK;
			can_check_ctag = FALSE;
		}

		if (!cbdav->priv->opened) {
			gchar *certificate_pem = NULL;
			GTlsCertificateFlags certificate_errors = 0;
			GError *local_error = NULL;

			if (open_calendar_wrapper (cbdav, NULL, &local_error, TRUE, &know_unreachable, &certificate_pem, &certificate_errors)) {
				cbdav->priv->opened = TRUE;
				update_slave_cmd (cbdav->priv, SLAVE_SHOULD_WORK);
				g_cond_signal (&cbdav->priv->cond);

				check_server_tweaks (cbdav);
				know_unreachable = FALSE;
			} else {
				ESourceCredentialsReason reason = E_SOURCE_CREDENTIALS_REASON_REQUIRED;
				GError *local_error2 = NULL;

				if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
					reason = E_SOURCE_CREDENTIALS_REASON_SSL_FAILED;
				}

				if (!e_backend_credentials_required_sync (E_BACKEND (cbdav), reason, certificate_pem, certificate_errors,
					local_error, NULL, &local_error2)) {
					g_warning ("%s: Failed to call credentials required: %s", G_STRFUNC, local_error2 ? local_error2->message : "Unknown error");
				}

				g_clear_error (&local_error2);
			}

			g_clear_error (&local_error);
			g_free (certificate_pem);
		}

		if (cbdav->priv->opened) {
			time (&now);
			/* check for events in the month before/after today first,
			 * to show user actual data as soon as possible */
			caldav_synchronize_cache (cbdav, time_add_week_with_zone (now, -5, utc), time_add_week_with_zone (now, +5, utc), can_check_ctag, NULL);

			if (cbdav->priv->slave_cmd != SLAVE_SHOULD_SLEEP) {
				/* and then check for changes in a whole calendar */
				caldav_synchronize_cache (cbdav, 0, 0, can_check_ctag, NULL);
			}

			if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
				GSList *c_objs;

				c_objs = e_cal_backend_store_get_components (cbdav->priv->store);

				printf ("CalDAV - finished syncing with %d items in a cache\n", g_slist_length (c_objs)); fflush (stdout);

				g_slist_foreach (c_objs, (GFunc) g_object_unref, NULL);
				g_slist_free (c_objs);
			}
		}

		cbdav->priv->slave_busy = FALSE;

		/* puhh that was hard, get some rest :) */
		g_cond_wait (&cbdav->priv->cond, &cbdav->priv->busy_lock);
	}

	cbdav->priv->synch_slave = NULL;

	/* signal we are done */
	g_cond_signal (&cbdav->priv->slave_gone_cond);

	/* we got killed ... */
	g_mutex_unlock (&cbdav->priv->busy_lock);
	return NULL;
}

static gchar *
maybe_append_email_domain (const gchar *username,
                           const gchar *may_append)
{
	if (!username || !*username)
		return NULL;

	if (strchr (username, '@'))
		return g_strdup (username);

	return g_strconcat (username, may_append, NULL);
}

static gchar *
get_usermail (ECalBackend *backend)
{
	ECalBackendCalDAV *cbdav;
	ESource *source;
	ESourceAuthentication *auth_extension;
	ESourceWebdav *webdav_extension;
	const gchar *extension_name;
	gchar *usermail;
	gchar *username;
	gchar *res = NULL;

	g_return_val_if_fail (backend != NULL, NULL);

	source = e_backend_get_source (E_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	/* This will never return an empty string. */
	usermail = e_source_webdav_dup_email_address (webdav_extension);

	if (usermail != NULL)
		return usermail;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);
	username = e_source_authentication_dup_user (auth_extension);

	if (cbdav->priv && cbdav->priv->is_google)
		res = maybe_append_email_domain (username, "@gmail.com");

	g_free (username);

	return res;
}

/* ************************************************************************* */
/* ********** ECalBackendSync virtual function implementation *************  */

static gchar *
caldav_get_backend_property (ECalBackend *backend,
                             const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, FALSE);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		ESourceWebdav *extension;
		ESource *source;
		GString *caps;
		gchar *usermail;
		const gchar *extension_name;

		caps = g_string_new (
			CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
			CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED);

		usermail = get_usermail (E_CAL_BACKEND (backend));
		if (!usermail || !*usermail)
			g_string_append (caps, "," CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS);
		g_free (usermail);

		source = e_backend_get_source (E_BACKEND (backend));

		extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
		extension = e_source_get_extension (source, extension_name);

		if (e_source_webdav_get_calendar_auto_schedule (extension)) {
			g_string_append (
				caps,
				"," CAL_STATIC_CAPABILITY_CREATE_MESSAGES
				"," CAL_STATIC_CAPABILITY_SAVE_SCHEDULES);
		}

		return g_string_free (caps, FALSE);

	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		return get_usermail (E_CAL_BACKEND (backend));

	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		ECalComponent *comp;
		gchar *prop_value;

		comp = e_cal_component_new ();

		switch (e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		case ICAL_VEVENT_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
			break;
		case ICAL_VTODO_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
			break;
		case ICAL_VJOURNAL_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_JOURNAL);
			break;
		default:
			g_object_unref (comp);
			return NULL;
		}

		prop_value = e_cal_component_get_as_string (comp);

		g_object_unref (comp);

		return prop_value;
	}

	/* Chain up to parent's get_backend_property() method. */
	return E_CAL_BACKEND_CLASS (e_cal_backend_caldav_parent_class)->
		get_backend_property (backend, prop_name);
}

static void
caldav_shutdown (ECalBackend *backend)
{
	ECalBackendCalDAVPrivate *priv;
	ESource *source;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (backend);

	/* Chain up to parent's shutdown() method. */
	E_CAL_BACKEND_CLASS (e_cal_backend_caldav_parent_class)->shutdown (backend);

	/* tell the slave to stop before acquiring a lock,
	 * as it can work at the moment, and lock can be locked */
	update_slave_cmd (priv, SLAVE_SHOULD_DIE);

	g_mutex_lock (&priv->busy_lock);

	/* XXX Not sure if this really needs to be part of
	 *     shutdown or if we can just do it in dispose(). */
	source = e_backend_get_source (E_BACKEND (backend));
	if (source) {
		g_signal_handlers_disconnect_by_func (G_OBJECT (source), caldav_source_changed_cb, backend);

		if (priv->refresh_id) {
			e_source_refresh_remove_timeout (source, priv->refresh_id);
			priv->refresh_id = 0;
		}
	}

	/* stop the slave  */
	while (priv->synch_slave) {
		g_cond_signal (&priv->cond);

		/* wait until the slave died */
		g_cond_wait (&priv->slave_gone_cond, &priv->busy_lock);
	}

	g_mutex_unlock (&priv->busy_lock);
}

static gboolean
initialize_backend (ECalBackendCalDAV *cbdav,
                    GError **perror)
{
	ESourceAuthentication    *auth_extension;
	ESourceOffline           *offline_extension;
	ESourceWebdav            *webdav_extension;
	ECalBackend              *backend;
	SoupURI                  *soup_uri;
	ESource                  *source;
	gsize                     len;
	const gchar              *cache_dir;
	const gchar              *extension_name;

	backend = E_CAL_BACKEND (cbdav);
	cache_dir = e_cal_backend_get_cache_dir (backend);
	source = e_backend_get_source (E_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	offline_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	if (!g_signal_handler_find (G_OBJECT (source), G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL, caldav_source_changed_cb, cbdav))
		g_signal_connect (G_OBJECT (source), "changed", G_CALLBACK (caldav_source_changed_cb), cbdav);

	cbdav->priv->loaded = TRUE;
	cbdav->priv->do_offline = e_source_offline_get_stay_synchronized (offline_extension);
	cbdav->priv->auth_required = e_source_authentication_required (auth_extension);

	soup_uri = e_source_webdav_dup_soup_uri (webdav_extension);

	/* properly encode uri */
	if (soup_uri != NULL && soup_uri->path != NULL) {
		gchar *tmp, *path;

		if (strchr (soup_uri->path, '%')) {
			/* If path contains anything already encoded, then
			 * decode it first, thus it'll be managed properly.
			 * For example, the '#' in a path is in URI shown as
			 * %23 and not doing this decode makes it being like
			 * %2523, which is not what is wanted here. */
			tmp = soup_uri_decode (soup_uri->path);
			soup_uri_set_path (soup_uri, tmp);
			g_free (tmp);
		}

		tmp = soup_uri_encode (soup_uri->path, NULL);
		path = soup_uri_normalize (tmp, "/");

		soup_uri_set_path (soup_uri, path);

		g_free (tmp);
		g_free (path);
	}

	g_free (cbdav->priv->uri);
	cbdav->priv->uri = soup_uri_to_string (soup_uri, FALSE);

	soup_uri_free (soup_uri);

	g_return_val_if_fail (cbdav->priv->uri != NULL, FALSE);

	/* remove trailing slashes... */
	if (cbdav->priv->uri != NULL) {
		len = strlen (cbdav->priv->uri);
		while (len--) {
			if (cbdav->priv->uri[len] == '/') {
				cbdav->priv->uri[len] = '\0';
			} else {
				break;
			}
		}
	}

	/* ...and append exactly one slash */
	if (cbdav->priv->uri && *cbdav->priv->uri) {
		gchar *tmp = cbdav->priv->uri;

		cbdav->priv->uri = g_strconcat (cbdav->priv->uri, "/", NULL);

		g_free (tmp);
	}

	if (cbdav->priv->store == NULL) {
		/* remove the old cache while migrating to ECalBackendStore */
		e_cal_backend_cache_remove (cache_dir, "cache.xml");
		cbdav->priv->store = e_cal_backend_store_new (
			cache_dir, E_TIMEZONE_CACHE (cbdav));
		e_cal_backend_store_load (cbdav->priv->store);
	}

	/* Set the local attachment store */
	if (g_mkdir_with_parents (cache_dir, 0700) < 0) {
		g_propagate_error (perror, e_data_cal_create_error_fmt (OtherError, _("Cannot create local cache folder '%s'"), cache_dir));
		return FALSE;
	}

	if (!cbdav->priv->synch_slave) {
		GThread *slave;

		update_slave_cmd (cbdav->priv, SLAVE_SHOULD_SLEEP);
		slave = g_thread_new (NULL, caldav_synch_slave_loop, cbdav);

		cbdav->priv->synch_slave = slave;
		g_thread_unref (slave);
	}

	if (cbdav->priv->refresh_id == 0) {
		cbdav->priv->refresh_id = e_source_refresh_add_timeout (
			source, NULL, time_to_refresh_caldav_calendar_cb, cbdav, NULL);
	}

	return TRUE;
}

static gboolean
caldav_was_ever_connected (ECalBackendCalDAV *cbdav)
{
	gboolean has_components;
	GSList *uids;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);

	if (!cbdav->priv->store)
		return FALSE;

	uids = e_cal_backend_store_get_component_ids (cbdav->priv->store);

	/* Assume the calendar was connected if it has any events stored;
	   obviously, empty calendars will fail this check. */
	has_components = uids != NULL;

	g_slist_free_full (uids, (GDestroyNotify) e_cal_component_free_id);

	return has_components;
}

static gboolean
open_calendar_wrapper (ECalBackendCalDAV *cbdav,
		       GCancellable *cancellable,
		       GError **error,
		       gboolean first_attempt,
		       gboolean *know_unreachable,
		       gchar **out_certificate_pem,
		       GTlsCertificateFlags *out_certificate_errors)
{
	gboolean server_unreachable = FALSE;
	gboolean awaiting_credentials = FALSE;
	gboolean success;
	GError *local_error = NULL;

	g_return_val_if_fail (cbdav != NULL, FALSE);

	if (!cbdav->priv->loaded && !initialize_backend (cbdav, error))
		return FALSE;

	if (!caldav_maybe_prepare_bearer_auth (cbdav, cancellable, error))
		return FALSE;

	success = caldav_server_open_calendar (cbdav, &server_unreachable, out_certificate_pem, out_certificate_errors, cancellable, &local_error);

	if (first_attempt && g_error_matches (local_error, E_DATA_CAL_ERROR, AuthenticationFailed)) {
		g_clear_error (&local_error);
		awaiting_credentials = TRUE;
		success = caldav_credentials_required_sync (cbdav, FALSE, first_attempt, cancellable, &local_error);
	}

	if (success) {
		check_server_tweaks (cbdav);

		if (!awaiting_credentials) {
			update_slave_cmd (cbdav->priv, SLAVE_SHOULD_WORK);
			g_cond_signal (&cbdav->priv->cond);
		}
	} else if (server_unreachable) {
		cbdav->priv->opened = FALSE;
		e_cal_backend_set_writable (E_CAL_BACKEND (cbdav), FALSE);
		if (local_error) {
			if (know_unreachable && !*know_unreachable) {
				gchar *msg = g_strdup_printf (_("Server is unreachable, calendar is opened in read-only mode.\nError message: %s"), local_error->message);
				e_cal_backend_notify_error (E_CAL_BACKEND (cbdav), msg);
				g_free (msg);
				g_clear_error (&local_error);

				*know_unreachable = TRUE;
			} else if (caldav_was_ever_connected (cbdav)) {
				/* This allows to open the calendar in read-only mode, which can be done
				   if it was ever connected to the server. */
				g_clear_error (&local_error);
				success = TRUE;
			}
		}
	}

	if (local_error != NULL)
		g_propagate_error (error, local_error);

	return success;
}

static void
caldav_do_open (ECalBackendSync *backend,
                EDataCal *cal,
                GCancellable *cancellable,
                gboolean only_if_exists,
                GError **perror)
{
	ECalBackendCalDAV *cbdav;
	ESourceWebdav *webdav_extension;
	ESourceAuthentication *auth_extension;
	ESource *source;
	gboolean online;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	g_mutex_lock (&cbdav->priv->busy_lock);

	source = e_backend_get_source (E_BACKEND (cbdav));
	webdav_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_WEBDAV_BACKEND);
	auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	e_source_webdav_unset_temporary_ssl_trust (webdav_extension);

	/* let it decide the 'getctag' extension availability again */
	cbdav->priv->ctag_supported = TRUE;

	if (!cbdav->priv->loaded && !initialize_backend (cbdav, perror)) {
		g_mutex_unlock (&cbdav->priv->busy_lock);
		return;
	}

	online = e_backend_get_online (E_BACKEND (backend));

	if (!cbdav->priv->do_offline && !online) {
		g_mutex_unlock (&cbdav->priv->busy_lock);
		g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
		return;
	}

	cbdav->priv->opened = TRUE;
	cbdav->priv->is_google = FALSE;

	if (online) {
		gchar *certificate_pem = NULL, *auth_method;
		GTlsCertificateFlags certificate_errors = 0;
		GError *local_error = NULL;

		auth_method = e_source_authentication_dup_method (auth_extension);

		if ((g_strcmp0 (auth_method, "Google") == 0 ||
		    !open_calendar_wrapper (cbdav, cancellable, &local_error, TRUE, NULL, &certificate_pem, &certificate_errors)) &&
		    !g_cancellable_is_cancelled (cancellable)) {
			ESourceCredentialsReason reason = E_SOURCE_CREDENTIALS_REASON_REQUIRED;
			GError *local_error2 = NULL;

			if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
				reason = E_SOURCE_CREDENTIALS_REASON_SSL_FAILED;
			}

			if (!e_backend_credentials_required_sync (E_BACKEND (backend), reason, certificate_pem, certificate_errors,
				local_error, cancellable, &local_error2)) {
				g_warning ("%s: Failed to call credentials required: %s", G_STRFUNC, local_error2 ? local_error2->message : "Unknown error");
			}

			if (!local_error2 && (
			    g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED) ||
			    g_error_matches (local_error, E_DATA_CAL_ERROR, AuthenticationRequired) ||
			    g_error_matches (local_error, E_DATA_CAL_ERROR, AuthenticationFailed))) {
				/* These errors are treated through the authentication */
				g_clear_error (&local_error);
			} else {
				if (local_error)
					g_propagate_error (perror, local_error);
				local_error = NULL;
			}
			g_clear_error (&local_error2);
		}

		g_clear_error (&local_error);
		g_free (certificate_pem);
		g_free (auth_method);
	} else {
		e_cal_backend_set_writable (E_CAL_BACKEND (cbdav), FALSE);
	}

	g_mutex_unlock (&cbdav->priv->busy_lock);
}

static void
caldav_refresh (ECalBackendSync *backend,
                EDataCal *cal,
                GCancellable *cancellable,
                GError **perror)
{
	ECalBackendCalDAV        *cbdav;
	gboolean                  online;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	g_mutex_lock (&cbdav->priv->busy_lock);

	if (!cbdav->priv->loaded
	    || cbdav->priv->slave_cmd == SLAVE_SHOULD_DIE) {
		g_mutex_unlock (&cbdav->priv->busy_lock);
		return;
	}

	if (!e_backend_get_online (E_BACKEND (backend)) &&
	    e_backend_is_destination_reachable (E_BACKEND (backend), cancellable, NULL)) {
		e_backend_set_online (E_BACKEND (backend), TRUE);
	}

	if (!check_state (cbdav, &online, NULL) || !online) {
		g_mutex_unlock (&cbdav->priv->busy_lock);
		return;
	}

	update_slave_cmd (cbdav->priv, SLAVE_SHOULD_WORK_NO_CTAG_CHECK);

	/* wake it up */
	g_cond_signal (&cbdav->priv->cond);
	g_mutex_unlock (&cbdav->priv->busy_lock);
}

static void
remove_comp_from_cache_cb (gpointer value,
                           gpointer user_data)
{
	ECalComponent *comp = value;
	ECalBackendStore *store = user_data;
	ECalComponentId *id;

	g_return_if_fail (comp != NULL);
	g_return_if_fail (store != NULL);

	id = e_cal_component_get_id (comp);
	g_return_if_fail (id != NULL);

	e_cal_backend_store_remove_component (store, id->uid, id->rid);
	e_cal_component_free_id (id);
}

static gboolean
remove_comp_from_cache (ECalBackendCalDAV *cbdav,
                        const gchar *uid,
                        const gchar *rid)
{
	gboolean res = FALSE;

	if (!rid || !*rid) {
		/* get with detached instances */
		GSList *objects = e_cal_backend_store_get_components_by_uid (cbdav->priv->store, uid);

		if (objects) {
			g_slist_foreach (objects, (GFunc) remove_comp_from_cache_cb, cbdav->priv->store);
			g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
			g_slist_free (objects);

			res = TRUE;
		}
	} else {
		res = e_cal_backend_store_remove_component (cbdav->priv->store, uid, rid);
	}

	return res;
}

static void
add_detached_recur_to_vcalendar_cb (gpointer value,
                                    gpointer user_data)
{
	icalcomponent *recurrence = e_cal_component_get_icalcomponent (value);
	icalcomponent *vcalendar = user_data;

	icalcomponent_add_component (
		vcalendar,
		icalcomponent_new_clone (recurrence));
}

static gint
sort_master_first (gconstpointer a,
                   gconstpointer b)
{
	icalcomponent *ca, *cb;

	ca = e_cal_component_get_icalcomponent ((ECalComponent *) a);
	cb = e_cal_component_get_icalcomponent ((ECalComponent *) b);

	if (!ca) {
		if (!cb)
			return 0;
		else
			return -1;
	} else if (!cb) {
		return 1;
	}

	return icaltime_compare (icalcomponent_get_recurrenceid (ca), icalcomponent_get_recurrenceid (cb));
}

/* Returns new icalcomponent, with all detached instances stored in a cache.
 * The cache lock should be locked when called this function.
*/
static icalcomponent *
get_comp_from_cache (ECalBackendCalDAV *cbdav,
                     const gchar *uid,
                     const gchar *rid,
                     gchar **href,
                     gchar **etag)
{
	icalcomponent *icalcomp = NULL;

	if (rid == NULL || !*rid) {
		/* get with detached instances */
		GSList *objects = e_cal_backend_store_get_components_by_uid (cbdav->priv->store, uid);

		if (!objects) {
			return NULL;
		}

		if (g_slist_length (objects) == 1) {
			ECalComponent *comp = objects->data;

			/* will be unreffed a bit later */
			if (comp)
				icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
		} else {
			/* if we have detached recurrences, return a VCALENDAR */
			icalcomp = e_cal_util_new_top_level ();

			objects = g_slist_sort (objects, sort_master_first);

			/* add all detached recurrences and the master object */
			g_slist_foreach (objects, add_detached_recur_to_vcalendar_cb, icalcomp);
		}

		/* every component has set same href and etag, thus it doesn't matter where it will be read */
		if (href)
			*href = ecalcomp_get_href (objects->data);
		if (etag)
			*etag = ecalcomp_get_etag (objects->data);

		g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
		g_slist_free (objects);
	} else {
		/* get the exact object */
		ECalComponent *comp = e_cal_backend_store_get_component (cbdav->priv->store, uid, rid);

		if (comp) {
			icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
			if (href)
				*href = ecalcomp_get_href (comp);
			if (etag)
				*etag = ecalcomp_get_etag (comp);
			g_object_unref (comp);
		}
	}

	return icalcomp;
}

static void
put_server_comp_to_cache (ECalBackendCalDAV *cbdav,
                          icalcomponent *icomp,
                          const gchar *href,
                          const gchar *etag,
                          GTree *c_uid2complist)
{
	icalcomponent_kind kind;
	ECalBackend *cal_backend;

	g_return_if_fail (cbdav != NULL);
	g_return_if_fail (icomp != NULL);

	cal_backend = E_CAL_BACKEND (cbdav);
	kind = icalcomponent_isa (icomp);
	extract_timezones (cbdav, icomp);

	if (kind == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;

		kind = e_cal_backend_get_kind (cal_backend);

		for (subcomp = icalcomponent_get_first_component (icomp, kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icomp, kind)) {
			ECalComponent *new_comp, *old_comp;

			convert_to_url_attachment (cbdav, subcomp);
			new_comp = e_cal_component_new ();
			if (e_cal_component_set_icalcomponent (new_comp, icalcomponent_new_clone (subcomp))) {
				const gchar *uid = NULL;
				struct cache_comp_list *ccl;

				e_cal_component_get_uid (new_comp, &uid);
				if (!uid) {
					g_warning ("%s: no UID on component!", G_STRFUNC);
					g_object_unref (new_comp);
					continue;
				}

				if (href)
					ecalcomp_set_href (new_comp, href);
				if (etag)
					ecalcomp_set_etag (new_comp, etag);

				old_comp = NULL;
				if (c_uid2complist) {
					ccl = g_tree_lookup (c_uid2complist, uid);
					if (ccl) {
						gchar *nc_rid = e_cal_component_get_recurid_as_string (new_comp);
						GSList *p;

						for (p = ccl->slist; p && !old_comp; p = p->next) {
							gchar *oc_rid;

							old_comp = p->data;

							oc_rid = e_cal_component_get_recurid_as_string (old_comp);
							if (g_strcmp0 (nc_rid, oc_rid) != 0) {
								old_comp = NULL;
							}

							g_free (oc_rid);
						}

						g_free (nc_rid);
					}
				}

				put_component_to_store (cbdav, new_comp);

				if (old_comp == NULL) {
					e_cal_backend_notify_component_created (cal_backend, new_comp);
				} else {
					e_cal_backend_notify_component_modified (cal_backend, old_comp, new_comp);

					if (ccl)
						ccl->slist = g_slist_remove (ccl->slist, old_comp);
					g_object_unref (old_comp);
				}
			}

			g_object_unref (new_comp);
		}
	}
}

static gboolean
put_comp_to_cache (ECalBackendCalDAV *cbdav,
                   icalcomponent *icalcomp,
                   const gchar *href,
                   const gchar *etag)
{
	icalcomponent_kind my_kind;
	ECalComponent *comp;
	gboolean res = FALSE;

	g_return_val_if_fail (cbdav != NULL, FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	my_kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbdav));
	comp = e_cal_component_new ();

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;

		/* remove all old components from the cache first */
		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icalcomp, my_kind)) {
			remove_comp_from_cache (cbdav, icalcomponent_get_uid (subcomp), NULL);
		}

		/* then put new. It's because some detached instances could be removed on the server. */
		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icalcomp, my_kind)) {
			/* because reusing the same comp doesn't clear recur_id member properly */
			g_object_unref (comp);
			comp = e_cal_component_new ();

			if (e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp))) {
				if (href)
					ecalcomp_set_href (comp, href);
				if (etag)
					ecalcomp_set_etag (comp, etag);

				if (put_component_to_store (cbdav, comp))
					res = TRUE;
			}
		}
	} else if (icalcomponent_isa (icalcomp) == my_kind) {
		remove_comp_from_cache (cbdav, icalcomponent_get_uid (icalcomp), NULL);

		if (e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp))) {
			if (href)
				ecalcomp_set_href (comp, href);
			if (etag)
				ecalcomp_set_etag (comp, etag);

			res = put_component_to_store (cbdav, comp);
		}
	}

	g_object_unref (comp);

	return res;
}

static void
remove_property (gpointer prop,
                 gpointer icomp)
{
	icalcomponent_remove_property (icomp, prop);
	icalproperty_free (prop);
}

static void
strip_unneeded_x_props (icalcomponent *icomp)
{
	icalproperty *prop;
	GSList *to_remove = NULL;

	g_return_if_fail (icomp != NULL);
	g_return_if_fail (icalcomponent_isa (icomp) != ICAL_VCALENDAR_COMPONENT);

	for (prop = icalcomponent_get_first_property (icomp, ICAL_X_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property (icomp, ICAL_X_PROPERTY)) {
		if (g_str_has_prefix (icalproperty_get_x_name (prop), X_E_CALDAV)) {
			to_remove = g_slist_prepend (to_remove, prop);
		}
	}

	for (prop = icalcomponent_get_first_property (icomp, ICAL_XLICERROR_PROPERTY);
	     prop;
	     prop = icalcomponent_get_next_property (icomp, ICAL_XLICERROR_PROPERTY)) {
		to_remove = g_slist_prepend (to_remove, prop);
	}

	g_slist_foreach (to_remove, remove_property, icomp);
	g_slist_free (to_remove);
}

static gboolean
is_stored_on_server (ECalBackendCalDAV *cbdav,
                     const gchar *uri)
{
	SoupURI *my_uri, *test_uri;
	gboolean res;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);
	g_return_val_if_fail (cbdav->priv->uri != NULL, FALSE);
	g_return_val_if_fail (uri != NULL, FALSE);

	my_uri = soup_uri_new (cbdav->priv->uri);
	g_return_val_if_fail (my_uri != NULL, FALSE);

	test_uri = soup_uri_new (uri);
	if (!test_uri) {
		soup_uri_free (my_uri);
		return FALSE;
	}

	res = my_uri->host && test_uri->host && g_ascii_strcasecmp (my_uri->host, test_uri->host) == 0;

	soup_uri_free (my_uri);
	soup_uri_free (test_uri);

	return res;
}

static void
convert_to_inline_attachment (ECalBackendCalDAV *cbdav,
                              icalcomponent *icalcomp)
{
	icalcomponent *cclone;
	icalproperty *p;
	GSList *to_remove = NULL;

	g_return_if_fail (icalcomp != NULL);

	cclone = icalcomponent_new_clone (icalcomp);

	/* Remove local url attachments first */
	for (p = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	     p;
	     p = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
		icalattach *attach;

		attach = icalproperty_get_attach ((const icalproperty *) p);
		if (icalattach_get_is_url (attach)) {
			const gchar *url;

			url = icalattach_get_url (attach);
			if (g_str_has_prefix (url, LOCAL_PREFIX))
				to_remove = g_slist_prepend (to_remove, p);
		}
	}
	g_slist_foreach (to_remove, remove_property, icalcomp);
	g_slist_free (to_remove);

	/* convert local url attachments to inline attachments now */
	for (p = icalcomponent_get_first_property (cclone, ICAL_ATTACH_PROPERTY);
	     p;
	     p = icalcomponent_get_next_property (cclone, ICAL_ATTACH_PROPERTY)) {
		icalattach *attach;
		GFile *file;
		GError *error = NULL;
		const gchar *uri;
		gchar *basename;
		gchar *content;
		gsize len;

		attach = icalproperty_get_attach ((const icalproperty *) p);
		if (!icalattach_get_is_url (attach))
			continue;

		uri = icalattach_get_url (attach);
		if (!g_str_has_prefix (uri, LOCAL_PREFIX))
			continue;

		file = g_file_new_for_uri (uri);
		basename = g_file_get_basename (file);
		if (g_file_load_contents (file, NULL, &content, &len, NULL, &error)) {
			icalproperty *prop;
			icalparameter *param;
			gchar *encoded;

			/*
			 * do a base64 encoding so it can
			 * be embedded in a soap message
			 */
			encoded = g_base64_encode ((guchar *) content, len);
			attach = icalattach_new_from_data (encoded, NULL, NULL);
			g_free (content);
			g_free (encoded);

			prop = icalproperty_new_attach (attach);
			icalattach_unref (attach);

			param = icalparameter_new_value (ICAL_VALUE_BINARY);
			icalproperty_add_parameter (prop, param);

			param = icalparameter_new_encoding (ICAL_ENCODING_BASE64);
			icalproperty_add_parameter (prop, param);

			param = icalparameter_new_x (basename);
			icalparameter_set_xname (param, X_E_CALDAV_ATTACHMENT_NAME);
			icalproperty_add_parameter (prop, param);

			icalcomponent_add_property (icalcomp, prop);
		} else {
			g_warning ("%s\n", error->message);
			g_clear_error (&error);
		}
		g_free (basename);
		g_object_unref (file);
	}
	icalcomponent_free (cclone);
}

static void
convert_to_url_attachment (ECalBackendCalDAV *cbdav,
                           icalcomponent *icalcomp)
{
	ECalBackend *backend;
	GSList *to_remove = NULL, *to_remove_after_download = NULL;
	icalcomponent *cclone;
	icalproperty *p;
	gint fileindex;

	g_return_if_fail (cbdav != NULL);
	g_return_if_fail (icalcomp != NULL);

	backend = E_CAL_BACKEND (cbdav);
	cclone = icalcomponent_new_clone (icalcomp);

	/* Remove all inline attachments first */
	for (p = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	     p;
	     p = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
		icalattach *attach;

		attach = icalproperty_get_attach ((const icalproperty *) p);
		if (!icalattach_get_is_url (attach))
			to_remove = g_slist_prepend (to_remove, p);
		else if (is_stored_on_server (cbdav, icalattach_get_url (attach)))
			to_remove_after_download = g_slist_prepend (to_remove_after_download, p);
	}
	g_slist_foreach (to_remove, remove_property, icalcomp);
	g_slist_free (to_remove);

	/* convert inline attachments to url attachments now */
	for (p = icalcomponent_get_first_property (cclone, ICAL_ATTACH_PROPERTY), fileindex = 0;
	     p;
	     p = icalcomponent_get_next_property (cclone, ICAL_ATTACH_PROPERTY), fileindex++) {
		icalattach *attach;
		gsize len = -1;
		gchar *decoded = NULL;
		gchar *basename, *local_filename;

		attach = icalproperty_get_attach ((const icalproperty *) p);
		if (icalattach_get_is_url (attach)) {
			const gchar *attach_url = icalattach_get_url (attach);
			GError *error = NULL;

			if (!is_stored_on_server (cbdav, attach_url))
				continue;

			if (!caldav_server_download_attachment (cbdav, attach_url, &decoded, &len, &error)) {
				if (caldav_debug_show (DEBUG_ATTACHMENTS))
					g_print ("CalDAV::%s: Failed to download from a server: %s\n", G_STRFUNC, error ? error->message : "Unknown error");
				continue;
			}
		}

		basename = icalproperty_get_parameter_as_string_r (p, X_E_CALDAV_ATTACHMENT_NAME);
		local_filename = e_cal_backend_create_cache_filename (backend, icalcomponent_get_uid (icalcomp), basename, fileindex);
		g_free (basename);

		if (local_filename) {
			GError *error = NULL;

			if (decoded == NULL) {
				gchar *content;

				content = (gchar *) icalattach_get_data (attach);
				decoded = (gchar *) g_base64_decode (content, &len);
			}

			if (g_file_set_contents (local_filename, decoded, len, &error)) {
				icalproperty *prop;
				gchar *url;

				url = g_filename_to_uri (local_filename, NULL, NULL);
				attach = icalattach_new_from_url (url);
				prop = icalproperty_new_attach (attach);
				icalattach_unref (attach);
				icalcomponent_add_property (icalcomp, prop);
				g_free (url);
			} else {
				g_warning ("%s\n", error->message);
				g_clear_error (&error);
			}

			g_free (local_filename);
		}
	}

	icalcomponent_free (cclone);

	g_slist_foreach (to_remove_after_download, remove_property, icalcomp);
	g_slist_free (to_remove_after_download);
}

static void
remove_files (const gchar *dir,
              const gchar *fileprefix)
{
	GDir *d;

	g_return_if_fail (dir != NULL);
	g_return_if_fail (fileprefix != NULL);
	g_return_if_fail (*fileprefix != '\0');

	d = g_dir_open (dir, 0, NULL);
	if (d) {
		const gchar *entry;
		gint len = strlen (fileprefix);

		while ((entry = g_dir_read_name (d)) != NULL) {
			if (entry && strncmp (entry, fileprefix, len) == 0) {
				gchar *path;

				path = g_build_filename (dir, entry, NULL);
				if (!g_file_test (path, G_FILE_TEST_IS_DIR))
					g_unlink (path);
				g_free (path);
			}
		}
		g_dir_close (d);
	}
}

static void
remove_cached_attachment (ECalBackendCalDAV *cbdav,
                          const gchar *uid)
{
	GSList *l;
	guint len;
	gchar *dir;
	gchar *fileprefix;

	g_return_if_fail (cbdav != NULL);
	g_return_if_fail (uid != NULL);

	l = e_cal_backend_store_get_components_by_uid (cbdav->priv->store, uid);
	len = g_slist_length (l);
	g_slist_foreach (l, (GFunc) g_object_unref, NULL);
	g_slist_free (l);
	if (len > 0)
		return;

	dir = e_cal_backend_create_cache_filename (E_CAL_BACKEND (cbdav), uid, "a", 0);
	if (!dir)
		return;

	fileprefix = g_strrstr (dir, G_DIR_SEPARATOR_S);
	if (fileprefix) {
		*fileprefix = '\0';
		fileprefix++;

		if (*fileprefix)
			fileprefix[strlen (fileprefix) - 1] = '\0';

		remove_files (dir, fileprefix);
	}

	g_free (dir);
}

/* callback for icalcomponent_foreach_tzid */
typedef struct {
	ECalBackendStore *store;
	icalcomponent *vcal_comp;
	icalcomponent *icalcomp;
} ForeachTzidData;

static void
add_timezone_cb (icalparameter *param,
                 gpointer data)
{
	icaltimezone *tz;
	const gchar *tzid;
	icalcomponent *vtz_comp;
	ForeachTzidData *f_data = (ForeachTzidData *) data;
	ETimezoneCache *cache;

	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	tz = icalcomponent_get_timezone (f_data->vcal_comp, tzid);
	if (tz)
		return;

	cache = e_cal_backend_store_ref_timezone_cache (f_data->store);

	tz = icalcomponent_get_timezone (f_data->icalcomp, tzid);
	if (tz == NULL)
		tz = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	if (tz == NULL)
		tz = e_timezone_cache_get_timezone (cache, tzid);

	vtz_comp = icaltimezone_get_component (tz);

	if (tz != NULL && vtz_comp != NULL)
		icalcomponent_add_component (
			f_data->vcal_comp,
			icalcomponent_new_clone (vtz_comp));

	g_object_unref (cache);
}

static void
add_timezones_from_component (ECalBackendCalDAV *cbdav,
                              icalcomponent *vcal_comp,
                              icalcomponent *icalcomp)
{
	ForeachTzidData f_data;

	g_return_if_fail (cbdav != NULL);
	g_return_if_fail (vcal_comp != NULL);
	g_return_if_fail (icalcomp != NULL);

	f_data.store = cbdav->priv->store;
	f_data.vcal_comp = vcal_comp;
	f_data.icalcomp = icalcomp;

	icalcomponent_foreach_tzid (icalcomp, add_timezone_cb, &f_data);
}

/* also removes X-EVOLUTION-CALDAV from all the components */
static gchar *
pack_cobj (ECalBackendCalDAV *cbdav,
           icalcomponent *icomp)
{
	icalcomponent *calcomp;
	gchar          *objstr;

	if (icalcomponent_isa (icomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *cclone;

		calcomp = e_cal_util_new_top_level ();

		cclone = icalcomponent_new_clone (icomp);
		strip_unneeded_x_props (cclone);
		convert_to_inline_attachment (cbdav, cclone);
		icalcomponent_add_component (calcomp, cclone);
		add_timezones_from_component (cbdav, calcomp, cclone);
	} else {
		icalcomponent *subcomp;
		icalcomponent_kind my_kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbdav));

		calcomp = icalcomponent_new_clone (icomp);
		for (subcomp = icalcomponent_get_first_component (calcomp, my_kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (calcomp, my_kind)) {
			strip_unneeded_x_props (subcomp);
			convert_to_inline_attachment (cbdav, subcomp);
			add_timezones_from_component (cbdav, calcomp, subcomp);
		}
	}

	objstr = icalcomponent_as_ical_string_r (calcomp);
	icalcomponent_free (calcomp);

	g_return_val_if_fail (objstr != NULL, NULL);

	return objstr;

}

static void
maybe_correct_tzid (ECalBackendCalDAV *cbdav,
		    ECalComponentDateTime *dt)
{
	icaltimezone *zone;

	zone = e_timezone_cache_get_timezone (E_TIMEZONE_CACHE (cbdav), dt->tzid);
	if (!zone) {
		g_free ((gchar *) dt->tzid);
		dt->tzid = g_strdup ("UTC");
	} else if (cbdav->priv->is_icloud && !dt->value->is_date) {
		const gchar *location = icaltimezone_get_location (zone);

		if (location && *location) {
			g_free ((gchar *) dt->tzid);
			dt->tzid = g_strdup (location);
		} else {
			/* No location available for this timezone, convert to UTC */
			dt->value->zone = zone;
			*dt->value = icaltime_convert_to_zone (*dt->value, icaltimezone_get_utc_timezone ());
			g_free ((gchar *) dt->tzid);
			dt->tzid = g_strdup ("UTC");
		}
	}
}

static void
sanitize_component (ECalBackendCalDAV *cbdav,
                    ECalComponent *comp)
{
	ECalComponentDateTime dt;

	/* Check dtstart, dtend and due's timezone, and convert it to local
	 * default timezone if the timezone is not in our builtin timezone
	 * list */
	e_cal_component_get_dtstart (comp, &dt);
	if (dt.value && dt.tzid) {
		maybe_correct_tzid (cbdav, &dt);
		e_cal_component_set_dtstart (comp, &dt);
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_get_dtend (comp, &dt);
	if (dt.value && dt.tzid) {
		maybe_correct_tzid (cbdav, &dt);
		e_cal_component_set_dtend (comp, &dt);
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_get_due (comp, &dt);
	if (dt.value && dt.tzid) {
		maybe_correct_tzid (cbdav, &dt);
		e_cal_component_set_due (comp, &dt);
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_abort_sequence (comp);
}

static gboolean
cache_contains (ECalBackendCalDAV *cbdav,
                const gchar *uid,
                const gchar *rid)
{
	gboolean res;
	ECalComponent *comp;

	g_return_val_if_fail (cbdav != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	g_return_val_if_fail (cbdav->priv->store != NULL, FALSE);

	comp = e_cal_backend_store_get_component (cbdav->priv->store, uid, rid);
	res = comp != NULL;

	if (comp)
		g_object_unref (comp);

	return res;
}

/* Returns subcomponent of icalcomp, which is a master object, or icalcomp itself, if it's not a VCALENDAR;
 * Do not free returned pointer, it'll be freed together with the icalcomp.
*/
static icalcomponent *
get_master_comp (ECalBackendCalDAV *cbdav,
                 icalcomponent *icalcomp)
{
	icalcomponent *master = icalcomp;

	g_return_val_if_fail (cbdav != NULL, NULL);
	g_return_val_if_fail (icalcomp != NULL, NULL);

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;
		icalcomponent_kind my_kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbdav));

		master = NULL;

		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icalcomp, my_kind)) {
			struct icaltimetype sub_rid = icalcomponent_get_recurrenceid (subcomp);

			if (icaltime_is_null_time (sub_rid)) {
				master = subcomp;
				break;
			}
		}
	}

	return master;
}

static gboolean
remove_instance (ECalBackendCalDAV *cbdav,
                 icalcomponent *icalcomp,
                 struct icaltimetype rid,
                 ECalObjModType mod,
                 gboolean also_exdate)
{
	icalcomponent *master = icalcomp;
	struct icaltimetype master_dtstart;
	gboolean res = FALSE;

	g_return_val_if_fail (icalcomp != NULL, res);
	g_return_val_if_fail (!icaltime_is_null_time (rid), res);

	master_dtstart = icalcomponent_get_dtstart (master);
	if (master_dtstart.zone && master_dtstart.zone != rid.zone)
		rid = icaltime_convert_to_zone (rid, (icaltimezone *) master_dtstart.zone);

	rid = icaltime_convert_to_zone (rid, icaltimezone_get_utc_timezone ());

	/* remove an instance only */
	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;
		icalcomponent_kind my_kind = e_cal_backend_get_kind (E_CAL_BACKEND (cbdav));
		gint left = 0;
		gboolean start_first = FALSE;

		master = NULL;

		/* remove old instance first */
		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = start_first ? icalcomponent_get_first_component (icalcomp, my_kind) : icalcomponent_get_next_component (icalcomp, my_kind)) {
			struct icaltimetype sub_rid = icalcomponent_get_recurrenceid (subcomp);

			start_first = FALSE;

			if (icaltime_is_null_time (sub_rid)) {
				master = subcomp;
				left++;
			} else if (icaltime_compare (sub_rid, rid) == 0) {
				icalcomponent_remove_component (icalcomp, subcomp);
				icalcomponent_free (subcomp);
				if (master) {
					break;
				} else {
					/* either no master or master not as the first component, thus rescan */
					left = 0;
					start_first = TRUE;
				}
			} else {
				left++;
			}
		}

		/* whether left at least one instance or a master object */
		res = left > 0;
	} else {
		res = TRUE;
	}

	if (master && also_exdate) {
		e_cal_util_remove_instances (master, rid, mod);
	}

	return res;
}

static icalcomponent *
replace_master (ECalBackendCalDAV *cbdav,
                icalcomponent *old_comp,
                icalcomponent *new_master)
{
	icalcomponent *old_master;
	if (icalcomponent_isa (old_comp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (old_comp);
		return new_master;
	}

	old_master = get_master_comp (cbdav, old_comp);
	if (!old_master) {
		/* no master, strange */
		icalcomponent_free (new_master);
	} else {
		icalcomponent_remove_component (old_comp, old_master);
		icalcomponent_free (old_master);

		icalcomponent_add_component (old_comp, new_master);
	}

	return old_comp;
}

/* the resulting component should be unreffed when done with it;
 * the fallback_comp is cloned, if used */
static ECalComponent *
get_ecalcomp_master_from_cache_or_fallback (ECalBackendCalDAV *cbdav,
                                            const gchar *uid,
                                            const gchar *rid,
                                            ECalComponent *fallback_comp)
{
	ECalComponent *comp = NULL;
	icalcomponent *icalcomp;

	g_return_val_if_fail (cbdav != NULL, NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	icalcomp = get_comp_from_cache (cbdav, uid, rid, NULL, NULL);
	if (icalcomp) {
		icalcomponent *master = get_master_comp (cbdav, icalcomp);

		if (master) {
			comp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master));
		}

		icalcomponent_free (icalcomp);
	}

	if (!comp && fallback_comp)
		comp = e_cal_component_clone (fallback_comp);

	return comp;
}

/* a busy_lock is supposed to be locked already, when calling this function */
static void
do_create_objects (ECalBackendCalDAV *cbdav,
                   const GSList *in_calobjs,
                   GSList **uids,
                   GSList **new_components,
                   GCancellable *cancellable,
                   GError **perror)
{
	ECalComponent            *comp;
	gboolean                  online, did_put = FALSE;
	struct icaltimetype current;
	icalcomponent *icalcomp;
	const gchar *in_calobj = in_calobjs->data;
	const gchar *comp_uid;

	if (!check_state (cbdav, &online, perror))
		return;

	/* We make the assumption that the in_calobjs list we're passed is always exactly one element long, since we haven't specified "bulk-adds"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
	if (in_calobjs->next != NULL) {
		g_propagate_error (perror, e_data_cal_create_error (UnsupportedMethod, _("CalDAV does not support bulk additions")));
		return;
	}

	comp = e_cal_component_new_from_string (in_calobj);

	if (comp == NULL) {
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		return;
	}

	icalcomp = e_cal_component_get_icalcomponent (comp);
	if (icalcomp == NULL) {
		g_object_unref (comp);
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		return;
	}

	comp_uid = icalcomponent_get_uid (icalcomp);
	if (!comp_uid) {
		gchar *new_uid;

		new_uid = e_cal_component_gen_uid ();
		if (!new_uid) {
			g_object_unref (comp);
			g_propagate_error (perror, EDC_ERROR (InvalidObject));
			return;
		}

		icalcomponent_set_uid (icalcomp, new_uid);
		comp_uid = icalcomponent_get_uid (icalcomp);

		g_free (new_uid);
	}

	/* check the object is not in our cache */
	if (cache_contains (cbdav, comp_uid, NULL)) {
		g_object_unref (comp);
		g_propagate_error (perror, EDC_ERROR (ObjectIdAlreadyExists));
		return;
	}

	/* Set the created and last modified times on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (comp, &current);
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component*/
	sanitize_component (cbdav, comp);

	if (online) {
		CalDAVObject object;

		object.href = ecalcomp_gen_href (comp);
		object.etag = NULL;
		object.cdata = pack_cobj (cbdav, icalcomp);

		did_put = caldav_server_put_object (cbdav, &object, icalcomp, cancellable, perror);

		caldav_object_free (&object, FALSE);
	} else {
		/* mark component as out of synch */
		/*ecalcomp_set_synch_state (comp, ECALCOMP_LOCALLY_CREATED); */
	}

	if (did_put) {
		if (uids)
			*uids = g_slist_prepend (*uids, g_strdup (comp_uid));

		if (new_components)
			*new_components = g_slist_prepend(*new_components, get_ecalcomp_master_from_cache_or_fallback (cbdav, comp_uid, NULL, comp));
	}

	g_object_unref (comp);
}

/* a busy_lock is supposed to be locked already, when calling this function */
static void
do_modify_objects (ECalBackendCalDAV *cbdav,
                   const GSList *calobjs,
                   ECalObjModType mod,
                   GSList **old_components,
                   GSList **new_components,
                   GCancellable *cancellable,
                   GError **error)
{
	ECalComponent            *comp;
	icalcomponent            *cache_comp, *master_comp;
	gboolean                  online, did_put = FALSE, success = TRUE;
	ECalComponentId		 *id;
	struct icaltimetype current;
	gchar *href = NULL, *etag = NULL;
	const gchar *calobj = calobjs->data;

	if (new_components)
		*new_components = NULL;

	if (!check_state (cbdav, &online, error))
		return;

	/* We make the assumption that the calobjs list we're passed is always exactly one element long, since we haven't specified "bulk-modifies"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
	if (calobjs->next != NULL) {
		g_propagate_error (error, e_data_cal_create_error (UnsupportedMethod, _("CalDAV does not support bulk modifications")));
		return;
	}

	comp = e_cal_component_new_from_string (calobj);

	if (comp == NULL) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	if (!e_cal_component_get_icalcomponent (comp) ||
	    icalcomponent_isa (e_cal_component_get_icalcomponent (comp)) != e_cal_backend_get_kind (E_CAL_BACKEND (cbdav))) {
		g_object_unref (comp);
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	/* Set the last modified time on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component */
	sanitize_component (cbdav, comp);

	id = e_cal_component_get_id (comp);
	if (id == NULL) {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	/* fetch full component from cache, it will be pushed to the server */
	cache_comp = get_comp_from_cache (cbdav, id->uid, NULL, &href, &etag);

	if (cache_comp == NULL) {
		e_cal_component_free_id (id);
		g_object_unref (comp);
		g_free (href);
		g_free (etag);
		g_propagate_error (error, EDC_ERROR (ObjectNotFound));
		return;
	}

	if (!online) {
		/* mark component as out of synch */
		/*ecalcomp_set_synch_state (comp, ECALCOMP_LOCALLY_MODIFIED);*/
	}

	if (old_components) {
		*old_components = NULL;

		if (e_cal_component_is_instance (comp)) {
			/* set detached instance as the old object, if any */
			ECalComponent *old_instance = e_cal_backend_store_get_component (cbdav->priv->store, id->uid, id->rid);

			/* This will give a reference to 'old_component' */
			if (old_instance) {
				*old_components = g_slist_prepend (*old_components, e_cal_component_clone (old_instance));
				g_object_unref (old_instance);
			}
		}

		if (!*old_components) {
			icalcomponent *master = get_master_comp (cbdav, cache_comp);

			if (master) {
				/* set full component as the old object */
				*old_components = g_slist_prepend (*old_components, e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master)));
			}
		}
	}

	switch (mod) {
	case E_CAL_OBJ_MOD_ONLY_THIS:
	case E_CAL_OBJ_MOD_THIS:
		if (e_cal_component_is_instance (comp)) {
			icalcomponent *new_comp = e_cal_component_get_icalcomponent (comp);

			/* new object is only this instance */
			if (new_components)
				*new_components = g_slist_prepend (*new_components, e_cal_component_clone (comp));

			/* add the detached instance */
			if (icalcomponent_isa (cache_comp) == ICAL_VCALENDAR_COMPONENT) {
				/* do not modify the EXDATE, as the component will be put back */
				remove_instance (cbdav, cache_comp, icalcomponent_get_recurrenceid (new_comp), mod, FALSE);
			} else {
				/* this is only a master object, thus make is a VCALENDAR component */
				icalcomponent *icomp;

				icomp = e_cal_util_new_top_level ();
				icalcomponent_add_component (icomp, cache_comp);

				/* no need to free the cache_comp, as it is inside icomp */
				cache_comp = icomp;
			}

			if (cache_comp && cbdav->priv->is_google) {
				icalcomponent_set_sequence (cache_comp, icalcomponent_get_sequence (cache_comp) + 1);
				icalcomponent_set_sequence (new_comp, icalcomponent_get_sequence (new_comp) + 1);
			}

			/* add the detached instance finally */
			icalcomponent_add_component (cache_comp, icalcomponent_new_clone (new_comp));
		} else {
			cache_comp = replace_master (cbdav, cache_comp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
		}
		break;
	case E_CAL_OBJ_MOD_ALL:
		e_cal_recur_ensure_end_dates (comp, TRUE, resolve_tzid, cbdav);
		cache_comp = replace_master (cbdav, cache_comp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
		break;
	case E_CAL_OBJ_MOD_THIS_AND_PRIOR:
	case E_CAL_OBJ_MOD_THIS_AND_FUTURE:
		master_comp = get_master_comp (cbdav, cache_comp);
		if (e_cal_component_is_instance (comp) && master_comp) {
			ECalComponent *mcomp;
			struct icaltimetype rid, master_dtstart;
			icalcomponent *icalcomp = e_cal_component_get_icalcomponent (comp);
			icalcomponent *split_icalcomp;
			icalproperty *prop;

			rid = icalcomponent_get_recurrenceid (icalcomp);
			mcomp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master_comp));

			if (mod == E_CAL_OBJ_MOD_THIS_AND_FUTURE &&
			    e_cal_util_is_first_instance (mcomp, icalcomponent_get_recurrenceid (icalcomp), resolve_tzid, cbdav)) {
				icalproperty *prop = icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY);

				if (prop)
					icalcomponent_remove_property (icalcomp, prop);

				e_cal_component_rescan (comp);
				e_cal_recur_ensure_end_dates (comp, TRUE, resolve_tzid, cbdav);

				/* Then do it like for "mod_all" */
				cache_comp = replace_master (cbdav, cache_comp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
				g_clear_object (&mcomp);

				if (new_components) {
					/* read the comp from cache again, as some servers can modify it on put */
					*new_components = g_slist_prepend (*new_components, get_ecalcomp_master_from_cache_or_fallback (cbdav, id->uid, NULL, comp));
				}
				break;
			}

			prop = icalcomponent_get_first_property (icalcomp, ICAL_RECURRENCEID_PROPERTY);
			if (prop)
				icalcomponent_remove_property (icalcomp, prop);
			e_cal_component_rescan (comp);

			master_dtstart = icalcomponent_get_dtstart (master_comp);
			if (master_dtstart.zone && master_dtstart.zone != rid.zone)
				rid = icaltime_convert_to_zone (rid, (icaltimezone *) master_dtstart.zone);
			split_icalcomp = e_cal_util_split_at_instance (icalcomp, rid, master_dtstart);
			if (split_icalcomp) {
				ECalComponent *prev_comp;

				prev_comp = e_cal_component_clone (mcomp);

				rid = icaltime_convert_to_zone (rid, icaltimezone_get_utc_timezone ());
				e_cal_util_remove_instances (master_comp, rid, mod);
				e_cal_component_rescan (mcomp);
				e_cal_recur_ensure_end_dates (mcomp, TRUE, resolve_tzid, cbdav);

				if (new_components) {
					*new_components = g_slist_prepend (*new_components,
						get_ecalcomp_master_from_cache_or_fallback (cbdav, id->uid, NULL, mcomp));
				}

				g_clear_object (&prev_comp);
			}

			cache_comp = replace_master (cbdav, cache_comp, icalcomponent_new_clone (master_comp));
			if (split_icalcomp) {
				gchar *new_uid;

				new_uid = e_cal_component_gen_uid ();
				icalcomponent_set_uid (split_icalcomp, new_uid);
				g_free (new_uid);

				g_warn_if_fail (e_cal_component_set_icalcomponent (comp, split_icalcomp));

				e_cal_recur_ensure_end_dates (comp, TRUE, resolve_tzid, cbdav);

				/* sanitize the component */
				sanitize_component (cbdav, comp);

				if (online) {
					CalDAVObject object;

					object.href = ecalcomp_gen_href (comp);
					object.etag = NULL;
					object.cdata = pack_cobj (cbdav, split_icalcomp);

					success = caldav_server_put_object (cbdav, &object, split_icalcomp, cancellable, error);
					if (success && new_components) {
						ECalComponent *new_comp;

						/* read the comp from cache again, as some servers can modify it on put */
						new_comp = get_ecalcomp_master_from_cache_or_fallback (cbdav, icalcomponent_get_uid (split_icalcomp), NULL, comp);
						if (new_comp)
							e_cal_backend_notify_component_created (E_CAL_BACKEND (cbdav), new_comp);

						g_clear_object (&new_comp);
					}

					caldav_object_free (&object, FALSE);
				} else {
					/* mark component as out of synch */
					/*ecalcomp_set_synch_state (comp, ECALCOMP_LOCALLY_CREATED); */
				}
			}

			g_clear_object (&mcomp);
		} else {
			cache_comp = replace_master (cbdav, cache_comp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
		}
		break;
	}

	if (online) {
		CalDAVObject object;

		object.href = href;
		object.etag = etag;
		object.cdata = pack_cobj (cbdav, cache_comp);

		did_put = success && caldav_server_put_object (cbdav, &object, cache_comp, cancellable, error);

		caldav_object_free (&object, FALSE);
		href = NULL;
		etag = NULL;
	} else {
		/* mark component as out of synch */
		/*ecalcomp_set_synch_state (comp, ECALCOMP_LOCALLY_MODIFIED);*/
	}

	if (did_put) {
		if (new_components && !*new_components) {
			/* read the comp from cache again, as some servers can modify it on put */
			*new_components = g_slist_prepend (*new_components, get_ecalcomp_master_from_cache_or_fallback (cbdav, id->uid, id->rid, NULL));
		}
	}

	e_cal_component_free_id (id);
	icalcomponent_free (cache_comp);
	g_object_unref (comp);
	g_free (href);
	g_free (etag);
}

/* a busy_lock is supposed to be locked already, when calling this function */
static void
do_remove_objects (ECalBackendCalDAV *cbdav,
                   const GSList *ids,
                   ECalObjModType mod,
                   GSList **old_components,
                   GSList **new_components,
                   GCancellable *cancellable,
                   GError **perror)
{
	icalcomponent            *cache_comp;
	gboolean                  online;
	gchar *href = NULL, *etag = NULL;
	const gchar *uid = ((ECalComponentId *) ids->data)->uid;
	const gchar *rid = ((ECalComponentId *) ids->data)->rid;

	if (new_components)
		*new_components = NULL;

	if (!check_state (cbdav, &online, perror))
		return;

	/* We make the assumption that the ids list we're passed is always exactly one element long, since we haven't specified "bulk-removes"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
	if (ids->next != NULL) {
		g_propagate_error (perror, e_data_cal_create_error (UnsupportedMethod, _("CalDAV does not support bulk removals")));
		return;
	}

	cache_comp = get_comp_from_cache (cbdav, uid, NULL, &href, &etag);

	if (cache_comp == NULL) {
		g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
		return;
	}

	if (old_components) {
		ECalComponent *old = e_cal_backend_store_get_component (cbdav->priv->store, uid, rid);

		if (old) {
			*old_components = g_slist_prepend (*old_components, e_cal_component_clone (old));
			g_object_unref (old);
		} else {
			icalcomponent *master = get_master_comp (cbdav, cache_comp);
			if (master) {
				*old_components = g_slist_prepend (*old_components, e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master)));
			}
		}
	}

	switch (mod) {
	case E_CAL_OBJ_MOD_ONLY_THIS:
	case E_CAL_OBJ_MOD_THIS:
		if (rid && *rid) {
			/* remove one instance from the component */
			if (remove_instance (cbdav, cache_comp, icaltime_from_string (rid), mod, mod != E_CAL_OBJ_MOD_ONLY_THIS)) {
				if (new_components) {
					icalcomponent *master = get_master_comp (cbdav, cache_comp);
					if (master) {
						*new_components = g_slist_prepend (*new_components, e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master)));
					}
				}
			} else {
				/* this was the last instance, thus delete whole component */
				rid = NULL;
				remove_comp_from_cache (cbdav, uid, NULL);
			}
		} else {
			/* remove whole object */
			remove_comp_from_cache (cbdav, uid, NULL);
		}
		break;
	case E_CAL_OBJ_MOD_ALL:
		remove_comp_from_cache (cbdav, uid, NULL);
		break;
	case E_CAL_OBJ_MOD_THIS_AND_PRIOR:
	case E_CAL_OBJ_MOD_THIS_AND_FUTURE:
		if (remove_instance (cbdav, cache_comp, icaltime_from_string (rid), mod, TRUE)) {
			if (new_components) {
				icalcomponent *master = get_master_comp (cbdav, cache_comp);
				if (master) {
					*new_components = g_slist_prepend (*new_components, e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master)));
				}
			}
		}
		break;
	}

	if (online) {
		CalDAVObject caldav_object;

		caldav_object.href = href;
		caldav_object.etag = etag;
		caldav_object.cdata = NULL;

		if (mod == E_CAL_OBJ_MOD_THIS && rid && *rid) {
			caldav_object.cdata = pack_cobj (cbdav, cache_comp);

			caldav_server_put_object (cbdav, &caldav_object, cache_comp, cancellable, perror);
		} else
			caldav_server_delete_object (cbdav, &caldav_object, cancellable, perror);

		caldav_object_free (&caldav_object, FALSE);
		href = NULL;
		etag = NULL;
	} else {
		/* mark component as out of synch */
		/*if (mod == E_CAL_OBJ_MOD_THIS && rid && *rid)
			ecalcomp_set_synch_state (cache_comp_master, ECALCOMP_LOCALLY_MODIFIED);
		else
			ecalcomp_set_synch_state (cache_comp_master, ECALCOMP_LOCALLY_DELETED);*/
	}
	remove_cached_attachment (cbdav, uid);

	icalcomponent_free (cache_comp);
	g_free (href);
	g_free (etag);
}

static void
extract_objects (icalcomponent *icomp,
                 icalcomponent_kind ekind,
                 GSList **objects,
                 GError **error)
{
	icalcomponent         *scomp;
	icalcomponent_kind     kind;
	GSList *link;

	kind = icalcomponent_isa (icomp);

	if (kind == ekind) {
		*objects = g_slist_prepend (NULL, icomp);
		return;
	}

	if (kind != ICAL_VCALENDAR_COMPONENT) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	*objects = NULL;
	scomp = icalcomponent_get_first_component (icomp, ekind);

	while (scomp) {
		*objects = g_slist_prepend (*objects, scomp);

		scomp = icalcomponent_get_next_component (icomp, ekind);
	}

	for (link = *objects; link; link = g_slist_next (link)) {
		/* Remove components from toplevel here */
		icalcomponent_remove_component (icomp, link->data);
	}
}

static gboolean
extract_timezones (ECalBackendCalDAV *cbdav,
                   icalcomponent *icomp)
{
	ETimezoneCache *timezone_cache;
	GSList *timezones = NULL, *iter;
	icaltimezone *zone;
	GError *err = NULL;

	g_return_val_if_fail (cbdav != NULL, FALSE);
	g_return_val_if_fail (icomp != NULL, FALSE);

	timezone_cache = E_TIMEZONE_CACHE (cbdav);

	extract_objects (icomp, ICAL_VTIMEZONE_COMPONENT, &timezones, &err);
	if (err) {
		g_error_free (err);
		return FALSE;
	}

	zone = icaltimezone_new ();
	for (iter = timezones; iter; iter = iter->next) {
		if (icaltimezone_set_component (zone, iter->data)) {
			e_timezone_cache_add_timezone (timezone_cache, zone);
		} else {
			icalcomponent_free (iter->data);
		}
	}

	icaltimezone_free (zone, TRUE);
	g_slist_free (timezones);

	return TRUE;
}

static void
process_object (ECalBackendCalDAV *cbdav,
                ECalComponent *ecomp,
                gboolean online,
                icalproperty_method method,
                GCancellable *cancellable,
                GError **error)
{
	ESourceRegistry *registry;
	ECalBackend              *backend;
	struct icaltimetype       now;
	gchar *new_obj_str;
	gboolean is_declined, is_in_cache;
	ECalObjModType mod;
	ECalComponentId *id = e_cal_component_get_id (ecomp);
	GError *err = NULL;

	backend = E_CAL_BACKEND (cbdav);

	if (id == NULL) {
		g_set_error_literal (
			error, E_CAL_CLIENT_ERROR,
			E_CAL_CLIENT_ERROR_INVALID_OBJECT,
			e_cal_client_error_to_string (
			E_CAL_CLIENT_ERROR_INVALID_OBJECT));
		return;
	}

	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cbdav));

	/* ctime, mtime */
	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (ecomp, &now);
	e_cal_component_set_last_modified (ecomp, &now);

	/* just to check whether component exists in a cache */
	is_in_cache = cache_contains (cbdav, id->uid, NULL) || cache_contains (cbdav, id->uid, id->rid);

	new_obj_str = e_cal_component_get_as_string (ecomp);
	mod = e_cal_component_is_instance (ecomp) ? E_CAL_OBJ_MOD_THIS : E_CAL_OBJ_MOD_ALL;

	switch (method) {
	case ICAL_METHOD_PUBLISH:
	case ICAL_METHOD_REQUEST:
	case ICAL_METHOD_REPLY:
		is_declined = e_cal_backend_user_declined (
			registry, e_cal_component_get_icalcomponent (ecomp));
		if (is_in_cache) {
			if (!is_declined) {
				GSList *new_components = NULL, *old_components = NULL;
				GSList new_obj_strs = {0,};

				new_obj_strs.data = new_obj_str;
				do_modify_objects (cbdav, &new_obj_strs, mod,
						  &old_components, &new_components, cancellable, &err);
				if (!err && new_components && new_components->data) {
					if (!old_components || !old_components->data) {
						e_cal_backend_notify_component_created (backend, new_components->data);
					} else {
						e_cal_backend_notify_component_modified (backend, old_components->data, new_components->data);
					}
				}

				e_util_free_nullable_object_slist (old_components);
				e_util_free_nullable_object_slist (new_components);
			} else {
				GSList *new_components = NULL, *old_components = NULL;
				GSList ids = {0,};

				ids.data = id;
				do_remove_objects (cbdav, &ids, mod, &old_components, &new_components, cancellable, &err);
				if (!err && old_components && old_components->data) {
					if (new_components && new_components->data) {
						e_cal_backend_notify_component_modified (backend, old_components->data, new_components->data);
					} else {
						e_cal_backend_notify_component_removed (backend, id, old_components->data, NULL);
					}
				}

				e_util_free_nullable_object_slist (old_components);
				e_util_free_nullable_object_slist (new_components);
			}
		} else if (!is_declined) {
			GSList *new_components = NULL;
			GSList new_objs = {0,};

			new_objs.data = new_obj_str;

			do_create_objects (cbdav, &new_objs, NULL, &new_components, cancellable, &err);

			if (!err) {
				if (new_components && new_components->data)
					e_cal_backend_notify_component_created (backend, new_components->data);
			}

			e_util_free_nullable_object_slist (new_components);
		}
		break;
	case ICAL_METHOD_CANCEL:
		if (is_in_cache) {
			GSList *new_components = NULL, *old_components = NULL;
			GSList ids = {0,};

			ids.data = id;
			do_remove_objects (cbdav, &ids, E_CAL_OBJ_MOD_THIS, &old_components, &new_components, cancellable, &err);
			if (!err && old_components && old_components->data) {
				if (new_components && new_components->data) {
					e_cal_backend_notify_component_modified (backend, old_components->data, new_components->data);
				} else {
					e_cal_backend_notify_component_removed (backend, id, old_components->data, NULL);
				}
			}

			e_util_free_nullable_object_slist (old_components);
			e_util_free_nullable_object_slist (new_components);
		} else {
			err = EDC_ERROR (ObjectNotFound);
		}
		break;

	default:
		err = EDC_ERROR (UnsupportedMethod);
		break;
	}

	e_cal_component_free_id (id);
	g_free (new_obj_str);

	if (err)
		g_propagate_error (error, err);
}

static void
do_receive_objects (ECalBackendSync *backend,
                    EDataCal *cal,
                    GCancellable *cancellable,
                    const gchar *calobj,
                    GError **perror)
{
	ECalBackendCalDAV        *cbdav;
	icalcomponent            *icomp;
	icalcomponent_kind        kind;
	icalproperty_method       tmethod;
	gboolean                  online;
	GSList                   *objects, *iter;
	GError *err = NULL;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	if (!check_state (cbdav, &online, perror))
		return;

	icomp = icalparser_parse_string (calobj);

	/* Try to parse cal object string */
	if (icomp == NULL) {
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		return;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
	extract_objects (icomp, kind, &objects, &err);

	if (err) {
		icalcomponent_free (icomp);
		g_propagate_error (perror, err);
		return;
	}

	/* Extract optional timezone compnents */
	extract_timezones (cbdav, icomp);

	if (icalcomponent_get_first_property (icomp, ICAL_METHOD_PROPERTY))
		tmethod = icalcomponent_get_method (icomp);
	else
		tmethod = ICAL_METHOD_PUBLISH;

	for (iter = objects; iter && !err; iter = iter->next) {
		icalcomponent       *scomp;
		ECalComponent       *ecomp;
		icalproperty_method  method;

		scomp = (icalcomponent *) iter->data;
		ecomp = e_cal_component_new ();

		e_cal_component_set_icalcomponent (ecomp, scomp);

		if (icalcomponent_get_first_property (scomp, ICAL_METHOD_PROPERTY)) {
			method = icalcomponent_get_method (scomp);
		} else {
			method = tmethod;
		}

		process_object (cbdav, ecomp, online, method, cancellable, &err);
		g_object_unref (ecomp);
	}

	g_slist_free (objects);

	icalcomponent_free (icomp);

	if (err)
		g_propagate_error (perror, err);
}

#define caldav_busy_stub(_func_name, _params, _call_func, _call_params) \
static void \
_func_name _params \
{ \
	ECalBackendCalDAV        *cbdav; \
	SlaveCommand		  old_slave_cmd; \
	gboolean		  was_slave_busy; \
 \
	cbdav = E_CAL_BACKEND_CALDAV (backend); \
 \
	/* this is done before locking */ \
	old_slave_cmd = cbdav->priv->slave_cmd; \
	was_slave_busy = cbdav->priv->slave_busy; \
	if (was_slave_busy) { \
		/* let it pause its work and do our job */ \
		update_slave_cmd (cbdav->priv, SLAVE_SHOULD_SLEEP); \
	} \
 \
	g_mutex_lock (&cbdav->priv->busy_lock); \
	_call_func _call_params; \
 \
	/* this is done before unlocking */ \
	if (was_slave_busy) { \
		update_slave_cmd (cbdav->priv, old_slave_cmd); \
		g_cond_signal (&cbdav->priv->cond); \
	} \
 \
	g_mutex_unlock (&cbdav->priv->busy_lock); \
}

caldav_busy_stub (
        caldav_create_objects,
                  (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const GSList *in_calobjs,
                  GSList **uids,
                  GSList **new_components,
                  GError **perror),
        do_create_objects,
                  (cbdav,
                  in_calobjs,
                  uids,
                  new_components,
                  cancellable,
                  perror))

caldav_busy_stub (
        caldav_modify_objects,
                  (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const GSList *calobjs,
                  ECalObjModType mod,
                  GSList **old_components,
                  GSList **new_components,
                  GError **perror),
        do_modify_objects,
                  (cbdav,
                  calobjs,
                  mod,
                  old_components,
                  new_components,
                  cancellable,
                  perror))

caldav_busy_stub (
        caldav_remove_objects,
                  (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const GSList *ids,
                  ECalObjModType mod,
                  GSList **old_components,
                  GSList **new_components,
                  GError **perror),
        do_remove_objects,
                  (cbdav,
                  ids,
                  mod,
                  old_components,
                  new_components,
                  cancellable,
                  perror))

caldav_busy_stub (
        caldav_receive_objects,
                  (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const gchar *calobj,
                  GError **perror),
        do_receive_objects,
                  (backend,
                  cal,
                  cancellable,
                  calobj,
                  perror))

static void
caldav_send_objects (ECalBackendSync *backend,
                     EDataCal *cal,
                     GCancellable *cancellable,
                     const gchar *calobj,
                     GSList **users,
                     gchar **modified_calobj,
                     GError **perror)
{
	*users = NULL;
	*modified_calobj = g_strdup (calobj);
}

static gboolean
caldav_server_download_uid (ECalBackendCalDAV *cbdav,
			    const gchar *uid,
			    GCancellable *cancellable,
			    GError **error)
{
	CalDAVObject obj;
	GError *local_error = NULL;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	obj.href = g_strdup (uid);
	obj.etag = NULL;
	obj.status = 0;
	obj.cdata = NULL;

	if (!caldav_server_get_object (cbdav, &obj, cancellable, &local_error)) {
		if (g_error_matches (local_error, E_DATA_CAL_ERROR, ObjectNotFound)) {
			gchar *file;

			/* OK, the event was properly created, but cannot be found on the place
			 * where it was PUT - why didn't server tell us where it saved it? */
			g_clear_error (&local_error);

			/* try whether it's saved as its UID.ics file */
			file = caldav_gen_file_from_uid (cbdav, uid);
			if (file) {
				g_free (obj.href);
				obj.href = file;

				if (!caldav_server_get_object (cbdav, &obj, cancellable, &local_error)) {
				}
			}
		}
	}

	if (!local_error) {
		icalcomponent *use_comp = NULL;

		if (obj.cdata) {
			/* maybe server also modified component, thus rather store the server's */
			use_comp = icalparser_parse_string (obj.cdata);
			put_comp_to_cache (cbdav, use_comp, obj.href, obj.etag);
		}

		if (use_comp)
			icalcomponent_free (use_comp);
		else
			local_error = EDC_ERROR (ObjectNotFound);
	}

	if (local_error) {
		g_propagate_error (error, local_error);

		return FALSE;
	}

	g_free (obj.href);
	g_free (obj.etag);
	g_free (obj.cdata);

	return TRUE;
}

static void
caldav_get_object (ECalBackendSync *backend,
                   EDataCal *cal,
                   GCancellable *cancellable,
                   const gchar *uid,
                   const gchar *rid,
                   gchar **object,
                   GError **perror)
{
	ECalBackendCalDAV        *cbdav;
	icalcomponent            *icalcomp;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	*object = NULL;
	icalcomp = get_comp_from_cache (cbdav, uid, rid, NULL, NULL);

	if (!icalcomp && e_backend_get_online (E_BACKEND (backend))) {
		/* try to fetch from the server, maybe the event was received only recently */
		if (caldav_server_download_uid (cbdav, uid, cancellable, NULL)) {
			icalcomp = get_comp_from_cache (cbdav, uid, rid, NULL, NULL);
		}
	}

	if (!icalcomp) {
		g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
		return;
	}

	*object = icalcomponent_as_ical_string_r (icalcomp);
	icalcomponent_free (icalcomp);
}

static void
caldav_add_timezone (ECalBackendSync *backend,
                     EDataCal *cal,
                     GCancellable *cancellable,
                     const gchar *tzobj,
                     GError **error)
{
	ETimezoneCache *timezone_cache;
	icalcomponent *tz_comp;

	timezone_cache = E_TIMEZONE_CACHE (backend);

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);

		e_timezone_cache_add_timezone (timezone_cache, zone);

		icaltimezone_free (zone, TRUE);
	} else {
		icalcomponent_free (tz_comp);
	}
}

static void
caldav_get_object_list (ECalBackendSync *backend,
                        EDataCal *cal,
                        GCancellable *cancellable,
                        const gchar *sexp_string,
                        GSList **objects,
                        GError **perror)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendSExp	 *sexp;
	ETimezoneCache *cache;
	gboolean                  do_search;
	GSList			 *list, *iter;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	sexp = e_cal_backend_sexp_new (sexp_string);

	if (sexp == NULL) {
		g_propagate_error (perror, EDC_ERROR (InvalidQuery));
		return;
	}

	if (g_str_equal (sexp_string, "#t")) {
		do_search = FALSE;
	} else {
		do_search = TRUE;
	}

	*objects = NULL;

	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (sexp, &occur_start, &occur_end);

	cache = E_TIMEZONE_CACHE (backend);

	list = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (cbdav->priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (cbdav->priv->store);

	for (iter = list; iter; iter = g_slist_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search ||
		    e_cal_backend_sexp_match_comp (sexp, comp, cache)) {
			*objects = g_slist_prepend (*objects, e_cal_component_get_as_string (comp));
		}

		g_object_unref (comp);
	}

	g_object_unref (sexp);
	g_slist_free (list);
}

static void
caldav_start_view (ECalBackend *backend,
                   EDataCalView *query)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendSExp	 *sexp;
	ETimezoneCache *cache;
	gboolean                  do_search;
	GSList			 *list, *iter;
	const gchar               *sexp_string;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;
	cbdav = E_CAL_BACKEND_CALDAV (backend);

	sexp = e_data_cal_view_get_sexp (query);
	sexp_string = e_cal_backend_sexp_text (sexp);

	if (g_str_equal (sexp_string, "#t")) {
		do_search = FALSE;
	} else {
		do_search = TRUE;
	}
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (
		sexp,
		&occur_start,
		&occur_end);

	cache = E_TIMEZONE_CACHE (backend);

	list = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (cbdav->priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (cbdav->priv->store);

	for (iter = list; iter; iter = g_slist_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search ||
		    e_cal_backend_sexp_match_comp (sexp, comp, cache)) {
			e_data_cal_view_notify_components_added_1 (query, comp);
		}

		g_object_unref (comp);
	}

	g_slist_free (list);

	e_data_cal_view_notify_complete (query, NULL /* Success */);
}

static void
caldav_get_free_busy (ECalBackendSync *backend,
                      EDataCal *cal,
                      GCancellable *cancellable,
                      const GSList *users,
                      time_t start,
                      time_t end,
                      GSList **freebusy,
                      GError **error)
{
	ECalBackendCalDAV *cbdav;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	ECalComponentDateTime dt;
	ECalComponentOrganizer organizer = {NULL};
	ESourceAuthentication *auth_extension;
	ESource *source;
	struct icaltimetype dtvalue;
	icaltimezone *utc;
	gchar *str;
	const GSList *u;
	GSList *attendees = NULL, *to_free = NULL;
	const gchar *extension_name;
	gchar *usermail;
	GError *err = NULL;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	if (!cbdav->priv->calendar_schedule) {
		g_propagate_error (error, EDC_ERROR_EX (OtherError, _("Calendar doesn't support Free/Busy")));
		return;
	}

	if (!cbdav->priv->schedule_outbox_url) {
		caldav_receive_schedule_outbox_url (cbdav, cancellable, error);
		if (!cbdav->priv->schedule_outbox_url) {
			cbdav->priv->calendar_schedule = FALSE;
			if (error && !*error)
				g_propagate_error (error, EDC_ERROR_EX (OtherError, _("Schedule outbox url not found")));
			return;
		}
	}

	comp = e_cal_component_new ();
	e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_FREEBUSY);

	str = e_cal_component_gen_uid ();
	e_cal_component_set_uid (comp, str);
	g_free (str);

	utc = icaltimezone_get_utc_timezone ();
	dt.value = &dtvalue;
	dt.tzid = icaltimezone_get_tzid (utc);

	dtvalue = icaltime_current_time_with_zone (utc);
	e_cal_component_set_dtstamp (comp, &dtvalue);

	dtvalue = icaltime_from_timet_with_zone (start, FALSE, utc);
	e_cal_component_set_dtstart (comp, &dt);

	dtvalue = icaltime_from_timet_with_zone (end, FALSE, utc);
	e_cal_component_set_dtend (comp, &dt);

	usermail = get_usermail (E_CAL_BACKEND (backend));
	if (usermail != NULL && *usermail == '\0') {
		g_free (usermail);
		usermail = NULL;
	}

	source = e_backend_get_source (E_BACKEND (backend));
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	if (usermail == NULL)
		usermail = e_source_authentication_dup_user (auth_extension);

	organizer.value = g_strconcat ("mailto:", usermail, NULL);
	e_cal_component_set_organizer (comp, &organizer);
	g_free ((gchar *) organizer.value);

	g_free (usermail);

	for (u = users; u; u = u->next) {
		ECalComponentAttendee *ca;
		gchar *temp = g_strconcat ("mailto:", (const gchar *) u->data, NULL);

		ca = g_new0 (ECalComponentAttendee, 1);

		ca->value = temp;
		ca->cutype = ICAL_CUTYPE_INDIVIDUAL;
		ca->status = ICAL_PARTSTAT_NEEDSACTION;
		ca->role = ICAL_ROLE_CHAIR;

		to_free = g_slist_prepend (to_free, temp);
		attendees = g_slist_append (attendees, ca);
	}

	e_cal_component_set_attendee_list (comp, attendees);

	g_slist_foreach (attendees, (GFunc) g_free, NULL);
	g_slist_free (attendees);

	g_slist_foreach (to_free, (GFunc) g_free, NULL);
	g_slist_free (to_free);

	e_cal_component_abort_sequence (comp);

	/* put the free/busy request to a VCALENDAR */
	icalcomp = e_cal_util_new_top_level ();
	icalcomponent_set_method (icalcomp, ICAL_METHOD_REQUEST);
	icalcomponent_add_component (icalcomp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));

	str = icalcomponent_as_ical_string_r (icalcomp);

	icalcomponent_free (icalcomp);
	g_object_unref (comp);

	caldav_post_freebusy (cbdav, cbdav->priv->schedule_outbox_url, &str, cancellable, &err);

	if (!err) {
		/* parse returned xml */
		xmlDocPtr doc;

		doc = xmlReadMemory (str, strlen (str), "response.xml", NULL, 0);
		if (doc != NULL) {
			xmlXPathContextPtr xpctx;
			xmlXPathObjectPtr result;

			xpctx = xmlXPathNewContext (doc);
			xmlXPathRegisterNs (xpctx, (xmlChar *) "D", (xmlChar *) "DAV:");
			xmlXPathRegisterNs (xpctx, (xmlChar *) "C", (xmlChar *) "urn:ietf:params:xml:ns:caldav");

			result = xpath_eval (xpctx, "/C:schedule-response/C:response");

			if (result == NULL || result->type != XPATH_NODESET) {
				err = EDC_ERROR_EX (OtherError, _("Unexpected result in schedule-response"));
			} else {
				gint i, n;

				n = xmlXPathNodeSetGetLength (result->nodesetval);
				for (i = 0; i < n; i++) {
					gchar *tmp;

					tmp = xp_object_get_string (xpath_eval (xpctx, "string(/C:schedule-response/C:response[%d]/C:calendar-data)", i + 1));
					if (tmp && *tmp) {
						GSList *objects = NULL, *o;

						icalcomp = icalparser_parse_string (tmp);
						if (icalcomp)
							extract_objects (icalcomp, ICAL_VFREEBUSY_COMPONENT, &objects, &err);
						if (icalcomp && !err) {
							for (o = objects; o; o = o->next) {
								gchar *obj_str = icalcomponent_as_ical_string_r (o->data);

								if (obj_str && *obj_str)
									*freebusy = g_slist_append (*freebusy, obj_str);
								else
									g_free (obj_str);
							}
						}

						g_slist_foreach (objects, (GFunc) icalcomponent_free, NULL);
						g_slist_free (objects);

						if (icalcomp)
							icalcomponent_free (icalcomp);
						if (err)
							g_error_free (err);
						err = NULL;
					}

					g_free (tmp);
				}
			}

			if (result != NULL)
				xmlXPathFreeObject (result);
			xmlXPathFreeContext (xpctx);
			xmlFreeDoc (doc);
		}
	}

	g_free (str);

	if (err)
		g_propagate_error (error, err);
}

static void
caldav_notify_online_cb (ECalBackend *backend,
                         GParamSpec *pspec)
{
	ECalBackendCalDAV        *cbdav;
	gboolean online;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	/*g_mutex_lock (&cbdav->priv->busy_lock);*/

	online = e_backend_get_online (E_BACKEND (backend));

	if (!cbdav->priv->loaded) {
		/*g_mutex_unlock (&cbdav->priv->busy_lock);*/
		return;
	}

	if (online) {
		/* Wake up the slave thread */
		update_slave_cmd (cbdav->priv, SLAVE_SHOULD_WORK);
		g_cond_signal (&cbdav->priv->cond);
	} else {
		soup_session_abort (cbdav->priv->session);
		update_slave_cmd (cbdav->priv, SLAVE_SHOULD_SLEEP);
	}

	/*g_mutex_unlock (&cbdav->priv->busy_lock);*/
}

static gpointer
caldav_source_changed_thread (gpointer data)
{
	ECalBackendCalDAV *cbdav = data;
	SlaveCommand old_slave_cmd;
	gboolean old_slave_busy;

	g_return_val_if_fail (cbdav != NULL, NULL);

	old_slave_cmd = cbdav->priv->slave_cmd;
	old_slave_busy = cbdav->priv->slave_busy;
	if (old_slave_busy)
		update_slave_cmd (cbdav->priv, SLAVE_SHOULD_SLEEP);

	g_mutex_lock (&cbdav->priv->busy_lock);

	/* guard the call with busy_lock, thus the two threads (this 'source changed'
	 * thread and the 'backend open' thread) will not clash on internal data
	 * when they are called in once */
	initialize_backend (cbdav, NULL);

	/* always wakeup thread, even when it was sleeping */
	g_cond_signal (&cbdav->priv->cond);

	if (old_slave_busy)
		update_slave_cmd (cbdav->priv, old_slave_cmd);

	g_mutex_unlock (&cbdav->priv->busy_lock);

	cbdav->priv->updating_source = FALSE;

	g_object_unref (cbdav);

	return NULL;
}

static void
caldav_source_changed_cb (ESource *source,
                          ECalBackendCalDAV *cbdav)
{
	GThread *thread;

	g_return_if_fail (source != NULL);
	g_return_if_fail (cbdav != NULL);

	if (cbdav->priv->updating_source ||
	    !cbdav->priv->loaded ||
	    !e_cal_backend_is_opened (E_CAL_BACKEND (cbdav)))
		return;

	cbdav->priv->updating_source = TRUE;

	thread = g_thread_new (NULL, caldav_source_changed_thread, g_object_ref (cbdav));
	g_thread_unref (thread);
}

static ESourceAuthenticationResult
caldav_authenticate_sync (EBackend *backend,
			  const ENamedParameters *credentials,
			  gchar **out_certificate_pem,
			  GTlsCertificateFlags *out_certificate_errors,
			  GCancellable *cancellable,
			  GError **error)
{
	ECalBackendCalDAV *cbdav;
	ESourceAuthenticationResult result;
	GError *local_error = NULL;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	g_mutex_lock (&cbdav->priv->busy_lock);

	e_named_parameters_free (cbdav->priv->credentials);
	cbdav->priv->credentials = e_named_parameters_new_clone (credentials);

	open_calendar_wrapper (cbdav, cancellable, &local_error, FALSE, NULL, out_certificate_pem, out_certificate_errors);

	if (local_error == NULL) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;

		update_slave_cmd (cbdav->priv, SLAVE_SHOULD_WORK);
		g_cond_signal (&cbdav->priv->cond);
	} else if (g_error_matches (local_error, E_DATA_CAL_ERROR, AuthenticationFailed) ||
		   g_error_matches (local_error, E_DATA_CAL_ERROR, AuthenticationRequired)) {
		const gchar *username;
		gchar *auth_user = NULL;

		username = e_named_parameters_get (cbdav->priv->credentials, E_SOURCE_CREDENTIAL_USERNAME);

		if (!username || !*username) {
			ESource *source;
			ESourceAuthentication *auth_extension;

			source = e_backend_get_source (backend);
			auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
			auth_user = e_source_authentication_dup_user (auth_extension);

			username = auth_user;
		}

		if (username && *username) {
			if (!cbdav->priv->using_bearer_auth &&
			    !e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD))
				result = E_SOURCE_AUTHENTICATION_REQUIRED;
			else
				result = E_SOURCE_AUTHENTICATION_REJECTED;
			g_clear_error (&local_error);
		} else {
			result = E_SOURCE_AUTHENTICATION_ERROR;
			g_propagate_error (error, local_error);
		}

		g_free (auth_user);
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
		result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;
		g_propagate_error (error, local_error);
	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
		g_propagate_error (error, local_error);
	}

	g_mutex_unlock (&cbdav->priv->busy_lock);

	return result;
}

/* ************************************************************************* */
/* ***************************** GObject Foo ******************************* */

static void
e_cal_backend_caldav_dispose (GObject *object)
{
	ECalBackendCalDAVPrivate *priv;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (object);

	g_clear_object (&priv->store);
	g_clear_object (&priv->session);
	g_clear_object (&priv->using_bearer_auth);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_cal_backend_caldav_finalize (GObject *object)
{
	ECalBackendCalDAVPrivate *priv;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (object);

	g_mutex_clear (&priv->busy_lock);
	g_cond_clear (&priv->cond);
	g_cond_clear (&priv->slave_gone_cond);

	g_free (priv->uri);
	e_named_parameters_free (priv->credentials);
	priv->credentials = NULL;
	g_free (priv->schedule_outbox_url);

	if (priv->ctag_to_store) {
		g_free (priv->ctag_to_store);
		priv->ctag_to_store = NULL;
	}

	g_clear_error (&priv->bearer_auth_error);
	g_mutex_clear (&priv->bearer_auth_error_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
caldav_backend_initable_init (GInitable *initable,
                              GCancellable *cancellable,
                              GError **error)
{
	ECalBackendCalDAVPrivate *priv;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (initable);

	g_mutex_init (&priv->bearer_auth_error_lock);

	return TRUE;
}

static void
e_cal_backend_caldav_init (ECalBackendCalDAV *cbdav)
{
	cbdav->priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	cbdav->priv->session = soup_session_sync_new ();
	g_object_set (
		cbdav->priv->session,
		SOUP_SESSION_TIMEOUT, 90,
		SOUP_SESSION_SSL_STRICT, TRUE,
		SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
		SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
		NULL);

	e_binding_bind_property (
		cbdav, "proxy-resolver",
		cbdav->priv->session, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	if (G_UNLIKELY (caldav_debug_show (DEBUG_MESSAGE)))
		caldav_debug_setup (cbdav->priv->session);

	cbdav->priv->loaded = FALSE;
	cbdav->priv->opened = FALSE;

	/* Thinks the 'getctag' extension is available the first time, but unset it when realizes it isn't. */
	cbdav->priv->ctag_supported = TRUE;
	cbdav->priv->ctag_to_store = NULL;

	cbdav->priv->schedule_outbox_url = NULL;

	cbdav->priv->is_google = FALSE;

	g_mutex_init (&cbdav->priv->busy_lock);
	g_cond_init (&cbdav->priv->cond);
	g_cond_init (&cbdav->priv->slave_gone_cond);

	/* Slave control ... */
	cbdav->priv->slave_cmd = SLAVE_SHOULD_SLEEP;
	cbdav->priv->slave_busy = FALSE;

	g_signal_connect (
		cbdav->priv->session, "authenticate",
		G_CALLBACK (soup_authenticate), cbdav);

	g_signal_connect (
		cbdav, "notify::online",
		G_CALLBACK (caldav_notify_online_cb), NULL);
}

static void
e_cal_backend_caldav_class_init (ECalBackendCalDAVClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	ECalBackendClass *cal_backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = G_OBJECT_CLASS (class);
	backend_class = E_BACKEND_CLASS (class);
	cal_backend_class = E_CAL_BACKEND_CLASS (class);
	sync_class = E_CAL_BACKEND_SYNC_CLASS (class);

	caldav_debug_init ();

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (ECalBackendCalDAVPrivate));

	object_class->dispose = e_cal_backend_caldav_dispose;
	object_class->finalize = e_cal_backend_caldav_finalize;

	backend_class->authenticate_sync = caldav_authenticate_sync;

	cal_backend_class->get_backend_property = caldav_get_backend_property;
	cal_backend_class->shutdown = caldav_shutdown;
	cal_backend_class->start_view = caldav_start_view;

	sync_class->open_sync = caldav_do_open;
	sync_class->refresh_sync = caldav_refresh;

	sync_class->create_objects_sync = caldav_create_objects;
	sync_class->modify_objects_sync = caldav_modify_objects;
	sync_class->remove_objects_sync = caldav_remove_objects;

	sync_class->receive_objects_sync = caldav_receive_objects;
	sync_class->send_objects_sync = caldav_send_objects;
	sync_class->get_object_sync = caldav_get_object;
	sync_class->get_object_list_sync = caldav_get_object_list;
	sync_class->add_timezone_sync = caldav_add_timezone;
	sync_class->get_free_busy_sync = caldav_get_free_busy;
}

static void
e_caldav_backend_initable_init (GInitableIface *interface)
{
	interface->init = caldav_backend_initable_init;
}

