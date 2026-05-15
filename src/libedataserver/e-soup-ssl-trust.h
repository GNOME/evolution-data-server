/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedateserver.h> should be included directly."
#endif

#ifndef E_SOUP_SSL_TRUST_H
#define E_SOUP_SSL_TRUST_H

#include <libedataserver/e-source.h>

#include <libsoup/soup.h>

G_BEGIN_DECLS

void		e_soup_ssl_trust_connect	(SoupMessage *soup_message,
						 ESource *source);

G_END_DECLS

#endif /* E_SOUP_SSL_TRUST_H */
