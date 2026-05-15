/*
 * SPDX-FileCopyrightText: (C) 2013 Intel Corporation
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Tristan Van Berkom <tristanvb@openismus.com>
 */

#include "cursor-example.h"

gint
main (gint argc,
      gchar *argv[])
{
  GtkWidget *example;

  gtk_init (&argc, &argv);

  if (argc < 2)
    example = cursor_example_new ("./data");
  else
    example = cursor_example_new (argv[1]);

  g_signal_connect (example, "destroy",
		    G_CALLBACK (gtk_main_quit), NULL);

  gtk_widget_show (example);
  gtk_main ();

  return 0;
}
