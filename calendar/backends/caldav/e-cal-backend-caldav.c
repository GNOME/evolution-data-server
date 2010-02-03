/*
 * Evolution calendar - caldav backend
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Christian Kellner <gicmo@gnome.org>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <gconf/gconf-client.h>
#include <glib/gstdio.h>
#include <glib/gi18n-lib.h>
#include "libedataserver/e-xml-hash-utils.h"
#include "libedataserver/e-proxy.h"
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-time-util.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-file-store.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>

/* LibXML2 includes */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

/* LibSoup includes */
#include <libsoup/soup.h>

#include "e-cal-backend-caldav.h"

#define d(x)

#define CALDAV_CTAG_KEY "CALDAV_CTAG"
#define CALDAV_MAX_MULTIGET_AMOUNT 100 /* what's the maximum count of items to fetch within a multiget request */
#define LOCAL_PREFIX "file://"

/* in seconds */
#define DEFAULT_REFRESH_TIME 60

typedef enum {

	SLAVE_SHOULD_SLEEP,
	SLAVE_SHOULD_WORK,
	SLAVE_SHOULD_DIE

} SlaveCommand;

/* Private part of the ECalBackendHttp structure */
struct _ECalBackendCalDAVPrivate {

	/* online/offline */
	CalMode mode;

	/* The local disk cache */
	ECalBackendStore *store;

	/* local attachments store */
	gchar *local_attachments_store;

	/* should we sync for offline mode? */
	gboolean do_offline;

	/* TRUE after caldav_open */
	gboolean loaded;

	/* lock to indicate a busy state */
	GMutex *busy_lock;

	/* cond to synch threads */
	GCond *cond;

	/* cond to know the slave gone */
	GCond *slave_gone_cond;

	/* BG synch thread */
	const GThread *synch_slave; /* just for a reference, whether thread exists */
	SlaveCommand slave_cmd;
	gboolean slave_busy; /* whether is slave working */
	GTimeVal refresh_time;

	/* The main soup session  */
	SoupSession *session;
	EProxy *proxy;

	/* well, guess what */
	gboolean read_only;

	/* clandar uri */
	gchar *uri;

	/* Authentication info */
	gchar *username;
	gchar *password;
	gboolean need_auth;

	/* object cleanup */
	gboolean disposed;

	icaltimezone *default_zone;

	/* support for 'getctag' extension */
	gboolean ctag_supported;
	gchar *ctag_to_store;

	/* TRUE when 'calendar-schedule' supported on the server */
	gboolean calendar_schedule;
	/* with 'calendar-schedule' supported, here's an outbox url
	   for queries of free/busy information */
	gchar *schedule_outbox_url;

	/* "Temporary hack" to indicate it's talking to a google calendar.
	   The proper solution should be to subclass whole backend and change only
	   necessary parts in it, but this will give us more freedom, as also direct
	   caldav calendars can profit from this. */
	gboolean is_google;
};

/* ************************************************************************* */
/* Debugging */

#define DEBUG_MESSAGE "message"
#define DEBUG_MESSAGE_HEADER "message:header"
#define DEBUG_MESSAGE_BODY "message:body"
#define DEBUG_SERVER_ITEMS "items"

static void convert_to_inline_attachment (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp);
static void convert_to_url_attachment (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp);
static void remove_cached_attachment (ECalBackendCalDAV *cbdav, const gchar *uid);

static gboolean caldav_debug_all = FALSE;
static GHashTable *caldav_debug_table = NULL;

static void
add_debug_key (const gchar *start, const gchar *end)
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

	g_hash_table_insert (caldav_debug_table,
			     debug_key,
			     debug_value);

	d(g_debug ("Adding %s to enabled debugging keys", debug_key));
}

static gpointer
caldav_debug_init_once (gpointer data)
{
	const gchar *dbg;

	dbg = g_getenv ("CALDAV_DEBUG");

	if (dbg) {
		const gchar *ptr;

		d(g_debug ("Got debug env variable: [%s]", dbg));

		caldav_debug_table = g_hash_table_new (g_str_hash,
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

	g_once (&debug_once,
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

static ECalBackendSyncClass *parent_class = NULL;

static icaltimezone *caldav_internal_get_default_timezone (ECalBackend *backend);
static icaltimezone *caldav_internal_get_timezone (ECalBackend *backend, const gchar *tzid);
static void caldav_source_changed_cb (ESource *source, ECalBackendCalDAV *cbdav);

static gboolean remove_comp_from_cache (ECalBackendCalDAV *cbdav, const gchar *uid, const gchar *rid);
static gboolean put_comp_to_cache (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp, const gchar *href, const gchar *etag);

/* ************************************************************************* */
/* Misc. utility functions */
#define X_E_CALDAV "X-EVOLUTION-CALDAV-"
#define X_E_CALDAV_ATTACHMENT_NAME X_E_CALDAV "ATTACHMENT-NAME"

static void
icomp_x_prop_set (icalcomponent *comp, const gchar *key, const gchar *value)
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
icomp_x_prop_get (icalcomponent *comp, const gchar *key)
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
ecalcomp_set_href (ECalComponent *comp, const gchar *href)
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

	str =  icomp_x_prop_get (icomp, X_E_CALDAV "HREF");

	return str;
}

/* passing NULL as 'etag' removes the property */
static void
ecalcomp_set_etag (ECalComponent *comp, const gchar *etag)
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

	str =  icomp_x_prop_get (icomp, X_E_CALDAV "ETAG");

	return str;
}

/*typedef enum {

	/ * object is in synch,
	 * now isnt that ironic? :) * /
	ECALCOMP_IN_SYNCH = 0,

	/ * local changes * /
	ECALCOMP_LOCALLY_CREATED,
	ECALCOMP_LOCALLY_DELETED,
	ECALCOMP_LOCALLY_MODIFIED

} ECalCompSyncState;

/ * oos = out of synch * /
static void
ecalcomp_set_synch_state (ECalComponent *comp, ECalCompSyncState state)
{
	icalcomponent *icomp;
	gchar          *state_string;

	icomp = e_cal_component_get_icalcomponent (comp);

	state_string = g_strdup_printf ("%d", state);

	icomp_x_prop_set (icomp, X_E_CALDAV "ETAG", state_string);

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

static ECalBackendSyncStatus
status_code_to_result (guint status_code, ECalBackendCalDAVPrivate  *priv)
{
	ECalBackendSyncStatus result;

	if (SOUP_STATUS_IS_SUCCESSFUL (status_code)) {
		return GNOME_Evolution_Calendar_Success;
	}

	switch (status_code) {

	case 404:
		result = GNOME_Evolution_Calendar_NoSuchCal;
		break;

	case 403:
		result = GNOME_Evolution_Calendar_AuthenticationFailed;
		break;

	case 401:
		if (priv && priv->need_auth)
			result = GNOME_Evolution_Calendar_AuthenticationFailed;
		else
			result = GNOME_Evolution_Calendar_AuthenticationRequired;
		break;

	default:
		d(g_debug ("CalDAV:%s: Unhandled status code %d\n", G_STRFUNC, status_code));
		result = GNOME_Evolution_Calendar_OtherError;
	}

	return result;
}

/* !TS, call with lock held */
static ECalBackendSyncStatus
check_state (ECalBackendCalDAV *cbdav, gboolean *online)
{
	ECalBackendCalDAVPrivate *priv;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	*online = FALSE;

	if (!priv->loaded) {
		return GNOME_Evolution_Calendar_OtherError;
	}

	if (priv->mode == CAL_MODE_LOCAL) {

		if (!priv->do_offline) {
			return GNOME_Evolution_Calendar_RepositoryOffline;
		}

	} else {
		*online = TRUE;
	}

	return	GNOME_Evolution_Calendar_Success;
}

/* ************************************************************************* */
/* XML Parsing code */

static xmlXPathObjectPtr
xpath_eval (xmlXPathContextPtr ctx, const gchar *format, ...)
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
parse_status_node (xmlNodePtr node, guint *status_code)
{
	xmlChar  *content;
	gboolean  res;

	content = xmlNodeGetContent (node);

	res = soup_headers_parse_status_line ((gchar *) content,
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
		res = soup_headers_parse_status_line ((gchar *) result->stringval,
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
caldav_object_free (CalDAVObject *object, gboolean free_object_itself)
{
	g_free (object->href);
	g_free (object->etag);
	g_free (object->cdata);

	if (free_object_itself) {
		g_free (object);
	}
}

static gboolean
parse_report_response (SoupMessage *soup_message, CalDAVObject **objs, gint *len)
{
	xmlXPathContextPtr xpctx;
	xmlXPathObjectPtr  result;
	xmlDocPtr          doc;
	gint                i, n;
	gboolean           res;

	g_return_val_if_fail (soup_message != NULL, FALSE);
	g_return_val_if_fail (objs != NULL || len != NULL, FALSE);

	res = TRUE;
	doc = xmlReadMemory (soup_message->response_body->data,
			     soup_message->response_body->length,
			     "response.xml",
			     NULL,
			     0);

	if (doc == NULL) {
		return FALSE;
	}

	xpctx = xmlXPathNewContext (doc);

	xmlXPathRegisterNs (xpctx, (xmlChar *) "D",
			    (xmlChar *) "DAV:");

	xmlXPathRegisterNs (xpctx, (xmlChar *) "C",
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
parse_propfind_response (SoupMessage *message, const gchar *xpath_status, const gchar *xpath_value, gchar **value)
{
	xmlXPathContextPtr xpctx;
	xmlDocPtr          doc;
	gboolean           res = FALSE;

	g_return_val_if_fail (message != NULL, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	doc = xmlReadMemory (message->response_body->data,
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

			if (*txt == '\"' && len > 2 && txt [len - 1] == '\"') {
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
soup_authenticate (SoupSession  *session,
		   SoupMessage  *msg,
		   SoupAuth     *auth,
		   gboolean      retrying,
		   gpointer      data)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendCalDAV        *cbdav;

	cbdav = E_CAL_BACKEND_CALDAV (data);
	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	/* do not send same password twice, but keep it for later use */
	if (!retrying)
		soup_auth_authenticate (auth, priv->username, priv->password);
}

/* ************************************************************************* */
/* direct CalDAV server access functions */

static void
redirect_handler (SoupMessage *msg, gpointer user_data)
{
	if (SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
		SoupSession *soup_session = user_data;
		SoupURI *new_uri;
		const gchar *new_loc;

		new_loc = soup_message_headers_get (msg->response_headers, "Location");
		if (!new_loc)
			return;

		new_uri = soup_uri_new_with_base (soup_message_get_uri (msg), new_loc);
		if (!new_uri) {
			soup_message_set_status_full (msg,
						      SOUP_STATUS_MALFORMED,
						      "Invalid Redirect URL");
			return;
		}

		soup_message_set_uri (msg, new_uri);
		soup_session_requeue_message (soup_session, msg);

		soup_uri_free (new_uri);
	}
}

static void
send_and_handle_redirection (SoupSession *soup_session, SoupMessage *msg, gchar **new_location)
{
	gchar *old_uri = NULL;

	if (new_location)
		old_uri = soup_uri_to_string (soup_message_get_uri (msg), FALSE);

	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	soup_message_add_header_handler (msg, "got_body", "Location", G_CALLBACK (redirect_handler), soup_session);
	soup_session_send_message (soup_session, msg);

	if (new_location) {
		gchar *new_loc = soup_uri_to_string (soup_message_get_uri (msg), FALSE);

		if (new_loc && old_uri && !g_str_equal (new_loc, old_uri))
			*new_location = new_loc;
		else
			g_free (new_loc);
	}

	g_free (old_uri);
}

static gchar *
caldav_generate_uri (ECalBackendCalDAV *cbdav, const gchar *target)
{
	ECalBackendCalDAVPrivate  *priv;
	gchar *uri;
	const gchar *slash;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	slash = strrchr (target, '/');
	if (slash)
		target = slash + 1;

	/* priv->uri *have* trailing slash already */
	uri = g_strconcat (priv->uri, target, NULL);

	return uri;
}

static ECalBackendSyncStatus
caldav_server_open_calendar (ECalBackendCalDAV *cbdav)
{
	ECalBackendCalDAVPrivate  *priv;
	SoupMessage               *message;
	const gchar                *header;
	gboolean                   calendar_access;
	gboolean                   put_allowed;
	gboolean                   delete_allowed;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	/* FIXME: setup text_uri */

	message = soup_message_new (SOUP_METHOD_OPTIONS, priv->uri);
	if (message == NULL)
		return GNOME_Evolution_Calendar_NoSuchCal;
	soup_message_headers_append (message->request_headers,
				     "User-Agent", "Evolution/" VERSION);

	send_and_handle_redirection (priv->session, message, NULL);

	if (!SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		guint status_code = message->status_code;

		g_object_unref (message);

		return status_code_to_result (status_code, priv);
	}

	/* parse the dav header, we are intreseted in the
	 * calendar-access bit only at the moment */
	header = soup_message_headers_get (message->response_headers, "DAV");
	if (header) {
		calendar_access = soup_header_contains (header, "calendar-access");
		priv->calendar_schedule = soup_header_contains (header, "calendar-schedule");
	} else {
		calendar_access = FALSE;
		priv->calendar_schedule = FALSE;
	}

	/* parse the Allow header and look for PUT, DELETE at the
	 * moment (maybe we should check more here, for REPORT eg) */
	header = soup_message_headers_get (message->response_headers, "Allow");
	if (header) {
		put_allowed = soup_header_contains (header, "PUT");
		delete_allowed = soup_header_contains (header, "DELETE");
	} else
		put_allowed = delete_allowed = FALSE;

	g_object_unref (message);

	if (calendar_access) {
		priv->read_only = !(put_allowed && delete_allowed);
		return GNOME_Evolution_Calendar_Success;
	}

	return GNOME_Evolution_Calendar_NoSuchCal;
}

/* Returns whether calendar changed on the server. This works only when server
   supports 'getctag' extension. */
static gboolean
check_calendar_changed_on_server (ECalBackendCalDAV *cbdav)
{
	ECalBackendCalDAVPrivate *priv;
	xmlOutputBufferPtr	  buf;
	SoupMessage              *message;
	xmlDocPtr		  doc;
	xmlNodePtr		  root, node;
	xmlNsPtr		  ns, nsdav;
	gboolean		  result = TRUE;

	g_return_val_if_fail (cbdav != NULL, TRUE);

	priv   = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	/* no support for 'getctag', thus update cache */
	if (!priv->ctag_supported)
		return TRUE;

	/* Prepare the soup message */
	message = soup_message_new ("PROPFIND", priv->uri);
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

	soup_message_headers_append (message->request_headers,
				     "User-Agent", "Evolution/" VERSION);
	soup_message_headers_append (message->request_headers,
				     "Depth", "0");

	soup_message_set_request (message,
				  "application/xml",
				  SOUP_MEMORY_COPY,
				  (gchar *) buf->buffer->content,
				  buf->buffer->use);

	/* Send the request now */
	send_and_handle_redirection (priv->session, message, NULL);

	/* Clean up the memory */
	xmlOutputBufferClose (buf);
	xmlFreeDoc (doc);

	/* Check the result */
	if (message->status_code != 207) {
		/* does not support it, but report calendar changed to update cache */
		priv->ctag_supported = FALSE;
	} else {
		gchar *ctag = NULL;

		if (parse_propfind_response (message, XPATH_GETCTAG_STATUS, XPATH_GETCTAG, &ctag)) {
			const gchar *my_ctag;

			my_ctag = e_cal_backend_store_get_key_value (priv->store, CALDAV_CTAG_KEY);

			if (ctag && my_ctag && g_str_equal (ctag, my_ctag)) {
				/* ctag is same, no change in the calendar */
				result = FALSE;
			} else {
				/* do not store ctag now, do it rather after complete sync */
				g_free (priv->ctag_to_store);
				priv->ctag_to_store = ctag;
				ctag = NULL;
			}

			g_free (ctag);
		} else {
			priv->ctag_supported = FALSE;
		}
	}

	g_object_unref (message);

	return result;
}

/* only_hrefs is a list of requested objects to fetch; it has precedence from
   start_time/end_time, which are used only when both positive.
   Times are supposed to be in UTC, if set.
*/
static gboolean
caldav_server_list_objects (ECalBackendCalDAV *cbdav, CalDAVObject **objs, gint *len, GSList *only_hrefs, time_t start_time, time_t end_time)
{
	ECalBackendCalDAVPrivate *priv;
	xmlOutputBufferPtr   buf;
	SoupMessage         *message;
	xmlNodePtr           node;
	xmlNodePtr           sn;
	xmlNodePtr           root;
	xmlDocPtr            doc;
	xmlNsPtr             nsdav;
	xmlNsPtr             nscd;
	gboolean             result;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	/* Allocate the soup message */
	message = soup_message_new ("REPORT", priv->uri);
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
	soup_message_headers_append (message->request_headers,
				     "User-Agent", "Evolution/" VERSION);
	soup_message_headers_append (message->request_headers,
				     "Depth", "1");

	soup_message_set_request (message,
				  "application/xml",
				  SOUP_MEMORY_COPY,
				  (gchar *) buf->buffer->content,
				  buf->buffer->use);

	/* Send the request now */
	send_and_handle_redirection (priv->session, message, NULL);

	/* Clean up the memory */
	xmlOutputBufferClose (buf);
	xmlFreeDoc (doc);

	/* Check the result */
	if (message->status_code != 207) {
		g_warning ("Server did not response with 207, but with code %d (%s)", message->status_code, soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : "Unknown code");

		g_object_unref (message);
		return FALSE;
	}

	/* Parse the response body */
	result = parse_report_response (message, objs, len);

	g_object_unref (message);
	return result;
}

static ECalBackendSyncStatus
caldav_server_get_object (ECalBackendCalDAV *cbdav, CalDAVObject *object)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     result;
	SoupMessage              *message;
	const gchar               *hdr;
	gchar                     *uri;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	result = GNOME_Evolution_Calendar_Success;

	g_assert (object != NULL && object->href != NULL);

	uri = caldav_generate_uri (cbdav, object->href);
	message = soup_message_new (SOUP_METHOD_GET, uri);
	if (message == NULL) {
		g_free (uri);
		return GNOME_Evolution_Calendar_NoSuchCal;
	}

	soup_message_headers_append (message->request_headers,
				     "User-Agent", "Evolution/" VERSION);

	send_and_handle_redirection (priv->session, message, NULL);

	if (!SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		guint status_code = message->status_code;
		g_object_unref (message);

		g_warning ("Could not fetch object '%s' from server, status:%d (%s)", uri, status_code, soup_status_get_phrase (status_code) ? soup_status_get_phrase (status_code) : "Unknown code");
		g_free (uri);
		return status_code_to_result (status_code, priv);
	}

	hdr = soup_message_headers_get (message->response_headers, "Content-Type");

	if (hdr == NULL || g_ascii_strncasecmp (hdr, "text/calendar", 13)) {
		result = GNOME_Evolution_Calendar_InvalidObject;
		g_object_unref (message);
		g_warning ("Object to fetch '%s' not of type text/calendar", uri);
		g_free (uri);
		return result;
	}

	hdr = soup_message_headers_get (message->response_headers, "ETag");

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

	return result;
}

static ECalBackendSyncStatus
caldav_post_freebusy (ECalBackendCalDAV *cbdav, const gchar *url, gchar **post_fb)
{
	ECalBackendCalDAVPrivate *priv;
	SoupMessage *message;

	g_return_val_if_fail (cbdav != NULL, GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (url != NULL, GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (post_fb != NULL, GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (*post_fb != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	message = soup_message_new (SOUP_METHOD_POST, url);
	if (message == NULL) {
		return GNOME_Evolution_Calendar_NoSuchCal;
	}

	soup_message_headers_append (message->request_headers, "User-Agent", "Evolution/" VERSION);
	soup_message_set_request (message,
				  "text/calendar; charset=utf-8",
				  SOUP_MEMORY_COPY,
				  *post_fb, strlen (*post_fb));

	send_and_handle_redirection (priv->session, message, NULL);

	if (!SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		guint status_code = message->status_code;
		g_object_unref (message);

		g_warning ("Could not post free/busy request to '%s', status:%d (%s)", url, status_code, soup_status_get_phrase (status_code) ? soup_status_get_phrase (status_code) : "Unknown code");
		return status_code_to_result (status_code, priv);
	}

	g_free (*post_fb);
	*post_fb = g_strdup (message->response_body->data);

	g_object_unref (message);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_server_put_object (ECalBackendCalDAV *cbdav, CalDAVObject *object, icalcomponent *icalcomp)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     result;
	SoupMessage              *message;
	const gchar               *hdr;
	gchar                     *uri;

	priv   = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	result = GNOME_Evolution_Calendar_Success;
	hdr    = NULL;

	g_assert (object != NULL && object->cdata != NULL);

	uri = caldav_generate_uri (cbdav, object->href);
	message = soup_message_new (SOUP_METHOD_PUT, uri);
	g_free (uri);
	if (message == NULL)
		return GNOME_Evolution_Calendar_NoSuchCal;

	soup_message_headers_append (message->request_headers,
				     "User-Agent", "Evolution/" VERSION);

	/* For new items we use the If-None-Match so we don't
	 * acidently override resources, for item updates we
	 * use the If-Match header to avoid the Lost-update
	 * problem */
	if (object->etag == NULL) {
		soup_message_headers_append (message->request_headers, "If-None-Match", "*");
	} else {
		soup_message_headers_append (message->request_headers,
					     "If-Match", object->etag);
	}

	soup_message_set_request (message,
				  "text/calendar; charset=utf-8",
				  SOUP_MEMORY_COPY,
				  object->cdata,
				  strlen (object->cdata));

	uri = NULL;
	send_and_handle_redirection (priv->session, message, &uri);

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

	result = status_code_to_result (message->status_code, priv);

	if (result == GNOME_Evolution_Calendar_Success) {
		gboolean was_get = FALSE;

		hdr = soup_message_headers_get (message->response_headers, "ETag");
		if (hdr != NULL) {
			g_free (object->etag);
			object->etag = quote_etag (hdr);
		} else {
			/* no ETag header returned, check for it with a GET */
			hdr = soup_message_headers_get (message->response_headers, "Location");
			if (hdr) {
				/* reflect possible href change first */
				gchar *file = strrchr (hdr, '/');

				if (file) {
					gchar *decoded;

					g_free (object->href);

					decoded = soup_uri_decode (file + 1);
					object->href = soup_uri_encode (decoded ? decoded : (file + 1), NULL);

					g_free (decoded);
				}
			}
		}

		result = caldav_server_get_object (cbdav, object);
		was_get = TRUE;

		if (result == GNOME_Evolution_Calendar_Success) {
			icalcomponent *use_comp = NULL;

			if (object->cdata && was_get) {
				/* maybe server also modified component, thus rather store the server's */
				use_comp = icalparser_parse_string (object->cdata);
			}

			if (!use_comp)
				use_comp = icalcomp;

			put_comp_to_cache (cbdav, use_comp, object->href, object->etag);

			if (use_comp != icalcomp)
				icalcomponent_free (use_comp);
		}
	}

	g_object_unref (message);
	return result;
}

static ECalBackendSyncStatus
caldav_server_delete_object (ECalBackendCalDAV *cbdav, CalDAVObject *object)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     result;
	SoupMessage              *message;
	gchar                     *uri;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	result = GNOME_Evolution_Calendar_Success;

	g_assert (object != NULL && object->href != NULL);

	uri = caldav_generate_uri (cbdav, object->href);
	message = soup_message_new (SOUP_METHOD_DELETE, uri);
	g_free (uri);
	if (message == NULL)
		return GNOME_Evolution_Calendar_NoSuchCal;

	soup_message_headers_append (message->request_headers,
				     "User-Agent", "Evolution/" VERSION);

	if (object->etag != NULL) {
		soup_message_headers_append (message->request_headers,
					     "If-Match", object->etag);
	}

	send_and_handle_redirection (priv->session, message, NULL);

	result = status_code_to_result (message->status_code, priv);

	g_object_unref (message);

	return result;
}

static gboolean
caldav_receive_schedule_outbox_url (ECalBackendCalDAV *cbdav)
{
	ECalBackendCalDAVPrivate *priv;
	SoupMessage *message;
	xmlOutputBufferPtr buf;
	xmlDocPtr doc;
	xmlNodePtr root, node;
	xmlNsPtr nsdav;
	gchar *owner = NULL;

	g_return_val_if_fail (cbdav != NULL, FALSE);

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	g_return_val_if_fail (priv != NULL, FALSE);
	g_return_val_if_fail (priv->schedule_outbox_url == NULL, TRUE);

	/* Prepare the soup message */
	message = soup_message_new ("PROPFIND", priv->uri);
	if (message == NULL)
		return FALSE;

	doc = xmlNewDoc ((xmlChar *) "1.0");
	root = xmlNewDocNode (doc, NULL, (xmlChar *) "propfind", NULL);
	xmlDocSetRootElement (doc, root);
	nsdav = xmlNewNs (root, (xmlChar *) "DAV:", NULL);

	node = xmlNewTextChild (root, nsdav, (xmlChar *) "prop", NULL);
	node = xmlNewTextChild (node, nsdav, (xmlChar *) "owner", NULL);

	buf = xmlAllocOutputBuffer (NULL);
	xmlNodeDumpOutput (buf, doc, root, 0, 1, NULL);
	xmlOutputBufferFlush (buf);

	soup_message_headers_append (message->request_headers, "User-Agent", "Evolution/" VERSION);
	soup_message_headers_append (message->request_headers, "Depth", "0");

	soup_message_set_request (message,
				  "application/xml",
				  SOUP_MEMORY_COPY,
				  (gchar *) buf->buffer->content,
				  buf->buffer->use);

	/* Send the request now */
	send_and_handle_redirection (priv->session, message, NULL);

	/* Clean up the memory */
	xmlOutputBufferClose (buf);
	xmlFreeDoc (doc);

	/* Check the result */
	if (message->status_code == 207 && parse_propfind_response (message, XPATH_OWNER_STATUS, XPATH_OWNER, &owner) && owner && *owner) {
		xmlNsPtr nscd;
		SoupURI *suri;

		g_object_unref (message);

		/* owner is a full path to the user's URL, thus change it in
		   calendar's uri when asking for schedule-outbox-URL */
		suri = soup_uri_new (priv->uri);
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
		node = xmlNewTextChild (node, nscd, (xmlChar *) "schedule-outbox-URL", NULL);

		buf = xmlAllocOutputBuffer (NULL);
		xmlNodeDumpOutput (buf, doc, root, 0, 1, NULL);
		xmlOutputBufferFlush (buf);

		soup_message_headers_append (message->request_headers, "User-Agent", "Evolution/" VERSION);
		soup_message_headers_append (message->request_headers, "Depth", "0");

		soup_message_set_request (message,
				  "application/xml",
				  SOUP_MEMORY_COPY,
				  (gchar *) buf->buffer->content,
				  buf->buffer->use);

		/* Send the request now */
		send_and_handle_redirection (priv->session, message, NULL);

		if (message->status_code == 207 && parse_propfind_response (message, XPATH_SCHEDULE_OUTBOX_URL_STATUS, XPATH_SCHEDULE_OUTBOX_URL, &priv->schedule_outbox_url)) {
			if (!*priv->schedule_outbox_url) {
				g_free (priv->schedule_outbox_url);
				priv->schedule_outbox_url = NULL;
			} else {
				/* make it a full URI */
				suri = soup_uri_new (priv->uri);
				soup_uri_set_path (suri, priv->schedule_outbox_url);
				g_free (priv->schedule_outbox_url);
				priv->schedule_outbox_url = soup_uri_to_string (suri, FALSE);
				soup_uri_free (suri);
			}
		}

		/* Clean up the memory */
		xmlOutputBufferClose (buf);
		xmlFreeDoc (doc);
	}

	if (message)
		g_object_unref (message);

	g_free (owner);

	return priv->schedule_outbox_url != NULL;
}

/* ************************************************************************* */
/* Synchronization foo */

static gboolean extract_timezones (ECalBackendCalDAV *cbdav, icalcomponent *icomp);

struct cache_comp_list
{
	GSList *slist;
};

static gboolean
remove_complist_from_cache_and_notify_cb (gpointer key, gpointer value, gpointer data)
{
	GSList *l;
	struct cache_comp_list *ccl = value;
	ECalBackendCalDAV *cbdav = data;
	ECalBackendCalDAVPrivate *priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	for (l = ccl->slist; l; l = l->next) {
		ECalComponent *old_comp = l->data;
		ECalComponentId *id;

		id = e_cal_component_get_id (old_comp);
		if (!id) {
			continue;
		}

		if (e_cal_backend_store_remove_component (priv->store, id->uid, id->rid)) {
			gchar *old_str = e_cal_component_get_as_string (old_comp);

			e_cal_backend_notify_object_removed ((ECalBackend *)cbdav, id, old_str, NULL);

			g_free (old_str);
		}

		e_cal_component_free_id (id);
	}
	remove_cached_attachment (cbdav, (const gchar *)key);

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

#define etags_match(_tag1, _tag2) ((_tag1 == _tag2) ? TRUE :                 \
				   g_str_equal (_tag1 != NULL ? _tag1 : "",  \
						_tag2 != NULL ? _tag2 : ""))

/* start_time/end_time is an interval for checking changes. If both greater than zero,
   only the interval is checked and the removed items are not notified, as they can
   be still there.
*/
static void
synchronize_cache (ECalBackendCalDAV *cbdav, time_t start_time, time_t end_time)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackend *bkend;
	CalDAVObject *sobjs, *object;
	GSList *c_objs, *c_iter; /* list of all items known from our cache */
	GTree *c_uid2complist;  /* cache components list (with detached instances) sorted by (master's) uid */
	GHashTable *c_href2uid; /* connection between href and a (master's) uid */
	GSList *hrefs_to_update, *htu; /* list of href-s to update */
	gint i, len;

	if (!check_calendar_changed_on_server (cbdav)) {
		/* no changes on the server, no update required */
		return;
	}

	priv   = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	bkend  = E_CAL_BACKEND (cbdav);
	len    = 0;
	sobjs  = NULL;

	/* get list of server objects */
	if (!caldav_server_list_objects (cbdav, &sobjs, &len, NULL, start_time, end_time))
		return;

	c_objs = e_cal_backend_store_get_components (priv->store);

	if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
		printf ("CalDAV - found %d objects on the server, locally stored %d objects\n", len, g_slist_length (c_objs)); fflush (stdout);
	}

	/* do not store changes in cache immediately - makes things significantly quicker */
	e_cal_backend_store_freeze_changes (priv->store);

	c_uid2complist = g_tree_new_full ((GCompareDataFunc)g_strcmp0, NULL, g_free, free_comp_list);
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
			   freed before a removal from c_uid2complist, thus do not duplicate it,
			   rather save memory */
			g_hash_table_insert (c_href2uid, href, (gpointer)uid);
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
	for (i = 0, object = sobjs; i < len && priv->slave_cmd == SLAVE_SHOULD_WORK; i++, object++) {
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
	while (htu && priv->slave_cmd == SLAVE_SHOULD_WORK) {
		gint count = 0;
		GSList *to_fetch = NULL;

		while (count < CALDAV_MAX_MULTIGET_AMOUNT && htu) {
			to_fetch = g_slist_prepend (to_fetch, htu->data);
			htu = htu->next;
			count++;
		}

		if (to_fetch && priv->slave_cmd == SLAVE_SHOULD_WORK) {
			CalDAVObject *up_sobjs = NULL;

			if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
				printf ("CalDAV - going to fetch %d items\n", g_slist_length (to_fetch)); fflush (stdout);
			}

			count = 0;
			if (!caldav_server_list_objects (cbdav, &up_sobjs, &count, to_fetch, 0, 0)) {
				fprintf (stderr, "CalDAV - failed to retrieve bunch of items\n"); fflush (stderr);
				break;
			}

			if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
				printf ("CalDAV - fetched bunch of %d items\n", count); fflush (stdout);
			}

			/* we are going to update cache */
			/* they are downloaded, so process them */
			for (i = 0, object = up_sobjs; i < count /*&& priv->slave_cmd == SLAVE_SHOULD_WORK*/; i++, object++) {
				if (object->status == 200 && object->href && object->etag && object->cdata && *object->cdata) {
					icalcomponent *icomp = icalparser_parse_string (object->cdata);

					if (icomp) {
						icalcomponent_kind kind = icalcomponent_isa (icomp);

						extract_timezones (cbdav, icomp);

						if (kind == ICAL_VCALENDAR_COMPONENT) {
							icalcomponent *subcomp;

							kind = e_cal_backend_get_kind (bkend);

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

									ecalcomp_set_href (new_comp, object->href);
									ecalcomp_set_etag (new_comp, object->etag);

									old_comp = NULL;
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

									e_cal_backend_store_put_component (priv->store, new_comp);

									if (old_comp == NULL) {
										gchar *new_str = e_cal_component_get_as_string (new_comp);

										e_cal_backend_notify_object_created (bkend, new_str);

										g_free (new_str);
									} else {
										gchar *new_str = e_cal_component_get_as_string (new_comp);
										gchar *old_str = e_cal_component_get_as_string (old_comp);

										e_cal_backend_notify_object_modified (bkend, old_str, new_str);

										g_free (new_str);
										g_free (old_str);

										ccl->slist = g_slist_remove (ccl->slist, old_comp);
										g_object_unref (old_comp);
									}
								}

								g_object_unref (new_comp);
							}
						}

						icalcomponent_free (icomp);
					}
				}

				/* these free immediately */
				caldav_object_free (object, FALSE);
			}

			/* cache update done for fetched items */
		}

		/* do not free 'data' itself, it's part of 'sobjs' */
		g_slist_free (to_fetch);
	}

	/* if not interrupted and not using the time range... */
	if (priv->slave_cmd == SLAVE_SHOULD_WORK && (!start_time || !end_time)) {
		/* ...remove old (not on server anymore) items from our cache and notify of a removal */
		g_tree_foreach (c_uid2complist, remove_complist_from_cache_and_notify_cb, cbdav);
	}

	if (priv->ctag_to_store) {
		/* store only when wasn't interrupted */
		if (priv->slave_cmd == SLAVE_SHOULD_WORK && start_time == 0 && end_time == 0) {
			e_cal_backend_store_put_key_value (priv->store, CALDAV_CTAG_KEY, priv->ctag_to_store);
		}

		g_free (priv->ctag_to_store);
		priv->ctag_to_store = NULL;
	}

	/* save cache changes to disk finally */
	e_cal_backend_store_thaw_changes (priv->store);

	for (i = 0, object = sobjs; i < len; i++, object++) {
		caldav_object_free (object, FALSE);
	}

	g_tree_destroy (c_uid2complist);
}

/* ************************************************************************* */
static gpointer
caldav_synch_slave_loop (gpointer data)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendCalDAV        *cbdav;
	time_t now;
	icaltimezone *utc = icaltimezone_get_utc_timezone ();

	cbdav = E_CAL_BACKEND_CALDAV (data);
	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_lock (priv->busy_lock);

	while (priv->slave_cmd != SLAVE_SHOULD_DIE) {
		GTimeVal alarm_clock;
		if (priv->slave_cmd == SLAVE_SHOULD_SLEEP) {
			/* just sleep until we get woken up again */
			g_cond_wait (priv->cond, priv->busy_lock);

			/* check if we should die, work or sleep again */
			continue;
		}

		/* Ok here we go, do some real work
		 * Synch it baby one more time ...
		 */
		priv->slave_busy = TRUE;

		time (&now);
		/* check for events in the month before/after today first,
		   to show user actual data as soon as possible */
		synchronize_cache (cbdav, time_add_week_with_zone (now, -5, utc), time_add_week_with_zone (now, +5, utc));

		if (priv->slave_cmd != SLAVE_SHOULD_SLEEP) {
			/* and then check for changes in a whole calendar */
			synchronize_cache (cbdav, 0, 0);
		}

		if (caldav_debug_show (DEBUG_SERVER_ITEMS)) {
			GSList *c_objs;

			c_objs = e_cal_backend_store_get_components (priv->store);

			printf ("CalDAV - finished syncing with %d items in a cache\n", g_slist_length (c_objs)); fflush (stdout);

			g_slist_foreach (c_objs, (GFunc) g_object_unref, NULL);
			g_slist_free (c_objs);
		}

		priv->slave_busy = FALSE;

		/* puhh that was hard, get some rest :) */
		g_get_current_time (&alarm_clock);
		alarm_clock.tv_sec += priv->refresh_time.tv_sec;
		g_cond_timed_wait (priv->cond,
				   priv->busy_lock,
				   &alarm_clock);

	}

	/* signal we are done */
	g_cond_signal (priv->slave_gone_cond);

	priv->synch_slave = NULL;

	/* we got killed ... */
	g_mutex_unlock (priv->busy_lock);
	return NULL;
}

static gchar *
get_users_email (const gchar *username, const gchar *may_append)
{
	if (!username || !*username)
		return NULL;

	if (strchr (username, '@'))
		return g_strdup (username);

	return g_strconcat (username, may_append, NULL);
}

/* ************************************************************************* */
/* ********** ECalBackendSync virtual function implementation *************  */

static ECalBackendSyncStatus
caldav_is_read_only (ECalBackendSync *backend,
		     EDataCal        *cal,
		     gboolean        *read_only)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	/* no write support in offline mode yet! */
	if (priv->mode == CAL_MODE_LOCAL) {
		*read_only = TRUE;
	} else {
		*read_only = priv->read_only;
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_cal_address (ECalBackendSync  *backend,
			EDataCal         *cal,
			gchar            **address)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	*address = NULL;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	if (priv && priv->is_google && priv->username) {
		*address = get_users_email (priv->username, "@gmail.com");
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_alarm_email_address (ECalBackendSync  *backend,
				EDataCal         *cal,
				gchar            **address)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	*address = NULL;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	if (priv && priv->is_google && priv->username) {
		*address = get_users_email (priv->username, "@gmail.com");
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_ldap_attribute (ECalBackendSync  *backend,
			   EDataCal         *cal,
			   gchar           **attribute)
{
	*attribute = NULL;
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_static_capabilities (ECalBackendSync  *backend,
				EDataCal         *cal,
				gchar            **capabilities)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	if (priv && priv->is_google)
		*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
					  CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
					  CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED);
	else
		*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","
					  CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
					  CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
					  CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
initialize_backend (ECalBackendCalDAV *cbdav)
{
	ECalBackendSyncStatus     result;
	ECalBackendCalDAVPrivate *priv;
	ECalSourceType            source_type;
	ESource                  *source;
	const gchar		 *os_val;
	const gchar               *uri;
	gsize                     len;
	const gchar              *refresh;
	const gchar              *stype;
	gchar                    *filename;
	gchar                    *mangled_uri;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	result = GNOME_Evolution_Calendar_Success;
	source = e_cal_backend_get_source (E_CAL_BACKEND (cbdav));

	if (!g_signal_handler_find (G_OBJECT (source), G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL, caldav_source_changed_cb, cbdav))
		g_signal_connect (G_OBJECT (source), "changed", G_CALLBACK (caldav_source_changed_cb), cbdav);

	os_val = e_source_get_property (source, "offline_sync");

	if (!os_val || !g_str_equal (os_val, "1")) {
		priv->do_offline = FALSE;
	}

	os_val = e_source_get_property (source, "auth");
	priv->need_auth = os_val != NULL;

	os_val = e_source_get_property(source, "ssl");
	uri = e_cal_backend_get_uri (E_CAL_BACKEND (cbdav));

	g_free (priv->uri);
	priv->uri = NULL;
	if (g_str_has_prefix (uri, "caldav://")) {
		const gchar *proto;

		if (os_val && os_val[0] == '1') {
			proto = "https://";
		} else {
			proto = "http://";
		}

		priv->uri = g_strconcat (proto, uri + 9, NULL);
	} else {
		priv->uri = g_strdup (uri);
	}

	if (priv->uri) {
		SoupURI *suri = soup_uri_new (priv->uri);

		/* properly encode uri */
		if (suri && suri->path) {
			gchar *tmp, *path;

			if (suri->path && strchr (suri->path, '%')) {
				/* If path contains anything already encoded, then decode it first,
				   thus it'll be managed properly. For example, the '#' in a path
				   is in URI shown as %23 and not doing this decode makes it being
				   like %2523, which is not what is wanted here. */
				tmp = soup_uri_decode (suri->path);
				soup_uri_set_path (suri, tmp);
				g_free (tmp);
			}

			tmp = soup_uri_encode (suri->path, NULL);
			path = soup_uri_normalize (tmp, "/");

			soup_uri_set_path (suri, path);

			g_free (tmp);
			g_free (path);
			g_free (priv->uri);

			priv->uri = soup_uri_to_string (suri, FALSE);
		}

		soup_uri_free (suri);
	}

	/* remove trailing slashes... */
	len = strlen (priv->uri);
	while (len--) {
		if (priv->uri[len] == '/') {
			priv->uri[len] = '\0';
		} else {
			break;
		}
	}

	/* ...and append exactly one slash */
	if (priv->uri && *priv->uri) {
		gchar *tmp = priv->uri;

		priv->uri = g_strconcat (priv->uri, "/", NULL);

		g_free (tmp);
	}

	switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbdav))) {
		default:
		case ICAL_VEVENT_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_EVENT;
			stype = "calendar";
			break;
		case ICAL_VTODO_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_TODO;
			stype = "tasks";
			break;
		case ICAL_VJOURNAL_COMPONENT:
			source_type = E_CAL_SOURCE_TYPE_JOURNAL;
			stype = "journal";
			break;
	}

	if (priv->store == NULL) {
		/* remove the old cache while migrating to ECalBackendStore */
		e_cal_backend_cache_remove (priv->uri, source_type);
		priv->store = (ECalBackendStore *) e_cal_backend_file_store_new (priv->uri, source_type);

		if (priv->store == NULL) {
			result = GNOME_Evolution_Calendar_OtherError;
			goto out;
		}

		e_cal_backend_store_load (priv->store);
	}

	/* Set the local attachment store */
	mangled_uri = g_strdup (uri);
	mangled_uri = g_strdelimit (mangled_uri, ":/", '_');
	filename = g_build_filename (g_get_home_dir (),
			".evolution", "cache", stype,
			mangled_uri, NULL);
	g_free (mangled_uri);
	if (priv->local_attachments_store)
		g_free (priv->local_attachments_store);
	priv->local_attachments_store = filename;
	if (g_mkdir_with_parents (filename, 0700) < 0) {
		result = GNOME_Evolution_Calendar_OtherError;
		goto out;
	}

	refresh = e_source_get_property (source, "refresh");
	priv->refresh_time.tv_sec  = (refresh && atoi (refresh) > 0) ? (60 * atoi (refresh)) : (DEFAULT_REFRESH_TIME);

	if (!priv->synch_slave) {
		GThread *slave;

		priv->slave_cmd = SLAVE_SHOULD_SLEEP;
		slave = g_thread_create (caldav_synch_slave_loop, cbdav, FALSE, NULL);

		if (slave == NULL) {
			g_warning ("Could not create synch slave");
			result = GNOME_Evolution_Calendar_OtherError;
		}

		priv->synch_slave = slave;
	}
out:
	return result;
}

static void
proxy_settings_changed (EProxy *proxy, gpointer user_data)
{
	SoupURI *proxy_uri = NULL;
	ECalBackendCalDAVPrivate *priv = (ECalBackendCalDAVPrivate *) user_data;

	if (!priv || !priv->uri || !priv->session)
		return;

	/* use proxy if necessary */
	if (e_proxy_require_proxy_for_uri (proxy, priv->uri)) {
		proxy_uri = e_proxy_peek_uri_for (proxy, priv->uri);
	}

	g_object_set (priv->session, SOUP_SESSION_PROXY_URI, proxy_uri, NULL);
}

static gboolean
is_google_uri (const gchar *uri)
{
	SoupURI *suri;
	gboolean res;

	g_return_val_if_fail (uri != NULL, FALSE);

	suri = soup_uri_new (uri);
	g_return_val_if_fail (suri != NULL, FALSE);

	res = suri->host && g_ascii_strcasecmp (suri->host, "www.google.com") == 0;

	soup_uri_free (suri);

	return res;
}

static ECalBackendSyncStatus
caldav_do_open (ECalBackendSync *backend,
		EDataCal        *cal,
		gboolean         only_if_exists,
		const gchar      *username,
		const gchar      *password)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	status = GNOME_Evolution_Calendar_Success;

	g_mutex_lock (priv->busy_lock);

	/* let it decide the 'getctag' extension availability again */
	priv->ctag_supported = TRUE;

	if (!priv->loaded) {
		status = initialize_backend (cbdav);
	}

	if (status != GNOME_Evolution_Calendar_Success) {
		g_mutex_unlock (priv->busy_lock);
		return status;
	}

	if (priv->need_auth) {
		if ((username == NULL || password == NULL)) {
			g_mutex_unlock (priv->busy_lock);
			return GNOME_Evolution_Calendar_AuthenticationRequired;
		}

		g_free (priv->username);
		priv->username = g_strdup (username);
		g_free (priv->password);
		priv->password = g_strdup (password);
	}

	if (!priv->do_offline && priv->mode == CAL_MODE_LOCAL) {
		g_mutex_unlock (priv->busy_lock);
		return GNOME_Evolution_Calendar_RepositoryOffline;
	}

	priv->loaded = TRUE;
	priv->is_google = FALSE;

	if (priv->mode == CAL_MODE_REMOTE) {
		/* set forward proxy */
		proxy_settings_changed (priv->proxy, priv);

		status = caldav_server_open_calendar (cbdav);

		if (status == GNOME_Evolution_Calendar_Success) {
			priv->slave_cmd = SLAVE_SHOULD_WORK;
			g_cond_signal (priv->cond);

			priv->is_google = is_google_uri (priv->uri);
		}
	} else {
		priv->read_only = TRUE;
	}

	g_mutex_unlock (priv->busy_lock);

	return status;
}

static ECalBackendSyncStatus
caldav_refresh (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	gboolean                  online;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_lock (priv->busy_lock);

	if (!priv->loaded
	    || priv->slave_cmd != SLAVE_SHOULD_SLEEP
	    || check_state (cbdav, &online) != GNOME_Evolution_Calendar_Success
	    || !online) {
		g_mutex_unlock (priv->busy_lock);
		return GNOME_Evolution_Calendar_Success;
	}

	priv->slave_cmd = SLAVE_SHOULD_WORK;

	/* wake it up */
	g_cond_signal (priv->cond);
	g_mutex_unlock (priv->busy_lock);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_remove (ECalBackendSync *backend,
	       EDataCal        *cal)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	gboolean                  online;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	/* first tell it to die, then wait for its lock */
	priv->slave_cmd = SLAVE_SHOULD_DIE;

	g_mutex_lock (priv->busy_lock);

	if (!priv->loaded) {
		g_mutex_unlock (priv->busy_lock);
		return GNOME_Evolution_Calendar_Success;
	}

	status = check_state (cbdav, &online);

	/* lie here a bit, but otherwise the calendar will not be removed, even it should */
	if (status != GNOME_Evolution_Calendar_Success)
		g_print (G_STRLOC ": %s", e_cal_backend_status_to_string (status));

	e_cal_backend_store_remove (priv->store);
	priv->store = NULL;
	priv->loaded = FALSE;

	if (priv->synch_slave) {
		g_cond_signal (priv->cond);

		/* wait until the slave died */
		g_cond_wait (priv->slave_gone_cond, priv->busy_lock);
	}

	g_mutex_unlock (priv->busy_lock);

	return GNOME_Evolution_Calendar_Success;
}

static void
remove_comp_from_cache_cb (gpointer value, gpointer user_data)
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
remove_comp_from_cache (ECalBackendCalDAV *cbdav, const gchar *uid, const gchar *rid)
{
	ECalBackendCalDAVPrivate *priv;
	gboolean res = FALSE;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	if (!rid || !*rid) {
		/* get with detached instances */
		GSList *objects = e_cal_backend_store_get_components_by_uid (priv->store, uid);

		if (objects) {
			g_slist_foreach (objects, (GFunc)remove_comp_from_cache_cb, priv->store);
			g_slist_foreach (objects, (GFunc)g_object_unref, NULL);
			g_slist_free (objects);

			res = TRUE;
		}
	} else {
		res = e_cal_backend_store_remove_component (priv->store, uid, rid);
	}

	return res;
}

static void
add_detached_recur_to_vcalendar_cb (gpointer value, gpointer user_data)
{
	icalcomponent *recurrence = e_cal_component_get_icalcomponent (value);
	icalcomponent *vcalendar = user_data;

	icalcomponent_add_component (
		vcalendar,
		icalcomponent_new_clone (recurrence));
}

static gint
sort_master_first (gconstpointer a, gconstpointer b)
{
	icalcomponent *ca, *cb;

	ca = e_cal_component_get_icalcomponent ((ECalComponent *)a);
	cb = e_cal_component_get_icalcomponent ((ECalComponent *)b);

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
   The cache lock should be locked when called this function.
*/
static icalcomponent *
get_comp_from_cache (ECalBackendCalDAV *cbdav, const gchar *uid, const gchar *rid, gchar **href, gchar **etag)
{
	ECalBackendCalDAVPrivate *priv;
	icalcomponent *icalcomp = NULL;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	if (rid == NULL || !*rid) {
		/* get with detached instances */
		GSList *objects = e_cal_backend_store_get_components_by_uid (priv->store, uid);

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

		g_slist_foreach (objects, (GFunc)g_object_unref, NULL);
		g_slist_free (objects);
	} else {
		/* get the exact object */
		ECalComponent *comp = e_cal_backend_store_get_component (priv->store, uid, rid);

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

static gboolean
put_comp_to_cache (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp, const gchar *href, const gchar *etag)
{
	ECalBackendCalDAVPrivate *priv;
	icalcomponent_kind my_kind;
	ECalComponent *comp;
	gboolean res = FALSE;

	g_return_val_if_fail (cbdav != NULL, FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

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

				if (e_cal_backend_store_put_component (priv->store, comp))
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

			res = e_cal_backend_store_put_component (priv->store, comp);
		}
	}

	g_object_unref (comp);

	return res;
}

static void
remove_property (gpointer prop, gpointer icomp)
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

static void
convert_to_inline_attachment (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp)
{
	ECalBackendCalDAVPrivate *priv;
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

		attach = icalproperty_get_attach ((const icalproperty *)p);
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
	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
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

		attach = icalproperty_get_attach ((const icalproperty *)p);
		if (!icalattach_get_is_url (attach))
			continue;

		uri = icalattach_get_url (attach);
		if (!g_str_has_prefix (uri, LOCAL_PREFIX))
			continue;

		file = g_file_new_for_uri (uri);
		basename = g_file_get_basename (file);
		if (g_file_load_contents (file, NULL, &content, &len, NULL, &error) == TRUE) {
			icalproperty *prop;
			icalparameter *param;
			gchar *encoded;

			/*
			 * do a base64 encoding so it can
			 * be embedded in a soap message
			 */
			encoded = g_base64_encode ((guchar *) content, len);
			attach = icalattach_new_from_data ((guchar *) encoded, NULL, NULL);
			g_free(content);
			g_free(encoded);

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
convert_to_url_attachment (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp)
{
	ECalBackendCalDAVPrivate *priv;
	GSList *to_remove = NULL;
	icalcomponent *cclone;
	icalproperty *p;

	g_return_if_fail (cbdav != NULL);
	g_return_if_fail (icalcomp != NULL);

	cclone = icalcomponent_new_clone (icalcomp);

	/* Remove all inline attachments first */
	for (p = icalcomponent_get_first_property (icalcomp, ICAL_ATTACH_PROPERTY);
	     p;
	     p = icalcomponent_get_next_property (icalcomp, ICAL_ATTACH_PROPERTY)) {
		icalattach *attach;

		attach = icalproperty_get_attach ((const icalproperty *)p);
		if (!icalattach_get_is_url (attach))
			to_remove = g_slist_prepend (to_remove, p);
	}
	g_slist_foreach (to_remove, remove_property, icalcomp);
	g_slist_free (to_remove);

	/* convert inline attachments to url attachments now */
	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	for (p = icalcomponent_get_first_property (cclone, ICAL_ATTACH_PROPERTY);
	     p;
	     p = icalcomponent_get_next_property (cclone, ICAL_ATTACH_PROPERTY)) {
		icalattach *attach;
		gchar *dir;

		attach = icalproperty_get_attach ((const icalproperty *)p);
		if (icalattach_get_is_url (attach))
			continue;

		dir = g_build_filename (priv->local_attachments_store,
				icalcomponent_get_uid (icalcomp),
				NULL);
		if (g_mkdir_with_parents (dir, 0700) >= 0) {
			GError *error = NULL;
			gchar *basename;
			gchar *dest;
			gchar *content;
			gsize len;
			gchar *decoded;

			basename = icalproperty_get_parameter_as_string_r (p,
					X_E_CALDAV_ATTACHMENT_NAME);
			dest = g_build_filename (dir, basename, NULL);
			g_free (basename);

			content = (gchar *)icalattach_get_data (attach);
			decoded = (gchar *)g_base64_decode (content, &len);
			if (g_file_set_contents (dest, decoded, len, &error) == TRUE) {
				icalproperty *prop;
				gchar *url;

				url = g_filename_to_uri (dest, NULL, NULL);
				attach = icalattach_new_from_url (url);
				prop = icalproperty_new_attach (attach);
				icalattach_unref (attach);
				icalcomponent_add_property (icalcomp, prop);
				g_free (url);
			} else {
				g_warning ("%s\n", error->message);
				g_clear_error (&error);
			}
			g_free (decoded);
			g_free (dest);
		}
		g_free (dir);
	}
	icalcomponent_free (cclone);
}

static void
remove_dir (const gchar *dir)
{
	GDir *d;

	/*
	 * remove all files in the direcory first
	 * and call rmdir to remove the empty directory
	 * because ZFS does not support unlinking a directory.
	 */
	d = g_dir_open (dir, 0, NULL);
	if (d) {
		const gchar *entry;

		while ((entry = g_dir_read_name (d)) != NULL) {
			gchar *path;
			gint ret;

			path = g_build_filename (dir, entry, NULL);
			if (g_file_test (path, G_FILE_TEST_IS_DIR))
				remove_dir (path);
			else
				ret = g_unlink (path);
			g_free (path);
		}
		g_dir_close (d);
	}
	g_rmdir (dir);
}

static void
remove_cached_attachment (ECalBackendCalDAV *cbdav, const gchar *uid)
{
	ECalBackendCalDAVPrivate *priv;
	GSList *l;
	guint len;
	gchar *dir;

	g_return_if_fail (cbdav != NULL);
	g_return_if_fail (uid != NULL);

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	l = e_cal_backend_store_get_components_by_uid (priv->store, uid);
	len = g_slist_length (l);
	g_slist_foreach (l, (GFunc)g_object_unref, NULL);
	g_slist_free (l);
	if (len > 0)
		return;

	dir = g_build_filename (priv->local_attachments_store,
			uid, NULL);
	remove_dir (dir);
	g_free (dir);
}

/* callback for icalcomponent_foreach_tzid */
typedef struct {
	ECalBackendStore *store;
	icalcomponent *vcal_comp;
	icalcomponent *icalcomp;
} ForeachTzidData;

static void
add_timezone_cb (icalparameter *param, gpointer data)
{
	icaltimezone *tz;
	const gchar *tzid;
	icalcomponent *vtz_comp;
	ForeachTzidData *f_data = (ForeachTzidData *) data;

	tzid = icalparameter_get_tzid (param);
	if (!tzid)
		return;

	tz = icalcomponent_get_timezone (f_data->vcal_comp, tzid);
	if (tz)
		return;

	tz = icalcomponent_get_timezone (f_data->icalcomp, tzid);
	if (!tz) {
		tz = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!tz)
			tz = (icaltimezone *) e_cal_backend_store_get_timezone (f_data->store, tzid);
		if (!tz)
			return;
	}

	vtz_comp = icaltimezone_get_component (tz);
	if (!vtz_comp)
		return;

	icalcomponent_add_component (f_data->vcal_comp,
				     icalcomponent_new_clone (vtz_comp));
}

static void
add_timezones_from_component (ECalBackendCalDAV *cbdav, icalcomponent *vcal_comp, icalcomponent *icalcomp)
{
	ForeachTzidData f_data;
	ECalBackendCalDAVPrivate *priv;

	g_return_if_fail (cbdav != NULL);
	g_return_if_fail (vcal_comp != NULL);
	g_return_if_fail (icalcomp != NULL);

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	f_data.store = priv->store;
	f_data.vcal_comp = vcal_comp;
	f_data.icalcomp = icalcomp;

	icalcomponent_foreach_tzid (icalcomp, add_timezone_cb, &f_data);
}

/* also removes X-EVOLUTION-CALDAV from all the components */
static gchar *
pack_cobj (ECalBackendCalDAV *cbdav, icalcomponent *icomp)
{
	ECalBackendCalDAVPrivate *priv;
	icalcomponent *calcomp;
	gchar          *objstr;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

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

	g_assert (objstr);

	return objstr;

}

static void
sanitize_component (ECalBackend *cb, ECalComponent *comp)
{
	ECalComponentDateTime dt;
	icaltimezone *zone, *default_zone;

	/* Check dtstart, dtend and due's timezone, and convert it to local
	 * default timezone if the timezone is not in our builtin timezone
	 * list */
	e_cal_component_get_dtstart (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = caldav_internal_get_timezone (cb, dt.tzid);
		if (!zone) {
			default_zone = caldav_internal_get_default_timezone (cb);
			g_free ((gchar *)dt.tzid);
			dt.tzid = g_strdup (icaltimezone_get_tzid (default_zone));
			e_cal_component_set_dtstart (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_get_dtend (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = caldav_internal_get_timezone (cb, dt.tzid);
		if (!zone) {
			default_zone = caldav_internal_get_default_timezone (cb);
			g_free ((gchar *)dt.tzid);
			dt.tzid = g_strdup (icaltimezone_get_tzid (default_zone));
			e_cal_component_set_dtend (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_get_due (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = caldav_internal_get_timezone (cb, dt.tzid);
		if (!zone) {
			default_zone = caldav_internal_get_default_timezone (cb);
			g_free ((gchar *)dt.tzid);
			dt.tzid = g_strdup (icaltimezone_get_tzid (default_zone));
			e_cal_component_set_due (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);
	e_cal_component_abort_sequence (comp);
}

static gboolean
cache_contains (ECalBackendCalDAV *cbdav, const gchar *uid, const gchar *rid)
{
	ECalBackendCalDAVPrivate *priv;
	gboolean res;
	ECalComponent *comp;

	g_return_val_if_fail (cbdav != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	g_return_val_if_fail (priv != NULL && priv->store != NULL, FALSE);

	comp = e_cal_backend_store_get_component (priv->store, uid, rid);
	res = comp != NULL;

	if (comp)
		g_object_unref (comp);

	return res;
}

/* Returns subcomponent of icalcomp, which is a master object, or icalcomp itself, if it's not a VCALENDAR;
   Do not free returned pointer, it'll be freed together with the icalcomp.
*/
static icalcomponent *
get_master_comp (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp)
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
remove_instance (ECalBackendCalDAV *cbdav, icalcomponent *icalcomp, struct icaltimetype rid, CalObjModType mod, gboolean also_exdate)
{
	icalcomponent *master = icalcomp;
	gboolean res = FALSE;

	g_return_val_if_fail (icalcomp != NULL, res);
	g_return_val_if_fail (!icaltime_is_null_time (rid), res);

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
replace_master (ECalBackendCalDAV *cbdav, icalcomponent *old_comp, icalcomponent *new_master)
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

/* a busy_lock is supposed to be locked already, when calling this function */
static ECalBackendSyncStatus
do_create_object (ECalBackendCalDAV *cbdav, gchar **calobj, gchar **uid)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	ECalComponent            *comp;
	gboolean                  online;
	struct icaltimetype current;
	icalcomponent *icalcomp;
	const gchar *comp_uid;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	status = check_state (cbdav, &online);

	if (status != GNOME_Evolution_Calendar_Success) {
		return status;
	}

	comp = e_cal_component_new_from_string (*calobj);

	if (comp == NULL) {
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	icalcomp = e_cal_component_get_icalcomponent (comp);
	if (icalcomp == NULL) {
		g_object_unref (comp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	comp_uid = icalcomponent_get_uid (icalcomp);
	if (!comp_uid) {
		gchar *new_uid;

		new_uid = e_cal_component_gen_uid ();
		if (!new_uid) {
			g_object_unref (comp);
			return GNOME_Evolution_Calendar_InvalidObject;
		}

		icalcomponent_set_uid (icalcomp, new_uid);
		comp_uid = icalcomponent_get_uid (icalcomp);

		g_free (new_uid);
	}

	/* check the object is not in our cache */
	if (cache_contains (cbdav, comp_uid, NULL)) {
		g_object_unref (comp);
		return GNOME_Evolution_Calendar_ObjectIdAlreadyExists;
	}

	/* Set the created and last modified times on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (comp, &current);
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component*/
	sanitize_component ((ECalBackend *)cbdav, comp);

	if (online) {
		CalDAVObject object;

		object.href  = ecalcomp_gen_href (comp);
		object.etag  = NULL;
		object.cdata = pack_cobj (cbdav, icalcomp);

		status = caldav_server_put_object (cbdav, &object, icalcomp);

		caldav_object_free (&object, FALSE);
	} else {
		/* mark component as out of synch */
		/*ecalcomp_set_synch_state (comp, ECALCOMP_LOCALLY_CREATED); */
	}

	if (status == GNOME_Evolution_Calendar_Success) {
		if (uid)
			*uid = g_strdup (comp_uid);

		icalcomp = get_comp_from_cache (cbdav, comp_uid, NULL, NULL, NULL);

		if (icalcomp) {
			icalcomponent *master = get_master_comp (cbdav, icalcomp);

			if (!master)
				*calobj = e_cal_component_get_as_string (comp);
			else
				*calobj = icalcomponent_as_ical_string_r (master);

			icalcomponent_free (icalcomp);
		} else {
			*calobj = e_cal_component_get_as_string (comp);
		}
	}

	g_object_unref (comp);

	return status;
}

/* a busy_lock is supposed to be locked already, when calling this function */
static ECalBackendSyncStatus
do_modify_object (ECalBackendCalDAV *cbdav, const gchar *calobj, CalObjModType mod, gchar **old_object, gchar **new_object)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	ECalComponent            *comp;
	icalcomponent            *cache_comp;
	gboolean                  online;
	ECalComponentId		 *id;
	struct icaltimetype current;
	gchar *href = NULL, *etag = NULL;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	if (new_object)
		*new_object = NULL;

	status = check_state (cbdav, &online);
	if (status != GNOME_Evolution_Calendar_Success) {
		return status;
	}

	comp = e_cal_component_new_from_string (calobj);

	if (comp == NULL) {
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	if (!e_cal_component_get_icalcomponent (comp) ||
	    icalcomponent_isa (e_cal_component_get_icalcomponent (comp)) != e_cal_backend_get_kind (E_CAL_BACKEND (cbdav))) {
		g_object_unref (comp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* Set the last modified time on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component */
	sanitize_component ((ECalBackend *)cbdav, comp);

	id = e_cal_component_get_id (comp);
	g_return_val_if_fail (id != NULL, GNOME_Evolution_Calendar_OtherError);

	/* fetch full component from cache, it will be pushed to the server */
	cache_comp = get_comp_from_cache (cbdav, id->uid, NULL, &href, &etag);

	if (cache_comp == NULL) {
		e_cal_component_free_id (id);
		g_object_unref (comp);
		g_free (href);
		g_free (etag);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	if (!online) {
		/* mark component as out of synch */
		/*ecalcomp_set_synch_state (comp, ECALCOMP_LOCALLY_MODIFIED);*/
	}

	if (old_object) {
		*old_object = NULL;

		if (e_cal_component_is_instance (comp)) {
			/* set detached instance as the old object, if any */
			ECalComponent *old_instance = old_instance = e_cal_backend_store_get_component (priv->store, id->uid, id->rid);

			if (old_instance) {
				*old_object = e_cal_component_get_as_string (old_instance);
				g_object_unref (old_instance);
			}
		}

		if (!*old_object) {
			icalcomponent *master = get_master_comp (cbdav, cache_comp);

			if (master) {
				/* set full component as the old object */
				*old_object = icalcomponent_as_ical_string_r (master);
			}
		}
	}

	switch (mod) {
	case CALOBJ_MOD_THIS:
		if (e_cal_component_is_instance (comp)) {
			icalcomponent *new_comp = e_cal_component_get_icalcomponent (comp);

			/* new object is only this instance */
			if (new_object)
				*new_object = e_cal_component_get_as_string (comp);

			/* add the detached instance */
			if (icalcomponent_isa (cache_comp) == ICAL_VCALENDAR_COMPONENT) {
				/* do not modify the EXDATE, as the component will be put back */
				remove_instance (cbdav, cache_comp, icalcomponent_get_recurrenceid (new_comp), mod, FALSE);
			} else {
				/* this is only a master object, thus make is a VCALENDAR component */
				icalcomponent *icomp;

				icomp = e_cal_util_new_top_level();
				icalcomponent_add_component (icomp, cache_comp);

				/* no need to free the cache_comp, as it is inside icomp */
				cache_comp = icomp;
			}

			if (cache_comp && priv->is_google) {
				icalcomponent_set_sequence (cache_comp, icalcomponent_get_sequence (cache_comp) + 1);
				icalcomponent_set_sequence (new_comp, icalcomponent_get_sequence (new_comp) + 1);
			}

			/* add the detached instance finally */
			icalcomponent_add_component (cache_comp, icalcomponent_new_clone (new_comp));
		} else {
			cache_comp = replace_master (cbdav, cache_comp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
		}
		break;
	case CALOBJ_MOD_ALL:
		cache_comp = replace_master (cbdav, cache_comp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
		break;
	case CALOBJ_MOD_THISANDPRIOR:
	case CALOBJ_MOD_THISANDFUTURE:
		break;
	}

	if (online) {
		CalDAVObject object;

		object.href  = href;
		object.etag  = etag;
		object.cdata = pack_cobj (cbdav, cache_comp);

		status = caldav_server_put_object (cbdav, &object, cache_comp);

		caldav_object_free (&object, FALSE);
		href = NULL;
		etag = NULL;
	} else {
		/* mark component as out of synch */
		/*ecalcomp_set_synch_state (comp, ECALCOMP_LOCALLY_MODIFIED);*/
	}

	if (status == GNOME_Evolution_Calendar_Success) {
		if (new_object && !*new_object) {
			/* read the comp from cache again, as some servers can modify it on put */
			icalcomponent *newcomp = get_comp_from_cache (cbdav, id->uid, NULL, NULL, NULL), *master;

			if (!newcomp)
				newcomp = cache_comp;

			master = get_master_comp (cbdav, newcomp);

			if (master)
				*new_object = icalcomponent_as_ical_string_r (master);

			if (cache_comp != newcomp)
				icalcomponent_free (newcomp);
		}
	}

	e_cal_component_free_id (id);
	icalcomponent_free (cache_comp);
	g_object_unref (comp);
	g_free (href);
	g_free (etag);

	return status;
}

/* a busy_lock is supposed to be locked already, when calling this function */
static ECalBackendSyncStatus
do_remove_object (ECalBackendCalDAV *cbdav, const gchar *uid, const gchar *rid, CalObjModType mod, gchar **old_object, gchar **object)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	icalcomponent            *cache_comp;
	gboolean                  online;
	gchar *href = NULL, *etag = NULL;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	if (object)
		*object = NULL;

	status = check_state (cbdav, &online);
	if (status != GNOME_Evolution_Calendar_Success) {
		return status;
	}

	cache_comp = get_comp_from_cache (cbdav, uid, NULL, &href, &etag);

	if (cache_comp == NULL) {
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	if (old_object) {
		ECalComponent *old = e_cal_backend_store_get_component (priv->store, uid, rid);

		if (old) {
			*old_object = e_cal_component_get_as_string (old);
			g_object_unref (old);
		} else {
			icalcomponent *master = get_master_comp (cbdav, cache_comp);

			if (master)
				*old_object = icalcomponent_as_ical_string_r (master);
		}
	}

	switch (mod) {
	case CALOBJ_MOD_THIS:
		if (rid && *rid) {
			/* remove one instance from the component */
			if (remove_instance (cbdav, cache_comp, icaltime_from_string (rid), mod, TRUE)) {
				if (object) {
					icalcomponent *master = get_master_comp (cbdav, cache_comp);

					if (master)
						*object = icalcomponent_as_ical_string_r (master);
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
	case CALOBJ_MOD_ALL:
		remove_comp_from_cache (cbdav, uid, NULL);
		break;
	case CALOBJ_MOD_THISANDPRIOR:
	case CALOBJ_MOD_THISANDFUTURE:
		break;
	}

	if (online) {
		CalDAVObject caldav_object;

		caldav_object.href  = href;
		caldav_object.etag  = etag;
		caldav_object.cdata = NULL;

		if (mod == CALOBJ_MOD_THIS && rid && *rid) {
			caldav_object.cdata = pack_cobj (cbdav, cache_comp);

			status = caldav_server_put_object (cbdav, &caldav_object, cache_comp);
		} else
			status = caldav_server_delete_object (cbdav, &caldav_object);

		caldav_object_free (&caldav_object, FALSE);
		href = NULL;
		etag = NULL;
	} else {
		/* mark component as out of synch */
		/*if (mod == CALOBJ_MOD_THIS && rid && *rid)
			ecalcomp_set_synch_state (cache_comp_master, ECALCOMP_LOCALLY_MODIFIED);
		else
			ecalcomp_set_synch_state (cache_comp_master, ECALCOMP_LOCALLY_DELETED);*/
	}
	remove_cached_attachment (cbdav, uid);

	icalcomponent_free (cache_comp);
	g_free (href);
	g_free (etag);

	return status;
}

static ECalBackendSyncStatus
extract_objects (icalcomponent       *icomp,
		 icalcomponent_kind   ekind,
		 GList              **objects)
{
	icalcomponent         *scomp;
	icalcomponent_kind     kind;

	g_return_val_if_fail (icomp, GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (objects, GNOME_Evolution_Calendar_OtherError);

	kind = icalcomponent_isa (icomp);

	if (kind == ekind) {
		*objects = g_list_prepend (NULL, icomp);
		return GNOME_Evolution_Calendar_Success;
	}

	if (kind != ICAL_VCALENDAR_COMPONENT) {
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	*objects = NULL;
	scomp = icalcomponent_get_first_component (icomp,
						   ekind);

	while (scomp) {
		/* Remove components from toplevel here */
		*objects = g_list_prepend (*objects, scomp);
		icalcomponent_remove_component (icomp, scomp);

		scomp = icalcomponent_get_next_component (icomp, ekind);
	}

	return GNOME_Evolution_Calendar_Success;
}

static gboolean
extract_timezones (ECalBackendCalDAV *cbdav, icalcomponent *icomp)
{
	ECalBackendCalDAVPrivate *priv;
	GList *timezones = NULL, *iter;
	icaltimezone *zone;

	g_return_val_if_fail (cbdav != NULL, FALSE);
	g_return_val_if_fail (icomp != NULL, FALSE);

	if (extract_objects (icomp, ICAL_VTIMEZONE_COMPONENT, &timezones) != GNOME_Evolution_Calendar_Success) {
		return FALSE;
	}

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	zone = icaltimezone_new ();
	for (iter = timezones; iter; iter = iter->next) {
		if (icaltimezone_set_component (zone, iter->data)) {
			e_cal_backend_store_put_timezone (priv->store, zone);
		} else {
			icalcomponent_free (iter->data);
		}
	}

	icaltimezone_free (zone, TRUE);
	g_list_free (timezones);

	return TRUE;
}

static ECalBackendSyncStatus
process_object (ECalBackendCalDAV   *cbdav,
		ECalComponent       *ecomp,
		gboolean             online,
		icalproperty_method  method)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	ECalBackend              *backend;
	struct icaltimetype       now;
	gchar *new_obj_str;
	gboolean is_declined, is_in_cache;
	CalObjModType mod;
	ECalComponentId *id = e_cal_component_get_id (ecomp);

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	backend = E_CAL_BACKEND (cbdav);

	g_return_val_if_fail (id != NULL, GNOME_Evolution_Calendar_InvalidObject);

	/* ctime, mtime */
	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (ecomp, &now);
	e_cal_component_set_last_modified (ecomp, &now);

	/* just to check whether component exists in a cache */
	is_in_cache = cache_contains (cbdav, id->uid, NULL) || cache_contains (cbdav, id->uid, id->rid);

	new_obj_str = e_cal_component_get_as_string (ecomp);
	mod = e_cal_component_is_instance (ecomp) ? CALOBJ_MOD_THIS : CALOBJ_MOD_ALL;
	status = GNOME_Evolution_Calendar_Success;

	switch (method) {
	case ICAL_METHOD_PUBLISH:
	case ICAL_METHOD_REQUEST:
	case ICAL_METHOD_REPLY:
		is_declined = e_cal_backend_user_declined (e_cal_component_get_icalcomponent (ecomp));
		if (is_in_cache) {
			if (!is_declined) {
				gchar *new_object = NULL, *old_object = NULL;

				status = do_modify_object (cbdav, new_obj_str, mod, &old_object, &new_object);
				if (status == GNOME_Evolution_Calendar_Success) {
					if (!old_object)
						e_cal_backend_notify_object_created (backend, new_object);
					else
						e_cal_backend_notify_object_modified (backend, old_object, new_object);
				}

				g_free (new_object);
				g_free (old_object);
			} else {
				gchar *new_object = NULL, *old_object = NULL;

				status = do_remove_object (cbdav, id->uid, id->rid, mod, &old_object, &new_object);
				if (status == GNOME_Evolution_Calendar_Success) {
					if (new_object) {
						e_cal_backend_notify_object_modified (backend, old_object, new_object);
					} else {
						e_cal_backend_notify_object_removed (backend, id, old_object, NULL);
					}
				}

				g_free (new_object);
				g_free (old_object);
			}
		} else if (!is_declined) {
			gchar *new_object = new_obj_str;

			status = do_create_object (cbdav, &new_object, NULL);
			if (status == GNOME_Evolution_Calendar_Success) {
				e_cal_backend_notify_object_created (backend, new_object);
			}

			if (new_object != new_obj_str)
				g_free (new_object);
		}
		break;
	case ICAL_METHOD_CANCEL:
		if (is_in_cache) {
			gchar *old_object = NULL, *new_object = NULL;

			status = do_remove_object (cbdav, id->uid, id->rid, CALOBJ_MOD_THIS, &old_object, &new_object);
			if (status == GNOME_Evolution_Calendar_Success) {
				if (new_object) {
					e_cal_backend_notify_object_modified (backend, old_object, new_object);
				} else {
					e_cal_backend_notify_object_removed (backend, id, old_object, NULL);
				}
			}

			g_free (old_object);
			g_free (new_object);
		} else {
			status = GNOME_Evolution_Calendar_ObjectNotFound;
		}
		break;

	default:
		status = GNOME_Evolution_Calendar_UnsupportedMethod;
		break;
	}

	e_cal_component_free_id (id);
	g_free (new_obj_str);

	return status;
}

static ECalBackendSyncStatus
do_receive_objects (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	icalcomponent            *icomp;
	icalcomponent_kind        kind;
	icalproperty_method       tmethod;
	gboolean                  online;
	GList                    *objects;
	GList                    *iter;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	status = check_state (cbdav, &online);
	if (status != GNOME_Evolution_Calendar_Success) {
		return status;
	}

	icomp = icalparser_parse_string (calobj);

	/* Try to parse cal object string */
	if (icomp == NULL) {
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
	status = extract_objects (icomp, kind, &objects);

	if (status != GNOME_Evolution_Calendar_Success) {
		icalcomponent_free (icomp);
		return status;
	}

	/* Extract optional timezone compnents */
	extract_timezones (cbdav, icomp);

	tmethod = icalcomponent_get_method (icomp);

	for (iter = objects; iter && status == GNOME_Evolution_Calendar_Success; iter = iter->next) {
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

		status = process_object (cbdav, ecomp, online, method);

		g_object_unref (ecomp);
	}

	g_list_free (objects);

	icalcomponent_free (icomp);

	return status;
}

#define caldav_busy_stub(_func_name, _params, _call_func, _call_params)	\
static ECalBackendSyncStatus						\
_func_name _params							\
{									\
	ECalBackendCalDAV        *cbdav;				\
	ECalBackendCalDAVPrivate *priv;					\
	ECalBackendSyncStatus     status;				\
	SlaveCommand		  old_slave_cmd;			\
	gboolean		  was_slave_busy;			\
									\
	cbdav = E_CAL_BACKEND_CALDAV (backend);				\
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);		\
									\
	/* this is done before locking */				\
	old_slave_cmd = priv->slave_cmd;				\
	was_slave_busy = priv->slave_busy;				\
	if (was_slave_busy) {						\
		/* let it pause its work and do our job */		\
		priv->slave_cmd = SLAVE_SHOULD_SLEEP;			\
	}								\
									\
	g_mutex_lock (priv->busy_lock);					\
	status = _call_func _call_params;				\
									\
	/* this is done before unlocking */				\
	if (was_slave_busy) {						\
		priv->slave_cmd = old_slave_cmd;			\
		g_cond_signal (priv->cond);				\
	}								\
									\
	g_mutex_unlock (priv->busy_lock);				\
									\
	return status;							\
}

caldav_busy_stub (
	caldav_create_object, (ECalBackendSync *backend, EDataCal *cal, gchar **calobj, gchar **uid),
	do_create_object, (cbdav, calobj, uid))

caldav_busy_stub (
	caldav_modify_object, (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj, CalObjModType mod, gchar **old_object, gchar **new_object),
	do_modify_object, (cbdav, calobj, mod, old_object, new_object))

caldav_busy_stub (
	caldav_remove_object, (ECalBackendSync *backend, EDataCal *cal, const gchar *uid, const gchar *rid, CalObjModType mod, gchar **old_object, gchar **object),
	do_remove_object, (cbdav, uid, rid, mod, old_object, object))

caldav_busy_stub (
	caldav_receive_objects, (ECalBackendSync *backend, EDataCal *cal, const gchar *calobj),
	do_receive_objects, (backend, cal, calobj))

static ECalBackendSyncStatus
caldav_discard_alarm (ECalBackendSync *backend,
		      EDataCal        *cal,
		      const gchar      *uid,
		      const gchar      *auid)
{
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_send_objects (ECalBackendSync  *backend,
		     EDataCal         *cal,
		     const gchar       *calobj,
		     GList           **users,
		     gchar            **modified_calobj)
{
	*users = NULL;
	*modified_calobj = g_strdup (calobj);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_default_object (ECalBackendSync  *backend,
			   EDataCal         *cal,
			   gchar            **object)
{
	ECalComponent *comp;

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
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_object (ECalBackendSync  *backend,
		   EDataCal         *cal,
		   const gchar       *uid,
		   const gchar       *rid,
		   gchar           **object)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	icalcomponent            *icalcomp;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	*object = NULL;
	icalcomp = get_comp_from_cache (cbdav, uid, rid, NULL, NULL);

	if (!icalcomp) {
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*object = icalcomponent_as_ical_string_r (icalcomp);
	icalcomponent_free (icalcomp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_add_timezone (ECalBackendSync *backend,
		     EDataCal        *cal,
		     const gchar      *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendCalDAV *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (backend);

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (cbdav), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);

		e_cal_backend_store_put_timezone (priv->store, zone);

		icaltimezone_free (zone, TRUE);
	} else {
		icalcomponent_free (tz_comp);
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_set_default_zone (ECalBackendSync *backend,
			     EDataCal        *cal,
			     const gchar      *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackendCalDAV *cbdav;
	ECalBackendCalDAVPrivate *priv;
	icaltimezone *zone;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (backend), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	if (priv->default_zone != icaltimezone_get_utc_timezone ())
		icaltimezone_free (priv->default_zone, 1);

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_object_list (ECalBackendSync  *backend,
			EDataCal         *cal,
			const gchar       *sexp_string,
			GList           **objects)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSExp	 *sexp;
	ECalBackend *bkend;
	gboolean                  do_search;
	GSList			 *list, *iter;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	sexp = e_cal_backend_sexp_new (sexp_string);

	if (sexp == NULL) {
		return GNOME_Evolution_Calendar_InvalidQuery;
	}

	if (g_str_equal (sexp_string, "#t")) {
		do_search = FALSE;
	} else {
		do_search = TRUE;
	}

	*objects = NULL;

	list = e_cal_backend_store_get_components (priv->store);

	bkend = E_CAL_BACKEND (backend);

	for (iter = list; iter; iter = g_slist_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search ||
		    e_cal_backend_sexp_match_comp (sexp, comp, bkend)) {
			*objects = g_list_prepend (*objects, e_cal_component_get_as_string (comp));
		}

		g_object_unref (comp);
	}

	g_object_unref (sexp);
	g_slist_free (list);

	return GNOME_Evolution_Calendar_Success;
}

static void
caldav_start_query (ECalBackend  *backend,
		    EDataCalView *query)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSExp	 *sexp;
	ECalBackend              *bkend;
	gboolean                  do_search;
	GSList			 *list, *iter;
	const gchar               *sexp_string;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	sexp_string = e_data_cal_view_get_text (query);
	sexp = e_cal_backend_sexp_new (sexp_string);

	/* FIXME:check invalid sexp */

	if (g_str_equal (sexp_string, "#t")) {
		do_search = FALSE;
	} else {
		do_search = TRUE;
	}

	list = e_cal_backend_store_get_components (priv->store);

	bkend = E_CAL_BACKEND (backend);

	for (iter = list; iter; iter = g_slist_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search ||
		    e_cal_backend_sexp_match_comp (sexp, comp, bkend)) {
			gchar *str = e_cal_component_get_as_string (comp);
			e_data_cal_view_notify_objects_added_1 (query, str);
			g_free (str);
		}

		g_object_unref (comp);
	}

	g_object_unref (sexp);
	g_slist_free (list);

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

static ECalBackendSyncStatus
caldav_get_free_busy (ECalBackendSync  *backend,
		      EDataCal         *cal,
		      GList            *users,
		      time_t            start,
		      time_t            end,
		      GList           **freebusy)
{
	ECalBackendCalDAV *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus status;
	icalcomponent *icalcomp;
	ECalComponent *comp;
	ECalComponentDateTime dt;
	struct icaltimetype dtvalue;
	icaltimezone *utc;
	gchar *str;
	GList *u;
	GSList *attendees = NULL, *to_free = NULL;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_return_val_if_fail (priv != NULL, GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (users != NULL, GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (freebusy != NULL, GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (start < end, GNOME_Evolution_Calendar_OtherError);

	if (!priv->calendar_schedule) {
		return GNOME_Evolution_Calendar_OtherError;
	}

	if (!priv->schedule_outbox_url) {
		caldav_receive_schedule_outbox_url (cbdav);
		if (!priv->schedule_outbox_url) {
			priv->calendar_schedule = FALSE;
			return GNOME_Evolution_Calendar_OtherError;
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

	if (priv->username) {
		ECalComponentOrganizer organizer = {NULL};

		organizer.value = priv->username;
		e_cal_component_set_organizer (comp, &organizer);
	}

	for (u = users; u; u = u->next) {
		ECalComponentAttendee *ca;
		gchar *temp = g_strconcat ("mailto:", (const gchar *)u->data, NULL);

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

	g_return_val_if_fail (str != NULL, GNOME_Evolution_Calendar_OtherError);

	status = caldav_post_freebusy (cbdav, priv->schedule_outbox_url, &str);

	if (status == GNOME_Evolution_Calendar_Success) {
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
				status = GNOME_Evolution_Calendar_OtherError;
			} else {
				gint i, n;

				n = xmlXPathNodeSetGetLength (result->nodesetval);
				for (i = 0; i < n; i++) {
					gchar *tmp;

					tmp = xp_object_get_string (xpath_eval (xpctx, "string(/C:schedule-response/C:response[%d]/C:calendar-data)", i + 1));
					if (tmp && *tmp) {
						GList *objects = NULL, *o;

						icalcomp = icalparser_parse_string (tmp);
						if (icalcomp && extract_objects (icalcomp, ICAL_VFREEBUSY_COMPONENT, &objects) == GNOME_Evolution_Calendar_Success) {
							for (o = objects; o; o = o->next) {
								gchar *obj_str = icalcomponent_as_ical_string_r (o->data);

								if (obj_str && *obj_str)
									*freebusy = g_list_append (*freebusy, obj_str);
								else
									g_free (obj_str);
							}
						}

						g_list_foreach (objects, (GFunc)icalcomponent_free, NULL);
						g_list_free (objects);

						if (icalcomp)
							icalcomponent_free (icalcomp);
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

	return status;
}

static ECalBackendSyncStatus
caldav_get_changes (ECalBackendSync  *backend,
		    EDataCal         *cal,
		    const gchar       *change_id,
		    GList           **adds,
		    GList           **modifies,
		    GList **deletes)
{
	/* FIXME: implement me! */
	g_warning ("function not implemented %s", G_STRFUNC);
	return GNOME_Evolution_Calendar_OtherError;
}

static gboolean
caldav_is_loaded (ECalBackend *backend)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	return priv->loaded;
}

static CalMode
caldav_get_mode (ECalBackend *backend)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	return priv->mode;
}

static void
caldav_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	/*g_mutex_lock (priv->busy_lock);*/

	/* We only support online and offline */
	if (mode != CAL_MODE_REMOTE &&
	    mode != CAL_MODE_LOCAL) {
		e_cal_backend_notify_mode (backend,
					   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
					   cal_mode_to_corba (mode));
		/*g_mutex_unlock (priv->busy_lock);*/
		return;
	}

	if (priv->mode == mode || !priv->loaded) {
		priv->mode = mode;
		e_cal_backend_notify_mode (backend,
					   GNOME_Evolution_Calendar_CalListener_MODE_SET,
					   cal_mode_to_corba (mode));
		/*g_mutex_unlock (priv->busy_lock);*/
		return;
	}

	priv->mode = mode;

	if (mode == CAL_MODE_REMOTE) {
		/* Wake up the slave thread */
		priv->slave_cmd = SLAVE_SHOULD_WORK;
		g_cond_signal (priv->cond);
	} else {
		soup_session_abort (priv->session);
		priv->slave_cmd = SLAVE_SHOULD_SLEEP;
	}

	e_cal_backend_notify_mode (backend,
				   GNOME_Evolution_Calendar_CalListener_MODE_SET,
				   cal_mode_to_corba (mode));

	/*g_mutex_unlock (priv->busy_lock);*/
}

static icaltimezone *
caldav_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackendCalDAV *cbdav;
	ECalBackendCalDAVPrivate *priv;

	g_return_val_if_fail (E_IS_CAL_BACKEND_CALDAV (backend), NULL);

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_return_val_if_fail (priv->default_zone != NULL, NULL);

	return priv->default_zone;
}

static icaltimezone *
caldav_internal_get_timezone (ECalBackend *backend,
			      const gchar *tzid)
{
	icaltimezone *zone;
	ECalBackendCalDAV *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	zone = NULL;

	if (priv->store)
		zone = (icaltimezone *) e_cal_backend_store_get_timezone (priv->store, tzid);

	if (!zone && E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone)
		zone = E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone (backend, tzid);

	return zone;
}

static void
caldav_source_changed_cb (ESource *source, ECalBackendCalDAV *cbdav)
{
	ECalBackendCalDAVPrivate *priv;
	SlaveCommand old_slave_cmd;
	gboolean old_slave_busy;

	g_return_if_fail (source != NULL);
	g_return_if_fail (cbdav != NULL);

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	g_return_if_fail (priv != NULL);

	old_slave_cmd = priv->slave_cmd;
	old_slave_busy = priv->slave_busy;
	if (old_slave_busy) {
		priv->slave_cmd = SLAVE_SHOULD_SLEEP;
		g_mutex_lock (priv->busy_lock);
	}

	initialize_backend (cbdav);

	/* always wakeup thread, even when it was sleeping */
	g_cond_signal (priv->cond);

	if (old_slave_busy) {
		priv->slave_cmd = old_slave_cmd;
		g_mutex_unlock (priv->busy_lock);
	}
}

/* ************************************************************************* */
/* ***************************** GObject Foo ******************************* */

G_DEFINE_TYPE (ECalBackendCalDAV, e_cal_backend_caldav, E_TYPE_CAL_BACKEND_SYNC)

static void
e_cal_backend_caldav_dispose (GObject *object)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ESource *source;

	cbdav = E_CAL_BACKEND_CALDAV (object);
	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	/* tell the slave to stop before acquiring a lock,
	   as it can work at the moment, and lock can be locked */
	priv->slave_cmd = SLAVE_SHOULD_DIE;

	g_mutex_lock (priv->busy_lock);

	if (priv->disposed) {
		g_mutex_unlock (priv->busy_lock);
		return;
	}

	source = e_cal_backend_get_source (E_CAL_BACKEND (cbdav));
	if (source)
		g_signal_handlers_disconnect_by_func (G_OBJECT (source), caldav_source_changed_cb, cbdav);

	/* stop the slave  */
	if (priv->synch_slave) {
		g_cond_signal (priv->cond);

		/* wait until the slave died */
		g_cond_wait (priv->slave_gone_cond, priv->busy_lock);
	}

	g_object_unref (priv->session);
	g_object_unref (priv->proxy);

	g_free (priv->username);
	g_free (priv->password);
	g_free (priv->uri);
	g_free (priv->schedule_outbox_url);

	if (priv->local_attachments_store) {
		g_free (priv->local_attachments_store);
		priv->local_attachments_store = NULL;
	}

	if (priv->store != NULL) {
		g_object_unref (priv->store);
	}

	priv->disposed = TRUE;
	g_mutex_unlock (priv->busy_lock);

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

static void
e_cal_backend_caldav_finalize (GObject *object)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (object);
	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_free (priv->busy_lock);
	g_cond_free (priv->cond);
	g_cond_free (priv->slave_gone_cond);

	if (priv->default_zone && priv->default_zone != icaltimezone_get_utc_timezone ()) {
		icaltimezone_free (priv->default_zone, 1);
	}
	priv->default_zone = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

static void
e_cal_backend_caldav_init (ECalBackendCalDAV *cbdav)
{
	ECalBackendCalDAVPrivate *priv;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	priv->session = soup_session_sync_new ();
	priv->proxy = e_proxy_new ();
	e_proxy_setup_proxy (priv->proxy);
	g_signal_connect (priv->proxy, "changed", G_CALLBACK (proxy_settings_changed), priv);

	if (G_UNLIKELY (caldav_debug_show (DEBUG_MESSAGE)))
		caldav_debug_setup (priv->session);

	priv->default_zone = icaltimezone_get_utc_timezone ();

	priv->disposed = FALSE;
	priv->loaded   = FALSE;

	/* Thinks the 'getctag' extension is available the first time, but unset it when realizes it isn't. */
	priv->ctag_supported = TRUE;
	priv->ctag_to_store = NULL;

	priv->schedule_outbox_url = NULL;

	priv->is_google = FALSE;

	priv->busy_lock = g_mutex_new ();
	priv->cond = g_cond_new ();
	priv->slave_gone_cond = g_cond_new ();

	/* Slave control ... */
	priv->slave_cmd = SLAVE_SHOULD_SLEEP;
	priv->slave_busy = FALSE;
	priv->refresh_time.tv_usec = 0;
	priv->refresh_time.tv_sec  = DEFAULT_REFRESH_TIME;

	g_signal_connect (priv->session, "authenticate",
			  G_CALLBACK (soup_authenticate), cbdav);

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cbdav), FALSE);
}

static void
e_cal_backend_caldav_class_init (ECalBackendCalDAVClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	caldav_debug_init ();

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);
	g_type_class_add_private (class, sizeof (ECalBackendCalDAVPrivate));

	object_class->dispose  = e_cal_backend_caldav_dispose;
	object_class->finalize = e_cal_backend_caldav_finalize;

	sync_class->is_read_only_sync            = caldav_is_read_only;
	sync_class->get_cal_address_sync         = caldav_get_cal_address;
	sync_class->get_alarm_email_address_sync = caldav_get_alarm_email_address;
	sync_class->get_ldap_attribute_sync      = caldav_get_ldap_attribute;
	sync_class->get_static_capabilities_sync = caldav_get_static_capabilities;

	sync_class->open_sync                    = caldav_do_open;
	sync_class->refresh_sync                 = caldav_refresh;
	sync_class->remove_sync                  = caldav_remove;

	sync_class->create_object_sync = caldav_create_object;
	sync_class->modify_object_sync = caldav_modify_object;
	sync_class->remove_object_sync = caldav_remove_object;

	sync_class->discard_alarm_sync        = caldav_discard_alarm;
	sync_class->receive_objects_sync      = caldav_receive_objects;
	sync_class->send_objects_sync         = caldav_send_objects;
	sync_class->get_default_object_sync   = caldav_get_default_object;
	sync_class->get_object_sync           = caldav_get_object;
	sync_class->get_object_list_sync      = caldav_get_object_list;
	sync_class->add_timezone_sync         = caldav_add_timezone;
	sync_class->set_default_zone_sync = caldav_set_default_zone;
	sync_class->get_freebusy_sync         = caldav_get_free_busy;
	sync_class->get_changes_sync          = caldav_get_changes;

	backend_class->is_loaded   = caldav_is_loaded;
	backend_class->start_query = caldav_start_query;
	backend_class->get_mode    = caldav_get_mode;
	backend_class->set_mode    = caldav_set_mode;

	backend_class->internal_get_default_timezone = caldav_internal_get_default_timezone;
	backend_class->internal_get_timezone         = caldav_internal_get_timezone;
}
