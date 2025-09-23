/* e-contact.h
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
 * Authors: Chris Toshok <toshok@ximian.com>
 *          Tristan Van Berkom <tristanvb@openismus.com>
 */

#if !defined (__LIBEBOOK_CONTACTS_H_INSIDE__) && !defined (LIBEBOOK_CONTACTS_COMPILATION)
#error "Only <libebook-contacts/libebook-contacts.h> should be included directly."
#endif

#ifndef E_CONTACT_H
#define E_CONTACT_H

#include <time.h>
#include <stdio.h>
#include <libebook-contacts/e-vcard.h>

/* Standard GObject macros */
#define E_TYPE_CONTACT \
	(e_contact_get_type ())
#define E_CONTACT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CONTACT, EContact))
#define E_CONTACT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CONTACT, EContactClass))
#define E_IS_CONTACT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CONTACT))
#define E_IS_CONTACT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CONTACT))
#define E_CONTACT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CONTACT, EContactClass))

#define E_TYPE_CONTACT_DATE       (e_contact_date_get_type ())
#define E_TYPE_CONTACT_DATE_TIME  (e_contact_date_time_get_type ())
#define E_TYPE_CONTACT_NAME       (e_contact_name_get_type ())
#define E_TYPE_CONTACT_PHOTO      (e_contact_photo_get_type ())
#define E_TYPE_CONTACT_CERT       (e_contact_cert_get_type ())
#define E_TYPE_CONTACT_ADDRESS    (e_contact_address_get_type ())
#define E_TYPE_CONTACT_ATTR_LIST  (e_contact_attr_list_get_type ())
#define E_TYPE_CONTACT_GENDER     (e_contact_gender_get_type ())

G_BEGIN_DECLS

typedef struct _EContact EContact;
typedef struct _EContactClass EContactClass;
typedef struct _EContactPrivate EContactPrivate;

typedef enum {

	E_CONTACT_UID = 1,	 /* string field */
	E_CONTACT_FILE_AS,	 /* string field */
	E_CONTACT_BOOK_UID,      /* string field */

	/* Name fields */
	E_CONTACT_FULL_NAME,	 /* string field */
	E_CONTACT_GIVEN_NAME,	 /* synthetic string field */
	E_CONTACT_FAMILY_NAME,	 /* synthetic string field */
	E_CONTACT_NICKNAME,	 /* string field */

	/* Email fields */
	E_CONTACT_EMAIL_1,	 /* synthetic string field */
	E_CONTACT_EMAIL_2,	 /* synthetic string field */
	E_CONTACT_EMAIL_3,	 /* synthetic string field */
	E_CONTACT_EMAIL_4,       /* synthetic string field */

	E_CONTACT_MAILER,        /* string field */

	/* Address Labels */
	E_CONTACT_ADDRESS_LABEL_HOME,  /* synthetic string field */
	E_CONTACT_ADDRESS_LABEL_WORK,  /* synthetic string field */
	E_CONTACT_ADDRESS_LABEL_OTHER, /* synthetic string field */

	/* Phone fields */
	E_CONTACT_PHONE_ASSISTANT,
	E_CONTACT_PHONE_BUSINESS,
	E_CONTACT_PHONE_BUSINESS_2,
	E_CONTACT_PHONE_BUSINESS_FAX,
	E_CONTACT_PHONE_CALLBACK,
	E_CONTACT_PHONE_CAR,
	E_CONTACT_PHONE_COMPANY,
	E_CONTACT_PHONE_HOME,
	E_CONTACT_PHONE_HOME_2,
	E_CONTACT_PHONE_HOME_FAX,
	E_CONTACT_PHONE_ISDN,
	E_CONTACT_PHONE_MOBILE,
	E_CONTACT_PHONE_OTHER,
	E_CONTACT_PHONE_OTHER_FAX,
	E_CONTACT_PHONE_PAGER,
	E_CONTACT_PHONE_PRIMARY,
	E_CONTACT_PHONE_RADIO,
	E_CONTACT_PHONE_TELEX,
	E_CONTACT_PHONE_TTYTDD,

	/* Organizational fields */
	E_CONTACT_ORG,		 /* string field */
	E_CONTACT_ORG_UNIT,	 /* string field */
	E_CONTACT_OFFICE,	 /* string field */
	E_CONTACT_TITLE,	 /* string field */
	E_CONTACT_ROLE,	 /* string field */
	E_CONTACT_MANAGER,	 /* string field */
	E_CONTACT_ASSISTANT,	 /* string field */

	/* Web fields */
	E_CONTACT_HOMEPAGE_URL,  /* string field */
	E_CONTACT_BLOG_URL,      /* string field */

	/* Contact categories */
	E_CONTACT_CATEGORIES,    /* string field */

	/* Collaboration fields */
	E_CONTACT_CALENDAR_URI,  /* string field */
	E_CONTACT_FREEBUSY_URL,  /* string field */
	E_CONTACT_ICS_CALENDAR,  /* string field */
	E_CONTACT_VIDEO_URL,      /* string field */

	/* misc fields */
	E_CONTACT_SPOUSE,        /* string field */
	E_CONTACT_NOTE,          /* string field */

	E_CONTACT_IM_AIM_HOME_1,       /* Synthetic string field */
	E_CONTACT_IM_AIM_HOME_2,       /* Synthetic string field */
	E_CONTACT_IM_AIM_HOME_3,       /* Synthetic string field */
	E_CONTACT_IM_AIM_WORK_1,       /* Synthetic string field */
	E_CONTACT_IM_AIM_WORK_2,       /* Synthetic string field */
	E_CONTACT_IM_AIM_WORK_3,       /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_HOME_1, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_HOME_2, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_HOME_3, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_WORK_1, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_WORK_2, /* Synthetic string field */
	E_CONTACT_IM_GROUPWISE_WORK_3, /* Synthetic string field */
	E_CONTACT_IM_JABBER_HOME_1,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_HOME_2,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_HOME_3,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_WORK_1,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_WORK_2,    /* Synthetic string field */
	E_CONTACT_IM_JABBER_WORK_3,    /* Synthetic string field */
	E_CONTACT_IM_YAHOO_HOME_1,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_HOME_2,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_HOME_3,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_WORK_1,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_WORK_2,     /* Synthetic string field */
	E_CONTACT_IM_YAHOO_WORK_3,     /* Synthetic string field */
	E_CONTACT_IM_MSN_HOME_1,       /* Synthetic string field */
	E_CONTACT_IM_MSN_HOME_2,       /* Synthetic string field */
	E_CONTACT_IM_MSN_HOME_3,       /* Synthetic string field */
	E_CONTACT_IM_MSN_WORK_1,       /* Synthetic string field */
	E_CONTACT_IM_MSN_WORK_2,       /* Synthetic string field */
	E_CONTACT_IM_MSN_WORK_3,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_HOME_1,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_HOME_2,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_HOME_3,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_WORK_1,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_WORK_2,       /* Synthetic string field */
	E_CONTACT_IM_ICQ_WORK_3,       /* Synthetic string field */

	/* Convenience field for getting a name from the contact.
	 * Returns the first one of[File-As, Full Name, Org, Email1]
	 * to be set */
	E_CONTACT_REV,     /* string field to hold  time of last update to this vcard */
	E_CONTACT_NAME_OR_ORG,

	/* Address fields */
	E_CONTACT_ADDRESS,       /* Multi-valued structured (EContactAddress) */
	E_CONTACT_ADDRESS_HOME,  /* synthetic structured field (EContactAddress) */
	E_CONTACT_ADDRESS_WORK,  /* synthetic structured field (EContactAddress) */
	E_CONTACT_ADDRESS_OTHER, /* synthetic structured field (EContactAddress) */

	E_CONTACT_CATEGORY_LIST, /* multi-valued */

	/* Photo/Logo */
	E_CONTACT_PHOTO,	 /* structured field (EContactPhoto) */
	E_CONTACT_LOGO,	 /* structured field (EContactPhoto) */

	E_CONTACT_NAME,		 /* structured field (EContactName) */
	E_CONTACT_EMAIL,	 /* Multi-valued */

	/* Instant Messaging fields */
	E_CONTACT_IM_AIM,	 /* Multi-valued */
	E_CONTACT_IM_GROUPWISE,  /* Multi-valued */
	E_CONTACT_IM_JABBER,	 /* Multi-valued */
	E_CONTACT_IM_YAHOO,	 /* Multi-valued */
	E_CONTACT_IM_MSN,	 /* Multi-valued */
	E_CONTACT_IM_ICQ,	 /* Multi-valued */

	E_CONTACT_WANTS_HTML,    /* boolean field */

	/* fields used for describing contact lists.  a contact list
	 * is just a contact with _IS_LIST set to true.  the members
	 * are listed in the _EMAIL field. */
	E_CONTACT_IS_LIST,             /* boolean field */
	E_CONTACT_LIST_SHOW_ADDRESSES, /* boolean field */

	E_CONTACT_BIRTH_DATE,    /* structured field (EContactDate) */
	E_CONTACT_ANNIVERSARY,   /* structured field (EContactDate) */

	/* Security Fields */
	E_CONTACT_X509_CERT,     /* structured field (EContactCert) */
	E_CONTACT_PGP_CERT,      /* structured field (EContactCert) */

	E_CONTACT_IM_GADUGADU_HOME_1,  /* Synthetic string field */
	E_CONTACT_IM_GADUGADU_HOME_2,  /* Synthetic string field */
	E_CONTACT_IM_GADUGADU_HOME_3,  /* Synthetic string field */
	E_CONTACT_IM_GADUGADU_WORK_1,  /* Synthetic string field */
	E_CONTACT_IM_GADUGADU_WORK_2,  /* Synthetic string field */
	E_CONTACT_IM_GADUGADU_WORK_3,  /* Synthetic string field */

	E_CONTACT_IM_GADUGADU,   /* Multi-valued */

	E_CONTACT_GEO,	/* structured field (EContactGeo) */

	E_CONTACT_TEL, /* list of strings */

	E_CONTACT_IM_SKYPE_HOME_1,     /* Synthetic string field */
	E_CONTACT_IM_SKYPE_HOME_2,     /* Synthetic string field */
	E_CONTACT_IM_SKYPE_HOME_3,     /* Synthetic string field */
	E_CONTACT_IM_SKYPE_WORK_1,     /* Synthetic string field */
	E_CONTACT_IM_SKYPE_WORK_2,     /* Synthetic string field */
	E_CONTACT_IM_SKYPE_WORK_3,     /* Synthetic string field */
	E_CONTACT_IM_SKYPE,		/* Multi-valued */

	E_CONTACT_SIP,

	E_CONTACT_IM_GOOGLE_TALK_HOME_1,     /* Synthetic string field */
	E_CONTACT_IM_GOOGLE_TALK_HOME_2,     /* Synthetic string field */
	E_CONTACT_IM_GOOGLE_TALK_HOME_3,     /* Synthetic string field */
	E_CONTACT_IM_GOOGLE_TALK_WORK_1,     /* Synthetic string field */
	E_CONTACT_IM_GOOGLE_TALK_WORK_2,     /* Synthetic string field */
	E_CONTACT_IM_GOOGLE_TALK_WORK_3,     /* Synthetic string field */
	E_CONTACT_IM_GOOGLE_TALK,		/* Multi-valued */

	E_CONTACT_IM_TWITTER,		/* Multi-valued */

	E_CONTACT_IM_MATRIX_HOME_1,	/* Synthetic string field */
	E_CONTACT_IM_MATRIX_HOME_2,	/* Synthetic string field */
	E_CONTACT_IM_MATRIX_HOME_3,	/* Synthetic string field */
	E_CONTACT_IM_MATRIX_WORK_1,	/* Synthetic string field */
	E_CONTACT_IM_MATRIX_WORK_2,	/* Synthetic string field */
	E_CONTACT_IM_MATRIX_WORK_3,	/* Synthetic string field */
	E_CONTACT_IM_MATRIX,		/* Multi-valued */

	/* vCard 4.0 properties */
	E_CONTACT_KIND,			/* string */
	E_CONTACT_SOURCE,		/* string (uri) */
	E_CONTACT_XML,			/* string */
	E_CONTACT_GENDER,		/* structured field (EContactGender) */
	E_CONTACT_IMPP,			/* Multi-valued; read IMPP as attributes, which are either EVC_IMPP (vCard 4.0) or EVC_X_EVOLUTION_IMPP (vCard 3.0 for unknown IM schemes) */
	E_CONTACT_LANG,			/* Multi-valued */
	E_CONTACT_MEMBER,		/* Multi-valued */
	E_CONTACT_RELATED,		/* Multi-valued */
	E_CONTACT_BIRTHPLACE,		/* string */
	E_CONTACT_DEATHPLACE,		/* string */
	E_CONTACT_DEATHDATE,		/* structured field (EContactDate) */
	E_CONTACT_EXPERTISE,		/* Multi-valued */
	E_CONTACT_HOBBY,		/* Multi-valued */
	E_CONTACT_INTEREST,		/* Multi-valued */
	E_CONTACT_ORG_DIRECTORY,	/* Multi-valued */
	E_CONTACT_CONTACT_URI,		/* string */
	E_CONTACT_CREATED,		/* structured field (EContactDateTime) */
	E_CONTACT_SOCIALPROFILE,	/* string */

	E_CONTACT_FIELD_LAST,
	E_CONTACT_FIELD_FIRST = E_CONTACT_UID,

	/* useful constants */
	E_CONTACT_LAST_SIMPLE_STRING = E_CONTACT_NAME_OR_ORG,
	E_CONTACT_FIRST_PHONE_ID = E_CONTACT_PHONE_ASSISTANT,
	E_CONTACT_LAST_PHONE_ID = E_CONTACT_PHONE_TTYTDD,
	E_CONTACT_FIRST_EMAIL_ID = E_CONTACT_EMAIL_1,
	E_CONTACT_LAST_EMAIL_ID = E_CONTACT_EMAIL_4,
	E_CONTACT_FIRST_ADDRESS_ID = E_CONTACT_ADDRESS_HOME,
	E_CONTACT_LAST_ADDRESS_ID = E_CONTACT_ADDRESS_OTHER,
	E_CONTACT_FIRST_LABEL_ID = E_CONTACT_ADDRESS_LABEL_HOME,
	E_CONTACT_LAST_LABEL_ID = E_CONTACT_ADDRESS_LABEL_OTHER

} EContactField;

typedef struct {
	gchar *family;
	gchar *given;
	gchar *additional;
	gchar *prefixes;
	gchar *suffixes;
} EContactName;

/**
 * EContactGeo:
 * @latitude: latitude
 * @longitude: longitude
 *
 * Since: 1.12
 **/
typedef struct {
	gdouble latitude;
	gdouble longitude;
} EContactGeo;

typedef enum {
	E_CONTACT_PHOTO_TYPE_INLINED,
	E_CONTACT_PHOTO_TYPE_URI
} EContactPhotoType;

typedef struct {
	EContactPhotoType type;
	union {
		struct {
			gchar *mime_type;
			gsize length;
			guchar *data;
		} inlined;
		gchar *uri;
	} data;
} EContactPhoto;

typedef struct {
	gchar *address_format; /* the two letter country code that
				* determines the format/meaning of the
				* following fields */
	gchar *po;
	gchar *ext;
	gchar *street;
	gchar *locality;
	gchar *region;
	gchar *code;
	gchar *country;
} EContactAddress;

typedef struct {
	guint year;
	guint month;
	guint day;
} EContactDate;

typedef struct {
	gushort year;	/* zero for unset */
	gushort month;	/* zero for unset */
	gushort day;	/* zero for unset */
	gushort hour;	/* out of bounds for unset (0-23) */
	gushort minute;	/* out of bounds for unset (0-59) */
	gushort second;	/* out of bounds for unset (0-59) */
	gint utc_offset;/* out of bounds for unset (-1400 - +1400, as HHMM) */
} EContactDateTime;

typedef struct {
	gsize length;
	gchar *data;
} EContactCert;

struct _EContact {
	EVCard parent;
	/*< private >*/
	EContactPrivate *priv;
};

struct _EContactClass {
	EVCardClass parent_class;

	/* Padding for future expansion */
	void (*_ebook_reserved0) (void);
	void (*_ebook_reserved1) (void);
	void (*_ebook_reserved2) (void);
	void (*_ebook_reserved3) (void);
	void (*_ebook_reserved4) (void);
};

GType		e_contact_get_type		(void) G_GNUC_CONST;
EContact *	e_contact_new			(void);
EContact *	e_contact_new_from_vcard	(const gchar *vcard);
EContact *	e_contact_new_from_vcard_with_uid
						(const gchar *vcard,
						 const gchar *uid);
EContact *	e_contact_duplicate		(EContact *contact);
EContact *	e_contact_convert		(EContact *self,
						 EVCardVersion to_version);
gpointer	e_contact_get			(EContact *contact,
						 EContactField field_id);
gconstpointer	e_contact_get_const		(EContact *contact,
						 EContactField field_id);
void		e_contact_set			(EContact *contact,
						 EContactField field_id,
						 gconstpointer value);

/* misc functions for structured values */
GType		e_contact_date_get_type		(void);
EContactDate *	e_contact_date_new		(void);
EContactDate *	e_contact_date_from_string	(const gchar *str);
gchar *		e_contact_date_to_string	(EContactDate *dt,
						 EVCardVersion for_version);
gboolean	e_contact_date_equal		(EContactDate *dt1,
						 EContactDate *dt2);
void		e_contact_date_free		(EContactDate *date);

GType		e_contact_date_time_get_type	(void);
EContactDateTime *
		e_contact_date_time_new		(void);
EContactDateTime *
		e_contact_date_time_from_string	(const gchar *str,
						 EContactDateTimeFlags flags);
gchar *		e_contact_date_time_to_string	(const EContactDateTime *self,
						 EVCardVersion for_version,
						 EContactDateTimeFlags flags);
gboolean	e_contact_date_time_equal	(const EContactDateTime *dt1,
						 const EContactDateTime *dt2);
gboolean	e_contact_date_time_has_date	(const EContactDateTime *self);
gboolean	e_contact_date_time_has_time	(const EContactDateTime *self);
void		e_contact_date_time_free	(EContactDateTime *self);

GType		e_contact_name_get_type		(void);
EContactName *	e_contact_name_new		(void);
gchar *		e_contact_name_to_string	(const EContactName *name);
EContactName *	e_contact_name_from_string	(const gchar *name_str);
EContactName *	e_contact_name_copy		(EContactName *n);
void		e_contact_name_free		(EContactName *name);

GType		e_contact_photo_get_type	(void);
EContactPhoto *	e_contact_photo_new		(void);
void		e_contact_photo_free		(EContactPhoto *photo);
EContactPhoto *	e_contact_photo_copy		(EContactPhoto *photo);
const guchar *	e_contact_photo_get_inlined	(EContactPhoto *photo,
						 gsize *len);
void		e_contact_photo_set_inlined	(EContactPhoto *photo,
						 const guchar *data,
						 gsize len);
const gchar *	e_contact_photo_get_mime_type	(EContactPhoto *photo);
void		e_contact_photo_set_mime_type	(EContactPhoto *photo,
						 const gchar *mime_type);
const gchar *	e_contact_photo_get_uri		(EContactPhoto *photo);
void		e_contact_photo_set_uri		(EContactPhoto *photo,
						 const gchar *uri);
gboolean	e_contact_inline_local_photos	(EContact *contact,
						 GError **error);

EContactGeo *	e_contact_geo_new		(void);
GType		e_contact_geo_get_type		(void);
void		e_contact_geo_free		(EContactGeo *geo);

EContactCert *	e_contact_cert_new		(void);
GType		e_contact_cert_get_type		(void);
void		e_contact_cert_free		(EContactCert *cert);

GType		e_contact_address_get_type	(void);
EContactAddress *
		e_contact_address_new		(void);
void		e_contact_address_free		(EContactAddress *address);

GType		e_contact_attr_list_get_type	(void);
GList *		e_contact_attr_list_copy	(GList *list);
void		e_contact_attr_list_free	(GList *list);

typedef enum _EContactGenderSex {
	E_CONTACT_GENDER_SEX_UNKNOWN,
	E_CONTACT_GENDER_SEX_NOT_SET,
	E_CONTACT_GENDER_SEX_MALE,
	E_CONTACT_GENDER_SEX_FEMALE,
	E_CONTACT_GENDER_SEX_OTHER,
	E_CONTACT_GENDER_SEX_NOT_APPLICABLE
} EContactGenderSex;

const gchar *	e_contact_gender_sex_to_string	(EContactGenderSex sex);
EContactGenderSex
		e_contact_gender_sex_from_string(const gchar *string);

typedef struct _EContactGender {
	EContactGenderSex sex;
	gchar *identity;
} EContactGender;

GType		e_contact_gender_get_type	(void);
EContactGender *e_contact_gender_new		(void);
EContactGender *e_contact_gender_copy		(EContactGender *self);
void		e_contact_gender_free		(EContactGender *self);

GType		e_contact_field_type		(EContactField field_id);
const gchar *	e_contact_field_name		(EContactField field_id);
const gchar *	e_contact_pretty_name		(EContactField field_id);
const gchar *	e_contact_vcard_attribute	(EContactField field_id);
const gchar *	e_contact_vcard_attribute_fallback
						(EContactField field_id);
gboolean	e_contact_field_is_string	(EContactField field_id);
EContactField	e_contact_field_id		(const gchar *field_name);
EContactField	e_contact_field_id_from_vcard	(const gchar *vcard_field);
EContactField	e_contact_impp_scheme_to_field	(const gchar *scheme,
						 guint *out_scheme_len);
const gchar *	e_contact_impp_field_to_scheme	(EContactField field);

G_END_DECLS

#endif /* E_CONTACT_H */

