/* xbt/dict.h -- api to a generic dictionary                                */

/* Copyright (c) 2004-2025. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef XBT_DICT_H
#define XBT_DICT_H

#include <xbt/dynar.h> /* void_f_pvoid_t */
#include <xbt/misc.h>  /* SG_BEGIN_DECL */

#include <stdint.h>             /* uintptr_t */

SG_BEGIN_DECL

/** @addtogroup XBT_dict
 *  @brief The dictionary data structure (comparable to hash tables)
 *
 *  This section describes the API to a dictionary structure that  associates as string to a void* key. It provides the
 *  same functionality than an hash table.
 *
 *  @deprecated If you are using C++, you might want to use `std::unordered_map` instead.
 *
 *  Here is a little example of use:

@verbatim
 xbt_dict_t mydict = xbt_dict_new_homogeneous(free);
 char buff[512];

 sprintf(buff,"some very precious data");
 xbt_dict_set(mydict,"my data", strdup(buff));

 sprintf(buff,"another good stuff");
 xbt_dict_set(mydict,"my data", strdup(buff)); // previous data gets erased (and freed) by second add
@endverbatim
 */

/** @defgroup XBT_dict_cons Dict constructor and destructor
 *  @ingroup XBT_dict
 *
 *  @{
 */

/** @brief Dictionary data type (opaque structure) */
typedef struct s_xbt_dict *xbt_dict_t;
typedef const struct s_xbt_dict* const_xbt_dict_t;
typedef struct s_xbt_dictelm *xbt_dictelm_t;
typedef struct s_xbt_dictelm {
  char *key;
  int key_len;
  unsigned int hash_code;

  void *content;

  xbt_dictelm_t next;
} s_xbt_dictelm_t;

XBT_PUBLIC xbt_dict_t xbt_dict_new_homogeneous(void_f_pvoid_t free_ctn);
XBT_PUBLIC void xbt_dict_free(xbt_dict_t* dict);
XBT_PUBLIC unsigned int xbt_dict_size(const_xbt_dict_t dict);

/** @} */
/** @defgroup XBT_dict_basic Dictionaries basic usage
 *  @ingroup XBT_dict
 *
 * Careful, those functions assume that the key is null-terminated.
 *
 *  @{
 */

XBT_PUBLIC void xbt_dict_set(xbt_dict_t dict, const char* key, void* data);
XBT_PUBLIC void* xbt_dict_get_or_null(const_xbt_dict_t dict, const char* key);
XBT_PUBLIC xbt_dictelm_t xbt_dict_get_elm_or_null(const_xbt_dict_t dict, const char* key);

XBT_PUBLIC int xbt_dict_length(const_xbt_dict_t dict);
XBT_PUBLIC int xbt_dict_is_empty(const_xbt_dict_t dict);

/** @} */
/** @defgroup XBT_dict_nnul Dictionaries with non-nul terminated keys
 *  @ingroup XBT_dict
 *
 * Those functions work even with non-null terminated keys.
 *
 *  @{
 */
XBT_PUBLIC void xbt_dict_set_ext(xbt_dict_t dict, const char* key, int key_len, void* data);
XBT_PUBLIC void* xbt_dict_get_or_null_ext(const_xbt_dict_t dict, const char* key, int key_len);
XBT_PUBLIC void xbt_dict_remove_ext(xbt_dict_t dict, const char* key, int key_len);

struct s_xbt_dict_cursor {
  xbt_dictelm_t current;
  int line;
  const_xbt_dict_t dict;
};

/** @} */
/** @defgroup XBT_dict_curs Cursors on dictionaries
 *  @ingroup XBT_dict
 *
 *  Don't get impressed, there is a lot of functions here, but traversing a dictionary is immediate with the
 *  xbt_dict_foreach macro.
 *  You only need the other functions in rare cases (they are not used directly in SG itself).
 *
 *  Here is an example (assuming that the dictionary contains strings, i.e., that the <tt>data</tt> argument of
 *  xbt_dict_set was always a null-terminated char*):
@verbatim xbt_dict_cursor_t cursor=NULL;
 char *key,*data;

 xbt_dict_foreach(dict,cursor,key,data) {
    printf("   - Seen:  %s->%s\n",key,data);
 }@endverbatim

 *
 *  @warning Do not add or remove entries to the cache while traversing !!
 *
 *  @{ */

/** @brief Cursor on dictionaries (opaque type) */
typedef struct s_xbt_dict_cursor *xbt_dict_cursor_t;
typedef const struct s_xbt_dict_cursor* const_xbt_dict_cursor_t;

XBT_PUBLIC xbt_dict_cursor_t xbt_dict_cursor_new(const_xbt_dict_t dict);
XBT_PUBLIC void xbt_dict_cursor_free(xbt_dict_cursor_t* cursor);

XBT_PUBLIC void xbt_dict_cursor_rewind(xbt_dict_cursor_t cursor);

XBT_PUBLIC char* xbt_dict_cursor_get_key(const_xbt_dict_cursor_t cursor);
XBT_PUBLIC void* xbt_dict_cursor_get_data(const_xbt_dict_cursor_t cursor);

XBT_PUBLIC void xbt_dict_cursor_first(const_xbt_dict_t dict, xbt_dict_cursor_t* cursor);
XBT_PUBLIC void xbt_dict_cursor_step(xbt_dict_cursor_t cursor);
XBT_PUBLIC int xbt_dict_cursor_get_or_free(xbt_dict_cursor_t* cursor, char** key, void** data);
/** @def xbt_dict_foreach
 *  @param dict a @ref xbt_dict_t iterator
 *  @param cursor an @ref xbt_dict_cursor_t used as cursor
 *  @param key a char*
 *  @param data a void** output
 *  @hideinitializer
 *
 * @note An example of usage:
 * @code
xbt_dict_cursor_t cursor = NULL;
char *key;
char *data;

xbt_dict_foreach(head, cursor, key, data) {
 printf("Key %s with data %s\n",key,data);
}
@endcode
 */
#define xbt_dict_foreach(dict, cursor, key, data)                                                                      \
  for ((cursor) = NULL, xbt_dict_cursor_first((dict), &(cursor));                                                      \
       xbt_dict_cursor_get_or_free(&(cursor), (char**)&(key), (void**)&(data)); xbt_dict_cursor_step(cursor))

/** @} */

SG_END_DECL

#endif /* XBT_DICT_H */
