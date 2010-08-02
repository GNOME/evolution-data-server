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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-cipher-context.h"
#include "camel-debug.h"
#include "camel-session.h"
#include "camel-stream.h"
#include "camel-operation.h"

#include "camel-mime-utils.h"
#include "camel-medium.h"
#include "camel-multipart.h"
#include "camel-mime-message.h"
#include "camel-mime-filter-canon.h"
#include "camel-stream-filter.h"

#define CIPHER_LOCK(ctx)   g_mutex_lock (((CamelCipherContext *) ctx)->priv->lock)
#define CIPHER_UNLOCK(ctx) g_mutex_unlock (((CamelCipherContext *) ctx)->priv->lock);

#define d(x)

#define CAMEL_CIPHER_CONTEXT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_CIPHER_CONTEXT, CamelCipherContextPrivate))

struct _CamelCipherContextPrivate {
	CamelSession *session;
	GMutex *lock;
};

enum {
	PROP_0,
	PROP_SESSION
};

G_DEFINE_TYPE (CamelCipherContext, camel_cipher_context, CAMEL_TYPE_OBJECT)

static gint
cipher_sign (CamelCipherContext *ctx,
             const gchar *userid,
             CamelCipherHash hash,
             CamelMimePart *ipart,
             CamelMimePart *opart,
             GError **error)
{
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Signing is not supported by this cipher"));

	return -1;
}

/**
 * camel_cipher_sign:
 * @context: Cipher Context
 * @userid: private key to use to sign the stream
 * @hash: preferred Message-Integrity-Check hash algorithm
 * @ipart: Input part.
 * @opart: output part.
 * @error: return location for a #GError, or %NULL
 *
 * Converts the (unsigned) part @ipart into a new self-contained mime part @opart.
 * This may be a multipart/signed part, or a simple part for enveloped types.
 *
 * Returns: 0 for success or -1 for failure.
 **/
gint
camel_cipher_sign (CamelCipherContext *context,
                   const gchar *userid,
                   CamelCipherHash hash,
                   CamelMimePart *ipart,
                   CamelMimePart *opart,
                   GError **error)
{
	CamelCipherContextClass *class;
	gint retval;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), -1);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->sign != NULL, -1);

	camel_operation_start (NULL, _("Signing message"));

	CIPHER_LOCK (context);

	retval = class->sign (context, userid, hash, ipart, opart, error);
	CAMEL_CHECK_GERROR (context, sign, retval == 0, error);

	CIPHER_UNLOCK (context);

	camel_operation_end (NULL);

	return retval;
}

static CamelCipherValidity *
cipher_verify (CamelCipherContext *context,
               CamelMimePart *sigpart,
               GError **error)
{
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Verifying is not supported by this cipher"));

	return NULL;
}

/**
 * camel_cipher_verify:
 * @context: Cipher Context
 * @ipart: part to verify
 * @error: return location for a #GError, or %NULL
 *
 * Verifies the signature. If @istream is a clearsigned stream,
 * you should pass %NULL as the sigstream parameter. Otherwise
 * @sigstream is assumed to be the signature stream and is used to
 * verify the integirity of the @istream.
 *
 * Returns: a CamelCipherValidity structure containing information
 * about the integrity of the input stream or %NULL on failure to
 * execute at all.
 **/
CamelCipherValidity *
camel_cipher_verify (CamelCipherContext *context,
                     CamelMimePart *ipart,
                     GError **error)
{
	CamelCipherContextClass *class;
	CamelCipherValidity *valid;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), NULL);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->verify != NULL, NULL);

	camel_operation_start (NULL, _("Verifying message"));

	CIPHER_LOCK (context);

	valid = class->verify (context, ipart, error);
	CAMEL_CHECK_GERROR (context, verify, valid != NULL, error);

	CIPHER_UNLOCK (context);

	camel_operation_end (NULL);

	return valid;
}

static gint
cipher_encrypt (CamelCipherContext *context,
                const gchar *userid,
                GPtrArray *recipients,
                CamelMimePart *ipart,
                CamelMimePart *opart,
                GError **error)
{
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Encryption is not supported by this cipher"));

	return -1;
}

/**
 * camel_cipher_encrypt:
 * @context: Cipher Context
 * @userid: key id (or email address) to use when signing, or NULL to not sign.
 * @recipients: an array of recipient key ids and/or email addresses
 * @ipart: cleartext input stream
 * @opart: ciphertext output stream
 * @error: return location for a #GError, or %NULL
 *
 * Encrypts (and optionally signs) the cleartext input stream and
 * writes the resulting ciphertext to the output stream.
 *
 * Returns: 0 for success or -1 for failure.
 **/
gint
camel_cipher_encrypt (CamelCipherContext *context,
                      const gchar *userid,
                      GPtrArray *recipients,
                      CamelMimePart *ipart,
                      CamelMimePart *opart,
                      GError **error)
{
	CamelCipherContextClass *class;
	gint retval;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), -1);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->encrypt != NULL, -1);

	camel_operation_start (NULL, _("Encrypting message"));

	CIPHER_LOCK (context);

	retval = class->encrypt (
		context, userid, recipients, ipart, opart, error);
	CAMEL_CHECK_GERROR (context, encrypt, retval == 0, error);

	CIPHER_UNLOCK (context);

	camel_operation_end (NULL);

	return retval;
}

static CamelCipherValidity *
cipher_decrypt (CamelCipherContext *context,
                CamelMimePart *ipart,
                CamelMimePart *opart,
                GError **error)
{
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("Decryption is not supported by this cipher"));

	return NULL;
}

/**
 * camel_cipher_decrypt:
 * @context:
 * @ipart:
 * @opart:
 * @error: return location for a #GError, or %NULL
 *
 * Decrypts @ipart into @opart.
 *
 * Returns: A validity/encryption status.
 **/
CamelCipherValidity *
camel_cipher_decrypt (CamelCipherContext *context,
                      CamelMimePart *ipart,
                      CamelMimePart *opart,
                      GError **error)
{
	CamelCipherContextClass *class;
	CamelCipherValidity *valid;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), NULL);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->decrypt != NULL, NULL);

	camel_operation_start (NULL, _("Decrypting message"));

	CIPHER_LOCK (context);

	valid = class->decrypt (context, ipart, opart, error);
	CAMEL_CHECK_GERROR (context, decrypt, valid != NULL, error);

	CIPHER_UNLOCK (context);

	camel_operation_end (NULL);

	return valid;
}

static gint
cipher_import_keys (CamelCipherContext *context,
                    CamelStream *istream,
                    GError **error)
{
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("You may not import keys with this cipher"));

	return -1;
}

/**
 * camel_cipher_import_keys:
 * @context: Cipher Context
 * @istream: input stream (containing keys)
 * @error: return location for a #GError, or %NULL
 *
 * Imports a stream of keys/certificates contained within @istream
 * into the key/certificate database controlled by @ctx.
 *
 * Returns: 0 on success or -1 on fail.
 **/
gint
camel_cipher_import_keys (CamelCipherContext *context,
                          CamelStream *istream,
                          GError **error)
{
	CamelCipherContextClass *class;
	gint retval;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (istream), -1);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->import_keys != NULL, -1);

	retval = class->import_keys (context, istream, error);
	CAMEL_CHECK_GERROR (context, import_keys, retval == 0, error);

	return retval;
}

static gint
cipher_export_keys (CamelCipherContext *context,
                    GPtrArray *keys,
                    CamelStream *ostream,
                    GError **error)
{
	g_set_error (
		error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
		_("You may not export keys with this cipher"));

	return -1;
}

/**
 * camel_cipher_export_keys:
 * @context: Cipher Context
 * @keys: an array of key ids
 * @ostream: output stream
 * @error: return location for a #GError, or %NULL
 *
 * Exports the keys/certificates in @keys to the stream @ostream from
 * the key/certificate database controlled by @ctx.
 *
 * Returns: 0 on success or -1 on fail.
 **/
gint
camel_cipher_export_keys (CamelCipherContext *context,
                          GPtrArray *keys,
                          CamelStream *ostream,
                          GError **error)
{
	CamelCipherContextClass *class;
	gint retval;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (ostream), -1);
	g_return_val_if_fail (keys != NULL, -1);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->export_keys != NULL, -1);

	retval = class->export_keys (context, keys, ostream, error);
	CAMEL_CHECK_GERROR (context, export_keys, retval == 0, error);

	return retval;
}

static CamelCipherHash
cipher_id_to_hash (CamelCipherContext *context, const gchar *id)
{
	return CAMEL_CIPHER_HASH_DEFAULT;
}

/* a couple of util functions */
CamelCipherHash
camel_cipher_id_to_hash (CamelCipherContext *context,
                         const gchar *id)
{
	CamelCipherContextClass *class;

	g_return_val_if_fail (
		CAMEL_IS_CIPHER_CONTEXT (context),
		CAMEL_CIPHER_HASH_DEFAULT);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (
		class->id_to_hash != NULL, CAMEL_CIPHER_HASH_DEFAULT);

	return class->id_to_hash (context, id);
}

static const gchar *
cipher_hash_to_id (CamelCipherContext *context, CamelCipherHash hash)
{
	return NULL;
}

const gchar *
camel_cipher_hash_to_id (CamelCipherContext *context,
                         CamelCipherHash hash)
{
	CamelCipherContextClass *class;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), NULL);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->hash_to_id != NULL, NULL);

	return class->hash_to_id (context, hash);
}

/* Cipher Validity stuff */
static void
ccv_certinfo_free (CamelCipherCertInfo *info)
{
	g_free (info->name);
	g_free (info->email);

	if (info->cert_data && info->cert_data_free)
		info->cert_data_free (info->cert_data);

	g_free (info);
}

CamelCipherValidity *
camel_cipher_validity_new (void)
{
	CamelCipherValidity *validity;

	validity = g_malloc (sizeof (*validity));
	camel_cipher_validity_init (validity);

	return validity;
}

void
camel_cipher_validity_init (CamelCipherValidity *validity)
{
	g_assert (validity != NULL);

	memset (validity, 0, sizeof (*validity));
	camel_dlist_init (&validity->children);
	camel_dlist_init (&validity->sign.signers);
	camel_dlist_init (&validity->encrypt.encrypters);
}

gboolean
camel_cipher_validity_get_valid (CamelCipherValidity *validity)
{
	return validity != NULL
		&& validity->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_GOOD;
}

void
camel_cipher_validity_set_valid (CamelCipherValidity *validity, gboolean valid)
{
	g_assert (validity != NULL);

	validity->sign.status = valid?CAMEL_CIPHER_VALIDITY_SIGN_GOOD:CAMEL_CIPHER_VALIDITY_SIGN_BAD;
}

gchar *
camel_cipher_validity_get_description (CamelCipherValidity *validity)
{
	if (validity == NULL)
		return NULL;

	return validity->sign.description;
}

void
camel_cipher_validity_set_description (CamelCipherValidity *validity, const gchar *description)
{
	g_assert (validity != NULL);

	g_free (validity->sign.description);
	validity->sign.description = g_strdup (description);
}

void
camel_cipher_validity_clear (CamelCipherValidity *validity)
{
	g_assert (validity != NULL);

	/* TODO: this doesn't free children/clear key lists */
	g_free (validity->sign.description);
	g_free (validity->encrypt.description);
	camel_cipher_validity_init (validity);
}

CamelCipherValidity *
camel_cipher_validity_clone (CamelCipherValidity *vin)
{
	CamelCipherValidity *vo;
	CamelCipherCertInfo *info;

	vo = camel_cipher_validity_new ();
	vo->sign.status = vin->sign.status;
	vo->sign.description = g_strdup (vin->sign.description);
	vo->encrypt.status = vin->encrypt.status;
	vo->encrypt.description = g_strdup (vin->encrypt.description);

	info = (CamelCipherCertInfo *)vin->sign.signers.head;
	while (info->next) {
		if (info->cert_data && info->cert_data_clone && info->cert_data_free)
			camel_cipher_validity_add_certinfo_ex (vo, CAMEL_CIPHER_VALIDITY_SIGN, info->name, info->email, info->cert_data_clone (info->cert_data), info->cert_data_free, info->cert_data_clone);
		else
			camel_cipher_validity_add_certinfo (vo, CAMEL_CIPHER_VALIDITY_SIGN, info->name, info->email);
		info = info->next;
	}

	info = (CamelCipherCertInfo *)vin->encrypt.encrypters.head;
	while (info->next) {
		if (info->cert_data && info->cert_data_clone && info->cert_data_free)
			camel_cipher_validity_add_certinfo_ex (vo, CAMEL_CIPHER_VALIDITY_SIGN, info->name, info->email, info->cert_data_clone (info->cert_data), info->cert_data_free, info->cert_data_clone);
		else
			camel_cipher_validity_add_certinfo (vo, CAMEL_CIPHER_VALIDITY_ENCRYPT, info->name, info->email);
		info = info->next;
	}

	return vo;
}

/**
 * camel_cipher_validity_add_certinfo:
 * @vin:
 * @mode:
 * @name:
 * @email:
 *
 * Add a cert info to the signer or encrypter info.
 **/
void
camel_cipher_validity_add_certinfo (CamelCipherValidity *vin, enum _camel_cipher_validity_mode_t mode, const gchar *name, const gchar *email)
{
	camel_cipher_validity_add_certinfo_ex (vin, mode, name, email, NULL, NULL, NULL);
}

/**
 * camel_cipher_validity_add_certinfo_ex:
 *
 * Add a cert info to the signer or encrypter info, with extended data set.
 *
 * Since: 2.30
 **/
void
camel_cipher_validity_add_certinfo_ex (CamelCipherValidity *vin, camel_cipher_validity_mode_t mode, const gchar *name, const gchar *email, gpointer cert_data, void (*cert_data_free)(gpointer cert_data), gpointer (*cert_data_clone)(gpointer cert_data))
{
	CamelCipherCertInfo *info;
	CamelDList *list;

	info = g_malloc0 (sizeof (*info));
	info->name = g_strdup (name);
	info->email = g_strdup (email);
	if (cert_data) {
		if (cert_data_free && cert_data_clone) {
			info->cert_data = cert_data;
			info->cert_data_free = cert_data_free;
			info->cert_data_clone = cert_data_clone;
		} else {
			if (!cert_data_free)
				g_warning ("%s: requires non-NULL cert_data_free function!", G_STRFUNC);
			if (!cert_data_clone)
				g_warning ("%s: requires non-NULL cert_data_clone function!", G_STRFUNC);
		}
	}

	list = (mode==CAMEL_CIPHER_VALIDITY_SIGN)?&vin->sign.signers:&vin->encrypt.encrypters;
	camel_dlist_addtail (list, (CamelDListNode *)info);
}

/**
 * camel_cipher_validity_envelope:
 * @parent:
 * @valid:
 *
 * Calculate a conglomerate validity based on wrapping one secure part inside
 * another one.
 **/
void
camel_cipher_validity_envelope (CamelCipherValidity *parent, CamelCipherValidity *valid)
{
	CamelCipherCertInfo *info;

	if (parent->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE
	    && parent->encrypt.status == CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE
	    && valid->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_NONE
	    && valid->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE) {
		/* case 1: only signed inside only encrypted -> merge both */
		parent->encrypt.status = valid->encrypt.status;
		parent->encrypt.description = g_strdup (valid->encrypt.description);
		info = (CamelCipherCertInfo *)valid->encrypt.encrypters.head;
		while (info->next) {
			camel_cipher_validity_add_certinfo (parent, CAMEL_CIPHER_VALIDITY_ENCRYPT, info->name, info->email);
			info = info->next;
		}
	} else if (parent->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_NONE
		   && parent->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE
		   && valid->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE
		   && valid->encrypt.status == CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE) {
		/* case 2: only encrypted inside only signed */
		parent->sign.status = valid->sign.status;
		parent->sign.description = g_strdup (valid->sign.description);
		info = (CamelCipherCertInfo *)valid->sign.signers.head;
		while (info->next) {
			camel_cipher_validity_add_certinfo (parent, CAMEL_CIPHER_VALIDITY_SIGN, info->name, info->email);
			info = info->next;
		}
	}
	/* Otherwise, I dunno - what do you do? */
}

void
camel_cipher_validity_free (CamelCipherValidity *validity)
{
	CamelCipherValidity *child;
	CamelCipherCertInfo *info;

	if (validity == NULL)
		return;

	while ((child = (CamelCipherValidity *)camel_dlist_remhead (&validity->children)))
		camel_cipher_validity_free (child);

	while ((info = (CamelCipherCertInfo *)camel_dlist_remhead (&validity->sign.signers)))
		ccv_certinfo_free (info);

	while ((info = (CamelCipherCertInfo *)camel_dlist_remhead (&validity->encrypt.encrypters)))
		ccv_certinfo_free (info);

	camel_cipher_validity_clear (validity);
	g_free (validity);
}

/* ********************************************************************** */

static void
cipher_context_set_session (CamelCipherContext *context,
                            CamelSession *session)
{
	g_return_if_fail (CAMEL_IS_SESSION (session));
	g_return_if_fail (context->priv->session == NULL);

	context->priv->session = g_object_ref (session);
}

static void
cipher_context_set_property (GObject *object,
                             guint property_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SESSION:
			cipher_context_set_session (
				CAMEL_CIPHER_CONTEXT (object),
				g_value_get_object (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cipher_context_get_property (GObject *object,
                             guint property_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_SESSION:
			g_value_set_object (
				value, camel_cipher_context_get_session (
				CAMEL_CIPHER_CONTEXT (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
cipher_context_dispose (GObject *object)
{
	CamelCipherContextPrivate *priv;

	priv = CAMEL_CIPHER_CONTEXT_GET_PRIVATE (object);

	if (priv->session != NULL) {
		g_object_unref (priv->session);
		priv->session = NULL;
	}

	/* Chain up to parent's dispose () method. */
	G_OBJECT_CLASS (camel_cipher_context_parent_class)->dispose (object);
}

static void
cipher_context_finalize (GObject *object)
{
	CamelCipherContextPrivate *priv;

	priv = CAMEL_CIPHER_CONTEXT_GET_PRIVATE (object);

	g_mutex_free (priv->lock);

	/* Chain up to parent's finalize () method. */
	G_OBJECT_CLASS (camel_cipher_context_parent_class)->finalize (object);
}

static void
camel_cipher_context_class_init (CamelCipherContextClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelCipherContextPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = cipher_context_set_property;
	object_class->get_property = cipher_context_get_property;
	object_class->dispose = cipher_context_dispose;
	object_class->finalize = cipher_context_finalize;

	class->hash_to_id = cipher_hash_to_id;
	class->id_to_hash = cipher_id_to_hash;
	class->sign = cipher_sign;
	class->verify = cipher_verify;
	class->encrypt = cipher_encrypt;
	class->decrypt = cipher_decrypt;
	class->import_keys = cipher_import_keys;
	class->export_keys = cipher_export_keys;

	g_object_class_install_property (
		object_class,
		PROP_SESSION,
		g_param_spec_object (
			"session",
			"Session",
			NULL,
			CAMEL_TYPE_SESSION,
			G_PARAM_READWRITE |
			G_PARAM_CONSTRUCT_ONLY));
}

static void
camel_cipher_context_init (CamelCipherContext *context)
{
	context->priv = CAMEL_CIPHER_CONTEXT_GET_PRIVATE (context);
	context->priv->lock = g_mutex_new ();
}

/**
 * camel_cipher_context_new:
 * @session: a #CamelSession
 *
 * This creates a new CamelCipherContext object which is used to sign,
 * verify, encrypt and decrypt streams.
 *
 * Returns: the new CamelCipherContext
 **/
CamelCipherContext *
camel_cipher_context_new (CamelSession *session)
{
	g_return_val_if_fail (session != NULL, NULL);

	return g_object_new (
		CAMEL_TYPE_CIPHER_CONTEXT,
		"session", session, NULL);
}

/**
 * camel_cipher_context_get_session:
 * @context: a #CamelCipherContext
 *
 * Since: 2.32
 **/
CamelSession *
camel_cipher_context_get_session (CamelCipherContext *context)
{
	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), NULL);

	return context->priv->session;
}

/* See rfc3156, section 2 and others */
/* We do this simply: Anything not base64 must be qp
   This is so that we can safely translate any occurance of "From "
   into the quoted-printable escaped version safely. */
static void
cc_prepare_sign (CamelMimePart *part)
{
	CamelDataWrapper *dw;
	CamelTransferEncoding encoding;
	gint parts, i;

	dw = camel_medium_get_content ((CamelMedium *)part);
	if (!dw)
		return;

	if (CAMEL_IS_MULTIPART (dw)) {
		parts = camel_multipart_get_number ((CamelMultipart *)dw);
		for (i = 0; i < parts; i++)
			cc_prepare_sign (camel_multipart_get_part ((CamelMultipart *)dw, i));
	} else if (CAMEL_IS_MIME_MESSAGE (dw)) {
		cc_prepare_sign ((CamelMimePart *)dw);
	} else {
		encoding = camel_mime_part_get_encoding (part);

		if (encoding != CAMEL_TRANSFER_ENCODING_BASE64
		    && encoding != CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE) {
			camel_mime_part_set_encoding (part, CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE);
		}
	}
}

/**
 * camel_cipher_canonical_to_stream:
 * @part: Part to write.
 * @flags: flags for the canonicalisation filter (CamelMimeFilterCanon)
 * @ostream: stream to write canonicalised output to.
 * @error: return location for a #GError, or %NULL
 *
 * Writes a part to a stream in a canonicalised format, suitable for signing/encrypting.
 *
 * The transfer encoding paramaters for the part may be changed by this function.
 *
 * Returns: -1 on error;
 **/
gint
camel_cipher_canonical_to_stream (CamelMimePart *part,
                                  guint32 flags,
                                  CamelStream *ostream,
                                  GError **error)
{
	CamelStream *filter;
	CamelMimeFilter *canon;
	gint res = -1;

	if (flags & (CAMEL_MIME_FILTER_CANON_FROM|CAMEL_MIME_FILTER_CANON_STRIP))
		cc_prepare_sign (part);

	filter = camel_stream_filter_new (ostream);
	canon = camel_mime_filter_canon_new (flags);
	camel_stream_filter_add (CAMEL_STREAM_FILTER (filter), canon);
	g_object_unref (canon);

	if (camel_data_wrapper_write_to_stream (
		(CamelDataWrapper *)part, filter, error) != -1
	    && camel_stream_flush (filter, error) != -1)
		res = 0;

	g_object_unref (filter);
	camel_stream_reset (ostream, NULL);

	return res;
}
