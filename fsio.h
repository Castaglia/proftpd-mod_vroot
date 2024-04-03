/*
 * ProFTPD - mod_vroot FSIO API
 * Copyright (c) 2016-2024 TJ Saunders
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

#ifndef MOD_VROOT_FSIO_H
#define MOD_VROOT_FSIO_H

#include "mod_vroot.h"

int vroot_fsio_stat(pr_fs_t *fs, const char *path, struct stat *st);
int vroot_fsio_lstat(pr_fs_t *fs, const char *path, struct stat *st);
int vroot_fsio_rename(pr_fs_t *fs, const char *from, const char *to);
int vroot_fsio_unlink(pr_fs_t *fs, const char *path);
int vroot_fsio_open(pr_fh_t *fh, const char *path, int flags);
int vroot_fsio_creat(pr_fh_t *fh, const char *path, mode_t mode);
int vroot_fsio_link(pr_fs_t *fs, const char *dst_path, const char *src_path);
int vroot_fsio_symlink(pr_fs_t *fs, const char *dst_path, const char *src_path);
int vroot_fsio_readlink(pr_fs_t *fs, const char *path, char *buf, size_t bufsz);
int vroot_fsio_truncate(pr_fs_t *fs, const char *path, off_t len);
int vroot_fsio_chmod(pr_fs_t *fs, const char *path, mode_t mode);
int vroot_fsio_chown(pr_fs_t *fs, const char *path, uid_t uid, gid_t gid);
int vroot_fsio_lchown(pr_fs_t *fs, const char *path, uid_t uid, gid_t gid);
int vroot_fsio_chroot(pr_fs_t *fs, const char *path);
int vroot_fsio_chdir(pr_fs_t *fs, const char *path);
int vroot_fsio_utimes(pr_fs_t *fs, const char *path, struct timeval *tvs);
const char *vroot_fsio_realpath(pr_fs_t *fs, pool *p, const char *path);
void *vroot_fsio_opendir(pr_fs_t *fs, const char *path);
struct dirent *vroot_fsio_readdir(pr_fs_t *fs, void *dirh);
int vroot_fsio_closedir(pr_fs_t *fs, void *dirh);
int vroot_fsio_mkdir(pr_fs_t *fs, const char *path, mode_t mode);
int vroot_fsio_rmdir(pr_fs_t *fs, const char *path);

/* Internal use only. */
int vroot_fsio_init(pool *p);
int vroot_fsio_free(void);

#endif /* MOD_VROOT_FSIO_H */
