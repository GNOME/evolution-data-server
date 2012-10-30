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

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_CERTDB_H
#define CAMEL_CERTDB_H

#include <stdio.h>
#include <camel/camel-memchunk.h>
#include <camel/camel-object.h>

/* Standard GObject macros */
#define CAMEL_TYPE_CERTDB \
	(camel_certdb_get_type ())
#define CAMEL_CERTDB(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_CERTDB, CamelCertDB))
#define CAMEL_CERTDB_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_CERTDB, CamelCertDBClass))
#define CAMEL_IS_CERTDB(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_CERTDB))
#define CAMEL_IS_CERTDB_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_CERTDB))
#define CAMEL_CERTDB_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_CERTDB, CamelCertDBClass))

G_BEGIN_DECLS

typedef struct _CamelCertDB CamelCertDB;
typedef struct _CamelCertDBClass CamelCertDBClass;
typedef struct _CamelCertDBPrivate CamelCertDBPrivate;

typedef enum {
	CAMEL_CERTDB_DIRTY = 1 << 0
} CamelCertDBFlags;

enum {
	CAMEL_CERT_STRING_ISSUER,
	CAMEL_CERT_STRING_SUBJECT,
	CAMEL_CERT_STRING_HOSTNAME,
	CAMEL_CERT_STRING_FINGERPRINT
};

typedef enum {
	CAMEL_CERT_TRUST_UNKNOWN,
	CAMEL_CERT_TRUST_NEVER,
	CAMEL_CERT_TRUST_MARGINAL,
	CAMEL_CERT_TRUST_FULLY,
	CAMEL_CERT_TRUST_ULTIMATE,
	CAMEL_CERT_TRUST_TEMPORARY
} CamelCertTrust;

typedef struct {
	guint32 refcount;

	gchar *issuer;
	gchar *subject;
	gchar *hostname;
	gchar *fingerprint;

	CamelCertTrust trust;
	GByteArray *rawcert;
} CamelCert;

/**
 * CamelCertDBLock:
 *
 * Since: 2.32
 **/
typedef enum {
	CAMEL_CERTDB_DB_LOCK,
	CAMEL_CERTDB_IO_LOCK,
	CAMEL_CERTDB_ALLOC_LOCK,
	CAMEL_CERTDB_REF_LOCK
} CamelCertDBLock;

struct _CamelCertDB {
	CamelObject parent;
	CamelCertDBPrivate *priv;

	gchar *filename;
	guint32 version;
	guint32 saved_certs;
	CamelCertDBFlags flags;

	guint32 cert_size;

	CamelMemChunk *cert_chunks;

	GPtrArray *certs;
	GHashTable *cert_hash;
};

struct _CamelCertDBClass {
	CamelObjectClass parent_class;

	gint (*header_load) (CamelCertDB *certdb, FILE *istream);
	gint (*header_save) (CamelCertDB *certdb, FILE *ostream);

	CamelCert * (*cert_load) (CamelCertDB *certdb, FILE *istream);
	gint (*cert_save) (CamelCertDB *certdb, CamelCert *cert, FILE *ostream);

	CamelCert *  (*cert_new) (CamelCertDB *certdb);
	void        (*cert_free) (CamelCertDB *certdb, CamelCert *cert);

	const gchar * (*cert_get_string) (CamelCertDB *certdb, CamelCert *cert, gint string);
	void (*cert_set_string) (CamelCertDB *certdb, CamelCert *cert, gint string, const gchar *value);
};

GType camel_certdb_get_type (void);

CamelCertDB *camel_certdb_new (void);

void camel_certdb_set_default (CamelCertDB *certdb);
CamelCertDB *camel_certdb_get_default (void);

void camel_certdb_set_filename (CamelCertDB *certdb, const gchar *filename);

gint camel_certdb_load (CamelCertDB *certdb);
gint camel_certdb_save (CamelCertDB *certdb);

void camel_certdb_touch (CamelCertDB *certdb);

/* The lookup key was changed from fingerprint to hostname to fix bug 606181. */

/* Get the certificate for the given hostname, if any. */
CamelCert *camel_certdb_get_host (CamelCertDB *certdb, const gchar *hostname, const gchar *fingerprint);

/* Store cert for cert->hostname, replacing any existing certificate for the
 * same hostname. */
void camel_certdb_put (CamelCertDB *certdb, CamelCert *cert);

/* Remove any user-accepted certificate for the given hostname. */
void camel_certdb_remove_host (CamelCertDB *certdb, const gchar *hostname, const gchar *fingerprint);

CamelCert *camel_certdb_cert_new (CamelCertDB *certdb);
void camel_certdb_cert_ref (CamelCertDB *certdb, CamelCert *cert);
void camel_certdb_cert_unref (CamelCertDB *certdb, CamelCert *cert);

void camel_certdb_clear (CamelCertDB *certdb);

const gchar *camel_cert_get_string (CamelCertDB *certdb, CamelCert *cert, gint string);
void camel_cert_set_string (CamelCertDB *certdb, CamelCert *cert, gint string, const gchar *value);

#define camel_cert_get_issuer(certdb,cert) camel_cert_get_string (certdb, cert, CAMEL_CERT_STRING_ISSUER)
#define camel_cert_get_subject(certdb,cert) camel_cert_get_string (certdb, cert, CAMEL_CERT_STRING_SUBJECT)
#define camel_cert_get_hostname(certdb,cert) camel_cert_get_string (certdb, cert, CAMEL_CERT_STRING_HOSTNAME)
#define camel_cert_get_fingerprint(certdb,cert) camel_cert_get_string (certdb, cert, CAMEL_CERT_STRING_FINGERPRINT)

#define camel_cert_set_issuer(certdb,cert,issuer) camel_cert_set_string (certdb, cert, CAMEL_CERT_STRING_ISSUER, issuer)
#define camel_cert_set_subject(certdb,cert,subject) camel_cert_set_string (certdb, cert, CAMEL_CERT_STRING_SUBJECT, subject)
#define camel_cert_set_hostname(certdb,cert,hostname) camel_cert_set_string (certdb, cert, CAMEL_CERT_STRING_HOSTNAME, hostname)
#define camel_cert_set_fingerprint(certdb,cert,fingerprint) camel_cert_set_string (certdb, cert, CAMEL_CERT_STRING_FINGERPRINT, fingerprint)

CamelCertTrust camel_cert_get_trust (CamelCertDB *certdb, CamelCert *cert);
void camel_cert_set_trust (CamelCertDB *certdb, CamelCert *cert, CamelCertTrust trust);

void camel_certdb_lock	 (CamelCertDB *certdb, CamelCertDBLock lock);
void camel_certdb_unlock (CamelCertDB *certdb, CamelCertDBLock lock);

G_END_DECLS

#endif /* CAMEL_CERTDB_H */
