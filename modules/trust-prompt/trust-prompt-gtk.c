/*
 * trust-prompt-gtk.c
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

#include "trust-prompt.h"
#include "certificate-viewer.h"

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
	g_object_set (
		G_OBJECT (widget),
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

static void
trust_prompt_free_certificate (gpointer cert)
{
	if (!cert)
		return;

	CERT_DestroyCertificate (cert);
}

static void
trust_prompt_free_issuers (gpointer issuers)
{
	if (!issuers)
		return;

	g_slist_free_full (issuers, trust_prompt_free_certificate);
}

#define TRUST_PROMP_ID_KEY	"ETrustPrompt::prompt-id-key"
#define TRUST_PROMP_CERT_KEY	"ETrustPrompt::cert-key"
#define TRUST_PROMP_ISSUERS_KEY	"ETrustPrompt::issuers-key"

static void
trust_prompt_response_cb (GtkWidget *dialog,
                          gint response,
                          EUserPrompterServerExtension *extension)
{
	gint prompt_id;

	if (response == GTK_RESPONSE_HELP) {
		GtkWidget *viewer;

		viewer = certificate_viewer_new (
			GTK_WINDOW (dialog),
			g_object_get_data (G_OBJECT (dialog), TRUST_PROMP_CERT_KEY),
			g_object_get_data (G_OBJECT (dialog), TRUST_PROMP_ISSUERS_KEY));

		gtk_dialog_run (GTK_DIALOG (viewer));
		gtk_widget_destroy (viewer);

		return;
	}

	prompt_id = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (dialog), TRUST_PROMP_ID_KEY));
	gtk_widget_destroy (dialog);

	if (response == GTK_RESPONSE_REJECT)
		response = TRUST_PROMPT_RESPONSE_REJECT;
	else if (response == GTK_RESPONSE_ACCEPT)
		response = TRUST_PROMPT_RESPONSE_ACCEPT_PERMANENTLY;
	else if (response == GTK_RESPONSE_YES)
		response = TRUST_PROMPT_RESPONSE_ACCEPT_TEMPORARILY;
	else
		response = TRUST_PROMPT_RESPONSE_UNKNOWN;

	e_user_prompter_server_extension_response (extension, prompt_id, response, NULL);
}

gboolean
trust_prompt_show (EUserPrompterServerExtension *extension,
                   gint prompt_id,
                   const gchar *host,
                   const gchar *markup,
                   const CERTCertificate *pcert,
                   const gchar *cert_fingerprint,
                   const gchar *reason,
                   const GSList *pissuers)
{
	GtkWidget *dialog, *widget;
	GtkGrid *grid;
	gchar *tmp, *issuer, *subject, *head;
	GSList *issuers, *iter;
	CERTCertificate *cert;
	gint row = 0;

	cert = CERT_DupCertificate ((CERTCertificate *) pcert);
	issuers = g_slist_copy ((GSList *) pissuers);
	for (iter = issuers; iter; iter = g_slist_next (iter)) {
		if (iter->data)
			iter->data = CERT_DupCertificate (iter->data);
	}

	dialog = gtk_dialog_new_with_buttons (
		_("Certificate trust..."), NULL, 0,
		_("_View Certificate"), GTK_RESPONSE_HELP,
		_("_Reject"), GTK_RESPONSE_REJECT,
		_("Accept _Temporarily"), GTK_RESPONSE_YES,
		_("_Accept Permanently"), GTK_RESPONSE_ACCEPT,
		NULL);

	gtk_window_set_icon_name (GTK_WINDOW (dialog), "evolution");
	gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_YES);

	grid = g_object_new (
		GTK_TYPE_GRID,
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
	g_object_set (
		G_OBJECT (widget),
		"vexpand", FALSE,
		"valign", GTK_ALIGN_START,
		"xpad", 6,
		NULL);
	gtk_grid_attach (grid, widget, 0, row, 1, 3);

	tmp = NULL;
	if (!markup || !*markup) {
		gchar *bhost;

		bhost = g_strconcat ("<b>", host, "</b>", NULL);
		tmp = g_strdup_printf (_("SSL certificate for '%s' is not trusted. Do you wish to accept it?"), bhost);
		g_free (bhost);

		markup = tmp;
	}

	head = g_strdup_printf ("%s\n\n%s", markup, _("Detailed information about the certificate:"));

	widget = gtk_label_new (NULL);
	gtk_label_set_markup (GTK_LABEL (widget), head);
	gtk_misc_set_alignment (GTK_MISC (widget), 0.0, 0.0);
	g_free (head);
	g_free (tmp);

	gtk_grid_attach (grid, widget, 1, row, 2, 1);
	row++;

	issuer = CERT_NameToAscii (&cert->issuer);
	subject = CERT_NameToAscii (&cert->subject);

	trust_prompt_add_info_line (grid, _("Issuer:"), issuer, TRUE, &row);
	trust_prompt_add_info_line (grid, _("Subject:"), subject, TRUE, &row);
	trust_prompt_add_info_line (grid, _("Fingerprint:"), cert_fingerprint, TRUE, &row);
	trust_prompt_add_info_line (grid, _("Reason:"), reason, FALSE, &row);

	PORT_Free (issuer);
	PORT_Free (subject);

	g_object_set_data (G_OBJECT (dialog), TRUST_PROMP_ID_KEY, GINT_TO_POINTER (prompt_id));
	g_object_set_data_full (G_OBJECT (dialog), TRUST_PROMP_CERT_KEY, cert, trust_prompt_free_certificate);
	g_object_set_data_full (G_OBJECT (dialog), TRUST_PROMP_ISSUERS_KEY, issuers, trust_prompt_free_issuers);

	g_signal_connect (dialog, "response", G_CALLBACK (trust_prompt_response_cb), extension);

	gtk_widget_show_all (GTK_WIDGET (grid));
	gtk_widget_show (dialog);

	return TRUE;
}
