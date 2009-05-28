/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_clib_ext_h_
#define	_clib_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

#ifndef HAVE_GETCWD
gchar *getcwd __P((gchar *, size_t));
#endif
#ifndef HAVE_GETOPT
gint getopt __P((int, gchar * const *, const gchar *));
#endif
#ifndef HAVE_MEMCMP
gint memcmp __P((gconstpointer , gconstpointer , size_t));
#endif
#ifndef HAVE_MEMCPY
gpointer memcpy __P((gpointer , gconstpointer , size_t));
#endif
#ifndef HAVE_MEMMOVE
gpointer memmove __P((gpointer , gconstpointer , size_t));
#endif
#ifndef HAVE_RAISE
gint raise __P((int));
#endif
#ifndef HAVE_SNPRINTF
gint snprintf __P((gchar *, size_t, const gchar *, ...));
#endif
#ifndef HAVE_STRCASECMP
gint strcasecmp __P((const gchar *, const gchar *));
#endif
#ifndef HAVE_STRCASECMP
gint strncasecmp __P((const gchar *, const gchar *, size_t));
#endif
#ifndef HAVE_STRDUP
gchar *strdup __P((const gchar *));
#endif
#ifndef HAVE_STRERROR
gchar *strerror __P((int));
#endif
#ifndef HAVE_VSNPRINTF
gint vsnprintf __P((gchar *, size_t, const gchar *, va_list));
#endif

#if defined(__cplusplus)
}
#endif
#endif /* !_clib_ext_h_ */
