/* Evolution calendar client - test program
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#include <config.h>
#include <stdlib.h>
#include <bonobo-activation/bonobo-activation.h>
#include <bonobo/bonobo-i18n.h>
#include <bonobo/bonobo-main.h>
#include "e-cal.h"
#include "e-cal-component.h"

static ECal *client1;
static ECal *client2;

/* Prints a message with a client identifier */
static void
cl_printf (ECal *client, const char *format, ...)
{
	va_list args;

	va_start (args, format);
	printf ("Client %s: ",
		client == client1 ? "1" :
		client == client2 ? "2" :
		"UNKNOWN");
	vprintf (format, args);
	va_end (args);
}

static void
objects_added_cb (GObject *object, GList *objects, gpointer data)
{
	GList *l;
	
	for (l = objects; l; l = l->next)
		cl_printf (data, "Object added %s\n", icalcomponent_get_uid (l->data));
}

static void
objects_modified_cb (GObject *object, GList *objects, gpointer data)
{
	GList *l;
	
	for (l = objects; l; l = l->next)
		cl_printf (data, "Object modified %s\n", icalcomponent_get_uid (l->data));
}

static void
objects_removed_cb (GObject *object, GList *objects, gpointer data)
{
	GList *l;
	
	for (l = objects; l; l = l->next)
		cl_printf (data, "Object removed %s\n", icalcomponent_get_uid (l->data));
}

static void
view_done_cb (GObject *object, ECalendarStatus status, gpointer data)
{
	cl_printf (data, "View done\n");
}

/* Lists the UIDs of objects in a calendar, called as an idle handler */
static gboolean
list_uids (gpointer data)
{
	ECal *client;
	GList *objects = NULL;
	GList *l;
	
	client = E_CAL (data);

	g_message ("Blah");
	
	if (!e_cal_get_object_list (client, "(contains? \"any\" \"Test4\")", &objects, NULL))
		return FALSE;
	
	cl_printf (client, "UIDS: ");

	if (!objects)
		printf ("none\n");
	else {
		for (l = objects; l; l = l->next) {
			const char *uid;

			uid = icalcomponent_get_uid (l->data);
			printf ("`%s' ", uid);
		}

		printf ("\n");

		for (l = objects; l; l = l->next) {
			printf ("------------------------------\n");
			printf ("%s", icalcomponent_as_ical_string (l->data));
			printf ("------------------------------\n");
		}
	}

	e_cal_free_object_list (objects);

	g_object_unref (client);

	return FALSE;
}

/* Callback used when a client is destroyed */
static void
client_destroy_cb (gpointer data, GObject *object)
{
	if (E_CAL (object) == client1)
		client1 = NULL;
	else if (E_CAL (object) == client2)
		client2 = NULL;
	else
		g_assert_not_reached ();

	if (!client1 && !client2)
		bonobo_main_quit ();
}

/* Creates a calendar client and tries to load the specified URI into it */
static void
create_client (ECal **client, const gchar *uri, ECalSourceType type, gboolean only_if_exists)
{
	ECalView *query;
	GError *error = NULL;
	
	*client = e_cal_new_from_uri (uri, type);
	if (!*client) {
		g_message (G_STRLOC ": could not create the client");
		exit (1);
	}

	g_object_weak_ref (G_OBJECT (*client), client_destroy_cb, NULL);

	cl_printf (*client, "Calendar loading `%s'...\n", uri);

	if (!e_cal_open (*client, only_if_exists, &error)) {
		cl_printf (*client, "Load/create %s\n", error->message);
		exit (1);
	}
	g_clear_error (&error);
	
	if (!e_cal_get_query (*client, "(contains? \"any\" \"Test4\")", &query, NULL)) {
		cl_printf (*client, G_STRLOC ": Unable to obtain query");
		exit (1);		
	}
	
	g_signal_connect (G_OBJECT (query), "objects_added", 
			  G_CALLBACK (objects_added_cb), client);
	g_signal_connect (G_OBJECT (query), "objects_modified", 
			  G_CALLBACK (objects_modified_cb), client);
	g_signal_connect (G_OBJECT (query), "objects_removed", 
			  G_CALLBACK (objects_removed_cb), client);
	g_signal_connect (G_OBJECT (query), "view_done",
			  G_CALLBACK (view_done_cb), client);
	
	e_cal_view_start (query);
	
	g_idle_add (list_uids, *client);

}

int
main (int argc, char **argv)
{
	bindtextdomain (GETTEXT_PACKAGE, EVOLUTION_LOCALEDIR);
	textdomain (GETTEXT_PACKAGE);

	g_type_init ();
	bonobo_activation_init (argc, argv);

	if (!bonobo_init (&argc, argv)) {
		g_message ("main(): could not initialize Bonobo");
		exit (1);
	}

#if 0
	source = e_source_new ("test-source", "file:///home/gnome24-evolution-new-calendar/evolution/local/Calendar");
#endif
	create_client (&client1, "file:///home/hpj/.evolution/calendar/local/OnThisComputer/Pakk", E_CAL_SOURCE_TYPE_EVENT, FALSE);
//	create_client (&client2, "file:///tmp/tasks", TRUE);

	bonobo_main ();
	return 0;
}
