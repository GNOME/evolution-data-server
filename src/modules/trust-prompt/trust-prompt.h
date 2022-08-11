/*
 * trust-prompt.h
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef TRUST_PROMPT_H
#define TRUST_PROMPT_H

#include <libebackend/libebackend.h>

G_BEGIN_DECLS

/* This shows a trust-prompt. The function may not block and returns whether
 * showed a dialog or not. It calls e_user_prompter_server_extension_response()
 * when a user responded to the dialog with one of the TRUST_PROMPT_RESPONSE values.
*/

#define TRUST_PROMPT_RESPONSE_UNKNOWN			-1
#define TRUST_PROMPT_RESPONSE_REJECT			 0
#define TRUST_PROMPT_RESPONSE_ACCEPT_PERMANENTLY	 1
#define TRUST_PROMPT_RESPONSE_ACCEPT_TEMPORARILY	 2

gboolean
trust_prompt_show (EUserPrompterServerExtension *extension,
		   gint prompt_id,
		   const gchar *host,
		   const gchar *markup,
		   const gchar *base64_certificate_der,
		   const gchar *reason);

G_END_DECLS

#endif /* TRUST_PROMPT_H */
