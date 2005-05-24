/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* e-contact.c
 *
 * Copyright (C) 2003 Ximian, Inc.
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
 * Author: Chris Toshok (toshok@ximian.com)
 */

#include <config.h>

#include <glib.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <glib/gi18n-lib.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include "e-contact.h"
#include "e-book.h"
#include "e-name-western.h"

#define d(x)

struct _EContactPrivate {
	char *cached_strings [E_CONTACT_FIELD_LAST];
};

#define E_CONTACT_FIELD_TYPE_STRING       0x00000001   /* used for simple single valued attributes */
/*E_CONTACT_FIELD_TYPE_FLOAT*/
#define E_CONTACT_FIELD_TYPE_LIST         0x00000002   /* used for multivalued single attributes - the elements are of type char* */
#define E_CONTACT_FIELD_TYPE_MULTI        0x00000004   /* used for multivalued attributes - the elements are of type EVCardAttribute */
#define E_CONTACT_FIELD_TYPE_GETSET       0x00000008   /* used for attributes that need custom handling for getting/setting */
#define E_CONTACT_FIELD_TYPE_STRUCT       0x00000010   /* used for structured types (N and ADR properties, in particular) */
#define E_CONTACT_FIELD_TYPE_BOOLEAN      0x00000020   /* used for boolean types (WANTS_HTML) */

#define E_CONTACT_FIELD_TYPE_SYNTHETIC    0x10000000   /* used when there isn't a corresponding vcard field (such as email_1) */
#define E_CONTACT_FIELD_TYPE_LIST_ELEM    0x20000000   /* used when a synthetic attribute is a numbered list element */
#define E_CONTACT_FIELD_TYPE_MULTI_ELEM   0x40000000   /* used when we're looking for the nth attribute where more than 1 can be present in the vcard */
#define E_CONTACT_FIELD_TYPE_ATTR_TYPE    0x80000000   /* used when a synthetic attribute is flagged with a TYPE= that we'll be looking for */

typedef struct {
	guint32 t;

	EContactField field_id;
	const char *vcard_field_name;
	const char *field_name;      /* non translated */
	const char *pretty_name;     /* translated */
	
	gboolean read_only;

	int list_elem;
	const char *attr_type1;
	const char *attr_type2;

	void* (*struct_getter)(EContact *contact, EVCardAttribute *attribute);
	void (*struct_setter)(EContact *contact, EVCardAttribute *attribute, void *data);

	GType (*boxed_type_getter) (void);
} EContactFieldInfo;

static void* photo_getter (EContact *contact, EVCardAttribute *attr);
static void photo_setter (EContact *contact, EVCardAttribute *attr, void *data);
static void* fn_getter (EContact *contact, EVCardAttribute *attr);
static void fn_setter (EContact *contact, EVCardAttribute *attr, void *data);
static void* n_getter (EContact *contact, EVCardAttribute *attr);
static void n_setter (EContact *contact, EVCardAttribute *attr, void *data);
static void* adr_getter (EContact *contact, EVCardAttribute *attr);
static void adr_setter (EContact *contact, EVCardAttribute *attr, void *data);
static void* date_getter (EContact *contact, EVCardAttribute *attr);
static void date_setter (EContact *contact, EVCardAttribute *attr, void *data);
static void* cert_getter (EContact *contact, EVCardAttribute *attr);
static void cert_setter (EContact *contact, EVCardAttribute *attr, void *data);

#define STRING_FIELD(id,vc,n,pn,ro)  { E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro) }
#define BOOLEAN_FIELD(id,vc,n,pn,ro)  { E_CONTACT_FIELD_TYPE_BOOLEAN, (id), (vc), (n), (pn), (ro) }
#define LIST_FIELD(id,vc,n,pn,ro)      { E_CONTACT_FIELD_TYPE_LIST, (id), (vc), (n), (pn), (ro) }
#define MULTI_LIST_FIELD(id,vc,n,pn,ro) { E_CONTACT_FIELD_TYPE_MULTI, (id), (vc), (n), (pn), (ro) }
#define GETSET_FIELD(id,vc,n,pn,ro,get,set)    { E_CONTACT_FIELD_TYPE_STRING | E_CONTACT_FIELD_TYPE_GETSET, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, (get), (set) }
#define STRUCT_FIELD(id,vc,n,pn,ro,get,set,ty)    { E_CONTACT_FIELD_TYPE_STRUCT | E_CONTACT_FIELD_TYPE_GETSET, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, (get), (set), (ty) }
#define SYNTH_STR_FIELD(id,n,pn,ro)  { E_CONTACT_FIELD_TYPE_STRING | E_CONTACT_FIELD_TYPE_SYNTHETIC, (id), NULL, (n), (pn), (ro) }
#define LIST_ELEM_STR_FIELD(id,vc,n,pn,ro,nm) { E_CONTACT_FIELD_TYPE_LIST_ELEM | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), (nm) }
#define MULTI_ELEM_STR_FIELD(id,vc,n,pn,ro,nm) { E_CONTACT_FIELD_TYPE_MULTI_ELEM | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), (nm) }
#define ATTR_TYPE_STR_FIELD(id,vc,n,pn,ro,at1,nth) { E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), (nth), (at1), NULL }
#define ATTR_TYPE_GETSET_FIELD(id,vc,n,pn,ro,at1,nth,get,set) { E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_GETSET, (id), (vc), (n), (pn), (ro), (nth), (at1), NULL, (get), (set) }
#define ATTR2_TYPE_STR_FIELD(id,vc,n,pn,ro,at1,at2,nth) { E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), (nth), (at1), (at2) }
#define ATTR_TYPE_STRUCT_FIELD(id,vc,n,pn,ro,at,get,set,ty) { E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_GETSET | E_CONTACT_FIELD_TYPE_STRUCT, (id), (vc), (n), (pn), (ro), 0, (at), NULL, (get), (set), (ty) }

static EContactFieldInfo field_info[] = {
 	STRING_FIELD (E_CONTACT_UID,        EVC_UID,       "id",         N_("Unique ID"),  FALSE),
	STRING_FIELD (E_CONTACT_FILE_AS,    EVC_X_FILE_AS, "file_as",    N_("File Under"),    FALSE),

	/* Name fields */
	/* FN isn't really a structured field - we use a getter/setter
	   so we can set the N property (since evo 1.4 works fine with
	   vcards that don't even have a N attribute.  *sigh*) */
	GETSET_FIELD        (E_CONTACT_FULL_NAME,   EVC_FN,       "full_name",   N_("Full Name"),   FALSE, fn_getter, fn_setter),
	STRUCT_FIELD        (E_CONTACT_NAME,        EVC_N,        "name",        N_("Name"),        FALSE, n_getter, n_setter, e_contact_name_get_type),
	LIST_ELEM_STR_FIELD (E_CONTACT_GIVEN_NAME,  EVC_N,        "given_name",  N_("Given Name"),  FALSE, 1),
	LIST_ELEM_STR_FIELD (E_CONTACT_FAMILY_NAME, EVC_N,        "family_name", N_("Family Name"), FALSE, 0),
	STRING_FIELD        (E_CONTACT_NICKNAME,    EVC_NICKNAME, "nickname",    N_("Nickname"),    FALSE),
	SYNTH_STR_FIELD     (E_CONTACT_NAME_OR_ORG,               "name_or_org", N_("Name or Org"), TRUE),

	/* Address fields */
	MULTI_LIST_FIELD       (E_CONTACT_ADDRESS,       EVC_ADR, "address",       N_("Address List"),  FALSE),
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_ADDRESS_HOME,  EVC_ADR, "address_home",  N_("Home Address"),  FALSE, "HOME",  adr_getter, adr_setter, e_contact_address_get_type),
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_ADDRESS_WORK,  EVC_ADR, "address_work",  N_("Work Address"),  FALSE, "WORK",  adr_getter, adr_setter, e_contact_address_get_type),
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_ADDRESS_OTHER, EVC_ADR, "address_other", N_("Other Address"), FALSE, "OTHER", adr_getter, adr_setter, e_contact_address_get_type),

	ATTR_TYPE_STR_FIELD (E_CONTACT_ADDRESS_LABEL_HOME,  EVC_LABEL, "address_label_home",  N_("Home Address Label"),  FALSE, "HOME", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_ADDRESS_LABEL_WORK,  EVC_LABEL, "address_label_work",  N_("Work Address Label"),  FALSE, "WORK", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_ADDRESS_LABEL_OTHER, EVC_LABEL, "address_label_other", N_("Other Address Label"), FALSE, "OTHER", 0),

	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_ASSISTANT,    EVC_TEL, "assistant_phone",   N_("Assistant Phone"),  FALSE, EVC_X_ASSISTANT, 0),
	ATTR2_TYPE_STR_FIELD (E_CONTACT_PHONE_BUSINESS,     EVC_TEL, "business_phone",    N_("Business Phone"),   FALSE, "WORK", "VOICE",         0),
	ATTR2_TYPE_STR_FIELD (E_CONTACT_PHONE_BUSINESS_2,   EVC_TEL, "business_phone_2",  N_("Business Phone 2"), FALSE, "WORK", "VOICE",         1),
	ATTR2_TYPE_STR_FIELD (E_CONTACT_PHONE_BUSINESS_FAX, EVC_TEL, "business_fax",      N_("Business Fax"),     FALSE, "WORK", "FAX",           0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_CALLBACK,     EVC_TEL, "callback_phone",    N_("Callback Phone"),   FALSE, EVC_X_CALLBACK,  0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_CAR,          EVC_TEL, "car_phone",         N_("Car Phone"),        FALSE, "CAR",                   0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_COMPANY,      EVC_TEL, "company_phone",     N_("Company Phone"),    FALSE, EVC_X_COMPANY,   0),
	ATTR2_TYPE_STR_FIELD (E_CONTACT_PHONE_HOME,         EVC_TEL, "home_phone",        N_("Home Phone"),       FALSE, "HOME", "VOICE",         0),
	ATTR2_TYPE_STR_FIELD (E_CONTACT_PHONE_HOME_2,       EVC_TEL, "home_phone_2",      N_("Home Phone 2"),     FALSE, "HOME", "VOICE",         1),
	ATTR2_TYPE_STR_FIELD (E_CONTACT_PHONE_HOME_FAX,     EVC_TEL, "home_fax",          N_("Home Fax"),         FALSE, "HOME", "FAX",           0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_ISDN,         EVC_TEL, "isdn_phone",        N_("ISDN"),             FALSE, "ISDN",                  0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_MOBILE,       EVC_TEL, "mobile_phone",      N_("Mobile Phone"),     FALSE, "CELL",                  0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_OTHER,        EVC_TEL, "other_phone",       N_("Other Phone"),      FALSE, "VOICE",                 0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_OTHER_FAX,    EVC_TEL, "other_fax",         N_("Other Fax"),        FALSE, "FAX",                   0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_PAGER,        EVC_TEL, "pager",             N_("Pager"),            FALSE, "PAGER",                 0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_PRIMARY,      EVC_TEL, "primary_phone",     N_("Primary Phone"),    FALSE, "PREF",                  0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_RADIO,        EVC_TEL, "radio",             N_("Radio"),            FALSE, EVC_X_RADIO,     0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_TELEX,        EVC_TEL, "telex",             N_("Telex"),            FALSE, EVC_X_TELEX,     0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_TTYTDD,       EVC_TEL, "tty",               N_("TTY"),              FALSE, EVC_X_TTYTDD,    0),
	
	/* Email fields */
	MULTI_LIST_FIELD     (E_CONTACT_EMAIL,      EVC_EMAIL,        "email",      N_("Email List"),      FALSE),
	MULTI_ELEM_STR_FIELD (E_CONTACT_EMAIL_1,    EVC_EMAIL,        "email_1",    N_("Email 1"),         FALSE, 0),
	MULTI_ELEM_STR_FIELD (E_CONTACT_EMAIL_2,    EVC_EMAIL,        "email_2",    N_("Email 2"),         FALSE, 1),
	MULTI_ELEM_STR_FIELD (E_CONTACT_EMAIL_3,    EVC_EMAIL,        "email_3",    N_("Email 3"),         FALSE, 2),
	MULTI_ELEM_STR_FIELD (E_CONTACT_EMAIL_4,    EVC_EMAIL,        "email_4",    N_("Email 4"),         FALSE, 3),
	STRING_FIELD         (E_CONTACT_MAILER,     EVC_MAILER,       "mailer",     N_("Mailer"),          FALSE),
	BOOLEAN_FIELD        (E_CONTACT_WANTS_HTML, EVC_X_WANTS_HTML, "wants_html", N_("Wants HTML Mail"), FALSE),

	/* Instant messaging fields */
	MULTI_LIST_FIELD (E_CONTACT_IM_AIM,       EVC_X_AIM,       "im_aim",       N_("AIM Screen Name List"),    FALSE),
        MULTI_LIST_FIELD (E_CONTACT_IM_GROUPWISE, EVC_X_GROUPWISE, "im_groupwise", N_("GroupWise Id List"),       FALSE),
	MULTI_LIST_FIELD (E_CONTACT_IM_JABBER, 	  EVC_X_JABBER,    "im_jabber",    N_("Jabber Id List"),          FALSE),
	MULTI_LIST_FIELD (E_CONTACT_IM_YAHOO,  	  EVC_X_YAHOO,     "im_yahoo",     N_("Yahoo! Screen Name List"), FALSE),
	MULTI_LIST_FIELD (E_CONTACT_IM_MSN,    	  EVC_X_MSN,       "im_msn",       N_("MSN Screen Name List"),    FALSE),
	MULTI_LIST_FIELD (E_CONTACT_IM_ICQ,    	  EVC_X_ICQ,       "im_icq",       N_("ICQ Id List"),             FALSE),
 
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_AIM_HOME_1,    EVC_X_AIM,    "im_aim_home_1",    N_("AIM Home Screen Name 1"),    FALSE, "HOME", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_AIM_HOME_2,    EVC_X_AIM,    "im_aim_home_2",    N_("AIM Home Screen Name 2"),    FALSE, "HOME", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_AIM_HOME_3,    EVC_X_AIM,    "im_aim_home_3",    N_("AIM Home Screen Name 3"),    FALSE, "HOME", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_AIM_WORK_1,    EVC_X_AIM,    "im_aim_work_1",    N_("AIM Work Screen Name 1"),    FALSE, "WORK", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_AIM_WORK_2,    EVC_X_AIM,    "im_aim_work_2",    N_("AIM Work Screen Name 2"),    FALSE, "WORK", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_AIM_WORK_3,    EVC_X_AIM,    "im_aim_work_3",    N_("AIM Work Screen Name 3"),    FALSE, "WORK", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_GROUPWISE_HOME_1, EVC_X_GROUPWISE, "im_groupwise_home_1", N_("GroupWise Home Screen Name 1"),    FALSE, "HOME", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_GROUPWISE_HOME_2, EVC_X_GROUPWISE, "im_groupwise_home_2", N_("GroupWise Home Screen Name 2"),    FALSE, "HOME", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_GROUPWISE_HOME_3, EVC_X_GROUPWISE, "im_groupwise_home_3", N_("GroupWise Home Screen Name 3"),    FALSE, "HOME", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_GROUPWISE_WORK_1, EVC_X_GROUPWISE, "im_groupwise_work_1", N_("GroupWise Work Screen Name 1"),    FALSE, "WORK", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_GROUPWISE_WORK_2, EVC_X_GROUPWISE, "im_groupwise_work_2", N_("GroupWise Work Screen Name 2"),    FALSE, "WORK", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_GROUPWISE_WORK_3, EVC_X_GROUPWISE, "im_groupwise_work_3", N_("GroupWise Work Screen Name 3"),    FALSE, "WORK", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_JABBER_HOME_1, EVC_X_JABBER, "im_jabber_home_1", N_("Jabber Home Id 1"),          FALSE, "HOME", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_JABBER_HOME_2, EVC_X_JABBER, "im_jabber_home_2", N_("Jabber Home Id 2"),          FALSE, "HOME", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_JABBER_HOME_3, EVC_X_JABBER, "im_jabber_home_3", N_("Jabber Home Id 3"),          FALSE, "HOME", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_JABBER_WORK_1, EVC_X_JABBER, "im_jabber_work_1", N_("Jabber Work Id 1"),          FALSE, "WORK", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_JABBER_WORK_2, EVC_X_JABBER, "im_jabber_work_3", N_("Jabber Work Id 2"),          FALSE, "WORK", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_JABBER_WORK_3, EVC_X_JABBER, "im_jabber_work_2", N_("Jabber Work Id 3"),          FALSE, "WORK", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_YAHOO_HOME_1,  EVC_X_YAHOO,  "im_yahoo_home_1",  N_("Yahoo! Home Screen Name 1"), FALSE, "HOME", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_YAHOO_HOME_2,  EVC_X_YAHOO,  "im_yahoo_home_2",  N_("Yahoo! Home Screen Name 2"), FALSE, "HOME", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_YAHOO_HOME_3,  EVC_X_YAHOO,  "im_yahoo_home_3",  N_("Yahoo! Home Screen Name 3"), FALSE, "HOME", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_YAHOO_WORK_1,  EVC_X_YAHOO,  "im_yahoo_work_1",  N_("Yahoo! Work Screen Name 1"), FALSE, "WORK", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_YAHOO_WORK_2,  EVC_X_YAHOO,  "im_yahoo_work_2",  N_("Yahoo! Work Screen Name 2"), FALSE, "WORK", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_YAHOO_WORK_3,  EVC_X_YAHOO,  "im_yahoo_work_3",  N_("Yahoo! Work Screen Name 3"), FALSE, "WORK", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_MSN_HOME_1,    EVC_X_MSN,    "im_msn_home_1",    N_("MSN Home Screen Name 1"),    FALSE, "HOME", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_MSN_HOME_2,    EVC_X_MSN,    "im_msn_home_2",    N_("MSN Home Screen Name 2"),    FALSE, "HOME", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_MSN_HOME_3,    EVC_X_MSN,    "im_msn_home_3",    N_("MSN Home Screen Name 3"),    FALSE, "HOME", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_MSN_WORK_1,    EVC_X_MSN,    "im_msn_work_1",    N_("MSN Work Screen Name 1"),    FALSE, "WORK", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_MSN_WORK_2,    EVC_X_MSN,    "im_msn_work_2",    N_("MSN Work Screen Name 2"),    FALSE, "WORK", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_MSN_WORK_3,    EVC_X_MSN,    "im_msn_work_3",    N_("MSN Work Screen Name 3"),    FALSE, "WORK", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_ICQ_HOME_1,    EVC_X_ICQ,    "im_icq_home_1",    N_("ICQ Home Id 1"),             FALSE, "HOME", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_ICQ_HOME_2,    EVC_X_ICQ,    "im_icq_home_2",    N_("ICQ Home Id 2"),             FALSE, "HOME", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_ICQ_HOME_3,    EVC_X_ICQ,    "im_icq_home_3",    N_("ICQ Home Id 3"),             FALSE, "HOME", 2),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_ICQ_WORK_1,    EVC_X_ICQ,    "im_icq_work_1",    N_("ICQ Work Id 1"),             FALSE, "WORK", 0),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_ICQ_WORK_2,    EVC_X_ICQ,    "im_icq_work_2",    N_("ICQ Work Id 2"),             FALSE, "WORK", 1),
	ATTR_TYPE_STR_FIELD (E_CONTACT_IM_ICQ_WORK_3,    EVC_X_ICQ,    "im_icq_work_3",    N_("ICQ Work Id 3"),             FALSE, "WORK", 2),

	/* Organizational fields */
	LIST_ELEM_STR_FIELD (E_CONTACT_ORG,      EVC_ORG, "org",      N_("Organization"),        FALSE, 0),
	LIST_ELEM_STR_FIELD (E_CONTACT_ORG_UNIT, EVC_ORG, "org_unit", N_("Organizational Unit"), FALSE, 1),
	LIST_ELEM_STR_FIELD (E_CONTACT_OFFICE,   EVC_ORG, "office",   N_("Office"),              FALSE, 2),

	STRING_FIELD    (E_CONTACT_TITLE,     EVC_TITLE,       "title",     N_("Title"),           FALSE),
	STRING_FIELD    (E_CONTACT_ROLE,      EVC_ROLE,        "role",      N_("Role"),            FALSE),
	STRING_FIELD    (E_CONTACT_MANAGER,   EVC_X_MANAGER,   "manager",   N_("Manager"),         FALSE),
	STRING_FIELD    (E_CONTACT_ASSISTANT, EVC_X_ASSISTANT, "assistant", N_("Assistant"),       FALSE),

	/* Web fields */
	STRING_FIELD (E_CONTACT_HOMEPAGE_URL, EVC_URL,         "homepage_url", N_("Homepage URL"), FALSE),
	STRING_FIELD (E_CONTACT_BLOG_URL,     EVC_X_BLOG_URL,  "blog_url",     N_("Weblog URL"),   FALSE),
	STRING_FIELD (E_CONTACT_VIDEO_URL,    EVC_X_VIDEO_URL, "video_url",    N_("Video Conferencing URL"),   FALSE),

	/* Photo/Logo */
	STRUCT_FIELD    (E_CONTACT_PHOTO, EVC_PHOTO, "photo", N_("Photo"), FALSE, photo_getter, photo_setter, e_contact_photo_get_type),
	STRUCT_FIELD    (E_CONTACT_LOGO,  EVC_LOGO,  "logo",  N_("Logo"),  FALSE, photo_getter, photo_setter, e_contact_photo_get_type),

	/* Security fields */
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_X509_CERT,  EVC_KEY, "x509Cert",  N_("X.509 Certificate"), FALSE, "X509", cert_getter, cert_setter, e_contact_cert_get_type),

	/* Contact categories */
	LIST_FIELD      (E_CONTACT_CATEGORY_LIST, EVC_CATEGORIES, "category_list", N_("Category List"), FALSE),
	SYNTH_STR_FIELD (E_CONTACT_CATEGORIES,                    "categories",    N_("Categories"),    FALSE),

	/* Collaboration fields */
	STRING_FIELD (E_CONTACT_CALENDAR_URI, EVC_CALURI,      "caluri",     N_("Calendar URI"),  FALSE),
	STRING_FIELD (E_CONTACT_FREEBUSY_URL, EVC_FBURL,       "fburl",       N_("Free/Busy URL"), FALSE),
	STRING_FIELD (E_CONTACT_ICS_CALENDAR, EVC_ICSCALENDAR, "icscalendar", N_("ICS Calendar"),  FALSE),

	/* Misc fields */
	STRING_FIELD (E_CONTACT_SPOUSE, EVC_X_SPOUSE,    "spouse", N_("Spouse's Name"), FALSE),
	STRING_FIELD (E_CONTACT_NOTE,   EVC_NOTE,        "note",   N_("Note"),          FALSE),

	STRUCT_FIELD (E_CONTACT_BIRTH_DATE,  EVC_BDAY,          "birth_date",  N_("Birth Date"), FALSE, date_getter, date_setter, e_contact_date_get_type),
	STRUCT_FIELD (E_CONTACT_ANNIVERSARY, EVC_X_ANNIVERSARY, "anniversary", N_("Anniversary"), FALSE, date_getter, date_setter, e_contact_date_get_type),
	
	BOOLEAN_FIELD (E_CONTACT_IS_LIST,             EVC_X_LIST, "list", N_("List"), FALSE),
	BOOLEAN_FIELD (E_CONTACT_LIST_SHOW_ADDRESSES, EVC_X_LIST_SHOW_ADDRESSES, "list_show_addresses", N_("List Show Addresses"), FALSE),

	STRING_FIELD (E_CONTACT_REV, EVC_REV, "Rev", N_("Last Revision"), FALSE)
};

#undef LIST_ELEM_STR_FIELD
#undef STRING_FIELD
#undef SYNTH_STR_FIELD
#undef LIST_FIELD
#undef GETSET_FIELD

static GObjectClass *parent_class;

static void e_contact_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void e_contact_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void
e_contact_dispose (GObject *object)
{
	EContact *ec = E_CONTACT (object);

	if (ec->priv) {
		g_free (ec->priv);
		ec->priv = NULL;
	}

	if (G_OBJECT_CLASS (parent_class)->dispose)
		G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_contact_class_init (EContactClass *klass)
{
	GObjectClass *object_class;
	int i;

	object_class = G_OBJECT_CLASS(klass);

	parent_class = g_type_class_ref (E_TYPE_VCARD);

	object_class->dispose = e_contact_dispose;
	object_class->set_property = e_contact_set_property;
	object_class->get_property = e_contact_get_property;

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		GParamSpec *pspec = NULL;
		if (field_info[i].t & E_CONTACT_FIELD_TYPE_STRING)
			pspec = g_param_spec_string (field_info[i].field_name,
						     _(field_info[i].pretty_name),
						     "" /* XXX blurb */,
						     NULL,
						     field_info[i].read_only ? G_PARAM_READABLE : G_PARAM_READWRITE);
		else if (field_info[i].t & E_CONTACT_FIELD_TYPE_BOOLEAN)
			pspec = g_param_spec_boolean (field_info[i].field_name,
						      _(field_info[i].pretty_name),
						      "" /* XXX blurb */,
						      FALSE,
						      field_info[i].read_only ? G_PARAM_READABLE : G_PARAM_READWRITE);
		else if (field_info[i].t & E_CONTACT_FIELD_TYPE_STRUCT)
			pspec = g_param_spec_boxed (field_info[i].field_name,
						    _(field_info[i].pretty_name),
						    "" /* XXX blurb */,
						    field_info[i].boxed_type_getter(),
						    field_info[i].read_only ? G_PARAM_READABLE : G_PARAM_READWRITE);
		else
			pspec = g_param_spec_pointer (field_info[i].field_name,
						      _(field_info[i].pretty_name),
						      "" /* XXX blurb */,
						      field_info[i].read_only ? G_PARAM_READABLE : G_PARAM_READWRITE);

		g_object_class_install_property (object_class, field_info[i].field_id,
						 pspec);
	}
}

static void
e_contact_init (EContact *ec)
{
	ec->priv = g_new0 (EContactPrivate, 1);
}

GType
e_contact_get_type (void)
{
	static GType contact_type = 0;

	if (!contact_type) {
		static const GTypeInfo contact_info =  {
			sizeof (EContactClass),
			NULL,           /* base_init */
			NULL,           /* base_finalize */
			(GClassInitFunc) e_contact_class_init,
			NULL,           /* class_finalize */
			NULL,           /* class_data */
			sizeof (EContact),
			0,             /* n_preallocs */
			(GInstanceInitFunc) e_contact_init,
		};

		contact_type = g_type_register_static (E_TYPE_VCARD, "EContact", &contact_info, 0);
	}

	return contact_type;
}

static EVCardAttribute*
e_contact_get_first_attr (EContact *contact, const char *attr_name)
{
	GList *attrs, *l;

	attrs = e_vcard_get_attributes (E_VCARD (contact));

	for (l = attrs; l; l = l->next) {
		EVCardAttribute *attr = l->data;
		const char *name;

		name = e_vcard_attribute_get_name (attr);

		if (!strcasecmp (name, attr_name))
			return attr;
	}

	return NULL;
}



static void*
photo_getter (EContact *contact, EVCardAttribute *attr)
{
	if (attr) {
		GList *values = e_vcard_attribute_get_values_decoded (attr);

		if (values && values->data) {
			GString *s = values->data;
			EContactPhoto *photo;

			if (!s->len)
				return NULL;

			photo = g_new (EContactPhoto, 1);
			photo->length = s->len;
			photo->data = g_malloc (photo->length);
			memcpy (photo->data, s->str, photo->length);

			return photo;
		}
	}

	return NULL;
}

static void
photo_setter (EContact *contact, EVCardAttribute *attr, void *data)
{
	EContactPhoto *photo = data;
	const char *mime_type;
	char *image_type = "X-EVOLUTION-UNKNOWN";

	g_return_if_fail (photo->length > 0);

	e_vcard_attribute_add_param_with_value (attr,
						e_vcard_attribute_param_new (EVC_ENCODING),
						"b");

	mime_type = gnome_vfs_get_mime_type_for_data (photo->data, photo->length);
	if (!strcmp (mime_type, "image/gif"))
		image_type = "GIF";
	else if (!strcmp (mime_type, "image/jpeg"))
		image_type = "JPEG";
	else if (!strcmp (mime_type, "image/png"))
		image_type = "PNG";
	else if (!strcmp (mime_type, "image/tiff"))
		image_type = "TIFF";
	/* i have no idea what these last 2 are.. :) */
	else if (!strcmp (mime_type, "image/ief"))
		image_type = "IEF";
	else if (!strcmp (mime_type, "image/cgm"))
		image_type = "CGM";

	e_vcard_attribute_add_param_with_value (attr,
						e_vcard_attribute_param_new (EVC_TYPE),
						image_type);

	e_vcard_attribute_add_value_decoded (attr, photo->data, photo->length);
}


static void*
fn_getter (EContact *contact, EVCardAttribute *attr)
{
	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);

		return p && p->data ? p->data : "";
	}
	else
		return NULL;
}

static void
fn_setter (EContact *contact, EVCardAttribute *attr, void *data)
{
	gchar *name_str = data;

	e_vcard_attribute_add_value (attr, name_str);

	attr = e_contact_get_first_attr (contact, EVC_N);
	if (!attr) {
		EContactName *name = e_contact_name_from_string ((char*)data);

		attr = e_vcard_attribute_new (NULL, EVC_N);
		e_vcard_add_attribute (E_VCARD (contact), attr);

		/* call the setter directly */
		n_setter (contact, attr, name);

		e_contact_name_free (name);
	}
}



static void*
n_getter (EContact *contact, EVCardAttribute *attr)
{
	EContactName *name = g_new0 (EContactName, 1);
	EVCardAttribute *new_attr;
	char *name_str;

	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);

		name->family     = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		name->given      = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		name->additional = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		name->prefixes   = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		name->suffixes   = g_strdup (p && p->data ? p->data : "");
	}

	new_attr = e_contact_get_first_attr (contact, EVC_FN);
	if (!new_attr) {
		new_attr = e_vcard_attribute_new (NULL, EVC_FN);
		e_vcard_add_attribute (E_VCARD (contact), new_attr);
		name_str = e_contact_name_to_string (name);
		e_vcard_attribute_add_value (new_attr, name_str);
		g_free (name_str);
	}

	return name;
}

static void
n_setter (EContact *contact, EVCardAttribute *attr, void *data)
{
	EContactName *name = data;

	e_vcard_attribute_add_value (attr, name->family);
	e_vcard_attribute_add_value (attr, name->given);
	e_vcard_attribute_add_value (attr, name->additional);
	e_vcard_attribute_add_value (attr, name->prefixes);
	e_vcard_attribute_add_value (attr, name->suffixes);

	/* now find the attribute for FileAs.  if it's not present, fill it in */
	attr = e_contact_get_first_attr (contact, EVC_X_FILE_AS);
	if (!attr) {
		char *strings[3], **stringptr;
		char *string;
		attr = e_vcard_attribute_new (NULL, EVC_X_FILE_AS);
		e_vcard_add_attribute (E_VCARD (contact), attr);

		stringptr = strings;
		if (name->family && *name->family)
			*(stringptr++) = name->family;
		if (name->given && *name->given)
			*(stringptr++) = name->given;
		*stringptr = NULL;
		string = g_strjoinv(", ", strings);

		e_vcard_attribute_add_value (attr, string);
		g_free (string);
	}

}



static void*
adr_getter (EContact *contact, EVCardAttribute *attr)
{
	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);
		EContactAddress *addr = g_new (EContactAddress, 1);

		addr->address_format = g_strdup ("");
		addr->po       = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->ext      = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->street   = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->locality = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->region   = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->code     = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->country  = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;

		return addr;
	}

	return NULL;
}

static void
adr_setter (EContact *contact, EVCardAttribute *attr, void *data)
{
	EContactAddress *addr = data;

	e_vcard_attribute_add_value (attr, addr->po);
	e_vcard_attribute_add_value (attr, addr->ext);
	e_vcard_attribute_add_value (attr, addr->street);
	e_vcard_attribute_add_value (attr, addr->locality);
	e_vcard_attribute_add_value (attr, addr->region);
	e_vcard_attribute_add_value (attr, addr->code);
	e_vcard_attribute_add_value (attr, addr->country);
}



static void*
date_getter (EContact *contact, EVCardAttribute *attr)
{
	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);
		EContactDate *date;

		if (p && p->data && ((gchar *) p->data) [0])
			date = e_contact_date_from_string ((char *) p->data);
		else
			date = NULL;

		return date;
	}

	return NULL;
}

static void
date_setter (EContact *contact, EVCardAttribute *attr, void *data)
{
	EContactDate *date = data;
	char *str;

	str = e_contact_date_to_string (date);

	e_vcard_attribute_add_value (attr, str);
	g_free (str);
}



static void*
cert_getter (EContact *contact, EVCardAttribute *attr)
{
	if (attr) {
		/* the certificate is stored in this vcard.  just
		   return the data */
		GList *values = e_vcard_attribute_get_values_decoded (attr);

		if (values && values->data) {
			GString *s = values->data;
			EContactCert *cert = g_new (EContactCert, 1);

			cert->length = s->len;
			cert->data = g_malloc (cert->length);
			memcpy (cert->data, s->str, cert->length);

			return cert;
		}
	}

	/* XXX if we stored a fingerprint in the cert we could look it
	   up via NSS, but that would require the additional NSS dep
	   here, and we'd have more than one process opening the
	   certdb, which is bad.  *sigh* */
	
	return NULL;
}

static void
cert_setter (EContact *contact, EVCardAttribute *attr, void *data)
{
	EContactCert *cert = data;

	e_vcard_attribute_add_param_with_value (attr,
						e_vcard_attribute_param_new (EVC_ENCODING),
						"b");

	e_vcard_attribute_add_value_decoded (attr, cert->data, cert->length);
}



/* Set_arg handler for the contact */
static void
e_contact_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	EContact *contact = E_CONTACT (object);
	int i;
	EContactFieldInfo *info = NULL;

	if (prop_id < 1 || prop_id >= E_CONTACT_FIELD_LAST) {
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}


	for (i = 0; i < G_N_ELEMENTS (field_info); i++) {
		if (field_info[i].field_id == prop_id) {
			info = &field_info[i];
			break;
		}
	}

	if (!info) {
		g_warning ("unknown field %d", prop_id);
		return;
	}

	if (info->t & E_CONTACT_FIELD_TYPE_MULTI) {
		GList *new_values = g_value_get_pointer (value);
		GList *l;

		/* first we remove all attributes of the type we're
		   adding, then add new ones based on the values that
		   are passed in */
		e_vcard_remove_attributes (E_VCARD (contact), NULL, info->vcard_field_name);

		for (l = new_values; l; l = l->next)
			e_vcard_add_attribute_with_value (E_VCARD (contact),
							  e_vcard_attribute_new (NULL, info->vcard_field_name),
							  (char*)l->data);
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_SYNTHETIC) {
		if (info->t & E_CONTACT_FIELD_TYPE_MULTI_ELEM) {
			/* XXX this is kinda broken - we don't insert
			   insert padding elements if, e.g. the user
			   sets email 3 when email 1 and 2 don't
			   exist.  But, if we *did* pad the lists we'd
			   end up with empty items in the vcard.  I
			   dunno which is worse. */
			EVCardAttribute *attr = NULL;
			gboolean found = FALSE;
			int num_left = info->list_elem;
			GList *attrs = e_vcard_get_attributes (E_VCARD (contact));
			GList *l;
			const char *sval;

			for (l = attrs; l; l = l->next) {
				const char *name;

				attr = l->data;
				name = e_vcard_attribute_get_name (attr);

				if (!strcasecmp (name, info->vcard_field_name)) {
					if (num_left-- == 0) {
						found = TRUE;
						break;
					}
				}
			}

			sval = g_value_get_string (value);
			if (sval && *sval) {
				if (found) {
					/* we found it, overwrite it */
					e_vcard_attribute_remove_values (attr);
				}
				else {
					/* we didn't find it - add a new attribute */
					attr = e_vcard_attribute_new (NULL, info->vcard_field_name);
					e_vcard_add_attribute (E_VCARD (contact), attr);
				}

				e_vcard_attribute_add_value (attr, sval);
			}
			else {
				if (found)
					e_vcard_remove_attribute (E_VCARD (contact), attr);
			}
		}
		else if (info->t & E_CONTACT_FIELD_TYPE_ATTR_TYPE) {
			/* XXX this is kinda broken - we don't insert
			   insert padding elements if, e.g. the user
			   sets email 3 when email 1 and 2 don't
			   exist.  But, if we *did* pad the lists we'd
			   end up with empty items in the vcard.  I
			   dunno which is worse. */
			EVCardAttribute *attr = NULL;
			gboolean found = FALSE;
			int num_left = info->list_elem;
			GList *attrs = e_vcard_get_attributes (E_VCARD (contact));
			GList *l;

			for (l = attrs; l && !found; l = l->next) {
				const char *name;
				gboolean found_needed1, found_needed2;

				found_needed1 = (info->attr_type1 == NULL);
				found_needed2 = (info->attr_type2 == NULL);

				attr = l->data;
				name = e_vcard_attribute_get_name (attr);

				if (!strcasecmp (name, info->vcard_field_name)) {
					GList *params;

					for (params = e_vcard_attribute_get_params (attr); params; params = params->next) {
						EVCardAttributeParam *param = params->data;
						const char *name = e_vcard_attribute_param_get_name (param);

						if (!strcasecmp (name, EVC_TYPE)) {
							GList *values = e_vcard_attribute_param_get_values (param);
							if (values && values->data) {
								gboolean matches = FALSE;
								if (!found_needed1 && !strcasecmp ((char*)values->data, info->attr_type1)) {
									found_needed1 = TRUE;
									matches = TRUE;
								}
								else if (!found_needed2 && !strcasecmp ((char*)values->data, info->attr_type2)) {
									found_needed2 = TRUE;
									matches = TRUE;
								}
								if (!matches) {
									/* this is to enforce that we find an attribute
									   with *only* the TYPE='s we need.  This may seem like
									   an odd restriction but it's the only way at present to
									   implement the Other Fax and Other Phone attributes. */
									found_needed1 =
										found_needed2 = FALSE;
									break;
								}
							}
						}

						if (found_needed1 && found_needed2) {
							if (num_left-- == 0) {
								found = TRUE;
								break;
							}
						}
					}
				}
			}

			if (found) {
				/* we found it, overwrite it */
				e_vcard_attribute_remove_values (attr);
			}
			else {
				/* we didn't find it - add a new attribute */
				attr = e_vcard_attribute_new (NULL, info->vcard_field_name);
				e_vcard_add_attribute (E_VCARD (contact), attr);
				if (info->attr_type1)
					e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE),
										info->attr_type1);
				if (info->attr_type2)
					e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_TYPE),
										info->attr_type2);
			}

			if (info->t & E_CONTACT_FIELD_TYPE_STRUCT || info->t & E_CONTACT_FIELD_TYPE_GETSET) {
				void *data = info->t & E_CONTACT_FIELD_TYPE_STRUCT ? g_value_get_boxed (value) : (char*)g_value_get_string (value);

				if ((info->t & E_CONTACT_FIELD_TYPE_STRUCT && data)
				    || (data && *(char*)data))
					info->struct_setter (contact, attr, data);
				else
					e_vcard_remove_attribute (E_VCARD (contact), attr);
			}
			else {
				const char *sval = g_value_get_string (value);

				if (sval && *sval)
					e_vcard_attribute_add_value (attr, sval);
				else
					e_vcard_remove_attribute (E_VCARD (contact), attr);
			}
		}
		else if (info->t & E_CONTACT_FIELD_TYPE_LIST_ELEM) {
			EVCardAttribute *attr = e_contact_get_first_attr (contact, info->vcard_field_name);
			GList *values;
			GList *p;
			const char *sval = g_value_get_string (value);

			if (!attr) {
				if (!sval || !*sval)
					return;

				d(printf ("adding new %s\n", info->vcard_field_name));

				attr = e_vcard_attribute_new (NULL, info->vcard_field_name);
				e_vcard_add_attribute (E_VCARD (contact), attr);
			}
			
			values = e_vcard_attribute_get_values (attr);
			p = g_list_nth (values, info->list_elem);

			if (p) {
				g_free (p->data);
				p->data = g_strdup (g_value_get_string (value));
			}
			else {
				/* there weren't enough elements in the list, pad it */
				int count = info->list_elem - g_list_length (values);

				while (count--)
					e_vcard_attribute_add_value (attr, "");

				e_vcard_attribute_add_value (attr, g_value_get_string (value));
			}

		}
		else {
			switch (info->field_id) {
			case E_CONTACT_CATEGORIES: {
				EVCardAttribute *attr = e_contact_get_first_attr (contact, EVC_CATEGORIES);
				char **split, **s;
				const char *str;

				if (attr)
					e_vcard_attribute_remove_values (attr);
				else {
					/* we didn't find it - add a new attribute */
					attr = e_vcard_attribute_new (NULL, EVC_CATEGORIES);
					e_vcard_add_attribute (E_VCARD (contact), attr);
				}

				str = g_value_get_string (value);
				if (str && *str) {
					split = g_strsplit (str, ",", 0);
					if (split) {
						for (s = split; *s; s++) {
							e_vcard_attribute_add_value (attr, g_strstrip (*s));
						}
						g_strfreev (split);
					} else
						e_vcard_attribute_add_value (attr, str);
				}
				else {
					d(printf ("removing %s\n", info->vcard_field_name));

					e_vcard_remove_attribute (E_VCARD (contact), attr);
				}
				break;
			}
			default:
				g_warning ("unhandled synthetic field 0x%02x", info->field_id);
				break;
			}
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_STRUCT || info->t & E_CONTACT_FIELD_TYPE_GETSET) {
		EVCardAttribute *attr = e_contact_get_first_attr (contact, info->vcard_field_name);
		void *data = info->t & E_CONTACT_FIELD_TYPE_STRUCT ? g_value_get_boxed (value) : (char*)g_value_get_string (value);

		if (attr) {
			if ((info->t & E_CONTACT_FIELD_TYPE_STRUCT && data)
			    || (data && *(char*)data)) {
				d(printf ("overwriting existing %s\n", info->vcard_field_name));
				/* remove all existing values and parameters.
				   the setter will add the correct ones */
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_remove_params (attr);

				info->struct_setter (contact, attr, data);
			}
			else {
				d(printf ("removing %s\n", info->vcard_field_name));

				e_vcard_remove_attribute (E_VCARD (contact), attr);
			}
		}
		else if ((info->t & E_CONTACT_FIELD_TYPE_STRUCT && data)
			 || (data && *(char*)data)) {
			d(printf ("adding new %s\n", info->vcard_field_name));
			attr = e_vcard_attribute_new (NULL, info->vcard_field_name);

			e_vcard_add_attribute (E_VCARD (contact), attr);

			info->struct_setter (contact, attr, data);
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_BOOLEAN) {
		EVCardAttribute *attr;

		/* first we search for an attribute we can overwrite */
		attr = e_contact_get_first_attr (contact, info->vcard_field_name);
		if (attr) {
			d(printf ("setting %s to `%s'\n", info->vcard_field_name, g_value_get_string (value)));
			e_vcard_attribute_remove_values (attr);
			e_vcard_attribute_add_value (attr, g_value_get_boolean (value) ? "TRUE" : "FALSE");
		}
		else {
			/* and if we don't find one we create a new attribute */
			e_vcard_add_attribute_with_value (E_VCARD (contact),
							  e_vcard_attribute_new (NULL, info->vcard_field_name),
							  g_value_get_boolean (value) ? "TRUE" : "FALSE");
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
		EVCardAttribute *attr;
		const char *sval = g_value_get_string (value);

		/* first we search for an attribute we can overwrite */
		attr = e_contact_get_first_attr (contact, info->vcard_field_name);
		if (attr) {
			d(printf ("setting %s to `%s'\n", info->vcard_field_name, sval));
			e_vcard_attribute_remove_values (attr);
			if (sval) {
				e_vcard_attribute_add_value (attr, sval);
			}
			else {
				d(printf ("removing %s\n", info->vcard_field_name));

				e_vcard_remove_attribute (E_VCARD (contact), attr);
			}

		}
		else if (sval) {
			/* and if we don't find one we create a new attribute */
			e_vcard_add_attribute_with_value (E_VCARD (contact),
							  e_vcard_attribute_new (NULL, info->vcard_field_name),
							  g_value_get_string (value));
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_LIST) {
		EVCardAttribute *attr;
		GList *values, *l;

		values = g_value_get_pointer (value);

		attr = e_contact_get_first_attr (contact, info->vcard_field_name);

		if (attr) {
			e_vcard_attribute_remove_values (attr);

			if (!values)
				e_vcard_remove_attribute (E_VCARD (contact), attr);
		}
		else if (values) {
			attr = e_vcard_attribute_new (NULL, info->vcard_field_name);
			e_vcard_add_attribute (E_VCARD (contact), attr);
		}

		for (l = values; l != NULL; l = l->next)
			e_vcard_attribute_add_value (attr, l->data);
	}
	else {
		g_warning ("unhandled attribute `%s'", info->vcard_field_name);
	}
}

static EVCardAttribute *
e_contact_find_attribute_with_types (EContact *contact, const char *attr_name, const char *type_needed1, const char *type_needed2, int nth)
{
	GList *l, *attrs;
	gboolean found_needed1, found_needed2;

	attrs = e_vcard_get_attributes (E_VCARD (contact));

	for (l = attrs; l; l = l->next) {
		EVCardAttribute *attr = l->data;
		const char *name;

		found_needed1 = (type_needed1 == NULL);
		found_needed2 = (type_needed2 == NULL);

		name = e_vcard_attribute_get_name (attr);

		if (!strcasecmp (name, attr_name)) {
			GList *params;

			for (params = e_vcard_attribute_get_params (attr); params; params = params->next) {
				EVCardAttributeParam *param = params->data;
				const char *name = e_vcard_attribute_param_get_name (param);

				if (!strcasecmp (name, EVC_TYPE)) {
					GList *values = e_vcard_attribute_param_get_values (param);
					if (values && values->data) {
						gboolean matches = FALSE;

						if (!found_needed1 && !strcasecmp ((char*)values->data, type_needed1)) {
							found_needed1 = TRUE;
							matches = TRUE;
						}
						else if (!found_needed2 && !strcasecmp ((char*)values->data, type_needed2)) {
							found_needed2 = TRUE;
							matches = TRUE;
						}

						if (!matches) {
							/* this is to enforce that we find an attribute
							   with *only* the TYPE='s we need.  This may seem like
							   an odd restriction but it's the only way at present to
							   implement the Other Fax and Other Phone attributes. */
							found_needed1 =
								found_needed2 = FALSE;
							break;
						}
					}
				}

				if (found_needed1 && found_needed2) {
					if (nth-- == 0)
						return attr;
					else
						break;
				}
			}
		}
	}

	return NULL;
}

static void
e_contact_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	EContact *contact = E_CONTACT (object);
	int i;
	EContactFieldInfo *info = NULL;

	if (prop_id < 1 || prop_id >= E_CONTACT_FIELD_LAST) {
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		g_value_reset (value);
		return;
	}

	for (i = 0; i < G_N_ELEMENTS (field_info); i++) {
		if (field_info[i].field_id == prop_id) {
			info = &field_info[i];
			break;
		}
	}

	if (!info) {
		g_warning ("unknown field %d", prop_id);
		return;
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_BOOLEAN) {
		EVCardAttribute *attr = e_contact_get_first_attr (contact, info->vcard_field_name);
		gboolean rv = FALSE;

		if (attr) {
			GList *v = e_vcard_attribute_get_values (attr);
			rv = v && v->data && !strcasecmp ((char*)v->data, "true");
		}

		g_value_set_boolean (value, rv);
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_LIST) {
		EVCardAttribute *attr = e_contact_get_first_attr (contact, info->vcard_field_name);

		if (attr) {
			GList *list = g_list_copy (e_vcard_attribute_get_values (attr));
			GList *l;
			for (l = list; l; l = l->next)
				l->data = g_strdup (l->data);

			g_value_set_pointer (value, list);
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_LIST_ELEM) {
		if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
			GList *attrs, *l;

			attrs = e_vcard_get_attributes (E_VCARD (contact));

			for (l = attrs; l; l = l->next) {
				EVCardAttribute *attr = l->data;
				const char *name;

				name = e_vcard_attribute_get_name (attr);

				if (!strcasecmp (name, info->vcard_field_name)) {
					GList *v;
					int count;

					v = e_vcard_attribute_get_values (attr);
					count = info->list_elem;

					v = g_list_nth (v, info->list_elem);

					g_value_set_string (value, v ? v->data : NULL);
				}
			}
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_MULTI_ELEM) {
		if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
			GList *attrs, *l;
			int num_left = info->list_elem;

			attrs = e_vcard_get_attributes (E_VCARD (contact));

			for (l = attrs; l; l = l->next) {
				EVCardAttribute *attr = l->data;
				const char *name;

				name = e_vcard_attribute_get_name (attr);

				if (!strcasecmp (name, info->vcard_field_name)) {
					if (num_left-- == 0) {
						GList *v = e_vcard_attribute_get_values (attr);

						g_value_set_string (value, v ? v->data : NULL);
						break;
					}
				}
			}
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_ATTR_TYPE) {
		EVCardAttribute *attr = e_contact_find_attribute_with_types (contact, info->vcard_field_name, info->attr_type1, info->attr_type2, info->list_elem);

		if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
			if (attr) {
				GList *p = e_vcard_attribute_get_values (attr);
				char *rv = p->data;

				g_value_set_string (value, rv);
			}
			else {
				g_value_set_string (value, NULL);
			}
		}
		else { /* struct */
			gpointer rv = info->struct_getter (contact, attr);

			g_value_set_boxed (value, rv);
		}

	}
	else if (info->t & E_CONTACT_FIELD_TYPE_STRUCT) {
		EVCardAttribute *attr = e_contact_get_first_attr (contact, info->vcard_field_name);
		void *rv = NULL;

		if (attr)
			rv = info->struct_getter (contact, attr);

		g_value_set_boxed (value, rv);
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_GETSET) {
		EVCardAttribute *attr = e_contact_get_first_attr (contact, info->vcard_field_name);
		void *rv = NULL;

		if (attr)
			rv = info->struct_getter (contact, attr);

		if (info->t & E_CONTACT_FIELD_TYPE_STRUCT)
			g_value_set_boxed (value, rv);
		else
			g_value_set_string (value, (char*)rv);
			
	}

	else if (info->t & E_CONTACT_FIELD_TYPE_SYNTHETIC) {
		switch (info->field_id) {
		case E_CONTACT_NAME_OR_ORG: {
			char *str;

			str = e_contact_get_const (contact, E_CONTACT_FILE_AS);
			if (!str)
				str = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
			if (!str)
				str = e_contact_get_const (contact, E_CONTACT_ORG);
			if (!str) {
				gboolean is_list = GPOINTER_TO_INT (e_contact_get (contact, E_CONTACT_IS_LIST));

				if (is_list)
					str = _("Unnamed List");
				else
					str = e_contact_get_const (contact, E_CONTACT_EMAIL_1);
			}

			g_value_set_string (value, str);
			break;
		}
		case E_CONTACT_CATEGORIES: {
			EVCardAttribute *attr = e_contact_get_first_attr (contact, EVC_CATEGORIES);
			char *rv = NULL;

			if (attr) {
				GString *str = g_string_new ("");
				GList *v = e_vcard_attribute_get_values (attr);
				while (v) {
					g_string_append (str, (char*)v->data);
					v = v->next;
					if (v)
						g_string_append_c (str, ',');
				}

				rv = g_string_free (str, FALSE);
			}

			g_value_set_string (value, rv);
			g_free (rv);
			break;
		}
		default:
			g_warning ("unhandled synthetic field 0x%02x", info->field_id);
			break;
		}
	}
	else {
		GList *attrs, *l;
		GList *rv = NULL; /* used for multi attribute lists */

		attrs = e_vcard_get_attributes (E_VCARD (contact));

		for (l = attrs; l; l = l->next) {
			EVCardAttribute *attr = l->data;
			const char *name;

			name = e_vcard_attribute_get_name (attr);

			if (!strcasecmp (name, info->vcard_field_name)) {
				GList *v;
				v = e_vcard_attribute_get_values (attr);

				if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
					g_value_set_string (value, v ? v->data : NULL);
				}
				else {
					rv = g_list_append (rv, v ? g_strdup (v->data) : NULL);

					g_value_set_pointer (value, rv);
				}
			}
		}
	}
}



/**
 * e_contact_new:
 *
 * Creates a new, blank #EContact.
 *
 * Return value: A new #EContact.
 **/
EContact*
e_contact_new (void)
{
	return e_contact_new_from_vcard ("");
}

/**
 * e_contact_new_from_vcard:
 * @vcard: a string representing a vcard
 * 
 * Creates a new #EContact based on a vcard.
 *
 * Return value: A new #EContact.
 **/
EContact*
e_contact_new_from_vcard  (const char *vcard)
{
	EContact *contact;
	const gchar *file_as;

	g_return_val_if_fail (vcard != NULL, NULL);

	contact = g_object_new (E_TYPE_CONTACT, NULL);
	e_vcard_construct (E_VCARD (contact), vcard);

	/* Generate a FILE_AS field if needed */

	file_as = e_contact_get_const (contact, E_CONTACT_FILE_AS);
	if (!file_as || !*file_as) {
		EContactName *name;
		const gchar *org;
		gchar *file_as_new = NULL;
		gchar *strings [4];
		gchar **strings_p = strings;

		name = e_contact_get (contact, E_CONTACT_NAME);
		org = e_contact_get_const (contact, E_CONTACT_ORG);

		if (name) {
			if (name->family && *name->family)
				*(strings_p++) = name->family;
			if (name->given && *name->given)
				*(strings_p++) = name->given;

			if (strings_p != strings) {
				*strings_p = NULL;
				file_as_new = g_strjoinv (", ", strings);
			}

			e_contact_name_free (name);
		}

		if (!file_as_new && org && *org)
			file_as_new = g_strdup (org);

		if (file_as_new) {
			e_contact_set (contact, E_CONTACT_FILE_AS, file_as_new);
			g_free (file_as_new);
		}
	}

	return contact;
}

/**
 * e_contact_duplicate:
 * @contact: an #EContact
 *
 * Creates a copy of @contact.
 *
 * Return value: A new #EContact identical to @contact.
 **/
EContact*
e_contact_duplicate (EContact *contact)
{
	char *vcard;
	EContact *c;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	vcard = e_vcard_to_string (E_VCARD (contact), EVC_FORMAT_VCARD_30);
	c = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	return c;
}

/**
 * e_contact_field_name:
 * @field_id: an #EContactField
 *
 * Gets the string representation of @field_id.
 *
 * Return value: The string representation of @field_id, or %NULL if it doesn't exist.
 **/
const char *
e_contact_field_name (EContactField field_id)
{
	int i;

	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST, "");

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		if (field_id == field_info[i].field_id)
			return field_info[i].field_name;
	}

	g_warning ("unknown field id %d", field_id);
	return "";
}

/**
 * e_contact_pretty_name:
 * @field_id: an #EContactField
 *
 * Gets a human-readable, translated string representation
 * of @field_id.
 *
 * Return value: The human-readable representation of @field_id, or %NULL if it doesn't exist.
 **/
const char *
e_contact_pretty_name (EContactField field_id)
{
	int i;

	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST, "");

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, EVOLUTION_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		if (field_id == field_info[i].field_id)
			return _(field_info[i].pretty_name);
	}

	g_warning ("unknown field id %d", field_id);
	return "";
}

/**
 * e_contact_vcard_attribute:
 * @field_id: an #EContactField
 *
 * Gets the vcard attribute corresponding to @field_id, as a string.
 *
 * Return value: The vcard attribute corresponding to @field_id, or %NULL if it doesn't exist.
 **/
const char*
e_contact_vcard_attribute  (EContactField field_id)
{
	int i;

	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST, "");

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		if (field_id == field_info[i].field_id)
			return field_info[i].vcard_field_name;
	}

	g_warning ("unknown field id %d", field_id);
	return NULL;
}

/**
 * e_contact_field_id:
 * @field_name: a string representing a contact field
 * 
 * Gets the #EContactField corresponding to the @field_name.
 *
 * Return value: An #EContactField corresponding to @field_name, or %0 if it doesn't exist.
 **/
EContactField
e_contact_field_id (const char *field_name)
{
	int i;

	for (i = 0; i < G_N_ELEMENTS (field_info); i ++) {
		if (!strcmp (field_info[i].field_name, field_name))
			return field_info[i].field_id;
	}

	g_warning ("unknown field name `%s'", field_name);
	return 0;
}

/**
 * e_contact_get:
 * @contact: an #EContact
 * @field_id: an #EContactField
 *
 * Gets the value of @contact's field specified by @field_id.
 *
 * Return value: Depends on the field's type, owned by the caller.
 **/
gpointer
e_contact_get (EContact *contact, EContactField field_id)
{
	gpointer value = NULL;

	g_return_val_if_fail (contact && E_IS_CONTACT (contact), NULL);
	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST, NULL);

	g_object_get (contact,
		      e_contact_field_name (field_id), &value,
		      NULL);

	return value;
}

static void
free_const_data (gpointer data, GObject *where_object_was)
{
	g_free (data);
}

/**
 * e_contact_get_const:
 * @contact: an #EContact
 * @field_id: an #EContactField
 *
 * Gets the value of @contact's field specified by @field_id, caching
 * the result so it can be freed later.
 *
 * Return value: Depends on the field's type, owned by the #EContact.
 **/
const gpointer
e_contact_get_const (EContact *contact, EContactField field_id)
{
	gboolean is_string = FALSE;
	gpointer value = NULL;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);
	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_LAST_SIMPLE_STRING, NULL);

	if (field_info [field_id].t & E_CONTACT_FIELD_TYPE_STRING)
		is_string = TRUE;

	if (is_string)
		value = contact->priv->cached_strings[field_id];

	if (!value) {
		value = e_contact_get (contact, field_id);
		if (is_string && value)
			contact->priv->cached_strings[field_id] = value;

		if (value)
			g_object_weak_ref (G_OBJECT (contact), free_const_data, value);
	}

	return value;
}

/**
 * e_contact_set;
 * @contact: an #EContact
 * @field_id: an #EContactField
 * @value: a value whose type depends on the @field_id
 *
 * Sets the value of @contact's field specified by @field_id to @value.
 **/
void
e_contact_set (EContact *contact, EContactField field_id, gpointer value)
{
	d(printf ("e_contact_set (%p, %d, %p)\n", contact, field_id, value));

	g_return_if_fail (contact && E_IS_CONTACT (contact));
	g_return_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST);

	/* set the cached slot to NULL so we'll re-get the new string
	   if e_contact_get_const is called again */
	contact->priv->cached_strings[field_id] = NULL;

	g_object_set (contact,
		      e_contact_field_name (field_id), value,
		      NULL);
}

/**
 * e_contact_get_attributes:
 * @contact: an #EContact
 * @field_id: an #EContactField
 *
 * Gets a list of the vcard attributes for @contact's @field_id.
 *
 * Return value: A #GList of pointers to #EVCardAttribute, owned by the caller.
 **/
GList*
e_contact_get_attributes (EContact *contact, EContactField field_id)
{
	GList *l = NULL;
	GList *attrs, *a;
	int i;
	EContactFieldInfo *info = NULL;

	g_return_val_if_fail (contact && E_IS_CONTACT (contact), NULL);
	g_return_val_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST, NULL);

	for (i = 0; i < G_N_ELEMENTS (field_info); i++) {
		if (field_info[i].field_id == field_id) {
			info = &field_info[i];
			break;
		}
	}

	if (!info) {
		g_warning ("unknown field %d", field_id);
		return NULL;
	}

	attrs = e_vcard_get_attributes (E_VCARD (contact));

	for (a = attrs; a; a = a->next) {
		EVCardAttribute *attr = a->data;
		const char *name;

		name = e_vcard_attribute_get_name (attr);

		if (!strcasecmp (name, info->vcard_field_name)) {
			l = g_list_append (l, e_vcard_attribute_copy (attr));
		}
	}

	return l;
}

/**
 * e_contact_set_attributes:
 * @contact: an #EContact
 * @field_id: an #EContactField
 * @attributes: a #GList of pointers to #EVCardAttribute
 *
 * Sets the vcard attributes for @contact's @field_id.
 **/
void
e_contact_set_attributes (EContact *contact, EContactField field_id, GList *attributes)
{
	EContactFieldInfo *info = NULL;
	GList *l;
	int i;

	g_return_if_fail (contact && E_IS_CONTACT (contact));
	g_return_if_fail (field_id >= 1 && field_id <= E_CONTACT_FIELD_LAST);

	for (i = 0; i < G_N_ELEMENTS (field_info); i++) {
		if (field_info[i].field_id == field_id) {
			info = &field_info[i];
			break;
		}
	}

	if (!info) {
		g_warning ("unknown field %d", field_id);
		return;
	}

	e_vcard_remove_attributes (E_VCARD (contact), NULL, info->vcard_field_name);

	for (l = attributes; l; l = l->next)
		e_vcard_add_attribute (E_VCARD (contact),
				       e_vcard_attribute_copy ((EVCardAttribute*)l->data));
}

/**
 * e_contact_name_new:
 *
 * Creates a new #EContactName struct.
 *
 * Return value: A new #EContactName struct.
 **/
EContactName*
e_contact_name_new ()
{
	return g_new0 (EContactName, 1);
}

/**
 * e_contact_name_to_string:
 * @name: an #EContactName
 *
 * Generates a string representation of @name.
 *
 * Return value: The string representation of @name.
 **/
char *
e_contact_name_to_string(const EContactName *name)
{
	char *strings[6], **stringptr = strings;

	g_return_val_if_fail (name != NULL, NULL);

	if (name->prefixes && *name->prefixes)
		*(stringptr++) = name->prefixes;
	if (name->given && *name->given)
		*(stringptr++) = name->given;
	if (name->additional && *name->additional)
		*(stringptr++) = name->additional;
	if (name->family && *name->family)
		*(stringptr++) = name->family;
	if (name->suffixes && *name->suffixes)
		*(stringptr++) = name->suffixes;
	*stringptr = NULL;
	return g_strjoinv(" ", strings);
}

/**
 * e_contact_name_from_string:
 * @name_str: a string representing a contact's full name
 *
 * Creates a new #EContactName based on the parsed @name_str.
 *
 * Return value: A new #EContactName struct.
 **/
EContactName*
e_contact_name_from_string (const char *name_str)
{
	EContactName *name = e_contact_name_new();
	ENameWestern *western;

	g_return_val_if_fail (name_str != NULL, NULL);

	western = e_name_western_parse (name_str ? name_str : "");
	
	name->prefixes   = g_strdup (western->prefix);
	name->given      = g_strdup (western->first );
	name->additional = g_strdup (western->middle);
	name->family     = g_strdup (western->last  );
	name->suffixes   = g_strdup (western->suffix);
	
	e_name_western_free(western);
	
	return name;
}

/**
 * e_contact_name_copy:
 * @n: an #EContactName
 *
 * Creates a copy of @n.
 *
 * Return value: A new #EContactName identical to @n.
 **/
EContactName*
e_contact_name_copy (EContactName *n)
{
	EContactName *name;

	g_return_val_if_fail (n != NULL, NULL);

	name = e_contact_name_new();
	
	name->prefixes   = g_strdup (n->prefixes);
	name->given      = g_strdup (n->given);
	name->additional = g_strdup (n->additional);
	name->family     = g_strdup (n->family);
	name->suffixes   = g_strdup (n->suffixes);
	
	return name;
}

/**
 * e_contact_name_free:
 * @name: an #EContactName
 *
 * Frees @name and its contents.
 **/
void
e_contact_name_free (EContactName *name)
{
	if (!name)
		return;

	g_free (name->family);
	g_free (name->given);
	g_free (name->additional);
	g_free (name->prefixes);
	g_free (name->suffixes);

	g_free (name);
}

GType
e_contact_name_get_type (void)
{
	static GType type_id = 0;

	if (!type_id)
		type_id = g_boxed_type_register_static ("EContactName",
							(GBoxedCopyFunc) e_contact_name_copy,
							(GBoxedFreeFunc) e_contact_name_free);
	return type_id;
}

/**
 * e_contact_date_from_string:
 * @str: a date string in the format YYYY-MM-DD or YYYYMMDD
 *
 * Creates a new #EContactDate based on @str.
 *
 * Return value: A new #EContactDate struct.
 **/
EContactDate*
e_contact_date_from_string (const char *str)
{
	EContactDate* date;
	int length;

	g_return_val_if_fail (str != NULL, NULL);

	date = e_contact_date_new();
	length = strlen(str);
	
	if (length == 10 ) {
		date->year = str[0] * 1000 + str[1] * 100 + str[2] * 10 + str[3] - '0' * 1111;
		date->month = str[5] * 10 + str[6] - '0' * 11;
		date->day = str[8] * 10 + str[9] - '0' * 11;
	} else if ( length == 8 ) {
		date->year = str[0] * 1000 + str[1] * 100 + str[2] * 10 + str[3] - '0' * 1111;
		date->month = str[4] * 10 + str[5] - '0' * 11;
		date->day = str[6] * 10 + str[7] - '0' * 11;
	}
	
	return date;
}

/**
 * e_contact_date_to_string:
 * @dt: an #EContactDate
 *
 * Generates a date string in the format YYYY-MM-DD based
 * on the values of @dt.
 *
 * Return value: A date string, owned by the caller.
 **/
char *
e_contact_date_to_string (EContactDate *dt)
{
	if (dt) 
		return g_strdup_printf ("%04d-%02d-%02d",
					CLAMP(dt->year, 1000, 9999),
					CLAMP(dt->month, 1, 12),
					CLAMP(dt->day, 1, 31));
	else
		return NULL;
}

/**
 * e_contact_date_equal:
 * @dt1: an #EContactDate
 * @dt2: an #EContactDate
 *
 * Checks if @dt1 and @dt2 are the same date.
 *
 * Return value: %TRUE if @dt1 and @dt2 are equal, %FALSE otherwise.
 **/
gboolean
e_contact_date_equal (EContactDate *dt1, EContactDate *dt2)
{
	if (dt1 && dt2) {
		return (dt1->year == dt2->year &&
			dt1->month == dt2->month &&
			dt1->day == dt2->day);
	} else
		return (!!dt1 == !!dt2);
}

/**
 * e_contact_date_copy:
 * @dt: an #EContactDate
 *
 * Creates a copy of @dt.
 *
 * Return value: A new #EContactDate struct identical to @dt.
 **/
static EContactDate *
e_contact_date_copy (EContactDate *dt)
{
	EContactDate *dt2 = e_contact_date_new ();
	dt2->year = dt->year;
	dt2->month = dt->month;
	dt2->day = dt->day;

	return dt2;
}

/**
 * e_contact_date_free:
 * @dt: an #EContactDate
 *
 * Frees the @dt struct and its contents.
 **/
void
e_contact_date_free (EContactDate *dt)
{
	g_free (dt);
}

GType
e_contact_date_get_type (void)
{
	static GType type_id = 0;

	if (!type_id)
		type_id = g_boxed_type_register_static ("EContactDate",
							(GBoxedCopyFunc) e_contact_date_copy,
							(GBoxedFreeFunc) e_contact_date_free);
	return type_id;
}

/**
 * e_contact_date_new:
 * 
 * Creates a new #EContactDate struct.
 *
 * Return value: A new #EContactDate struct.
 **/
EContactDate*
e_contact_date_new (void)
{
	return g_new0 (EContactDate, 1);
}

/**
 * e_contact_photo_free:
 * @photo: an #EContactPhoto struct
 *
 * Frees the @photo struct and its contents.
 **/
void
e_contact_photo_free (EContactPhoto *photo)
{
	if (!photo)
		return;

	g_free (photo->data);
	g_free (photo);
}

/**
 * e_contact_photo_copy:
 * @photo: an #EContactPhoto
 *
 * Creates a copy of @photo.
 *
 * Return value: A new #EContactPhoto struct identical to @photo.
 **/
static EContactPhoto *
e_contact_photo_copy (EContactPhoto *photo)
{
	EContactPhoto *photo2 = g_new (EContactPhoto, 1);
	photo2->length = photo->length;
	photo2->data = g_malloc (photo2->length);
	memcpy (photo2->data, photo->data, photo->length);

	return photo2;
}

GType
e_contact_photo_get_type (void)
{
	static GType type_id = 0;

	if (!type_id)
		type_id = g_boxed_type_register_static ("EContactPhoto",
							(GBoxedCopyFunc) e_contact_photo_copy,
							(GBoxedFreeFunc) e_contact_photo_free);
	return type_id;
}

/**
 * e_contact_address_free:
 * @address: an #EContactAddress
 *
 * Frees the @address struct and its contents.
 **/
void
e_contact_address_free (EContactAddress *address)
{
	if (!address)
		return;

	g_free (address->address_format);
	g_free (address->po);
	g_free (address->ext);
	g_free (address->street);
	g_free (address->locality);
	g_free (address->region);
	g_free (address->code);
	g_free (address->country);

	g_free (address);
}

static EContactAddress *
e_contact_address_copy (EContactAddress *address)
{
	EContactAddress *address2 = g_new (EContactAddress, 1);

	address2->address_format = g_strdup (address->address_format);
	address2->po = g_strdup (address->po);
	address2->ext = g_strdup (address->ext);
	address2->street = g_strdup (address->street);
	address2->locality = g_strdup (address->locality);
	address2->region = g_strdup (address->region);
	address2->code = g_strdup (address->code);
	address2->country = g_strdup (address->country);

	return address2;
}

GType
e_contact_address_get_type (void)
{
	static GType type_id = 0;

	if (!type_id)
		type_id = g_boxed_type_register_static ("EContactAddress",
							(GBoxedCopyFunc) e_contact_address_copy,
							(GBoxedFreeFunc) e_contact_address_free);
	return type_id;
}

/**
 * e_contact_cert_free:
 * @cert: an #EContactCert
 *
 * Frees the @cert struct and its contents.
 **/
void
e_contact_cert_free (EContactCert *cert)
{
	if (!cert)
		return;

	g_free (cert->data);
	g_free (cert);
}

static EContactCert *
e_contact_cert_copy (EContactCert *cert)
{
	EContactCert *cert2 = g_new (EContactCert, 1);
	cert2->length = cert->length;
	cert2->data = g_malloc (cert2->length);
	memcpy (cert2->data, cert->data, cert->length);

	return cert2;
}

GType
e_contact_cert_get_type (void)
{
	static GType type_id = 0;

	if (!type_id)
		type_id = g_boxed_type_register_static ("EContactCert",
							(GBoxedCopyFunc) e_contact_cert_copy,
							(GBoxedFreeFunc) e_contact_cert_free);
	return type_id;
}
