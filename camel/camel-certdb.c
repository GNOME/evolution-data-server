/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <glib/gstdio.h>

#include "camel-certdb.h"
#include "camel-file-utils.h"
#include "camel-win32.h"

#define CAMEL_CERTDB_VERSION  0x100

#define CAMEL_CERTDB_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_CERTDB, CamelCertDBPrivate))

struct _CamelCertDBPrivate {
	GMutex *db_lock;	/* for the db hashtable/array */
	GMutex *io_lock;	/* load/save lock, for access to saved_count, etc */
	GMutex *alloc_lock;	/* for setting up and using allocators */
	GMutex *ref_lock;	/* for reffing/unreffing certs */
};

static gint certdb_header_load (CamelCertDB *certdb, FILE *istream);
static gint certdb_header_save (CamelCertDB *certdb, FILE *ostream);
static CamelCert *certdb_cert_load (CamelCertDB *certdb, FILE *istream);
static gint certdb_cert_save (CamelCertDB *certdb, CamelCert *cert, FILE *ostream);
static CamelCert *certdb_cert_new (CamelCertDB *certdb);
static void certdb_cert_free (CamelCertDB *certdb, CamelCert *cert);

static const gchar *cert_get_string (CamelCertDB *certdb, CamelCert *cert, gint string);
static void cert_set_string (CamelCertDB *certdb, CamelCert *cert, gint string, const gchar *value);

G_DEFINE_TYPE (CamelCertDB, camel_certdb, CAMEL_TYPE_OBJECT)

typedef struct {
	gchar *hostname;
	gchar *fingerprint;
} CamelCertDBKey;

static CamelCertDBKey *
certdb_key_new (const gchar *hostname,
                const gchar *fingerprint)
{
	CamelCertDBKey *key;

	key = g_new0 (CamelCertDBKey, 1);
	key->hostname = g_strdup (hostname);
	key->fingerprint = g_strdup (fingerprint);

	return key;
}

static void
certdb_key_free (gpointer ptr)
{
	CamelCertDBKey *key = ptr;

	if (!key)
		return;

	g_free (key->hostname);
	g_free (key->fingerprint);
	g_free (key);
}

static guint
certdb_key_hash (gconstpointer ptr)
{
	const CamelCertDBKey *key = ptr;

	if (!key)
		return 0;

	/* hash by fingerprint only, but compare by both hostname and fingerprint */
	return g_str_hash (key->fingerprint);
}

static gboolean
certdb_key_equal (gconstpointer ptr1,
                  gconstpointer ptr2)
{
	const CamelCertDBKey *key1 = ptr1, *key2 = ptr2;
	gboolean same_hostname;

	if (!key1 || !key2)
		return key1 == key2;

	if (!key1->hostname || !key2->hostname)
		same_hostname = key1->hostname == key2->hostname;
	else
		same_hostname = g_ascii_strcasecmp (key1->hostname, key2->hostname) == 0;

	if (same_hostname) {
		if (!key1->fingerprint || !key2->fingerprint)
			return key1->fingerprint == key2->fingerprint;

		return g_ascii_strcasecmp (key1->fingerprint, key2->fingerprint) == 0;
	}

	return same_hostname;
}

static void
certdb_finalize (GObject *object)
{
	CamelCertDB *certdb = CAMEL_CERTDB (object);
	CamelCertDBPrivate *priv;

	priv = CAMEL_CERTDB_GET_PRIVATE (object);

	if (certdb->flags & CAMEL_CERTDB_DIRTY)
		camel_certdb_save (certdb);

	camel_certdb_clear (certdb);
	g_ptr_array_free (certdb->certs, TRUE);
	g_hash_table_destroy (certdb->cert_hash);

	g_free (certdb->filename);

	if (certdb->cert_chunks)
		camel_memchunk_destroy (certdb->cert_chunks);

	g_mutex_free (priv->db_lock);
	g_mutex_free (priv->io_lock);
	g_mutex_free (priv->alloc_lock);
	g_mutex_free (priv->ref_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_certdb_parent_class)->finalize (object);
}

static void
camel_certdb_class_init (CamelCertDBClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelCertDBPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = certdb_finalize;

	class->header_load = certdb_header_load;
	class->header_save = certdb_header_save;

	class->cert_new  = certdb_cert_new;
	class->cert_load = certdb_cert_load;
	class->cert_save = certdb_cert_save;
	class->cert_free = certdb_cert_free;
	class->cert_get_string = cert_get_string;
	class->cert_set_string = cert_set_string;
}

static void
camel_certdb_init (CamelCertDB *certdb)
{
	certdb->priv = CAMEL_CERTDB_GET_PRIVATE (certdb);

	certdb->filename = NULL;
	certdb->version = CAMEL_CERTDB_VERSION;
	certdb->saved_certs = 0;

	certdb->cert_size = sizeof (CamelCert);

	certdb->cert_chunks = NULL;

	certdb->certs = g_ptr_array_new ();
	certdb->cert_hash = g_hash_table_new_full (certdb_key_hash, certdb_key_equal, certdb_key_free, NULL);

	certdb->priv->db_lock = g_mutex_new ();
	certdb->priv->io_lock = g_mutex_new ();
	certdb->priv->alloc_lock = g_mutex_new ();
	certdb->priv->ref_lock = g_mutex_new ();
}

CamelCertDB *
camel_certdb_new (void)
{
	return g_object_new (CAMEL_TYPE_CERTDB, NULL);
}

static CamelCertDB *default_certdb = NULL;
static GStaticMutex default_certdb_lock = G_STATIC_MUTEX_INIT;

void
camel_certdb_set_default (CamelCertDB *certdb)
{
	g_static_mutex_lock (&default_certdb_lock);

	if (default_certdb)
		g_object_unref (default_certdb);

	if (certdb)
		g_object_ref (certdb);

	default_certdb = certdb;

	g_static_mutex_unlock (&default_certdb_lock);
}

CamelCertDB *
camel_certdb_get_default (void)
{
	CamelCertDB *certdb;

	g_static_mutex_lock (&default_certdb_lock);

	if (default_certdb)
		g_object_ref (default_certdb);

	certdb = default_certdb;

	g_static_mutex_unlock (&default_certdb_lock);

	return certdb;
}

void
camel_certdb_set_filename (CamelCertDB *certdb,
                           const gchar *filename)
{
	g_return_if_fail (CAMEL_IS_CERTDB (certdb));
	g_return_if_fail (filename != NULL);

	camel_certdb_lock (certdb, CAMEL_CERTDB_DB_LOCK);

	g_free (certdb->filename);
	certdb->filename = g_strdup (filename);

	camel_certdb_unlock (certdb, CAMEL_CERTDB_DB_LOCK);
}

static gint
certdb_header_load (CamelCertDB *certdb,
                    FILE *istream)
{
	if (camel_file_util_decode_uint32 (istream, &certdb->version) == -1)
		return -1;
	if (camel_file_util_decode_uint32 (istream, &certdb->saved_certs) == -1)
		return -1;

	return 0;
}

static CamelCert *
certdb_cert_load (CamelCertDB *certdb,
                  FILE *istream)
{
	CamelCert *cert;

	cert = camel_certdb_cert_new (certdb);

	if (camel_file_util_decode_string (istream, &cert->issuer) == -1)
		goto error;
	if (camel_file_util_decode_string (istream, &cert->subject) == -1)
		goto error;
	if (camel_file_util_decode_string (istream, &cert->hostname) == -1)
		goto error;
	if (camel_file_util_decode_string (istream, &cert->fingerprint) == -1)
		goto error;
	if (camel_file_util_decode_uint32 (istream, &cert->trust) == -1)
		goto error;

	/* unset temporary trusts on load */
	if (cert->trust == CAMEL_CERT_TRUST_TEMPORARY)
		cert->trust = CAMEL_CERT_TRUST_UNKNOWN;

	return cert;

 error:

	camel_certdb_cert_unref (certdb, cert);

	return NULL;
}

gint
camel_certdb_load (CamelCertDB *certdb)
{
	CamelCertDBClass *class;
	CamelCert *cert;
	FILE *in;
	gint i;

	g_return_val_if_fail (CAMEL_IS_CERTDB (certdb), -1);
	g_return_val_if_fail (certdb->filename, -1);

	in = g_fopen (certdb->filename, "rb");
	if (in == NULL)
		return -1;

	class = CAMEL_CERTDB_GET_CLASS (certdb);
	if (!class->header_load || !class->cert_load) {
		fclose (in);
		in = NULL;
	}
	g_return_val_if_fail (class->header_load != NULL, -1);
	g_return_val_if_fail (class->cert_load != NULL, -1);

	camel_certdb_lock (certdb, CAMEL_CERTDB_IO_LOCK);
	if (class->header_load (certdb, in) == -1)
		goto error;

	for (i = 0; i < certdb->saved_certs; i++) {
		cert = class->cert_load (certdb, in);

		if (cert == NULL)
			goto error;

		/* NOTE: If we are upgrading from an evolution-data-server version
		 * prior to the change to look up certs by hostname (bug 606181),
		 * this "put" will result in duplicate entries for the same
		 * hostname being dropped.  The change will become permanent on
		 * disk the next time the certdb is dirtied for some reason and
		 * has to be saved. */
		camel_certdb_put (certdb, cert);
	}

	camel_certdb_unlock (certdb, CAMEL_CERTDB_IO_LOCK);

	if (fclose (in) != 0)
		return -1;

	certdb->flags &= ~CAMEL_CERTDB_DIRTY;

	return 0;

 error:

	g_warning ("Cannot load certificate database: %s", g_strerror (ferror (in)));

	camel_certdb_unlock (certdb, CAMEL_CERTDB_IO_LOCK);

	fclose (in);

	return -1;
}

static gint
certdb_header_save (CamelCertDB *certdb,
                    FILE *ostream)
{
	if (camel_file_util_encode_uint32 (ostream, certdb->version) == -1)
		return -1;
	if (camel_file_util_encode_uint32 (ostream, certdb->saved_certs) == -1)
		return -1;

	return 0;
}

static gint
certdb_cert_save (CamelCertDB *certdb,
                  CamelCert *cert,
                  FILE *ostream)
{
	if (camel_file_util_encode_string (ostream, cert->issuer) == -1)
		return -1;
	if (camel_file_util_encode_string (ostream, cert->subject) == -1)
		return -1;
	if (camel_file_util_encode_string (ostream, cert->hostname) == -1)
		return -1;
	if (camel_file_util_encode_string (ostream, cert->fingerprint) == -1)
		return -1;
	if (camel_file_util_encode_uint32 (ostream, cert->trust) == -1)
		return -1;

	return 0;
}

gint
camel_certdb_save (CamelCertDB *certdb)
{
	CamelCertDBClass *class;
	CamelCert *cert;
	gchar *filename;
	gint fd, i;
	FILE *out;

	g_return_val_if_fail (CAMEL_IS_CERTDB (certdb), -1);
	g_return_val_if_fail (certdb->filename, -1);

	/* no change, nothing new to save, simply return success */
	if ((certdb->flags & CAMEL_CERTDB_DIRTY) == 0)
		return 0;

	filename = alloca (strlen (certdb->filename) + 4);
	sprintf (filename, "%s~", certdb->filename);

	fd = g_open (filename, O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0600);
	if (fd == -1)
		return -1;

	out = fdopen (fd, "wb");
	if (out == NULL) {
		i = errno;
		close (fd);
		g_unlink (filename);
		errno = i;
		return -1;
	}

	class = CAMEL_CERTDB_GET_CLASS (certdb);
	if (!class->header_save || !class->cert_save) {
		fclose (out);
		out = NULL;
	}
	g_return_val_if_fail (class->header_save != NULL, -1);
	g_return_val_if_fail (class->cert_save != NULL, -1);

	camel_certdb_lock (certdb, CAMEL_CERTDB_IO_LOCK);

	certdb->saved_certs = certdb->certs->len;
	if (class->header_save (certdb, out) == -1)
		goto error;

	for (i = 0; i < certdb->saved_certs; i++) {
		cert = (CamelCert *) certdb->certs->pdata[i];

		if (class->cert_save (certdb, cert, out) == -1)
			goto error;
	}

	camel_certdb_unlock (certdb, CAMEL_CERTDB_IO_LOCK);

	if (fflush (out) != 0 || fsync (fileno (out)) == -1) {
		i = errno;
		fclose (out);
		g_unlink (filename);
		errno = i;
		return -1;
	}

	if (fclose (out) != 0) {
		i = errno;
		g_unlink (filename);
		errno = i;
		return -1;
	}

	if (g_rename (filename, certdb->filename) == -1) {
		i = errno;
		g_unlink (filename);
		errno = i;
		return -1;
	}

	certdb->flags &= ~CAMEL_CERTDB_DIRTY;

	return 0;

 error:

	g_warning ("Cannot save certificate database: %s", g_strerror (ferror (out)));

	camel_certdb_unlock (certdb, CAMEL_CERTDB_IO_LOCK);

	i = errno;
	fclose (out);
	g_unlink (filename);
	errno = i;

	return -1;
}

void
camel_certdb_touch (CamelCertDB *certdb)
{
	g_return_if_fail (CAMEL_IS_CERTDB (certdb));

	certdb->flags |= CAMEL_CERTDB_DIRTY;
}

/**
 * camel_certdb_get_host:
 *
 * FIXME Document me!
 *
 * Since: 3.6
 **/
CamelCert *
camel_certdb_get_host (CamelCertDB *certdb,
                       const gchar *hostname,
                       const gchar *fingerprint)
{
	CamelCert *cert;
	CamelCertDBKey *key;

	g_return_val_if_fail (CAMEL_IS_CERTDB (certdb), NULL);

	camel_certdb_lock (certdb, CAMEL_CERTDB_DB_LOCK);

	key = certdb_key_new (hostname, fingerprint);

	cert = g_hash_table_lookup (certdb->cert_hash, key);
	if (cert)
		camel_certdb_cert_ref (certdb, cert);

	certdb_key_free (key);

	camel_certdb_unlock (certdb, CAMEL_CERTDB_DB_LOCK);

	return cert;
}

/**
 * camel_certdb_put:
 *
 * FIXME Document me!
 *
 * Since: 3.6
 **/
void
camel_certdb_put (CamelCertDB *certdb,
                  CamelCert *cert)
{
	CamelCert *old_cert;
	CamelCertDBKey *key;

	g_return_if_fail (CAMEL_IS_CERTDB (certdb));

	camel_certdb_lock (certdb, CAMEL_CERTDB_DB_LOCK);

	key = certdb_key_new (cert->hostname, cert->fingerprint);

	/* Replace an existing entry with the same hostname. */
	old_cert = g_hash_table_lookup (certdb->cert_hash, key);
	if (old_cert) {
		g_hash_table_remove (certdb->cert_hash, key);
		g_ptr_array_remove (certdb->certs, old_cert);
		camel_certdb_cert_unref (certdb, old_cert);
	}

	camel_certdb_cert_ref (certdb, cert);
	g_ptr_array_add (certdb->certs, cert);
	/* takes ownership of 'key' */
	g_hash_table_insert (certdb->cert_hash, key, cert);

	certdb->flags |= CAMEL_CERTDB_DIRTY;

	camel_certdb_unlock (certdb, CAMEL_CERTDB_DB_LOCK);
}

/**
 * camel_certdb_remove_host:
 *
 * FIXME Document me!
 *
 * Since: 3.6
 **/
void
camel_certdb_remove_host (CamelCertDB *certdb,
                          const gchar *hostname,
                          const gchar *fingerprint)
{
	CamelCert *cert;
	CamelCertDBKey *key;

	g_return_if_fail (CAMEL_IS_CERTDB (certdb));

	camel_certdb_lock (certdb, CAMEL_CERTDB_DB_LOCK);

	key = certdb_key_new (hostname, fingerprint);
	cert = g_hash_table_lookup (certdb->cert_hash, key);
	if (cert) {
		g_hash_table_remove (certdb->cert_hash, key);
		g_ptr_array_remove (certdb->certs, cert);
		camel_certdb_cert_unref (certdb, cert);

		certdb->flags |= CAMEL_CERTDB_DIRTY;
	}

	certdb_key_free (key);

	camel_certdb_unlock (certdb, CAMEL_CERTDB_DB_LOCK);
}

static CamelCert *
certdb_cert_new (CamelCertDB *certdb)
{
	CamelCert *cert;

	if (certdb->cert_chunks)
		cert = camel_memchunk_alloc0 (certdb->cert_chunks);
	else
		cert = g_malloc0 (certdb->cert_size);

	cert->refcount = 1;

	return cert;
}

CamelCert *
camel_certdb_cert_new (CamelCertDB *certdb)
{
	CamelCertDBClass *class;
	CamelCert *cert;

	g_return_val_if_fail (CAMEL_IS_CERTDB (certdb), NULL);

	class = CAMEL_CERTDB_GET_CLASS (certdb);
	g_return_val_if_fail (class->cert_new != NULL, NULL);

	camel_certdb_lock (certdb, CAMEL_CERTDB_ALLOC_LOCK);

	cert = class->cert_new (certdb);

	camel_certdb_unlock (certdb, CAMEL_CERTDB_ALLOC_LOCK);

	return cert;
}

void
camel_certdb_cert_ref (CamelCertDB *certdb,
                       CamelCert *cert)
{
	g_return_if_fail (CAMEL_IS_CERTDB (certdb));
	g_return_if_fail (cert != NULL);

	camel_certdb_lock (certdb, CAMEL_CERTDB_REF_LOCK);
	cert->refcount++;
	camel_certdb_unlock (certdb, CAMEL_CERTDB_REF_LOCK);
}

static void
certdb_cert_free (CamelCertDB *certdb,
                  CamelCert *cert)
{
	g_free (cert->issuer);
	g_free (cert->subject);
	g_free (cert->hostname);
	g_free (cert->fingerprint);
	if (cert->rawcert)
		g_byte_array_free (cert->rawcert, TRUE);
}

void
camel_certdb_cert_unref (CamelCertDB *certdb,
                         CamelCert *cert)
{
	CamelCertDBClass *class;

	g_return_if_fail (CAMEL_IS_CERTDB (certdb));
	g_return_if_fail (cert != NULL);

	class = CAMEL_CERTDB_GET_CLASS (certdb);
	g_return_if_fail (class->cert_free != NULL);

	camel_certdb_lock (certdb, CAMEL_CERTDB_REF_LOCK);

	if (cert->refcount <= 1) {
		class->cert_free (certdb, cert);
		if (certdb->cert_chunks)
			camel_memchunk_free (certdb->cert_chunks, cert);
		else
			g_free (cert);
	} else {
		cert->refcount--;
	}

	camel_certdb_unlock (certdb, CAMEL_CERTDB_REF_LOCK);
}

static gboolean
cert_remove (gpointer key,
             gpointer value,
             gpointer user_data)
{
	return TRUE;
}

void
camel_certdb_clear (CamelCertDB *certdb)
{
	CamelCert *cert;
	gint i;

	g_return_if_fail (CAMEL_IS_CERTDB (certdb));

	camel_certdb_lock (certdb, CAMEL_CERTDB_DB_LOCK);

	g_hash_table_foreach_remove (certdb->cert_hash, cert_remove, NULL);
	for (i = 0; i < certdb->certs->len; i++) {
		cert = (CamelCert *) certdb->certs->pdata[i];
		camel_certdb_cert_unref (certdb, cert);
	}

	certdb->saved_certs = 0;
	g_ptr_array_set_size (certdb->certs, 0);
	certdb->flags |= CAMEL_CERTDB_DIRTY;

	camel_certdb_unlock (certdb, CAMEL_CERTDB_DB_LOCK);
}

static const gchar *
cert_get_string (CamelCertDB *certdb,
                 CamelCert *cert,
                 gint string)
{
	switch (string) {
	case CAMEL_CERT_STRING_ISSUER:
		return cert->issuer;
	case CAMEL_CERT_STRING_SUBJECT:
		return cert->subject;
	case CAMEL_CERT_STRING_HOSTNAME:
		return cert->hostname;
	case CAMEL_CERT_STRING_FINGERPRINT:
		return cert->fingerprint;
	default:
		return NULL;
	}
}

const gchar *
camel_cert_get_string (CamelCertDB *certdb,
                       CamelCert *cert,
                       gint string)
{
	CamelCertDBClass *class;

	g_return_val_if_fail (CAMEL_IS_CERTDB (certdb), NULL);
	g_return_val_if_fail (cert != NULL, NULL);

	class = CAMEL_CERTDB_GET_CLASS (certdb);
	g_return_val_if_fail (class->cert_get_string != NULL, NULL);

	/* FIXME: do locking? */

	return class->cert_get_string (certdb, cert, string);
}

static void
cert_set_string (CamelCertDB *certdb,
                 CamelCert *cert,
                 gint string,
                 const gchar *value)
{
	switch (string) {
	case CAMEL_CERT_STRING_ISSUER:
		g_free (cert->issuer);
		cert->issuer = g_strdup (value);
		break;
	case CAMEL_CERT_STRING_SUBJECT:
		g_free (cert->subject);
		cert->subject = g_strdup (value);
		break;
	case CAMEL_CERT_STRING_HOSTNAME:
		g_free (cert->hostname);
		cert->hostname = g_strdup (value);
		break;
	case CAMEL_CERT_STRING_FINGERPRINT:
		g_free (cert->fingerprint);
		cert->fingerprint = g_strdup (value);
		break;
	default:
		break;
	}
}

void
camel_cert_set_string (CamelCertDB *certdb,
                       CamelCert *cert,
                       gint string,
                       const gchar *value)
{
	CamelCertDBClass *class;

	g_return_if_fail (CAMEL_IS_CERTDB (certdb));
	g_return_if_fail (cert != NULL);

	class = CAMEL_CERTDB_GET_CLASS (certdb);
	g_return_if_fail (class->cert_set_string != NULL);

	/* FIXME: do locking? */

	class->cert_set_string (certdb, cert, string, value);
}

CamelCertTrust
camel_cert_get_trust (CamelCertDB *certdb,
                      CamelCert *cert)
{
	g_return_val_if_fail (CAMEL_IS_CERTDB (certdb), CAMEL_CERT_TRUST_UNKNOWN);
	g_return_val_if_fail (cert != NULL, CAMEL_CERT_TRUST_UNKNOWN);

	return cert->trust;
}

void
camel_cert_set_trust (CamelCertDB *certdb,
                      CamelCert *cert,
                      CamelCertTrust trust)
{
	g_return_if_fail (CAMEL_IS_CERTDB (certdb));
	g_return_if_fail (cert != NULL);

	cert->trust = trust;
}

/**
 * camel_certdb_lock:
 * @certdb: a #CamelCertDB
 * @lock: lock type to lock
 *
 * Locks @certdb's @lock. Unlock it with camel_certdb_unlock().
 *
 * Since: 2.32
 **/
void
camel_certdb_lock (CamelCertDB *certdb,
                   CamelCertDBLock lock)
{
	g_return_if_fail (CAMEL_IS_CERTDB (certdb));

	switch (lock) {
	case CAMEL_CERTDB_DB_LOCK:
		g_mutex_lock (certdb->priv->db_lock);
		break;
	case CAMEL_CERTDB_IO_LOCK:
		g_mutex_lock (certdb->priv->io_lock);
		break;
	case CAMEL_CERTDB_ALLOC_LOCK:
		g_mutex_lock (certdb->priv->alloc_lock);
		break;
	case CAMEL_CERTDB_REF_LOCK:
		g_mutex_lock (certdb->priv->ref_lock);
		break;
	default:
		g_return_if_reached ();
	}
}

/**
 * camel_certdb_unlock:
 * @certdb: a #CamelCertDB
 * @lock: lock type to unlock
 *
 * Unlocks @certdb's @lock, previously locked with camel_certdb_lock().
 *
 * Since: 2.32
 **/
void
camel_certdb_unlock (CamelCertDB *certdb,
                     CamelCertDBLock lock)
{
	g_return_if_fail (CAMEL_IS_CERTDB (certdb));

	switch (lock) {
	case CAMEL_CERTDB_DB_LOCK:
		g_mutex_unlock (certdb->priv->db_lock);
		break;
	case CAMEL_CERTDB_IO_LOCK:
		g_mutex_unlock (certdb->priv->io_lock);
		break;
	case CAMEL_CERTDB_ALLOC_LOCK:
		g_mutex_unlock (certdb->priv->alloc_lock);
		break;
	case CAMEL_CERTDB_REF_LOCK:
		g_mutex_unlock (certdb->priv->ref_lock);
		break;
	default:
		g_return_if_reached ();
	}
}
