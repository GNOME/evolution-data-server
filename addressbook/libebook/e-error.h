#define e_return_error_if_fail(expr,error_code)	G_STMT_START{		\
     if G_LIKELY(expr) { } else						\
       {								\
	 g_log (G_LOG_DOMAIN,						\
		G_LOG_LEVEL_CRITICAL,					\
		"file %s: line %d (%s): assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		G_STRFUNC,					\
		#expr);							\
	 g_set_error (error, E_BOOK_ERROR, (error_code),                \
		"file %s: line %d (%s): assertion `%s' failed",		\
		__FILE__,						\
		__LINE__,						\
		G_STRFUNC,					\
		#expr);							\
	 return FALSE;							\
       };				}G_STMT_END

#define e_return_async_error_if_fail(expr, error) G_STMT_START { \
    if G_LIKELY (expr) {} else {                                 \
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,                 \
             "file %s: line %d (%s): assertion `%s' failed",     \
             __FILE__, __LINE__, G_STRFUNC, #expr);    \
      cb (book, error, closure);                           \
      return 0;                                                  \
    }                                                            \
  } G_STMT_END                                                   \

#define e_return_async_error_val_if_fail(expr, error) G_STMT_START { \
    if G_LIKELY (expr) {} else {                                 \
      g_log (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,                 \
             "file %s: line %d (%s): assertion `%s' failed",     \
             __FILE__, __LINE__, G_STRFUNC, #expr);    \
      cb (book, error, NULL, closure);                           \
      return 0;                                                  \
    }                                                            \
  } G_STMT_END                                                   \

