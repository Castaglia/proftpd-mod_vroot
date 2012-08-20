/*
 * ProFTPD: mod_vroot -- a module implementing a virtual chroot capability
 *                       via the FSIO API
 *
 * Copyright (c) 2002-2011 TJ Saunders
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
 *
 * This is mod_vroot, contrib software for proftpd 1.2 and above.
 * For more information contact TJ Saunders <tj@castaglia.org>.
 *
 * $Id: mod_vroot.c,v 1.24 2011/01/11 02:41:10 tj Exp tj $
 */

#include "conf.h"
#include "privs.h"

#define MOD_VROOT_VERSION 	"mod_vroot/0.9.2"

/* Make sure the version of proftpd is as necessary. */
#if PROFTPD_VERSION_NUMBER < 0x0001030201
# error "ProFTPD 1.3.2rc1 or later required"
#endif

static const char *vroot_log = NULL;
static int vroot_logfd = -1;

static char vroot_cwd[PR_TUNABLE_PATH_MAX + 1];
static char vroot_base[PR_TUNABLE_PATH_MAX + 1];
static size_t vroot_baselen = 0;
static unsigned char vroot_engine = FALSE;

static pool *vroot_alias_pool = NULL;
static pr_table_t *vroot_aliastab = NULL;

static pool *vroot_dir_pool = NULL;
static pr_table_t *vroot_dirtab = NULL;

static unsigned int vroot_opts = 0;
#define	VROOT_OPT_ALLOW_SYMLINKS	0x0001

/* vroot_lookup_path() flags */
#define VROOT_LOOKUP_FL_NO_ALIASES	0x0001

static const char *trace_channel = "vroot";

static int vroot_is_alias(const char *);

/* Support routines note: some of these support functions are borrowed from
 * pure-ftpd.
 */

static void strmove(register char *dst, register const char *src) {
  if (!dst || !src)
    return;

  while (*src != 0) {
    *dst++ = *src++;
  }

  *dst = 0;
}

static void vroot_clean_path(char *path) {
  char *p;

  if (path == NULL || *path == 0)
      return;

  while ((p = strstr(path, "//")) != NULL)
    strmove(p, p + 1);

  while ((p = strstr(path, "/./")) != NULL)
    strmove(p, p + 2);

  while (strncmp(path, "../", 3) == 0)
    path += 3;

  p = strstr(path, "/../");
  if (p != NULL) {
    if (p == path) {
      while (strncmp(path, "/../", 4) == 0)
        strmove(path, path + 3);

      p = strstr(path, "/../");
    }

    while (p != NULL) {
      char *next_elem = p + 4;

      if (p != path && *p == '/') 
        p--;

      while (p != path && *p != '/')
        p--;

      if (*p == '/')
        p++;

      strmove(p, next_elem);
      p = strstr(path, "/../");
    }
  }

  p = path;

  if (*p == '.') {
    p++;

    if (*p == '\0')
      return;

    if (*p == '/') {
      while (*p == '/') 
        p++;

      strmove(path, p);
    }
  }

  if (*p == '\0')
    return;

  p = path + strlen(path) - 1;
  if (*p != '.' || p == path)
    return;

  p--;
  if (*p == '/' || p == path) {
    p[1] = '\0';
    return;
  }

  if (*p != '.' || p == path)
    return;

  p--;
  if (*p != '/')
    return;

  *p = '\0';
  p = strrchr(path, '/');
  if (p == NULL) {
    *path = '/';
    path[1] = '\0';
    return;
  }

  p[1] = '\0';
}

static int vroot_lookup_path(pool *p, char *path, size_t pathlen,
    const char *dir, int flags, char **alias_path) {
  char buf[PR_TUNABLE_PATH_MAX + 1], *bufp = NULL;

  memset(buf, '\0', sizeof(buf));
  memset(path, '\0', pathlen);

  if (strcmp(dir, ".") != 0) {
    sstrncpy(buf, dir, sizeof(buf));

  } else {
    sstrncpy(buf, pr_fs_getcwd(), sizeof(buf));
  }

  vroot_clean_path(buf);

  bufp = buf;

  if (strncmp(bufp, vroot_base, vroot_baselen) == 0) {
    bufp += vroot_baselen;
  }

loop:
  pr_signals_handle();

  if (bufp[0] == '.' &&
      bufp[1] == '.' &&
      (bufp[2] == '\0' ||
       bufp[2] == '/')) {
    char *tmp = NULL;

    tmp = strrchr(path, '/');
    if (tmp != NULL) {
      *tmp = '\0';

    } else {
      *path = '\0';
    }

    if (strncmp(path, vroot_base, vroot_baselen) == 0 ||
         path[vroot_baselen] != '/') {
      snprintf(path, pathlen, "%s/", vroot_base);
    }

    if (bufp[0] == '.' &&
        bufp[1] == '.' &&
        bufp[2] == '/') {
      bufp += 3;
      goto loop;
    }

  } else if (*bufp == '/') {
    snprintf(path, pathlen, "%s/", vroot_base);

    bufp += 1;
    goto loop;

  } else if (*bufp != '\0') {
    size_t buflen, tmplen;
    char *ptr;

    ptr = strstr(bufp, "..");
    if (ptr != NULL) {
      size_t ptrlen;

      /* We need to watch for path components/filenames which legitimately
       * contain two or more periods in addition to other characters.
       */

      ptrlen = strlen(ptr);
      if (ptrlen >= 3) {

        /* If this ".." occurrence is the start of the buffer AND the next
         * character after the ".." is a slash, then deny it.
         */
        if (ptr == bufp &&
            ptr[2] == '/') {
          errno = EPERM;
          return -1;
        }

        /* If this ".." occurrence is NOT the start of the buffer AND the
         * characters preceeding and following the ".." are slashes, then
         * deny it.
         */
        if (ptr != bufp &&
            ptr[-1] == '/' &&
            ptr[2] == '/') {
          errno = EPERM;
          return -1;
        }
      }
    }

    buflen = strlen(bufp) + 1;
    tmplen = strlen(path);

    if (tmplen + buflen >= pathlen) {
      errno = ENAMETOOLONG;
      return -1;
    }

    path[tmplen] = '/';
    memcpy(path + tmplen + 1, bufp, buflen);
  }

  /* Clean any unnecessary characters added by the above processing. */
  vroot_clean_path(path);

  if (!(flags & VROOT_LOOKUP_FL_NO_ALIASES)) {
    /* Check to see if this path is an alias; if so, return the real path. */
    if (vroot_aliastab != NULL) {
      char *start_ptr = NULL, *end_ptr = NULL, *src_path = NULL;

      start_ptr = path;
      while (start_ptr != NULL) {
        char *ptr;

        pr_signals_handle();

        pr_trace_msg(trace_channel, 15, "checking for alias for '%s'",
          start_ptr);

        src_path = pr_table_get(vroot_aliastab, start_ptr, NULL);
        if (src_path != NULL) {
          pr_trace_msg(trace_channel, 15, "found '%s' for alias '%s'", src_path,
            start_ptr);

          /* If the caller provided a pointer for wanting to know the full
           * alias path (not the true path), then fill that pointer.
           */
          if (alias_path != NULL) {
            if (end_ptr != NULL) {
              *alias_path = pdircat(p, start_ptr, end_ptr + 1, NULL);

            } else {
              *alias_path = pstrdup(p, start_ptr);
            }

            pr_trace_msg(trace_channel, 19, "using alias path '%s' for '%s'",
              *alias_path, start_ptr);
          }

          sstrncpy(path, src_path, pathlen);

          if (end_ptr != NULL) {
            sstrcat(path, "/", pathlen);
            sstrcat(path, end_ptr + 1, pathlen);
          }

          break;
        }

        ptr = strrchr(start_ptr, '/');

        if (end_ptr != NULL) {
          *end_ptr = '/';
        }

        if (ptr == NULL) {
          break;
        }

        /* If this is the start of the path, we're done. */
        if (ptr == start_ptr) {
          break;
        }

        end_ptr = ptr;
        *end_ptr = '\0';
      }
    }
  }

  return 0;
}

static int vroot_is_alias(const char *path) {
  if (pr_table_get(vroot_aliastab, path, 0) != NULL) {
    return 0;
  }

  errno = ENOENT;
  return -1;
}

static int handle_vroot_alias(void) {
  config_rec *c;
  pool *tmp_pool;

  /* Handle any VRootAlias settings. */

  tmp_pool = make_sub_pool(session.pool);

  c = find_config(main_server->conf, CONF_PARAM, "VRootAlias", FALSE);
  while (c) {
    char src_path[PR_TUNABLE_PATH_MAX+1], dst_path[PR_TUNABLE_PATH_MAX+1],
      vpath[PR_TUNABLE_PATH_MAX+1], *ptr;

    pr_signals_handle();

    /* XXX Note that by using vroot_lookup_path(), we assume a POST_CMD
     * invocation.  Looks like VRootAlias might end up being incompatible
     * with VRootServerRoot.
     */

    memset(src_path, '\0', sizeof(src_path));
    sstrncpy(src_path, c->argv[0], sizeof(src_path)-1);
    vroot_clean_path(src_path);

    ptr = dir_best_path(tmp_pool, c->argv[1]);
    vroot_lookup_path(NULL, dst_path, sizeof(dst_path)-1, ptr,
      VROOT_LOOKUP_FL_NO_ALIASES, NULL);

    /* If the vroot of the source path matches the vroot of the destination
     * path, then we have a badly configured VRootAlias, one which is trying
     * to override itself.  Need to check for, and reject, such cases.
     */
    vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, src_path,
      VROOT_LOOKUP_FL_NO_ALIASES, NULL);
    if (strcmp(dst_path, vpath) == 0) {
      (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
        "alias '%s' maps to its real path '%s' inside the vroot, "
        "ignoring bad alias", dst_path, src_path);

      c = find_config_next(c, c->next, CONF_PARAM, "VRootAlias", FALSE);
      continue;
    }

    if (vroot_alias_pool == NULL) {
      vroot_alias_pool = make_sub_pool(session.pool);
      pr_pool_tag(vroot_alias_pool, "VRoot Alias Pool");

      vroot_aliastab = pr_table_alloc(vroot_alias_pool, 0);
    }

    if (pr_table_add(vroot_aliastab, pstrdup(vroot_alias_pool, dst_path),
        pstrdup(vroot_alias_pool, src_path), 0) < 0) {

      /* Make a slightly better log message when there is an alias collision. */
      if (errno == EEXIST) {
        (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
          "VRootAlias already configured for '%s', ignoring bad alias",
          (char *) c->argv[1]);

      } else {
        (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
          "error stashing VRootAlias '%s': %s", dst_path, strerror(errno));
      }

    } else {
      (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
        "aliased '%s' to real path '%s'", dst_path, src_path);
    }

    c = find_config_next(c, c->next, CONF_PARAM, "VRootAlias", FALSE);
  }

  destroy_pool(tmp_pool);
  return 0;
}

/* FS callbacks
 */

static int vroot_stat(pr_fs_t *fs, const char *orig_path, struct stat *st) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *path;
  size_t path_len = 0;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    return stat(orig_path, st);
  }

  tmp_pool = make_sub_pool(session.pool);

  path = pstrdup(tmp_pool, orig_path);
  vroot_clean_path(path);

  /* If the given path ends in a slash, remove it.  The handling of
   * VRootAliases is sensitive to such things.
   */
  path_len = strlen(path);
  if (path_len > 1 &&
      path[path_len-1] == '/') {
    path[path_len-1] = '\0';
    path_len--;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    destroy_pool(tmp_pool);
    return -1;
  }

  res = stat(vpath, st);
  destroy_pool(tmp_pool);
  return res;
}

static int vroot_lstat(pr_fs_t *fs, const char *orig_path, struct stat *st) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *path;
  size_t path_len = 0;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    return lstat(orig_path, st);
  }

  tmp_pool = make_sub_pool(session.pool);

  path = pstrdup(tmp_pool, orig_path);
  vroot_clean_path(path);

  /* If the given path ends in a slash, remove it.  The handling of
   * VRootAliases is sensitive to such things.
   */
  path_len = strlen(path);
  if (path_len > 1 &&
      path[path_len-1] == '/') {
    path[path_len-1] = '\0';
    path_len--;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    destroy_pool(tmp_pool);
    return -1;
  }

  if ((vroot_opts & VROOT_OPT_ALLOW_SYMLINKS) ||
      vroot_is_alias(path) == 0) {
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

static int vroot_rename(pr_fs_t *fs, const char *rnfm, const char *rnto) {
  int res;
  char vpath1[PR_TUNABLE_PATH_MAX + 1], vpath2[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = rename(rnfm, rnto);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath1, sizeof(vpath1)-1, rnfm, 0, NULL) < 0) {
    return -1;
  }

  if (vroot_lookup_path(NULL, vpath2, sizeof(vpath2)-1, rnto, 0, NULL) < 0) {
    return -1;
  }

  res = rename(vpath1, vpath2);
  return res;
}

static int vroot_unlink(pr_fs_t *fs, const char *path) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = unlink(path);
    return res;
  }

  /* Do not allow deleting of aliased files/directories; the aliases may only
   * exist for this user/group.
   */
  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path,
      VROOT_LOOKUP_FL_NO_ALIASES, NULL) < 0) {
    return -1;
  }

  if (vroot_is_alias(vpath) == 0) {
    (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
      "denying delete of '%s' because it is a VRootAlias", vpath);
    errno = EACCES;
    return -1;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    return -1;
  }

  res = unlink(vpath);
  return res;
}

static int vroot_open(pr_fh_t *fh, const char *path, int flags) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = open(path, flags, PR_OPEN_MODE);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0)
    return -1;

  res = open(vpath, flags, PR_OPEN_MODE);
  return res;
}

static int vroot_creat(pr_fh_t *fh, const char *path, mode_t mode) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = creat(path, mode);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0)
    return -1;

  res = creat(vpath, mode);
  return res;
}

static int vroot_link(pr_fs_t *fs, const char *path1, const char *path2) {
  int res;
  char vpath1[PR_TUNABLE_PATH_MAX + 1], vpath2[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = link(path1, path2);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath1, sizeof(vpath1)-1, path1, 0, NULL) < 0) {
    return -1;
  }

  if (vroot_lookup_path(NULL, vpath2, sizeof(vpath2)-1, path2, 0, NULL) < 0) {
    return -1;
  }

  res = link(vpath1, vpath2);
  return res;
}

static int vroot_symlink(pr_fs_t *fs, const char *path1, const char *path2) {
  int res;
  char vpath1[PR_TUNABLE_PATH_MAX + 1], vpath2[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = symlink(path1, path2);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath1, sizeof(vpath1)-1, path1, 0, NULL) < 0) {
    return -1;
  }

  if (vroot_lookup_path(NULL, vpath2, sizeof(vpath2)-1, path2, 0, NULL) < 0) {
    return -1;
  }

  res = symlink(vpath1, vpath2);
  return res;
}

static int vroot_readlink(pr_fs_t *fs, const char *path, char *buf,
    size_t max) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    return readlink(path, buf, max);
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0)
    return -1;

  res = readlink(vpath, buf, max);
  return res;
}

static int vroot_truncate(pr_fs_t *fs, const char *path, off_t length) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = truncate(path, length);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0)
    return -1;

  res = truncate(vpath, length);
  return res;
}

static int vroot_chmod(pr_fs_t *fs, const char *path, mode_t mode) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = chmod(path, mode);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0)
    return -1;

  res = chmod(vpath, mode);
  return res;
}

static int vroot_chown(pr_fs_t *fs, const char *path, uid_t uid, gid_t gid) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = chown(path, uid, gid);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0)
    return -1;

  res = chown(vpath, uid, gid);
  return res;
}

static int vroot_chroot(pr_fs_t *fs, const char *path) {
  char *chroot_path = "/", *tmp = NULL;
  config_rec *c;

  if (!path ||
      *path == '\0') {
    errno = EINVAL;
    return -1;
  }

  memset(vroot_base, '\0', sizeof(vroot_base));

  if (path[0] == '/' &&
      path[1] == '\0') {
    /* chrooting to '/', nothing needs to be done. */
    return 0;
  }

  c = find_config(main_server->conf, CONF_PARAM, "VRootServerRoot", FALSE);
  if (c) {
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

      pr_fs_clean_path(path + strlen(server_root), vroot_base,
        sizeof(vroot_base));
      chroot_path = server_root;

    } else {
      (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
        "chroot path '%s' is not within VRootServerRoot '%s', "
        "not chrooting to VRootServerRoot", path, server_root);
      pr_fs_clean_path(path, vroot_base, sizeof(vroot_base));
    }

  } else {
    pr_fs_clean_path(path, vroot_base, sizeof(vroot_base));
  }

  tmp = vroot_base;

  /* Advance to the end of the path. */
  while (*tmp != '\0') {
    tmp++;
  }

  for (;;) {
    tmp--;

    if (tmp == vroot_base ||
        *tmp != '/') {
      break;
    }

    *tmp = '\0';
  }

  vroot_baselen = strlen(vroot_base);
  if (vroot_baselen >= sizeof(vroot_cwd)) {
    errno = ENAMETOOLONG;
    return -1;
  }

  session.chroot_path = pstrdup(session.pool, chroot_path);
  return 0;
}

static int vroot_chdir(pr_fs_t *fs, const char *path) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *vpathp = NULL, *alias_path = NULL;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = chdir(path);
    return res;
  }

  tmp_pool = make_sub_pool(session.pool);
  if (vroot_lookup_path(tmp_pool, vpath, sizeof(vpath)-1, path, 0,
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

  if (strncmp(vpathp, vroot_base, vroot_baselen) == 0) {
    pr_trace_msg(trace_channel, 19,
      "adjusting vpath '%s' to account for vroot base '%s' (%lu)", vpathp,
      vroot_base, (unsigned long) vroot_baselen);
    vpathp += vroot_baselen;
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

static struct dirent vroot_dent;
static array_header *vroot_dir_aliases = NULL;
static int vroot_dir_idx = -1;

static int vroot_alias_dirscan(const void *key_data, size_t key_datasz,
    void *value_data, size_t value_datasz, void *user_data) {
  const char *alias_path, *dir_path, *real_path;
  char *ptr;

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

  /* If the length from the start of the alias path to the last occurring
   * slash is longer than the length of the directory path, then this
   * alias obviously does not occur in this directory.
   */
  if ((ptr - alias_path) > strlen(dir_path)) {
    return 0;
  }

  if (strncmp(dir_path, alias_path, (ptr - alias_path)) == 0) {
    *((char **) push_array(vroot_dir_aliases)) = pstrdup(vroot_dir_pool,
      ptr + 1);
  }

  return 0;
}

static int vroot_dirtab_keycmp_cb(const void *key1, size_t keysz1,
    const void *key2, size_t keysz2) {
  unsigned int k1, k2;

  memcpy(&k1, key1, sizeof(k1));
  memcpy(&k2, key2, sizeof(k2));

  return (k1 == k2 ? 0 : 1);
}

static unsigned int vroot_dirtab_hash_cb(const void *key, size_t keysz) {
  unsigned int h;

  memcpy(&h, key, sizeof(h));
  return h;
}

static void *vroot_opendir(pr_fs_t *fs, const char *orig_path) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1], *path;
  void *dirh;
  struct stat st;
  size_t path_len = 0;
  pool *tmp_pool = NULL;

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    dirh = opendir(orig_path);
    return dirh;
  }

  tmp_pool = make_sub_pool(session.pool);

  /* If the given path ends in a slash, remove it.  The handling of
   * VRootAliases is sensitive to trailing slashes.
   */
  path = pstrdup(tmp_pool, orig_path);
  vroot_clean_path(path);

  /* If the given path ends in a slash, remove it.  The handling of
   * VRootAliases is sensitive to such things.
   */
  path_len = strlen(path);
  if (path_len > 1 &&
      path[path_len-1] == '/') {
    path[path_len-1] = '\0';
    path_len--;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0) {
    destroy_pool(tmp_pool);
    return NULL;
  }

  /* Check if the looked-up vpath is a symlink; we may need to resolve any
   * links ourselves, rather than assuming that the system opendir(3) can
   * handle it.
   */

  res = vroot_lstat(fs, vpath, &st);
  while (res == 0 &&
         S_ISLNK(st.st_mode)) {
    char data[PR_TUNABLE_PATH_MAX + 1];

    pr_signals_handle();

    memset(data, '\0', sizeof(data));
    res = vroot_readlink(fs, vpath, data, sizeof(data)-1);
    if (res < 0)
      break;

    data[res] = '\0';

    sstrncpy(vpath, data, sizeof(vpath));
    res = vroot_lstat(fs, vpath, &st);
  }

  dirh = opendir(vpath);
  if (dirh == NULL) {
    (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
      "error opening virtualized directory '%s' (from '%s'): %s", vpath, path,
      strerror(errno));
    destroy_pool(tmp_pool);
    return NULL;
  }

  if (vroot_aliastab != NULL) {
    unsigned int *cache_dirh;

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

    cache_dirh = palloc(vroot_dir_pool, sizeof(unsigned int));
    *cache_dirh = (unsigned int) dirh;

    if (pr_table_kadd(vroot_dirtab, cache_dirh, sizeof(unsigned int),
        pstrdup(vroot_dir_pool, vpath), strlen(vpath) + 1) < 0) {
      (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
        "error stashing path '%s' (key %p) in directory table: %s", vpath,
        dirh, strerror(errno));

    } else {
      vroot_dir_aliases = make_array(vroot_dir_pool, 0, sizeof(char *));

      res = pr_table_do(vroot_aliastab, vroot_alias_dirscan, vpath,
        PR_TABLE_DO_FL_ALL);
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

static struct dirent *vroot_readdir(pr_fs_t *fs, void *dirh) {
  struct dirent *dent;

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

      memset(&vroot_dent, 0, sizeof(vroot_dent));
      sstrncpy(vroot_dent.d_name, elts[vroot_dir_idx++],
        sizeof(vroot_dent.d_name));

      return &vroot_dent;
    }
  }

  return dent;
}

static int vroot_closedir(pr_fs_t *fs, void *dirh) {
  int res;

  res = closedir((DIR *) dirh);

  if (vroot_dirtab != NULL) {
    unsigned int lookup_dirh;
    int count;

    lookup_dirh = (unsigned int) dirh;
    (void) pr_table_kremove(vroot_dirtab, &lookup_dirh, sizeof(unsigned int),
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

static int vroot_mkdir(pr_fs_t *fs, const char *path, mode_t mode) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = mkdir(path, mode);
    return res;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0)
    return -1;

  res = mkdir(vpath, mode);
  return res;
}

static int vroot_rmdir(pr_fs_t *fs, const char *path) {
  int res;
  char vpath[PR_TUNABLE_PATH_MAX + 1];

  if (session.curr_phase == LOG_CMD ||
      session.curr_phase == LOG_CMD_ERR ||
      (session.sf_flags & SF_ABORT) ||
      *vroot_base == '\0') {
    /* NOTE: once stackable FS modules are supported, have this fall through
     * to the next module in the stack.
     */
    res = rmdir(path);
    return res;
  }

  /* Do not allow deleting of aliased files/directories; the aliases may only
   * exist for this user/group.
   */
  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path,
      VROOT_LOOKUP_FL_NO_ALIASES, NULL) < 0) {
    return -1;
  }

  if (vroot_is_alias(vpath) == 0) {
    (void) pr_log_writefile(vroot_logfd, MOD_VROOT_VERSION,
      "denying delete of '%s' because it is a VRootAlias", vpath);
    errno = EACCES;
    return -1;
  }

  if (vroot_lookup_path(NULL, vpath, sizeof(vpath)-1, path, 0, NULL) < 0)
    return -1;

  res = rmdir(vpath);
  return res;
}

/* Configuration handlers
 */

/* usage: VRootAlias src-path dst-path */
MODRET set_vrootalias(cmd_rec *cmd) {
  config_rec *c;

  CHECK_ARGS(cmd, 2);
  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  if (pr_fs_valid_path(cmd->argv[1]) < 0) {
    CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "source path '", cmd->argv[1],
      "' is not an absolute path", NULL));
  }

  c = add_config_param_str(cmd->argv[0], 2, cmd->argv[1], cmd->argv[2]);

  /* Set this flag in order to allow mod_ifsession to work properly with
   * multiple VRootAlias directives.
   */
  c->flags |= CF_MERGEDOWN_MULTI;

  return PR_HANDLED(cmd);
}

/* usage: VRootEngine on|off */
MODRET set_vrootengine(cmd_rec *cmd) {
  int bool = -1;
  config_rec *c = NULL;

  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  bool = get_boolean(cmd, 1);
  if (bool == -1)
    CONF_ERROR(cmd, "expected Boolean parameter");

  c = add_config_param(cmd->argv[0], 1, NULL);
  c->argv[0] = pcalloc(c->pool, sizeof(unsigned char));
  *((unsigned char *) c->argv[0]) = bool;

  return PR_HANDLED(cmd);
}

/* usage: VRootLog path|"none" */
MODRET set_vrootlog(cmd_rec *cmd) {
  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  if (pr_fs_valid_path(cmd->argv[1]) < 0)
    CONF_ERROR(cmd, "must be an absolute path");

  (void) add_config_param_str(cmd->argv[0], 1, cmd->argv[1]);
  return PR_HANDLED(cmd);
}

/* usage: VRootOptions opt1 opt2 ... optN */
MODRET set_vrootoptions(cmd_rec *cmd) {
  config_rec *c = NULL;
  register unsigned int i;
  unsigned int opts = 0U;

  if (cmd->argc-1 == 0)
    CONF_ERROR(cmd, "wrong number of parameters");

  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  c = add_config_param(cmd->argv[0], 1, NULL);
  for (i = 1; i < cmd->argc; i++) {
    if (strcmp(cmd->argv[i], "allowSymlinks") == 0) {
      opts |= VROOT_OPT_ALLOW_SYMLINKS;

    } else {
      CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, ": unknown VRootOption: '",
        cmd->argv[i], "'", NULL));
    }
  }

  c->argv[0] = pcalloc(c->pool, sizeof(unsigned int));
  *((unsigned int *) c->argv[0]) = opts;

  return PR_HANDLED(cmd);
}

/* usage: VRootServerRoot path */
MODRET set_vrootserverroot(cmd_rec *cmd) {
  struct stat st;
  config_rec *c;
  size_t pathlen;

  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  if (pr_fs_valid_path(cmd->argv[1]) < 0)
    CONF_ERROR(cmd, "must be an absolute path");

  if (stat(cmd->argv[1], &st) < 0) {
    CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "error checking '", cmd->argv[1],
      "': ", strerror(errno), NULL));
  }

  if (!S_ISDIR(st.st_mode)) {
    CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "'", cmd->argv[1],
      "' is not a directory", NULL));
  }

  c = add_config_param(cmd->argv[0], 1, NULL);

  /* Make sure the configured path has a trailing path separater ('/').
   * This is important.
   */
 
  pathlen = strlen(cmd->argv[1]);
  if (cmd->argv[1][pathlen - 1] != '/') {
    c->argv[0] = pstrcat(c->pool, cmd->argv[1], "/", NULL);

  } else {
    c->argv[0] = pstrdup(c->pool, cmd->argv[1]);
  }

  return PR_HANDLED(cmd);
}

/* Command handlers
 */

MODRET vroot_pre_pass(cmd_rec *cmd) {
  pr_fs_t *fs = NULL;
  unsigned char *use_vroot = NULL;

  use_vroot = get_param_ptr(main_server->conf, "VRootEngine", FALSE); 

  if (!use_vroot ||
      *use_vroot == FALSE) {
    vroot_engine = FALSE;
    return PR_DECLINED(cmd);
  }

  /* First, make sure that we have not already registered our FS object. */
  fs = pr_unmount_fs("/", "vroot");
  if (fs) {
    destroy_pool(fs->fs_pool);
  }

  fs = pr_register_fs(main_server->pool, "vroot", "/");
  if (fs == NULL) {
    pr_log_debug(DEBUG3, MOD_VROOT_VERSION ": error registering fs: %s",
      strerror(errno));
    return PR_DECLINED(cmd);
  }

  pr_log_debug(DEBUG5, MOD_VROOT_VERSION ": vroot registered");

  /* Add the module's custom FS callbacks here. This module does not
   * provide callbacks for the following (as they are unnecessary):
   * close(), read(), write(), and lseek().
   */
  fs->stat = vroot_stat;
  fs->lstat = vroot_lstat;
  fs->rename = vroot_rename;
  fs->unlink = vroot_unlink;
  fs->open = vroot_open;
  fs->creat = vroot_creat;
  fs->link = vroot_link;
  fs->readlink = vroot_readlink;
  fs->symlink = vroot_symlink;
  fs->truncate = vroot_truncate;
  fs->chmod = vroot_chmod;
  fs->chown = vroot_chown;
  fs->chdir = vroot_chdir;
  fs->chroot = vroot_chroot;
  fs->opendir = vroot_opendir;
  fs->readdir = vroot_readdir;
  fs->closedir = vroot_closedir;
  fs->mkdir = vroot_mkdir;
  fs->rmdir = vroot_rmdir;

  vroot_engine = TRUE;
  return PR_DECLINED(cmd);
}

MODRET vroot_post_pass(cmd_rec *cmd) {
  if (vroot_engine) {

    /* If not chrooted, unregister vroot. */
    if (!session.chroot_path) {
      if (pr_unregister_fs("/") < 0) {
        pr_log_debug(DEBUG2, MOD_VROOT_VERSION
          ": error unregistering vroot: %s", strerror(errno));

      } else {
        pr_log_debug(DEBUG5, MOD_VROOT_VERSION ": vroot unregistered");
        pr_fs_setcwd(pr_fs_getvwd());
        pr_fs_clear_cache();
      }

    } else {
      config_rec *c;

      /* Otherwise, lookup and process any VRootOptions. */
      c = find_config(main_server->conf, CONF_PARAM, "VRootOptions", FALSE);
      if (c) {
        vroot_opts = *((unsigned int *) c->argv[0]);
      }

      /* XXX This needs to be in the PRE_CMD PASS handler, as when
       * VRootServer is used, so that a real chroot(2) occurs.
       */
      handle_vroot_alias();
    }
  }

  return PR_DECLINED(cmd);
}

MODRET vroot_post_pass_err(cmd_rec *cmd) {
  if (vroot_engine) {

    /* If not chrooted, unregister vroot. */
    if (session.chroot_path == NULL) {
      if (pr_unregister_fs("/") < 0) {
        pr_log_debug(DEBUG2, MOD_VROOT_VERSION
          ": error unregistering vroot: %s", strerror(errno));

      } else {
        pr_log_debug(DEBUG5, MOD_VROOT_VERSION ": vroot unregistered");
      }
    }

    if (vroot_aliastab) {
      pr_table_empty(vroot_aliastab);
      destroy_pool(vroot_alias_pool);
      vroot_alias_pool = NULL;
      vroot_aliastab = NULL;
    }
  }

  return PR_DECLINED(cmd);
}

/* Initialization routines
 */

static int vroot_sess_init(void) {
  config_rec *c;

  c = find_config(main_server->conf, CONF_PARAM, "VRootLog", FALSE);
  if (c) {
    vroot_log = c->argv[0];
  }

  if (vroot_log &&
      strcasecmp(vroot_log, "none") != 0) {
    int res;

    PRIVS_ROOT
    res = pr_log_openfile(vroot_log, &vroot_logfd, 0660);
    PRIVS_RELINQUISH

    switch (res) {
      case 0:
        break;

      case -1:
        pr_log_debug(DEBUG1, MOD_VROOT_VERSION
          ": unable to open VRootLog '%s': %s", vroot_log, strerror(errno));
        break;

      case PR_LOG_SYMLINK:
        pr_log_debug(DEBUG1, MOD_VROOT_VERSION
          ": unable to open VRootLog '%s': %s", vroot_log, "is a symlink");
        break;

      case PR_LOG_WRITABLE_DIR:
        pr_log_debug(DEBUG1, MOD_VROOT_VERSION
          ": unable to open VRootLog '%s': %s", vroot_log,
          "parent directory is world-writable");
        break;
    }
  }

  return 0;
}

/* Module API tables
 */

static conftable vroot_conftab[] = {
  { "VRootAlias",	set_vrootalias,		NULL },
  { "VRootEngine",	set_vrootengine,	NULL },
  { "VRootLog",		set_vrootlog,		NULL },
  { "VRootOptions",	set_vrootoptions,	NULL },
  { "VRootServerRoot",	set_vrootserverroot,	NULL },
  { NULL }
};

static cmdtable vroot_cmdtab[] = {
  { PRE_CMD,		C_PASS,	G_NONE,	vroot_pre_pass, FALSE, FALSE },
  { POST_CMD,		C_PASS,	G_NONE,	vroot_post_pass, FALSE, FALSE },
  { POST_CMD_ERR,	C_PASS,	G_NONE,	vroot_post_pass_err, FALSE, FALSE },
  { 0, NULL }
};

module vroot_module = {
  NULL, NULL,

  /* Module API version 2.0 */
  0x20,

  /* Module name */
  "vroot",

  /* Module configuration handler table */
  vroot_conftab,

  /* Module command handler table */
  vroot_cmdtab,

  /* Module authentication handler table */
  NULL,

  /* Module initialization function */
  NULL,

  /* Session initialization function */
  vroot_sess_init,

  /* Module version */
  MOD_VROOT_VERSION
};
