/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Srinivasa Ragavan <sragavan@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#ifdef HAVE_CONFIG_H
#include <config.h>  
#endif

#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <glib.h>

#include <sys/time.h>
/*
** #include <glib/gi18n-lib.h>
*/

#include <libedataserver/e-sexp.h>
#include "libedataserver/e-flag.h"
#include <libebook/e-contact.h>

#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-cache.h>
#include <libedata-book/e-book-backend-summary.h>
#include "e-book-backend-mapi.h"


static EBookBackendClass *e_book_backend_mapi_parent_class;
static gboolean enable_debug = TRUE;

struct _EBookBackendMAPIPrivate
{
	char *profile;
	mapi_id_t fid;
	int mode;
	gboolean marked_for_offline;
	gboolean is_cache_ready;
	gboolean is_summary_ready;
	gboolean is_writable;
	char *uri;
	char *book_name;
	
	GMutex *lock;
	char *summary_file_name;
	EBookBackendSummary *summary;
	EBookBackendCache *cache;

};

#define LOCK() g_mutex_lock (priv->lock)
#define UNLOCK() g_mutex_unlock (priv->lock)

#define ELEMENT_TYPE_SIMPLE 0x01
#define ELEMENT_TYPE_COMPLEX 0x02 /* fields which require explicit functions to set values into EContact and EGwItem */

#define SUMMARY_FLUSH_TIMEOUT 5000
#define ELEMENT_TYPE_SIMPLE 0x01
#define ELEMENT_TYPE_COMPLEX 0x02

static EContact * emapidump_contact(struct mapi_SPropValue_array *properties);

static const struct field_element_mapping {
		EContactField field_id;
		int element_type;
	        int mapi_id;
	        int contact_type;
//		char *element_name;
//		void (*populate_contact_func)(EContact *contact,    gpointer data);
//		void (*set_value_in_gw_item) (EGwItem *item, gpointer data);
//		void (*set_changes) (EGwItem *new_item, EGwItem *old_item);

	} mappings [] = { 

	{ E_CONTACT_UID, PT_STRING8, 0, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_REV, PT_SYSTIME, PR_LAST_MODIFICATION_TIME, ELEMENT_TYPE_SIMPLE},
		
	{ E_CONTACT_FILE_AS, PT_STRING8, PR_EMS_AB_MANAGER_T, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_FULL_NAME, PT_STRING8, PR_DISPLAY_NAME, ELEMENT_TYPE_SIMPLE },
	{ E_CONTACT_GIVEN_NAME, PT_STRING8, PR_GIVEN_NAME, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_FAMILY_NAME, PT_STRING8, PR_SURNAME , ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_NICKNAME, PT_STRING8, PR_NICKNAME, ELEMENT_TYPE_SIMPLE },

	{ E_CONTACT_EMAIL_1, PT_STRING8, 0x8083001e, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_EMAIL_2, PT_STRING8, 0x8093001e, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_EMAIL_3, PT_STRING8, 0x80a3001e, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_IM_AIM, PT_STRING8, 0x8062001e, ELEMENT_TYPE_COMPLEX},
		
	{ E_CONTACT_PHONE_BUSINESS, PT_STRING8, PR_OFFICE_TELEPHONE_NUMBER, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_PHONE_HOME, PT_STRING8, PR_HOME_TELEPHONE_NUMBER, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_PHONE_MOBILE, PT_STRING8, PR_MOBILE_TELEPHONE_NUMBER, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_PHONE_HOME_FAX, PT_STRING8, PR_HOME_FAX_NUMBER ,ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_PHONE_BUSINESS_FAX, PT_STRING8, PR_BUSINESS_FAX_NUMBER,ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_PHONE_PAGER, PT_STRING8, PR_PAGER_TELEPHONE_NUMBER,ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_PHONE_ASSISTANT, PT_STRING8, PR_ASSISTANT_TELEPHONE_NUMBER ,ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_PHONE_COMPANY, PT_STRING8, PR_COMPANY_MAIN_PHONE_NUMBER ,ELEMENT_TYPE_SIMPLE},

	{ E_CONTACT_HOMEPAGE_URL, PT_STRING8, 0x802b001e, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_FREEBUSY_URL, PT_STRING8, 0x80d8001e, ELEMENT_TYPE_SIMPLE},

	{ E_CONTACT_ROLE, PT_STRING8, PR_PROFESSION, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_TITLE, PT_STRING8, PR_TITLE, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_ORG, PT_STRING8, PR_COMPANY_NAME, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_ORG_UNIT, PT_STRING8, PR_DEPARTMENT_NAME,ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_MANAGER, PT_STRING8, PR_MANAGER_NAME, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_ASSISTANT, PT_STRING8, PR_ASSISTANT, ELEMENT_TYPE_SIMPLE},
		
	{ E_CONTACT_OFFICE, PT_STRING8, PR_OFFICE_LOCATION, ELEMENT_TYPE_SIMPLE},
	{ E_CONTACT_SPOUSE, PT_STRING8, PR_SPOUSE_NAME, ELEMENT_TYPE_SIMPLE},
		
	{ E_CONTACT_BIRTH_DATE,  PT_SYSTIME, PR_BIRTHDAY, ELEMENT_TYPE_COMPLEX},
	{ E_CONTACT_ANNIVERSARY, PT_SYSTIME, PR_WEDDING_ANNIVERSARY, ELEMENT_TYPE_COMPLEX},
		  
	{ E_CONTACT_NOTE, PT_STRING8, PR_BODY, ELEMENT_TYPE_SIMPLE},
		

	{ E_CONTACT_ADDRESS_HOME, PT_STRING8, 0x801a001e, ELEMENT_TYPE_COMPLEX},
	{ E_CONTACT_ADDRESS_WORK, PT_STRING8, 0x801c001e, ELEMENT_TYPE_COMPLEX},
//		{ E_CONTACT_BOOK_URI, ELEMENT_TYPE_SIMPLE, "book_uri"}
//		{ E_CONTACT_EMAIL, PT_STRING8, 0x8084001e},
//		{ E_CONTACT_CATEGORIES, },		
	};

static maplen = G_N_ELEMENTS(mappings);

static EDataBookView *
find_book_view (EBookBackendMAPI *ebmapi)
{
	EList *views = e_book_backend_get_book_views (E_BOOK_BACKEND (ebmapi));
	EIterator *iter;
	EDataBookView *rv = NULL;

	if (!views)
		return NULL;

	iter = e_list_get_iterator (views);

	if (!iter) {
		g_object_unref (views);
		return NULL;
	}

	if (e_iterator_is_valid (iter)) {
		/* just always use the first book view */
		EDataBookView *v = (EDataBookView*)e_iterator_get(iter);
		if (v)
			rv = v;
	}

	g_object_unref (iter);
	g_object_unref (views);
	
	return rv;
}

#define IS_RFC2254_CHAR(c) ((c) == '*' || (c) =='\\' || (c) == '(' || (c) == ')' || (c) == '\0')
static char *
rfc2254_escape(char *str)
{
	int i;
	int len = strlen(str);
	int newlen = 0;

	for (i = 0; i < len; i ++) {
		if (IS_RFC2254_CHAR(str[i]))
			newlen += 3;
		else
			newlen ++;
	}

	if (len == newlen) {
		return g_strdup (str);
	}
	else {
		char *newstr = g_malloc0 (newlen + 1);
		int j = 0;
		for (i = 0; i < len; i ++) {
			if (IS_RFC2254_CHAR(str[i])) {
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

/* Sigh somewhere libmapi/samba defines it and it breaks us. */
#undef bool

static ESExpResult *
func_and(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	GString *string;
	int i;

	/* Check for short circuit */
	for (i = 0; i < argc; i++) {
		if (argv[i]->type == ESEXP_RES_BOOL &&
		    argv[i]->value.bool == FALSE) {
			r = e_sexp_result_new(f, ESEXP_RES_BOOL);
			r->value.bool = FALSE;
			return r;
		} else if (argv[i]->type == ESEXP_RES_UNDEFINED)
			return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	}

	string = g_string_new("(&");
	for (i = 0; i < argc; i ++) {
		if (argv[i]->type != ESEXP_RES_STRING)
			continue;
		g_string_append(string, argv[i]->value.string);
	}
	g_string_append(string, ")");

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = string->str;
	g_string_free(string, FALSE);

	return r;
}

static ESExpResult *
func_or(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	GString *string;
	int i;

	/* Check for short circuit */
	for (i = 0; i < argc; i++) {
		if (argv[i]->type == ESEXP_RES_BOOL &&
		    argv[i]->value.bool == TRUE) {
			r = e_sexp_result_new(f, ESEXP_RES_BOOL);
			r->value.bool = TRUE;
			return r;
		} else if (argv[i]->type == ESEXP_RES_UNDEFINED)
			return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	}

	string = g_string_new("(|");
	for (i = 0; i < argc; i ++) {
		if (argv[i]->type != ESEXP_RES_STRING)
			continue;
		g_string_append(string, argv[i]->value.string);
	}
	g_string_append(string, ")");

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = string->str;
	g_string_free(string, FALSE);

	return r;
}

static ESExpResult *
func_not(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;

	if (argc != 1 ||
	    (argv[0]->type != ESEXP_RES_STRING &&
	     argv[0]->type != ESEXP_RES_BOOL))
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);

	if (argv[0]->type == ESEXP_RES_STRING) {
		r = e_sexp_result_new(f, ESEXP_RES_STRING);
		r->value.string = g_strdup_printf ("(!%s)",
						   argv[0]->value.string);
	} else {
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = !argv[0]->value.bool;
	}

	return r;
}

static ESExpResult *
func_contains(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	char *propname, *ldap_attr, *str;

	if (argc != 2 ||
	    argv[0]->type != ESEXP_RES_STRING ||
	    argv[1]->type != ESEXP_RES_STRING)
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);

	propname = argv[0]->value.string;
	str = argv[1]->value.string;

	if (!strcmp(propname, "x-evolution-any-field")) {
		/* This gui does (contains "x-evolution-any-field" ""),
		 * when you hit "Clear". We want that to be empty. But
		 * other "any field contains" searches should give an
		 * error.
		 */
		if (strlen(str) == 0) {
			r = e_sexp_result_new(f, ESEXP_RES_BOOL);
			r->value.bool = FALSE;
		} else {
			r = e_sexp_result_new(f, ESEXP_RES_STRING);
			r->value.string = g_strdup_printf ("(mailNickname=%s)", str);			
		}
		
		return r;
	}

	ldap_attr = query_prop_to_ldap(argv[0]->value.string);
	if (!ldap_attr) {
		/* Attribute doesn't exist, so it can't possibly match */
		r = e_sexp_result_new(f, ESEXP_RES_BOOL);
		r->value.bool = FALSE;
		return r;
	}

	/* AD doesn't do substring indexes, so we only allow
	 * (contains FIELD ""), meaning "FIELD exists".
	 */
	if (strlen(str) == 0) {
		r = e_sexp_result_new(f, ESEXP_RES_STRING);
		r->value.string = g_strdup_printf ("(%s=*)", ldap_attr);
	} else if (!strcmp(propname, "file_as")) {
		r = e_sexp_result_new(f, ESEXP_RES_STRING);
		r->value.string = g_strdup_printf ("(|(displayName=%s*)(sn=%s*)(%s=%s*))", str, str, ldap_attr, str);
	} else 
		r = e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	return r;
}

static ESExpResult *
func_is_or_begins_with(ESExp *f, int argc, ESExpResult **argv, gboolean exact)
{
	ESExpResult *r;
	char *propname, *str, *star, *filter;

	if (argc != 2
	    || argv[0]->type != ESEXP_RES_STRING
	    || argv[1]->type != ESEXP_RES_STRING)
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);

	propname = argv[0]->value.string;
	str = rfc2254_escape(argv[1]->value.string);
	star = exact ? "" : "*";

	if (!exact && strlen (str) == 0 && strcmp(propname, "file_as")) {
		/* Can't do (beginswith FIELD "") */
		return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
	}

	/* We use the query "(beginswith fileas "")" while building cache for
	 * GAL offline, where we try to retrive all the contacts and store it 
	 * locally. Retrieving *all* the contacts may not be possible in case 
	 * of large number of contacts and huge data, (for the same reason
	 * we don't support empty queries in GAL when online.) In such cases 
	 * cache may not be complete.
	 */
	if (!strcmp(propname, "file_as")) {
		filter = g_strdup_printf("(displayName=%s%s)", str, star);
		goto done;
	}

	if (!strcmp (propname, "full_name")) {
		char *first, *last, *space;

		space = strchr (str, ' ');
		if (space && space > str) {
			if (*(space - 1) == ',') {
				first = g_strdup (space + 1);
				last = g_strndup (str, space - str - 1);
			} else {
				first = g_strndup (str, space - str);
				last = g_strdup (space + 1);
			}
			filter = g_strdup_printf("(|(displayName=%s%s)(sn=%s%s)(givenName=%s%s)(&(givenName=%s%s)(sn=%s%s)))",
						 str, star, str, star,
						 str, star, first, star,
						 last, star);
			g_free (first);
			g_free (last);
		} else {
			filter = g_strdup_printf("(|(displayName=%s%s)(sn=%s%s)(givenName=%s%s)(mailNickname=%s%s))",
						 str, star, str, star,
						 str, star, str, star);
		}
	} else 
		filter = g_strdup_printf("(%s=%s%s)", "email", str, star);

 done:
	g_free (str);

	r = e_sexp_result_new(f, ESEXP_RES_STRING);
	r->value.string = filter;
	return r;
}

static ESExpResult *
func_is(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	return func_is_or_begins_with(f, argc, argv, TRUE);
}

static ESExpResult *
func_beginswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	return func_is_or_begins_with(f, argc, argv, FALSE);
}

static ESExpResult *
func_endswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	/* We don't allow endswith searches */
	return e_sexp_result_new(f, ESEXP_RES_UNDEFINED);
}

/* 'builtin' functions */
static struct {
	char *name;
	ESExpFunc *func;
} symbols[] = {
	{ "and", func_and },
	{ "or", func_or },
	{ "not", func_not },
	{ "contains", func_contains },
	{ "is", func_is },
	{ "beginswith", func_beginswith },
	{ "endswith", func_endswith },
};

static int
build_query (const char *query, char **query_email)
{
	ESExp *sexp;
	ESExpResult *r;
	int i, retval;

	sexp = e_sexp_new();

	for(i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++) {
		e_sexp_add_function(sexp, 0, symbols[i].name,
				    symbols[i].func, NULL);
	}

	e_sexp_input_text (sexp, query, strlen (query));
	e_sexp_parse (sexp);

	r = e_sexp_eval (sexp);

	if (r->type == ESEXP_RES_STRING) {
		*query_email = g_strdup (r->value.string);
		retval = GNOME_Evolution_Addressbook_Success;
	} else if (r->type == ESEXP_RES_BOOL) {
		/* If it's FALSE, that means "no matches". If it's TRUE
		 * that means "everything matches", but we don't support
		 * that, so it also means "no matches".
		 */
		*query_email = NULL;
		retval = GNOME_Evolution_Addressbook_Success;
	} else {
		/* Bad query */
		*query_email = NULL;
		retval = GNOME_Evolution_Addressbook_QueryRefused;
	}

	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);

	return retval;
}

static gboolean
build_restriction_emails_contains (struct mapi_SRestriction *res, 
				   char *query)
{
	char *email=NULL, *tmp;;
	int status;
	

	/* This currently supports "is email foo@bar.soo" */
	status = build_query (query, &email);
	email = strchr (email, '=');
	email++;
	tmp = strchr (email, ')');
	*tmp = 0;
	printf("building restrition on %s\n", email);

	if (status != GNOME_Evolution_Addressbook_Success || email==NULL)
		return FALSE;

	res->rt = RES_PROPERTY;
	res->res.resProperty.relop = RES_PROPERTY;
	res->res.resProperty.ulPropTag = 0x8084001e; /* EMAIL */
	res->res.resProperty.lpProp.ulPropTag = 0x8084001e; /* EMAIL*/
	res->res.resProperty.lpProp.value.lpszA = email;

	return TRUE;
}

static char *
get_filename_from_uri (const char *uri, const char *file)
{
	char *mangled_uri, *filename;
	int i;

	/* mangle the URI to not contain invalid characters */
	mangled_uri = g_strdup (uri);
	for (i = 0; i < strlen (mangled_uri); i++) {
		switch (mangled_uri[i]) {
		case ':' :
		case '/' :
			mangled_uri[i] = '_';
		}
	}

	/* generate the file name */
	filename = g_build_filename (g_get_home_dir (), ".evolution/cache/addressbook",
				     mangled_uri, file, NULL);

	/* free memory */
	g_free (mangled_uri);

	return filename;
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_mapi_load_source (EBookBackend           *backend,
				       ESource                *source,
				       gboolean                only_if_exists)
{
	char *tmp;
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;
	char * offline;
	char **tokens;
	char *uri;
	if (enable_debug)
		printf("MAPI load source\n");
	offline = e_source_get_property (source, "offline_sync");
	if (offline  && g_str_equal (offline, "1"))
		priv->marked_for_offline = TRUE;



	/* Either we are in Online mode or this is marked for offline */
	
	priv->uri = g_strdup (e_source_get_uri (source));

	tokens = g_strsplit (priv->uri, ";", 2);
  	if (tokens[0])
 		uri = g_strdup (tokens [0]);
  	priv->book_name  = g_strdup (tokens[1]);
  	if (priv->book_name == NULL) {
		g_warning ("Bookname is null for %s\n", uri);
  		return GNOME_Evolution_Addressbook_OtherError;
	}
  	g_strfreev (tokens);

	if (priv->mode ==  GNOME_Evolution_Addressbook_MODE_LOCAL &&
	    !priv->marked_for_offline ) {
		return GNOME_Evolution_Addressbook_OfflineUnavailable;
	}
	
	if (priv->marked_for_offline) {
 		priv->summary_file_name = get_filename_from_uri (priv->uri, "cache.summary"); 
		if (g_file_test (priv->summary_file_name, G_FILE_TEST_EXISTS)) {
			printf("Loading the summary\n");
			priv->summary = e_book_backend_summary_new (priv->summary_file_name, 
								    SUMMARY_FLUSH_TIMEOUT);
			e_book_backend_summary_load (priv->summary);
			priv->is_summary_ready = TRUE;
		}

		/* Load the cache as well.*/
		if (e_book_backend_cache_exists (priv->uri)) {
			printf("Loading the cache\n");
			priv->cache = e_book_backend_cache_new (priv->uri);
			priv->is_cache_ready = TRUE;
		}
		//FIXME: We may have to do a time based reload. Or deltas should upload.
	}

	g_free (uri);
	e_book_backend_set_is_loaded (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_set_is_writable (backend, TRUE);	
	if (priv->mode ==  GNOME_Evolution_Addressbook_MODE_LOCAL) {
		e_book_backend_set_is_writable (backend, FALSE);
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE);
		if (!priv->cache) {
			printf("Unfortunately the cache is not yet created\n");
			return GNOME_Evolution_Addressbook_OfflineUnavailable;
		}
	} else {
		e_book_backend_notify_connection_status (backend, TRUE);
	}
	
	priv->profile = g_strdup (e_source_get_property (source, "profile"));
	exchange_mapi_util_mapi_id_from_string (e_source_get_property (source, "folder-id"), &priv->fid);

	tmp = e_source_get_property (source, "folder-id");
	printf("Folder is %s %016llX\n", tmp, priv->fid);

	/* Once aunthentication in address book works this can be removed */
	if (priv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		return GNOME_Evolution_Addressbook_Success;
	}

	// writable property will be set in authenticate_user callback
	e_book_backend_set_is_loaded (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_notify_connection_status (E_BOOK_BACKEND (backend), TRUE);


	if (enable_debug)
		printf("For profile %s and folder %s - %016llX\n", priv->profile, tmp, priv->fid);

	return GNOME_Evolution_Addressbook_Success;
}

static char *
e_book_backend_mapi_get_static_capabilities (EBookBackend *backend)
{
	if(enable_debug)
		printf("mapi get_static_capabilities\n");
	//FIXME: Implement this.
	
	return g_strdup ("net,bulk-removes,do-initial-query,contact-lists");
}

gboolean
build_name_id (struct mapi_nameid *nameid, gpointer data)
{
	EContact *contact = data;
	
	mapi_nameid_OOM_add(nameid, "FileAs", PSETID_Address);
	mapi_nameid_lid_add(nameid, 0x8084, PSETID_Address);
	mapi_nameid_OOM_add(nameid, "Email1Address", PSETID_Address);

	mapi_nameid_lid_add(nameid, 0x8093, PSETID_Address);
	mapi_nameid_lid_add(nameid, 0x80A3, PSETID_Address);
	
	mapi_nameid_string_add(nameid, "urn:schemas:contacts:fileas", PS_PUBLIC_STRINGS);

	mapi_nameid_OOM_add(nameid, "WebPage", PSETID_Address);
	mapi_nameid_OOM_add(nameid, "IMAddress", PSETID_Address);

	mapi_nameid_OOM_add(nameid, "HomeAddress", PSETID_Address);	
	mapi_nameid_OOM_add(nameid, "BusinessAddress", PSETID_Address);

	// FIXME : Patch has to go into libmapi.
	//	mapi_nameid_lid_add(nameid, 0x3A4F, PS_MAPI);

	mapi_nameid_lid_add(nameid, 0x8094, PSETID_Address);
	mapi_nameid_lid_add(nameid, 0x80A4, PSETID_Address);
	

	printf("NAMMMMMMMMMMMM %d\n", nameid->count);

	return TRUE;
}

#define set_str_value(field_id, hex) if (e_contact_get (contact, field_id)) set_SPropValue_proptag (&props[i++], hex, e_contact_get (contact, field_id));

int
build_props (struct SPropValue ** value, struct SPropTagArray * SPropTagArray, gpointer data)
{
	EContact *contact = data;	
	int len = -1;
	struct SPropValue *props;
	int i=0;

	for (i=0; i<13; i++)
		printf("hex %x\n", SPropTagArray->aulPropTag[i]);
	i=0;
	props = g_new (struct SPropValue, 50); //FIXME: Correct value tbd
	set_str_value ( E_CONTACT_FILE_AS, SPropTagArray->aulPropTag[0]);

	set_str_value (E_CONTACT_FULL_NAME, PR_DISPLAY_NAME);
	set_SPropValue_proptag(&props[i++], PR_MESSAGE_CLASS, (const void *)"IPM.Contact");
	set_str_value (E_CONTACT_FILE_AS, PR_NORMALIZED_SUBJECT);
	set_str_value (E_CONTACT_EMAIL_1,  SPropTagArray->aulPropTag[1]);
	set_str_value (E_CONTACT_EMAIL_1,  SPropTagArray->aulPropTag[2]);
	set_str_value (E_CONTACT_FILE_AS,  SPropTagArray->aulPropTag[5]);

	
//	set_str_value ( E_CONTACT_EMAIL_1, 0x8083001e);
	set_str_value ( E_CONTACT_EMAIL_2, SPropTagArray->aulPropTag[3]);
	set_str_value ( E_CONTACT_EMAIL_2, SPropTagArray->aulPropTag[11]);
	
	set_str_value ( E_CONTACT_EMAIL_3, SPropTagArray->aulPropTag[4]);
	set_str_value ( E_CONTACT_EMAIL_3, SPropTagArray->aulPropTag[12]);
	
	set_str_value (E_CONTACT_HOMEPAGE_URL, SPropTagArray->aulPropTag[6]);
	set_str_value (E_CONTACT_FREEBUSY_URL, 0x812C001E);
	

	set_str_value ( E_CONTACT_PHONE_BUSINESS, PR_OFFICE_TELEPHONE_NUMBER);
	set_str_value ( E_CONTACT_PHONE_HOME, PR_HOME_TELEPHONE_NUMBER);
	set_str_value ( E_CONTACT_PHONE_MOBILE, PR_MOBILE_TELEPHONE_NUMBER);
	set_str_value ( E_CONTACT_PHONE_HOME_FAX, PR_HOME_FAX_NUMBER);
	set_str_value ( E_CONTACT_PHONE_BUSINESS_FAX, PR_BUSINESS_FAX_NUMBER);
	set_str_value ( E_CONTACT_PHONE_PAGER, PR_PAGER_TELEPHONE_NUMBER);
	set_str_value ( E_CONTACT_PHONE_ASSISTANT, PR_ASSISTANT_TELEPHONE_NUMBER);
	set_str_value ( E_CONTACT_PHONE_COMPANY, PR_COMPANY_MAIN_PHONE_NUMBER);

	set_str_value (E_CONTACT_MANAGER, PR_MANAGER_NAME);
	set_str_value (E_CONTACT_ASSISTANT, PR_ASSISTANT);
	set_str_value (E_CONTACT_ORG, PR_COMPANY_NAME);
	set_str_value (E_CONTACT_ORG_UNIT, PR_DEPARTMENT_NAME);
	set_str_value (E_CONTACT_ROLE, PR_PROFESSION);
	set_str_value (E_CONTACT_TITLE, PR_TITLE);

	set_str_value (E_CONTACT_OFFICE, PR_OFFICE_LOCATION);
	set_str_value (E_CONTACT_SPOUSE, PR_SPOUSE_NAME);

	set_str_value (E_CONTACT_NOTE, PR_BODY);

	//BDAY AND ANNV
	if (e_contact_get (contact, E_CONTACT_BIRTH_DATE)) {
		EContactDate *date = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
		struct tm tmtime;
		time_t lt;
		NTTIME nt;
		struct FILETIME t;
		
		tmtime.tm_mday = date->day - 1;
		tmtime.tm_mon = date->month - 1;
		tmtime.tm_year = date->year - 1900;

		lt = mktime (&tmtime);
		unix_to_nt_time (&nt, lt);
		t.dwLowDateTime = (nt << 32) >> 32;
		t.dwHighDateTime = (nt >> 32);
		printf("sending bday\n");
		set_SPropValue_proptag (&props[i++], PR_BIRTHDAY, &t);
	}

	if (e_contact_get (contact, E_CONTACT_ANNIVERSARY)) {
		EContactDate *date = e_contact_get (contact, E_CONTACT_ANNIVERSARY);
		struct tm tmtime;
		time_t lt;
		NTTIME nt;
		struct FILETIME t;
		
		tmtime.tm_mday = date->day - 1;
		tmtime.tm_mon = date->month - 1;
		tmtime.tm_year = date->year - 1900;

		lt = mktime (&tmtime);
		unix_to_nt_time (&nt, lt);
		t.dwLowDateTime = (nt << 32) >> 32;
		t.dwHighDateTime = (nt >> 32);
		printf("sending wed\n");
		set_SPropValue_proptag (&props[i++], PR_WEDDING_ANNIVERSARY, &t);
	}	
	//Home and Office address
	if (e_contact_get (contact, E_CONTACT_ADDRESS_HOME)) {
		EContactAddress *contact_addr;

		contact_addr = e_contact_get (contact, E_CONTACT_ADDRESS_HOME);
		set_SPropValue_proptag (&props[i++], SPropTagArray->aulPropTag[8], contact_addr->street);
		set_SPropValue_proptag (&props[i++], PR_HOME_ADDRESS_POST_OFFICE_BOX, contact_addr->ext);
		set_SPropValue_proptag (&props[i++], PR_HOME_ADDRESS_CITY, contact_addr->locality);
		set_SPropValue_proptag (&props[i++], PR_HOME_ADDRESS_STATE_OR_PROVINCE, contact_addr->region);
		set_SPropValue_proptag (&props[i++], PR_HOME_ADDRESS_POSTAL_CODE, contact_addr->code);
		set_SPropValue_proptag (&props[i++], PR_HOME_ADDRESS_COUNTRY, contact_addr->country);				
	}

	if (e_contact_get (contact, E_CONTACT_ADDRESS_WORK)) {
		EContactAddress *contact_addr;

		contact_addr = e_contact_get (contact, E_CONTACT_ADDRESS_WORK);
		set_SPropValue_proptag (&props[i++], SPropTagArray->aulPropTag[9], contact_addr->street);
		set_SPropValue_proptag (&props[i++], PR_POST_OFFICE_BOX, contact_addr->ext);
		set_SPropValue_proptag (&props[i++], PR_LOCALITY, contact_addr->locality);
		set_SPropValue_proptag (&props[i++], PR_STATE_OR_PROVINCE, contact_addr->region);
		set_SPropValue_proptag (&props[i++], PR_POSTAL_CODE, contact_addr->code);
		set_SPropValue_proptag (&props[i++], PR_COUNTRY, contact_addr->country);				
	}

	
// 	set_str_value (E_CONTACT_NICKNAME, SPropTagArray->aulPropTag[10]); 
	if (e_contact_get (contact, E_CONTACT_IM_AIM)) {
		GList *l = e_contact_get (contact, E_CONTACT_IM_AIM);
		set_SPropValue_proptag (&props[i++], SPropTagArray->aulPropTag[7], l->data);
	}

	if (e_contact_get (contact, E_CONTACT_NICKNAME)) {
		char *nick  = e_contact_get (contact, E_CONTACT_NICKNAME);
		set_SPropValue_proptag (&props[i++], SPropTagArray->aulPropTag[10], nick);
		printf("nickname %s %x\n", nick,  SPropTagArray->aulPropTag[10]);
	}
	
	*value =props;
	printf("Sending %d \n", i);
	return i;
}

static void
e_book_backend_mapi_create_contact (EBookBackend *backend,
					  EDataBook *book,
					  guint32 opid,
					  const char *vcard )
{
	EContact *contact;
	char *id;
	mapi_id_t status;
	int element_type;
	char* value;
	int i;
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;

	if(enable_debug)
		printf("mapi create_contact \n");
	
	switch (priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		return;
	   
	case  GNOME_Evolution_Addressbook_MODE_REMOTE :
		contact = e_contact_new_from_vcard(vcard);
		status = exchange_mapi_create_item (olFolderContacts, priv->fid, build_name_id, contact, build_props, contact, NULL, NULL);
		if (!status) {
			e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			return;
		}
		id = exchange_mapi_util_mapi_ids_to_uid (priv->fid, status); 
	
		/* UID of the contact is nothing but the concatenated string of hex id of folder and the message.*/
		e_contact_set (contact, E_CONTACT_UID, id);		
		e_contact_set (contact, E_CONTACT_BOOK_URI, priv->uri);
		
		//somehow get the mid.
		//add to summary and cache.
		if (priv->marked_for_offline && priv->is_cache_ready)
			e_book_backend_cache_add_contact (priv->cache, contact);

		if (priv->marked_for_offline && priv->is_summary_ready)
			e_book_backend_summary_add_contact (priv->summary, contact);

		e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_Success, contact);
		return;			
	}
	
	return;
}

struct folder_data {
	mapi_id_t id;
};
static void
e_book_backend_mapi_remove_contacts (EBookBackend *backend,
					   EDataBook    *book,
					   guint32 opid,
					   GList *id_list)
{
	GSList *list=NULL, *tmp = id_list;
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;
	mapi_id_t fid, mid;
			
	if(enable_debug)
		printf("mapi: remove_contacts\n");

	switch (priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_remove_contacts (book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		return;

	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		
		while (tmp) {
			struct folder_data *data = g_new (struct folder_data, 1);
			exchange_mapi_util_mapi_ids_from_uid (tmp->data, &fid, &mid);
			data->id = mid;
			list = g_slist_prepend (list, (gpointer) data);
			tmp = tmp->next;
		}

		exchange_mapi_remove_items (olFolderContacts, priv->fid, list);
		if (priv->marked_for_offline && priv->is_cache_ready) {
			tmp = id_list;
			while (tmp) {
				e_book_backend_cache_remove_contact (priv->cache, tmp->data);
				tmp = tmp->next;
			}
		}

		if (priv->marked_for_offline && priv->is_summary_ready) {
			tmp = id_list;
			while (tmp) {
				e_book_backend_summary_remove_contact (priv->summary, tmp->data);		
				tmp = tmp->next;
			}
		}
		
		g_slist_free (list);
		e_data_book_respond_remove_contacts (book, opid,
							     GNOME_Evolution_Addressbook_Success,  id_list);
		return;
	default:
		break;
	}
}

static void
e_book_backend_mapi_modify_contact (EBookBackend *backend,
					  EDataBook    *book,
					  guint32       opid,
					  const char   *vcard)
{
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;
	EContact *contact;
	mapi_id_t fid, mid;
	gboolean status;
	char *tmp, *id;
	GList *l = NULL;
	
	if(enable_debug)
		printf("mapi: modify_contacts\n");

	switch (priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_modify(book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		return;
	case GNOME_Evolution_Addressbook_MODE_REMOTE :
		contact = e_contact_new_from_vcard(vcard);
		tmp = e_contact_get (contact, E_CONTACT_UID);
		exchange_mapi_util_mapi_ids_from_uid (tmp, &fid, &mid);		
		printf("modify id %s\n", tmp);
		
		status = exchange_mapi_modify_item (olFolderContacts, priv->fid, mid, build_name_id, contact, build_props, contact);
		printf("getting %016llX\n", status);
		if (!status) {
			e_data_book_respond_modify(book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			return;
		}
		
		e_contact_set (contact, E_CONTACT_BOOK_URI, priv->uri);

		//FIXME: Write it cleanly
		if (priv->marked_for_offline && priv->is_cache_ready)
			printf("delete cache %d\n", e_book_backend_cache_remove_contact (priv->cache, tmp));

		if (priv->marked_for_offline && priv->is_summary_ready)
				e_book_backend_summary_remove_contact (priv->summary, tmp);
		
		if (priv->marked_for_offline && priv->is_cache_ready)
			e_book_backend_cache_add_contact (priv->cache, contact);

		if (priv->marked_for_offline && priv->is_summary_ready)
			e_book_backend_summary_add_contact (priv->summary, contact);
		
		
		e_data_book_respond_modify (book, opid, GNOME_Evolution_Addressbook_Success, contact);


	}
}

static gpointer
create_contact_item (struct mapi_SPropValue_array *array, mapi_id_t fid, mapi_id_t mid)
{
	EContact *contact;
	char *suid;
	
	contact = emapidump_contact (array);
	suid = exchange_mapi_util_mapi_ids_to_uid (fid, mid);
	printf("got contact %s\n", suid);
	if (contact) {
		/* UID of the contact is nothing but the concatenated string of hex id of folder and the message.*/
		e_contact_set (contact, E_CONTACT_UID, suid);		
	}

	g_free (suid);
	return (gpointer) contact;
}

static void
e_book_backend_mapi_get_contact (EBookBackend *backend,
				       EDataBook    *book,
				       guint32       opid,
				       const char   *id)
{
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;
	EContact *contact;
	char *vcard;
	
	if (enable_debug)
		printf("mapi: get_contact %s\n", id);

	switch (priv->mode) {
	
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		contact = e_book_backend_cache_get_contact (priv->cache,
							    id);
		if (contact) {
			vcard =  e_vcard_to_string (E_VCARD (contact), 
						     EVC_FORMAT_VCARD_30);
			e_data_book_respond_get_contact (book,
							 opid,
							 GNOME_Evolution_Addressbook_Success,
							 vcard);
			g_free (vcard);
			g_object_unref (contact);
			return;
		}
		else {
			e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_ContactNotFound, "");			
			return;
		}
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		if (priv->marked_for_offline && e_book_backend_cache_is_populated (priv->cache)) {
			contact = e_book_backend_cache_get_contact (priv->cache,
								    id);
			if (contact) {
				vcard =  e_vcard_to_string (E_VCARD (contact), 
							     EVC_FORMAT_VCARD_30);
				e_data_book_respond_get_contact (book,
								 opid,
								 GNOME_Evolution_Addressbook_Success,
								 vcard);
				g_free (vcard);
				g_object_unref (contact);
				return;
			}
			else {
				e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_ContactNotFound, "");			
				return;
			}

		} else {
			mapi_id_t fid, mid;
			
			exchange_mapi_util_mapi_ids_from_uid (id, &fid, &mid);
			contact = exchange_mapi_connection_fetch_item (olFolderContacts, priv->fid, mid, create_contact_item);
			if (contact) {
				e_contact_set (contact, E_CONTACT_BOOK_URI, priv->uri);
				vcard =  e_vcard_to_string (E_VCARD (contact), 
							     EVC_FORMAT_VCARD_30);
				e_data_book_respond_get_contact (book,
								 opid,
								 GNOME_Evolution_Addressbook_Success,
								 vcard);
				g_free (vcard);
				g_object_unref (contact);
				return;
			
			} else {
				e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_ContactNotFound, "");			
				return;				
			}
		}

	default:
		break;
	}

	return;
	
}

static gboolean
create_contact_list_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, GSList *recipients, GSList *attachments, gpointer data)
{
	GList *list = * (GList **) data;
	EContact *contact;
	char *suid;
	
	contact = emapidump_contact (array);
	suid = exchange_mapi_util_mapi_ids_to_uid (fid, mid);
	
	if (contact) {
		/* UID of the contact is nothing but the concatenated string of hex id of folder and the message.*/
		e_contact_set (contact, E_CONTACT_UID, suid);		
//		e_contact_set (contact, E_CONTACT_BOOK_URI, priv->uri);
		//FIXME: Should we set this? How can we get this first?
		list = g_list_prepend (list, e_vcard_to_string (E_VCARD (contact),
							        EVC_FORMAT_VCARD_30));
		g_object_unref (contact);
	}

	g_free (suid);
	return TRUE;
}

static void
e_book_backend_mapi_get_contact_list (EBookBackend *backend,
					    EDataBook    *book,
					    guint32       opid,
					    const char   *query )
{
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;

	printf("mapi: get contact list %s\n", query);
	switch (priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		if (priv->marked_for_offline && priv->cache) {
			GList *contacts;
			GList *vcard_strings = NULL;
			GList *l;

			contacts = e_book_backend_cache_get_contacts (priv->cache, query);

			for (l = contacts; l; l = g_list_next (l)) {
				EContact *contact = l->data;
				vcard_strings = g_list_prepend (vcard_strings, e_vcard_to_string (E_VCARD (contact),
								EVC_FORMAT_VCARD_30));
				g_object_unref (contact);
			}

			g_list_free (contacts);
			printf("get_contact_list in  %s returning %d contacts\n", priv->uri, g_list_length (vcard_strings));
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_Success, vcard_strings);
			return;
		}
		e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_RepositoryOffline,
						      NULL);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		printf("Mode : Remote\n");
		if (priv->marked_for_offline && priv->cache) {
			GList *contacts;
			GList *vcard_strings = NULL;
			GList *l;

			contacts = e_book_backend_cache_get_contacts (priv->cache, query);

			for (l = contacts; l ;l = g_list_next (l)) {
				EContact *contact = l->data;
				vcard_strings = g_list_prepend (vcard_strings, e_vcard_to_string (E_VCARD (contact),
							        EVC_FORMAT_VCARD_30));
				g_object_unref (contact);
			}

			g_list_free (contacts);
			printf("get_contact_list in %s  returning %d contacts\n", priv->uri, g_list_length (vcard_strings));			
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_Success, vcard_strings);
			return ;
		}
		else {
			struct mapi_SRestriction res;
			GList *vcard_str = NULL;
			if (1 || !build_restriction_emails_contains (&res, query)) {
				e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
				return ;				
			}
			if (!exchange_mapi_connection_fetch_items (olFolderContacts, &res, create_contact_list_cb, priv->fid, &vcard_str)) {
				e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
				return ;
			}
			printf("get_contact_list in %s returning %d contacts\n", priv->uri, g_list_length (vcard_str));			
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_Success, vcard_str);
			return ;
			
		}
	}	
}

typedef struct {
	EBookBackendMAPI *bg;
	GThread *thread;
	EFlag *running;
} BESearchClosure;

static void
closure_destroy (BESearchClosure *closure)
{
	e_flag_free (closure->running);
	g_free (closure);
}

static BESearchClosure*
init_closure (EDataBookView *book_view, EBookBackendMAPI *bg)
{
	BESearchClosure *closure = g_new (BESearchClosure, 1);

	closure->bg = bg;
	closure->thread = NULL;
	closure->running = e_flag_new ();

	g_object_set_data_full (G_OBJECT (book_view), "closure",
				closure, (GDestroyNotify)closure_destroy);

	return closure;
}

static BESearchClosure*
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (G_OBJECT (book_view), "closure");
}

static void
mapi_dump_props (struct mapi_SPropValue_array *properties)
{
	int i;
	
	for (i = 0; i < properties->cValues; i++) {
		struct mapi_SPropValue *lpProp = &properties->lpProps[i];
		const char *tmp =  get_proptag_name (lpProp->ulPropTag);
		if (tmp && *tmp)
			printf("%s \t",tmp);
		else
			printf("%x \t", lpProp->ulPropTag);
		switch(lpProp->ulPropTag & 0xFFFF) {
		case PT_BOOLEAN:
			printf(" (bool) - %d\n", lpProp->value.b);
			break;
		case PT_I2:
			printf(" (uint16_t) - %d\n", lpProp->value.i);
			break;
		case PT_LONG:
			printf(" (long) - %ld\n", lpProp->value.l);
			break;
		case PT_DOUBLE:
			printf (" (double) -  %lf\n", lpProp->value.dbl);
			break;
		case PT_I8:
			printf (" (int) - %d\n", lpProp->value.d);
			break;
		case PT_SYSTIME:
			printf (" (struct FILETIME *) - %p\n", &lpProp->value.ft);
			break;
		case PT_ERROR:
			printf (" (error) - %p\n", lpProp->value.err);
			break;
		case PT_STRING8:
			printf(" (string) - %s\n", lpProp->value.lpszA ? lpProp->value.lpszA : "null" );
			break;
		case PT_UNICODE:
			printf(" (unicodestring) - %s\n", lpProp->value.lpszW ? lpProp->value.lpszW : "null");
			break;
		case PT_BINARY:
			printf(" (struct SBinary_short *) - %p\n", &lpProp->value.bin);
			break;
		case PT_MV_STRING8:
			printf(" (struct mapi_SLPSTRArray *) - %p\n", &lpProp->value.MVszA);
			break;
		default:
			printf(" - NONE NULL\n");
		}
		
	}
	
}
//FIXME: Be more clever in dumping contacts. Can we have a callback mechanism for each types?
static EContact *
emapidump_contact(struct mapi_SPropValue_array *properties)
{
	EContact *contact = e_contact_new ();
	int i;
	
//	mapi_dump_props (properties);
	for (i=1; i<maplen; i++) {
		gpointer value;

		value = find_mapi_SPropValue_data (properties, mappings[i].mapi_id);
		if (mappings[i].element_type == PT_STRING8 && mappings[i].contact_type == ELEMENT_TYPE_SIMPLE) {
			if (value)
				e_contact_set (contact, mappings[i].field_id, value);
		} else if (mappings[i].contact_type == ELEMENT_TYPE_SIMPLE) {
			if (value && mappings[i].element_type == PT_SYSTIME) {
				struct FILETIME *t = value;
				time_t time;
				NTTIME nt;
				char *tmp;
				nt = t->dwHighDateTime;
				nt = nt << 32;
				nt |= t->dwLowDateTime;
				time = nt_time_to_unix (nt);
				tmp = ctime (&time);
				e_contact_set (contact, mappings[i].field_id, tmp);
				//g_free (tmp);
			} else
				printf("Nothing is printed\n");
		} else if (mappings[i].contact_type == ELEMENT_TYPE_COMPLEX) {
			if (mappings[i].field_id == E_CONTACT_IM_AIM) {
				GList *list = NULL;
				EVCardAttribute *attr;
				
				attr = e_vcard_attribute_new ("", e_contact_vcard_attribute(E_CONTACT_IM_AIM));
//				e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), "AIM");
				e_vcard_attribute_add_value (attr, value);
				list = g_list_append (list, value);
				printf("%s -----\n", value);
				e_contact_set (contact, mappings[i].field_id, list);
				//FIXME: FREE them
			} else if (mappings[i].field_id == E_CONTACT_BIRTH_DATE
				   || mappings[i].field_id == E_CONTACT_ANNIVERSARY) {
				struct FILETIME *t = value;
				time_t time;
				NTTIME nt;
				struct tm * tmtime;
				if (value) {
					EContactDate *date = g_new (EContactDate, 1);
					nt = t->dwHighDateTime;
					nt = nt << 32;
					nt |= t->dwLowDateTime;
					time = nt_time_to_unix (nt);
					tmtime = gmtime (&time);
					//FIXME: Move to new libmapi api to get string dates.
					date->day = tmtime->tm_mday + 1;
					date->month = tmtime->tm_mon + 1;
					date->year = tmtime->tm_year + 1900;
					e_contact_set (contact, mappings[i].field_id, date);
					
				}
				
			} else if (mappings[i].field_id == E_CONTACT_ADDRESS_WORK
				   || mappings[i].field_id == E_CONTACT_ADDRESS_HOME) {
				EContactAddress *contact_addr;

				contact_addr = g_new0(EContactAddress, 1);
				if (mappings[i].field_id == E_CONTACT_ADDRESS_HOME) {
						contact_addr->address_format = NULL;
						contact_addr->po = NULL;
						contact_addr->street = value;
						contact_addr->ext = find_mapi_SPropValue_data (properties, PR_HOME_ADDRESS_POST_OFFICE_BOX);
						contact_addr->locality = find_mapi_SPropValue_data (properties, PR_HOME_ADDRESS_CITY);
						contact_addr->region = find_mapi_SPropValue_data (properties, PR_HOME_ADDRESS_STATE_OR_PROVINCE);
						contact_addr->code = find_mapi_SPropValue_data (properties, PR_HOME_ADDRESS_POSTAL_CODE);
						contact_addr->country = find_mapi_SPropValue_data (properties, PR_HOME_ADDRESS_COUNTRY);

				} else {
					printf("Value %s\n", value);
						contact_addr->address_format = NULL;
						contact_addr->po = NULL;
						contact_addr->street = value;
						contact_addr->ext = find_mapi_SPropValue_data (properties, PR_POST_OFFICE_BOX);
						contact_addr->locality = find_mapi_SPropValue_data (properties, PR_LOCALITY);
						contact_addr->region = find_mapi_SPropValue_data (properties, PR_STATE_OR_PROVINCE);
						contact_addr->code = find_mapi_SPropValue_data (properties, PR_POSTAL_CODE);
						contact_addr->country = find_mapi_SPropValue_data (properties, PR_COUNTRY);
				}
				e_contact_set (contact, mappings[i].field_id, contact_addr);
				//FIXME: Free everything.
				
			}
			
			
		}
	}
	
	return contact;
}

static void
get_contacts_from_cache (EBookBackendMAPI *ebmapi, 
			 const char *query,
			 GPtrArray *ids,
			 EDataBookView *book_view, 
			 BESearchClosure *closure)
{
	int i;

	if (enable_debug)
		printf ("\nread contacts from cache for the ids found in summary\n");
	for (i = 0; i < ids->len; i ++) {
		char *uid;
		EContact *contact; 

                if (!e_flag_is_set (closure->running))
                        break;

 		uid = g_ptr_array_index (ids, i);
		contact = e_book_backend_cache_get_contact (ebmapi->priv->cache, uid);
		if (contact) {
			e_data_book_view_notify_update (book_view, contact);
			g_object_unref (contact);
		}
	}
	if (e_flag_is_set (closure->running))
		e_data_book_view_notify_complete (book_view, 
						  GNOME_Evolution_Addressbook_Success);
}

static gboolean
create_contact_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, GSList *recipients, GSList *attachments, gpointer data)
{
	EDataBookView *book_view = data;
	BESearchClosure *closure = get_closure (book_view);
	EBookBackendMAPI *be = closure->bg;
	EContact *contact;
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) be)->priv;
	char *suid;
	
	if (!e_flag_is_set (closure->running)) {
		printf("Might be that the operation is cancelled. Lets ask our parent also to do.\n");
		return FALSE;
	}
	
	contact = emapidump_contact (array);
	suid = exchange_mapi_util_mapi_ids_to_uid (fid, mid);
	
	if (contact) {
		/* UID of the contact is nothing but the concatenated string of hex id of folder and the message.*/
		e_contact_set (contact, E_CONTACT_UID, suid);		
		e_contact_set (contact, E_CONTACT_BOOK_URI, priv->uri);
		e_data_book_view_notify_update (book_view, contact);
		g_object_unref(contact);
	}

	g_free (suid);
	return TRUE;
}

static void
book_view_thread (gpointer data)
{
	EDataBookView *book_view = data;
	BESearchClosure *closure = get_closure (book_view);
	EBookBackend  *backend = closure->bg;
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;
	const char *query = NULL;
	GPtrArray *ids = NULL;
	GList *contacts = NULL, *temp_list = NULL;
	
	if (enable_debug)
		printf("mapi: book view\n");
	
	bonobo_object_ref (book_view);
	e_flag_set (closure->running);
						
	e_data_book_view_notify_status_message (book_view, "Searching...");
	query = e_data_book_view_get_card_query (book_view);
						
	switch (priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		if (!priv->marked_for_offline) {
			e_data_book_view_notify_complete (book_view, 
					GNOME_Evolution_Addressbook_OfflineUnavailable);
			bonobo_object_unref (book_view);
			return;
		}
		if (!priv->cache) {
			printf("The cache is not yet built\n");
			e_data_book_view_notify_complete (book_view, 
					GNOME_Evolution_Addressbook_Success);
			return;
		}

		if (priv->is_summary_ready && 
	    	    e_book_backend_summary_is_summary_query (priv->summary, query)) {
			if (enable_debug)
				printf ("reading the contacts from summary \n");
			ids = e_book_backend_summary_search (priv->summary, query);
			if (ids && ids->len > 0) {
				get_contacts_from_cache (backend, query, ids, book_view, closure);
				g_ptr_array_free (ids, TRUE);
			}
			bonobo_object_unref (book_view);
			return;
		}

		/* fall back to cache */
		if (enable_debug)
			printf ("summary not found or a summary query  reading the contacts from cache %s\n", query);
		
		contacts = e_book_backend_cache_get_contacts (priv->cache, 
							      query);
		temp_list = contacts;
		for (; contacts != NULL; contacts = g_list_next(contacts)) {
			if (!e_flag_is_set (closure->running)) {
				for (;contacts != NULL; contacts = g_list_next (contacts))
					g_object_unref (contacts->data);
				break;
			}			
			e_data_book_view_notify_update (book_view, 
							E_CONTACT(contacts->data));
			g_object_unref (contacts->data);
		}
		if (e_flag_is_set (closure->running))
			e_data_book_view_notify_complete (book_view, 
							  GNOME_Evolution_Addressbook_Success);
		if (temp_list)
			 g_list_free (temp_list);
		bonobo_object_unref (book_view);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		if (!exchange_mapi_connection_exists ()) {
			e_book_backend_notify_auth_required (backend);
			e_data_book_view_notify_complete (book_view,
						GNOME_Evolution_Addressbook_AuthenticationRequired);
			bonobo_object_unref (book_view);
			return;
		}
		

		if (priv->marked_for_offline && priv->cache && priv->is_cache_ready) {
			if (priv->is_summary_ready && 
			    e_book_backend_summary_is_summary_query (priv->summary, query)) {
				if (enable_debug)
					printf ("reading the contacts from summary \n");
				ids = e_book_backend_summary_search (priv->summary, query);
				if (ids && ids->len > 0) {
					get_contacts_from_cache (backend, query, ids, book_view, closure);
					g_ptr_array_free (ids, TRUE);
				}
				bonobo_object_unref (book_view);
				return;
			}
			
			printf("Summary seems to be not there or not a summary query, lets fetch from cache directly\n");
			
			/* We are already cached. Lets return from there. */
			contacts = e_book_backend_cache_get_contacts (priv->cache, 
								      query);
			temp_list = contacts;
			for (; contacts != NULL; contacts = g_list_next(contacts)) {
				if (!e_flag_is_set (closure->running)) {
					for (;contacts != NULL; contacts = g_list_next (contacts))
						g_object_unref (contacts->data);
					break;
				}							
				e_data_book_view_notify_update (book_view, 
								E_CONTACT(contacts->data));
				g_object_unref (contacts->data);
			}
			if (e_flag_is_set (closure->running))
				e_data_book_view_notify_complete (book_view, 
								  GNOME_Evolution_Addressbook_Success);
			if (temp_list)
				 g_list_free (temp_list);
			bonobo_object_unref (book_view);
			return;
		}

		//FIXME: We need to fetch only the query from the server live and not everything.
		/* execute the query */
		if (!exchange_mapi_connection_fetch_items (olFolderContacts, NULL, create_contact_cb, priv->fid, book_view)) {
			if (e_flag_is_set (closure->running))
				e_data_book_view_notify_complete (book_view, 
								  GNOME_Evolution_Addressbook_OtherError);	
			bonobo_object_unref (book_view);
			return;
		}

		if (e_flag_is_set (closure->running))
			e_data_book_view_notify_complete (book_view,
							  GNOME_Evolution_Addressbook_Success);
		bonobo_object_unref (book_view);

		

	default:
		break;
	}

	return;
	

}

static void
e_book_backend_mapi_start_book_view (EBookBackend  *backend,
					   EDataBookView *book_view)
{
	BESearchClosure *closure = init_closure (book_view, backend);

	if (enable_debug)
		printf ("mapi: start_book_view...\n");
	closure->thread = g_thread_create (book_view_thread, book_view, FALSE, NULL);
	e_flag_wait (closure->running);
	
	/* at this point we know the book view thread is actually running */	
}

static void
e_book_backend_mapi_stop_book_view (EBookBackend  *backend,
					  EDataBookView *book_view)
{
	if(enable_debug)
		printf("mapi: stop book view\n");	
	/* FIXME : provide implmentation */
}

static void
e_book_backend_mapi_get_changes (EBookBackend *backend,
				       EDataBook    *book,
				       guint32       opid,
				       const char *change_id  )
{
	if(enable_debug)
		printf("mapi: get changes\n");	
	/* FIXME : provide implmentation */
}


static gboolean
update_cache (EBookBackendMAPI *ebmapi)
{
	//FIXME: Implement this once libmapi has notification/mod-time based restrictions.
}

static gboolean 
cache_contact_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, GSList *recipients, GSList *attachments, gpointer data)
{
	EBookBackendMAPI *be = data;
	EContact *contact;
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) be)->priv;
	char *suid;

	contact = emapidump_contact (array);
	suid = exchange_mapi_util_mapi_ids_to_uid (fid, mid);
	
	if (contact) {
		/* UID of the contact is nothing but the concatenated string of hex id of folder and the message.*/
		e_contact_set (contact, E_CONTACT_UID, suid);		
		e_contact_set (contact, E_CONTACT_BOOK_URI, priv->uri);
		e_book_backend_cache_add_contact (priv->cache, contact);
		e_book_backend_summary_add_contact (priv->summary, contact);		
		g_object_unref(contact);
	}

	g_free (suid);
	return TRUE;	
}

static gpointer
build_cache (EBookBackendMAPI *ebmapi)
{
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) ebmapi)->priv;
	
	//FIXME: What if book view is NULL? Can it be? Check that.
	if (!priv->cache) {
		printf("Caching for the first time\n");
		priv->cache = e_book_backend_cache_new (priv->uri);
	}

	if (!priv->summary) {
		priv->summary = e_book_backend_summary_new (priv->summary_file_name, 
							    SUMMARY_FLUSH_TIMEOUT);
		printf("Summary file name is %s\n", priv->summary_file_name);
	}
	
	e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache));
	
	if (!exchange_mapi_connection_fetch_items (olFolderContacts, NULL, cache_contact_cb, priv->fid, ebmapi)) {
		printf("Error during caching addressbook\n");
		e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));
		return NULL;
	}
	e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));
	e_book_backend_summary_save (priv->summary);
	priv->is_cache_ready = TRUE;
	priv->is_summary_ready = TRUE;
	return NULL;		
}

static void
e_book_backend_mapi_authenticate_user (EBookBackend *backend,
					    EDataBook    *book,
					    guint32       opid,
					    const char *user,
					    const char *passwd,
					    const char *auth_method)
{
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;
	
	if (enable_debug) {
		printf ("mapi: authenticate user\n");
	}	

	
	switch (priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE); 
		e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_Success); 
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		
		if (!exchange_mapi_connection_new (priv->profile, NULL))
			return e_data_book_respond_authenticate_user (book, opid,GNOME_Evolution_Addressbook_OtherError);

		if (priv->cache && priv->is_cache_ready) {
			printf("FIXME: Should check for an update in the cache\n");
/*			if (priv->is_writable)
				g_thread_create ((GThreadFunc) update_cache, 
						  backend, FALSE, NULL);*/
		}
		else if (priv->marked_for_offline && !priv->is_cache_ready){
			/* Means we dont have a cache. Lets build that first */
			printf("Preparing to build cache\n");
			g_thread_create ((GThreadFunc) build_cache, backend, FALSE, backend);
		}
		e_book_backend_set_is_writable (backend, TRUE);
		e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_Success);
		return;
		
	default :
		break;
	}	
}

static void
e_book_backend_mapi_get_required_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;

	if (enable_debug)
		printf ("mapi get_required_fields...\n");
  
	fields = g_list_append (fields, (char *)e_contact_field_name (E_CONTACT_FILE_AS));
	e_data_book_respond_get_supported_fields (book, opid,
						  GNOME_Evolution_Addressbook_Success,
						  fields);
	g_list_free (fields);	
}

static void
e_book_backend_mapi_get_supported_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;
	int i;

	if (enable_debug)
		printf ("mapi get_supported_fields...\n");

	for (i=0; i<maplen; i++)
	{
		fields = g_list_append (fields, (char *)e_contact_field_name (mappings[i].field_id));
	}
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_BOOK_URI)));

	e_data_book_respond_get_supported_fields (book, opid,
						  GNOME_Evolution_Addressbook_Success,
						  fields);
	g_list_free (fields);
	
}

static void 
e_book_backend_mapi_get_supported_auth_methods (EBookBackend *backend, EDataBook *book, guint32 opid)
{
	GList *auth_methods = NULL;
	char *auth_method;
	
	if (enable_debug)
		printf ("mapi get_supported_auth_methods...\n");

	auth_method =  g_strdup_printf ("plain/password");
	auth_methods = g_list_append (auth_methods, auth_method);
	e_data_book_respond_get_supported_auth_methods (book,
							opid,
							GNOME_Evolution_Addressbook_Success,
							auth_methods);  
	g_free (auth_method);
	g_list_free (auth_methods);	
}


static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_mapi_cancel_operation (EBookBackend *backend, EDataBook *book)
{
	if (enable_debug)
		printf ("mapi cancel_operation...\n");
	return GNOME_Evolution_Addressbook_CouldNotCancel;	
}


static void
e_book_backend_mapi_remove (EBookBackend *backend,
				  EDataBook    *book,
				  guint32      opid)
{
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;
	char *cache_uri = NULL;
	gboolean status;

	if(enable_debug)
		printf("mapi: remove\n");
	
	switch (priv->mode) {
	
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		e_data_book_respond_remove (book, opid, GNOME_Evolution_Addressbook_OfflineUnavailable);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:

		status = exchange_mapi_remove_folder (olFolderContacts, priv->fid);
		if (!status) {
			e_data_book_respond_remove (book, opid, GNOME_Evolution_Addressbook_OtherError);
			return;			
		}
		
		if (priv->marked_for_offline && priv->is_summary_ready) {
			g_object_unref (priv->summary);
			priv->summary = NULL;
		}

		if (e_book_backend_cache_exists (priv->uri)) {

			g_object_unref (priv->cache);
			priv->cache= NULL;
			
		}

		/* Remove the summary and cache independent of whether they are loaded or not. */		
		cache_uri = get_filename_from_uri (priv->uri, "cache.summary");
		if (g_file_test (cache_uri, G_FILE_TEST_EXISTS)) {
			g_unlink (cache_uri);
		}
		g_free (cache_uri);
		
		cache_uri = get_filename_from_uri (priv->uri, "cache.xml");
		if (g_file_test (cache_uri, G_FILE_TEST_EXISTS)) {
			g_unlink (cache_uri);
		}
		g_free (cache_uri);
				
		e_data_book_respond_remove (book, opid, GNOME_Evolution_Addressbook_Success);
		return;


	default:
		break;
	}

	return;
	
	/* FIXME : provide implmentation */
}

static void 
e_book_backend_mapi_set_mode (EBookBackend *backend, int mode)
{
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;

	if(enable_debug)
		printf("mapi: set_mode \n");
	
	priv->mode = mode;
	if (e_book_backend_is_loaded (backend)) {
		if (mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
			e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, FALSE);
			/* FIXME: Uninitialize mapi here. may be.*/
		}
		else if (mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
			e_book_backend_notify_writable (backend, TRUE);
			e_book_backend_notify_connection_status (backend, TRUE);
			e_book_backend_notify_auth_required (backend); //FIXME: WTH is this required.
		}
	}	
}

static void
e_book_backend_mapi_dispose (GObject *object)
{
	/* FIXME : provide implmentation */
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) object)->priv;
	
	if (priv->profile) {
		g_free (priv->profile);
		priv->profile = NULL;
	}
	if (priv->uri) {
		g_free (priv->uri);
		priv->uri = NULL;
	}
	
}



static void e_book_backend_mapi_class_init (EBookBackendMAPIClass *klass)
{
	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *parent_class;
	
	
	e_book_backend_mapi_parent_class = g_type_class_peek_parent (klass);
	
	parent_class = E_BOOK_BACKEND_CLASS (klass);
	
	/* Set the virtual methods. */
	parent_class->load_source		   = e_book_backend_mapi_load_source;
	parent_class->get_static_capabilities    = e_book_backend_mapi_get_static_capabilities;
	parent_class->create_contact             = e_book_backend_mapi_create_contact;
	parent_class->remove_contacts            = e_book_backend_mapi_remove_contacts;
	parent_class->modify_contact             = e_book_backend_mapi_modify_contact;
	parent_class->get_contact                = e_book_backend_mapi_get_contact;
	parent_class->get_contact_list           = e_book_backend_mapi_get_contact_list;
	parent_class->start_book_view            = e_book_backend_mapi_start_book_view;
	parent_class->stop_book_view             = e_book_backend_mapi_stop_book_view;
	parent_class->get_changes                = e_book_backend_mapi_get_changes;
	parent_class->authenticate_user          = e_book_backend_mapi_authenticate_user;
	parent_class->get_required_fields        = e_book_backend_mapi_get_required_fields;
	parent_class->get_supported_fields       = e_book_backend_mapi_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_mapi_get_supported_auth_methods;
	parent_class->cancel_operation           = e_book_backend_mapi_cancel_operation;
	parent_class->remove                     = e_book_backend_mapi_remove;
	parent_class->set_mode                   = e_book_backend_mapi_set_mode;
	object_class->dispose                    = e_book_backend_mapi_dispose;
	
}

EBookBackend *e_book_backend_mapi_new (void)
{
	EBookBackendMAPI *backend;
	
	
	backend = g_object_new (E_TYPE_BOOK_BACKEND_MAPI, NULL);
	return E_BOOK_BACKEND (backend);
}


static void	e_book_backend_mapi_init (EBookBackendMAPI *backend)
{
	EBookBackendMAPIPrivate *priv;
  
	priv= g_new0 (EBookBackendMAPIPrivate, 1);
	/* Priv Struct init */
	backend->priv = priv;

	priv->marked_for_offline = FALSE;
	priv->uri = NULL;
	priv->cache = NULL;
	priv->is_summary_ready = FALSE;
	priv->is_cache_ready = FALSE;
	
	if (g_getenv ("MAPI_DEBUG"))
		enable_debug = TRUE;
	else
		enable_debug = FALSE;
	
	
}


GType	e_book_backend_mapi_get_type (void)
{
	static GType type = 0;
	
	if (! type) {
		GTypeInfo info = {
			sizeof (EBookBackendMAPIClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_mapi_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackendMAPI),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_mapi_init
		};
		
		type = g_type_register_static (E_TYPE_BOOK_BACKEND, "EBookBackendMAPI", &info, 0);
	}
	
	return type;
}
