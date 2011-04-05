
#include <string.h>
#include <glib.h>
#include "utils.h"

/* 
 * EDS_MAIL_DEBUG should be a CSV
 * export EDS_MAIL_DEBUG=folder,store,session,micro,ipc
 * */

static int mail_debug_flag = 0;

void
mail_debug_int ()
{
	const char *log = g_getenv ("EDS_MAIL_DEBUG");
	char **tokens;

	if (log && *log) {
		int i=0;
		tokens = g_strsplit (log, ",", 0);
		
		while (tokens[i]) {
			if (strcmp (tokens[i], "folder") == 0)
				mail_debug_flag |= EMAIL_DEBUG_FOLDER;
			else if (strcmp (tokens[i], "store") == 0)
				mail_debug_flag |= EMAIL_DEBUG_STORE;
			else if (strcmp (tokens[i], "session") == 0)
				mail_debug_flag |= EMAIL_DEBUG_SESSION;
			else if (strcmp (tokens[i], "micro") == 0)
				mail_debug_flag |= EMAIL_DEBUG_MICRO;		
			else if (strcmp (tokens[i], "ipc") == 0)
				mail_debug_flag |= EMAIL_DEBUG_IPC;						
			else if (strcmp(tokens[i], "all") == 0)
				mail_debug_flag |= EMAIL_DEBUG_SESSION|EMAIL_DEBUG_STORE|EMAIL_DEBUG_STORE|EMAIL_DEBUG_MICRO|EMAIL_DEBUG_IPC;
			i++;
		}

		g_strfreev (tokens);
	}
}

gboolean
mail_debug_log (EMailDebugFlag flag)
{
	return (mail_debug_flag & flag) != 0;
}
