/*
 * ProFTPD: mod_vroot Alias API
 * Copyright (c) 2002-2016 TJ Saunders
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Suite 500, Boston, MA 02110-1335, USA.
 *
 * As a special exemption, TJ Saunders and other respective copyright holders
 * give permission to link this program with OpenSSL, and distribute the
 * resulting executable, without including the source code for OpenSSL in the
 * source distribution.
 */

#include "alias.h"

static pool *alias_pool = NULL;
static pr_table_t *alias_tab = NULL;

unsigned int vroot_alias_count(void) {
  int count;

  count = pr_table_count(alias_tab);
  if (count < 0) {
    return 0;
  }

  return count;
}

int vroot_alias_do(int cb(const void *key_data, size_t key_datasz,
    const void *value_data, size_t value_datasz, void *user_data),
    void *user_data) {
  int res;

  if (cb == NULL) {
    errno = EINVAL;
    return -1;
  }

  res = pr_table_do(alias_tab, cb, user_data, PR_TABLE_DO_FL_ALL);
  return res;
}

int vroot_alias_exists(const char *path) {
  if (path == NULL) {
    return FALSE;
  }

  if (pr_table_get(alias_tab, path, 0) != NULL) {
    return TRUE;
  }

  return FALSE;
}

const char *vroot_alias_get(const char *path) {
  const void *v;

  if (path == NULL) {
    errno = EINVAL;
    return NULL;
  }

  v = pr_table_get(alias_tab, path, NULL);
  return v;
}

int vroot_alias_add(const char *dst_path, const char *src_path) {
  int res;

  if (dst_path == NULL ||
      src_path == NULL) {
    errno = EINVAL;
    return -1;
  }

  res = pr_table_add(alias_tab, pstrdup(alias_pool, dst_path),
    pstrdup(alias_pool, src_path), 0);
  return res;
}

int vroot_alias_init(pool *p) {
  if (p == NULL) {
    errno = EINVAL;
    return -1;
  }

  if (alias_pool == NULL) {
    alias_pool = make_sub_pool(p);
    pr_pool_tag(alias_pool, "VRoot Alias Pool");

    alias_tab = pr_table_alloc(alias_pool, 0);
  }

  return 0;
}

int vroot_alias_free(void) {
  if (alias_pool != NULL) {
    pr_table_empty(alias_tab);
    destroy_pool(alias_pool);
    alias_pool = NULL;
    alias_tab = NULL;
  }

  return 0;
}
