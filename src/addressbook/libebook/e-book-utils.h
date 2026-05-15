/*
 * SPDX-FileCopyrightText: (C) 2018 Red Hat, Inc. (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEBOOK_H_INSIDE__) && !defined (LIBEBOOK_COMPILATION)
#error "Only <libebook/libebook.h> should be included directly."
#endif

#ifndef E_BOOK_UTILS_H
#define E_BOOK_UTILS_H

#include <libedataserver/libedataserver.h>

G_BEGIN_DECLS

gboolean	e_book_utils_get_recipient_certificates_sync
							(ESourceRegistry *registry,
							 const GSList *only_clients, /* EBookClient * */
							 guint32 flags, /* bit-or of CamelRecipientCertificateFlags */
							 const GPtrArray *recipients, /* gchar * */
							 GSList **out_certificates, /* gchar * */
							 GCancellable *cancellable,
							 GError **error);

G_END_DECLS

#endif /* E_BOOK_UTILS_H */
