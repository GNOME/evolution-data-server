/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#include <string.h>

#include "camel-mime-filter.h"

/*#define MALLOC_CHECK */ /* for some malloc checking, requires mcheck enabled */

/* only suitable for glibc */
#ifdef MALLOC_CHECK
#include <mcheck.h>
#endif

struct _CamelMimeFilterPrivate {
	char *inbuf;
	size_t inlen;
};

#define PRE_HEAD (64)
#define BACK_HEAD (64)
#define _PRIVATE(o) (((CamelMimeFilter *)(o))->priv)
#define FCLASS(o) ((CamelMimeFilterClass *)(CAMEL_OBJECT_GET_CLASS(o)))

static CamelObjectClass *camel_mime_filter_parent;

static void complete (CamelMimeFilter *mf, char *in, size_t len, 
		      size_t prespace, char **out, size_t *outlen, 
		      size_t *outprespace);

static void
camel_mime_filter_class_init (CamelMimeFilterClass *klass)
{
	camel_mime_filter_parent = camel_type_get_global_classfuncs (camel_object_get_type ());

	klass->complete = complete;
}

static void
camel_mime_filter_init (CamelMimeFilter *obj)
{
	obj->outreal = NULL;
	obj->outbuf = NULL;
	obj->outsize = 0;

	obj->backbuf = NULL;
	obj->backsize = 0;
	obj->backlen = 0;

	_PRIVATE(obj) = g_malloc0(sizeof(*obj->priv));
}

static void
camel_mime_filter_finalize(CamelObject *o)
{
	CamelMimeFilter *f = (CamelMimeFilter *)o;
	struct _CamelMimeFilterPrivate *p = _PRIVATE(f);

	g_free(f->outreal);
	g_free(f->backbuf);
	g_free(p->inbuf);
	g_free(p);
}

CamelType
camel_mime_filter_get_type (void)
{
	static CamelType camel_mime_filter_type = CAMEL_INVALID_TYPE;
	
	if (camel_mime_filter_type == CAMEL_INVALID_TYPE) {
		camel_mime_filter_type = camel_type_register (CAMEL_OBJECT_TYPE, "CamelMimeFilter",
							      sizeof (CamelMimeFilter),
							      sizeof (CamelMimeFilterClass),
							      (CamelObjectClassInitFunc) camel_mime_filter_class_init,
							      NULL,
							      (CamelObjectInitFunc) camel_mime_filter_init,
							      (CamelObjectFinalizeFunc) camel_mime_filter_finalize);
	}
	
	return camel_mime_filter_type;
}

static void
complete(CamelMimeFilter *mf, char *in, size_t len, size_t prespace, char **out, size_t *outlen, size_t *outprespace)
{
	/* default - do nothing */
}


/**
 * camel_mime_filter_new:
 *
 * Create a new #CamelMimeFilter object.
 * 
 * Returns a new #CamelMimeFilter
 **/
CamelMimeFilter *
camel_mime_filter_new (void)
{
	CamelMimeFilter *new = CAMEL_MIME_FILTER ( camel_object_new (camel_mime_filter_get_type ()));
	return new;
}

#ifdef MALLOC_CHECK
static void
checkmem(void *p)
{
	if (p) {
		int status = mprobe(p);

		switch (status) {
		case MCHECK_HEAD:
			printf("Memory underrun at %p\n", p);
			abort();
		case MCHECK_TAIL:
			printf("Memory overrun at %p\n", p);
			abort();
		case MCHECK_FREE:
			printf("Double free %p\n", p);
			abort();
		}
	}
}
#endif

static void filter_run(CamelMimeFilter *f,
		       char *in, size_t len, size_t prespace,
		       char **out, size_t *outlen, size_t *outprespace,
		       void (*filterfunc)(CamelMimeFilter *f,
					  char *in, size_t len, size_t prespace,
					  char **out, size_t *outlen, size_t *outprespace))
{
	struct _CamelMimeFilterPrivate *p;

#ifdef MALLOC_CHECK
	checkmem(f->outreal);
	checkmem(f->backbuf);
#endif
	/*
	  here we take a performance hit, if the input buffer doesn't
	  have the pre-space required.  We make a buffer that does ...
	*/
	if (prespace < f->backlen) {
		int newlen = len+prespace+f->backlen;
		p = _PRIVATE(f);
		if (p->inlen < newlen) {
			/* NOTE: g_realloc copies data, we dont need that (slower) */
			g_free(p->inbuf);
			p->inbuf = g_malloc(newlen+PRE_HEAD);
			p->inlen = newlen+PRE_HEAD;
		}
		/* copy to end of structure */
		memcpy(p->inbuf+p->inlen - len, in, len);
		in = p->inbuf+p->inlen - len;
		prespace = p->inlen - len;
	}

#ifdef MALLOC_CHECK
	checkmem(f->outreal);
	checkmem(f->backbuf);
#endif

	/* preload any backed up data */
	if (f->backlen > 0) {
		memcpy(in-f->backlen, f->backbuf, f->backlen);
		in -= f->backlen;
		len += f->backlen;
		prespace -= f->backlen;
		f->backlen = 0;
	}
	
	filterfunc(f, in, len, prespace, out, outlen, outprespace);

#ifdef MALLOC_CHECK
	checkmem(f->outreal);
	checkmem(f->backbuf);
#endif

}


/**
 * camel_mime_filter_filter:
 * @filter: a #CamelMimeFilter object
 * @in: input buffer
 * @len: length of @in
 * @prespace: amount of prespace
 * @out: pointer to the output buffer (to be set)
 * @outlen: pointer to the length of the output buffer (to be set)
 * @outprespace: pointer to the output prespace length (to be set)
 *
 * Passes the input buffer, @in, through @filter and generates an
 * output buffer, @out.
 **/
void
camel_mime_filter_filter (CamelMimeFilter *filter,
			  char *in, size_t len, size_t prespace,
			  char **out, size_t *outlen, size_t *outprespace)
{
	if (FCLASS(filter)->filter)
		filter_run(filter, in, len, prespace, out, outlen, outprespace, FCLASS(filter)->filter);
	else
		g_error("Filter function unplmenented in class");
}


/**
 * camel_mime_filter_complete:
 * @filter: a #CamelMimeFilter object
 * @in: input buffer
 * @len: length of @in
 * @prespace: amount of prespace
 * @out: pointer to the output buffer (to be set)
 * @outlen: pointer to the length of the output buffer (to be set)
 * @outprespace: pointer to the output prespace length (to be set)
 *
 * Passes the input buffer, @in, through @filter and generates an
 * output buffer, @out and makes sure that all data is flushed to the
 * output buffer. This must be the last filtering call made, no
 * further calls to #camel_mime_filter_filter may be called on @filter
 * until @filter has been reset using #camel_mime_filter_reset.
 **/
void
camel_mime_filter_complete (CamelMimeFilter *filter,
			    char *in, size_t len, size_t prespace,
			    char **out, size_t *outlen, size_t *outprespace)
{
	if (FCLASS(filter)->complete)
		filter_run(filter, in, len, prespace, out, outlen, outprespace, FCLASS(filter)->complete);
}


/**
 * camel_mime_filter_reset:
 * @filter: a #CamelMimeFilter object
 *
 * Resets the state on @filter so that it may be used again.
 **/
void
camel_mime_filter_reset(CamelMimeFilter *filter)
{
	if (FCLASS(filter)->reset) {
		FCLASS(filter)->reset(filter);
	}

	/* could free some buffers, if they are really big? */
	filter->backlen = 0;
}


/**
 * camel_mime_filter_backup:
 * @filter: a #camelMimeFilter object
 * @data: data buffer to backup
 * @length: length of @data
 *
 * Saves @data to be used as prespace input data to the next call to
 * #camel_mime_filter_filter or #camel_mime_filter_complete.
 *
 * Note: New calls replace old data.
 **/
void
camel_mime_filter_backup(CamelMimeFilter *filter, const char *data, size_t length)
{
	if (filter->backsize < length) {
		/* g_realloc copies data, unnecessary overhead */
		g_free(filter->backbuf);
		filter->backbuf = g_malloc(length+BACK_HEAD);
		filter->backsize = length+BACK_HEAD;
	}
	filter->backlen = length;
	memcpy(filter->backbuf, data, length);
}


/**
 * camel_mime_filter_set_size:
 * @filter: a #camelMimeFilter object
 * @size: requested amount of storage space
 * @keep: %TRUE to keep existing buffered data or %FALSE otherwise
 *
 * Ensure that @filter has enough storage space to store @size bytes
 * for filter output.
 **/
void
camel_mime_filter_set_size(CamelMimeFilter *filter, size_t size, int keep)
{
	if (filter->outsize < size) {
		int offset = filter->outptr - filter->outreal;
		if (keep) {
			filter->outreal = g_realloc(filter->outreal, size + PRE_HEAD*4);
		} else {
			g_free(filter->outreal);
			filter->outreal = g_malloc(size + PRE_HEAD*4);
		}
		filter->outptr = filter->outreal + offset;
		filter->outbuf = filter->outreal + PRE_HEAD*4;
		filter->outsize = size;
		/* this could be offset from the end of the structure, but 
		   this should be good enough */
		filter->outpre = PRE_HEAD*4;
	}
}
