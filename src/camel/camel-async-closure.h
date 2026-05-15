/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_ASYNC_CLOSURE_H
#define CAMEL_ASYNC_CLOSURE_H

#include <gio/gio.h>

typedef struct _CamelAsyncClosure CamelAsyncClosure;

CamelAsyncClosure *
		camel_async_closure_new		(void);
GAsyncResult *	camel_async_closure_wait	(CamelAsyncClosure *closure);
void		camel_async_closure_free	(CamelAsyncClosure *closure);
void		camel_async_closure_callback	(GObject *source_object,
						 GAsyncResult *result,
						 gpointer closure);

#endif /* CAMEL_ASYNC_CLOSURE_H */

