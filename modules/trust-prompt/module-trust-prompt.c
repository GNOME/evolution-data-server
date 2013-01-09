/*
 * module-trust-prompt.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <glib/gi18n-lib.h>

#include <cert.h>

#include <libebackend/libebackend.h>
#include "trust-prompt.h"

/* Standard GObject macros */
#define E_TYPE_TRUST_PROMPT (e_trust_prompt_get_type ())
#define E_TRUST_PROMPT(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_TRUST_PROMPT, ETrustPrompt))

typedef struct _ETrustPrompt ETrustPrompt;
typedef struct _ETrustPromptClass ETrustPromptClass;

struct _ETrustPrompt {
	EUserPrompterServerExtension parent;

	gboolean nss_initialized;
};

struct _ETrustPromptClass {
	EUserPrompterServerExtensionClass parent_class;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_trust_prompt_get_type (void);

G_DEFINE_DYNAMIC_TYPE (ETrustPrompt, e_trust_prompt, E_TYPE_USER_PROMPTER_SERVER_EXTENSION)

static gboolean trust_prompt_show_trust_prompt (EUserPrompterServerExtension *extension,
						gint prompt_id,
						const ENamedParameters *parameters);

#define TRUST_PROMPT_DIALOG "ETrustPrompt::trust-prompt"

static void
trust_prompt_register_dialogs (EExtension *extension,
			       EUserPrompterServer *server)
{
	ETrustPrompt *trust_prompt = E_TRUST_PROMPT (extension);

	if (!trust_prompt->nss_initialized) {
		trust_prompt->nss_initialized = TRUE;

		/* Use camel_init() to initialise NSS consistently... */
		camel_init (e_get_user_data_dir (), TRUE);
	}

	e_user_prompter_server_register (server, extension, TRUST_PROMPT_DIALOG);
}

static gboolean
trust_prompt_prompt (EUserPrompterServerExtension *extension,
		     gint prompt_id,
		     const gchar *dialog_name,
		     const ENamedParameters *parameters)
{
	if (g_strcmp0 (dialog_name, TRUST_PROMPT_DIALOG) == 0)
		return trust_prompt_show_trust_prompt (extension, prompt_id, parameters);

	return FALSE;
}

static void
trust_prompt_finalize (GObject *object)
{
	ETrustPrompt *trust_prompt = E_TRUST_PROMPT (object);

	if (trust_prompt->nss_initialized)
		camel_shutdown ();

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_trust_prompt_parent_class)->finalize (object);
}

static void
e_trust_prompt_class_init (ETrustPromptClass *class)
{
	GObjectClass *object_class;
	EUserPrompterServerExtensionClass *server_extension_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = trust_prompt_finalize;

	server_extension_class = E_USER_PROMPTER_SERVER_EXTENSION_CLASS (class);
	server_extension_class->register_dialogs = trust_prompt_register_dialogs;
	server_extension_class->prompt = trust_prompt_prompt;
}

static void
e_trust_prompt_class_finalize (ETrustPromptClass *class)
{
}

static void
e_trust_prompt_init (ETrustPrompt *trust_prompt)
{
	trust_prompt->nss_initialized = FALSE;
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	e_trust_prompt_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

/* dialog definitions */

/* ETrustPrompt::trust-prompt
   The dialog expects these parameters:
      "host" - host from which the certificate is received
      "markup" - markup for the trust prompt, if not set, then "SSL certificate for '<b>host</b>' is not trusted. Do you wish to accept it?" is used
      "certificate" - a base64-encoded DER certificate, for which ask on trust
      "certificate-errors" - a hexa-decimal integer (as string) corresponding to GTlsCertificateFlags

   It can contain, optionally, chain of issuers:
      "issuer"   - a base64-encoded DER certificate, issuer of "certificate"
      "issuer-1" - a base64-encoded DER certificate, issuer of "issuer"
      "issuer-2" - a base64-encoded DER certificate, issuer of "issuer-1"
      and so on

   Result of the dialog is:
      0 - reject
      1 - accept permanently
      2 - accept temporarily
     -1 - user didn't choose any of the above

   The dialog doesn't provide any additional values in the response.
 */

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
		*f++ = ':';
	}

	fingerprint[47] = 0;

	return g_strdup ((gchar *) fingerprint);
}

static gchar *
cert_errors_to_reason (GTlsCertificateFlags flags)
{
	struct _convert_table {
		GTlsCertificateFlags flag;
		const gchar *description;
	} convert_table[] = {
		{ G_TLS_CERTIFICATE_UNKNOWN_CA,
		  N_("The signing certificate authority is not known.") },
		{ G_TLS_CERTIFICATE_BAD_IDENTITY,
		  N_("The certificate does not match the expected identity of the site that it was retrieved from.") },
		{ G_TLS_CERTIFICATE_NOT_ACTIVATED,
		  N_("The certificate's activation time is still in the future.") },
		{ G_TLS_CERTIFICATE_EXPIRED,
		  N_("The certificate has expired.") },
		{ G_TLS_CERTIFICATE_REVOKED,
		  N_("The certificate has been revoked according to the connection's certificate revocation list.") },
		{ G_TLS_CERTIFICATE_INSECURE,
		  N_("The certificate's algorithm is considered insecure.") }
	};

	GString *reason = g_string_new ("");
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (convert_table); ii++) {
		if ((flags & convert_table[ii].flag) != 0) {
			if (reason->len > 0)
				g_string_append (reason, "\n");

			g_string_append (reason, _(convert_table[ii].description));
		}
	}

	return g_string_free (reason, FALSE);
}

static void
trust_prompt_free_certificate (gpointer cert)
{
	if (!cert)
		return;

	CERT_DestroyCertificate (cert);
}

static GSList *
trust_prompt_get_issuers (CERTCertDBHandle *certdb,
			  const ENamedParameters *parameters)
{
	GSList *issuers = NULL;
	CERTCertificate *cert;
	SECItem derCert;
	gsize derCert_len = 0;
	gint ii;

	g_return_val_if_fail (certdb != NULL, NULL);
	g_return_val_if_fail (parameters != NULL, NULL);

	for (ii = 0; ii >= 0; ii++) {
		const gchar *base64_cert;

		if (ii == 0) {
			base64_cert = e_named_parameters_get (parameters, "issuer");
		} else {
			gchar *key;

			key = g_strdup_printf ("issuer-%d", ii);
			base64_cert = e_named_parameters_get (parameters, key);
			g_free (key);
		}

		if (!base64_cert)
			break;

		derCert.type = siDERCertBuffer;
		derCert.data = g_base64_decode (base64_cert, &derCert_len);
		if (!derCert.data)
			break;

		derCert.len = derCert_len;

		cert = CERT_NewTempCertificate (certdb, &derCert, NULL, PR_FALSE, PR_TRUE);
		g_free (derCert.data);

		if (!cert)
			break;

		issuers = g_slist_prepend (issuers, cert);
	}

	return g_slist_reverse (issuers);
}

static gboolean
trust_prompt_show_trust_prompt (EUserPrompterServerExtension *extension,
				gint prompt_id,
				const ENamedParameters *parameters)
{
	const gchar *host, *markup, *base64_cert, *cert_errs_str;
	gchar *fingerprint, *reason;
	gint64 cert_errs;
	CERTCertDBHandle *certdb;
	CERTCertificate *cert;
	GSList *issuers;
	SECItem derCert;
	gsize derCert_len = 0;
	gboolean success;

	g_return_val_if_fail (extension != NULL, FALSE);
	g_return_val_if_fail (parameters != NULL, FALSE);

	host = e_named_parameters_get (parameters, "host");
	markup = e_named_parameters_get (parameters, "markup");
	base64_cert = e_named_parameters_get (parameters, "certificate");
	cert_errs_str = e_named_parameters_get (parameters, "certificate-errors");

	g_return_val_if_fail (host != NULL, FALSE);
	g_return_val_if_fail (base64_cert != NULL, FALSE);
	g_return_val_if_fail (cert_errs_str != NULL, FALSE);

	derCert.type = siDERCertBuffer;
	derCert.data = g_base64_decode (base64_cert, &derCert_len);
	g_return_val_if_fail (derCert.data != NULL, FALSE);
	derCert.len = derCert_len;

	certdb = CERT_GetDefaultCertDB ();
	cert = CERT_NewTempCertificate (certdb, &derCert, NULL, PR_FALSE, PR_TRUE);
	g_return_val_if_fail (cert != NULL, FALSE);

	issuers = trust_prompt_get_issuers (certdb, parameters);

	cert_errs = g_ascii_strtoll (cert_errs_str, NULL, 16);
	reason = cert_errors_to_reason (cert_errs);
	fingerprint = cert_fingerprint (cert);

	success = trust_prompt_show (extension, prompt_id, host, markup, cert, fingerprint, reason, issuers);

	trust_prompt_free_certificate (cert);
	g_slist_free_full (issuers, trust_prompt_free_certificate);
	g_free (derCert.data);
	g_free (fingerprint);
	g_free (reason);

	return success;
}
