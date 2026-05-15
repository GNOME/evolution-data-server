/*
 * SPDX-FileCopyrightText: (C) 2021 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_HOSTNAME_UTILS_H
#define CAMEL_HOSTNAME_UTILS_H

#include <glib.h>

G_BEGIN_DECLS

gboolean	camel_hostname_utils_requires_ascii	(const gchar *hostname);
gboolean	camel_hostname_utils_host_is_in_domain	(const gchar *host,
							 const gchar *domain);

G_END_DECLS

#endif /* CAMEL_HOSTNAME_UTILS_H */
