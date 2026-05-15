/*
 * SPDX-FileCopyrightText: (C) 2015 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_TRUST_PROMPT_H
#define E_TRUST_PROMPT_H

#include <glib.h>
#include <gio/gio.h>

#include <gtk/gtk.h>

#if !defined (__LIBEDATASERVERUI_H_INSIDE__) && !defined (LIBEDATASERVERUI_COMPILATION)
#if GTK_CHECK_VERSION(4, 0, 0)
#error "Only <libedataserverui4/libedataserverui4.h> should be included directly."
#else
#error "Only <libedataserverui/libedataserverui.h> should be included directly."
#endif
#endif

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

gchar *		e_trust_prompt_describe_certificate_errors
					(GTlsCertificateFlags flags);
ETrustPromptResponse
		e_trust_prompt_run_modal(GtkWindow *parent,
					 const gchar *source_extension,
					 const gchar *source_display_name,
					 const gchar *host,
					 const gchar *certificate_pem,
					 GTlsCertificateFlags certificate_errors,
					 const gchar *error_text);

void		e_trust_prompt_run_for_source
					(GtkWindow *parent,
					 ESource *source,
					 const gchar *certificate_pem,
					 GTlsCertificateFlags certificate_errors,
					 const gchar *error_text,
					 gboolean allow_source_save,
					 GCancellable *cancellable,
					 GAsyncReadyCallback callback,
					 gpointer user_data);
gboolean	e_trust_prompt_run_for_source_finish
					(ESource *source,
					 GAsyncResult *result,
					 ETrustPromptResponse *response,
					 GError **error);

G_END_DECLS

#endif /* E_TRUST_PROMPT_H */
