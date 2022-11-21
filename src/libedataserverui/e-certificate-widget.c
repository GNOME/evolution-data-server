/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * SPDX-FileCopyrightText: (C) 2022 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * SECTION: e-certificate-widget
 * @short_description: A widget to show a certificate
 *
 * The #ECertificateWidget shows information about the provided certificate.
 **/

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <cert.h>
#include <pk11func.h>
#include <nss.h>

#include "libedataserver/libedataserver.h"

#include "e-certificate-widget.h"

struct _ECertificateWidgetPrivate {
	GtkWidget *grid;
	GHashTable *section_labels; /* index to keys[] ~> GtkLabel * */
	GHashTable *key_widgets; /* index to keys[] ~> KeyWidgets * */
};

#if GTK_CHECK_VERSION(4, 0, 0)
G_DEFINE_TYPE_WITH_PRIVATE (ECertificateWidget, e_certificate_widget, GTK_TYPE_BOX)
#else
G_DEFINE_TYPE_WITH_PRIVATE (ECertificateWidget, e_certificate_widget, GTK_TYPE_SCROLLED_WINDOW)
#endif

static gchar *
ecw_dup_from_cert_name (const CERTName *name,
			gchar *(*get_func) (const CERTName *name))
{
	gchar *value, *in_cert;

	in_cert = get_func (name);
	if (!in_cert)
		return NULL;

	value = g_strdup (in_cert);

	PORT_Free (in_cert);

	return value;
}

static gchar *
ecw_dup_subject_common_name (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->subject, CERT_GetCommonName);
}

static gchar *
ecw_dup_subject_nickname (CERTCertificate *cert)
{
	return g_strdup (cert->nickname);
}

static gchar *
ecw_dup_subject_name (CERTCertificate *cert)
{
	return g_strdup (cert->subjectName);
}

static gchar *
ecw_dup_identity (CERTCertificate *cert)
{
	gchar *value;

	value = ecw_dup_subject_common_name (cert);
	if (value && *value)
		return value;

	g_free (value);

	value = ecw_dup_subject_nickname (cert);
	if (value && *value)
		return value;

	g_free (value);

	return ecw_dup_subject_name (cert);
}

static gchar *
ecw_dup_subject_email (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->subject, CERT_GetCertEmailAddress);
}

static gchar *
ecw_dup_subject_country (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->subject, CERT_GetCountryName);
}

static gchar *
ecw_dup_subject_state (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->subject, CERT_GetStateName);
}

static gchar *
ecw_dup_subject_locality (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->subject, CERT_GetLocalityName);
}

static gchar *
ecw_dup_subject_org (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->subject, CERT_GetOrgName);
}

static gchar *
ecw_dup_subject_org_unit (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->subject, CERT_GetOrgUnitName);
}

static gchar *
ecw_dup_subject_domain_component_name (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->subject, CERT_GetDomainComponentName);
}

static gchar *
ecw_dup_subject_alternative_emails (CERTCertificate *cert)
{
	const gchar *email;
	gchar *main_email;
	GString *str = NULL;

	main_email = ecw_dup_subject_email (cert);

	for (email = CERT_GetFirstEmailAddress (cert); email; email = CERT_GetNextEmailAddress (cert, email)) {
		if (g_strcmp0 (email, main_email) == 0)
			continue;

		if (!str) {
			str = g_string_new (email);
		} else {
			g_string_append_c (str, '\n');
			g_string_append (str, email);
		}
	}

	g_free (main_email);

	return str ? g_string_free (str, FALSE) : NULL;
}

static gchar *
ecw_dup_issuer_common_name (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->issuer, CERT_GetCommonName);
}

static gchar *
ecw_dup_issuer_name (CERTCertificate *cert)
{
	return g_strdup (cert->issuerName);
}

static gchar *
ecw_dup_issuer (CERTCertificate *cert)
{
	gchar *value;

	value = ecw_dup_issuer_common_name (cert);
	if (value && *value)
		return value;

	g_free (value);

	return ecw_dup_issuer_name (cert);
}

static gchar *
ecw_dup_issuer_email (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->issuer, CERT_GetCertEmailAddress);
}

static gchar *
ecw_dup_issuer_country (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->issuer, CERT_GetCountryName);
}

static gchar *
ecw_dup_issuer_state (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->issuer, CERT_GetStateName);
}

static gchar *
ecw_dup_issuer_locality (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->issuer, CERT_GetLocalityName);
}

static gchar *
ecw_dup_issuer_org (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->issuer, CERT_GetOrgName);
}

static gchar *
ecw_dup_issuer_org_unit (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->issuer, CERT_GetOrgUnitName);
}

static gchar *
ecw_dup_issuer_domain_component_name (CERTCertificate *cert)
{
	return ecw_dup_from_cert_name (&cert->issuer, CERT_GetDomainComponentName);
}

static gchar *
ecw_dup_time (PRTime *prtime)
{
	PRExplodedTime explodedTime;
	struct tm exploded_tm;
	gchar buffer[512];
	gsize wrote;

	memset (&exploded_tm, 0, sizeof (struct tm));

	PR_ExplodeTime (*prtime, PR_LocalTimeParameters, &explodedTime);

	exploded_tm.tm_sec = explodedTime.tm_sec;
	exploded_tm.tm_min = explodedTime.tm_min;
	exploded_tm.tm_hour = explodedTime.tm_hour;
	exploded_tm.tm_mday = explodedTime.tm_mday;
	exploded_tm.tm_mon = explodedTime.tm_month;
	exploded_tm.tm_year = explodedTime.tm_year - 1900;

	wrote = e_strftime (buffer, sizeof (buffer), "%c", &exploded_tm);

	if (!wrote)
		return NULL;

	return  g_strndup (buffer, wrote);
}

static gchar *
ecw_dup_not_before (CERTCertificate *cert)
{
	PRTime not_before;
	PRTime not_after;

	if (SECSuccess != CERT_GetCertTimes (cert, &not_before, &not_after))
		return NULL;

	return ecw_dup_time (&not_before);
}

static gchar *
ecw_dup_not_after (CERTCertificate *cert)
{
	PRTime not_before;
	PRTime not_after;

	if (SECSuccess != CERT_GetCertTimes (cert, &not_before, &not_after))
		return NULL;

	return ecw_dup_time (&not_after);
}

static gchar *
ecw_dup_usage (CERTCertificate *cert)
{
	struct {
		gint bit;
		const gchar *text;
	} usageinfo[] = {
		/* x509 certificate usage types */
		{ certificateUsageEmailSigner, N_("Digital Signature") },
		{ certificateUsageEmailRecipient, N_("Key Encipherment") }
	};

	GString *str = NULL;
	gint ii;

	for (ii = 0; ii < G_N_ELEMENTS (usageinfo); ii++) {
		if ((cert->keyUsage & usageinfo[ii].bit) != 0) {
			if (!str)
				str = g_string_new ("");
			if (str->len != 0)
				g_string_append (str, ", ");
			g_string_append (str, _(usageinfo[ii].text));
		}
	}

	return str ? g_string_free (str, FALSE) : NULL;
}

static gchar *
ecw_dup_hexify (const guchar *data,
		guint data_len)
{
	gchar *hexified, *value;
	SECItem item;

	if (!data|| !data_len)
		return NULL;

	item.data = (guchar *) data;
	item.len = data_len;

	hexified = CERT_Hexify (&item, TRUE);

	value = g_strdup (hexified);

	if (hexified)
		PORT_Free (hexified);

	return value;
}

static gchar *
ecw_dup_version (CERTCertificate *cert)
{
	return ecw_dup_hexify (cert->version.data, cert->version.len);
}

static gchar *
ecw_dup_serial_number (CERTCertificate *cert)
{
	return ecw_dup_hexify (cert->serialNumber.data, cert->serialNumber.len);
}

static gchar *
ecw_dup_subject_key_id (CERTCertificate *cert)
{
	return ecw_dup_hexify (cert->subjectKeyID.data, cert->subjectKeyID.len);
}

static gchar *
ecw_dup_signature_alg (CERTCertificate *cert)
{
	SECOidTag tag;
	const gchar *description;

	tag = SECOID_GetAlgorithmTag (&cert->signature);

	if (tag == SEC_OID_UNKNOWN)
		return NULL;

	description = SECOID_FindOIDTagDescription (tag);

	return g_strdup (description);
}

static gchar *
ecw_dup_issuer_fingerprint_sha256 (CERTCertificate *cert)
{
	guchar fingerprint[SHA256_LENGTH + 1];

	if (!cert->derIssuer.data || !cert->derIssuer.len)
		return NULL;

	memset (fingerprint, 0, sizeof fingerprint);
	PK11_HashBuf (SEC_OID_SHA256, fingerprint, cert->derIssuer.data, cert->derIssuer.len);

	return ecw_dup_hexify (fingerprint, SHA256_LENGTH);
}

static gchar *
ecw_dup_fingerprint_sha256 (CERTCertificate *cert)
{
	guchar fingerprint[SHA256_LENGTH + 1];

	memset (fingerprint, 0, sizeof fingerprint);
	PK11_HashBuf (SEC_OID_SHA256, fingerprint, cert->derCert.data, cert->derCert.len);

	return ecw_dup_hexify (fingerprint, SHA256_LENGTH);
}

static gchar *
ecw_dup_public_key_alg (CERTCertificate *cert)
{
	SECOidTag tag;
	const gchar *description;

	tag = SECOID_GetAlgorithmTag (&cert->subjectPublicKeyInfo.algorithm);

	if (tag == SEC_OID_UNKNOWN)
		return NULL;

	description = SECOID_FindOIDTagDescription (tag);

	return g_strdup (description);
}

static struct _SectionKey {
	const gchar *section_caption; /* non-NULL, to start a new section */
	const gchar *name;
	gchar * (*dup_value) (CERTCertificate *cert);
} keys[] = {
	{ N_("Certificate"),	N_("Identity"), ecw_dup_identity },
	{ NULL,			N_("Issuer"), ecw_dup_issuer },
	{ NULL,			N_("Expires on"), ecw_dup_not_after },
	{ N_("Subject"),	N_("Common Name"), ecw_dup_subject_common_name },
	{ NULL,			N_("Nickname"), ecw_dup_subject_nickname },
	{ NULL,			N_("Email"), ecw_dup_subject_email },
	{ NULL,			N_("Organization"), ecw_dup_subject_org },
	{ NULL,			N_("Organization Unit"), ecw_dup_subject_org_unit },
	{ NULL,			N_("Country"), ecw_dup_subject_country },
	{ NULL,			N_("State"), ecw_dup_subject_state },
	{ NULL,			N_("Locality"), ecw_dup_subject_locality },
	{ NULL,			N_("Domain Component Name"), ecw_dup_subject_domain_component_name },
	{ NULL,			N_("Alternative Emails"), ecw_dup_subject_alternative_emails },
	{ N_("Issuer"),		N_("Common Name"), ecw_dup_issuer_common_name },
	{ NULL,			N_("Email"), ecw_dup_issuer_email },
	{ NULL,			N_("Organization"), ecw_dup_issuer_org },
	{ NULL,			N_("Organization Unit"), ecw_dup_issuer_org_unit },
	{ NULL,			N_("Country"), ecw_dup_issuer_country },
	{ NULL,			N_("State"), ecw_dup_issuer_state },
	{ NULL,			N_("Locality"), ecw_dup_issuer_locality },
	{ NULL,			N_("Domain Component Name"), ecw_dup_issuer_domain_component_name },
	{ NULL,			N_("SHA-256 Fingerprint"), ecw_dup_issuer_fingerprint_sha256 },
	{ N_("Details"),	N_("Not Before"), ecw_dup_not_before },
	{ NULL,			N_("Not After"), ecw_dup_not_after },
	{ NULL,			N_("Usage"), ecw_dup_usage },
	{ NULL,			N_("Version"), ecw_dup_version },
	{ NULL,			N_("Serial Number"), ecw_dup_serial_number },
	{ NULL,			N_("Key ID"), ecw_dup_subject_key_id },
	{ NULL,			N_("Signature Algorithm"), ecw_dup_signature_alg },
	{ NULL,			N_("SHA-256 Fingerprint"), ecw_dup_fingerprint_sha256 },
	{ N_("Public Key"),	N_("Algorithm"), ecw_dup_public_key_alg }
};

typedef struct _KeyWidgets {
	GtkWidget *name;
	GtkWidget *value;
} KeyWidgets;

#if !GTK_CHECK_VERSION(4, 0, 0)
static void
e_certificate_widget_show_all (GtkWidget *widget)
{
	gtk_widget_show (widget);
}
#endif

static void
e_certificate_widget_finalize (GObject *object)
{
	ECertificateWidget *self = E_CERTIFICATE_WIDGET (object);

	g_hash_table_destroy (self->priv->section_labels);
	g_hash_table_destroy (self->priv->key_widgets);

	/* Chain up to parent's method */
	G_OBJECT_CLASS (e_certificate_widget_parent_class)->finalize (object);
}

static void
e_certificate_widget_class_init (ECertificateWidgetClass *klass)
{
	GObjectClass *object_class;
#if !GTK_CHECK_VERSION(4, 0, 0)
	GtkWidgetClass *widget_class;
#endif

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = e_certificate_widget_finalize;

#if !GTK_CHECK_VERSION(4, 0, 0)
	widget_class = GTK_WIDGET_CLASS (klass);
	widget_class->show_all = e_certificate_widget_show_all;
#endif
}

static void
e_certificate_widget_init (ECertificateWidget *self)
{
	GtkWidget *scrolled_window;

	self->priv = e_certificate_widget_get_instance_private (self);
	self->priv->section_labels = g_hash_table_new (g_direct_hash, g_direct_equal);
	self->priv->key_widgets = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);

#if GTK_CHECK_VERSION(4, 0, 0)
	scrolled_window = gtk_scrolled_window_new ();
	gtk_box_append (GTK_BOX (self), scrolled_window);
#else
	scrolled_window = GTK_WIDGET (self);
#endif

	g_object_set (scrolled_window,
		"halign", GTK_ALIGN_FILL,
		"hexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		"hscrollbar-policy", GTK_POLICY_AUTOMATIC,
		"vscrollbar-policy", GTK_POLICY_AUTOMATIC,
		"min-content-height", 100,
#if GTK_CHECK_VERSION(4, 0, 0)
		"has-frame", FALSE,
#else
		"shadow-type", GTK_SHADOW_NONE,
#endif
		NULL);

	self->priv->grid = gtk_grid_new ();

	g_object_set (self->priv->grid,
		"halign", GTK_ALIGN_FILL,
		"hexpand", TRUE,
		"valign", GTK_ALIGN_FILL,
		"vexpand", TRUE,
		"column-spacing", 8,
		"row-spacing", 4,
		NULL);

	gtk_style_context_add_class (gtk_widget_get_style_context (scrolled_window), "view");

#if GTK_CHECK_VERSION(4, 0, 0)
	gtk_scrolled_window_set_child (GTK_SCROLLED_WINDOW (scrolled_window), self->priv->grid);
#else
	gtk_container_add (GTK_CONTAINER (scrolled_window), self->priv->grid);
#endif
}

/**
 * e_certificate_widget_new:
 *
 * Creates a new #ECertificateWidget
 *
 * Returns: (transfer full): a new #ECertificateWidget
 *
 * Since: 3.46
 **/
GtkWidget *
e_certificate_widget_new (void)
{
	return g_object_new (E_TYPE_CERTIFICATE_WIDGET, NULL);
}

/**
 * e_certificate_widget_set_der:
 * @self: an #ECertificateWidget
 * @der_data: (nullable): certificate data in DER format, or %NULL
 * @der_data_len: length of the @der_data
 *
 * Updates the content of the @self with the certificate information
 * described by the @der_data of the length @der_data_len in the DER
 * format.
 *
 * The content of the @self is cleared when the @der_data is %NULL.
 *
 * Since: 3.46
 **/
void
e_certificate_widget_set_der (ECertificateWidget *self,
			      gconstpointer der_data,
			      guint der_data_len)
{
	CERTCertificate *cert;
	PangoAttrList *attrs = NULL;
	GtkGrid *grid;
	guint ii, section_start = 0, row = 0;
	gboolean anything_in_section = FALSE;

	g_return_if_fail (E_IS_CERTIFICATE_WIDGET (self));

	if (!der_data) {
		gtk_widget_hide (self->priv->grid);
		return;
	}

	/* The app may or may not have initialized NSS on its own; if not, initialize it now */
	if (!NSS_IsInitialized ())
		NSS_NoDB_Init (g_get_tmp_dir ());

	cert = CERT_DecodeCertFromPackage ((gchar *) der_data, der_data_len);

	if (!cert) {
		gtk_widget_hide (self->priv->grid);
		return;
	}

	gtk_widget_show (self->priv->grid);

	grid = GTK_GRID (self->priv->grid);

	for (ii = 0; ii < G_N_ELEMENTS (keys); ii++) {
		KeyWidgets *key_widgets;
		gchar *value;

		if (ii > 0 && keys[ii].section_caption != NULL) {
			GtkWidget *section_label;

			section_label = g_hash_table_lookup (self->priv->section_labels, GUINT_TO_POINTER (section_start));
			if (section_label)
				gtk_widget_set_visible (section_label, anything_in_section);

			anything_in_section = FALSE;
			section_start = ii;
		}

		if (keys[ii].section_caption && !g_hash_table_contains (self->priv->section_labels, GUINT_TO_POINTER (ii))) {
			GtkWidget *label;

			if (!attrs) {
				attrs = pango_attr_list_new ();
				pango_attr_list_insert (attrs, pango_attr_weight_new (PANGO_WEIGHT_BOLD));
			}

			label = gtk_label_new (_(keys[ii].section_caption));

			g_object_set (label,
				"halign", GTK_ALIGN_START,
				"margin-start", 8,
				"margin-end", 8,
				"margin-top", 8,
				"attributes", attrs,
				"xalign", 0.0,
				NULL);

			gtk_grid_attach (grid, label, 0, row, 2, 1);
			row++;

			g_hash_table_insert (self->priv->section_labels, GUINT_TO_POINTER (ii), label);

			if (ii > 0)
				gtk_widget_set_margin_top (label, 16);
		}

		key_widgets = g_hash_table_lookup (self->priv->key_widgets, GUINT_TO_POINTER (ii));

		if (!key_widgets) {
			key_widgets = g_new0 (KeyWidgets, 1);
			key_widgets->name = gtk_label_new (_(keys[ii].name));
			key_widgets->value = gtk_label_new ("");

			g_hash_table_insert (self->priv->key_widgets, GUINT_TO_POINTER (ii), key_widgets);

			g_object_set (key_widgets->name,
				"halign", GTK_ALIGN_END,
				"valign", GTK_ALIGN_START,
				"margin-start", 12,
				"justify", GTK_JUSTIFY_RIGHT,
				"xalign", 1.0,
				NULL);

			g_object_set (key_widgets->value,
				"halign", GTK_ALIGN_START,
				"valign", GTK_ALIGN_START,
				"xalign", 0.0,
				"margin-end", 12,
				"max-width-chars", 80,
				"wrap", TRUE,
				"selectable", TRUE,
				NULL);

			gtk_grid_attach (grid, key_widgets->name, 0, row, 1, 1);
			gtk_grid_attach (grid, key_widgets->value, 1, row, 1, 1);
			row++;
		}

		value = keys[ii].dup_value (cert);

		if (value && *value) {
			anything_in_section = TRUE;

			gtk_label_set_label (GTK_LABEL (key_widgets->value), value);

			gtk_widget_show (key_widgets->name);
			gtk_widget_show (key_widgets->value);
		} else {
			gtk_widget_hide (key_widgets->name);
			gtk_widget_hide (key_widgets->value);
		}

		g_free (value);
	}

	if (ii > 0) {
		GtkWidget *section_label;

		section_label = g_hash_table_lookup (self->priv->section_labels, GUINT_TO_POINTER (section_start));
		if (section_label)
			gtk_widget_set_visible (section_label, anything_in_section);
	}

	CERT_DestroyCertificate (cert);
	g_clear_pointer (&attrs, pango_attr_list_unref);
}

/**
 * e_certificate_widget_set_pem:
 * @self: an #ECertificateWidget
 * @pem_data: (nullable): certificate data in PEM format, or %NULL
 *
 * Updates the content of the @self with the certificate information
 * described by the @pem_data in the PEM format.
 *
 * The content of the @self is cleared when the @pem_data is %NULL.
 *
 * Since: 3.46
 **/
void
e_certificate_widget_set_pem (ECertificateWidget *self,
			      const gchar *pem_data)
{
	GTlsCertificate *tls_cert;
	GByteArray *der_bytes = NULL;

	g_return_if_fail (E_IS_CERTIFICATE_WIDGET (self));

	if (!pem_data) {
		e_certificate_widget_set_der (self, NULL, 0);
		return;
	}

	tls_cert = g_tls_certificate_new_from_pem (pem_data, -1, NULL);

	if (!tls_cert) {
		e_certificate_widget_set_der (self, NULL, 0);
		return;
	}

	g_object_get (tls_cert, "certificate", &der_bytes, NULL);

	e_certificate_widget_set_der (self, der_bytes ? der_bytes->data : NULL, der_bytes ? der_bytes->len : 0);

	g_clear_pointer (&der_bytes, g_byte_array_unref);
	g_clear_object (&tls_cert);
}
