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

#ifndef _CAMEL_SPAM_PLUGIN_H
#define _CAMEL_SPAM_PLUGIN_H

#include <camel/camel-mime-message.h>

#define CAMEL_SPAM_PLUGIN(x) ((CamelSpamPlugin *) x)

typedef struct _CamelSpamPlugin CamelSpamPlugin;

struct _CamelSpamPlugin
{
	/* spam filter human readable name, translated */
	const char * (*get_name) (void);

	/* should be set to 1 */
	int api_version;

	/* when called, it should return TRUE if message is identified as spam,
	   FALSE otherwise */
	int (*check_spam) (CamelMimeMessage *message);

	/* called when user identified a message to be spam */
	void (*report_spam) (CamelMimeMessage *message);

	/* called when user identified a message not to be spam */
	void (*report_ham) (CamelMimeMessage *message);

	/* called after one or more spam/ham(s) reported */
	void (*commit_reports) (void);
};

const char * camel_spam_plugin_get_name (CamelSpamPlugin *csp);
int camel_spam_plugin_check_spam (CamelSpamPlugin *csp, CamelMimeMessage *message);
void camel_spam_plugin_report_spam (CamelSpamPlugin *csp, CamelMimeMessage *message);
void camel_spam_plugin_report_ham (CamelSpamPlugin *csp, CamelMimeMessage *message);
void camel_spam_plugin_commit_reports (CamelSpamPlugin *csp);

#endif
