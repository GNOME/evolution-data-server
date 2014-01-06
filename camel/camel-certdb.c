/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
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
	gchar *filename;
	guint32 version;
	guint32 saved_certs;
	gboolean dirty;

	GPtrArray *certs;
	GHashTable *cert_hash;

	GMutex db_lock;		/* for the db hashtable/array */
	GMutex io_lock;		/* load/save lock, for access to saved_count, etc */
};

G_DEFINE_TYPE (CamelCertDB, camel_certdb, G_TYPE_OBJECT)

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
	CamelCertDBPrivate *priv;

	priv = CAMEL_CERTDB_GET_PRIVATE (object);

	if (priv->dirty)
		camel_certdb_save (CAMEL_CERTDB (object));

	camel_certdb_clear (CAMEL_CERTDB (object));
	g_ptr_array_free (priv->certs, TRUE);
	g_hash_table_destroy (priv->cert_hash);

	g_free (priv->filename);

	g_mutex_clear (&priv->db_lock);
	g_mutex_clear (&priv->io_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_certdb_parent_class)->finalize (object);
}

static gint
certdb_header_load (CamelCertDB *certdb,
                    FILE *istream)
{
	if (camel_file_util_decode_uint32 (
		istream, &certdb->priv->version) == -1)
		return -1;
	if (camel_file_util_decode_uint32 (
		istream, &certdb->priv->saved_certs) == -1)
		return -1;

	return 0;
}

static gint
certdb_header_save (CamelCertDB *certdb,
                    FILE *ostream)
{
	if (camel_file_util_encode_uint32 (
		ostream, certdb->priv->version) == -1)
		return -1;
	if (camel_file_util_encode_uint32 (
		ostream, certdb->priv->saved_certs) == -1)
		return -1;

	return 0;
}

static CamelCert *
certdb_cert_load (CamelCertDB *certdb,
                  FILE *istream)
{
	CamelCert *cert;

	cert = camel_cert_new ();

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
	camel_cert_unref (cert);

	return NULL;
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

static void
camel_certdb_class_init (CamelCertDBClass *class)
{
	GObjectClass *object_class;

	g_type_class_add_private (class, sizeof (CamelCertDBPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = certdb_finalize;

	class->header_load = certdb_header_load;
	class->header_save = certdb_header_save;
	class->cert_load = certdb_cert_load;
	class->cert_save = certdb_cert_save;
}

static void
camel_certdb_init (CamelCertDB *certdb)
{
	certdb->priv = CAMEL_CERTDB_GET_PRIVATE (certdb);

	certdb->priv->version = CAMEL_CERTDB_VERSION;

	certdb->priv->certs = g_ptr_array_new ();
	certdb->priv->cert_hash = g_hash_table_new_full (
		(GHashFunc) certdb_key_hash,
		(GEqualFunc) certdb_key_equal,
		(GDestroyNotify) certdb_key_free,
		(GDestroyNotify) NULL);

	g_mutex_init (&certdb->priv->db_lock);
	g_mutex_init (&certdb->priv->io_lock);
}

CamelCert *
camel_cert_new (void)
{
	CamelCert *cert;

	cert = g_slice_new0 (CamelCert);
	cert->refcount = 1;

	return cert;
}

void
camel_cert_ref (CamelCert *cert)
{
	g_return_if_fail (cert != NULL);
	g_return_if_fail (cert->refcount > 0);

	g_atomic_int_inc (&cert->refcount);
}

void
camel_cert_unref (CamelCert *cert)
{
	g_return_if_fail (cert != NULL);
	g_return_if_fail (cert->refcount > 0);

	if (g_atomic_int_dec_and_test (&cert->refcount)) {
		g_free (cert->issuer);
		g_free (cert->subject);
		g_free (cert->hostname);
		g_free (cert->fingerprint);

		if (cert->rawcert != NULL)
			g_bytes_unref (cert->rawcert);

		g_slice_free (CamelCert, cert);
	}
}

CamelCertDB *
camel_certdb_new (void)
{
	return g_object_new (CAMEL_TYPE_CERTDB, NULL);
}

static CamelCertDB *default_certdb = NULL;
static GMutex default_certdb_lock;

void
camel_certdb_set_default (CamelCertDB *certdb)
{
	g_mutex_lock (&default_certdb_lock);

	if (default_certdb)
		g_object_unref (default_certdb);

	if (certdb)
		g_object_ref (certdb);

	default_certdb = certdb;

	g_mutex_unlock (&default_certdb_lock);
}

CamelCertDB *
camel_certdb_get_default (void)
{
	CamelCertDB *certdb;

	g_mutex_lock (&default_certdb_lock);

	if (default_certdb)
		g_object_ref (default_certdb);

	certdb = default_certdb;

	g_mutex_unlock (&default_certdb_lock);

	return certdb;
}

void
camel_certdb_set_filename (CamelCertDB *certdb,
                           const gchar *filename)
{
	g_return_if_fail (CAMEL_IS_CERTDB (certdb));
	g_return_if_fail (filename != NULL);

	g_mutex_lock (&certdb->priv->db_lock);

	g_free (certdb->priv->filename);
	certdb->priv->filename = g_strdup (filename);

	g_mutex_unlock (&certdb->priv->db_lock);
}

gint
camel_certdb_load (CamelCertDB *certdb)
{
	CamelCertDBClass *class;
	CamelCert *cert;
	FILE *in;
	gint i;

	g_return_val_if_fail (CAMEL_IS_CERTDB (certdb), -1);
	g_return_val_if_fail (certdb->priv->filename != NULL, -1);

	in = g_fopen (certdb->priv->filename, "rb");
	if (in == NULL)
		return -1;

	class = CAMEL_CERTDB_GET_CLASS (certdb);
	if (!class->header_load || !class->cert_load) {
		fclose (in);
		in = NULL;
		g_warn_if_reached ();

		return -1;
	}

	g_mutex_lock (&certdb->priv->io_lock);
	if (class->header_load (certdb, in) == -1)
		goto error;

	for (i = 0; i < certdb->priv->saved_certs; i++) {
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

	g_mutex_unlock (&certdb->priv->io_lock);

	if (fclose (in) != 0)
		return -1;

	certdb->priv->dirty = FALSE;

	return 0;

 error:

	g_warning ("Cannot load certificate database: %s", g_strerror (ferror (in)));

	g_mutex_unlock (&certdb->priv->io_lock);

	fclose (in);

	return -1;
}

gint
camel_certdb_save (CamelCertDB *certdb)
{
	CamelCertDBClass *class;
	CamelCert *cert;
	gchar *filename;
	gsize filename_len;
	gint fd, i;
	FILE *out;

	g_return_val_if_fail (CAMEL_IS_CERTDB (certdb), -1);
	g_return_val_if_fail (certdb->priv->filename != NULL, -1);

	/* no change, nothing new to save, simply return success */
	if (!certdb->priv->dirty)
		return 0;

	filename_len = strlen (certdb->priv->filename) + 4;
	filename = alloca (filename_len);
	g_snprintf (filename, filename_len, "%s~", certdb->priv->filename);

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
		g_warn_if_reached ();

		return -1;
	}

	g_mutex_lock (&certdb->priv->io_lock);

	certdb->priv->saved_certs = certdb->priv->certs->len;
	if (class->header_save (certdb, out) == -1)
		goto error;

	for (i = 0; i < certdb->priv->saved_certs; i++) {
		cert = (CamelCert *) certdb->priv->certs->pdata[i];

		if (class->cert_save (certdb, cert, out) == -1)
			goto error;
	}

	g_mutex_unlock (&certdb->priv->io_lock);

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

	if (g_rename (filename, certdb->priv->filename) == -1) {
		i = errno;
		g_unlink (filename);
		errno = i;
		return -1;
	}

	certdb->priv->dirty = FALSE;

	return 0;

 error:

	g_warning ("Cannot save certificate database: %s", g_strerror (ferror (out)));

	g_mutex_unlock (&certdb->priv->io_lock);

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

	certdb->priv->dirty = TRUE;
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

	g_mutex_lock (&certdb->priv->db_lock);

	key = certdb_key_new (hostname, fingerprint);

	cert = g_hash_table_lookup (certdb->priv->cert_hash, key);
	if (cert != NULL)
		camel_cert_ref (cert);

	certdb_key_free (key);

	g_mutex_unlock (&certdb->priv->db_lock);

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

	g_mutex_lock (&certdb->priv->db_lock);

	key = certdb_key_new (cert->hostname, cert->fingerprint);

	/* Replace an existing entry with the same hostname. */
	old_cert = g_hash_table_lookup (certdb->priv->cert_hash, key);
	if (old_cert != NULL) {
		g_hash_table_remove (certdb->priv->cert_hash, key);
		g_ptr_array_remove (certdb->priv->certs, old_cert);
		camel_cert_unref (old_cert);
	}

	camel_cert_ref (cert);
	g_ptr_array_add (certdb->priv->certs, cert);
	/* takes ownership of 'key' */
	g_hash_table_insert (certdb->priv->cert_hash, key, cert);

	certdb->priv->dirty = TRUE;

	g_mutex_unlock (&certdb->priv->db_lock);
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

	g_mutex_lock (&certdb->priv->db_lock);

	key = certdb_key_new (hostname, fingerprint);
	cert = g_hash_table_lookup (certdb->priv->cert_hash, key);
	if (cert != NULL) {
		g_hash_table_remove (certdb->priv->cert_hash, key);
		g_ptr_array_remove (certdb->priv->certs, cert);
		camel_cert_unref (cert);

		certdb->priv->dirty = TRUE;
	}

	certdb_key_free (key);

	g_mutex_unlock (&certdb->priv->db_lock);
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

	g_mutex_lock (&certdb->priv->db_lock);

	g_hash_table_foreach_remove (certdb->priv->cert_hash, cert_remove, NULL);
	for (i = 0; i < certdb->priv->certs->len; i++) {
		cert = (CamelCert *) certdb->priv->certs->pdata[i];
		camel_cert_unref (cert);
	}

	certdb->priv->saved_certs = 0;
	g_ptr_array_set_size (certdb->priv->certs, 0);
	certdb->priv->dirty = TRUE;

	g_mutex_unlock (&certdb->priv->db_lock);
}

