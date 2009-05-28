/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_log_ext_h_
#define	_log_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

gint __log_open __P((DB_ENV *));
gint __log_find __P((DB_LOG *, int, u_int32_t *, logfile_validity *));
gint __log_valid __P((DB_LOG *, u_int32_t, int, logfile_validity *));
gint __log_dbenv_refresh __P((DB_ENV *));
gint __log_stat __P((DB_ENV *, DB_LOG_STAT **, u_int32_t));
void __log_get_cached_ckp_lsn __P((DB_ENV *, DB_LSN *));
void __log_region_destroy __P((DB_ENV *, REGINFO *));
gint __log_vtruncate __P((DB_ENV *, DB_LSN *, DB_LSN *));
gint __log_is_outdated __P((DB_ENV *dbenv, u_int32_t fnum, gint *outdatedp));
gint __log_archive __P((DB_ENV *, gchar **[], u_int32_t));
gint __log_cursor __P((DB_ENV *, DB_LOGC **, u_int32_t));
void __log_dbenv_create __P((DB_ENV *));
gint __log_put __P((DB_ENV *, DB_LSN *, const DBT *, u_int32_t));
void __log_txn_lsn __P((DB_ENV *, DB_LSN *, u_int32_t *, u_int32_t *));
gint __log_newfile __P((DB_LOG *, DB_LSN *));
gint __log_flush __P((DB_ENV *, const DB_LSN *));
gint __log_file __P((DB_ENV *, const DB_LSN *, gchar *, size_t));
gint __log_name __P((DB_LOG *, u_int32_t, gchar **, DB_FH *, u_int32_t));
gint __log_rep_put __P((DB_ENV *, DB_LSN *, const DBT *));

#if defined(__cplusplus)
}
#endif
#endif /* !_log_ext_h_ */
