#include <config.h>
#include <glib/gmain.h>
#include <libgnome/gnome-init.h>
#include "e-gw-connection.h"

static GMainLoop *main_loop;
static char *arg_hostname, *arg_username;

static gboolean
idle_cb (gpointer data)
{
	EGwConnection *cnc;

	cnc = e_gw_connection_new (arg_hostname, arg_username, NULL);
	if (E_IS_GW_CONNECTION (cnc)) {
		g_print ("Connected to %s!\n", arg_hostname);

		g_object_unref (cnc);
	} else
		g_print ("ERROR: Could not connect to %s\n", arg_hostname);

	g_main_loop_quit (main_loop);

	return FALSE;
}

int
main (int argc, char *argv[])
{
	gnome_program_init (PACKAGE, VERSION,
			    LIBGNOME_MODULE,
			    argc, argv,
			    NULL);

	if (argc != 3) {
		g_print ("Usage: %s hostname username\n", argv[0]);
		return -1;
	}

	arg_hostname = argv[1];
	arg_username = argv[2];

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_main_loop_unref (main_loop);

	return 0;
}
