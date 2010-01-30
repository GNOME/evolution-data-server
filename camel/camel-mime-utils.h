/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *           Jeffrey Stedfast <fejj@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _CAMEL_MIME_UTILS_H
#define _CAMEL_MIME_UTILS_H

#include <time.h>
#include <glib.h>

/* maximum recommended size of a line from camel_header_fold() */
#define CAMEL_FOLD_SIZE (77)
/* maximum hard size of a line from camel_header_fold() */
#define CAMEL_FOLD_MAX_SIZE (998)

#define CAMEL_UUDECODE_STATE_INIT   (0)
#define CAMEL_UUDECODE_STATE_BEGIN  (1 << 16)
#define CAMEL_UUDECODE_STATE_END    (1 << 17)
#define CAMEL_UUDECODE_STATE_MASK   (CAMEL_UUDECODE_STATE_BEGIN | CAMEL_UUDECODE_STATE_END)

G_BEGIN_DECLS

/* note, if you change this, make sure you change the 'encodings' array in camel-mime-part.c */
typedef enum _CamelTransferEncoding {
	CAMEL_TRANSFER_ENCODING_DEFAULT,
	CAMEL_TRANSFER_ENCODING_7BIT,
	CAMEL_TRANSFER_ENCODING_8BIT,
	CAMEL_TRANSFER_ENCODING_BASE64,
	CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE,
	CAMEL_TRANSFER_ENCODING_BINARY,
	CAMEL_TRANSFER_ENCODING_UUENCODE,
	CAMEL_TRANSFER_NUM_ENCODINGS
} CamelTransferEncoding;

/* a list of references for this message */
struct _camel_header_references {
	struct _camel_header_references *next;
	gchar *id;
};

struct _camel_header_param {
	struct _camel_header_param *next;
	gchar *name;
	gchar *value;
};

/* describes a content-type */
typedef struct {
	gchar *type;
	gchar *subtype;
	struct _camel_header_param *params;
	guint refcount;
} CamelContentType;

/* a raw rfc822 header */
/* the value MUST be US-ASCII */
struct _camel_header_raw {
	struct _camel_header_raw *next;
	gchar *name;
	gchar *value;
	gint offset;		/* in file, if known */
};

typedef struct _CamelContentDisposition {
	gchar *disposition;
	struct _camel_header_param *params;
	guint refcount;
} CamelContentDisposition;

typedef enum _camel_header_address_t {
	CAMEL_HEADER_ADDRESS_NONE,	/* uninitialised */
	CAMEL_HEADER_ADDRESS_NAME,
	CAMEL_HEADER_ADDRESS_GROUP
} camel_header_address_t;

struct _camel_header_address {
	struct _camel_header_address *next;
	camel_header_address_t type;
	gchar *name;
	union {
		gchar *addr;
		struct _camel_header_address *members;
	} v;
	guint refcount;
};

struct _camel_header_newsgroup {
	struct _camel_header_newsgroup *next;

	gchar *newsgroup;
};

/* Address lists */
struct _camel_header_address *camel_header_address_new (void);
struct _camel_header_address *camel_header_address_new_name (const gchar *name, const gchar *addr);
struct _camel_header_address *camel_header_address_new_group (const gchar *name);
void camel_header_address_ref (struct _camel_header_address *addrlist);
void camel_header_address_unref (struct _camel_header_address *addrlist);
void camel_header_address_set_name (struct _camel_header_address *addrlist, const gchar *name);
void camel_header_address_set_addr (struct _camel_header_address *addrlist, const gchar *addr);
void camel_header_address_set_members (struct _camel_header_address *addrlist, struct _camel_header_address *group);
void camel_header_address_add_member (struct _camel_header_address *addrlist, struct _camel_header_address *member);
void camel_header_address_list_append_list (struct _camel_header_address **addrlistp, struct _camel_header_address **addrs);
void camel_header_address_list_append (struct _camel_header_address **addrlistp, struct _camel_header_address *addr);
void camel_header_address_list_clear (struct _camel_header_address **addrlistp);

struct _camel_header_address *camel_header_address_decode (const gchar *in, const gchar *charset);
struct _camel_header_address *camel_header_mailbox_decode (const gchar *in, const gchar *charset);
/* for mailing */
gchar *camel_header_address_list_encode (struct _camel_header_address *addrlist);
/* for display */
gchar *camel_header_address_list_format (struct _camel_header_address *addrlist);

/* structured header prameters */
struct _camel_header_param *camel_header_param_list_decode (const gchar *in);
gchar *camel_header_param (struct _camel_header_param *params, const gchar *name);
struct _camel_header_param *camel_header_set_param (struct _camel_header_param **paramsp, const gchar *name, const gchar *value);
void camel_header_param_list_format_append (GString *out, struct _camel_header_param *params);
gchar *camel_header_param_list_format (struct _camel_header_param *params);
void camel_header_param_list_free (struct _camel_header_param *params);

/* Content-Type header */
CamelContentType *camel_content_type_new (const gchar *type, const gchar *subtype);
CamelContentType *camel_content_type_decode (const gchar *in);
void camel_content_type_unref (CamelContentType *content_type);
void camel_content_type_ref (CamelContentType *content_type);
const gchar *camel_content_type_param (CamelContentType *content_type, const gchar *name);
void camel_content_type_set_param (CamelContentType *content_type, const gchar *name, const gchar *value);
gint camel_content_type_is (CamelContentType *content_type, const gchar *type, const gchar *subtype);
gchar *camel_content_type_format (CamelContentType *content_type);
gchar *camel_content_type_simple (CamelContentType *content_type);

/* DEBUGGING function */
void camel_content_type_dump (CamelContentType *content_type);

/* Content-Disposition header */
CamelContentDisposition *camel_content_disposition_decode (const gchar *in);
void camel_content_disposition_ref (CamelContentDisposition *disposition);
void camel_content_disposition_unref (CamelContentDisposition *disposition);
gchar *camel_content_disposition_format (CamelContentDisposition *disposition);

/* decode the contents of a content-encoding header */
gchar *camel_content_transfer_encoding_decode (const gchar *in);

/* raw headers */
void camel_header_raw_append (struct _camel_header_raw **list, const gchar *name, const gchar *value, gint offset);
void camel_header_raw_append_parse (struct _camel_header_raw **list, const gchar *header, gint offset);
const gchar *camel_header_raw_find (struct _camel_header_raw **list, const gchar *name, gint *offset);
const gchar *camel_header_raw_find_next (struct _camel_header_raw **list, const gchar *name, gint *offset, const gchar *last);
void camel_header_raw_replace (struct _camel_header_raw **list, const gchar *name, const gchar *value, gint offset);
void camel_header_raw_remove (struct _camel_header_raw **list, const gchar *name);
void camel_header_raw_fold (struct _camel_header_raw **list);
void camel_header_raw_clear (struct _camel_header_raw **list);

gchar *camel_header_raw_check_mailing_list (struct _camel_header_raw **list);

/* fold a header */
gchar *camel_header_address_fold (const gchar *in, gsize headerlen);
gchar *camel_header_fold (const gchar *in, gsize headerlen);
gchar *camel_header_unfold (const gchar *in);

/* decode a header which is a simple token */
gchar *camel_header_token_decode (const gchar *in);

gint camel_header_decode_int (const gchar **in);

/* decode/encode a string type, like a subject line */
gchar *camel_header_decode_string (const gchar *in, const gchar *default_charset);
gchar *camel_header_encode_string (const guchar *in);

/* decode (text | comment) - a one-way op */
gchar *camel_header_format_ctext (const gchar *in, const gchar *default_charset);

/* encode a phrase, like the real name of an address */
gchar *camel_header_encode_phrase (const guchar *in);

/* FIXME: these are the only 2 functions in this header which are ch_(action)_type
   rather than ch_type_(action) */

/* decode an email date field into a GMT time, + optional offset */
time_t camel_header_decode_date (const gchar *str, gint *tz_offset);
gchar *camel_header_format_date (time_t date, gint tz_offset);

/* decode a message id */
gchar *camel_header_msgid_decode (const gchar *in);
gchar *camel_header_contentid_decode (const gchar *in);

/* generate msg id */
gchar *camel_header_msgid_generate (void);

/* decode a References or In-Reply-To header */
struct _camel_header_references *camel_header_references_inreplyto_decode (const gchar *in);
struct _camel_header_references *camel_header_references_decode (const gchar *in);
void camel_header_references_list_clear (struct _camel_header_references **list);
void camel_header_references_list_append_asis (struct _camel_header_references **list, gchar *ref);
gint camel_header_references_list_size (struct _camel_header_references **list);
struct _camel_header_references *camel_header_references_dup (const struct _camel_header_references *list);

/* decode content-location */
gchar *camel_header_location_decode (const gchar *in);

/* nntp stuff */
struct _camel_header_newsgroup *camel_header_newsgroups_decode(const gchar *in);
void camel_header_newsgroups_free(struct _camel_header_newsgroup *ng);

const gchar *camel_transfer_encoding_to_string (CamelTransferEncoding encoding);
CamelTransferEncoding camel_transfer_encoding_from_string (const gchar *string);

/* decode the mime-type header */
void camel_header_mime_decode (const gchar *in, gint *maj, gint *min);

#ifndef CAMEL_DISABLE_DEPRECATED
/* do incremental base64/quoted-printable (de/en)coding */
gsize camel_base64_decode_step (guchar *in, gsize len, guchar *out, gint *state, guint *save);

gsize camel_base64_encode_step (guchar *in, gsize inlen, gboolean break_lines, guchar *out, gint *state, gint *save);
gsize camel_base64_encode_close (guchar *in, gsize inlen, gboolean break_lines, guchar *out, gint *state, gint *save);
#endif

gsize camel_uudecode_step (guchar *in, gsize inlen, guchar *out, gint *state, guint32 *save);

gsize camel_uuencode_step (guchar *in, gsize len, guchar *out, guchar *uubuf, gint *state,
		      guint32 *save);
gsize camel_uuencode_close (guchar *in, gsize len, guchar *out, guchar *uubuf, gint *state,
		       guint32 *save);

gsize camel_quoted_decode_step (guchar *in, gsize len, guchar *out, gint *savestate, gint *saveme);

gsize camel_quoted_encode_step (guchar *in, gsize len, guchar *out, gint *state, gint *save);
gsize camel_quoted_encode_close (guchar *in, gsize len, guchar *out, gint *state, gint *save);

#ifndef CAMEL_DISABLE_DEPRECATED
gchar *camel_base64_encode_simple (const gchar *data, gsize len);
gsize camel_base64_decode_simple (gchar *data, gsize len);
#endif

/* camel ctype type functions for rfc822/rfc2047/other, which are non-locale specific */
enum {
	CAMEL_MIME_IS_CTRL		= 1<<0,
	CAMEL_MIME_IS_LWSP		= 1<<1,
	CAMEL_MIME_IS_TSPECIAL	= 1<<2,
	CAMEL_MIME_IS_SPECIAL	= 1<<3,
	CAMEL_MIME_IS_SPACE	= 1<<4,
	CAMEL_MIME_IS_DSPECIAL	= 1<<5,
	CAMEL_MIME_IS_QPSAFE	= 1<<6,
	CAMEL_MIME_IS_ESAFE	= 1<<7,	/* encoded word safe */
	CAMEL_MIME_IS_PSAFE	= 1<<8,	/* encoded word in phrase safe */
	CAMEL_MIME_IS_ATTRCHAR  = 1<<9	/* attribute-char safe (rfc2184) */
};

extern unsigned short camel_mime_special_table[256];

#define camel_mime_is_ctrl(x) ((camel_mime_special_table[(guchar)(x)] & CAMEL_MIME_IS_CTRL) != 0)
#define camel_mime_is_lwsp(x) ((camel_mime_special_table[(guchar)(x)] & CAMEL_MIME_IS_LWSP) != 0)
#define camel_mime_is_tspecial(x) ((camel_mime_special_table[(guchar)(x)] & CAMEL_MIME_IS_TSPECIAL) != 0)
#define camel_mime_is_type(x, t) ((camel_mime_special_table[(guchar)(x)] & (t)) != 0)
#define camel_mime_is_ttoken(x) ((camel_mime_special_table[(guchar)(x)] & (CAMEL_MIME_IS_TSPECIAL|CAMEL_MIME_IS_LWSP|CAMEL_MIME_IS_CTRL)) == 0)
#define camel_mime_is_atom(x) ((camel_mime_special_table[(guchar)(x)] & (CAMEL_MIME_IS_SPECIAL|CAMEL_MIME_IS_SPACE|CAMEL_MIME_IS_CTRL)) == 0)
#define camel_mime_is_dtext(x) ((camel_mime_special_table[(guchar)(x)] & CAMEL_MIME_IS_DSPECIAL) == 0)
#define camel_mime_is_fieldname(x) ((camel_mime_special_table[(guchar)(x)] & (CAMEL_MIME_IS_CTRL|CAMEL_MIME_IS_SPACE)) == 0)
#define camel_mime_is_qpsafe(x) ((camel_mime_special_table[(guchar)(x)] & CAMEL_MIME_IS_QPSAFE) != 0)
#define camel_mime_is_especial(x) ((camel_mime_special_table[(guchar)(x)] & CAMEL_MIME_IS_ESPECIAL) != 0)
#define camel_mime_is_psafe(x) ((camel_mime_special_table[(guchar)(x)] & CAMEL_MIME_IS_PSAFE) != 0)
#define camel_mime_is_attrchar(x) ((camel_mime_special_table[(guchar)(x)] & CAMEL_MIME_IS_ATTRCHAR) != 0)

G_END_DECLS

#endif /* _CAMEL_MIME_UTILS_H */
