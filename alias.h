/*
 * ProFTPD - mod_vroot Alias API
 * Copyright (c) 2016 TJ Saunders
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

#ifndef MOD_VROOT_ALIAS_H
#define MOD_VROOT_ALIAS_H

#include "mod_vroot.h"

unsigned int vroot_alias_count(void);

int vroot_alias_do(int cb(const void *key_data, size_t key_datasz,
  const void *value_data, size_t value_datasz, void *user_data),
  void *user_data);

int vroot_alias_exists(const char *path);

const char *vroot_alias_get(const char *dst_path);

int vroot_alias_add(const char *dst_path, const char *src_path);

/* Internal use only. */
int vroot_alias_init(pool *p);
int vroot_alias_free(void);

#endif /* MOD_VROOT_ALIAS_H */
