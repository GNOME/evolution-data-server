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
#include <glib/gi18n-lib.h>
#include "libedataserver/e-xml-hash-utils.h"
#include "libedataserver/e-proxy.h"
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-time-util.h>
#include <libedata-cal/e-cal-backend-cache.h>
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
	ECalBackendCache *cache;

	/* should we sync for offline mode? */
	gboolean do_offline;

	/* TRUE after caldav_open */
	gboolean loaded;

	/* lock to protect cache */
	GMutex *lock;

	/* cond to synch threads */
	GCond *cond;

	/* cond to know the slave gone */
	GCond *slave_gone_cond;

	/* BG synch thread */
	const GThread *synch_slave; /* just for a reference, whether thread exists */
	SlaveCommand slave_cmd;
	GTimeVal refresh_time;
	gboolean do_synch;

	/* The main soup session  */
	SoupSession *session;
	EProxy *proxy;

	/* well, guess what */
	gboolean read_only;

	/* whehter the synch function
	 * should report changes to the
	 * backend */
	gboolean report_changes;

	/* clandar uri */
	char *uri;

	/* Authentication info */
	char *username;
	char *password;
	gboolean need_auth;

	/* object cleanup */
	gboolean disposed;

	icaltimezone *default_zone;

	/* support for 'getctag' extension */
	gboolean ctag_supported;
	gchar *ctag;
};

/* ************************************************************************* */
/* Debugging */

#define DEBUG_MESSAGE "message"
#define DEBUG_MESSAGE_HEADER "message:header"
#define DEBUG_MESSAGE_BODY "message:body"
#define DEBUG_SERVER_ITEMS "items"

static gboolean caldav_debug_all = FALSE;
static GHashTable *caldav_debug_table = NULL;

static void
add_debug_key (const char *start, const char *end)
{
	char *debug_key;
	char *debug_value;

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
	const char *dbg;

	dbg = g_getenv ("CALDAV_DEBUG");

	if (dbg) {
		const char *ptr;

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
caldav_debug_show (const char *component)
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
static icaltimezone *caldav_internal_get_timezone (ECalBackend *backend, const char *tzid);

/* ************************************************************************* */
/* Misc. utility functions */
#define X_E_CALDAV "X-EVOLUTION-CALDAV-"

static void
icomp_x_prop_set (icalcomponent *comp, const char *key, const char *value)
{
	icalproperty *xprop;

	/* Find the old one first */
	xprop = icalcomponent_get_first_property (comp, ICAL_X_PROPERTY);

	while (xprop) {
		const char *str = icalproperty_get_x_name (xprop);

		if (!strcmp (str, key)) {
			icalcomponent_remove_property (comp, xprop);
			icalproperty_free (xprop);
			break;
		}

		xprop = icalcomponent_get_next_property (comp, ICAL_X_PROPERTY);
	}

	/* couldnt we be a bit smarter here and reuse the property? */

	xprop = icalproperty_new_x (value);
	icalproperty_set_x_name (xprop, key);
	icalcomponent_add_property (comp, xprop);
}


static char *
icomp_x_prop_get (icalcomponent *comp, const char *key)
{
	icalproperty *xprop;

	/* Find the old one first */
	xprop = icalcomponent_get_first_property (comp, ICAL_X_PROPERTY);

	while (xprop) {
		const char *str = icalproperty_get_x_name (xprop);

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


static void
e_cal_component_set_href (ECalComponent *comp, const char *href)
{
	icalcomponent *icomp;

	g_return_if_fail (href != NULL);

	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_if_fail (icomp != NULL);

	icomp_x_prop_set (icomp, X_E_CALDAV "HREF", href);
}

static char *
e_cal_component_get_href (ECalComponent *comp)
{
	icalcomponent *icomp;
	char          *str;

	str = NULL;
	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_val_if_fail (icomp != NULL, NULL);

	str =  icomp_x_prop_get (icomp, X_E_CALDAV "HREF");

	return str;
}


static void
e_cal_component_set_etag (ECalComponent *comp, const char *etag)
{
	icalcomponent *icomp;

	g_return_if_fail (etag != NULL);

	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_if_fail (icomp != NULL);

	icomp_x_prop_set (icomp, X_E_CALDAV "ETAG", etag);
}

static char *
e_cal_component_get_etag (ECalComponent *comp)
{
	icalcomponent *icomp;
	char          *str;

	str = NULL;
	icomp = e_cal_component_get_icalcomponent (comp);
	g_return_val_if_fail (icomp != NULL, NULL);

	str =  icomp_x_prop_get (icomp, X_E_CALDAV "ETAG");

	return str;
}

typedef enum {

	/* object is in synch,
	 * now isnt that ironic? :) */
	E_CAL_COMPONENT_IN_SYNCH = 0,

	/* local changes */
	E_CAL_COMPONENT_LOCALLY_CREATED,
	E_CAL_COMPONENT_LOCALLY_DELETED,
	E_CAL_COMPONENT_LOCALLY_MODIFIED,

} ECalComponentSyncState;

/* oos = out of synch */
static void
e_cal_component_set_synch_state (ECalComponent          *comp,
				 ECalComponentSyncState  state)
{
	icalcomponent *icomp;
	char          *state_string;

	icomp = e_cal_component_get_icalcomponent (comp);

	state_string = g_strdup_printf ("%d", state);

	icomp_x_prop_set (icomp, X_E_CALDAV "ETAG", state_string);

	g_free (state_string);
}


/* gen uid, set it internally and report it back so we can instantly
 * use it
 * and btw FIXME!!! */
static char *
e_cal_component_gen_href (ECalComponent *comp)
{
	char *href, *iso;

	icalcomponent *icomp;

	iso = isodate_from_time_t (time (NULL));

	href = g_strconcat (iso, ".ics", NULL);

	g_free (iso);

	icomp = e_cal_component_get_icalcomponent (comp);
	icomp_x_prop_set (icomp, X_E_CALDAV "HREF", href);

	return href;
}

/* ensure etag is quoted (to workaround potential server bugs) */
static char *
quote_etag (const char *etag)
{
	char *ret;

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

		if (! priv->do_offline) {
			return GNOME_Evolution_Calendar_RepositoryOffline;
		}

	} else {
		*online = TRUE;
	}

	return 	GNOME_Evolution_Calendar_Success;
}

/* ************************************************************************* */
/* XML Parsing code */

static xmlXPathObjectPtr
xpath_eval (xmlXPathContextPtr ctx, char *format, ...)
{
	xmlXPathObjectPtr  result;
	va_list            args;
	char              *expr;

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

		g_print ("No result\n");

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

	res = soup_headers_parse_status_line ((char *) content,
					      NULL,
					      status_code,
					      NULL);
	xmlFree (content);

	return res;
}
#endif

static char *
xp_object_get_string (xmlXPathObjectPtr result)
{
	char *ret = NULL;

	if (result == NULL)
		return ret;

	if (result->type == XPATH_STRING) {
		ret = g_strdup ((char *) result->stringval);
	}

	xmlXPathFreeObject (result);
	return ret;
}

/* as get_string but will normailze it (i.e. only take
 * the last part of the href) */
static char *
xp_object_get_href (xmlXPathObjectPtr result)
{
	char *ret = NULL;
	char *val;

	if (result == NULL)
		return ret;

	if (result->type == XPATH_STRING) {
		val = (char *) result->stringval;

		if ((ret = g_strrstr (val, "/")) == NULL) {
			ret = val;
		} else {
			ret++; /* skip the unwanted "/" */
		}

		ret = g_strdup (ret);

		if (caldav_debug_show (DEBUG_SERVER_ITEMS))
			printf ("CalDAV found href: %s\n", ret);
	}

	xmlXPathFreeObject (result);
	return ret;
}

/* like get_string but will quote the etag if necessary */
static char *
xp_object_get_etag (xmlXPathObjectPtr result)
{
	char *ret = NULL;
	char *str;

	if (result == NULL)
		return ret;

	if (result->type == XPATH_STRING) {
		str = (char *) result->stringval;

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
		res = soup_headers_parse_status_line ((char *) result->stringval,
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
static int
xp_object_get_number (xmlXPathObjectPtr result)
{
	int ret = -1;

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
#define XPATH_CALENDAR_DATA "string(/D:multistatus/D:response[%d]/C:calendar-data)"
#define XPATH_GETCTAG_STATUS "string(/D:multistatus/D:response/D:propstat/D:prop/CS:getctag/../../D:status)"
#define XPATH_GETCTAG "string(/D:multistatus/D:response/D:propstat/D:prop/CS:getctag)"

typedef struct _CalDAVObject CalDAVObject;

struct _CalDAVObject {

	char *href;
	char *etag;

	guint status;

	char *cdata;
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
parse_report_response (SoupMessage *soup_message, CalDAVObject **objs, int *len)
{
	xmlXPathContextPtr xpctx;
	xmlXPathObjectPtr  result;
	xmlDocPtr          doc;
	int                i, n;
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
		object->href = xp_object_get_href (xpres);

		xpres = xpath_eval (xpctx,XPATH_STATUS , i + 1);
		object->status = xp_object_get_status (xpres);

		//dump_xp_object (xpres);
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
		const char *new_loc;

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
send_and_handle_redirection (SoupSession *soup_session, SoupMessage *msg, char **new_location)
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

static char *
caldav_generate_uri (ECalBackendCalDAV *cbdav, const char *target)
{
	ECalBackendCalDAVPrivate  *priv;
	char *uri;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	/* priv->uri *have* trailing slash already */
	uri = g_strconcat (priv->uri, target, NULL);

	return uri;
}

static ECalBackendSyncStatus
caldav_server_open_calendar (ECalBackendCalDAV *cbdav)
{
	ECalBackendCalDAVPrivate  *priv;
	SoupMessage               *message;
	const char                *header;
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

	if (! SOUP_STATUS_IS_SUCCESSFUL (message->status_code)) {
		guint status_code = message->status_code;

		g_object_unref (message);

		return status_code_to_result (status_code, priv);
	}

	/* parse the dav header, we are intreseted in the
	 * calendar-access bit only at the moment */
	header = soup_message_headers_get (message->response_headers, "DAV");
	if (header)
		calendar_access = soup_header_contains (header, "calendar-access");
	else
		calendar_access = FALSE;

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
		priv->read_only = ! (put_allowed && delete_allowed);
		priv->do_synch = TRUE;
		return GNOME_Evolution_Calendar_Success;
	}

	return GNOME_Evolution_Calendar_NoSuchCal;
}

/* returns whether was able to read new ctag from the server's response */
static gboolean
parse_getctag_response (SoupMessage *message, gchar **new_ctag)
{
	xmlXPathContextPtr xpctx;
	xmlDocPtr          doc;
	gboolean           res = FALSE;

	g_return_val_if_fail (message != NULL, FALSE);
	g_return_val_if_fail (new_ctag != NULL, FALSE);

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
	xmlXPathRegisterNs (xpctx, (xmlChar *) "CS", (xmlChar *) "http://calendarserver.org/ns/");

	if (xp_object_get_status (xpath_eval (xpctx, XPATH_GETCTAG_STATUS)) == 200) {
		char *txt = xp_object_get_string (xpath_eval (xpctx, XPATH_GETCTAG));

		if (txt && *txt) {
			int len = strlen (txt);

			if (*txt == '\"' && len > 2 && txt [len - 1] == '\"') {
				/* dequote */
				*new_ctag = g_strndup (txt + 1, len - 2);
			} else {
				*new_ctag = txt;
				txt = NULL;
			}

			res = (*new_ctag) != NULL;
		}

		g_free (txt);
	}

	xmlXPathFreeContext (xpctx);
	xmlFreeDoc (doc);

	return res;
}

/* Returns whether calendar changed on the server. This works only when server
   supports 'getctag' extension. */
static gboolean
check_calendar_changed_on_server (ECalBackendCalDAV *cbdav)
{
	ECalBackendCalDAVPrivate *priv;
	xmlOutputBufferPtr   	  buf;
	SoupMessage              *message;
	xmlDocPtr		  doc;
	xmlNodePtr           	  root, node;
	xmlNsPtr		  ns, nsdav;
	gboolean 		  result = TRUE;

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
				  (char *) buf->buffer->content,
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
		char *ctag = NULL;

		if (parse_getctag_response (message, &ctag)) {
			if (ctag && priv->ctag && g_str_equal (ctag, priv->ctag)) {
				/* ctag is same, no change in the calendar */
				result = FALSE;
				g_free (ctag);
			} else {
				g_free (priv->ctag);
				priv->ctag = ctag;
			}
		} else {
			priv->ctag_supported = FALSE;
		}
	}

	g_object_unref (message);

	return result;
}

static gboolean
caldav_server_list_objects (ECalBackendCalDAV *cbdav, CalDAVObject **objs, int *len)
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
	root = xmlNewDocNode (doc, NULL, (xmlChar *) "calendar-query", NULL);
	nscd = xmlNewNs (root, (xmlChar *) "urn:ietf:params:xml:ns:caldav", (xmlChar *) "C");
	xmlSetNs (root, nscd);
	xmlDocSetRootElement (doc, root);

	/* Add webdav tags */
	nsdav = xmlNewNs (root, (xmlChar *) "DAV:", (xmlChar *) "D");
	node = xmlNewTextChild (root, nsdav, (xmlChar *) "prop", NULL);
	xmlNewTextChild (node, nsdav, (xmlChar *) "getetag", NULL);

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
	/* ^^^ add timerange for performance?  */


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
				  (char *) buf->buffer->content,
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
	const char               *hdr;
	char                     *uri;

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
caldav_server_put_object (ECalBackendCalDAV *cbdav, CalDAVObject *object)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     result;
	SoupMessage              *message;
	const char               *hdr;
	char                     *uri;

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
		char *file = strrchr (uri, '/');

		/* there was a redirect, update href properly */
		if (file) {
			g_free (object->href);
			object->href = soup_uri_encode (file + 1, NULL);
		}

		g_free (uri);
	}

	/* FIXME: sepcial case precondition errors ?*/
	result = status_code_to_result (message->status_code, priv);

	if (result == GNOME_Evolution_Calendar_Success) {
		hdr = soup_message_headers_get (message->response_headers, "ETag");
		if (hdr != NULL) {
			g_free (object->etag);
			object->etag = quote_etag (hdr);
		} else {
			/* no ETag header returned, check for it with a GET */
			hdr = soup_message_headers_get (message->response_headers, "Location");
			if (hdr) {
				/* reflect possible href change first */
				char *file = strrchr (hdr, '/');

				if (file) {
					g_free (object->href);
					object->href = soup_uri_encode (file + 1, NULL);
				}
			}

			result = caldav_server_get_object (cbdav, object);
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
	char                     *uri;

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

/* ************************************************************************* */
/* Synchronization foo */

static gboolean
synchronize_object (ECalBackendCalDAV *cbdav,
		    CalDAVObject      *object,
		    ECalComponent     *old_comp,
		    GList            **created,
		    GList            **modified)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     result;
	ECalBackend              *bkend;
	ECalComponent            *comp;
	icalcomponent 		 *icomp, *subcomp;
	icalcomponent_kind        kind;
	gboolean                  res;

	comp = NULL;
	res  = TRUE;
	result  = caldav_server_get_object (cbdav, object);

	if (result != GNOME_Evolution_Calendar_Success)
		return FALSE;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	icomp = icalparser_parse_string (object->cdata);
	kind  = icalcomponent_isa (icomp);
	bkend = E_CAL_BACKEND (cbdav);

	if (kind == ICAL_VCALENDAR_COMPONENT) {
		kind = e_cal_backend_get_kind (bkend);
		subcomp = icalcomponent_get_first_component (icomp, kind);

		if (!subcomp) {
			res = FALSE;
		} else {
			comp = e_cal_component_new ();
			res = e_cal_component_set_icalcomponent (comp,
						   icalcomponent_new_clone (subcomp));
			if (res) {
				icaltimezone *zone = icaltimezone_new ();

				e_cal_component_set_href (comp, object->href);
				e_cal_component_set_etag (comp, object->etag);

				for (subcomp = icalcomponent_get_first_component (icomp, ICAL_VTIMEZONE_COMPONENT);
				    subcomp;
				    subcomp = icalcomponent_get_next_component (icomp, ICAL_VTIMEZONE_COMPONENT)) {
					/* copy timezones of the component to our cache to have it available later */
					if (icaltimezone_set_component (zone, subcomp))
						e_cal_backend_cache_put_timezone (priv->cache, zone);
				}

				icaltimezone_free (zone, TRUE);
			} else {
				g_object_unref (comp);
				comp = NULL;
			}
		}
	} else {
		res = FALSE;
	}

	icalcomponent_free (icomp);

	if (!res) {
		return res;
	}

	if (priv->report_changes) {
		if (old_comp == NULL) {
			*created = g_list_prepend (*created, g_object_ref (comp));
		} else {
			/* they will be in the opposite order in the list */
			*modified = g_list_prepend (*modified, g_object_ref (old_comp));
			*modified = g_list_prepend (*modified, g_object_ref (comp));
		}
	}

	g_object_unref (comp);

	return res;
}

#define etags_match(_tag1, _tag2) ((_tag1 == _tag2) ? TRUE :                 \
				   g_str_equal (_tag1 != NULL ? _tag1 : "",  \
					        _tag2 != NULL ? _tag2 : ""))

static void
synchronize_cache (ECalBackendCalDAV *cbdav)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackend              *bkend;
	ECalBackendCache         *bcache;
	CalDAVObject             *sobjs;
	CalDAVObject             *object;
	GHashTable               *hindex;
	GList                    *cobjs, *created = NULL, *modified = NULL;
	GList                    *citer;
	gboolean                  res;
	int			  len;
	int                       i;

	priv   = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	bkend  = E_CAL_BACKEND (cbdav);
	bcache = priv->cache;
	len    = 0;
	sobjs  = NULL;

	if (!check_calendar_changed_on_server (cbdav)) {
		/* no changes on the server, no update required */
		return;
	}

	res = caldav_server_list_objects (cbdav, &sobjs, &len);

	if (!res)
		return;

	hindex = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
	cobjs = e_cal_backend_cache_get_components (bcache);

	/* build up a index for the href entry */
	for (citer = cobjs; citer; citer = g_list_next (citer)) {
		ECalComponent *ccomp = E_CAL_COMPONENT (citer->data);
		char *href;

		href = e_cal_component_get_href (ccomp);

		if (href == NULL) {
			g_warning ("href of object NULL :(");
			continue;
		}

		g_hash_table_insert (hindex, (gpointer) href, ccomp);
	}

	/* see if we have to upate or add some objects */
	for (i = 0, object = sobjs; i < len; i++, object++) {
		ECalComponent *ccomp;
		char *etag = NULL;

		if (object->status != 200) {
			/* just continue here, so that the object
			 * doesnt get removed from the cobjs list
			 * - therefore it will be removed */
			continue;
		}

		res = TRUE;
		ccomp = g_hash_table_lookup (hindex, object->href);

		if (ccomp != NULL) {
			etag = e_cal_component_get_etag (ccomp);
		}

		if (!etag || !etags_match (etag, object->etag)) {
			res = synchronize_object (cbdav, object, ccomp, &created, &modified);
		}

		if (res) {
			cobjs = g_list_remove (cobjs, ccomp);
			if (ccomp)
				g_object_unref (ccomp);
		}

		caldav_object_free (object, FALSE);
		g_free (etag);
	}

	/* remove old (not on server anymore) items from cache... */
	for (citer = cobjs; citer; citer = g_list_next (citer)) {
		ECalComponent *comp;
		const char *uid = NULL;

		comp = E_CAL_COMPONENT (citer->data);
		e_cal_component_get_uid (comp, &uid);

		if (e_cal_backend_cache_remove_component (bcache, uid, NULL) &&
		    priv->report_changes) {
			char *str = e_cal_component_get_as_string (comp);
			ECalComponentId *id = e_cal_component_get_id (comp);

			e_cal_backend_notify_object_removed (E_CAL_BACKEND (cbdav),
							     id, str, NULL);
			e_cal_component_free_id (id);
			g_free (str);
		}

		g_object_unref (comp);
	}

	/* ...then notify created and modified components */
	for (citer = created; citer; citer = citer->next) {
		ECalComponent *comp = citer->data;
		char *comp_str = e_cal_component_get_as_string (comp);

		if (e_cal_backend_cache_put_component (priv->cache, comp))
			e_cal_backend_notify_object_created (bkend, comp_str);

		g_free (comp_str);
		g_object_unref (comp);
	}

	for (citer = modified; citer; citer = citer->next) {
		ECalComponent *comp, *old_comp;
		char *new_str, *old_str;

		/* always even number of items in the 'modified' list */
		comp = citer->data;
		citer = citer->next;
		old_comp = citer->data;

		new_str = e_cal_component_get_as_string (comp);
		old_str = e_cal_component_get_as_string (old_comp);

		if (e_cal_backend_cache_put_component (priv->cache, comp))
			e_cal_backend_notify_object_modified (bkend, old_str, new_str);

		g_free (new_str);
		g_free (old_str);
		g_object_unref (comp);
		g_object_unref (old_comp);
	}

	g_hash_table_destroy (hindex);
	g_list_free (cobjs);
	g_list_free (created);
	g_list_free (modified);
}

/* ************************************************************************* */
static gpointer
synch_slave_loop (gpointer data)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendCalDAV        *cbdav;

	cbdav = E_CAL_BACKEND_CALDAV (data);
	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_lock (priv->lock);

	while (priv->slave_cmd != SLAVE_SHOULD_DIE) {
		GTimeVal alarm_clock;
		if (priv->slave_cmd == SLAVE_SHOULD_SLEEP) {
			/* just sleep until we get woken up again */
			g_cond_wait (priv->cond, priv->lock);

			/* check if we should die, work or sleep again */
			continue;
		}

		/* Ok here we go, do some real work
		 * Synch it baby one more time ...
		 */
		synchronize_cache (cbdav);

		/* puhh that was hard, get some rest :) */
		g_get_current_time (&alarm_clock);
		alarm_clock.tv_sec += priv->refresh_time.tv_sec;
		g_cond_timed_wait (priv->cond,
				   priv->lock,
				   &alarm_clock);

	}

	/* signal we are done */
	g_cond_signal (priv->slave_gone_cond);

	priv->synch_slave = NULL;

	/* we got killed ... */
	g_mutex_unlock (priv->lock);
	return NULL;
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
			char            **address)
{
	*address = NULL;
	return GNOME_Evolution_Calendar_Success;
}



static ECalBackendSyncStatus
caldav_get_ldap_attribute (ECalBackendSync  *backend,
			   EDataCal         *cal,
			   char           **attribute)
{
	*attribute = NULL;
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_alarm_email_address (ECalBackendSync  *backend,
				EDataCal         *cal,
				char            **address)
{
	*address = NULL;
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_static_capabilities (ECalBackendSync  *backend,
				EDataCal         *cal,
				char            **capabilities)
{
	*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","
				  CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
				  CAL_STATIC_CAPABILITY_NO_THISANDPRIOR);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
initialize_backend (ECalBackendCalDAV *cbdav)
{
	ECalBackendSyncStatus     result;
	ECalBackendCalDAVPrivate *priv;
	ESource                  *source;
	const char		 *os_val;
	const char               *uri;
	gsize                     len;
	const char               *refresh;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	result = GNOME_Evolution_Calendar_Success;
	source = e_cal_backend_get_source (E_CAL_BACKEND (cbdav));

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
		const char *proto;

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
		char *p = strstr (priv->uri, "://");
		char *tmp, *old = priv->uri;

		/* properly encode uri */
		tmp = soup_uri_encode (p ? p + 3 : priv->uri, NULL);

		priv->uri = soup_uri_normalize (tmp, "/");
		g_free (tmp);

		if (p) {
			/* prepend protocol */
			tmp = priv->uri;
			p [3] = 0;

			priv->uri = g_strconcat (old, tmp, NULL);
			g_free (tmp);
		}

		g_free (old);
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
		char *tmp = priv->uri;

		priv->uri = g_strconcat (priv->uri, "/", NULL);

		g_free (tmp);
	}

	if (priv->cache == NULL) {
		ECalSourceType source_type;

		switch (e_cal_backend_get_kind (E_CAL_BACKEND (cbdav))) {
			default:
			case ICAL_VEVENT_COMPONENT:
				source_type = E_CAL_SOURCE_TYPE_EVENT;
				break;
			case ICAL_VTODO_COMPONENT:
				source_type = E_CAL_SOURCE_TYPE_TODO;
				break;
			case ICAL_VJOURNAL_COMPONENT:
				source_type = E_CAL_SOURCE_TYPE_JOURNAL;
				break;
		}

		priv->cache = e_cal_backend_cache_new (priv->uri, source_type);

		if (priv->cache == NULL) {
			result = GNOME_Evolution_Calendar_OtherError;
			goto out;
		}

	}

	refresh = e_source_get_property (source, "refresh");
	priv->refresh_time.tv_sec  = (refresh && atoi (refresh) > 0) ? (60 * atoi (refresh)) : (DEFAULT_REFRESH_TIME);

	if (!priv->synch_slave) {
		GThread *slave;

		priv->slave_cmd = SLAVE_SHOULD_SLEEP;
		slave = g_thread_create (synch_slave_loop, cbdav, FALSE, NULL);

		if (slave == NULL) {
			g_warning ("Could not create synch slave");
			result = GNOME_Evolution_Calendar_OtherError;
		}

		priv->report_changes = TRUE;
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

static ECalBackendSyncStatus
caldav_do_open (ECalBackendSync *backend,
		EDataCal        *cal,
		gboolean         only_if_exists,
		const char      *username,
		const char      *password)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	status = GNOME_Evolution_Calendar_Success;

	g_mutex_lock (priv->lock);

	/* let it decide the 'getctag' extension availability again */
	g_free (priv->ctag);
	priv->ctag = NULL;
	priv->ctag_supported = TRUE;

	if (!priv->loaded) {
		status = initialize_backend (cbdav);
	}

	if (status != GNOME_Evolution_Calendar_Success) {
		g_mutex_unlock (priv->lock);
		return status;
	}

	if (priv->need_auth) {
		if ((username == NULL || password == NULL)) {
			g_mutex_unlock (priv->lock);
			return GNOME_Evolution_Calendar_AuthenticationRequired;
		}

		g_free (priv->username);
		priv->username = g_strdup (username);
		g_free (priv->password);
		priv->password = g_strdup (password);
	}

	if (!priv->do_offline && priv->mode == CAL_MODE_LOCAL) {
		g_mutex_unlock (priv->lock);
		return GNOME_Evolution_Calendar_RepositoryOffline;
	}

	priv->loaded = TRUE;

	if (priv->mode == CAL_MODE_REMOTE) {
		/* set forward proxy */
		proxy_settings_changed (priv->proxy, priv);

		status = caldav_server_open_calendar (cbdav);

		if (status == GNOME_Evolution_Calendar_Success) {
			priv->slave_cmd = SLAVE_SHOULD_WORK;
			g_cond_signal (priv->cond);
		}
	} else {
		priv->read_only = TRUE;
	}

	g_mutex_unlock (priv->lock);

	return status;
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

	g_mutex_lock (priv->lock);

	if (!priv->loaded) {
		g_mutex_unlock (priv->lock);
		return GNOME_Evolution_Calendar_Success;
	}

	status = check_state (cbdav, &online);

	/* lie here a bit, but otherwise the calendar will not be removed, even it should */
	if (status != GNOME_Evolution_Calendar_Success)
		g_print (G_STRLOC ": %s", e_cal_backend_status_to_string (status));

	e_file_cache_remove (E_FILE_CACHE (priv->cache));
	priv->cache  = NULL;
	priv->loaded = FALSE;
	priv->slave_cmd = SLAVE_SHOULD_DIE;

	if (priv->synch_slave) {
		g_cond_signal (priv->cond);

		/* wait until the slave died */
		g_cond_wait (priv->slave_gone_cond, priv->lock);
	}

	g_mutex_unlock (priv->lock);

	return GNOME_Evolution_Calendar_Success;
}


static char *
pack_cobj (ECalBackendCalDAV *cbdav, ECalComponent *ecomp)
{
	ECalBackendCalDAVPrivate *priv;
	icalcomponent *calcomp;
	icalcomponent *icomp;
	char          *objstr;

	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	icomp = e_cal_component_get_icalcomponent (ecomp);

	if (icalcomponent_isa (icomp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *cclone;

		calcomp = e_cal_util_new_top_level ();
		cclone = icalcomponent_new_clone (icomp);
		icalcomponent_add_component (calcomp, cclone);
		e_cal_util_add_timezones_from_component(calcomp,
							cclone);
	} else {
		calcomp = icalcomponent_new_clone (icomp);
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
			g_free ((char *)dt.tzid);
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
			g_free ((char *)dt.tzid);
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
			g_free ((char *)dt.tzid);
			dt.tzid = g_strdup (icaltimezone_get_tzid (default_zone));
			e_cal_component_set_due (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);
	e_cal_component_abort_sequence (comp);
}

static ECalBackendSyncStatus
caldav_create_object (ECalBackendSync  *backend,
		      EDataCal         *cal,
		      char            **calobj,
		      char            **uid)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	ECalComponent            *comp;
	gboolean                  online;
	char                     *href;
	struct icaltimetype current;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_lock (priv->lock);

	status = check_state (cbdav, &online);

	if (status != GNOME_Evolution_Calendar_Success) {
		g_mutex_unlock (priv->lock);
		return status;
	}

	comp = e_cal_component_new_from_string (*calobj);

	if (comp == NULL) {
		g_mutex_unlock (priv->lock);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* Set the created and last modified times on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (comp, &current);
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component*/
	sanitize_component ((ECalBackend *)cbdav, comp);

	if (online) {
		CalDAVObject object;

		href = e_cal_component_gen_href (comp);
		
		object.href  = href;
		object.etag  = NULL;
		object.cdata = pack_cobj (cbdav, comp);

		status = caldav_server_put_object (cbdav, &object);
		if (status == GNOME_Evolution_Calendar_Success) {
			e_cal_component_set_href (comp, object.href);
			e_cal_component_set_etag (comp, object.etag);
		}

		caldav_object_free (&object, FALSE);
	} else {
		/* mark component as out of synch */
		e_cal_component_set_synch_state (comp,
				E_CAL_COMPONENT_LOCALLY_CREATED);
	}

	if (status != GNOME_Evolution_Calendar_Success) {
		g_object_unref (comp);
		g_mutex_unlock (priv->lock);
		return status;
	}

	/* We should prolly check for cache errors
	 * but when that happens we are kinda hosed anyway */
	e_cal_backend_cache_put_component (priv->cache, comp);
	*calobj = e_cal_component_get_as_string (comp);

	g_mutex_unlock (priv->lock);

	return status;
}

static ECalBackendSyncStatus
caldav_modify_object (ECalBackendSync  *backend,
		      EDataCal         *cal,
		      const char       *calobj,
		      CalObjModType     mod,
		      char            **old_object,
		      char            **new_object)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	ECalComponent            *comp;
	ECalComponent            *cache_comp;
	gboolean                  online;
	const char		 *uid = NULL;
	struct icaltimetype current;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_lock (priv->lock);

	status = check_state (cbdav, &online);

	if (status != GNOME_Evolution_Calendar_Success) {
		g_mutex_unlock (priv->lock);
		return status;
	}

	comp = e_cal_component_new_from_string (calobj);

	if (comp == NULL) {
		g_mutex_unlock (priv->lock);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	/* Set the last modified time on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component*/
	sanitize_component ((ECalBackend *)cbdav, comp);

	e_cal_component_get_uid (comp, &uid);

	cache_comp = e_cal_backend_cache_get_component (priv->cache, uid, NULL);
	if (cache_comp == NULL) {
		g_mutex_unlock (priv->lock);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	if (online) {
		CalDAVObject object;

		object.href  = e_cal_component_get_href (cache_comp);
		object.etag  = e_cal_component_get_etag (cache_comp);
		object.cdata = pack_cobj (cbdav, comp);

		status = caldav_server_put_object (cbdav, &object);
		if (status == GNOME_Evolution_Calendar_Success) {
			e_cal_component_set_href (comp, object.href);
			e_cal_component_set_etag (comp, object.etag);
		}

		caldav_object_free (&object, FALSE);
	} else {
		/* mark component as out of synch */
		e_cal_component_set_synch_state (comp,
				E_CAL_COMPONENT_LOCALLY_MODIFIED);
	}

	if (status != GNOME_Evolution_Calendar_Success) {
		g_object_unref (comp);
		g_mutex_unlock (priv->lock);
		return status;
	}

	/* We should prolly check for cache errors
	 * but when that happens we are kinda hosed anyway */
	e_cal_backend_cache_put_component (priv->cache, comp);
	*old_object = e_cal_component_get_as_string (cache_comp);
	*new_object = e_cal_component_get_as_string (comp);

	g_mutex_unlock (priv->lock);

	return status;
}

static ECalBackendSyncStatus
caldav_remove_object (ECalBackendSync  *backend,
		      EDataCal         *cal,
		      const char       *uid,
		      const char       *rid,
		      CalObjModType     mod,
		      char            **old_object,
		      char            **object)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	ECalComponent            *cache_comp;
	gboolean                  online;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_lock (priv->lock);

	status = check_state (cbdav, &online);

	if (status != GNOME_Evolution_Calendar_Success) {
		g_mutex_unlock (priv->lock);
		return status;
	}

	cache_comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);

	if (cache_comp == NULL && rid && *rid) {
		/* we do not have this instance in cache directly, thus try to get master object */
		cache_comp = e_cal_backend_cache_get_component (priv->cache, uid, "");
	}

	if (cache_comp == NULL) {
		g_mutex_unlock (priv->lock);
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*old_object = e_cal_component_get_as_string (cache_comp);

	if (mod == CALOBJ_MOD_THIS && rid && *rid)
		e_cal_util_remove_instances (e_cal_component_get_icalcomponent (cache_comp), icaltime_from_string (rid), mod);

	if (online) {
		CalDAVObject caldav_object;

		caldav_object.href  = e_cal_component_get_href (cache_comp);
		caldav_object.etag  = e_cal_component_get_etag (cache_comp);
		caldav_object.cdata = NULL;

		if (mod == CALOBJ_MOD_THIS && rid && *rid) {
			caldav_object.cdata = pack_cobj (cbdav, cache_comp);

			status = caldav_server_put_object (cbdav, &caldav_object);
			if (status == GNOME_Evolution_Calendar_Success) {
				e_cal_component_set_href (cache_comp, caldav_object.href);
				e_cal_component_set_etag (cache_comp, caldav_object.etag);
			}
		} else
			status = caldav_server_delete_object (cbdav, &caldav_object);

		caldav_object_free (&caldav_object, FALSE);
	} else {
		/* mark component as out of synch */
		if (mod == CALOBJ_MOD_THIS && rid && *rid)
			e_cal_component_set_synch_state (cache_comp, E_CAL_COMPONENT_LOCALLY_MODIFIED);
		else
			e_cal_component_set_synch_state (cache_comp, E_CAL_COMPONENT_LOCALLY_DELETED);
	}

	if (status != GNOME_Evolution_Calendar_Success) {
		g_mutex_unlock (priv->lock);
		return status;
	}

	/* We should prolly check for cache errors
	 * but when that happens we are kinda hosed anyway */
	if (mod == CALOBJ_MOD_THIS && rid && *rid) {
		e_cal_backend_cache_put_component (priv->cache, cache_comp);
		*object = e_cal_component_get_as_string (cache_comp);
	} else
		e_cal_backend_cache_remove_component (priv->cache, uid, rid);

	g_mutex_unlock (priv->lock);

	return status;
}

static ECalBackendSyncStatus
caldav_discard_alarm (ECalBackendSync *backend,
		      EDataCal        *cal,
		      const char      *uid,
		      const char      *auid)
{
	return GNOME_Evolution_Calendar_Success;
}

/* FIXME: use list here? */
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

#define is_error(__status) (__status != GNOME_Evolution_Calendar_Success)

static ECalBackendSyncStatus
process_object (ECalBackendCalDAV   *cbdav,
		ECalComponent       *ecomp,
		gboolean             online,
		icalproperty_method  method)
{
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	ECalBackend              *backend;
	ECalComponent            *ccomp;
	struct icaltimetype       now;
	ECalComponentId          *id;
	const char               *uid;
	char                     *rid;
	char                     *ostr;
	char                     *oostr;
	gboolean                  is_declined;

	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);
	backend = E_CAL_BACKEND (cbdav);

	/* ctime, mtime */
	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (ecomp, &now);
	e_cal_component_set_last_modified (ecomp, &now);

	e_cal_component_get_uid (ecomp, &uid);
	rid = e_cal_component_get_recurid_as_string (ecomp);

	ccomp = e_cal_backend_cache_get_component (priv->cache, uid, NULL);

	if (ccomp != NULL) {
		oostr = e_cal_component_get_as_string (ccomp);
	} else {
		oostr = NULL;
	}

	ostr = e_cal_component_get_as_string (ecomp);

	status = GNOME_Evolution_Calendar_Success;

	switch (method) {

		case ICAL_METHOD_PUBLISH:
		case ICAL_METHOD_REQUEST:
		case ICAL_METHOD_REPLY:

		is_declined = e_cal_backend_user_declined (e_cal_component_get_icalcomponent (ecomp));
		if (online) {
			CalDAVObject object = { NULL, };

			if (ccomp) {
				char *href;
				char *etag;

				href = e_cal_component_get_href (ccomp);
				etag = e_cal_component_get_etag (ccomp);

				object.href  = href;
				object.etag  = etag;

			} else if (!is_declined) {
				object.href = e_cal_component_gen_href (ecomp);
			}

			if (!is_declined || ccomp) {
				if (!is_declined) {
					object.cdata = pack_cobj (cbdav, ecomp);
					status = caldav_server_put_object (cbdav, &object);

					if (status == GNOME_Evolution_Calendar_Success) {
						e_cal_component_set_href (ecomp, object.href);
						e_cal_component_set_etag (ecomp, object.etag);
					}
				} else {
					object.cdata = NULL;
					status = caldav_server_delete_object (cbdav, &object);
				}
				caldav_object_free (&object, FALSE);
			}
		} else {
			ECalComponentSyncState sstate = E_CAL_COMPONENT_IN_SYNCH;

			if (ccomp) {
				if (!is_declined)
					sstate = E_CAL_COMPONENT_LOCALLY_MODIFIED;
				else
					sstate = E_CAL_COMPONENT_LOCALLY_DELETED;
			} else if (!is_declined) {
				sstate = E_CAL_COMPONENT_LOCALLY_CREATED;
			}

			e_cal_component_set_synch_state (ecomp, sstate);

		}

		if (status != GNOME_Evolution_Calendar_Success) {
			break;
		}

		if (!is_declined)
			e_cal_backend_cache_put_component (priv->cache, ecomp);
		else
			e_cal_backend_cache_remove_component (priv->cache, uid, rid);

		if (ccomp) {
			if (!is_declined)
				e_cal_backend_notify_object_modified (backend, ostr, oostr);
			else {
				id = e_cal_component_get_id (ccomp);
				e_cal_backend_notify_object_removed (E_CAL_BACKEND (backend), id, oostr, NULL);
				e_cal_component_free_id (id);
			}
		} else if (!is_declined) {
			e_cal_backend_notify_object_created (backend, ostr);
		}

		break;


		case ICAL_METHOD_CANCEL:

			if (ccomp == NULL) {
				status = GNOME_Evolution_Calendar_ObjectNotFound;
				break;
			}

			/* FIXME: this is not working for instances
			 * of recurring appointments - yet - */
			if (online) {
				CalDAVObject object;
				char *href;
				char *etag;

				href = e_cal_component_get_href (ccomp);
				etag = e_cal_component_get_etag (ccomp);

				object.href  = href;
				object.etag  = etag;
				object.cdata = NULL;

				status = caldav_server_delete_object (cbdav,
								      &object);

				caldav_object_free (&object, FALSE);

			} else {
				/* mark component as out of synch */
				e_cal_component_set_synch_state (ecomp,
				E_CAL_COMPONENT_LOCALLY_DELETED);

			}

			if (status != GNOME_Evolution_Calendar_Success) {
				break;
			}

			e_cal_backend_cache_remove_component (priv->cache,
							      uid,
							      rid);

			id = e_cal_component_get_id (ccomp);
			e_cal_backend_notify_object_removed (E_CAL_BACKEND (backend),
							     id,
							     oostr,
							     ostr);
			e_cal_component_free_id (id);
			break;

		default:
			/* WTF ? */
			status = GNOME_Evolution_Calendar_UnsupportedMethod;
			break;
	}

	g_free (ostr);
	g_free (oostr);
	g_free (rid);

	if (ccomp) {
		g_object_unref (ccomp);
	}

	return status;
}

static ECalBackendSyncStatus
caldav_receive_objects (ECalBackendSync *backend,
			EDataCal        *cal,
			const char      *calobj)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSyncStatus     status;
	icalcomponent            *icomp;
	icalcomponent_kind        kind;
	icalproperty_method       tmethod;
	gboolean                  online;
	GList                    *timezones = NULL;
	GList                    *objects;
	GList                    *iter;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	icomp = icalparser_parse_string (calobj);

	/* Try to parse cal object string */
	if (icomp == NULL) {
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
	status = extract_objects (icomp, kind, &objects);

	if (status != GNOME_Evolution_Calendar_Success) {
		return status;
	}

	/* Extract optional timezone compnents */
	kind = ICAL_VTIMEZONE_COMPONENT;
	status = extract_objects (icomp, kind, &timezones);

	if (status == GNOME_Evolution_Calendar_Success) {
		for (iter = timezones; iter; iter = iter->next) {
			icaltimezone *zone = icaltimezone_new ();

			if (icaltimezone_set_component (zone, iter->data))
				e_cal_backend_cache_put_timezone (priv->cache, zone);
			else
				icalcomponent_free (iter->data);

			icaltimezone_free (zone, TRUE);
		}
	}

	/*   */
	g_mutex_lock (priv->lock);

	status = check_state (cbdav, &online);

	if (status != GNOME_Evolution_Calendar_Success) {
		/* FIXME: free components here */
		g_mutex_unlock (priv->lock);
		return status;
	}

	tmethod = icalcomponent_get_method (icomp);

	for (iter = objects; iter && ! is_error (status); iter = iter->next) {
		icalcomponent       *scomp;
		ECalComponent       *ecomp;
		icalproperty_method  method;

		scomp = (icalcomponent *) iter->data;
		ecomp = e_cal_component_new ();

		e_cal_component_set_icalcomponent (ecomp, scomp);

		if (icalcomponent_get_first_property (scomp,
						      ICAL_METHOD_PROPERTY)) {

			method = icalcomponent_get_method (scomp);
		} else {
			method = tmethod;
		}

		status = process_object (cbdav, ecomp, online, method);

		g_object_unref (ecomp);
	}

	g_list_free (objects);
	g_list_free (timezones);

	g_mutex_unlock (priv->lock);

	return status;
}

static ECalBackendSyncStatus
caldav_send_objects (ECalBackendSync  *backend,
		     EDataCal         *cal,
		     const char       *calobj,
		     GList           **users,
		     char            **modified_calobj)
{
	*users = NULL;
	*modified_calobj = g_strdup (calobj);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_default_object (ECalBackendSync  *backend,
			   EDataCal         *cal,
			   char            **object)
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
		   const char       *uid,
		   const char       *rid,
		   char           **object)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalComponent            *comp;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_lock (priv->lock);
	comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
	g_mutex_unlock (priv->lock);

	if (comp == NULL) {
		*object = NULL;
		return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_get_timezone (ECalBackendSync  *backend,
		     EDataCal         *cal,
		     const char       *tzid,
		     char            **object)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	const icaltimezone       *zone;
	icalcomponent            *icalcomp;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_return_val_if_fail (tzid, GNOME_Evolution_Calendar_ObjectNotFound);

	/* first try to get the timezone from the cache */
	g_mutex_lock (priv->lock);
	zone = e_cal_backend_cache_get_timezone (priv->cache, tzid);
	g_mutex_unlock (priv->lock);

	if (!zone) {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone) {
			return GNOME_Evolution_Calendar_ObjectNotFound;
		}
	}

	icalcomp = icaltimezone_get_component ((icaltimezone *) zone);

	if (!icalcomp) {
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	*object = icalcomponent_as_ical_string_r (icalcomp);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_add_timezone (ECalBackendSync *backend,
		     EDataCal        *cal,
		     const char      *tzobj)
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

		g_mutex_lock (priv->lock);
		e_cal_backend_cache_put_timezone (priv->cache, zone);
		g_mutex_unlock (priv->lock);

		icaltimezone_free (zone, TRUE);
	} else {
		icalcomponent_free (tz_comp);
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
caldav_set_default_zone (ECalBackendSync *backend,
			     EDataCal        *cal,
			     const char      *tzobj)
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
			const char       *sexp_string,
			GList           **objects)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSExp 	 *sexp;
	ECalBackendCache         *bcache;
	ECalBackend              *bkend;
	gboolean                  do_search;
	GList			 *list, *iter;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	sexp = e_cal_backend_sexp_new (sexp_string);

	if (sexp == NULL) {
		return GNOME_Evolution_Calendar_InvalidQuery;
	}

	if (g_str_equal (sexp, "#t")) {
		do_search = FALSE;
	} else {
		do_search = TRUE;
	}

	*objects = NULL;
	bcache = priv->cache;

	g_mutex_lock (priv->lock);

	list = e_cal_backend_cache_get_components (bcache);
	bkend = E_CAL_BACKEND (backend);

	for (iter = list; iter; iter = g_list_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search ||
		    e_cal_backend_sexp_match_comp (sexp, comp, bkend)) {
			char *str = e_cal_component_get_as_string (comp);
			*objects = g_list_prepend (*objects, str);
		}

		g_object_unref (comp);
	}

	g_object_unref (sexp);
	g_list_free (list);

	g_mutex_unlock (priv->lock);

	return GNOME_Evolution_Calendar_Success;
}

static void
caldav_start_query (ECalBackend  *backend,
		    EDataCalView *query)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;
	ECalBackendSExp 	 *sexp;
	ECalBackend              *bkend;
	gboolean                  do_search;
	GList			 *list, *iter;
	const char               *sexp_string;

	cbdav = E_CAL_BACKEND_CALDAV (backend);
	priv  = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	sexp_string = e_data_cal_view_get_text (query);
	sexp = e_cal_backend_sexp_new (sexp_string);

	/* FIXME:check invalid sexp */

	if (g_str_equal (sexp, "#t")) {
		do_search = FALSE;
	} else {
		do_search = TRUE;
	}

	g_mutex_lock (priv->lock);

	list = e_cal_backend_cache_get_components (priv->cache);
	bkend = E_CAL_BACKEND (backend);

	for (iter = list; iter; iter = g_list_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search ||
		    e_cal_backend_sexp_match_comp (sexp, comp, bkend)) {
			char *str = e_cal_component_get_as_string (comp);
			e_data_cal_view_notify_objects_added_1 (query, str);
			g_free (str);
		}

		g_object_unref (comp);
	}

	g_object_unref (sexp);
	g_list_free (list);


	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
	g_mutex_unlock (priv->lock);
	return;
}

static ECalBackendSyncStatus
caldav_get_free_busy (ECalBackendSync  *backend,
		      EDataCal         *cal,
		      GList            *users,
		      time_t            start,
		      time_t            end,
		      GList           **freebusy)
{
	/* FIXME: implement me! */
	g_warning ("function not implemented %s", G_STRFUNC);
	return GNOME_Evolution_Calendar_OtherError;
}

static ECalBackendSyncStatus
caldav_get_changes (ECalBackendSync  *backend,
		    EDataCal         *cal,
		    const char       *change_id,
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

	g_mutex_lock (priv->lock);

	/* We only support online and offline
	 * (is there something else?) */
	if (mode != CAL_MODE_REMOTE &&
	    mode != CAL_MODE_LOCAL) {
		e_cal_backend_notify_mode (backend,
					   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
					   cal_mode_to_corba (mode));
		g_mutex_unlock (priv->lock);
		return;
	}

	if (priv->mode == mode || !priv->loaded) {
		priv->mode = mode;
		e_cal_backend_notify_mode (backend,
					   GNOME_Evolution_Calendar_CalListener_MODE_SET,
					   cal_mode_to_corba (mode));
		g_mutex_unlock (priv->lock);
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

	g_mutex_unlock (priv->lock);
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
			      const char *tzid)
{
	icaltimezone *zone;

	zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);

	if (!zone && E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone)
		zone = E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone (backend, tzid);

	if (!zone) {
		zone = icaltimezone_get_utc_timezone ();
	}

	return zone;
}

/* ************************************************************************* */
/* ***************************** GObject Foo ******************************* */

G_DEFINE_TYPE (ECalBackendCalDAV, e_cal_backend_caldav, E_TYPE_CAL_BACKEND_SYNC);

static void
e_cal_backend_caldav_dispose (GObject *object)
{
	ECalBackendCalDAV        *cbdav;
	ECalBackendCalDAVPrivate *priv;

	cbdav = E_CAL_BACKEND_CALDAV (object);
	priv = E_CAL_BACKEND_CALDAV_GET_PRIVATE (cbdav);

	g_mutex_lock (priv->lock);

	if (priv->disposed) {
		g_mutex_unlock (priv->lock);
		return;
	}

	/* stop the slave  */
	priv->slave_cmd = SLAVE_SHOULD_DIE;
	if (priv->synch_slave) {
		g_cond_signal (priv->cond);

		/* wait until the slave died */
		g_cond_wait (priv->slave_gone_cond, priv->lock);
	}

	g_object_unref (priv->session);
	g_object_unref (priv->proxy);

	g_free (priv->username);
	g_free (priv->password);
	g_free (priv->uri);

	if (priv->cache != NULL) {
		g_object_unref (priv->cache);
	}

	priv->disposed = TRUE;
	g_mutex_unlock (priv->lock);

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

	g_free (priv->ctag);
	priv->ctag = NULL;

	g_mutex_free (priv->lock);
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
	priv->do_synch = FALSE;
	priv->loaded   = FALSE;

	/* Thinks the 'getctag' extension is available the first time, but unset it when realizes it isn't. */
	priv->ctag_supported = TRUE;
	priv->ctag = NULL;

	priv->lock = g_mutex_new ();
	priv->cond = g_cond_new ();
	priv->slave_gone_cond = g_cond_new ();

	/* Slave control ... */
	priv->slave_cmd = SLAVE_SHOULD_SLEEP;
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
	sync_class->get_timezone_sync         = caldav_get_timezone;
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

