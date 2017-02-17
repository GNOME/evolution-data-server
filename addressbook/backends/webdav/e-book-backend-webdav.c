/* e-book-backend-webdav.c - Webdav contact backend.
 *
 * Copyright (C) 2008 Matthias Braun <matze@braunis.de>
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
 * Authors: Matthias Braun <matze@braunis.de>
 */

/*
 * Implementation notes:
 *   We use the DavResource URIs as UID in the evolution contact
 *   ETags are saved in the WEBDAV_CONTACT_ETAG field so we know which cached contacts
 *   are outdated.
 */
#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n-lib.h>

#include "e-book-backend-webdav.h"

#include <libsoup/soup.h>

#include <libxml/parser.h>
#include <libxml/xmlreader.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#define E_BOOK_BACKEND_WEBDAV_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_BOOK_BACKEND_WEBDAV, EBookBackendWebdavPrivate))

#define USERAGENT             "Evolution/" VERSION
#define WEBDAV_CLOSURE_NAME   "EBookBackendWebdav.BookView::closure"
#define WEBDAV_CTAG_KEY "WEBDAV_CTAG"
#define WEBDAV_CACHE_VERSION_KEY "WEBDAV_CACHE_VERSION"
#define WEBDAV_CACHE_VERSION "2"
#define WEBDAV_CONTACT_ETAG "X-EVOLUTION-WEBDAV-ETAG"
#define WEBDAV_CONTACT_HREF "X-EVOLUTION-WEBDAV-HREF"

G_DEFINE_TYPE (EBookBackendWebdav, e_book_backend_webdav, E_TYPE_BOOK_BACKEND)

struct _EBookBackendWebdavPrivate {
	gboolean           marked_for_offline;
	SoupSession       *session;
	gchar             *uri;
	gchar              *username;
	gchar              *password;
	gboolean supports_getctag;
	gint64 last_server_test_us; /* real-time, in microseconds, when the last server test
					for changes had been made, when the server doesn't support ctag */

	GMutex cache_lock;
	GMutex update_lock;
	EBookBackendCache *cache;
};

typedef struct {
	EBookBackendWebdav *webdav;
	GThread            *thread;
	EFlag              *running;
} WebdavBackendSearchClosure;

static void
webdav_debug_setup (SoupSession *session)
{
	const gchar *debug_str;
	SoupLogger *logger;
	SoupLoggerLogLevel level;

	g_return_if_fail (session != NULL);

	debug_str = g_getenv ("WEBDAV_DEBUG");
	if (!debug_str || !*debug_str)
		return;

	if (g_ascii_strcasecmp (debug_str, "all") == 0)
		level = SOUP_LOGGER_LOG_BODY;
	else if (g_ascii_strcasecmp (debug_str, "headers") == 0)
		level = SOUP_LOGGER_LOG_HEADERS;
	else
		level = SOUP_LOGGER_LOG_MINIMAL;

	logger = soup_logger_new (level, 100 * 1024 * 1024);
	soup_session_add_feature (session, SOUP_SESSION_FEATURE (logger));
	g_object_unref (logger);
}

static void
webdav_contact_set_etag (EContact *contact,
                         const gchar *etag)
{
	EVCardAttribute *attr;

	g_return_if_fail (E_IS_CONTACT (contact));

	attr = e_vcard_get_attribute (E_VCARD (contact), WEBDAV_CONTACT_ETAG);

	if (attr) {
		e_vcard_attribute_remove_values (attr);
		if (etag) {
			e_vcard_attribute_add_value (attr, etag);
		} else {
			e_vcard_remove_attribute (E_VCARD (contact), attr);
		}
	} else if (etag) {
		e_vcard_append_attribute_with_value (
			E_VCARD (contact),
			e_vcard_attribute_new (NULL, WEBDAV_CONTACT_ETAG),
			etag);
	}
}

static gchar *
webdav_contact_get_etag (EContact *contact)
{
	EVCardAttribute *attr;
	GList *v = NULL;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	attr = e_vcard_get_attribute (E_VCARD (contact), WEBDAV_CONTACT_ETAG);

	if (attr)
		v = e_vcard_attribute_get_values (attr);

	return ((v && v->data) ? g_strstrip (g_strdup (v->data)) : NULL);
}

static void
webdav_contact_set_href (EContact *contact,
                         const gchar *href)
{
	EVCardAttribute *attr;

	g_return_if_fail (E_IS_CONTACT (contact));

	attr = e_vcard_get_attribute (E_VCARD (contact), WEBDAV_CONTACT_HREF);

	if (attr) {
		e_vcard_attribute_remove_values (attr);
		if (href) {
			e_vcard_attribute_add_value (attr, href);
		} else {
			e_vcard_remove_attribute (E_VCARD (contact), attr);
		}
	} else if (href) {
		e_vcard_append_attribute_with_value (
			E_VCARD (contact),
			e_vcard_attribute_new (NULL, WEBDAV_CONTACT_HREF),
			href);
	}
}

static gchar *
webdav_contact_get_href (EContact *contact)
{
	EVCardAttribute *attr;
	GList *v = NULL;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	attr = e_vcard_get_attribute (E_VCARD (contact), WEBDAV_CONTACT_HREF);

	if (attr)
		v = e_vcard_attribute_get_values (attr);

	return ((v && v->data) ? g_strstrip (g_strdup (v->data)) : NULL);
}

static void
closure_destroy (WebdavBackendSearchClosure *closure)
{
	e_flag_free (closure->running);
	if (closure->thread)
		g_thread_unref (closure->thread);
	g_free (closure);
}

static WebdavBackendSearchClosure *
init_closure (EDataBookView *book_view,
              EBookBackendWebdav *webdav)
{
	WebdavBackendSearchClosure *closure = g_new (WebdavBackendSearchClosure, 1);

	closure->webdav = webdav;
	closure->thread = NULL;
	closure->running = e_flag_new ();

	g_object_set_data_full (
		G_OBJECT (book_view), WEBDAV_CLOSURE_NAME,
		closure, (GDestroyNotify) closure_destroy);

	return closure;
}

static WebdavBackendSearchClosure *
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (G_OBJECT (book_view), WEBDAV_CLOSURE_NAME);
}

static guint
send_and_handle_ssl (EBookBackendWebdav *webdav,
                     SoupMessage *message,
                     GCancellable *cancellable)
{
	guint status_code;

	e_soup_ssl_trust_connect (message, e_backend_get_source (E_BACKEND (webdav)));

	status_code = soup_session_send_message (webdav->priv->session, message);

	if (SOUP_STATUS_IS_SUCCESSFUL (status_code))
		e_backend_ensure_source_status_connected (E_BACKEND (webdav));

	return status_code;
}

static EContact *
download_contact (EBookBackendWebdav *webdav,
                  const gchar *uri,
                  GCancellable *cancellable)
{
	SoupMessage *message;
	const gchar  *etag;
	EContact    *contact;
	guint        status;

	message = soup_message_new (SOUP_METHOD_GET, uri);
	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");

	status = send_and_handle_ssl (webdav, message, cancellable);
	if (status != 200) {
		g_warning ("Couldn't load '%s' (http status %d)", uri, status);
		g_object_unref (message);
		return NULL;
	}

	if (message->response_body == NULL) {
		g_message ("no response body after requesting '%s'", uri);
		g_object_unref (message);
		return NULL;
	}

	if (message->response_body->length <= 11 || 0 != g_ascii_strncasecmp ((const gchar *) message->response_body->data, "BEGIN:VCARD", 11)) {
		g_object_unref (message);
		return NULL;
	}

	etag = soup_message_headers_get_list (message->response_headers, "ETag");

	/* we use our URI as UID */
	contact = e_contact_new_from_vcard (message->response_body->data);
	if (contact == NULL) {
		g_warning ("Invalid vcard at '%s'", uri);
		g_object_unref (message);
		return NULL;
	}

	webdav_contact_set_href (contact, uri);
	/* the etag is remembered in the WEBDAV_CONTACT_ETAG field */
	if (etag != NULL) {
		webdav_contact_set_etag (contact, etag);
	}

	g_object_unref (message);
	return contact;
}

static guint
upload_contact (EBookBackendWebdav *webdav,
		const gchar *uri,
                EContact *contact,
                gchar **reason,
                GCancellable *cancellable)
{
	ESource     *source;
	ESourceWebdav *webdav_extension;
	SoupMessage *message;
	gchar       *etag;
	const gchar  *new_etag, *redir_uri;
	gchar        *request;
	guint        status;
	gboolean     avoid_ifmatch;
	const gchar *extension_name;

	g_return_val_if_fail (uri != NULL, SOUP_STATUS_BAD_REQUEST);

	source = e_backend_get_source (E_BACKEND (webdav));

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	message = soup_message_new (SOUP_METHOD_PUT, uri);
	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");

	avoid_ifmatch = e_source_webdav_get_avoid_ifmatch (webdav_extension);

	/* some servers (like apache < 2.2.8) don't handle If-Match, correctly so
	 * we can leave it out */
	if (!avoid_ifmatch) {
		/* only override if etag is still the same on the server */
		etag = webdav_contact_get_etag (contact);
		if (etag == NULL) {
			soup_message_headers_append (
				message->request_headers,
				"If-None-Match", "*");
		} else if (etag[0] == 'W' && etag[1] == '/') {
			g_warning ("we only have a weak ETag, don't use If-Match synchronisation");
		} else {
			soup_message_headers_append (
				message->request_headers,
				"If-Match", etag);
		}

		g_free (etag);
	}

	/* Remove the stored ETag, before saving to the server */
	webdav_contact_set_etag (contact, NULL);

	request = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	soup_message_set_request (
		message, "text/vcard", SOUP_MEMORY_TEMPORARY,
		request, strlen (request));

	status = send_and_handle_ssl (webdav, message, cancellable);
	new_etag = soup_message_headers_get_list (message->response_headers, "ETag");

	redir_uri = soup_message_headers_get_list (message->response_headers, "Location");

	/* set UID and WEBDAV_CONTACT_ETAG fields */
	webdav_contact_set_etag (contact, new_etag);
	if (redir_uri && *redir_uri) {
		if (!strstr (redir_uri, "://")) {
			/* it's a relative URI */
			SoupURI *suri = soup_uri_new (uri);
			gchar *full_uri;

			if (*redir_uri != '/' && *redir_uri != '\\') {
				gchar *slashed_path = g_strconcat ("/", redir_uri, NULL);

				soup_uri_set_path (suri, slashed_path);
				g_free (slashed_path);
			} else {
				soup_uri_set_path (suri, redir_uri);
			}
			full_uri = soup_uri_to_string (suri, FALSE);

			webdav_contact_set_href (contact, full_uri);

			g_free (full_uri);
			soup_uri_free (suri);
		} else {
			webdav_contact_set_href (contact, redir_uri);
		}
	} else {
		webdav_contact_set_href (contact, uri);
	}

	if (reason != NULL) {
		const gchar *phrase;

		phrase = message->reason_phrase;
		if (phrase == NULL)
			phrase = soup_status_get_phrase (message->status_code);
		if (phrase == NULL)
			phrase = _("Unknown error");

		*reason = g_strdup (phrase);
	}

	g_object_unref (message);
	g_free (request);

	return status;
}

static gboolean
webdav_handle_auth_request (EBookBackendWebdav *webdav,
                            GError **error)
{
	EBookBackendWebdavPrivate *priv = webdav->priv;

	if (priv->username != NULL) {
		g_free (priv->username);
		priv->username = NULL;
		g_free (priv->password);
		priv->password = NULL;

		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_AUTHENTICATION_FAILED,
			e_client_error_to_string (
			E_CLIENT_ERROR_AUTHENTICATION_FAILED));
	} else {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_AUTHENTICATION_REQUIRED,
			e_client_error_to_string (
			E_CLIENT_ERROR_AUTHENTICATION_REQUIRED));
	}

	return FALSE;
}

static guint
delete_contact (EBookBackendWebdav *webdav,
                const gchar *uri,
                GCancellable *cancellable)
{
	SoupMessage *message;
	guint        status;

	message = soup_message_new (SOUP_METHOD_DELETE, uri);
	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");

	status = send_and_handle_ssl (webdav, message, cancellable);
	g_object_unref (message);

	return status;
}

typedef struct parser_strings_t {
	const xmlChar *multistatus;
	const xmlChar *dav;
	const xmlChar *href;
	const xmlChar *response;
	const xmlChar *propstat;
	const xmlChar *prop;
	const xmlChar *getetag;
} parser_strings_t;

typedef struct response_element_t response_element_t;
struct response_element_t {
	xmlChar            *href;
	xmlChar            *etag;
	response_element_t *next;
};

static response_element_t *
parse_response_tag (const parser_strings_t *strings,
                    xmlTextReaderPtr reader)
{
	xmlChar            *href = NULL;
	xmlChar            *etag = NULL;
	gint                 depth = xmlTextReaderDepth (reader);
	response_element_t *element;

	while (xmlTextReaderRead (reader) == 1 && xmlTextReaderDepth (reader) > depth) {
		const xmlChar *tag_name;
		if (xmlTextReaderNodeType (reader) != XML_READER_TYPE_ELEMENT)
			continue;

		if (xmlTextReaderConstNamespaceUri (reader) != strings->dav)
			continue;

		tag_name = xmlTextReaderConstLocalName (reader);
		if (tag_name == strings->href) {
			if (href != NULL) {
				/* multiple href elements?!? */
				xmlFree (href);
			}
			href = xmlTextReaderReadString (reader);
		} else if (tag_name == strings->propstat) {
			/* find <propstat><prop><getetag> hierarchy */
			gint depth2 = xmlTextReaderDepth (reader);
			while (xmlTextReaderRead (reader) == 1 && xmlTextReaderDepth (reader) > depth2) {
				gint depth3;
				if (xmlTextReaderNodeType (reader) != XML_READER_TYPE_ELEMENT)
					continue;

				if (xmlTextReaderConstNamespaceUri (reader) != strings->dav
						|| xmlTextReaderConstLocalName (reader) != strings->prop)
					continue;

				depth3 = xmlTextReaderDepth (reader);
				while (xmlTextReaderRead (reader) == 1 && xmlTextReaderDepth (reader) > depth3) {
					if (xmlTextReaderNodeType (reader) != XML_READER_TYPE_ELEMENT)
						continue;

					if (xmlTextReaderConstNamespaceUri (reader) != strings->dav
							|| xmlTextReaderConstLocalName (reader)
							!= strings->getetag)
						continue;

					if (etag != NULL) {
						/* multiple etag elements?!? */
						xmlFree (etag);
					}
					etag = xmlTextReaderReadString (reader);
				}
			}
		}
	}

	if (href == NULL) {
		g_warning ("webdav returned response element without href");
		return NULL;
	}

	/* append element to list */
	element = g_malloc (sizeof (element[0]));
	element->href = href;
	element->etag = etag;
	return element;
}

static response_element_t *
parse_propfind_response (xmlTextReaderPtr reader)
{
	parser_strings_t    strings;
	response_element_t *elements;

	/* get internalized versions of some strings to avoid strcmp while
	 * parsing */
	strings.multistatus = xmlTextReaderConstString (reader, BAD_CAST "multistatus");
	strings.dav = xmlTextReaderConstString (reader, BAD_CAST "DAV:");
	strings.href = xmlTextReaderConstString (reader, BAD_CAST "href");
	strings.response = xmlTextReaderConstString (reader, BAD_CAST "response");
	strings.propstat = xmlTextReaderConstString (reader, BAD_CAST "propstat");
	strings.prop = xmlTextReaderConstString (reader, BAD_CAST "prop");
	strings.getetag = xmlTextReaderConstString (reader, BAD_CAST "getetag");

	while (xmlTextReaderRead (reader) == 1 && xmlTextReaderNodeType (reader) != XML_READER_TYPE_ELEMENT) {
	}

	if (xmlTextReaderConstLocalName (reader) != strings.multistatus
			|| xmlTextReaderConstNamespaceUri (reader) != strings.dav) {
		g_warning ("webdav PROPFIND result is not <DAV:multistatus>");
		return NULL;
	}

	elements = NULL;

	/* parse all DAV:response tags */
	while (xmlTextReaderRead (reader) == 1 && xmlTextReaderDepth (reader) > 0) {
		response_element_t *element;

		if (xmlTextReaderNodeType (reader) != XML_READER_TYPE_ELEMENT)
			continue;

		if (xmlTextReaderConstLocalName (reader) != strings.response
				|| xmlTextReaderConstNamespaceUri (reader) != strings.dav)
			continue;

		element = parse_response_tag (&strings, reader);
		if (element == NULL)
			continue;

		element->next = elements;
		elements = element;
	}

	return elements;
}

static SoupMessage *
send_propfind (EBookBackendWebdav *webdav,
	       GCancellable *cancellable,
	       GError **error)
{
	SoupMessage               *message;
	EBookBackendWebdavPrivate *priv = webdav->priv;
	const gchar               *request =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<propfind xmlns=\"DAV:\"><prop><getetag/></prop></propfind>";

	message = soup_message_new (SOUP_METHOD_PROPFIND, priv->uri);
	if (!message) {
		g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, _("Malformed URI: %s"), priv->uri);
		return NULL;
	}

	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");
	soup_message_headers_append (message->request_headers, "Depth", "1");
	soup_message_set_request (
		message, "text/xml", SOUP_MEMORY_TEMPORARY,
		(gchar *) request, strlen (request));

	send_and_handle_ssl (webdav, message, cancellable);

	return message;
}

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

static guint
xp_object_get_status (xmlXPathObjectPtr result)
{
	gboolean res;
	guint    ret = 0;

	if (result == NULL)
		return ret;

	if (result->type == XPATH_STRING) {
		res = soup_headers_parse_status_line ((gchar *) result->stringval, NULL, &ret, NULL);
		if (!res) {
			ret = 0;
		}
	}

	xmlXPathFreeObject (result);
	return ret;
}

static gboolean
check_addressbook_changed (EBookBackendWebdav *webdav,
                           gchar **new_ctag,
                           GCancellable *cancellable)
{
	gboolean res = TRUE;
	const gchar *request = "<?xml version=\"1.0\" encoding=\"utf-8\"?><propfind xmlns=\"DAV:\"><prop><getctag/></prop></propfind>";
	EBookBackendWebdavPrivate *priv;
	SoupMessage *message;

	g_return_val_if_fail (webdav != NULL, TRUE);
	g_return_val_if_fail (new_ctag != NULL, TRUE);

	*new_ctag = NULL;
	priv = webdav->priv;

	if (!priv->supports_getctag) {
		gint64 real_time_us = g_get_real_time ();

		/* Fifteen minutes in microseconds */
		if (real_time_us - priv->last_server_test_us < 15 * 60 * 1000 * 1000)
			return FALSE;

		priv->last_server_test_us = real_time_us;

		return TRUE;
	}

	priv->supports_getctag = FALSE;

	message = soup_message_new (SOUP_METHOD_PROPFIND, priv->uri);
	if (!message)
		return TRUE;

	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");
	soup_message_headers_append (message->request_headers, "Depth", "0");
	soup_message_set_request (message, "text/xml", SOUP_MEMORY_TEMPORARY, (gchar *) request, strlen (request));
	send_and_handle_ssl (webdav, message, cancellable);

	if (message->status_code == 207 && message->response_body) {
		xmlDocPtr xml;

		xml = xmlReadMemory (message->response_body->data, message->response_body->length, NULL, NULL, XML_PARSE_NOWARNING);
		if (xml) {
			const gchar *GETCTAG_XPATH_STATUS = "string(/D:multistatus/D:response/D:propstat/D:prop/D:getctag/../../D:status)";
			const gchar *GETCTAG_XPATH_VALUE = "string(/D:multistatus/D:response/D:propstat/D:prop/D:getctag)";
			xmlXPathContextPtr xpctx;

			xpctx = xmlXPathNewContext (xml);
			xmlXPathRegisterNs (xpctx, (xmlChar *) "D", (xmlChar *) "DAV:");

			if (xp_object_get_status (xpath_eval (xpctx, GETCTAG_XPATH_STATUS)) == 200) {
				gchar *txt = xp_object_get_string (xpath_eval (xpctx, GETCTAG_XPATH_VALUE));
				const gchar *stored_version;
				gboolean old_version;

				g_mutex_lock (&priv->cache_lock);
				stored_version = e_file_cache_get_object (E_FILE_CACHE (priv->cache), WEBDAV_CACHE_VERSION_KEY);

				/* The ETag was moved from REV to its own attribute, thus
				 * if the cache version is too low, update it. */
				old_version = !stored_version || atoi (stored_version) < atoi (WEBDAV_CACHE_VERSION);
				g_mutex_unlock (&priv->cache_lock);

				if (txt && *txt) {
					gint len = strlen (txt);

					if (*txt == '\"' && len > 2 && txt[len - 1] == '\"') {
						/* dequote */
						*new_ctag = g_strndup (txt + 1, len - 2);
					} else {
						*new_ctag = txt;
						txt = NULL;
					}

					if (*new_ctag) {
						const gchar *my_ctag;

						g_mutex_lock (&priv->cache_lock);
						my_ctag = e_file_cache_get_object (E_FILE_CACHE (priv->cache), WEBDAV_CTAG_KEY);
						res = old_version || !my_ctag || !g_str_equal (my_ctag, *new_ctag);

						priv->supports_getctag = TRUE;
						g_mutex_unlock (&priv->cache_lock);
					}
				}

				g_free (txt);

				if (old_version) {
					g_mutex_lock (&priv->cache_lock);

					if (!e_file_cache_replace_object (E_FILE_CACHE (priv->cache),
						WEBDAV_CACHE_VERSION_KEY,
						WEBDAV_CACHE_VERSION))
						e_file_cache_add_object (
							E_FILE_CACHE (priv->cache),
							WEBDAV_CACHE_VERSION_KEY,
							WEBDAV_CACHE_VERSION);

					g_mutex_unlock (&priv->cache_lock);
				}
			}

			xmlXPathFreeContext (xpctx);
			xmlFreeDoc (xml);
		}
	}

	g_object_unref (message);

	return res;
}

static void
remove_unknown_contacts_cb (gpointer href,
			    gpointer pcontact,
			    gpointer pwebdav)
{
	EContact *contact = pcontact;
	EBookBackendWebdav *webdav = pwebdav;
	const gchar *uid;

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	if (uid && e_book_backend_cache_remove_contact (webdav->priv->cache, uid))
		e_book_backend_notify_remove ((EBookBackend *) webdav, uid);
}

static gboolean
download_contacts (EBookBackendWebdav *webdav,
                   EFlag *running,
                   EDataBookView *book_view,
		   gboolean force,
                   GCancellable *cancellable,
                   GError **error)
{
	EBookBackendWebdavPrivate *priv = webdav->priv;
	EBookBackend		  *book_backend;
	SoupMessage               *message;
	guint                      status;
	xmlTextReaderPtr           reader;
	response_element_t        *elements;
	response_element_t        *element;
	response_element_t        *next;
	gint                        count;
	gint                        i;
	gchar                     *new_ctag = NULL;
	GHashTable                *href_to_contact;
	GList                     *cached_contacts, *iter;

	g_mutex_lock (&priv->update_lock);

	if (!force && !check_addressbook_changed (webdav, &new_ctag, cancellable)) {
		g_free (new_ctag);
		g_mutex_unlock (&priv->update_lock);
		return TRUE;
	}

	book_backend = E_BOOK_BACKEND (webdav);

	if (book_view != NULL) {
		e_data_book_view_notify_progress (book_view, -1,
				_("Loading Addressbook summary..."));
	}

	message = send_propfind (webdav, cancellable, error);
	if (!message) {
		g_free (new_ctag);
		if (book_view)
			e_data_book_view_notify_progress (book_view, -1, NULL);
		g_mutex_unlock (&priv->update_lock);
		return FALSE;
	}

	status = message->status_code;

	if (status == SOUP_STATUS_UNAUTHORIZED ||
	    status == SOUP_STATUS_PROXY_UNAUTHORIZED ||
	    status == SOUP_STATUS_FORBIDDEN) {
		g_object_unref (message);
		g_free (new_ctag);
		if (book_view)
			e_data_book_view_notify_progress (book_view, -1, NULL);
		g_mutex_unlock (&priv->update_lock);
		return webdav_handle_auth_request (webdav, error);
	}
	if (status != 207) {
		g_set_error (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			_("PROPFIND on webdav failed with HTTP status %d (%s)"),
			status,
			message->reason_phrase && *message->reason_phrase ? message->reason_phrase :
			(soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : _("Unknown error")));

		g_object_unref (message);
		g_free (new_ctag);

		if (book_view)
			e_data_book_view_notify_progress (book_view, -1, NULL);

		g_mutex_unlock (&priv->update_lock);

		return FALSE;
	}
	if (message->response_body == NULL) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			_("No response body in webdav PROPFIND result"));

		g_object_unref (message);
		g_free (new_ctag);

		if (book_view)
			e_data_book_view_notify_progress (book_view, -1, NULL);

		g_mutex_unlock (&priv->update_lock);

		return FALSE;
	}

	/* parse response */
	reader = xmlReaderForMemory (
		message->response_body->data,
		message->response_body->length, NULL, NULL,
		XML_PARSE_NOWARNING);

	elements = parse_propfind_response (reader);

	/* count contacts */
	count = 0;
	for (element = elements; element != NULL; element = element->next) {
		++count;
	}

	href_to_contact = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_object_unref);
	g_mutex_lock (&priv->cache_lock);
	e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache));
	cached_contacts = e_book_backend_cache_get_contacts (priv->cache, NULL);
	for (iter = cached_contacts; iter; iter = g_list_next (iter)) {
		EContact *contact = iter->data;
		gchar *href;

		if (!contact)
			continue;

		href = webdav_contact_get_href (contact);

		if (href)
			g_hash_table_insert (href_to_contact, href, g_object_ref (contact));
	}
	g_list_free_full (cached_contacts, g_object_unref);
	g_mutex_unlock (&priv->cache_lock);

	/* download contacts */
	i = 0;
	for (element = elements; element != NULL; element = element->next, ++i) {
		const gchar  *uri;
		const gchar *etag;
		EContact    *contact;
		gchar *complete_uri, *stored_etag;

		/* stop downloading if search was aborted */
		if (running != NULL && !e_flag_is_set (running))
			break;

		if (book_view != NULL) {
			gfloat percent = 100.0 / count * i;
			gchar buf[100];
			snprintf (buf, sizeof (buf), _("Loading Contacts (%d%%)"), (gint) percent);
			e_data_book_view_notify_progress (book_view, -1, buf);
		}

		/* skip collections */
		uri = (const gchar *) element->href;
		if (uri[strlen (uri) - 1] == '/')
			continue;

		/* uri might be relative, construct complete one */
		if (uri[0] == '/') {
			SoupURI *soup_uri = soup_uri_new (priv->uri);
			g_free (soup_uri->path);
			soup_uri->path = g_strdup (uri);

			complete_uri = soup_uri_to_string (soup_uri, FALSE);
			soup_uri_free (soup_uri);
		} else {
			complete_uri = g_strdup (uri);
		}

		etag = (const gchar *) element->etag;

		contact = g_hash_table_lookup (href_to_contact, complete_uri);
		if (contact) {
			g_object_ref (contact);
			g_hash_table_remove (href_to_contact, complete_uri);
			stored_etag = webdav_contact_get_etag (contact);
		} else {
			stored_etag = NULL;
		}


		/* download contact if it is not cached or its ETag changed */
		if (contact == NULL || etag == NULL || !stored_etag ||
		    strcmp (stored_etag, etag) != 0) {
			if (contact != NULL)
				g_object_unref (contact);
			contact = download_contact (webdav, complete_uri, cancellable);
			if (contact != NULL) {
				const gchar *uid;

				uid = e_contact_get_const (contact, E_CONTACT_UID);

				g_mutex_lock (&priv->cache_lock);
				if (e_book_backend_cache_remove_contact (priv->cache, uid))
					e_book_backend_notify_remove (book_backend, uid);
				e_book_backend_cache_add_contact (priv->cache, contact);
				g_mutex_unlock (&priv->cache_lock);
				e_book_backend_notify_update (book_backend, contact);
			}
		}

		if (contact != NULL)
			g_object_unref (contact);
		g_free (complete_uri);
		g_free (stored_etag);
	}

	/* free element list */
	for (element = elements; element != NULL; element = next) {
		next = element->next;

		xmlFree (element->href);
		xmlFree (element->etag);
		g_free (element);
	}

	xmlFreeTextReader (reader);
	g_object_unref (message);

	if (new_ctag) {
		g_mutex_lock (&priv->cache_lock);
		if (!e_file_cache_replace_object (E_FILE_CACHE (priv->cache), WEBDAV_CTAG_KEY, new_ctag))
			e_file_cache_add_object (E_FILE_CACHE (priv->cache), WEBDAV_CTAG_KEY, new_ctag);
		g_mutex_unlock (&priv->cache_lock);
	}
	g_free (new_ctag);

	if (book_view)
		e_data_book_view_notify_progress (book_view, -1, NULL);

	g_mutex_lock (&priv->cache_lock);

	if (!g_cancellable_is_cancelled (cancellable) &&
	    (!running || e_flag_is_set (running))) {
		/* clean-up the cache only if it wasn't cancelled during the work */
		g_hash_table_foreach (href_to_contact, remove_unknown_contacts_cb, webdav);
	}
	e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));
	g_mutex_unlock (&priv->cache_lock);
	g_mutex_unlock (&priv->update_lock);

	g_hash_table_destroy (href_to_contact);

	return TRUE;
}

static gpointer
book_view_thread (gpointer data)
{
	EDataBookView *book_view = data;
	WebdavBackendSearchClosure *closure = get_closure (book_view);
	EBookBackendWebdav *webdav = closure->webdav;

	e_flag_set (closure->running);

	/* ref the book view because it'll be removed and unrefed when/if
	 * it's stopped */
	g_object_ref (book_view);

	download_contacts (webdav, closure->running, book_view, FALSE, NULL, NULL);

	g_object_unref (book_view);

	return NULL;
}

static void
e_book_backend_webdav_start_view (EBookBackend *backend,
                                  EDataBookView *book_view)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv = webdav->priv;
	EBookBackendSExp *sexp;
	const gchar *query;
	GList *contacts;
	GList *l;

	sexp = e_data_book_view_get_sexp (book_view);
	query = e_book_backend_sexp_text (sexp);

	g_mutex_lock (&priv->cache_lock);
	contacts = e_book_backend_cache_get_contacts (priv->cache, query);
	g_mutex_unlock (&priv->cache_lock);

	for (l = contacts; l != NULL; l = g_list_next (l)) {
		EContact *contact = l->data;
		e_data_book_view_notify_update (book_view, contact);
		g_object_unref (contact);
	}
	g_list_free (contacts);

	/* this way the UI is notified about cached contacts immediately,
	 * and the update thread notifies about possible changes only */
	e_data_book_view_notify_complete (book_view, NULL /* Success */);

	if (e_backend_get_online (E_BACKEND (backend))) {
		WebdavBackendSearchClosure *closure;

		closure = init_closure (
			book_view, E_BOOK_BACKEND_WEBDAV (backend));

		closure->thread = g_thread_new (
			NULL, book_view_thread, book_view);

		e_flag_wait (closure->running);
	}
}

static void
e_book_backend_webdav_stop_view (EBookBackend *backend,
                                 EDataBookView *book_view)
{
	WebdavBackendSearchClosure *closure;
	gboolean                    need_join;

	if (!e_backend_get_online (E_BACKEND (backend)))
		return;

	closure = get_closure (book_view);
	if (closure == NULL)
		return;

	need_join = e_flag_is_set (closure->running);
	e_flag_clear (closure->running);

	if (need_join) {
		g_thread_join (closure->thread);
		closure->thread = NULL;
	}
}

/** authentication callback for libsoup */
static void
soup_authenticate (SoupSession *session,
                   SoupMessage *message,
                   SoupAuth *auth,
                   gboolean retrying,
                   gpointer data)
{
	EBookBackendWebdav        *webdav = data;
	EBookBackendWebdavPrivate *priv = webdav->priv;

	if (retrying)
		return;

	if (!priv->username || !*priv->username || !priv->password)
		soup_message_set_status (message, SOUP_STATUS_FORBIDDEN);
	else
		soup_auth_authenticate (auth, priv->username, priv->password);
}

static void
e_book_backend_webdav_notify_online_cb (EBookBackend *backend,
                                        GParamSpec *pspec)
{
	gboolean online;

	/* set_mode is called before the backend is loaded */
	if (!e_book_backend_is_opened (backend))
		return;

	/* XXX Could just use a property binding for this.
	 *     EBackend:online --> EBookBackend:writable */
	online = e_backend_get_online (E_BACKEND (backend));
	e_book_backend_set_writable (backend, online);
}

static void
book_backend_webdav_dispose (GObject *object)
{
	EBookBackendWebdavPrivate *priv;

	priv = E_BOOK_BACKEND_WEBDAV_GET_PRIVATE (object);

	g_clear_object (&priv->session);
	g_clear_object (&priv->cache);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_book_backend_webdav_parent_class)->dispose (object);
}

static void
book_backend_webdav_finalize (GObject *object)
{
	EBookBackendWebdav *webdav = E_BOOK_BACKEND_WEBDAV (object);
	EBookBackendWebdavPrivate *priv = webdav->priv;

	g_free (priv->uri);
	g_free (priv->username);
	g_free (priv->password);

	g_mutex_clear (&priv->cache_lock);
	g_mutex_clear (&priv->update_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_webdav_parent_class)->finalize (object);
}

static gchar *
book_backend_webdav_get_backend_property (EBookBackend *backend,
                                          const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		return g_strdup ("net,do-initial-query,contact-lists,refresh-supported");

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		return g_strdup (e_contact_field_name (E_CONTACT_FILE_AS));

	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		GString *fields;
		gint ii;

		fields = g_string_sized_new (1024);

		/* we support everything */
		for (ii = 1; ii < E_CONTACT_FIELD_LAST; ii++) {
			if (fields->len > 0)
				g_string_append_c (fields, ',');
			g_string_append (fields, e_contact_field_name (ii));
		}

		return g_string_free (fields, FALSE);
	}

	/* Chain up to parent's get_backend_property() method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_webdav_parent_class)->
		get_backend_property (backend, prop_name);
}

static gboolean
book_backend_webdav_test_can_connect (EBookBackendWebdav *webdav,
				      gchar **out_certificate_pem,
				      GTlsCertificateFlags *out_certificate_errors,
				      GCancellable *cancellable,
				      GError **error)
{
	SoupMessage *message;
	gboolean res = FALSE;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_WEBDAV (webdav), FALSE);

	/* Send a PROPFIND to test whether user/password is correct. */
	message = send_propfind (webdav, cancellable, error);
	if (!message)
		return FALSE;

	switch (message->status_code) {
		case SOUP_STATUS_OK:
		case SOUP_STATUS_MULTI_STATUS:
			res = TRUE;
			break;

		case SOUP_STATUS_UNAUTHORIZED:
		case SOUP_STATUS_PROXY_UNAUTHORIZED:
			g_free (webdav->priv->username);
			webdav->priv->username = NULL;
			g_free (webdav->priv->password);
			webdav->priv->password = NULL;
			g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED,
				e_client_error_to_string (E_CLIENT_ERROR_AUTHENTICATION_FAILED));
			break;

		case SOUP_STATUS_FORBIDDEN:
			g_free (webdav->priv->username);
			webdav->priv->username = NULL;
			g_free (webdav->priv->password);
			webdav->priv->password = NULL;
			g_set_error_literal (error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_REQUIRED,
				e_client_error_to_string (E_CLIENT_ERROR_AUTHENTICATION_REQUIRED));
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

			g_set_error_literal (
				error, SOUP_HTTP_ERROR,
				message->status_code,
				message->reason_phrase);
			break;

		default:
			g_set_error_literal (
				error, SOUP_HTTP_ERROR,
				message->status_code,
				message->reason_phrase);
			break;
	}

	g_object_unref (message);

	return res;
}

static gboolean
book_backend_webdav_open_sync (EBookBackend *backend,
                               GCancellable *cancellable,
                               GError **error)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	ESourceAuthentication     *auth_extension;
	ESourceOffline            *offline_extension;
	ESourceWebdav             *webdav_extension;
	ESource                   *source;
	const gchar               *extension_name;
	const gchar               *cache_dir;
	gchar                     *filename;
	SoupSession               *session;
	SoupURI                   *suri;
	gboolean                   success = TRUE;

	/* will try fetch ctag for the first time, if it fails then sets this to FALSE */
	webdav->priv->supports_getctag = TRUE;

	source = e_backend_get_source (E_BACKEND (backend));
	cache_dir = e_book_backend_get_cache_dir (backend);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	offline_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	webdav->priv->marked_for_offline =
		e_source_offline_get_stay_synchronized (offline_extension);

	if (!e_backend_get_online (E_BACKEND (backend)) &&
	    !webdav->priv->marked_for_offline ) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE,
			e_client_error_to_string (
			E_CLIENT_ERROR_OFFLINE_UNAVAILABLE));
		return FALSE;
	}

	suri = e_source_webdav_dup_soup_uri (webdav_extension);

	webdav->priv->uri = soup_uri_to_string (suri, FALSE);
	if (!webdav->priv->uri || !*webdav->priv->uri) {
		g_free (webdav->priv->uri);
		webdav->priv->uri = NULL;
		soup_uri_free (suri);
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			_("Cannot transform SoupURI to string"));
		return FALSE;
	}

	g_mutex_lock (&webdav->priv->cache_lock);

	/* make sure the uri ends with a forward slash */
	if (webdav->priv->uri[strlen (webdav->priv->uri) - 1] != '/') {
		gchar *tmp = webdav->priv->uri;
		webdav->priv->uri = g_strconcat (tmp, "/", NULL);
		g_free (tmp);
	}

	if (!webdav->priv->cache) {
		filename = g_build_filename (cache_dir, "cache.xml", NULL);
		webdav->priv->cache = e_book_backend_cache_new (filename);
		g_free (filename);
	}
	g_mutex_unlock (&webdav->priv->cache_lock);

	session = soup_session_sync_new ();
	g_object_set (
		session,
		SOUP_SESSION_TIMEOUT, 90,
		SOUP_SESSION_SSL_STRICT, TRUE,
		SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE, TRUE,
		SOUP_SESSION_ACCEPT_LANGUAGE_AUTO, TRUE,
		NULL);

	e_binding_bind_property (
		backend, "proxy-resolver",
		session, "proxy-resolver",
		G_BINDING_SYNC_CREATE);

	e_source_webdav_unset_temporary_ssl_trust (webdav_extension);

	g_signal_connect (
		session, "authenticate",
		G_CALLBACK (soup_authenticate), webdav);

	webdav->priv->session = session;
	webdav_debug_setup (webdav->priv->session);

	e_backend_set_online (E_BACKEND (backend), TRUE);
	e_book_backend_set_writable (backend, TRUE);

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

	if (e_source_authentication_required (auth_extension)) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

		success = e_backend_credentials_required_sync (E_BACKEND (backend),
			E_SOURCE_CREDENTIALS_REASON_REQUIRED, NULL, 0, NULL,
			cancellable, error);
	} else {
		gchar *certificate_pem = NULL;
		GTlsCertificateFlags certificate_errors = 0;
		GError *local_error = NULL;

		success = book_backend_webdav_test_can_connect (webdav, &certificate_pem, &certificate_errors, cancellable, &local_error);
		if (!success && !g_cancellable_is_cancelled (cancellable)) {
			ESourceCredentialsReason reason;
			GError *local_error2 = NULL;

			if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
				reason = E_SOURCE_CREDENTIALS_REASON_SSL_FAILED;
				e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_SSL_FAILED);
			} else if (g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED) ||
			           g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_REQUIRED)) {
				reason = E_SOURCE_CREDENTIALS_REASON_REQUIRED;
			} else {
				reason = E_SOURCE_CREDENTIALS_REASON_ERROR;
			}

			if (!e_backend_credentials_required_sync (E_BACKEND (backend), reason, certificate_pem, certificate_errors,
				local_error, cancellable, &local_error2)) {
				g_warning ("%s: Failed to call credentials required: %s", G_STRFUNC, local_error2 ? local_error2->message : "Unknown error");
			}

			if (!local_error2 && g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
				/* These cerificate errors are treated through the authentication */
				g_clear_error (&local_error);
			} else {
				g_propagate_error (error, local_error);
				local_error = NULL;
			}

			g_clear_error (&local_error2);
		} else {
			e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);
		}

		g_free (certificate_pem);

		if (local_error)
			g_propagate_error (error, local_error);
	}

	soup_uri_free (suri);

	return success;
}

static gboolean
webdav_can_use_uid (const gchar *uid)
{
	const gchar *ptr;

	if (!uid || !*uid)
		return FALSE;

	for (ptr = uid; *ptr; ptr++) {
		if ((*ptr >= 'a' && *ptr <= 'z') ||
		    (*ptr >= 'A' && *ptr <= 'Z') ||
		    (*ptr >= '0' && *ptr <= '9') ||
		    strchr (".-@", *ptr) != NULL)
			continue;

		return FALSE;
	}

	return TRUE;
}

static gboolean
book_backend_webdav_create_contacts_sync (EBookBackend *backend,
                                          const gchar * const *vcards,
                                          GQueue *out_contacts,
                                          GCancellable *cancellable,
                                          GError **error)
{
	EBookBackendWebdav *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EContact *contact;
	gchar *uid, *href;
	const gchar *orig_uid;
	guint status;
	gchar *status_reason = NULL, *stored_etag;

	/* We make the assumption that the vCard list we're passed is
	 * always exactly one element long, since we haven't specified
	 * "bulk-adds" in our static capability list.  This is because
	 * there is no way to roll back changes in case of an error. */
	if (g_strv_length ((gchar **) vcards) > 1) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_SUPPORTED,
			_("The backend does not support bulk additions"));
		return FALSE;
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_REPOSITORY_OFFLINE,
			e_client_error_to_string (
			E_CLIENT_ERROR_REPOSITORY_OFFLINE));
		return FALSE;
	}

	contact = e_contact_new_from_vcard (vcards[0]);

	orig_uid = e_contact_get_const (contact, E_CONTACT_UID);
	if (orig_uid && *orig_uid && webdav_can_use_uid (orig_uid) && !e_book_backend_cache_check_contact (webdav->priv->cache, orig_uid)) {
		uid = g_strdup (orig_uid);
	} else {
		uid = NULL;

		do {
			g_free (uid);

			/* do 3 random() calls to construct a unique ID... poor way but should be
			 * good enough for us */
			uid = g_strdup_printf ("%08X-%08X-%08X", g_random_int (), g_random_int (), g_random_int ());

		} while (e_book_backend_cache_check_contact (webdav->priv->cache, uid) &&
			 !g_cancellable_is_cancelled (cancellable));

		e_contact_set (contact, E_CONTACT_UID, uid);
	}

	href = g_strconcat (webdav->priv->uri, uid, ".vcf", NULL);

	/* kill WEBDAV_CONTACT_ETAG field (might have been set by some other backend) */
	webdav_contact_set_href (contact, NULL);
	webdav_contact_set_etag (contact, NULL);

	status = upload_contact (webdav, href, contact, &status_reason, cancellable);
	g_free (href);

	if (status != 201 && status != 204) {
		g_object_unref (contact);
		if (status == 401 || status == 407) {
			webdav_handle_auth_request (webdav, error);
		} else {
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Create resource '%s' failed with HTTP status %d (%s)"),
				uid, status, status_reason);
		}
		g_free (uid);
		g_free (status_reason);
		return FALSE;
	}

	g_free (status_reason);
	g_free (uid);

	/* PUT request didn't return an etag? try downloading to get one */
	stored_etag = webdav_contact_get_etag (contact);
	if (!stored_etag) {
		gchar *href;
		EContact *new_contact = NULL;

		href = webdav_contact_get_href (contact);
		if (href) {
			new_contact = download_contact (webdav, href, cancellable);
			g_free (href);
		}

		g_object_unref (contact);

		if (new_contact == NULL) {
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				e_client_error_to_string (
				E_CLIENT_ERROR_OTHER_ERROR));
			return FALSE;
		}
		contact = new_contact;
	} else {
		g_free (stored_etag);
	}

	g_mutex_lock (&webdav->priv->cache_lock);
	e_book_backend_cache_add_contact (webdav->priv->cache, contact);
	g_mutex_unlock (&webdav->priv->cache_lock);

	g_queue_push_tail (out_contacts, g_object_ref (contact));

	g_object_unref (contact);

	return TRUE;
}

static gboolean
book_backend_webdav_modify_contacts_sync (EBookBackend *backend,
                                          const gchar * const *vcards,
                                          GQueue *out_contacts,
                                          GCancellable *cancellable,
                                          GError **error)
{
	EBookBackendWebdav *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EContact *contact;
	const gchar *uid;
	gchar *href, *etag;
	guint status;
	gchar *status_reason = NULL;

	/* We make the assumption that the vCard list we're passed is
	 * always exactly one element long, since we haven't specified
	 * "bulk-modifies" in our static capability list.  This is because
	 * there is no clean way to roll back changes in case of an error. */
	if (g_strv_length ((gchar **) vcards) > 1) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_SUPPORTED,
			_("The backend does not support bulk modifications"));
		return FALSE;
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_REPOSITORY_OFFLINE,
			e_client_error_to_string (
			E_CLIENT_ERROR_REPOSITORY_OFFLINE));
		return FALSE;
	}

	/* modify contact */
	contact = e_contact_new_from_vcard (vcards[0]);
	href = webdav_contact_get_href (contact);
	status = upload_contact (webdav, href, contact, &status_reason, cancellable);
	g_free (href);
	if (status != 200 && status != 201 && status != 204) {
		g_object_unref (contact);
		if (status == 401 || status == 407) {
			webdav_handle_auth_request (webdav, error);
			g_free (status_reason);
			return FALSE;
		}
		/* data changed on server while we were editing */
		if (status == 412) {
			/* too bad no special error code in evolution for this... */
			g_set_error_literal (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("Contact on server changed -> not modifying"));
			g_free (status_reason);
			return FALSE;
		}

		g_set_error (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			_("Modify contact failed with HTTP status %d (%s)"),
			status, status_reason);

		g_free (status_reason);
		return FALSE;
	}

	g_free (status_reason);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	g_mutex_lock (&webdav->priv->cache_lock);
	e_book_backend_cache_remove_contact (webdav->priv->cache, uid);

	etag = webdav_contact_get_etag (contact);

	/* PUT request didn't return an etag? try downloading to get one */
	if (etag == NULL || (etag[0] == 'W' && etag[1] == '/')) {
		EContact *new_contact = NULL;

		href = webdav_contact_get_href (contact);
		if (href) {
			new_contact = download_contact (webdav, href, cancellable);
			g_free (href);
		}

		if (new_contact != NULL) {
			g_object_unref (contact);
			contact = new_contact;
		}
	}

	g_free (etag);

	e_book_backend_cache_add_contact (webdav->priv->cache, contact);
	g_mutex_unlock (&webdav->priv->cache_lock);

	g_queue_push_tail (out_contacts, g_object_ref (contact));

	g_object_unref (contact);

	return TRUE;
}

static gboolean
book_backend_webdav_remove_contacts_sync (EBookBackend *backend,
                                          const gchar * const *uids,
                                          GCancellable *cancellable,
                                          GError **error)
{
	EBookBackendWebdav *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EContact *contact;
	gchar *href;
	guint status;

	/* We make the assumption that the ID list we're passed is
	 * always exactly one element long, since we haven't specified
	 * "bulk-removes" in our static capability list. */
	if (g_strv_length ((gchar **) uids) > 1) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_NOT_SUPPORTED,
			_("The backend does not support bulk removals"));
		return FALSE;
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		g_set_error_literal (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_REPOSITORY_OFFLINE,
			e_client_error_to_string (
			E_CLIENT_ERROR_REPOSITORY_OFFLINE));
		return FALSE;
	}

	g_mutex_lock (&webdav->priv->cache_lock);
	contact = e_book_backend_cache_get_contact (webdav->priv->cache, uids[0]);
	g_mutex_unlock (&webdav->priv->cache_lock);

	if (!contact) {
		g_set_error_literal (
			error, E_BOOK_CLIENT_ERROR,
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
			e_book_client_error_to_string (
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));
		return FALSE;
	}

	href = webdav_contact_get_href (contact);
	if (!href) {
		g_object_unref (contact);
		g_set_error (
			error, E_CLIENT_ERROR,
			E_CLIENT_ERROR_OTHER_ERROR,
			_("DELETE failed with HTTP status %d"), SOUP_STATUS_MALFORMED);
		return FALSE;
	}

	status = delete_contact (webdav, href, cancellable);

	g_object_unref (contact);
	g_free (href);

	if (!SOUP_STATUS_IS_SUCCESSFUL (status)) {
		if (status == 401 || status == 407) {
			webdav_handle_auth_request (webdav, error);
		} else {
			g_set_error (
				error, E_CLIENT_ERROR,
				E_CLIENT_ERROR_OTHER_ERROR,
				_("DELETE failed with HTTP status %d"), status);
		}
		return FALSE;
	}

	g_mutex_lock (&webdav->priv->cache_lock);
	e_book_backend_cache_remove_contact (webdav->priv->cache, uids[0]);
	g_mutex_unlock (&webdav->priv->cache_lock);

	return TRUE;
}

static EContact *
book_backend_webdav_get_contact_sync (EBookBackend *backend,
                                      const gchar *uid,
                                      GCancellable *cancellable,
                                      GError **error)
{
	EBookBackendWebdav *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EContact *contact;

	g_mutex_lock (&webdav->priv->cache_lock);
	contact = e_book_backend_cache_get_contact (
		webdav->priv->cache, uid);
	g_mutex_unlock (&webdav->priv->cache_lock);

	if (contact && e_backend_get_online (E_BACKEND (backend))) {
		gchar *href;

		href = webdav_contact_get_href (contact);
		g_object_unref (contact);

		if (href) {
			contact = download_contact (webdav, href, cancellable);
			g_free (href);
		} else {
			contact = NULL;
		}

		/* update cache as we possibly have changes */
		if (contact != NULL) {
			g_mutex_lock (&webdav->priv->cache_lock);
			e_book_backend_cache_remove_contact (
				webdav->priv->cache, uid);
			e_book_backend_cache_add_contact (
				webdav->priv->cache, contact);
			g_mutex_unlock (&webdav->priv->cache_lock);
		}
	}

	if (contact == NULL) {
		g_set_error_literal (
			error, E_BOOK_CLIENT_ERROR,
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND,
			e_book_client_error_to_string (
			E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND));
		return FALSE;
	}

	return contact;
}

static gboolean
book_backend_webdav_get_contact_list_sync (EBookBackend *backend,
                                           const gchar *query,
                                           GQueue *out_contacts,
                                           GCancellable *cancellable,
                                           GError **error)
{
	EBookBackendWebdav *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	GList *contact_list;

	if (e_backend_get_online (E_BACKEND (backend)) &&
	    e_source_get_connection_status (e_backend_get_source (E_BACKEND (backend))) == E_SOURCE_CONNECTION_STATUS_CONNECTED) {
		/* make sure the cache is up to date */
		if (!download_contacts (webdav, NULL, NULL, FALSE, cancellable, error))
			return FALSE;
	}

	/* answer query from cache */
	g_mutex_lock (&webdav->priv->cache_lock);
	contact_list = e_book_backend_cache_get_contacts (
		webdav->priv->cache, query);
	g_mutex_unlock (&webdav->priv->cache_lock);

	/* This appends contact_list to out_contacts, one element at a
	 * time, since GLib lacks something like g_queue_append_list().
	 *
	 * XXX Would be better if e_book_backend_cache_get_contacts()
	 *     took an output GQueue instead of returning a GList. */
	while (contact_list != NULL) {
		GList *link = contact_list;
		contact_list = g_list_remove_link (contact_list, link);
		g_queue_push_tail_link (out_contacts, link);
	}

	return TRUE;
}

static ESourceAuthenticationResult
book_backend_webdav_authenticate_sync (EBackend *backend,
				       const ENamedParameters *credentials,
				       gchar **out_certificate_pem,
				       GTlsCertificateFlags *out_certificate_errors,
				       GCancellable *cancellable,
				       GError **error)
{
	EBookBackendWebdav *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	ESourceAuthentication *auth_extension;
	ESourceAuthenticationResult result;
	ESource *source;
	const gchar *username;
	GError *local_error = NULL;

	source = e_backend_get_source (backend);
	auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	g_free (webdav->priv->username);
	webdav->priv->username = NULL;

	g_free (webdav->priv->password);
	webdav->priv->password = g_strdup (e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD));

	username = e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME);
	if (username && *username) {
		webdav->priv->username = g_strdup (username);
	} else {
		webdav->priv->username = e_source_authentication_dup_user (auth_extension);
	}

	if (book_backend_webdav_test_can_connect (webdav, out_certificate_pem, out_certificate_errors, cancellable, &local_error)) {
		result = E_SOURCE_AUTHENTICATION_ACCEPTED;
	} else if (g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED) ||
		   g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_REQUIRED)) {
		if (!e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD) ||
		    g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_REQUIRED))
			result = E_SOURCE_AUTHENTICATION_REQUIRED;
		else
			result = E_SOURCE_AUTHENTICATION_REJECTED;
		g_clear_error (&local_error);
	} else if (g_error_matches (local_error, SOUP_HTTP_ERROR, SOUP_STATUS_SSL_FAILED)) {
		result = E_SOURCE_AUTHENTICATION_ERROR_SSL_FAILED;
		g_propagate_error (error, local_error);
	} else {
		result = E_SOURCE_AUTHENTICATION_ERROR;
		g_propagate_error (error, local_error);
	}

	return result;
}

static gboolean
e_book_backend_webdav_refresh_sync (EBookBackend *book_backend,
				    GCancellable *cancellable,
				    GError **error)
{
	EBackend *backend;

	g_return_val_if_fail (E_IS_BOOK_BACKEND_WEBDAV (book_backend), FALSE);

	backend = E_BACKEND (book_backend);

	if (!e_backend_get_online (backend) &&
	    e_backend_is_destination_reachable (backend, cancellable, NULL)) {
		e_backend_set_online (backend, TRUE);
	}

	if (e_backend_get_online (backend) && !g_cancellable_is_cancelled (cancellable)) {
		return download_contacts (E_BOOK_BACKEND_WEBDAV (book_backend), NULL, NULL, TRUE, cancellable, error);
	}

	return TRUE;
}

static void
e_book_backend_webdav_class_init (EBookBackendWebdavClass *class)
{
	GObjectClass *object_class;
	EBackendClass *backend_class;
	EBookBackendClass *book_backend_class;

	g_type_class_add_private (class, sizeof (EBookBackendWebdavPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = book_backend_webdav_dispose;
	object_class->finalize = book_backend_webdav_finalize;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = book_backend_webdav_authenticate_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (class);
	book_backend_class->get_backend_property = book_backend_webdav_get_backend_property;
	book_backend_class->open_sync = book_backend_webdav_open_sync;
	book_backend_class->create_contacts_sync = book_backend_webdav_create_contacts_sync;
	book_backend_class->modify_contacts_sync = book_backend_webdav_modify_contacts_sync;
	book_backend_class->remove_contacts_sync = book_backend_webdav_remove_contacts_sync;
	book_backend_class->get_contact_sync = book_backend_webdav_get_contact_sync;
	book_backend_class->get_contact_list_sync = book_backend_webdav_get_contact_list_sync;
	book_backend_class->start_view = e_book_backend_webdav_start_view;
	book_backend_class->stop_view = e_book_backend_webdav_stop_view;
	book_backend_class->refresh_sync = e_book_backend_webdav_refresh_sync;
}

static void
e_book_backend_webdav_init (EBookBackendWebdav *backend)
{
	backend->priv = E_BOOK_BACKEND_WEBDAV_GET_PRIVATE (backend);

	g_mutex_init (&backend->priv->cache_lock);
	g_mutex_init (&backend->priv->update_lock);

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_webdav_notify_online_cb), NULL);
}

