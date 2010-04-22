
#include <ctype.h>
#include <errno.h>
#include <string.h>

#include "camel-imapx-folder.h"
#include "camel-imapx-stream.h"
#include "camel-imapx-summary.h"
#include "camel-imapx-store-summary.h"
#include "camel-imapx-utils.h"
#include "camel-imapx-exception.h"

/* high-level parser state */
#define p(x)
/* debug */
#define d(x)

#include "camel-imapx-tokenise.h"
#define SUBFOLDER_DIR_NAME     "subfolders"

#ifdef __GNUC__
__inline
#endif
camel_imapx_id_t
imapx_tokenise (register const gchar *str, register guint len)
{
	struct _imapx_keyword *k = imapx_tokenise_struct(str, len);

	if (k)
		return k->id;
	return 0;
}

static void imapx_namespace_clear (CamelIMAPXStoreNamespace **ns);
static const gchar * rename_label_flag (const gchar *flag, gint len, gboolean server_to_evo);
static GPtrArray *imapx_parse_uids (CamelIMAPXStream *is, CamelException *ex);

/* flag table */
static struct {
	const gchar *name;
	guint32 flag;
} flag_table[] = {
	{ "\\ANSWERED", CAMEL_MESSAGE_ANSWERED },
	{ "\\DELETED", CAMEL_MESSAGE_DELETED },
	{ "\\DRAFT", CAMEL_MESSAGE_DRAFT },
	{ "\\FLAGGED", CAMEL_MESSAGE_FLAGGED },
	{ "\\SEEN", CAMEL_MESSAGE_SEEN },
	{ "\\RECENT", CAMEL_IMAPX_MESSAGE_RECENT },
	{ "\\*", CAMEL_MESSAGE_USER }
};

/* utility functions
   shoudl this be part of imapx-driver? */
/* mabye this should be a stream op? */
void
imapx_parse_flags(CamelIMAPXStream *stream, guint32 *flagsp, CamelFlag **user_flagsp, CamelException *ex)
/* throws IO,PARSE exception */
{
	gint tok, i;
	guint len;
	guchar *token, *p, c;
	guint32 flags = 0;

	*flagsp = flags;

	tok = camel_imapx_stream_token(stream, &token, &len, ex);
	if (tok == '(') {
		do {
			tok = camel_imapx_stream_token(stream, &token, &len, ex);
			if (tok == IMAPX_TOK_TOKEN) {
				p = token;
				// FIXME: ascii_toupper
				while ((c=*p))
					*p++ = toupper(c);
				for (i = 0; i < G_N_ELEMENTS (flag_table); i++)
					if (!strcmp((gchar *)token, flag_table[i].name)) {
						flags |= flag_table[i].flag;
						goto found;
					}
				if (user_flagsp) {
					const gchar *flag_name = rename_label_flag ((gchar *) token, strlen ((gchar *) token), TRUE);

					camel_flag_set(user_flagsp, flag_name, TRUE);

				}
			found:
				tok = tok; /* fixes stupid warning */
			} else if (tok != ')') {
				camel_exception_set (ex, 1, "expecting flag");
				return;
			}
		} while (tok != ')');
	} else {
		camel_exception_set (ex, 1, "expecting flag list");
		return;
	}

	*flagsp = flags;
}

/*
 * rename_flag
 * Converts label flag name on server to name used in Evolution or back.
 * if the flags does not match returns the original one as it is.
 * It will never return NULL, it will return empty string, instead.
 *
 * @flag: Flag to rename.
 * @len: Length of the flag name.
 * @server_to_evo: if TRUE, then converting server names to evo's names, if FALSE then opposite.
 */
static const gchar *
rename_label_flag (const gchar *flag, gint len, gboolean server_to_evo)
{
	gint i;
	const gchar *labels[] = {
		"$Label1", "$Labelimportant",
		"$Label2", "$Labelwork",
		"$Label3", "$Labelpersonal",
		"$Label4", "$Labeltodo",
		"$Label5", "$Labellater",
		NULL,      NULL };

	/* It really can pass zero-length flags inside, in that case it was able
	   to always add first label, which is definitely wrong. */
	if (!len || !flag || !*flag)
		return "";

	for (i = 0 + (server_to_evo ? 0 : 1); labels[i]; i = i + 2) {
		if (!g_ascii_strncasecmp (flag, labels[i], len))
			return labels [i + (server_to_evo ? 1 : -1)];
	}

	return flag;
}

void
imapx_write_flags(CamelStream *stream, guint32 flags, CamelFlag *user_flags, CamelException *ex)
/* throws IO exception */
{
	gint i;

	if (camel_stream_write(stream, "(", 1) == -1) {
		camel_exception_setv (ex, 1, "io error: %s", strerror(errno));
		return;
	}

	for (i=0;flags!=0 && i< G_N_ELEMENTS (flag_table);i++) {
		if (flag_table[i].flag & flags) {
			if (flags & CAMEL_IMAPX_MESSAGE_RECENT)
				continue;

			if (camel_stream_write (stream, flag_table[i].name, strlen(flag_table[i].name)) == -1) {
				camel_exception_setv (ex,1, "io error: %s", strerror(errno));
				return;
			}

			flags &= ~flag_table[i].flag;
			if (flags != 0 && user_flags == NULL)
				if (camel_stream_write(stream, " ", 1) == -1) {
					camel_exception_setv (ex, 1, "io error: %s", strerror(errno));
					return;
				}
		}
	}

	while (user_flags) {
		const gchar *flag_name = rename_label_flag (user_flags->name, strlen (user_flags->name), FALSE);

		if (camel_stream_write(stream, flag_name, strlen (flag_name)) == -1) {
			camel_exception_setv (ex, 1, "io error: %s", strerror(errno));
			return;
		}

		if (user_flags->next && camel_stream_write(stream, " ", 1) == -1) {
			camel_exception_setv (ex, 1, "io error: %s", strerror(errno));
			return;
		}

		user_flags = user_flags->next;
	}

	if (camel_stream_write(stream, ")", 1) == -1) {
		camel_exception_setv (ex, 1, "io error: %s", strerror(errno));
		return;
	}
}

static gboolean
imapx_update_user_flags (CamelMessageInfo *info, CamelFlag *server_user_flags)
{
	gboolean changed = FALSE;
	CamelMessageInfoBase *binfo = (CamelMessageInfoBase *) info;
	gboolean set_cal = FALSE;

	if (camel_flag_get (&binfo->user_flags, "$has_cal"))
		set_cal = TRUE;

	changed = camel_flag_list_copy(&binfo->user_flags, &server_user_flags);

	/* reset the calendar flag if it was set in messageinfo before */
	if (set_cal)
		camel_flag_set (&binfo->user_flags, "$has_cal", TRUE);

	return changed;
}

gboolean
imapx_update_message_info_flags (CamelMessageInfo *info, guint32 server_flags, CamelFlag *server_user_flags, CamelFolder *folder)
{
	gboolean changed = FALSE;
	CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) info;

	if (server_flags != xinfo->server_flags)
	{
		guint32 server_set, server_cleared;
		gint read=0, deleted=0, junk=0;

		server_set = server_flags & ~xinfo->server_flags;
		server_cleared = xinfo->server_flags & ~server_flags;

		if (server_set & CAMEL_MESSAGE_SEEN)
			read = 1;
		else if (server_cleared & CAMEL_MESSAGE_SEEN)
			read = -1;

		if (server_set & CAMEL_MESSAGE_DELETED)
			deleted = 1;
		else if (server_cleared & CAMEL_MESSAGE_DELETED)
			deleted = -1;

		if (server_set & CAMEL_MESSAGE_JUNK)
			junk = 1;
		else if (server_cleared & CAMEL_MESSAGE_JUNK)
			junk = -1;

		d(printf("%s %s %s %s\n", xinfo->info.uid, read == 1 ? "read" : ( read == -1 ? "unread" : ""),
					deleted == 1 ? "deleted" : ( deleted == -1 ? "undeleted" : ""),
					junk == 1 ? "junk" : ( junk == -1 ? "unjunked" : "")));

		if (read)
			folder->summary->unread_count -= read;
		if (deleted)
			folder->summary->deleted_count += deleted;
		if (junk)
			folder->summary->junk_count += junk;
		if (junk && !deleted)
			folder->summary->junk_not_deleted_count += junk;
		if (junk ||  deleted)
			folder->summary->visible_count -= junk ? junk : deleted;

		xinfo->info.flags = (xinfo->info.flags | server_set) & ~server_cleared;
		xinfo->server_flags = server_flags;
		xinfo->info.dirty = TRUE;
		if (info->summary)
			camel_folder_summary_touch (info->summary);
		changed = TRUE;
	}

	if ((folder->permanent_flags & CAMEL_MESSAGE_USER) != 0 && imapx_update_user_flags (info, server_user_flags))
		changed = TRUE;

	return changed;
}

void
imapx_set_message_info_flags_for_new_message (CamelMessageInfo *info, guint32 server_flags, CamelFlag *server_user_flags, CamelFolder *folder)
{
	CamelMessageInfoBase *binfo = (CamelMessageInfoBase *) info;
	CamelIMAPXMessageInfo *xinfo = (CamelIMAPXMessageInfo *) info;
	gint unread=0, deleted=0, junk=0;
	guint32 flags;

	binfo->flags |= server_flags;
	xinfo->server_flags = server_flags;

	if (folder->permanent_flags & CAMEL_MESSAGE_USER)
		imapx_update_user_flags (info, server_user_flags);

	/* update the summary count */
	flags = binfo->flags;

	if (!(flags & CAMEL_MESSAGE_SEEN))
		unread = 1;

	if (flags & CAMEL_MESSAGE_DELETED)
		deleted = 1;

	if (flags & CAMEL_MESSAGE_JUNK)
		junk = 1;

	if (folder->summary) {

		if (unread)
			folder->summary->unread_count += unread;
		if (deleted)
			folder->summary->deleted_count += deleted;
		if (junk)
			folder->summary->junk_count += junk;
		if (junk && !deleted)
			folder->summary->junk_not_deleted_count += junk;
		folder->summary->visible_count++;
		if (junk ||  deleted)
			folder->summary->visible_count -= junk ? junk : deleted;

		folder->summary->saved_count++;
		camel_folder_summary_touch(folder->summary);
	}

	binfo->flags &= ~CAMEL_MESSAGE_FOLDER_FLAGGED;
}

void
imapx_update_summary_for_removed_message (CamelMessageInfo *info, CamelFolder *folder)
{
	CamelMessageInfoBase *dinfo = (CamelMessageInfoBase *) info;
	gint unread=0, deleted=0, junk=0;
	guint32 flags;

	flags = dinfo->flags;
	if (!(flags & CAMEL_MESSAGE_SEEN))
		unread = 1;

	if (flags & CAMEL_MESSAGE_DELETED)
		deleted = 1;

	if (flags & CAMEL_MESSAGE_JUNK)
		junk = 1;

	if (unread)
		folder->summary->unread_count--;

	if (deleted)
		folder->summary->deleted_count--;
	if (junk)
		folder->summary->junk_count--;

	if (junk && !deleted)
		folder->summary->junk_not_deleted_count--;

	if (!junk &&  !deleted)
		folder->summary->visible_count--;

	folder->summary->saved_count--;
}

void
imapx_update_store_summary (CamelFolder *folder)
{
	CamelStoreInfo *si;

	si = camel_store_summary_path ((CamelStoreSummary *) ((CamelIMAPXStore *) folder->parent_store)->summary, folder->full_name);
	if (si) {
		guint32 unread, total;

		camel_object_get(folder, NULL, CAMEL_FOLDER_TOTAL, &total, CAMEL_FOLDER_UNREAD, &unread, NULL);
		if (si->unread != unread || si->total != total) {
			si->unread = unread;
			si->total = total;

			camel_store_summary_touch ((CamelStoreSummary *)((CamelIMAPXStore *) folder->parent_store)->summary);
			camel_store_summary_save ((CamelStoreSummary *)((CamelIMAPXStore *) folder->parent_store)->summary);
		}
	}
}

/*
capability_data ::= "CAPABILITY" SPACE [1#capability SPACE] "IMAP4rev1"
                    [SPACE 1#capability]
                    ;; IMAP4rev1 servers which offer RFC 1730
                    ;; compatibility MUST list "IMAP4" as the first
                    ;; capability.
*/

struct {
	const gchar *name;
	guint32 flag;
} capa_table[] = {
	{ "IMAP4", IMAPX_CAPABILITY_IMAP4 },
	{ "IMAP4REV1", IMAPX_CAPABILITY_IMAP4REV1 },
	{ "STATUS",  IMAPX_CAPABILITY_STATUS } ,
	{ "NAMESPACE", IMAPX_CAPABILITY_NAMESPACE },
	{ "UIDPLUS",  IMAPX_CAPABILITY_UIDPLUS },
	{ "LITERAL+", IMAPX_CAPABILITY_LITERALPLUS },
	{ "STARTTLS", IMAPX_CAPABILITY_STARTTLS },
	{ "IDLE", IMAPX_CAPABILITY_IDLE },
};

struct _capability_info *
imapx_parse_capability(CamelIMAPXStream *stream, CamelException *ex)
{
	gint tok, i;
	guint len;
	guchar *token, *p, c;
	gboolean free_token = FALSE;
	struct _capability_info * cinfo;

	cinfo = g_malloc0(sizeof(*cinfo));
	cinfo->auth_types = g_hash_table_new_full (g_str_hash, g_str_equal, (GDestroyNotify) g_free, NULL);

	/* FIXME: handle auth types */
	while (!camel_exception_is_set (ex) && (tok = camel_imapx_stream_token(stream, &token, &len, ex)) != '\n') {
		switch (tok) {
			case 43:
				token = (guchar *) g_strconcat ((gchar *)token, "+", NULL);
				free_token = TRUE;
			case IMAPX_TOK_TOKEN:
			case IMAPX_TOK_STRING:
				p = token;
				while ((c = *p))
					*p++ = toupper(c);
				if (!strncmp ((gchar *) token, "AUTH=", 5)) {
					g_hash_table_insert (cinfo->auth_types,
							g_strdup ((gchar *)token + 5),
							GINT_TO_POINTER (1));
					break;
				}
			case IMAPX_TOK_INT:
				d(printf(" cap: '%s'\n", token));
				for (i = 0; i < G_N_ELEMENTS (capa_table); i++)
					if (!strcmp((gchar *) token, capa_table[i].name))
						cinfo->capa |= capa_table[i].flag;
				if (free_token) {
					g_free (token);
					token = NULL;
				}
				free_token = FALSE;
				break;
			default:
				camel_exception_set (ex, 1, "capability: expecting name");
				break;
		}
	}

	if (camel_exception_is_set (ex)) {
		imapx_free_capability(cinfo);
		cinfo = NULL;
	}

	return cinfo;
}

void imapx_free_capability(struct _capability_info *cinfo)
{
	g_hash_table_destroy (cinfo->auth_types);
	g_free(cinfo);
}

struct _CamelIMAPXNamespaceList *
imapx_parse_namespace_list (CamelIMAPXStream *stream, CamelException *ex)
{
	CamelIMAPXStoreNamespace *namespaces[3], *node, *tail;
	CamelIMAPXNamespaceList *nsl = NULL;
	gint tok, i;
	guint len;
	guchar *token;
	gint n = 0;

	nsl = g_malloc0(sizeof(CamelIMAPXNamespaceList));
	nsl->personal = NULL;
	nsl->shared = NULL;
	nsl->other = NULL;

	tok = camel_imapx_stream_token (stream, &token, &len, ex);
	do {
		namespaces[n] = NULL;
		tail = (CamelIMAPXStoreNamespace *) &namespaces[n];

		if (tok == '(') {
			tok = camel_imapx_stream_token (stream, &token, &len, ex);

			while (tok == '(') {
				tok = camel_imapx_stream_token (stream, &token, &len, ex);
				if (tok != IMAPX_TOK_STRING) {
					camel_exception_set (ex, 1, "namespace: expected a string path name");
					goto exception;
				}

				node = g_new0 (CamelIMAPXStoreNamespace, 1);
				node->next = NULL;
				node->path = g_strdup ((gchar *) token);

				tok = camel_imapx_stream_token (stream, &token, &len, ex);

				if (tok == IMAPX_TOK_STRING) {
					if (strlen ((gchar *) token) == 1) {
						node->sep = *token;
					} else {
						if (*token)
							node->sep = node->path [strlen (node->path) - 1];
						else
							node->sep = '\0';
					}
				} else if (tok == IMAPX_TOK_TOKEN) {
					/* will a NIL be possible here? */
					node->sep = '\0';
				} else {
					camel_exception_set (ex, 1, "namespace: expected a string separator");
					g_free (node->path);
					g_free (node);
					goto exception;
				}

				tail->next = node;
				tail = node;

				if (*node->path && node->path [strlen (node->path) -1] == node->sep)
					node->path [strlen (node->path) - 1] = '\0';

				if (!g_ascii_strncasecmp (node->path, "INBOX", 5) &&
						(node->path [6] == '\0' || node->path [6] == node->sep ))
					memcpy (node->path, "INBOX", 5);
				
				/* TODO remove full_name later. not required */
				node->full_name = g_strdup (node->path);

				tok = camel_imapx_stream_token (stream, &token, &len, ex);
				if (tok != ')') {
					camel_exception_set (ex, 1, "namespace: expected a ')'");
					goto exception;
				}

				tok = camel_imapx_stream_token (stream, &token, &len, ex);
			}

			if (tok != ')') {
				camel_exception_set (ex, 1, "namespace: expected a ')'");
				goto exception;
			}

		} else if (tok == IMAPX_TOK_TOKEN && !strcmp ((gchar *) token, "NIL")) {
			namespaces [n] = NULL;
		} else {
			camel_exception_set (ex, 1, "namespace: expected either a '(' or NIL");
			goto exception;
		}

		tok = camel_imapx_stream_token (stream, &token, &len, ex);
		n++;
	} while (n < 3);

	nsl->personal = namespaces [0];
	nsl->shared = namespaces [1];
	nsl->other = namespaces [2];

	return nsl;
exception:
	g_free (nsl);
	for (i=0; i < 3; i++)
		imapx_namespace_clear (&namespaces [i]);

	return NULL;
}

/*
body            ::= "(" body_type_1part / body_type_mpart ")"

body_extension  ::= nstring / number / "(" 1#body_extension ")"
                    ;; Future expansion.  Client implementations
                    ;; MUST accept body_extension fields.  Server
                    ;; implementations MUST NOT generate
                    ;; body_extension fields except as defined by
                    ;; future standard or standards-track
                    ;; revisions of this specification.

body_ext_1part  ::= body_fld_md5 [SPACE body_fld_dsp
                    [SPACE body_fld_lang
                    [SPACE 1#body_extension]]]
                    ;; MUST NOT be returned on non-extensible
                    ;; "BODY" fetch

body_ext_mpart  ::= body_fld_param
                    [SPACE body_fld_dsp SPACE body_fld_lang
                    [SPACE 1#body_extension]]
                    ;; MUST NOT be returned on non-extensible
                    ;; "BODY" fetch

body_fields     ::= body_fld_param SPACE body_fld_id SPACE
                    body_fld_desc SPACE body_fld_enc SPACE
                    body_fld_octets

body_fld_desc   ::= nstring

body_fld_dsp    ::= "(" string SPACE body_fld_param ")" / nil

body_fld_enc    ::= (<"> ("7BIT" / "8BIT" / "BINARY" / "BASE64"/
                    "QUOTED-PRINTABLE") <">) / string

body_fld_id     ::= nstring

body_fld_lang   ::= nstring / "(" 1#string ")"

body_fld_lines  ::= number

body_fld_md5    ::= nstring

body_fld_octets ::= number

body_fld_param  ::= "(" 1#(string SPACE string) ")" / nil

body_type_1part ::= (body_type_basic / body_type_msg / body_type_text)
                    [SPACE body_ext_1part]

body_type_basic ::= media_basic SPACE body_fields
                    ;; MESSAGE subtype MUST NOT be "RFC822"

body_type_mpart ::= 1*body SPACE media_subtype
                    [SPACE body_ext_mpart]

body_type_msg   ::= media_message SPACE body_fields SPACE envelope
                    SPACE body SPACE body_fld_lines

body_type_text  ::= media_text SPACE body_fields SPACE body_fld_lines

envelope        ::= "(" env_date SPACE env_subject SPACE env_from
                    SPACE env_sender SPACE env_reply_to SPACE env_to
                    SPACE env_cc SPACE env_bcc SPACE env_in_reply_to
                    SPACE env_message_id ")"

env_bcc         ::= "(" 1*address ")" / nil

env_cc          ::= "(" 1*address ")" / nil

env_date        ::= nstring

env_from        ::= "(" 1*address ")" / nil

env_in_reply_to ::= nstring

env_message_id  ::= nstring

env_reply_to    ::= "(" 1*address ")" / nil

env_sender      ::= "(" 1*address ")" / nil

env_subject     ::= nstring

env_to          ::= "(" 1*address ")" / nil

media_basic     ::= (<"> ("APPLICATION" / "AUDIO" / "IMAGE" /
                    "MESSAGE" / "VIDEO") <">) / string)
                    SPACE media_subtype
                    ;; Defined in [MIME-IMT]

media_message   ::= <"> "MESSAGE" <"> SPACE <"> "RFC822" <">
                    ;; Defined in [MIME-IMT]

media_subtype   ::= string
                    ;; Defined in [MIME-IMT]

media_text      ::= <"> "TEXT" <"> SPACE media_subtype
                    ;; Defined in [MIME-IMT]

 ( "type" "subtype"  body_fields [envelope body body_fld_lines]
                                 [body_fld_lines]

 (("TEXT" "PLAIN" ("CHARSET"
                     "US-ASCII") NIL NIL "7BIT" 1152 23)("TEXT" "PLAIN"
                     ("CHARSET" "US-ASCII" "NAME" "cc.diff")
                     "<960723163407.20117h@cac.washington.edu>"
                     "Compiler diff" "BASE64" 4554 73) "MIXED"))

*/

/*
struct _body_fields {
	CamelContentType *ct;
	gchar *msgid, *desc;
	CamelTransferEncoding encoding;
	guint32 size;
	};*/

void
imapx_free_body(struct _CamelMessageContentInfo *cinfo)
{
	struct _CamelMessageContentInfo *list, *next;

	list = cinfo->childs;
	while (list) {
		next = list->next;
		imapx_free_body(list);
		list = next;
	}

	if (cinfo->type)
		camel_content_type_unref(cinfo->type);
	g_free(cinfo->id);
	g_free(cinfo->description);
	g_free(cinfo->encoding);
	g_free(cinfo);
}

void
imapx_parse_param_list(CamelIMAPXStream *is, struct _camel_header_param **plist, CamelException *ex)
{
	gint tok;
	guint len;
	guchar *token;
	gchar *param;

	p(printf("body_fld_param\n"));

	/* body_fld_param  ::= "(" 1#(string SPACE string) ")" / nil */
	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok == '(') {
		while (1) {
			tok = camel_imapx_stream_token(is, &token, &len, ex);
			if (tok == ')')
				break;
			camel_imapx_stream_ungettoken(is, tok, token, len);

			camel_imapx_stream_astring(is, &token, ex);
			param = alloca (strlen ((gchar *) token)+1);
			strcpy(param, (gchar *) token);
			camel_imapx_stream_astring(is, &token, ex);
			camel_header_set_param(plist, param, (gchar *) token);
		}
	} /* else check nil?  no need */
}

struct _CamelContentDisposition *
imapx_parse_ext_optional(CamelIMAPXStream *is, CamelException *ex)
{
	gint tok;
	guint len;
	guchar *token;
	struct _CamelContentDisposition *dinfo = NULL;

	/* this parses both extension types, from the body_fld_dsp onwards */
	/* although the grammars are different, they can be parsed the same way */

	/* body_ext_1part  ::= body_fld_md5 [SPACE body_fld_dsp
	   [SPACE body_fld_lang
	   [SPACE 1#body_extension]]]
	   ;; MUST NOT be returned on non-extensible
	   ;; "BODY" fetch */

	/* body_ext_mpart  ::= body_fld_param
	   [SPACE body_fld_dsp SPACE body_fld_lang
	   [SPACE 1#body_extension]]
	   ;; MUST NOT be returned on non-extensible
	   ;; "BODY" fetch */

	/* body_fld_dsp    ::= "(" string SPACE body_fld_param ")" / nil */

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	switch (tok) {
		case '(':
			dinfo = g_malloc0(sizeof(*dinfo));
			dinfo->refcount = 1;
			/* should be string */
			camel_imapx_stream_astring(is, &token, ex);

			dinfo->disposition = g_strdup((gchar *) token);
			imapx_parse_param_list(is, &dinfo->params, ex);
		case IMAPX_TOK_TOKEN:
			d(printf("body_fld_dsp: NIL\n"));
			break;
		default:
			camel_exception_set (ex, 1, "body_fld_disp: expecting nil or list");
			return NULL;
	}

	p(printf("body_fld_lang\n"));

	/* body_fld_lang   ::= nstring / "(" 1#string ")" */

	/* we just drop the lang string/list, save it somewhere? */

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	switch (tok) {
		case '(':
			while (1) {
				tok = camel_imapx_stream_token(is, &token, &len, ex);
				if (tok == ')') {
					break;
				} else if (tok != IMAPX_TOK_STRING) {
					camel_exception_set (ex, 1, "expecting string");
					break;
				}
			}
			break;
		case IMAPX_TOK_TOKEN:
			d(printf("body_fld_lang = nil\n"));
			/* treat as 'nil' */
			break;
		case IMAPX_TOK_STRING:
			/* we have a string */
			break;
		case IMAPX_TOK_LITERAL:
			/* we have a literal string */
			camel_imapx_stream_set_literal(is, len);
			while ((tok = camel_imapx_stream_getl(is, &token, &len)) > 0) {
				d(printf("Skip literal data '%.*s'\n", (gint)len, token));
			}
			break;

	}

	if camel_exception_is_set (ex) {
		if (dinfo)
			camel_content_disposition_unref(dinfo);
	}

	return dinfo;
}

struct _CamelMessageContentInfo *
imapx_parse_body_fields(CamelIMAPXStream *is, CamelException *ex)
{
	guchar *token;
	gchar  *type;
	struct _CamelMessageContentInfo *cinfo;

	/* body_fields     ::= body_fld_param SPACE body_fld_id SPACE
	   body_fld_desc SPACE body_fld_enc SPACE
	   body_fld_octets */

	p(printf("body_fields\n"));

	cinfo = g_malloc0(sizeof(*cinfo));

	/* this should be string not astring */
	camel_imapx_stream_astring(is, &token, ex);
	if (camel_exception_is_set (ex))
		goto error;
	type = alloca(strlen( (gchar *) token)+1);
	strcpy(type, (gchar *) token);
	camel_imapx_stream_astring(is, &token, ex);
	if (camel_exception_is_set (ex))
		goto error;
	cinfo->type = camel_content_type_new(type, (gchar *) token);
	imapx_parse_param_list(is, &cinfo->type->params, ex);
	if (camel_exception_is_set (ex))
		goto error;

	/* body_fld_id     ::= nstring */
	camel_imapx_stream_nstring(is, &token, ex);
	if (camel_exception_is_set (ex))
		goto error;
	cinfo->id = g_strdup((gchar *) token);

	/* body_fld_desc   ::= nstring */
	camel_imapx_stream_nstring(is, &token, ex);
	if (camel_exception_is_set (ex))
		goto error;
	cinfo->description = g_strdup((gchar *) token);

	/* body_fld_enc    ::= (<"> ("7BIT" / "8BIT" / "BINARY" / "BASE64"/
	   "QUOTED-PRINTABLE") <">) / string */
	camel_imapx_stream_astring(is, &token, ex);
	if (camel_exception_is_set (ex))
		goto error;
	cinfo->encoding = g_strdup((gchar *) token);

	/* body_fld_octets ::= number */
	cinfo->size = camel_imapx_stream_number(is, ex);
	if (camel_exception_is_set (ex))
		goto error;

	return cinfo;
error:
	imapx_free_body(cinfo);
	return cinfo;
}

struct _camel_header_address *
imapx_parse_address_list(CamelIMAPXStream *is, CamelException *ex)
/* throws PARSE,IO exception */
{
	gint tok;
	guint len;
	guchar *token, *host;
	gchar *mbox;
	struct _camel_header_address *list = NULL;

	/* "(" 1*address ")" / nil */

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok == '(') {
		while (1) {
			struct _camel_header_address *addr, *group = NULL;

			/* address         ::= "(" addr_name SPACE addr_adl SPACE addr_mailbox
			   SPACE addr_host ")" */
			tok = camel_imapx_stream_token(is, &token, &len, ex);
			if (tok == ')')
				break;
			if (tok != '(') {
				camel_header_address_list_clear(&list);
				camel_exception_set (ex, 1, "missing '(' for address");
				return NULL;
			}

			addr = camel_header_address_new();
			addr->type = CAMEL_HEADER_ADDRESS_NAME;
			tok = camel_imapx_stream_nstring(is, &token, ex);
			addr->name = g_strdup((gchar *) token);
			/* we ignore the route, nobody uses it in the real world */
			tok = camel_imapx_stream_nstring(is, &token, ex);

			/* [RFC-822] group syntax is indicated by a special
			   form of address structure in which the host name
			   field is NIL.  If the mailbox name field is also
			   NIL, this is an end of group marker (semi-colon in
			   RFC 822 syntax).  If the mailbox name field is
			   non-NIL, this is a start of group marker, and the
			   mailbox name field holds the group name phrase. */

			tok = camel_imapx_stream_nstring(is,(guchar **) &mbox, ex);
			mbox = g_strdup(mbox);
			tok = camel_imapx_stream_nstring(is, &host, ex);
			if (host == NULL) {
				if (mbox == NULL) {
					group = NULL;
				} else {
					d(printf("adding group '%s'\n", mbox));
					g_free(addr->name);
					addr->name = mbox;
					addr->type = CAMEL_HEADER_ADDRESS_GROUP;
					camel_header_address_list_append(&list, addr);
					group = addr;
				}
			} else {
				addr->v.addr = g_strdup_printf("%s%s%s", mbox? mbox:"", host?"@":"", host?(gchar *)host:"");
				g_free(mbox);
				d(printf("adding address '%s'\n", addr->v.addr));
				if (group != NULL)
					camel_header_address_add_member(group, addr);
				else
					camel_header_address_list_append(&list, addr);
			}
			do {
				tok = camel_imapx_stream_token(is, &token, &len, ex);
			} while (tok != ')');
		}
	} else {
		d(printf("empty, nil '%s'\n", token));
	}

	/* CHEN TODO handle exception at required places */

	return list;
}

struct _CamelMessageInfo *
imapx_parse_envelope(CamelIMAPXStream *is, CamelException *ex)
{
	gint tok;
	guint len;
	guchar *token;
	struct _camel_header_address *addr, *addr_from;
	gchar *addrstr;
	struct _CamelMessageInfoBase *minfo;

	/* envelope        ::= "(" env_date SPACE env_subject SPACE env_from
	   SPACE env_sender SPACE env_reply_to SPACE env_to
	   SPACE env_cc SPACE env_bcc SPACE env_in_reply_to
	   SPACE env_message_id ")" */

	p(printf("envelope\n"));

	minfo = (CamelMessageInfoBase *)camel_message_info_new(NULL);

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok != '(') {
		camel_message_info_free(minfo);
		camel_exception_set (ex, 1, "envelope: expecting '('");
		return NULL;
	}

	/* env_date        ::= nstring */
	camel_imapx_stream_nstring(is, &token, ex);
	minfo->date_sent = camel_header_decode_date((gchar *) token, NULL);

	/* env_subject     ::= nstring */
	tok = camel_imapx_stream_nstring(is, &token, ex);
	minfo->subject = camel_pstring_strdup((gchar *) token);

	/* we merge from/sender into from, append should probably merge more smartly? */

	/* env_from        ::= "(" 1*address ")" / nil */
	addr_from = imapx_parse_address_list(is, ex);

	/* env_sender      ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list(is, ex);
	if (addr_from) {
		camel_header_address_list_clear(&addr);
#if 0
		if (addr)
			camel_header_address_list_append_list(&addr_from, &addr);
#endif
	} else {
		if (addr)
			addr_from = addr;
	}

	if (addr_from) {
		addrstr = camel_header_address_list_format(addr_from);
		minfo->from = camel_pstring_strdup(addrstr);
		g_free(addrstr);
		camel_header_address_list_clear(&addr_from);
	}

	/* we dont keep reply_to */

	/* env_reply_to    ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list(is, ex);
	camel_header_address_list_clear(&addr);

	/* env_to          ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list(is, ex);
	if (addr) {
		addrstr = camel_header_address_list_format(addr);
		minfo->to = camel_pstring_strdup(addrstr);
		g_free(addrstr);
		camel_header_address_list_clear(&addr);
	}

	/* env_cc          ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list(is, ex);
	if (addr) {
		addrstr = camel_header_address_list_format(addr);
		minfo->cc = camel_pstring_strdup(addrstr);
		g_free(addrstr);
		camel_header_address_list_clear(&addr);
	}

	/* we dont keep bcc either */

	/* env_bcc         ::= "(" 1*address ")" / nil */
	addr = imapx_parse_address_list(is, ex);
	camel_header_address_list_clear(&addr);

	/* FIXME: need to put in-reply-to into references hash list */

	/* env_in_reply_to ::= nstring */
	tok = camel_imapx_stream_nstring(is, &token, ex);

	/* FIXME: need to put message-id into message-id hash */

	/* env_message_id  ::= nstring */
	tok = camel_imapx_stream_nstring(is, &token, ex);

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok != ')') {
		camel_message_info_free(minfo);
		camel_exception_set (ex, 1, "expecting ')'");
		return NULL;
	}

	/* CHEN TODO handle exceptions better */

	return (CamelMessageInfo *)minfo;
}

struct _CamelMessageContentInfo *
imapx_parse_body(CamelIMAPXStream *is, CamelException *ex)
{
	gint tok;
	guint len;
	guchar *token;
	struct _CamelMessageContentInfo * cinfo = NULL;
	struct _CamelMessageContentInfo *subinfo, *last;
	struct _CamelContentDisposition * dinfo = NULL;
	struct _CamelMessageInfo * minfo = NULL;

	/* body            ::= "(" body_type_1part / body_type_mpart ")" */

	p(printf("body\n"));

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok != '(') {
		camel_exception_set (ex, 1, "body: expecting '('");
		return NULL;
	}

	/* 1*body (optional for multiparts) */
	tok = camel_imapx_stream_token(is, &token, &len, ex);
	camel_imapx_stream_ungettoken(is, tok, token, len);
	if (tok == '(') {
		/* body_type_mpart ::= 1*body SPACE media_subtype
		   [SPACE body_ext_mpart] */

		cinfo = g_malloc0(sizeof(*cinfo));
		last = (struct _CamelMessageContentInfo *)&cinfo->childs;
		do {
			subinfo = imapx_parse_body(is, ex);
			last->next = subinfo;
			last = subinfo;
			subinfo->parent = cinfo;
			tok = camel_imapx_stream_token(is, &token, &len, ex);
			camel_imapx_stream_ungettoken(is, tok, token, len);
		} while (tok == '(');

		d(printf("media_subtype\n"));

		camel_imapx_stream_astring(is, &token, ex);
		cinfo->type = camel_content_type_new("multipart", (gchar *) token);

		/* body_ext_mpart  ::= body_fld_param
		   [SPACE body_fld_dsp SPACE body_fld_lang
		   [SPACE 1#body_extension]]
		   ;; MUST NOT be returned on non-extensible
		   ;; "BODY" fetch */

		d(printf("body_ext_mpart\n"));

		tok = camel_imapx_stream_token(is, &token, &len, ex);
		camel_imapx_stream_ungettoken(is, tok, token, len);
		if (tok == '(') {
			imapx_parse_param_list(is, &cinfo->type->params, ex);

			/* body_fld_dsp    ::= "(" string SPACE body_fld_param ")" / nil */

			tok = camel_imapx_stream_token(is, &token, &len, ex);
			camel_imapx_stream_ungettoken(is, tok, token, len);
			if (tok == '(' || tok == IMAPX_TOK_TOKEN) {
				dinfo = imapx_parse_ext_optional(is, ex);
				/* other extension fields?, soaked up below */
			} else {
				camel_imapx_stream_ungettoken(is, tok, token, len);
			}
		}
	} else {
		/* body_type_1part ::= (body_type_basic / body_type_msg / body_type_text)
		   [SPACE body_ext_1part]

		   body_type_basic ::= media_basic SPACE body_fields
		   body_type_text  ::= media_text SPACE body_fields SPACE body_fld_lines
		   body_type_msg   ::= media_message SPACE body_fields SPACE envelope
		   SPACE body SPACE body_fld_lines */

		d(printf("Single part body\n"));

		cinfo = imapx_parse_body_fields(is, ex);

		d(printf("envelope?\n"));

		/* do we have an envelope following */
		tok = camel_imapx_stream_token(is, &token, &len, ex);
		camel_imapx_stream_ungettoken(is, tok, token, len);
		if (tok == '(') {
			/* what do we do with the envelope?? */
			minfo = imapx_parse_envelope(is, ex);
			/* what do we do with the message content info?? */
			//((CamelMessageInfoBase *)minfo)->content = imapx_parse_body(is);
			camel_message_info_free(minfo);
			minfo = NULL;
			d(printf("Scanned envelope - what do i do with it?\n"));
		}

		d(printf("fld_lines?\n"));

		/* do we have fld_lines following? */
		tok = camel_imapx_stream_token(is, &token, &len, ex);
		if (tok == IMAPX_TOK_INT) {
			d(printf("field lines: %s\n", token));
			tok = camel_imapx_stream_token(is, &token, &len, ex);
		}
		camel_imapx_stream_ungettoken(is, tok, token, len);

		/* body_ext_1part  ::= body_fld_md5 [SPACE body_fld_dsp
		   [SPACE body_fld_lang
		   [SPACE 1#body_extension]]]
		   ;; MUST NOT be returned on non-extensible
		   ;; "BODY" fetch */

		d(printf("extension data?\n"));

		if (tok != ')') {
			camel_imapx_stream_nstring(is, &token, ex);

			d(printf("md5: %s\n", token?(gchar *)token:"NIL"));

			/* body_fld_dsp    ::= "(" string SPACE body_fld_param ")" / nil */

			tok = camel_imapx_stream_token(is, &token, &len, ex);
			camel_imapx_stream_ungettoken(is, tok, token, len);
			if (tok == '(' || tok == IMAPX_TOK_TOKEN) {
				dinfo = imapx_parse_ext_optional(is, ex);
				/* then other extension fields, soaked up below */
			}
		}
	}

	/* soak up any other extension fields that may be present */
	/* there should only be simple tokens, no lists */
	do {
		tok = camel_imapx_stream_token(is, &token, &len, ex);
		if (tok != ')') {
			d(printf("Dropping extension data '%s'\n", token));
		}
	} while (tok != ')');

	/* CHEN TODO handle exceptions better */
	if (camel_exception_is_set (ex)) {
		if (cinfo)
			imapx_free_body(cinfo);
		if (dinfo)
			camel_content_disposition_unref(dinfo);
		if (minfo)
			camel_message_info_free(minfo);
		return NULL;
	}

	/* FIXME: do something with the disposition, currently we have no way to pass it out? */
	if (dinfo)
		camel_content_disposition_unref(dinfo);

	return cinfo;
}

gchar *
imapx_parse_section(CamelIMAPXStream *is, CamelException *ex)
{
	gint tok;
	guint len;
	guchar *token;
	gchar * section = NULL;

	/* currently we only return the part within the [section] specifier
	   any header fields are parsed, but dropped */

	/*
	  section         ::= "[" [section_text /
	  (nz_number *["." nz_number] ["." (section_text / "MIME")])] "]"

	  section_text    ::= "HEADER" / "HEADER.FIELDS" [".NOT"]
	  SPACE header_list / "TEXT"
	*/

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok != '[') {
		camel_exception_set (ex, 1, "section: expecting '['");
		return NULL;
	}

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok == IMAPX_TOK_INT || tok == IMAPX_TOK_TOKEN)
		section = g_strdup((gchar *) token);
	else if (tok == ']') {
		section = g_strdup("");
		camel_imapx_stream_ungettoken(is, tok, token, len);
	} else {
		camel_exception_set (ex, 1, "section: expecting token");
		return NULL;
	}

	/* header_list     ::= "(" 1#header_fld_name ")"
	   header_fld_name ::= astring */

	/* we dont need the header specifiers */
	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok == '(') {
		do {
			tok = camel_imapx_stream_token(is, &token, &len, ex);
			if (tok == IMAPX_TOK_STRING || tok == IMAPX_TOK_TOKEN || tok == IMAPX_TOK_INT) {
				/* ?do something? */
			} else if (tok != ')') {
				camel_exception_set (ex, 1, "section: header fields: expecting string");
				g_free (section);
				return NULL;
			}
		} while (tok != ')');
		tok = camel_imapx_stream_token(is, &token, &len, ex);
	}

	if (tok != ']') {
		camel_exception_set (ex, 1, "section: expecting ']'");
		g_free(section);
		return NULL;
	}

	return section;
}

void
imapx_free_fetch(struct _fetch_info *finfo)
{
	if (finfo == NULL)
		return;

	if (finfo->body)
		g_object_unref (finfo->body);
	if (finfo->text)
		g_object_unref (finfo->text);
	if (finfo->header)
		g_object_unref (finfo->header);
	if (finfo->minfo)
		camel_message_info_free(finfo->minfo);
	if (finfo->cinfo)
		imapx_free_body(finfo->cinfo);
	camel_flag_list_free(&finfo->user_flags);
	g_free(finfo->date);
	g_free(finfo->section);
	g_free(finfo->uid);
	g_free(finfo);
}

/* debug, dump one out */
void
imapx_dump_fetch(struct _fetch_info *finfo)
{
	CamelStream *sout;
	gint fd;

	d(printf("Fetch info:\n"));
	if (finfo == NULL) {
		d(printf("Empty\n"));
		return;
	}

	fd = dup(1);
	sout = camel_stream_fs_new_with_fd(fd);
	if (finfo->body) {
		camel_stream_printf(sout, "Body content:\n");
		camel_stream_write_to_stream(finfo->body, sout);
		camel_stream_reset(finfo->body);
	}
	if (finfo->text) {
		camel_stream_printf(sout, "Text content:\n");
		camel_stream_write_to_stream(finfo->text, sout);
		camel_stream_reset(finfo->text);
	}
	if (finfo->header) {
		camel_stream_printf(sout, "Header content:\n");
		camel_stream_write_to_stream(finfo->header, sout);
		camel_stream_reset(finfo->header);
	}
	if (finfo->minfo) {
		camel_stream_printf(sout, "Message Info:\n");
		camel_message_info_dump(finfo->minfo);
	}
	if (finfo->cinfo) {
		camel_stream_printf(sout, "Content Info:\n");
		//camel_content_info_dump(finfo->cinfo, 0);
	}
	if (finfo->got & FETCH_SIZE)
		camel_stream_printf(sout, "Size: %d\n", (gint)finfo->size);
	if (finfo->got & FETCH_BODY)
		camel_stream_printf(sout, "Offset: %d\n", (gint)finfo->offset);
	if (finfo->got & FETCH_FLAGS)
		camel_stream_printf(sout, "Flags: %08x\n", (gint)finfo->flags);
	if (finfo->date)
		camel_stream_printf(sout, "Date: '%s'\n", finfo->date);
	if (finfo->section)
		camel_stream_printf(sout, "Section: '%s'\n", finfo->section);
	if (finfo->date)
		camel_stream_printf(sout, "UID: '%s'\n", finfo->uid);
	g_object_unref (sout);
}

struct _fetch_info *
imapx_parse_fetch(CamelIMAPXStream *is, CamelException *ex)
{
	gint tok;
	guint len;
	guchar *token, *p, c;
	struct _fetch_info *finfo;

	finfo = g_malloc0(sizeof(*finfo));

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok != '(') {
		camel_exception_set (ex, 1, "fetch: expecting '('");
		g_free (finfo);
		return NULL;
	}

	while ((tok = camel_imapx_stream_token(is, &token, &len, ex)) == IMAPX_TOK_TOKEN) {

		p = token;
		while ((c=*p))
			*p++ = toupper(c);

		switch (imapx_tokenise((gchar *) token, len)) {
			case IMAPX_ENVELOPE:
				finfo->minfo = imapx_parse_envelope(is, ex);
				finfo->got |= FETCH_MINFO;
				break;
			case IMAPX_FLAGS:
				imapx_parse_flags(is, &finfo->flags, &finfo->user_flags, ex);
				finfo->got |= FETCH_FLAGS;
				break;
			case IMAPX_INTERNALDATE:
				camel_imapx_stream_nstring(is, &token, ex);
				/* TODO: convert to camel format? */
				finfo->date = g_strdup((gchar *) token);
				finfo->got |= FETCH_DATE;
				break;
			case IMAPX_RFC822_HEADER:
				camel_imapx_stream_nstring_stream(is, &finfo->header, ex);
				finfo->got |= FETCH_HEADER;
				break;
			case IMAPX_RFC822_TEXT:
				camel_imapx_stream_nstring_stream(is, &finfo->text, ex);
				finfo->got |= FETCH_TEXT;
				break;
			case IMAPX_RFC822_SIZE:
				finfo->size = camel_imapx_stream_number(is, ex);
				finfo->got |= FETCH_SIZE;
				break;
			case IMAPX_BODYSTRUCTURE:
				finfo->cinfo = imapx_parse_body(is, ex);
				finfo->got |= FETCH_CINFO;
				break;
			case IMAPX_BODY:
				tok = camel_imapx_stream_token(is, &token, &len, ex);
				camel_imapx_stream_ungettoken(is, tok, token, len);
				if (tok == '(') {
					finfo->cinfo = imapx_parse_body(is, ex);
					finfo->got |= FETCH_CINFO;
				} else if (tok == '[') {
					finfo->section = imapx_parse_section(is, ex);
					finfo->got |= FETCH_SECTION;
					tok = camel_imapx_stream_token(is, &token, &len, ex);
					if (token[0] == '<') {
						finfo->offset = strtoul((gchar *) token+1, NULL, 10);
					} else {
						camel_imapx_stream_ungettoken(is, tok, token, len);
					}
					camel_imapx_stream_nstring_stream(is, &finfo->body, ex);
					finfo->got |= FETCH_BODY;
				} else {
					camel_exception_set (ex, 1, "unknown body response");
					imapx_free_fetch(finfo);
					return NULL;
				}
				break;
			case IMAPX_UID:
				tok = camel_imapx_stream_token(is, &token, &len, ex);
				if (tok != IMAPX_TOK_INT) {
					camel_exception_set (ex, 1, "uid not integer");
				}

				finfo->uid = g_strdup((gchar *) token);
				finfo->got |= FETCH_UID;
				break;
			default:
				imapx_free_fetch(finfo);
				camel_exception_set (ex, 1, "unknown body response");
				return NULL;
		}
	}

	if (tok != ')') {
		camel_exception_set (ex, 1, "missing closing ')' on fetch response");
		imapx_free_fetch (finfo);
		return NULL;
	}

	return finfo;
}

struct _state_info *
imapx_parse_status_info (struct _CamelIMAPXStream *is, CamelException *ex)
{
	struct _state_info *sinfo;
	gint tok;
	guint len;
	guchar *token;

	sinfo = g_malloc0 (sizeof(*sinfo));

	/* skip the folder name */
	camel_imapx_stream_token (is, &token, &len, ex);

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok != '(') {
		camel_exception_set (ex, 1, "parse status info: expecting '('");
		g_free (sinfo);
		return NULL;
	}

	while ((tok = camel_imapx_stream_token(is, &token, &len, ex)) == IMAPX_TOK_TOKEN) {
		switch (imapx_tokenise((gchar *) token, len)) {
			case IMAPX_MESSAGES:
				sinfo->messages = camel_imapx_stream_number (is, ex);
				break;
			case IMAPX_RECENT:
				sinfo->recent = camel_imapx_stream_number (is, ex);
				break;
			case IMAPX_UIDNEXT:
				sinfo->uidnext = camel_imapx_stream_number (is, ex);
				break;
			case IMAPX_UIDVALIDITY:
				sinfo->uidvalidity = camel_imapx_stream_number (is, ex);
				break;
			case IMAPX_UNSEEN:
				sinfo->unseen = camel_imapx_stream_number (is, ex);
				break;
			default:
				g_free (sinfo);
				camel_exception_set (ex, 1, "unknown status response");
				return NULL;
		}
	}

	if (tok != ')') {
		camel_exception_set (ex, 1, "missing closing ')' on status response");
		g_free (sinfo);
		return NULL;
	}

	return sinfo;
}

static void
generate_uids_from_sequence (GPtrArray *uids, guint32 begin_uid, guint32 end_uid)
{
	guint32 i;

	for (i = begin_uid; i <= end_uid; i++)
		g_ptr_array_add (uids, GUINT_TO_POINTER (i));
}

static GPtrArray *
imapx_parse_uids (CamelIMAPXStream *is, CamelException *ex)
{
	GPtrArray *uids = g_ptr_array_new ();
	guchar *token;
	gchar **splits;
	guint len, str_len;
	gint tok, i;

	tok = camel_imapx_stream_token (is, &token, &len, ex);
	splits = g_strsplit ((gchar *) token, ",", -1);
	str_len = g_strv_length (splits);

	for (i = 0; i < str_len; i++)	{
		if (g_strstr_len (splits [i], -1, ":")) {
			gchar **seq = g_strsplit (splits [i], ":", -1);
			guint32 uid1 = strtoul ((gchar *) seq [0], NULL, 10);
			guint32 uid2 = strtoul ((gchar *) seq [1], NULL, 10);

			generate_uids_from_sequence (uids, uid1, uid2);
			g_strfreev (seq);
		} else {
			guint32 uid = strtoul ((gchar *) token, NULL, 10);
			g_ptr_array_add (uids, GUINT_TO_POINTER (uid));
		}
	}

	g_strfreev (splits);

	return uids;
}

/* rfc 2060 section 7.1 Status Responses */
/* shoudl this start after [ or before the [? token_unget anyone? */
struct _status_info *
imapx_parse_status(CamelIMAPXStream *is, CamelException *ex)
{
	gint tok;
	guint len;
	guchar *token;
	struct _status_info *sinfo;

	sinfo = g_malloc0(sizeof(*sinfo));

	camel_imapx_stream_atom(is, &token, &len, ex);

	/*
	   resp_cond_auth  ::= ("OK" / "PREAUTH") SPACE resp_text
	   ;; Authentication condition

	   resp_cond_bye   ::= "BYE" SPACE resp_text

	   resp_cond_state ::= ("OK" / "NO" / "BAD") SPACE resp_text
	   ;; Status condition
	 */

	sinfo->result = imapx_tokenise((gchar *) token, len);
	switch (sinfo->result) {
		case IMAPX_OK:
		case IMAPX_NO:
		case IMAPX_BAD:
		case IMAPX_PREAUTH:
		case IMAPX_BYE:
			break;
		default:
			camel_exception_set (ex, 1, "expecting OK/NO/BAD");
			g_free (sinfo);
			return NULL;
	}

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok == '[') {
		camel_imapx_stream_atom(is, &token, &len, ex);
		sinfo->condition = imapx_tokenise((gchar *) token, len);

		/* parse any details */
		switch (sinfo->condition) {
			case IMAPX_READ_ONLY:
			case IMAPX_READ_WRITE:
			case IMAPX_ALERT:
			case IMAPX_PARSE:
			case IMAPX_TRYCREATE:
				break;
			case IMAPX_APPENDUID:
				sinfo->u.appenduid.uidvalidity = camel_imapx_stream_number(is, ex);
				sinfo->u.appenduid.uid = camel_imapx_stream_number(is, ex);
				break;
			case IMAPX_COPYUID:
				sinfo->u.copyuid.uidvalidity = camel_imapx_stream_number(is, ex);
				sinfo->u.copyuid.uids = imapx_parse_uids (is, ex);
				sinfo->u.copyuid.copied_uids = imapx_parse_uids (is, ex);
				break;
			case IMAPX_NEWNAME:
				/* the rfc doesn't specify the bnf for this */
				camel_imapx_stream_astring(is, &token, ex);
				sinfo->u.newname.oldname = g_strdup((gchar *) token);
				camel_imapx_stream_astring(is, &token, ex);
				sinfo->u.newname.newname = g_strdup((gchar *) token);
				break;
			case IMAPX_PERMANENTFLAGS:
				/* we only care about \* for permanent flags, not user flags */
				imapx_parse_flags(is, &sinfo->u.permanentflags, NULL, ex);
				break;
			case IMAPX_UIDVALIDITY:
				sinfo->u.uidvalidity = camel_imapx_stream_number(is, ex);
				break;
			case IMAPX_UIDNEXT:
				sinfo->u.uidnext = camel_imapx_stream_number (is, ex);
				break;
			case IMAPX_UNSEEN:
				sinfo->u.unseen = camel_imapx_stream_number(is, ex);
				break;
			default:
				sinfo->condition = IMAPX_UNKNOWN;
				d(printf("Got unknown response code: %s: ignored\n", token));
		}

		/* ignore anything we dont know about */
		do {
			tok = camel_imapx_stream_token(is, &token, &len, ex);
			if (tok == '\n') {
				camel_exception_set (ex, 1, "server response truncated");
				imapx_free_status(sinfo);
				return NULL;
			}
		} while (tok != ']');
	} else {
		camel_imapx_stream_ungettoken(is, tok, token, len);
	}

	/* and take the human readable response */
	camel_imapx_stream_text(is, (guchar **)&sinfo->text, ex);

	return sinfo;
}

struct _status_info *
imapx_copy_status(struct _status_info *sinfo)
{
	struct _status_info *out;

	out = g_malloc(sizeof(*out));
	memcpy(out, sinfo, sizeof(*out));
	out->text = g_strdup(out->text);
	if (out->condition == IMAPX_NEWNAME) {
		out->u.newname.oldname = g_strdup(out->u.newname.oldname);
		out->u.newname.newname = g_strdup(out->u.newname.newname);
	}

	return out;
}

void
imapx_free_status(struct _status_info *sinfo)
{
	if (sinfo == NULL)
		return;

	switch (sinfo->condition) {
	case IMAPX_NEWNAME:
		g_free(sinfo->u.newname.oldname);
		g_free(sinfo->u.newname.newname);
		break;
	case IMAPX_COPYUID:
		g_ptr_array_free (sinfo->u.copyuid.uids, FALSE);
		g_ptr_array_free (sinfo->u.copyuid.copied_uids, FALSE);
		break;
	default:
		break;
	}

	g_free(sinfo->text);
	g_free(sinfo);
}

/* FIXME: use tokeniser? */
/* FIXME: real flags */
static struct {
	const gchar *name;
	guint32 flag;
} list_flag_table[] = {
	{ "\\NOINFERIORS", CAMEL_FOLDER_NOINFERIORS },
	{ "\\NOSELECT", CAMEL_FOLDER_NOSELECT },
	{ "\\MARKED", 1<< 16},
	{ "\\UNMARKED", 1<< 17},
};

struct _list_info *
imapx_parse_list(CamelIMAPXStream *is, CamelException *ex)
/* throws io, parse */
{
	gint tok, i;
	guint len;
	guchar *token, *p, c;
	struct _list_info * linfo;

	linfo = g_malloc0(sizeof(*linfo));

	/* mailbox_list    ::= "(" #("\Marked" / "\Noinferiors" /
	   "\Noselect" / "\Unmarked" / flag_extension) ")"
	   SPACE (<"> QUOTED_CHAR <"> / nil) SPACE mailbox */

	tok = camel_imapx_stream_token(is, &token, &len, ex);
	if (tok != '(') {
		camel_exception_set (ex, 1, "list: expecting '('");
		g_free (linfo);
		return NULL;
	}

	while ((tok = camel_imapx_stream_token(is, &token, &len, ex)) != ')') {
		if (tok == IMAPX_TOK_STRING || tok == IMAPX_TOK_TOKEN) {
			p = token;
			while ((c=*p))
				*p++ = toupper(c);
			for (i = 0; i < G_N_ELEMENTS (list_flag_table); i++)
				if (!strcmp((gchar *) token, list_flag_table[i].name))
					linfo->flags |= list_flag_table[i].flag;
		} else {
			imapx_free_list(linfo);
			camel_exception_set (ex, 1, "list: expecting flag or ')'");
			return NULL;
		}
	}

	camel_imapx_stream_nstring(is, &token, ex);
	linfo->separator = token?*token:0;
	camel_imapx_stream_astring(is, &token, ex);
	linfo->name = camel_utf7_utf8 ((gchar *) token);

	return linfo;
}

gchar *
imapx_list_get_path(struct _list_info *li)
{
	gchar *path, *p;
	gint c;
	const gchar *f;

	if (li->separator != 0 && li->separator != '/') {
		p = path = alloca(strlen(li->name)*3+1);
		f = li->name;
		while ((c = *f++ & 0xff)) {
			if (c == li->separator)
				*p++ = '/';
			else if (c == '/' || c == '%')
				p += sprintf(p, "%%%02X", c);
			else
				*p++ = c;
		}
		*p = 0;
	} else
		path = li->name;

	return camel_utf7_utf8(path);
}

void
imapx_free_list(struct _list_info *linfo)
{
	if (linfo) {
		g_free(linfo->name);
		g_free(linfo);
	}
}

/* ********************************************************************** */

/*
 From rfc2060

ATOM_CHAR       ::= <any CHAR except atom_specials>

atom_specials   ::= "(" / ")" / "{" / SPACE / CTL / list_wildcards /
                    quoted_specials

CHAR            ::= <any 7-bit US-ASCII character except NUL,
                     0x01 - 0x7f>

CTL             ::= <any ASCII control character and DEL,
                        0x00 - 0x1f, 0x7f>

SPACE           ::= <ASCII SP, space, 0x20>

list_wildcards  ::= "%" / "*"

quoted_specials ::= <"> / "\"

string          ::= quoted / literal

literal         ::= "{" number "}" CRLF *CHAR8
                    ;; Number represents the number of CHAR8 octets

quoted          ::= <"> *QUOTED_CHAR <">

QUOTED_CHAR     ::= <any TEXT_CHAR except quoted_specials> /
                    "\" quoted_specials

TEXT_CHAR       ::= <any CHAR except CR and LF>

*/

/*
ATOM = 1
SIMPLE? = 2
NOTID? = 4

QSPECIAL = 8

*/

guchar imapx_specials[256] = {
/* 00 */0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 0, 4, 0, 0,
/* 10 */0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 20 */4, 1, 0, 1, 1, 0, 1, 1, 0, 0, 2, 7, 1, 1, 1, 1,
/* 30 */1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 40 */7, 7, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 50 */1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 7, 0, 7, 1, 1,
/* 60 */1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* 70 */1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

#define list_wildcards "*%"
#define quoted_specials "\\\""
#define atom_specials "(){" list_wildcards quoted_specials /* + CTL */

/* special types for the tokeniser, come out as raw tokens */
#define token_specials "\n*()[]+"
#define notid_specials "\x20\r\n()[]+"

void imapx_utils_init(void)
{
	gint i;
	guchar v;

	for (i=0;i<128;i++) {
		v = 0;
		if (i>=1 && i<=0x7f) {
			v |= IMAPX_TYPE_CHAR;
			if (i != 0x0a && i != 0x0d) {
				v |= IMAPX_TYPE_TEXT_CHAR;
				if (i != '"' && i != '\\')
					v |= IMAPX_TYPE_QUOTED_CHAR;
			}
			if (i> 0x20 && i <0x7f && strchr(atom_specials, i) == NULL)
				v |= IMAPX_TYPE_ATOM_CHAR;
			if (strchr(token_specials, i) != NULL)
				v |= IMAPX_TYPE_TOKEN_CHAR;
			if (strchr(notid_specials, i) != NULL)
				v |= IMAPX_TYPE_NOTID_CHAR;
		}

		imapx_specials[i] = v;
	}
}

guchar imapx_is_mask(const gchar *p)
{
	guchar v = 0xff;

	while (*p) {
		v &= imapx_specials[((guchar)*p) & 0xff];
		p++;
	}

	return v;
}

gchar *
imapx_path_to_physical (const gchar *prefix, const gchar *vpath)
{
	GString *out = g_string_new(prefix);
	const gchar *p = vpath;
	gchar c, *res;

	g_string_append_c(out, '/');
	p = vpath;
	while ((c = *p++)) {
		if (c == '/') {
			g_string_append(out, "/" SUBFOLDER_DIR_NAME "/");
			while (*p == '/')
				p++;
		} else
			g_string_append_c(out, c);
	}

	res = out->str;
	g_string_free(out, FALSE);

	return res;
}

gchar *
imapx_concat (CamelIMAPXStore *imapx_store, const gchar *prefix, const gchar *suffix)
{
	gsize len;

	len = strlen (prefix);
	if (len == 0 || prefix[len - 1] == imapx_store->dir_sep)
		return g_strdup_printf ("%s%s", prefix, suffix);
	else
		return g_strdup_printf ("%s%c%s", prefix, imapx_store->dir_sep, suffix);
}

static void
imapx_namespace_clear (CamelIMAPXStoreNamespace **ns)
{
	CamelIMAPXStoreNamespace *node, *next;

	node = *ns;
	while (node != NULL) {
		next = node->next;
		g_free (node->path);
		g_free (node);
		node = next;
	}

	*ns = NULL;
}

void
camel_imapx_namespace_list_clear (struct _CamelIMAPXNamespaceList *nsl)
{
	if (!nsl)
		return;

	imapx_namespace_clear (&nsl->personal);
	imapx_namespace_clear (&nsl->shared);
	imapx_namespace_clear (&nsl->other);

	g_free (nsl);
	nsl = NULL;
}

static CamelIMAPXStoreNamespace *
imapx_namespace_copy (const CamelIMAPXStoreNamespace *ns)
{
	CamelIMAPXStoreNamespace *list, *node, *tail;

	list = NULL;
	tail = (CamelIMAPXStoreNamespace *) &list;

	while (ns != NULL) {
		tail->next = node = g_malloc (sizeof (CamelIMAPXStoreNamespace));
		node->path = g_strdup (ns->path);
		node->sep = ns->sep;
		ns = ns->next;
		tail = node;
	}

	tail->next = NULL;

	return list;
}

struct _CamelIMAPXNamespaceList *
camel_imapx_namespace_list_copy (const struct _CamelIMAPXNamespaceList *nsl)
{
	CamelIMAPXNamespaceList *new;

	new = g_malloc (sizeof (CamelIMAPXNamespaceList));
	new->personal = imapx_namespace_copy (nsl->personal);
	new->other = imapx_namespace_copy (nsl->other);
	new->shared = imapx_namespace_copy (nsl->shared);

	return new;
}

gchar *
imapx_get_temp_uid (void)
{
	gchar *res;

	static gint counter = 0;
	G_LOCK_DEFINE_STATIC (lock);

	G_LOCK (lock);
	res = g_strdup_printf ("tempuid-%lx-%d",
			       (gulong) time (NULL),
			       counter++);
	G_UNLOCK (lock);

	return res;
}
