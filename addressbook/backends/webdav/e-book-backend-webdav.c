/* e-book-backend-webdav.c - Webdav contact backend.
 *
 * Copyright (C) 2008 Matthias Braun <matze@braunis.de>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by the
 * Free Software Foundation; either version 2.1 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Matthias Braun <matze@braunis.de>
 */

/*
 * Implementation notes:
 *   We use the DavResource URIs as UID in the evolution contact
 *   ETags are saved in the E_CONTACT_REV field so we know which cached contacts
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

#define EDB_ERROR(_code) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, NULL)
#define EDB_ERROR_EX(_code, _msg) e_data_book_create_error (E_DATA_BOOK_STATUS_ ## _code, _msg)

#define USERAGENT             "Evolution/" VERSION
#define WEBDAV_CLOSURE_NAME   "EBookBackendWebdav.BookView::closure"
#define WEBDAV_CTAG_KEY "WEBDAV_CTAG"

/* Forward Declarations */
static void	e_book_backend_webdav_source_authenticator_init
				(ESourceAuthenticatorInterface *interface);

G_DEFINE_TYPE_WITH_CODE (
	EBookBackendWebdav,
	e_book_backend_webdav,
	E_TYPE_BOOK_BACKEND,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_SOURCE_AUTHENTICATOR,
		e_book_backend_webdav_source_authenticator_init))

struct _EBookBackendWebdavPrivate {
	gboolean           marked_for_offline;
	SoupSession       *session;
	EProxy		  *proxy;
	gchar             *uri;
	gchar              *username;
	gchar              *password;
	gboolean supports_getctag;

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
closure_destroy (WebdavBackendSearchClosure *closure)
{
	e_flag_free (closure->running);
	g_free (closure);
}

static WebdavBackendSearchClosure *
init_closure (EDataBookView *book_view,
              EBookBackendWebdav *webdav)
{
	WebdavBackendSearchClosure *closure = g_new (WebdavBackendSearchClosure, 1);

	closure->webdav  = webdav;
	closure->thread  = NULL;
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

static EContact *
download_contact (EBookBackendWebdav *webdav,
                  const gchar *uri)
{
	SoupMessage *message;
	const gchar  *etag;
	EContact    *contact;
	guint        status;

	message = soup_message_new (SOUP_METHOD_GET, uri);
	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");

	status = soup_session_send_message (webdav->priv->session, message);
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

	etag = soup_message_headers_get (message->response_headers, "ETag");

	/* we use our URI as UID */
	contact = e_contact_new_from_vcard_with_uid (message->response_body->data, uri);
	if (contact == NULL) {
		g_warning ("Invalid vcard at '%s'", uri);
		g_object_unref (message);
		return NULL;
	}

	/* the etag is remembered in the revision field */
	if (etag != NULL) {
		e_contact_set (contact, E_CONTACT_REV, (gconstpointer) etag);
	}

	g_object_unref (message);
	return contact;
}

static guint
upload_contact (EBookBackendWebdav *webdav,
                EContact *contact,
                gchar **reason)
{
	ESource     *source;
	ESourceWebdav *webdav_extension;
	SoupMessage *message;
	gchar       *uri;
	gchar       *etag;
	const gchar  *new_etag, *redir_uri;
	gchar        *request;
	guint        status;
	gboolean     avoid_ifmatch;
	const gchar *extension_name;

	source = e_backend_get_source (E_BACKEND (webdav));

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	source = e_backend_get_source (E_BACKEND (webdav));

	uri = e_contact_get (contact, E_CONTACT_UID);
	if (uri == NULL) {
		g_warning ("can't upload contact without UID");
		return 400;
	}

	message = soup_message_new (SOUP_METHOD_PUT, uri);
	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");

	avoid_ifmatch = e_source_webdav_get_avoid_ifmatch (webdav_extension);

	/* some servers (like apache < 2.2.8) don't handle If-Match, correctly so
	 * we can leave it out */
	if (!avoid_ifmatch) {
		/* only override if etag is still the same on the server */
		etag = e_contact_get (contact, E_CONTACT_REV);
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
			g_free (etag);
		}
	}

	request = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	soup_message_set_request (
		message, "text/vcard", SOUP_MEMORY_TEMPORARY,
		request, strlen (request));

	status   = soup_session_send_message (webdav->priv->session, message);
	new_etag = soup_message_headers_get (message->response_headers, "ETag");

	redir_uri = soup_message_headers_get (message->response_headers, "Location");

	/* set UID and REV fields */
	e_contact_set (contact, E_CONTACT_REV, (gconstpointer) new_etag);
	if (redir_uri && *redir_uri) {
		if (!strstr (redir_uri, "://")) {
			/* it's a relative URI */
			SoupURI *suri = soup_uri_new (uri);
			gchar *full_uri;

			soup_uri_set_path (suri, redir_uri);
			full_uri = soup_uri_to_string (suri, TRUE);

			e_contact_set (contact, E_CONTACT_UID, full_uri);

			g_free (full_uri);
			soup_uri_free (suri);
		} else {
			e_contact_set (contact, E_CONTACT_UID, redir_uri);
		}
	} else {
		e_contact_set (contact, E_CONTACT_UID, uri);
	}

	if (reason) {
		*reason = g_strdup (message->reason_phrase && *message->reason_phrase ? message->reason_phrase :
				    (soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : _("Unknown error")));
	}

	g_object_unref (message);
	g_free (request);
	g_free (uri);

	return status;
}

static GError *
webdav_handle_auth_request (EBookBackendWebdav *webdav)
{
	EBookBackendWebdavPrivate *priv = webdav->priv;

	if (priv->username != NULL) {
		g_free (priv->username);
		priv->username = NULL;
		g_free (priv->password);
		priv->password = NULL;

		return EDB_ERROR (AUTHENTICATION_FAILED);
	} else {
		return EDB_ERROR (AUTHENTICATION_REQUIRED);
	}
}

static void
e_book_backend_webdav_create_contacts (EBookBackend *backend,
                                       EDataBook *book,
                                       guint32 opid,
                                       GCancellable *cancellable,
                                       const GSList *vcards)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv   = webdav->priv;
	EContact                  *contact;
	gchar                     *uid;
	guint                      status;
	gchar                     *status_reason = NULL;
	const gchar               *vcard = (const gchar *) vcards->data;
	GSList                     added_contacts = {NULL,};

	/* We make the assumption that the vCard list we're passed is always exactly one element long, since we haven't specified "bulk-adds"
	 * in our static capability list. This is because there is no clean way to roll back changes in case of an error. */
	if (vcards->next != NULL) {
		e_data_book_respond_create_contacts (
			book, opid,
			EDB_ERROR_EX (NOT_SUPPORTED,
			_("The backend does not support bulk additions")),
			NULL);
		return;
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_create_contacts (book, opid, EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	}

	/* do 3 rand() calls to construct a unique ID... poor way but should be
	 * good enough for us */
	uid = g_strdup_printf (
		"%s%08X-%08X-%08X.vcf",
		priv->uri, rand (), rand (), rand ());

	contact = e_contact_new_from_vcard_with_uid (vcard, uid);

	/* kill revision field (might have been set by some other backend) */
	e_contact_set (contact, E_CONTACT_REV, NULL);

	status = upload_contact (webdav, contact, &status_reason);
	if (status != 201 && status != 204) {
		g_object_unref (contact);
		if (status == 401 || status == 407) {
			e_data_book_respond_create_contacts (book, opid, webdav_handle_auth_request (webdav), NULL);
		} else {
			e_data_book_respond_create_contacts (
				book, opid,
				e_data_book_create_error_fmt (
				E_DATA_BOOK_STATUS_OTHER_ERROR,
				_("Create resource '%s' failed with HTTP status: %d (%s)"),
				uid, status, status_reason),
					NULL);
		}
		g_free (uid);
		g_free (status_reason);
		return;
	}

	g_free (status_reason);

	/* PUT request didn't return an etag? try downloading to get one */
	if (e_contact_get_const (contact, E_CONTACT_REV) == NULL) {
		const gchar *new_uid;
		EContact *new_contact;

		g_warning ("Server didn't return etag for new address resource");
		new_uid = e_contact_get_const (contact, E_CONTACT_UID);
		new_contact = download_contact (webdav, new_uid);
		g_object_unref (contact);

		if (new_contact == NULL) {
			e_data_book_respond_create_contacts (
				book, opid,
				EDB_ERROR (OTHER_ERROR), NULL);
			g_free (uid);
			return;
		}
		contact = new_contact;
	}

	e_book_backend_cache_add_contact (priv->cache, contact);

	added_contacts.data = contact;
	e_data_book_respond_create_contacts (book, opid, EDB_ERROR (SUCCESS), &added_contacts);

	g_object_unref (contact);
	g_free (uid);
}

static guint
delete_contact (EBookBackendWebdav *webdav,
                const gchar *uri)
{
	SoupMessage *message;
	guint        status;

	message = soup_message_new (SOUP_METHOD_DELETE, uri);
	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");

	status = soup_session_send_message (webdav->priv->session, message);
	g_object_unref (message);

	return status;
}

static void
e_book_backend_webdav_remove_contacts (EBookBackend *backend,
                                       EDataBook *book,
                                       guint32 opid,
                                       GCancellable *cancellable,
                                       const GSList *id_list)
{
	EBookBackendWebdav        *webdav      = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv        = webdav->priv;
	gchar                     *uid         = id_list->data;
	GSList                     deleted_ids = {NULL,};
	guint                      status;

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_remove_contacts (
			book, opid,
			EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	}

	/* We make the assumption that the ID list we're passed is always exactly one element long, since we haven't specified "bulk-removes"
	 * in our static capability list. */
	if (id_list->next != NULL) {
		e_data_book_respond_remove_contacts (
			book, opid,
			EDB_ERROR_EX (NOT_SUPPORTED,
			_("The backend does not support bulk removals")),
			NULL);
		return;
	}

	status = delete_contact (webdav, uid);
	if (status != 204) {
		if (status == 401 || status == 407) {
			e_data_book_respond_remove_contacts (
				book, opid,
				webdav_handle_auth_request (webdav), NULL);
		} else {
			g_warning ("DELETE failed with HTTP status %d", status);
			e_data_book_respond_remove_contacts (
				book, opid,
				e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR,
				_("DELETE failed with HTTP status %d"), status),
				NULL);
		}
		return;
	}

	e_book_backend_cache_remove_contact (priv->cache, uid);

	deleted_ids.data = uid;
	e_data_book_respond_remove_contacts (book, opid, EDB_ERROR (SUCCESS), &deleted_ids);
}

static void
e_book_backend_webdav_modify_contacts (EBookBackend *backend,
                                       EDataBook *book,
                                       guint32 opid,
                                       GCancellable *cancellable,
                                       const GSList *vcards)
{
	EBookBackendWebdav        *webdav  = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv    = webdav->priv;
	EContact                  *contact;
	GSList                     modified_contacts = {NULL,};
	const gchar                *uid;
	const gchar                *etag;
	guint status;
	gchar *status_reason = NULL;
	const gchar *vcard = vcards->data;

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_create_contacts (
			book, opid,
			EDB_ERROR (REPOSITORY_OFFLINE), NULL);
		return;
	}

	/* We make the assumption that the vCard list we're passed is always exactly one element long, since we haven't specified "bulk-modifies"
	 * in our static capability list. This is because there is no clean way to roll back changes in case of an error. */
	if (vcards->next != NULL) {
		e_data_book_respond_modify_contacts (
			book, opid,
			EDB_ERROR_EX (
				NOT_SUPPORTED,
				_("The backend does not support bulk modifications")),
			NULL);
		return;
	}

	/* modify contact */
	contact = e_contact_new_from_vcard (vcard);
	status = upload_contact (webdav, contact, &status_reason);
	if (status != 201 && status != 204) {
		g_object_unref (contact);
		if (status == 401 || status == 407) {
			e_data_book_respond_modify_contacts (book, opid, webdav_handle_auth_request (webdav), NULL);
			g_free (status_reason);
			return;
		}
		/* data changed on server while we were editing */
		if (status == 412) {
			/* too bad no special error code in evolution for this... */
			e_data_book_respond_modify_contacts (book, opid,
				e_data_book_create_error_fmt (
				E_DATA_BOOK_STATUS_OTHER_ERROR,
				_("Contact on server changed -> not modifying")),
				NULL);
			g_free (status_reason);
			return;
		}

		e_data_book_respond_modify_contacts (book, opid,
			e_data_book_create_error_fmt (
			E_DATA_BOOK_STATUS_OTHER_ERROR,
			_("Modify contact failed with HTTP status: %d (%s)"),
			status, status_reason),
			NULL);
		g_free (status_reason);
		return;
	}

	g_free (status_reason);

	uid = e_contact_get_const (contact, E_CONTACT_UID);
	e_book_backend_cache_remove_contact (priv->cache, uid);

	etag = e_contact_get_const (contact, E_CONTACT_REV);

	/* PUT request didn't return an etag? try downloading to get one */
	if (etag == NULL || (etag[0] == 'W' && etag[1] == '/')) {
		EContact *new_contact;

		g_warning ("Server didn't return etag for modified address resource");
		new_contact = download_contact (webdav, uid);
		if (new_contact != NULL) {
			contact = new_contact;
		}
	}
	e_book_backend_cache_add_contact (priv->cache, contact);

	modified_contacts.data = contact;
	e_data_book_respond_modify_contacts (book, opid, EDB_ERROR (SUCCESS), &modified_contacts);

	g_object_unref (contact);
}

static void
e_book_backend_webdav_get_contact (EBookBackend *backend,
                                   EDataBook *book,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar *uid)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv   = webdav->priv;
	EContact                  *contact;
	gchar                      *vcard;

	if (!e_backend_get_online (E_BACKEND (backend))) {
		contact = e_book_backend_cache_get_contact (priv->cache, uid);
	} else {
		contact = download_contact (webdav, uid);
		/* update cache as we possibly have changes */
		if (contact != NULL) {
			e_book_backend_cache_remove_contact (priv->cache, uid);
			e_book_backend_cache_add_contact (priv->cache, contact);
		}
	}

	if (contact == NULL) {
		e_data_book_respond_get_contact (book, opid, EDB_ERROR (CONTACT_NOT_FOUND), NULL);
		return;
	}

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	e_data_book_respond_get_contact (book, opid, EDB_ERROR (SUCCESS), vcard);
	g_free (vcard);
	g_object_unref (contact);
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
	xmlChar            *href  = NULL;
	xmlChar            *etag  = NULL;
	gint                 depth = xmlTextReaderDepth (reader);
	response_element_t *element;

	while (xmlTextReaderRead (reader) && xmlTextReaderDepth (reader) > depth) {
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
			while (xmlTextReaderRead (reader)
					&& xmlTextReaderDepth (reader) > depth2) {
				gint depth3;
				if (xmlTextReaderNodeType (reader) != XML_READER_TYPE_ELEMENT)
					continue;

				if (xmlTextReaderConstNamespaceUri (reader) != strings->dav
						|| xmlTextReaderConstLocalName (reader) != strings->prop)
					continue;

				depth3 = xmlTextReaderDepth (reader);
				while (xmlTextReaderRead (reader)
						&& xmlTextReaderDepth (reader) > depth3) {
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
	strings.multistatus
		= xmlTextReaderConstString (reader, BAD_CAST "multistatus");
	strings.dav         = xmlTextReaderConstString (reader, BAD_CAST "DAV:");
	strings.href        = xmlTextReaderConstString (reader, BAD_CAST "href");
	strings.response    = xmlTextReaderConstString (reader, BAD_CAST "response");
	strings.propstat    = xmlTextReaderConstString (reader, BAD_CAST "propstat");
	strings.prop        = xmlTextReaderConstString (reader, BAD_CAST "prop");
	strings.getetag     = xmlTextReaderConstString (reader, BAD_CAST "getetag");

	while (xmlTextReaderRead (reader)
			&& xmlTextReaderNodeType (reader) != XML_READER_TYPE_ELEMENT) {
	}

	if (xmlTextReaderConstLocalName (reader) != strings.multistatus
			|| xmlTextReaderConstNamespaceUri (reader) != strings.dav) {
		g_warning ("webdav PROPFIND result is not <DAV:multistatus>");
		return NULL;
	}

	elements = NULL;

	/* parse all DAV:response tags */
	while (xmlTextReaderRead (reader) && xmlTextReaderDepth (reader) > 0) {
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
		elements      = element;
	}

	return elements;
}

static SoupMessage *
send_propfind (EBookBackendWebdav *webdav)
{
	SoupMessage               *message;
	EBookBackendWebdavPrivate *priv    = webdav->priv;
	const gchar               *request =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<propfind xmlns=\"DAV:\"><prop><getetag/></prop></propfind>";

	message = soup_message_new (SOUP_METHOD_PROPFIND, priv->uri);
	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");
	soup_message_headers_append (message->request_headers, "Depth", "1");
	soup_message_set_request (
		message, "text/xml", SOUP_MEMORY_TEMPORARY,
		(gchar *) request, strlen (request));

	soup_session_send_message (priv->session, message);

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
                           gchar **new_ctag)
{
	gboolean res = TRUE;
	const gchar *request = "<?xml version=\"1.0\" encoding=\"utf-8\"?><propfind xmlns=\"DAV:\"><prop><getctag/></prop></propfind>";
	EBookBackendWebdavPrivate *priv;
	SoupMessage *message;

	g_return_val_if_fail (webdav != NULL, TRUE);
	g_return_val_if_fail (new_ctag != NULL, TRUE);

	*new_ctag = NULL;
	priv = webdav->priv;

	if (!priv->supports_getctag)
		return TRUE;

	priv->supports_getctag = FALSE;

	message = soup_message_new (SOUP_METHOD_PROPFIND, priv->uri);
	if (!message)
		return TRUE;

	soup_message_headers_append (message->request_headers, "User-Agent", USERAGENT);
	soup_message_headers_append (message->request_headers, "Connection", "close");
	soup_message_headers_append (message->request_headers, "Depth", "0");
	soup_message_set_request (message, "text/xml", SOUP_MEMORY_TEMPORARY, (gchar *) request, strlen (request));
	soup_session_send_message (priv->session, message);

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

						my_ctag = e_file_cache_get_object (E_FILE_CACHE (priv->cache), WEBDAV_CTAG_KEY);
						res = !my_ctag || !g_str_equal (my_ctag, *new_ctag);
						priv->supports_getctag = TRUE;
					}
				}

				g_free (txt);
			}

			xmlXPathFreeContext (xpctx);
			xmlFreeDoc (xml);
		}
	}

	g_object_unref (message);

	return res;
}

static GError *
download_contacts (EBookBackendWebdav *webdav,
                   EFlag *running,
                  EDataBookView *book_view)
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

	if (!check_addressbook_changed (webdav, &new_ctag)) {
		g_free (new_ctag);
		return EDB_ERROR (SUCCESS);
	}

	book_backend = E_BOOK_BACKEND (webdav);

	if (book_view != NULL) {
		e_data_book_view_notify_progress (book_view, -1,
				_("Loading Addressbook summary..."));
	}

	message = send_propfind (webdav);
	status  = message->status_code;

	if (status == 401 || status == 407) {
		g_object_unref (message);
		g_free (new_ctag);
		if (book_view)
			e_data_book_view_notify_progress (book_view, -1, NULL);
		return webdav_handle_auth_request (webdav);
	}
	if (status != 207) {
		GError *error;

		error = e_data_book_create_error_fmt (
			E_DATA_BOOK_STATUS_OTHER_ERROR,
			_("PROPFIND on webdav failed with HTTP status %d (%s)"),
			status,
			message->reason_phrase && *message->reason_phrase ? message->reason_phrase :
			(soup_status_get_phrase (message->status_code) ? soup_status_get_phrase (message->status_code) : _("Unknown error")));

		g_object_unref (message);
		g_free (new_ctag);

		if (book_view)
			e_data_book_view_notify_progress (book_view, -1, NULL);

		return error;
	}
	if (message->response_body == NULL) {
		g_warning ("No response body in webdav PROPFIND result");
		g_object_unref (message);
		g_free (new_ctag);

		if (book_view)
			e_data_book_view_notify_progress (book_view, -1, NULL);

		return e_data_book_create_error_fmt (E_DATA_BOOK_STATUS_OTHER_ERROR, _("No response body in webdav PROPFIND result"));
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

	/* download contacts */
	i = 0;
	for (element = elements; element != NULL; element = element->next, ++i) {
		const gchar  *uri;
		const gchar *etag;
		EContact    *contact;
		gchar *complete_uri;

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
			soup_uri->path    = g_strdup (uri);

			complete_uri = soup_uri_to_string (soup_uri, 0);
			soup_uri_free (soup_uri);
		} else {
			complete_uri = g_strdup (uri);
		}

		etag = (const gchar *) element->etag;

		contact = e_book_backend_cache_get_contact (priv->cache, complete_uri);
		/* download contact if it is not cached or its ETag changed */
		if (contact == NULL || etag == NULL ||
		    strcmp (e_contact_get_const (contact, E_CONTACT_REV), etag) != 0) {
			contact = download_contact (webdav, complete_uri);
			if (contact != NULL) {
				if (e_book_backend_cache_remove_contact (priv->cache, complete_uri))
					e_book_backend_notify_remove (book_backend, complete_uri);
				e_book_backend_cache_add_contact (priv->cache, contact);
				e_book_backend_notify_update (book_backend, contact);
			}
		}

		g_free (complete_uri);
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
		if (!e_file_cache_replace_object (E_FILE_CACHE (priv->cache), WEBDAV_CTAG_KEY, new_ctag))
			e_file_cache_add_object (E_FILE_CACHE (priv->cache), WEBDAV_CTAG_KEY, new_ctag);
	}
	g_free (new_ctag);

	if (book_view)
		e_data_book_view_notify_progress (book_view, -1, NULL);

	return EDB_ERROR (SUCCESS);
}

static gpointer
book_view_thread (gpointer data)
{
	EDataBookView                          *book_view = data;
	WebdavBackendSearchClosure             *closure   = get_closure (book_view);
	EBookBackendWebdav                     *webdav    = closure->webdav;
	GError *error;

	e_flag_set (closure->running);

	/* ref the book view because it'll be removed and unrefed when/if
	 * it's stopped */
	e_data_book_view_ref (book_view);

	error = download_contacts (webdav, closure->running, book_view);

	e_data_book_view_unref (book_view);

	if (error)
		g_error_free (error);

	return NULL;
}

static void
e_book_backend_webdav_start_book_view (EBookBackend *backend,
                                      EDataBookView *book_view)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv   = webdav->priv;
	const gchar *query = e_data_book_view_get_card_query (book_view);
	GList *contacts = e_book_backend_cache_get_contacts (priv->cache, query);
	GList *l;

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
		WebdavBackendSearchClosure *closure
			= init_closure (book_view, E_BOOK_BACKEND_WEBDAV (backend));

		closure->thread
			= g_thread_create (book_view_thread, book_view, TRUE, NULL);

		e_flag_wait (closure->running);
	}
}

static void
e_book_backend_webdav_stop_book_view (EBookBackend *backend,
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
	}
}

static void
e_book_backend_webdav_get_contact_list (EBookBackend *backend,
                                        EDataBook *book,
                                        guint32 opid,
                                        GCancellable *cancellable,
                                        const gchar *query)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv   = webdav->priv;
	GList                     *contact_list;
	GSList                    *vcard_list;
	GList                     *c;

	if (e_backend_get_online (E_BACKEND (backend))) {
		/* make sure the cache is up to date */
		GError *error = download_contacts (webdav, NULL, NULL);

		if (error) {
			e_data_book_respond_get_contact_list (book, opid, error, NULL);
			return;
		}
	}

	/* answer query from cache */
	contact_list = e_book_backend_cache_get_contacts (priv->cache, query);
	vcard_list   = NULL;
	for (c = contact_list; c != NULL; c = g_list_next (c)) {
		EContact *contact = c->data;
		gchar     *vcard
			= e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
		vcard_list = g_slist_append (vcard_list, vcard);
		g_object_unref (contact);
	}
	g_list_free (contact_list);

	e_data_book_respond_get_contact_list (book, opid, EDB_ERROR (SUCCESS), vcard_list);

	g_slist_foreach (vcard_list, (GFunc) g_free, NULL);
	g_slist_free (vcard_list);
}

static void
e_book_backend_webdav_get_contact_list_uids (EBookBackend *backend,
                                             EDataBook *book,
                                             guint32 opid,
                                             GCancellable *cancellable,
                                             const gchar *query)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv   = webdav->priv;
	GList                     *contact_list;
	GSList                    *uids_list;
	GList                     *c;

	if (e_backend_get_online (E_BACKEND (backend))) {
		/* make sure the cache is up to date */
		GError *error = download_contacts (webdav, NULL, NULL);

		if (error) {
			e_data_book_respond_get_contact_list_uids (book, opid, error, NULL);
			return;
		}
	}

	/* answer query from cache */
	contact_list = e_book_backend_cache_get_contacts (priv->cache, query);
	uids_list   = NULL;
	for (c = contact_list; c != NULL; c = g_list_next (c)) {
		EContact *contact = c->data;

		uids_list = g_slist_append (uids_list, e_contact_get (contact, E_CONTACT_UID));

		g_object_unref (contact);
	}
	g_list_free (contact_list);

	e_data_book_respond_get_contact_list_uids (book, opid, EDB_ERROR (SUCCESS), uids_list);

	g_slist_foreach (uids_list, (GFunc) g_free, NULL);
	g_slist_free (uids_list);
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
	EBookBackendWebdavPrivate *priv   = webdav->priv;

	if (retrying)
		return;

	if (priv->username != NULL) {
		soup_auth_authenticate (auth, priv->username, priv->password);
	}
}

static void
proxy_settings_changed (EProxy *proxy,
                        gpointer user_data)
{
	SoupURI *proxy_uri = NULL;
	EBookBackendWebdavPrivate *priv = (EBookBackendWebdavPrivate *) user_data;

	if (!priv || !priv->uri || !priv->session)
		return;

	/* use proxy if necessary */
	if (e_proxy_require_proxy_for_uri (proxy, priv->uri)) {
		proxy_uri = e_proxy_peek_uri_for (proxy, priv->uri);
	}

	g_object_set (priv->session, SOUP_SESSION_PROXY_URI, proxy_uri, NULL);
}

static void
e_book_backend_webdav_open (EBookBackend *backend,
                            EDataBook *book,
                            guint opid,
                            GCancellable *cancellable,
                            gboolean only_if_exists)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (backend);
	EBookBackendWebdavPrivate *priv   = webdav->priv;
	ESourceAuthentication     *auth_extension;
	ESourceOffline            *offline_extension;
	ESourceWebdav             *webdav_extension;
	ESource                   *source;
	const gchar               *extension_name;
	const gchar               *cache_dir;
	gchar                     *filename;
	SoupSession               *session;
	SoupURI                   *suri;
	GError                    *error = NULL;

	/* will try fetch ctag for the first time, if it fails then sets this to FALSE */
	priv->supports_getctag = TRUE;

	source = e_backend_get_source (E_BACKEND (backend));
	cache_dir = e_book_backend_get_cache_dir (backend);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	offline_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_WEBDAV_BACKEND;
	webdav_extension = e_source_get_extension (source, extension_name);

	priv->marked_for_offline =
		e_source_offline_get_stay_synchronized (offline_extension);

	if (!e_backend_get_online (E_BACKEND (backend)) && !priv->marked_for_offline ) {
		e_book_backend_respond_opened (backend, book, opid, EDB_ERROR (OFFLINE_UNAVAILABLE));
		return;
	}

	suri = e_source_webdav_dup_soup_uri (webdav_extension);

	priv->uri = soup_uri_to_string (suri, FALSE);
	if (!priv->uri) {
		soup_uri_free (suri);
		e_book_backend_respond_opened (backend, book, opid, EDB_ERROR_EX (OTHER_ERROR, _("Cannot transform SoupURI to string")));
		return;
	}

	filename = g_build_filename (cache_dir, "cache.xml", NULL);
	priv->cache = e_book_backend_cache_new (filename);
	g_free (filename);

	session = soup_session_sync_new ();
	g_object_set (session, SOUP_SESSION_TIMEOUT, 90, NULL);

	g_object_bind_property (
		webdav_extension, "ignore-invalid-cert",
		session, SOUP_SESSION_SSL_USE_SYSTEM_CA_FILE,
		G_BINDING_SYNC_CREATE |
		G_BINDING_INVERT_BOOLEAN);

	g_signal_connect (
		session, "authenticate",
		G_CALLBACK (soup_authenticate), webdav);

	priv->session = session;
	priv->proxy = e_proxy_new ();
	e_proxy_setup_proxy (priv->proxy);
	g_signal_connect (
		priv->proxy, "changed",
		G_CALLBACK (proxy_settings_changed), priv);
	proxy_settings_changed (priv->proxy, priv);
	webdav_debug_setup (priv->session);

	e_book_backend_notify_online (backend, TRUE);
	e_book_backend_notify_readonly (backend, FALSE);

	if (e_source_authentication_required (auth_extension))
		e_backend_authenticate_sync (
			E_BACKEND (backend),
			E_SOURCE_AUTHENTICATOR (backend),
			cancellable, &error);

	soup_uri_free (suri);

	/* This function frees the GError passed to it. */
	e_book_backend_respond_opened (backend, book, opid, error);
}

static void
e_book_backend_webdav_notify_online_cb (EBookBackend *backend,
                                        GParamSpec *pspec)
{
	/* set_mode is called before the backend is loaded */
	if (!e_book_backend_is_opened (backend))
		return;

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_book_backend_notify_readonly (backend, TRUE);
		e_book_backend_notify_online (backend, FALSE);
	} else {
		e_book_backend_notify_readonly (backend, FALSE);
		e_book_backend_notify_online (backend, TRUE);
	}
}

static void
e_book_backend_webdav_get_backend_property (EBookBackend *backend,
                                            EDataBook *book,
                                            guint32 opid,
                                            GCancellable *cancellable,
                                            const gchar *prop_name)
{
	g_return_if_fail (prop_name != NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, "net,do-initial-query,contact-lists");
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, e_contact_field_name (E_CONTACT_FILE_AS));
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		gchar *fields_str;
		GSList *fields = NULL;
		gint    i;

		/* we support everything */
		for (i = 1; i < E_CONTACT_FIELD_LAST; ++i) {
			fields = g_slist_append (fields, (gpointer) e_contact_field_name (i));
		}

		fields_str = e_data_book_string_slist_to_comma_string (fields);

		e_data_book_respond_get_backend_property (book, opid, NULL, fields_str);

		g_slist_free (fields);
		g_free (fields_str);
	} else if (g_str_equal (prop_name, BOOK_BACKEND_PROPERTY_SUPPORTED_AUTH_METHODS)) {
		e_data_book_respond_get_backend_property (book, opid, NULL, "plain/password");
	} else {
		E_BOOK_BACKEND_CLASS (e_book_backend_webdav_parent_class)->get_backend_property (backend, book, opid, cancellable, prop_name);
	}
}

static void
e_book_backend_webdav_dispose (GObject *object)
{
	EBookBackendWebdav        *webdav = E_BOOK_BACKEND_WEBDAV (object);
	EBookBackendWebdavPrivate *priv   = webdav->priv;

	#define do_unref(x) { if (x) g_object_unref (x); x = NULL; }

	do_unref (priv->session);
	do_unref (priv->proxy);
	do_unref (priv->cache);

	g_free (priv->uri);
	g_free (priv->username);
	g_free (priv->password);

	#undef do_unref

	G_OBJECT_CLASS (e_book_backend_webdav_parent_class)->dispose (object);
}

static ESourceAuthenticationResult
book_backend_webdav_try_password_sync (ESourceAuthenticator *authenticator,
                                       const GString *password,
                                       GCancellable *cancellable,
                                       GError **error)
{
	EBookBackendWebdav *webdav = E_BOOK_BACKEND_WEBDAV (authenticator);
	ESourceAuthentication *auth_extension;
	ESourceAuthenticationResult result;
	ESource *source;
	SoupMessage *message;
	const gchar *extension_name;

	source = e_backend_get_source (E_BACKEND (authenticator));
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	webdav->priv->username =
		e_source_authentication_dup_user (auth_extension);
	webdav->priv->password = g_strdup (password->str);

	/* Send a PROPFIND to test whether user/password is correct. */
	message = send_propfind (webdav);

	switch (message->status_code) {
		case SOUP_STATUS_OK:
		case SOUP_STATUS_MULTI_STATUS:
			result = E_SOURCE_AUTHENTICATION_ACCEPTED;
			break;

		case SOUP_STATUS_UNAUTHORIZED:
		case SOUP_STATUS_PROXY_UNAUTHORIZED:  /* XXX really? */
			g_free (webdav->priv->username);
			webdav->priv->username = NULL;
			g_free (webdav->priv->password);
			webdav->priv->password = NULL;
			result = E_SOURCE_AUTHENTICATION_REJECTED;
			break;

		default:
			g_set_error (
				error, SOUP_HTTP_ERROR,
				message->status_code,
				"%s", message->reason_phrase);
			result = E_SOURCE_AUTHENTICATION_ERROR;
			break;
	}

	g_object_unref (message);

	return result;
}

static void
e_book_backend_webdav_class_init (EBookBackendWebdavClass *class)
{
	GObjectClass      *object_class = G_OBJECT_CLASS (class);
	EBookBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (EBookBackendWebdavPrivate));

	backend_class = E_BOOK_BACKEND_CLASS (class);

	/* Set the virtual methods. */
	backend_class->open			= e_book_backend_webdav_open;
	backend_class->get_backend_property	= e_book_backend_webdav_get_backend_property;

	backend_class->create_contacts		= e_book_backend_webdav_create_contacts;
	backend_class->remove_contacts		= e_book_backend_webdav_remove_contacts;
	backend_class->modify_contacts		= e_book_backend_webdav_modify_contacts;
	backend_class->get_contact		= e_book_backend_webdav_get_contact;
	backend_class->get_contact_list		= e_book_backend_webdav_get_contact_list;
	backend_class->get_contact_list_uids	= e_book_backend_webdav_get_contact_list_uids;
	backend_class->start_book_view		= e_book_backend_webdav_start_book_view;
	backend_class->stop_book_view		= e_book_backend_webdav_stop_book_view;

	object_class->dispose			= e_book_backend_webdav_dispose;
}

static void
e_book_backend_webdav_source_authenticator_init (ESourceAuthenticatorInterface *interface)
{
	interface->try_password_sync = book_backend_webdav_try_password_sync;
}

static void
e_book_backend_webdav_init (EBookBackendWebdav *backend)
{
	backend->priv = E_BOOK_BACKEND_WEBDAV_GET_PRIVATE (backend);

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_webdav_notify_online_cb), NULL);
}

