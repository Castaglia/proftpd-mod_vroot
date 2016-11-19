/*
 * ProFTPD: mod_vroot FSIO API
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 *
 * As a special exemption, TJ Saunders and other respective copyright holders
 * give permission to link this program with OpenSSL, and distribute the
 * resulting executable, without including the source code for OpenSSL in the
 * source distribution.
 */

#include "fsio.h"
#include "path.h"
#include "alias.h"

static pool *vroot_dir_pool = NULL;
static pr_table_t *vroot_dirtab = NULL;

static const char *trace_channel = "vroot.fsio";

int vroot_fsio_stat(pr_fs_t *fs, const char *stat_path, struct stat *st) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *path = NULL;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    return stat(stat_path, st);
  }

  tmp_pool = make_sub_pool(session.pool);
  pr_pool_tag(tmp_pool, "VRoot FSIO stat pool");
  path = vroot_realpath(tmp_pool, stat_path, 0);

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    destroy_pool(tmp_pool);
    return -1;
  }

  res = stat(vpath, st);
  destroy_pool(tmp_pool);
  return res;
}

int vroot_fsio_lstat(pr_fs_t *fs, const char *lstat_path, struct stat *st) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *path = NULL;
  size_t pathlen = 0;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    return lstat(lstat_path, st);
  }

  tmp_pool = make_sub_pool(session.pool);
  pr_pool_tag(tmp_pool, "VRoot FSIO lstat pool");

  path = pstrdup(tmp_pool, lstat_path);
  vroot_path_clean(path);

  /* If the given path ends in a slash, remove it.  The handling of
   * VRootAliases is sensitive to such things.
   */
  pathlen = strlen(path);
  if (pathlen > 1 &&
      path[pathlen-1] == '/') {
    path[pathlen-1] = '\0';
    pathlen--;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    destroy_pool(tmp_pool);
    return -1;
  }

  if ((vroot_opts & VROOT_OPT_ALLOW_SYMLINKS) ||
      vroot_alias_exists(path) == 0) {
    res = lstat(vpath, st);
    if (res < 0) {
      destroy_pool(tmp_pool);
      return -1;
    }

    res = stat(vpath, st);
    destroy_pool(tmp_pool);
    return res;
  }

  res = lstat(vpath, st);
  destroy_pool(tmp_pool);
  return res;
}

int vroot_fsio_rename(pr_fs_t *fs, const char *from, const char *to) {
  int res;
  char vpath1[PR_TUNABLE_PATH_MAX + 1], vpath2[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = rename(from, to);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath1, sizeof(vpath1)-1, from, 0, NULL) < 0) {
    return -1;
  }

  if (vroot_path_lookup(NULL, vpath2, sizeof(vpath2)-1, to, 0, NULL) < 0) {
    return -1;
  }

  res = rename(vpath1, vpath2);
  return res;
}

int vroot_fsio_unlink(pr_fs_t *fs, const char *path) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = unlink(path);
    return res;
  }

  /* Do not allow deleting of aliased files/directories; the aliases may only
   * exist for this user/group.
   */
  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path,
      VROOT_LOOKUP_FL_NO_ALIAS, NULL) < 0) {
    return -1;
  }

  if (vroot_alias_exists(vpath) == 0) {
    (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
      "denying delete of '%s' because it is a VRootAlias", vpath);
    errno = EACCES;
    return -1;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = unlink(vpath);
  return res;
}

int vroot_fsio_open(pr_fh_t *fh, const char *path, int flags) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = open(path, flags, PR_OPEN_MODE);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = open(vpath, flags, PR_OPEN_MODE);
  return res;
}

int vroot_fsio_creat(pr_fh_t *fh, const char *path, mode_t mode) {
  int res;
#if PROFTPD_VERSION_NUMBER < 0x0001030603
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = creat(path, mode);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = creat(vpath, mode);
#else
  errno = ENOSYS;
  res = -1;
#endif /* ProFTPD 1.3.6rc2 or earlier */

  return res;
}

int vroot_fsio_link(pr_fs_t *fs, const char *path1, const char *path2) {
  int res;
  char vpath1[PR_TUNABLE_PATH_MAX + 1], vpath2[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = link(path1, path2);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath1, sizeof(vpath1)-1, path1, 0, NULL) < 0) {
    return -1;
  }

  if (vroot_path_lookup(NULL, vpath2, sizeof(vpath2)-1, path2, 0, NULL) < 0) {
    return -1;
  }

  res = link(vpath1, vpath2);
  return res;
}

int vroot_fsio_symlink(pr_fs_t *fs, const char *path1, const char *path2) {
  int res;
  char vpath1[PR_TUNABLE_PATH_MAX + 1], vpath2[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = symlink(path1, path2);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath1, sizeof(vpath1)-1, path1, 0, NULL) < 0) {
    return -1;
  }

  if (vroot_path_lookup(NULL, vpath2, sizeof(vpath2)-1, path2, 0, NULL) < 0) {
    return -1;
  }

  res = symlink(vpath1, vpath2);
  return res;
}

int vroot_fsio_readlink(pr_fs_t *fs, const char *readlink_path, char *buf,
    size_t bufsz) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *path = NULL, *alias_path = NULL;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    return readlink(readlink_path, buf, bufsz);
  }

  /* In order to find any VRootAlias paths, we need to use the full path.
   * However, if we do NOT find any VRootAlias, then we do NOT want to use
   * the full path.
   */

  tmp_pool = make_sub_pool(session.pool);
  pr_pool_tag(tmp_pool, "VRoot FSIO readlink pool");

  path = vroot_realpath(tmp_pool, readlink_path, VROOT_REALPATH_FL_ABS_PATH);

  if (vroot_path_lookup(tmp_pool, vpath, sizeof(vpath)-1, path, 0,
      &alias_path) < 0) {
    destroy_pool(tmp_pool);
    return -1;
  }

  if (alias_path == NULL) {
    if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, readlink_path, 0,
        NULL) < 0) {
      destroy_pool(tmp_pool);
      return -1;
    }
  }

  res = readlink(vpath, buf, bufsz);
  destroy_pool(tmp_pool);
  return res;
}

int vroot_fsio_truncate(pr_fs_t *fs, const char *path, off_t len) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = truncate(path, len);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = truncate(vpath, len);
  return res;
}

int vroot_fsio_chmod(pr_fs_t *fs, const char *path, mode_t mode) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = chmod(path, mode);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = chmod(vpath, mode);
  return res;
}

int vroot_fsio_chown(pr_fs_t *fs, const char *path, uid_t uid, gid_t gid) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = chown(path, uid, gid);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = chown(vpath, uid, gid);
  return res;
}

int vroot_fsio_lchown(pr_fs_t *fs, const char *path, uid_t uid, gid_t gid) {
  int res;
#if PROFTPD_VERSION_NUMBER >= 0x0001030407
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = lchown(path, uid, gid);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = lchown(vpath, uid, gid);
#else
  errno = ENOSYS;
  res = -1;
#endif /* ProFTPD 1.3.4c or later */

  return res;
}

int vroot_fsio_chroot(pr_fs_t *fs, const char *path) {
  char base[PR_TUNABLE_PATH_MAX + 1];
  char *chroot_path = "/", *tmp = NULL;
  config_rec *c;
  size_t baselen = 0;

  if (path == NULL ||
      *path == '\0') {
    errno = EINVAL;
    return -1;
  }

  memset(base, '\0', sizeof(base));

  if (path[0] == '/' &&
      path[1] == '\0') {
    /* chrooting to '/', nothing needs to be done. */
    return 0;
  }

  c = find_config(main_server->conf, CONF_PARAM, "VRootServerRoot", FALSE);
  if (c != NULL) {
    int res;
    char *server_root, *ptr = NULL;

    server_root = c->argv[0];

    /* If the last character in the configured path is a slash, remove
     * it temporarily.
     */
    if (server_root[strlen(server_root)-1] == '/') {
      ptr = &(server_root[strlen(server_root)-1]);
      *ptr = '\0';
    }

    /* Now, make sure that the given path is below the configured
     * VRootServerRoot.  If so, then we perform a real chroot to the
     * VRootServerRoot directory, then use vroots from there.
     */ 

    res = strncmp(path, server_root, strlen(server_root));

    if (ptr != NULL) {
      *ptr = '/';
    }

    if (res == 0) {
      (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
        "chroot path '%s' within VRootServerRoot '%s', "
        "chrooting to VRootServerRoot", path, server_root);

      if (chroot(server_root) < 0) {
        (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
          "error chrooting to VRootServerRoot '%s': %s", server_root,
          strerror(errno));
        return -1;
      }

      pr_fs_clean_path(path + strlen(server_root), base, sizeof(base));
      chroot_path = server_root;

    } else {
      (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
        "chroot path '%s' is not within VRootServerRoot '%s', "
        "not chrooting to VRootServerRoot", path, server_root);
      pr_fs_clean_path(path, base, sizeof(base));
    }

  } else {
    pr_fs_clean_path(path, base, sizeof(base));
  }

  tmp = base;

  /* Advance to the end of the path. */
  while (*tmp != '\0') {
    tmp++;
  }

  for (;;) {
    tmp--;

    pr_signals_handle();

    if (tmp == base ||
        *tmp != '/') {
      break;
    }

    *tmp = '\0';
  }

  baselen = strlen(base);
  if (baselen >= PR_TUNABLE_PATH_MAX) {
    errno = ENAMETOOLONG;
    return -1;
  }

  vroot_path_set_base(base, baselen);
  session.chroot_path = pstrdup(session.pool, chroot_path);
  return 0;
}

int vroot_fsio_chdir(pr_fs_t *fs, const char *path) {
  int res;
  const char *base_path;
  size_t base_pathlen = 0;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *vpathp = NULL, *alias_path = NULL;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = chdir(path);
    return res;
  }

  tmp_pool = make_sub_pool(session.pool);
  pr_pool_tag(tmp_pool, "VRoot FSIO chdir pool");

  if (vroot_path_lookup(tmp_pool, vpath, sizeof(vpath)-1, path, 0,
      &alias_path) < 0) {
    destroy_pool(tmp_pool);
    return -1;
  }

  res = chdir(vpath);
  if (res < 0) {
    destroy_pool(tmp_pool);
    return -1;
  }

  if (alias_path != NULL) {
    vpathp = alias_path;

  } else {
    vpathp = vpath;
  }

  base_path = vroot_path_get_base(tmp_pool, &base_pathlen);
  if (strncmp(vpathp, base_path, base_pathlen) == 0) {
    pr_trace_msg(trace_channel, 19,
      "adjusting vpath '%s' to account for vroot base '%s' (%lu)", vpathp,
      base_path, (unsigned long) base_pathlen);
    vpathp += base_pathlen;
  }

  pr_trace_msg(trace_channel, 19,
    "setting current working directory to '%s'", vpathp);

  /* pr_fs_setcwd() makes a copy of the argument path, so we can safely
   * destroy our temporary pool.
   */
  pr_fs_setcwd(vpathp);

  destroy_pool(tmp_pool);
  return 0;
}

int vroot_fsio_utimes(pr_fs_t *fs, const char *utimes_path,
    struct timeval *tvs) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *path = NULL;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = utimes(utimes_path, tvs);
    return res;
  }

  tmp_pool = make_sub_pool(session.pool);
  pr_pool_tag(tmp_pool, "VRoot FSIO utimes pool");

  path = vroot_realpath(tmp_pool, utimes_path, VROOT_REALPATH_FL_ABS_PATH);
  
  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    destroy_pool(tmp_pool);
    return -1;
  }

  res = utimes(vpath, tvs);
  destroy_pool(tmp_pool);
  return res;
}

static struct dirent *vroot_dent = NULL;
static size_t vroot_dentsz = 0;

/* On most systems, dirent.d_name is an array into which we can copy the
 * name we want.
 *
 * However, on other systems (e.g. Solaris 2), dirent.d_name is an array size
 * of 1.  This approach makes use of the fact that the d_name member is the
 * last member of the struct, meaning that the actual size is variable.
 *
 * We need to Do The Right Thing(tm) in either case.
 */
static size_t vroot_dent_namesz = 0;

static array_header *vroot_dir_aliases = NULL;
static int vroot_dir_idx = -1;

static int vroot_alias_dirscan(const void *key_data, size_t key_datasz,
    const void *value_data, size_t value_datasz, void *user_data) {
  const char *alias_path = NULL, *dir_path = NULL, *real_path = NULL;
  char *ptr = NULL;
  size_t dir_pathlen;

  alias_path = key_data;
  real_path = value_data;
  dir_path = user_data;

  ptr = strrchr(alias_path, '/');
  if (ptr == NULL) {
    /* This is not likely to happen, but if it does, simply move to the
     * next item in the table.
     */
    return 0;
  }

  /* If the dir path and the real path are the same, skip this alias.
   * Otherwise we end up with an extraneous entry in the directory listing.
   */
  if (strcmp(real_path, dir_path) == 0) {
    return 0;
  }

  dir_pathlen = strlen(dir_path);

  if (strncmp(dir_path, alias_path, dir_pathlen) == 0) {
    pr_trace_msg(trace_channel, 17,
      "adding VRootAlias '%s' to list of aliases contained in '%s'",
      alias_path, dir_path);
    *((char **) push_array(vroot_dir_aliases)) = pstrdup(vroot_dir_pool,
      ptr + 1);
  }

  return 0;
}

static int vroot_dirtab_keycmp_cb(const void *key1, size_t keysz1,
    const void *key2, size_t keysz2) {
  unsigned long k1, k2;

  memcpy(&k1, key1, sizeof(k1));
  memcpy(&k2, key2, sizeof(k2));

  return (k1 == k2 ? 0 : 1);
}

static unsigned int vroot_dirtab_hash_cb(const void *key, size_t keysz) {
  unsigned long h;

  memcpy(&h, key, sizeof(h));
  return h;
}

void *vroot_fsio_opendir(pr_fs_t *fs, const char *orig_path) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *path = NULL;
  void *dirh = NULL;
  struct stat st;
  size_t pathlen = 0;
  pool *tmp_pool = NULL;
  unsigned int alias_count;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    dirh = opendir(orig_path);
    return dirh;
  }

  tmp_pool = make_sub_pool(session.pool);
  pr_pool_tag(tmp_pool, "VRoot FSIO opendir pool");

  /* If the given path ends in a slash, remove it.  The handling of
   * VRootAliases is sensitive to trailing slashes.
   */
  path = pstrdup(tmp_pool, orig_path);
  vroot_path_clean(path);

  /* If the given path ends in a slash, remove it.  The handling of
   * VRootAliases is sensitive to such things.
   */
  pathlen = strlen(path);
  if (pathlen > 1 &&
      path[pathlen-1] == '/') {
    path[pathlen-1] = '\0';
    pathlen--;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    destroy_pool(tmp_pool);
    return NULL;
  }

  /* Check if the looked-up vpath is a symlink; we may need to resolve any
   * links ourselves, rather than assuming that the system opendir(3) can
   * handle it.
   */

  res = vroot_fsio_lstat(fs, vpath, &st);
  while (res == 0 &&
         S_ISLNK(st.st_mode)) {
    char data[PR_TUNABLE_PATH_MAX + 1];

    pr_signals_handle();

    memset(data, '\0', sizeof(data));
    res = vroot_fsio_readlink(fs, vpath, data, sizeof(data)-1);
    if (res < 0) {
      break;
    }

    data[res] = '\0';

    sstrncpy(vpath, data, sizeof(vpath));
    res = vroot_fsio_lstat(fs, vpath, &st);
  }

  dirh = opendir(vpath);
  if (dirh == NULL) {
    int xerrno = errno;

    (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
      "error opening virtualized directory '%s' (from '%s'): %s", vpath, path,
      strerror(xerrno));
    destroy_pool(tmp_pool);

    errno = xerrno;
    return NULL;
  }

  alias_count = vroot_alias_count();
  if (alias_count > 0) {
    unsigned long *cache_dirh = NULL;

    if (vroot_dirtab == NULL) {
      vroot_dir_pool = make_sub_pool(session.pool);
      pr_pool_tag(vroot_dir_pool, "VRoot Directory Pool");

      vroot_dirtab = pr_table_alloc(vroot_dir_pool, 0);

      /* Since this table will use DIR pointers as keys, we want to override
       * the default hashing and key comparison functions used.
       */
    
      pr_table_ctl(vroot_dirtab, PR_TABLE_CTL_SET_KEY_HASH,
        vroot_dirtab_hash_cb);
      pr_table_ctl(vroot_dirtab, PR_TABLE_CTL_SET_KEY_CMP,
        vroot_dirtab_keycmp_cb);
    }

    cache_dirh = palloc(vroot_dir_pool, sizeof(unsigned long));
    *cache_dirh = (unsigned long) dirh;

    if (pr_table_kadd(vroot_dirtab, cache_dirh, sizeof(unsigned long),
        pstrdup(vroot_dir_pool, vpath), strlen(vpath) + 1) < 0) {
      (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
        "error stashing path '%s' (key %p) in directory table: %s", vpath,
        dirh, strerror(errno));

    } else {
      vroot_dir_aliases = make_array(vroot_dir_pool, 0, sizeof(char *));

      res = vroot_alias_do(vroot_alias_dirscan, vpath);
      if (res < 0) {
        (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
          "error doing dirscan on aliases table: %s", strerror(errno));

      } else {
        register unsigned int i;

        (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
          "found %d %s in directory '%s'", vroot_dir_aliases->nelts,
          vroot_dir_aliases->nelts != 1 ? "VRootAliases" : "VRootAlias",
          vpath);
        vroot_dir_idx = 0;

        for (i = 0; i < vroot_dir_aliases->nelts; i++) {
          char **elts = vroot_dir_aliases->elts;

          (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
            "'%s' aliases: [%u] %s", vpath, i, elts[i]);
        }
      }
    }
  }

  destroy_pool(tmp_pool);
  return dirh;
}

struct dirent *vroot_fsio_readdir(pr_fs_t *fs, void *dirh) {
  struct dirent *dent = NULL;

next_dent:
  dent = readdir((DIR *) dirh);

  if (vroot_dir_aliases != NULL) {
    char **elts;

    elts = vroot_dir_aliases->elts;

    if (dent != NULL) {
      register unsigned int i;

      /* If this dent has the same name as an alias, the alias wins.
       * This is similar to a mounted filesystem, which hides any directories
       * underneath the mount point for the duration of the mount.
       */

      /* Yes, this is a linear scan; it assumes that the number of configured
       * aliases for a site will be relatively few.  Should this assumption
       * not be borne out by reality, then we should switch to using a
       * table, not an array_header, for storing the aliased paths.
       */

      for (i = 0; i < vroot_dir_aliases->nelts; i++) {
        if (strcmp(dent->d_name, elts[i]) == 0) {
          (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
            "skipping directory entry '%s', as it is aliased", dent->d_name);
          goto next_dent;
        }
      }

    } else {
      if (vroot_dir_idx < 0 ||
          vroot_dir_idx >= vroot_dir_aliases->nelts) {
        return NULL;
      }

      memset(vroot_dent, 0, vroot_dentsz);

      if (vroot_dent_namesz == 0) {
        sstrncpy(vroot_dent->d_name, elts[vroot_dir_idx++],
          sizeof(vroot_dent->d_name));

      } else {
        sstrncpy(vroot_dent->d_name, elts[vroot_dir_idx++],
          vroot_dent_namesz);
      }

      return vroot_dent;
    }
  }

  return dent;
}

int vroot_fsio_closedir(pr_fs_t *fs, void *dirh) {
  int res;

  res = closedir((DIR *) dirh);

  if (vroot_dirtab != NULL) {
    unsigned long lookup_dirh;
    int count;

    lookup_dirh = (unsigned long) dirh;
    (void) pr_table_kremove(vroot_dirtab, &lookup_dirh, sizeof(unsigned long),
      NULL);

    /* If the dirtab table is empty, destroy the table. */
    count = pr_table_count(vroot_dirtab);
    if (count == 0) {
      pr_table_empty(vroot_dirtab);
      destroy_pool(vroot_dir_pool);
      vroot_dir_pool = NULL;
      vroot_dirtab = NULL;
      vroot_dir_aliases = NULL;
      vroot_dir_idx = -1;
    }
  }

  return res;
}

int vroot_fsio_mkdir(pr_fs_t *fs, const char *path, mode_t mode) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = mkdir(path, mode);
    return res;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = mkdir(vpath, mode);
  return res;
}

int vroot_fsio_rmdir(pr_fs_t *fs, const char *path) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      vroot_path_have_base() == FALSE) {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = rmdir(path);
    return res;
  }

  /* Do not allow deleting of aliased files/directories; the aliases may only
   * exist for this user/group.
   */
  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path,
      VROOT_LOOKUP_FL_NO_ALIAS, NULL) < 0) {
    return -1;
  }

  if (vroot_alias_exists(vpath) == TRUE) {
    (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
      "denying delete of '%s' because it is a VRootAlias", vpath);
    errno = EACCES;
    return -1;
  }

  if (vroot_path_lookup(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = rmdir(vpath);
  return res;
}

int vroot_fsio_init(pool *p) {
  struct dirent dent;

  if (p == NULL) {
    errno = EINVAL;
    return -1;
  }

  /* Allocate the memory for the static struct dirent that we use, including
   * determining the necessary sizes.
   */
  vroot_dentsz = sizeof(dent);
  if (sizeof(dent.d_name) == 1) {
    /* Allocate extra space for the dent path name. */
    vroot_dent_namesz = PR_TUNABLE_PATH_MAX;
  }

  vroot_dentsz += vroot_dent_namesz;
  vroot_dent = palloc(p, vroot_dentsz);

  return 0;
}

int vroot_fsio_free(void) {
  return 0;
}
