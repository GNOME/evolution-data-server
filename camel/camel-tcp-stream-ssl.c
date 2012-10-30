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

/* NOTE: This is the default implementation of CamelTcpStreamSSL,
 * used when the Mozilla NSS libraries are used.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <nspr.h>
#include <prio.h>
#include <prerror.h>
#include <prerr.h>
#include <secerr.h>
#include <sslerr.h>
#include "nss.h"    /* Don't use <> here or it will include the system nss.h instead */
#include <ssl.h>
#include <cert.h>
#include <certdb.h>
#include <pk11func.h>

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include "camel-certdb.h"
#include "camel-file-utils.h"
#include "camel-net-utils.h"
#include "camel-operation.h"
#include "camel-session.h"
#include "camel-stream-fs.h"
#include "camel-tcp-stream-ssl.h"

#ifdef G_OS_WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#define d(x)

#define IO_TIMEOUT (PR_TicksPerSecond() * 4 * 60)
#define CONNECT_TIMEOUT (PR_TicksPerSecond () * 4 * 60)

#define CAMEL_TCP_STREAM_SSL_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_TCP_STREAM_SSL, CamelTcpStreamSSLPrivate))

struct _CamelTcpStreamSSLPrivate {
	CamelSession *session;
	gchar *expected_host;
	gboolean ssl_mode;
	CamelTcpStreamSSLFlags flags;
};

G_DEFINE_TYPE (CamelTcpStreamSSL, camel_tcp_stream_ssl, CAMEL_TYPE_TCP_STREAM_RAW)

static const gchar *
tcp_stream_ssl_get_cert_dir (void)
{
	static gchar *cert_dir = NULL;

	if (G_UNLIKELY (cert_dir == NULL)) {
		const gchar *data_dir;
		const gchar *home_dir;
		gchar *old_dir;

		home_dir = g_get_home_dir ();
		data_dir = g_get_user_data_dir ();

		cert_dir = g_build_filename (data_dir, "camel_certs", NULL);

		/* Move the old certificate directory if present. */
		old_dir = g_build_filename (home_dir, ".camel_certs", NULL);
		if (g_file_test (old_dir, G_FILE_TEST_IS_DIR))
			g_rename (old_dir, cert_dir);
		g_free (old_dir);

		g_mkdir_with_parents (cert_dir, 0700);
	}

	return cert_dir;
}

static void
tcp_stream_ssl_dispose (GObject *object)
{
	CamelTcpStreamSSLPrivate *priv;

	priv = CAMEL_TCP_STREAM_SSL_GET_PRIVATE (object);

	if (priv->session != NULL) {
		g_object_unref (priv->session);
		priv->session = NULL;
	}

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_tcp_stream_ssl_parent_class)->dispose (object);
}

static void
tcp_stream_ssl_finalize (GObject *object)
{
	CamelTcpStreamSSLPrivate *priv;

	priv = CAMEL_TCP_STREAM_SSL_GET_PRIVATE (object);

	g_free (priv->expected_host);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_tcp_stream_ssl_parent_class)->finalize (object);
}

#if 0
/* Since this is default implementation, let NSS handle it. */
static SECStatus
ssl_get_client_auth (gpointer data,
                     PRFileDesc *sockfd,
                     struct CERTDistNamesStr *caNames,
                     struct CERTCertificateStr **pRetCert,
                     struct SECKEYPrivateKeyStr **pRetKey)
{
	SECStatus status = SECFailure;
	SECKEYPrivateKey *privkey;
	CERTCertificate *cert;
	gpointer proto_win;

	proto_win = SSL_RevealPinArg (sockfd);

	if ((gchar *) data) {
		cert = PK11_FindCertFromNickname ((gchar *) data, proto_win);
		if (cert) {
			privKey = PK11_FindKeyByAnyCert (cert, proto_win);
			if (privkey) {
				status = SECSuccess;
			} else {
				CERT_DestroyCertificate (cert);
			}
		}
	} else {
		/* no nickname given, automatically find the right cert */
		CERTCertNicknames *names;
		gint i;

		names = CERT_GetCertNicknames (
			CERT_GetDefaultCertDB (),
			SEC_CERT_NICKNAMES_USER,
			proto_win);

		if (names != NULL) {
			for (i = 0; i < names->numnicknames; i++) {
				cert = PK11_FindCertFromNickname (
					names->nicknames[i], proto_win);
				if (!cert)
					continue;

				/* Only check unexpired certs */
				if (CERT_CheckCertValidTimes (cert, PR_Now (), PR_FALSE) != secCertTimeValid) {
					CERT_DestroyCertificate (cert);
					continue;
				}

				status = NSS_CmpCertChainWCANames (cert, caNames);
				if (status == SECSuccess) {
					privkey = PK11_FindKeyByAnyCert (cert, proto_win);
					if (privkey)
						break;

					status = SECFailure;
					break;
				}

				CERT_FreeNicknames (names);
			}
		}
	}

	if (status == SECSuccess) {
		*pRetCert = cert;
		*pRetKey  = privkey;
	}

	return status;
}
#endif

#if 0
/* Since this is the default NSS implementation, no need for us to use this. */
static SECStatus
ssl_auth_cert (gpointer data,
               PRFileDesc *sockfd,
               PRBool checksig,
               PRBool is_server)
{
	CERTCertificate *cert;
	SECStatus status;
	gpointer pinarg;
	gchar *host;

	cert = SSL_PeerCertificate (sockfd);
	pinarg = SSL_RevealPinArg (sockfd);
	status = CERT_VerifyCertNow (
		(CERTCertDBHandle *) data, cert,
		checksig, certUsageSSLClient, pinarg);

	if (status != SECSuccess)
		return SECFailure;

	/* Certificate is OK.  Since this is the client side of an SSL
	 * connection, we need to verify that the name field in the cert
	 * matches the desired hostname.  This is our defense against
	 * man-in-the-middle attacks.
	 */

	/* SSL_RevealURL returns a hostname, not a URL. */
	host = SSL_RevealURL (sockfd);

	if (host && *host) {
		status = CERT_VerifyCertName (cert, host);
	} else {
		PR_SetError (SSL_ERROR_BAD_CERT_DOMAIN, 0);
		status = SECFailure;
	}

	if (host)
		PR_Free (host);

	return secStatus;
}
#endif

CamelCert *camel_certdb_nss_cert_get (CamelCertDB *certdb, CERTCertificate *cert, const gchar *hostname);
CamelCert *camel_certdb_nss_cert_convert (CamelCertDB *certdb, CERTCertificate *cert);
void camel_certdb_nss_cert_set (CamelCertDB *certdb, CamelCert *ccert, CERTCertificate *cert);

static gchar *
cert_fingerprint (CERTCertificate *cert)
{
	GChecksum *checksum;
	guint8 *digest;
	gsize length;
	guchar fingerprint[50], *f;
	gint i;
	const gchar tohex[16] = "0123456789abcdef";

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, cert->derCert.data, cert->derCert.len);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	for (i = 0,f = fingerprint; i < length; i++) {
		guint c = digest[i];

		*f++ = tohex[(c >> 4) & 0xf];
		*f++ = tohex[c & 0xf];
#ifndef G_OS_WIN32
		*f++ = ':';
#else
		/* The fingerprint is used as a file name, can't have
		 * colons in file names. Use underscore instead.
		 */
		*f++ = '_';
#endif
	}

	fingerprint[47] = 0;

	return g_strdup ((gchar *) fingerprint);
}

/* lookup a cert uses fingerprint to index an on-disk file */
CamelCert *
camel_certdb_nss_cert_get (CamelCertDB *certdb,
                           CERTCertificate *cert,
                           const gchar *hostname)
{
	gchar *fingerprint;
	CamelCert *ccert;

	fingerprint = cert_fingerprint (cert);

	ccert = camel_certdb_get_host (certdb, hostname, fingerprint);
	if (ccert == NULL) {
		g_free (fingerprint);
		return NULL;
	}

	if (ccert->rawcert == NULL) {
		GByteArray *array;
		gchar *filename;
		gchar *contents;
		gsize length;
		const gchar *cert_dir;
		GError *error = NULL;

		cert_dir = tcp_stream_ssl_get_cert_dir ();
		filename = g_build_filename (cert_dir, fingerprint, NULL);
		if (!g_file_get_contents (filename, &contents, &length, &error) ||
		    error != NULL) {
			g_warning (
				"Could not load cert %s: %s",
				filename, error ? error->message : "Unknown error");
			g_clear_error (&error);

			/* failed to load the certificate, thus remove it from
			 * the CertDB, thus it can be re-added and properly saved */
			camel_certdb_remove_host (certdb, hostname, fingerprint);
			camel_certdb_touch (certdb);
			g_free (fingerprint);
			g_free (filename);

			return ccert;
		}
		g_free (filename);

		array = g_byte_array_sized_new (length);
		g_byte_array_append (array, (guint8 *) contents, length);
		g_free (contents);

		ccert->rawcert = array;
	}

	g_free (fingerprint);
	if (ccert->rawcert->len != cert->derCert.len
	    || memcmp (ccert->rawcert->data, cert->derCert.data, cert->derCert.len) != 0) {
		g_warning ("rawcert != derCer");
		camel_cert_set_trust (certdb, ccert, CAMEL_CERT_TRUST_UNKNOWN);
		camel_certdb_touch (certdb);
	}

	return ccert;
}

/* Create a CamelCert corresponding to the NSS cert, with unknown trust. */
CamelCert *
camel_certdb_nss_cert_convert (CamelCertDB *certdb,
                               CERTCertificate *cert)
{
	CamelCert *ccert;
	gchar *fingerprint;

	fingerprint = cert_fingerprint (cert);

	ccert = camel_certdb_cert_new (certdb);
	camel_cert_set_issuer (certdb, ccert, CERT_NameToAscii (&cert->issuer));
	camel_cert_set_subject (certdb, ccert, CERT_NameToAscii (&cert->subject));
	/* hostname is set in caller */
	/*camel_cert_set_hostname(certdb, ccert, ssl->priv->expected_host);*/
	camel_cert_set_fingerprint (certdb, ccert, fingerprint);
	camel_cert_set_trust (certdb, ccert, CAMEL_CERT_TRUST_UNKNOWN);
	g_free (fingerprint);

	return ccert;
}

/* set the 'raw' cert (& save it) */
void
camel_certdb_nss_cert_set (CamelCertDB *certdb,
                           CamelCert *ccert,
                           CERTCertificate *cert)
{
	gchar *filename, *fingerprint;
	CamelStream *stream;
	const gchar *cert_dir;

	fingerprint = ccert->fingerprint;

	if (ccert->rawcert == NULL)
		ccert->rawcert = g_byte_array_new ();

	g_byte_array_set_size (ccert->rawcert, cert->derCert.len);
	memcpy (ccert->rawcert->data, cert->derCert.data, cert->derCert.len);

	cert_dir = tcp_stream_ssl_get_cert_dir ();
	filename = g_build_filename (cert_dir, fingerprint, NULL);

	stream = camel_stream_fs_new_with_name (
		filename, O_WRONLY | O_CREAT | O_TRUNC, 0600, NULL);
	if (stream != NULL) {
		if (camel_stream_write (
			stream, (const gchar *) ccert->rawcert->data,
			ccert->rawcert->len, NULL, NULL) == -1) {
			g_warning (
				"Could not save cert: %s: %s",
				filename, g_strerror (errno));
			g_unlink (filename);
		}
		camel_stream_close (stream, NULL, NULL);
		g_object_unref (stream);
	} else {
		g_warning (
			"Could not save cert: %s: %s",
			filename, g_strerror (errno));
	}

	g_free (filename);
}

#if 0
/* used by the mozilla-like code below */
static gchar *
get_nickname (CERTCertificate *cert)
{
	gchar *server, *nick = NULL;
	gint i;
	PRBool status = PR_TRUE;

	server = CERT_GetCommonName (&cert->subject);
	if (server == NULL)
		return NULL;

	for (i = 1; status == PR_TRUE; i++) {
		if (nick) {
			g_free (nick);
			nick = g_strdup_printf ("%s #%d", server, i);
		} else {
			nick = g_strdup (server);
		}
		status = SEC_CertNicknameConflict (server, &cert->derSubject, cert->dbhandle);
	}

	return nick;
}
#endif

static void
tcp_stream_cancelled (GCancellable *cancellable,
                      PRThread *thread)
{
	PR_Interrupt (thread);
}

static SECStatus
ssl_bad_cert (gpointer data,
              PRFileDesc *sockfd)
{
	gboolean accept;
	CamelCertDB *certdb = NULL;
	CamelCert *ccert = NULL;
	gboolean ccert_is_new = FALSE;
	gchar *prompt, *cert_str, *fingerprint;
	CamelTcpStreamSSL *ssl;
	CERTCertificate *cert;
	SECStatus status = SECFailure;

	g_return_val_if_fail (data != NULL, SECFailure);
	g_return_val_if_fail (CAMEL_IS_TCP_STREAM_SSL (data), SECFailure);

	ssl = data;

	cert = SSL_PeerCertificate (sockfd);
	if (cert == NULL)
		return SECFailure;

	certdb = camel_certdb_get_default ();
	ccert = camel_certdb_nss_cert_get (certdb, cert, ssl->priv->expected_host);
	if (ccert == NULL) {
		ccert = camel_certdb_nss_cert_convert (certdb, cert);
		camel_cert_set_hostname (certdb, ccert, ssl->priv->expected_host);
		/* Don't put in the certdb yet.  Since we can only store one
		 * entry per hostname, we'd rather not ruin any existing entry
		 * for this hostname if the user rejects the new certificate. */
		ccert_is_new = TRUE;
	}

	if (ccert->trust == CAMEL_CERT_TRUST_UNKNOWN) {
		GSList *button_captions = NULL;
		gint button_id;

		status = CERT_VerifyCertNow (cert->dbhandle, cert, TRUE, certUsageSSLClient, NULL);
		fingerprint = cert_fingerprint (cert);
		cert_str = g_strdup_printf (_(
			"   Issuer:       %s\n"
			"   Subject:      %s\n"
			"   Fingerprint:  %s\n"
			"   Signature:    %s"),
			CERT_NameToAscii (&cert->issuer),
			CERT_NameToAscii (&cert->subject),
			fingerprint,
			status == SECSuccess ? _("GOOD") : _("BAD"));
		g_free (fingerprint);

		/* construct our user prompt */
		prompt = g_strdup_printf (
			_("SSL Certificate for '%s' is not trusted. "
			"Do you wish to accept it?\n\n"
			"Detailed information about the certificate:\n%s"),
			ssl->priv->expected_host, cert_str);
		g_free (cert_str);

		button_captions = g_slist_append (button_captions, _("_Reject"));
		button_captions = g_slist_append (button_captions, _("Accept _Temporarily"));
		button_captions = g_slist_append (button_captions, _("_Accept Permanently"));

		/* query the user to find out if we want to accept this certificate */
		button_id = camel_session_alert_user (ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, button_captions);
		g_slist_free (button_captions);
		g_free (prompt);

		accept = button_id != 0;
		if (ccert_is_new) {
			camel_certdb_nss_cert_set (certdb, ccert, cert);
			camel_certdb_put (certdb, ccert);
		}

		switch (button_id) {
		case 0: /* Reject */
			camel_cert_set_trust (certdb, ccert, CAMEL_CERT_TRUST_NEVER);
			break;
		case 1: /* Accept temporarily */
			camel_cert_set_trust (certdb, ccert, CAMEL_CERT_TRUST_TEMPORARY);
			break;
		case 2: /* Accept permanently */
			camel_cert_set_trust (certdb, ccert, CAMEL_CERT_TRUST_FULLY);
			break;
		default: /* anything else means failure and will ask again */
			accept = FALSE;
			break;
		}
		camel_certdb_touch (certdb);
	} else {
		accept = ccert->trust != CAMEL_CERT_TRUST_NEVER;
	}

	camel_certdb_cert_unref (certdb, ccert);
	camel_certdb_save (certdb);
	g_object_unref (certdb);

	return accept ? SECSuccess : SECFailure;

#if 0
	gint i, error;
	CERTCertTrust trust;
	SECItem *certs[1];
	gint go = 1;
	gchar *host, *nick;

	error = PR_GetError ();

	/* This code is basically what mozilla does - however it doesn't seem to work here
	 * very reliably :-/ */
	while (go && status != SECSuccess) {
		gchar *prompt = NULL;

		printf ("looping, error '%d'\n", error);

		switch (error) {
		case SEC_ERROR_UNKNOWN_ISSUER:
		case SEC_ERROR_CA_CERT_INVALID:
		case SEC_ERROR_UNTRUSTED_ISSUER:
		case SEC_ERROR_EXPIRED_ISSUER_CERTIFICATE:
			/* add certificate */
			printf ("unknown issuer, adding ... \n");
			prompt = g_strdup_printf (_("Certificate problem: %s\nIssuer: %s"), cert->subjectName, cert->issuerName);

			if (camel_session_alert_user (ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE)) {

				nick = get_nickname (cert);
				if (NULL == nick) {
					g_free (prompt);
					status = SECFailure;
					break;
				}

				printf ("adding cert '%s'\n", nick);

				if (!cert->trust) {
					cert->trust = (CERTCertTrust *) PORT_ArenaZAlloc (cert->arena, sizeof (CERTCertTrust));
					CERT_DecodeTrustString (cert->trust, "P");
				}

				certs[0] = &cert->derCert;
				/*CERT_ImportCerts (cert->dbhandle, certUsageSSLServer, 1, certs, NULL, TRUE, FALSE, nick);*/
				CERT_ImportCerts (cert->dbhandle, certUsageUserCertImport, 1, certs, NULL, TRUE, FALSE, nick);
				g_free (nick);

				printf (" cert type %08x\n", cert->nsCertType);

				memset ((gpointer) &trust, 0, sizeof (trust));
				if (CERT_GetCertTrust (cert, &trust) != SECSuccess) {
					CERT_DecodeTrustString (&trust, "P");
				}
				trust.sslFlags |= CERTDB_VALID_PEER | CERTDB_TRUSTED;
				if (CERT_ChangeCertTrust (cert->dbhandle, cert, &trust) != SECSuccess) {
					printf ("couldn't change cert trust?\n");
				}

				/*status = SECSuccess;*/
#if 1
				/* re-verify? */
				status = CERT_VerifyCertNow (cert->dbhandle, cert, TRUE, certUsageSSLServer, NULL);
				error = PR_GetError ();
				printf ("re-verify status %d, error %d\n", status, error);
#endif

				printf (" cert type %08x\n", cert->nsCertType);
			} else {
				printf ("failed/cancelled\n");
				go = 0;
			}

			break;
		case SSL_ERROR_BAD_CERT_DOMAIN:
			printf ("bad domain\n");

			prompt = g_strdup_printf (_("Bad certificate domain: %s\nIssuer: %s"), cert->subjectName, cert->issuerName);

			if (camel_session_alert_user (ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE)) {
				host = SSL_RevealURL (sockfd);
				status = CERT_AddOKDomainName (cert, host);
				printf ("add ok domain name : %s\n", status == SECFailure?"fail":"ok");
				error = PR_GetError ();
				if (status == SECFailure)
					go = 0;
			} else {
				go = 0;
			}

			break;

		case SEC_ERROR_EXPIRED_CERTIFICATE:
			printf ("expired\n");

			prompt = g_strdup_printf (_("Certificate expired: %s\nIssuer: %s"), cert->subjectName, cert->issuerName);

			if (camel_session_alert_user (ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE)) {
				cert->timeOK = PR_TRUE;
				status = CERT_VerifyCertNow (cert->dbhandle, cert, TRUE, certUsageSSLClient, NULL);
				error = PR_GetError ();
				if (status == SECFailure)
					go = 0;
			} else {
				go = 0;
			}

			break;

		case SEC_ERROR_CRL_EXPIRED:
			printf ("crl expired\n");

			prompt = g_strdup_printf (_("Certificate revocation list expired: %s\nIssuer: %s"), cert->subjectName, cert->issuerName);

			if (camel_session_alert_user (ssl->priv->session, CAMEL_SESSION_ALERT_WARNING, prompt, TRUE)) {
				host = SSL_RevealURL (sockfd);
				status = CERT_AddOKDomainName (cert, host);
			}

			go = 0;
			break;

		default:
			printf ("generic error\n");
			go = 0;
			break;
		}

		g_free (prompt);
	}

	CERT_DestroyCertificate (cert);

	return status;
#endif
}

static PRFileDesc *
enable_ssl (CamelTcpStreamSSL *ssl,
            PRFileDesc *fd)
{
	PRFileDesc *ssl_fd;

	g_assert (fd != NULL);

	ssl_fd = SSL_ImportFD (NULL, fd);
	if (!ssl_fd)
		return NULL;

	SSL_OptionSet (ssl_fd, SSL_SECURITY, PR_TRUE);

	if (ssl->priv->flags & CAMEL_TCP_STREAM_SSL_ENABLE_SSL2) {
		SSL_OptionSet (ssl_fd, SSL_ENABLE_SSL2, PR_TRUE);
		SSL_OptionSet (ssl_fd, SSL_V2_COMPATIBLE_HELLO, PR_TRUE);
	} else {
		SSL_OptionSet (ssl_fd, SSL_ENABLE_SSL2, PR_FALSE);
		SSL_OptionSet (ssl_fd, SSL_V2_COMPATIBLE_HELLO, PR_FALSE);
	}

	if (ssl->priv->flags & CAMEL_TCP_STREAM_SSL_ENABLE_SSL3)
		SSL_OptionSet (ssl_fd, SSL_ENABLE_SSL3, PR_TRUE);
	else
		SSL_OptionSet (ssl_fd, SSL_ENABLE_SSL3, PR_FALSE);

	if (ssl->priv->flags & CAMEL_TCP_STREAM_SSL_ENABLE_TLS)
		SSL_OptionSet (ssl_fd, SSL_ENABLE_TLS, PR_TRUE);
	else
		SSL_OptionSet (ssl_fd, SSL_ENABLE_TLS, PR_FALSE);

	SSL_SetURL (ssl_fd, ssl->priv->expected_host);

	/* NSS provides a default implementation for the SSL_GetClientAuthDataHook callback
	 * but does not enable it by default. It must be explicltly requested by the application.
	 * See: http://www.mozilla.org/projects/security/pki/nss/ref/ssl/sslfnc.html#1126622 */
	SSL_GetClientAuthDataHook (ssl_fd, (SSLGetClientAuthData) &NSS_GetClientAuthData, NULL );

	/* NSS provides _and_ installs a default implementation for the
	 * SSL_AuthCertificateHook callback so we _don't_ need to install one. */
	SSL_BadCertHook (ssl_fd, ssl_bad_cert, ssl);

	return ssl_fd;
}

static PRFileDesc *
enable_ssl_or_close_fd (CamelTcpStreamSSL *ssl,
                        PRFileDesc *fd,
                        GError **error)
{
	PRFileDesc *ssl_fd;

	ssl_fd = enable_ssl (ssl, fd);
	if (ssl_fd == NULL) {
		gint errnosave;

		_set_errno_from_pr_error (PR_GetError ());
		errnosave = errno;
		PR_Shutdown (fd, PR_SHUTDOWN_BOTH);
		PR_Close (fd);
		errno = errnosave;
		_set_g_error_from_errno (error, FALSE);

		return NULL;
	}

	return ssl_fd;
}

static gboolean
rehandshake_ssl (PRFileDesc *fd,
                 GCancellable *cancellable,
                 GError **error)
{
	SECStatus status = SECSuccess;
	gulong cancel_id = 0;

	if (g_cancellable_set_error_if_cancelled (cancellable, error))
		return FALSE;

	if (G_IS_CANCELLABLE (cancellable))
		cancel_id = g_cancellable_connect (
			cancellable, G_CALLBACK (tcp_stream_cancelled),
			PR_GetCurrentThread (), (GDestroyNotify) NULL);

	if (status == SECSuccess)
		status = SSL_ResetHandshake (fd, FALSE);

	if (status == SECSuccess)
		status = SSL_ForceHandshake (fd);

	if (cancel_id > 0)
		g_cancellable_disconnect (cancellable, cancel_id);

	if (g_cancellable_set_error_if_cancelled (cancellable, error)) {
		status = SECFailure;

	} else if (status == SECFailure) {
		_set_error_from_pr_error (error);
	}

	return (status == SECSuccess);
}

static gint
tcp_stream_ssl_connect (CamelTcpStream *stream,
                        const gchar *host,
                        const gchar *service,
                        gint fallback_port,
                        GCancellable *cancellable,
                        GError **error)
{
	CamelTcpStreamSSL *ssl = CAMEL_TCP_STREAM_SSL (stream);
	gint retval;

	retval = CAMEL_TCP_STREAM_CLASS (
		camel_tcp_stream_ssl_parent_class)->connect (
		stream, host, service, fallback_port, cancellable, error);
	if (retval != 0)
		return retval;

	if (ssl->priv->ssl_mode) {
		PRFileDesc *fd;
		PRFileDesc *ssl_fd;

		d (g_print ("  enabling SSL\n"));

		fd = camel_tcp_stream_get_file_desc (stream);
		ssl_fd = enable_ssl_or_close_fd (ssl, fd, error);
		_camel_tcp_stream_raw_replace_file_desc (CAMEL_TCP_STREAM_RAW (stream), ssl_fd);

		if (!ssl_fd) {
			d (g_print ("  could not enable SSL\n"));
			return -1;
		} else {
			d (g_print ("  re-handshaking SSL\n"));

			if (!rehandshake_ssl (ssl_fd, cancellable, error)) {
				d (g_print ("  failed\n"));
				return -1;
			}
		}
	}

	return 0;
}

static void
camel_tcp_stream_ssl_class_init (CamelTcpStreamSSLClass *class)
{
	GObjectClass *object_class;
	CamelTcpStreamClass *tcp_stream_class;

	g_type_class_add_private (class, sizeof (CamelTcpStreamSSLPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = tcp_stream_ssl_dispose;
	object_class->finalize = tcp_stream_ssl_finalize;

	tcp_stream_class = CAMEL_TCP_STREAM_CLASS (class);
	tcp_stream_class->connect = tcp_stream_ssl_connect;
}

static void
camel_tcp_stream_ssl_init (CamelTcpStreamSSL *stream)
{
	stream->priv = CAMEL_TCP_STREAM_SSL_GET_PRIVATE (stream);
}

/**
 * camel_tcp_stream_ssl_new:
 * @session: an active #CamelSession object
 * @expected_host: host that the stream is expected to connect with
 * @flags: a bitwise combination of #CamelTcpStreamSSLFlags
 *
 * Since the SSL certificate authenticator may need to prompt the
 * user, a #CamelSession is needed. @expected_host is needed as a
 * protection against an MITM attack.
 *
 * Returns: a new #CamelTcpStreamSSL stream preset in SSL mode
 **/
CamelStream *
camel_tcp_stream_ssl_new (CamelSession *session,
                          const gchar *expected_host,
                          CamelTcpStreamSSLFlags flags)
{
	CamelTcpStreamSSL *stream;

	g_assert (CAMEL_IS_SESSION (session));

	stream = g_object_new (CAMEL_TYPE_TCP_STREAM_SSL, NULL);

	stream->priv->session = g_object_ref (session);
	stream->priv->expected_host = g_strdup (expected_host);
	stream->priv->ssl_mode = TRUE;
	stream->priv->flags = flags;

	return CAMEL_STREAM (stream);
}

/**
 * camel_tcp_stream_ssl_new_raw:
 * @session: an active #CamelSession object
 * @expected_host: host that the stream is expected to connect with
 * @flags: a bitwise combination of #CamelTcpStreamSSLFlags
 *
 * Since the SSL certificate authenticator may need to prompt the
 * user, a CamelSession is needed. @expected_host is needed as a
 * protection against an MITM attack.
 *
 * Returns: a new #CamelTcpStreamSSL stream not yet toggled into SSL mode
 **/
CamelStream *
camel_tcp_stream_ssl_new_raw (CamelSession *session,
                              const gchar *expected_host,
                              CamelTcpStreamSSLFlags flags)
{
	CamelTcpStreamSSL *stream;

	g_assert (CAMEL_IS_SESSION (session));

	stream = g_object_new (CAMEL_TYPE_TCP_STREAM_SSL, NULL);

	stream->priv->session = g_object_ref (session);
	stream->priv->expected_host = g_strdup (expected_host);
	stream->priv->ssl_mode = FALSE;
	stream->priv->flags = flags;

	return CAMEL_STREAM (stream);
}

/**
 * camel_tcp_stream_ssl_enable_ssl:
 * @ssl: a #CamelTcpStreamSSL object
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Toggles an ssl-capable stream into ssl mode (if it isn't already).
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_tcp_stream_ssl_enable_ssl (CamelTcpStreamSSL *ssl,
                                 GCancellable *cancellable,
                                 GError **error)
{
	PRFileDesc *fd, *ssl_fd;

	g_return_val_if_fail (CAMEL_IS_TCP_STREAM_SSL (ssl), -1);

	fd = camel_tcp_stream_get_file_desc (CAMEL_TCP_STREAM (ssl));

	if (fd && !ssl->priv->ssl_mode) {
		if (!(ssl_fd = enable_ssl (ssl, fd))) {
			_set_error_from_pr_error (error);
			return -1;
		}

		_camel_tcp_stream_raw_replace_file_desc (CAMEL_TCP_STREAM_RAW (ssl), ssl_fd);
		ssl->priv->ssl_mode = TRUE;

		if (!rehandshake_ssl (ssl_fd, cancellable, error))
			return -1;
	}

	ssl->priv->ssl_mode = TRUE;

	return 0;
}
