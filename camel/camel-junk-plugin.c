/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *  Radek Doulik <rodo@ximian.com>
 *
 * Copyright 2003 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#include <stdio.h>

#include <glib.h>

#include "camel-debug.h"
#include "camel-junk-plugin.h"
#include "camel-mime-message.h"

#define d(x) (camel_debug("junk")?(x):0)

const char *
camel_junk_plugin_get_name (CamelJunkPlugin *csp)
{
	g_return_val_if_fail (csp->get_name != NULL, NULL);

	d(fprintf (stderr, "camel_junk_plugin_get_namen"));

	return csp->get_name (csp);
}

int
camel_junk_plugin_check_junk (CamelJunkPlugin *csp, CamelMimeMessage *message)
{
	g_return_val_if_fail (csp->check_junk != NULL, FALSE);

	d(fprintf (stderr, "camel_junk_plugin_check_junk\n"));

	return csp->check_junk (csp, message);
}

void
camel_junk_plugin_report_junk (CamelJunkPlugin *csp, CamelMimeMessage *message)
{
	d(fprintf (stderr, "camel_junk_plugin_report_junk\n"));

	if (csp->report_junk)
		csp->report_junk (csp, message);
}

void
camel_junk_plugin_report_notjunk (CamelJunkPlugin *csp, CamelMimeMessage *message)
{
	d(fprintf (stderr, "camel_junk_plugin_report_notjunk\n"));

	if (csp->report_notjunk)
		csp->report_notjunk (csp, message);
}

void
camel_junk_plugin_commit_reports (CamelJunkPlugin *csp)
{
	d(fprintf (stderr, "camel_junk_plugin_commit_reports\n"));

	if (csp->commit_reports)
		csp->commit_reports (csp);
}

void
camel_junk_plugin_init (CamelJunkPlugin *csp)
{
	d(fprintf (stderr, "camel_junk_plugin_init\n"));

	if (csp->init)
		csp->init (csp);
}
