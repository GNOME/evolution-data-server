
#include <config.h>
#include <gconf/gconf-client.h>
#include <libgnome/gnome-init.h>
#include <libedataserver/e-source-list.h>

static GConfClient *conf_client;

static void
add_account (const char *conf_key, const char *hostname, const char *username)
{
	ESourceList *source_list;
	ESourceGroup *group;
	ESource *source;
	char *str;

	source_list = e_source_list_new_for_gconf (conf_client, conf_key);

	str = g_strdup_printf ("%s@%s", username, hostname);
	group = e_source_group_new (str, "groupwise://");
	g_free (str);

	source = e_source_new ("Default", hostname);
	e_source_set_group (source, group);

	e_source_list_add_group (source_list, group, -1);
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

	/* initialize variables */
	conf_client = gconf_client_get_default ();

	add_account ("/apps/evolution/calendar/sources", argv[1], argv[2]);
	add_account ("/apps/evolution/tasks/sources", argv[1], argv[2]);

	/* terminate */
	g_object_unref (conf_client);

	return 0;
}
