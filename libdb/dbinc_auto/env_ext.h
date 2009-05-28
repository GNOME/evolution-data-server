/* DO NOT EDIT: automatically built by dist/s_include. */
#ifndef	_env_ext_h_
#define	_env_ext_h_

#if defined(__cplusplus)
extern "C" {
#endif

void __db_shalloc_init __P((gpointer , size_t));
gint __db_shalloc_size __P((size_t, size_t));
gint __db_shalloc __P((gpointer , size_t, size_t, gpointer ));
void __db_shalloc_free __P((gpointer , gpointer ));
size_t __db_shsizeof __P((gpointer));
void __db_shalloc_dump __P((gpointer , FILE *));
gint __db_tablesize __P((u_int32_t));
void __db_hashinit __P((gpointer , u_int32_t));
gint __db_fileinit __P((DB_ENV *, DB_FH *, size_t, int));
gint __db_overwrite __P((DB_ENV *, const gchar *));
gint __db_mi_env __P((DB_ENV *, const gchar *));
gint __db_mi_open __P((DB_ENV *, const gchar *, int));
gint __db_env_config __P((DB_ENV *, gchar *, u_int32_t));
gint __dbenv_open __P((DB_ENV *, const gchar *, u_int32_t, int));
gint __dbenv_remove __P((DB_ENV *, const gchar *, u_int32_t));
gint __dbenv_close __P((DB_ENV *, u_int32_t));
gint __db_appname __P((DB_ENV *, APPNAME, const gchar *, u_int32_t, DB_FH *, gchar **));
gint __db_home __P((DB_ENV *, const gchar *, u_int32_t));
gint __db_apprec __P((DB_ENV *, DB_LSN *, u_int32_t));
gint __env_openfiles __P((DB_ENV *, DB_LOGC *, gpointer , DBT *, DB_LSN *, DB_LSN *, double, int));
gint __db_e_attach __P((DB_ENV *, u_int32_t *));
gint __db_e_detach __P((DB_ENV *, int));
gint __db_e_remove __P((DB_ENV *, u_int32_t));
gint __db_e_stat __P((DB_ENV *, REGENV *, REGION *, gint *, u_int32_t));
gint __db_r_attach __P((DB_ENV *, REGINFO *, size_t));
gint __db_r_detach __P((DB_ENV *, REGINFO *, int));

#if defined(__cplusplus)
}
#endif
#endif /* !_env_ext_h_ */
