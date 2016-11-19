/*
 * ProFTPD - mod_vroot Path API
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

#ifndef MOD_VROOT_PATH_H
#define MOD_VROOT_PATH_H

#include "mod_vroot.h"

int vroot_path_have_base(void);
const char *vroot_path_get_base(pool *p, size_t *baselen);
int vroot_path_set_base(const char *base, size_t baselen);

void vroot_path_clean(char *path);

int vroot_path_lookup(pool *p, char *path, size_t pathlen, const char *dir,
  int flags, char **alias_path);
#define VROOT_LOOKUP_FL_NO_ALIAS	0x001

char *vroot_realpath(pool *p, const char *path, int flags);
#define VROOT_REALPATH_FL_ABS_PATH	0x001

#endif /* MOD_VROOT_PATH_H */
