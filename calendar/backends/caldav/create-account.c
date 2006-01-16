
#include <config.h>
#include <gconf/gconf-client.h>
#include <glib/gmain.h>
#include <libedataserver/e-source-list.h>

static GConfClient *conf_client;
static GMainLoop *main_loop;
static char *arg_hostname, *arg_username, *arg_password, *arg_path;

static void
add_account (const char *conf_key, const char *hostname, const char *username)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	char *group_name;
	char *ruri;
	
	source_list = e_source_list_new_for_gconf (conf_client, conf_key);

	group = e_source_list_peek_group_by_name (source_list, "CalDAV");
	
	if (group == NULL) {
		group = e_source_group_new ("CalDAV", "caldav://");
		e_source_list_add_group (source_list, group, -1);	
	}
	
	ruri = g_strdup_printf ("%s/%s", hostname, arg_path);
	source = e_source_new (arg_path, ruri);
	e_source_set_property (source, "auth", "1");
	e_source_set_property (source, "username", username);
	e_source_group_add_source (group, source, -1);
	
	e_source_list_sync (source_list, NULL);

	g_object_unref (source);
	g_object_unref (group);
	g_object_unref (source_list);
}

static gboolean
idle_cb (gpointer data)
{
	add_account ("/apps/evolution/calendar/sources", arg_hostname, arg_username);

	g_main_loop_quit (main_loop);

	return FALSE;
}

int
main (int argc, char *argv[])
{
	g_type_init ();
	if (argc != 4 && argc != 5) {
		g_print ("Usage: %s hostname username path [password]\n", argv[0]);
		return -1;
	}

	arg_hostname = argv[1];
	arg_username = argv[2];
	arg_path     = argv[3];

	if (argc == 5)
		arg_password = argv[4];
	else
		arg_password = NULL;

	conf_client = gconf_client_get_default ();

	main_loop = g_main_loop_new (NULL, TRUE);
	g_idle_add ((GSourceFunc) idle_cb, NULL);
	g_main_loop_run (main_loop);

	/* terminate */
	g_object_unref (conf_client);
	g_main_loop_unref (main_loop);

	return 0;
}
