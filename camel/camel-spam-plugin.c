/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Author:
 *  Radek Doulik <rodo@ximian.com>
 *
 * Copyright 2003 Ximian, Inc. (www.ximian.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#include <camel/camel-spam-plugin.h>

#define d(x) x

const char *
camel_spam_plugin_get_name (CamelSpamPlugin *csp)
{
	g_return_val_if_fail (csp->get_name != NULL, NULL);

	d(fprintf (stderr, "camel_spam_plugin_get_namen");)

	return (*csp->get_name) ();
}

int
camel_spam_plugin_check_spam (CamelSpamPlugin *csp, CamelMimeMessage *message)
{
	g_return_val_if_fail (csp->check_spam != NULL, FALSE);

	d(fprintf (stderr, "camel_spam_plugin_check_spam\n");)

	return (*csp->check_spam) (message);
}

void
camel_spam_plugin_report_spam (CamelSpamPlugin *csp, CamelMimeMessage *message)
{
	d(fprintf (stderr, "camel_spam_plugin_report_spam\n");)

	if (csp->report_spam)
		(*csp->report_spam) (message);
}

void
camel_spam_plugin_report_ham (CamelSpamPlugin *csp, CamelMimeMessage *message)
{
	d(fprintf (stderr, "camel_spam_plugin_report_ham\n");)

	if (csp->report_ham)
		(*csp->report_ham) (message);
}

void
camel_spam_plugin_commit_reports (CamelSpamPlugin *csp)
{
	d(fprintf (stderr, "camel_spam_plugin_commit_reports\n");)

	if (csp->commit_reports)
		(*csp->commit_reports) ();
}
