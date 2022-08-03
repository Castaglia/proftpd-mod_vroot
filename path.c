/*
 * ProFTPD: mod_vroot Path API
 * Copyright (c) 2016-2022 TJ Saunders
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
 * As a special exemption, the respective copyright holders give permission
 * to link this program with OpenSSL, and distribute the resulting
 * executable, without including the source code for OpenSSL in the source
 * distribution.
 */

#include "path.h"
#include "alias.h"

static char vroot_base[PR_TUNABLE_PATH_MAX + 1];
static size_t vroot_baselen = 0;

static const char *trace_channel = "vroot.path";

/* Support routines note: some of these support functions are borrowed from
 * pure-ftpd.
 */

static void strmove(register char *dst, register const char *src) {
  if (dst == NULL ||
      src == NULL) {
    return;
  }

  while (*src != 0) {
    *dst++ = *src++;
  }

  *dst = 0;
}

int vroot_path_have_base(void) {
  if (*vroot_base == '\0') {
    return FALSE;
  }

  return TRUE;
}

const char *vroot_path_get_base(pool *p, size_t *baselen) {
  if (p == NULL) {
    errno = EINVAL;
    return NULL;
  }

  if (baselen != NULL) {
    *baselen = vroot_baselen;
  }

  return pstrdup(p, vroot_base);
}

int vroot_path_set_base(const char *base, size_t baselen) {
  if (base == NULL ||
      baselen >= sizeof(vroot_base)) {
    errno = EINVAL;
    return -1;
  }

  memset(vroot_base, '\0', sizeof(vroot_base));
  if (baselen > 0) {
    memcpy(vroot_base, base, baselen);
    vroot_base[sizeof(vroot_base)-1] = '\0';
  }
  vroot_baselen = baselen;

  return 0;
}

/* Note that we do in-place modifications of the given `path` buffer here,
 * which means that it MUST be writable; no constant strings, please.
 */
void vroot_path_clean(char *path) {
  char *ptr = NULL;

  if (path == NULL ||
      *path == 0) {
    return;
  }

  ptr = strstr(path, "//");
  while (ptr != NULL) {
    pr_signals_handle();

    strmove(ptr, ptr + 1);
    ptr = strstr(path, "//");
  }

  ptr = strstr(path, "/./");
  while (ptr != NULL) {
    pr_signals_handle();

    strmove(ptr, ptr + 2);
    ptr = strstr(path, "/./");
  }

  while (strncmp(path, "../", 3) == 0) {
    pr_signals_handle();

    path += 3;
  }

  ptr = strstr(path, "/../");
  if (ptr != NULL) {
    if (ptr == path) {
      while (strncmp(path, "/../", 4) == 0) {
        pr_signals_handle();

        strmove(path, path + 3);
      }

      ptr = strstr(path, "/../");
    }

    while (ptr != NULL) {
      char *next_elem;

      pr_signals_handle();

      next_elem = ptr + 4;

      if (ptr != path &&
          *ptr == '/') {
        ptr--;
      }

      while (ptr != path &&
             *ptr != '/') {
        ptr--;
      }

      if (*ptr == '/') {
        ptr++;
      }

      strmove(ptr, next_elem);
      ptr = strstr(path, "/../");
    }
  }

  ptr = path;

  if (*ptr == '.') {
    ptr++;

    if (*ptr == '\0') {
      return;
    }

    if (*ptr == '/') {
      while (*ptr == '/') {
        ptr++;
      }

      strmove(path, ptr);
    }
  }

  if (*ptr == '\0') {
    return;
  }

  ptr = path + strlen(path) - 1;
  if (*ptr != '.' ||
      ptr == path) {
    return;
  }

  ptr--;
  if (*ptr == '/' ||
      ptr == path) {
    ptr[1] = '\0';
    return;
  }

  if (*ptr != '.' ||
      ptr == path) {
    return;
  }

  ptr--;
  if (*ptr != '/') {
    return;
  }

  *ptr = '\0';
  ptr = strrchr(path, '/');
  if (ptr == NULL) {
    *path = '/';
    path[1] = '\0';
    return;
  }

  ptr[1] = '\0';
}

char *vroot_realpath(pool *p, const char *path, int flags) {
  char *real_path = NULL;
  size_t real_pathlen;

  if (p == NULL ||
      path == NULL) {
    errno = EINVAL;
    return NULL;
  }

  /* If not an absolute path, prepend the current location. */
  if (*path != '/' &&
      (flags & VROOT_REALPATH_FL_ABS_PATH)) {
    real_path = pdircat(p, pr_fs_getvwd(), path, NULL);

  } else {
    real_path = pstrdup(p, path);
  }

  vroot_path_clean(real_path);

  /* If the given path ends in a slash, remove it.  The handling of
   * VRootAliases is sensitive to such things.
   */
  real_pathlen = strlen(real_path);
  if (real_pathlen > 1 &&
      real_path[real_pathlen-1] == '/') {
    real_path[real_pathlen-1] = '\0';
    real_pathlen--;
  }

  return real_path;
}

/* The given `vpath` buffer is the looked-up path for the given `path`. */
int vroot_path_lookup(pool *p, char *vpath, size_t vpathsz, const char *path,
    int flags, char **alias_path) {
  char buf[PR_TUNABLE_PATH_MAX + 1], *bufp = NULL;
  const char *cwd;

  if (vpath == NULL ||
      path == NULL) {
    errno = EINVAL;
    return -1;
  }

  memset(buf, '\0', sizeof(buf));
  if (vpath != NULL &&
      vpathsz > 0) {
    memset(vpath, '\0', vpathsz);
  }

  cwd = pr_fs_getcwd();

  if (strcmp(path, ".") != 0) {
    sstrncpy(buf, path, sizeof(buf));

  } else {
    sstrncpy(buf, cwd, sizeof(buf));
  }

  vroot_path_clean(buf);

  bufp = buf;

  if (strncmp(bufp, vroot_base, vroot_baselen) == 0) {
    size_t len;

    /* Attempt to handle cases like "/base/base" and "/base/basefoo", where
     * the base is just "/base".
     * See https://github.com/proftpd/proftpd/issues/1491
     */
    len = strlen(bufp);
    if (len > vroot_baselen &&
        bufp[vroot_baselen] == '/') {
      bufp += vroot_baselen;
    }
  }

loop:
  pr_signals_handle();

  if (bufp[0] == '.' &&
      bufp[1] == '.' &&
      (bufp[2] == '\0' ||
       bufp[2] == '/')) {
    char *ptr = NULL;

    ptr = strrchr(vpath, '/');
    if (ptr != NULL) {
      *ptr = '\0';

    } else {
      *vpath = '\0';
    }

    if (strncmp(vpath, vroot_base, vroot_baselen) == 0 ||
         vpath[vroot_baselen] != '/') {
      snprintf(vpath, vpathsz, "%s/", vroot_base);
    }

    if (bufp[0] == '.' &&
        bufp[1] == '.' &&
        bufp[2] == '/') {
      bufp += 3;
      goto loop;
    }

  } else if (*bufp == '/') {
    snprintf(vpath, vpathsz, "%s/", vroot_base);
    bufp += 1;
    goto loop;

  } else if (*bufp != '\0') {
    size_t buflen, tmplen;
    char *ptr = NULL;

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
    tmplen = strlen(vpath);

    if (tmplen + buflen >= vpathsz) {
      errno = ENAMETOOLONG;
      return -1;
    }

    vpath[tmplen] = '/';
    memcpy(vpath + tmplen + 1, bufp, buflen);
  }

  /* Clean any unnecessary characters added by the above processing. */
  vroot_path_clean(vpath);

  if (!(flags & VROOT_LOOKUP_FL_NO_ALIAS)) {
    int alias_count;

    /* Check to see if this path is an alias; if so, return the real path. */
    alias_count = vroot_alias_count();
    if (alias_count > 0) {
      char *start_ptr = NULL, *end_ptr = NULL;
      const char *src_path = NULL;

      /* buf is used here for storing the "suffix", to be appended later when
       * aliases are found.
       */
      bufp = buf;
      start_ptr = vpath;

      while (start_ptr != NULL) {
        char *ptr = NULL;

        pr_signals_handle();

        pr_trace_msg(trace_channel, 15, "checking for alias for '%s'",
          start_ptr);

        src_path = vroot_alias_get(start_ptr);
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

          sstrncpy(vpath, src_path, vpathsz);

          if (end_ptr != NULL) {
            /* Now tack on our suffix from the scratchpad. */
            sstrcat(vpath, bufp, vpathsz);
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

        /* Store the suffix in the buf scratchpad. */
        sstrncpy(buf, ptr, sizeof(buf));
        end_ptr = ptr;
        *end_ptr = '\0';
      }
    }
  }

  /* Note that logging the session.chroot_path here will not help; mod_vroot
   * deliberately always sets that to just "/".
   */
  pr_trace_msg(trace_channel, 19,
    "lookup: path = '%s', cwd = '%s', base = '%s', vpath = '%s'", path, cwd,
    vroot_base, vpath);
  return 0;
}
