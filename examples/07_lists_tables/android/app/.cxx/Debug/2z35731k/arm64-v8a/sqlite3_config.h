/* the compile-time options used to build the SQLite3 library
 *
 * https://github.com/azadkuh/sqlite-amalgamation
 */

#ifndef SQLITE3_CONFIG_H
#define SQLITE3_CONFIG_H


/* #undef SQLITE_ENABLE_COLUMN_METADATA */
/* #undef SQLITE_ENABLE_DBSTAT_VTAB */
/* #undef SQLITE_ENABLE_FTS3 */
/* #undef SQLITE_ENABLE_FTS4 */
/* #undef SQLITE_ENABLE_FTS5 */
/* #undef SQLITE_ENABLE_GEOPOLY */
/* #undef SQLITE_ENABLE_ICU */
#define SQLITE_ENABLE_MATH_FUNCTIONS
/* #undef SQLITE_ENABLE_RBU */
/* #undef SQLITE_ENABLE_RTREE */
/* #undef SQLITE_ENABLE_STAT4 */
#define SQLITE_OMIT_DECLTYPE
/* #undef SQLITE_OMIT_JSON */
/* #undef SQLITE_USE_URI */

#define SQLITE_RECOMMENDED_OPTIONS
#if defined(SQLITE_RECOMMENDED_OPTIONS)
#   define SQLITE_DQS 0
#   define SQLITE_DEFAULT_MEMSTATUS 0
#   define SQLITE_DEFAULT_WAL_SYNCHRONOUS 1
#   define SQLITE_LIKE_DOESNT_MATCH_BLOBS
#   define SQLITE_MAX_EXPR_DEPTH 1
#   define SQLITE_OMIT_DEPRECATED
#   define SQLITE_OMIT_PROGRESS_CALLBACK
#   define SQLITE_OMIT_SHARED_CACHE
#   define SQLITE_USE_ALLOCA
#endif /* SQLITE_RECOMMENDED_OPTIONS */


#endif /* SQLITE3_CONFIG_H */
