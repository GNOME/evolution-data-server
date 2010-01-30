/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* test-source-list.c - Test for the ESourceList class.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Ettore Perazzoli <ettore@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include "e-source-list.h"

/* Globals. */

static GMainLoop *main_loop = NULL;
static ESourceList *list = NULL;
static gint idle_dump_id = 0;

/* Options.  */

static gboolean listen = FALSE;
static gboolean dump = FALSE;
static gchar *key_arg = (gchar *) "/apps/evolution/test/source_list";
static gchar *source_arg = NULL;
static gchar *group_arg = NULL;
static gchar *add_group_arg = NULL;
static gchar *add_source_arg = NULL;
static gchar *remove_group_arg = NULL;
static gchar *remove_source_arg = NULL;
static gchar *property_arg = NULL;
static gchar *set_name_arg = NULL;
static gchar *set_base_uri_arg = NULL;
static gchar *set_relative_uri_arg = NULL;
static gchar *set_color_arg = NULL;
static gchar *set_value_arg = NULL;
static gboolean unset_value = FALSE;
static gboolean unset_color = FALSE;

static GOptionEntry entries[] = {
	{ "key", '\0', 0, G_OPTION_ARG_STRING, &key_arg, "Name of the GConf key to use", "PATH" },
	{ "source", '\0', 0, G_OPTION_ARG_STRING, &source_arg, "UID of source to apply operations too", "UID"},
	{ "group", '\0', 0, G_OPTION_ARG_STRING, &group_arg, "UID of group to apply operations too", "UID" },
	{ "add-group", '\0', 0, G_OPTION_ARG_STRING, &add_group_arg, "Add group of specified name", "GROUP" },
	{ "add-source", '\0', 0, G_OPTION_ARG_STRING, &add_source_arg, "Add source of specified name", "SOURCE" },
	{ "remove-group", '\0', 0, G_OPTION_ARG_STRING, &remove_group_arg, "Remove group of specified name", "GROUP" },
	{ "remove-source", '\0', 0, G_OPTION_ARG_STRING, &remove_source_arg, "Remove source of specified name", "SOURCE" },
	{ "property", '\0', 0, G_OPTION_ARG_STRING, &property_arg, "Name of source property to apply operation to", "PROPERTY" },
	{ "set-name", '\0', 0, G_OPTION_ARG_STRING, &set_name_arg, "Set name of source or group.  When used with --group, it sets the name of a group. When used with both --group and --source, it sets the name of a source.", "NAME" },
	{ "set-relative-uri", '\0', 0, G_OPTION_ARG_STRING, &set_relative_uri_arg, "Set relative URI of a source.  Use with --source or --add-source.", "NAME" },
	{ "set-base-uri", '\0', 0, G_OPTION_ARG_STRING, &set_base_uri_arg, "Set base URI of a group.  Use with --group or --add-group.", "NAME" },
	{ "set-color", '\0', 0, G_OPTION_ARG_STRING, &set_color_arg, "Set the color of a source.  Use with --source or --add-source.", "COLOR (rrggbb)" },
	{ "unset-color", '\0', 0, G_OPTION_ARG_NONE, &unset_color, "Unset the color of a source.  Use with --source or --add-source.", NULL },
	{ "set-value", '\0', 0, G_OPTION_ARG_STRING, &set_value_arg, "Set a property on a source.  Use with --source and --property.", NULL },
	{ "unset-value", '\0', 0, G_OPTION_ARG_NONE, &unset_value, "Unset a property on a source.  Use with --source and --property.", NULL },
	{ "listen", '\0', 0, G_OPTION_ARG_NONE, &listen, "Wait and listen for changes.", NULL },
	{ "dump", '\0', 0, G_OPTION_ARG_NONE, &dump, "List the current configured sources.", NULL },
	{ NULL }
};

/* Forward decls.  */
static void group_added_callback (ESourceList *list, ESourceGroup *group);
static void group_removed_callback (ESourceList *list, ESourceGroup *group);
static void source_added_callback (ESourceGroup *group, ESource *source);
static void source_removed_callback (ESourceGroup *group, ESource *source);

static void
dump_property (const gchar *prop, const gchar *value)
{
	g_print ("\t\t\t%s: %s\n", prop, value);
}

static void
dump_source (ESource *source)
{
	gchar *uri = e_source_get_uri (source);
	const gchar *color_spec;

	g_print ("\tSource %s\n", e_source_peek_uid (source));
	g_print ("\t\tname: %s\n", e_source_peek_name (source));
	g_print ("\t\trelative_uri: %s\n", e_source_peek_relative_uri (source));
	g_print ("\t\tabsolute_uri: %s\n", uri);

	color_spec = e_source_peek_color_spec (source);
	if (color_spec != NULL)
		g_print ("\t\tcolor: %s\n", color_spec);

	g_print ("\t\tproperties:\n");
	e_source_foreach_property (source, (GHFunc) dump_property, NULL);

	g_free (uri);
}

static void
dump_group (ESourceGroup *group)
{
	GSList *sources, *p;

	g_print ("Group %s\n", e_source_group_peek_uid (group));
	g_print ("\tname: %s\n", e_source_group_peek_name (group));
	g_print ("\tbase_uri: %s\n", e_source_group_peek_base_uri (group));

	sources = e_source_group_peek_sources (group);
	for (p = sources; p != NULL; p = p->next) {
		ESource *source = E_SOURCE (p->data);

		dump_source (source);

		if (e_source_peek_group (source) != group)
			g_warning ("\t\t** ERROR ** parent pointer is %p, should be %p",
				   (gpointer) e_source_peek_group (source),
				   (gpointer) group);
	}
}

static void
dump_list (void)
{
	GSList *groups, *p;

	groups = e_source_list_peek_groups (list);
	if (groups == NULL) {
		g_print ("(No items)\n");
		return;
	}

	for (p = groups; p != NULL; p = p->next)
		dump_group (E_SOURCE_GROUP (p->data));
}

static gint
idle_dump_callback (gpointer unused_data)
{
	dump_list ();
	idle_dump_id = 0;

	return FALSE;
}

static void
dump_on_idle (void)
{
	if (idle_dump_id == 0)
		idle_dump_id = g_idle_add (idle_dump_callback, NULL);
}

static void
source_changed_callback (ESource *source)
{
	static gint count = 0;

	g_print ("** Event: source \"%s\" changed (%d)\n", e_source_peek_name (source), ++count);

	dump_on_idle ();
}

static void
group_changed_callback (ESourceGroup *group)
{
	static gint count = 0;

	g_print ("** Event: group \"%s\" changed (%d)\n", e_source_group_peek_name (group), ++count);

	dump_on_idle ();
}

static void
list_changed_callback (ESourceGroup *group)
{
	static gint count = 0;

	g_print ("** Event: list changed (%d)\n", ++count);

	dump_on_idle ();
}

static void
connect_source (ESource *source)
{
	g_object_ref (source);
	g_signal_connect (source, "changed", G_CALLBACK (source_changed_callback), NULL);
}

static void
connect_group (ESourceGroup *group)
{
	GSList *sources, *p;

	g_object_ref (group);
	g_signal_connect (group, "changed", G_CALLBACK (group_changed_callback), NULL);
	g_signal_connect (group, "source_added", G_CALLBACK (source_added_callback), NULL);
	g_signal_connect (group, "source_removed", G_CALLBACK (source_removed_callback), NULL);

	sources = e_source_group_peek_sources (group);
	for (p = sources; p != NULL; p = p->next)
		connect_source (E_SOURCE (p->data));
}

static void
connect_list (void)
{
	GSList *groups, *p;

	g_signal_connect (list, "changed", G_CALLBACK (list_changed_callback), NULL);
	g_signal_connect (list, "group_added", G_CALLBACK (group_added_callback), NULL);
	g_signal_connect (list, "group_removed", G_CALLBACK (group_removed_callback), NULL);

	groups = e_source_list_peek_groups (list);
	for (p = groups; p != NULL; p = p->next)
		connect_group (E_SOURCE_GROUP (p->data));
}

static void
disconnect_group (ESourceGroup *group)
{
	g_signal_handlers_disconnect_by_func (group, G_CALLBACK (group_changed_callback), NULL);
	g_signal_handlers_disconnect_by_func (group, G_CALLBACK (source_added_callback), NULL);

	g_object_unref (group);
}

static void
disconnect_source (ESource *source)
{
	g_signal_handlers_disconnect_by_func (source, G_CALLBACK (source_changed_callback), NULL);

	g_object_unref (source);
}

static void
source_added_callback (ESourceGroup *group,
		       ESource *source)
{
	static gint count = 0;

	g_print ("** Event: source \"%s\" added (%d)\n", e_source_peek_name (source), ++count);

	connect_source (source);
	dump_on_idle ();
}

static void
source_removed_callback (ESourceGroup *group,
			 ESource *source)
{
	static gint count = 0;

	g_print ("** Event: source \"%s\" removed (%d)\n", e_source_peek_name (source), ++count);

	disconnect_source (source);
	dump_on_idle ();
}

static void
group_added_callback (ESourceList *list,
		      ESourceGroup *group)
{
	static gint count = 0;

	g_print ("** Event: group \"%s\" added (%d)\n", e_source_group_peek_name (group), ++count);

	connect_group (group);
	dump_on_idle ();
}

static void
group_removed_callback (ESourceList *list,
			ESourceGroup *group)
{
	static gint count = 0;

	g_print ("** Event: group \"%s\" removed (%d)\n", e_source_group_peek_name (group), ++count);

	disconnect_group (group);
	dump_on_idle ();
}

static gint
on_idle_do_stuff (gpointer unused_data)
{
	GConfClient *client = gconf_client_get_default ();
	ESourceGroup *new_group = NULL;
	ESource *new_source = NULL;

	list = e_source_list_new_for_gconf (client, key_arg);
	g_object_unref (client);

	if (add_group_arg != NULL) {
		if (group_arg != NULL) {
			fprintf (stderr, "--add-group and --group cannot be used at the same time.\n");
			exit (1);
		}
		if (set_base_uri_arg == NULL) {
			fprintf (stderr, "When using --add-group, you need to specify a base URI using --set-base-uri.\n");
			exit (1);
		}

		new_group = e_source_group_new (add_group_arg, set_base_uri_arg);
		e_source_list_add_group (list, new_group, -1);
		g_object_unref (new_group);

		e_source_list_sync (list, NULL);
	}

	if (remove_group_arg != NULL) {
		ESourceGroup *group;

		group = e_source_list_peek_group_by_uid (list, remove_group_arg);
		if (group == NULL) {
			fprintf (stderr, "No such group \"%s\".\n", remove_group_arg);
			exit (1);
		}

		e_source_list_remove_group (list, group);
		e_source_list_sync (list, NULL);
	}

	if (add_source_arg != NULL) {
		ESourceGroup *group;

		if (group_arg == NULL && new_group == NULL) {
			fprintf (stderr,
				 "When using --add-source, you need to specify a group using either --group\n"
				 "or --add-group.\n");
			exit (1);
		}
		if (set_relative_uri_arg == NULL) {
			fprintf (stderr,
				 "When using --add-source, you need to specify a relative URI using\n"
				 "--set-relative-uri.\n");
			exit (1);
		}

		if (group_arg == NULL) {
			group = new_group;
		} else {
			group = e_source_list_peek_group_by_uid (list, group_arg);
			if (group == NULL) {
				fprintf (stderr, "No such group \"%s\".\n", group_arg == NULL ? add_group_arg : group_arg);
				exit (1);
			}
		}

		new_source = e_source_new (add_source_arg, set_relative_uri_arg);
		e_source_group_add_source (group, new_source, -1);
		e_source_list_sync (list, NULL);
	}

	if (remove_source_arg != NULL) {
		ESource *source;

		source = e_source_list_peek_source_by_uid (list, remove_source_arg);
		if (source == NULL) {
			fprintf (stderr, "No such source \"%s\".\n", remove_source_arg);
			exit (1);
		}

		e_source_list_remove_source_by_uid (list, remove_source_arg);
		e_source_list_sync (list, NULL);
	}

	if (set_name_arg != NULL) {
		if (group_arg == NULL && source_arg == NULL) {
			fprintf (stderr,
				 "When using --set-name, you need to specify a source (using --source"
				 "alone) or a group (using --group alone).\n");
			exit (1);
		}

		if (source_arg != NULL) {
			ESource *source = e_source_list_peek_source_by_uid (list, source_arg);

			if (source != NULL) {
				e_source_set_name (source, set_name_arg);
			} else {
				fprintf (stderr, "No such source \"%s\".\n", source_arg);
				exit (1);
			}
		} else {
			ESourceGroup *group = e_source_list_peek_group_by_uid (list, group_arg);

			if (group != NULL) {
				e_source_group_set_name (group, set_name_arg);
			} else {
				fprintf (stderr, "No such group \"%s\".\n", group_arg);
				exit (1);
			}
		}

		e_source_list_sync (list, NULL);
	}

	if (set_relative_uri_arg != NULL && add_source_arg == NULL) {
		ESource *source;

		if (source_arg == NULL) {
			fprintf (stderr,
				 "When using --set-relative-uri, you need to specify a source using "
				 "--source.\n");
			exit (1);
		}

		source = e_source_list_peek_source_by_uid (list, source_arg);
		e_source_set_relative_uri (source, set_relative_uri_arg);
		e_source_list_sync (list, NULL);
	}

	if (set_color_arg != NULL) {
		ESource *source;

		if (add_source_arg == NULL && source_arg == NULL) {
			fprintf (stderr,
				 "When using --set-color, you need to specify a source using --source\n");
			exit (1);
		}

		if (add_source_arg != NULL)
			source = new_source;
		else
			source = e_source_list_peek_source_by_uid (list, source_arg);

		e_source_set_color_spec (source, set_color_arg);
		e_source_list_sync (list, NULL);
	}

	if (unset_color) {
		ESource *source;

		if (add_source_arg == NULL && source_arg == NULL) {
			fprintf (stderr,
				 "When using --unset-color, you need to specify a source using --source\n");
			exit (1);
		}

		if (add_source_arg != NULL)
			source = new_source;
		else
			source = e_source_list_peek_source_by_uid (list, source_arg);

		e_source_set_color_spec (source, NULL);
		e_source_list_sync (list, NULL);
	}

	if (set_base_uri_arg != NULL && add_group_arg == NULL) {
		ESourceGroup *group;

		if (group_arg == NULL) {
			fprintf (stderr,
				 "When using --set-base-uri, you need to specify a group using --group.\n");
			exit (1);
		}

		group = e_source_list_peek_group_by_uid (list, group_arg);
		e_source_group_set_base_uri (group, set_base_uri_arg);
		e_source_list_sync (list, NULL);
	}

	if (set_value_arg != NULL) {
		ESource *source;

		if (add_source_arg == NULL && source_arg == NULL) {
			fprintf (stderr,
				 "When using --set-value, you need to specify a source using --source\n");
			exit (1);
		}

		if (property_arg == NULL) {
			fprintf (stderr,
				 "When using --set-value, you need to specify a property using --property\n");
			exit (1);
		}

		if (add_source_arg != NULL)
			source = new_source;
		else
			source = e_source_list_peek_source_by_uid (list, source_arg);

		e_source_set_property (source, property_arg, set_value_arg);
		e_source_list_sync (list, NULL);
	}

	if (unset_value) {
		ESource *source;

		if (add_source_arg == NULL && source_arg == NULL) {
			fprintf (stderr,
				 "When using --unset-value, you need to specify a source using --source\n");
			exit (1);
		}

		if (property_arg == NULL) {
			fprintf (stderr,
				 "When using --unset-value, you need to specify a property using --property\n");
			exit (1);
		}

		if (add_source_arg != NULL)
			source = new_source;
		else
			source = e_source_list_peek_source_by_uid (list, source_arg);

		e_source_set_property (source, property_arg, NULL);
		e_source_list_sync (list, NULL);
	}

	connect_list ();

	if (dump)
		dump_list ();

	if (!listen)
		g_main_loop_quit (main_loop);

	return FALSE;
}

gint
main (gint argc, gchar **argv)
{
	GOptionContext *context;
	GError *error = NULL;

	context = g_option_context_new ("- test source lists");
	g_option_context_add_main_entries (context, entries, GETTEXT_PACKAGE);
	g_option_context_parse (context, &argc, &argv, &error);

	g_idle_add (on_idle_do_stuff, NULL);

	main_loop = g_main_loop_new (NULL, TRUE);
	g_main_loop_run (main_loop);

	return 0;
}
