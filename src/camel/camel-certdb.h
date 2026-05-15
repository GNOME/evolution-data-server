/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_CERTDB_H
#define CAMEL_CERTDB_H

#include <stdio.h>
#include <glib-object.h>

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
	CAMEL_CERT_TRUST_UNKNOWN,
	CAMEL_CERT_TRUST_NEVER,
	CAMEL_CERT_TRUST_MARGINAL,
	CAMEL_CERT_TRUST_FULLY,
	CAMEL_CERT_TRUST_ULTIMATE,
	CAMEL_CERT_TRUST_TEMPORARY
} CamelCertTrust;

typedef struct {
	volatile gint refcount;

	gchar *issuer;
	gchar *subject;
	gchar *hostname;
	gchar *fingerprint;

	CamelCertTrust trust;
	GBytes *rawcert; /* loaded on demand, with camel_cert_load_cert_file() */
} CamelCert;

struct _CamelCertDB {
	GObject parent;
	CamelCertDBPrivate *priv;
};

struct _CamelCertDBClass {
	GObjectClass parent_class;

	gint		(*header_load)		(CamelCertDB *certdb,
						 FILE *istream);
	gint		(*header_save)		(CamelCertDB *certdb,
						 FILE *ostream);

	CamelCert *	(*cert_load)		(CamelCertDB *certdb,
						 FILE *istream);
	gint		(*cert_save)		(CamelCertDB *certdb,
						 CamelCert *cert,
						 FILE *ostream);

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_cert_get_type		(void) G_GNUC_CONST;
CamelCert *	camel_cert_new			(void);
CamelCert *	camel_cert_ref			(CamelCert *cert);
void		camel_cert_unref		(CamelCert *cert);
gboolean	camel_cert_load_cert_file	(CamelCert *cert,
						 GError **error);
gboolean	camel_cert_save_cert_file	(CamelCert *cert,
						 const GByteArray *der_data,
						 GError **error);

GType		camel_certdb_get_type		(void) G_GNUC_CONST;
CamelCertDB *	camel_certdb_new		(void);
void		camel_certdb_set_default	(CamelCertDB *certdb);
CamelCertDB *	camel_certdb_get_default	(void);
void		camel_certdb_set_filename	(CamelCertDB *certdb,
						 const gchar *filename);
gint		camel_certdb_load		(CamelCertDB *certdb);
gint		camel_certdb_save		(CamelCertDB *certdb);
void		camel_certdb_touch		(CamelCertDB *certdb);

/* The lookup key was changed from fingerprint to hostname to fix bug 606181. */

/* Get the certificate for the given hostname, if any. */
CamelCert *	camel_certdb_get_host		(CamelCertDB *certdb,
						 const gchar *hostname,
						 const gchar *fingerprint);

/* Store cert for cert->hostname, replacing any existing certificate for the
 * same hostname. */
void		camel_certdb_put		(CamelCertDB *certdb,
						 CamelCert *cert);

/* Remove any user-accepted certificate for the given hostname. */
void		camel_certdb_remove_host	(CamelCertDB *certdb,
						 const gchar *hostname,
						 const gchar *fingerprint);

void		camel_certdb_clear		(CamelCertDB *certdb);

GSList *	camel_certdb_list_certs		(CamelCertDB *certdb);

G_END_DECLS

#endif /* CAMEL_CERTDB_H */
