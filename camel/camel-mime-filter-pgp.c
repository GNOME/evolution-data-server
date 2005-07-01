/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2005 Matt Brown.
 *
 * Authors: Matt Brown <matt@mattb.net.nz>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* Strips PGP message headers from the input stream and also performs
 * pgp decoding as described in section 7.1 of RFC2440 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <ctype.h>

#include "camel-mime-filter-pgp.h"

static void filter (CamelMimeFilter *f, char *in, size_t len, size_t prespace,
		    char **out, size_t *outlen, size_t *outprespace);
static void complete (CamelMimeFilter *f, char *in, size_t len,
		      size_t prespace, char **out, size_t *outlen,
		      size_t *outprespace);
static void reset (CamelMimeFilter *f);

enum {
	PGPF_HEADER,
	PGPF_MESSAGE,
	PGPF_FOOTER,
};

static void
camel_mime_filter_pgp_class_init (CamelMimeFilterPgpClass *klass)
{
	CamelMimeFilterClass *mime_filter_class = (CamelMimeFilterClass *) klass;
	
	mime_filter_class->filter = filter;
	mime_filter_class->complete = complete;
	mime_filter_class->reset = reset;
}

CamelType
camel_mime_filter_pgp_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type(), "CamelMimeFilterPgp",
					    sizeof (CamelMimeFilterPgp),
					    sizeof (CamelMimeFilterPgpClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_pgp_class_init,
					    NULL,
					    NULL,
					    NULL);
	}
	
	return type;
}

static void
filter_run(CamelMimeFilter *f, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace, int last)
{
	CamelMimeFilterPgp *pgpfilter = (CamelMimeFilterPgp *)f;
	char *inptr, *inend;
	register char *o;
	char *start = in;
	int tmplen;
	
	/* only need as much space as the input, we're stripping chars */
	camel_mime_filter_set_size(f, len, FALSE);

	o = f->outbuf;
	inptr = in;
	inend = in+len;
	while (inptr < inend) {
		start = inptr;

		while (inptr < inend && *inptr != '\n')
			inptr++;
			
		if (inptr == inend) {
			if (!last) {
				camel_mime_filter_backup(f, start, inend-start);
				inend = start;
			}
			break;
		}

		*inptr++ = 0;
		
		switch (pgpfilter->state) {
		case PGPF_HEADER:
			/* Wait for a blank line */
			if (strlen(start)==0)
				pgpfilter->state = PGPF_MESSAGE;
			break;
		case PGPF_MESSAGE:
			/* In the message body, check for end of body */
			if (strncmp(start, "-----", 5)==0) {
				pgpfilter->state = PGPF_FOOTER;
				break;
			}			
			/* do dash decoding */
			if (strncmp(start, "- ", 2)==0) {
				/* Dash encoded line found, skip encoding */
				start+=2;
			}
			tmplen=strlen(start)+1;
			inptr[-1] = '\n';
			strncpy(o, start, tmplen);
			o+=tmplen;	
			break;
		case PGPF_FOOTER:
			/* After end of message, (ie signature or something) skip it */
			break;
		}
		inptr[-1] = '\n';
	}
	
	*out = f->outbuf;
	*outlen = o - f->outbuf;
	*outprespace = f->outpre;
	
}

static void
filter(CamelMimeFilter *f, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace)
{
	filter_run(f, in, len, prespace, out, outlen, outprespace, FALSE);
}

static void 
complete(CamelMimeFilter *f, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace)
{
	filter_run(f, in, len, prespace, out, outlen, outprespace, TRUE);
}

static void
reset (CamelMimeFilter *f)
{
	/* no-op */
}

CamelMimeFilter *
camel_mime_filter_pgp_new(void)
{
	CamelMimeFilterPgp *pgpfilter = (CamelMimeFilterPgp *)camel_object_new (camel_mime_filter_pgp_get_type());

	return (CamelMimeFilter *) pgpfilter;
}
