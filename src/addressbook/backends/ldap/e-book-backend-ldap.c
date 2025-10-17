/* e-book-backend-ldap.c - LDAP contact backend.
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
 * Authors: Chris Toshok <toshok@ximian.com>
 *          Hans Petter Jansson <hpj@novell.com>
 */

#define DEBUG

#include "evolution-data-server-config.h"

#include <stdlib.h>
#include <fcntl.h>
#include <string.h>

#include <glib.h>

#ifdef DEBUG
#define LDAP_DEBUG
#define LDAP_DEBUG_ADD
#define LDAP_DEBUG_MODIFY
#endif
#include <ldap.h>
#ifdef DEBUG
#undef LDAP_DEBUG
#endif

#define d(x)

/* LDAP_VENDOR_VERSION is 0 if OpenLDAP is built from git/master */
#if !defined(LDAP_VENDOR_VERSION) || LDAP_VENDOR_VERSION == 0 || LDAP_VENDOR_VERSION > 20000
#define OPENLDAP2
#else
#define OPENLDAP1
#endif

#ifdef OPENLDAP2
#ifndef SUNLDAP
#include <ldap_schema.h>
#endif
#endif

#ifdef SUNLDAP
#include "openldap-extract.h"
#endif

#include <sys/time.h>

#include <glib/gi18n-lib.h>

#include <libedataserver/libedataserver.h>

#include "e-book-backend-ldap.h"

/* this is broken currently, don't enable it */
/*#define ENABLE_SASL_BINDS*/

/* interval for our poll_ldap timeout */
#define LDAP_POLL_INTERVAL 20

/* timeout for ldap_result */
#define LDAP_RESULT_TIMEOUT_MILLIS 10

#define LDAP_SEARCH_OP_IDENT "EBookBackendLDAP.BookView::search_op"

/* the objectClasses we need */
#define TOP                  "top"
#define PERSON               "person"
#define ORGANIZATIONALPERSON "organizationalPerson"
#define INETORGPERSON        "inetOrgPerson"
#define CALENTRY             "calEntry"
#define EVOLUTIONPERSON      "evolutionPerson"
#define GROUPOFNAMES         "groupOfNames"

static gboolean enable_debug = FALSE;

static const gchar *
		query_prop_to_ldap		(const gchar *query_prop,
						 gboolean evolution_person_supported,
						 gboolean calentry_supported);
static gchar *	e_book_backend_ldap_build_query	(EBookBackendLDAP *bl,
						 const gchar *query);

typedef struct LDAPOp LDAPOp;

#define EC_ERROR(_code) e_client_error_create (_code, NULL)
#define EC_ERROR_EX(_code, _msg)  e_client_error_create (_code, _msg)
#define EBC_ERROR(_code) e_book_client_error_create (_code, NULL)

/* Translators: An error message shown to a user when trying to do an
 * operation on the LDAP address book which is not connected to the server */
#define EC_ERROR_NOT_CONNECTED() e_client_error_create (E_CLIENT_ERROR_OTHER_ERROR, _("Not connected"))
#define EC_ERROR_MSG_TYPE(_msg_type) e_client_error_create_fmt (E_CLIENT_ERROR_INVALID_ARG, "Incorrect msg type %d passed to %s", _msg_type, G_STRFUNC)

struct _EBookBackendLDAPPrivate {
	gboolean connected;

	gchar    *ldap_host;   /* the hostname of the server */
	gint      ldap_port;    /* the port of the server */
	gchar     *schema_dn;   /* the base dn for schema information */
	gchar    *ldap_rootdn; /* the base dn of our searches */
	gint      ldap_scope;   /* the scope used for searches */
	gchar	*ldap_search_filter;
	gint      ldap_limit;   /* the search limit */
	gint      ldap_timeout; /* the search timeout */

	gchar   *auth_dn;
	gchar   *auth_secret;

	gboolean ldap_v3;      /* TRUE if the server supports protocol
                                  revision 3 (necessary for TLS) */
	gboolean starttls;     /* TRUE if the *library * supports
                                  starttls.  will be false if openssl
                                  was not built into openldap. */
	ESourceLDAPSecurity security;

	LDAP     *ldap;

	GSList   *supported_fields;
	GSList   *supported_auth_methods;

	EBookBackendCache *cache;

	/* whether or not there's support for the objectclass we need
 *         to store all our additional fields */
	gboolean evolutionPersonSupported;
	gboolean calEntrySupported;
	gboolean evolutionPersonChecked;
	gboolean marked_for_offline;
	gboolean marked_can_browse;

	/* our operations */
	GRecMutex op_hash_mutex; /* lock also eds_ldap_handler_lock before this lock */
	GHashTable *id_to_op;
	gint active_ops;
	guint poll_timeout;

	/* summary file related */
	gchar *summary_file_name;
	gboolean is_summary_ready;
	EBookBackendSummary *summary;

	gboolean generate_cache_in_progress; /* set to TRUE, when updating local cache for offline */

	GMutex view_mutex;
};

G_DEFINE_TYPE_WITH_PRIVATE (EBookBackendLDAP, e_book_backend_ldap, E_TYPE_BOOK_BACKEND)

typedef void (*LDAPOpHandler)(LDAPOp *op, LDAPMessage *res);
typedef void (*LDAPOpDtor)(LDAPOp *op);

struct LDAPOp {
	LDAPOpHandler  handler;
	LDAPOpDtor     dtor;
	EBookBackend  *backend;
	EDataBook     *book;
	EDataBookView *view;
	guint32        opid; /* the libebook operation id */
	gint            id;   /* the ldap msg id */
};

/* every access to priv->ldap should be guarded with this lock */
static GRecMutex eds_ldap_handler_lock;

static void     ldap_op_add (LDAPOp *op, EBookBackend *backend, EDataBook *book,
			     EDataBookView *view, gint opid, gint msgid, LDAPOpHandler handler, LDAPOpDtor dtor);
static void     ldap_op_finished (LDAPOp *op);

static gboolean poll_ldap (gpointer user_data);

static EContact *build_contact_from_entry (EBookBackendLDAP *bl, LDAPMessage *e, GList **existing_objectclasses, gchar **ldap_uid);

static void email_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval ** email_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean email_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static void member_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval ** member_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean member_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static void homephone_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval ** homephone_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean homephone_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static void business_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval ** business_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean business_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static void anniversary_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval ** anniversary_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean anniversary_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static void birthday_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval ** birthday_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean birthday_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static void category_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval ** category_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean category_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static void home_address_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static struct berval **home_address_ber (EBookBackendLDAP *self, EContact *card, const gchar *ldap_attr, GError **error);
static gboolean home_address_compare (EBookBackendLDAP *self, EContact *ecard1, EContact * ecard2, const gchar *ldap_attr);

static void work_address_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static struct berval **work_address_ber (EBookBackendLDAP *self, EContact *card, const gchar *ldap_attr, GError **error);
static gboolean work_address_compare (EBookBackendLDAP *self, EContact *ecard1, EContact * ecard2, const gchar *ldap_attr);

static void other_address_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static struct berval **other_address_ber (EBookBackendLDAP *self, EContact *card, const gchar *ldap_attr, GError **error);
static gboolean other_address_compare (EBookBackendLDAP *self, EContact *ecard1, EContact * ecard2, const gchar *ldap_attr);

static void work_city_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static void work_state_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static void work_po_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static void work_zip_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static void work_country_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static void home_city_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static void home_state_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static void home_zip_populate (EBookBackendLDAP *self, EContact *card, gchar **values);
static void home_country_populate (EBookBackendLDAP *self, EContact *card, gchar **values);

static void photo_populate (EBookBackendLDAP *self, EContact *contact, struct berval **ber_values);
static struct berval **photo_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean photo_compare (EBookBackendLDAP *self, EContact *ecard1, EContact *ecard2, const gchar *ldap_attr);

static void cert_populate (EBookBackendLDAP *self, EContact *contact, struct berval **ber_values);
static struct berval **cert_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean cert_compare (EBookBackendLDAP *self, EContact *ecard1, EContact *ecard2, const gchar *ldap_attr);

static void org_unit_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval ** org_unit_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean org_unit_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static void nickname_populate (EBookBackendLDAP *self, EContact *contact, gchar **values);
static struct berval **nickname_ber (EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
static gboolean nickname_compare (EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

static struct prop_info {
	EContactField field_id;
	const gchar *ldap_attr;
#define PROP_TYPE_STRING	(1 << 0)
#define PROP_TYPE_COMPLEX	(1 << 1)
#define PROP_TYPE_BINARY	(1 << 2)
#define PROP_CALENTRY		(1 << 3)
#define PROP_EVOLVE		(1 << 4)
#define PROP_WRITE_ONLY		(1 << 5)
#define PROP_TYPE_GROUP		(1 << 6)
#define PROP_TYPE_CONTACT	(1 << 7) /* is ignored for contact lists */
#define PROP_TYPE_FORCE_BINARY	(1 << 8) /* to force ";binary" in attribute name */
#define PROP_WITH_EVOSCHEME	(1 << 9)
#define PROP_WITHOUT_EVOSCHEME	(1 << 10)
	gint prop_type;

	/* the remaining items are only used for the TYPE_COMPLEX props */

	/* used when reading from the ldap server populates EContact with the values in **values. */
	void (*populate_contact_func)(EBookBackendLDAP *self, EContact *contact, gchar **values);
	/* used when writing to an ldap server.  returns a NULL terminated array of berval*'s */
	struct berval ** (*ber_func)(EBookBackendLDAP *self, EContact *contact, const gchar *ldap_attr, GError **error);
	/* used to compare list attributes */
	gboolean (*compare_func)(EBookBackendLDAP *self, EContact *contact1, EContact *contact2, const gchar *ldap_attr);

	void (*binary_populate_contact_func)(EBookBackendLDAP *self, EContact *contact, struct berval **ber_values);

} prop_info[] = {

#define BINARY_PROP(fid,a,ctor,ber,cmp) {fid, a, PROP_TYPE_BINARY, NULL, ber, cmp, ctor}
#define BINARY_PROP_FORCED(fid,a,ctor,ber,cmp) {fid, a, PROP_TYPE_BINARY | PROP_TYPE_FORCE_BINARY, NULL, ber, cmp, ctor}
#define COMPLEX_PROP(fid,a,ctor,ber,cmp) {fid, a, PROP_TYPE_COMPLEX, ctor, ber, cmp}
#define E_COMPLEX_PROP(fid,a,ctor,ber,cmp) {fid, a, PROP_TYPE_COMPLEX | PROP_EVOLVE, ctor, ber, cmp}
#define STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING}
#define WRITE_ONLY_STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING | PROP_WRITE_ONLY}
#define E_STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING | PROP_EVOLVE}
#define GROUP_PROP(fid,a,ctor,ber,cmp) {fid, a, PROP_TYPE_GROUP, ctor, ber, cmp}
#define ADDRESS_STRING_PROP(fid,a, ctor) {fid, a, PROP_TYPE_COMPLEX, ctor}
#define CONTACT_STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING | PROP_TYPE_CONTACT}
#define CONTACT_STRING_PROP_WITH_EVOSCHEME(fid,a) {fid, a, PROP_TYPE_STRING | PROP_TYPE_CONTACT | PROP_WITH_EVOSCHEME}
#define CONTACT_STRING_PROP_WITHOUT_EVOSCHEME(fid,a) {fid, a, PROP_TYPE_STRING | PROP_TYPE_CONTACT | PROP_WITHOUT_EVOSCHEME}
#define CALENTRY_CONTACT_STRING_PROP(fid,a) {fid, a, PROP_TYPE_STRING | PROP_TYPE_CONTACT | PROP_CALENTRY}

	/* name fields */
	STRING_PROP (E_CONTACT_FULL_NAME,   "cn" ),
	/* WRITE_ONLY_STRING_PROP (E_CONTACT_FAMILY_NAME, "sn" ), */
	CONTACT_STRING_PROP (E_CONTACT_GIVEN_NAME, "givenName"),
	CONTACT_STRING_PROP (E_CONTACT_FAMILY_NAME, "sn" ),

	/* email addresses */
	COMPLEX_PROP   (E_CONTACT_EMAIL, "mail", email_populate, email_ber, email_compare),
	GROUP_PROP   (E_CONTACT_EMAIL, "member", member_populate, member_ber, member_compare),

	/* phone numbers */
	E_STRING_PROP (E_CONTACT_PHONE_PRIMARY,      "primaryPhone"),
	COMPLEX_PROP  (E_CONTACT_PHONE_BUSINESS,     "telephoneNumber", business_populate, business_ber, business_compare),
	COMPLEX_PROP  (E_CONTACT_PHONE_HOME,         "homePhone", homephone_populate, homephone_ber, homephone_compare),
	CONTACT_STRING_PROP   (E_CONTACT_PHONE_MOBILE,       "mobile"),
	E_STRING_PROP (E_CONTACT_PHONE_CAR,          "carPhone"),
	CONTACT_STRING_PROP   (E_CONTACT_PHONE_BUSINESS_FAX, "facsimileTelephoneNumber"),
	E_STRING_PROP (E_CONTACT_PHONE_HOME_FAX,     "homeFacsimileTelephoneNumber"),
	E_STRING_PROP (E_CONTACT_PHONE_OTHER,        "otherPhone"),
	E_STRING_PROP (E_CONTACT_PHONE_OTHER_FAX,    "otherFacsimileTelephoneNumber"),
	CONTACT_STRING_PROP   (E_CONTACT_PHONE_ISDN,         "internationaliSDNNumber"),
	CONTACT_STRING_PROP   (E_CONTACT_PHONE_PAGER,        "pager"),
	E_STRING_PROP (E_CONTACT_PHONE_RADIO,        "radio"),
	E_STRING_PROP (E_CONTACT_PHONE_TELEX,        "telex"),
	E_STRING_PROP (E_CONTACT_PHONE_ASSISTANT,    "assistantPhone"),
	E_STRING_PROP (E_CONTACT_PHONE_COMPANY,      "companyPhone"),
	E_STRING_PROP (E_CONTACT_PHONE_CALLBACK,     "callbackPhone"),
	E_STRING_PROP (E_CONTACT_PHONE_TTYTDD,       "tty"),

	/* org information */
	CONTACT_STRING_PROP   (E_CONTACT_ORG,       "o"),
	COMPLEX_PROP  (E_CONTACT_ORG_UNIT,          "ou", org_unit_populate, org_unit_ber, org_unit_compare),
	COMPLEX_PROP  (E_CONTACT_ORG_UNIT,          "departmentNumber", org_unit_populate, org_unit_ber, org_unit_compare), /* Keep this after "ou" */
	CONTACT_STRING_PROP   (E_CONTACT_OFFICE,    "roomNumber"),
	CONTACT_STRING_PROP   (E_CONTACT_TITLE,     "title"),
	E_STRING_PROP (E_CONTACT_ROLE,      "businessRole"),
	E_STRING_PROP (E_CONTACT_MANAGER,   "managerName"),
	E_STRING_PROP (E_CONTACT_ASSISTANT, "assistantName"),

	/* addresses */
	COMPLEX_PROP  (E_CONTACT_ADDRESS_LABEL_WORK, "postalAddress", work_address_populate, work_address_ber, work_address_compare),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_WORK, "l", work_city_populate),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_WORK, "st", work_state_populate),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_WORK, "postofficebox", work_po_populate),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_WORK, "postalcode", work_zip_populate),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_WORK, "c", work_country_populate),

	COMPLEX_PROP  (E_CONTACT_ADDRESS_LABEL_HOME, "homePostalAddress", home_address_populate, home_address_ber, home_address_compare),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_HOME, "mozillaHomeLocalityName", home_city_populate),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_HOME, "mozillaHomeState", home_state_populate),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_HOME, "mozillaHomePostalCode", home_zip_populate),
	ADDRESS_STRING_PROP (E_CONTACT_ADDRESS_HOME, "mozillaHomeCountryName", home_country_populate),

	E_COMPLEX_PROP (E_CONTACT_ADDRESS_LABEL_OTHER, "otherPostalAddress", other_address_populate, other_address_ber, other_address_compare),

	/* photos */
	BINARY_PROP  (E_CONTACT_PHOTO,       "jpegPhoto", photo_populate, photo_ber, photo_compare),

	/* certificate foo. */
	BINARY_PROP_FORCED (E_CONTACT_X509_CERT,   "userCertificate", cert_populate, cert_ber, cert_compare),
#if 0
	/* hm, which do we use?  the inetOrgPerson schema says that
	 * userSMIMECertificate should be used in favor of
	 * userCertificate for S/MIME applications. */
	BINARY_PROP  (E_CONTACT_X509_CERT,   "userSMIMECertificate", cert_populate, cert_ber, cert_compare),
#endif

	/* misc fields */
	CONTACT_STRING_PROP    (E_CONTACT_HOMEPAGE_URL,  "labeledURI"),
	/* map nickname to the displayName with the evo scheme, or possibly prefill the file-as without the evo scheme */
	COMPLEX_PROP   (E_CONTACT_NICKNAME, "displayName", nickname_populate, nickname_ber, nickname_compare),
	E_STRING_PROP  (E_CONTACT_SPOUSE,      "spouseName"),
	CONTACT_STRING_PROP_WITH_EVOSCHEME (E_CONTACT_NOTE, "note"),
	CONTACT_STRING_PROP_WITHOUT_EVOSCHEME (E_CONTACT_NOTE, "description"),
	E_COMPLEX_PROP (E_CONTACT_ANNIVERSARY, "anniversary", anniversary_populate, anniversary_ber, anniversary_compare),
	E_COMPLEX_PROP (E_CONTACT_BIRTH_DATE,  "birthDate", birthday_populate, birthday_ber, birthday_compare),
	E_STRING_PROP  (E_CONTACT_MAILER,      "mailer"),

	E_STRING_PROP  (E_CONTACT_FILE_AS,     "fileAs"),

	E_COMPLEX_PROP (E_CONTACT_CATEGORY_LIST,  "category", category_populate, category_ber, category_compare),

	CALENTRY_CONTACT_STRING_PROP (E_CONTACT_CALENDAR_URI,   "calCalURI"),
	CALENTRY_CONTACT_STRING_PROP (E_CONTACT_FREEBUSY_URL,   "calFBURL"),
	CONTACT_STRING_PROP (E_CONTACT_ICS_CALENDAR,   "icsCalendar"),

#undef E_STRING_PROP
#undef STRING_PROP
#undef E_COMPLEX_PROP
#undef COMPLEX_PROP
#undef GROUP_PROP
#undef CONTACT_STRING_PROP
};

static gboolean
can_browse (EBookBackend *backend)
{
	ESource *source;
	ESourceLDAP *extension;
	const gchar *extension_name;

	if (!E_IS_BOOK_BACKEND (backend))
		return FALSE;

	source = e_backend_get_source (E_BACKEND (backend));
	extension_name = E_SOURCE_EXTENSION_LDAP_BACKEND;
	extension = e_source_get_extension (source, extension_name);

	return e_source_ldap_get_can_browse (extension);
}

/* because the priv->marked_for_offline is populated only after open,
 * thus get the actual value directy from the source */
static gboolean
get_marked_for_offline (EBookBackend *backend)
{
	ESource *source;
	ESourceOffline *extension;

	if (!E_IS_BOOK_BACKEND (backend))
		return FALSE;

	source = e_backend_get_source (E_BACKEND (backend));
	extension = e_source_get_extension (source, E_SOURCE_EXTENSION_OFFLINE);

	return e_source_offline_get_stay_synchronized (extension);
}

static EDataBookView *
find_book_view (EBookBackendLDAP *bl)
{
	EDataBookView *view = NULL;
	GList *list;

	list = e_book_backend_list_views (E_BOOK_BACKEND (bl));

	if (list != NULL) {
		/* FIXME Drop the EDataBookView reference for backward-
		 *       compatibility, but at some point the LDAP backend
		 *       should learn to expect a new reference from this
		 *       function and clean up after itself.  Currently
		 *       this is not thread-safe. */
		view = E_DATA_BOOK_VIEW (list->data);
		g_list_free_full (list, (GDestroyNotify) g_object_unref);
	}

	return view;
}

static gboolean
book_view_is_valid (EBookBackendLDAP *bl,
                    EDataBookView *book_view)
{
	GList *list;
	gboolean found;

	list = e_book_backend_list_views (E_BOOK_BACKEND (bl));
	found = (g_list_find (list, book_view) != NULL);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	return found;
}

static void
book_view_notify_status (EBookBackendLDAP *bl,
                         EDataBookView *view,
                         const gchar *status)
{
	if (!book_view_is_valid (bl, view))
		return;
	e_data_book_view_notify_progress (view, -1, status);
}

static void
add_to_supported_fields (EBookBackendLDAP *bl,
                         gchar **attrs,
                         GHashTable *attr_hash)
{
	gint i;
	for (i = 0; attrs[i]; i++) {
		gchar *query_prop = g_hash_table_lookup (attr_hash, attrs[i]);

		if (query_prop == NULL)
			continue;

		bl->priv->supported_fields = g_slist_append (
			bl->priv->supported_fields, g_strdup (query_prop));

		/* handle the list attributes here */
		if (!strcmp (query_prop, e_contact_field_name (E_CONTACT_EMAIL))) {
			bl->priv->supported_fields = g_slist_append (
				bl->priv->supported_fields,
				g_strdup (e_contact_field_name (E_CONTACT_EMAIL_1)));
			bl->priv->supported_fields = g_slist_append (
				bl->priv->supported_fields,
				g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
			bl->priv->supported_fields = g_slist_append (
				bl->priv->supported_fields,
				g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
			bl->priv->supported_fields = g_slist_append (
				bl->priv->supported_fields,
				g_strdup (e_contact_field_name (E_CONTACT_EMAIL_4)));
		} else if (!strcmp (query_prop, e_contact_field_name (E_CONTACT_PHONE_BUSINESS))) {
			bl->priv->supported_fields = g_slist_append (
				bl->priv->supported_fields,
				g_strdup (e_contact_field_name (E_CONTACT_PHONE_BUSINESS_2)));
		} else if (!strcmp (query_prop, e_contact_field_name (E_CONTACT_PHONE_HOME))) {
			bl->priv->supported_fields = g_slist_append (
				bl->priv->supported_fields,
				g_strdup (e_contact_field_name (E_CONTACT_PHONE_HOME_2)));
		} else if (!strcmp (query_prop, e_contact_field_name (E_CONTACT_CATEGORY_LIST) )) {
			bl->priv->supported_fields = g_slist_append (
				bl->priv->supported_fields,
				g_strdup (e_contact_field_name (E_CONTACT_CATEGORIES)));
		}
	}
}

static void
add_oc_attributes_to_supported_fields (EBookBackendLDAP *bl,
                                       LDAPObjectClass *oc)
{
	gint i;
	GHashTable *attr_hash = g_hash_table_new (g_str_hash, g_str_equal);

	for (i = 0; i < G_N_ELEMENTS (prop_info); i++)
		g_hash_table_insert (attr_hash, (gpointer) prop_info[i].ldap_attr, (gchar *) e_contact_field_name (prop_info[i].field_id));

	if (oc->oc_at_oids_must)
		add_to_supported_fields (bl, oc->oc_at_oids_must, attr_hash);

	if (oc->oc_at_oids_may)
		add_to_supported_fields (bl, oc->oc_at_oids_may, attr_hash);

	g_hash_table_destroy (attr_hash);
}

static void
check_schema_support (EBookBackendLDAP *bl)
{
	const gchar *attrs[2];
	LDAPMessage *resp;
	struct timeval timeout;
	gchar *lst;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		return;
	}

	if (!bl->priv->schema_dn) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		return;
	}

	bl->priv->evolutionPersonChecked = TRUE;

	attrs[0] = "objectClasses";
	attrs[1] = NULL;

	timeout.tv_sec = 30;
	timeout.tv_usec = 0;

	if (ldap_search_ext_s (bl->priv->ldap, bl->priv->schema_dn, LDAP_SCOPE_BASE,
			       "(objectClass=subschema)", (gchar **) attrs, 0,
			       NULL, NULL, &timeout, LDAP_NO_LIMIT, &resp) == LDAP_SUCCESS) {
		gchar **values;

		values = ldap_get_values (bl->priv->ldap, resp, "objectClasses");
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		if (values) {
			gint i;
			for (i = 0; values[i]; i++) {
				gint j;
				gint code;
				const gchar *err;
				LDAPObjectClass *oc = ldap_str2objectclass (values[i], &code, &err, 0);

				if (!oc)
					continue;

				for (j = 0; oc->oc_names[j]; j++)
					if (!g_ascii_strcasecmp (oc->oc_names[j], EVOLUTIONPERSON)) {
						if (enable_debug)
							g_print ("support found on ldap server for objectclass evolutionPerson\n");
						bl->priv->evolutionPersonSupported = TRUE;

						add_oc_attributes_to_supported_fields (bl, oc);
					}
					else if (!g_ascii_strcasecmp (oc->oc_names[j], CALENTRY)) {
						if (enable_debug)
							g_print ("support found on ldap server for objectclass calEntry\n");
						bl->priv->calEntrySupported = TRUE;
						add_oc_attributes_to_supported_fields (bl, oc);
					}
					else if (!g_ascii_strcasecmp (oc->oc_names[j], INETORGPERSON)
						 || !g_ascii_strcasecmp (oc->oc_names[j], ORGANIZATIONALPERSON)
						 || !g_ascii_strcasecmp (oc->oc_names[j], PERSON)
						 || !g_ascii_strcasecmp (oc->oc_names[j], GROUPOFNAMES)) {
						add_oc_attributes_to_supported_fields (bl, oc);
					}

				ldap_objectclass_free (oc);
			}

			ldap_value_free (values);
		}
		else {
			/* the reason for this is so that if the user
			 * ends up authenticating to the ldap server,
			 * we will requery for the subschema values.
			 * This makes it a bit more robust in the face
			 * of draconian acl's that keep subschema
			 * reads from working until the user is
			 * authed. */
			if (e_book_backend_is_readonly (E_BOOK_BACKEND (bl))) {
				g_warning ("subschema read returned nothing before successful auth");
				bl->priv->evolutionPersonChecked = FALSE;
			}
			else {
				g_warning ("subschema read returned nothing after successful auth");
			}
		}

		ldap_msgfree (resp);
	}
	else {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	}

	lst = e_data_book_string_slist_to_comma_string (bl->priv->supported_fields);
	e_book_backend_notify_property_changed (E_BOOK_BACKEND (bl), E_BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS, lst);
	g_free (lst);
}

#ifndef SUNLDAP
static void
get_ldap_library_info (void)
{
	LDAPAPIInfo info;
	LDAP *ldap;

	ldap = ldap_init (NULL, 0);
	if (ldap == NULL) {
		g_warning ("couldn't create LDAP* for getting at the client lib api info");
		return;
	}

	info.ldapai_info_version = LDAP_API_INFO_VERSION;

	if (LDAP_OPT_SUCCESS != ldap_get_option (ldap, LDAP_OPT_API_INFO, &info)) {
		g_warning ("couldn't get ldap api info");
	}
	else {
		gint i;
		if (enable_debug) {
			g_message (
				"libldap vendor/version: %s %2d.%02d.%02d",
				info.ldapai_vendor_name,
				info.ldapai_vendor_version / 10000,
				(info.ldapai_vendor_version % 10000) / 1000,
				info.ldapai_vendor_version % 1000);

			g_message ("library extensions present:");
		}

		/* yuck.  we have to free these? */
		for (i = 0; info.ldapai_extensions[i]; i++) {
			gchar *extension = info.ldapai_extensions[i];
			if (enable_debug)
				g_message ("%s", extension);
			ldap_memfree (extension);
		}
		ldap_memfree (info.ldapai_extensions);
		ldap_memfree (info.ldapai_vendor_name);
	}

	ldap_unbind (ldap);
}
#endif

static gint
query_ldap_root_dse (EBookBackendLDAP *bl)
{
#define MAX_DSE_ATTRS 20
	LDAPMessage *resp;
	gint ldap_error = LDAP_OTHER;
	const gchar *attrs[MAX_DSE_ATTRS];
	gchar **values;
	gint i = 0;
	struct timeval timeout;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		return ldap_error;
	}

	attrs[i++] = "supportedControl";
	attrs[i++] = "supportedExtension";
	attrs[i++] = "supportedFeatures";
	attrs[i++] = "supportedLDAPVersion";
	attrs[i++] = "subschemaSubentry"; /* OpenLDAP's dn for schema information */
	attrs[i++] = "schemaNamingContext"; /* Active directory's dn for schema information */
	attrs[i] = NULL;

	timeout.tv_sec = 30;
	timeout.tv_usec = 0;

	ldap_error = ldap_search_ext_s (
		bl->priv->ldap,
		LDAP_ROOT_DSE, LDAP_SCOPE_BASE,
		"(objectclass=*)",
		(gchar **) attrs, 0, NULL, NULL, &timeout, LDAP_NO_LIMIT, &resp);
	if (ldap_error != LDAP_SUCCESS) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		g_warning (
			"could not perform query on Root DSE "
			"(ldap_error 0x%02x/%s)", ldap_error,
			ldap_err2string (ldap_error) ?
				ldap_err2string (ldap_error) :
				"Unknown error");
		return ldap_error;
	}

	values = ldap_get_values (bl->priv->ldap, resp, "supportedControl");
	if (values) {
		if (enable_debug) {
			for (i = 0; values[i]; i++)
				g_message ("supported server control: %s", values[i]);
		}
		ldap_value_free (values);
	}

	values = ldap_get_values (bl->priv->ldap, resp, "supportedExtension");
	if (values) {
		if (enable_debug) {
			for (i = 0; values[i]; i++) {
				g_message ("supported server extension: %s", values[i]);
				if (!strcmp (values[i], LDAP_EXOP_START_TLS)) {
					g_message ("server reports LDAP_EXOP_START_TLS");
				}
			}
		}
		ldap_value_free (values);
	}

	values = ldap_get_values (bl->priv->ldap, resp, "subschemaSubentry");
	if (!values || !values[0]) {
		if (values) ldap_value_free (values);
		values = ldap_get_values (bl->priv->ldap, resp, "schemaNamingContext");
	}
	if (values && values[0]) {
		g_free (bl->priv->schema_dn);
		bl->priv->schema_dn = g_strdup (values[0]);
	} else {
		g_warning ("could not determine location of schema information on LDAP server");
	}
	if (values)
		ldap_value_free (values);

	ldap_msgfree (resp);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	return LDAP_SUCCESS;
}

static gboolean
e_book_backend_ldap_connect (EBookBackendLDAP *bl,
                             GError **error)
{
	EBookBackendLDAPPrivate *blpriv = bl->priv;
	gint protocol_version = LDAP_VERSION3;
	gint64 start = 0;
#ifdef SUNLDAP
	gint ldap_flag;
#endif

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	/* close connection first if it's open first */
	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (blpriv->ldap) {
		ldap_unbind (blpriv->ldap);
	}

#ifdef SUNLDAP
	if (bl->priv->security == E_SOURCE_LDAP_SECURITY_LDAPS) {
		const gchar *user_data_dir = e_get_user_data_dir ();
		ldap_flag = ldapssl_client_init (user_data_dir, NULL);
		blpriv->ldap = ldapssl_init (blpriv->ldap_host, blpriv->ldap_port, 1);
	} else
		blpriv->ldap = ldap_init (blpriv->ldap_host, blpriv->ldap_port);
#else
	blpriv->ldap = ldap_init (blpriv->ldap_host, blpriv->ldap_port);
#endif

	if (NULL != blpriv->ldap) {
		gint ldap_error;

#if defined (DEBUG) && defined (LDAP_OPT_DEBUG_LEVEL)
	{
		gint debug_level = 4;
		ldap_set_option (blpriv->ldap, LDAP_OPT_DEBUG_LEVEL, &debug_level);
	}
#endif
		ldap_error = ldap_set_option (blpriv->ldap, LDAP_OPT_PROTOCOL_VERSION, &protocol_version);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning ("failed to set protocol version to LDAPv3");
			bl->priv->ldap_v3 = FALSE;
		} else
			bl->priv->ldap_v3 = TRUE;

		if (!bl->priv->ldap_v3 && bl->priv->security == E_SOURCE_LDAP_SECURITY_STARTTLS) {
			g_message ("TLS not available (fatal version), v3 protocol could not be established (ldap_error 0x%02x)", ldap_error);
			ldap_unbind (blpriv->ldap);
			blpriv->ldap = NULL;
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_TLS_NOT_AVAILABLE));
			return FALSE;
		}

		if (bl->priv->security == E_SOURCE_LDAP_SECURITY_LDAPS) {
#ifdef SUNLDAP
			if (ldap_error == LDAP_SUCCESS) {
				ldap_set_option (blpriv->ldap, LDAP_OPT_RECONNECT, LDAP_OPT_ON );
			}
#else
#if defined (LDAP_OPT_X_TLS_HARD) && defined (LDAP_OPT_X_TLS)
				gint tls_level = LDAP_OPT_X_TLS_HARD;
				ldap_set_option (blpriv->ldap, LDAP_OPT_X_TLS, &tls_level);

				/* setup this on the global option set */
				tls_level = LDAP_OPT_X_TLS_ALLOW;
				ldap_set_option (NULL, LDAP_OPT_X_TLS_REQUIRE_CERT, &tls_level);
#elif defined (G_OS_WIN32)
			ldap_set_option (blpriv->ldap, LDAP_OPT_SSL, LDAP_OPT_ON);
#else
			g_message ("TLS option not available");
#endif
#endif
		} else if (bl->priv->security == E_SOURCE_LDAP_SECURITY_STARTTLS) {
#ifdef SUNLDAP
			if (ldap_error == LDAP_SUCCESS) {
				ldap_set_option (blpriv->ldap, LDAP_OPT_RECONNECT, LDAP_OPT_ON );
			}
#else
			ldap_error = ldap_start_tls_s (blpriv->ldap, NULL, NULL);
#endif
			if (ldap_error != LDAP_SUCCESS) {
				if (ldap_error == LDAP_SERVER_DOWN) {
					g_message ("TLS failed due to server being down");
					g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE));
				} else {
					g_message ("TLS not available (fatal version), (ldap_error 0x%02x)", ldap_error);
					g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_TLS_NOT_AVAILABLE));
				}
				ldap_unbind (blpriv->ldap);
				blpriv->ldap = NULL;
				g_rec_mutex_unlock (&eds_ldap_handler_lock);
				return FALSE;
			} else if (enable_debug)
				g_message ("TLS active");
		}

		/* bind anonymously initially, we'll actually
		 * authenticate the user properly later (in
		 * authenticate_user) if they've selected
		 * authentication */
		ldap_error = ldap_simple_bind_s (blpriv->ldap, blpriv->auth_dn, blpriv->auth_secret);
		if (ldap_error == LDAP_PROTOCOL_ERROR) {
			g_warning ("failed to bind using v3.  trying v2.");
			/* server doesn't support v3 binds, so let's
			 * drop it down to v2 and try again. */
			bl->priv->ldap_v3 = FALSE;

			protocol_version = LDAP_VERSION2;
			ldap_set_option (blpriv->ldap, LDAP_OPT_PROTOCOL_VERSION, &protocol_version);

			ldap_error = ldap_simple_bind_s (blpriv->ldap, blpriv->auth_dn, blpriv->auth_secret);
		}

		if (ldap_error == LDAP_PROTOCOL_ERROR) {
			g_warning ("failed to bind using either v3 or v2 binds.");
			g_clear_pointer (&blpriv->ldap, ldap_unbind);
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			g_propagate_error (error,
				e_client_error_create (E_CLIENT_ERROR_OTHER_ERROR, _("Failed to bind using either v3 or v2 binds")));
			return FALSE;

		} else if (ldap_error == LDAP_SERVER_DOWN) {
			/* we only want this to be fatal if the server is down. */
			g_warning ("failed to bind anonymously while connecting (ldap_error 0x%02x)", ldap_error);
			g_clear_pointer (&blpriv->ldap, ldap_unbind);
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE));
			return FALSE;
		} else if (ldap_error == LDAP_INVALID_CREDENTIALS) {
			g_warning ("Invalid credentials while connecting (ldap_error 0x%02x)", ldap_error);
			g_clear_pointer (&blpriv->ldap, ldap_unbind);
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_AUTHENTICATION_FAILED));
			return FALSE;
		}

		if (ldap_error == LDAP_INSUFFICIENT_ACCESS)
			ldap_error = LDAP_SUCCESS;
		else
			ldap_error = query_ldap_root_dse (bl);
		/* query_ldap_root_dse will cause the actual
		 * connect (), so any tcpip problems will show up
		 * here */

		/* we can't just check for LDAP_SUCCESS here since in
		 * older servers (namely openldap1.x servers), there's
		 * not a root DSE at all, so the query will fail with
		 * LDAP_NO_SUCH_OBJECT, and GWIA's LDAP server (which
		 * is v2 based and doesn't have a root dse) seems to
		 * fail with LDAP_PARTIAL_RESULTS. */
		if (ldap_error == LDAP_SUCCESS
		    || ldap_error == LDAP_PARTIAL_RESULTS
		    || LDAP_NAME_ERROR (ldap_error)) {
			blpriv->connected = TRUE;
			g_rec_mutex_unlock (&eds_ldap_handler_lock);

			/* check to see if evolutionPerson is supported, if we can (me
			 * might not be able to if we can't authenticate.  if we
			 * can't, try again in auth_user.) */
			if (!bl->priv->evolutionPersonChecked)
				check_schema_support (bl);

			if (enable_debug) {
				GTimeSpan diff = g_get_monotonic_time () - start;

				printf ("%s: success, took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
					G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
			}
			e_backend_ensure_source_status_connected (E_BACKEND (bl));
			return TRUE;
		} else if (ldap_error == LDAP_UNWILLING_TO_PERFORM) {
			g_clear_pointer (&blpriv->ldap, ldap_unbind);
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_AUTHENTICATION_FAILED));
			return FALSE;
		} else {
			g_clear_pointer (&blpriv->ldap, ldap_unbind);
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			g_warning ("Failed to perform root dse query anonymously, (ldap_error 0x%02x)", ldap_error);
		}
	} else {
		g_clear_pointer (&blpriv->ldap, ldap_unbind);
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	g_warning (
		"e_book_backend_ldap_connect failed for "
		"'ldap://%s:%d/%s'\n",
		blpriv->ldap_host,
		blpriv->ldap_port,
		blpriv->ldap_rootdn ? blpriv->ldap_rootdn : "");
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
	blpriv->connected = FALSE;

	g_propagate_error (error, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE));

	return FALSE;
}

static gboolean
e_book_backend_ldap_reconnect (EBookBackendLDAP *bl,
                               EDataBookView *book_view,
                               gint ldap_status)
{
	gint64 start = 0;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return FALSE;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	/* we need to reconnect if we were previously connected */
	if (bl->priv->connected && ldap_status == LDAP_SERVER_DOWN) {
		gint ldap_error = LDAP_SUCCESS;

		book_view_notify_status (bl, book_view, _("Reconnecting to LDAP server..."));

		if (!e_book_backend_ldap_connect (bl, NULL)) {
			book_view_notify_status (bl, book_view, "");
			if (enable_debug)
				printf ("%s: failed (server down?)\n", G_STRFUNC);
			return FALSE;
		}

		if (bl->priv->auth_dn) {
			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap) {
				ldap_error = ldap_simple_bind_s (
					bl->priv->ldap,
					bl->priv->auth_dn,
					bl->priv->auth_secret);
			} else {
				ldap_error = LDAP_SERVER_DOWN;
			}
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
		}
		book_view_notify_status (bl, book_view, "");

		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: returning %d, took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, ldap_error, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}

		return (ldap_error == LDAP_SUCCESS);
	}
	else {
		return FALSE;
	}
}

static void
ldap_op_add (LDAPOp *op,
             EBookBackend *backend,
             EDataBook *book,
             EDataBookView *view,
             gint opid,
             gint msgid,
             LDAPOpHandler handler,
             LDAPOpDtor dtor)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	op->backend = backend;
	op->book = book;
	op->view = view;
	op->opid = opid;
	op->id = msgid;
	op->handler = handler;
	op->dtor = dtor;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	g_rec_mutex_lock (&bl->priv->op_hash_mutex);
	if (g_hash_table_lookup (bl->priv->id_to_op, &op->id)) {
		g_warning ("conflicting ldap msgid's");
	}

	g_hash_table_insert (bl->priv->id_to_op, &op->id, op);

	bl->priv->active_ops++;

	if (bl->priv->poll_timeout == 0) {
		bl->priv->poll_timeout = e_named_timeout_add (
			LDAP_POLL_INTERVAL, poll_ldap, bl);
	}

	g_rec_mutex_unlock (&bl->priv->op_hash_mutex);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
}

static void
ldap_op_finished (LDAPOp *op)
{
	EBookBackend *backend = op->backend;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	g_rec_mutex_lock (&bl->priv->op_hash_mutex);
	g_hash_table_remove (bl->priv->id_to_op, &op->id);

	/* clear the status message too */
	book_view_notify_status (bl, find_book_view (bl), "");

	/* should handle errors here */
	if (bl->priv->ldap)
		ldap_abandon (bl->priv->ldap, op->id);

	if (op->dtor)
		op->dtor (op);

	bl->priv->active_ops--;

	if (bl->priv->active_ops == 0) {
		if (bl->priv->poll_timeout > 0) {
			g_source_remove (bl->priv->poll_timeout);
			bl->priv->poll_timeout = 0;
		}
	}
	g_rec_mutex_unlock (&bl->priv->op_hash_mutex);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
}

static void
ldap_op_change_id (LDAPOp *op,
                   gint msg_id)
{
	EBookBackend *backend = op->backend;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	g_rec_mutex_lock (&bl->priv->op_hash_mutex);
	g_hash_table_remove (bl->priv->id_to_op, &op->id);

	op->id = msg_id;

	g_hash_table_insert (bl->priv->id_to_op, &op->id, op);
	g_rec_mutex_unlock (&bl->priv->op_hash_mutex);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
}

static GError *
ldap_error_to_response (gint ldap_error)
{
	if (ldap_error == LDAP_SUCCESS)
		return NULL;
	else if (ldap_error == LDAP_INVALID_DN_SYNTAX)
		return e_client_error_create (E_CLIENT_ERROR_OTHER_ERROR, _("Invalid DN syntax"));
	else if (LDAP_NAME_ERROR (ldap_error))
		return EBC_ERROR (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND);
	else if (ldap_error == LDAP_INSUFFICIENT_ACCESS)
		return EC_ERROR (E_CLIENT_ERROR_PERMISSION_DENIED);
	else if (ldap_error == LDAP_STRONG_AUTH_REQUIRED)
		return EC_ERROR (E_CLIENT_ERROR_AUTHENTICATION_REQUIRED);
	else if (ldap_error == LDAP_SERVER_DOWN)
		return EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE);
	else if (ldap_error == LDAP_ALREADY_EXISTS)
		return EBC_ERROR (E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS);
	else if (ldap_error == LDAP_TYPE_OR_VALUE_EXISTS )
		return EBC_ERROR (E_BOOK_CLIENT_ERROR_CONTACT_ID_ALREADY_EXISTS);
	else
		return e_client_error_create_fmt (
			E_CLIENT_ERROR_OTHER_ERROR,
			_("LDAP error 0x%x (%s)"), ldap_error,
			ldap_err2string (ldap_error) ?
			ldap_err2string (ldap_error) :
			_("Unknown error"));
}

static const gchar *
get_dn_attribute_name (gchar *rootdn,
                       EContact *contact)
{
	/* Use 'uid' is already used in root DN,
	 * then use the 'cn' field. */
	if (strncmp (rootdn, "uid=", 4) == 0 ||
	    strstr (rootdn, ",uid=") ||
	    (contact && e_contact_get (contact, E_CONTACT_IS_LIST)))
		return "cn";

	/* Use 'uid' field */
	return "uid";
}

static gchar *
create_dn_from_contact (EContact *contact,
                        gchar *rootdn)
{
	gchar *cn, *cn_part = NULL;
	gchar *dn;

	cn = e_contact_get (contact, E_CONTACT_FAMILY_NAME);
	if (!cn || e_contact_get (contact, E_CONTACT_IS_LIST)) {
		g_free (cn);

		cn = e_contact_get (contact, E_CONTACT_FILE_AS);
		if (!cn)
			cn = e_contact_get (contact, E_CONTACT_FULL_NAME);
	}

	if (cn) {
		gint pos = 0;
		cn_part = g_malloc0 (strlen (cn) + 1);
		while (cn[pos])	{
			if (g_ascii_isalnum (cn[pos])) {
				cn_part[pos] = g_ascii_tolower (cn[pos]);
			}
			pos++;
		}
	}

	dn = g_strdup_printf (
		"%s=%s%s%" G_GINT64_FORMAT,
		get_dn_attribute_name (rootdn, contact),
		(cn_part && *cn_part) ? cn_part : "",
		(cn_part && *cn_part) ? "." : "",
		(gint64) time (NULL));

	g_free (cn_part);
	g_free (cn);

	g_print ("generated dn: %s\n", dn);

	return dn;
}

static gchar *
create_full_dn_from_contact (gchar *dn,
                             const gchar *root_dn)
{
	gchar *full_dn = g_strdup_printf (
		"%s%s%s", dn,
		(root_dn && *root_dn) ? "," : "",
		(root_dn && *root_dn) ? root_dn: "");

	g_print ("generated full dn: %s\n", full_dn);

	return full_dn;
}

static void
free_mods (GPtrArray *mods)
{
	gint i = 0;
	LDAPMod *mod;

	if (!mods)
		return;

	while ((mod = g_ptr_array_index (mods, i++))) {
		gint j;
		g_free (mod->mod_type);

		if (mod->mod_op & LDAP_MOD_BVALUES && mod->mod_bvalues) {
			for (j = 0; mod->mod_bvalues[j]; j++) {
				g_free (mod->mod_bvalues[j]->bv_val);
				g_free (mod->mod_bvalues[j]);
			}
			g_free (mod->mod_bvalues);
		}
		else if (mod->mod_values) {
			for (j = 0; mod->mod_values[j]; j++) {
				g_free (mod->mod_values[j]);
			}
			g_free (mod->mod_values);
		}
		g_free (mod);
	}

	g_ptr_array_free (mods, TRUE);
}

static GPtrArray *
build_mods_from_contacts (EBookBackendLDAP *bl,
			  EContact *current,
			  EContact *new,
			  gboolean *new_dn_needed,
			  gchar *ldap_uid,
			  GError **error)
{
	gboolean adding = (current == NULL), is_list = FALSE;
	GPtrArray *result = g_ptr_array_new ();
	gint i;

	if (new_dn_needed)
		*new_dn_needed = FALSE;
	if (e_contact_get (new, E_CONTACT_IS_LIST))
		is_list = TRUE;

	/* add LDAP uid attribute, if given */
	if (ldap_uid) {
		gchar *ldap_uid_value = strchr (ldap_uid, '=');
		if (ldap_uid_value) {
			LDAPMod *mod = g_new (LDAPMod, 1);
			mod->mod_op = LDAP_MOD_ADD;
			mod->mod_type = g_strdup ("uid");
			mod->mod_values = g_new (gchar *, 2);
			mod->mod_values[0] = g_strdup (ldap_uid_value + 1);
			mod->mod_values[1] = NULL;
			g_ptr_array_add (result, mod);
		}
	}

	/* we walk down the list of properties we can deal with (that
	 big table at the top of the file) */

	for (i = 0; i < G_N_ELEMENTS (prop_info); i++) {
		gboolean include;
		gboolean new_prop_present = FALSE;
		gboolean current_prop_present = FALSE;
		struct berval ** new_prop_bers = NULL;
		gchar *new_prop = NULL;
		gchar *current_prop = NULL;
		GError *local_error = NULL;

		/* XXX if it's an evolutionPerson prop and the ldap
		 * server doesn't support that objectclass, skip it. */
		if (prop_info[i].prop_type & PROP_EVOLVE ) {
			if (!bl->priv->evolutionPersonSupported)
				continue;
			if (is_list)
				continue;
		}

		if (((prop_info[i].prop_type & PROP_WITHOUT_EVOSCHEME) != 0 &&
		    bl->priv->evolutionPersonSupported) ||
		    ((prop_info[i].prop_type & PROP_WITH_EVOSCHEME) != 0 &&
		    !bl->priv->evolutionPersonSupported))
			continue;

		if ((prop_info[i].prop_type & PROP_CALENTRY) != 0) {
			if (!bl->priv->calEntrySupported)
				continue;
		}
		if (((prop_info[i].prop_type & PROP_TYPE_COMPLEX) ||
		     (prop_info[i].prop_type & PROP_TYPE_BINARY)) && is_list) {
			continue;
		}

		/* get the value for the new contact, and compare it to
		 * the value in the current contact to see if we should
		 * update it -- if adding is TRUE, short circuit the
		 * check. */
		if (prop_info[i].prop_type & PROP_TYPE_STRING) {
			if (is_list && (prop_info[i].prop_type & PROP_TYPE_CONTACT) != 0)
				continue;

			new_prop = e_contact_get (new, prop_info[i].field_id);
			new_prop_present = (new_prop != NULL);
		}
		else {
			new_prop_bers = prop_info[i].ber_func ? prop_info[i].ber_func (bl, new, prop_info[i].ldap_attr, &local_error) : NULL;
			new_prop_present = (new_prop_bers != NULL);
		}

		/* need to set INCLUDE to true if the field needs to
		 * show up in the ldap modify request */
		if (adding) {
			/* if we're creating a new contact, include it if the
			 * field is there at all */
			if (prop_info[i].prop_type & PROP_TYPE_STRING)
				include = (new_prop_present && *new_prop); /* empty strings cause problems */
			else
				include = new_prop_present;
		}
		else {
			/* if we're modifying an existing contact,
			 * include it if the current field value is
			 * different than the new one, if it didn't
			 * exist previously, or if it's been
			 * removed. */
			if (prop_info[i].prop_type & PROP_TYPE_STRING) {
				current_prop = e_contact_get (current, prop_info[i].field_id);
				current_prop_present = (current_prop != NULL);

				if (new_prop && current_prop)
					include = *new_prop && strcmp (new_prop, current_prop);
				else
					include = (new_prop != current_prop) && (!new_prop || *new_prop); /* empty strings cause problems */
			}
			else {
				gint j;
				struct berval **current_prop_bers = prop_info[i].ber_func ? prop_info[i].ber_func (bl, current, prop_info[i].ldap_attr, &local_error) : NULL;

				current_prop_present = (current_prop_bers != NULL);

				/* free up the current_prop_bers */
				if (current_prop_bers) {
					for (j = 0; current_prop_bers[j]; j++) {
						g_free (current_prop_bers[j]->bv_val);
						g_free (current_prop_bers[j]);
					}
					g_free (current_prop_bers);
				}

				include = prop_info[i].compare_func ? !prop_info[i].compare_func (bl, new, current, prop_info[i].ldap_attr) : FALSE;
			}
		}

		if (include) {
			LDAPMod *mod = g_new (LDAPMod, 1);

			/* the included attribute has changed - we
			 * need to update the dn if it's one of the
			 * attributes we compute the dn from. */
			if (new_dn_needed) {
				const gchar *current_dn = e_contact_get_const (current, E_CONTACT_UID);

				/* check, if this attribute's name is found in the uid string */
				if (current_dn && current_prop) {
					gchar *cid = g_strdup_printf (",%s=", prop_info[i].ldap_attr);
					if (cid) {
						if (!strncmp (current_dn, cid + 1, strlen (cid) - 1) ||
						    strstr (current_dn, cid)) {
							*new_dn_needed = TRUE;
						}
						g_free (cid);
					}
				}
			}

			if (adding) {
				mod->mod_op = LDAP_MOD_ADD;
			}
			else {
				if (!new_prop_present)
					mod->mod_op = LDAP_MOD_DELETE;
				else if (!current_prop_present)
					mod->mod_op = LDAP_MOD_ADD;
				else
					mod->mod_op = LDAP_MOD_REPLACE;
			}

			if ((prop_info[i].prop_type & PROP_TYPE_FORCE_BINARY) != 0)
				mod->mod_type = g_strconcat (prop_info[i].ldap_attr, ";binary", NULL);
			else
				mod->mod_type = g_strdup (prop_info[i].ldap_attr);

			if (prop_info[i].prop_type & PROP_TYPE_STRING) {
				mod->mod_values = g_new (gchar *, 2);
				mod->mod_values[0] = new_prop;
				mod->mod_values[1] = NULL;
			}
			else { /* PROP_TYPE_COMPLEX/PROP_TYPE_GROUP */
				mod->mod_op |= LDAP_MOD_BVALUES;
				mod->mod_bvalues = new_prop_bers;
			}

			g_ptr_array_add (result, mod);
		} else {
			g_free (new_prop);

			if (new_prop_bers) {
				gint jj;

				for (jj = 0; new_prop_bers[jj]; jj++) {
					g_free (new_prop_bers[jj]->bv_val);
					g_free (new_prop_bers[jj]);
				}

				g_free (new_prop_bers);
			}
		}

		g_free (current_prop);

		if (local_error) {
			g_propagate_error (error, local_error);
			break;
		}
	}

	/* NULL terminate the list of modifications */
	g_ptr_array_add (result, NULL);
	return result;
}

static void
add_objectclass_mod (EBookBackendLDAP *bl,
                     GPtrArray *mod_array,
                     GList *existing_objectclasses,
                     gboolean is_list,
                     gboolean is_rename)
{
#define FIND_INSERT(oc) \
	if (!g_list_find_custom (existing_objectclasses, (oc), (GCompareFunc) g_ascii_strcasecmp)) \
		 g_ptr_array_add (objectclasses, g_strdup ((oc)))
#define INSERT(oc) \
		 g_ptr_array_add (objectclasses, g_strdup ((oc)))

	LDAPMod *objectclass_mod;
	GPtrArray *objectclasses = g_ptr_array_new ();

	if (existing_objectclasses) {
		objectclass_mod = g_new (LDAPMod, 1);
		objectclass_mod->mod_op = LDAP_MOD_ADD;
		objectclass_mod->mod_type = g_strdup ("objectClass");

		/* yes, this is a linear search for each of our
		 * objectclasses, but really, how many objectclasses
		 * are there going to be in any sane ldap entry? */
		if (!is_rename)
			FIND_INSERT (TOP);
		if (is_list) {
			FIND_INSERT (GROUPOFNAMES);
		}
		else {
			FIND_INSERT (PERSON);
			FIND_INSERT (ORGANIZATIONALPERSON);
			FIND_INSERT (INETORGPERSON);
			if (bl->priv->calEntrySupported)
				FIND_INSERT (CALENTRY);
			if (bl->priv->evolutionPersonSupported)
				FIND_INSERT (EVOLUTIONPERSON);
		}

		if (objectclasses->len) {
			g_ptr_array_add (objectclasses, NULL);
			objectclass_mod->mod_values = (gchar **) objectclasses->pdata;
			g_ptr_array_add (mod_array, objectclass_mod);
			g_ptr_array_free (objectclasses, FALSE);
		}
		else {
			g_ptr_array_free (objectclasses, TRUE);
			g_free (objectclass_mod->mod_type);
			g_free (objectclass_mod);
		}
	}
	else {
		objectclass_mod = g_new (LDAPMod, 1);
		objectclass_mod->mod_op = LDAP_MOD_ADD;
		objectclass_mod->mod_type = g_strdup ("objectClass");

		if (!is_rename)
			INSERT (TOP);
		if (is_list) {
			INSERT (GROUPOFNAMES);
		}
		else {
			INSERT (PERSON);
			INSERT (ORGANIZATIONALPERSON);
			INSERT (INETORGPERSON);
			if (bl->priv->calEntrySupported)
				INSERT (CALENTRY);
			if (bl->priv->evolutionPersonSupported)
				INSERT (EVOLUTIONPERSON);
		}
		g_ptr_array_add (objectclasses, NULL);
		objectclass_mod->mod_values = (gchar **) objectclasses->pdata;
		g_ptr_array_add (mod_array, objectclass_mod);
		g_ptr_array_free (objectclasses, FALSE);
	}
}

typedef struct {
	LDAPOp op;
	gchar *dn;
	EContact *new_contact;
} LDAPCreateOp;

static void
create_contact_handler (LDAPOp *op,
                        LDAPMessage *res)
{
	LDAPCreateOp *create_op = (LDAPCreateOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	gchar *ldap_error_msg;
	gint ldap_error;
	GSList added_contacts = {NULL,};

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_create_contacts (
			op->book,
			op->opid,
			EC_ERROR_NOT_CONNECTED (),
			NULL);
		ldap_op_finished (op);
		return;
	}

	if (LDAP_RES_ADD != ldap_msgtype (res)) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_create_contacts (
			op->book,
			op->opid,
			EC_ERROR_MSG_TYPE (ldap_msgtype (res)),
			NULL);
		ldap_op_finished (op);
		return;
	}

	ldap_parse_result (
		bl->priv->ldap, res, &ldap_error,
		NULL, &ldap_error_msg, NULL, NULL, 0);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
	if (ldap_error != LDAP_SUCCESS) {
		g_warning (
			"create_contact_handler: %02X (%s), additional info: %s",
			ldap_error,
			ldap_err2string (ldap_error), ldap_error_msg);
	} else {
		if (bl->priv->cache)
			e_book_backend_cache_add_contact (bl->priv->cache, create_op->new_contact);
	}
	ldap_memfree (ldap_error_msg);

	/* and lastly respond */
	added_contacts.data = create_op->new_contact;
	e_data_book_respond_create_contacts (
		op->book,
		op->opid,
		ldap_error_to_response (ldap_error),
		&added_contacts);

	ldap_op_finished (op);
}

static void
create_contact_dtor (LDAPOp *op)
{
	LDAPCreateOp *create_op = (LDAPCreateOp *) op;

	g_free (create_op->dn);
	g_object_unref (create_op->new_contact);
	g_free (create_op);
}

typedef struct {
	LDAPOp op;
	gchar *id;
} LDAPRemoveOp;

static void
remove_contact_handler (LDAPOp *op,
                        LDAPMessage *res)
{
	LDAPRemoveOp *remove_op = (LDAPRemoveOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	gchar *ldap_error_msg;
	gint ldap_error;
	GSList *ids = NULL;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_remove_contacts (op->book, op->opid, EC_ERROR_NOT_CONNECTED (), NULL);
		ldap_op_finished (op);
		return;
	}

	if (LDAP_RES_DELETE != ldap_msgtype (res)) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_remove_contacts (
			op->book,
			op->opid,
			EC_ERROR_MSG_TYPE (ldap_msgtype (res)),
			NULL);
		ldap_op_finished (op);
		return;
	}

	ldap_parse_result (
		bl->priv->ldap, res, &ldap_error,
		NULL, &ldap_error_msg, NULL, NULL, 0);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
	if (ldap_error != LDAP_SUCCESS) {
		g_warning (
			"remove_contact_handler: %02X (%s), additional info: %s",
			ldap_error,
			ldap_err2string (ldap_error), ldap_error_msg);
	} else {
		/* Remove from cache too */
		if (bl->priv->cache)
			e_book_backend_cache_remove_contact (bl->priv->cache, remove_op->id);
	}

	ldap_memfree (ldap_error_msg);

	ids = g_slist_append (ids, remove_op->id);
	e_data_book_respond_remove_contacts (
		remove_op->op.book,
		op->opid,
		ldap_error_to_response (ldap_error),
		ldap_error == LDAP_SUCCESS ? ids : NULL);
	g_slist_free (ids);
	ldap_op_finished (op);
}

static void
remove_contact_dtor (LDAPOp *op)
{
	LDAPRemoveOp *remove_op = (LDAPRemoveOp *) op;

	g_free (remove_op->id);
	g_free (remove_op);
}

/*
** MODIFY
**
** The modification request is actually composed of 2 (or 3) separate
** requests.  Since we need to get a list of theexisting objectclasses
** used by the ldap server for the entry, and since the UI only sends
** us the current contact, we need to query the ldap server for the
** existing contact.
**
*/

typedef struct {
	LDAPOp op;
	const gchar *id; /* the id of the contact we're modifying */
	EContact *current_contact;
	EContact *contact;
	GList *existing_objectclasses;
	GPtrArray *mod_array;
	gchar *ldap_uid; /* the ldap uid field */
	gchar *new_id; /* the new id after a rename */
} LDAPModifyOp;

static void
modify_contact_modify_handler (LDAPOp *op,
                               LDAPMessage *res)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	gchar *ldap_error_msg;
	gint ldap_error;
	GSList modified_contacts = {NULL,};

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_modify_contacts (op->book,
						     op->opid,
						     EC_ERROR_NOT_CONNECTED (),
						     NULL);
		ldap_op_finished (op);
		return;
	}

	if (LDAP_RES_MODIFY != ldap_msgtype (res)) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_modify_contacts (op->book,
						     op->opid,
						     EC_ERROR_MSG_TYPE (ldap_msgtype (res)),
						     NULL);
		ldap_op_finished (op);
		return;
	}

	ldap_parse_result (
		bl->priv->ldap, res, &ldap_error,
		NULL, &ldap_error_msg, NULL, NULL, 0);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
	if (ldap_error != LDAP_SUCCESS) {
		g_warning (
			"modify_contact_modify_handler: %02X (%s), additional info: %s",
			ldap_error,
			ldap_err2string (ldap_error), ldap_error_msg);
	} else {
		if (bl->priv->cache)
			e_book_backend_cache_add_contact (bl->priv->cache, modify_op->contact);
	}
	ldap_memfree (ldap_error_msg);

	/* and lastly respond */
	modified_contacts.data = modify_op->contact;
	e_data_book_respond_modify_contacts (op->book,
					     op->opid,
					     ldap_error_to_response (ldap_error),
					     &modified_contacts);
	ldap_op_finished (op);
}

/* forward declaration */
static void modify_contact_rename_handler (LDAPOp *op, LDAPMessage *res);

static void
modify_contact_search_handler (LDAPOp *op,
                               LDAPMessage *res)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	gint msg_type;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_modify_contacts (op->book, op->opid,
						     EC_ERROR_NOT_CONNECTED (), NULL);
		ldap_op_finished (op);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	/* if it's successful, we should get called with a
	 * RES_SEARCH_ENTRY and a RES_SEARCH_RESULT.  if it's
	 * unsuccessful, we should only see a RES_SEARCH_RESULT */

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		LDAPMessage *e;

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap)
			e = ldap_first_entry (bl->priv->ldap, res);
		else
			e = NULL;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		if (!e) {
			e_data_book_respond_modify_contacts (
				op->book, op->opid,
				e_client_error_create_fmt (E_CLIENT_ERROR_OTHER_ERROR,
					_("%s: NULL returned from ldap_first_entry"), G_STRFUNC),
				NULL);
			ldap_op_finished (op);
			return;
		}

		modify_op->current_contact = build_contact_from_entry (bl, e,
								       &modify_op->existing_objectclasses,
								       &modify_op->ldap_uid);
	} else if (msg_type == LDAP_RES_SEARCH_REFERENCE) {
		/* ignore references */
	} else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		gchar *ldap_error_msg = NULL;
		gint ldap_error;
		gint new_dn_needed;
		GError *local_error = NULL;

		/* grab the result code, and set up the actual modify (or rename)
		 * if it was successful */
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_parse_result (
				bl->priv->ldap, res, &ldap_error,
				NULL, &ldap_error_msg, NULL, NULL, 0);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning (
				"modify_contact_search_handler: %02X (%s), additional info: %s",
				ldap_error,
				ldap_err2string (ldap_error), ldap_error_msg);
		}
		if (ldap_error_msg)
			ldap_memfree (ldap_error_msg);

		if (ldap_error != LDAP_SUCCESS) {
			/* more here i'm sure */
			e_data_book_respond_modify_contacts (op->book,
							     op->opid,
							     ldap_error_to_response (ldap_error),
							     NULL);
			ldap_op_finished (op);
			return;
		}

		/* build our mods */
		modify_op->mod_array = build_mods_from_contacts (bl, modify_op->current_contact, modify_op->contact, &new_dn_needed, NULL, &local_error);

		if (local_error) {
			e_data_book_respond_modify_contacts (op->book, op->opid, local_error, NULL);
			ldap_op_finished (op);
			return;
		}

		/* UID rename necessary? */
		if (new_dn_needed) {
			const gchar *current_dn = e_contact_get_const (modify_op->current_contact, E_CONTACT_UID);
			gchar *new_uid;

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (modify_op->ldap_uid)
				new_uid = g_strdup_printf (
					"%s=%s", get_dn_attribute_name (bl->priv->ldap_rootdn, NULL),
					modify_op->ldap_uid);
			else
				new_uid = create_dn_from_contact (modify_op->contact, bl->priv->ldap_rootdn);

			if (new_uid)
				modify_op->new_id = create_full_dn_from_contact (new_uid, bl->priv->ldap_rootdn);
			g_rec_mutex_unlock (&eds_ldap_handler_lock);

#ifdef LDAP_DEBUG_MODIFY
			if (enable_debug)
				printf ("Rename of DN necessary: %s -> %s (%s)\n", current_dn, modify_op->new_id, new_uid);
#endif
			if (current_dn && new_uid && modify_op->new_id) {
				gint rename_contact_msgid;

				/* actually perform the ldap rename */
				g_rec_mutex_lock (&eds_ldap_handler_lock);
				if (bl->priv->ldap) {
					ldap_error = ldap_rename (
						bl->priv->ldap, current_dn,
						new_uid /* newRDN */,
						NULL    /* NewSuperior */,
						0       /* deleteOldRDN */,
						NULL, NULL, &rename_contact_msgid);
				} else {
					ldap_error = LDAP_SERVER_DOWN;
				}
				g_rec_mutex_unlock (&eds_ldap_handler_lock);

				g_free (new_uid);

				if (ldap_error == LDAP_SUCCESS) {
					op->handler = modify_contact_rename_handler;
					ldap_op_change_id (
						(LDAPOp *) modify_op,
						rename_contact_msgid);

					/* Remove old entry from cache */
					if (bl->priv->cache)
						e_book_backend_cache_remove_contact (bl->priv->cache, modify_op->id);
				} else {
					g_warning ("ldap_rename returned %d\n", ldap_error);
					e_data_book_respond_modify_contacts (op->book,
									     op->opid,
									     ldap_error_to_response (ldap_error),
									     NULL);
					ldap_op_finished (op);
					return;
				}
			} else {
				/* rename failed */
				g_free (new_uid);
				ldap_op_finished (op);
				return;
			}
		} else {
			/* no renaming necessary, just call the modify function */
			modify_op->new_id = NULL;
			modify_contact_rename_handler (op, NULL);
		}
	}
}

static void
modify_contact_rename_handler (LDAPOp *op,
                               LDAPMessage *res)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	gchar *ldap_error_msg = NULL;
	gint ldap_error;
	LDAPMod **ldap_mods;
	gboolean differences;
	gint modify_contact_msgid;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_modify_contacts (op->book,
						     op->opid,
						     EC_ERROR_NOT_CONNECTED (),
						     NULL);
		ldap_op_finished (op);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	/* was a rename necessary? */
	if (modify_op->new_id) {
		if (LDAP_RES_RENAME != ldap_msgtype (res)) {
			e_data_book_respond_modify_contacts (op->book,
							     op->opid,
							     EC_ERROR_MSG_TYPE (ldap_msgtype (res)),
							     NULL);
			ldap_op_finished (op);
			return;
		}

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_parse_result (
				bl->priv->ldap, res, &ldap_error,
				NULL, &ldap_error_msg, NULL, NULL, 0);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning (
				"modify_contact_rename_handler: %02X (%s), additional info: %s",
				ldap_error,
				ldap_err2string (ldap_error), ldap_error_msg);
		} else {
			if (bl->priv->cache)
				e_book_backend_cache_add_contact (bl->priv->cache, modify_op->contact);
		}
		if (ldap_error_msg)
			ldap_memfree (ldap_error_msg);

		if (ldap_error != LDAP_SUCCESS) {
			e_data_book_respond_modify_contacts (op->book,
							     op->opid,
							     ldap_error_to_response (ldap_error),
							     NULL);
			ldap_op_finished (op);
			return;
		}

		/* rename was successful => replace old IDs */
		e_contact_set (modify_op->current_contact, E_CONTACT_UID, modify_op->new_id);
		e_contact_set (modify_op->contact, E_CONTACT_UID, modify_op->new_id);
		modify_op->id = e_contact_get_const (modify_op->contact, E_CONTACT_UID);
	}

	differences = modify_op->mod_array->len > 0;

	if (differences) {
		/* remove the NULL at the end */
		g_ptr_array_remove (modify_op->mod_array, NULL);

		/* add our objectclass(es), making sure
		 * evolutionPerson is there if it's supported */
		if (e_contact_get (modify_op->current_contact, E_CONTACT_IS_LIST))
			add_objectclass_mod (bl, modify_op->mod_array, modify_op->existing_objectclasses, TRUE, TRUE);
		else
			add_objectclass_mod (bl, modify_op->mod_array, modify_op->existing_objectclasses, FALSE, TRUE);

		/* then put the NULL back */
		g_ptr_array_add (modify_op->mod_array, NULL);

		ldap_mods = (LDAPMod **) modify_op->mod_array->pdata;
#ifdef LDAP_DEBUG_MODIFY
		if (enable_debug) {
			gint i;
			printf ("Sending the following to the server as MOD\n");

			for (i = 0; g_ptr_array_index (modify_op->mod_array, i); i++) {
				LDAPMod *mod = g_ptr_array_index (modify_op->mod_array, i);
				if (mod->mod_op & LDAP_MOD_DELETE)
					printf ("del ");
				else if (mod->mod_op & LDAP_MOD_REPLACE)
					printf ("rep ");
				else
					printf ("add ");
				if (mod->mod_op & LDAP_MOD_BVALUES)
					printf ("ber ");
				else
					printf ("    ");

				printf (" %s:\n", mod->mod_type);

				if (mod->mod_op & LDAP_MOD_BVALUES) {
					gint j;
					for (j = 0; mod->mod_bvalues && mod->mod_bvalues[j] && mod->mod_bvalues[j]->bv_val; j++)
						printf ("\t\t'%s'\n", mod->mod_bvalues[j]->bv_val);
				} else {
					gint j;
					for (j = 0; mod->mod_values && mod->mod_values[j]; j++)
						printf ("\t\t'%s'\n", mod->mod_values[j]);
				}
			}
		}
#endif
		/* actually perform the ldap modify */
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_error = ldap_modify_ext (
				bl->priv->ldap, modify_op->id, ldap_mods,
				NULL, NULL, &modify_contact_msgid);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		if (ldap_error == LDAP_SUCCESS) {
			op->handler = modify_contact_modify_handler;
			ldap_op_change_id (
				(LDAPOp *) modify_op,
				modify_contact_msgid);
		} else {
			g_warning ("ldap_modify_ext returned %d\n", ldap_error);
			e_data_book_respond_modify_contacts (op->book,
							     op->opid,
							     ldap_error_to_response (ldap_error),
							     NULL);
			ldap_op_finished (op);
			return;
		}
	} else {
		e_data_book_respond_modify_contacts (op->book,
						     op->opid,
						     e_client_error_create_fmt (E_CLIENT_ERROR_OTHER_ERROR,
						     _("%s: Unhandled result type %d returned"), G_STRFUNC, ldap_msgtype (res)),
						     NULL);
		ldap_op_finished (op);
	}
}

static void
modify_contact_dtor (LDAPOp *op)
{
	LDAPModifyOp *modify_op = (LDAPModifyOp *) op;

	g_free (modify_op->new_id);
	g_free (modify_op->ldap_uid);
	free_mods (modify_op->mod_array);
	g_list_foreach (modify_op->existing_objectclasses, (GFunc) g_free, NULL);
	g_list_free (modify_op->existing_objectclasses);
	if (modify_op->current_contact)
		g_object_unref (modify_op->current_contact);
	if (modify_op->contact)
		g_object_unref (modify_op->contact);
	g_free (modify_op);
}

typedef struct {
	LDAPOp op;
} LDAPGetContactOp;

static void
get_contact_handler (LDAPOp *op,
                     LDAPMessage *res)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	gint msg_type;
	gint64 start = 0;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_get_contact (op->book, op->opid, EC_ERROR_NOT_CONNECTED (), NULL);
		ldap_op_finished (op);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	/* the msg_type will be either SEARCH_ENTRY (if we're
	 * successful) or SEARCH_RESULT (if we're not), so we finish
	 * the op after either */
	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		LDAPMessage *e;
		EContact *contact;

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap)
			e = ldap_first_entry (bl->priv->ldap, res);
		else
			e = NULL;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		if (!e) {
			e_data_book_respond_get_contact (
				op->book, op->opid,
				e_client_error_create_fmt (
					E_CLIENT_ERROR_OTHER_ERROR,
					_("%s: NULL returned from ldap_first_entry"),
					G_STRFUNC),
				NULL);
			ldap_op_finished (op);
			return;
		}

		contact = build_contact_from_entry (bl, e, NULL, NULL);
		if (!contact) {
			e_data_book_respond_get_contact (
				op->book, op->opid,
				e_client_error_create_fmt (
					E_CLIENT_ERROR_OTHER_ERROR,
					_("%s: NULL returned from ldap_first_entry"),
					G_STRFUNC),
				NULL);
			ldap_op_finished (op);
			return;
		}

		e_data_book_respond_get_contact (
			op->book,
			op->opid,
			NULL,
			contact);
		g_object_unref (contact);
		ldap_op_finished (op);

		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;
			printf ("%s: took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}
	} else if (msg_type == LDAP_RES_SEARCH_REFERENCE) {
		/* ignore references */
	} else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		gchar *ldap_error_msg = NULL;
		gint ldap_error;

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_parse_result (
				bl->priv->ldap, res, &ldap_error,
				NULL, &ldap_error_msg, NULL, NULL, 0);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning (
				"get_contact_handler: %02X (%s), additional info: %s",
				ldap_error,
				ldap_err2string (ldap_error), ldap_error_msg);
		}
		if (ldap_error_msg)
			ldap_memfree (ldap_error_msg);

		e_data_book_respond_get_contact (
			op->book,
			op->opid,
			ldap_error_to_response (ldap_error),
			NULL);
		ldap_op_finished (op);
	}
	else {
		e_data_book_respond_get_contact (
			op->book,
			op->opid,
			e_client_error_create_fmt (E_CLIENT_ERROR_OTHER_ERROR,
							_("%s: Unhandled result type %d returned"), G_STRFUNC, msg_type),
			NULL);
		ldap_op_finished (op);
	}

}

static void
get_contact_dtor (LDAPOp *op)
{
	LDAPGetContactOp *get_contact_op = (LDAPGetContactOp *) op;

	g_free (get_contact_op);
}

typedef struct {
	LDAPOp op;
	GSList *contacts;
} LDAPGetContactListOp;

static void
contact_list_handler (LDAPOp *op,
                      LDAPMessage *res)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAPMessage *e;
	gint msg_type;
	gint64 start = 0;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_get_contact_list (op->book, op->opid, EC_ERROR_NOT_CONNECTED (), NULL);
		ldap_op_finished (op);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap)
			e = ldap_first_entry (bl->priv->ldap, res);
		else
			e = NULL;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		while (NULL != e) {
			EContact *contact;

			contact = build_contact_from_entry (bl, e, NULL, NULL);
			if (contact) {
				if (enable_debug) {
					gchar *vcard;

					vcard = e_vcard_to_string (E_VCARD (contact));
					printf ("vcard = %s\n", vcard);
					g_free (vcard);
				}

				contact_list_op->contacts = g_slist_append (contact_list_op->contacts, contact);
			}

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap)
				e = ldap_next_entry (bl->priv->ldap, e);
			else
				e = NULL;
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
		}
	} else if (msg_type == LDAP_RES_SEARCH_REFERENCE) {
		/* ignore references */
	} else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		gchar *ldap_error_msg = NULL;
		gint ldap_error;

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_parse_result (
				bl->priv->ldap, res, &ldap_error,
				NULL, &ldap_error_msg, NULL, NULL, 0);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning (
				"contact_list_handler: %02X (%s), additional info: %s",
				ldap_error,
				ldap_err2string (ldap_error), ldap_error_msg);
		}
		if (ldap_error_msg)
			ldap_memfree (ldap_error_msg);

		if (ldap_error == LDAP_TIMELIMIT_EXCEEDED)
			e_data_book_respond_get_contact_list (
				op->book,
				op->opid,
				EC_ERROR (E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED),
				contact_list_op->contacts);
		else if (ldap_error == LDAP_SIZELIMIT_EXCEEDED)
			e_data_book_respond_get_contact_list (
				op->book,
				op->opid,
				EC_ERROR (E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED),
				contact_list_op->contacts);
		else if (ldap_error == LDAP_SUCCESS)
			e_data_book_respond_get_contact_list (
				op->book,
				op->opid,
				NULL,
				contact_list_op->contacts);
		else
			e_data_book_respond_get_contact_list (
				op->book,
				op->opid,
				ldap_error_to_response (ldap_error),
				contact_list_op->contacts);

		ldap_op_finished (op);
		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: success, took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}
	}
	else {
		g_warning ("unhandled search result type %d returned", msg_type);
		e_data_book_respond_get_contact_list (
			op->book,
			op->opid,
			e_client_error_create_fmt (E_CLIENT_ERROR_OTHER_ERROR,
			_("%s: Unhandled search result type %d returned"), G_STRFUNC, msg_type),
			NULL);
		ldap_op_finished (op);
	}
}

static void
contact_list_dtor (LDAPOp *op)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp *) op;

	g_slist_free_full (contact_list_op->contacts, g_object_unref);

	g_free (contact_list_op);
}

typedef struct {
	LDAPOp op;
	GSList *uids;
} LDAPGetContactListUIDsOp;

static void
contact_list_uids_handler (LDAPOp *op,
                           LDAPMessage *res)
{
	LDAPGetContactListUIDsOp *contact_list_uids_op = (LDAPGetContactListUIDsOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAPMessage *e;
	gint msg_type;
	gint64 start = 0;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_get_contact_list_uids (op->book, op->opid, EC_ERROR_NOT_CONNECTED (), NULL);
		ldap_op_finished (op);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap)
			e = ldap_first_entry (bl->priv->ldap, res);
		else
			e = NULL;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		while (NULL != e) {
			EContact *contact;
			gchar *uid = NULL;

			contact = build_contact_from_entry (bl, e, NULL, &uid);
			g_clear_object (&contact);

			if (enable_debug)
				printf ("uid = %s\n", uid ? uid : "(null)");

			if (uid)
				contact_list_uids_op->uids = g_slist_append (contact_list_uids_op->uids, uid);

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap)
				e = ldap_next_entry (bl->priv->ldap, e);
			else
				e = NULL;
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
		}
	} else if (msg_type == LDAP_RES_SEARCH_REFERENCE) {
		/* ignore references */
	} else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		gchar *ldap_error_msg = NULL;
		gint ldap_error;

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_parse_result (
				bl->priv->ldap, res, &ldap_error,
				NULL, &ldap_error_msg, NULL, NULL, 0);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		if (ldap_error != LDAP_SUCCESS) {
			g_warning (
				"contact_list_uids_handler: %02X (%s), additional info: %s",
				ldap_error,
				ldap_err2string (ldap_error), ldap_error_msg);
		}
		if (ldap_error_msg)
			ldap_memfree (ldap_error_msg);

		g_warning ("search returned %d\n", ldap_error);

		if (ldap_error == LDAP_TIMELIMIT_EXCEEDED)
			e_data_book_respond_get_contact_list_uids (
				op->book, op->opid,
				EC_ERROR (E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED),
				contact_list_uids_op->uids);
		else if (ldap_error == LDAP_SIZELIMIT_EXCEEDED)
			e_data_book_respond_get_contact_list_uids (
				op->book, op->opid,
				EC_ERROR (E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED),
				contact_list_uids_op->uids);
		else if (ldap_error == LDAP_SUCCESS)
			e_data_book_respond_get_contact_list_uids (
				op->book, op->opid,
				NULL,
				contact_list_uids_op->uids);
		else
			e_data_book_respond_get_contact_list_uids (
				op->book, op->opid,
				ldap_error_to_response (ldap_error),
				contact_list_uids_op->uids);

		ldap_op_finished (op);
		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: success, took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}
	}
	else {
		g_warning ("unhandled search result type %d returned", msg_type);
		e_data_book_respond_get_contact_list_uids (
			op->book, op->opid,
			e_client_error_create_fmt (E_CLIENT_ERROR_OTHER_ERROR,
			_("%s: Unhandled search result type %d returned"), G_STRFUNC, msg_type),
			NULL);
		ldap_op_finished (op);
	}
}

static void
contact_list_uids_dtor (LDAPOp *op)
{
	LDAPGetContactListUIDsOp *contact_list_uids_op = (LDAPGetContactListUIDsOp *) op;

	g_slist_foreach (contact_list_uids_op->uids, (GFunc) g_free, NULL);
	g_slist_free (contact_list_uids_op->uids);

	g_free (contact_list_uids_op);
}

static EContactField email_ids[4] = {
	E_CONTACT_EMAIL_1,
	E_CONTACT_EMAIL_2,
	E_CONTACT_EMAIL_3,
	E_CONTACT_EMAIL_4
};

/* List property functions */
static void
email_populate (EBookBackendLDAP *self,
		EContact *contact,
		gchar **values)
{
	gint i;
	for (i = 0; values[i] && i < 4; i++)
		e_contact_set (contact, email_ids[i], values[i]);
}

static struct berval **
email_ber (EBookBackendLDAP *self,
	   EContact *contact,
	   const gchar *ldap_attr,
	   GError **error)
{
	struct berval ** result;
	const gchar *emails[4];
	gint i, j, num = 0;

	if (e_contact_get (contact, E_CONTACT_IS_LIST))
		return NULL;

	for (i = 0; i < 4; i++) {
		emails[i] = e_contact_get (contact, email_ids[i]);
		if (emails[i])
			num++;
	}

	if (num == 0)
		return NULL;

	result = g_new (struct berval *, num + 1);

	for (i = 0; i < num; i++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 4; i++) {
		if (emails[i]) {
			result[j]->bv_val = g_strdup (emails[i]);
			result[j++]->bv_len = strlen (emails[i]);
		}
	}

	result[num] = NULL;

	return result;
}

static gboolean
email_compare (EBookBackendLDAP *self,
	       EContact *contact1,
	       EContact *contact2,
	       const gchar *ldap_attr)
{
	const gchar *email1, *email2;
	gint i;
	/*
	if (e_contact_get (contact1, E_CONTACT_IS_LIST))
		return TRUE;
	*/

	for (i = 0; i < 4; i++) {
		gboolean equal;
		email1 = e_contact_get_const (contact1, email_ids[i]);
		email2 = e_contact_get_const (contact2, email_ids[i]);

		if (email1 && email2)
			equal = !strcmp (email1, email2);
		else
			equal = (!!email1 == !!email2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

static void
member_populate (EBookBackendLDAP *self,
		 EContact *contact,
		 gchar **values)
{
	gint i;
	gchar **member_info;

	for (i = 0; values[i]; i++) {
		EVCardAttribute *attr;

		member_info = g_strsplit (values[i], ";", -1);

		attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
		e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_X_DEST_CONTACT_UID), member_info[1]);

		if (member_info[2]) {
			gint len = strlen (member_info[2]);
			gchar *value;

			if (member_info[2][0] == '\"' && member_info[2][len - 1] == '\"')
				value = g_strdup_printf ("%s <%s>", member_info[2], member_info[0]);
			else
				value = g_strdup_printf ("\"%s\" <%s>", member_info[2], member_info[0]);

			e_vcard_attribute_add_value_take (attr, g_steal_pointer (&value));
		} else {
			e_vcard_attribute_add_value (attr, member_info[0]);
		}

		e_vcard_add_attribute (E_VCARD (contact), attr);
		g_strfreev (member_info);
	}
}

static struct berval **
member_ber (EBookBackendLDAP *self,
	    EContact *contact,
	    const gchar *ldap_attr,
	    GError **error)
{
	struct berval ** result;
	GList *members, *l, *p;
	gint i = 0, num = 0, missing = 0;
	gchar *dn;

	if (!(e_contact_get (contact, E_CONTACT_IS_LIST)))
		return NULL;

	members = e_vcard_get_attributes_by_name (E_VCARD (contact), EVC_EMAIL);
	num = g_list_length (members);
	if (num == 0) {
		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, _("LDAP contact lists cannot be empty.")));
		return NULL;
	}

	result = g_new (struct berval *, num + 1);

	for (l = members; l != NULL; l = g_list_next (l)) {
		EVCardAttribute *attr = l->data;

		missing++;
		dn = NULL;

		for (p = e_vcard_attribute_get_params (attr); p; p = p->next) {
			EVCardAttributeParam *param = p->data;
			const gchar *param_name = e_vcard_attribute_param_get_name (param);

			if (!g_ascii_strcasecmp (param_name, EVC_X_DEST_CONTACT_UID)) {
				GList *v = e_vcard_attribute_param_get_values (param);
				dn = v ? v->data : NULL;
				if (dn) {
					result[i] = g_new (struct berval, 1);
					result[i]->bv_val = g_strdup (dn);
					result[i]->bv_len = strlen (dn);
					i++;
					missing--;
					break;
				}
			}
		}
	}
	result[i] = NULL;

	g_list_free (members);

	if (missing) {
		gchar *msg;

		msg = g_strdup_printf (g_dngettext (GETTEXT_PACKAGE,
			"Contact lists in LDAP address books require each member to be from the same LDAP address book,"
			" but one member could not be recognized.",
			"Contact lists in LDAP address books require each member to be from the same LDAP address book,"
			" but %d members could not be recognized.",
			missing), missing);

		g_propagate_error (error, EC_ERROR_EX (E_CLIENT_ERROR_OTHER_ERROR, msg));

		g_free (msg);
	}

	return result;
}

static gboolean
member_compare (EBookBackendLDAP *self,
		EContact *contact_new,
		EContact *contact_current,
		const gchar *ldap_attr)
{
	GList *members_new, *members_cur, *l1, *l2, *p_new, *p_cur;
	gint len1 = 0, len2 = 0;
	gchar *list_name1, *list_name2;
	gboolean equal;

	if (!(e_contact_get (contact_new, E_CONTACT_IS_LIST)))
		return TRUE;
	if (!(e_contact_get (contact_current, E_CONTACT_IS_LIST)))
		return TRUE;

	list_name1 = e_contact_get (contact_new, E_CONTACT_FULL_NAME);
	list_name2 = e_contact_get (contact_current, E_CONTACT_FULL_NAME);
	if (list_name1 && list_name2)
		equal = !strcmp (list_name1, list_name2);
	else
		equal = (!!list_name1 == !!list_name2);

	g_free (list_name1);
	g_free (list_name2);

	if (!equal)
		return equal;

	members_new = e_vcard_get_attributes_by_name (E_VCARD (contact_new), EVC_EMAIL);
	len1 = g_list_length (members_new);
	members_cur = e_vcard_get_attributes_by_name (E_VCARD (contact_current), EVC_EMAIL);
	len2 = g_list_length (members_cur);
	if (len1 != len2) {
		g_list_free (members_new);
		g_list_free (members_cur);

		return FALSE;
	}

	for (l1 = members_new; l1 != NULL; l1 = g_list_next (l1)) {
		EVCardAttribute *attr_new = l1->data;
		gchar *dn_new = NULL;

		for (p_new = e_vcard_attribute_get_params (attr_new); p_new; p_new = p_new->next) {
			EVCardAttributeParam *param = p_new->data;
			const gchar *param_name1 = e_vcard_attribute_param_get_name (param);

			if (!g_ascii_strcasecmp (param_name1, EVC_X_DEST_CONTACT_UID)) {
				GList *v = e_vcard_attribute_param_get_values (param);
				dn_new = v ? v->data : NULL;
				if (dn_new) {
					for (l2 = members_cur; l2 != NULL; l2 = g_list_next (l2)) {
						EVCardAttribute *attr_cur = l2->data;
						gchar *dn_cur = NULL;

						for (p_cur = e_vcard_attribute_get_params (attr_cur); p_cur; p_cur = p_cur->next) {
							EVCardAttributeParam *param2 = p_cur->data;
							const gchar *param_name2 = e_vcard_attribute_param_get_name (param2);

							if (!g_ascii_strcasecmp (param_name2, EVC_X_DEST_CONTACT_UID)) {
								GList *v2 = e_vcard_attribute_param_get_values (param2);
								dn_cur = v2 ? v2->data : NULL;

								if (dn_cur) {
									if (!g_ascii_strcasecmp (dn_new, dn_cur)) {
										members_cur = g_list_remove (members_cur, attr_cur);
										goto next_member;
									}
								}
							}
						}
					}
					g_list_free (members_new);
					g_list_free (members_cur);
					return FALSE;
				}
			}
		}
		next_member:
		continue;
	}

	g_list_free (members_new);
	g_list_free (members_cur);

	return TRUE;
}

static void
homephone_populate (EBookBackendLDAP *self,
		    EContact *contact,
		    gchar **values)
{
	if (values[0]) {
		e_contact_set (contact, E_CONTACT_PHONE_HOME, values[0]);
		if (values[1])
			e_contact_set (contact, E_CONTACT_PHONE_HOME_2, values[1]);
	}
}

static struct berval **
homephone_ber (EBookBackendLDAP *self,
	       EContact *contact,
	       const gchar *ldap_attr,
	       GError **error)
{
	struct berval ** result;
	const gchar *homephones[3];
	gint i, j, num;

	num = 0;
	if ((homephones[0] = e_contact_get (contact, E_CONTACT_PHONE_HOME)))
		num++;
	if ((homephones[1] = e_contact_get (contact, E_CONTACT_PHONE_HOME_2)))
		num++;

	if (num == 0)
		return NULL;

	result = g_new (struct berval *, num + 1);

	for (i = 0; i < num; i++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 2; i++) {
		if (homephones[i]) {
			result[j]->bv_val = g_strdup (homephones[i]);
			result[j++]->bv_len = strlen (homephones[i]);
		}
	}

	result[num] = NULL;

	return result;
}

static gboolean
homephone_compare (EBookBackendLDAP *self,
		   EContact *contact1,
		   EContact *contact2,
		   const gchar *ldap_attr)
{
	gint phone_ids[2] = { E_CONTACT_PHONE_HOME, E_CONTACT_PHONE_HOME_2 };
	gchar *phone1, *phone2;
	gint i;

	for (i = 0; i < 2; i++) {
		gboolean equal;
		phone1 = e_contact_get (contact1, phone_ids[i]);
		phone2 = e_contact_get (contact2, phone_ids[i]);

		if (phone1 && phone2)
			equal = !strcmp (phone1, phone2);
		else
			equal = (!!phone1 == !!phone2);

		g_free (phone1);
		g_free (phone2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

static void
business_populate (EBookBackendLDAP *self,
		   EContact *contact,
		   gchar **values)
{
	if (values[0]) {
		e_contact_set (contact, E_CONTACT_PHONE_BUSINESS, values[0]);
		if (values[1])
			e_contact_set (contact, E_CONTACT_PHONE_BUSINESS_2, values[1]);
	}
}

static struct berval **
business_ber (EBookBackendLDAP *self,
	      EContact *contact,
	      const gchar *ldap_attr,
	      GError **error)
{
	struct berval ** result;
	const gchar *business_phones[3];
	gint i, j, num;

	num = 0;
	if ((business_phones[0] = e_contact_get (contact, E_CONTACT_PHONE_BUSINESS)))
		num++;
	if ((business_phones[1] = e_contact_get (contact, E_CONTACT_PHONE_BUSINESS_2)))
		num++;

	if (num == 0)
		return NULL;

	result = g_new (struct berval *, num + 1);

	for (i = 0; i < num; i++)
		result[i] = g_new (struct berval, 1);

	j = 0;
	for (i = 0; i < 2; i++) {
		if (business_phones[i]) {
			result[j]->bv_val = g_strdup (business_phones[i]);
			result[j++]->bv_len = strlen (business_phones[i]);
		}
	}

	result[num] = NULL;

	return result;
}

static gboolean
business_compare (EBookBackendLDAP *self,
		  EContact *contact1,
		  EContact *contact2,
		  const gchar *ldap_attr)
{
	gint phone_ids[2] = { E_CONTACT_PHONE_BUSINESS, E_CONTACT_PHONE_BUSINESS_2 };
	gchar *phone1, *phone2;
	gint i;

	for (i = 0; i < 2; i++) {
		gboolean equal;
		phone1 = e_contact_get (contact1, phone_ids[i]);
		phone2 = e_contact_get (contact2, phone_ids[i]);

		if (phone1 && phone2)
			equal = !strcmp (phone1, phone2);
		else
			equal = (!!phone1 == !!phone2);

		g_free (phone1);
		g_free (phone2);

		if (!equal)
			return equal;
	}

	return TRUE;
}

static void
anniversary_populate (EBookBackendLDAP *self,
		      EContact *contact,
		      gchar **values)
{
	if (values[0]) {
		EContactDate *dt = e_contact_date_from_string (values[0]);
		e_contact_set (contact, E_CONTACT_ANNIVERSARY, dt);
		e_contact_date_free (dt);
	}
}

static struct berval **
anniversary_ber (EBookBackendLDAP *self,
		 EContact *contact,
		 const gchar *ldap_attr,
		 GError **error)
{
	EContactDate *dt;
	struct berval ** result = NULL;

	dt = e_contact_get (contact, E_CONTACT_ANNIVERSARY);

	if (dt) {
		gchar *anniversary;

		anniversary = e_contact_date_to_string (dt, E_VCARD_VERSION_30);

		result = g_new (struct berval *, 2);
		result[0] = g_new (struct berval, 1);
		result[0]->bv_val = anniversary;
		result[0]->bv_len = strlen (anniversary);

		result[1] = NULL;

		e_contact_date_free (dt);
	}

	return result;
}

static gboolean
anniversary_compare (EBookBackendLDAP *self,
		     EContact *contact1,
		     EContact *contact2,
		     const gchar *ldap_attr)
{
	EContactDate *dt1, *dt2;
	gboolean equal;

	dt1 = e_contact_get (contact1, E_CONTACT_ANNIVERSARY);
	dt2 = e_contact_get (contact2, E_CONTACT_ANNIVERSARY);

	equal = e_contact_date_equal (dt1, dt2);

	e_contact_date_free (dt1);
	e_contact_date_free (dt2);

	return equal;
}

static void
birthday_populate (EBookBackendLDAP *self,
		   EContact *contact,
		   gchar **values)
{
	if (values[0]) {
		EContactDate *dt = e_contact_date_from_string (values[0]);
		e_contact_set (contact, E_CONTACT_BIRTH_DATE, dt);
		e_contact_date_free (dt);
	}
}

static struct berval **
birthday_ber (EBookBackendLDAP *self,
	      EContact *contact,
	      const gchar *ldap_attr,
	      GError **error)
{
	EContactDate *dt;
	struct berval ** result = NULL;

	dt = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
	if (dt) {
		gchar *birthday;

		birthday = e_contact_date_to_string (dt, E_VCARD_VERSION_30);

		result = g_new (struct berval *, 2);
		result[0] = g_new (struct berval, 1);
		result[0]->bv_val = birthday;
		result[0]->bv_len = strlen (birthday);

		result[1] = NULL;

		e_contact_date_free (dt);
	}

	return result;
}

static gboolean
birthday_compare (EBookBackendLDAP *self,
		  EContact *contact1,
		  EContact *contact2,
		  const gchar *ldap_attr)
{
	EContactDate *dt1, *dt2;
	gboolean equal;

	dt1 = e_contact_get (contact1, E_CONTACT_BIRTH_DATE);
	dt2 = e_contact_get (contact2, E_CONTACT_BIRTH_DATE);

	equal = e_contact_date_equal (dt1, dt2);

	e_contact_date_free (dt1);
	e_contact_date_free (dt2);

	return equal;
}

static void
category_populate (EBookBackendLDAP *self,
		   EContact *contact,
		   gchar **values)
{
	gint i;
	GList *categories = NULL;

	for (i = 0; values[i]; i++)
		categories = g_list_append (categories, g_strdup (values[i]));

	e_contact_set (contact, E_CONTACT_CATEGORY_LIST, categories);

	g_list_foreach (categories, (GFunc) g_free, NULL);
	g_list_free (categories);
}

static struct berval **
category_ber (EBookBackendLDAP *self,
	      EContact *contact,
	      const gchar *ldap_attr,
	      GError **error)
{
	struct berval ** result = NULL;
	GList *categories;
	const gchar *category_string;

	category_string = e_contact_get (contact, E_CONTACT_CATEGORIES);
	if (!category_string || !*category_string)
		return NULL;

	categories = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);

	if (g_list_length (categories) != 0) {
		gint i;
		GList *iter;
		result = g_new0 (struct berval *, g_list_length (categories) + 1);

		for (iter = categories, i = 0; iter; iter = iter->next) {
			gchar *category = iter->data;

			if (category && *category) {
				result[i] = g_new (struct berval, 1);
				result[i]->bv_val = g_strdup (category);
				result[i]->bv_len = strlen (category);
				i++;
			}
		}
	}

	g_list_foreach (categories, (GFunc) g_free, NULL);
	g_list_free (categories);
	return result;
}

static gboolean
category_compare (EBookBackendLDAP *self,
		  EContact *contact1,
		  EContact *contact2,
		  const gchar *ldap_attr)
{
	const gchar *categories1, *categories2;
	gboolean equal;

	categories1 = e_contact_get_const (contact1, E_CONTACT_CATEGORIES);
	categories2 = e_contact_get_const (contact2, E_CONTACT_CATEGORIES);

	if (categories1 && categories2)
		equal = !strcmp (categories1, categories2);
	else
		equal = (categories1 == categories2);

	return equal;
}

static EContactAddress *
getormakeEContactAddress (EContact *card,
			  EContactField field)
{
	EContactAddress *contact_addr = e_contact_get (card, field);
	if (!contact_addr)
		contact_addr = e_contact_address_new ();
	return contact_addr;
}

static void
replace_address_member (gchar **property,
			gchar *value)
{
	g_clear_pointer (property, g_free);
	*property = value;
}

static void
address_populate (EContact *card,
                  gchar **values,
                  EContactField field,
                  EContactField other_field)
{
	if (values[0]) {
		EContactAddress *contact_addr;
		gchar *temp = g_strdup (values[0]);
		gchar *i;
		for (i = temp; *i != '\0'; i++) {
			if (*i == '$') {
				*i = '\n';
			}
		}
		e_contact_set (card, field, temp);

		contact_addr = getormakeEContactAddress (card, other_field);
		replace_address_member (&contact_addr->street, temp);
		e_contact_set (card, other_field, contact_addr);
		e_contact_address_free (contact_addr);
	}
}

static void
work_city_populate (EBookBackendLDAP *self,
		    EContact *card,
		    gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_WORK);
	replace_address_member (&contact_addr->locality, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_WORK, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
work_state_populate (EBookBackendLDAP *self,
		     EContact *card,
		     gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_WORK);
	replace_address_member (&contact_addr->region, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_WORK, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
work_po_populate (EBookBackendLDAP *self,
		  EContact *card,
		  gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_WORK);
	replace_address_member (&contact_addr->po, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_WORK, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
work_zip_populate (EBookBackendLDAP *self,
		   EContact *card,
		   gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_WORK);
	replace_address_member (&contact_addr->code, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_WORK, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
work_country_populate (EBookBackendLDAP *self,
		       EContact *card,
		       gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_WORK);
	replace_address_member (&contact_addr->country, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_WORK, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
home_city_populate (EBookBackendLDAP *self,
		    EContact *card,
		    gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_HOME);
	replace_address_member (&contact_addr->locality, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_HOME, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
home_state_populate (EBookBackendLDAP *self,
		     EContact *card,
		     gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_HOME);
	replace_address_member (&contact_addr->region, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_HOME, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
home_zip_populate (EBookBackendLDAP *self,
		   EContact *card,
		   gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_HOME);
	replace_address_member (&contact_addr->code, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_HOME, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
home_country_populate (EBookBackendLDAP *self,
		       EContact *card,
		       gchar **values)
{
	EContactAddress *contact_addr = getormakeEContactAddress (card, E_CONTACT_ADDRESS_HOME);
	replace_address_member (&contact_addr->country, g_strdup (values[0]));
	e_contact_set (card, E_CONTACT_ADDRESS_HOME, contact_addr);
	e_contact_address_free (contact_addr);
}

static void
home_address_populate (EBookBackendLDAP *self,
		       EContact *card,
		       gchar **values)
{
	address_populate (card, values, E_CONTACT_ADDRESS_LABEL_HOME, E_CONTACT_ADDRESS_HOME);
}

static void
work_address_populate (EBookBackendLDAP *self,
		       EContact *card,
		       gchar **values)
{
	address_populate (card, values, E_CONTACT_ADDRESS_LABEL_WORK, E_CONTACT_ADDRESS_WORK);
}

static void
other_address_populate (EBookBackendLDAP *self,
			EContact *card,
			gchar **values)
{
	address_populate (card, values, E_CONTACT_ADDRESS_LABEL_OTHER, E_CONTACT_ADDRESS_OTHER);
}

static struct berval **
address_ber (EContact *card,
	     EContactField field,
	     GError **error)
{
	struct berval **result = NULL;
	gchar *address, *i;

	address = e_contact_get (card, field);
	if (address) {
		for (i = address; *i != '\0'; i++) {
			if (*i == '\n') {
				*i = '$';
			}
		}

		result = g_new (struct berval *, 2);
		result[0] = g_new (struct berval, 1);
		result[0]->bv_val = address;
		result[0]->bv_len = strlen (address);

		result[1] = NULL;
	}
	return result;
}

static struct berval **
home_address_ber (EBookBackendLDAP *self,
		  EContact *card,
		  const gchar *ldap_attr,
		  GError **error)
{
	return address_ber (card, E_CONTACT_ADDRESS_LABEL_HOME, error);
}

static struct berval **
work_address_ber (EBookBackendLDAP *self,
		  EContact *card,
		  const gchar *ldap_attr,
		  GError **error)
{
	return address_ber (card, E_CONTACT_ADDRESS_LABEL_WORK, error);
}

static struct berval **
other_address_ber (EBookBackendLDAP *self,
		   EContact *card,
		   const gchar *ldap_attr,
		   GError **error)
{
	return address_ber (card, E_CONTACT_ADDRESS_LABEL_OTHER, error);
}

static gboolean
address_compare (EContact *ecard1,
                 EContact *ecard2,
                 EContactField field)
{
	const gchar *address1, *address2;

	gboolean equal;
	address1 = e_contact_get_const (ecard1, field);
	address2 = e_contact_get_const (ecard2, field);

	if (address1 && address2)
		equal = !strcmp (address1, address2);
	else
		equal = (!!address1 == !!address2);

	return equal;
}

static gboolean
home_address_compare (EBookBackendLDAP *self,
		      EContact *ecard1,
		      EContact *ecard2,
		      const gchar *ldap_attr)
{
	return address_compare (ecard1, ecard2, E_CONTACT_ADDRESS_LABEL_HOME);
}

static gboolean
work_address_compare (EBookBackendLDAP *self,
		      EContact *ecard1,
		      EContact *ecard2,
		      const gchar *ldap_attr)
{
	return address_compare (ecard1, ecard2, E_CONTACT_ADDRESS_LABEL_WORK);
}

static gboolean
other_address_compare (EBookBackendLDAP *self,
		       EContact *ecard1,
		       EContact *ecard2,
		       const gchar *ldap_attr)
{
	return address_compare (ecard1, ecard2, E_CONTACT_ADDRESS_LABEL_OTHER);
}

static void
photo_populate (EBookBackendLDAP *self,
		EContact *contact,
		struct berval **ber_values)
{
	if (ber_values && ber_values[0]) {
		EContactPhoto photo;
		photo.type = E_CONTACT_PHOTO_TYPE_INLINED;
		photo.data.inlined.mime_type = NULL;
		photo.data.inlined.data = (guchar *) ber_values[0]->bv_val;
		photo.data.inlined.length = ber_values[0]->bv_len;

		e_contact_set (contact, E_CONTACT_PHOTO, &photo);
	}
}

static struct berval **
photo_ber (EBookBackendLDAP *self,
	   EContact *contact,
	   const gchar *ldap_attr,
	   GError **error)
{
	struct berval **result = NULL;
	EContactPhoto *photo;

	photo = e_contact_get (contact, E_CONTACT_PHOTO);
	if (photo && photo->type == E_CONTACT_PHOTO_TYPE_INLINED) {

		result = g_new (struct berval *, 2);
		result[0] = g_new (struct berval, 1);
		result[0]->bv_len = photo->data.inlined.length;
		result[0]->bv_val = g_malloc (photo->data.inlined.length);
		memcpy (result[0]->bv_val, photo->data.inlined.data, photo->data.inlined.length);

		result[1] = NULL;
	}

	e_contact_photo_free (photo);

	return result;
}

static gboolean
photo_compare (EBookBackendLDAP *self,
	       EContact *ecard1,
	       EContact *ecard2,
	       const gchar *ldap_attr)
{
	EContactPhoto *photo1, *photo2;
	gboolean equal;

	photo1 = e_contact_get (ecard1, E_CONTACT_PHOTO);
	photo2 = e_contact_get (ecard2, E_CONTACT_PHOTO);

	if (photo1 && photo2) {
		if (photo1->type == photo2->type && photo1->type == E_CONTACT_PHOTO_TYPE_INLINED) {
			equal = ((photo1->data.inlined.length == photo2->data.inlined.length)
				 && !memcmp (photo1->data.inlined.data, photo2->data.inlined.data, photo1->data.inlined.length));
		} else if (photo1->type == photo2->type && photo1->type == E_CONTACT_PHOTO_TYPE_URI) {

			equal = !strcmp (photo1->data.uri, photo2->data.uri);
		} else {
			equal = FALSE;
		}

	}
	else {
		equal = (!!photo1 == !!photo2);
	}

	if (photo1)
		e_contact_photo_free (photo1);
	if (photo2)
		e_contact_photo_free (photo2);

	return equal;
}

static void
cert_populate (EBookBackendLDAP *self,
	       EContact *contact,
	       struct berval **ber_values)
{
	if (ber_values && ber_values[0]) {
		EContactCert cert;
		cert.data = ber_values[0]->bv_val;
		cert.length = ber_values[0]->bv_len;

		e_contact_set (contact, E_CONTACT_X509_CERT, &cert);
	}
}

static struct berval **
cert_ber (EBookBackendLDAP *self,
	  EContact *contact,
	  const gchar *ldap_attr,
	  GError **error)
{
	struct berval **result = NULL;
	EContactCert *cert;

	cert = e_contact_get (contact, E_CONTACT_X509_CERT);

	if (cert && cert->length && cert->data) {

		result = g_new (struct berval *, 2);
		result[0] = g_new (struct berval, 1);
		result[0]->bv_val = g_malloc (cert->length);
		result[0]->bv_len = cert->length;
		memcpy (result[0]->bv_val, cert->data, cert->length);

		result[1] = NULL;
	}

	e_contact_cert_free (cert);

	return result;
}

static gboolean
cert_compare (EBookBackendLDAP *self,
	      EContact *ecard1,
	      EContact *ecard2,
	      const gchar *ldap_attr)
{
	EContactCert *cert1, *cert2;
	gboolean equal;

	cert1 = e_contact_get (ecard1, E_CONTACT_X509_CERT);
	cert2 = e_contact_get (ecard2, E_CONTACT_X509_CERT);

	if (cert1 && cert2) {
		equal = cert1->length == cert2->length && cert1->data && cert2->data &&
			!memcmp (cert1->data, cert2->data, cert1->length);
	} else {
		equal = cert1 == cert2;
	}

	e_contact_cert_free (cert1);
	e_contact_cert_free (cert2);

	return equal;
}

static void
org_unit_populate (EBookBackendLDAP *self,
		   EContact *contact,
		   gchar **values)
{
	GString *str;
	gchar *org_unit = NULL;
	guint ii;

	if (!values[0] || !*values[0])
		return;

	org_unit = e_contact_get (contact, E_CONTACT_ORG_UNIT);
	str = g_string_new (org_unit ? org_unit : "");

	for (ii = 0; values[ii]; ii++) {
		const gchar *value = values[ii];

		if (value && *value) {
			if (str->len)
				g_string_append_c (str, ';');
			g_string_append (str, value);
		}
	}

	if (str->len && g_strcmp0 (str->str, org_unit) != 0)
		e_contact_set (contact, E_CONTACT_ORG_UNIT, str->str);

	g_string_free (str, TRUE);
	g_free (org_unit);
}

static struct berval **
org_unit_ber (EBookBackendLDAP *self,
	      EContact *contact,
	      const gchar *ldap_attr,
	      GError **error)
{
	struct berval ** result;
	gchar *ptr;
	gchar *org_unit;

	org_unit = e_contact_get (contact, E_CONTACT_ORG_UNIT);
	if (!org_unit || !*org_unit) {
		g_free (org_unit);
		return NULL;
	}

	ptr = strchr (org_unit, ';');

	if (g_strcmp0 (ldap_attr, "departmentNumber") == 0) {
		GPtrArray *array;
		const gchar *from;

		if (!ptr || !ptr[1]) {
			g_free (org_unit);
			return NULL;
		}

		array = g_ptr_array_new ();
		ptr++; /* skip the ';' */

		for (from = ptr; *ptr; ptr++) {
			if (!ptr[1] || (*ptr == ';' && ptr[1])) {
				if (*ptr == ';')
					*ptr = '\0';
				if (from + 1 < ptr) {
					struct berval *bval = g_new (struct berval, 1);
					bval->bv_val = g_strdup (from);
					bval->bv_len = strlen (from);
					g_ptr_array_add (array, bval);
				}

				from = ptr + 1;
			}
		}

		g_ptr_array_add (array, NULL);
		result = (struct berval **) g_ptr_array_free (array, array->len == 1);
	} else {
		if (ptr)
			*ptr = '\0';

		if (*org_unit) {
			result = g_new (struct berval *, 2);
			result[0] = g_new (struct berval, 1);
			result[0]->bv_val = org_unit;
			result[0]->bv_len = strlen (org_unit);
			result[1] = NULL;

			org_unit = NULL;
		} else {
			result = NULL;
		}
	}

	g_free (org_unit);

	return result;
}

static gboolean
org_unit_compare (EBookBackendLDAP *self,
		  EContact *contact1,
		  EContact *contact2,
		  const gchar *ldap_attr)
{
	gchar *org_unit1, *org_unit2;
	gboolean equal;

	org_unit1 = e_contact_get (contact1, E_CONTACT_ORG_UNIT);
	org_unit2 = e_contact_get (contact2, E_CONTACT_ORG_UNIT);

	if (g_strcmp0 (ldap_attr, "departmentNumber") == 0) {
		gchar *ptr;

		if (org_unit1) {
			ptr = strchr (org_unit1, ';');
			if (ptr && ptr[1]) {
				gchar *tmp = g_strdup (ptr + 1);
				g_free (org_unit1);
				org_unit1 = tmp;
			} else {
				g_free (org_unit1);
				org_unit1 = NULL;
			}
		}

		if (org_unit2) {
			ptr = strchr (org_unit2, ';');
			if (ptr && ptr[1]) {
				gchar *tmp = g_strdup (ptr + 1);
				g_free (org_unit2);
				org_unit2 = tmp;
			} else {
				g_free (org_unit2);
				org_unit2 = NULL;
			}
		}
	} else {
		gchar *ptr;

		if (org_unit1) {
			ptr = strchr (org_unit1, ';');
			if (ptr)
				*ptr = '\0';
		}

		if (org_unit2) {
			ptr = strchr (org_unit2, ';');
			if (ptr)
				*ptr = '\0';
		}
	}

	equal = g_strcmp0 (org_unit1, org_unit2) == 0;

	g_free (org_unit1);
	g_free (org_unit2);

	return equal;
}

static void
nickname_populate (EBookBackendLDAP *self,
		   EContact *contact,
		   gchar **values)
{
	const gchar *nickname = values[0];

	if (!nickname || !*nickname)
		return;

	e_contact_set (contact, E_CONTACT_NICKNAME, nickname);

	/* When does not have the Evolution's Person scheme set the displayName
	   also as the FileAs, if it has at least two words (a space in the value). */
	if (!self->priv->evolutionPersonSupported &&
	    strchr (nickname, ' ') != NULL) {
		e_contact_set (contact, E_CONTACT_FILE_AS, nickname);
	}
}

static struct berval **
nickname_ber (EBookBackendLDAP *self,
	      EContact *contact,
	      const gchar *ldap_attr,
	      GError **error)
{
	struct berval **result;
	gchar *nickname;

	nickname = e_contact_get (contact, E_CONTACT_NICKNAME);

	if (!nickname || !*nickname) {
		g_free (nickname);
		return NULL;
	}

	result = g_new (struct berval *, 2);
	result[0] = g_new (struct berval, 1);
	result[0]->bv_val = nickname;
	result[0]->bv_len = strlen (nickname);
	result[1] = NULL;

	return result;
}

static gboolean
nickname_compare (EBookBackendLDAP *self,
		  EContact *contact1,
		  EContact *contact2,
		  const gchar *ldap_attr)
{
	gchar *nickname1, *nickname2;
	gboolean equal;

	nickname1 = e_contact_get (contact1, E_CONTACT_NICKNAME);
	nickname2 = e_contact_get (contact2, E_CONTACT_NICKNAME);

	equal = g_strcmp0 (nickname1, nickname2) == 0;

	g_free (nickname1);
	g_free (nickname2);

	return equal;
}

typedef struct {
	EBookBackendLDAP *bl;
} EBookBackendLDAPSExpData;

#define IS_RFC2254_CHAR(c) ((c) == '*' || (c) =='\\' || (c) == '(' || (c) == ')' || (c) == '\0')
static gchar *
rfc2254_escape (gchar *str)
{
	gint i;
	gint len = strlen (str);
	gint newlen = 0;

	for (i = 0; i < len; i++) {
		if (IS_RFC2254_CHAR (str[i]))
			newlen += 3;
		else
			newlen++;
	}

	if (len == newlen) {
		return g_strdup (str);
	}
	else {
		gchar *newstr = g_malloc0 (newlen + 1);
		gint j = 0;
		for (i = 0; i < len; i++) {
			if (IS_RFC2254_CHAR (str[i])) {
				sprintf (newstr + j, "\\%02x", str[i]);
				j+= 3;
			}
			else {
				newstr[j++] = str[i];
			}
		}
		return newstr;
	}
}

/** for each first space in a sequence of spaces in @param str it will exchange
 * that first with a '*' character, but only if that space is
 * not at the beginning or at the end of the str.
 * Return value is changed @param str. (ie. this function is changeing
 * str itself, didn't alocate new memory.)
 */
static gchar *
extend_query_value (gchar *str)
{
	if (str && g_utf8_strlen (str, -1) > 0) {
		gchar *next;
		gchar *last_star = NULL;
		gboolean have_nonspace = FALSE;

		for (next = str; next && *next; next = g_utf8_next_char (next) ) {
			if (*next == ' ') {
				if (have_nonspace && !last_star) {
					/* exchange only first space after nonspace character */
					*next = '*';
					last_star = next;
				}
			}else{
				have_nonspace = TRUE;
				last_star = NULL;
			}
		}

		if (last_star) {
			/* we placed a star at the end of str, so make it back a space */
			*last_star = ' ';
		}
	}

	return str;
}

static ESExpResult *
join_args (const gchar op,
	   struct _ESExp *ff,
	   gint argc,
	   struct _ESExpResult **argv)
{
	ESExpResult *rr;
	GString *str = NULL;
	gint ii;

	if (op == '&' || op == '|') {
		const gchar *value = NULL;
		gint valid = 0;

		for (ii = 0; ii < argc; ii++) {
			ESExpResult *resval = argv[ii];

			if (resval->type == ESEXP_RES_STRING) {
				valid++;
				value = resval->value.string;
			}
		}

		/* '&' or '|' with one argument is useless, thus avoid such */
		if (valid == 1 && value) {
			rr = e_sexp_result_new (ff, ESEXP_RES_STRING);
			rr->value.string = g_strdup (value);

			return rr;
		}
	}

	for (ii = 0; ii < argc; ii++) {
		ESExpResult *resval = argv[ii];

		if (resval->type == ESEXP_RES_STRING) {
			if (!str) {
				str = g_string_new ("(");
				g_string_append_c (str, op);
			} else {
				g_string_append_c (str, ' ');
			}

			g_string_append (str, resval->value.string);
		}
	}

	if (str) {
		g_string_append_c (str, ')');

		rr = e_sexp_result_new (ff, ESEXP_RES_STRING);
		rr->value.string = g_string_free (str, FALSE);
	} else {
		rr = e_sexp_result_new (ff, ESEXP_RES_BOOL);
		rr->value.boolean = FALSE;
	}

	return rr;
}

static ESExpResult *
func_and (struct _ESExp *f,
          gint argc,
          struct _ESExpResult **argv,
          gpointer data)
{
	return join_args ('&', f, argc, argv);
}

static ESExpResult *
func_or (struct _ESExp *f,
         gint argc,
         struct _ESExpResult **argv,
         gpointer data)
{
	return join_args ('|', f, argc, argv);
}

static ESExpResult *
func_not (struct _ESExp *f,
          gint argc,
          struct _ESExpResult **argv,
          gpointer data)
{
	return join_args ('!', f, argc, argv);
}

static ESExpResult *
func_contains (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;
	gchar *value = NULL;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = extend_query_value ( rfc2254_escape (argv[1]->value.string));
		gboolean one_star = FALSE;

		if (strlen (str) == 0)
			one_star = TRUE;

		if (!strcmp (propname, "x-evolution-any-field")) {
			gint i;
			GString *big_query;
			gchar *match_str;
			if (one_star) {
				g_free (str);

				/* ignore NULL query */
				r = e_sexp_result_new (f, ESEXP_RES_BOOL);
				r->value.boolean = FALSE;
				return r;
			}

			match_str = g_strdup_printf ("=*%s*)", str);

			big_query = g_string_sized_new (G_N_ELEMENTS (prop_info) * 7);
			g_string_append (big_query, "(|");
			for (i = 0; i < G_N_ELEMENTS (prop_info); i++) {
				if ((prop_info[i].prop_type & PROP_TYPE_STRING) != 0 &&
				    !(prop_info[i].prop_type & PROP_WRITE_ONLY) &&
				    (ldap_data->bl->priv->evolutionPersonSupported ||
				     !(prop_info[i].prop_type & PROP_EVOLVE)) &&
				    (!(prop_info[i].prop_type & (PROP_WITH_EVOSCHEME | PROP_WITHOUT_EVOSCHEME)) ||
				     ((prop_info[i].prop_type & PROP_WITHOUT_EVOSCHEME) != 0 &&
				     !ldap_data->bl->priv->evolutionPersonSupported) ||
				    ((prop_info[i].prop_type & PROP_WITH_EVOSCHEME) != 0 &&
				     ldap_data->bl->priv->evolutionPersonSupported)) &&
				    (ldap_data->bl->priv->calEntrySupported ||
				     !(prop_info[i].prop_type & PROP_CALENTRY))) {
					g_string_append_c (big_query, '(');
					g_string_append (big_query, prop_info[i].ldap_attr);
					g_string_append (big_query, match_str);
				}
			}
			g_string_append_c (big_query, ')');

			value = g_string_free (big_query, FALSE);

			g_free (match_str);
		} else {
			const gchar *ldap_attr = query_prop_to_ldap (propname, ldap_data->bl->priv->evolutionPersonSupported, ldap_data->bl->priv->calEntrySupported);

			if (ldap_attr)
				value = g_strdup_printf (
						"(%s=*%s%s)",
						ldap_attr,
						str,
						one_star ? "" : "*");
		}

		g_free (str);
	}

	if (value) {
		r = e_sexp_result_new (f, ESEXP_RES_STRING);
		r->value.string = value;
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.boolean = FALSE;
	}

	return r;
}

static ESExpResult *
func_is (struct _ESExp *f,
         gint argc,
         struct _ESExpResult **argv,
         gpointer data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;
	gchar *value = NULL;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = rfc2254_escape (argv[1]->value.string);
		const gchar *ldap_attr = query_prop_to_ldap (propname, ldap_data->bl->priv->evolutionPersonSupported, ldap_data->bl->priv->calEntrySupported);

		if (ldap_attr) {
			value = g_strdup_printf (
					"(%s=%s)",
					ldap_attr, str);
		} else {
			g_warning ("LDAP: unknown query property '%s'\n", propname);
			/* we want something that'll always be false */
			value = g_strdup ("objectClass=MyBarnIsBiggerThanYourBarn");
		}

		g_free (str);
	}

	if (value) {
		r = e_sexp_result_new (f, ESEXP_RES_STRING);
		r->value.string = value;
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.boolean = FALSE;
	}

	return r;
}

static ESExpResult *
func_beginswith (struct _ESExp *f,
                 gint argc,
                 struct _ESExpResult **argv,
                 gpointer data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;
	gchar *value = NULL;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = rfc2254_escape (argv[1]->value.string);
		const gchar *ldap_attr = query_prop_to_ldap (propname, ldap_data->bl->priv->evolutionPersonSupported, ldap_data->bl->priv->calEntrySupported);

		if (!*str) {
			g_free (str);

			r = e_sexp_result_new (f, ESEXP_RES_BOOL);
			r->value.boolean = FALSE;

			return r;
		}

		/* insert hack for fileAs queries, since we need to do
		 * the right thing if the server supports them or not,
		 * and for entries that have no fileAs attribute. */
		if (ldap_attr) {
			if (!strcmp (propname, "full_name")) {
				value = g_strdup_printf (
						"(|(cn=%s*)(sn=%s*))",
						str, str);
			} else if (!strcmp (ldap_attr, "fileAs")) {
				if (ldap_data->bl->priv->evolutionPersonSupported)
					value = g_strdup_printf (
							"(|(fileAs=%s*)"
							"(&(!(fileAs=*))"
							"(sn=%s*)))",
							str, str);
				else
					value = g_strdup_printf ("(sn=%s*)", str);
			} else {
				value = g_strdup_printf ("(%s=%s*)", ldap_attr, str);
			}
		}

		g_free (str);
	}

	if (value) {
		r = e_sexp_result_new (f, ESEXP_RES_STRING);
		r->value.string = value;
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.boolean = FALSE;
	}

	return r;
}

static ESExpResult *
func_endswith (struct _ESExp *f,
               gint argc,
               struct _ESExpResult **argv,
               gpointer data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;
	gchar *value = NULL;

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;
		gchar *str = rfc2254_escape (argv[1]->value.string);
		const gchar *ldap_attr = query_prop_to_ldap (propname, ldap_data->bl->priv->evolutionPersonSupported, ldap_data->bl->priv->calEntrySupported);

		if (ldap_attr)
			value = g_strdup_printf (
					"(%s=*%s)",
					ldap_attr, str);
		g_free (str);
	}

	if (value) {
		r = e_sexp_result_new (f, ESEXP_RES_STRING);
		r->value.string = value;
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.boolean = FALSE;
	}

	return r;
}

static ESExpResult *
func_exists (struct _ESExp *f,
             gint argc,
             struct _ESExpResult **argv,
             gpointer data)
{
	EBookBackendLDAPSExpData *ldap_data = data;
	ESExpResult *r;
	gchar *value = NULL;

	if (argc == 1
	    && argv[0]->type == ESEXP_RES_STRING) {
		gchar *propname = argv[0]->value.string;

		if (!strcmp (propname, "x-evolution-any-field")) {
			gint i;
			GString *big_query;

			big_query = g_string_sized_new (G_N_ELEMENTS (prop_info) * 7);
			g_string_append (big_query, "(|");
			for (i = 0; i < G_N_ELEMENTS (prop_info); i++) {
				if (!(prop_info[i].prop_type & PROP_WRITE_ONLY) &&
				    (ldap_data->bl->priv->evolutionPersonSupported ||
				     !(prop_info[i].prop_type & PROP_EVOLVE)) &&
				    (!(prop_info[i].prop_type & (PROP_WITH_EVOSCHEME | PROP_WITHOUT_EVOSCHEME)) ||
				     ((prop_info[i].prop_type & PROP_WITHOUT_EVOSCHEME) != 0 &&
				     !ldap_data->bl->priv->evolutionPersonSupported) ||
				    ((prop_info[i].prop_type & PROP_WITH_EVOSCHEME) != 0 &&
				     ldap_data->bl->priv->evolutionPersonSupported)) &&
				    (ldap_data->bl->priv->calEntrySupported ||
				     !(prop_info[i].prop_type & PROP_CALENTRY))) {
					g_string_append_c (big_query, '(');
					g_string_append (big_query, prop_info[i].ldap_attr);
					g_string_append_len (big_query, "=*)", 3);
				}
			}
			g_string_append_c (big_query, ')');

			value = g_string_free (big_query, FALSE);
		} else {
			const gchar *ldap_attr = query_prop_to_ldap (propname, ldap_data->bl->priv->evolutionPersonSupported, ldap_data->bl->priv->calEntrySupported);

			if (ldap_attr)
				value = g_strdup_printf ("(%s=*)", ldap_attr);
		}
	}

	if (value) {
		r = e_sexp_result_new (f, ESEXP_RES_STRING);
		r->value.string = value;
	} else {
		r = e_sexp_result_new (f, ESEXP_RES_BOOL);
		r->value.boolean = FALSE;
	}

	return r;
}

/* 'builtin' functions */
static struct {
	const gchar *name;
	ESExpFunc *func;
	gint type;		/* set to 1 if a function can perform shortcut evaluation, or
				   doesn't execute everything, 0 otherwise */
} symbols[] = {
	{ "and", func_and, 0 },
	{ "or", func_or, 0 },
	{ "not", func_not, 0 },
	{ "contains", func_contains, 0 },
	{ "is", func_is, 0 },
	{ "beginswith", func_beginswith, 0 },
	{ "endswith", func_endswith, 0 },
	{ "exists", func_exists, 0 },
};

static gchar *
e_book_backend_ldap_build_query (EBookBackendLDAP *bl,
                                 const gchar *query)
{
	ESExp *sexp;
	ESExpResult *r;
	gchar *retval;
	EBookBackendLDAPSExpData data;
	gint i;

	data.bl = bl;

	sexp = e_sexp_new ();

	for (i = 0; i < G_N_ELEMENTS (symbols); i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction (sexp, 0, symbols[i].name,
					     (ESExpIFunc *) symbols[i].func, &data);
		} else {
			e_sexp_add_function (
				sexp, 0, symbols[i].name,
				symbols[i].func, &data);
		}
	}

	e_sexp_input_text (sexp, query, strlen (query));
	if (e_sexp_parse (sexp) == -1) {
		g_warning ("%s: Error in parsing '%s': %s", G_STRFUNC, query, e_sexp_get_error (sexp));
		g_object_unref (sexp);
		return NULL;
	}

	r = e_sexp_eval (sexp);

	if (r && r->type == ESEXP_RES_STRING) {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap_search_filter && *bl->priv->ldap_search_filter &&
		    g_ascii_strcasecmp (bl->priv->ldap_search_filter, "(objectClass=*)") != 0) {
			retval = g_strdup_printf ("(& %s %s)", bl->priv->ldap_search_filter, r->value.string);
		} else {
			retval = r->value.string;
			r->value.string = NULL;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	} else {
		if (g_strcmp0 (query, "(contains \"x-evolution-any-field\" \"\")") != 0)
			g_warning ("LDAP: conversion of '%s' to ldap query string failed", query);
		retval = NULL;
	}

	e_sexp_result_free (sexp, r);
	g_object_unref (sexp);

	if (enable_debug)
		printf ("%s: '%s'~>'%s'\n", G_STRFUNC, query, retval ? retval : "[null]");

	return retval;
}

static const gchar *
query_prop_to_ldap (const gchar *query_prop,
		    gboolean evolution_person_supported,
		    gboolean calentry_supported)
{
	gint i;

	if (g_strcmp0 (query_prop, "categories") == 0)
		query_prop = "category_list";

	for (i = 0; i < G_N_ELEMENTS (prop_info); i++) {
		if (!strcmp (query_prop, e_contact_field_name (prop_info[i].field_id))) {
			if ((evolution_person_supported ||
			    !(prop_info[i].prop_type & PROP_EVOLVE)) &&
			    (!(prop_info[i].prop_type & (PROP_WITH_EVOSCHEME | PROP_WITHOUT_EVOSCHEME)) ||
			     ((prop_info[i].prop_type & PROP_WITHOUT_EVOSCHEME) != 0 &&
			     !evolution_person_supported) ||
			    ((prop_info[i].prop_type & PROP_WITH_EVOSCHEME) != 0 &&
			     evolution_person_supported)) &&
			    (calentry_supported ||
			    !(prop_info[i].prop_type & PROP_CALENTRY))) {
				return prop_info[i].ldap_attr;
			}

			break;
		}
	}

	return NULL;
}

typedef struct {
	LDAPOp op;
	EDataBookView *view;

	/* used to detect problems with start/stop_view racing */
	gboolean aborted;
	/* used by search_handler to only send the status messages once */
	gboolean notified_receiving_results;
} LDAPSearchOp;

static EContact *
build_contact_from_entry (EBookBackendLDAP *bl,
                          LDAPMessage *e,
                          GList **existing_objectclasses,
                          gchar **ldap_uid)
{
	EContact *contact = e_contact_new ();
	gchar *dn;
	gchar *attr;
	BerElement *ber = NULL;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		return NULL;
	}
	dn = ldap_get_dn (bl->priv->ldap, e);
	e_contact_set (contact, E_CONTACT_UID, dn);
	ldap_memfree (dn);
	if (ldap_uid) *ldap_uid = NULL;

	for (attr = ldap_first_attribute (bl->priv->ldap, e, &ber); attr && bl->priv->ldap;
	     attr = bl->priv->ldap ? ldap_next_attribute (bl->priv->ldap, e, ber) : NULL) {
		gint i;
		struct prop_info *info = NULL;
		gchar **values;

		if (enable_debug)
			printf ("attr = %s\n", attr);
		if (ldap_uid && !g_ascii_strcasecmp (attr, "uid")) {
			values = ldap_get_values (bl->priv->ldap, e, attr);
			if (values) {
				if (enable_debug)
					printf ("uid value = %s\n", values[0]);
				if (values[0])
					*ldap_uid = g_strdup (values[0]);
				ldap_value_free (values);
			}
		} else if (!g_ascii_strcasecmp (attr, "objectclass")) {
			values = ldap_get_values (bl->priv->ldap, e, attr);
			for (i = 0; values[i]; i++) {
				if (enable_debug)
					printf ("value = %s\n", values[i]);
				if (!g_ascii_strcasecmp (values[i], "groupOfNames")) {
					e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (TRUE));
					e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES, GINT_TO_POINTER (TRUE));
				}
				if (existing_objectclasses)
					*existing_objectclasses = g_list_append (*existing_objectclasses, g_strdup (values[i]));
			}
			ldap_value_free (values);
		}
		else {
			for (i = 0; i < G_N_ELEMENTS (prop_info); i++) {
				if (!g_ascii_strcasecmp (attr, prop_info[i].ldap_attr) &&
				    (!(prop_info[i].prop_type & (PROP_WITH_EVOSCHEME | PROP_WITHOUT_EVOSCHEME)) ||
				     ((prop_info[i].prop_type & PROP_WITHOUT_EVOSCHEME) != 0 &&
				      !bl->priv->evolutionPersonSupported) ||
				     ((prop_info[i].prop_type & PROP_WITH_EVOSCHEME) != 0 &&
				      bl->priv->evolutionPersonSupported))) {
					info = &prop_info[i];
					break;
				}

				if ((prop_info[i].prop_type & PROP_TYPE_FORCE_BINARY) != 0) {
					const gchar *semicolon;

					semicolon = strchr (attr, ';');

					if (semicolon && g_ascii_strcasecmp (semicolon, ";binary") == 0 &&
					    g_ascii_strncasecmp (attr, prop_info[i].ldap_attr, semicolon - attr) == 0) {
						info = &prop_info[i];
						break;
					}
				}
			}

			if (enable_debug)
				printf ("info = %p\n", (gpointer) info);

			if (info) {
				if (info->prop_type & PROP_WRITE_ONLY) {
					continue;
				}

				if (info->prop_type & PROP_TYPE_BINARY) {
					struct berval **ber_values;

					ber_values = ldap_get_values_len (bl->priv->ldap, e, attr);

					if (ber_values) {
						info->binary_populate_contact_func (bl, contact, ber_values);

						ldap_value_free_len (ber_values);
					}
				}
				else {
					values = ldap_get_values (bl->priv->ldap, e, attr);

					if (values) {
						if (info->prop_type & PROP_TYPE_STRING) {
							if (enable_debug)
								printf ("value = %s\n", values[0]);
							/* if it's a normal property just set the string */
							if (values[0])
								e_contact_set (contact, info->field_id, values[0]);
						}
						else if (info->prop_type & PROP_TYPE_COMPLEX) {
							/* if it's a list call the contact-populate function,
							 * which calls g_object_set to set the property */
							info->populate_contact_func (bl, contact, values);
						}
						else if (info->prop_type & PROP_TYPE_GROUP) {
							const gchar *grpattrs[3];
							gint j, view_limit = -1, ldap_error, count;
							EDataBookView *book_view;
							gchar **email_values, **cn_values, **member_info;

							grpattrs[0] = "cn";
							grpattrs[1] = "mail";
							grpattrs[2] = NULL;
							/* search for member attributes */
							/* get the e-mail id for each member and add them to the list */

							book_view = find_book_view (bl);
							view_limit = bl->priv->ldap_limit;

							count = ldap_count_values (values);
							member_info = g_new0 (gchar *, count + 1);

							for (j = 0; values[j] ; j++) {
								/* get the email id for the given dn */
								/* set base to DN and scope to base */
								if (enable_debug)
									printf ("value (dn) = %s\n", values[j]);
								do {
									LDAPMessage *result = NULL;

									if (bl->priv->ldap) {
										ldap_error = ldap_search_ext_s (bl->priv->ldap,
											values[j],
											LDAP_SCOPE_BASE,
											NULL,
											(gchar **) grpattrs, 0,
											NULL,
											NULL,
											NULL,
											view_limit,
											&result);
									} else {
										ldap_error = LDAP_SERVER_DOWN;
									}

									if (ldap_error == LDAP_SUCCESS) {
										/* find the e-mail ids of members */
										cn_values = ldap_get_values (bl->priv->ldap, result, "cn");
										email_values = ldap_get_values (bl->priv->ldap, result, "mail");

										if (email_values) {
											if (enable_debug)
												printf ("email = %s\n", email_values[0]);
											*(member_info + j) =
												g_strdup_printf (
													"%s;%s;",
													email_values[0], values[j]);
											ldap_value_free (email_values);
										}
										if (cn_values) {
											gchar *old = *(member_info + j);
											if (enable_debug)
												printf ("cn = %s\n", cn_values[0]);
											*(member_info + j) =
												g_strconcat (
													old,
													cn_values[0], NULL);
											ldap_value_free (cn_values);
											g_free (old);
										}

										ldap_msgfree (result);
									}
								}
								while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

								if (ldap_error != LDAP_SUCCESS) {
									book_view_notify_status (bl, book_view,
												 ldap_err2string (ldap_error));
									continue;
								}
							}
							/* call populate function */
							info->populate_contact_func (bl, contact, member_info);

							for (j = 0; j < count; j++) {
								g_free (*(member_info + j));
							}
							g_free (member_info);
						}

						ldap_value_free (values);
					}
				}
			}
		}

		ldap_memfree (attr);
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	if (ber)
		ber_free (ber, 0);

	return contact;
}

static gboolean
poll_ldap (gpointer user_data)
{
	EBookBackendLDAP *bl;
	gint rc;
	LDAPMessage *res;
	struct timeval timeout;
	const gchar *ldap_timeout_string;
	gboolean again;

	bl = E_BOOK_BACKEND_LDAP (user_data);

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap || !bl->priv->poll_timeout) {
		bl->priv->poll_timeout = 0;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		return FALSE;
	}

	if (!bl->priv->active_ops) {
		g_warning ("poll_ldap being called for backend with no active operations");
		bl->priv->poll_timeout = 0;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		return FALSE;
	}

	timeout.tv_sec = 0;
	ldap_timeout_string = g_getenv ("LDAP_TIMEOUT");
	if (ldap_timeout_string) {
		timeout.tv_usec = g_ascii_strtod (ldap_timeout_string, NULL) * 1000;
	}
	else
		timeout.tv_usec = LDAP_RESULT_TIMEOUT_MILLIS * 1000;

	rc = ldap_result (bl->priv->ldap, LDAP_RES_ANY, 0, &timeout, &res);
	if (rc != 0) {/* rc == 0 means timeout exceeded */
		if (rc == -1) {
			EDataBookView *book_view = find_book_view (bl);
			g_warning ("%s: ldap_result returned -1, restarting ops", G_STRFUNC);

			if (!bl->priv->poll_timeout || !e_book_backend_ldap_reconnect (bl, book_view, LDAP_SERVER_DOWN)) {
				if (bl->priv->poll_timeout)
					g_warning ("%s: Failed to reconnect to LDAP server", G_STRFUNC);
				g_rec_mutex_unlock (&eds_ldap_handler_lock);
				return FALSE;
			}
		} else {
			gint msgid = ldap_msgid (res);
			LDAPOp *op;

			g_rec_mutex_lock (&bl->priv->op_hash_mutex);
			op = g_hash_table_lookup (bl->priv->id_to_op, &msgid);

			d (printf ("looked up msgid %d, got op %p\n", msgid, op));

			if (op && op->handler)
				op->handler (op, res);
			else
				g_warning ("unknown operation, msgid = %d", msgid);

			/* XXX should the call to op->handler be
			 * protected by the lock? */
			g_rec_mutex_unlock (&bl->priv->op_hash_mutex);

			ldap_msgfree (res);
		}
	}

	/* the poll_timeout is set to 0, when finalizing the backend */
	again = bl->priv->poll_timeout > 0;
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	return again;
}

static void
ldap_search_handler (LDAPOp *op,
                     LDAPMessage *res)
{
	LDAPSearchOp *search_op = (LDAPSearchOp *) op;
	EDataBookView *view = search_op->view;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAPMessage *e;
	gint msg_type;
	gint64 start = 0;

	d (printf ("%s: (%p)\n", G_STRFUNC, view));
	if (enable_debug)
		start = g_get_monotonic_time ();

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		GError *edb_err = EC_ERROR_NOT_CONNECTED ();
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_view_notify_complete (view, edb_err);
		ldap_op_finished (op);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		g_error_free (edb_err);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	if (!search_op->notified_receiving_results) {
		search_op->notified_receiving_results = TRUE;
		book_view_notify_status (bl, op->view, _("Receiving LDAP search results..."));
	}

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap)
			e = ldap_first_entry (bl->priv->ldap, res);
		else
			e = NULL;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		while (NULL != e) {
			EContact *contact = build_contact_from_entry (bl, e, NULL, NULL);

			if (contact) {
				e_data_book_view_notify_update (view, contact);
				g_object_unref (contact);
			}

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap)
				e = ldap_next_entry (bl->priv->ldap, e);
			else
				e = NULL;
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
		}
	} else if (msg_type == LDAP_RES_SEARCH_REFERENCE) {
		/* ignore references */
	} else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		GError *ec_err = NULL;
		gchar *ldap_error_msg = NULL;
		gint ldap_error;

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_parse_result (
				bl->priv->ldap, res, &ldap_error,
				NULL, &ldap_error_msg, NULL, NULL, 0);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		if ((ldap_error == LDAP_TIMELIMIT_EXCEEDED || ldap_error == LDAP_SIZELIMIT_EXCEEDED) && can_browse ((EBookBackend *) bl))
			;/* do not complain when search limit exceeded for browseable LDAPs */
		else if (ldap_error == LDAP_TIMELIMIT_EXCEEDED)
			ec_err = EC_ERROR (E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED);
		else if (ldap_error == LDAP_SIZELIMIT_EXCEEDED)
			ec_err = EC_ERROR (E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED);
		else if (ldap_error != LDAP_SUCCESS)
			ec_err = e_client_error_create_fmt (
				E_CLIENT_ERROR_OTHER_ERROR,
				_("LDAP error 0x%x (%s)"), ldap_error,
				ldap_err2string (ldap_error) ? ldap_err2string (ldap_error) : _("Unknown error"));

		e_data_book_view_notify_complete (view, ec_err);
		g_clear_error (&ec_err);

		ldap_op_finished (op);
		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: completed with error code %d (%s%s%s), took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, ldap_error, ldap_err2string (ldap_error) ? ldap_err2string (ldap_error) : "Unknown error",
				ldap_error_msg ? " / " : "", ldap_error_msg ? ldap_error_msg : "", diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}

		if (ldap_error_msg)
			ldap_memfree (ldap_error_msg);
	}
	else {
		GError *ec_err = EC_ERROR (E_CLIENT_ERROR_INVALID_QUERY);
		g_warning ("unhandled search result type %d returned", msg_type);
		e_data_book_view_notify_complete (view, ec_err);
		ldap_op_finished (op);
		g_error_free (ec_err);
	}
}

static void
ldap_search_dtor (LDAPOp *op)
{
	EBookBackend *backend;
	EBookBackendLDAP *bl;
	LDAPSearchOp *search_op = (LDAPSearchOp *) op;

	d (printf ("ldap_search_dtor (%p)\n", search_op->view));

	backend = e_data_book_view_ref_backend (op->view);
	bl = backend ? E_BOOK_BACKEND_LDAP (backend) : NULL;

	/* unhook us from our EDataBookView */
	if (bl)
		g_mutex_lock (&bl->priv->view_mutex);
	g_object_set_data (G_OBJECT (search_op->view), LDAP_SEARCH_OP_IDENT, NULL);
	if (bl)
		g_mutex_unlock (&bl->priv->view_mutex);

	g_object_unref (search_op->view);

	if (!search_op->aborted)
		g_free (search_op);

	g_clear_object (&backend);
}

static void
e_book_backend_ldap_search (EBookBackendLDAP *bl,
                            EDataBook *book,
                            EDataBookView *view)
{
	EBookBackendSExp *sexp;
	const gchar *query;
	gchar *ldap_query;
	GList *contacts;
	GList *l;
	gint64 start = 0;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	sexp = e_data_book_view_get_sexp (view);
	query = e_book_backend_sexp_text (sexp);

	if (!e_backend_get_online (E_BACKEND (bl)) || (bl->priv->marked_for_offline && bl->priv->cache)) {
		if (!(bl->priv->marked_for_offline && bl->priv->cache)) {
			GError *edb_err = EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE);
			e_data_book_view_notify_complete (view, edb_err);
			g_error_free (edb_err);
			return;
		}

		contacts = e_book_backend_cache_get_contacts (bl->priv->cache, query);

		for (l = contacts; l; l = g_list_next (l)) {
			EContact *contact = l->data;
			e_data_book_view_notify_update (view, contact);
			g_object_unref (contact);
		}

		g_list_free (contacts);

		e_data_book_view_notify_complete (view, NULL /* Success */);
		return;
	}

	ldap_query = e_book_backend_ldap_build_query (bl, query);

	/* search for nonempty full names */
	if (!ldap_query && can_browse ((EBookBackend *) bl))
		ldap_query = g_strdup ("(cn=*)");

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (ldap_query != NULL && bl->priv->ldap) {
		gint ldap_err;
		gint search_msgid;
		gint view_limit;

		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		view_limit = bl->priv->ldap_limit;
		/* if (view_limit == -1 || view_limit > bl->priv->ldap_limit)
			view_limit = bl->priv->ldap_limit; */

		if (enable_debug)
			printf ("searching server using filter: %s (expecting max %d results)\n", ldap_query, view_limit);

		do {
			book_view_notify_status (bl, view, _("Searching..."));

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap) {
				ldap_err = ldap_search_ext (
					bl->priv->ldap, bl->priv->ldap_rootdn,
					bl->priv->ldap_scope,
					ldap_query,
					NULL, 0,
					NULL, /* XXX */
					NULL, /* XXX */
					NULL, /* XXX timeout */
					view_limit, &search_msgid);
			} else {
				ldap_err = LDAP_SERVER_DOWN;
			}
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
		} while (e_book_backend_ldap_reconnect (bl, view, ldap_err));

		g_free (ldap_query);

		if (ldap_err != LDAP_SUCCESS) {
			book_view_notify_status (bl, view, ldap_err2string (ldap_err));
			return;
		} else if (search_msgid == -1) {
			book_view_notify_status (bl, view,
						 _("Error performing search"));
			return;
		} else {
			LDAPSearchOp *op = g_new0 (LDAPSearchOp, 1);

			d (printf ("adding search_op (%p, %d)\n", view, search_msgid));

			op->view = view;
			op->aborted = FALSE;
			g_object_ref (view);

			ldap_op_add (
				(LDAPOp *) op, E_BOOK_BACKEND (bl), book, view,
				0, search_msgid,
				ldap_search_handler, ldap_search_dtor);

			if (enable_debug) {
				GTimeSpan diff = g_get_monotonic_time () - start;

				printf ("%s: invoked ldap_search_handler, took  %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
					G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
			}

			g_mutex_lock (&bl->priv->view_mutex);
			g_object_set_data (G_OBJECT (view), LDAP_SEARCH_OP_IDENT, op);
			g_mutex_unlock (&bl->priv->view_mutex);
		}
		return;
	} else {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		/* Ignore NULL query */
		e_data_book_view_notify_complete (view, NULL /* Success */);
		g_free (ldap_query);
		return;
	}
}

static void
book_backend_ldap_start_view (EBookBackend *backend,
                              EDataBookView *view)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	d (printf ("start_view (%p)\n", view));

	e_book_backend_ldap_search (bl, NULL /* XXX ugh */, view);
}

static void
book_backend_ldap_stop_view (EBookBackend *backend,
                             EDataBookView *view)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	LDAPSearchOp *op;

	d (printf ("stop_view (%p)\n", view));

	g_mutex_lock (&bl->priv->view_mutex);
	op = g_object_get_data (G_OBJECT (view), LDAP_SEARCH_OP_IDENT);
	g_object_set_data (G_OBJECT (view), LDAP_SEARCH_OP_IDENT, NULL);
	g_mutex_unlock (&bl->priv->view_mutex);

	if (op) {
		op->aborted = TRUE;
		ldap_op_finished ((LDAPOp *) op);

		g_free (op);
	}
}

#define LDAP_SIMPLE_PREFIX "ldap/simple-"
#define SASL_PREFIX "sasl/"

static void
generate_cache_handler (LDAPOp *op,
                        LDAPMessage *res)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAPMessage *e;
	gint msg_type;
	EDataBookView *book_view;
	gint64 start = 0;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		ldap_op_finished (op);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	book_view = find_book_view (bl);

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap)
			e = ldap_first_entry (bl->priv->ldap, res);
		else
			e = NULL;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		while (e != NULL) {
			EContact *contact = build_contact_from_entry (bl, e, NULL, NULL);

			if (contact)
				contact_list_op->contacts = g_slist_prepend (contact_list_op->contacts, contact);

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap)
				e = ldap_next_entry (bl->priv->ldap, e);
			else
				e = NULL;
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
		}
	} else {
		GSList *l;
		gint contact_num = 0;
		gchar *status_msg;
		GDateTime *now;
		gchar *update_str;
		GList *contacts, *link;

		contacts = e_book_backend_cache_get_contacts (bl->priv->cache, NULL);
		for (link = contacts; link; link = g_list_next (link)) {
			EContact *contact = link->data;
			e_book_backend_notify_remove (op->backend, e_contact_get_const (contact, E_CONTACT_UID));
		}
		g_list_free_full (contacts, g_object_unref);

		e_file_cache_clean (E_FILE_CACHE (bl->priv->cache));

		e_file_cache_freeze_changes (E_FILE_CACHE (bl->priv->cache));
		for (l = contact_list_op->contacts; l; l = g_slist_next (l)) {
			EContact *contact = l->data;

			contact_num++;
			if (book_view) {
				status_msg = g_strdup_printf (
					_("Downloading contacts (%d)..."),
					contact_num);
				book_view_notify_status (bl, book_view, status_msg);
				g_free (status_msg);
			}
			e_book_backend_cache_add_contact (bl->priv->cache, contact);
			e_book_backend_notify_update (op->backend, contact);
		}
		e_book_backend_cache_set_populated (bl->priv->cache);
		now = g_date_time_new_now_utc ();
		update_str = g_date_time_format_iso8601 (now);
		g_date_time_unref (now);
		e_book_backend_cache_set_time (bl->priv->cache, update_str);
		g_free (update_str);
		e_file_cache_thaw_changes (E_FILE_CACHE (bl->priv->cache));
		e_book_backend_notify_complete (op->backend);
		ldap_op_finished (op);
		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: completed in %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}
	}
}

static void
generate_cache_dtor (LDAPOp *op)
{
	LDAPGetContactListOp *contact_list_op = (LDAPGetContactListOp *) op;
	EBookBackendLDAP *ldap_backend = E_BOOK_BACKEND_LDAP (op->backend);

	g_slist_free_full (contact_list_op->contacts, g_object_unref);
	g_free (contact_list_op);

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (ldap_backend && ldap_backend->priv) {
		e_book_backend_foreach_view_notify_progress (E_BOOK_BACKEND (ldap_backend), TRUE, 0, NULL);
		ldap_backend->priv->generate_cache_in_progress = FALSE;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
}

static void
generate_cache (EBookBackendLDAP *book_backend_ldap)
{
	LDAPGetContactListOp *contact_list_op = g_new0 (LDAPGetContactListOp, 1);
	EBookBackendLDAPPrivate *priv;
	gint contact_list_msgid;
	gint ldap_error;
	gint64 start = 0;
	gchar *last_update_str;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	priv = book_backend_ldap->priv;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!priv->ldap || !priv->cache) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		g_free (contact_list_op);
		if (enable_debug)
			printf ("%s: failed ... ldap handler is NULL or no cache set\n", G_STRFUNC);
		return;
	}

	if (priv->generate_cache_in_progress) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		g_free (contact_list_op);
		if (enable_debug)
			printf ("LDAP generating offline cache skipped: Another request in progress\n");
		return;
	}

	last_update_str = e_book_backend_cache_get_time (priv->cache);
	if (last_update_str) {
		GDateTime *last_update;

		last_update = g_date_time_new_from_iso8601 (last_update_str, NULL);
		g_free (last_update_str);
		if (last_update) {
			GDateTime *now;
			GTimeSpan time_span;

			now = g_date_time_new_now_utc ();
			time_span = g_date_time_difference (now, last_update);
			g_date_time_unref (now);
			g_date_time_unref (last_update);

			/* update up to once a week */
			if (time_span <= 7 * G_TIME_SPAN_DAY) {
				g_rec_mutex_unlock (&eds_ldap_handler_lock);
				g_free (contact_list_op);
				if (enable_debug)
					printf ("LDAP generating offline cache skipped: it's not 7 days since the last check yet\n");
				return;
			}
		}

	}

	priv->generate_cache_in_progress = TRUE;
	e_book_backend_foreach_view_notify_progress (E_BOOK_BACKEND (book_backend_ldap), TRUE, 0, _("Refreshing…"));

	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	do {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (priv->ldap) {
			ldap_error = ldap_search_ext (
				priv->ldap,
				priv->ldap_rootdn,
				priv->ldap_scope,
				"(cn=*)",
				NULL, 0, NULL, NULL,
				NULL, /* XXX timeout */
				LDAP_NO_LIMIT, &contact_list_msgid);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	} while (e_book_backend_ldap_reconnect (book_backend_ldap, NULL, ldap_error));

	if (ldap_error == LDAP_SUCCESS) {
		ldap_op_add (
			(LDAPOp *) contact_list_op, (EBookBackend *) book_backend_ldap, NULL /* book */,
			NULL /* book_view */, 0 /* opid */, contact_list_msgid,
			generate_cache_handler, generate_cache_dtor);
		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: invoked generate_cache_handler, took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}
	} else {
		generate_cache_dtor ((LDAPOp *) contact_list_op);
	}
}

static void
book_backend_ldap_refresh (EBookBackend *backend,
			   EDataBook *book,
			   guint32 opid,
			   GCancellable *cancellable)
{
	EBookBackendLDAP *ldap_backend;

	g_return_if_fail (E_IS_BOOK_BACKEND_LDAP (backend));
	g_return_if_fail (E_IS_DATA_BOOK (book));

	ldap_backend = E_BOOK_BACKEND_LDAP (backend);
	g_return_if_fail (ldap_backend != NULL);
	g_return_if_fail (ldap_backend->priv != NULL);

	if (ldap_backend->priv->cache && ldap_backend->priv->marked_for_offline &&
	    !ldap_backend->priv->generate_cache_in_progress) {
		e_book_backend_cache_set_time (ldap_backend->priv->cache, "");
		generate_cache (ldap_backend);
	}

	e_data_book_respond_refresh (book, opid, NULL);
}

static void
ldap_cancel_op (gpointer key,
                gpointer value,
                gpointer data)
{
	EBookBackendLDAP *bl = data;
	LDAPOp *op = value;

	/* ignore errors, its only best effort? */
	/* lock is held by the caller */
	/* g_rec_mutex_lock (&eds_ldap_handler_lock); */
	if (bl->priv->ldap)
		ldap_abandon (bl->priv->ldap, op->id);
	/* g_rec_mutex_unlock (&eds_ldap_handler_lock); */
}

static void
ldap_cancel_all_operations (EBookBackend *backend)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	g_rec_mutex_lock (&bl->priv->op_hash_mutex);
	g_hash_table_foreach (bl->priv->id_to_op, ldap_cancel_op, bl);
	g_rec_mutex_unlock (&bl->priv->op_hash_mutex);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
}

static void
e_book_backend_ldap_notify_online_cb (EBookBackend *backend,
                                      GParamSpec *pspec)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	/* Cancel all running operations */
	ldap_cancel_all_operations (backend);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		/* Go offline */

		e_book_backend_set_writable (backend, FALSE);

		bl->priv->connected = FALSE;
	} else {
		/* Go online */

		e_book_backend_set_writable (backend, TRUE);

		if (e_book_backend_is_opened (backend)) {
			GError *error = NULL;

			if (!e_book_backend_ldap_connect (bl, &error)) {
				e_book_backend_notify_error (
					backend, error->message);
				g_error_free (error);
			}

			if (bl->priv->marked_for_offline && bl->priv->cache)
				generate_cache (bl);
		}
	}
}

static gboolean
call_dtor (gint msgid,
           LDAPOp *op,
           gpointer data)
{
	EBookBackendLDAP *bl;

	bl = E_BOOK_BACKEND_LDAP (op->backend);

	/* lock is held by the caller */
	/* g_rec_mutex_lock (&eds_ldap_handler_lock); */
	if (bl->priv->ldap)
		ldap_abandon (bl->priv->ldap, op->id);
	/* g_rec_mutex_unlock (&eds_ldap_handler_lock); */

	op->dtor (op);

	return TRUE;
}

static void
book_backend_ldap_finalize (GObject *object)
{
	EBookBackendLDAPPrivate *priv;

	priv = E_BOOK_BACKEND_LDAP (object)->priv;

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	g_rec_mutex_lock (&priv->op_hash_mutex);
	g_hash_table_foreach_remove (priv->id_to_op, (GHRFunc) call_dtor, NULL);
	g_hash_table_destroy (priv->id_to_op);
	g_rec_mutex_unlock (&priv->op_hash_mutex);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);
	g_rec_mutex_clear (&priv->op_hash_mutex);
	g_mutex_clear (&priv->view_mutex);

	/* Remove the timeout before unbinding to avoid a race. */
	if (priv->poll_timeout > 0) {
		g_source_remove (priv->poll_timeout);
		priv->poll_timeout = 0;
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (priv->ldap)
		ldap_unbind (priv->ldap);
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	g_slist_foreach (priv->supported_fields, (GFunc) g_free, NULL);
	g_slist_free (priv->supported_fields);

	g_slist_foreach (priv->supported_auth_methods, (GFunc) g_free, NULL);
	g_slist_free (priv->supported_auth_methods);

	g_free (priv->summary_file_name);

	if (priv->summary) {
		e_book_backend_summary_save (priv->summary);
		g_object_unref (priv->summary);
		priv->summary = NULL;
	}

	g_clear_object (&priv->cache);

	g_free (priv->ldap_host);
	g_free (priv->ldap_rootdn);
	g_free (priv->ldap_search_filter);
	g_free (priv->schema_dn);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_book_backend_ldap_parent_class)->finalize (object);
}

static gchar *
book_backend_ldap_get_backend_property (EBookBackend *backend,
                                        const gchar *prop_name)
{
	g_return_val_if_fail (prop_name != NULL, NULL);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		if (get_marked_for_offline (backend))
			return g_strdup ("net,anon-access,contact-lists,do-initial-query,refresh-supported");
		else if (can_browse (backend))
			return g_strdup ("net,anon-access,contact-lists,do-initial-query");
		else
			return g_strdup ("net,anon-access,contact-lists");

	} else if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_REQUIRED_FIELDS)) {
		GSList *fields = NULL;
		gchar *prop_value;

		/*FIMEME we should look at mandatory attributs in the schema
		  and return all those fields here */
		fields = g_slist_append (fields, (gpointer) e_contact_field_name (E_CONTACT_FILE_AS));
		fields = g_slist_append (fields, (gpointer) e_contact_field_name (E_CONTACT_FULL_NAME));
		fields = g_slist_append (fields, (gpointer) e_contact_field_name (E_CONTACT_FAMILY_NAME));

		prop_value = e_data_book_string_slist_to_comma_string (fields);

		g_slist_free (fields);

		return prop_value;

	} else if (g_str_equal (prop_name, E_BOOK_BACKEND_PROPERTY_SUPPORTED_FIELDS)) {
		EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

		return e_data_book_string_slist_to_comma_string (bl->priv->supported_fields);

	}

	/* Chain up to parent's method. */
	return E_BOOK_BACKEND_CLASS (e_book_backend_ldap_parent_class)->impl_get_backend_property (backend, prop_name);
}

static gboolean
book_backend_ldap_read_settings (EBookBackendLDAP *bl)
{
	ESourceAuthentication *auth_extension;
	ESourceLDAP *ldap_extension;
	ESourceOffline *offline_extension;
	ESource *source;
	gint ldap_port = 0, ldap_scope = LDAP_SCOPE_ONELEVEL;
	gboolean changed = FALSE;

	#define check_str(_prop, _value) G_STMT_START { \
			gchar *tmp = _value; \
			if (g_strcmp0 (_prop, tmp) != 0) { \
				g_free (_prop); \
				_prop = tmp; \
				changed = TRUE; \
			} else { \
				g_free (tmp); \
			} \
		} G_STMT_END
	#define check_direct(_prop, _value) G_STMT_START { \
			if (_prop != _value) { \
				_prop = _value; \
				changed = TRUE; \
			} \
		} G_STMT_END
	#define check_bool(_prop, _value) G_STMT_START { \
			if (((_prop) ? 1 : 0) != ((_value) ? 1 : 0)) { \
				_prop = _value; \
				changed = TRUE; \
			} \
		} G_STMT_END

	source = e_backend_get_source (E_BACKEND (bl));
	auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);
	ldap_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_LDAP_BACKEND);
	offline_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_OFFLINE);

	ldap_port = e_source_authentication_get_port (auth_extension);
	/* If a port wasn't specified, default to LDAP_PORT. */
	if (ldap_port == 0)
		ldap_port = LDAP_PORT;

	switch (e_source_ldap_get_scope (ldap_extension)) {
		case E_SOURCE_LDAP_SCOPE_ONELEVEL:
			ldap_scope = LDAP_SCOPE_ONELEVEL;
			break;
		case E_SOURCE_LDAP_SCOPE_SUBTREE:
			ldap_scope = LDAP_SCOPE_SUBTREE;
			break;
		default:
			g_warn_if_reached ();
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);

	check_bool (bl->priv->marked_for_offline, e_source_offline_get_stay_synchronized (offline_extension));
	check_bool (bl->priv->marked_can_browse, e_source_ldap_get_can_browse (ldap_extension));
	check_direct (bl->priv->security, e_source_ldap_get_security (ldap_extension));
	check_str (bl->priv->ldap_host, e_source_authentication_dup_host (auth_extension));
	check_direct (bl->priv->ldap_port, ldap_port);
	check_direct (bl->priv->ldap_scope, ldap_scope);
	check_str (bl->priv->ldap_rootdn, e_source_ldap_dup_root_dn (ldap_extension));
	check_str (bl->priv->ldap_search_filter, e_source_ldap_dup_filter (ldap_extension));
	check_direct (bl->priv->ldap_limit, e_source_ldap_get_limit (ldap_extension));

	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	#undef check_str
	#undef check_direct
	#undef check_bool

	return changed;
}

static void
book_backend_ldap_check_settings_changed (EBookBackend *backend,
					  gpointer user_data,
					  GCancellable *cancellable,
					  GError **error)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);

	if (book_backend_ldap_read_settings (bl) && e_backend_get_online (E_BACKEND (backend))) {
		/* Cancel all running operations */
		ldap_cancel_all_operations (backend);

		e_book_backend_set_writable (backend, TRUE);

		if (e_book_backend_is_opened (backend) &&
		    e_book_backend_ldap_connect (bl, error)) {
			if (bl->priv->marked_for_offline && bl->priv->cache) {
				e_book_backend_cache_set_time (bl->priv->cache, "");
				generate_cache (bl);
			}
		}
	}
}

static void
book_backend_ldap_source_changed_cb (ESource *source,
				     gpointer user_data)
{
	EBookBackend *backend = user_data;
	EBookBackendLDAP *bl = user_data;

	g_return_if_fail (E_IS_BOOK_BACKEND_LDAP (bl));

	if ((bl->priv->marked_for_offline ? 0 : 1) != (get_marked_for_offline (backend) ? 1 : 0) ||
	    (bl->priv->marked_can_browse ? 0 : 1) != (can_browse (backend) ? 1 : 0)) {
		gchar *value;
		gboolean old_marked_for_offline = bl->priv->marked_for_offline;
		gboolean old_marked_can_browse = bl->priv->marked_can_browse;

		bl->priv->marked_for_offline = get_marked_for_offline (backend);
		bl->priv->marked_can_browse = can_browse (backend);

		value = book_backend_ldap_get_backend_property (backend, CLIENT_BACKEND_PROPERTY_CAPABILITIES);

		e_book_backend_notify_property_changed (backend, CLIENT_BACKEND_PROPERTY_CAPABILITIES, value);

		g_free (value);

		/* Restore the values, thus the below callback notices something changed
		   and will refresh the backend cache. */
		bl->priv->marked_for_offline = old_marked_for_offline;
		bl->priv->marked_can_browse = old_marked_can_browse;
	}

	e_book_backend_schedule_custom_operation (backend, NULL, book_backend_ldap_check_settings_changed, NULL, NULL);
}

static void
book_backend_ldap_open (EBookBackend *backend,
                        EDataBook *book,
                        guint opid,
                        GCancellable *cancellable)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	ESourceAuthentication *auth_extension;
	ESource *source;
	const gchar *extension_name;
	const gchar *cache_dir;
	gchar *filename;
	gboolean auth_required;
	GError *error = NULL;

	g_return_if_fail (!bl->priv->connected);

	if (enable_debug)
		printf ("%s: ...\n", G_STRFUNC);

	source = e_backend_get_source (E_BACKEND (backend));
	cache_dir = e_book_backend_get_cache_dir (backend);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	auth_extension = e_source_get_extension (source, extension_name);

	book_backend_ldap_read_settings (bl);

	g_clear_object (&bl->priv->cache);

	filename = g_build_filename (cache_dir, "cache.xml", NULL);
	bl->priv->cache = e_book_backend_cache_new (filename);
	g_free (filename);

	if (!e_backend_get_online (E_BACKEND (backend))) {

		/* Offline */
		e_book_backend_set_writable (backend, FALSE);

		if (!bl->priv->marked_for_offline)
			error = EC_ERROR (E_CLIENT_ERROR_OFFLINE_UNAVAILABLE);

		e_data_book_respond_open (book, opid, error);
		return;
	}

	e_book_backend_set_writable (backend, TRUE);

	e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTING);

	auth_required = e_source_authentication_required (auth_extension);

	if (!auth_required)
		e_book_backend_ldap_connect (bl, &error);

	if (g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_REQUIRED)) {
		g_clear_error (&error);
		auth_required = TRUE;
	}

	if (auth_required && error == NULL) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);

		e_backend_credentials_required_sync (E_BACKEND (backend), E_SOURCE_CREDENTIALS_REASON_REQUIRED,
			NULL, 0, NULL, cancellable, &error);
	} else if (!auth_required && !error) {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_CONNECTED);
	} else {
		e_source_set_connection_status (source, E_SOURCE_CONNECTION_STATUS_DISCONNECTED);
	}

	if (error != NULL && enable_debug)
		printf ("%s: failed to connect to server: %s\n", G_STRFUNC, error->message);

	/* Ignore 'Repository Offline' error when being marked for offline work */
	if (bl->priv->marked_for_offline && g_error_matches (error, E_CLIENT_ERROR, E_CLIENT_ERROR_REPOSITORY_OFFLINE))
		g_clear_error (&error);
	else if (!error && bl->priv->marked_for_offline)
		generate_cache (bl);

	g_signal_connect_object (source, "changed",
		G_CALLBACK (book_backend_ldap_source_changed_cb), bl, 0);

	e_data_book_respond_open (book, opid, error);
}

static void
book_backend_ldap_create_contacts (EBookBackend *backend,
				   EDataBook *book,
				   guint32 opid,
				   GCancellable *cancellable,
				   const gchar * const *vcards,
				   guint32 opflags)
{
	LDAPCreateOp *create_op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	EDataBookView *book_view;
	gint create_contact_msgid;
	gint err;
	GPtrArray *mod_array;
	LDAPMod **ldap_mods;
	gchar *new_uid;
	const gchar *vcard;
	gboolean is_list;
	GError *local_error = NULL;

	g_return_if_fail (vcards != NULL);

	vcard = vcards[0];

	/* We make the assumption that the vCard list we're passed is always exactly one element long, since we haven't specified "bulk-adds"
	 * in our static capability list. This is because there is no clean way to roll back changes in case of an error. */
	if (!vcards[0] || vcards[1]) {
		e_data_book_respond_create_contacts (
			book, opid,
			EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
			_("The backend does not support bulk additions")),
			NULL);
		return;
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_create_contacts (book, opid, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE), NULL);
		return;
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_create_contacts (book, opid, EC_ERROR_NOT_CONNECTED (), NULL);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	book_view = find_book_view (bl);

	if (enable_debug)
		printf ("Create Contact: vcard = %s\n", vcard);

	create_op = g_new0 (LDAPCreateOp, 1);
	create_op->new_contact = e_contact_new_from_vcard (vcard);

	g_rec_mutex_lock (&eds_ldap_handler_lock);

	new_uid = create_dn_from_contact (create_op->new_contact, bl->priv->ldap_rootdn);
	create_op->dn = create_full_dn_from_contact (new_uid, bl->priv->ldap_rootdn);

	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	e_contact_set (create_op->new_contact, E_CONTACT_UID, create_op->dn);

	is_list = e_contact_get (create_op->new_contact, E_CONTACT_IS_LIST) != NULL;

	/* build our mods */
	mod_array = build_mods_from_contacts (bl, NULL, create_op->new_contact, NULL, is_list ? NULL : new_uid, &local_error);
	g_free (new_uid);

	if (local_error) {
		free_mods (mod_array);
		e_data_book_respond_create_contacts (book, opid, local_error, NULL);
		create_contact_dtor ((LDAPOp *) create_op);
		return;
	}

	/* remove the NULL at the end */
	g_ptr_array_remove (mod_array, NULL);

	/* add our objectclass(es) */
	add_objectclass_mod (bl, mod_array, NULL, is_list, FALSE);

	/* then put the NULL back */
	g_ptr_array_add (mod_array, NULL);

#ifdef LDAP_DEBUG_ADD
	if (enable_debug) {
		gint i;
		printf ("Sending the following to the server as ADD\n");
		printf ("Adding DN: %s\n", create_op->dn);

		for (i = 0; g_ptr_array_index (mod_array, i); i++) {
			LDAPMod *mod = g_ptr_array_index (mod_array, i);
			if (mod->mod_op & LDAP_MOD_DELETE)
				printf ("del ");
			else if (mod->mod_op & LDAP_MOD_REPLACE)
				printf ("rep ");
			else
				printf ("add ");
			if (mod->mod_op & LDAP_MOD_BVALUES)
				printf ("ber ");
			else
				printf ("    ");

			printf (" %s:\n", mod->mod_type);

			if (mod->mod_op & LDAP_MOD_BVALUES) {
				gint j;
				for (j = 0; mod->mod_bvalues[j] && mod->mod_bvalues[j]->bv_val; j++)
					printf ("\t\t'%s'\n", mod->mod_bvalues[j]->bv_val);
			} else {
				gint j;

				for (j = 0; mod->mod_values[j]; j++)
					printf ("\t\t'%s'\n", mod->mod_values[j]);
			}
		}
	}
#endif

	ldap_mods = (LDAPMod **) mod_array->pdata;

	do {
		book_view_notify_status (bl, book_view, _("Adding contact to LDAP server..."));
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			err = ldap_add_ext (
				bl->priv->ldap, create_op->dn, ldap_mods,
				NULL, NULL, &create_contact_msgid);
		} else {
			err = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

	} while (e_book_backend_ldap_reconnect (bl, book_view, err));

	/* and clean up */
	free_mods (mod_array);

	if (LDAP_SUCCESS != err) {
		e_data_book_respond_create_contacts (
			book,
			opid,
			ldap_error_to_response (err),
			NULL);
		create_contact_dtor ((LDAPOp *) create_op);
		return;
	} else {
		g_print ("ldap_add_ext returned %d\n", err);
		ldap_op_add (
			(LDAPOp *) create_op, backend, book,
			book_view, opid, create_contact_msgid,
			create_contact_handler, create_contact_dtor);
	}
}

static void
book_backend_ldap_modify_contacts (EBookBackend *backend,
                                   EDataBook *book,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar * const *vcards,
				   guint32 opflags)
{
	LDAPModifyOp *modify_op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	gint ldap_error;
	gint modify_contact_msgid;
	EDataBookView *book_view;
	const gchar *vcard;

	g_return_if_fail (vcards != NULL);

	vcard = vcards[0];

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_modify_contacts (book, opid, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE), NULL);
		return;
	}

	/* We make the assumption that the vCard list we're passed is always exactly one element long, since we haven't specified "bulk-modifies"
	 * in our static capability list. This is because there is no clean way to roll back changes in case of an error. */
	if (!vcards[0] || vcards[1]) {
		e_data_book_respond_modify_contacts (book, opid,
						     EC_ERROR_EX (E_CLIENT_ERROR_NOT_SUPPORTED,
						     _("The backend does not support bulk modifications")),
						     NULL);
		return;
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_modify_contacts (book, opid, EC_ERROR_NOT_CONNECTED (), NULL);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	book_view = find_book_view (bl);

	if (enable_debug)
		printf ("Modify Contact: vcard = %s\n", vcard);
	modify_op = g_new0 (LDAPModifyOp, 1);
	modify_op->contact = e_contact_new_from_vcard (vcard);
	modify_op->id = e_contact_get_const (modify_op->contact, E_CONTACT_UID);

	do {
		book_view_notify_status (bl, book_view, _("Modifying contact from LDAP server..."));

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_error = ldap_search_ext (
				bl->priv->ldap, modify_op->id,
				LDAP_SCOPE_BASE,
				"(objectclass=*)",
				NULL, 0, NULL, NULL,
				NULL, /* XXX timeout */
				1, &modify_contact_msgid);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

	} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

	if (ldap_error == LDAP_SUCCESS) {
		ldap_op_add (
			(LDAPOp *) modify_op, backend, book,
			book_view, opid, modify_contact_msgid,
			modify_contact_search_handler, modify_contact_dtor);
	} else {
		e_data_book_respond_modify_contacts (book,
						     opid,
						     ldap_error_to_response (ldap_error),
						     NULL);
		modify_contact_dtor ((LDAPOp *) modify_op);
	}
}

static void
book_backend_ldap_remove_contacts (EBookBackend *backend,
                                   EDataBook *book,
                                   guint32 opid,
                                   GCancellable *cancellable,
                                   const gchar * const *uids,
				   guint32 opflags)
{
	LDAPRemoveOp *remove_op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	EDataBookView *book_view;
	gint remove_msgid;
	gint ldap_error;

	g_return_if_fail (uids != NULL);

	if (!e_backend_get_online (E_BACKEND (backend))) {
		e_data_book_respond_remove_contacts (book, opid, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE), NULL);
		return;
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_remove_contacts (book, opid, EC_ERROR_NOT_CONNECTED (), NULL);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	book_view = find_book_view (bl);

	/*
	** since we didn't pass "bulk-removes" in our static
	** capabilities, we should only get 1 length lists here, so
	** the id we're deleting is the first and only id in the list.
	*/
	remove_op = g_new0 (LDAPRemoveOp, 1);
	remove_op->id = g_strdup (uids[0]);

	do {
		book_view_notify_status (bl, book_view, _("Removing contact from LDAP server..."));

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_error = ldap_delete_ext (
				bl->priv->ldap,
				remove_op->id,
				NULL, NULL, &remove_msgid);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

	if (ldap_error != LDAP_SUCCESS) {
		e_data_book_respond_remove_contacts (
			book,
			opid,
			ldap_error_to_response (ldap_error),
			NULL);
		ldap_op_finished ((LDAPOp *) remove_op);
		remove_contact_dtor ((LDAPOp *) remove_op);
		return;
	} else {
		g_print ("ldap_delete_ext returned %d\n", ldap_error);
		ldap_op_add (
			(LDAPOp *) remove_op, backend, book,
			book_view, opid, remove_msgid,
			remove_contact_handler, remove_contact_dtor);
	}
}

static void
book_backend_ldap_get_contact (EBookBackend *backend,
                               EDataBook *book,
                               guint32 opid,
                               GCancellable *cancellable,
                               const gchar *id)
{
	LDAPGetContactOp *get_contact_op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	gint get_contact_msgid;
	EDataBookView *book_view;
	gint ldap_error;
	gint64 start = 0;

	if (!e_backend_get_online (E_BACKEND (backend))) {
		if (bl->priv->marked_for_offline && bl->priv->cache) {
			EContact *contact = e_book_backend_cache_get_contact (bl->priv->cache, id);

			if (!contact) {
				e_data_book_respond_get_contact (book, opid, EBC_ERROR (E_BOOK_CLIENT_ERROR_CONTACT_NOT_FOUND), NULL);
				return;
			}

			e_data_book_respond_get_contact (
				book,
				opid,
				NULL,
				contact);
			g_object_unref (contact);
			return;
		}

		e_data_book_respond_get_contact (book, opid, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE), NULL);
		return;
	}

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_get_contact (book, opid, EC_ERROR_NOT_CONNECTED (), NULL);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	get_contact_op = g_new0 (LDAPGetContactOp, 1);
	book_view = find_book_view (bl);

	do {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_error = ldap_search_ext (
				bl->priv->ldap, id,
				LDAP_SCOPE_BASE,
				"(objectclass=*)",
				NULL, 0, NULL, NULL,
				NULL, /* XXX timeout */
				1, &get_contact_msgid);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

	if (ldap_error == LDAP_SUCCESS) {
		ldap_op_add (
			(LDAPOp *) get_contact_op, backend, book,
			book_view, opid, get_contact_msgid,
			get_contact_handler, get_contact_dtor);

		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: invoked get_contact_handler, took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}
	} else {
		e_data_book_respond_get_contact (
			book,
			opid,
			ldap_error_to_response (ldap_error),
			NULL);
		get_contact_dtor ((LDAPOp *) get_contact_op);
	}
}

static void
book_backend_ldap_get_contact_list (EBookBackend *backend,
                                    EDataBook *book,
                                    guint32 opid,
                                    GCancellable *cancellable,
                                    const gchar *query)
{
	LDAPGetContactListOp *contact_list_op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	gint contact_list_msgid;
	EDataBookView *book_view;
	gint ldap_error;
	gchar *ldap_query;
	gint64 start = 0;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		if (bl->priv->marked_for_offline && bl->priv->cache) {
			GList *contacts;
			GSList *contacts_slist = NULL;
			GList *l;

			contacts = e_book_backend_cache_get_contacts (bl->priv->cache, query);

			for (l = contacts; l; l = g_list_next (l)) {
				EContact *contact = l->data;
				contacts_slist = g_slist_prepend (contacts_slist, contact);
			}

			e_data_book_respond_get_contact_list (book, opid, NULL, contacts_slist);

			g_list_free_full (contacts, g_object_unref);
			g_slist_free (contacts_slist);
			return;
		}

		e_data_book_respond_get_contact_list (book, opid, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE), NULL);
		return;
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_get_contact_list (book, opid, EC_ERROR_NOT_CONNECTED (), NULL);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	contact_list_op = g_new0 (LDAPGetContactListOp, 1);
	book_view = find_book_view (bl);

	ldap_query = e_book_backend_ldap_build_query (bl, query);

	if (enable_debug)
		printf ("getting contact list with filter: %s\n", ldap_query);

	do {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_error = ldap_search_ext (
				bl->priv->ldap,
				bl->priv->ldap_rootdn,
				bl->priv->ldap_scope,
				ldap_query,
				NULL, 0, NULL, NULL,
				NULL, /* XXX timeout */
				LDAP_NO_LIMIT, &contact_list_msgid);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

	g_free (ldap_query);

	if (ldap_error == LDAP_SUCCESS) {
		ldap_op_add (
			(LDAPOp *) contact_list_op, backend, book,
			book_view, opid, contact_list_msgid,
			contact_list_handler, contact_list_dtor);
		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: invoked contact_list_handler, took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}
	} else {
		e_data_book_respond_get_contact_list (
			book,
			opid,
			ldap_error_to_response (ldap_error),
			NULL);
		contact_list_dtor ((LDAPOp *) contact_list_op);
	}
}

static void
book_backend_ldap_get_contact_list_uids (EBookBackend *backend,
                                         EDataBook *book,
                                         guint32 opid,
                                         GCancellable *cancellable,
                                         const gchar *query)
{
	LDAPGetContactListUIDsOp *contact_list_uids_op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	gint contact_list_uids_msgid;
	EDataBookView *book_view;
	gint ldap_error;
	gchar *ldap_query;
	gint64 start = 0;

	if (enable_debug) {
		printf ("%s: ...\n", G_STRFUNC);
		start = g_get_monotonic_time ();
	}

	if (!e_backend_get_online (E_BACKEND (backend))) {
		if (bl->priv->marked_for_offline && bl->priv->cache) {
			GList *contacts;
			GSList *uids = NULL;
			GList *l;

			contacts = e_book_backend_cache_get_contacts (bl->priv->cache, query);

			for (l = contacts; l; l = g_list_next (l)) {
				EContact *contact = l->data;
				uids = g_slist_prepend (uids, e_contact_get (contact, E_CONTACT_UID));
				g_object_unref (contact);
			}

			g_list_free (contacts);
			e_data_book_respond_get_contact_list_uids (book, opid, NULL, uids);
			g_slist_free_full (uids, g_free);
			return;
		}

		e_data_book_respond_get_contact_list_uids (book, opid, EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE), NULL);
		return;
	}

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_get_contact_list_uids (book, opid, EC_ERROR_NOT_CONNECTED (), NULL);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	contact_list_uids_op = g_new0 (LDAPGetContactListUIDsOp, 1);
	book_view = find_book_view (bl);

	ldap_query = e_book_backend_ldap_build_query (bl, query);

	if (enable_debug)
		printf ("getting contact list uids with filter: %s\n", ldap_query);

	do {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_error = ldap_search_ext (
				bl->priv->ldap,
				bl->priv->ldap_rootdn,
				bl->priv->ldap_scope,
				ldap_query,
				NULL, 0, NULL, NULL,
				NULL, /* XXX timeout */
				LDAP_NO_LIMIT, &contact_list_uids_msgid);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

	g_free (ldap_query);

	if (ldap_error == LDAP_SUCCESS) {
		ldap_op_add (
			(LDAPOp *) contact_list_uids_op, backend, book,
			book_view, opid, contact_list_uids_msgid,
			contact_list_uids_handler, contact_list_uids_dtor);
		if (enable_debug) {
			GTimeSpan diff = g_get_monotonic_time () - start;

			printf ("%s: invoked contact_list_uids_handler, took %" G_GINT64_FORMAT ".%03" G_GINT64_FORMAT " seconds\n",
				G_STRFUNC, diff / G_TIME_SPAN_SECOND, diff % G_TIME_SPAN_SECOND);
		}
	} else {
		e_data_book_respond_get_contact_list_uids (book, opid, ldap_error_to_response (ldap_error), NULL);
		contact_list_uids_dtor ((LDAPOp *) contact_list_uids_op);
	}
}

typedef struct {
	LDAPOp op;
	gboolean found;
} LDAPContainsEmailOp;

static void
contains_email_dtor (LDAPOp *op)
{
	LDAPContainsEmailOp *contains_email_op = (LDAPContainsEmailOp *) op;

	g_slice_free (LDAPContainsEmailOp, contains_email_op);
}

static void
contains_email_handler (LDAPOp *op,
			LDAPMessage *res)
{
	LDAPContainsEmailOp *contains_email_op = (LDAPContainsEmailOp *) op;
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (op->backend);
	LDAPMessage *e;
	gint msg_type;

	if (enable_debug)
		printf ("%s:\n", G_STRFUNC);

	g_rec_mutex_lock (&eds_ldap_handler_lock);
	if (!bl->priv->ldap) {
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		e_data_book_respond_contains_email (op->book, op->opid, EC_ERROR_NOT_CONNECTED (), FALSE);
		ldap_op_finished (op);
		if (enable_debug)
			printf ("%s: ldap handler is NULL\n", G_STRFUNC);
		return;
	}
	g_rec_mutex_unlock (&eds_ldap_handler_lock);

	msg_type = ldap_msgtype (res);
	if (msg_type == LDAP_RES_SEARCH_ENTRY) {
		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap)
			e = ldap_first_entry (bl->priv->ldap, res);
		else
			e = NULL;
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		while (NULL != e) {
			EContact *contact;
			gchar *uid = NULL;

			contact = build_contact_from_entry (bl, e, NULL, &uid);
			g_clear_object (&contact);

			if (enable_debug)
				printf ("uid = %s\n", uid ? uid : "(null)");

			if (uid) {
				contains_email_op->found = TRUE;
				g_free (uid);
			}

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap)
				e = ldap_next_entry (bl->priv->ldap, e);
			else
				e = NULL;
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
		}
	} else if (msg_type == LDAP_RES_SEARCH_REFERENCE) {
		/* ignore references */
	} else if (msg_type == LDAP_RES_SEARCH_RESULT) {
		gchar *ldap_error_msg = NULL;
		gint ldap_error;

		g_rec_mutex_lock (&eds_ldap_handler_lock);
		if (bl->priv->ldap) {
			ldap_parse_result (
				bl->priv->ldap, res, &ldap_error,
				NULL, &ldap_error_msg, NULL, NULL, 0);
		} else {
			ldap_error = LDAP_SERVER_DOWN;
		}
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
		if (ldap_error != LDAP_SUCCESS) {
			printf (
				"%s: %02X (%s), additional info: %s\n", G_STRFUNC,
				ldap_error,
				ldap_err2string (ldap_error), ldap_error_msg);
		}
		if (ldap_error_msg)
			ldap_memfree (ldap_error_msg);

		if (ldap_error == LDAP_TIMELIMIT_EXCEEDED)
			e_data_book_respond_contains_email (
				op->book, op->opid,
				EC_ERROR (E_CLIENT_ERROR_SEARCH_TIME_LIMIT_EXCEEDED),
				FALSE);
		else if (ldap_error == LDAP_SIZELIMIT_EXCEEDED)
			e_data_book_respond_contains_email (
				op->book, op->opid,
				EC_ERROR (E_CLIENT_ERROR_SEARCH_SIZE_LIMIT_EXCEEDED),
				FALSE);
		else if (ldap_error == LDAP_SUCCESS)
			e_data_book_respond_contains_email (
				op->book, op->opid,
				NULL,
				contains_email_op->found);
		else
			e_data_book_respond_contains_email (
				op->book, op->opid,
				ldap_error_to_response (ldap_error),
				contains_email_op->found);

		ldap_op_finished (op);
	} else {
		g_warning ("unhandled search result type %d returned", msg_type);
		e_data_book_respond_contains_email (
			op->book, op->opid,
			e_client_error_create_fmt (E_CLIENT_ERROR_OTHER_ERROR,
			_("%s: Unhandled search result type %d returned"), G_STRFUNC, msg_type),
			FALSE);
		ldap_op_finished (op);
	}
}

static gboolean
book_backend_ldap_gather_addresses_cb (gpointer ptr_name,
				       gpointer ptr_email,
				       gpointer user_data)
{
	GPtrArray *array = user_data;
	const gchar *email = ptr_email;

	if (email && *email)
		g_ptr_array_add (array, e_book_query_field_test (E_CONTACT_EMAIL, E_BOOK_QUERY_IS, email));

	return TRUE;
}

static void
book_backend_ldap_contains_email (EBookBackend *backend,
				  EDataBook *book,
				  guint32 opid,
				  GCancellable *cancellable,
				  const gchar *email_address)
{
	EBookBackendLDAP *bl = E_BOOK_BACKEND_LDAP (backend);
	EBookQuery *book_query = NULL;
	GPtrArray *array;
	gchar *sexp = NULL;
	GError *error = NULL;
	gboolean found = FALSE, finished = TRUE;

	array = g_ptr_array_new_full (1, (GDestroyNotify) e_book_query_unref);

	e_book_util_foreach_address (email_address, book_backend_ldap_gather_addresses_cb, array);

	if (array->len > 0)
		book_query = e_book_query_or (array->len, (EBookQuery **) array->pdata, FALSE);

	if (book_query)
		sexp = e_book_query_to_string (book_query);

	if (sexp) {
		if (bl->priv->cache) {
			GList *contacts;
			contacts = e_book_backend_cache_get_contacts (bl->priv->cache, sexp);
			found = contacts != NULL;
			g_list_free_full (contacts, g_object_unref);
		}

		if (!found && !e_backend_get_online (E_BACKEND (backend))) {
			if (!bl->priv->marked_for_offline)
				error = EC_ERROR (E_CLIENT_ERROR_REPOSITORY_OFFLINE);
		} else if (!found) {
			LDAPContainsEmailOp *contains_email_op;
			gint contains_email_msgid = 0;
			EDataBookView *book_view;
			gint ldap_error;
			gchar *ldap_query;

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (!bl->priv->ldap) {
				g_rec_mutex_unlock (&eds_ldap_handler_lock);
				error = EC_ERROR_NOT_CONNECTED ();
				goto out;
			}
			g_rec_mutex_unlock (&eds_ldap_handler_lock);

			contains_email_op = g_slice_new0 (LDAPContainsEmailOp);
			book_view = find_book_view (bl);

			ldap_query = e_book_backend_ldap_build_query (bl, sexp);

			if (enable_debug)
				printf ("checking emails with filter: '%s'\n", ldap_query);

			do {
				g_rec_mutex_lock (&eds_ldap_handler_lock);
				if (bl->priv->ldap) {
					ldap_error = ldap_search_ext (
						bl->priv->ldap,
						bl->priv->ldap_rootdn,
						bl->priv->ldap_scope,
						ldap_query,
						NULL, 0, NULL, NULL,
						NULL, /* XXX timeout */
						1 /* sizelimit */, &contains_email_msgid);
				} else {
					ldap_error = LDAP_SERVER_DOWN;
				}
				g_rec_mutex_unlock (&eds_ldap_handler_lock);
			} while (e_book_backend_ldap_reconnect (bl, book_view, ldap_error));

			g_free (ldap_query);

			if (ldap_error == LDAP_SUCCESS) {
				finished = FALSE;
				ldap_op_add (
					(LDAPOp *) contains_email_op, backend, book,
					book_view, opid, contains_email_msgid,
					contains_email_handler, contains_email_dtor);
			} else {
				error = ldap_error_to_response (ldap_error);
				contains_email_dtor ((LDAPOp *) contains_email_op);
			}
		}
	} else {
		error = EC_ERROR (E_CLIENT_ERROR_INVALID_QUERY);
	}

 out:
	if (finished)
		e_data_book_respond_contains_email (book, opid, error, found);
	else
		g_clear_error (&error);

	g_clear_pointer (&book_query, e_book_query_unref);
	g_ptr_array_unref (array);
	g_free (sexp);
}

static ESourceAuthenticationResult
book_backend_ldap_authenticate_sync (EBackend *backend,
				     const ENamedParameters *credentials,
				     gchar **out_certificate_pem,
				     GTlsCertificateFlags *out_certificate_errors,
				     GCancellable *cancellable,
				     GError **error)
{
	ESourceAuthenticationResult result;
	EBookBackendLDAP *bl;
	ESourceAuthentication *auth_extension;
	ESource *source;
	gint ldap_error;
	gchar *dn = NULL;
	const gchar *username;
	gchar *method;
	gchar *auth_user;

	bl = E_BOOK_BACKEND_LDAP (backend);
	source = e_backend_get_source (backend);

	auth_extension = e_source_get_extension (source, E_SOURCE_EXTENSION_AUTHENTICATION);

	/* We should not have gotten here if we're offline. */
	g_return_val_if_fail (
		e_backend_get_online (backend),
		E_SOURCE_AUTHENTICATION_ERROR);

	method = e_source_authentication_dup_method (auth_extension);
	auth_user = e_source_authentication_dup_user (auth_extension);

	username = e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_USERNAME);
	if (!username || !*username) {
		username = auth_user;
	}

	if (!method)
		method = g_strdup ("none");

	if (!g_ascii_strncasecmp (method, LDAP_SIMPLE_PREFIX, strlen (LDAP_SIMPLE_PREFIX))) {

		if (bl->priv->ldap && !strcmp (method, "ldap/simple-email")) {
			LDAPMessage    *res, *e;
			gchar *query = g_strdup_printf ("(mail=%s)", username);
			gchar *entry_dn;

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap) {
				ldap_error = ldap_search_s (
					bl->priv->ldap,
					bl->priv->ldap_rootdn,
					bl->priv->ldap_scope,
					query,
					NULL, 0, &res);
			} else {
				ldap_error = LDAP_SERVER_DOWN;
			}
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			g_free (query);

			if (ldap_error != LDAP_SUCCESS)
				goto exit;

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap)
				e = ldap_first_entry (bl->priv->ldap, res);
			else
				e = NULL;
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			if (!e) {
				ldap_msgfree (res);
				g_set_error (
					error, G_IO_ERROR,
					G_IO_ERROR_INVALID_DATA,
					_("Failed to get the DN "
					"for user “%s”"), username);

				g_free (method);
				g_free (auth_user);

				return E_SOURCE_AUTHENTICATION_ERROR;
			}

			g_rec_mutex_lock (&eds_ldap_handler_lock);
			if (bl->priv->ldap)
				entry_dn = ldap_get_dn (bl->priv->ldap, e);
			else
				entry_dn = NULL;
			bl->priv->connected = FALSE; /* to reconnect with credentials */
			g_rec_mutex_unlock (&eds_ldap_handler_lock);
			dn = g_strdup (entry_dn);

			ldap_memfree (entry_dn);
			ldap_msgfree (res);

		} else if (!g_strcmp0 (method, "ldap/simple-binddn")) {
			dn = g_strdup (username);
		}

		g_free (bl->priv->auth_dn);
		g_free (bl->priv->auth_secret);

		bl->priv->auth_dn = dn;
		bl->priv->auth_secret = g_strdup (e_named_parameters_get (credentials, E_SOURCE_CREDENTIAL_PASSWORD));

		/* now authenticate against the DN we were either supplied or queried for */
		if (enable_debug)
			printf ("simple auth as '%s'\n", dn);

		g_rec_mutex_lock (&eds_ldap_handler_lock);

		if (!bl->priv->connected || !bl->priv->ldap) {
			GError *local_error = NULL;

			g_rec_mutex_unlock (&eds_ldap_handler_lock);

			e_book_backend_ldap_connect (bl, &local_error);

			g_free (method);
			g_free (auth_user);

			if (local_error == NULL) {
				return E_SOURCE_AUTHENTICATION_ACCEPTED;

			} else if (g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
				g_clear_error (&local_error);
				return E_SOURCE_AUTHENTICATION_REJECTED;

			} else {
				g_propagate_error (error, local_error);
				return E_SOURCE_AUTHENTICATION_ERROR;
			}
		}

		ldap_error = ldap_simple_bind_s (
			bl->priv->ldap,
			bl->priv->auth_dn,
			bl->priv->auth_secret);
		g_rec_mutex_unlock (&eds_ldap_handler_lock);

		/* Some ldap servers are returning (ex active directory ones)
		 * LDAP_SERVER_DOWN when we try to do an ldap operation after
		 * being idle for some time. This error is handled by poll_ldap
		 * in case of search operations. 
		 *
		 * We need to handle it explicitly for this bind call.
		 * We call reconnect so that we get a fresh ldap handle.
		 * Fixes #67541 */
		if (ldap_error == LDAP_SERVER_DOWN) {
			if (e_book_backend_ldap_reconnect (bl, find_book_view (bl), ldap_error))
				ldap_error = LDAP_SUCCESS;
		}
	}
#ifdef ENABLE_SASL_BINDS
	else if (!g_ascii_strncasecmp (method, SASL_PREFIX, strlen (SASL_PREFIX))) {
		g_print ("sasl bind (mech = %s) as %s", method + strlen (SASL_PREFIX), username);
		g_rec_mutex_lock (&eds_ldap_handler_lock);

		if (!bl->priv->connected || !bl->priv->ldap) {
			GError *local_error = NULL;

			g_rec_mutex_unlock (&eds_ldap_handler_lock);

			e_book_backend_ldap_connect (bl, &local_error);

			g_free (method);
			g_free (auth_user);

			if (local_error == NULL) {
				return E_SOURCE_AUTHENTICATION_ACCEPTED;

			} else if (g_error_matches (local_error, E_CLIENT_ERROR, E_CLIENT_ERROR_AUTHENTICATION_FAILED)) {
				g_clear_error (&local_error);
				return E_SOURCE_AUTHENTICATION_REJECTED;

			} else {
				g_propagate_error (error, local_error);
				return E_SOURCE_AUTHENTICATION_ERROR;
			}
		}

		ldap_error = ldap_sasl_bind_s (
			bl->priv->ldap,
			NULL,
			method + strlen (SASL_PREFIX),
			bl->priv->auth_secret,
			NULL,
			NULL,
			NULL);
		g_rec_mutex_unlock (&eds_ldap_handler_lock);
	}
#endif
	else {
		ldap_error = LDAP_NOT_SUPPORTED;
	}

exit:
	switch (ldap_error) {
		case LDAP_SUCCESS:
			e_book_backend_set_writable (E_BOOK_BACKEND (backend), TRUE);

			/* force a requery on the root dse since some ldap
			 * servers are set up such that they don't report
			 * anything (including the schema DN) until the user
			 * is authenticated */
			if (!bl->priv->evolutionPersonChecked) {
				ldap_error = query_ldap_root_dse (bl);

				if (LDAP_SUCCESS == ldap_error) {
					if (!bl->priv->evolutionPersonChecked)
						check_schema_support (bl);
				} else
					g_warning ("Failed to perform root dse query after authenticating, (ldap_error 0x%02x)", ldap_error);
			}

			if (bl->priv->marked_for_offline && bl->priv->cache)
				generate_cache (bl);

			result = E_SOURCE_AUTHENTICATION_ACCEPTED;
			break;

		case LDAP_INVALID_CREDENTIALS:
			result = E_SOURCE_AUTHENTICATION_REJECTED;
			break;

		case LDAP_NOT_SUPPORTED:
			g_propagate_error (
				error, EC_ERROR (E_CLIENT_ERROR_UNSUPPORTED_AUTHENTICATION_METHOD));
			result = E_SOURCE_AUTHENTICATION_ERROR;
			break;

		default:
			g_propagate_error (
				error, ldap_error_to_response (ldap_error));
			result = E_SOURCE_AUTHENTICATION_ERROR;
			break;
	}

	g_free (method);
	g_free (auth_user);

	return result;
}

static void
e_book_backend_ldap_class_init (EBookBackendLDAPClass *class)
{
	GObjectClass  *object_class;
	EBackendClass *backend_class;
	EBookBackendClass *book_backend_class;

#ifndef SUNLDAP
	/* get client side information (extensions present in the library) */
	get_ldap_library_info ();
#endif

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = book_backend_ldap_finalize;

	backend_class = E_BACKEND_CLASS (class);
	backend_class->authenticate_sync = book_backend_ldap_authenticate_sync;

	book_backend_class = E_BOOK_BACKEND_CLASS (class);
	book_backend_class->impl_get_backend_property = book_backend_ldap_get_backend_property;
	book_backend_class->impl_open = book_backend_ldap_open;
	book_backend_class->impl_create_contacts = book_backend_ldap_create_contacts;
	book_backend_class->impl_modify_contacts = book_backend_ldap_modify_contacts;
	book_backend_class->impl_remove_contacts = book_backend_ldap_remove_contacts;
	book_backend_class->impl_get_contact = book_backend_ldap_get_contact;
	book_backend_class->impl_get_contact_list = book_backend_ldap_get_contact_list;
	book_backend_class->impl_get_contact_list_uids = book_backend_ldap_get_contact_list_uids;
	book_backend_class->impl_contains_email = book_backend_ldap_contains_email;
	book_backend_class->impl_start_view = book_backend_ldap_start_view;
	book_backend_class->impl_stop_view = book_backend_ldap_stop_view;
	book_backend_class->impl_refresh = book_backend_ldap_refresh;

	/* Register our ESource extension. */
	g_type_ensure (E_TYPE_SOURCE_LDAP);
}

static void
e_book_backend_ldap_init (EBookBackendLDAP *backend)
{
	backend->priv = e_book_backend_ldap_get_instance_private (backend);

	backend->priv->ldap_limit = 100;
	backend->priv->id_to_op = g_hash_table_new (g_int_hash, g_int_equal);

	g_mutex_init (&backend->priv->view_mutex);
	g_rec_mutex_init (&backend->priv->op_hash_mutex);

	if (g_getenv ("LDAP_DEBUG"))
		enable_debug = TRUE;

	g_signal_connect (
		backend, "notify::online",
		G_CALLBACK (e_book_backend_ldap_notify_online_cb), NULL);
}
