/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef DOVECOT_HELPER_H
#define DOVECOT_HELPER_H

#include <glib.h>

G_BEGIN_DECLS

typedef struct _DovecotTestServer DovecotTestServer;

DovecotTestServer *	dovecot_test_server_new		(void);
void			dovecot_test_server_free	(DovecotTestServer *server);

const gchar *		dovecot_test_server_get_host	(DovecotTestServer *server);
guint16			dovecot_test_server_get_port	(DovecotTestServer *server);
const gchar *		dovecot_test_server_get_user	(DovecotTestServer *server);
const gchar *		dovecot_test_server_get_password	(DovecotTestServer *server);

G_END_DECLS

#endif /* DOVECOT_HELPER_H */
