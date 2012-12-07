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

#include <gtk/gtk.h>
#include <glib/gi18n-lib.h>

#include <cert.h>

#include <libebackend/libebackend.h>

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
      "certificate" - a base64-encoded DER certificate, for which ask on trust
      "certificate-errors" - a hexa-decimal integer (as string) corresponding to GTlsCertificateFlags

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
trust_prompt_add_info_line (GtkGrid *grid,
			    const gchar *label_text,
			    const gchar *value_text,
			    gboolean ellipsize,
			    gint *at_row)
{
	GtkWidget *widget;
	PangoAttribute *attr;
	PangoAttrList *bold;

	g_return_if_fail (grid != NULL);
	g_return_if_fail (label_text != NULL);
	g_return_if_fail (at_row != NULL);

	if (!value_text || !*value_text)
		return;

	bold = pango_attr_list_new ();
	attr = pango_attr_weight_new (PANGO_WEIGHT_BOLD);
	pango_attr_list_insert (bold, attr);

	widget = gtk_label_new (label_text);
	gtk_misc_set_padding (GTK_MISC (widget), 12, 0);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.0);

	gtk_grid_attach (grid, widget, 1, *at_row, 1, 1);

	widget = gtk_label_new (value_text);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.0);
	g_object_set (G_OBJECT (widget),
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"justify", GTK_JUSTIFY_LEFT,
		"attributes", bold,
		"selectable", TRUE,
		"ellipsize", ellipsize ? PANGO_ELLIPSIZE_END : PANGO_ELLIPSIZE_NONE,
		NULL);

	gtk_grid_attach (grid, widget, 2, *at_row, 1, 1);

	*at_row = (*at_row) + 1;

	pango_attr_list_unref (bold);
}

#define TRUST_PROMP_ID_KEY "ETrustPrompt::prompt-id-key"

static void
trust_prompt_response_cb (GtkWidget *dialog,
			  gint response,
			  EUserPrompterServerExtension *extension)
{
	gint prompt_id;

	if (response == GTK_RESPONSE_HELP) {
		/* view certificate */
		return;
	}

	prompt_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (dialog), TRUST_PROMP_ID_KEY));
	gtk_widget_destroy (dialog);

	if (response == GTK_RESPONSE_REJECT)
		response = 0;
	else if (response == GTK_RESPONSE_ACCEPT)
		response = 1;
	else if (response == GTK_RESPONSE_YES)
		response = 2;
	else
		response = -1;

	e_user_prompter_server_extension_response (extension, prompt_id, response, NULL);
}

static gboolean
trust_prompt_show_trust_prompt (EUserPrompterServerExtension *extension,
				gint prompt_id,
				const ENamedParameters *parameters)
{
	const gchar *host, *base64_cert, *cert_errs_str;
	gchar *tmp, *reason, *issuer, *subject;
	gint row = 0;
	gint64 cert_errs;
	GtkWidget *dialog, *widget;
	GtkGrid *grid;
	CERTCertDBHandle *certdb;
	CERTCertificate *cert;
	SECItem derCert;
	gsize derCert_len = 0;

	g_return_val_if_fail (extension != NULL, FALSE);
	g_return_val_if_fail (parameters != NULL, FALSE);

	host = e_named_parameters_get (parameters, "host");
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

	cert_errs = g_ascii_strtoll (cert_errs_str, NULL, 16);

	dialog = gtk_dialog_new_with_buttons (_("Certificate trust..."), NULL, 0,
		_("_View Certificate"), GTK_RESPONSE_HELP,
		_("_Reject"), GTK_RESPONSE_REJECT,
		_("Accept _Temporarily"), GTK_RESPONSE_YES,
		_("_Accept Permanently"), GTK_RESPONSE_ACCEPT,
		NULL);

	gtk_window_set_icon_name (GTK_WINDOW (dialog), "evolution");
	gtk_dialog_set_response_sensitive (GTK_DIALOG (dialog), GTK_RESPONSE_HELP, FALSE);
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

	grid = g_object_new (GTK_TYPE_GRID,
		"orientation", GTK_ORIENTATION_HORIZONTAL,
		"row-homogeneous", FALSE,
		"row-spacing", 2,
		"column-homogeneous", FALSE,
		"column-spacing", 6,
		"hexpand", TRUE,
		"halign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"border-width", 12,
		NULL);

	widget = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
	gtk_container_add (GTK_CONTAINER (widget), GTK_WIDGET (grid));

	widget = gtk_image_new_from_stock (GTK_STOCK_DIALOG_WARNING, GTK_ICON_SIZE_DIALOG);
	g_object_set (G_OBJECT (widget),
		"vexpand", FALSE,
		"valign", GTK_ALIGN_START,
		"xpad", 6,
		NULL);
	gtk_grid_attach (grid, widget, 0, row, 1, 3);

	reason = g_strconcat ("<b>", host, "</b>", NULL);
	tmp = g_strdup_printf (_("SSL Certificate for '%s' is not trusted. Do you wish to accept it?\n\n"
				    "Detailed information about the certificate:"), reason);
	g_free (reason);
	widget = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (widget), tmp);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.0);
	g_free (tmp);

	gtk_grid_attach (grid, widget, 1, row, 2, 1);
	row++;

	issuer = CERT_NameToAscii (&cert->issuer);
	subject = CERT_NameToAscii (&cert->subject);
	reason = cert_errors_to_reason ((GTlsCertificateFlags) cert_errs);
	tmp = cert_fingerprint (cert);

	trust_prompt_add_info_line (grid, _("Issuer:"), issuer, TRUE, &row);
	trust_prompt_add_info_line (grid, _("Subject:"), subject, TRUE, &row);
	trust_prompt_add_info_line (grid, _("Fingerprint:"), tmp, TRUE, &row);
	trust_prompt_add_info_line (grid, _("Reason:"), reason, FALSE, &row);

	PORT_Free (issuer);
	PORT_Free (subject);
	g_free (reason);
	g_free (tmp);

	g_object_set_data (G_OBJECT (dialog), TRUST_PROMP_ID_KEY, GINT_TO_POINTER (prompt_id));
	g_signal_connect (dialog, "response", G_CALLBACK (trust_prompt_response_cb), extension);

	gtk_widget_show_all (GTK_WIDGET (grid));
	gtk_widget_show (dialog);

	CERT_DestroyCertificate (cert);
	g_free (derCert.data);

	return TRUE;
}
