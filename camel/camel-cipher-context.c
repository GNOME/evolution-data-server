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

#define CAMEL_CIPHER_CONTEXT_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_CIPHER_CONTEXT, CamelCipherContextPrivate))

#define CIPHER_LOCK(ctx) \
	g_mutex_lock (((CamelCipherContext *) ctx)->priv->lock)
#define CIPHER_UNLOCK(ctx) \
	g_mutex_unlock (((CamelCipherContext *) ctx)->priv->lock);

#define d(x)

typedef struct _AsyncContext AsyncContext;

struct _CamelCipherContextPrivate {
	CamelSession *session;
	GMutex *lock;
};

struct _AsyncContext {
	/* arguments */
	CamelCipherHash hash;
	CamelMimePart *ipart;
	CamelMimePart *opart;
	CamelStream *stream;
	GPtrArray *strings;
	gchar *userid;

	/* results */
	CamelCipherValidity *validity;
};

enum {
	PROP_0,
	PROP_SESSION
};

G_DEFINE_TYPE (CamelCipherContext, camel_cipher_context, CAMEL_TYPE_OBJECT)

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->ipart != NULL)
		g_object_unref (async_context->ipart);

	if (async_context->opart != NULL)
		g_object_unref (async_context->opart);

	if (async_context->stream != NULL)
		g_object_unref (async_context->stream);

	if (async_context->strings != NULL) {
		g_ptr_array_foreach (
			async_context->strings, (GFunc) g_free, NULL);
		g_ptr_array_free (async_context->strings, TRUE);
	}

	if (async_context->validity != NULL)
		camel_cipher_validity_free (async_context->validity);

	g_free (async_context->userid);

	g_slice_free (AsyncContext, async_context);
}

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

static const gchar *
cipher_context_hash_to_id (CamelCipherContext *context,
                           CamelCipherHash hash)
{
	return NULL;
}

static CamelCipherHash
cipher_context_id_to_hash (CamelCipherContext *context,
                           const gchar *id)
{
	return CAMEL_CIPHER_HASH_DEFAULT;
}

static gboolean
cipher_context_sign_sync (CamelCipherContext *ctx,
                          const gchar *userid,
                          CamelCipherHash hash,
                          CamelMimePart *ipart,
                          CamelMimePart *opart,
                          GCancellable *cancellable,
                          GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Signing is not supported by this cipher"));

	return FALSE;
}

static CamelCipherValidity *
cipher_context_verify_sync (CamelCipherContext *context,
                            CamelMimePart *sigpart,
                            GCancellable *cancellable,
                            GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Verifying is not supported by this cipher"));

	return NULL;
}

static gboolean
cipher_context_encrypt_sync (CamelCipherContext *context,
                             const gchar *userid,
                             GPtrArray *recipients,
                             CamelMimePart *ipart,
                             CamelMimePart *opart,
                             GCancellable *cancellable,
                             GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Encryption is not supported by this cipher"));

	return FALSE;
}

static CamelCipherValidity *
cipher_context_decrypt_sync (CamelCipherContext *context,
                             CamelMimePart *ipart,
                             CamelMimePart *opart,
                             GCancellable *cancellable,
                             GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("Decryption is not supported by this cipher"));

	return NULL;
}

static gboolean
cipher_context_import_keys_sync (CamelCipherContext *context,
                                 CamelStream *istream,
                                 GCancellable *cancellable,
                                 GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("You may not import keys with this cipher"));

	return FALSE;
}

static gint
cipher_context_export_keys_sync (CamelCipherContext *context,
                                 GPtrArray *keys,
                                 CamelStream *ostream,
                                 GCancellable *cancellable,
                                 GError **error)
{
	g_set_error (
		error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
		_("You may not export keys with this cipher"));

	return FALSE;
}

static void
cipher_context_sign_thread (GSimpleAsyncResult *simple,
                            GObject *object,
                            GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_cipher_context_sign_sync (
		CAMEL_CIPHER_CONTEXT (object),
		async_context->userid, async_context->hash,
		async_context->ipart, async_context->opart,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
cipher_context_sign (CamelCipherContext *context,
                     const gchar *userid,
                     CamelCipherHash hash,
                     CamelMimePart *ipart,
                     CamelMimePart *opart,
                     gint io_priority,
                     GCancellable *cancellable,
                     GAsyncReadyCallback callback,
                     gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->userid = g_strdup (userid);
	async_context->hash = hash;
	async_context->ipart = g_object_ref (ipart);
	async_context->opart = g_object_ref (opart);

	simple = g_simple_async_result_new (
		G_OBJECT (context), callback, user_data, cipher_context_sign);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, cipher_context_sign_thread, io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
cipher_context_sign_finish (CamelCipherContext *context,
                            GAsyncResult *result,
                            GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (context), cipher_context_sign), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
cipher_context_verify_thread (GSimpleAsyncResult *simple,
                              GObject *object,
                              GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->validity = camel_cipher_context_verify_sync (
		CAMEL_CIPHER_CONTEXT (object), async_context->ipart,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
cipher_context_verify (CamelCipherContext *context,
                       CamelMimePart *ipart,
                       gint io_priority,
                       GCancellable *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->ipart = g_object_ref (ipart);

	simple = g_simple_async_result_new (
		G_OBJECT (context), callback,
		user_data, cipher_context_verify);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, cipher_context_verify_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static CamelCipherValidity *
cipher_context_verify_finish (CamelCipherContext *context,
                              GAsyncResult *result,
                              GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	CamelCipherValidity *validity;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (context), cipher_context_verify), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	validity = async_context->validity;
	async_context->validity = NULL;

	return validity;
}

static void
cipher_context_encrypt_thread (GSimpleAsyncResult *simple,
                               GObject *object,
                               GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_cipher_context_encrypt_sync (
		CAMEL_CIPHER_CONTEXT (object),
		async_context->userid, async_context->strings,
		async_context->ipart, async_context->opart,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
cipher_context_encrypt (CamelCipherContext *context,
                        const gchar *userid,
                        GPtrArray *recipients,
                        CamelMimePart *ipart,
                        CamelMimePart *opart,
                        gint io_priority,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	guint ii;

	async_context = g_slice_new0 (AsyncContext);
	async_context->userid = g_strdup (userid);
	async_context->strings = g_ptr_array_new ();
	async_context->ipart = g_object_ref (ipart);
	async_context->opart = g_object_ref (opart);

	for (ii = 0; ii < recipients->len; ii++)
		g_ptr_array_add (
			async_context->strings,
			g_strdup (recipients->pdata[ii]));

	simple = g_simple_async_result_new (
		G_OBJECT (context), callback,
		user_data, cipher_context_encrypt);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, cipher_context_encrypt_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
cipher_context_encrypt_finish (CamelCipherContext *context,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (context), cipher_context_encrypt), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
cipher_context_decrypt_thread (GSimpleAsyncResult *simple,
                               GObject *object,
                               GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	async_context->validity = camel_cipher_context_decrypt_sync (
		CAMEL_CIPHER_CONTEXT (object), async_context->ipart,
		async_context->opart, cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
cipher_context_decrypt (CamelCipherContext *context,
                        CamelMimePart *ipart,
                        CamelMimePart *opart,
                        gint io_priority,
                        GCancellable *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->ipart = g_object_ref (ipart);
	async_context->opart = g_object_ref (opart);

	simple = g_simple_async_result_new (
		G_OBJECT (context), callback,
		user_data, cipher_context_decrypt);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, cipher_context_decrypt_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static CamelCipherValidity *
cipher_context_decrypt_finish (CamelCipherContext *context,
                               GAsyncResult *result,
                               GError **error)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	CamelCipherValidity *validity;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (context), cipher_context_decrypt), NULL);

	simple = G_SIMPLE_ASYNC_RESULT (result);
	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	if (g_simple_async_result_propagate_error (simple, error))
		return NULL;

	validity = async_context->validity;
	async_context->validity = NULL;

	return validity;
}

static void
cipher_context_import_keys_thread (GSimpleAsyncResult *simple,
                                   GObject *object,
                                   GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_cipher_context_import_keys_sync (
		CAMEL_CIPHER_CONTEXT (object), async_context->stream,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
cipher_context_import_keys (CamelCipherContext *context,
                            CamelStream *istream,
                            gint io_priority,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->stream = g_object_ref (istream);

	simple = g_simple_async_result_new (
		G_OBJECT (context), callback,
		user_data, cipher_context_import_keys);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, cipher_context_import_keys_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
cipher_context_import_keys_finish (CamelCipherContext *context,
                                   GAsyncResult *result,
                                   GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (context),
		cipher_context_import_keys), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
cipher_context_export_keys_thread (GSimpleAsyncResult *simple,
                                   GObject *object,
                                   GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_cipher_context_export_keys_sync (
		CAMEL_CIPHER_CONTEXT (object), async_context->strings,
		async_context->stream, cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
cipher_context_export_keys (CamelCipherContext *context,
                            GPtrArray *keys,
                            CamelStream *ostream,
                            gint io_priority,
                            GCancellable *cancellable,
                            GAsyncReadyCallback callback,
                            gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;
	guint ii;

	async_context = g_slice_new0 (AsyncContext);
	async_context->strings = g_ptr_array_new ();
	async_context->stream = g_object_ref (ostream);

	for (ii = 0; ii < keys->len; ii++)
		g_ptr_array_add (
			async_context->strings,
			g_strdup (keys->pdata[ii]));

	simple = g_simple_async_result_new (
		G_OBJECT (context), callback,
		user_data, cipher_context_export_keys);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, cipher_context_export_keys_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
cipher_context_export_keys_finish (CamelCipherContext *context,
                                   GAsyncResult *result,
                                   GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (context),
		cipher_context_export_keys), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
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

	class->hash_to_id = cipher_context_hash_to_id;
	class->id_to_hash = cipher_context_id_to_hash;

	class->sign_sync = cipher_context_sign_sync;
	class->verify_sync = cipher_context_verify_sync;
	class->encrypt_sync = cipher_context_encrypt_sync;
	class->decrypt_sync = cipher_context_decrypt_sync;
	class->import_keys_sync = cipher_context_import_keys_sync;
	class->export_keys_sync = cipher_context_export_keys_sync;

	class->sign = cipher_context_sign;
	class->sign_finish = cipher_context_sign_finish;
	class->verify = cipher_context_verify;
	class->verify_finish = cipher_context_verify_finish;
	class->encrypt = cipher_context_encrypt;
	class->encrypt_finish = cipher_context_encrypt_finish;
	class->decrypt = cipher_context_decrypt;
	class->decrypt_finish = cipher_context_decrypt_finish;
	class->import_keys = cipher_context_import_keys;
	class->import_keys_finish = cipher_context_import_keys_finish;
	class->export_keys = cipher_context_export_keys;
	class->export_keys_finish = cipher_context_export_keys_finish;

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
 * camel_cipher_context_sign_sync:
 * @context: a #CamelCipherContext
 * @userid: a private key to use to sign the stream
 * @hash: preferred Message-Integrity-Check hash algorithm
 * @ipart: input #CamelMimePart
 * @opart: output #CamelMimePart
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Converts the (unsigned) part @ipart into a new self-contained MIME
 * part @opart.  This may be a multipart/signed part, or a simple part
 * for enveloped types.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_cipher_context_sign_sync (CamelCipherContext *context,
                                const gchar *userid,
                                CamelCipherHash hash,
                                CamelMimePart *ipart,
                                CamelMimePart *opart,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelCipherContextClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), FALSE);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->sign_sync != NULL, FALSE);

	CIPHER_LOCK (context);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		CIPHER_UNLOCK (context);
		return FALSE;
	}

	camel_operation_push_message (cancellable, _("Signing message"));

	success = class->sign_sync (
		context, userid, hash, ipart, opart, cancellable, error);
	CAMEL_CHECK_GERROR (context, sign_sync, success, error);

	camel_operation_pop_message (cancellable);

	CIPHER_UNLOCK (context);

	return success;
}

/**
 * camel_cipher_context_sign:
 * @context: a #CamelCipherContext
 * @userid: a private key to use to sign the stream
 * @hash: preferred Message-Integrity-Check hash algorithm
 * @ipart: input #CamelMimePart
 * @opart: output #CamelMimePart
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously converts the (unsigned) part @ipart into a new
 * self-contained MIME part @opart.  This may be a multipart/signed part,
 * or a simple part for enveloped types.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_cipher_context_sign_finish() to get the result of the operation.
 *
 * Since: 3.0
 **/
void
camel_cipher_context_sign (CamelCipherContext *context,
                           const gchar *userid,
                           CamelCipherHash hash,
                           CamelMimePart *ipart,
                           CamelMimePart *opart,
                           gint io_priority,
                           GCancellable *cancellable,
                           GAsyncReadyCallback callback,
                           gpointer user_data)
{
	CamelCipherContextClass *class;

	g_return_if_fail (CAMEL_IS_CIPHER_CONTEXT (context));

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_if_fail (class->sign != NULL);

	class->sign (
		context, userid, hash, ipart, opart, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_cipher_context_sign_finish:
 * @context: a #CamelCipherContext
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_cipher_context_sign().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_cipher_context_sign_finish (CamelCipherContext *context,
                                  GAsyncResult *result,
                                  GError **error)
{
	CamelCipherContextClass *class;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->sign_finish != NULL, FALSE);

	return class->sign_finish (context, result, error);
}

/**
 * camel_cipher_context_verify_sync:
 * @context: a #CamelCipherContext
 * @ipart: the #CamelMimePart to verify
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Verifies the signature.
 *
 * Returns: a #CamelCipherValidity structure containing information
 * about the integrity of the input stream, or %NULL on failure to
 * execute at all
 **/
CamelCipherValidity *
camel_cipher_context_verify_sync (CamelCipherContext *context,
                                  CamelMimePart *ipart,
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelCipherContextClass *class;
	CamelCipherValidity *valid;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), NULL);
	g_return_val_if_fail (CAMEL_IS_MIME_PART (ipart), NULL);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->verify_sync != NULL, NULL);

	CIPHER_LOCK (context);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		CIPHER_UNLOCK (context);
		return NULL;
	}

	valid = class->verify_sync (context, ipart, cancellable, error);
	CAMEL_CHECK_GERROR (context, verify_sync, valid != NULL, error);

	CIPHER_UNLOCK (context);

	return valid;
}

/**
 * camel_cipher_context_verify:
 * @context: a #CamelCipherContext
 * @ipart: the #CamelMimePart to verify
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously verifies the signature.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_cipher_context_verify_finish() to get the result of
 * the operation.
 *
 * Since: 3.0
 **/
void
camel_cipher_context_verify (CamelCipherContext *context,
                             CamelMimePart *ipart,
                             gint io_priority,
                             GCancellable *cancellable,
                             GAsyncReadyCallback callback,
                             gpointer user_data)
{
	CamelCipherContextClass *class;

	g_return_if_fail (CAMEL_IS_CIPHER_CONTEXT (context));
	g_return_if_fail (CAMEL_IS_MIME_PART (ipart));

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_if_fail (class->verify != NULL);

	class->verify (
		context, ipart, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_cipher_context_verify_finish:
 * @context: a #CamelCipherContext
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_cipher_context_verify().
 *
 * Returns: a #CamelCipherValidity structure containing information
 * about the integrity of the input stream, or %NULL on failure to
 * execute at all
 *
 * Since: 3.0
 **/
CamelCipherValidity *
camel_cipher_context_verify_finish (CamelCipherContext *context,
                                    GAsyncResult *result,
                                    GError **error)
{
	CamelCipherContextClass *class;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (context), NULL);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->verify_finish != NULL, NULL);

	return class->verify_finish (context, result, error);
}

/**
 * camel_cipher_context_encrypt_sync:
 * @context: a #CamelCipherContext
 * @userid: key ID (or email address) to use when signing, or %NULL to not sign
 * @recipients: an array of recipient key IDs and/or email addresses
 * @ipart: clear-text #CamelMimePart
 * @opart: cipher-text #CamelMimePart
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Encrypts (and optionally signs) the clear-text @ipart and writes the
 * resulting cipher-text to @opart.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_cipher_context_encrypt_sync (CamelCipherContext *context,
                                   const gchar *userid,
                                   GPtrArray *recipients,
                                   CamelMimePart *ipart,
                                   CamelMimePart *opart,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelCipherContextClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_PART (ipart), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_PART (opart), FALSE);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->encrypt_sync != NULL, FALSE);

	CIPHER_LOCK (context);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		CIPHER_UNLOCK (context);
		return FALSE;
	}

	camel_operation_push_message (cancellable, _("Encrypting message"));

	success = class->encrypt_sync (
		context, userid, recipients,
		ipart, opart, cancellable, error);
	CAMEL_CHECK_GERROR (context, encrypt_sync, success, error);

	camel_operation_pop_message (cancellable);

	CIPHER_UNLOCK (context);

	return success;
}

/**
 * camel_cipher_context_encrypt:
 * @context: a #CamelCipherContext
 * @userid: key id (or email address) to use when signing, or %NULL to not sign
 * @recipients: an array of recipient key IDs and/or email addresses
 * @ipart: clear-text #CamelMimePart
 * @opart: cipher-text #CamelMimePart
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously encrypts (and optionally signs) the clear-text @ipart and
 * writes the resulting cipher-text to @opart.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_cipher_context_encrypt_finish() to get the result of
 * the operation.
 *
 * Since: 3.0
 **/
void
camel_cipher_context_encrypt (CamelCipherContext *context,
                              const gchar *userid,
                              GPtrArray *recipients,
                              CamelMimePart *ipart,
                              CamelMimePart *opart,
                              gint io_priority,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	CamelCipherContextClass *class;

	g_return_if_fail (CAMEL_IS_CIPHER_CONTEXT (context));
	g_return_if_fail (CAMEL_IS_MIME_PART (ipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (opart));

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_if_fail (class->encrypt != NULL);

	class->encrypt (
		context, userid, recipients, ipart, opart,
		io_priority, cancellable, callback, user_data);
}

/**
 * camel_cipher_context_encrypt_finish:
 * @context: a #CamelCipherContext
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_cipher_context_encrypt().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_cipher_context_encrypt_finish (CamelCipherContext *context,
                                     GAsyncResult *result,
                                     GError **error)
{
	CamelCipherContextClass *class;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->encrypt_finish != NULL, FALSE);

	return class->encrypt_finish (context, result, error);
}

/**
 * camel_cipher_context_decrypt_sync:
 * @context: a #CamelCipherContext
 * @ipart: cipher-text #CamelMimePart
 * @opart: clear-text #CamelMimePart
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Decrypts @ipart into @opart.
 *
 * Returns: a validity/encryption status, or %NULL on error
 *
 * Since: 3.0
 **/
CamelCipherValidity *
camel_cipher_context_decrypt_sync (CamelCipherContext *context,
                                   CamelMimePart *ipart,
                                   CamelMimePart *opart,
                                   GCancellable *cancellable,
                                   GError **error)
{
	CamelCipherContextClass *class;
	CamelCipherValidity *valid;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), NULL);
	g_return_val_if_fail (CAMEL_IS_MIME_PART (ipart), NULL);
	g_return_val_if_fail (CAMEL_IS_MIME_PART (opart), NULL);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->decrypt_sync != NULL, NULL);

	CIPHER_LOCK (context);

	/* Check for cancellation after locking. */
	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		CIPHER_UNLOCK (context);
		return NULL;
	}

	camel_operation_push_message (cancellable, _("Decrypting message"));

	valid = class->decrypt_sync (
		context, ipart, opart, cancellable, error);
	CAMEL_CHECK_GERROR (context, decrypt_sync, valid != NULL, error);

	camel_operation_pop_message (cancellable);

	CIPHER_UNLOCK (context);

	return valid;
}

/**
 * camel_cipher_context_decrypt:
 * @context: a #CamelCipherContext
 * @ipart: cipher-text #CamelMimePart
 * @opart: clear-text #CamelMimePart
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously decrypts @ipart into @opart.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_cipher_context_decrypt_finish() to get the result of
 * the operation.
 *
 * Since: 3.0
 **/
void
camel_cipher_context_decrypt (CamelCipherContext *context,
                              CamelMimePart *ipart,
                              CamelMimePart *opart,
                              gint io_priority,
                              GCancellable *cancellable,
                              GAsyncReadyCallback callback,
                              gpointer user_data)
{
	CamelCipherContextClass *class;

	g_return_if_fail (CAMEL_IS_CIPHER_CONTEXT (context));
	g_return_if_fail (CAMEL_IS_MIME_PART (ipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (opart));

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_if_fail (class->decrypt != NULL);

	class->decrypt (
		context, ipart, opart, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_cipher_context_decrypt_finish:
 * @context: a #CamelCipherContext
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_cipher_context_decrypt().
 *
 * Returns: a validity/encryption status, or %NULL on error
 *
 * Since: 3.0
 **/
CamelCipherValidity *
camel_cipher_context_decrypt_finish (CamelCipherContext *context,
                                     GAsyncResult *result,
                                     GError **error)
{
	CamelCipherContextClass *class;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), NULL);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), NULL);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->decrypt_finish != NULL, NULL);

	return class->decrypt_finish (context, result, error);
}

/**
 * camel_cipher_context_import_keys_sync:
 * @context: a #CamelCipherContext
 * @istream: an input stream containing keys
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Imports a stream of keys/certificates contained within @istream
 * into the key/certificate database controlled by @context.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_cipher_context_import_keys_sync (CamelCipherContext *context,
                                       CamelStream *istream,
                                       GCancellable *cancellable,
                                       GError **error)
{
	CamelCipherContextClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), FALSE);
	g_return_val_if_fail (CAMEL_IS_STREAM (istream), FALSE);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->import_keys_sync != NULL, FALSE);

	success = class->import_keys_sync (
		context, istream, cancellable, error);
	CAMEL_CHECK_GERROR (context, import_keys_sync, success, error);

	return success;
}

/**
 * camel_cipher_context_import_keys:
 * @context: a #CamelCipherContext
 * @istream: an input stream containing keys
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously imports a stream of keys/certificates contained within
 * @istream into the key/certificate database controlled by @context.
 *
 * When the operation is finished, @callback will be called.  You can
 * then call camel_cipher_context_import_keys_finish() to get the result
 * of the operation.
 *
 * Since: 3.0
 **/
void
camel_cipher_context_import_keys (CamelCipherContext *context,
                                  CamelStream *istream,
                                  gint io_priority,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	CamelCipherContextClass *class;

	g_return_if_fail (CAMEL_IS_CIPHER_CONTEXT (context));
	g_return_if_fail (CAMEL_IS_STREAM (istream));

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_if_fail (class->import_keys != NULL);

	class->import_keys (
		context, istream, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_cipher_context_import_keys_finish:
 * @context: a #CamelCipherContext
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_cipher_context_import_keys().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_cipher_context_import_keys_finish (CamelCipherContext *context,
                                         GAsyncResult *result,
                                         GError **error)
{
	CamelCipherContextClass *class;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->import_keys_finish != NULL, FALSE);

	return class->import_keys_finish (context, result, error);
}

/**
 * camel_cipher_context_export_keys_sync:
 * @context: a #CamelCipherContext
 * @keys: an array of key IDs
 * @ostream: an output stream
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Exports the keys/certificates in @keys to the stream @ostream from
 * the key/certificate database controlled by @context.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_cipher_context_export_keys_sync (CamelCipherContext *context,
                                       GPtrArray *keys,
                                       CamelStream *ostream,
                                       GCancellable *cancellable,
                                       GError **error)
{
	CamelCipherContextClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), FALSE);
	g_return_val_if_fail (keys != NULL, FALSE);
	g_return_val_if_fail (CAMEL_IS_STREAM (ostream), FALSE);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->export_keys_sync != NULL, FALSE);

	success = class->export_keys_sync (
		context, keys, ostream, cancellable, error);
	CAMEL_CHECK_GERROR (context, export_keys_sync, success, error);

	return success;
}

/**
 * camel_cipher_context_export_keys:
 * @context: a #CamelCipherContext
 * @keys: an array of key IDs
 * @ostream: an output stream
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously exports the keys/certificates in @keys to the stream
 * @ostream from the key/certificate database controlled by @context.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_cipher_context_export_keys_finish() to get the result of the
 * operation.
 *
 * Since: 3.0
 **/
void
camel_cipher_context_export_keys (CamelCipherContext *context,
                                  GPtrArray *keys,
                                  CamelStream *ostream,
                                  gint io_priority,
                                  GCancellable *cancellable,
                                  GAsyncReadyCallback callback,
                                  gpointer user_data)
{
	CamelCipherContextClass *class;

	g_return_if_fail (CAMEL_IS_CIPHER_CONTEXT (context));
	g_return_if_fail (keys != NULL);
	g_return_if_fail (CAMEL_IS_STREAM (ostream));

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_if_fail (class->export_keys != NULL);

	class->export_keys (
		context, keys, ostream, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_cipher_context_export_keys_finish:
 * @context: a #CamelCipherContext
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_cipher_context_export_keys().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_cipher_context_export_keys_finish (CamelCipherContext *context,
                                         GAsyncResult *result,
                                         GError **error)
{
	CamelCipherContextClass *class;

	g_return_val_if_fail (CAMEL_IS_CIPHER_CONTEXT (context), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_CIPHER_CONTEXT_GET_CLASS (context);
	g_return_val_if_fail (class->export_keys_finish != NULL, FALSE);

	return class->export_keys_finish (context, result, error);
}

/* a couple of util functions */
CamelCipherHash
camel_cipher_context_id_to_hash (CamelCipherContext *context,
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

const gchar *
camel_cipher_context_hash_to_id (CamelCipherContext *context,
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
	g_return_if_fail (info != NULL);

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
	g_return_if_fail (validity != NULL);

	memset (validity, 0, sizeof (*validity));
	g_queue_init (&validity->children);
	g_queue_init (&validity->sign.signers);
	g_queue_init (&validity->encrypt.encrypters);
}

gboolean
camel_cipher_validity_get_valid (CamelCipherValidity *validity)
{
	return validity != NULL
		&& validity->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_GOOD;
}

void
camel_cipher_validity_set_valid (CamelCipherValidity *validity,
                                 gboolean valid)
{
	g_return_if_fail (validity != NULL);

	validity->sign.status = valid ? CAMEL_CIPHER_VALIDITY_SIGN_GOOD : CAMEL_CIPHER_VALIDITY_SIGN_BAD;
}

gchar *
camel_cipher_validity_get_description (CamelCipherValidity *validity)
{
	g_return_val_if_fail (validity != NULL, NULL);

	return validity->sign.description;
}

void
camel_cipher_validity_set_description (CamelCipherValidity *validity,
                                       const gchar *description)
{
	g_return_if_fail (validity != NULL);

	g_free (validity->sign.description);
	validity->sign.description = g_strdup (description);
}

void
camel_cipher_validity_clear (CamelCipherValidity *validity)
{
	g_return_if_fail (validity != NULL);

	/* TODO: this doesn't free children/clear key lists */
	g_free (validity->sign.description);
	g_free (validity->encrypt.description);
	camel_cipher_validity_init (validity);
}

CamelCipherValidity *
camel_cipher_validity_clone (CamelCipherValidity *vin)
{
	CamelCipherValidity *vo;
	GList *head, *link;

	g_return_val_if_fail (vin != NULL, NULL);

	vo = camel_cipher_validity_new ();
	vo->sign.status = vin->sign.status;
	vo->sign.description = g_strdup (vin->sign.description);
	vo->encrypt.status = vin->encrypt.status;
	vo->encrypt.description = g_strdup (vin->encrypt.description);

	head = g_queue_peek_head_link (&vin->sign.signers);
	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelCipherCertInfo *info = link->data;

		if (info->cert_data && info->cert_data_clone && info->cert_data_free)
			camel_cipher_validity_add_certinfo_ex (vo, CAMEL_CIPHER_VALIDITY_SIGN, info->name, info->email, info->cert_data_clone (info->cert_data), info->cert_data_free, info->cert_data_clone);
		else
			camel_cipher_validity_add_certinfo (vo, CAMEL_CIPHER_VALIDITY_SIGN, info->name, info->email);
	}

	head = g_queue_peek_head_link (&vin->encrypt.encrypters);
	for (link = head; link != NULL; link = g_list_next (link)) {
		CamelCipherCertInfo *info = link->data;

		if (info->cert_data && info->cert_data_clone && info->cert_data_free)
			camel_cipher_validity_add_certinfo_ex (vo, CAMEL_CIPHER_VALIDITY_SIGN, info->name, info->email, info->cert_data_clone (info->cert_data), info->cert_data_free, info->cert_data_clone);
		else
			camel_cipher_validity_add_certinfo (vo, CAMEL_CIPHER_VALIDITY_ENCRYPT, info->name, info->email);
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
camel_cipher_validity_add_certinfo (CamelCipherValidity *vin,
                                    enum _camel_cipher_validity_mode_t mode,
                                    const gchar *name,
                                    const gchar *email)
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
camel_cipher_validity_add_certinfo_ex (CamelCipherValidity *vin,
                                       camel_cipher_validity_mode_t mode,
                                       const gchar *name,
                                       const gchar *email,
                                       gpointer cert_data,
                                       void (*cert_data_free)(gpointer cert_data),
                                       gpointer (*cert_data_clone)(gpointer cert_data))
{
	CamelCipherCertInfo *info;

	g_return_if_fail (vin != NULL);
	if (cert_data) {
		g_return_if_fail (cert_data_free != NULL);
		g_return_if_fail (cert_data_clone != NULL);
	}

	info = g_malloc0 (sizeof (*info));
	info->name = g_strdup (name);
	info->email = g_strdup (email);
	if (cert_data) {
		info->cert_data = cert_data;
		info->cert_data_free = cert_data_free;
		info->cert_data_clone = cert_data_clone;
	}

	if (mode == CAMEL_CIPHER_VALIDITY_SIGN)
		g_queue_push_tail (&vin->sign.signers, info);
	else
		g_queue_push_tail (&vin->encrypt.encrypters, info);
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
camel_cipher_validity_envelope (CamelCipherValidity *parent,
                                CamelCipherValidity *valid)
{

	g_return_if_fail (parent != NULL);
	g_return_if_fail (valid != NULL);

	if (parent->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE
	    && parent->encrypt.status == CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE
	    && valid->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_NONE
	    && valid->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE) {
		GList *head, *link;

		/* case 1: only signed inside only encrypted -> merge both */
		parent->encrypt.status = valid->encrypt.status;
		parent->encrypt.description = g_strdup (valid->encrypt.description);

		head = g_queue_peek_head_link (&valid->encrypt.encrypters);
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelCipherCertInfo *info = link->data;
			camel_cipher_validity_add_certinfo (
				parent, CAMEL_CIPHER_VALIDITY_ENCRYPT,
				info->name, info->email);
		}
	} else if (parent->sign.status == CAMEL_CIPHER_VALIDITY_SIGN_NONE
		   && parent->encrypt.status != CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE
		   && valid->sign.status != CAMEL_CIPHER_VALIDITY_SIGN_NONE
		   && valid->encrypt.status == CAMEL_CIPHER_VALIDITY_ENCRYPT_NONE) {
		GList *head, *link;

		/* case 2: only encrypted inside only signed */
		parent->sign.status = valid->sign.status;
		parent->sign.description = g_strdup (valid->sign.description);

		head = g_queue_peek_head_link (&valid->sign.signers);
		for (link = head; link != NULL; link = g_list_next (link)) {
			CamelCipherCertInfo *info = link->data;
			camel_cipher_validity_add_certinfo (
				parent, CAMEL_CIPHER_VALIDITY_SIGN,
				info->name, info->email);
		}
	}
	/* Otherwise, I dunno - what do you do? */
}

void
camel_cipher_validity_free (CamelCipherValidity *validity)
{
	CamelCipherValidity *child;
	CamelCipherCertInfo *info;
	GQueue *queue;

	if (validity == NULL)
		return;

	queue = &validity->children;
	while ((child = g_queue_pop_head (queue)) != NULL)
		camel_cipher_validity_free (child);

	queue = &validity->sign.signers;
	while ((info = g_queue_pop_head (queue)) != NULL)
		ccv_certinfo_free (info);

	queue = &validity->encrypt.encrypters;
	while ((info = g_queue_pop_head (queue)) != NULL)
		ccv_certinfo_free (info);

	camel_cipher_validity_clear (validity);
	g_free (validity);
}

/* ********************************************************************** */

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
 * This is so that we can safely translate any occurance of "From "
 * into the quoted-printable escaped version safely. */
static void
cc_prepare_sign (CamelMimePart *part)
{
	CamelDataWrapper *dw;
	CamelTransferEncoding encoding;
	gint parts, i;

	dw = camel_medium_get_content ((CamelMedium *) part);
	if (!dw)
		return;

	if (CAMEL_IS_MULTIPART (dw)) {
		parts = camel_multipart_get_number ((CamelMultipart *) dw);
		for (i = 0; i < parts; i++)
			cc_prepare_sign (camel_multipart_get_part ((CamelMultipart *) dw, i));
	} else if (CAMEL_IS_MIME_MESSAGE (dw)) {
		cc_prepare_sign ((CamelMimePart *) dw);
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
 * @cancellable: optional #GCancellable object, or %NULL
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
                                  GCancellable *cancellable,
                                  GError **error)
{
	CamelStream *filter;
	CamelMimeFilter *canon;
	gint res = -1;

	g_return_val_if_fail (CAMEL_IS_MIME_PART (part), -1);
	g_return_val_if_fail (CAMEL_IS_STREAM (ostream), -1);

	if (flags & (CAMEL_MIME_FILTER_CANON_FROM | CAMEL_MIME_FILTER_CANON_STRIP))
		cc_prepare_sign (part);

	filter = camel_stream_filter_new (ostream);
	canon = camel_mime_filter_canon_new (flags);
	camel_stream_filter_add (CAMEL_STREAM_FILTER (filter), canon);
	g_object_unref (canon);

	if (camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (part), filter, cancellable, error) != -1
	    && camel_stream_flush (filter, cancellable, error) != -1)
		res = 0;

	g_object_unref (filter);

	/* Reset stream position to beginning. */
	if (G_IS_SEEKABLE (ostream))
		g_seekable_seek (
			G_SEEKABLE (ostream), 0,
			G_SEEK_SET, NULL, NULL);

	return res;
}
