/*
 * ProFTPD: mod_vroot Path API
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
  if (base == NULL) {
    errno = EINVAL;
    return -1;
  }

  memset(vroot_base, '\0', sizeof(vroot_base));
  memcpy(vroot_base, base, sizeof(vroot_base)-1);
  vroot_baselen = baselen;

  return 0;
}

void vroot_path_clean(char *path) {
  char *ptr = NULL;

  if (path == NULL ||
      *path == 0) {
    return;
  }

  ptr = strstr(path, "//");
  while (ptr != NULL) {
    strmove(ptr, ptr + 1);
    ptr = strstr(path, "//");
  }

  ptr = strstr(path, "/./");
  while (ptr != NULL) {
    strmove(ptr, ptr + 2);
    ptr = strstr(path, "/./");
  }

  while (strncmp(path, "../", 3) == 0) {
    path += 3;
  }

  ptr = strstr(path, "/../");
  if (ptr != NULL) {
    if (ptr == path) {
      while (strncmp(path, "/../", 4) == 0) {
        strmove(path, path + 3);
      }

      ptr = strstr(path, "/../");
    }

    while (ptr != NULL) {
      char *next_elem = ptr + 4;

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

  if (flags & VROOT_REALPATH_FL_ABS_PATH) {
    /* If not an absolute path, prepend the current location. */
    if (*path != '/') {
      real_path = pdircat(p, pr_fs_getvwd(), path, NULL);

    } else {
      real_path = pstrdup(p, path);
    }

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

int vroot_path_lookup(pool *p, char *path, size_t pathlen, const char *dir,
    int flags, char **alias_path) {
  char buf[PR_TUNABLE_PATH_MAX + 1], *bufp = NULL;

  memset(buf, '\0', sizeof(buf));
  memset(path, '\0', pathlen);

  if (strcmp(dir, ".") != 0) {
    sstrncpy(buf, dir, sizeof(buf));

  } else {
    sstrncpy(buf, pr_fs_getcwd(), sizeof(buf));
  }

  vroot_path_clean(buf);

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
    tmplen = strlen(path);

    if (tmplen + buflen >= pathlen) {
      errno = ENAMETOOLONG;
      return -1;
    }

    path[tmplen] = '/';
    memcpy(path + tmplen + 1, bufp, buflen);
  }

  /* Clean any unnecessary characters added by the above processing. */
  vroot_path_clean(path);

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
      start_ptr = path;

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

          sstrncpy(path, src_path, pathlen);

          if (end_ptr != NULL) {
            /* Now tack on our suffix from the scratchpad. */
            sstrcat(path, bufp, pathlen);
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

  return 0;
}
