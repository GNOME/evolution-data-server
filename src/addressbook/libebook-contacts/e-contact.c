/* e-contact.c
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2012 Intel Corporation
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
 * Authors: Chris Toshok (toshok@ximian.com)
 *          Tristan Van Berkom <tristanvb@openismus.com>
 */

/**
 * SECTION:e-contact
 * @short_description: A convenience interface for interacting with contacts
 * @include: libebook-contacts/libebook-contacts.h
 *
 * This subclass of #EVCard is a convenient interface for interacting with
 * vCards. The #EBookClient, #EBookClientView and #EBookClientCursor return
 * vCards in the form of an #EContact for your convenience.
 *
 * Using e_contact_set() and e_contact_get() you get automatic conversion
 * between vCard standards. For example, when the #EContact holds a vCard 3.0
 * data and %E_CONTACT_BIRTHPLACE is used, an EVC_X_EVOLUTION_BIRTHPLACE attribute
 * is used to store the value, but when the #EContact holds a vCard 4.0 data,
 * then the EVC_BIRTHPLACE attribute is used, because it's a valid attribute
 * in the vCard 4.0. Similarly the data formats are converted as needed.
 *
 * Note: Some of the attributes are very specific and the #EContact can construct
 * their values from multiple other attributes. One such is E_CONTACT_IMPP, which
 * always returns the data in the vCard 4.0 format ("scheme:value"), which the vCard
 * 4.0 data has stored in an attribute EVC_IMPP, but the previous vCard versions
 * have it split into multiple different attributes (not only EVC_X_EVOLUTION_IMPP),
 * depending whether there exists a known #EContact field for the used "scheme".
 * These things are taken care of both when using e_contact_get() and e_contact_set(),
 * to preserve as much compatibility between the vCard versions as possible, while
 * keeping the abstraction the #EContact offers.
 **/

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <gio/gio.h>
#include <glib/gi18n-lib.h>

#include "camel/camel.h"
#include "libedataserver/libedataserver.h"

#include "e-contact.h"
#include "e-name-western.h"

#ifdef G_OS_WIN32
#undef LOCALEDIR
#define LOCALEDIR e_util_get_localedir ()
#endif

#define d(x)

struct _EContactPrivate {
	gchar *cached_strings[E_CONTACT_FIELD_LAST];
};

G_DEFINE_TYPE_WITH_PRIVATE (EContact, e_contact, E_TYPE_VCARD)

typedef struct _AttrTypeValue {
	const gchar *attr_name;
	const gchar *type_values; /* semicolon-delimited upper-case list of used TYPE values */
} AttrTypeValue;

static AttrTypeValue glob_attr_type_values[] = {
	{ EVC_ADR, "WORK;HOME;OTHER" },
	{ EVC_KEY, "PGP;X509" },
	{ EVC_LABEL, "WORK;HOME;OTHER" },
	{ EVC_TEL, "WORK;HOME;CAR;CELL;FAX;ISDN;PAGER;PREF;VOICE;" EVC_X_ASSISTANT ";" EVC_X_CALLBACK ";" EVC_X_COMPANY ";" EVC_X_RADIO ";" EVC_X_TELEX ";" EVC_X_TTYTDD },
	{ EVC_X_AIM, "WORK;HOME" },
	{ EVC_X_GADUGADU, "WORK;HOME" },
	{ EVC_X_GOOGLE_TALK, "WORK;HOME" },
	{ EVC_X_GROUPWISE, "WORK;HOME" },
	{ EVC_X_ICQ, "WORK;HOME" },
	{ EVC_X_JABBER, "WORK;HOME" },
	{ EVC_X_MSN, "WORK;HOME" },
	{ EVC_X_SKYPE, "WORK;HOME" },
	{ EVC_X_YAHOO, "WORK;HOME" },
	{ EVC_X_MATRIX, "WORK;HOME" }
};

#define E_CONTACT_FIELD_TYPE_STRING       0x00000001   /* used for simple single valued attributes */
/*E_CONTACT_FIELD_TYPE_FLOAT*/
#define E_CONTACT_FIELD_TYPE_LIST         0x00000002   /* used for multivalued single attributes - the elements are of type gchar * */
#define E_CONTACT_FIELD_TYPE_MULTI        0x00000004   /* used for multivalued attributes - the elements are of type EVCardAttribute */
#define E_CONTACT_FIELD_TYPE_GETSET       0x00000008   /* used for attributes that need custom handling for getting/setting */
#define E_CONTACT_FIELD_TYPE_STRUCT       0x00000010   /* used for structured types (N and ADR properties, in particular) */
#define E_CONTACT_FIELD_TYPE_BOOLEAN      0x00000020   /* used for boolean types (WANTS_HTML) */

#define E_CONTACT_FIELD_TYPE_SYNTHETIC    0x10000000   /* used when there isn't a corresponding vcard field (such as email_1) */
#define E_CONTACT_FIELD_TYPE_LIST_ELEM    0x20000000   /* used when a synthetic attribute is a numbered list element */
#define E_CONTACT_FIELD_TYPE_MULTI_ELEM   0x40000000   /* used when we're looking for the nth attribute where more than 1 can be present in the vcard */
#define E_CONTACT_FIELD_TYPE_ATTR_TYPE    0x80000000   /* used when a synthetic attribute is flagged with a TYPE= that we'll be looking for */
#define E_CONTACT_FIELD_TYPE_REPLACEABLE  0x01000000   /* used for attributes which can be replaced between 4.0 and 3.0 (using get_full/set_full functions) */

typedef struct _EContactFieldInfo {
	guint32 t;

	EContactField field_id;
	const gchar *vcard_field_name;
	const gchar *field_name;      /* non translated */
	const gchar *pretty_name;     /* translated */

	gboolean read_only;

	gint list_elem;
	const gchar *attr_type1;
	const gchar *attr_type2;

	gpointer  (*struct_getter)(EContact *contact, EVCardAttribute *attribute);
	void (*struct_setter)(EContact *contact, EVCardAttribute *attribute, gpointer data);

	GType (*boxed_type_getter) (void);

	const gchar *fallback_vcard40_field_name;
	gpointer (* get_full)(EVCard *vcard, const struct _EContactFieldInfo *info);
	void (* set_full)(EVCard *vcard, const struct _EContactFieldInfo *info, gpointer data);
} EContactFieldInfo;

static gpointer  photo_getter (EContact *contact, EVCardAttribute *attr);
static void photo_setter (EContact *contact, EVCardAttribute *attr, gpointer data);
static gpointer  geo_getter (EContact *contact, EVCardAttribute *attr);
static void geo_setter (EContact *contact, EVCardAttribute *attr, gpointer data);
static gpointer  fn_getter (EContact *contact, EVCardAttribute *attr);
static void fn_setter (EContact *contact, EVCardAttribute *attr, gpointer data);
static gpointer  n_getter (EContact *contact, EVCardAttribute *attr);
static void n_setter (EContact *contact, EVCardAttribute *attr, gpointer data);
static gpointer  fileas_getter (EContact *contact, EVCardAttribute *attr);
static void fileas_setter (EContact *contact, EVCardAttribute *attr, gpointer data);
static gpointer  adr_getter (EContact *contact, EVCardAttribute *attr);
static void adr_setter (EContact *contact, EVCardAttribute *attr, gpointer data);
static gpointer  date_getter (EContact *contact, EVCardAttribute *attr);
static void date_setter (EContact *contact, EVCardAttribute *attr, gpointer data);
static gpointer  cert_getter (EContact *contact, EVCardAttribute *attr);
static void cert_setter (EContact *contact, EVCardAttribute *attr, gpointer data);
static gpointer datetime_getter (EContact *contact, EVCardAttribute *attr);
static void datetime_setter (EContact *contact, EVCardAttribute *attr, gpointer data);

static gpointer get_adr_label_full (EVCard *vcard, const EContactFieldInfo *info);
static void set_adr_label_full (EVCard *vcard, const EContactFieldInfo *info, gpointer data);
static gpointer get_impp_one_full (EVCard *vcard, const EContactFieldInfo *info);
static void set_impp_one_full (EVCard *vcard, const EContactFieldInfo *info, gpointer data);
static gpointer get_impp_multi_full (EVCard *vcard, const EContactFieldInfo *info);
static void set_impp_multi_full (EVCard *vcard, const EContactFieldInfo *info, gpointer data);
static gpointer get_impp_all (EVCard *vcard, const EContactFieldInfo *info);
static void set_impp_all (EVCard *vcard, const EContactFieldInfo *info, gpointer data);
static gpointer get_is_list_full (EVCard *vcard, const EContactFieldInfo *info);
static void set_is_list_full (EVCard *vcard, const EContactFieldInfo *info, gpointer data);
static gpointer get_kind_full (EVCard *vcard, const EContactFieldInfo *info);
static void set_kind_full (EVCard *vcard, const EContactFieldInfo *info, gpointer data);
static gpointer gender_getter (EContact *contact, EVCardAttribute *attr);
static void gender_setter (EContact *contact, EVCardAttribute *attr, gpointer data);

#define STRING_FIELD(id,vc,n,pn,ro)  { E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define BOOLEAN_FIELD(id,vc,n,pn,ro)  { E_CONTACT_FIELD_TYPE_BOOLEAN, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define LIST_FIELD(id,vc,n,pn,ro)      { E_CONTACT_FIELD_TYPE_LIST, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define MULTI_LIST_FIELD(id,vc,n,pn,ro) { E_CONTACT_FIELD_TYPE_MULTI, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define GETSET_FIELD(id,vc,n,pn,ro,get,set)    { E_CONTACT_FIELD_TYPE_STRING | E_CONTACT_FIELD_TYPE_GETSET, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, (get), (set), NULL, NULL, NULL, NULL }
#define STRUCT_FIELD(id,vc,n,pn,ro,get,set,ty)    { E_CONTACT_FIELD_TYPE_STRUCT | E_CONTACT_FIELD_TYPE_GETSET, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, (get), (set), (ty), NULL, NULL, NULL }
#define SYNTH_STR_FIELD(id,n,pn,ro)  { E_CONTACT_FIELD_TYPE_STRING | E_CONTACT_FIELD_TYPE_SYNTHETIC, (id), NULL, (n), (pn), (ro), -1, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define LIST_ELEM_STR_FIELD(id,vc,n,pn,ro,nm) { E_CONTACT_FIELD_TYPE_LIST_ELEM | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), (nm), NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define MULTI_ELEM_STR_FIELD(id,vc,n,pn,ro,nm) { E_CONTACT_FIELD_TYPE_MULTI_ELEM | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), (nm), NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define ATTR_TYPE_STR_FIELD(id,vc,n,pn,ro,at1,nth) { E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), (nth), (at1), NULL, NULL, NULL, NULL, NULL, NULL, NULL }
#define ATTR_TYPE_GETSET_FIELD(id,vc,n,pn,ro,at1,nth,get,set) { E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_GETSET, (id), (vc), (n), (pn), (ro), (nth), (at1), NULL, (get), (set), NULL, NULL, NULL, NULL }
#define ATTR2_TYPE_STR_FIELD(id,vc,n,pn,ro,at1,at2,nth) { E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), (nth), (at1), (at2), NULL, NULL, NULL, NULL, NULL, NULL }
#define ATTR_TYPE_STRUCT_FIELD(id,vc,n,pn,ro,at,get,set,ty) { E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_SYNTHETIC | E_CONTACT_FIELD_TYPE_GETSET | E_CONTACT_FIELD_TYPE_STRUCT, (id), (vc), (n), (pn), (ro), 0, (at), NULL, (get), (set), (ty), NULL, NULL, NULL }
#define STRING_FIELD_40(id,vc,n,pn,ro,fb)  { E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, NULL, NULL, NULL, (fb), NULL, NULL }
#define STRUCT_FIELD_40(id,vc,n,pn,ro,get,set,ty,fb)    { E_CONTACT_FIELD_TYPE_STRUCT | E_CONTACT_FIELD_TYPE_GETSET, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, (get), (set), (ty), (fb), NULL, NULL }
#define MULTI_LIST_FIELD_40(id,vc,n,pn,ro,fb) { E_CONTACT_FIELD_TYPE_MULTI, (id), (vc), (n), (pn), (ro), -1, NULL, NULL, NULL, NULL, NULL, (fb), NULL, NULL }
#define STRING_FIELD_REPLACEABLE_40(id,vc,n,pn,fb,get,set)  { E_CONTACT_FIELD_TYPE_REPLACEABLE | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), FALSE, -1, NULL, NULL, NULL, NULL, NULL, (fb), (get), (set) }
#define MULTI_LIST_REPLACEABLE_40(id,vc,n,pn,fb,get,set) { E_CONTACT_FIELD_TYPE_REPLACEABLE | E_CONTACT_FIELD_TYPE_MULTI, (id), (vc), (n), (pn), FALSE, -1, NULL, NULL, NULL, NULL, NULL, (fb), (get), (set) }
#define BOOLEAN_FIELD_REPLACEABLE(id,vc,n,pn,get,set)  { E_CONTACT_FIELD_TYPE_REPLACEABLE | E_CONTACT_FIELD_TYPE_BOOLEAN, (id), (vc), (n), (pn), FALSE, -1, NULL, NULL, NULL, NULL, NULL, NULL, (get), (set) }
#define MULTI_LIST_REPLACEABLE(id,vc,n,pn,get,set) { E_CONTACT_FIELD_TYPE_REPLACEABLE | E_CONTACT_FIELD_TYPE_MULTI, (id), (vc), (n), (pn), FALSE, -1, NULL, NULL, NULL, NULL, NULL, NULL, (get), (set) }
#define ATTR_TYPE_FIELD_REPLACEABLE(id,vc,n,pn,at1,nth,get,set) { E_CONTACT_FIELD_TYPE_REPLACEABLE | E_CONTACT_FIELD_TYPE_ATTR_TYPE | E_CONTACT_FIELD_TYPE_STRING, (id), (vc), (n), (pn), FALSE, (nth), (at1), NULL, NULL, NULL, NULL, NULL, (get), (set) }

/* This *must* be kept in the same order as the EContactField enum */
static const EContactFieldInfo field_info[] = {
	{0,}, /* Dummy row as EContactField starts from 1 */
	STRING_FIELD (E_CONTACT_UID,        EVC_UID,       "id",         N_("Unique ID"),  FALSE),
	/* FILE_AS is not really a structured field - we use a getter/setter
	 * so we can generate its value if necessary in the getter */
	/* Translators: This is an EContact field description, in this case it's a
	 * preferred user's description (or display name) of the contact. Note 'File' is a verb here. */
	GETSET_FIELD (E_CONTACT_FILE_AS,    EVC_X_FILE_AS, "file_as", N_("File Under"),    FALSE, fileas_getter, fileas_setter),
	/* URI of the book to which the contact belongs to */
	STRING_FIELD (E_CONTACT_BOOK_UID, EVC_X_BOOK_UID, "book_uid", N_("Book UID"), FALSE),

	/* Name fields */
	/* FN isn't really a structured field - we use a getter/setter
	 * so we can set the N property (since evo 1.4 works fine with
	 * vcards that don't even have a N attribute.  *sigh*) */
	GETSET_FIELD        (E_CONTACT_FULL_NAME,   EVC_FN,       "full_name",   N_("Full Name"),   FALSE, fn_getter, fn_setter),
	LIST_ELEM_STR_FIELD (E_CONTACT_GIVEN_NAME,  EVC_N,        "given_name",  N_("Given Name"),  FALSE, 1),
	LIST_ELEM_STR_FIELD (E_CONTACT_FAMILY_NAME, EVC_N,        "family_name", N_("Family Name"), FALSE, 0),
	STRING_FIELD        (E_CONTACT_NICKNAME,    EVC_NICKNAME, "nickname",    N_("Nickname"),    FALSE),

	/* Email fields */
	MULTI_ELEM_STR_FIELD (E_CONTACT_EMAIL_1,    EVC_EMAIL,        "email_1",    N_("Email 1"),         FALSE, 0),
	MULTI_ELEM_STR_FIELD (E_CONTACT_EMAIL_2,    EVC_EMAIL,        "email_2",    N_("Email 2"),         FALSE, 1),
	MULTI_ELEM_STR_FIELD (E_CONTACT_EMAIL_3,    EVC_EMAIL,        "email_3",    N_("Email 3"),         FALSE, 2),
	MULTI_ELEM_STR_FIELD (E_CONTACT_EMAIL_4,    EVC_EMAIL,        "email_4",    N_("Email 4"),         FALSE, 3),

	STRING_FIELD         (E_CONTACT_MAILER,     EVC_MAILER,       "mailer",     N_("Mailer"),          FALSE),

	/* Address Labels */
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_ADDRESS_LABEL_HOME,  EVC_LABEL, "address_label_home",  N_("Home Address Label"),  "HOME", 0, get_adr_label_full, set_adr_label_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_ADDRESS_LABEL_WORK,  EVC_LABEL, "address_label_work",  N_("Work Address Label"),  "*WORK", 0, get_adr_label_full, set_adr_label_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_ADDRESS_LABEL_OTHER, EVC_LABEL, "address_label_other", N_("Other Address Label"), "OTHER", 0, get_adr_label_full, set_adr_label_full),

	/* Phone fields */
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
	ATTR2_TYPE_STR_FIELD (E_CONTACT_PHONE_OTHER,        EVC_TEL, "other_phone",       N_("Other Phone"),      FALSE, "*VOICE", "",            0),
	ATTR2_TYPE_STR_FIELD (E_CONTACT_PHONE_OTHER_FAX,    EVC_TEL, "other_fax",         N_("Other Fax"),        FALSE, "FAX", "",               0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_PAGER,        EVC_TEL, "pager",             N_("Pager"),            FALSE, "PAGER",                 0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_PRIMARY,      EVC_TEL, "primary_phone",     N_("Primary Phone"),    FALSE, "PREF",                  0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_RADIO,        EVC_TEL, "radio",             N_("Radio"),            FALSE, EVC_X_RADIO,     0),
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_TELEX,        EVC_TEL, "telex",             N_("Telex"),            FALSE, EVC_X_TELEX,     0),
	/* To translators: TTY is Teletypewriter */
	ATTR_TYPE_STR_FIELD  (E_CONTACT_PHONE_TTYTDD,       EVC_TEL, "tty",               N_("TTY"),              FALSE, EVC_X_TTYTDD,    0),

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

	/* Contact categories */
	SYNTH_STR_FIELD (E_CONTACT_CATEGORIES,                    "categories",    N_("Categories"),    FALSE),

	/* Collaboration fields */
	STRING_FIELD (E_CONTACT_CALENDAR_URI, EVC_CALURI,      "caluri",     N_("Calendar URI"),  FALSE),
	STRING_FIELD (E_CONTACT_FREEBUSY_URL, EVC_FBURL,       "fburl",       N_("Free/Busy URL"), FALSE),
	STRING_FIELD (E_CONTACT_ICS_CALENDAR, EVC_ICSCALENDAR, "icscalendar", N_("ICS Calendar"),  FALSE),
	STRING_FIELD (E_CONTACT_VIDEO_URL,    EVC_X_VIDEO_URL, "video_url",    N_("Video Conferencing URL"),   FALSE),

	/* Misc fields */
	STRING_FIELD (E_CONTACT_SPOUSE, EVC_X_SPOUSE,    "spouse", N_("Spouse’s Name"), FALSE),
	STRING_FIELD (E_CONTACT_NOTE,   EVC_NOTE,        "note",   N_("Note"),          FALSE),

	/* Instant messaging fields */
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_AIM_HOME_1,    EVC_X_AIM,    "im_aim_home_1",    N_("AIM Home Screen Name 1"),    "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_AIM_HOME_2,    EVC_X_AIM,    "im_aim_home_2",    N_("AIM Home Screen Name 2"),    "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_AIM_HOME_3,    EVC_X_AIM,    "im_aim_home_3",    N_("AIM Home Screen Name 3"),    "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_AIM_WORK_1,    EVC_X_AIM,    "im_aim_work_1",    N_("AIM Work Screen Name 1"),    "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_AIM_WORK_2,    EVC_X_AIM,    "im_aim_work_2",    N_("AIM Work Screen Name 2"),    "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_AIM_WORK_3,    EVC_X_AIM,    "im_aim_work_3",    N_("AIM Work Screen Name 3"),    "WORK", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GROUPWISE_HOME_1, EVC_X_GROUPWISE, "im_groupwise_home_1", N_("GroupWise Home Screen Name 1"),    "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GROUPWISE_HOME_2, EVC_X_GROUPWISE, "im_groupwise_home_2", N_("GroupWise Home Screen Name 2"),    "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GROUPWISE_HOME_3, EVC_X_GROUPWISE, "im_groupwise_home_3", N_("GroupWise Home Screen Name 3"),    "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GROUPWISE_WORK_1, EVC_X_GROUPWISE, "im_groupwise_work_1", N_("GroupWise Work Screen Name 1"),    "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GROUPWISE_WORK_2, EVC_X_GROUPWISE, "im_groupwise_work_2", N_("GroupWise Work Screen Name 2"),    "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GROUPWISE_WORK_3, EVC_X_GROUPWISE, "im_groupwise_work_3", N_("GroupWise Work Screen Name 3"),    "WORK", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_JABBER_HOME_1, EVC_X_JABBER, "im_jabber_home_1", N_("Jabber Home ID 1"),          "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_JABBER_HOME_2, EVC_X_JABBER, "im_jabber_home_2", N_("Jabber Home ID 2"),          "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_JABBER_HOME_3, EVC_X_JABBER, "im_jabber_home_3", N_("Jabber Home ID 3"),          "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_JABBER_WORK_1, EVC_X_JABBER, "im_jabber_work_1", N_("Jabber Work ID 1"),          "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_JABBER_WORK_2, EVC_X_JABBER, "im_jabber_work_2", N_("Jabber Work ID 2"),          "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_JABBER_WORK_3, EVC_X_JABBER, "im_jabber_work_3", N_("Jabber Work ID 3"),          "WORK", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_YAHOO_HOME_1,  EVC_X_YAHOO,  "im_yahoo_home_1",  N_("Yahoo! Home Screen Name 1"), "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_YAHOO_HOME_2,  EVC_X_YAHOO,  "im_yahoo_home_2",  N_("Yahoo! Home Screen Name 2"), "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_YAHOO_HOME_3,  EVC_X_YAHOO,  "im_yahoo_home_3",  N_("Yahoo! Home Screen Name 3"), "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_YAHOO_WORK_1,  EVC_X_YAHOO,  "im_yahoo_work_1",  N_("Yahoo! Work Screen Name 1"), "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_YAHOO_WORK_2,  EVC_X_YAHOO,  "im_yahoo_work_2",  N_("Yahoo! Work Screen Name 2"), "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_YAHOO_WORK_3,  EVC_X_YAHOO,  "im_yahoo_work_3",  N_("Yahoo! Work Screen Name 3"), "WORK", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MSN_HOME_1,    EVC_X_MSN,    "im_msn_home_1",    N_("MSN Home Screen Name 1"),    "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MSN_HOME_2,    EVC_X_MSN,    "im_msn_home_2",    N_("MSN Home Screen Name 2"),    "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MSN_HOME_3,    EVC_X_MSN,    "im_msn_home_3",    N_("MSN Home Screen Name 3"),    "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MSN_WORK_1,    EVC_X_MSN,    "im_msn_work_1",    N_("MSN Work Screen Name 1"),    "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MSN_WORK_2,    EVC_X_MSN,    "im_msn_work_2",    N_("MSN Work Screen Name 2"),    "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MSN_WORK_3,    EVC_X_MSN,    "im_msn_work_3",    N_("MSN Work Screen Name 3"),    "WORK", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_ICQ_HOME_1,    EVC_X_ICQ,    "im_icq_home_1",    N_("ICQ Home ID 1"),             "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_ICQ_HOME_2,    EVC_X_ICQ,    "im_icq_home_2",    N_("ICQ Home ID 2"),             "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_ICQ_HOME_3,    EVC_X_ICQ,    "im_icq_home_3",    N_("ICQ Home ID 3"),             "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_ICQ_WORK_1,    EVC_X_ICQ,    "im_icq_work_1",    N_("ICQ Work ID 1"),             "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_ICQ_WORK_2,    EVC_X_ICQ,    "im_icq_work_2",    N_("ICQ Work ID 2"),             "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_ICQ_WORK_3,    EVC_X_ICQ,    "im_icq_work_3",    N_("ICQ Work ID 3"),             "WORK", 2, get_impp_one_full, set_impp_one_full),

	/* Last modified time */
	STRING_FIELD (E_CONTACT_REV, EVC_REV, "Rev", N_("Last Revision"), FALSE),
	/* Translators: This is an EContact field description, in this case it's a
	 * virtual field, which returns either name of the contact or the organization
	 * name, recognized by multiple other fields, where the first filled is used. */
	SYNTH_STR_FIELD     (E_CONTACT_NAME_OR_ORG,               "name_or_org", N_("Name or Org"), TRUE),

	/* Address fields */
	MULTI_LIST_FIELD       (E_CONTACT_ADDRESS,       EVC_ADR, "address",       N_("Address List"),  FALSE),
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_ADDRESS_HOME,  EVC_ADR, "address_home",  N_("Home Address"),  FALSE, "HOME",  adr_getter, adr_setter, e_contact_address_get_type),
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_ADDRESS_WORK,  EVC_ADR, "address_work",  N_("Work Address"),  FALSE, "*WORK",  adr_getter, adr_setter, e_contact_address_get_type),
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_ADDRESS_OTHER, EVC_ADR, "address_other", N_("Other Address"), FALSE, "OTHER", adr_getter, adr_setter, e_contact_address_get_type),

	/* Contact categories */
	LIST_FIELD      (E_CONTACT_CATEGORY_LIST, EVC_CATEGORIES, "category_list", N_("Category List"), FALSE),

	/* Photo/Logo */
	STRUCT_FIELD    (E_CONTACT_PHOTO, EVC_PHOTO, "photo", N_("Photo"), FALSE, photo_getter, photo_setter, e_contact_photo_get_type),
	STRUCT_FIELD    (E_CONTACT_LOGO,  EVC_LOGO,  "logo",  N_("Logo"),  FALSE, photo_getter, photo_setter, e_contact_photo_get_type),

	/* Translators: This is an EContact field description, in this case it's a name
	 * of the contact, as specified in http://tools.ietf.org/html/rfc6350#section-6.2.2 */
	STRUCT_FIELD        (E_CONTACT_NAME,        EVC_N,        "name", N_("Name"),        FALSE, n_getter, n_setter, e_contact_name_get_type),
	MULTI_LIST_FIELD     (E_CONTACT_EMAIL,      EVC_EMAIL,        "email",      N_("Email List"),      FALSE),

	/* Instant messaging fields */
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_AIM,       EVC_X_AIM,       "im_aim",       N_("AIM Screen Name List"), get_impp_multi_full, set_impp_multi_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_GROUPWISE, EVC_X_GROUPWISE, "im_groupwise", N_("GroupWise ID List"),    get_impp_multi_full, set_impp_multi_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_JABBER,	EVC_X_JABBER,    "im_jabber",    N_("Jabber ID List"),       get_impp_multi_full, set_impp_multi_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_YAHOO,	EVC_X_YAHOO,     "im_yahoo",     N_("Yahoo! Screen Name List"), get_impp_multi_full, set_impp_multi_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_MSN,	EVC_X_MSN,       "im_msn",       N_("MSN Screen Name List"), get_impp_multi_full, set_impp_multi_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_ICQ,	EVC_X_ICQ,       "im_icq",       N_("ICQ ID List"),          get_impp_multi_full, set_impp_multi_full),

	BOOLEAN_FIELD        (E_CONTACT_WANTS_HTML, EVC_X_WANTS_HTML, "wants_html", N_("Wants HTML Mail"), FALSE),

	/* Translators: This is an EContact field description, in this case it's a
	 * field describing whether it's a Contact list (list of email addresses) or a
	 * regular contact for one person/organization/... */
	BOOLEAN_FIELD_REPLACEABLE (E_CONTACT_IS_LIST,             EVC_X_LIST, "list", N_("List"), get_is_list_full, set_is_list_full),
	/* Translators: This is an EContact field description, in this case it's a flag
	 * used to determine whether when sending to Contact lists the addresses should be
	 * shown or not to other recipients - basically whether to use BCC field or CC
	 * message header when sending messages to this Contact list. */
	BOOLEAN_FIELD (E_CONTACT_LIST_SHOW_ADDRESSES, EVC_X_LIST_SHOW_ADDRESSES, "list_show_addresses", N_("List Shows Addresses"), FALSE),

	STRUCT_FIELD (E_CONTACT_BIRTH_DATE,  EVC_BDAY,          "birth_date",  N_("Birth Date"), FALSE, date_getter, date_setter, e_contact_date_get_type),
	STRUCT_FIELD_40 (E_CONTACT_ANNIVERSARY,  EVC_ANNIVERSARY, "anniversary", N_("Anniversary"), FALSE, date_getter, date_setter, e_contact_date_get_type, EVC_X_ANNIVERSARY),

	/* Security fields */
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_X509_CERT,  EVC_KEY, "x509Cert",  N_("X.509 Certificate"), FALSE, "X509", cert_getter, cert_setter, e_contact_cert_get_type),
	ATTR_TYPE_STRUCT_FIELD (E_CONTACT_PGP_CERT,   EVC_KEY, "pgpCert",  N_("PGP Certificate"), FALSE, "PGP", cert_getter, cert_setter, e_contact_cert_get_type),

	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GADUGADU_HOME_1,  EVC_X_GADUGADU,  "im_gadugadu_home_1",  N_("Gadu-Gadu Home ID 1"), "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GADUGADU_HOME_2,  EVC_X_GADUGADU,  "im_gadugadu_home_2",  N_("Gadu-Gadu Home ID 2"), "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GADUGADU_HOME_3,  EVC_X_GADUGADU,  "im_gadugadu_home_3",  N_("Gadu-Gadu Home ID 3"), "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GADUGADU_WORK_1,  EVC_X_GADUGADU,  "im_gadugadu_work_1",  N_("Gadu-Gadu Work ID 1"), "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GADUGADU_WORK_2,  EVC_X_GADUGADU,  "im_gadugadu_work_2",  N_("Gadu-Gadu Work ID 2"), "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GADUGADU_WORK_3,  EVC_X_GADUGADU,  "im_gadugadu_work_3",  N_("Gadu-Gadu Work ID 3"), "WORK", 2, get_impp_one_full, set_impp_one_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_GADUGADU,  EVC_X_GADUGADU,  "im_gadugadu", N_("Gadu-Gadu ID List"), get_impp_multi_full, set_impp_multi_full),

	/* Geo information */
	STRUCT_FIELD	(E_CONTACT_GEO,  EVC_GEO, "geo",  N_("Geographic Information"),  FALSE, geo_getter, geo_setter, e_contact_geo_get_type),

	MULTI_LIST_FIELD     (E_CONTACT_TEL,      EVC_TEL,        "phone",      N_("Telephone"),      FALSE),

	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_SKYPE_HOME_1,  EVC_X_SKYPE,  "im_skype_home_1",  N_("Skype Home Name 1"), "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_SKYPE_HOME_2,  EVC_X_SKYPE,  "im_skype_home_2",  N_("Skype Home Name 2"), "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_SKYPE_HOME_3,  EVC_X_SKYPE,  "im_skype_home_3",  N_("Skype Home Name 3"), "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_SKYPE_WORK_1,  EVC_X_SKYPE,  "im_skype_work_1",  N_("Skype Work Name 1"), "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_SKYPE_WORK_2,  EVC_X_SKYPE,  "im_skype_work_2",  N_("Skype Work Name 2"), "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_SKYPE_WORK_3,  EVC_X_SKYPE,  "im_skype_work_3",  N_("Skype Work Name 3"), "WORK", 2, get_impp_one_full, set_impp_one_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_SKYPE, EVC_X_SKYPE, "im_skype", N_("Skype Name List"), get_impp_multi_full, set_impp_multi_full),

	MULTI_LIST_FIELD (E_CONTACT_SIP, EVC_X_SIP, "sip", N_("SIP address"), FALSE),

	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GOOGLE_TALK_HOME_1,  EVC_X_GOOGLE_TALK,  "im_google_talk_home_1",  N_("Google Talk Home Name 1"), "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GOOGLE_TALK_HOME_2,  EVC_X_GOOGLE_TALK,  "im_google_talk_home_2",  N_("Google Talk Home Name 2"), "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GOOGLE_TALK_HOME_3,  EVC_X_GOOGLE_TALK,  "im_google_talk_home_3",  N_("Google Talk Home Name 3"), "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GOOGLE_TALK_WORK_1,  EVC_X_GOOGLE_TALK,  "im_google_talk_work_1",  N_("Google Talk Work Name 1"), "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GOOGLE_TALK_WORK_2,  EVC_X_GOOGLE_TALK,  "im_google_talk_work_2",  N_("Google Talk Work Name 2"), "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_GOOGLE_TALK_WORK_3,  EVC_X_GOOGLE_TALK,  "im_google_talk_work_3",  N_("Google Talk Work Name 3"), "WORK", 2, get_impp_one_full, set_impp_one_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_GOOGLE_TALK, EVC_X_GOOGLE_TALK, "im_google_talk", N_("Google Talk Name List"), get_impp_multi_full, set_impp_multi_full),

	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_TWITTER, EVC_X_TWITTER, "im_twitter", N_("Twitter Name List"), get_impp_multi_full, set_impp_multi_full),

	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MATRIX_HOME_1, EVC_X_MATRIX, "im_matrix_home_1", N_("Matrix Home ID 1"), "HOME", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MATRIX_HOME_2, EVC_X_MATRIX, "im_matrix_home_2", N_("Matrix Home ID 2"), "HOME", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MATRIX_HOME_3, EVC_X_MATRIX, "im_matrix_home_3", N_("Matrix Home ID 3"), "HOME", 2, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MATRIX_WORK_1, EVC_X_MATRIX, "im_matrix_work_1", N_("Matrix Work ID 1"), "WORK", 0, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MATRIX_WORK_2, EVC_X_MATRIX, "im_matrix_work_2", N_("Matrix Work ID 2"), "WORK", 1, get_impp_one_full, set_impp_one_full),
	ATTR_TYPE_FIELD_REPLACEABLE (E_CONTACT_IM_MATRIX_WORK_3, EVC_X_MATRIX, "im_matrix_work_3", N_("Matrix Work ID 3"), "WORK", 2, get_impp_one_full, set_impp_one_full),
	MULTI_LIST_REPLACEABLE (E_CONTACT_IM_MATRIX, EVC_X_MATRIX, "im_matrix", N_("Matrix ID List"), get_impp_multi_full, set_impp_multi_full),

	/* vCard 4.0 */
	STRING_FIELD_REPLACEABLE_40 (E_CONTACT_KIND, EVC_KIND, "kind", N_("Kind"), EVC_X_EVOLUTION_KIND, get_kind_full, set_kind_full),
	STRING_FIELD_40 (E_CONTACT_SOURCE, EVC_SOURCE, "source", N_("Source"), FALSE, EVC_X_EVOLUTION_SOURCE),
	STRING_FIELD_40 (E_CONTACT_XML, EVC_XML, "XML", N_("XML"), FALSE, EVC_X_EVOLUTION_XML),
	STRUCT_FIELD_40 (E_CONTACT_GENDER, EVC_GENDER, "gender", N_("Gender"), FALSE, gender_getter, gender_setter, e_contact_gender_get_type, EVC_X_EVOLUTION_GENDER),
	MULTI_LIST_REPLACEABLE_40 (E_CONTACT_IMPP, EVC_IMPP, "impp", N_("Instant Messaging"), EVC_X_EVOLUTION_IMPP, get_impp_all, set_impp_all),
	MULTI_LIST_FIELD_40 (E_CONTACT_LANG, EVC_LANG, "lang", N_("Language"), FALSE, EVC_X_EVOLUTION_LANG),
	MULTI_LIST_FIELD_40 (E_CONTACT_MEMBER, EVC_MEMBER, "member", N_("Member"), FALSE, EVC_X_EVOLUTION_MEMBER),
	MULTI_LIST_FIELD_40 (E_CONTACT_RELATED, EVC_RELATED, "related", N_("Related"), FALSE, EVC_X_EVOLUTION_RELATED),
	STRING_FIELD_40 (E_CONTACT_BIRTHPLACE, EVC_BIRTHPLACE, "birth_place", N_("Birth Place"), FALSE, EVC_X_EVOLUTION_BIRTHPLACE),
	STRING_FIELD_40 (E_CONTACT_DEATHPLACE, EVC_DEATHPLACE, "death_place", N_("Death Place"), FALSE, EVC_X_EVOLUTION_DEATHPLACE),
	STRUCT_FIELD_40 (E_CONTACT_DEATHDATE,  EVC_DEATHDATE, "death_date",  N_("Death Date"), FALSE, date_getter, date_setter, e_contact_date_get_type, EVC_X_EVOLUTION_DEATHDATE),
	MULTI_LIST_FIELD_40 (E_CONTACT_EXPERTISE, EVC_EXPERTISE, "expertise", N_("Expertise"), FALSE, EVC_X_EVOLUTION_EXPERTISE),
	MULTI_LIST_FIELD_40 (E_CONTACT_HOBBY, EVC_HOBBY, "hobby", N_("Hobby"), FALSE, EVC_X_EVOLUTION_HOBBY),
	MULTI_LIST_FIELD_40 (E_CONTACT_INTEREST, EVC_INTEREST, "interest", N_("Interest"), FALSE, EVC_X_EVOLUTION_INTEREST),
	MULTI_LIST_FIELD_40 (E_CONTACT_ORG_DIRECTORY, EVC_ORG_DIRECTORY, "org_directory", N_("Organizational Directory"), FALSE, EVC_X_EVOLUTION_ORG_DIRECTORY),
	STRING_FIELD_40 (E_CONTACT_CONTACT_URI, EVC_CONTACT_URI, "contact_uri", N_("Contact URI"), FALSE, EVC_X_EVOLUTION_CONTACT_URI),
	STRUCT_FIELD_40 (E_CONTACT_CREATED,  EVC_CREATED, "created",  N_("Created"), FALSE, datetime_getter, datetime_setter, e_contact_date_time_get_type, EVC_X_EVOLUTION_CREATED),
	STRING_FIELD_40 (E_CONTACT_SOCIALPROFILE, EVC_SOCIALPROFILE, "social_profile", N_("Social Profile"), FALSE, EVC_X_SOCIALPROFILE)
};

#undef STRING_FIELD_40
#undef STRUCT_FIELD_40
#undef MULTI_LIST_FIELD_40
#undef LIST_ELEM_STR_FIELD
#undef STRING_FIELD
#undef SYNTH_STR_FIELD
#undef LIST_FIELD
#undef GETSET_FIELD

static const AttrTypeValue *
e_contact_find_attr_type_values (const gchar *attr_name)
{
	gint ii;

	g_return_val_if_fail (attr_name != NULL, NULL);

	for (ii = 0; ii < G_N_ELEMENTS (glob_attr_type_values); ii++) {
		if (g_ascii_strcasecmp (glob_attr_type_values[ii].attr_name, attr_name) == 0) {
			return &(glob_attr_type_values[ii]);
		}
	}

	g_warn_if_reached ();

	return NULL;
}

static gboolean
e_contact_check_attr_type_value_used (const AttrTypeValue *attr_type_values,
				      const gchar *type_value)
{
	gint ii, pos;

	if (!attr_type_values)
		return TRUE;

	if (!type_value || !*type_value)
		return FALSE;

	pos = 0;

	for (ii = 0; attr_type_values->type_values[ii]; ii++) {
		gboolean skip = FALSE;

		if (attr_type_values->type_values[ii] == g_ascii_toupper (type_value[pos])) {
			pos++;

			if (!type_value[pos]) {
				if (attr_type_values->type_values[ii + 1] == 0 || attr_type_values->type_values[ii + 1] == ';')
					return TRUE;

				skip = TRUE;
			}
		} else {
			skip = TRUE;
		}

		if (skip) {
			pos = 0;

			while (attr_type_values->type_values[ii] && attr_type_values->type_values[ii] != ';') {
				/* To avoid buffer overflow, where the 'for' itself also ii++ */
				if (!attr_type_values->type_values[ii + 1])
					break;
				ii++;
			}
		}
	}

	return FALSE;
}

#ifdef ENABLE_MAINTAINER_MODE

static void
e_contact_maybe_insert_attr_type (GHashTable *used_attr_types,
				  const gchar *vcard_field_name,
				  const gchar *type_value)
{
	g_return_if_fail (used_attr_types != NULL);
	g_return_if_fail (vcard_field_name != NULL);

	/* Skip leading '*', it denotes default value */
	if (type_value && *type_value == '*')
		type_value++;

	if (type_value && *type_value) {
		GHashTable *types_hash;

		types_hash = g_hash_table_lookup (used_attr_types, vcard_field_name);

		if (!types_hash) {
			types_hash = g_hash_table_new (g_str_hash, g_str_equal);

			g_hash_table_insert (used_attr_types, (gpointer) vcard_field_name, types_hash);
		}

		g_hash_table_insert (types_hash, (gpointer) type_value, NULL);
	}
}

static gint
e_contact_cmp_type_values (gconstpointer v1,
			   gconstpointer v2)
{
	const gchar *type1 = v1, *type2 = v2;
	gint who1, who2;

	/* Work/Home/Other has precedence over others, in this order */
	if (g_str_equal (type1, "WORK"))
		who1 = 3;
	else if (g_str_equal (type1, "HOME"))
		who1 = 2;
	else if (g_str_equal (type1, "OTHER"))
		who1 = 1;
	else
		who1 = -1;

	if (g_str_equal (type2, "WORK"))
		who2 = 3;
	else if (g_str_equal (type2, "HOME"))
		who2 = 2;
	else if (g_str_equal (type2, "OTHER"))
		who2 = 1;
	else
		who2 = -1;

	if (who1 == -1) {
		if (who2 == -1)
			return g_strcmp0 (type1, type2);

		return 1;
	}

	if (who2 == -1)
		return -1;

	return who2 - who1;
}

#endif /* ENABLE_MAINTAINER_MODE */

static void e_contact_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void e_contact_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void
e_contact_finalize (GObject *object)
{
	EContactPrivate *priv;
	gint ii;

	priv = E_CONTACT (object)->priv;

	for (ii = E_CONTACT_FIELD_FIRST; ii < E_CONTACT_FIELD_LAST; ii++)
		g_free (priv->cached_strings[ii]);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_contact_parent_class)->finalize (object);
}

static void
e_contact_class_init (EContactClass *class)
{
	GObjectClass *object_class;
	#ifdef ENABLE_MAINTAINER_MODE
	GHashTable *used_attr_types; /* gchar *attr_name ~> GHashTable { gchar *type_value ~> NULL } */
	GHashTableIter iter;
	gpointer key, value;
	#endif
	gint ii;

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = e_contact_set_property;
	object_class->get_property = e_contact_get_property;
	object_class->finalize = e_contact_finalize;

	#ifdef ENABLE_MAINTAINER_MODE
	used_attr_types = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) g_hash_table_destroy);
	#endif

	for (ii = E_CONTACT_FIELD_FIRST; ii < E_CONTACT_FIELD_LAST; ii++) {
		GParamSpec *pspec = NULL;
		GParamFlags flags;

		/* Verify the table is correctly ordered */
		g_return_if_fail (ii == field_info[ii].field_id);

		flags = G_PARAM_READABLE |
			G_PARAM_STATIC_NICK |
			G_PARAM_STATIC_BLURB;

		if (!field_info[ii].read_only)
			flags |= G_PARAM_WRITABLE;

		if (field_info[ii].t & E_CONTACT_FIELD_TYPE_STRING)
			pspec = g_param_spec_string (
				field_info[ii].field_name,
				_(field_info[ii].pretty_name),
				field_info[ii].pretty_name,
				NULL,
				flags);
		else if (field_info[ii].t & E_CONTACT_FIELD_TYPE_BOOLEAN)
			pspec = g_param_spec_boolean (
				field_info[ii].field_name,
				_(field_info[ii].pretty_name),
				field_info[ii].pretty_name,
				FALSE,
				flags);
		else if (field_info[ii].t & E_CONTACT_FIELD_TYPE_STRUCT)
			pspec = g_param_spec_boxed (
				field_info[ii].field_name,
				_(field_info[ii].pretty_name),
				field_info[ii].pretty_name,
				field_info[ii].boxed_type_getter (),
				flags);
		else if (field_info[ii].t & E_CONTACT_FIELD_TYPE_MULTI)
			pspec = g_param_spec_boxed (
				field_info[ii].field_name,
				_(field_info[ii].pretty_name),
				field_info[ii].pretty_name,
				E_TYPE_CONTACT_ATTR_LIST,
				flags);
		else
			pspec = g_param_spec_pointer (
				field_info[ii].field_name,
				_(field_info[ii].pretty_name),
				field_info[ii].pretty_name,
				flags);

		g_object_class_install_property (
			object_class, field_info[ii].field_id, pspec);

		#ifdef ENABLE_MAINTAINER_MODE
		if (field_info[ii].t & E_CONTACT_FIELD_TYPE_ATTR_TYPE) {
			e_contact_maybe_insert_attr_type (used_attr_types, field_info[ii].vcard_field_name, field_info[ii].attr_type1);
			e_contact_maybe_insert_attr_type (used_attr_types, field_info[ii].vcard_field_name, field_info[ii].attr_type2);
		}
		#endif
	}

	/* To verify whether the static glob_attr_type_values array is filled properly */
	#ifdef ENABLE_MAINTAINER_MODE

	g_assert_cmpint (g_hash_table_size (used_attr_types), ==, G_N_ELEMENTS (glob_attr_type_values));

	g_hash_table_iter_init (&iter, used_attr_types);

	while (g_hash_table_iter_next (&iter, &key, &value)) {
		GHashTableIter iter2;
		gpointer key2;
		GSList *values = NULL, *link;
		GString *expected;
		const AttrTypeValue *attr_data;

		g_hash_table_iter_init (&iter2, value);
		while (g_hash_table_iter_next (&iter2, &key2, NULL)) {
			const gchar *type_value = key2;

			for (ii = 0; type_value && type_value[ii]; ii++) {
				/* Verify strings are in upper-case */
				g_assert_cmpint (g_ascii_toupper (type_value[ii]), ==, type_value[ii]);
			}

			values = g_slist_prepend (values, (gpointer) type_value);
		}

		values = g_slist_sort (values, e_contact_cmp_type_values);

		expected = g_string_new ("");

		for (link = values; link; link = g_slist_next (link)) {
			g_string_append (expected, link->data);

			if (link->next)
				g_string_append_c (expected, ';');
		}

		attr_data = e_contact_find_attr_type_values (key);
		g_assert_nonnull (attr_data);
		g_assert_cmpstr (attr_data->type_values, ==, expected->str);

		g_slist_free (values);
		g_string_free (expected, TRUE);
	}

	g_hash_table_destroy (used_attr_types);

	#endif /* ENABLE_MAINTAINER_MODE */
}

static void
e_contact_init (EContact *ec)
{
	ec->priv = e_contact_get_instance_private (ec);
}

static gpointer
geo_getter (EContact *contact,
            EVCardAttribute *attr)
{
	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);
		EContactGeo *geo = g_new0 (EContactGeo, 1);

		geo->latitude = (p && p->data ? g_ascii_strtod (p->data, NULL) : 0); if (p) p = p->next;
		geo->longitude = (p && p->data ? g_ascii_strtod (p->data, NULL) : 0);

		return geo;
	}
	else
		return NULL;
}

static void
geo_setter (EContact *contact,
            EVCardAttribute *attr,
            gpointer data)
{
	EContactGeo *geo = data;
	gchar buf[G_ASCII_DTOSTR_BUF_SIZE];

	e_vcard_attribute_add_value
		(attr, g_ascii_dtostr (buf, sizeof (buf), geo->latitude));

	e_vcard_attribute_add_value
		(attr, g_ascii_dtostr (buf, sizeof (buf), geo->longitude));
}

static gpointer
photo_getter (EContact *contact,
              EVCardAttribute *attr)
{
	GList *values;

	if (!attr)
		return NULL;

	values = e_vcard_attribute_get_param (attr, EVC_ENCODING);
	if (values && (g_ascii_strcasecmp (values->data, "b") == 0 ||
		       /* second for photo vCard 2.1 support */
		       g_ascii_strcasecmp (values->data, "base64") == 0)) {
		values = e_vcard_attribute_get_values_decoded (attr);
		if (values && values->data) {
			GString *s = values->data;
			EContactPhoto *photo;

			if (!s->len)
				return NULL;

			photo = g_new0 (EContactPhoto, 1);
			photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
			photo->data.inlined.length = s->len;
			photo->data.inlined.data = g_malloc (photo->data.inlined.length);
			memcpy (photo->data.inlined.data, s->str, photo->data.inlined.length);

			values = e_vcard_attribute_get_param (attr, EVC_TYPE);
			if (values && values->data)
				photo->data.inlined.mime_type = g_strdup_printf ("image/%s", (gchar *) values->data);
			return photo;
		}
	}

	if (e_vcard_attribute_is_single_valued (attr)) {
		const gchar *value;

		values = e_vcard_attribute_get_values (attr);
		value = values ? values->data : NULL;

		if (value && g_ascii_strncasecmp (value, "data:", 5) == 0) {
			const gchar *data_start = NULL;
			gchar *mime_type = NULL;
			gboolean is_base64 = FALSE;

			if (e_util_split_data_uri (value, &mime_type, NULL, &is_base64, &data_start) && is_base64) {
				EContactPhoto *photo;
				gsize len = 0;

				photo = g_new0 (EContactPhoto, 1);
				photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
				photo->data.inlined.data = g_base64_decode (data_start, &len);
				photo->data.inlined.length = len;
				if (mime_type)
					photo->data.inlined.mime_type = g_steal_pointer (&mime_type);

				return photo;
			}

			g_free (mime_type);
		}
	}

	values = e_vcard_attribute_get_param (attr, EVC_VALUE);
	if ((values && g_ascii_strcasecmp (values->data, "uri") == 0) ||
	    e_vcard_get_version (E_VCARD (contact)) >= E_VCARD_VERSION_40) {
		EContactPhoto *photo;
		photo = g_new0 (EContactPhoto, 1);
		photo->type = E_CONTACT_PHOTO_TYPE_URI;
		photo->data.uri = e_vcard_attribute_get_value (attr);
		return photo;
	}
	return NULL;
}

static void
photo_setter (EContact *contact,
              EVCardAttribute *attr,
              gpointer data)
{
	EContactPhoto *photo = data;
	const gchar *image_type, *p;

	switch (photo->type) {
	case E_CONTACT_PHOTO_TYPE_INLINED:
		g_return_if_fail (photo->data.inlined.length > 0);

		if (e_vcard_get_version (E_VCARD (contact)) <= E_VCARD_VERSION_30) {
			e_vcard_attribute_add_param_with_value (
				attr,
				e_vcard_attribute_param_new (EVC_ENCODING),
				"b");
			if (photo->data.inlined.mime_type && (p = strchr (photo->data.inlined.mime_type, '/'))) {
				image_type = p + 1;
			} else {
				image_type = "X-EVOLUTION-UNKNOWN";
			}
			e_vcard_attribute_add_param_with_value (
				attr,
				e_vcard_attribute_param_new (EVC_TYPE),
				image_type);

			e_vcard_attribute_add_value_decoded (attr, (gchar *) photo->data.inlined.data, photo->data.inlined.length);
		} else {
			GString *value;
			gchar *encoded;

			encoded = g_base64_encode ((guchar *) photo->data.inlined.data, photo->data.inlined.length);

			value = g_string_new ("data:");
			if (photo->data.inlined.mime_type) {
				g_string_append (value, photo->data.inlined.mime_type);
				g_string_append_c (value, ';');
			}

			g_string_append (value, "base64,");
			g_string_append (value, encoded);

			e_vcard_attribute_add_value_take (attr, g_string_free (value, FALSE));

			g_free (encoded);
		}
		break;
	case E_CONTACT_PHOTO_TYPE_URI:
		if (e_vcard_get_version (E_VCARD (contact)) < E_VCARD_VERSION_40)
			e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_VALUE), "uri");

		e_vcard_attribute_add_value (attr, photo->data.uri);
		break;
	default:
		g_warning ("Unknown EContactPhotoType %d", photo->type);
		break;
	}
}

static gpointer
fn_getter (EContact *contact,
           EVCardAttribute *attr)
{
	/* this fills FN, if not there yet */
	if (!attr) {
		EContactName *name;

		name = e_contact_get (contact, E_CONTACT_NAME);
		if (name)
			e_contact_name_free (name);

		attr = e_vcard_get_attribute (E_VCARD (contact), EVC_FN);
	}

	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);

		return p && p->data ? p->data : (gpointer) "";
	} else
		return NULL;
}

static void
fn_setter (EContact *contact,
           EVCardAttribute *attr,
           gpointer data)
{
	gchar *name_str = data;

	e_vcard_attribute_add_value (attr, name_str);

	attr = e_vcard_get_attribute (E_VCARD (contact), EVC_N);
	if (!attr) {
		EContactName *name = e_contact_name_from_string ((gchar *) data);

		attr = e_vcard_attribute_new (NULL, EVC_N);
		e_vcard_append_attribute (E_VCARD (contact), attr);

		/* call the setter directly */
		n_setter (contact, attr, name);

		e_contact_name_free (name);
	}
}

static gpointer
fileas_getter (EContact *contact,
               EVCardAttribute *attr)
{
	GList *p = NULL;

	if (attr) {
		p = e_vcard_attribute_get_values (attr);
		if (!p || !p->data || !*((const gchar *) p->data))
			p = NULL;
	}

	if (!p) {
		/* Generate a FILE_AS field */
		const gchar *full_name = e_contact_get_const (contact, E_CONTACT_FULL_NAME);
		gchar *new_file_as = NULL;

		/* Full Name is close to what the user prefers */
		if (full_name && *full_name)
			new_file_as = g_strdup (full_name);

		if (!new_file_as) {
			EContactName *name = e_contact_get (contact, E_CONTACT_NAME);

			/* Use name if available */
			if (name) {
				gchar *strings[3], **stringptr;

				stringptr = strings;
				if (name->family && *name->family)
					*(stringptr++) = name->family;
				if (name->given && *name->given)
					*(stringptr++) = name->given;
				if (stringptr != strings) {
					*stringptr = NULL;
					new_file_as = g_strjoinv (", ", strings);
				}

				e_contact_name_free (name);
			}
		}

		/* Use org as fallback */
		if (!new_file_as) {
			const gchar *org = e_contact_get_const (contact, E_CONTACT_ORG);

			if (org && *org) {
				new_file_as = g_strdup (org);
			}
		}

		/* Add the FILE_AS attribute to the vcard */
		if (new_file_as) {
			attr = e_vcard_attribute_new (NULL, EVC_X_FILE_AS);
			e_vcard_add_attribute_with_value_take (E_VCARD (contact), attr, g_steal_pointer (&new_file_as));
		}
	}

	if (attr) {
		p = e_vcard_attribute_get_values (attr);

		return p && p->data ? p->data : (gpointer) "";
	} else {
		return NULL;
	}
}

static void
fileas_setter (EContact *contact,
               EVCardAttribute *attr,
               gpointer data)
{
	/* Default implementation */
	const gchar *file_as = data;
	e_vcard_attribute_add_value (attr, file_as ? file_as : "");
}



static gpointer
n_getter (EContact *contact,
          EVCardAttribute *attr)
{
	EContactName *name = g_new0 (EContactName, 1);
	EVCardAttribute *new_attr;
	gchar *name_str;

	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);

		name->family = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		name->given = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		name->additional = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		name->prefixes = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		name->suffixes = g_strdup (p && p->data ? p->data : "");
	}

	new_attr = e_vcard_get_attribute (E_VCARD (contact), EVC_FN);
	if (!new_attr) {
		new_attr = e_vcard_attribute_new (NULL, EVC_FN);
		e_vcard_append_attribute (E_VCARD (contact), new_attr);
		name_str = e_contact_name_to_string (name);
		e_vcard_attribute_add_value_take (new_attr, g_steal_pointer (&name_str));
	}

	return name;
}

static void
n_setter (EContact *contact,
          EVCardAttribute *attr,
          gpointer data)
{
	EContactName *name = data;

	e_vcard_attribute_add_value (attr, name->family ? name->family : "");
	e_vcard_attribute_add_value (attr, name->given ? name->given : "");
	e_vcard_attribute_add_value (attr, name->additional ? name->additional : "");
	e_vcard_attribute_add_value (attr, name->prefixes ? name->prefixes : "");
	e_vcard_attribute_add_value (attr, name->suffixes ? name->suffixes : "");

	/* now find the attribute for FileAs.  If it's not present, fill it in */
	attr = e_vcard_get_attribute (E_VCARD (contact), EVC_X_FILE_AS);
	if (!attr) {
		gchar *strings[3], **stringptr;
		gchar *string;
		attr = e_vcard_attribute_new (NULL, EVC_X_FILE_AS);
		e_vcard_append_attribute (E_VCARD (contact), attr);

		stringptr = strings;
		if (name->family && *name->family)
			*(stringptr++) = name->family;
		if (name->given && *name->given)
			*(stringptr++) = name->given;
		*stringptr = NULL;
		string = g_strjoinv (", ", strings);

		e_vcard_attribute_add_value_take (attr, g_steal_pointer (&string));
	}

}



static gpointer
adr_getter (EContact *contact,
            EVCardAttribute *attr)
{
	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);
		EContactAddress *addr = g_new0 (EContactAddress, 1);

		addr->address_format = g_strdup ("");
		addr->po = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->ext = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->street = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->locality = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->region = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->code = g_strdup (p && p->data ? p->data : ""); if (p) p = p->next;
		addr->country = g_strdup (p && p->data ? p->data : ""); /* if (p) p = p->next; */

		return addr;
	}

	return NULL;
}

static void
adr_setter (EContact *contact,
            EVCardAttribute *attr,
            gpointer data)
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



static gpointer
date_getter (EContact *contact,
             EVCardAttribute *attr)
{
	if (attr) {
		GList *p = e_vcard_attribute_get_values (attr);
		EContactDate *date;

		if (p && p->data && ((gchar *) p->data)[0])
			date = e_contact_date_from_string ((gchar *) p->data);
		else
			date = NULL;

		return date;
	}

	return NULL;
}

static void
date_setter (EContact *contact,
             EVCardAttribute *attr,
             gpointer data)
{
	EContactDate *date = data;
	gchar *str;

	str = e_contact_date_to_string (date, e_vcard_get_version (E_VCARD (contact)));

	e_vcard_attribute_add_value_take (attr, g_steal_pointer (&str));
}



static gpointer
cert_getter (EContact *contact,
             EVCardAttribute *attr)
{
	if (attr) {
		/* the certificate is stored in this vcard.  just
		 * return the data */
		GList *values = e_vcard_attribute_get_values_decoded (attr);

		if (values && values->data) {
			GString *s = values->data;
			EContactCert *cert = g_new0 (EContactCert, 1);

			cert->length = s->len;
			cert->data = g_malloc (cert->length);
			memcpy (cert->data, s->str, cert->length);

			return cert;
		}
	}

	/* XXX if we stored a fingerprint in the cert we could look it
	 * up via NSS, but that would require the additional NSS dep
	 * here, and we'd have more than one process opening the
	 * certdb, which is bad.  *sigh * */

	return NULL;
}

static void
cert_setter (EContact *contact,
             EVCardAttribute *attr,
             gpointer data)
{
	EContactCert *cert = data;

	e_vcard_attribute_add_param_with_value (
		attr,
		e_vcard_attribute_param_new (EVC_ENCODING),
		"b");

	e_vcard_attribute_add_value_decoded (attr, cert->data, cert->length);
}

static gpointer
datetime_type_getter (EContact *contact,
		      EVCardAttribute *attr,
		      EContactDateTimeFlags flags)
{
	if (attr) {
		GList *values = e_vcard_attribute_get_values (attr);

		if (values && values->data) {
			const gchar *value = values->data;
			EContactDateTime *dt = e_contact_date_time_from_string (value, flags);

			return dt;
		}
	}

	return NULL;
}

static void
datetime_type_setter (EContact *contact,
		      EVCardAttribute *attr,
		      gpointer data,
		      EContactDateTimeFlags flags)
{
	const EContactDateTime *dt = data;
	gchar *str = e_contact_date_time_to_string (dt, e_vcard_get_version (E_VCARD (contact)), flags);

	e_vcard_attribute_add_value_take (attr, g_steal_pointer (&str));
}

static gpointer
datetime_getter (EContact *contact,
		 EVCardAttribute *attr)
{
	return datetime_type_getter (contact, attr, E_CONTACT_DATE_TIME_FLAG_DATE_TIME);
}

static void
datetime_setter (EContact *contact,
		 EVCardAttribute *attr,
		 gpointer data)
{
	datetime_type_setter (contact, attr, data, E_CONTACT_DATE_TIME_FLAG_DATE_TIME);
}

static void
e_contact_add_type_attrs (EVCardAttribute *attr,
			  const gchar *attr_type1,
			  const gchar *attr_type2)
{
	if (attr_type1 && (attr_type1[0] != '*' || attr_type1[1])) {
		e_vcard_attribute_add_param_with_value (
			attr, e_vcard_attribute_param_new (EVC_TYPE),
			attr_type1[0] == '*' ? attr_type1 + 1 : attr_type1);
	}

	if (attr_type2 && *attr_type2) {
		e_vcard_attribute_add_param_with_value (
			attr, e_vcard_attribute_param_new (EVC_TYPE),
			attr_type2);
	}
}

static EVCardAttribute *
e_contact_find_attribute_with_types (EVCard *vcard,
                                     const gchar *attr_name,
                                     const gchar *type_needed1,
                                     const gchar *type_needed2,
                                     gint nth,
				     gboolean (* custom_equal_func) (EVCardAttribute *attr, const gchar *attr_name, gpointer user_data),
				     gpointer user_data)
{
	GList *l, *attrs;
	const AttrTypeValue *attr_type_values;
	gboolean found_needed1, found_needed2;
	gboolean can_empty_needed2;
	gboolean use_for_no_type;

	can_empty_needed2 = g_ascii_strcasecmp (attr_name, "TEL") == 0 && type_needed2 &&
			    g_ascii_strcasecmp (type_needed2, "VOICE") == 0;

	attr_type_values = e_contact_find_attr_type_values (attr_name);

	/* The first type can start with a '*', which means that the found attribute
	   can be also the one with no type specified. It's used for default types
	   per RFC2426.*/
	use_for_no_type = type_needed1 && (*type_needed1) == '*';

	if (use_for_no_type) {
		type_needed1++;

		if (!*type_needed1)
			type_needed1 = NULL;
	}

	attrs = e_vcard_get_attributes (vcard);

	for (l = attrs; l; l = l->next) {
		EVCardAttribute *attr = l->data;
		const gchar *name;

		found_needed1 = (type_needed1 == NULL);
		found_needed2 = (type_needed2 == NULL);

		name = e_vcard_attribute_get_name (attr);

		if ((custom_equal_func && custom_equal_func (attr, attr_name, user_data)) ||
		    (!custom_equal_func && !g_ascii_strcasecmp (name, attr_name))) {
			GSList *found_types = NULL;
			GList *params;
			guint n_types = 0, n_found_types = 0;

			for (params = e_vcard_attribute_get_params (attr); params; params = params->next) {
				EVCardAttributeParam *param = params->data;
				const gchar *param_name = e_vcard_attribute_param_get_name (param);

				if (!g_ascii_strcasecmp (param_name, EVC_TYPE)) {
					GList *value;

					for (value = e_vcard_attribute_param_get_values (param);
					     value;
					     value = g_list_next (value)) {
						const gchar *type_value = value->data;

						n_types++;

						/* Skip any type values which are not used, thus they do not confuse the search */
						if (type_value && *type_value &&
						    e_contact_check_attr_type_value_used (attr_type_values, type_value)) {
							found_types = g_slist_prepend (found_types, (gpointer) type_value);
							n_found_types++;
						}
					}
				}
			}

			if (!n_types) {
				if (use_for_no_type && (nth-- == 0))
					return attr;
			} else {
				GSList *link;
				gboolean matches = FALSE;

				/* empty string on type_needed2 is to get only those attributes,
				 * which has exactly one TYPE, to not rewrite those with multiple */
				if (type_needed2 && !*type_needed2)
					found_needed2 = n_found_types == 1;

				for (link = found_types; link; link = g_slist_next (link)) {
					const gchar *type_value = link->data;

					if (!type_value)
						continue;

					if (!found_needed1 && !g_ascii_strcasecmp (type_value, type_needed1)) {
						found_needed1 = TRUE;
						matches = TRUE;
					} else if (!found_needed2 && !g_ascii_strcasecmp (type_value, type_needed2)) {
						found_needed2 = TRUE;
						matches = TRUE;
					} else if (found_needed1) {
						if (!found_needed2)
							matches = FALSE;
						break;
					}
				}

				if (!matches && (!can_empty_needed2 || n_found_types != 1)) {
					/* this is to enforce that we find an attribute
					 * with *only* the TYPE='s we need.  This may seem like
					 * an odd restriction but it's the only way at present to
					 * implement the Other Fax and Other Phone attributes. */
					found_needed1 = FALSE;
				}

				if (found_needed1 && (found_needed2 || (n_found_types == 1 && can_empty_needed2))) {
					if (nth-- == 0) {
						g_slist_free (found_types);
						return attr;
					}
				}
			}

			g_slist_free (found_types);
		}
	}

	return NULL;
}

static gpointer
get_adr_label_full (EVCard *vcard,
		    const EContactFieldInfo *info)
{
	EContact *contact = E_CONTACT (vcard);
	EVCardAttribute *attr;
	const gchar *cached_value;

	cached_value = contact->priv->cached_strings[info->field_id];
	if (cached_value)
		return g_strdup (cached_value);

	if (e_vcard_get_version (vcard) < E_VCARD_VERSION_40) {
		attr = e_contact_find_attribute_with_types (vcard, info->vcard_field_name, info->attr_type1, info->attr_type2, info->list_elem, NULL, NULL);
		if (attr) {
			GList *values = e_vcard_attribute_get_values (attr);

			if (values && values->data)
				return g_strstrip (g_strdup (values->data));
		}
	} else {
		attr = e_contact_find_attribute_with_types (vcard, EVC_ADR, info->attr_type1, info->attr_type2, info->list_elem, NULL, NULL);

		if (attr) {
			GList *values = e_vcard_attribute_get_param (attr, EVC_LABEL);

			if (values && values->data)
				return g_strstrip (g_strdup (values->data));
		}
	}

	return NULL;
}

static void
set_adr_label_full (EVCard *vcard,
		    const EContactFieldInfo *info,
		    gpointer data)
{
	EVCardAttribute *attr;
	const gchar *value = data;

	if (e_vcard_get_version (vcard) < E_VCARD_VERSION_40) {
		attr = e_contact_find_attribute_with_types (vcard, info->vcard_field_name, info->attr_type1, info->attr_type2, info->list_elem, NULL, NULL);
		if (attr) {
			if (value && *value) {
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_add_value (attr, value);
			} else {
				e_vcard_remove_attribute (vcard, attr);
			}
		} else if (value && *value) {
			attr = e_vcard_attribute_new (NULL, info->vcard_field_name);
			e_contact_add_type_attrs (attr, info->attr_type1, info->attr_type2);
			e_vcard_append_attribute_with_value (vcard, attr, value);
		}
	} else {
		attr = e_contact_find_attribute_with_types (vcard, EVC_ADR, info->attr_type1, info->attr_type2, info->list_elem, NULL, NULL);
		if (attr) {
			e_vcard_attribute_remove_param (attr, EVC_LABEL);

			if (value && *value) {
				e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_LABEL), value);
			} else {
				GList *values = e_vcard_attribute_get_values (attr);

				/* empty address => drop the whole attribute */
				if (!values || !values->next || !values->data || !*((const gchar *) values->data))
					e_vcard_remove_attribute (vcard, attr);
			}
		} else if (value && *value) {
			attr = e_vcard_attribute_new (NULL, EVC_ADR);
			e_contact_add_type_attrs (attr, info->attr_type1, info->attr_type2);
			e_vcard_attribute_add_param_with_value (attr, e_vcard_attribute_param_new (EVC_LABEL), value);
			e_vcard_append_attribute_with_value (vcard, attr, "");
		}
	}
}

static void
impp_map_scheme_field (EContactField in_field_id,
		       const gchar **out_scheme,
		       const gchar *in_scheme, /* possibly with value, like "icq:1234", which is the same as "icq:" */
		       EContactField *out_field_id,
		       guint *out_scheme_len,
		       guint index,
		       EContactField *out_field_id_by_index)
{
	struct _mapping {
		EContactField field_id;
		const gchar *scheme; /* NULL means to use the previous non-NULL */
	} mappings[] = {
		{ E_CONTACT_IM_AIM, "aim:" },
		{ E_CONTACT_IM_AIM_HOME_1, NULL },
		{ E_CONTACT_IM_AIM_HOME_2, NULL },
		{ E_CONTACT_IM_AIM_HOME_3, NULL },
		{ E_CONTACT_IM_AIM_WORK_1, NULL },
		{ E_CONTACT_IM_AIM_WORK_2, NULL },
		{ E_CONTACT_IM_AIM_WORK_3, NULL },

		{ E_CONTACT_IM_GROUPWISE, "groupwise:" },
		{ E_CONTACT_IM_GROUPWISE_HOME_1, NULL },
		{ E_CONTACT_IM_GROUPWISE_HOME_2, NULL },
		{ E_CONTACT_IM_GROUPWISE_HOME_3, NULL },
		{ E_CONTACT_IM_GROUPWISE_WORK_1, NULL },
		{ E_CONTACT_IM_GROUPWISE_WORK_2, NULL },
		{ E_CONTACT_IM_GROUPWISE_WORK_3, NULL },

		{ E_CONTACT_IM_JABBER, "jabber:" },
		{ E_CONTACT_IM_JABBER_HOME_1, NULL },
		{ E_CONTACT_IM_JABBER_HOME_2, NULL },
		{ E_CONTACT_IM_JABBER_HOME_3, NULL },
		{ E_CONTACT_IM_JABBER_WORK_1, NULL },
		{ E_CONTACT_IM_JABBER_WORK_2, NULL },
		{ E_CONTACT_IM_JABBER_WORK_3, NULL },

		{ E_CONTACT_IM_YAHOO, "yahoo:" },
		{ E_CONTACT_IM_YAHOO_HOME_1, NULL },
		{ E_CONTACT_IM_YAHOO_HOME_2, NULL },
		{ E_CONTACT_IM_YAHOO_HOME_3, NULL },
		{ E_CONTACT_IM_YAHOO_WORK_1, NULL },
		{ E_CONTACT_IM_YAHOO_WORK_2, NULL },
		{ E_CONTACT_IM_YAHOO_WORK_3, NULL },

		{ E_CONTACT_IM_MSN, "msn:" },
		{ E_CONTACT_IM_MSN_HOME_1, NULL },
		{ E_CONTACT_IM_MSN_HOME_2, NULL },
		{ E_CONTACT_IM_MSN_HOME_3, NULL },
		{ E_CONTACT_IM_MSN_WORK_1, NULL },
		{ E_CONTACT_IM_MSN_WORK_2, NULL },
		{ E_CONTACT_IM_MSN_WORK_3, NULL },

		{ E_CONTACT_IM_ICQ, "icq:" },
		{ E_CONTACT_IM_ICQ_HOME_1, NULL },
		{ E_CONTACT_IM_ICQ_HOME_2, NULL },
		{ E_CONTACT_IM_ICQ_HOME_3, NULL },
		{ E_CONTACT_IM_ICQ_WORK_1, NULL },
		{ E_CONTACT_IM_ICQ_WORK_2, NULL },
		{ E_CONTACT_IM_ICQ_WORK_3, NULL },

		{ E_CONTACT_IM_GADUGADU, "gadugadu:" },
		{ E_CONTACT_IM_GADUGADU_HOME_1, NULL },
		{ E_CONTACT_IM_GADUGADU_HOME_2, NULL },
		{ E_CONTACT_IM_GADUGADU_HOME_3, NULL },
		{ E_CONTACT_IM_GADUGADU_WORK_1, NULL },
		{ E_CONTACT_IM_GADUGADU_WORK_2, NULL },
		{ E_CONTACT_IM_GADUGADU_WORK_3, NULL },

		{ E_CONTACT_IM_SKYPE, "skype:" },
		{ E_CONTACT_IM_SKYPE_HOME_1, NULL },
		{ E_CONTACT_IM_SKYPE_HOME_2, NULL },
		{ E_CONTACT_IM_SKYPE_HOME_3, NULL },
		{ E_CONTACT_IM_SKYPE_WORK_1, NULL },
		{ E_CONTACT_IM_SKYPE_WORK_2, NULL },
		{ E_CONTACT_IM_SKYPE_WORK_3, NULL },

		{ E_CONTACT_IM_GOOGLE_TALK, "googletalk:" },
		{ E_CONTACT_IM_GOOGLE_TALK_HOME_1, NULL },
		{ E_CONTACT_IM_GOOGLE_TALK_HOME_2, NULL },
		{ E_CONTACT_IM_GOOGLE_TALK_HOME_3, NULL },
		{ E_CONTACT_IM_GOOGLE_TALK_WORK_1, NULL },
		{ E_CONTACT_IM_GOOGLE_TALK_WORK_2, NULL },
		{ E_CONTACT_IM_GOOGLE_TALK_WORK_3, NULL },

		{ E_CONTACT_IM_MATRIX, "matrix:" },
		{ E_CONTACT_IM_MATRIX_HOME_1, NULL },
		{ E_CONTACT_IM_MATRIX_HOME_2, NULL },
		{ E_CONTACT_IM_MATRIX_HOME_3, NULL },
		{ E_CONTACT_IM_MATRIX_WORK_1, NULL },
		{ E_CONTACT_IM_MATRIX_WORK_2, NULL },
		{ E_CONTACT_IM_MATRIX_WORK_3, NULL },

		{ E_CONTACT_IM_TWITTER, "twitter:" }
	};
	guint ii;

	if (out_field_id_by_index != NULL) {
		if (index < G_N_ELEMENTS (mappings))
			*out_field_id_by_index = mappings[index].field_id;
		else
			*out_field_id_by_index = E_CONTACT_FIELD_LAST;
	} else if (in_scheme != NULL) {
		const gchar *ptr;
		guint len;

		g_return_if_fail (out_field_id != NULL);
		g_return_if_fail (out_scheme == NULL);
		g_return_if_fail (out_scheme_len != NULL);

		ptr = strchr (in_scheme, ':');
		if (!ptr) {
			*out_field_id = E_CONTACT_FIELD_LAST;
			return;
		}

		len = ptr - in_scheme + 1; /* include the ':' */

		for (ii = 0; ii < G_N_ELEMENTS (mappings); ii++) {
			if (mappings[ii].scheme && g_ascii_strncasecmp (mappings[ii].scheme, in_scheme, len) == 0 &&
			    mappings[ii].scheme[len] == '\0') {
				*out_field_id = mappings[ii].field_id;
				*out_scheme_len = len;
				return;
			}
		}

		*out_field_id = E_CONTACT_FIELD_LAST;
	} else {
		const gchar *adept_scheme = NULL;

		g_return_if_fail (out_field_id == NULL);
		g_return_if_fail (out_scheme != NULL);

		for (ii = 0; ii < G_N_ELEMENTS (mappings); ii++) {
			if (mappings[ii].scheme != NULL)
				adept_scheme = mappings[ii].scheme;

			if (mappings[ii].field_id == in_field_id) {
				*out_scheme = adept_scheme;
				return;
			}
		}

		*out_scheme = NULL;
	}
}

typedef struct _ImppEqualData {
	const gchar *attr_name;
	const gchar *scheme;
	guint scheme_len;
} ImppEqualData;

static void
impp_equal_data_init (ImppEqualData *data,
		      const EContactFieldInfo *info)
{
	data->attr_name = info->vcard_field_name;
	data->scheme = NULL;
	impp_map_scheme_field (info->field_id, &data->scheme, NULL, NULL, NULL, 0, NULL);
	data->scheme_len = data->scheme ? strlen (data->scheme) : 0;
}

static gboolean
impp_attr_equal_func_ex (EVCardAttribute *attr,
			 const gchar *lookup_name,
			 ImppEqualData *eq_data,
			 gboolean *out_is_impp)
{
	const gchar *attr_name = e_vcard_attribute_get_name (attr);

	if (out_is_impp)
		*out_is_impp = FALSE;

	if (g_ascii_strcasecmp (attr_name, lookup_name) == 0)
		return TRUE;

	if (g_ascii_strcasecmp (attr_name, EVC_IMPP) != 0 &&
	    g_ascii_strcasecmp (attr_name, EVC_X_EVOLUTION_IMPP) != 0)
		return FALSE;

	if (eq_data->scheme_len > 0) {
		GList *values = e_vcard_attribute_get_values (attr);

		if (values && values->data) {
			const gchar *value = values->data;

			if (out_is_impp)
				*out_is_impp = TRUE;

			return value && *value && g_ascii_strncasecmp (value, eq_data->scheme, eq_data->scheme_len) == 0;
		}
	}

	return FALSE;
}

static gboolean
impp_attr_equal_func (EVCardAttribute *attr,
		      const gchar *attr_name,
		      gpointer user_data)
{
	ImppEqualData *eq_data = user_data;

	return impp_attr_equal_func_ex (attr, attr_name, eq_data, NULL);
}

static gpointer
get_impp_one_full (EVCard *vcard,
		   const EContactFieldInfo *info)
{
	EVCardAttribute *attr;
	GList *values;
	ImppEqualData eq_data = { 0, };
	const gchar *value;

	impp_equal_data_init (&eq_data, info);

	attr = e_contact_find_attribute_with_types (vcard, info->vcard_field_name, info->attr_type1, info->attr_type2, info->list_elem,
		impp_attr_equal_func, &eq_data);
	if (!attr)
		return NULL;

	values = e_vcard_attribute_get_values (attr);
	if (!values || !values->data)
		return NULL;

	value = values->data;

	if (g_ascii_strncasecmp (value, eq_data.scheme, eq_data.scheme_len) == 0)
		value += eq_data.scheme_len;

	return g_strdup (value);
}

static void
set_impp_one_full (EVCard *vcard,
		   const EContactFieldInfo *info,
		   gpointer data)
{
	EVCardAttribute *attr;
	ImppEqualData eq_data = { 0, };
	const gchar *value = data;

	impp_equal_data_init (&eq_data, info);

	attr = e_contact_find_attribute_with_types (vcard, info->vcard_field_name, info->attr_type1, info->attr_type2, info->list_elem,
		impp_attr_equal_func, &eq_data);
	if (attr) {
		if (value && *value) {
			gchar *tmp = NULL;

			if (e_vcard_get_version (vcard) >= E_VCARD_VERSION_40 &&
			    g_ascii_strncasecmp (value, eq_data.scheme, eq_data.scheme_len) != 0) {
				tmp = g_strconcat (eq_data.scheme, value, NULL);
				value = tmp;
			}

			e_vcard_attribute_remove_values (attr);
			e_vcard_attribute_add_value (attr, value);

			g_free (tmp);
		} else {
			e_vcard_remove_attribute (vcard, attr);
		}
	} else if (value && *value) {
		gchar *tmp = NULL;

		if (e_vcard_get_version (vcard) < E_VCARD_VERSION_40) {
			attr = e_vcard_attribute_new (NULL, info->vcard_field_name);
		} else {
			attr = e_vcard_attribute_new (NULL, EVC_IMPP);

			if (g_ascii_strncasecmp (value, eq_data.scheme, eq_data.scheme_len) != 0) {
				tmp = g_strconcat (eq_data.scheme, value, NULL);
				value = tmp;
			}
		}

		e_contact_add_type_attrs (attr, info->attr_type1, info->attr_type2);
		e_vcard_append_attribute_with_value (vcard, attr, value);

		g_free (tmp);
	}
}

static gpointer
get_impp_multi_full (EVCard *vcard,
		     const EContactFieldInfo *info)
{
	ImppEqualData eq_data = { 0, };
	GList *res_values = NULL;
	GList *link;

	impp_equal_data_init (&eq_data, info);

	for (link = e_vcard_get_attributes (vcard); link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;
		GList *values;
		gboolean is_impp = FALSE;

		if (!attr || !e_vcard_attribute_get_name (attr))
			continue;

		if (impp_attr_equal_func_ex (attr, info->vcard_field_name, &eq_data, &is_impp)) {
			const gchar *value;

			values = e_vcard_attribute_get_values (attr);
			value = values ? values->data : NULL;

			if (value && *value) {
				if (is_impp) {
					if (strlen (value) > eq_data.scheme_len) {
						value += eq_data.scheme_len;

						if (*value)
							res_values = g_list_prepend (res_values, g_strstrip (g_strdup (value)));
					}
				} else {
					res_values = g_list_prepend (res_values, g_strstrip (g_strdup (value)));
				}
			}
		}
	}

	return g_list_reverse (res_values);
}

static void
set_impp_multi_full (EVCard *vcard,
		     const EContactFieldInfo *info,
		     gpointer data)
{
	ImppEqualData eq_data = { 0, };
	GList *link;
	GPtrArray *to_remove = NULL; /* EVCardAttribute * */
	EVCardVersion version = e_vcard_get_version (vcard);

	impp_equal_data_init (&eq_data, info);

	for (link = e_vcard_get_attributes (vcard); link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;

		if (impp_attr_equal_func_ex (attr, info->vcard_field_name, &eq_data, NULL)) {
			if (!to_remove)
				to_remove = g_ptr_array_new ();
			g_ptr_array_add (to_remove, attr);
		}
	}

	if (to_remove != NULL) {
		guint ii;

		for (ii = 0; ii < to_remove->len; ii++) {
			EVCardAttribute *attr = g_ptr_array_index (to_remove, ii);

			e_vcard_remove_attribute (vcard, attr);
		}

		g_ptr_array_unref (to_remove);
	}

	for (link = data; link; link = g_list_next (link)) {
		const gchar *value = link->data;

		if (value && *value) {
			if (version < E_VCARD_VERSION_40) {
				if (g_ascii_strncasecmp (value, eq_data.scheme, eq_data.scheme_len) == 0)
					value += eq_data.scheme_len;

				if (*value) {
					EVCardAttribute *attr;

					attr = e_vcard_attribute_new (NULL, info->vcard_field_name);
					e_contact_add_type_attrs (attr, info->attr_type1, info->attr_type2);
					e_vcard_append_attribute_with_value (vcard, attr, value);
				}
			} else {
				EVCardAttribute *attr;
				gchar *tmp = NULL;

				if (g_ascii_strncasecmp (value, eq_data.scheme, eq_data.scheme_len) != 0) {
					tmp = g_strconcat (eq_data.scheme, value, NULL);
					value = tmp;
				}

				attr = e_vcard_attribute_new (NULL, EVC_IMPP);
				e_contact_add_type_attrs (attr, info->attr_type1, info->attr_type2);
				e_vcard_append_attribute_with_value (vcard, attr, value);

				g_free (tmp);
			}
		}
	}
}

static void
get_impp_attr_field_id (const gchar *in_vcard_name,
			EContactField *out_field_id,
			EContactField in_field_id,
			const gchar **out_vcard_name)
{
	struct _mapping {
		const gchar *vcard_name;
		EContactField field_id;
	} mappings[] = {
		{ EVC_X_AIM, E_CONTACT_IM_AIM },
		{ EVC_X_GROUPWISE, E_CONTACT_IM_GROUPWISE },
		{ EVC_X_JABBER, E_CONTACT_IM_JABBER },
		{ EVC_X_YAHOO, E_CONTACT_IM_YAHOO },
		{ EVC_X_MSN, E_CONTACT_IM_MSN },
		{ EVC_X_ICQ, E_CONTACT_IM_ICQ },
		{ EVC_X_GADUGADU, E_CONTACT_IM_GADUGADU },
		{ EVC_X_SKYPE, E_CONTACT_IM_SKYPE },
		{ EVC_X_GOOGLE_TALK, E_CONTACT_IM_GOOGLE_TALK },
		{ EVC_X_MATRIX, E_CONTACT_IM_MATRIX },
		{ EVC_X_TWITTER, E_CONTACT_IM_TWITTER }
	};
	guint ii;

	if (in_vcard_name) {
		g_return_if_fail (out_field_id != NULL);
		g_return_if_fail (out_vcard_name == NULL);

		for (ii = 0; ii < G_N_ELEMENTS (mappings); ii++) {
			if (g_ascii_strcasecmp (in_vcard_name, mappings[ii].vcard_name) == 0) {
				*out_field_id = mappings[ii].field_id;
				return;
			}
		}

		*out_field_id = E_CONTACT_FIELD_LAST;
	} else {
		g_return_if_fail (out_vcard_name != NULL);

		for (ii = 0; ii < G_N_ELEMENTS (mappings); ii++) {
			if (in_field_id == mappings[ii].field_id) {
				*out_vcard_name = mappings[ii].vcard_name;
				return;
			}
		}

		*out_vcard_name = NULL;
	}
}

static gpointer
get_impp_all (EVCard *vcard,
	      const EContactFieldInfo *info)
{
	GList *result = NULL, *link, *values;

	for (link = e_vcard_get_attributes (vcard); link; link = g_list_next (link)) {
		EVCardAttribute *attr = link->data;
		const gchar *name = attr ? e_vcard_attribute_get_name (attr) : NULL;

		if (!name)
			continue;

		if (g_ascii_strcasecmp (name, EVC_IMPP) == 0 ||
		    g_ascii_strcasecmp (name, EVC_X_EVOLUTION_IMPP) == 0) {
			values = e_vcard_attribute_get_values (attr);
			if (values && values->data)
				result = g_list_prepend (result, g_strstrip (g_strdup (values->data)));
		} else {
			EContactField field_id = E_CONTACT_FIELD_LAST;

			get_impp_attr_field_id (name, &field_id, E_CONTACT_FIELD_LAST, NULL);

			if (field_id != E_CONTACT_FIELD_LAST) {
				values = e_vcard_attribute_get_values (attr);
				if (values && values->data) {
					const gchar *value = values->data;
					const gchar *scheme = NULL;

					impp_map_scheme_field (field_id, &scheme, NULL, NULL, NULL, 0, NULL);

					g_warn_if_fail (scheme != NULL);

					if (g_ascii_strncasecmp (value, scheme, strlen (scheme)) == 0)
						result = g_list_prepend (result, g_strstrip (g_strdup (value)));
					else
						result = g_list_prepend (result, g_strconcat (scheme, value, NULL));
				}
			}
		}
	}

	return g_list_reverse (result);
}

static void
set_impp_all (EVCard *vcard,
	      const EContactFieldInfo *info,
	      gpointer data)
{
	GList *items = data, *link;
	gboolean is_pre_40 = e_vcard_get_version (vcard) < E_VCARD_VERSION_40;

	e_vcard_remove_attributes (vcard, NULL, EVC_IMPP);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_EVOLUTION_IMPP);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_AIM);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_GROUPWISE);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_JABBER);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_YAHOO);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_MSN);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_ICQ);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_GADUGADU);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_SKYPE);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_GOOGLE_TALK);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_MATRIX);
	e_vcard_remove_attributes (vcard, NULL, EVC_X_TWITTER);

	for (link = items; link; link = g_list_next (link)) {
		const gchar *value = link->data;

		if (!value || !*value)
			continue;

		if (is_pre_40) {
			EContactField field_id = E_CONTACT_FIELD_LAST;
			guint scheme_len = 0;

			impp_map_scheme_field (E_CONTACT_FIELD_LAST, NULL, value, &field_id, &scheme_len, 0, NULL);

			if (field_id != E_CONTACT_FIELD_LAST) {
				const gchar *vcard_name = NULL;

				get_impp_attr_field_id (NULL, NULL, field_id, &vcard_name);

				g_warn_if_fail (vcard_name != NULL);

				e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, vcard_name), value + scheme_len);
			} else {
				e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_X_EVOLUTION_IMPP), value);
			}
		} else {
			e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_IMPP), value);
		}
	}
}

static gpointer
get_is_list_full (EVCard *vcard,
		  const EContactFieldInfo *info)
{
	EVCardAttribute *attr;

	if (e_vcard_get_version (vcard) < E_VCARD_VERSION_40) {
		attr = e_vcard_get_attribute (vcard, EVC_X_LIST);
		if (attr) {
			GList *values = e_vcard_attribute_get_values (attr);
			if (values && values->data && g_ascii_strcasecmp (values->data, "true") == 0)
				return (gpointer) "1";
		}
	} else {
		attr = e_vcard_get_attribute (vcard, EVC_KIND);
		if (attr) {
			GList *values = e_vcard_attribute_get_values (attr);
			if (values && values->data && g_ascii_strcasecmp (values->data, "group") == 0)
				return (gpointer) "1";
		}
	}

	return NULL;
}

static void
set_is_list_full (EVCard *vcard,
		  const EContactFieldInfo *info,
		  gpointer data)
{
	EVCardAttribute *attr;
	gboolean is_list = GPOINTER_TO_INT (data) != 0;

	if (e_vcard_get_version (vcard) < E_VCARD_VERSION_40) {
		attr = e_vcard_get_attribute (vcard, EVC_X_LIST);
		if (attr) {
			if (is_list) {
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_add_value (attr, "TRUE");
			} else {
				e_vcard_remove_attribute (vcard, attr);
			}
		} else if (is_list) {
			e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_X_LIST), "TRUE");
		}

		attr = e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND);
		if (attr) {
			GList *values = e_vcard_attribute_get_values (attr);
			const gchar *value = values ? values->data : NULL;

			if (value && g_ascii_strcasecmp (value, "group") == 0) {
				if (!is_list)
					e_vcard_remove_attribute (vcard, attr);
			} else if (is_list) {
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_add_value (attr, "group");
			}
		}
	} else {
		attr = e_vcard_get_attribute (vcard, EVC_KIND);
		if (attr) {
			GList *values = e_vcard_attribute_get_values (attr);
			if (values && values->data && g_ascii_strcasecmp (values->data, "group") == 0) {
				if (!is_list) {
					e_vcard_attribute_remove_values (attr);
					e_vcard_attribute_add_value (attr, "individual");
				}
			} else if (is_list) {
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_add_value (attr, "group");
			}
		} else if (is_list) {
			e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_KIND), "group");
		}
	}
}

static gpointer
get_kind_full (EVCard *vcard,
	       const EContactFieldInfo *info)
{
	EVCardAttribute *attr;

	attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
	if (!attr)
		attr = e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND);
	if (attr) {
		GList *values = e_vcard_attribute_get_values (attr);
		if (values && values->data)
			return g_strstrip (g_strdup (values->data));

		return NULL;
	}

	attr = e_vcard_get_attribute (vcard, EVC_X_LIST);
	if (attr) {
		GList *values = e_vcard_attribute_get_values (attr);

		if (values && values->data && g_ascii_strcasecmp (values->data, "true") == 0) {
			return g_strdup ("group");
		}

		return g_strdup ("individual");
	}

	return NULL;
}

static void
set_kind_full (EVCard *vcard,
	       const EContactFieldInfo *info,
	       gpointer data)
{
	const gchar *value = data;

	if (e_vcard_get_version (vcard) < E_VCARD_VERSION_40) {
		EVCardAttribute *attr;
		gboolean is_list = value && *value && g_ascii_strcasecmp (value, "group") == 0;

		attr = e_vcard_get_attribute (vcard, EVC_X_EVOLUTION_KIND);
		if (attr) {
			if (value && *value) {
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_add_value (attr, value);
			} else {
				e_vcard_remove_attribute (vcard, attr);
			}
		} else if (value && *value && !is_list) {
			e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_X_EVOLUTION_KIND), value);
		}

		attr = e_vcard_get_attribute (vcard, EVC_X_LIST);
		if (attr) {
			if (is_list) {
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_add_value (attr, "TRUE");
			} else {
				e_vcard_remove_attribute (vcard, attr);
			}
		} else if (is_list) {
			e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, EVC_X_LIST), "TRUE");
		}
	} else {
		EVCardAttribute *attr;

		attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
		if (attr) {
			if (value && *value) {
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_add_value (attr, value);
			} else {
				e_vcard_remove_attribute (vcard, attr);
			}
		} else if (value && *value) {
			e_vcard_append_attribute_with_value (vcard, e_vcard_attribute_new (NULL, info->vcard_field_name), value);
		}
	}
}

static gpointer
gender_getter (EContact *contact,
	       EVCardAttribute *attr)
{
	if (attr) {
		EContactGender *gender;
		const gchar *sex, *identity;

		sex = e_vcard_attribute_get_nth_value (attr, 0);
		if (!sex)
			return NULL;

		identity = e_vcard_attribute_get_nth_value (attr, 1);

		gender = e_contact_gender_new ();
		gender->sex = e_contact_gender_sex_from_string (sex);
		gender->identity = g_strdup (identity);

		if (gender->identity)
			gender->identity = g_strstrip (gender->identity);

		return gender;
	}

	return NULL;
}

static void
gender_setter (EContact *contact,
	       EVCardAttribute *attr,
	       gpointer data)
{
	const EContactGender *gender = data;

	e_vcard_attribute_add_value (attr, e_contact_gender_sex_to_string (gender->sex));

	if (gender->identity && *gender->identity) {
		gchar *identity = g_strstrip (g_strdup (gender->identity));

		if (*identity)
			e_vcard_attribute_add_value_take (attr, identity);
		else
			g_free (identity);
	}
}

/* Set_arg handler for the contact */
static void
e_contact_set_property (GObject *object,
                        guint prop_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
	EContact *contact = E_CONTACT (object);
	EVCard *vcard = E_VCARD (contact);
	const EContactFieldInfo *info = NULL;
	const gchar *vcard_field_name;

	if (prop_id < 1 || prop_id >= E_CONTACT_FIELD_LAST) {
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		return;
	}

	info = &field_info[prop_id];

	vcard_field_name = info->vcard_field_name;
	if (info->fallback_vcard40_field_name && e_vcard_get_version (vcard) != E_VCARD_VERSION_40)
		vcard_field_name = info->fallback_vcard40_field_name;

	if ((info->t & E_CONTACT_FIELD_TYPE_REPLACEABLE) != 0) {
		gpointer new_values;
		if ((info->t & E_CONTACT_FIELD_TYPE_STRING) != 0) {
			new_values = (gpointer) g_value_get_string (value);
		} else if ((info->t & E_CONTACT_FIELD_TYPE_BOOLEAN) != 0) {
			new_values = (gpointer) (g_value_get_boolean (value) ? "TRUE" : NULL);
		} else {
			new_values = g_value_get_boxed (value);
		}

		info->set_full (vcard, info, new_values);
	} else if (info->t & E_CONTACT_FIELD_TYPE_MULTI) {
		GList *new_values = g_value_get_boxed (value);
		GList *l;

		/* first we remove all attributes of the type we're
		 * adding, then add new ones based on the values that
		 * are passed in */
		e_vcard_remove_attributes (vcard, NULL, info->vcard_field_name);
		if (info->fallback_vcard40_field_name)
			e_vcard_remove_attributes (vcard, NULL, info->fallback_vcard40_field_name);

		for (l = new_values; l; l = l->next)
			e_vcard_append_attribute_with_value (
				vcard,
				e_vcard_attribute_new (NULL, vcard_field_name),
				(gchar *) l->data);
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_SYNTHETIC) {
		if (info->t & E_CONTACT_FIELD_TYPE_MULTI_ELEM) {
			/* XXX this is kinda broken - we don't insert
			 * insert padding elements if, e.g. the user
			 * sets email 3 when email 1 and 2 don't
			 * exist.  But, if we *did* pad the lists we'd
			 * end up with empty items in the vcard.  I
			 * dunno which is worse. */
			EVCardAttribute *attr = NULL;
			gboolean found = FALSE;
			gint num_left = info->list_elem;
			GList *attrs = e_vcard_get_attributes (vcard);
			GList *l;
			const gchar *sval;

			for (l = attrs; l; l = l->next) {
				const gchar *name;

				attr = l->data;
				name = e_vcard_attribute_get_name (attr);

				if (!g_ascii_strcasecmp (name, info->vcard_field_name) ||
				    (info->fallback_vcard40_field_name && !g_ascii_strcasecmp (name, info->fallback_vcard40_field_name))) {
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
					attr = e_vcard_attribute_new (NULL, vcard_field_name);
					if (!g_ascii_strcasecmp (info->vcard_field_name, "EMAIL") &&
					    (!info->attr_type1 || (info->attr_type1[0] == '*' && !info->attr_type1[1])) &&
					    (!info->attr_type2 || !*info->attr_type2)) {
						/* Add default type */
						e_vcard_attribute_add_param_with_value (
							attr,
							e_vcard_attribute_param_new (EVC_TYPE),
							"OTHER");
					}
					e_vcard_append_attribute (vcard, attr);
				}

				e_vcard_attribute_add_value (attr, sval);
			}
			else {
				if (found)
					e_vcard_remove_attribute (vcard, attr);
			}
		}
		else if (info->t & E_CONTACT_FIELD_TYPE_ATTR_TYPE) {
			/* XXX this is kinda broken - we don't insert
			 * insert padding elements if, e.g. the user
			 * sets email 3 when email 1 and 2 don't
			 * exist.  But, if we *did* pad the lists we'd
			 * end up with empty items in the vcard.  I
			 * dunno which is worse. */
			EVCardAttribute *attr = e_contact_find_attribute_with_types (vcard, info->vcard_field_name, info->attr_type1, info->attr_type2, info->list_elem, NULL, NULL);
			if (!attr && info->fallback_vcard40_field_name)
				attr = e_contact_find_attribute_with_types (vcard, info->fallback_vcard40_field_name, info->attr_type1, info->attr_type2, info->list_elem, NULL, NULL);
			if (attr) {
				/* we found it, overwrite it */
				e_vcard_attribute_remove_values (attr);
			}
			else {
				/* we didn't find it - add a new attribute */
				attr = e_vcard_attribute_new (NULL, vcard_field_name);
				e_contact_add_type_attrs (attr, info->attr_type1, info->attr_type2);
				e_vcard_append_attribute (vcard, attr);
			}

			if (info->t & E_CONTACT_FIELD_TYPE_STRUCT || info->t & E_CONTACT_FIELD_TYPE_GETSET) {
				gpointer data = info->t & E_CONTACT_FIELD_TYPE_STRUCT ? g_value_get_boxed (value) : (gchar *) g_value_get_string (value);

				if ((info->t & E_CONTACT_FIELD_TYPE_STRUCT && data)
				    || (data && *(gchar *) data))
					info->struct_setter (contact, attr, data);
				else
					e_vcard_remove_attribute (vcard, attr);
			}
			else {
				const gchar *sval = g_value_get_string (value);

				if (sval && *sval)
					e_vcard_attribute_add_value (attr, sval);
				else
					e_vcard_remove_attribute (vcard, attr);
			}
		}
		else if (info->t & E_CONTACT_FIELD_TYPE_LIST_ELEM) {
			EVCardAttribute *attr = e_vcard_get_attribute (vcard, vcard_field_name);
			GList *values;
			GList *p;
			const gchar *sval = g_value_get_string (value);

			if (!attr && info->fallback_vcard40_field_name)
				attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);

			if (!attr) {
				if (!sval || !*sval)
					return;

				d (printf ("adding new %s\n", vcard_field_name));

				attr = e_vcard_attribute_new (NULL, vcard_field_name);
				e_vcard_append_attribute (vcard, attr);
			}

			values = e_vcard_attribute_get_values (attr);
			p = g_list_nth (values, info->list_elem);

			if (p) {
				g_free (p->data);
				p->data = g_strdup (g_value_get_string (value));
			}
			else {
				/* there weren't enough elements in the list, pad it */
				gint count = info->list_elem - g_list_length (values);

				while (count--)
					e_vcard_attribute_add_value (attr, "");

				e_vcard_attribute_add_value (attr, g_value_get_string (value));
			}
		}
		else {
			switch (info->field_id) {
			case E_CONTACT_CATEGORIES: {
				EVCardAttribute *attr = e_vcard_get_attribute (vcard, EVC_CATEGORIES);
				gchar **split, **s;
				const gchar *str;

				if (attr)
					e_vcard_attribute_remove_values (attr);
				else {
					/* we didn't find it - add a new attribute */
					attr = e_vcard_attribute_new (NULL, EVC_CATEGORIES);
					e_vcard_append_attribute (vcard, attr);
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
					d (printf ("removing %s\n", vcard_field_name));

					e_vcard_remove_attribute (vcard, attr);
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
		EVCardAttribute *attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
		gpointer data = info->t & E_CONTACT_FIELD_TYPE_STRUCT ? g_value_get_boxed (value) : (gchar *) g_value_get_string (value);

		if (!attr && info->fallback_vcard40_field_name)
			attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);

		if (attr) {
			if ((info->t & E_CONTACT_FIELD_TYPE_STRUCT && data)
			    || (data && *(gchar *) data)) {
				d (printf ("overwriting existing %s\n", vcard_field_name));
				/* remove all existing values and parameters.
				 * the setter will add the correct ones */
				e_vcard_attribute_remove_values (attr);
				e_vcard_attribute_remove_params (attr);

				info->struct_setter (contact, attr, data);
			}
			else {
				d (printf ("removing %s\n", vcard_field_name));

				e_vcard_remove_attribute (vcard, attr);
			}
		}
		else if ((info->t & E_CONTACT_FIELD_TYPE_STRUCT && data)
			 || (data && *(gchar *) data)) {
			d (printf ("adding new %s\n", vcard_field_name));
			attr = e_vcard_attribute_new (NULL, vcard_field_name);

			e_vcard_append_attribute (vcard, attr);

			info->struct_setter (contact, attr, data);
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_BOOLEAN) {
		EVCardAttribute *attr;

		/* first we search for an attribute we can overwrite */
		attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
		if (!attr && info->fallback_vcard40_field_name)
			attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);
		if (attr) {
			d (printf ("setting %s to `%s'\n", vcard_field_name, g_value_get_string (value)));
			e_vcard_attribute_remove_values (attr);
			e_vcard_attribute_add_value (attr, g_value_get_boolean (value) ? "TRUE" : "FALSE");
		}
		else {
			/* and if we don't find one we create a new attribute */
			e_vcard_append_attribute_with_value (
				vcard,
				e_vcard_attribute_new (NULL, vcard_field_name),
				g_value_get_boolean (value) ? "TRUE" : "FALSE");
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
		EVCardAttribute *attr;
		const gchar *sval = g_value_get_string (value);

		/* first we search for an attribute we can overwrite */
		if (sval == NULL || g_ascii_strcasecmp (info->vcard_field_name, EVC_UID) != 0) {
			attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
			if (!attr && info->fallback_vcard40_field_name)
				attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);
		} else {
			/* Avoid useless vcard parsing when trying to set a new non-empty UID.
			 * Parsing the vcard is pointless in this particular case because even
			 * if there is a UID in the unparsed vcard, it is going to be ignored
			 * upon parsing if we already have a UID for the vcard */
			attr = e_vcard_get_attribute_if_parsed (vcard, EVC_UID);
		}

		if (attr) {
			d (printf ("setting %s to `%s'\n", vcard_field_name, sval));
			e_vcard_attribute_remove_values (attr);
			if (sval) {
				e_vcard_attribute_add_value (attr, sval);
			}
			else {
				d (printf ("removing %s\n", vcard_field_name));

				e_vcard_remove_attribute (vcard, attr);
			}

		}
		else if (sval) {
			/* and if we don't find one we create a new attribute */
			e_vcard_append_attribute_with_value (
				vcard,
				e_vcard_attribute_new (NULL, vcard_field_name),
				sval);
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_LIST) {
		EVCardAttribute *attr;
		GList *values, *l;

		values = g_value_get_pointer (value);

		attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
		if (!attr && info->fallback_vcard40_field_name)
			attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);

		if (attr) {
			e_vcard_attribute_remove_values (attr);

			if (!values)
				e_vcard_remove_attribute (vcard, attr);
		}
		else if (values) {
			attr = e_vcard_attribute_new (NULL, vcard_field_name);
			e_vcard_append_attribute (vcard, attr);
		}

		for (l = values; l != NULL; l = l->next)
			e_vcard_attribute_add_value (attr, l->data);
	}
	else {
		g_warning ("unhandled attribute `%s'", vcard_field_name);
	}
}

static void
e_contact_get_property (GObject *object,
                        guint prop_id,
                        GValue *value,
                        GParamSpec *pspec)
{
	const EContactFieldInfo *info = NULL;
	gpointer data;

	if (prop_id < 1 || prop_id >= E_CONTACT_FIELD_LAST) {
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		g_value_reset (value);
		return;
	}

	info = &field_info[prop_id];
	data = e_contact_get (E_CONTACT (object), prop_id);

	if (info->t & E_CONTACT_FIELD_TYPE_MULTI) {
		g_value_take_boxed (value, data);
	} else if (info->t & E_CONTACT_FIELD_TYPE_BOOLEAN) {
		g_value_set_boolean (value, data != NULL);
	} else if (info->t & E_CONTACT_FIELD_TYPE_LIST) {
		g_value_set_pointer (value, data);
	} else if (info->t & E_CONTACT_FIELD_TYPE_STRUCT) {
		g_value_take_boxed (value, data);
	} else if (info->t & E_CONTACT_FIELD_TYPE_GETSET) {
		if (info->t & E_CONTACT_FIELD_TYPE_STRUCT) {
			g_value_set_boxed (value, data);
		} else {
			g_value_take_string (value, data);
		}
	} else if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
		g_value_take_string (value, data);
	} else {
		g_value_set_pointer (value, data);
	}
}



/**
 * e_contact_new:
 *
 * Creates a new, blank #EContact.
 *
 * Returns: A new #EContact.
 **/
EContact *
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
 * Returns: A new #EContact.
 **/
EContact *
e_contact_new_from_vcard (const gchar *vcard)
{
	EContact *contact;
	g_return_val_if_fail (vcard != NULL, NULL);

	contact = g_object_new (E_TYPE_CONTACT, NULL);
	e_vcard_construct (E_VCARD (contact), vcard);

	return contact;
}

/**
 * e_contact_new_from_vcard_with_uid:
 * @vcard: a string representing a vcard
 * @uid: a contact UID
 *
 * Creates a new #EContact based on a vcard and a predefined UID.
 *
 * Returns: A new #EContact.
 *
 * Since: 3.4
 **/
EContact *
e_contact_new_from_vcard_with_uid (const gchar *vcard,
                                   const gchar *uid)
{
	EContact *contact;
	g_return_val_if_fail (vcard != NULL, NULL);

	contact = g_object_new (E_TYPE_CONTACT, NULL);
	e_vcard_construct_with_uid (E_VCARD (contact), vcard, uid);

	return contact;
}

/**
 * e_contact_duplicate:
 * @contact: an #EContact
 *
 * Creates a copy of @contact.
 *
 * Returns: (transfer full): A new #EContact identical to @contact.
 **/
EContact *
e_contact_duplicate (EContact *contact)
{
	gchar *vcard;
	EContact *c;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);

	vcard = e_vcard_to_string (E_VCARD (contact));
	c = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	return c;
}

/**
 * e_contact_convert:
 * @self: an #EContact
 * @to_version: the requested vCard version, one of #EVCardVersion
 *
 * Converts the @self into the vCard version @to_version and returns a converted
 * copy of the @self. When the @to_version matches the version of the @self,
 * then does nothing and returns %NULL.
 *
 * Returns: (transfer full) (nullable): the @self converted to @to_version,
 *    or %NULL, when it is in this version already
 *
 * Since: 3.60
 **/
EContact *
e_contact_convert (EContact *self,
		   EVCardVersion to_version)
{
	EContact *converted;
	gchar *vcard;

	g_return_val_if_fail (E_IS_CONTACT (self), NULL);

	if (e_vcard_get_version (E_VCARD (self)) == to_version)
		return NULL;

	vcard = e_vcard_convert_to_string (E_VCARD (self), to_version);
	if (vcard == NULL)
		return NULL;

	converted = e_contact_new_from_vcard (vcard);
	g_free (vcard);

	return converted;
}

/**
 * e_contact_field_type:
 * @field_id: an #EContactField
 *
 * Gets the #GType used for this contact field, this indicates
 * what kind of value can be passed to e_contact_set().
 *
 * Returns: The #GType used for @field_id, or %G_TYPE_INVALID if it doesn't exist.
 *
 * Since: 3.8
 **/
GType
e_contact_field_type (EContactField field_id)
{
	GTypeClass  *class;
	GParamSpec  *pspec;
	const gchar *field_name;
	GType       type = G_TYPE_INVALID;

	g_return_val_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST, G_TYPE_INVALID);

	field_name = e_contact_field_name (field_id);
	class = g_type_class_ref (E_TYPE_CONTACT);
	pspec = g_object_class_find_property (G_OBJECT_CLASS (class), field_name);

	if (pspec)
		type = G_PARAM_SPEC_VALUE_TYPE (pspec);

	g_type_class_unref (class);

	return type;
}

/**
 * e_contact_field_name:
 * @field_id: an #EContactField
 *
 * Gets the string representation of @field_id.
 *
 * Returns: The string representation of @field_id
 **/
const gchar *
e_contact_field_name (EContactField field_id)
{
	g_return_val_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST, "");

	return field_info[field_id].field_name;
}

/**
 * e_contact_pretty_name:
 * @field_id: an #EContactField
 *
 * Gets a human-readable, translated string representation
 * of @field_id.
 *
 * Returns: The human-readable representation of @field_id
 **/
const gchar *
e_contact_pretty_name (EContactField field_id)
{
	g_return_val_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST, "");

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

	return _(field_info[field_id].pretty_name);
}

/**
 * e_contact_vcard_attribute:
 * @field_id: an #EContactField
 *
 * Gets the vcard attribute corresponding to @field_id, as a string.
 *
 * Returns: The vcard attribute corresponding to @field_id
 **/
const gchar *
e_contact_vcard_attribute (EContactField field_id)
{
	g_return_val_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST, "");

	return field_info[field_id].vcard_field_name;
}

/**
 * e_contact_vcard_attribute_fallback:
 * @field_id: an #EContactField
 *
 * Gets the vCard attribute name corresponding to @field_id, which is used
 * to hold vCard attributes in an earlier versions of the vCard standard
 * than in which the attribute had been added for.
 *
 * Not every attribute has it set, thus a %NULL is a valid return value.
 *
 * Returns: (nullable): the vCard fallback attribute name corresponding to @field_id,
 *    or %NULL, when the @field_id has not set any such fallback attribute.
 *
 * Since: 3.60
 **/
const gchar *
e_contact_vcard_attribute_fallback (EContactField field_id)
{
	g_return_val_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST, NULL);

	if (field_id == E_CONTACT_IMPP)
		return EVC_X_EVOLUTION_IMPP;

	return field_info[field_id].fallback_vcard40_field_name;
}

/**
 * e_contact_field_is_string:
 * @field_id: an #EContactField
 *
 * Returns whether the @field_id is of a string type,
 * thus it can be used with e_contact_get_const().
 *
 * Returns: Whether the @field_id is of a string type.
 *
 * Since: 3.16
 **/
gboolean
e_contact_field_is_string (EContactField field_id)
{
	g_return_val_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST, FALSE);

	return (field_info[field_id].t & E_CONTACT_FIELD_TYPE_STRING) != 0;
}

/**
 * e_contact_field_id:
 * @field_name: a string representing a contact field
 *
 * Gets the #EContactField corresponding to the @field_name.
 *
 * Returns: An #EContactField corresponding to @field_name, or 0 if it doesn't exist.
 **/
EContactField
e_contact_field_id (const gchar *field_name)
{
	gint ii;

	for (ii = E_CONTACT_FIELD_FIRST; ii < E_CONTACT_FIELD_LAST; ii++) {
		if (!g_ascii_strcasecmp (field_info[ii].field_name, field_name))
			return field_info[ii].field_id;
	}

	return 0;
}

/**
 * e_contact_field_id_from_vcard:
 * @vcard_field: a string representing a vCard field
 *
 * Gets the #EContactField corresponding to the @vcard_field.
 *
 * Returns: An #EContactField corresponding to @vcard_field, or 0 if it doesn't exist.
 *
 * Since: 2.26
 **/
EContactField
e_contact_field_id_from_vcard (const gchar *vcard_field)
{
	gint ii;

	for (ii = E_CONTACT_FIELD_FIRST; ii < E_CONTACT_FIELD_LAST; ii++) {
		if (field_info[ii].vcard_field_name == NULL)
			continue;
		if (field_info[ii].t & E_CONTACT_FIELD_TYPE_SYNTHETIC)
			continue;
		if (!strcmp (field_info[ii].vcard_field_name, vcard_field))
			return field_info[ii].field_id;
	}

	g_warning ("unknown vCard field `%s'", vcard_field);
	return 0;
}

/**
 * e_contact_get:
 * @contact: an #EContact
 * @field_id: an #EContactField
 *
 * Gets the value of @contact's field specified by @field_id.
 *
 * Returns: (transfer full) (nullable): Depends on the field's type, owned by the caller. This may be %NULL if the field isn't set.
 **/
gpointer
e_contact_get (EContact *contact,
               EContactField field_id)
{
	const EContactFieldInfo *info = NULL;
	EVCard *vcard;

	g_return_val_if_fail (contact && E_IS_CONTACT (contact), NULL);
	g_return_val_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST, NULL);

	info = &field_info[field_id];

	vcard = E_VCARD (contact);

	if ((info->t & E_CONTACT_FIELD_TYPE_REPLACEABLE) != 0) {
		return info->get_full (vcard, info);
	} else if (info->t & E_CONTACT_FIELD_TYPE_BOOLEAN) {
		EVCardAttribute *attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
		gboolean rv = FALSE;

		if (!attr && info->fallback_vcard40_field_name)
			attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);

		if (attr) {
			GList *v = e_vcard_attribute_get_values (attr);
			rv = v && v->data && !g_ascii_strcasecmp ((gchar *) v->data, "true");
			return rv ? (gpointer) "1" : NULL;
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_LIST) {
		EVCardAttribute *attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
		if (!attr && info->fallback_vcard40_field_name)
			attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);

		if (attr) {
			GList *list = g_list_copy (e_vcard_attribute_get_values (attr));
			GList *l;
			for (l = list; l; l = l->next)
				l->data = l->data ? g_strstrip (g_strdup (l->data)) : NULL;
			return list;
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_LIST_ELEM) {
		if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
			EVCardAttribute *attr = e_vcard_get_attribute (vcard, info->vcard_field_name);

			if (!attr && info->fallback_vcard40_field_name)
				attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);

			if (attr) {
				GList *v;

				v = e_vcard_attribute_get_values (attr);
				v = g_list_nth (v, info->list_elem);

				return (v && v->data) ? g_strstrip (g_strdup (v->data)) : NULL;
			}
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_MULTI_ELEM) {
		if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
			GList *attrs, *l;
			gint num_left = info->list_elem;

			attrs = e_vcard_get_attributes (vcard);

			for (l = attrs; l; l = l->next) {
				EVCardAttribute *attr = l->data;
				const gchar *name;

				name = e_vcard_attribute_get_name (attr);

				if (!g_ascii_strcasecmp (name, info->vcard_field_name) ||
				    (info->fallback_vcard40_field_name && !g_ascii_strcasecmp (name, info->fallback_vcard40_field_name))) {
					if (num_left-- == 0) {
						GList *v = e_vcard_attribute_get_values (attr);

						return (v && v->data) ? g_strstrip (g_strdup (v->data)) : NULL;
					}
				}
			}
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_ATTR_TYPE) {
		EVCardAttribute *attr = e_contact_find_attribute_with_types (vcard, info->vcard_field_name, info->attr_type1, info->attr_type2, info->list_elem, NULL, NULL);

		if (!attr && info->fallback_vcard40_field_name)
			attr = e_contact_find_attribute_with_types (vcard, info->fallback_vcard40_field_name, info->attr_type1, info->attr_type2, info->list_elem, NULL, NULL);

		if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
			if (attr) {
				GList *p = e_vcard_attribute_get_values (attr);
				return (p && p->data) ? g_strstrip (g_strdup (p->data)) : NULL;
			}
			else {
				return NULL;
			}
		}
		else { /* struct */
			return info->struct_getter (contact, attr);
		}

	}
	else if (info->t & E_CONTACT_FIELD_TYPE_STRUCT) {
		EVCardAttribute *attr = e_vcard_get_attribute (vcard, info->vcard_field_name);

		if (!attr && info->fallback_vcard40_field_name)
			attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);

		if (attr)
			return info->struct_getter (contact, attr);
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_GETSET) {
		EVCardAttribute *attr = e_vcard_get_attribute (vcard, info->vcard_field_name);
		gpointer rv = NULL;

		if (!attr && info->fallback_vcard40_field_name)
			attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);


		rv = info->struct_getter (contact, attr);

		if (info->t & E_CONTACT_FIELD_TYPE_STRUCT)
			return (gpointer) info->boxed_type_getter ();
		else if (!rv)
			return NULL;
		else
			return g_strstrip (g_strdup (rv));
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_SYNTHETIC) {
		switch (info->field_id) {
		case E_CONTACT_NAME_OR_ORG: {
			const gchar *str;

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

			return str ? g_strstrip (g_strdup (str)) : NULL;
		}
		case E_CONTACT_CATEGORIES: {
			EVCardAttribute *attr = e_vcard_get_attribute (vcard, EVC_CATEGORIES);

			if (attr) {
				GString *str = g_string_new ("");
				GList *v = e_vcard_attribute_get_values (attr);
				while (v) {
					g_string_append (str, (gchar *) v->data);
					v = v->next;
					if (v)
						g_string_append_c (str, ',');
				}

				return g_string_free (str, FALSE);
			}
			return NULL;
		}
		default:
			g_warning ("unhandled synthetic field 0x%02x", info->field_id);
			break;
		}
	}
	else if (info->t & E_CONTACT_FIELD_TYPE_STRING) {
		EVCardAttribute *attr;
		const gchar *cv = NULL;
		GList *v = NULL;

		/* Do our best to avoid vcard parsing by not calling
		 * e_vcard_get_attributes */

		cv = contact->priv->cached_strings[field_id];
		if (cv)
			return g_strdup (cv);

		attr = e_vcard_get_attribute (vcard, info->vcard_field_name);

		if (!attr && info->fallback_vcard40_field_name)
			attr = e_vcard_get_attribute (vcard, info->fallback_vcard40_field_name);

		if (attr)
			v = e_vcard_attribute_get_values (attr);

		return ((v && v->data) ? g_strstrip (g_strdup (v->data)) : NULL);
	}
	else { /* E_CONTACT_FIELD_TYPE_MULTI */
		GList *attrs, *l;
		GList *rv = NULL; /* used for multi attribute lists */

		attrs = e_vcard_get_attributes (vcard);

		for (l = attrs; l; l = l->next) {
			EVCardAttribute *attr = l->data;
			const gchar *name;

			name = e_vcard_attribute_get_name (attr);

			if (!g_ascii_strcasecmp (name, info->vcard_field_name) ||
			    (info->fallback_vcard40_field_name && !g_ascii_strcasecmp (name, info->fallback_vcard40_field_name))) {
				GList *v;
				v = e_vcard_attribute_get_values (attr);

				rv = g_list_append (rv, (v && v->data) ? g_strstrip (g_strdup (v->data)) : NULL);
			}
		}
		return rv;
	}
	return NULL;
}

/**
 * e_contact_get_const:
 * @contact: an #EContact
 * @field_id: an #EContactField
 *
 * Gets the value of @contact's field specified by @field_id, caching
 * the result so it can be freed later. Use e_contact_field_is_string()
 * to check whether the field can be used here.
 *
 * Returns: (transfer none): Depends on the field's type, owned by the
 * #EContact.
 **/
gconstpointer
e_contact_get_const (EContact *contact,
                     EContactField field_id)
{
	gpointer value = NULL;

	g_return_val_if_fail (E_IS_CONTACT (contact), NULL);
	g_return_val_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST, NULL);
	g_return_val_if_fail (field_info[field_id].t & E_CONTACT_FIELD_TYPE_STRING, NULL);

	value = contact->priv->cached_strings[field_id];

	if (!value) {
		value = e_contact_get (contact, field_id);
		if (value)
			contact->priv->cached_strings[field_id] = value;
	}

	return value;
}

/**
 * e_contact_set:
 * @contact: an #EContact
 * @field_id: an #EContactField
 * @value: a value whose type depends on the @field_id
 *
 * Sets the value of @contact's field specified by @field_id to @value.
 **/
void
e_contact_set (EContact *contact,
               EContactField field_id,
               gconstpointer value)
{
	d (printf ("e_contact_set (%p, %d, %p)\n", contact, field_id, value));

	g_return_if_fail (contact && E_IS_CONTACT (contact));
	g_return_if_fail (field_id >= 1 && field_id < E_CONTACT_FIELD_LAST);

	/* set the cached slot to NULL so we'll re-get the new string
	 * if e_contact_get_const is called again */
	g_free (contact->priv->cached_strings[field_id]);
	contact->priv->cached_strings[field_id] = NULL;

	g_object_set (
		contact,
		e_contact_field_name (field_id), value,
		NULL);
}

/**
 * e_contact_name_new:
 *
 * Creates a new #EContactName struct.
 *
 * Returns: A new #EContactName struct.
 **/
EContactName *
e_contact_name_new (void)
{
	return g_new0 (EContactName, 1);
}

/**
 * e_contact_name_to_string:
 * @name: an #EContactName
 *
 * Generates a string representation of @name.
 *
 * Returns: The string representation of @name.
 **/
gchar *
e_contact_name_to_string (const EContactName *name)
{
	gchar *strings[6], **stringptr = strings;

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
	return g_strjoinv (" ", strings);
}

/**
 * e_contact_name_from_string:
 * @name_str: a string representing a contact's full name
 *
 * Creates a new #EContactName based on the parsed @name_str.
 *
 * Returns: A new #EContactName struct.
 **/
EContactName *
e_contact_name_from_string (const gchar *name_str)
{
	EContactName *name;
	ENameWestern *western;

	g_return_val_if_fail (name_str != NULL, NULL);

	name = e_contact_name_new ();
	western = e_name_western_parse (name_str);

	name->prefixes = g_strdup (western->prefix);
	name->given = g_strdup (western->first );
	name->additional = g_strdup (western->middle);
	name->family = g_strdup (western->last  );
	name->suffixes = g_strdup (western->suffix);

	e_name_western_free (western);

	return name;
}

/**
 * e_contact_name_copy:
 * @n: an #EContactName
 *
 * Creates a copy of @n.
 *
 * Returns: (transfer full): A new #EContactName identical to @n.
 **/
EContactName *
e_contact_name_copy (EContactName *n)
{
	EContactName *name;

	g_return_val_if_fail (n != NULL, NULL);

	name = e_contact_name_new ();

	name->prefixes = g_strdup (n->prefixes);
	name->given = g_strdup (n->given);
	name->additional = g_strdup (n->additional);
	name->family = g_strdup (n->family);
	name->suffixes = g_strdup (n->suffixes);

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

#define E_CONTACT_DEFINE_BOXED_TYPE(_tp,_nm) \
	GType \
	_tp ## _get_type (void) \
	{ \
		static gsize type_id__volatile = 0; \
 \
		if (g_once_init_enter (&type_id__volatile)) { \
			GType type_id; \
 \
			type_id = g_boxed_type_register_static (_nm, \
				(GBoxedCopyFunc) _tp ## _copy, \
				(GBoxedFreeFunc) _tp ## _free); \
 \
			g_once_init_leave (&type_id__volatile, type_id);\
	} \
 \
	return type_id__volatile; \
}

E_CONTACT_DEFINE_BOXED_TYPE (e_contact_name, "EContactName")

static void
parse_date_part (EContactDateTime *self,
		 const gchar **inout_ptr);

/**
 * e_contact_date_from_string:
 * @str: a date string in the format YYYY-MM-DD or YYYYMMDD
 *
 * Creates a new #EContactDate based on @str.
 *
 * Returns: (transfer full): A new #EContactDate struct.
 **/
EContactDate *
e_contact_date_from_string (const gchar *str)
{
	EContactDate *date;
	EContactDateTime dt = { 0, };
	const gchar *ptr;

	g_return_val_if_fail (str != NULL, NULL);

	ptr = str;
	parse_date_part (&dt, &ptr);

	date = e_contact_date_new ();

	if (ptr != str && (*ptr == '\0' || *ptr == 't' || *ptr == 'T')) {
		date->year = dt.year;
		date->month = dt.month;
		date->day = dt.day;
	}

	return date;
}

/**
 * e_contact_date_to_string:
 * @dt: an #EContactDate
 * @for_version: an #EVCardVersion
 *
 * Generates a date string in the format YYYY-MM-DD based
 * on the values of @dt.
 *
 * Returns: A date string, owned by the caller.
 *
 * Since: 3.60
 **/
gchar *
e_contact_date_to_string (EContactDate *dt,
			  EVCardVersion for_version)
{
	EContactDateTime dtt = { 0, };

	if (!dt)
		return NULL;

	if (for_version < E_VCARD_VERSION_40)
		return g_strdup_printf (
			"%04d-%02d-%02d",
			CLAMP (dt->year, 1000, 9999),
			CLAMP (dt->month, 1, 12),
			CLAMP (dt->day, 1, 31));

	dtt.year = dt->year;
	dtt.month = dt->month;
	dtt.day = dt->day;
	dtt.hour = G_MAXUSHORT;
	dtt.minute = G_MAXUSHORT;
	dtt.second = G_MAXUSHORT;
	dtt.utc_offset = G_MAXINT;

	return e_contact_date_time_to_string (&dtt, for_version, E_CONTACT_DATE_TIME_FLAG_NONE);
}

/**
 * e_contact_date_equal:
 * @dt1: an #EContactDate
 * @dt2: an #EContactDate
 *
 * Checks if @dt1 and @dt2 are the same date.
 *
 * Returns: %TRUE if @dt1 and @dt2 are equal, %FALSE otherwise.
 **/
gboolean
e_contact_date_equal (EContactDate *dt1,
                      EContactDate *dt2)
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
 * Returns: (transfer full): A new #EContactDate struct identical to @dt.
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
 * @date: an #EContactDate
 *
 * Frees the @date struct and its contents.
 */
void
e_contact_date_free (EContactDate *date)
{
	g_free (date);
}

E_CONTACT_DEFINE_BOXED_TYPE (e_contact_date, "EContactDate")

/**
 * e_contact_date_new:
 *
 * Creates a new #EContactDate struct.
 *
 * Returns: A new #EContactDate struct.
 **/
EContactDate *
e_contact_date_new (void)
{
	return g_new0 (EContactDate, 1);
}

static gboolean
only_numbers (const gchar *str,
	      guint index_from,
	      guint use_len)
{
	guint ii;

	if (!str)
		return FALSE;

	for (ii = 0; ii < use_len; ii++) {
		const gchar val = str[index_from + ii];

		if (val < '0' || val > '9')
			return FALSE;
	}

	return TRUE;
}

static gint
str_to_num (const gchar *str,
	    guint index_from,
	    guint use_len)
{
	gint res = 0;
	guint ii;

	for (ii = 0; ii < use_len; ii++) {
		const gchar val = str[index_from + ii];

		if (val >= '0' && val <= '9') {
			res *= 10;
			res += val - '0';
		} else {
			break;
		}
	}

	return res;
}

static void
parse_date_part (EContactDateTime *self,
		 const gchar **inout_ptr)
{
	const gchar *ptr = *inout_ptr;

	#define ends_date_at(_idx) (ptr[_idx] == 't' || ptr[_idx] == 'T' || ptr[_idx] == '\0')

	/* YYYYMMDD */
	if (only_numbers (ptr, 0, 8) && ends_date_at (8)) {
		self->year = str_to_num (ptr, 0, 4);
		self->month = str_to_num (ptr, 4, 2);
		self->day = str_to_num (ptr, 6, 2);
		ptr += 8;
	/* --MMDD */
	} else if (ptr[0] == '-' && ptr[1] == '-' && only_numbers (ptr, 2, 4) && ends_date_at (6)) {
		self->month = str_to_num (ptr, 2, 2);
		self->day = str_to_num (ptr, 4, 2);
		ptr += 6;
	/* ---DD */
	} else if (ptr[0] == '-' && ptr[1] == '-' && ptr[2] == '-' && only_numbers (ptr, 3, 2) && ends_date_at (5)) {
		self->day = str_to_num (ptr, 3, 2);
		ptr += 5;
	/* YYYY-MM */
	} else if (only_numbers (ptr, 0, 4) && ptr[4] == '-' && only_numbers (ptr, 5, 2) && ends_date_at (7)) {
		self->year = str_to_num (ptr, 0, 4);
		self->month = str_to_num (ptr, 5, 2);
		ptr += 7;
	/* YYYY */
	} else if (only_numbers (ptr, 0, 4) && ends_date_at (4)) {
		self->year = str_to_num (ptr, 0, 4);
		ptr += 4;
	/* YYYY-MM-DD - it is not allowed in the vCard 4.0, but when some client accidentally uses it... */
	} else if (only_numbers (ptr, 0, 4) && ptr[4] == '-' && only_numbers (ptr, 5, 2) && ptr[7] == '-' && only_numbers (ptr, 8, 2) && ends_date_at (10)) {
		self->year = str_to_num (ptr, 0, 4);
		self->month = str_to_num (ptr, 5, 2);
		self->day = str_to_num (ptr, 8, 2);
		ptr += 10;
	}

	#undef ends_date_at

	*inout_ptr = ptr;
}

static void
parse_time_part (EContactDateTime *self,
		 const gchar **inout_ptr)
{
	const gchar *ptr = *inout_ptr;

	if (*ptr == 't' || *ptr == 'T')
		ptr++;

	/* HHMMSS */
	if (only_numbers (ptr, 0, 6)) {
		self->hour = str_to_num (ptr, 0, 2);
		self->minute = str_to_num (ptr, 2, 2);
		self->second = str_to_num (ptr, 4, 2);
		ptr += 6;

		/* Z */
		if (*ptr == 'Z' || *ptr == 'Z') {
			self->utc_offset = 0;
			ptr++;
		} else if (*ptr == '+' || *ptr == '-') {
			gint coef = *ptr == '+' ? 1 : -1;

			ptr++;

			/* ZZZZ */
			if (only_numbers (ptr, 0, 4) && ptr[4] == '\0') {
				self->utc_offset = coef * str_to_num (ptr, 0, 4);
				ptr += 4;
			/* ZZ */
			} else if (only_numbers (ptr, 0, 2) && ptr[2] == '\0') {
				self->utc_offset = coef * str_to_num (ptr, 0, 2) * 100;
				ptr += 2;
			/* ZZ:ZZ */
			} else if (only_numbers (ptr, 0, 2) && ptr[2] == ':' && str_to_num (ptr, 3, 2) && ptr[5] == '\0') {
				self->utc_offset = coef * (str_to_num (ptr, 0, 2) * 100 + str_to_num (ptr, 3, 2));
				ptr += 5;
			}
		}
	/* -MMSS */
	} else if (ptr[0] == '-' && only_numbers (ptr, 1, 4) && ptr[5] == '\0') {
		self->minute = str_to_num (ptr, 1, 2);
		self->second = str_to_num (ptr, 3, 2);
		ptr += 5;
	/* --SS */
	} else if (ptr[0] == '-' && ptr[1] == '-' && only_numbers (ptr, 2, 2) && ptr[4] == '\0') {
		self->second = str_to_num (ptr, 2, 2);
		ptr += 4;
	/* HHMM */
	} else if (only_numbers (ptr, 0, 4) && ptr[4] == '\0') {
		self->hour = str_to_num (ptr, 0, 2);
		self->minute = str_to_num (ptr, 2, 2);
		ptr += 4;
	/* HH */
	} else if (only_numbers (ptr, 0, 2) && ptr[2] == '\0') {
		self->hour = str_to_num (ptr, 0, 2);
		ptr += 2;
	/* HH-SS - not shown in the RFC 6350, but maybe valid too */
	} else if (only_numbers (ptr, 0, 2) && ptr[2] == '-' && only_numbers (ptr, 3, 2) && ptr[5] == '\0') {
		self->hour = str_to_num (ptr, 0, 2);
		self->second = str_to_num (ptr, 3, 2);
		ptr += 5;
	}

	*inout_ptr = ptr;
}

/**
 * e_contact_date_time_from_string:
 * @str: a date, time, date-time string in vCard format
 *
 * Creates a new #EContactDateTime based on @str.
 *
 * Returns: (transfer full): A new #EContactDateTime struct.
 *
 * Since: 3.60
 **/
EContactDateTime *
e_contact_date_time_from_string (const gchar *str,
				 EContactDateTimeFlags flags)
{
	EContactDateTime *self;
	const gchar *tt, *ptr;

	g_return_val_if_fail (str != NULL, NULL);

	self = e_contact_date_time_new ();

	tt = strchr (str, 'T');
	if (!tt)
		tt = strchr (str, 't');

	ptr = str;

	if (tt && tt != str) {
		parse_date_part (self, &ptr);
		if (ptr == tt)
			parse_time_part (self, &ptr);
	} else if ((flags & E_CONTACT_DATE_TIME_FLAG_TIME) != 0 || tt) {
		parse_time_part (self, &ptr);
	} else {
		parse_date_part (self, &ptr);
		if (ptr == str)
			parse_time_part (self, &ptr);
	}

	if (*ptr != '\0')
		g_clear_pointer (&self, e_contact_date_time_free);

	return self;
}

/**
 * e_contact_date_time_to_string:
 * @self: an #EContactDateTime
 * @for_version: an #EVCardVersion with version to use the format for
 * @flags: bit-or of #EContactDateTimeFlags
 *
 * Generates a date/time string in the @for_version format used by vCard standard,
 * omitting unset values of the @self. Free the returned string with
 * g_free(), when no longer needed.
 *
 * Returns: (transfer full) (nullable): a new date/time string, or %NULL,
 *    when @self is %NULL or when all members are unset.
 *
 * Since: 3.60
 **/
gchar *
e_contact_date_time_to_string (const EContactDateTime *self,
			       EVCardVersion for_version,
			       EContactDateTimeFlags flags)
{
	GString *str;
	gboolean has_date, has_time;

	g_return_val_if_fail (self != NULL, NULL);

	has_date = e_contact_date_time_has_date (self);
	has_time = e_contact_date_time_has_time (self);

	if (!has_date && !has_time)
		return NULL;

	str = g_string_new ("");

	if (has_date) {
		gboolean has_year = self->year > 0 && self->year <= 9999;
		gboolean has_month = self->month >= 1 && self->month <= 12;
		gboolean has_day = self->day >= 1 && self->day <= 31;

		if (has_year)
			g_string_append_printf (str, "%04u", self->year);
		else
			g_string_append (str, "--");

		if (has_month) {
			if (has_year && !has_day)
				g_string_append_printf (str, "-%02u", self->month);
			else
				g_string_append_printf (str, "%02u", self->month);
		} else if (has_day) {
			g_string_append_c (str, '-');
		}

		if (has_day)
			g_string_append_printf (str, "%02u", self->day);
	}

	if ((flags & E_CONTACT_DATE_TIME_FLAG_DATE_TIME) != 0 || (has_date && has_time))
		g_string_append_c (str, 'T');

	if (has_time || (flags & E_CONTACT_DATE_TIME_FLAG_DATE_TIME) != 0) {
		gboolean has_hour = self->hour < 24;
		gboolean has_minute = self->minute < 60;
		gboolean has_second = self->second <= 60;

		if (has_hour)
			g_string_append_printf (str, "%02u", self->hour);
		else
			g_string_append_c (str, '-');

		if (has_minute)
			g_string_append_printf (str, "%02u", self->minute);
		else if (has_second)
			g_string_append_c (str, '-');

		if (has_second)
			g_string_append_printf (str, "%02u", self->second);

		if (has_hour && has_minute && has_second && self->utc_offset >= -1400 && self->utc_offset <= 1400) {
			gint abs_utc_offset = (self->utc_offset < 0 ? -1 : 1) * self->utc_offset;
			gchar offset_sign = self->utc_offset < 0 ? '-' : '+';

			if (!self->utc_offset)
				g_string_append_c (str, 'Z');
			else if (abs_utc_offset <= 14)
				g_string_append_printf (str, "%c%02d", offset_sign, abs_utc_offset);
			else if ((abs_utc_offset % 100) == 0)
				g_string_append_printf (str, "%c%02d", offset_sign, abs_utc_offset / 100);
			else
				g_string_append_printf (str, "%c%04d", offset_sign, abs_utc_offset);
		}
	}

	return g_string_free (str, FALSE);
}

/**
 * e_contact_date_time_equal:
 * @dt1: an #EContactDateTime
 * @dt2: an #EContactDateTime
 *
 * Checks if @dt1 and @dt2 are the same date time.
 *
 * Returns: %TRUE if @dt1 and @dt2 are equal, %FALSE otherwise.
 *
 * Since: 3.60
 **/
gboolean
e_contact_date_time_equal (const EContactDateTime *dt1,
			   const EContactDateTime *dt2)
{
	if (dt1 && dt2) {
		return (dt1->year == dt2->year &&
			dt1->month == dt2->month &&
			dt1->day == dt2->day &&
			dt1->hour == dt2->hour &&
			dt1->minute == dt2->minute &&
			dt1->second == dt2->second &&
			dt1->utc_offset == dt2->utc_offset);
	} else
		return (!!dt1 == !!dt2);
}

/**
 * e_contact_date_time_has_date:
 * @self: an #EContactDateTime
 *
 * Checks whether the @self has set at least one of the date members.
 *
 * Returns: whether the @self has set at least one of the date members
 *
 * Since: 3.60
 **/
gboolean
e_contact_date_time_has_date (const EContactDateTime *self)
{
	return self != NULL && ((self->year > 0 && self->year <= 9999) || (self->month >= 1 && self->month <= 12) || (self->day >= 1 && self->day <= 31));
}

/**
 * e_contact_date_time_has_time:
 * @self: an #EContactDateTime
 *
 * Checks whether the @self has set at least one of the time members.
 *
 * Returns: whether the @self has set at least one of the time members
 *
 * Since: 3.60
 **/
gboolean
e_contact_date_time_has_time (const EContactDateTime *self)
{
	return self != NULL && (self->hour < 24 || self->minute < 59 || self->second <= 60);
}

/*
 * e_contact_date_time_copy:
 * @self: an #EContactDateTime
 *
 * Creates a copy of @self.
 *
 * Returns: (transfer full): A new #EContactDateTime struct identical to @self.
 */
static EContactDateTime *
e_contact_date_time_copy (EContactDateTime *self)
{
	EContactDateTime *dt2 = e_contact_date_time_new ();
	dt2->year = self->year;
	dt2->month = self->month;
	dt2->day = self->day;
	dt2->hour = self->hour;
	dt2->minute = self->minute;
	dt2->second = self->second;
	dt2->utc_offset = self->utc_offset;

	return dt2;
}

/**
 * e_contact_date_time_free:
 * @self: an #EContactDateTime
 *
 * Frees the @self struct and its content.
 *
 * Since: 3.60
 */
void
e_contact_date_time_free (EContactDateTime *self)
{
	g_free (self);
}

E_CONTACT_DEFINE_BOXED_TYPE (e_contact_date_time, "EContactDateTime")

/**
 * e_contact_date_time_new:
 *
 * Creates a new #EContactDateTime struct with unset date and time.
 *
 * Returns: A new #EContactDateTime struct.
 *
 * Since: 3.60
 **/
EContactDateTime *
e_contact_date_time_new (void)
{
	EContactDateTime *dt;

	dt = g_new0 (EContactDateTime, 1);

	dt->hour = G_MAXUSHORT;
	dt->minute = G_MAXUSHORT;
	dt->second = G_MAXUSHORT;
	dt->utc_offset = G_MAXINT;

	return dt;
}

/**
 * e_contact_photo_new:
 *
 * Creates a new #EContactPhoto struct.
 *
 * Returns: (transfer full): A new #EContactPhoto struct.
 *
 * Since: 3.2
 **/
EContactPhoto *
e_contact_photo_new (void)
{
	return g_new0 (EContactPhoto, 1);
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

	switch (photo->type) {
	case E_CONTACT_PHOTO_TYPE_INLINED:
		g_free (photo->data.inlined.mime_type);
		g_free (photo->data.inlined.data);
		break;
	case E_CONTACT_PHOTO_TYPE_URI:
		g_free (photo->data.uri);
		break;
	default:
		g_warning ("Unknown EContactPhotoType %d", photo->type);
		break;
	}

	g_free (photo);
}

/**
 * e_contact_photo_copy:
 * @photo: an #EContactPhoto
 *
 * Creates a copy of @photo.
 *
 * Returns: (transfer full): A new #EContactPhoto struct identical to @photo.
 *
 * Since: 3.8
 **/
EContactPhoto *
e_contact_photo_copy (EContactPhoto *photo)
{
	EContactPhoto *copy;

	g_return_val_if_fail (photo != NULL, NULL);

	copy = g_new0 (EContactPhoto, 1);

	switch (photo->type) {
	case E_CONTACT_PHOTO_TYPE_INLINED:
		copy->type = E_CONTACT_PHOTO_TYPE_INLINED;
		copy->data.inlined.mime_type =
			g_strdup (photo->data.inlined.mime_type);
		copy->data.inlined.length = photo->data.inlined.length;
		copy->data.inlined.data = g_malloc (copy->data.inlined.length);
		memcpy (
			copy->data.inlined.data,
			photo->data.inlined.data,
			photo->data.inlined.length);
		break;
	case E_CONTACT_PHOTO_TYPE_URI:
		copy->type = E_CONTACT_PHOTO_TYPE_URI;
		copy->data.uri = g_strdup (photo->data.uri);
		break;
	default:
		g_warning ("Unknown EContactPhotoType %d", photo->type);
		break;
	}

	return copy;
}

/**
 * e_contact_photo_get_inlined:
 * @photo: an #EContactPhoto
 * @len: (out): the length of the inlined data
 *
 * Gets the @photo's data.
 *
 * Returns: (transfer none) (array length=len) (nullable): the inlined image in the
 * #EContactPhoto, or %NULL if it has not been set.
 *
 * Since: 3.2
 **/
const guchar *
e_contact_photo_get_inlined (EContactPhoto *photo,
                             gsize *len)
{
	g_return_val_if_fail (photo != NULL, NULL);
	g_return_val_if_fail (photo->type == E_CONTACT_PHOTO_TYPE_INLINED, NULL);

	*len = photo->data.inlined.length;
	return photo->data.inlined.data;
}

/**
 * e_contact_photo_set_inlined:
 * @photo: an #EContactPhoto
 * @data: (transfer none) (array length=len): the inlined image data
 * @len: the length of @data
 *
 * Sets the @photo's inlined data.
 *
 * Since: 3.2
 **/
void
e_contact_photo_set_inlined (EContactPhoto *photo,
                             const guchar *data,
                             gsize len)
{
	g_return_if_fail (photo != NULL);
	g_return_if_fail (photo->type == E_CONTACT_PHOTO_TYPE_INLINED);

	photo->data.inlined.data = g_malloc (len);
	memcpy (photo->data.inlined.data, data, len);
	photo->data.inlined.length = len;
}

/**
 * e_contact_photo_get_mime_type:
 * @photo: an #EContactPhoto
 *
 * Gets the @photo's mime type.
 *
 * Returns: (transfer none) (nullable): the MIME type of the image, or %NULL if it has not been set.
 *
 * Since: 3.2
 **/
const gchar *
e_contact_photo_get_mime_type (EContactPhoto *photo)
{
	g_return_val_if_fail (photo != NULL, NULL);
	g_return_val_if_fail (photo->type == E_CONTACT_PHOTO_TYPE_INLINED, NULL);

	return photo->data.inlined.mime_type;
}

/**
 * e_contact_photo_set_mime_type:
 * @photo: an #EContactPhoto
 * @mime_type: the mime type
 *
 * Sets the @photo's mime type.
 *
 * Since: 3.2
 **/
void
e_contact_photo_set_mime_type (EContactPhoto *photo,
                               const gchar *mime_type)
{
	g_return_if_fail (photo != NULL);
	g_return_if_fail (photo->type == E_CONTACT_PHOTO_TYPE_INLINED);

	g_free (photo->data.inlined.mime_type);
	photo->data.inlined.mime_type = g_strdup (mime_type);
}

/**
 * e_contact_photo_get_uri:
 * @photo: an #EContactPhoto
 *
 * Gets the @photo's URI.
 *
 * Returns: (transfer none) (nullable): the URI of the image, or %NULL if it has not been set
 *
 * Since: 3.2
 **/
const gchar *
e_contact_photo_get_uri (EContactPhoto *photo)
{
	g_return_val_if_fail (photo != NULL, NULL);
	g_return_val_if_fail (photo->type == E_CONTACT_PHOTO_TYPE_URI, NULL);

	return photo->data.uri;
}

/**
 * e_contact_photo_set_uri:
 * @photo: an #EContactPhoto
 * @uri: the @photo's URI
 *
 * Sets the @photo's URI.
 *
 * Since: 3.2
 **/
void
e_contact_photo_set_uri (EContactPhoto *photo,
                         const gchar *uri)
{
	g_return_if_fail (photo != NULL);
	g_return_if_fail (photo->type == E_CONTACT_PHOTO_TYPE_URI);

	g_free (photo->data.uri);
	photo->data.uri = g_strdup (uri);
}

/* Try to unescape a mime type which was encoded into
 * the filename, return the mime type if g_content_type_from_mime_type()
 * returns something for the decoded filename extension.
 */
static gchar *
mime_type_from_filename (const gchar *filename)
{
	gchar *extension;
	gchar *mime_type;
	gchar *content_type;
	guint len;

	extension = strrchr (filename, '.');
	if (extension)
		extension++;

	if (!extension)
		return NULL;

	len = strlen (extension);

	if (len == 3 || len == 4) {
		mime_type = g_strconcat ("image/", extension, NULL);

	/* The '/' is URL-escaped to '%2F' and the '%' is replaced to '-',
	   to avoid URL-escaping in the URI */
	} else if (strstr (extension, "-2F") || strstr (extension, "-2f")) {
		gchar *copy, *pp, *ww;

		copy = g_strdup (extension);

		/* De-mangle dashes to percent marks; cannot recognize valid dashes,
		   but the extension might be "image-2Fext", which should be fine
		   (it decodes to "image/ext" at the end). */
		for (pp = copy, ww = pp; *pp; pp++, ww++) {
			if (*pp == '-' && pp[1] == '2' && (pp[2] == 'F' || pp[2] == 'f')) {
				*ww = '/';
				pp += 2;
			} else if (pp != ww) {
				*ww = *pp;
			}
		}

		*ww = '\0';

		mime_type = g_uri_unescape_string (copy, NULL);

		g_free (copy);
	} else {
		mime_type = g_uri_unescape_string (extension, NULL);
	}

	content_type = mime_type ? g_content_type_from_mime_type (mime_type) : NULL;

	if (!content_type) {
		g_free (mime_type);
		mime_type = NULL;
	}

	g_free (content_type);

	return mime_type;
}

static gboolean
e_contact_photo_make_inline (EContactPhoto *photo,
                             GError **error)
{
	gchar *filename;
	gchar *contents = NULL;
	gsize  length;
	gboolean success = FALSE;

	/* Just a sanity check, this wont happen but return TRUE anyway */
	if (photo->type != E_CONTACT_PHOTO_TYPE_URI)
		return TRUE;

	filename = g_filename_from_uri (photo->data.uri, NULL, error);
	if (!filename)
		return FALSE;

	if (g_file_get_contents (filename, &contents, &length, error)) {
		gchar *mime_type;

		mime_type = mime_type_from_filename (filename);
		if (!mime_type) {
			gchar *content_type =
				g_content_type_guess (NULL, (const guchar *) contents, length, NULL);

			if (content_type)
				mime_type = g_content_type_get_mime_type (content_type);

			g_free (content_type);
		}

		g_free (photo->data.uri);

		photo->type = E_CONTACT_PHOTO_TYPE_INLINED;
		photo->data.inlined.data = (guchar *) contents;
		photo->data.inlined.length = length;
		photo->data.inlined.mime_type = mime_type;

		success = TRUE;
	}

	g_free (filename);
	return success;
}

static gboolean
e_contact_inline_photo_field (EContact *contact,
                              EContactField field,
                              GError **error)
{
	EContactPhoto *photo;
	gboolean       success = TRUE;

	photo = e_contact_get (contact, field);
	if (photo) {

		if (photo->type == E_CONTACT_PHOTO_TYPE_URI &&
		    g_str_has_prefix (photo->data.uri, "file://")) {
			success = e_contact_photo_make_inline (photo, error);

			if (success)
				e_contact_set (contact, field, photo);
		}
		e_contact_photo_free (photo);
	}

	return success;
}

/**
 * e_contact_inline_local_photos:
 * @contact: an #EContact
 * @error: the location to store any #GError which might occur
 *
 * Tries to modify any #EContactPhoto fields which are
 * stored on the local file system as type %E_CONTACT_PHOTO_TYPE_URI
 * to be inlined and stored as %E_CONTACT_PHOTO_TYPE_INLINED instead.
 *
 * Returns: %TRUE if there were no errors, upon error %FALSE is returned
 *    and @error is set.
 *
 * Since: 3.4
 */
gboolean
e_contact_inline_local_photos (EContact *contact,
                               GError **error)
{
	gboolean success = TRUE;

	g_return_val_if_fail (E_IS_CONTACT (contact), FALSE);

	success = e_contact_inline_photo_field (contact, E_CONTACT_PHOTO, error);
	if (success)
		success = e_contact_inline_photo_field (contact, E_CONTACT_LOGO, error);

	return success;
}

E_CONTACT_DEFINE_BOXED_TYPE (e_contact_photo, "EContactPhoto")

/**
 * e_contact_geo_new:
 *
 * Creates an #EContactGeo struct with all coordinates set to 0.
 *
 * Returns: (transfer full): A new #EContactGeo struct.
 *
 * Since: 3.8
 **/
EContactGeo *
e_contact_geo_new (void)
{
	EContactGeo *geo;
	geo = g_new0 (EContactGeo, 1);
	geo->latitude = 0;
	geo->longitude = 0;
	return geo;
}

/**
 * e_contact_geo_free:
 * @geo: an #EContactGeo
 *
 * Frees the @geo struct and its contents.
 *
 * Since: 1.12
 **/
void
e_contact_geo_free (EContactGeo *geo)
{
	g_free (geo);
}

/**
 * e_contact_geo_copy:
 * @geo: an #EContactGeo
 *
 * Creates a copy of @geo.
 *
 * Returns: (transfer full): A new #EContactGeo struct identical to @geo.
 **/
static EContactGeo *
e_contact_geo_copy (EContactGeo *geo)
{
	EContactGeo *geo2 = g_new0 (EContactGeo, 1);
	geo2->latitude = geo->latitude;
	geo2->longitude = geo->longitude;

	return geo2;
}

E_CONTACT_DEFINE_BOXED_TYPE (e_contact_geo, "EContactGeo")

/**
 * e_contact_address_new:
 *
 * Creates a new #EContactAddress struct.
 *
 * Returns: (transfer full): A new #EContactAddress struct.
 *
 * Since: 3.2
 **/
EContactAddress *
e_contact_address_new (void)
{
	return g_new0 (EContactAddress, 1);
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

/**
 * e_contact_address_copy:
 * @address: an #EContactAddress
 *
 * Creates a copy of @address.
 *
 * Returns: (transfer full): A new #EContactAddress struct identical to @address.
 **/
static EContactAddress *
e_contact_address_copy (EContactAddress *address)
{
	EContactAddress *address2 = g_new0 (EContactAddress, 1);

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

E_CONTACT_DEFINE_BOXED_TYPE (e_contact_address, "EContactAddress")

/**
 * e_contact_cert_new:
 *
 * Creates an #EContactCert struct with all values set to 0.
 *
 * Returns: (transfer full): A new #EContactCert struct.
 *
 * Since: 3.8
 **/
EContactCert *
e_contact_cert_new (void)
{
	EContactCert *cert;
	cert = g_new0 (EContactCert, 1);
	return cert;
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

/**
 * e_contact_cert_copy:
 * @cert: an #EContactCert
 *
 * Creates a copy of @cert.
 *
 * Returns: (transfer full): A new #EContactCert struct identical to @cert.
 **/
static EContactCert *
e_contact_cert_copy (EContactCert *cert)
{
	EContactCert *cert2 = g_new0 (EContactCert, 1);
	cert2->length = cert->length;
	cert2->data = g_malloc (cert2->length);
	memcpy (cert2->data, cert->data, cert->length);

	return cert2;
}

E_CONTACT_DEFINE_BOXED_TYPE (e_contact_cert, "EContactCert")

/**
 * e_contact_attr_list_copy:
 * @list: (element-type utf8): A #GList of strings
 *
 * Copies a list of allocated strings, specifically
 * for the #EContactAttrList boxed type used for multi valued
 * contact fields.
 *
 * Returns: (transfer full) (element-type utf8): A copy of @list
 *
 * Since: 3.8
 */
GList *
e_contact_attr_list_copy (GList *list)
{
	return g_list_copy_deep (list, (GCopyFunc) g_strdup, NULL);
}

/**
 * e_contact_attr_list_free:
 * @list: (element-type utf8): A #GList of strings
 *
 * Frees a list of allocated strings, specifically
 * for the #EContactAttrList boxed type used for multi valued
 * contact fields.
 *
 * Since: 3.8
 */
void
e_contact_attr_list_free (GList *list)
{
	g_list_free_full (list, (GDestroyNotify) g_free);
}

typedef GList EContactAttrList;
G_DEFINE_BOXED_TYPE (EContactAttrList, e_contact_attr_list, e_contact_attr_list_copy, e_contact_attr_list_free);

/**
 * e_contact_gender_sex_to_string:
 * @sex: an #EContactGenderSex
 *
 * Converts the @sex into a string, as used by the vCard 4.0 standard.
 *
 * Returns: (nullable): a vCard string representation of the @sex, or %NULL,
 *    when the value is not recognized
 *
 * Since: 3.60
 **/
const gchar *
e_contact_gender_sex_to_string (EContactGenderSex sex)
{
	switch (sex) {
	case E_CONTACT_GENDER_SEX_UNKNOWN:
		return "U";
	case E_CONTACT_GENDER_SEX_NOT_SET:
		return "";
	case E_CONTACT_GENDER_SEX_MALE:
		return "M";
	case E_CONTACT_GENDER_SEX_FEMALE:
		return "F";
	case E_CONTACT_GENDER_SEX_OTHER:
		return "O";
	case E_CONTACT_GENDER_SEX_NOT_APPLICABLE:
		return "N";
	}

	return NULL;
}

/**
 * e_contact_gender_sex_from_string:
 * @string: a string representation of a gender sex
 *
 * Converts a string into an #EContactGenderSex. Unrecognized values
 * are returned as %E_CONTACT_GENDER_SEX_NOT_SET.
 *
 * Returns: a corresponding #EContactGenderSex
 *
 * Since: 3.60
 **/
EContactGenderSex
e_contact_gender_sex_from_string (const gchar *string)
{
	g_return_val_if_fail (string != NULL, E_CONTACT_GENDER_SEX_NOT_SET);

	if (g_strcmp0 (string, "M") == 0 ||
	    g_strcmp0 (string, "m") == 0)
		return E_CONTACT_GENDER_SEX_MALE;
	if (g_strcmp0 (string, "F") == 0 ||
	    g_strcmp0 (string, "f") == 0)
		return E_CONTACT_GENDER_SEX_FEMALE;
	if (g_strcmp0 (string, "O") == 0 ||
	    g_strcmp0 (string, "o") == 0)
		return E_CONTACT_GENDER_SEX_OTHER;
	if (g_strcmp0 (string, "N") == 0 ||
	    g_strcmp0 (string, "n") == 0)
		return E_CONTACT_GENDER_SEX_NOT_APPLICABLE;
	if (g_strcmp0 (string, "U") == 0 ||
	    g_strcmp0 (string, "u") == 0)
		return E_CONTACT_GENDER_SEX_UNKNOWN;

	return E_CONTACT_GENDER_SEX_NOT_SET;
}

E_CONTACT_DEFINE_BOXED_TYPE (e_contact_gender, "EContactGender")

/**
 * e_contact_gender_new:
 *
 * Creates a new empty #EContactGender structure. Free it
 * with e_contact_gender_free() when no longer needed.
 *
 * Returns: (transfer full): a new empty #EContactGender
 *
 * Since: 3.60
 **/
EContactGender *
e_contact_gender_new (void)
{
	return g_new0 (EContactGender, 1);
}

/**
 * e_contact_gender_copy:
 * @self: (nullable): an #EContactGender
 *
 * Creates a copy of the @self. Returns %NULL when @self is %NULL.
 * Free it with e_contact_gender_free(), when no longer needed.
 *
 * Returns: (nullable) (transfer full): a new copy of
 *    the @self, or %NULL when the @self is %NULL
 *
 * Since: 3.60
 **/
EContactGender *
e_contact_gender_copy (EContactGender *self)
{
	EContactGender *copy;

	if (!self)
		return NULL;

	copy = e_contact_gender_new ();
	copy->sex = self->sex;
	copy->identity = g_strdup (self->identity);

	return copy;
}

/**
 * e_contact_gender_free:
 * @self: (nullable) (transfer full): an #EContactGender
 *
 * Frees the @self. Does nothing, when @self is %NULL.
 *
 * Since: 3.60
 **/
void
e_contact_gender_free (EContactGender *self)
{
	if (self) {
		g_free (self->identity);
		g_free (self);
	}
}

/**
 * e_contact_impp_scheme_to_field:
 * @scheme: a URI scheme as "icq:", but can be with a value too
 * @out_scheme_len: (out) (optional): optional place where to store how many letters the scheme has
 *
 * Tries to find one of the predefined #EContactField for
 * the @scheme, which can be either the scheme itself, like "icq:",
 * or a scheme with the value, like "icq:1234". The @out_scheme_len
 * can be used to return how many letters from the beginning
 * of the @scheme can be skipped to get to the actual value.
 * Returns value %E_CONTACT_FIELD_LAST means no matching
 * #EContactField was found for the @scheme. The %E_CONTACT_IMPP
 * is never returned, then the @scheme can be a valid scheme,
 * only the #EContactField does not have a mapping for it.
 *
 * Returns: an #EContactField corresponding to the @scheme,
 *    or %E_CONTACT_FIELD_LAST when no mapping exists for the @scheme.
 *
 * Since: 3.60
 **/
EContactField
e_contact_impp_scheme_to_field (const gchar *scheme,
				guint *out_scheme_len)
{
	EContactField field_id = E_CONTACT_FIELD_LAST;
	guint scheme_len = 0;

	if (scheme == NULL || *scheme == '\0')
		return E_CONTACT_FIELD_LAST;

	impp_map_scheme_field (E_CONTACT_FIELD_LAST, NULL, scheme,  &field_id, &scheme_len, 0, NULL);

	if (out_scheme_len)
		*out_scheme_len = scheme_len;

	return field_id;
}

/**
 * e_contact_impp_field_to_scheme:
 * @field: an #EContactField, one of E_CONTACT_IM_....
 *
 * Gets a scheme (like "icq:") for the corresponding IM @field.
 *
 * Returns: (nullable): a scheme for the corresponding IM field, or %NULL
 *    when the @field is no predefined IM field.
 *
 * Since: 3.60
 **/
const gchar *
e_contact_impp_field_to_scheme (EContactField field)
{
	const gchar *scheme = NULL;

	impp_map_scheme_field (field, &scheme, NULL, NULL, NULL, 0, NULL);

	return scheme;
}
