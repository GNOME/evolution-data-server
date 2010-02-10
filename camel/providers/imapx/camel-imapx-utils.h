
#ifndef _CAMEL_IMAPX_UTILS_H
#define _CAMEL_IMAPX_UTILS_H

#include <camel/camel-mime-utils.h>
#include <camel/camel-folder-summary.h>
#include "camel-imapx-store.h"

struct _CamelIMAPXStream;
struct _CamelFlag;
struct _CamelIMAPXNamespaceList;

/* list of strings we know about that can be *quickly* tokenised */
typedef enum _camel_imapx_id_t {
	IMAPX_UNKNOWN = 0,
	IMAPX_ALERT,
	IMAPX_APPENDUID,
	IMAPX_BAD,
	IMAPX_BODY,
	IMAPX_BODYSTRUCTURE,
	IMAPX_BYE,
	IMAPX_CAPABILITY,
	IMAPX_COPYUID,
	IMAPX_ENVELOPE,
	IMAPX_EXISTS,
	IMAPX_EXPUNGE,
	IMAPX_FETCH,
	IMAPX_FLAGS,
	IMAPX_INTERNALDATE,
	IMAPX_LIST,
	IMAPX_LSUB,
	IMAPX_MESSAGES,
	IMAPX_NAMESPACE,
	IMAPX_NEWNAME,
	IMAPX_NO,
	IMAPX_OK,
	IMAPX_PARSE,
	IMAPX_PERMANENTFLAGS,
	IMAPX_PREAUTH,
	IMAPX_READ_ONLY,
	IMAPX_READ_WRITE,
	IMAPX_RECENT,
	IMAPX_RFC822_HEADER,
	IMAPX_RFC822_SIZE,
	IMAPX_RFC822_TEXT,
	IMAPX_STATUS,
	IMAPX_TRYCREATE,
	IMAPX_UID,
	IMAPX_UIDVALIDITY,
	IMAPX_UNSEEN,
	IMAPX_UIDNEXT,
} camel_imapx_id_t;

/* str MUST be in upper case, tokenised using gperf function */
camel_imapx_id_t imapx_tokenise(register const gchar *str, register guint len);

/* this flag should be part of imapfoldersummary */
enum {
	CAMEL_IMAPX_MESSAGE_RECENT = (1<<21),
};

/* ********************************************************************** */

void imapx_parse_flags(struct _CamelIMAPXStream *stream, guint32 *flagsp, struct _CamelFlag **user_flagsp, CamelException *ex);
void imapx_write_flags(CamelStream *stream, guint32 flags, struct _CamelFlag *user_flags, CamelException *ex);
gboolean imapx_update_message_info_flags (CamelMessageInfo *info, guint32 server_flags, CamelFlag *server_user_flags, CamelFolder *folder);
void imapx_set_message_info_flags_for_new_message (CamelMessageInfo *info, guint32 server_flags, CamelFlag *server_user_flags,
							CamelFolder *folder);
void imapx_update_summary_for_removed_message (CamelMessageInfo *info, CamelFolder *folder);
void imapx_update_store_summary (CamelFolder *folder);

/* ********************************************************************** */
enum {
	IMAPX_CAPABILITY_IMAP4			= (1 << 0),
	IMAPX_CAPABILITY_IMAP4REV1		= (1 << 1),
	IMAPX_CAPABILITY_STATUS			= (1 << 2),
	IMAPX_CAPABILITY_NAMESPACE		= (1 << 3),
	IMAPX_CAPABILITY_UIDPLUS		= (1 << 4),
	IMAPX_CAPABILITY_LITERALPLUS		= (1 << 5),
	IMAPX_CAPABILITY_STARTTLS                = (1 << 6),
	IMAPX_CAPABILITY_IDLE			= (1 << 7),
};

struct _capability_info {
	guint32 capa;
	/* auth stuff here */
};

struct _capability_info *imapx_parse_capability(struct _CamelIMAPXStream *stream, CamelException *ex);
void imapx_free_capability(struct _capability_info *);

void imapx_parse_param_list(struct _CamelIMAPXStream *is, struct _camel_header_param **plist, CamelException *ex) /* IO,PARSE */;
struct _CamelContentDisposition *imapx_parse_ext_optional(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
struct _CamelMessageContentInfo *imapx_parse_body_fields(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
struct _camel_header_address *imapx_parse_address_list(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
struct _CamelMessageInfo *imapx_parse_envelope(struct _CamelIMAPXStream *is, CamelException *ex) /* IO, PARSE */;
struct _CamelMessageContentInfo *imapx_parse_body(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
gchar *imapx_parse_section(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
void imapx_free_body(struct _CamelMessageContentInfo *cinfo);

/* ********************************************************************** */
/* all the possible stuff we might get from a fetch request */
/* this assumes the caller/server doesn't send any one of these types twice */
struct _fetch_info {
	guint32 got;		/* what we got, see below */
	CamelStream *body;	/* BODY[.*](<.*>)? */
	CamelStream *text;	/* RFC822.TEXT */
	CamelStream *header;	/* RFC822.HEADER */
	struct _CamelMessageInfo *minfo; /* ENVELOPE */
	struct _CamelMessageContentInfo *cinfo;	/* BODYSTRUCTURE,BODY */
	guint32 size;		/* RFC822.SIZE */
	guint32 offset;		/* start offset of a BODY[]<offset.length> request */
	guint32 flags;		/* FLAGS */
	struct _CamelFlag *user_flags;
	gchar *date;		/* INTERNALDATE */
	gchar *section;		/* section for a BODY[section] request */
	gchar *uid;		/* UID */
};

#define FETCH_BODY (1<<0)
#define FETCH_TEXT (1<<1)
#define FETCH_HEADER (1<<2)
#define FETCH_MINFO (1<<3)
#define FETCH_CINFO (1<<4)
#define FETCH_SIZE (1<<5)
#define FETCH_OFFSET (1<<6)
#define FETCH_FLAGS (1<<7)
#define FETCH_DATE (1<<8)
#define FETCH_SECTION (1<<9)
#define FETCH_UID (1<<10)

struct _fetch_info *imapx_parse_fetch(struct _CamelIMAPXStream *is, CamelException *ex);
void imapx_free_fetch(struct _fetch_info *finfo);
void imapx_dump_fetch(struct _fetch_info *finfo);

/* ********************************************************************** */

struct _status_info {
	camel_imapx_id_t result; /* ok/no/bad/preauth only, user_cancel - client response */
	camel_imapx_id_t condition; /* read-only/read-write/alert/parse/trycreate/newname/permanentflags/uidvalidity/unseen */

	union {
		struct {
			gchar *oldname;
			gchar *newname;
		} newname;
		guint32 permanentflags;
		guint32 uidvalidity;
		guint32 uidnext;
		guint32 unseen;
		struct {
			guint32 uidvalidity;
			guint32 uid;
		} appenduid;
		struct {
			guint32 uidvalidity;
			GPtrArray *uids;
			GPtrArray *copied_uids;
		} copyuid;
	} u;

	gchar *text;
};

struct _status_info *imapx_parse_status(struct _CamelIMAPXStream *is, CamelException *ex);
struct _status_info *imapx_copy_status(struct _status_info *sinfo);
void imapx_free_status(struct _status_info *sinfo);

/* ********************************************************************** */
/* parses the response from the status command */
struct _state_info {
	guint32 messages;
	guint32 recent;
	guint32 uidnext;
	guint32 uidvalidity;
	guint32 unseen;
};

/* use g_free to free the return value */
struct _state_info *imapx_parse_status_info (struct _CamelIMAPXStream *is, CamelException *ex);

/* ********************************************************************** */

/* should this just return a FolderInfo?
   should this just return the name & flags & separator by reference? */
struct _list_info {
	guint32 flags:24;
	gchar separator;
	gchar *name;
};

struct _list_info *imapx_parse_list(struct _CamelIMAPXStream *is, CamelException *ex);
gchar *imapx_list_get_path(struct _list_info *li);
void imapx_free_list(struct _list_info *linfo);

/* ********************************************************************** */

extern guchar imapx_specials[256];

#define IMAPX_TYPE_CHAR (1<<0)
#define IMAPX_TYPE_TEXT_CHAR (1<<1)
#define IMAPX_TYPE_QUOTED_CHAR (1<<2)
#define IMAPX_TYPE_ATOM_CHAR (1<<3)
#define IMAPX_TYPE_TOKEN_CHAR (1<<4)
#define IMAPX_TYPE_NOTID_CHAR (1<<5)

guchar imapx_is_mask(const gchar *p);

#define imapx_is_text_char(c) ((imapx_specials[((unsigned char)(c))&0xff] & IMAPX_TYPE_TEXT_CHAR) != 0)
#define imapx_is_quoted_char(c) ((imapx_specials[((unsigned char)(c))&0xff] & IMAPX_TYPE_QUOTED_CHAR) != 0)
#define imapx_is_atom_char(c) ((imapx_specials[((unsigned char)(c))&0xff] & IMAPX_TYPE_ATOM_CHAR) != 0)
#define imapx_is_token_char(c) ((imapx_specials[((unsigned char)(c))&0xff] & IMAPX_TYPE_TOKEN_CHAR) != 0)
#define imapx_is_notid_char(c) ((imapx_specials[((unsigned char)(c))&0xff] & IMAPX_TYPE_NOTID_CHAR) != 0)

#define imapx_is_atom(s) (imapx_is_mask(s) & IMAPX_TYPE_ATOM_CHAR)

/* ********************************************************************** */

void imapx_utils_init(void);

/* chen adds from old imap provider - place it in right place */
gchar *imapx_path_to_physical (const gchar *prefix, const gchar *vpath);
gchar *imapx_concat (CamelIMAPXStore *imapx_store, const gchar *prefix, const gchar *suffix);
gchar * imapx_get_temp_uid (void);

void camel_imapx_namespace_list_clear (struct _CamelIMAPXNamespaceList *nsl);
struct _CamelIMAPXNamespaceList * imapx_parse_namespace_list (struct _CamelIMAPXStream *stream, CamelException *ex);
struct _CamelIMAPXNamespaceList *camel_imapx_namespace_list_copy (const struct _CamelIMAPXNamespaceList *nsl);

#endif
