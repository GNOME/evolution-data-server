
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
	IMAP_UNKNOWN = 0,
	IMAP_ALERT,
	IMAP_APPENDUID,
	IMAP_BAD,
	IMAP_BODY,
	IMAP_BODYSTRUCTURE,
	IMAP_BYE,
	IMAP_CAPABILITY,
	IMAP_ENVELOPE,
	IMAP_EXISTS,
	IMAP_EXPUNGE,
	IMAP_FETCH,
	IMAP_FLAGS,
	IMAP_INTERNALDATE,
	IMAP_LIST,
	IMAP_LSUB,
	IMAP_NAMESPACE,
	IMAP_NEWNAME,
	IMAP_NO,
	IMAP_OK,
	IMAP_PARSE,
	IMAP_PERMANENTFLAGS,
	IMAP_PREAUTH,
	IMAP_READ_ONLY,
	IMAP_READ_WRITE,
	IMAP_RECENT,
	IMAP_RFC822_HEADER,
	IMAP_RFC822_SIZE,
	IMAP_RFC822_TEXT,
	IMAP_TRYCREATE,
	IMAP_UID,
	IMAP_UIDVALIDITY,
	IMAP_UNSEEN,
} camel_imapx_id_t;

/* str MUST be in upper case, tokenised using gperf function */
camel_imapx_id_t imap_tokenise(register const gchar *str, register guint len);

/* this flag should be part of imapfoldersummary */
enum {
	CAMEL_IMAPX_MESSAGE_RECENT = (1<<8),
};

/* ********************************************************************** */

void imap_parse_flags(struct _CamelIMAPXStream *stream, guint32 *flagsp, struct _CamelFlag **user_flagsp, CamelException *ex);
void imap_write_flags(CamelStream *stream, guint32 flags, struct _CamelFlag *user_flags, CamelException *ex);

/* ********************************************************************** */
enum {
	IMAP_CAPABILITY_IMAP4			= (1 << 0),
	IMAP_CAPABILITY_IMAP4REV1		= (1 << 1),
	IMAP_CAPABILITY_STATUS			= (1 << 2),
	IMAP_CAPABILITY_NAMESPACE		= (1 << 3),
	IMAP_CAPABILITY_UIDPLUS			= (1 << 4),
	IMAP_CAPABILITY_LITERALPLUS		= (1 << 5),
	IMAP_CAPABILITY_STARTTLS                = (1 << 6),
};

struct _capability_info {
	guint32 capa;
	/* auth stuff here */
};

struct _capability_info *imap_parse_capability(struct _CamelIMAPXStream *stream, CamelException *ex);
void imap_free_capability(struct _capability_info *);

void imap_parse_param_list(struct _CamelIMAPXStream *is, struct _camel_header_param **plist, CamelException *ex) /* IO,PARSE */;
struct _CamelContentDisposition *imap_parse_ext_optional(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
struct _CamelMessageContentInfo *imap_parse_body_fields(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
struct _camel_header_address *imap_parse_address_list(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
struct _CamelMessageInfo *imap_parse_envelope(struct _CamelIMAPXStream *is, CamelException *ex) /* IO, PARSE */;
struct _CamelMessageContentInfo *imap_parse_body(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
gchar *imap_parse_section(struct _CamelIMAPXStream *is, CamelException *ex) /* IO,PARSE */;
void imap_free_body(struct _CamelMessageContentInfo *cinfo);

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

struct _fetch_info *imap_parse_fetch(struct _CamelIMAPXStream *is, CamelException *ex);
void imap_free_fetch(struct _fetch_info *finfo);
void imap_dump_fetch(struct _fetch_info *finfo);

/* ********************************************************************** */

struct _status_info {
	camel_imapx_id_t result; /* ok/no/bad/preauth only */
	camel_imapx_id_t condition; /* read-only/read-write/alert/parse/trycreate/newname/permanentflags/uidvalidity/unseen */

	union {
		struct {
			gchar *oldname;
			gchar *newname;
		} newname;
		guint32 permanentflags;
		guint32 uidvalidity;
		guint32 unseen;
		struct {
			guint32 uidvalidity;
			guint32 uid;
		} appenduid;
	} u;

	gchar *text;
};

struct _status_info *imap_parse_status(struct _CamelIMAPXStream *is, CamelException *ex);
struct _status_info *imap_copy_status(struct _status_info *sinfo);
void imap_free_status(struct _status_info *sinfo);

/* ********************************************************************** */

/* should this just return a FolderInfo?
   should this just return the name & flags & separator by reference? */
struct _list_info {
	guint32 flags:24;
	gchar separator;
	gchar *name;
};

struct _list_info *imap_parse_list(struct _CamelIMAPXStream *is, CamelException *ex);
gchar *imapx_list_get_path(struct _list_info *li);
void imap_free_list(struct _list_info *linfo);

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
gchar *imapx_concat (CamelIMAPXStore *imap_store, const gchar *prefix, const gchar *suffix);

void camel_imapx_namespace_list_clear (struct _CamelIMAPXNamespaceList *nsl);
struct _CamelIMAPXNamespaceList * imap_parse_namespace_list (struct _CamelIMAPXStream *stream, CamelException *ex);
struct _CamelIMAPXNamespaceList *camel_imapx_namespace_list_copy (const struct _CamelIMAPXNamespaceList *nsl);

#endif
