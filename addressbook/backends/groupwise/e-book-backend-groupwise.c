/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/* e-book-backend-groupwise.c - Groupwise contact backend.
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Authors: Sivaiah Nallagatla <snallagatla@novell.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif



#include <libebook/e-contact.h>
#include <glib/gi18n-lib.h>
#include <libedataserver/e-sexp.h>
#include <libedataserver/e-util.h>
#include <libedataserver/e-url.h> 
#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include <libedata-book/e-book-backend-cache.h>
#include "e-book-backend-groupwise.h"
#include <e-gw-connection.h>
#include <e-gw-item.h>
#include <e-gw-filter.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static EBookBackendClass *e_book_backend_groupwise_parent_class;
                                                                                                                             
struct _EBookBackendGroupwisePrivate {
	EGwConnection *cnc; 
	char *uri;
	char *container_id;
	char *book_name;
	char *original_uri;
	gboolean only_if_exists;
	GHashTable *categories_by_id;
	GHashTable *categories_by_name;
	gboolean is_writable;
	gboolean is_cache_ready;
	gboolean marked_for_offline;
	char *use_ssl;
	int mode;
	EBookBackendCache *cache;
};

#define ELEMENT_TYPE_SIMPLE 0x01
#define ELEMENT_TYPE_COMPLEX 0x02 /* fields which require explicit functions to set values into EContact and EGwItem */

static void populate_emails (EContact *contact, gpointer data);
static void set_emails_in_gw_item (EGwItem *item, gpointer data);
static void set_emails_changes (EGwItem *new_item, EGwItem *old_item);
static void populate_full_name (EContact *contact, gpointer data);
static void set_full_name_in_gw_item (EGwItem *item, gpointer data);
static void set_full_name_changes (EGwItem *new_item, EGwItem *old_item);
static void populate_contact_members (EContact *contact, gpointer data);
static void set_categories_changes (EGwItem *new_item, EGwItem *old_item);
static void populate_birth_date (EContact *contact, gpointer data);
static void set_birth_date_in_gw_item (EGwItem *item, gpointer data);
static void set_birth_date_changes  (EGwItem *new_item, EGwItem *old_item);
static void populate_address (EContact *contact, gpointer data);
static void set_address_in_gw_item (EGwItem *item, gpointer data);
static void set_address_changes (EGwItem *new_item, EGwItem *old_item);
static void populate_ims (EContact *contact, gpointer data);
static void set_ims_in_gw_item (EGwItem *item, gpointer data);
static void set_im_changes (EGwItem *new_item, EGwItem *old_item);
static void fill_contact_from_gw_item (EContact *contact, EGwItem *item, GHashTable *categories_by_ids);

struct field_element_mapping {
	EContactField field_id;
  	int element_type;
	char *element_name;
	void (*populate_contact_func)(EContact *contact,    gpointer data);
	void (*set_value_in_gw_item) (EGwItem *item, gpointer data);
	void (*set_changes) (EGwItem *new_item, EGwItem *old_item);
 
} mappings [] = { 
  
	{ E_CONTACT_UID, ELEMENT_TYPE_SIMPLE, "id"},
	{ E_CONTACT_FILE_AS, ELEMENT_TYPE_SIMPLE, "name" },
	{ E_CONTACT_FULL_NAME, ELEMENT_TYPE_COMPLEX, "full_name", populate_full_name, set_full_name_in_gw_item, set_full_name_changes},
	{ E_CONTACT_BIRTH_DATE, ELEMENT_TYPE_COMPLEX, "birthday", populate_birth_date, set_birth_date_in_gw_item, set_birth_date_changes },
	{ E_CONTACT_HOMEPAGE_URL, ELEMENT_TYPE_SIMPLE, "website"},
	{ E_CONTACT_NOTE, ELEMENT_TYPE_SIMPLE, "comment"},
	{ E_CONTACT_PHONE_PRIMARY, ELEMENT_TYPE_SIMPLE , "default_phone"},
	{ E_CONTACT_PHONE_BUSINESS, ELEMENT_TYPE_SIMPLE, "phone_Office"},
	{ E_CONTACT_PHONE_HOME, ELEMENT_TYPE_SIMPLE, "phone_Home"},
	{ E_CONTACT_PHONE_MOBILE, ELEMENT_TYPE_SIMPLE, "phone_Mobile"},
	{ E_CONTACT_PHONE_BUSINESS_FAX, ELEMENT_TYPE_SIMPLE, "phone_Fax" },
	{ E_CONTACT_PHONE_PAGER, ELEMENT_TYPE_SIMPLE, "phone_Pager"},
	{ E_CONTACT_ORG, ELEMENT_TYPE_SIMPLE, "organization"},
	{ E_CONTACT_ORG_UNIT, ELEMENT_TYPE_SIMPLE, "department"},
	{ E_CONTACT_TITLE, ELEMENT_TYPE_SIMPLE, "title"},
	{ E_CONTACT_EMAIL, ELEMENT_TYPE_COMPLEX, "members", populate_contact_members, NULL, NULL},
	{ E_CONTACT_ADDRESS_HOME, ELEMENT_TYPE_COMPLEX, "Home", populate_address, set_address_in_gw_item, set_address_changes },
	{ E_CONTACT_IM_AIM, ELEMENT_TYPE_COMPLEX, "ims", populate_ims, set_ims_in_gw_item, set_im_changes },
	{ E_CONTACT_CATEGORIES, ELEMENT_TYPE_COMPLEX, "categories", NULL, NULL, set_categories_changes},
	{ E_CONTACT_EMAIL_1, ELEMENT_TYPE_COMPLEX, "email", populate_emails, set_emails_in_gw_item, set_emails_changes }
}; 


static int num_mappings = sizeof(mappings) / sizeof(mappings [0]);

static void
free_attr_list (GList *attr_list)
{
        GList *l;
                                                                                                                             
        for (l = attr_list; l; l = g_list_next (l)) {
                EVCardAttribute *attr = l->data;
                e_vcard_attribute_free (attr);
        }
                                                                                                                             
        g_list_free (attr_list);
}


static void 
populate_ims (EContact *contact, gpointer data)
{
	GList *im_list;
	GList *aim_list = NULL;
	GList *icq_list = NULL;
	GList *yahoo_list = NULL;
	GList *msn_list = NULL;
	GList *jabber_list = NULL;
	GList *groupwise_list = NULL;
	IMAddress *address;
	EGwItem *item;
  
	item = E_GW_ITEM (data);
	im_list = e_gw_item_get_im_list (item);

	for (; im_list != NULL; im_list = g_list_next (im_list)) {
		EVCardAttribute *attr;
		GList **im_attr_list = NULL;
		int im_field_id = -1;

		address = (IMAddress *) (im_list->data);
		if (address->service == NULL) {
			continue;
		}
		
		if (g_str_equal (address->service, "icq")) {
			im_field_id = E_CONTACT_IM_ICQ;
			im_attr_list = &icq_list;
		}
		else if (g_str_equal (address->service, "aim")) {
			im_field_id = E_CONTACT_IM_AIM;
			im_attr_list = &aim_list;
		}
		else if ( g_str_equal (address->service, "msn")) {
			im_field_id = E_CONTACT_IM_MSN;
			im_attr_list = &msn_list;
		}
		else if (g_str_equal (address->service, "yahoo")) {
			im_field_id = E_CONTACT_IM_YAHOO;
			im_attr_list = &yahoo_list;
		}
		else if (g_str_equal (address->service, "jabber")) {
			im_field_id = E_CONTACT_IM_JABBER;
			im_attr_list = &jabber_list;
		}
			
		else if (g_str_equal (address->service, "nov")) {
			im_field_id = E_CONTACT_IM_GROUPWISE;
			im_attr_list = &groupwise_list;
		}
		if (im_field_id == -1)
			continue;

		attr = e_vcard_attribute_new ("", e_contact_vcard_attribute(im_field_id));
		e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE), "WORK");
		e_vcard_attribute_add_value (attr, address->address);
		*im_attr_list = g_list_append (*im_attr_list, attr);
	}
	
	e_contact_set_attributes (contact, E_CONTACT_IM_AIM, aim_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_JABBER, jabber_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_ICQ, icq_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_YAHOO, yahoo_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_MSN, msn_list);
	e_contact_set_attributes (contact, E_CONTACT_IM_GROUPWISE, groupwise_list);
	
	free_attr_list (aim_list);
	free_attr_list (jabber_list);
	free_attr_list (icq_list);
	free_attr_list (yahoo_list);
	free_attr_list (msn_list);
	free_attr_list (groupwise_list);
}


static void
append_ims_to_list (GList **im_list, EContact *contact,  char *service_name, EContactField field_id)
{
	GList *list;
	IMAddress *address;
	list = e_contact_get (contact, field_id);
	for (; list != NULL; list =  g_list_next (list)) {
		address = g_new0 (IMAddress , 1);
		address->service = g_strdup (service_name);
		address->address = list->data;
		*im_list = g_list_append (*im_list, address);
	}
	g_list_free (list);
	
}


static void 
set_ims_in_gw_item (EGwItem *item, gpointer data)
{
	EContact *contact;
	GList *im_list = NULL;
  
	contact = E_CONTACT (data);
  
	append_ims_to_list (&im_list, contact, "aim", E_CONTACT_IM_AIM);
	append_ims_to_list (&im_list, contact, "yahoo", E_CONTACT_IM_YAHOO);
	append_ims_to_list (&im_list, contact, "icq", E_CONTACT_IM_ICQ);
	append_ims_to_list (&im_list, contact, "msn", E_CONTACT_IM_MSN);
	append_ims_to_list (&im_list, contact, "jabber", E_CONTACT_IM_JABBER);
	append_ims_to_list (&im_list, contact, "nov", E_CONTACT_IM_GROUPWISE);
	if (im_list)
		e_gw_item_set_im_list (item, im_list);
}


static void
set_im_changes (EGwItem *new_item, EGwItem *old_item)
{
	GList *old_ims;
	GList *new_ims;
	GList *added_ims = NULL;
	GList *old_ims_copy;
	GList *temp;
	gboolean ims_matched;
	IMAddress *im1, *im2;

	old_ims = e_gw_item_get_im_list (old_item);
	new_ims = e_gw_item_get_im_list (new_item);

	if (old_ims && new_ims) {
	
		old_ims_copy = g_list_copy (old_ims);
		for ( ; new_ims != NULL; new_ims = g_list_next (new_ims)) {
			
			im1 = new_ims->data;
			temp = old_ims;
			ims_matched = FALSE;
			for(; temp != NULL; temp = g_list_next (temp)) {
				im2 = temp->data;
				if (g_str_equal (im1->service, im2->service) && g_str_equal (im1->address, im2->address)) {
					ims_matched = TRUE;
					old_ims_copy = g_list_remove (old_ims_copy, im2);
					break;
				}
				
			}
			if (! ims_matched)
				added_ims = g_list_append (added_ims, im1);
		}
			     
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "ims", added_ims);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "ims", old_ims_copy);

	} else if (!new_ims && old_ims) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "ims", g_list_copy (old_ims));
	} else if (new_ims && !old_ims) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "ims", g_list_copy (new_ims));
	}
	
}


static void 
copy_postal_address_to_contact_address ( EContactAddress *contact_addr, PostalAddress *address)
{
	contact_addr->address_format = NULL;
	contact_addr->po = NULL;
	contact_addr->street = g_strdup (address->street_address);
	contact_addr->ext = g_strdup (address->location);
	contact_addr->locality = g_strdup (address->city);
	contact_addr->region = g_strdup (address->state);
	contact_addr->code = g_strdup (address->postal_code);
	contact_addr->country = g_strdup (address->country);
}

static void 
copy_contact_address_to_postal_address (PostalAddress *address, EContactAddress *contact_addr)
{
	/* ugh, contact addr has null terminated strings instead of NULLs*/
	address->street_address = (contact_addr->street && *contact_addr->street) ? g_strdup (contact_addr->street): NULL;
	address->location = (contact_addr->ext && *contact_addr->ext) ? g_strdup (contact_addr->ext) : NULL;
	address->city = (contact_addr->locality && *contact_addr->locality) ? g_strdup (contact_addr->locality) : NULL;
	address->state = (contact_addr->region && *contact_addr->region) ?  g_strdup (contact_addr->region) : NULL;
	address->postal_code = (contact_addr->code && *contact_addr->code ) ? g_strdup (contact_addr->code) : NULL;
	address->country = (contact_addr->country && *(contact_addr->country)) ? g_strdup (contact_addr->country) : NULL;

	
}

static void 
populate_address (EContact *contact, gpointer data)
{
	PostalAddress *address;
	EGwItem *item;
	EContactAddress *contact_addr;
	
	item = E_GW_ITEM (data);
	
	address = e_gw_item_get_address (item, "Home");
	contact_addr = NULL;

	if (address) {
		contact_addr = g_new0(EContactAddress, 1);
		copy_postal_address_to_contact_address (contact_addr, address);
		e_contact_set (contact, E_CONTACT_ADDRESS_HOME, contact_addr);
		e_contact_address_free (contact_addr);
	}
  
	address = e_gw_item_get_address (item, "Office");
	if (address) {
		contact_addr = g_new0(EContactAddress, 1);
		copy_postal_address_to_contact_address (contact_addr, address);
		e_contact_set (contact, E_CONTACT_ADDRESS_WORK, contact_addr);
		e_contact_address_free (contact_addr);
	}

}



static void 
set_address_in_gw_item (EGwItem *item, gpointer data)
{
	EContact *contact;
	EContactAddress *contact_address;
	PostalAddress *address;

	contact = E_CONTACT (data);
	
	contact_address = e_contact_get (contact, E_CONTACT_ADDRESS_HOME);
	if (contact_address) {
		address = g_new0(PostalAddress, 1);
		copy_contact_address_to_postal_address (address, contact_address);
		e_gw_item_set_address (item, "Home", address);
		e_contact_address_free (contact_address);
	}
		
	contact_address = e_contact_get (contact, E_CONTACT_ADDRESS_WORK);
	if (contact_address) {
		address = g_new0(PostalAddress, 1);
		copy_contact_address_to_postal_address (address, contact_address);
		e_gw_item_set_address (item, "Office", address);
		e_contact_address_free (contact_address);
	}
	

}

static PostalAddress *
copy_postal_address (PostalAddress *address)
{
	PostalAddress *address_copy;

	address_copy = g_new0(PostalAddress, 1);

	address_copy->street_address = g_strdup (address->street_address);
	address_copy->location = g_strdup (address->location);
	address_copy->city = g_strdup (address->city);
	address_copy->state = g_strdup (address->state);
	address_copy->postal_code = g_strdup (address->postal_code);
	address_copy->country = g_strdup (address->country);
	return address_copy;
}

static void 
set_postal_address_change (EGwItem *new_item, EGwItem *old_item,  char *address_type)
{
	PostalAddress *old_postal_address;
	PostalAddress *new_postal_address;
	PostalAddress *update_postal_address, *delete_postal_address;
	char *s1, *s2;
	update_postal_address = g_new0(PostalAddress, 1);
	delete_postal_address = g_new0 (PostalAddress, 1);
	
	new_postal_address = e_gw_item_get_address (new_item,  address_type);
	old_postal_address = e_gw_item_get_address (old_item, address_type);
    	if (new_postal_address && old_postal_address) {
		s1 = new_postal_address->street_address;
		s2 = old_postal_address->street_address;
		if (!s1 && s2)
			delete_postal_address->street_address = g_strdup(s2);
		else if (s1)
			update_postal_address->street_address = g_strdup(s1);
		
		s1 =  new_postal_address->location;
		s2 = old_postal_address->location;
		if (!s1 && s2)
			delete_postal_address->location = g_strdup(s2);
		else if (s1)
			update_postal_address->location = g_strdup(s1);

		s1 = new_postal_address->city;
		s2 = old_postal_address->city;
		if (!s1 && s2)
			delete_postal_address->city = g_strdup(s2);
		else if (s1)
			update_postal_address->city = g_strdup(s1);

		s1 =  new_postal_address->state;
		s2 = old_postal_address->state;
		if (!s1 && s2)
			delete_postal_address->state = g_strdup(s2);
		else if (s1)
			update_postal_address->state = g_strdup(s1);
		s1 =  new_postal_address->postal_code;
		s2 = old_postal_address->postal_code;
		if (!s1 && s2)
			delete_postal_address->postal_code = g_strdup(s2);
		else if (s1)
			update_postal_address->postal_code = g_strdup(s1);

		s1 =  new_postal_address->country;
		s2 =  old_postal_address->country;
		if (!s1 && s2)
			delete_postal_address->country = g_strdup(s2);
		else if (s1)
			update_postal_address->country = g_strdup(s1);

		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_UPDATE, address_type, update_postal_address);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, address_type, delete_postal_address);
		
	} else if (!new_postal_address && old_postal_address) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, address_type, copy_postal_address(old_postal_address));
	} else if (new_postal_address && !old_postal_address) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, address_type, copy_postal_address(new_postal_address));
	}
}

static void 
set_address_changes (EGwItem *new_item , EGwItem *old_item)
{
	set_postal_address_change (new_item, old_item, "Home");
	set_postal_address_change (new_item, old_item, "Office");
}

static void 
populate_birth_date (EContact *contact, gpointer data)
{
	EGwItem *item;
	char *value ;
	EContactDate *date;
  
	item = E_GW_ITEM (data);
	value = e_gw_item_get_field_value (item, "birthday");
 	if (value) {
		date =  e_contact_date_from_string (value);
		e_contact_set (contact, E_CONTACT_BIRTH_DATE, date);
		e_contact_date_free (date);
	}
}

static void 
set_birth_date_in_gw_item (EGwItem *item, gpointer data)
{
	EContact *contact;
	EContactDate *date;
	char *date_string;
	contact = E_CONTACT (data);
	date = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
	if (date) {
		date_string = e_contact_date_to_string (date);
		e_gw_item_set_field_value (item, "birthday", date_string);
		e_contact_date_free (date);
		g_free (date_string);
	}

}

static void 
set_birth_date_changes (EGwItem *new_item, EGwItem *old_item)
{
	char *new_birthday;
	char *old_birthday;

	new_birthday = e_gw_item_get_field_value (new_item, "birthday");
	old_birthday = e_gw_item_get_field_value (old_item, "birthday");
	
	if (new_birthday && old_birthday) {
		if (!g_str_equal (new_birthday, old_birthday))
			e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_UPDATE, "birthday", new_birthday);
	}
	else if (!new_birthday && old_birthday) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "birthday", old_birthday);
	}
	else if (new_birthday && !old_birthday) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "birthday", new_birthday);
	}
	

		
}

static int email_fields [3] = {
	E_CONTACT_EMAIL_1,
	E_CONTACT_EMAIL_2,
	E_CONTACT_EMAIL_3

};

static void 
populate_emails (EContact *contact, gpointer data)
{
	GList *email_list;
	EGwItem *item;
	int i;

	item = E_GW_ITEM (data);
	email_list = e_gw_item_get_email_list(item);

	for (i =0 ; i < 3 && email_list; i++, email_list = g_list_next (email_list)) {
		if (email_list->data) 
			e_contact_set (contact, email_fields[i], email_list->data);
	}
  
} 


static void 
set_emails_in_gw_item (EGwItem *item, gpointer data)
{
	GList *email_list;
	EContact *contact;
	char *email;
	int i;

	contact = E_CONTACT (data);
	email_list = NULL;
	for (i =0 ; i < 3; i++) {
		email = e_contact_get (contact, email_fields[i]);
		if(email)
			email_list = g_list_append (email_list, g_strdup (email));
	}
	e_gw_item_set_email_list (item, email_list);

}  

static void 
compare_string_lists ( GList *old_list, GList *new_list, GList **additions, GList **deletions)
{
	GList *temp, *old_list_copy;
	gboolean strings_matched;
	char *string1, *string2;
        
	if (old_list && new_list) {
		old_list_copy = g_list_copy (old_list);
		for ( ; new_list != NULL; new_list = g_list_next (new_list)) {
			
			string1 = new_list->data;
			temp = old_list;
			strings_matched = FALSE;
			for(; temp != NULL; temp = g_list_next (temp)) {
				string2 = temp->data;
				if ( g_str_equal (string1, string2)) {
					strings_matched = TRUE;
					old_list_copy = g_list_remove (old_list_copy, string2);
					break;
				}
				
			}
			if (!strings_matched)
				*additions = g_list_append (*additions, string1);

		}
		*deletions = old_list_copy;
	}
	else if (!new_list && old_list) 
		*deletions = g_list_copy (old_list);
	else if (new_list && !old_list)
		*additions = g_list_copy (new_list);

}
 
static void 
set_emails_changes (EGwItem *new_item, EGwItem *old_item)
{
	GList *old_email_list;
	GList *new_email_list;
	GList  *added_emails = NULL, *deleted_emails = NULL;

	old_email_list = e_gw_item_get_email_list (old_item);
	new_email_list = e_gw_item_get_email_list (new_item);
	compare_string_lists (old_email_list, new_email_list, &added_emails, &deleted_emails);
	if (added_emails)
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "email", added_emails);
	if (deleted_emails)
		e_gw_item_set_change (new_item,  E_GW_ITEM_CHANGE_TYPE_DELETE, "email", deleted_emails);

}
 
static void 
populate_full_name (EContact *contact, gpointer data)
{
	EGwItem *item;
	FullName  *full_name ;
	char *full_name_string;

	item = E_GW_ITEM(data);
	full_name = e_gw_item_get_full_name (item);
	if (full_name) {
		full_name_string = g_strconcat ( (full_name->first_name == NULL) ? "\0" :    full_name->first_name, " ",
			    (full_name->middle_name == NULL) ? "\0" : full_name->middle_name, " ",
			    full_name->last_name == NULL ? "\0" : full_name->last_name, " ",
			    (full_name->name_suffix == NULL ) ? "\0" : full_name->name_suffix, NULL);
		full_name_string = g_strstrip (full_name_string);
		if (!g_str_equal (full_name_string, "\0"))
			e_contact_set (contact, E_CONTACT_FULL_NAME, full_name_string);
		g_free (full_name_string);
	}

}

static void 
set_full_name_in_gw_item (EGwItem *item, gpointer data)
{
	EContact *contact;
	char   *name;
	EContactName *contact_name;
	FullName *full_name;

	contact = E_CONTACT (data);
  
	name = e_contact_get (contact, E_CONTACT_FULL_NAME);

	if(name) {
		contact_name = e_contact_name_from_string (name);
		full_name = g_new0 (FullName, 1);
		if (contact_name && full_name) {
			full_name->name_prefix =  g_strdup (contact_name->prefixes);
			full_name->first_name =  g_strdup (contact_name->given);
			full_name->middle_name =  g_strdup (contact_name->additional);
			full_name->last_name =  g_strdup (contact_name->family);
			full_name->name_suffix = g_strdup (contact_name->suffixes);
			e_contact_name_free (contact_name);
		}
		  
		e_gw_item_set_full_name (item, full_name);
	}
}

static FullName *
copy_full_name (FullName *full_name)
{
	FullName *full_name_copy = g_new0(FullName, 1);
	full_name_copy->name_prefix = g_strdup (full_name->name_prefix);
	full_name_copy->first_name =  g_strdup (full_name->first_name);
	full_name_copy->middle_name = g_strdup (full_name->middle_name);
	full_name_copy->last_name = g_strdup (full_name->last_name);
	full_name_copy->name_suffix = g_strdup (full_name->name_suffix);
	return full_name_copy;
}


static void 
set_full_name_changes (EGwItem *new_item, EGwItem *old_item)
{
	FullName *old_full_name;
	FullName *new_full_name;
	FullName  *update_full_name, *delete_full_name;
	char *s1, *s2;
	update_full_name = g_new0(FullName, 1);
	delete_full_name = g_new0 (FullName, 1);
	
	old_full_name = e_gw_item_get_full_name (old_item);
	new_full_name = e_gw_item_get_full_name (new_item);
       
	
	if (old_full_name && new_full_name) {
		s1 = new_full_name->name_prefix;
		s2 = old_full_name->name_prefix;
	        if(!s1 && s2)
			delete_full_name->name_prefix = g_strdup(s2);
		else if (s1)
			update_full_name->name_prefix = g_strdup(s1);
		s1 = new_full_name->first_name;
		s2  = old_full_name->first_name;
		if(!s1 && s2)
			delete_full_name->first_name = g_strdup(s2);
		else if (s1)
			update_full_name->first_name = g_strdup(s1);
		s1 = new_full_name->middle_name;
		s2  = old_full_name->middle_name;
		if(!s1 && s2)
			delete_full_name->middle_name = g_strdup(s2);
		else if (s1)
			update_full_name->middle_name = g_strdup(s1);
		
		s1 = new_full_name->last_name;
		s2 = old_full_name->last_name;
		if(!s1 && s2)
			delete_full_name->last_name = g_strdup(s2);
		else if (s1)
			update_full_name->last_name = g_strdup(s1);
		s1 = new_full_name->name_suffix;
		s2  = old_full_name->name_suffix;
		if(!s1 && s2)
			delete_full_name->name_suffix = g_strdup(s2);
		else if (s1)
			update_full_name->name_suffix = g_strdup(s1);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_UPDATE,"full_name",  update_full_name);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE,"full_name",  delete_full_name);
	
	} else if (!new_full_name && old_full_name) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "full_name", copy_full_name(old_full_name));
	} else if (new_full_name && !old_full_name) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "full_name", copy_full_name(new_full_name));
	}
		

}

static void 
populate_contact_members (EContact *contact, gpointer data)
{
	EGwItem *item;
	GList *member_list;
	item = E_GW_ITEM(data);
	member_list = e_gw_item_get_member_list (item);
	for (; member_list != NULL; member_list = g_list_next (member_list)) {
		EVCardAttribute *attr;
		EGroupMember *member;
		member = (EGroupMember *) member_list->data;

		attr = e_vcard_attribute_new (NULL, EVC_EMAIL);
		e_vcard_attribute_add_param_with_value (attr,
                                                        e_vcard_attribute_param_new (EVC_X_DEST_CONTACT_UID),
							member->id);
		e_vcard_attribute_add_param_with_value (attr,
                                                        e_vcard_attribute_param_new (EVC_X_DEST_EMAIL),
							member->email);
		if (member->name)
			e_vcard_attribute_add_param_with_value (attr,
                                                        e_vcard_attribute_param_new (EVC_X_DEST_NAME),
							member->name);
		e_vcard_attribute_add_value (attr, member->email);
		e_vcard_add_attribute (E_VCARD (contact), attr);
	}
		
}


static void
set_members_in_gw_item (EGwItem  *item, EContact *contact, EBookBackendGroupwise *egwb)

{
  	GList  *members, *temp, *items, *p, *emails_without_ids;
	GList *group_members;
	char *email;
	EGwFilter *filter;
	int status;
	char *id;
	EGwItem *temp_item;
	int count = 0;
	EGroupMember *member;

	members = e_contact_get_attributes (contact, E_CONTACT_EMAIL);
	temp = members;
	filter = e_gw_filter_new ();
	group_members = NULL;
	emails_without_ids = NULL;
	for ( ;temp != NULL; temp = g_list_next (temp)) {
		EVCardAttribute *attr = temp->data;
		id = email = NULL;
		for (p = e_vcard_attribute_get_params (attr); p; p = p->next) {
			EVCardAttributeParam *param = p->data;
			const char *param_name = e_vcard_attribute_param_get_name (param);
			if (!g_ascii_strcasecmp (param_name,
						 EVC_X_DEST_CONTACT_UID)) {
				GList *v = e_vcard_attribute_param_get_values (param);
				id = v ? v->data : NULL;
			} else if (!g_ascii_strcasecmp (param_name,
							EVC_X_DEST_EMAIL)) {
				GList *v = e_vcard_attribute_param_get_values (param);
				email = v ? v->data : NULL;
			}
		}
		if (id) {
			member = g_new0 (EGroupMember , 1);
			member->id = g_strdup (id);
			group_members = g_list_append (group_members, member);
		} else if (email) {
			e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, "emailList/@primary", email);
			emails_without_ids = g_list_append (emails_without_ids, g_strdup (email));
			count++;
		}
	       
	}
	e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, count);
	items = NULL;
	if (count)
		status = e_gw_connection_get_items (egwb->priv->cnc, egwb->priv->container_id, "name email default members", filter, &items);
	for (; items != NULL; items = g_list_next (items )) {
		GList *emails;
		GList *ptr;
		temp_item = E_GW_ITEM (items->data);
		emails = e_gw_item_get_email_list (temp_item);
		if (emails_without_ids && (ptr = g_list_find_custom (emails_without_ids, emails->data, (GCompareFunc)strcasecmp ))) {
			emails_without_ids = g_list_remove_link (emails_without_ids, ptr);
			g_list_free (ptr);
			id = g_strdup (e_gw_item_get_id (temp_item));
			member = g_new0 (EGroupMember , 1);
			member->id = id;
			group_members = g_list_append (group_members, member);
		}
		g_object_unref (temp_item);
	}
       
	g_list_foreach (members, (GFunc) e_vcard_attribute_free, NULL);
	g_list_free (members);
	g_list_foreach (emails_without_ids, (GFunc) g_free, NULL);
	g_list_free (emails_without_ids);
	g_list_free (items);
       	e_gw_item_set_member_list (item, group_members);
  
}

static void 
set_member_changes (EGwItem *new_item, EGwItem *old_item, EBookBackendGroupwise *egwb)
{
	GList *old_members, *new_members ;
	GList *old_ids,  *new_ids,  *additions, *deletions;
       	old_ids = new_ids = additions = deletions = NULL;
	old_members = e_gw_item_get_member_list (old_item);
	new_members = e_gw_item_get_member_list (new_item);
	for ( ;old_members != NULL; old_members = g_list_next (old_members)) {
		EGroupMember *member;
		member = (EGroupMember *)old_members->data;
		old_ids = g_list_append (old_ids, member->id);
	}
	for ( ;new_members != NULL; new_members = g_list_next (new_members)) {
		EGroupMember *member;
		member = (EGroupMember *)new_members->data;
		new_ids = g_list_append (new_ids, member->id);
	}
	
	compare_string_lists (old_ids, new_ids, &additions, &deletions);
	if (additions) 
		e_gw_connection_add_members (egwb->priv->cnc, e_gw_item_get_id (old_item), additions);
	if (deletions)
		e_gw_connection_remove_members (egwb->priv->cnc, e_gw_item_get_id (old_item), deletions);
	g_list_free (new_ids);
	g_list_free (old_ids);
	g_list_free (additions);
	g_list_free (deletions);
		
}
static void 
set_organization_in_gw_item (EGwItem *item, EContact *contact, EBookBackendGroupwise *egwb)
{
	char *organization_name;
	EGwItem *org_item, *temp_item;
	EGwFilter *filter;
	int status;
	char *id;
	GList *items;
	
	organization_name = e_contact_get (contact, E_CONTACT_ORG);
	if (organization_name == NULL || strlen (organization_name) == 0)
		return;
	filter = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, "name", organization_name);
	items = NULL;
	status = e_gw_connection_get_items (egwb->priv->cnc, egwb->priv->container_id, NULL, filter, &items);
	g_object_unref (filter);
	id = NULL;
	for (; items != NULL; items = g_list_next (items )) {
		temp_item = E_GW_ITEM (items->data);
		if (e_gw_item_get_item_type (temp_item) == E_GW_ITEM_TYPE_ORGANISATION) {
			id = g_strdup (e_gw_item_get_id (temp_item));
			for (;items != NULL; items = g_list_next (items))
				g_object_unref (items->data);
			break;
			
		}
		g_object_unref (temp_item);
		
	}
	g_list_free (items);

	if (id == NULL) {
		org_item = e_gw_item_new_empty ();
		e_gw_item_set_container_id (org_item, egwb->priv->container_id);
		e_gw_item_set_field_value (org_item, "name", organization_name);
		e_gw_item_set_item_type (org_item, E_GW_ITEM_TYPE_ORGANISATION);
		status = e_gw_connection_create_item (egwb->priv->cnc, org_item, &id);
		if ((status == E_GW_CONNECTION_STATUS_OK) && id) {
			EContact *contact = e_contact_new ();
			fill_contact_from_gw_item (contact, org_item, egwb->priv->categories_by_id);
			e_contact_set (contact, E_CONTACT_UID, id);
			e_contact_set (contact, E_CONTACT_FULL_NAME, organization_name);
			e_book_backend_cache_add_contact (egwb->priv->cache, contact);
			g_object_unref (contact);
		}
		g_object_unref (org_item);
		if (status != E_GW_CONNECTION_STATUS_OK)
			return;
	}
	if (id == NULL) 
		return;
	e_gw_item_set_field_value (item, "organization_id", id);
	e_gw_item_set_field_value (item , "organization", organization_name);
	
	
}
static void
set_organization_changes_in_gw_item (EGwItem *new_item, EGwItem *old_item)
{
	char *old_value;
	char *new_value;
	char *old_org_id;
	char *new_org_id;
	old_value = e_gw_item_get_field_value (old_item, "organization");
	new_value = e_gw_item_get_field_value (new_item, "organization");
	old_org_id = e_gw_item_get_field_value (old_item, "organization_id");
	new_org_id = e_gw_item_get_field_value (new_item, "organization_id");
       	if (new_value && old_value) {
		if (!g_str_equal (new_value, old_value)) {
			e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_UPDATE, "organization", new_value);
			e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_UPDATE, "organization_id", new_org_id);
		}
	
	} else if (!new_value  && old_value) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE,"organization", old_value);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "organization_id", old_org_id);
	} else if (new_value && !old_value) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "organization", new_value);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "organization_id", new_org_id);
	}
	

}

static void 
set_categories_in_gw_item (EGwItem *item, EContact *contact, EBookBackendGroupwise *egwb)
{
	GHashTable *categories_by_name;
	GList *category_names,  *category_ids;
	char *id;
	int status;

	categories_by_name = egwb->priv->categories_by_name;
	category_names = e_contact_get (contact, E_CONTACT_CATEGORY_LIST);
	category_ids = NULL;
	id = NULL;
	for (; category_names != NULL; category_names = g_list_next (category_names)) {
		if (!category_names->data || strlen(category_names->data) == 0 )
			continue;
		id = g_hash_table_lookup (categories_by_name, category_names->data);
		if (id) 
			category_ids = g_list_append (category_ids, g_strdup (id));
		else {
			EGwItem *category_item;

			category_item = e_gw_item_new_empty();
			e_gw_item_set_item_type (category_item,  E_GW_ITEM_TYPE_CATEGORY);
			e_gw_item_set_category_name (category_item, category_names->data);
			status = e_gw_connection_create_item (egwb->priv->cnc, category_item, &id);
			if (status == E_GW_CONNECTION_STATUS_OK && id != NULL) {
				char **components = g_strsplit (id, "@", -1);
				char *temp_id = components[0];
							
				g_hash_table_insert (categories_by_name, g_strdup (category_names->data), g_strdup(temp_id));
				g_hash_table_insert (egwb->priv->categories_by_id, g_strdup(temp_id), g_strdup (category_names->data));
				category_ids = g_list_append (category_ids, g_strdup(temp_id));
				g_free (id);
				g_strfreev(components);
			}
			g_object_unref (category_item);
			
		}
	}
	e_gw_item_set_categories (item, category_ids);	


}


static void 
set_categories_changes (EGwItem *new_item, EGwItem *old_item)
{
	GList *old_category_list;
	GList *new_category_list;
	GList *temp, *old_categories_copy, *added_categories = NULL;
	gboolean categories_matched;
	char *category1, *category2;
	old_category_list = e_gw_item_get_categories (old_item);
	new_category_list = e_gw_item_get_categories (new_item);
	if (old_category_list && new_category_list) {
		old_categories_copy = g_list_copy (old_category_list);
		for ( ; new_category_list != NULL; new_category_list = g_list_next (new_category_list)) {
			
			category1  = new_category_list->data;
			temp = old_category_list;
			categories_matched  = FALSE;
			for(; temp != NULL; temp = g_list_next (temp)) {
				category2 = temp->data;
				if ( g_str_equal (category1, category2)) {
					categories_matched = TRUE;
					old_categories_copy = g_list_remove (old_categories_copy, category2);
					break;
				}
				
			}
			if (!categories_matched)
				added_categories = g_list_append (added_categories, category1);
		}
		
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "categories", added_categories);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "categories", old_categories_copy);

	} else if (!new_category_list && old_category_list) {
		e_gw_item_set_change (new_item,  E_GW_ITEM_CHANGE_TYPE_DELETE, "categories", old_category_list);
	} else if (new_category_list && !old_category_list) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "categories", new_category_list);
	}

}


static void 
fill_contact_from_gw_item (EContact *contact, EGwItem *item, GHashTable *categories_by_ids)
{

	char* value;
	int element_type;
	int i;
	gboolean is_contact_list;
	
	is_contact_list = e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_GROUP ? TRUE: FALSE;
	e_contact_set (contact, E_CONTACT_IS_LIST, GINT_TO_POINTER (is_contact_list));
	if (is_contact_list)
		e_contact_set (contact, E_CONTACT_LIST_SHOW_ADDRESSES, GINT_TO_POINTER (TRUE));

	for ( i = 0; i < num_mappings; i++) {
		element_type = mappings[i].element_type;
		if(element_type == ELEMENT_TYPE_SIMPLE)
			{
				value = e_gw_item_get_field_value (item, mappings[i].element_name);
				if(value != NULL)
					e_contact_set (contact, mappings[i].field_id, value);
	
			} else if (element_type == ELEMENT_TYPE_COMPLEX) {
				if (mappings[i].field_id == E_CONTACT_CATEGORIES) {
					GList *category_ids, *category_names;
					char *name;
					category_names = NULL;
					category_ids = e_gw_item_get_categories (item);
					for (; category_ids; category_ids = g_list_next (category_ids)) {
						name = g_hash_table_lookup (categories_by_ids, category_ids->data);
						if (name)
							category_names = g_list_append (category_names, name);
					}
					if (category_names) {
						e_contact_set (contact, E_CONTACT_CATEGORY_LIST, category_names);
						g_list_free (category_names);
					}
				}
				else      
					mappings[i].populate_contact_func(contact, item);
			}
    
	}
}

static void
e_book_backend_groupwise_create_contact (EBookBackend *backend,
					 EDataBook *book,
					 guint32 opid,
					 const char *vcard )
{
	EContact *contact;
	EBookBackendGroupwise *egwb;
	char *id;
	int status;
	EGwItem *item;
	int element_type;
	char* value;
	int i;

	egwb = E_BOOK_BACKEND_GROUPWISE (backend);


	switch (egwb->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		return;
	   
	case  GNOME_Evolution_Addressbook_MODE_REMOTE : 
		
		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_AuthenticationRequired, NULL);
			return;
		}
		if (!egwb->priv->is_writable) {
			e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_PermissionDenied, NULL);
			return;
		}
		contact = e_contact_new_from_vcard(vcard);
		item = e_gw_item_new_empty ();
		e_gw_item_set_item_type (item, e_contact_get (contact, E_CONTACT_IS_LIST) ? E_GW_ITEM_TYPE_GROUP :E_GW_ITEM_TYPE_CONTACT);
		e_gw_item_set_container_id (item, g_strdup(egwb->priv->container_id));
		
		for (i = 0; i < num_mappings; i++) {
			element_type = mappings[i].element_type;
			if (element_type == ELEMENT_TYPE_SIMPLE)  {
				value =  e_contact_get(contact, mappings[i].field_id);
				if (mappings[i].field_id == E_CONTACT_ORG) {
					set_organization_in_gw_item (item, contact, egwb);
					continue;
				}
				if (value != NULL)
					e_gw_item_set_field_value (item, mappings[i].element_name, value);
			} else if (element_type == ELEMENT_TYPE_COMPLEX) {
				if (mappings[i].field_id == E_CONTACT_CATEGORIES) 
					set_categories_in_gw_item (item, contact, egwb);
				else if (mappings[i].field_id == E_CONTACT_EMAIL) {
					if (e_contact_get (contact, E_CONTACT_IS_LIST))
						set_members_in_gw_item (item, contact, egwb);
				}
				else
					mappings[i].set_value_in_gw_item (item, contact);
			}
     
    
		}
		id = NULL;
		status = e_gw_connection_create_item (egwb->priv->cnc, item, &id);  
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION) 
			status = e_gw_connection_create_item (egwb->priv->cnc, item, &id);  

		/* Make sure server has returned  an id for the created contact */
		if (status == E_GW_CONNECTION_STATUS_OK && id) {
			e_contact_set (contact, E_CONTACT_UID, id);
			g_free (id);
			e_book_backend_cache_add_contact (egwb->priv->cache, contact);
			e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_Success, contact);
			
		}
		else {
			e_data_book_respond_create(book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
		}
		g_object_unref (item);
		return;
	default:
		break;
	}
}
static void
e_book_backend_groupwise_remove_contacts (EBookBackend *backend,
					  EDataBook    *book,
					  guint32 opid,
					  GList *id_list)
{
  
	char *id;
	EBookBackendGroupwise *ebgw;
	GList *deleted_ids = NULL;

	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	
	switch (ebgw->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_remove_contacts (book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		return;
	case GNOME_Evolution_Addressbook_MODE_REMOTE : 
		
		if (ebgw->priv->cnc == NULL) {
			e_data_book_respond_remove_contacts (book, opid, GNOME_Evolution_Addressbook_AuthenticationRequired, NULL);
			return;
		}
		
		if (!ebgw->priv->is_writable) {
			e_data_book_respond_remove_contacts (book, opid, GNOME_Evolution_Addressbook_PermissionDenied, NULL);
			return;
		}
		
		for ( ; id_list != NULL; id_list = g_list_next (id_list)) {
			id = (char*) id_list->data;
			e_gw_connection_remove_item (ebgw->priv->cnc, ebgw->priv->container_id, id);
			deleted_ids =  g_list_append (deleted_ids, id);
			e_book_backend_cache_remove_contact (ebgw->priv->cache, id);
		}
		e_data_book_respond_remove_contacts (book, opid,
						     GNOME_Evolution_Addressbook_Success,  deleted_ids);
		return;
	default :
		break;
	}
}


static void 
set_changes_in_gw_item (EGwItem *new_item, EGwItem *old_item)
{

	char* new_value;
	char *old_value;
	int element_type;
	int i;

	g_return_if_fail (E_IS_GW_ITEM(new_item));
	g_return_if_fail (E_IS_GW_ITEM(old_item));

	
	for ( i = 0; i < num_mappings; i++) {
		element_type = mappings[i].element_type;
		if(element_type == ELEMENT_TYPE_SIMPLE)
			{
				if (mappings[i].field_id == E_CONTACT_ORG) {
					set_organization_changes_in_gw_item (new_item, old_item);
					continue;
				}
					
				new_value = e_gw_item_get_field_value (new_item, mappings[i].element_name);
				old_value = e_gw_item_get_field_value (old_item, mappings[i].element_name);
				if (new_value && old_value) {
					if (!g_str_equal (new_value, old_value))
						e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_UPDATE, mappings[i].element_name, new_value);
				} else if (!new_value  && old_value) {
					e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, mappings[i].element_name, old_value);
				} else if (new_value && !old_value) {
					e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, mappings[i].element_name, new_value);
				}
									
			} else if (element_type == ELEMENT_TYPE_COMPLEX) {
				if (mappings[i].field_id != E_CONTACT_EMAIL)
					mappings[i].set_changes(new_item, old_item);
			}
    
	}


}

static void
e_book_backend_groupwise_modify_contact (EBookBackend *backend,
					 EDataBook    *book,
					 guint32       opid,
					 const char   *vcard)
{	
	EContact *contact;
	EBookBackendGroupwise *egwb;
	char *id;
	int status;
	EGwItem *new_item;
	EGwItem *old_item;
	int element_type;
	char* value;
	char *new_org, *old_org;
	int i;
	
	egwb = E_BOOK_BACKEND_GROUPWISE (backend);

	switch (egwb->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		e_data_book_respond_modify(book, opid, GNOME_Evolution_Addressbook_RepositoryOffline, NULL);
		return;
	case GNOME_Evolution_Addressbook_MODE_REMOTE :
			
		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_modify (book, opid, GNOME_Evolution_Addressbook_AuthenticationRequired, NULL);
			return;
		}
		if (!egwb->priv->is_writable) {
			e_data_book_respond_modify (book, opid, GNOME_Evolution_Addressbook_PermissionDenied, NULL);
			return;
		}
		contact = e_contact_new_from_vcard(vcard);
		new_item = e_gw_item_new_empty ();

		for (i = 0; i < num_mappings; i++) {
			element_type = mappings[i].element_type;
			if (element_type == ELEMENT_TYPE_SIMPLE)  {
				value =  e_contact_get(contact, mappings[i].field_id);
				if (value &&  *value)
					e_gw_item_set_field_value (new_item, mappings[i].element_name, value);
			} else if (element_type == ELEMENT_TYPE_COMPLEX) {
				if (mappings[i].field_id == E_CONTACT_CATEGORIES) 
					set_categories_in_gw_item (new_item, contact, egwb);
				else if (mappings[i].field_id == E_CONTACT_EMAIL) {
					if (e_contact_get (contact, E_CONTACT_IS_LIST))
						set_members_in_gw_item (new_item, contact, egwb);
				}
				else
					mappings[i].set_value_in_gw_item (new_item, contact);

			}
   
		}
	
		id = e_contact_get (contact, E_CONTACT_UID);
		old_item = NULL;
		status = e_gw_connection_get_item (egwb->priv->cnc, egwb->priv->container_id, id, NULL,  &old_item);
		
		if (old_item == NULL) {
			e_data_book_respond_modify (book, opid, GNOME_Evolution_Addressbook_ContactNotFound, NULL);
			return;
		}
		
		if (status != E_GW_CONNECTION_STATUS_OK) {
			e_data_book_respond_modify (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			return;
		}
		
		if (e_contact_get (contact, E_CONTACT_IS_LIST))
			set_member_changes (new_item, old_item, egwb);
		new_org = e_gw_item_get_field_value (new_item, "organization");
		old_org = e_gw_item_get_field_value (old_item, "organization");
		if (new_org && *new_org) {
			
			if ((old_org == NULL) || (old_org && strcmp (new_org, old_org)) != 0)
				set_organization_in_gw_item (new_item, contact, egwb);
		}
		
		set_changes_in_gw_item (new_item, old_item);
		
		e_gw_item_set_item_type (new_item, e_gw_item_get_item_type (old_item));
		status = e_gw_connection_modify_item (egwb->priv->cnc, id, new_item);
		if (status == E_GW_CONNECTION_STATUS_OK) {
			e_data_book_respond_modify (book, opid, GNOME_Evolution_Addressbook_Success, contact);
			e_book_backend_cache_remove_contact (egwb->priv->cache, id);
			e_book_backend_cache_add_contact (egwb->priv->cache, contact);
		}
		else 
			e_data_book_respond_modify (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
		g_object_unref (new_item);
		g_object_ref (old_item);
		g_object_unref (contact);
		return;
	default :
		break;
		
	}

}
	
static void
e_book_backend_groupwise_get_contact (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const char   *id)
{
	EBookBackendGroupwise *gwb;
	int status ;
	EGwItem *item;
	EContact *contact;
	char *vcard;

	gwb =  E_BOOK_BACKEND_GROUPWISE (backend);


	switch (gwb->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		contact = e_book_backend_cache_get_contact (gwb->priv->cache, id);
		vcard =  e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
		if (contact) {
			e_data_book_respond_get_contact(book, opid, GNOME_Evolution_Addressbook_Success, vcard);
			g_free (vcard);
			g_object_unref (contact);
		}
		else 
			e_data_book_respond_get_contact(book, opid, GNOME_Evolution_Addressbook_ContactNotFound, "");
			
		return;

	case GNOME_Evolution_Addressbook_MODE_REMOTE : 	
		if (gwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_OtherError, NULL);
			return;
		}
		status = e_gw_connection_get_item (gwb->priv->cnc, gwb->priv->container_id, id,  NULL, &item);
		if (status == E_GW_CONNECTION_STATUS_OK) {
			if (item) {
				contact = e_contact_new ();
				fill_contact_from_gw_item (contact, item, gwb->priv->categories_by_id);
				vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
				e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_Success, vcard);
				g_free (vcard);
				g_object_unref (contact);
				g_object_unref (item);
				return;
			}
    
		}
		e_data_book_respond_get_contact (book, opid, GNOME_Evolution_Addressbook_ContactNotFound, "");  
		return;
	default :
		break;
	}
}

typedef struct {
	EGwFilter *filter;
	gboolean is_filter_valid;
	gboolean is_personal_book;
	int auto_completion; 
	char *search_string;
} EBookBackendGroupwiseSExpData;

static ESExpResult *
func_and(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	EGwFilter *filter;
	EBookBackendGroupwiseSExpData *sexp_data;

	sexp_data = (EBookBackendGroupwiseSExpData *) data;
	filter = E_GW_FILTER (sexp_data->filter);
	if (argc > 0)
		e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_AND, argc);
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_or(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	EGwFilter *filter;
	EBookBackendGroupwiseSExpData *sexp_data;

	sexp_data = (EBookBackendGroupwiseSExpData *) data;
	filter = E_GW_FILTER (sexp_data->filter);
	if (argc > 0)
		 e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, argc);
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_not(ESExp *f, int argc, ESExpResult **argv, void *data)
{
	ESExpResult *r;
	EBookBackendGroupwiseSExpData *sexp_data;

	sexp_data = (EBookBackendGroupwiseSExpData *) data;
	sexp_data->is_filter_valid = FALSE;
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_contains(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	ESExpResult *r;
	EGwFilter *filter;
	EBookBackendGroupwiseSExpData *sexp_data;

	sexp_data = (EBookBackendGroupwiseSExpData *) data;
	filter = E_GW_FILTER (sexp_data->filter);

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = argv[1]->value.string;
		char *gw_field_name;
		
		gw_field_name = NULL;
		if (g_str_equal (propname, "full_name"))
			gw_field_name = "fullName";
		else if (g_str_equal (propname, "email"))
			gw_field_name = "emailList/email";
		else if (g_str_equal (propname, "file_as") || g_str_equal (propname, "nickname"))
			 gw_field_name = "name";
		

		if (gw_field_name) {
			if (g_str_equal (gw_field_name, "fullName")) {
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_CONTAINS, "fullName/firstName", str);	
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_CONTAINS, "fullName/lastName", str);
				if (sexp_data->is_personal_book) {
					e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_CONTAINS, "fullName/displayName", str);
					e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 3);
				}
				else 
					e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 2);
			}
			else 
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_CONTAINS, gw_field_name, str);
		}
		else 
		     sexp_data->is_filter_valid = FALSE; 
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_is(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	ESExpResult *r;
	EGwFilter *filter;
	EBookBackendGroupwiseSExpData *sexp_data;

	sexp_data = (EBookBackendGroupwiseSExpData *) data;
	filter = E_GW_FILTER (sexp_data->filter);

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = argv[1]->value.string;
		char *gw_field_name;
	
		gw_field_name = NULL;
		if (g_str_equal (propname, "full_name"))
			gw_field_name = "fullName";
		else if (g_str_equal (propname, "email"))
			gw_field_name = "emailList/email";
		else if (g_str_equal (propname, "file_as") || g_str_equal (propname, "nickname"))
			gw_field_name = "name";
		
		if (gw_field_name) {
			if (g_str_equal (gw_field_name, "fullName")) {
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, "fullName/firstName", str);	
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, "fullName/lastName", str);
				if (sexp_data->is_personal_book) {
					e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, "fullName/displayName", str);
					e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 3);
				}
				else 
					e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 2);
			}
			else 
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EQUAL, gw_field_name, str);
		
		}
		else 
		     sexp_data->is_filter_valid = FALSE;
		
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

#define BEGINS_WITH_NAME (1 << 0)
#define BEGINS_WITH_EMAIL (1 << 1)
#define BEGINS_WITH_FILE_AS (1 << 2)
#define BEGINS_WITH_NICK_NAME (1 << 3)
#define AUTO_COMPLETION_QUERY 15

static ESExpResult *
func_beginswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{

	ESExpResult *r;
	EGwFilter *filter;
	EBookBackendGroupwiseSExpData *sexp_data;

	sexp_data = (EBookBackendGroupwiseSExpData *) data;
	filter = E_GW_FILTER (sexp_data->filter);

	if (argc == 2
	    && argv[0]->type == ESEXP_RES_STRING
	    && argv[1]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = argv[1]->value.string;
		char *gw_field_name;
	
		gw_field_name = NULL;
		if (g_str_equal (propname, "full_name")) {
			gw_field_name = "fullName";
			sexp_data->auto_completion |= BEGINS_WITH_NAME;
			sexp_data->search_string = g_strdup (str);
		}
		else if (g_str_equal (propname, "email")) {
			gw_field_name = "emailList/email";
			sexp_data->auto_completion |= BEGINS_WITH_EMAIL;
		}
		else if (g_str_equal (propname, "file_as")) { 
			 gw_field_name = "name";
			 sexp_data->auto_completion |= BEGINS_WITH_FILE_AS;
		} else if (g_str_equal (propname, "nickname")) { 
			 gw_field_name = "name";
			 sexp_data->auto_completion |= BEGINS_WITH_NICK_NAME;
		}

		if (gw_field_name) {
			
			if (g_str_equal (gw_field_name, "fullName")) {
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_BEGINS, "fullName/firstName", str);	
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_BEGINS, "fullName/lastName", str);
				if (sexp_data->is_personal_book) {
					e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_BEGINS, "fullName/displayName", str);
					e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 3);
				}
				else 
					e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 2);
			}
			else 
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_BEGINS, gw_field_name, str);
			
		}
		else 
			sexp_data->is_filter_valid = FALSE;
	

	
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}

static ESExpResult *
func_endswith(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	EBookBackendGroupwiseSExpData *sexp_data;
	ESExpResult *r;

	sexp_data = (EBookBackendGroupwiseSExpData *) data;
	sexp_data->is_filter_valid = FALSE;
	
	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	
	r->value.bool = FALSE; 

	return r;
}

static ESExpResult *
func_exists(struct _ESExp *f, int argc, struct _ESExpResult **argv, void *data)
{
	ESExpResult *r;
	EGwFilter *filter;
	EBookBackendGroupwiseSExpData *sexp_data;

	sexp_data = (EBookBackendGroupwiseSExpData *) data;
	filter = E_GW_FILTER (sexp_data->filter);

	if (argc == 1
	    && argv[0]->type == ESEXP_RES_STRING) {
		char *propname = argv[0]->value.string;
		char *str = argv[1]->value.string;
		char *gw_field_name;
	
		gw_field_name = NULL;
		if (g_str_equal (propname, "full_name"))
			gw_field_name = "fullName";
		else if (g_str_equal (propname, "email"))
			gw_field_name = "emailList/email";
		else if (g_str_equal (propname, "file_as") || g_str_equal (propname, "nickname"))
			 gw_field_name = "name";

		if (gw_field_name) {
			
			if (g_str_equal (gw_field_name, "fullName")) {
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EXISTS, "fullName/firstName", str);	
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EXISTS, "fullName/lastName", str);
				if (sexp_data->is_personal_book) {
					e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EXISTS, "fullName/displayName", str);
					e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 3);
				}
				else 
					e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 2);
			}
			else 
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_EXISTS, gw_field_name, str);
		}
		else 
			sexp_data->is_filter_valid = FALSE;
	
	}

	r = e_sexp_result_new(f, ESEXP_RES_BOOL);
	r->value.bool = FALSE;

	return r;
}
/* 'builtin' functions */
static struct {
	char *name;
	ESExpFunc *func;
	int type;		/* set to 1 if a function can perform shortcut evaluation, or
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


static EGwFilter*
e_book_backend_groupwise_build_gw_filter (EBookBackendGroupwise *ebgw, const char *query, gpointer is_auto_completion, char ** search_string)
{
	ESExp *sexp;
	ESExpResult *r;
	EBookBackendGroupwiseSExpData *sexp_data;
	EGwFilter *filter;
	int i;


	sexp = e_sexp_new();
	filter = e_gw_filter_new ();
	
	sexp_data = g_new0 (EBookBackendGroupwiseSExpData, 1);
	sexp_data->filter = filter;
	sexp_data->is_filter_valid = TRUE;
	sexp_data->is_personal_book = e_book_backend_is_writable ( E_BOOK_BACKEND (ebgw));
	sexp_data->auto_completion = 0;
	sexp_data->search_string = NULL;

	for(i=0;i<sizeof(symbols)/sizeof(symbols[0]);i++) {
		if (symbols[i].type == 1) {
			e_sexp_add_ifunction(sexp, 0, symbols[i].name,
					     (ESExpIFunc *)symbols[i].func, sexp_data);
		} else {
			e_sexp_add_function(sexp, 0, symbols[i].name,
					    symbols[i].func, sexp_data);
		}
	}

	e_sexp_input_text(sexp, query, strlen(query));
	e_sexp_parse(sexp);
	r = e_sexp_eval(sexp);
	e_sexp_result_free(sexp, r);
	e_sexp_unref (sexp);
	
	
	if (sexp_data->is_filter_valid) {
		if (sexp_data->auto_completion == AUTO_COMPLETION_QUERY)
			*(gboolean *)is_auto_completion = TRUE;
		if (search_string)
			*search_string = sexp_data->search_string;
		g_free (sexp_data);
		return filter;
	}
	else {
		g_object_unref (filter);
		g_free (sexp_data);
		return NULL;
	}


}


static void
e_book_backend_groupwise_get_contact_list (EBookBackend *backend,
					   EDataBook    *book,
					   guint32       opid,
					   const char   *query )
{
  
	GList *vcard_list;
	int status;
	GList *gw_items, *contacts, *temp;
	EContact *contact;
	EBookBackendGroupwise *egwb;
	gboolean match_needed;
	EBookBackendSExp *card_sexp = NULL;
	EGwFilter *filter = NULL;
	GPtrArray *ids;
	gboolean is_auto_completion;
	
	egwb = E_BOOK_BACKEND_GROUPWISE (backend);
	vcard_list = NULL;
	gw_items = NULL;

	switch (egwb->priv->mode) {
		
	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		contacts = e_book_backend_cache_get_contacts (egwb->priv->cache, query);
		temp = contacts;
		for (; contacts != NULL; contacts = g_list_next(contacts)) { 
			vcard_list = g_list_append (vcard_list, e_vcard_to_string (E_VCARD (contacts->data), EVC_FORMAT_VCARD_30));
			g_object_unref (contacts->data);
		}
		e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_Success,
						      vcard_list);
		if (temp)
			g_list_free (temp);
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		
		if (egwb->priv->cnc == NULL) {
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_AuthenticationRequired, NULL);
			return;
		}
	
		match_needed = TRUE;
		card_sexp = e_book_backend_sexp_new (query);
		if (!card_sexp) {
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_InvalidQuery,
						      vcard_list);
		}
		
		status = E_GW_CONNECTION_STATUS_OK;
		if (egwb->priv->is_cache_ready ) {
			
			ids = e_book_backend_cache_search (egwb->priv->cache, query);
			if (ids->len > 0) {
				status = e_gw_connection_get_items_from_ids (egwb->priv->cnc, egwb->priv->container_id, "name email default members", ids, &gw_items);
				if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_gw_connection_get_items_from_ids (egwb->priv->cnc, egwb->priv->container_id, "name email default members", ids, &gw_items);
			}
			match_needed = FALSE;
			g_ptr_array_free (ids, TRUE);
			
			
		} else { 
			if (strcmp (query, "(contains \"x-evolution-any-field\" \"\")") != 0)
				filter = e_book_backend_groupwise_build_gw_filter (egwb, query, &is_auto_completion, NULL);
			if (filter)
				match_needed = FALSE;
			status = e_gw_connection_get_items (egwb->priv->cnc, egwb->priv->container_id, "name email default members", filter, &gw_items);
			if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_gw_connection_get_items (egwb->priv->cnc, egwb->priv->container_id, "name email default members", filter, &gw_items);
			
		}
		
		if (status != E_GW_CONNECTION_STATUS_OK) {
			e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_OtherError,
							      NULL);
			return;
		}
		for (; gw_items != NULL; gw_items = g_list_next(gw_items)) { 
			contact = e_contact_new ();
			fill_contact_from_gw_item (contact, E_GW_ITEM (gw_items->data), egwb->priv->categories_by_id);
			if (match_needed &&  e_book_backend_sexp_match_contact (card_sexp, contact))
				vcard_list = g_list_append (vcard_list, e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));
			else 
				vcard_list = g_list_append (vcard_list, e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));
			g_object_unref (contact);
			g_object_unref (gw_items->data);
		}
		if (gw_items)
			g_list_free (gw_items);
		e_data_book_respond_get_contact_list (book, opid, GNOME_Evolution_Addressbook_Success,
						      vcard_list);
		if (filter)
			g_object_unref (filter);
		return;
	default :
		break;
		
	}
}
	
typedef struct {
	EBookBackendGroupwise *bg;
	GMutex *mutex;
	GCond *cond;
	GThread *thread;
	gboolean stopped;
} GroupwiseBackendSearchClosure;

static void
closure_destroy (GroupwiseBackendSearchClosure *closure)
{
	g_mutex_free (closure->mutex);
	g_cond_free (closure->cond);
	g_free (closure);
}

static GroupwiseBackendSearchClosure*
init_closure (EDataBookView *book_view, EBookBackendGroupwise *bg)
{
	GroupwiseBackendSearchClosure *closure = g_new (GroupwiseBackendSearchClosure, 1);

	closure->bg = bg;
	closure->mutex = g_mutex_new ();
	closure->cond = g_cond_new ();
	closure->thread = NULL;
	closure->stopped = FALSE;

	g_object_set_data_full (G_OBJECT (book_view), "EBookBackendGroupwise.BookView::closure",
				closure, (GDestroyNotify)closure_destroy);

	return closure;
}

static GroupwiseBackendSearchClosure*
get_closure (EDataBookView *book_view)
{
	return g_object_get_data (G_OBJECT (book_view), "EBookBackendGroupwise.BookView::closure");
}
 
static gpointer
book_view_thread (gpointer data)
{
	int status;
	GList *gw_items, *temp_list, *contacts;
	EContact *contact;
	EBookBackendGroupwise *gwb;
	const char *query = NULL;
	EGwFilter *filter = NULL;
	GPtrArray *ids;
	gboolean stopped = FALSE;
	EDataBookView *book_view = data;
	GroupwiseBackendSearchClosure *closure = get_closure (book_view);
	char *view = NULL;
	gboolean is_auto_completion = FALSE;
	char *search_string = NULL;

	gwb  = closure->bg;
	gw_items = NULL;

       	bonobo_object_ref (book_view);
	g_mutex_lock (closure->mutex);
	g_cond_signal (closure->cond);
	g_mutex_unlock (closure->mutex);
	
	query = e_data_book_view_get_card_query (book_view);
	switch (gwb->priv->mode) {

	case GNOME_Evolution_Addressbook_MODE_LOCAL :
		if (!gwb->priv->cache) {
			e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_Success);
			return NULL;
		}
		contacts = e_book_backend_cache_get_contacts (gwb->priv->cache, query);
		temp_list = contacts;
		for (; contacts != NULL; contacts = g_list_next(contacts)) {
			g_mutex_lock (closure->mutex);
			stopped = closure->stopped;
			g_mutex_unlock (closure->mutex);
			if (stopped) {
				for (;contacts != NULL; contacts = g_list_next (contacts))
					g_object_unref (contacts->data);
				break;	
			}
			e_data_book_view_notify_update (book_view, E_CONTACT(contacts->data));
			g_object_unref (contacts->data);
		}
		if (!stopped)
			e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_Success);
		if (temp_list)
			g_list_free (temp_list);
		bonobo_object_unref (book_view);
		return NULL;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE :
		
		if (gwb->priv->cnc == NULL) {
			e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_AuthenticationRequired);
			bonobo_object_unref (book_view);
			return NULL; 
		}

		filter = e_book_backend_groupwise_build_gw_filter (gwb, query, &is_auto_completion, &search_string);
		view = "name email default members";
		if (is_auto_completion) 
			view = "name email";
		if (is_auto_completion && !gwb->priv->is_writable) {
			g_object_unref (filter);
			filter = NULL;
			
			if (search_string) {
				filter = e_gw_filter_new ();
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_BEGINS, "fullName/lastName", search_string);
				e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_BEGINS, "fullName/firstName", search_string);
				e_gw_filter_group_conditions (filter, E_GW_FILTER_OP_OR, 2);
				g_free (search_string);
				
			}
		}
		
		if (!gwb->priv->is_writable && !filter) {
			e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_Success);
			bonobo_object_unref (book_view);
			return NULL; 
		}
		
		
		
		else 
		status =  E_GW_CONNECTION_STATUS_OK;
		if (gwb->priv->is_cache_ready ) {
			e_data_book_view_notify_status_message (book_view, _("Searching..."));
			ids = e_book_backend_cache_search (gwb->priv->cache, query);
			
			if (ids->len > 0) {
				
				status = e_gw_connection_get_items_from_ids (gwb->priv->cnc, gwb->priv->container_id, view, ids, &gw_items);
				if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
					status = e_gw_connection_get_items_from_ids (gwb->priv->cnc, gwb->priv->container_id, view, ids, &gw_items);
			}
			g_ptr_array_free (ids, TRUE);
			
		
		} else { 
			if (filter) 
				e_data_book_view_notify_status_message (book_view, _("Searching..."));
			else 
				e_data_book_view_notify_status_message (book_view, _("Loading..."));
			status = e_gw_connection_get_items (gwb->priv->cnc, gwb->priv->container_id, view, filter, &gw_items);
			if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
				status = e_gw_connection_get_items (gwb->priv->cnc, gwb->priv->container_id, view, filter, &gw_items);
		}
		
		if (status != E_GW_CONNECTION_STATUS_OK) {
			e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_OtherError);
			bonobo_object_unref (book_view);
			return NULL;
		}
		temp_list = gw_items;
		for (; gw_items != NULL; gw_items = g_list_next(gw_items)) { 
			
			g_mutex_lock (closure->mutex);
			stopped = closure->stopped;
			g_mutex_unlock (closure->mutex);
			
			if (stopped) {
				for (;gw_items != NULL; gw_items = g_list_next (gw_items))
					g_object_unref (gw_items->data);
				break;
			}
			contact = e_contact_new ();
			fill_contact_from_gw_item (contact, E_GW_ITEM (gw_items->data), gwb->priv->categories_by_id);
			if (e_contact_get_const (contact, E_CONTACT_UID)) 
				e_data_book_view_notify_update (book_view, contact);
			else 
				g_critical ("Id missing for item %s\n", (char *)e_contact_get_const (contact, E_CONTACT_FILE_AS));
			
			g_object_unref(contact);
			g_object_unref (gw_items->data);
			
		}
		if (temp_list)
			g_list_free (temp_list);
		if (!stopped)
			e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_Success);
		if (filter)
			g_object_unref (filter);
		bonobo_object_unref (book_view);
		return NULL;
	default :
		break;
	}
	return NULL;
}
static void
e_book_backend_groupwise_start_book_view (EBookBackend  *backend,
				     EDataBookView *book_view)
{
	GroupwiseBackendSearchClosure *closure = init_closure (book_view, E_BOOK_BACKEND_GROUPWISE (backend));
	
	g_mutex_lock (closure->mutex);
	closure->thread = g_thread_create (book_view_thread, book_view, FALSE, NULL);
	g_cond_wait (closure->cond, closure->mutex);
	
	/* at this point we know the book view thread is actually running */
	g_mutex_unlock (closure->mutex);
}
  
static void
e_book_backend_groupwise_stop_book_view (EBookBackend  *backend,
					 EDataBookView *book_view)
{
	GroupwiseBackendSearchClosure *closure = get_closure (book_view);
	

	g_mutex_lock (closure->mutex);
	if (!closure->stopped)
		closure->stopped = TRUE;
	g_mutex_unlock (closure->mutex);
	
}

static void
e_book_backend_groupwise_get_changes (EBookBackend *backend,
				      EDataBook    *book,
				      guint32       opid,
				      const char *change_id  )
{

	/* FIXME : provide implmentation */

       
}



#define CURSOR_ITEM_LIMIT 100
/*
static gpointer
build_cache (EBookBackendGroupwise *ebgw)
{
	int status;
	GList *gw_items = NULL;
	EContact *contact;
	
	
	EBookBackendGroupwisePrivate *priv = ebgw->priv;
	
	
	status = e_gw_connection_get_items (ebgw->priv->cnc, ebgw->priv->container_id, "name email default members", NULL, &gw_items);
	if (status != E_GW_CONNECTION_STATUS_OK) 
		return FALSE;
	

	for (; gw_items != NULL; gw_items = g_list_next(gw_items)) { 
		contact = e_contact_new ();
		fill_contact_from_gw_item (contact, E_GW_ITEM (gw_items->data), ebgw->priv->categories_by_id);
		e_book_backend_cache_add_contact (ebgw->priv->cache, contact);
		g_object_unref(contact);
		g_object_unref (gw_items->data);
			
	}
		
	e_book_backend_cache_set_populated (priv->cache);
	priv->is_cache_ready=TRUE;
     	
	g_list_free (gw_items);

	
	
	return NULL;
}*/

/*FIXME using cursors for address book seems to be crashing server 
till it gets fixed we will use get items. cursor implementation is below */

static gpointer
build_cache (EBookBackendGroupwise *ebgw)
{
	int status;
	GList *gw_items = NULL, *l;
	EContact *contact;
	int cursor;
	gboolean done = FALSE;
	EBookBackendGroupwisePrivate *priv = ebgw->priv;
	const char *position = E_GW_CURSOR_POSITION_START;
	
	
	status = e_gw_connection_create_cursor (priv->cnc, priv->container_id, "name email default members", NULL, &cursor);
	if (status != E_GW_CONNECTION_STATUS_OK) 
		return FALSE;
	while (!done) {

		status = e_gw_connection_read_cursor (priv->cnc, priv->container_id, cursor, TRUE, CURSOR_ITEM_LIMIT, position, &gw_items);

		for (l = gw_items; l != NULL; l = g_list_next (l)) { 
			contact = e_contact_new ();
			fill_contact_from_gw_item (contact, E_GW_ITEM (l->data), ebgw->priv->categories_by_id);
			e_book_backend_cache_add_contact (ebgw->priv->cache, contact);
			g_object_unref(contact);
			g_object_unref (l->data);
			
		}
		if (!gw_items  || g_list_length (gw_items) == 0) {
			e_book_backend_cache_set_populated (priv->cache);
			done = TRUE;
			priv->is_cache_ready=TRUE;
		}
	       	
		g_list_free (gw_items);
		gw_items = NULL;
		position = E_GW_CURSOR_POSITION_CURRENT;
	}
	
	e_gw_connection_destroy_cursor (priv->cnc, priv->container_id, cursor);
	return NULL;
}

static gboolean
update_cache (EBookBackendGroupwise *ebgw)
{
	int status;
	GList *gw_items = NULL;
	EContact *contact;
	EGwFilter *filter;
	time_t mod_time;
	char time_string[25];
	const struct tm *tm;
	struct stat buf;
	const char *cache_file_name;
	
	cache_file_name = e_file_cache_get_filename (E_FILE_CACHE(ebgw->priv->cache));
	
	stat (cache_file_name, &buf);
	mod_time = buf.st_mtime;
	tm = gmtime (&mod_time);
	strftime (time_string, 100, "%Y-%m-%dT%H:%M:%SZ", tm);

	filter = e_gw_filter_new ();
	e_gw_filter_add_filter_component (filter, E_GW_FILTER_OP_GREATERTHAN, "modified", time_string);
	status = e_gw_connection_get_items (ebgw->priv->cnc, ebgw->priv->container_id, "name email default members", filter, &gw_items);
	if (status != E_GW_CONNECTION_STATUS_OK)
		return FALSE;
	
	for (; gw_items != NULL; gw_items = g_list_next(gw_items)) { 
		const char *id;
		contact = e_contact_new ();
		fill_contact_from_gw_item (contact, E_GW_ITEM (gw_items->data), ebgw->priv->categories_by_id);
		id =  e_contact_get_const (contact, E_CONTACT_UID);
		if (e_book_backend_cache_check_contact (ebgw->priv->cache, id)) {
			e_book_backend_cache_remove_contact (ebgw->priv->cache, id);
			e_book_backend_cache_add_contact (ebgw->priv->cache, contact);
			
		} else
		    e_book_backend_cache_add_contact (ebgw->priv->cache, contact);
		
		g_object_unref(contact);
		g_object_unref (gw_items->data);
		
		    
	}
	ebgw->priv->is_cache_ready = TRUE;
	g_object_unref (filter);
	g_list_free (gw_items);
	return FALSE;

}


static void
e_book_backend_groupwise_authenticate_user (EBookBackend *backend,
					    EDataBook    *book,
					    guint32       opid,
					    const char *user,
					    const char *passwd,
					    const char *auth_method)
{
	EBookBackendGroupwise *ebgw;
	EBookBackendGroupwisePrivate *priv;
	char *id;
	int status;
	char *http_uri;
	gboolean is_writable;
	
	printf ("authenticate user ............\n");
	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	priv = ebgw->priv;
	
	switch (ebgw->priv->mode) {
	case GNOME_Evolution_Addressbook_MODE_LOCAL:
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE); 
		e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_Success); 
		return;
		
	case GNOME_Evolution_Addressbook_MODE_REMOTE:
		
		if (priv->cnc) { /*we have already authenticated to server */
			e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_Success); 
			return;
		}
		
		priv->cnc = e_gw_connection_new (priv->uri, user, passwd);
		if (!E_IS_GW_CONNECTION(priv->cnc) && priv->use_ssl && g_str_equal (priv->use_ssl, "when-possible")) {
			http_uri = g_strconcat ("http://", priv->uri + 8, NULL);
			priv->cnc = e_gw_connection_new (http_uri, user, passwd);
			g_free (http_uri);
		}
		if (!E_IS_GW_CONNECTION(priv->cnc)) {
			e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_AuthenticationFailed);
			return;
		}
		
		id = NULL;
		is_writable = FALSE;
		status = e_gw_connection_get_address_book_id (priv->cnc,  priv->book_name, &id, &is_writable); 
		if (status == E_GW_CONNECTION_STATUS_INVALID_CONNECTION)
			status = e_gw_connection_get_address_book_id (priv->cnc,  priv->book_name, &id, &is_writable); 
		if (status == E_GW_CONNECTION_STATUS_OK) {
			if ( (id == NULL) && !priv->only_if_exists ) {
				status = e_gw_connection_create_book (priv->cnc, priv->book_name,  &id);
				is_writable = TRUE;
				if (status != E_GW_CONNECTION_STATUS_OK ) {
					e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_OtherError);
					return;
				}
				
			}
			
		}
		if (id != NULL) {
			priv->container_id = g_strdup (id);
			g_free(id);
			e_book_backend_set_is_writable (backend, is_writable);
			e_book_backend_notify_writable (backend, is_writable);
			e_book_backend_notify_connection_status (backend, TRUE); 
			priv->is_writable = is_writable;
			e_gw_connection_get_categories (priv->cnc, &priv->categories_by_id, &priv->categories_by_name);
			if (!e_gw_connection_get_version(priv->cnc))  
				e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_InvalidServerVersion);
			else
				e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_Success); 
			
			
		} else {
			e_book_backend_set_is_loaded (backend, FALSE);
			e_data_book_respond_authenticate_user (book, opid, GNOME_Evolution_Addressbook_NoSuchBook);
		}
		
	
		if (e_book_backend_cache_is_populated (priv->cache)) {
			if (priv->is_writable) 
				g_thread_create ((GThreadFunc) update_cache, ebgw, FALSE, NULL);
		}
		else if (priv->is_writable || priv->marked_for_offline){ /* for personal books we always cache*/
			g_thread_create ((GThreadFunc) build_cache, ebgw, FALSE, NULL);
		}
		return;
	default :
		break;
	}
}

static void
e_book_backend_groupwise_get_required_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;
  
	fields = g_list_append (fields , e_contact_field_name (E_CONTACT_FILE_AS));
	e_data_book_respond_get_supported_fields (book, opid,
						  GNOME_Evolution_Addressbook_Success,
						  fields);
	g_list_free (fields);
 
}

static void
e_book_backend_groupwise_get_supported_fields (EBookBackend *backend,
					       EDataBook    *book,
					       guint32       opid)
{
	GList *fields = NULL;
	int i;
  
	for (i = 0; i < num_mappings ; i ++)
		fields = g_list_append (fields, g_strdup (e_contact_field_name (mappings[i].field_id)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_2)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_EMAIL_3)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_ICQ)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_YAHOO)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_MSN)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_JABBER)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_IM_GROUPWISE)));
	fields = g_list_append (fields, g_strdup (e_contact_field_name (E_CONTACT_ADDRESS_WORK)));
	e_data_book_respond_get_supported_fields (book, opid,
						  GNOME_Evolution_Addressbook_Success,
						  fields);
	g_list_free (fields);
}

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_groupwise_cancel_operation (EBookBackend *backend, EDataBook *book)
{
	return GNOME_Evolution_Addressbook_CouldNotCancel;
}



static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_groupwise_load_source (EBookBackend           *backend,
				      ESource                *source,
				      gboolean                only_if_exists)
{
	EBookBackendGroupwise *ebgw;
	EBookBackendGroupwisePrivate *priv;
        char *book_name;
        char *uri;
	char **tokens;
   	const char *port;
	EUri *parsed_uri;
	int i;
	const char *use_ssl;
	const char *offline;

	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	priv = ebgw->priv;
	g_object_ref (source);

	offline = e_source_get_property (source, "offline_sync");
	if (offline  && g_str_equal (offline, "1"))
		priv->marked_for_offline = TRUE;
	
	if (priv->mode ==  GNOME_Evolution_Addressbook_MODE_LOCAL &&  !priv->marked_for_offline ) {
		return GNOME_Evolution_Addressbook_OfflineUnavailable;
	}
	
	uri =  e_source_get_uri (source);
	priv->original_uri = g_strdup (uri);
	if(uri == NULL)
		return  GNOME_Evolution_Addressbook_OtherError;

	tokens = g_strsplit (uri, ";", 2);
	g_free (uri);
	if (tokens[0]) 
		uri = g_strdup(tokens[0]);
	book_name = g_strdup (tokens[1]);
	if(book_name == NULL)
		return  GNOME_Evolution_Addressbook_OtherError;
	g_strfreev (tokens);
	parsed_uri = e_uri_new (uri);
	port = e_source_get_property (source, "port");
	if (port == NULL)
		port = "7191";
	use_ssl = e_source_get_property (source, "use_ssl");
	if (use_ssl && !g_str_equal (use_ssl, "never")) 
		priv->uri = g_strconcat ("https://", parsed_uri->host,":", port, "/soap", NULL );
	else 
		priv->uri = g_strconcat ("http://", parsed_uri->host,":", port, "/soap", NULL );
	priv->use_ssl = g_strdup (use_ssl);
	priv->only_if_exists = only_if_exists;
	
	
	priv->book_name = book_name;
	e_book_backend_set_is_loaded (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_set_is_writable (E_BOOK_BACKEND(backend), FALSE);  
	if (priv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
		e_book_backend_notify_writable (backend, FALSE);
		e_book_backend_notify_connection_status (backend, FALSE); 
	}
	else 
		e_book_backend_notify_connection_status (backend, TRUE); 	
	
	for (i = 0; i < strlen (uri); i++) {
		switch (uri[i]) {
		case ':' :
		case '/' :
			uri[i] = '_';
		}
	}
					 
	g_free (uri);
	e_uri_free (parsed_uri);

	if (priv->mode == GNOME_Evolution_Addressbook_MODE_LOCAL) 
		if (!e_book_backend_cache_exists (priv->original_uri))
			return GNOME_Evolution_Addressbook_OfflineUnavailable;
	
	priv->cache = e_book_backend_cache_new (priv->original_uri);
	return GNOME_Evolution_Addressbook_Success;

}

static void
e_book_backend_groupwise_remove (EBookBackend *backend,
				 EDataBook        *book,
				 guint32           opid)
{
	EBookBackendGroupwise *ebgw;
	int status;
  
	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	if (ebgw->priv->cnc == NULL) {
		e_data_book_respond_remove (book,  opid,  GNOME_Evolution_Addressbook_AuthenticationRequired);
		return;
	}
	if (!ebgw->priv->is_writable) {
		e_data_book_respond_remove (book,  opid,  GNOME_Evolution_Addressbook_PermissionDenied);
		return;
	}
	status = e_gw_connection_remove_item (ebgw->priv->cnc, NULL, ebgw->priv->container_id);
	if (status == E_GW_CONNECTION_STATUS_OK) 
		e_data_book_respond_remove (book,  opid, GNOME_Evolution_Addressbook_Success);
	else
		e_data_book_respond_remove (book,  opid, GNOME_Evolution_Addressbook_OtherError);
	unlink (e_file_cache_get_filename (E_FILE_CACHE (ebgw->priv->cache)));
}


static char *
e_book_backend_groupwise_get_static_capabilities (EBookBackend *backend)
{
	EBookBackendGroupwise *ebgw;
	
  
	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	if (ebgw->priv->is_writable)
		return g_strdup("net,bulk-removes,do-initial-query,contact-lists");
	else 
		return g_strdup("net,bulk-removes,contact-lists");
}
static void 
e_book_backend_groupwise_get_supported_auth_methods (EBookBackend *backend, EDataBook *book, guint32 opid)
{
	GList *auth_methods = NULL;
	char *auth_method;
	
	auth_method =  g_strdup_printf ("plain/password");
	auth_methods = g_list_append (auth_methods, auth_method);
	e_data_book_respond_get_supported_auth_methods (book,
							opid,
							GNOME_Evolution_Addressbook_Success,
							auth_methods);  
	g_free (auth_method);
	g_list_free (auth_methods);
	
}

static void 
e_book_backend_groupwise_set_mode (EBookBackend *backend, int mode)
{
	EBookBackendGroupwise *bg;
	
	bg = E_BOOK_BACKEND_GROUPWISE (backend);
	bg->priv->mode = mode;
	if (e_book_backend_is_loaded (backend)) {
		if (mode == GNOME_Evolution_Addressbook_MODE_LOCAL) {
			e_book_backend_notify_writable (backend, FALSE);
			e_book_backend_notify_connection_status (backend, FALSE);
			if (bg->priv->cnc) {
				g_object_unref (bg->priv->cnc);
				bg->priv->cnc=NULL;
			}
		}
		else if (mode == GNOME_Evolution_Addressbook_MODE_REMOTE) {
			e_book_backend_notify_writable (backend, TRUE);
			e_book_backend_notify_connection_status (backend, TRUE);
			e_book_backend_notify_auth_required (backend);
		}
	}
}

/**
 * e_book_backend_groupwise_new:
 */
EBookBackend *
e_book_backend_groupwise_new (void)
{
	EBookBackendGroupwise *backend;
                                                                                                                             
	backend = g_object_new (E_TYPE_BOOK_BACKEND_GROUPWISE, NULL);
                                                                                                       
	return E_BOOK_BACKEND (backend);
}

static void
e_book_backend_groupwise_dispose (GObject *object)
{
	EBookBackendGroupwise *bgw;
                                                                                                                             
	bgw = E_BOOK_BACKEND_GROUPWISE (object);
                                                                                                                             
	if (bgw->priv) {
		if (bgw->priv->uri) {
			g_free (bgw->priv->uri);
			bgw->priv->uri = NULL;
		}

		if (bgw->priv->original_uri) {
			g_free (bgw->priv->original_uri);
			bgw->priv->original_uri = NULL;
		}

		if (bgw->priv->cnc) {
			g_object_unref (bgw->priv->cnc);
			bgw->priv->cnc = NULL;
		}
		if (bgw->priv->container_id) {
			g_free (bgw->priv->container_id);
			bgw->priv->container_id = NULL;
		}
		if (bgw->priv->book_name) {
			g_free (bgw->priv->book_name);
			bgw->priv->book_name = NULL;
		}
		if (bgw->priv->cache) {
			g_object_unref (bgw->priv->cache);
		}
		if (bgw->priv->use_ssl) {
			g_free (bgw->priv->use_ssl);
		}
		g_free (bgw->priv);
		bgw->priv = NULL;
	}
                                                                                                                             
	G_OBJECT_CLASS (e_book_backend_groupwise_parent_class)->dispose (object);
}
                                                                                                                            
static void
e_book_backend_groupwise_class_init (EBookBackendGroupwiseClass *klass)
{
  

	GObjectClass  *object_class = G_OBJECT_CLASS (klass);
	EBookBackendClass *parent_class;


	e_book_backend_groupwise_parent_class = g_type_class_peek_parent (klass);

	parent_class = E_BOOK_BACKEND_CLASS (klass);

	/* Set the virtual methods. */
	parent_class->load_source             = e_book_backend_groupwise_load_source;
	parent_class->get_static_capabilities = e_book_backend_groupwise_get_static_capabilities;

	parent_class->create_contact          = e_book_backend_groupwise_create_contact;
	parent_class->remove_contacts         = e_book_backend_groupwise_remove_contacts;
	parent_class->modify_contact          = e_book_backend_groupwise_modify_contact;
	parent_class->get_contact             = e_book_backend_groupwise_get_contact;
	parent_class->get_contact_list        = e_book_backend_groupwise_get_contact_list;
	parent_class->start_book_view         = e_book_backend_groupwise_start_book_view;
	parent_class->stop_book_view          = e_book_backend_groupwise_stop_book_view;
	parent_class->get_changes             = e_book_backend_groupwise_get_changes;
	parent_class->authenticate_user       = e_book_backend_groupwise_authenticate_user;
	parent_class->get_required_fields     = e_book_backend_groupwise_get_required_fields;
	parent_class->get_supported_fields    = e_book_backend_groupwise_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_groupwise_get_supported_auth_methods;
	parent_class->cancel_operation        = e_book_backend_groupwise_cancel_operation;
	parent_class->remove                  = e_book_backend_groupwise_remove;
	parent_class->set_mode                = e_book_backend_groupwise_set_mode;
	object_class->dispose                 = e_book_backend_groupwise_dispose;
}

static void
e_book_backend_groupwise_init (EBookBackendGroupwise *backend)
{
	EBookBackendGroupwisePrivate *priv;
                                                                                                                             
	priv= g_new0 (EBookBackendGroupwisePrivate, 1);
	priv->is_writable = TRUE;
	priv->is_cache_ready = FALSE;
	priv->marked_for_offline = FALSE;
	priv->use_ssl = NULL;
	priv->cache=NULL;
	priv->original_uri = NULL;
       	backend->priv = priv;
	
}


/**
 * e_book_backend_groupwise_get_type:
 */
GType
e_book_backend_groupwise_get_type (void)
{
	static GType type = 0;
                                                                                                                             
	if (! type) {
		GTypeInfo info = {
			sizeof (EBookBackendGroupwiseClass),
			NULL, /* base_class_init */
			NULL, /* base_class_finalize */
			(GClassInitFunc)  e_book_backend_groupwise_class_init,
			NULL, /* class_finalize */
			NULL, /* class_data */
			sizeof (EBookBackendGroupwise),
			0,    /* n_preallocs */
			(GInstanceInitFunc) e_book_backend_groupwise_init
		};
                                                                                                                             
		type = g_type_register_static (E_TYPE_BOOK_BACKEND, "EBookBackendGroupwise", &info, 0);
	}
                                                                                                                             
	return type;
}

	
