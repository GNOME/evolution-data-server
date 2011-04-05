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
#include "mail-session.h"
#include "e-mail-store.h"
#include "mail-folder-cache.h"
#include "mail-mt.h"
#include "mail-config.h"
#include "mail-ops.h"
#include "e-dbus-manager.h"
#include "mail-send-recv.h"

#include "utils.h"

/* Yeah, the daemon shouldn't be a gtk+ app. But this code shuffling ends this up as a Gtk daemon. But once we solve password and alert issues, this should be a simple mainloop */

extern CamelSession *session;
static gint mail_sync_in_progress = 0;

static void
mail_sync_store_done_cb (CamelStore *store,
                         gpointer user_data,
			 GError *error)
{
	mail_sync_in_progress--;
}

static void
mail_sync_store_cb (CamelStore *store,
                    const gchar *display_name,
                    gpointer not_used)
{
	mail_sync_in_progress++;

	mail_sync_store (
		store, FALSE,
		mail_sync_store_done_cb,
		NULL);
}

static gboolean
mail_auto_sync ()
{
	/* If a sync is still in progress, skip this round. */
	if (mail_sync_in_progress)
		goto exit;

	e_mail_store_foreach (
		(GHFunc) mail_sync_store_cb,
		NULL);

exit:
	return TRUE;
}

static gboolean
start_mail_engine ()
{
	char *data_dir;

	mail_debug_int ();
	mail_session_start ();
	mail_folder_cache_get_default ();
	mail_config_init ();
	mail_msg_init ();

	data_dir = g_build_filename (e_get_user_data_dir(), "mail", NULL);
	if (!g_file_test (data_dir, G_FILE_TEST_EXISTS|G_FILE_TEST_IS_DIR)) {
		g_mkdir_with_parents (data_dir, 0700);
	}
	
	e_mail_store_init (data_dir);

	g_free(data_dir);

	mail_autoreceive_init (session);

	/* In regular intervals, sync to server. We donno how & when the daemon will die */
	g_timeout_add_seconds (
			mail_config_get_sync_timeout (),
			(GSourceFunc) mail_auto_sync,
			NULL);
	e_dbus_manager_new ();

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
	g_set_prgname ("e-mail-factory");
	if (!g_thread_supported ()) g_thread_init (NULL);

	e_passwords_init ();

	g_idle_add ((GSourceFunc) start_mail_engine, NULL);
	gtk_main ();

   	return 0;
}
