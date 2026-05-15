/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef PROMPT_USER_H
#define PROMPT_USER_H

#include <libebackend/libebackend.h>

/* initialize the GUI subsystem */
void
prompt_user_init (gint *argc,
		  gchar ***argv);

/* This is called when a request is initiated. The callback should not block,
 * and when a user responds, the e_user_prompter_server_response() should be called.
 */

void
prompt_user_show (EUserPrompterServer *server,
		  gint id,
		  const gchar *type,
		  const gchar *title,
		  const gchar *primary_text,
		  const gchar *secondary_text,
		  gboolean use_markup,
		  const GSList *button_captions);

#endif /* PROMPT_USER_H */
