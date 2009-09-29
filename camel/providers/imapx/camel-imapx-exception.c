
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

#include <glib.h>

#include "camel-imapx-exception.h"

#include <pthread.h>

static pthread_key_t handler_key = 0;

void camel_exception_setup(void)
{
	pthread_key_create(&handler_key, NULL);
}

void
camel_exception_try(struct _CamelExceptionEnv *env)
{
	struct _CamelExceptionEnv *handler;

	handler = pthread_getspecific(handler_key);
	env->parent = handler;
	handler = env;
	env->ex = NULL;

	pthread_setspecific(handler_key, handler);
}

void
camel_exception_throw_ex(CamelException *ex)
{
	struct _CamelExceptionEnv *env;

	printf("throwing exception '%s'\n", ex->desc);

	env = pthread_getspecific(handler_key);
	if (env != NULL) {
		env->ex = ex;
		pthread_setspecific(handler_key, env->parent);
		longjmp(env->env, ex->id);
	} else {
		fprintf(stderr, "\nUncaught exception: %s\n", ex->desc);
		/* we just crash and burn, this is a code problem */
		/* we dont use g_assert_not_reached() since its not a noreturn function */
		abort();
	}
}

void
camel_exception_throw(int id, char *fmt, ...)
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
	pthread_setspecific(handler_key, env->parent);
}

void
camel_exception_done(struct _CamelExceptionEnv *env)
{
	pthread_setspecific(handler_key, env->parent);
	if (env->ex != NULL) {
		camel_exception_free(env->ex);
	}
}
