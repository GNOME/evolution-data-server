/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_common_ext_h_
#define	_common_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __db_isbigendian __P((void));
gint __db_byteorder __P((DB_ENV *, int));
gint __db_fchk __P((DB_ENV *, const gchar *, u_int32_t, u_int32_t));
gint __db_fcchk __P((DB_ENV *, const gchar *, u_int32_t, u_int32_t, u_int32_t));
gint __db_ferr __P((const DB_ENV *, const gchar *, int));
void __db_pgerr __P((DB *, db_pgno_t, int));
gint __db_pgfmt __P((DB_ENV *, db_pgno_t));
gint __db_eopnotsup __P((const DB_ENV *));
#ifdef DIAGNOSTIC
void __db_assert __P((const gchar *, const gchar *, int));
#endif
gint __db_panic_msg __P((DB_ENV *));
gint __db_panic __P((DB_ENV *, int));
void __db_err __P((const DB_ENV *, const gchar *, ...));
void __db_errcall __P((const DB_ENV *, int, int, const gchar *, va_list));
void __db_errfile __P((const DB_ENV *, int, int, const gchar *, va_list));
void __db_logmsg __P((const DB_ENV *, DB_TXN *, const gchar *, u_int32_t, const gchar *, ...));
gint __db_unknown_flag __P((DB_ENV *, gchar *, u_int32_t));
gint __db_unknown_type __P((DB_ENV *, gchar *, DBTYPE));
gint __db_check_txn __P((DB *, DB_TXN *, u_int32_t, int));
gint __db_not_txn_env __P((DB_ENV *));
gint __db_getlong __P((DB *, const gchar *, gchar *, long, long, long *));
gint __db_getulong __P((DB *, const gchar *, gchar *, u_long, u_long, u_long *));
void __db_idspace __P((u_int32_t *, int, u_int32_t *, u_int32_t *));
u_int32_t __db_log2 __P((u_int32_t));
gint __db_util_arg __P((gchar *, gchar *, gint *, gchar ***));
gint __db_util_cache __P((DB_ENV *, DB *, u_int32_t *, gint *));
gint __db_util_logset __P((const gchar *, gchar *));
void __db_util_siginit __P((void));
gint __db_util_interrupted __P((void));
void __db_util_sigresend __P((void));

#if defined(__cplusplus)
}
#endif
#endif /* !_common_ext_h_ */
