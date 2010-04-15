
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <glib.h>

#include "camel-imapx-exception.h"

static GStaticPrivate handler_key = G_STATIC_PRIVATE_INIT;

void camel_exception_setup(void)
{
	/* No need for 'new' with GStaticPrivate */
	/* g_static_private_new (&handler_key); */
}

void
camel_exception_try(struct _CamelExceptionEnv *env)
{
	struct _CamelExceptionEnv *handler;

	handler = g_static_private_get (&handler_key);
	env->parent = handler;
	handler = env;
	env->ex = NULL;

	g_static_private_set (&handler_key, handler, NULL);
}

void
camel_exception_throw_ex(CamelException *ex)
{
	struct _CamelExceptionEnv *env;

	printf("throwing exception '%s'\n", ex->desc);

	env = g_static_private_get (&handler_key);
	if (env != NULL) {
		env->ex = ex;
		g_static_private_set (&handler_key, env->parent, NULL);
		longjmp(env->env, ex->id);
	} else {
		fprintf(stderr, "\nUncaught exception: %s\n", ex->desc);
		/* we just crash and burn, this is a code problem */
		/* we dont use g_assert_not_reached() since its not a noreturn function */
		abort();
	}
}

void
camel_exception_throw(gint id, gchar *fmt, ...)
{
	CamelException *ex;
	va_list ap;

	ex = camel_exception_new();
	ex->id = id;
	va_start(ap, fmt);
	ex->desc = g_strdup_vprintf(fmt, ap);
	va_end(ap);

	camel_exception_throw_ex(ex);
}

void
camel_exception_drop(struct _CamelExceptionEnv *env)
{
	g_static_private_set (&handler_key, env->parent, NULL);
}

void
camel_exception_done(struct _CamelExceptionEnv *env)
{
	g_static_private_set (&handler_key, env->parent, NULL);
	if (env->ex != NULL) {
		camel_exception_free(env->ex);
	}
}
