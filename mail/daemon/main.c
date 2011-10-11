/*
 * Copyright (C) 2011, Intel Corporation 2011.
 *
 * Author: Srinivasa Ragavan
 * 
 * 
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 * 
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <libedataserver/e-data-server-util.h>
#include <libedataserverui/e-passwords.h>
#include "libemail-engine/e-mail-session.h"
#include "libemail-engine/mail-folder-cache.h"
#include "libemail-utils/mail-mt.h"
#include "libemail-engine/mail-config.h"
#include "libemail-engine/mail-ops.h"
#include "libemail-engine/e-mail-store.h"

#include "mail-send-recv.h"
#include "e-dbus-manager.h"
#include "utils.h"

EMailSession *session = NULL;
MailFolderCache *folder_cache = NULL;

static gboolean
start_mail_engine ()
{
	char *data_dir;

	mail_debug_init ();

	if (camel_init (e_get_user_data_dir (), TRUE) != 0)
		exit (0);
	camel_provider_init ();

	data_dir = g_build_filename (e_get_user_data_dir(), "mail", NULL);
	if (!g_file_test (data_dir, G_FILE_TEST_EXISTS|G_FILE_TEST_IS_DIR)) {
		g_mkdir_with_parents (data_dir, 0700);
	}

	session = e_mail_session_new ();
	/* When the session emits flush-outbox, just call mail_send to flush it */
	g_signal_connect (session, "flush-outbox", G_CALLBACK(mail_send), session);

	folder_cache = e_mail_session_get_folder_cache (session);

	mail_config_init (session);
	mail_msg_init ();

	
	e_mail_store_init (session, data_dir);

	g_free(data_dir);

	//e_mail_connection_connman_new();
	mail_autoreceive_init (session);
	
	//e_dbus_manager_new ();

	return FALSE;
}

int 
main(int argc, char* argv[])
{
	gtk_init_with_args (
		&argc, &argv,
		_("- The Evolution Mail Data Server"),
		NULL, (gchar *) GETTEXT_PACKAGE, NULL);

	g_type_init ();
	g_set_prgname ("evolution-mail-factory");
	if (!g_thread_supported ()) g_thread_init (NULL);

	e_passwords_init ();

	g_idle_add ((GSourceFunc) start_mail_engine, NULL);
	gtk_main ();

   	return 0;
}
