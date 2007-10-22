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
#define SUMMARY_FLUSH_TIMEOUT 5000

static EContact * emapidump_contact(struct mapi_SPropValue_array *properties);
	
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
	char *email=NULL;
	int status;
	

	/* This currently supports "is email foo@bar.soo" */
	status = build_query (query, &email);

	if (status != GNOME_Evolution_Addressbook_Success || email==NULL)
		return FALSE;

	res->rt = RES_PROPERTY;
	res->res.resProperty.relop = RES_PROPERTY;
	res->res.resProperty.ulPropTag = 0x8084001e; /* EMAIL */
	res->res.resProperty.lpProp.ulPropTag = 0x8084001e; /* EMAIL*/
	res->res.resProperty.lpProp.value.lpszA = email;

	return TRUE;
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

	if (priv->mode ==  GNOME_Evolution_Addressbook_MODE_LOCAL &&
	    !priv->marked_for_offline ) {
		return GNOME_Evolution_Addressbook_OfflineUnavailable;
	}

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

	if (priv->marked_for_offline) {
		printf("Loading the summary\n");
 		priv->summary_file_name = g_build_filename (g_get_home_dir(), ".evolution/cache/addressbook" , uri, priv->book_name, "cache.summary", NULL);
 		priv->summary = e_book_backend_summary_new (priv->summary_file_name, 
 							    SUMMARY_FLUSH_TIMEOUT);
 		e_book_backend_summary_load (priv->summary);

		/* Load the cache as well.*/
		if (e_book_backend_cache_exists (priv->uri))
		    priv->cache = e_book_backend_cache_new (priv->uri);
		e_book_backend_summary_load (priv->summary);
		//FIXME: We may have to do a time based reload. Or deltas should upload.
	}

	g_free (uri);
	
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
	tmp = e_source_get_property (source, "folder-id");
	sscanf (tmp, "%llx", &priv->fid);

	printf("Folder is %s %016llx\n", tmp, priv->fid);

	/* Once aunthentication in address book works this can be removed */
	if (priv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		return GNOME_Evolution_Addressbook_Success;
	}

	// writable property will be set in authenticate_user callback
	e_book_backend_set_is_writable (E_BOOK_BACKEND(backend), FALSE);
	e_book_backend_set_is_loaded (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_notify_connection_status (E_BOOK_BACKEND (backend), TRUE);


	if (enable_debug)
		printf("For profile %s and folder %s - %llx\n", priv->profile, tmp, priv->fid);

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

static void
e_book_backend_mapi_create_contact (EBookBackend *backend,
					  EDataBook *book,
					  guint32 opid,
					  const char *vcard )
{
	/* FIXME : provide implmentation */
}

static void
e_book_backend_mapi_remove_contacts (EBookBackend *backend,
					   EDataBook    *book,
					   guint32 opid,
					   GList *id_list)
{
	/* FIXME : provide implmentation */
}

static void
e_book_backend_mapi_modify_contact (EBookBackend *backend,
					  EDataBook    *book,
					  guint32       opid,
					  const char   *vcard)
{	
	/* FIXME : provide implmentation */
}

static gpointer
create_contact_item (struct mapi_SPropValue_array *array, mapi_id_t fid, mapi_id_t mid)
{
	EContact *contact;
	char *suid;
	
	contact = emapidump_contact (array);
	suid = g_strdup_printf ("%016llx%016llx", fid, mid);
	
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
			
			sscanf(id, "%llx%llx", &fid, &mid);
			printf("FID %016llx mid %016llx\n", fid, mid);
			contact = exchange_mapi_connection_fetch_item (olFolderContacts, fid, mid, create_contact_item);
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
create_contact_list_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, gpointer data)
{
	GList *list = * (GList **) data;
	EContact *contact;
	char *suid;
	
	contact = emapidump_contact (array);
	suid = g_strdup_printf ("%016llx%016llx", fid, mid);
	
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
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_Success, vcard_strings);
			return ;
		}
		else {
			struct mapi_SRestriction res;
			GList *vcard_str = NULL;
			if (!build_restriction_emails_contains (&res, query)) {
				e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
				return ;				
			}
			if (!exchange_mapi_connection_fetch_items (olFolderContacts, &res, create_contact_list_cb, priv->fid, &vcard_str)) {
				e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
				return ;
			}
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

//FIXME: Be more clever in dumping contacts. Can we have a callback mechanism for each types?
static EContact *
emapidump_contact(struct mapi_SPropValue_array *properties)
{
	EContact *contact = e_contact_new ();
	const char	*card_name =NULL;
	const char	*topic =NULL;
	const char	*full_name = NULL;
	const char	*given_name = NULL;
	const char	*surname = NULL;
	const char	*company = NULL;
	const char	*email = NULL;
	const char	*title = NULL;
	const char      *office_phone = NULL;
	const char      *home_phone = NULL;
	const char      *mobile_phone = NULL;
	const char      *postal_address = NULL;
	const char      *street_address = NULL;
	const char      *locality = NULL;
	const char      *state = NULL;
	const char      *country = NULL;
	const char      *department = NULL;
	const char      *business_fax = NULL;
	const char      *business_home_page = NULL;

	card_name = (const char *)find_mapi_SPropValue_data(properties, 0x8005001e);
	topic = (const char *)find_mapi_SPropValue_data(properties, PR_CONVERSATION_TOPIC);
	company = (const char *)find_mapi_SPropValue_data(properties, PR_COMPANY_NAME);
	title = (const char *)find_mapi_SPropValue_data(properties, PR_TITLE);
	full_name = (const char *)find_mapi_SPropValue_data(properties, PR_DISPLAY_NAME);
	given_name = (const char *)find_mapi_SPropValue_data(properties, PR_GIVEN_NAME);
	surname = (const char *)find_mapi_SPropValue_data(properties, PR_SURNAME);
	department = (const char *)find_mapi_SPropValue_data(properties, PR_DEPARTMENT_NAME);
	email = (const char *)find_mapi_SPropValue_data(properties, 0x8084001e);
	office_phone = (const char *)find_mapi_SPropValue_data(properties, PR_OFFICE_TELEPHONE_NUMBER);
	home_phone = (const char *)find_mapi_SPropValue_data(properties, PR_HOME_TELEPHONE_NUMBER);
	mobile_phone = (const char *)find_mapi_SPropValue_data(properties, PR_MOBILE_TELEPHONE_NUMBER);
	business_fax = (const char *)find_mapi_SPropValue_data(properties, PR_BUSINESS_FAX_NUMBER);
	business_home_page = (const char *)find_mapi_SPropValue_data(properties, PR_BUSINESS_HOME_PAGE);
	postal_address = (const char*)find_mapi_SPropValue_data(properties, PR_POSTAL_ADDRESS);
	street_address = (const char*)find_mapi_SPropValue_data(properties, PR_STREET_ADDRESS);
	locality = (const char*)find_mapi_SPropValue_data(properties, PR_LOCALITY);
	state = (const char*)find_mapi_SPropValue_data(properties, PR_STATE_OR_PROVINCE);
	country = (const char*)find_mapi_SPropValue_data(properties, PR_COUNTRY);


	if (card_name) 
		printf("|== %s ==|\n", card_name );
	else if (topic)
		printf("|== %s ==|\n", topic );
	else 
		printf("|== <Unknown> ==|\n");
	fflush(0);
	if (topic) printf("Topic: %s\n", topic);
	fflush(0);
	if (full_name)
		printf("Full Name: %s\n", full_name);
	else if (given_name && surname)
		printf("Full Name: %s %s\n", given_name, surname); // initials? l10n?
	fflush(0);
	if (title) printf("Job Title: %s\n", title);
	fflush(0);
	if (department) printf("Department: %s\n", department);
	fflush(0);
	if (company) printf("Company: %s\n", company);
	fflush(0);
	if (email) printf("E-mail: %s\n", email);
	fflush(0);
	if (office_phone) printf("Office phone number: %s\n", office_phone);
	fflush(0);
	if (home_phone) printf("Work phone number: %s\n", home_phone);
	fflush(0);
	if (mobile_phone) printf("Mobile phone number: %s\n", mobile_phone);
	fflush(0);
	if (business_fax) printf("Business fax number: %s\n", business_fax);
	fflush(0);
	if (business_home_page) printf("Business home page: %s\n", business_home_page);
	fflush(0);
	if (postal_address) printf("Postal address: %s\n", postal_address);
	fflush(0);
	if (street_address) printf("Street address: %s\n", street_address);
	fflush(0);
	if (locality) printf("Locality: %s\n", locality);
	fflush(0);
	if (state) printf("State / Province: %s\n", state);
	fflush(0);
	if (country) printf("Country: %s\n", country);
	fflush(0);

	printf("\n");

	e_contact_set (contact, E_CONTACT_FULL_NAME, full_name);
	e_contact_set (contact, E_CONTACT_EMAIL_1, email);

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
create_contact_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, gpointer data)
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
	suid = g_strdup_printf ("%016llx%016llx", fid, mid);
	
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
			printf ("summary not found, reading the contacts from cache\n");
		
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
		

		if (priv->marked_for_offline && priv->cache) {
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
			
			printf("Summary seems to be not there, lets fetch from cache directly\n");
			
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
	/* FIXME : provide implmentation */
}

static void
e_book_backend_mapi_get_changes (EBookBackend *backend,
				       EDataBook    *book,
				       guint32       opid,
				       const char *change_id  )
{
	/* FIXME : provide implmentation */
}


static gboolean
update_cache (EBookBackendMAPI *ebmapi)
{
	//FIXME: Implement this once libmapi has notification/mod-time based restrictions.
}

static gboolean 
cache_contact_cb (struct mapi_SPropValue_array *array, const mapi_id_t fid, const mapi_id_t mid, gpointer data)
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
	suid = g_strdup_printf ("%016llx%016llx", fid, mid);
	
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
	EDataBookView *book_view;
	BESearchClosure *closure;
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) ebmapi)->priv;
	
	book_view = find_book_view (ebmapi);
	if (book_view) {
		closure = get_closure (book_view);
		bonobo_object_ref (book_view);
		if (closure)
			e_flag_set (closure->running);
	}
	//FIXME: What if book view is NULL? Can it be? Check that.
	
	e_file_cache_freeze_changes (E_FILE_CACHE (priv->cache));
	e_data_book_view_notify_status_message (book_view, "Caching contacts...");
	
	if (!exchange_mapi_connection_fetch_items (olFolderContacts, NULL, cache_contact_cb, priv->fid, book_view)) {
		if (e_flag_is_set (closure->running)) {
			printf("Error during caching addressbook\n");
			bonobo_object_unref (book_view);
			e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));
			return NULL;
		}
		
		if (e_flag_is_set (closure->running))
			e_data_book_view_notify_complete (book_view,
							  GNOME_Evolution_Addressbook_Success);
		bonobo_object_unref (book_view);
	}
	e_file_cache_thaw_changes (E_FILE_CACHE (priv->cache));
	e_book_backend_summary_save (priv->summary);
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

		if (priv->cache && e_book_backend_cache_is_populated (priv->cache)) {
/*			if (priv->is_writable)
				g_thread_create ((GThreadFunc) update_cache, 
						  backend, FALSE, NULL);*/
		}
		else if (priv->marked_for_offline){
			/* Means we dont have a cache. Lets build that first */
			g_thread_create ((GThreadFunc) build_cache, backend, FALSE, NULL);
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

	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_UID)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_FILE_AS)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_FULL_NAME)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_HOMEPAGE_URL)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_NOTE)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_PHONE_HOME)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_PHONE_BUSINESS)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_PHONE_MOBILE)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ORG)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ORG_UNIT)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_TITLE)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_HOME)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_AIM)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_CATEGORIES)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_1)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_REV)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_BOOK_URI)));
	//fields = g_list_append (fields, g_strdup (e_contact_field_name ()));

	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_WORK)));
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
	
	/* FIXME : provide implmentation */
}

static void 
e_book_backend_mapi_set_mode (EBookBackend *backend, int mode)
{
	EBookBackendMAPIPrivate *priv = ((EBookBackendMAPI *) backend)->priv;

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
