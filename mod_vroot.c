/*
 * ProFTPD: mod_vroot -- a module implementing a virtual chroot capability
 *                       via the FSIO API
 * Copyright (c) 2002-2024 TJ Saunders
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
 *
 * This is mod_vroot, contrib software for proftpd 1.3.x and above.
 * For more information contact TJ Saunders <tj@castaglia.org>.
 *
 * -----DO NOT EDIT BELOW THIS LINE-----
 * $Archive: mod_vroot.a $
 */

#include "mod_vroot.h"
#include "privs.h"
#include "alias.h"
#include "path.h"
#include "fsio.h"

int vroot_logfd = -1;
unsigned int vroot_opts = 0;

module vroot_module;

static int vroot_engine = FALSE;
static const char *trace_channel = "vroot";

#if PROFTPD_VERSION_NUMBER >= 0x0001030407
static int vroot_use_mkdtemp = FALSE;
#endif /* ProFTPD 1.3.4c or later */

static int handle_vrootaliases(void) {
  config_rec *c;
  pool *tmp_pool = NULL;

  /* Handle any VRootAlias settings. */

  tmp_pool = make_sub_pool(session.pool);
  pr_pool_tag(tmp_pool, "VRootAlias pool");

  c = find_config(main_server->conf, CONF_PARAM, "VRootAlias", FALSE);
  while (c != NULL) {
    char src_path[PR_TUNABLE_PATH_MAX+1], dst_path[PR_TUNABLE_PATH_MAX+1];
    const char *ptr;

    pr_signals_handle();

    /* XXX Note that by using vroot_path_lookup(), we assume a POST_CMD
     * invocation.  Looks like VRootAlias might end up being incompatible
     * with VRootServerRoot.
     */

    memset(src_path, '\0', sizeof(src_path));
    ptr = c->argv[0];

    /* Check for any expandable variables. */
    ptr = path_subst_uservar(tmp_pool, &ptr);

    sstrncpy(src_path, ptr, sizeof(src_path)-1);
    vroot_path_clean(src_path);

    ptr = c->argv[1];

    /* Check for any expandable variables. */
    ptr = path_subst_uservar(tmp_pool, &ptr);

    ptr = dir_best_path(tmp_pool, ptr);
    vroot_path_lookup(NULL, dst_path, sizeof(dst_path)-1, ptr,
      VROOT_LOOKUP_FL_NO_ALIAS, NULL);

    if (vroot_alias_add(dst_path, src_path) < 0) {
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
  int engine = -1;
  config_rec *c = NULL;

  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  engine = get_boolean(cmd, 1);
  if (engine == -1) {
    CONF_ERROR(cmd, "expected Boolean parameter");
  }

  c = add_config_param(cmd->argv[0], 1, NULL);
  c->argv[0] = pcalloc(c->pool, sizeof(int));
  *((int *) c->argv[0]) = engine;

  return PR_HANDLED(cmd);
}

/* usage: VRootLog path|"none" */
MODRET set_vrootlog(cmd_rec *cmd) {
  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  if (pr_fs_valid_path(cmd->argv[1]) < 0) {
    CONF_ERROR(cmd, "must be an absolute path");
  }

  (void) add_config_param_str(cmd->argv[0], 1, cmd->argv[1]);
  return PR_HANDLED(cmd);
}

/* usage: VRootOptions opt1 opt2 ... optN */
MODRET set_vrootoptions(cmd_rec *cmd) {
  config_rec *c = NULL;
  register unsigned int i;
  unsigned int opts = 0U;

  if (cmd->argc-1 == 0) {
    CONF_ERROR(cmd, "wrong number of parameters");
  }

  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  c = add_config_param(cmd->argv[0], 1, NULL);
  for (i = 1; i < cmd->argc; i++) {
    if (strcasecmp(cmd->argv[i], "AllowSymlinks") == 0) {
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
  char *path;
  size_t pathlen;

  CHECK_ARGS(cmd, 1);
  CHECK_CONF(cmd, CONF_ROOT|CONF_VIRTUAL|CONF_GLOBAL);

  path = cmd->argv[1];

  if (pr_fs_valid_path(path) < 0) {
    CONF_ERROR(cmd, "must be an absolute path");
  }

  if (stat(path, &st) < 0) {
    CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "error checking '", path, "': ",
      strerror(errno), NULL));
  }

  if (!S_ISDIR(st.st_mode)) {
    CONF_ERROR(cmd, pstrcat(cmd->tmp_pool, "'", path, "' is not a directory",
      NULL));
  }

  c = add_config_param(cmd->argv[0], 1, NULL);

  /* Make sure the configured path has a trailing path separater ('/').
   * This is important.
   */

  pathlen = strlen(path);
  if (path[pathlen - 1] != '/') {
    c->argv[0] = pstrcat(c->pool, path, "/", NULL);

  } else {
    c->argv[0] = pstrdup(c->pool, path);
  }

  return PR_HANDLED(cmd);
}

/* Command handlers
 */

static const char *vroot_cmd_fixup_path(cmd_rec *cmd, const char *key,
    int use_best_path) {
  const char *path;
  char *real_path = NULL;

  path = pr_table_get(cmd->notes, key, NULL);
  if (path != NULL) {
    if (use_best_path == TRUE) {
      /* Only needed for mod_sftp sessions, to do what mod_xfer does for FTP
       * commands, but in a way that does not require mod_sftp changes.
       * Probably too clever.
       */
      path = dir_best_path(cmd->pool, path);
    }

    if (*path == '/') {
      const char *base_path;

      base_path = vroot_path_get_base(cmd->tmp_pool, NULL);
      real_path = pdircat(cmd->pool, base_path, path, NULL);
      vroot_path_clean(real_path);

    } else {
      real_path = vroot_realpath(cmd->pool, path, VROOT_REALPATH_FL_ABS_PATH);
    }

    pr_trace_msg(trace_channel, 17,
      "fixed up '%s' path in command %s; was '%s', now '%s'", key,
      (char *) cmd->argv[0], path, real_path);
    pr_table_set(cmd->notes, key, real_path, 0);
  }

  return real_path;
}

MODRET vroot_pre_scp_retr(cmd_rec *cmd) {
  const char *key, *proto, *real_path;

  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

  /* As a PRE_CMD handler, we only run for SCP sessions. */
  proto = pr_session_get_protocol(0);
  if (strcmp(proto, "scp") != 0) {
    return PR_DECLINED(cmd);
  }

  /* Unlike SFTP sessions, mod_sftp does NOT set these cmd->notes for SCP
   * sessions before doing the PRE_CMD dispatching.  So we do it ourselves,
   * pre-emptively, before using our other machinery.
   */
  key = "mod_xfer.retr-path";
  (void) pr_table_add(cmd->notes, key, pstrdup(cmd->pool, cmd->arg), 0);

  real_path = vroot_cmd_fixup_path(cmd, key, TRUE);
  if (real_path != NULL) {
    /* In addition, for SCP sessions, we modify cmd->arg as well, for
     * mod_sftp's benefit.
     */
    cmd->arg = (char *) real_path;
  }

  return PR_DECLINED(cmd);
}

MODRET vroot_pre_sftp_retr(cmd_rec *cmd) {
  const char *key, *proto, *real_path;

  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

  /* As a PRE_CMD handler, we only run for SFTP sessions. */
  proto = pr_session_get_protocol(0);
  if (strcmp(proto, "sftp") != 0) {
    return PR_DECLINED(cmd);
  }

  key = "mod_xfer.retr-path";
  real_path = vroot_cmd_fixup_path(cmd, key, TRUE);
  if (real_path != NULL) {
    /* In addition, for SFTP sessions, we modify cmd->arg as well, for
     * mod_sftp's benefit.
     */
    cmd->arg = (char *) real_path;
  }

  return PR_DECLINED(cmd);
}

MODRET vroot_post_sftp_retr(cmd_rec *cmd) {
  const char *key, *path, *proto;

  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

  /* As a POST_CMD handler, we only run for SFTP sessions. */
  proto = pr_session_get_protocol(0);
  if (strcmp(proto, "sftp") != 0) {
    return PR_DECLINED(cmd);
  }

  key = "mod_xfer.retr-path";
  path = pr_table_get(cmd->notes, key, NULL);
  if (path != NULL) {
    /* In addition, for SFTP sessions, we modify session.xfer.path as well,
     * for mod_xfer's benefit in TransferLog entries.
     */
    session.xfer.path = pstrdup(session.xfer.p, path);
  }

  return PR_DECLINED(cmd);
}

MODRET vroot_log_retr(cmd_rec *cmd) {
  const char *key;

  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

  key = "mod_xfer.retr-path";
  (void) vroot_cmd_fixup_path(cmd, key, FALSE);
  return PR_DECLINED(cmd);
}

MODRET vroot_pre_scp_stor(cmd_rec *cmd) {
  const char *key, *proto, *real_path;

  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

  /* As a PRE_CMD handler, we only run for SCP sessions. */
  proto = pr_session_get_protocol(0);
  if (strcmp(proto, "scp") != 0) {
    return PR_DECLINED(cmd);
  }

  /* Unlike SFTP sessions, mod_sftp does NOT set these cmd->notes for SCP
   * sessions before doing the PRE_CMD dispatching.  So we do it ourselves,
   * pre-emptively, before using our other machinery.
   */
  key = "mod_xfer.store-path";
  (void) pr_table_add(cmd->notes, key, pstrdup(cmd->pool, cmd->arg), 0);

  real_path = vroot_cmd_fixup_path(cmd, key, TRUE);
  if (real_path != NULL) {
    /* In addition, for SCP sessions, we modify cmd->arg as well, for
     * mod_sftp's benefit.
     */
    cmd->arg = (char *) real_path;
  }

  return PR_DECLINED(cmd);
}

MODRET vroot_pre_sftp_stor(cmd_rec *cmd) {
  const char *key, *proto, *real_path;

  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

  /* As a PRE_CMD handler, we only run for SFTP sessions. */
  proto = pr_session_get_protocol(0);
  if (strcmp(proto, "sftp") != 0) {
    return PR_DECLINED(cmd);
  }

  key = "mod_xfer.store-path";
  real_path = vroot_cmd_fixup_path(cmd, key, TRUE);
  if (real_path != NULL) {
    /* In addition, for SFTP sessions, we modify cmd->arg as well, for
     * mod_sftp's benefit.
     */
    cmd->arg = (char *) real_path;
  }

  return PR_DECLINED(cmd);
}

MODRET vroot_post_sftp_stor(cmd_rec *cmd) {
  const char *key, *path, *proto;

  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

  /* As a POST_CMD handler, we only run for SFTP sessions. */
  proto = pr_session_get_protocol(0);
  if (strcmp(proto, "sftp") != 0) {
    return PR_DECLINED(cmd);
  }

  key = "mod_xfer.store-path";
  path = pr_table_get(cmd->notes, key, NULL);
  if (path != NULL) {
    /* In addition, for SFTP sessions, we modify session.xfer.path as well,
     * for mod_xfer's benefit in TransferLog entries.
     */
    session.xfer.path = pstrdup(session.xfer.p, path);
  }

  return PR_DECLINED(cmd);
}

MODRET vroot_log_stor(cmd_rec *cmd) {
  const char *key;

  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

  key = "mod_xfer.store-path";
  (void) vroot_cmd_fixup_path(cmd, key, FALSE);
  return PR_DECLINED(cmd);
}

MODRET vroot_pre_mkd(cmd_rec *cmd) {
  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

#if PROFTPD_VERSION_NUMBER >= 0x0001030407
  vroot_use_mkdtemp = pr_fsio_set_use_mkdtemp(FALSE);
#endif /* ProFTPD 1.3.4c or later */

  return PR_DECLINED(cmd);
}

MODRET vroot_post_mkd(cmd_rec *cmd) {
  if (vroot_engine == FALSE ||
      session.chroot_path == NULL) {
    return PR_DECLINED(cmd);
  }

#if PROFTPD_VERSION_NUMBER >= 0x0001030407
  pr_fsio_set_use_mkdtemp(vroot_use_mkdtemp);
#endif /* ProFTPD 1.3.4c or later */

  return PR_DECLINED(cmd);
}

MODRET vroot_post_pass(cmd_rec *cmd) {
  if (vroot_engine == FALSE) {
    return PR_DECLINED(cmd);
  }

  /* If not chrooted, umount our vroot FS. */
  if (session.chroot_path == NULL) {
    pr_fs_t *fs;

    fs = pr_unmount_fs("/", "vroot");
    if (fs != NULL) {
      destroy_pool(fs->fs_pool);
      pr_log_debug(DEBUG5, MOD_VROOT_VERSION ": vroot unmounted");
      pr_fs_setcwd(pr_fs_getvwd());
      pr_fs_clear_cache();

    } else {
      pr_log_debug(DEBUG2, MOD_VROOT_VERSION
        ": error unmounting vroot: %s", strerror(errno));
    }

  } else {
    config_rec *c;

    /* Otherwise, lookup and process any VRootOptions. */
    c = find_config(main_server->conf, CONF_PARAM, "VRootOptions", FALSE);
    if (c != NULL) {
      vroot_opts = *((unsigned int *) c->argv[0]);
    }

    /* XXX This needs to be in the PRE_CMD PASS handler, as when
     * VRootServer is used, so that a real chroot(2) occurs.
     */
    handle_vrootaliases();
  }

  return PR_DECLINED(cmd);
}

/* Event listeners
 */

static void vroot_chroot_ev(const void *event_data, void *user_data) {
  pr_fs_t *curr_fs = NULL, *our_fs = NULL;
  int match = FALSE, *use_vroot = NULL;

  use_vroot = get_param_ptr(main_server->conf, "VRootEngine", FALSE);
  if (use_vroot == NULL ||
      *use_vroot == FALSE) {
    vroot_engine = FALSE;
    return;
  }

  /* First, make sure that we have not already registered our FS object. */
  our_fs = pr_unmount_fs("/", "vroot");
  if (our_fs != NULL) {
    destroy_pool(our_fs->fs_pool);
  }

  /* Note that we need to be aware of other modules' FS handlers, such as
   * mod_facl (see Issue #1780).
   */
  curr_fs = pr_get_fs("/", &match);

  our_fs = pr_register_fs(main_server->pool, "vroot", "/");
  if (our_fs == NULL) {
    pr_log_debug(DEBUG3, MOD_VROOT_VERSION ": error registering fs: %s",
      strerror(errno));
    return;
  }

  pr_log_debug(DEBUG5, MOD_VROOT_VERSION ": vroot registered");

  if (curr_fs != NULL) {
    our_fs->fs_name = pstrcat(our_fs->fs_pool, "vroot+", curr_fs->fs_name,
      NULL);

    /* We provide our own handlers for most FS handlers, but not all.
     * Make sure to use the handlers of the current FS for the rest.
     */

    our_fs->fstat = curr_fs->fstat;
    our_fs->close = curr_fs->close;
    our_fs->read = curr_fs->read;
    our_fs->pread = curr_fs->pread;
    our_fs->write = curr_fs->write;
    our_fs->pwrite = curr_fs->pwrite;
    our_fs->lseek = curr_fs->lseek;
    our_fs->ftruncate = curr_fs->ftruncate;
    our_fs->fchmod = curr_fs->fchmod;
    our_fs->fchown = curr_fs->fchown;
    our_fs->access = curr_fs->access;
    our_fs->faccess = curr_fs->faccess;
    our_fs->futimes = curr_fs->futimes;
  }

  /* Add the module's custom FS callbacks here. This module does not
   * provide callbacks for the following (as they are unnecessary):
   * close(), read(), write(), and lseek().
   */
  our_fs->stat = vroot_fsio_stat;
  our_fs->lstat = vroot_fsio_lstat;
  our_fs->rename = vroot_fsio_rename;
  our_fs->unlink = vroot_fsio_unlink;
  our_fs->open = vroot_fsio_open;
#if PROFTPD_VERSION_NUMBER < 0x0001030603
  our_fs->creat = vroot_fsio_creat;
#endif /* ProFTPD 1.3.6rc2 or earlier */
  our_fs->link = vroot_fsio_link;
  our_fs->readlink = vroot_fsio_readlink;
  our_fs->symlink = vroot_fsio_symlink;
  our_fs->truncate = vroot_fsio_truncate;
  our_fs->chmod = vroot_fsio_chmod;
  our_fs->chown = vroot_fsio_chown;
#if PROFTPD_VERSION_NUMBER >= 0x0001030407
  our_fs->lchown = vroot_fsio_lchown;
#endif /* ProFTPD 1.3.4c or later */
  our_fs->chdir = vroot_fsio_chdir;
  our_fs->chroot = vroot_fsio_chroot;
  our_fs->utimes = vroot_fsio_utimes;
  our_fs->opendir = vroot_fsio_opendir;
  our_fs->readdir = vroot_fsio_readdir;
  our_fs->closedir = vroot_fsio_closedir;
  our_fs->mkdir = vroot_fsio_mkdir;
  our_fs->rmdir = vroot_fsio_rmdir;

  vroot_engine = TRUE;
}

static void vroot_exit_ev(const void *event_data, void *user_data) {
  (void) vroot_alias_free();
  (void) vroot_fsio_free();
}

/* Initialization routines
 */

static int vroot_sess_init(void) {
  config_rec *c;

  c = find_config(main_server->conf, CONF_PARAM, "VRootLog", FALSE);
  if (c != NULL) {
    const char *path;

    path = c->argv[0];
    if (strcasecmp(path, "none") != 0) {
      int res, xerrno;

      PRIVS_ROOT
      res = pr_log_openfile(path, &vroot_logfd, 0660);
      xerrno = errno;
      PRIVS_RELINQUISH

      switch (res) {
        case 0:
          break;

        case -1:
          pr_log_debug(DEBUG1, MOD_VROOT_VERSION
            ": unable to open VRootLog '%s': %s", path, strerror(xerrno));
          break;

        case PR_LOG_SYMLINK:
          pr_log_debug(DEBUG1, MOD_VROOT_VERSION
            ": unable to open VRootLog '%s': %s", path, "is a symlink");
          break;

        case PR_LOG_WRITABLE_DIR:
          pr_log_debug(DEBUG1, MOD_VROOT_VERSION
            ": unable to open VRootLog '%s': %s", path,
            "parent directory is world-writable");
          break;
      }
    }
  }

  vroot_alias_init(session.pool);
  vroot_fsio_init(session.pool);

  pr_event_register(&vroot_module, "core.chroot", vroot_chroot_ev, NULL);
  pr_event_register(&vroot_module, "core.exit", vroot_exit_ev, NULL);

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
  { POST_CMD,		C_PASS,	G_NONE,	vroot_post_pass, FALSE, FALSE },

  { PRE_CMD,		C_MKD,	G_NONE,	vroot_pre_mkd, FALSE, FALSE },
  { POST_CMD,		C_MKD,	G_NONE,	vroot_post_mkd, FALSE, FALSE },
  { POST_CMD_ERR,	C_MKD,	G_NONE,	vroot_post_mkd, FALSE, FALSE },
  { PRE_CMD,		C_XMKD,	G_NONE,	vroot_pre_mkd, FALSE, FALSE },
  { POST_CMD,		C_XMKD,	G_NONE,	vroot_post_mkd, FALSE, FALSE },
  { POST_CMD_ERR,	C_XMKD,	G_NONE,	vroot_post_mkd, FALSE, FALSE },

  /* These command handlers are for manipulating cmd->notes, to get
   * paths properly logged.
   *
   * Ideally these POST_CMD handlers would be LOG_CMD/LOG_CMD_ERR phase
   * handlers.  HOWEVER, we need to transform things before the cmd is
   * dispatched to mod_log, and mod_log uses a C_ANY handler for logging.
   * And when dispatching, C_ANY handlers are run before named handlers.
   * This means that using * LOG_CMD/LOG_CMD_ERR handlers would be run AFTER
   * mod_log's handler, even though we appear BEFORE mod_log in the module
   * load order.
   *
   * Thus to do the transformation, we actually use CMD/POST_CMD_ERR phase
   * handlers here.  The reason to use CMD, rather than POST_CMD, is the
   * the TransferLog entries are written by mod_xfer, in its CMD handlers.
   * Given this, you might be tempted to change these to PRE_CMD handlers.
   * That will not work, either, as the necessary cmd->notes keys are
   * populated by PRE_CMD handlers in mod_xfer, one of the last modules to
   * run.
   */
  { CMD,		C_APPE,	G_NONE, vroot_log_stor, FALSE, FALSE, CL_WRITE },
  { POST_CMD_ERR,	C_APPE,	G_NONE, vroot_log_stor, FALSE, FALSE },
  { CMD,		C_RETR,	G_NONE, vroot_log_retr, FALSE, FALSE, CL_READ },
  { POST_CMD_ERR,	C_RETR,	G_NONE, vroot_log_retr, FALSE, FALSE },
  { CMD,		C_STOR,	G_NONE, vroot_log_stor, FALSE, FALSE, CL_WRITE },
  { POST_CMD_ERR,	C_STOR,	G_NONE, vroot_log_stor, FALSE, FALSE },

  /* To make this more complicated, we DO actually want these handlers to
   * run as PRE_CMD handlers, but only for mod_sftp sessions.  Why?  The
   * mod_sftp module does not use the normal CMD handlers; it handles
   * dispatching on its own.  And we do still want mod_vroot to fix up
   * the paths properly for SFTP/SCP sessions, too.
   */
  { PRE_CMD,		C_APPE,	G_NONE, vroot_pre_sftp_stor, FALSE, FALSE, CL_WRITE },
  { POST_CMD,		C_APPE,	G_NONE, vroot_post_sftp_stor, FALSE, FALSE },
  { PRE_CMD,		C_RETR,	G_NONE, vroot_pre_sftp_retr, FALSE, FALSE, CL_READ },
  { POST_CMD,		C_RETR,	G_NONE, vroot_post_sftp_retr, FALSE, FALSE },
  { PRE_CMD,		C_STOR,	G_NONE, vroot_pre_sftp_stor, FALSE, FALSE, CL_WRITE },
  { POST_CMD,		C_STOR,	G_NONE, vroot_post_sftp_stor, FALSE, FALSE },

  { PRE_CMD,		C_RETR,	G_NONE, vroot_pre_scp_retr, FALSE, FALSE, CL_READ },
  { PRE_CMD,		C_STOR,	G_NONE, vroot_pre_scp_stor, FALSE, FALSE, CL_WRITE },

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
