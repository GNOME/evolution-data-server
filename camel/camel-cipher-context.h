/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_CIPHER_CONTEXT_H
#define CAMEL_CIPHER_CONTEXT_H

#include <camel/camel-list-utils.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-session.h>

/* Standard GObject macros */
#define CAMEL_TYPE_CIPHER_CONTEXT \
	(camel_cipher_context_get_type ())
#define CAMEL_CIPHER_CONTEXT(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_CIPHER_CONTEXT, CamelCipherContext))
#define CAMEL_CIPHER_CONTEXT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_CIPHER_CONTEXT, CamelCipherContextClass))
#define CAMEL_IS_CIPHER_CONTEXT(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_CIPHER_CONTEXT))
#define CAMEL_IS_CIPHER_CONTEXT_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_CIPHER_CONTEXT))
#define CAMEL_CIPHER_CONTEXT_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_CIPHER_CONTEXT, CamelCipherContextClass))

G_BEGIN_DECLS

typedef struct _CamelCipherValidity CamelCipherValidity;
typedef struct _CamelCipherCertInfo CamelCipherCertInfo;

typedef struct _CamelCipherContext CamelCipherContext;
typedef struct _CamelCipherContextClass CamelCipherContextClass;
typedef struct _CamelCipherContextPrivate CamelCipherContextPrivate;

typedef enum {
	CAMEL_CIPHER_HASH_DEFAULT,
	CAMEL_CIPHER_HASH_MD2,
	CAMEL_CIPHER_HASH_MD5,
	CAMEL_CIPHER_HASH_SHA1,
	CAMEL_CIPHER_HASH_SHA256,
	CAMEL_CIPHER_HASH_SHA384,
	CAMEL_CIPHER_HASH_SHA512,
	CAMEL_CIPHER_HASH_RIPEMD160,
	CAMEL_CIPHER_HASH_TIGER192,
	CAMEL_CIPHER_HASH_HAVAL5160
} CamelCipherHash;

typedef enum _camel_cipher_validity_sign_t {
	CAMEL_CIPHER_VALIDITY_SIGN_NONE,
	CAMEL_CIPHER_VALIDITY_SIGN_GOOD,
	CAMEL_CIPHER_VALIDITY_SIGN_BAD,
	CAMEL_CIPHER_VALIDITY_SIGN_UNKNOWN,
	CAMEL_CIPHER_VALIDITY_SIGN_NEED_PUBLIC_KEY
} camel_cipher_validity_sign_t;

typedef enum _camel_cipher_validity_encrypt_t {
	CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE,
	CAMEL_CIPHER_VALIDITY_ENCRYPT_WEAK,
	CAMEL_CIPHER_VALIDITY_ENCRYPT_ENCRYPTED, /* encrypted, unknown strenght */
	CAMEL_CIPHER_VALIDITY_ENCRYPT_STRONG
} camel_cipher_validity_encrypt_t;

typedef enum _camel_cipher_validity_mode_t {
	CAMEL_CIPHER_VALIDITY_SIGN,
	CAMEL_CIPHER_VALIDITY_ENCRYPT
} camel_cipher_validity_mode_t;

struct _CamelCipherCertInfo {
	struct _CamelCipherCertInfo *next;
	struct _CamelCipherCertInfo *prev;

	gchar *name;		/* common name */
	gchar *email;

	gpointer cert_data;  /* custom certificate data; can be NULL */
	void (*cert_data_free) (gpointer cert_data); /* called to free cert_data; can be NULL only if cert_data is NULL */
	gpointer (*cert_data_clone) (gpointer cert_data); /* called to clone cert_data; can be NULL only if cert_data is NULL */
};

struct _CamelCipherValidity {
	struct _CamelCipherValidity *next;
	struct _CamelCipherValidity *prev;
	CamelDList children;

	struct {
		enum _camel_cipher_validity_sign_t status;
		gchar *description;
		CamelDList signers;	/* CamelCipherCertInfo's */
	} sign;
	struct {
		enum _camel_cipher_validity_encrypt_t status;
		gchar *description;
		CamelDList encrypters;	/* CamelCipherCertInfo's */
	} encrypt;
};

struct _CamelCipherContext {
	CamelObject parent;
	CamelCipherContextPrivate *priv;
};

struct _CamelCipherContextClass {
	CamelObjectClass parent_class;

	/* these MUST be set by implementors */
	const gchar *sign_protocol;
	const gchar *encrypt_protocol;
	const gchar *key_protocol;

	CamelCipherHash	(*id_to_hash)		(CamelCipherContext *context,
						 const gchar *id);
	const gchar *	(*hash_to_id)		(CamelCipherContext *context,
						 CamelCipherHash hash);
	gint		(*sign)			(CamelCipherContext *context,
						 const gchar *userid,
						 CamelCipherHash hash,
						 CamelMimePart *ipart,
						 CamelMimePart *opart,
						 GError **error);
	CamelCipherValidity *
			(*verify)		(CamelCipherContext *context,
						 CamelMimePart *ipart,
						 GError **error);
	gint		(*encrypt)		(CamelCipherContext *context,
						 const gchar *userid,
						 GPtrArray *recipients,
						 CamelMimePart *ipart,
						 CamelMimePart *opart,
						 GError **error);
	CamelCipherValidity *
			(*decrypt)		(CamelCipherContext *context,
						 CamelMimePart *ipart,
						 CamelMimePart *opart,
						 GError **error);
	gint		(*import_keys)		(CamelCipherContext *context,
						 CamelStream *istream,
						 GError **error);
	gint		(*export_keys)		(CamelCipherContext *context,
						 GPtrArray *keys,
						 CamelStream *ostream,
						 GError **error);
};

GType		camel_cipher_context_get_type	(void);
CamelCipherContext *
		camel_cipher_context_new	(CamelSession *session);
CamelSession *	camel_cipher_context_get_session(CamelCipherContext *context);

/* cipher context util routines */
CamelCipherHash	     camel_cipher_id_to_hash (CamelCipherContext *context, const gchar *id);
const gchar *	     camel_cipher_hash_to_id (CamelCipherContext *context, CamelCipherHash hash);

/* FIXME:
   There are some inconsistencies here, the api's should probably handle CamelMimePart's as input/outputs,
   Something that might generate a multipart/signed should do it as part of that processing, internally
   to the cipher, etc etc. */

/* cipher routines */
gint                  camel_cipher_sign (CamelCipherContext *context, const gchar *userid, CamelCipherHash hash,
					CamelMimePart *ipart, CamelMimePart *opart, GError **error);
CamelCipherValidity *camel_cipher_verify (CamelCipherContext *context, CamelMimePart *ipart, GError **error);
gint                  camel_cipher_encrypt (CamelCipherContext *context, const gchar *userid,
					   GPtrArray *recipients, CamelMimePart *ipart, CamelMimePart *opart,
					   GError **error);
CamelCipherValidity *camel_cipher_decrypt (CamelCipherContext *context, CamelMimePart *ipart, CamelMimePart *opart,
					   GError **error);

/* key/certificate routines */
gint                  camel_cipher_import_keys (CamelCipherContext *context, CamelStream *istream,
					       GError **error);
gint                  camel_cipher_export_keys (CamelCipherContext *context, GPtrArray *keys,
					       CamelStream *ostream, GError **error);

/* CamelCipherValidity utility functions */
CamelCipherValidity *camel_cipher_validity_new (void);
void                 camel_cipher_validity_init (CamelCipherValidity *validity);
gboolean             camel_cipher_validity_get_valid (CamelCipherValidity *validity);
void                 camel_cipher_validity_set_valid (CamelCipherValidity *validity, gboolean valid);
gchar                *camel_cipher_validity_get_description (CamelCipherValidity *validity);
void                 camel_cipher_validity_set_description (CamelCipherValidity *validity, const gchar *description);
void                 camel_cipher_validity_clear (CamelCipherValidity *validity);
CamelCipherValidity *camel_cipher_validity_clone(CamelCipherValidity *vin);
void		     camel_cipher_validity_add_certinfo(CamelCipherValidity *vin, camel_cipher_validity_mode_t mode, const gchar *name, const gchar *email);
void		     camel_cipher_validity_add_certinfo_ex (
					CamelCipherValidity *vin,
					camel_cipher_validity_mode_t mode,
					const gchar *name,
					const gchar *email,
					gpointer cert_data,
					void (*cert_data_free) (gpointer cert_data),
					gpointer (*cert_data_clone) (gpointer cert_data));
void		     camel_cipher_validity_envelope(CamelCipherValidity *parent, CamelCipherValidity *valid);
void                 camel_cipher_validity_free (CamelCipherValidity *validity);

/* utility functions */
gint		     camel_cipher_canonical_to_stream(CamelMimePart *part, guint32 flags, CamelStream *ostream, GError **error);

G_END_DECLS

#endif /* CAMEL_CIPHER_CONTEXT_H */
