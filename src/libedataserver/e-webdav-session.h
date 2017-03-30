/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2017 Red Hat, Inc. (www.redhat.com)
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
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_WEBDAV_SESSION_H
#define E_WEBDAV_SESSION_H

#include <glib.h>
#include <libxml/xpath.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserver/e-soup-session.h>
#include <libedataserver/e-source.h>
#include <libedataserver/e-xml-document.h>

/* Standard GObject macros */
#define E_TYPE_WEBDAV_SESSION \
	(e_webdav_session_get_type ())
#define E_WEBDAV_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_WEBDAV_SESSION, EWebDAVSession))
#define E_WEBDAV_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_WEBDAV_SESSION, EWebDAVSessionClass))
#define E_IS_WEBDAV_SESSION(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_WEBDAV_SESSION))
#define E_IS_WEBDAV_SESSION_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_WEBDAV_SESSION))
#define E_WEBDAV_SESSION_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_WEBDAV_SESSION, EWebDAVSessionClass))

G_BEGIN_DECLS

#define E_WEBDAV_CAPABILITY_CLASS_1			"1"
#define E_WEBDAV_CAPABILITY_CLASS_2			"2"
#define E_WEBDAV_CAPABILITY_CLASS_3			"3"
#define E_WEBDAV_CAPABILITY_ACCESS_CONTROL		"access-control"
#define E_WEBDAV_CAPABILITY_BIND			"bind"
#define E_WEBDAV_CAPABILITY_EXTENDED_MKCOL		"extended-mkcol"
#define E_WEBDAV_CAPABILITY_ADDRESSBOOK			"addressbook"
#define E_WEBDAV_CAPABILITY_CALENDAR_ACCESS		"calendar-access"
#define E_WEBDAV_CAPABILITY_CALENDAR_SCHEDULE		"calendar-schedule"
#define E_WEBDAV_CAPABILITY_CALENDAR_AUTO_SCHEDULE	"calendar-auto-schedule"
#define E_WEBDAV_CAPABILITY_CALENDAR_PROXY		"calendar-proxy"

#define E_WEBDAV_ALLOW_OPTIONS			"OPTIONS"
#define E_WEBDAV_ALLOW_PROPFIND			"PROPFIND"
#define E_WEBDAV_ALLOW_REPORT			"REPORT"
#define E_WEBDAV_ALLOW_DELETE			"DELETE"
#define E_WEBDAV_ALLOW_GET			"GET"
#define E_WEBDAV_ALLOW_PUT			"PUT"
#define E_WEBDAV_ALLOW_HEAD			"HEAD"
#define E_WEBDAV_ALLOW_ACL			"ACL"
#define E_WEBDAV_ALLOW_LOCK			"LOCK"
#define E_WEBDAV_ALLOW_UNLOCK			"UNLOCK"
#define E_WEBDAV_ALLOW_MOVE			"MOVE"
#define E_WEBDAV_ALLOW_MKTICKET			"MKTICKET"
#define E_WEBDAV_ALLOW_DELTICKET		"DELTICKET"

#define E_WEBDAV_DEPTH_THIS			"0"
#define E_WEBDAV_DEPTH_THIS_AND_CHILDREN	"1"
#define E_WEBDAV_DEPTH_INFINITY			"infinity"

#define E_WEBDAV_CONTENT_TYPE_XML		"application/xml; charset=\"utf-8\""
#define E_WEBDAV_CONTENT_TYPE_CALENDAR		"text/calendar; charset=\"utf-8\""
#define E_WEBDAV_CONTENT_TYPE_VCARD		"text/vcard; charset=\"utf-8\""

#define E_WEBDAV_NS_DAV				"DAV:"
#define E_WEBDAV_NS_CALDAV			"urn:ietf:params:xml:ns:caldav"
#define E_WEBDAV_NS_CARDDAV			"urn:ietf:params:xml:ns:carddav"
#define E_WEBDAV_NS_CALENDARSERVER		"http://calendarserver.org/ns/"
#define E_WEBDAV_NS_ICAL			"http://apple.com/ns/ical/"

typedef struct _EWebDAVSession EWebDAVSession;
typedef struct _EWebDAVSessionClass EWebDAVSessionClass;
typedef struct _EWebDAVSessionPrivate EWebDAVSessionPrivate;

typedef enum {
	E_WEBDAV_RESOURCE_KIND_UNKNOWN,
	E_WEBDAV_RESOURCE_KIND_ADDRESSBOOK,
	E_WEBDAV_RESOURCE_KIND_CALENDAR,
	E_WEBDAV_RESOURCE_KIND_PRINCIPAL,
	E_WEBDAV_RESOURCE_KIND_COLLECTION,
	E_WEBDAV_RESOURCE_KIND_RESOURCE
} EWebDAVResourceKind;

typedef enum {
	E_WEBDAV_RESOURCE_SUPPORTS_NONE		= 0,
	E_WEBDAV_RESOURCE_SUPPORTS_CONTACTS	= 1 << 0,
	E_WEBDAV_RESOURCE_SUPPORTS_EVENTS	= 1 << 1,
	E_WEBDAV_RESOURCE_SUPPORTS_MEMOS	= 1 << 2,
	E_WEBDAV_RESOURCE_SUPPORTS_TASKS	= 1 << 3,
	E_WEBDAV_RESOURCE_SUPPORTS_FREEBUSY	= 1 << 4
} EWebDAVResourceSupports;

typedef struct _EWebDAVResource {
	EWebDAVResourceKind kind;
	guint32 supports;
	gchar *href;
	gchar *etag;
	gchar *display_name;
	gchar *content_type;
	gsize content_length;
	glong creation_date;
	glong last_modified;
	gchar *description;
	gchar *color;
} EWebDAVResource;

GType		e_webdav_resource_get_type		(void) G_GNUC_CONST;
EWebDAVResource *
		e_webdav_resource_new			(EWebDAVResourceKind kind,
							 guint32 supports,
							 const gchar *href,
							 const gchar *etag,
							 const gchar *display_name,
							 const gchar *content_type,
							 gsize content_length,
							 glong creation_date,
							 glong last_modified,
							 const gchar *description,
							 const gchar *color);
EWebDAVResource *
		e_webdav_resource_copy			(const EWebDAVResource *src);
void		e_webdav_resource_free			(gpointer ptr /* EWebDAVResource * */);

typedef enum {
	E_WEBDAV_LIST_ALL		= 0xFFFFFFFF,
	E_WEBDAV_LIST_NONE		= 0,
	E_WEBDAV_LIST_SUPPORTS		= 1 << 0,
	E_WEBDAV_LIST_ETAG		= 1 << 1,
	E_WEBDAV_LIST_DISPLAY_NAME	= 1 << 2,
	E_WEBDAV_LIST_CONTENT_TYPE	= 1 << 3,
	E_WEBDAV_LIST_CONTENT_LENGTH	= 1 << 4,
	E_WEBDAV_LIST_CREATION_DATE	= 1 << 5,
	E_WEBDAV_LIST_LAST_MODIFIED	= 1 << 6,
	E_WEBDAV_LIST_DESCRIPTION	= 1 << 7,
	E_WEBDAV_LIST_COLOR		= 1 << 8
} EWebDAVListFlags;

/**
 * EWebDAVMultistatusTraverseFunc:
 * @webdav: an #EWebDAVSession
 * @xpath_ctx: an #xmlXPathContextPtr
 * @xpath_prop_prefix: (nullable): an XPath prefix for the current prop element, without trailing forward slash
 * @request_uri: a #SoupURI, containing the request URI, maybe redirected by the server
 * @status_code: an HTTP status code for this property
 * @user_data: user data, as passed to e_webdav_session_propfind_sync()
 *
 * A callback function for e_webdav_session_propfind_sync().
 *
 * The @xpath_prop_prefix can be %NULL only once, for the first time,
 * which is meant to let the caller setup the @xpath_ctx, like to register
 * its own namespaces to it with e_xml_xpath_context_register_namespaces().
 * All other invocations of the function will have @xpath_prop_prefix non-%NULL.
 *
 * Returns: %TRUE to continue traversal of the returned multistatus response,
 *    %FALSE otherwise.
 *
 * Since: 3.26
 **/
typedef gboolean (* EWebDAVMultistatusTraverseFunc)	(EWebDAVSession *webdav,
							 xmlXPathContextPtr xpath_ctx,
							 const gchar *xpath_prop_prefix,
							 const SoupURI *request_uri,
							 guint status_code,
							 gpointer user_data);

typedef enum {
	E_WEBDAV_PROPERTY_SET,
	E_WEBDAV_PROPERTY_REMOVE
} EWebDAVPropertyChangeKind;

typedef struct _EWebDAVPropertyChange {
	EWebDAVPropertyChangeKind kind;
	gchar *ns_uri;
	gchar *name;
	gchar *value;
} EWebDAVPropertyChange;

GType		e_webdav_property_change_get_type	(void) G_GNUC_CONST;
EWebDAVPropertyChange *
		e_webdav_property_change_new_set	(const gchar *ns_uri,
							 const gchar *name,
							 const gchar *value);
EWebDAVPropertyChange *
		e_webdav_property_change_new_remove	(const gchar *ns_uri,
							 const gchar *name);
EWebDAVPropertyChange *
		e_webdav_property_change_copy		(const EWebDAVPropertyChange *src);
void		e_webdav_property_change_free		(gpointer ptr); /* EWebDAVPropertyChange * */

/**
 * EWebDAVSession:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.26
 **/
struct _EWebDAVSession {
	/*< private >*/
	ESoupSession parent;
	EWebDAVSessionPrivate *priv;
};

struct _EWebDAVSessionClass {
	ESoupSessionClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[10];
};

GType		e_webdav_session_get_type		(void) G_GNUC_CONST;

EWebDAVSession *e_webdav_session_new			(ESource *source);
gboolean	e_webdav_session_options_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 GHashTable **out_capabilities,
							 GHashTable **out_allows,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_propfind_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 const gchar *depth,
							 const EXmlDocument *xml,
							 EWebDAVMultistatusTraverseFunc func,
							 gpointer func_user_data,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_proppatch_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 const EXmlDocument *xml,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_mkcol_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_get_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 gchar **out_href,
							 gchar **out_etag,
							 GOutputStream *out_stream,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_get_data_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 gchar **out_href,
							 gchar **out_etag,
							 gchar **out_bytes,
							 gsize *out_length,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_put_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 const gchar *etag,
							 const gchar *content_type,
							 GInputStream *stream,
							 gchar **out_href,
							 gchar **out_etag,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_put_data_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 const gchar *etag,
							 const gchar *content_type,
							 const gchar *bytes,
							 gsize length,
							 gchar **out_href,
							 gchar **out_etag,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_delete_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 const gchar *depth,
							 const gchar *etag,
							 GCancellable *cancellable,
							 GError **error);
/*gboolean	e_webdav_session_copy_sync		(EWebDAVSession *webdav,
							 const gchar *source_uri,
							 const gchar *destination_uri,
							 const gchar *depth,
							 gboolean can_overwrite,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_move_sync		(EWebDAVSession *webdav,
							 const gchar *source_uri,
							 const gchar *destination_uri,
							 gboolean can_overwrite,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_lock_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 const gchar *depth,
							 const gchar *lock_token,
							 guint32 lock_timeout,
							 const EXmlDocument *xml,
							 gchar **out_lock_token,
							 EXmlDocument **out_xml_response,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_unlock_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 const gchar *lock_token,
							 GCancellable *cancellable,
							 GError **error);*/

gboolean	e_webdav_session_traverse_multistatus_response
							(EWebDAVSession *webdav,
							 const SoupMessage *message,
							 const GByteArray *xml_data,
							 EWebDAVMultistatusTraverseFunc func,
							 gpointer func_user_data,
							 GError **error);
gboolean	e_webdav_session_getctag_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 gchar **out_ctag,
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_list_sync		(EWebDAVSession *webdav,
							 const gchar *uri,
							 const gchar *depth,
							 guint32 flags, /* bit-or of EWebDAVListFlags */
							 GSList **out_resources, /* EWebDAVResource * */
							 GCancellable *cancellable,
							 GError **error);
gboolean	e_webdav_session_update_properties_sync	(EWebDAVSession *webdav,
							 const gchar *uri,
							 const GSList *changes, /* EWebDAVPropertyChange * */
							 GCancellable *cancellable,
							 GError **error);

G_END_DECLS

#endif /* E_WEBDAV_SESSION_H */
