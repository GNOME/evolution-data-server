/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *   Sivaiah Nallagatla (snallagatla@novell.com)
 *
 * Copyright 2004, Novell, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif



#include <libebook/e-contact.h>
                                                                                                                             
#include <libedata-book/e-book-backend-sexp.h>
#include <libedata-book/e-book-backend-summary.h>
#include <libedata-book/e-data-book.h>
#include <libedata-book/e-data-book-view.h>
#include "e-book-backend-groupwise.h"
#include  <e-gw-connection.h>
#include <e-gw-item.h>

static EBookBackendClass *e_book_backend_groupwise_parent_class;
                                                                                                                             
struct _EBookBackendGroupwisePrivate {
	EGwConnection *cnc; 
	char *uri;
	char *container_id;
	char *book_name;
	gboolean only_if_exists;

};

#define ELEMENT_TYPE_SIMPLE 0x01
#define ELEMENT_TYPE_COMPLEX 0x02 /* fields which require explicit functions to set values into EContact and EGwItem */

static void populate_emails (EContact *contact, gpointer data);
static void populate_full_name (EContact *contact, gpointer data);
static void populate_contact_members (EContact *contact, gpointer data);
static void set_full_name_in_gw_item (EGwItem *item, gpointer data);
static void  set_emails_in_gw_item (EGwItem *item, gpointer data);
static void set_members_in_gw_item (EGwItem *item, gpointer data);
static void populate_birth_date (EContact *contact, gpointer data);
static void set_birth_date_in_gw_item (EGwItem *item, gpointer data);
static void populate_address (EContact *contact, gpointer data);
static void set_address_in_gw_item (EGwItem *item, gpointer data);
static void populate_ims (EContact *contact, gpointer data);
static void set_ims_in_gw_item (EGwItem *item, gpointer data);
static void set_full_name_changes (EGwItem *new_item, EGwItem *old_item);
static void set_birth_date_changes  (EGwItem *new_item, EGwItem *old_item);
static void set_emails_changes (EGwItem *new_item, EGwItem *old_item);
static void set_members_changes (EGwItem *new_item, EGwItem *old_item);
static void set_address_changes (EGwItem *new_item, EGwItem *old_item);
static void set_im_changes (EGwItem *new_item, EGwItem *old_item);

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
	{ E_CONTACT_EMAIL, ELEMENT_TYPE_COMPLEX, "members", populate_contact_members, set_members_in_gw_item, set_members_changes},
	{ E_CONTACT_ADDRESS_HOME, ELEMENT_TYPE_COMPLEX, "Home", populate_address, set_address_in_gw_item, set_address_changes },
	{ E_CONTACT_IM_AIM, ELEMENT_TYPE_COMPLEX, "ims", populate_ims, set_ims_in_gw_item, set_im_changes },
	{ E_CONTACT_EMAIL_1, ELEMENT_TYPE_COMPLEX, "email", populate_emails, set_emails_in_gw_item, set_emails_changes }
}; 


static int num_mappings = sizeof(mappings) / sizeof(mappings [0]);



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

		address = (IMAddress *) (im_list->data);
		if (address->service == NULL) {
			continue;
		}
		if (g_str_equal (address->service, "icq"))
			icq_list = g_list_append (icq_list, address->address);
		else if (g_str_equal (address->service, "aim"))
			aim_list = g_list_append (aim_list, address->address);
		else if ( g_str_equal (address->service, "msn"))
			msn_list = g_list_append (msn_list, address->address);
		else if (g_str_equal (address->service, "yahoo"))
			yahoo_list = g_list_append (yahoo_list, address->address);
		else if (g_str_equal (address->service, "jabber"))
			jabber_list = g_list_append (jabber_list, address->address);
		else if (g_str_equal (address->service, "nov"))
			groupwise_list = g_list_append (groupwise_list, address->address);
	}
     
	e_contact_set (contact, E_CONTACT_IM_AIM, aim_list);
	e_contact_set (contact, E_CONTACT_IM_JABBER, jabber_list);
	e_contact_set (contact, E_CONTACT_IM_ICQ, icq_list);
	e_contact_set (contact, E_CONTACT_IM_YAHOO, yahoo_list);
	e_contact_set (contact, E_CONTACT_IM_MSN, msn_list);
	e_contact_set (contact, E_CONTACT_IM_GROUPWISE, groupwise_list);
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

	} else if (!new_item && old_item) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "ims", old_ims);
	} else if (new_item && !old_item) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "ims", new_ims);
	}
	
}


static void 
copy_postal_address_to_contact_address ( EContactAddress *contact_addr, PostalAddress *address)
{
	contact_addr->address_format = NULL;
	contact_addr->po = NULL;
	contact_addr->street = g_strdup (address->street_address);
	contact_addr->locality = g_strdup (address->location);
	contact_addr->region = g_strdup (address->state);
	contact_addr->code = g_strdup (address->postal_code);
	contact_addr->country = g_strdup (address->country);
}

static void 
copy_contact_address_to_postal_address (PostalAddress *address, EContactAddress *contact_addr)
{
	address->street_address = g_strdup (contact_addr->street);
	address->location = g_strdup (contact_addr->locality);
	address->state = g_strdup (contact_addr->region);
	address->postal_code = g_strdup (contact_addr->code);
	address->country = g_strdup (contact_addr->country);
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
	}
  
	address = e_gw_item_get_address (item, "Office");
	if (address) {
		contact_addr = g_new0(EContactAddress, 1);
		copy_postal_address_to_contact_address (contact_addr, address);
		e_contact_set (contact, E_CONTACT_ADDRESS_WORK, contact_addr);
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
	}
		
	contact_address = e_contact_get (contact, E_CONTACT_ADDRESS_WORK);
	if (contact_address) {
		address = g_new0(PostalAddress, 1);
		copy_contact_address_to_postal_address (address, contact_address);
		e_gw_item_set_address (item, "Office", address);
	}
	

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
	old_postal_address = e_gw_item_get_address (new_item, address_type);
	
	if (new_postal_address && old_postal_address) {
		s1 = new_postal_address->street_address;
		s2 = old_postal_address->street_address;
		if (!s1 && s2)
			delete_postal_address->street_address = s2;
		else if (s1 && s2)
			update_postal_address->street_address = s1;
		
		s1 =  new_postal_address->location;
		s2 = old_postal_address->location;
		if (!s1 && s2)
			delete_postal_address->location = s2;
		else if (s1 && s2)
			update_postal_address->location = s1;

		s1 =  new_postal_address->state;
		s2 = old_postal_address->state;
		if (!s1 && s2)
			delete_postal_address->state = s2;
		else if (s1 && s2)
			update_postal_address->state = s1;
		s1 =  new_postal_address->postal_code;
		s2 = old_postal_address->postal_code;
		if (!s1 && s2)
			delete_postal_address->postal_code = s2;
		else if (s1 && s2)
			update_postal_address->postal_code = s1;

		s1 =  new_postal_address->country;
		s2 =  old_postal_address->country;
		if (!s1 && s2)
			delete_postal_address->country = s2;
		else if (s1 && s2)
			update_postal_address->country = s1;

		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_UPDATE, address_type, update_postal_address);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, address_type, delete_postal_address);
		
	} else if (!new_postal_address && old_postal_address) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, address_type, old_postal_address);
	} else if (new_postal_address && !old_postal_address) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, address_type, new_postal_address);
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
  
	item = E_GW_ITEM (data);
	char *value ;
	EContactDate *date;
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

	contact = E_CONTACT (data);
	date = e_contact_get (contact, E_CONTACT_BIRTH_DATE);
	if (date)
		e_gw_item_set_field_value (item, "birthday", e_contact_date_to_string (date));


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
	contact = E_CONTACT (data);
	int i;

	email_list = NULL;
	for (i =0 ; i < 3; i++) {
		email = e_contact_get (contact, email_fields[i]);
		if(email)
			email_list = g_list_append (email_list, email);
	}
	e_gw_item_set_email_list (item, email_list);

}  

static void 
set_emails_changes (EGwItem *new_item, EGwItem *old_item)
{
	GList *old_email_list;
	GList *new_email_list;
	GList *temp, *old_emails_copy, *added_emails = NULL;
	gboolean emails_matched;
	char *email1, *email2;
	old_email_list = e_gw_item_get_email_list (old_item);
	new_email_list = e_gw_item_get_email_list (new_item);
	if (old_email_list && new_email_list) {
		old_emails_copy = g_list_copy (old_email_list);
		for ( ; new_email_list != NULL; new_email_list = g_list_next (new_email_list)) {
			
			email1 = new_email_list->data;
			temp = old_email_list;
			emails_matched = FALSE;
			for(; temp != NULL; temp = g_list_next (temp)) {
				email2 = temp->data;
				if ( g_str_equal (email1, email2)) {
					emails_matched = TRUE;
					old_emails_copy = g_list_remove (old_emails_copy, email2);
					break;
				}
				
			}
			if (!emails_matched)
				added_emails = g_list_append (added_emails, email1);
		}
			     
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "email", added_emails);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "email", old_emails_copy);

	} else if (!new_email_list && old_email_list) {
		e_gw_item_set_change (new_item,  E_GW_ITEM_CHANGE_TYPE_DELETE, "eamil", old_email_list);
	} else if (new_email_list && !old_email_list) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "email", new_email_list);
	}
	
}
 
static void 
populate_full_name (EContact *contact, gpointer data)
{
	EGwItem *item;
	item = E_GW_ITEM(data);
	FullName  *full_name ;
	char *full_name_string;

	full_name = e_gw_item_get_full_name (item);
	if (full_name) {
		full_name_string = g_strconcat ( (full_name->name_prefix == NULL) ? "\0" : full_name->name_prefix, " ",
			    (full_name->first_name == NULL) ? "\0" :    full_name->first_name, " ",
			    (full_name->middle_name == NULL) ? "\0" : full_name->middle_name, " ",
			    full_name->last_name == NULL ? "\0" : full_name->last_name, " ",
			    (full_name->name_suffix == NULL ) ? "\0" : full_name->name_suffix, NULL);
		full_name_string = g_strchug (full_name_string);
		if (!g_str_equal (full_name_string, "\0"))
			e_contact_set (contact, E_CONTACT_FULL_NAME, full_name_string);
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
		  
			full_name->name_prefix = g_strdup (contact_name->prefixes);
			full_name->first_name =  g_strdup (contact_name->given);
			full_name->middle_name = g_strdup (contact_name->additional);
			full_name->last_name = g_strdup (contact_name->family);
			full_name->name_suffix = g_strdup (contact_name->suffixes);
			e_contact_name_free (contact_name);
		}
		  
		e_gw_item_set_full_name (item, full_name);
	}
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
			delete_full_name->name_prefix = s2;
		else if (s1 && s2)
			update_full_name->name_prefix = s1;
		s1 = new_full_name->first_name;
		s2  = old_full_name->first_name;
		if(!s1 && s2)
			delete_full_name->first_name = s2;
		else if (s1 && s2)
			update_full_name->first_name = s1;
		s1 = new_full_name->middle_name;
		s2  = old_full_name->middle_name;
		if(!s1 && s2)
			delete_full_name->middle_name = s2;
		else if (s1 && s2)
			update_full_name->middle_name = s1;
		
		s1 = new_full_name->last_name;
		s2 = old_full_name->last_name;
		if(!s1 && s2)
			delete_full_name->last_name = s2;
		else if (s1 && s2)
			update_full_name->last_name = s1;
		s1 = new_full_name->name_suffix;
		s2  = old_full_name->name_suffix;
		if(!s1 && s2)
			delete_full_name->name_suffix = s2;
		else if (s1 && s2)
			update_full_name->name_suffix = s1;
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_UPDATE,"full_name",  new_full_name);
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE,"full_name",  delete_full_name);
	
	} else if (!new_full_name && old_full_name) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_DELETE, "full_name", old_full_name);
	} else if (new_full_name && !old_full_name) {
		e_gw_item_set_change (new_item, E_GW_ITEM_CHANGE_TYPE_ADD, "full_name", new_full_name);
	}
		

}
static void 
populate_contact_members (EContact *contact, gpointer data)
{
	EGwItem *item;
	item = E_GW_ITEM(data);
	e_contact_set (contact, E_CONTACT_EMAIL, e_gw_item_get_member_list(item));

}
static void
set_members_in_gw_item (EGwItem  *item, gpointer data)
{
  
	EContact *contact;
	contact = E_CONTACT (data);
	GList*  members ;
	members = e_contact_get (contact, E_CONTACT_EMAIL);
	e_gw_item_set_member_list (item, members);
  
}

static void 
set_members_changes (EGwItem *new_item, EGwItem *old_item)
{

}

static void 
fill_contact_from_gw_item (EContact *contact, EGwItem *item)
{

	char* value;
	int element_type;
	int i;

	e_contact_set (contact, E_CONTACT_IS_LIST, e_gw_item_get_item_type (item) == E_GW_ITEM_TYPE_GROUP ? TRUE: FALSE);
	for ( i = 0; i < num_mappings; i++) {
		element_type = mappings[i].element_type;
		if(element_type == ELEMENT_TYPE_SIMPLE)
			{
				value = e_gw_item_get_field_value (item, mappings[i].element_name);
				if(value != NULL)
					e_contact_set (contact, mappings[i].field_id, value);
	
			} else if (element_type == ELEMENT_TYPE_COMPLEX) {
				mappings[i].populate_contact_func(contact, item);
			}
    
	}
}

static void
e_book_backend_groupwise_create_contact (EBookBackend *backend,
					 EDataBook *book,
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
	if (egwb->priv->cnc == NULL) {
		e_data_book_respond_create(book, GNOME_Evolution_Addressbook_OtherError, NULL);
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
			if (value != NULL)
				e_gw_item_set_field_value (item, mappings[i].element_name, value);
		} else if (element_type == ELEMENT_TYPE_COMPLEX) {
			mappings[i].set_value_in_gw_item (item, contact);
		}
     
    
	}
	status = e_gw_connection_create_item (egwb->priv->cnc, item, &id);  
	if (status == E_GW_CONNECTION_STATUS_OK) {
		e_contact_set (contact, E_CONTACT_UID, id);
		e_data_book_respond_create(book,  GNOME_Evolution_Addressbook_Success, contact);
		g_free(id);
	}
	else {
		e_data_book_respond_create(book, GNOME_Evolution_Addressbook_OtherError, NULL);
	}
  
}

static void
e_book_backend_groupwise_remove_contacts (EBookBackend *backend,
					  EDataBook    *book,
					  GList *id_list    )
{
  
	char *id;
 
	EBookBackendGroupwise *ebgw;
	GList *deleted_ids = NULL;

	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	if (ebgw->priv->cnc == NULL) {
		e_data_book_respond_remove_contacts (book, GNOME_Evolution_Addressbook_OtherError, NULL);
		return;
	}
	/* FIXME use removeItems method so that all contacts can be deleted in a single SOAP interaction */
	
	for ( ; id_list != NULL; id_list = g_list_next (id_list)) {
		id = (char*) id_list->data;
		if (e_gw_connection_remove_item (ebgw->priv->cnc, ebgw->priv->container_id, id) == E_GW_CONNECTION_STATUS_OK) 
			deleted_ids =  g_list_append (deleted_ids, id);
	}
	e_data_book_respond_remove_contacts (book,
					     GNOME_Evolution_Addressbook_Success,  deleted_ids);
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

				mappings[i].set_changes(new_item, old_item);
			}
    
	}


}

static void
e_book_backend_groupwise_modify_contact (EBookBackend *backend,
					 EDataBook    *book,
					 const char *vcard    )
{	
	EContact *contact;
	EBookBackendGroupwise *egwb;
	char *id;
	int status;
	EGwItem *new_item;
	EGwItem *old_item;
	int element_type;
	char* value;
	int i;
	
	egwb = E_BOOK_BACKEND_GROUPWISE (backend);
	if (egwb->priv->cnc == NULL) {
		e_data_book_respond_modify (book, GNOME_Evolution_Addressbook_OtherError, NULL);
		return;
	}
	contact = e_contact_new_from_vcard(vcard);
	new_item = e_gw_item_new_empty ();

	for (i = 0; i < num_mappings; i++) {
		element_type = mappings[i].element_type;
		if (element_type == ELEMENT_TYPE_SIMPLE)  {
			value =  e_contact_get(contact, mappings[i].field_id);
			if (value != NULL)
				e_gw_item_set_field_value (new_item, mappings[i].element_name, value);
		} else if (element_type == ELEMENT_TYPE_COMPLEX) {
			mappings[i].set_value_in_gw_item (new_item, contact);
		}
   
	}

	id = e_contact_get (contact, E_CONTACT_UID);
	old_item = NULL;
	status = e_gw_connection_get_item (egwb->priv->cnc, egwb->priv->container_id, id,  &old_item);
	if ((status != E_GW_CONNECTION_STATUS_OK) || (old_item == NULL)) {
		e_data_book_respond_modify (book, GNOME_Evolution_Addressbook_OtherError, NULL);
		return;
	}
	set_changes_in_gw_item (new_item, old_item);
	e_gw_item_set_item_type (new_item, e_gw_item_get_item_type (old_item));
	status = e_gw_connection_modify_item (egwb->priv->cnc, id, new_item);
	if (status == E_GW_CONNECTION_STATUS_OK) 
		e_data_book_respond_modify (book, GNOME_Evolution_Addressbook_Success, contact);
	else 
		e_data_book_respond_modify (book, GNOME_Evolution_Addressbook_OtherError, NULL);
	g_object_unref (new_item);
	g_object_ref (old_item);
	g_object_unref (contact);
		

	
}

static void
e_book_backend_groupwise_get_contact (EBookBackend *backend,
				      EDataBook    *book,
				      const char *id   )
{
	EBookBackendGroupwise *gwb ;
	int status ;
	EGwItem *item;
	EContact *contact;
	char *vcard;

	gwb =  E_BOOK_BACKEND_GROUPWISE (backend);
	if (gwb->priv->cnc == NULL) {
		e_data_book_respond_get_contact (book, GNOME_Evolution_Addressbook_OtherError, NULL);
		return;
	}
  	status = e_gw_connection_get_item (gwb->priv->cnc, gwb->priv->container_id, id,  &item);
	if (status == E_GW_CONNECTION_STATUS_OK) {
		if (item) {
			contact = e_contact_new ();
			fill_contact_from_gw_item (contact, item);
			vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
			e_data_book_respond_get_contact (book, GNOME_Evolution_Addressbook_Success, vcard);
			g_free (vcard);
			g_object_unref (contact);
			g_object_unref (item);
			return;
		}
    
	}
	e_data_book_respond_get_contact (book, GNOME_Evolution_Addressbook_OtherError, "");  
	
}




static void
e_book_backend_groupwise_get_contact_list (EBookBackend *backend,
					   EDataBook    *book,
					   const char *query )
{
  
	GList *vcard_list;
	int status;
	GList *gw_items;
	EContact *contact;
	EBookBackendGroupwise *egwb;
	gboolean search_needed;
	EBookBackendSExp *card_sexp = NULL;

	egwb = E_BOOK_BACKEND_GROUPWISE (backend);
	vcard_list = NULL;
	gw_items = NULL;

	if (egwb->priv->cnc == NULL) {

		e_data_book_respond_get_contact_list (book, GNOME_Evolution_Addressbook_OtherError, NULL);
		return;
	}
	/* FIXME currently contacts received form Gw Server are checked for match against the query.
	   Instead of this Groupwise filter should be formed frm the query and given as input to get items calls
	   to make things more efficient */

	search_needed = TRUE;
	if ( g_str_equal(query, "(contains \"x-evolution-any-field\" \"\")"))
		search_needed = FALSE;

	card_sexp = e_book_backend_sexp_new (query);
	if (!card_sexp) {
		e_data_book_respond_get_contact_list (book, GNOME_Evolution_Addressbook_ContactNotFound,
						      vcard_list);
	}

	status = e_gw_connection_get_items (egwb->priv->cnc, egwb->priv->container_id, NULL, &gw_items);
	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_data_book_respond_get_contact_list (book, GNOME_Evolution_Addressbook_OtherError,
						      NULL);
		return;
	}
	for (; gw_items != NULL; gw_items = g_list_next(gw_items)) { 
		contact = e_contact_new ();
		fill_contact_from_gw_item (contact, E_GW_ITEM (gw_items->data));
		if ( (!search_needed) || e_book_backend_sexp_match_contact (card_sexp, contact));
		vcard_list = g_list_append (vcard_list, e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30));
		g_object_unref (contact);
		g_object_unref (gw_items->data);
    	}
  
	e_data_book_respond_get_contact_list (book, GNOME_Evolution_Addressbook_Success,
					      vcard_list);
  
}

 
static void
e_book_backend_groupwise_start_book_view (EBookBackend  *backend,
					  EDataBookView *book_view)
{

	int status;
	GList *gw_items;
	EContact *contact;
	EBookBackendGroupwise *gwb = E_BOOK_BACKEND_GROUPWISE (backend);
	gw_items = NULL;
    
	if (gwb->priv->cnc == NULL) {
		e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_OtherError);
		return;
	}
		
	e_data_book_view_notify_status_message (book_view, "Searching...");
	status = e_gw_connection_get_items (gwb->priv->cnc, gwb->priv->container_id, NULL, &gw_items);
    
	if (status != E_GW_CONNECTION_STATUS_OK) {
		e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_OtherError);
		return;
	}
	for (; gw_items != NULL; gw_items = g_list_next(gw_items)) { 
		contact = e_contact_new ();
		fill_contact_from_gw_item (contact, E_GW_ITEM (gw_items->data));
		printf ("contact = %s\n", e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30)); 
		e_data_book_view_notify_update (book_view, contact);
		g_object_unref(contact);
		g_object_unref (gw_items->data);
      
	}
    
	e_data_book_view_notify_complete (book_view, GNOME_Evolution_Addressbook_Success);
    
}     
  
static void
e_book_backend_groupwise_stop_book_view (EBookBackend  *backend,
					 EDataBookView *book_view)
{
	/* FIXME : provide implmentation */
}

static void
e_book_backend_groupwise_get_changes (EBookBackend *backend,
				      EDataBook    *book,
				      const char *change_id  )
{

	/* FIXME : provide implmentation */

       
}

static void
e_book_backend_groupwise_authenticate_user (EBookBackend *backend,
					    EDataBook    *book,
					    const char *user,
					    const char *passwd,
					    const char *auth_method)
{
	EBookBackendGroupwise *ebgw;
	EBookBackendGroupwisePrivate *priv;
	char *id;
	int status;
	gboolean is_writable;

	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	priv = ebgw->priv;
  
	priv->cnc = e_gw_connection_new (priv->uri, user, passwd);
	if (priv->cnc == NULL) {
		e_data_book_respond_authenticate_user (book,  GNOME_Evolution_Addressbook_OtherError);
		return;
	}
	printf ("authenitcating user\n");
	id = NULL;
	is_writable = FALSE;
	status = e_gw_connection_get_address_book_id (priv->cnc,  priv->book_name, &id, &is_writable); 
	if (status == E_GW_CONNECTION_STATUS_OK) {
		if ( (id == NULL) && !priv->only_if_exists ) {
			printf ("creating address book %s\n", priv->book_name);
			status = e_gw_connection_create_book (priv->cnc, priv->book_name,  &id);
			if (status != E_GW_CONNECTION_STATUS_OK ) {
				e_data_book_respond_authenticate_user (book,  GNOME_Evolution_Addressbook_OtherError);
				return;
			}
     
		}

	}
	if (id != NULL) {
		priv->container_id = g_strdup (id);
		g_free(id);
		e_book_backend_set_is_writable (backend, is_writable);
		e_data_book_report_writable (book, is_writable);
		e_data_book_respond_authenticate_user (book,  GNOME_Evolution_Addressbook_Success); 
   
	} else {
		e_book_backend_set_is_loaded (backend, FALSE);
		e_data_book_respond_authenticate_user (book,  GNOME_Evolution_Addressbook_OtherError);
	}
  
}

static void
e_book_backend_groupwise_get_supported_fields (EBookBackend *backend,
					       EDataBook    *book )
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
	e_data_book_respond_get_supported_fields (book,
						  GNOME_Evolution_Addressbook_Success,
						  fields);
 
}
  

static GNOME_Evolution_Addressbook_CallStatus
e_book_backend_groupwise_load_source (EBookBackend           *backend,
				      ESource                *source,
				      gboolean                only_if_exists)
{
	EBookBackendGroupwise *ebgw;
	EBookBackendGroupwisePrivate *priv;
	const char *book_name;
	const char *uri;

	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	priv = ebgw->priv;
	uri =  e_source_get_uri (source);
	if(uri == NULL)
		return  GNOME_Evolution_Addressbook_OtherError;
	priv->uri = g_strconcat ("http://", uri +12, NULL );
	priv->only_if_exists = only_if_exists;
	book_name = e_source_peek_name(source);
	if(book_name == NULL)
		return  GNOME_Evolution_Addressbook_OtherError;
	priv->book_name = g_strdup (book_name);
	printf ("is already existing %d\n", priv->only_if_exists);
	e_book_backend_set_is_loaded (E_BOOK_BACKEND (backend), TRUE);
	e_book_backend_set_is_writable (E_BOOK_BACKEND(backend), FALSE);  
	return GNOME_Evolution_Addressbook_Success;
}

static void
e_book_backend_groupwise_remove (EBookBackend *backend,
				 EDataBook        *book)
{
	EBookBackendGroupwise *ebgw;
	int status;
  
	ebgw = E_BOOK_BACKEND_GROUPWISE (backend);
	if (ebgw->priv->cnc == NULL) {
		e_data_book_respond_remove (book,  GNOME_Evolution_Addressbook_OtherError);
		return;
	}	
	status = e_gw_connection_remove_item (ebgw->priv->cnc, NULL, ebgw->priv->container_id);
	if (status == E_GW_CONNECTION_STATUS_OK) 
		e_data_book_respond_remove (book,  GNOME_Evolution_Addressbook_Success);
	else
		e_data_book_respond_remove (book,  GNOME_Evolution_Addressbook_OtherError);
    
}


static char *
e_book_backend_groupwise_get_static_capabilities (EBookBackend *backend)
{
	return g_strdup("net,bulk-removes,do-initial-query");
}
static void 
e_book_backend_groupwise_get_supported_auth_methods (EBookBackend *backend, EDataBook *book)
{
	/*FIXME  provide implementation*/  
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
	parent_class->get_supported_fields    = e_book_backend_groupwise_get_supported_fields;
	parent_class->get_supported_auth_methods = e_book_backend_groupwise_get_supported_auth_methods;
	parent_class->remove                  = e_book_backend_groupwise_remove;
	object_class->dispose                 = e_book_backend_groupwise_dispose;
}

static void
e_book_backend_groupwise_init (EBookBackendGroupwise *backend)
{
	EBookBackendGroupwisePrivate *priv;
                                                                                                                             
	priv= g_new0 (EBookBackendGroupwisePrivate, 1);
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

	
