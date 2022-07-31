/*
 * ProFTPD - mod_vroot testsuite
 * Copyright (c) 2016-2022 TJ Saunders <tj@castaglia.org>
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

/* vroot tests. */

#include "tests.h"
#include "path.h"

static pool *p = NULL;

static void set_up(void) {
  if (p == NULL) {
    p = make_sub_pool(NULL);
  }

  if (getenv("TEST_VERBOSE") != NULL) {
    pr_trace_set_levels("vroot.path", 1, 20);
  }
}

static void tear_down(void) {
  (void) vroot_path_set_base("", 0);

  if (getenv("TEST_VERBOSE") != NULL) {
    pr_trace_set_levels("vroot.path", 0, 0);
  }

  if (p != NULL) {
    destroy_pool(p);
    p = NULL;
  } 
}

START_TEST (path_have_base_test) {
  int res;

  mark_point();
  res = vroot_path_have_base();
  ck_assert_msg(res == FALSE, "Have vroot base unexpectedly");
}
END_TEST

START_TEST (path_get_base_test) {
  const char *res;

  mark_point();
  res = vroot_path_get_base(NULL, NULL);
  ck_assert_msg(res == NULL, "Failed to handle null pool");
  ck_assert_msg(errno == EINVAL, "Expected EINVAL (%d), got '%s' (%d)", EINVAL,
    strerror(errno), errno);

  mark_point();
  res = vroot_path_get_base(p, NULL);
  ck_assert_msg(res != NULL, "Failed to get base: %s", strerror(errno));
  ck_assert_msg(strcmp(res, "") == 0, "Expected '', got '%s'", res);
}
END_TEST

START_TEST (path_set_base_test) {
  int res;
  const char *path, *ptr;
  size_t pathlen, len;

  mark_point();
  res = vroot_path_set_base(NULL, 0);
  ck_assert_msg(res < 0, "Failed to handle missing path");
  ck_assert_msg(errno == EINVAL, "Expected EINVAL (%d), got '%s' (%d)", EINVAL,
    strerror(errno), errno);

  mark_point();
  path = "/foo";
  pathlen = (PR_TUNABLE_PATH_MAX * 4);
  res = vroot_path_set_base("foo", pathlen);
  ck_assert_msg(res < 0, "Failed to handle too-long pathlen");
  ck_assert_msg(errno == EINVAL, "Expected EINVAL (%d), got '%s' (%d)", EINVAL,
    strerror(errno), errno);

  mark_point();
  path = "/foo";
  pathlen = strlen(path);
  res = vroot_path_set_base(path, pathlen);
  ck_assert_msg(res == 0, "Failed to set base '%s': %s", path, strerror(errno));

  mark_point();
  res = vroot_path_have_base();
  ck_assert_msg(res == TRUE, "Have base is unexpectedly false");

  mark_point();
  ptr = vroot_path_get_base(p, &len);
  ck_assert_msg(ptr != NULL, "Failed to get base: %s", strerror(errno));
  ck_assert_msg(len == pathlen, "Expected %lu, got %lu",
    (unsigned long) pathlen, (unsigned long) len);

  /* Clear the base, using an empty string. */
  mark_point();
  path = "";
  res = vroot_path_set_base(path, 0);
  ck_assert_msg(res == 0, "Failed to set empty path as base: %s",
    strerror(errno));

  mark_point();
  res = vroot_path_have_base();
  ck_assert_msg(res == FALSE, "Have base is unexpectedly true");
}
END_TEST

START_TEST (path_clean_test) {
  char *path, *expected;

  mark_point();
  vroot_path_clean(NULL);

  mark_point();
  path = pstrdup(p, "//");
  expected = "/";
  vroot_path_clean(path);
  ck_assert_msg(strcmp(path, expected) == 0, "Expected '%s', got '%s'",
    expected, path);

  mark_point();
  path = pstrdup(p, "/foo/./bar//");
  expected = "/foo/bar/";
  vroot_path_clean(path);
  ck_assert_msg(strcmp(path, expected) == 0, "Expected '%s', got '%s'",
    expected, path);

  mark_point();
  path = pstrdup(p, "/foo/../bar//");
  expected = "/bar/";
  vroot_path_clean(path);
  ck_assert_msg(strcmp(path, expected) == 0, "Expected '%s', got '%s'",
    expected, path);

  mark_point();
  path = pstrdup(p, "/./.././.././bar/./");
  expected = "/bar/";
  vroot_path_clean(path);
  ck_assert_msg(strcmp(path, expected) == 0, "Expected '%s', got '%s'",
    expected, path);

  mark_point();
  path = pstrdup(p, ".");
  expected = ".";
  vroot_path_clean(path);
  ck_assert_msg(strcmp(path, expected) == 0, "Expected '%s', got '%s'",
    expected, path);
}
END_TEST

START_TEST (realpath_test) {
  char *res, *path, *expected;

  mark_point();
  res = vroot_realpath(NULL, NULL, 0);
  ck_assert_msg(res == NULL, "Failed to handle null pool");
  ck_assert_msg(errno == EINVAL, "Expected EINVAL (%d), got '%s' (%d)", EINVAL,
    strerror(errno), errno);

  mark_point();
  res = vroot_realpath(p, NULL, 0);
  ck_assert_msg(res == NULL, "Failed to handle null path");
  ck_assert_msg(errno == EINVAL, "Expected EINVAL (%d), got '%s' (%d)", EINVAL,
    strerror(errno), errno);

  mark_point();
  path = pstrdup(p, "/foo");
  expected = "/foo";
  res = vroot_realpath(p, path, 0);
  ck_assert_msg(res != NULL, "Failed to handle path: '%s'", strerror(errno));
  ck_assert_msg(strcmp(res, expected) == 0, "Expected '%s', got '%s'",
    expected, res);

  mark_point();
  path = pstrdup(p, "/foo/");
  expected = "/foo";
  res = vroot_realpath(p, path, 0);
  ck_assert_msg(res != NULL, "Failed to handle path: '%s'", strerror(errno));
  ck_assert_msg(strcmp(res, expected) == 0, "Expected '%s', got '%s'",
    expected, res);

  mark_point();
  path = pstrdup(p, "/foo//");
  expected = "/foo";
  res = vroot_realpath(p, path, 0);
  ck_assert_msg(res != NULL, "Failed to handle path: '%s'", strerror(errno));
  ck_assert_msg(strcmp(res, expected) == 0, "Expected '%s', got '%s'",
    expected, res);
}
END_TEST

START_TEST (path_lookup_test) {
  int res;
  char *path;
  size_t pathlen;
  const char *dir;

  mark_point();
  path = pstrdup(p, "/foo");
  pathlen = strlen(path);
  res = vroot_path_lookup(p, path, pathlen, NULL, 0, NULL);
  ck_assert_msg(res < 0, "Failed to handle null dir");
  ck_assert_msg(errno == EINVAL, "Expected EINVAL (%d), got '%s' (%d)", EINVAL,
    strerror(errno), errno);

  mark_point();
  dir = "/";
  res = vroot_path_lookup(NULL, NULL, 0, dir, 0, NULL);
  ck_assert_msg(res >= 0, "Failed to handle null pool, path: %s",
    strerror(errno));

  mark_point();
  res = vroot_path_lookup(p, path, pathlen, dir, 0, NULL);
  ck_assert_msg(res >= 0, "Failed to lookup path '%s': %s", path,
    strerror(errno));

  mark_point();
  dir = ".";
  res = vroot_path_lookup(p, path, pathlen, dir, 0, NULL);
  ck_assert_msg(res >= 0, "Failed to lookup path '%s': %s", path,
    strerror(errno));
}
END_TEST

Suite *tests_get_path_suite(void) {
  Suite *suite;
  TCase *testcase;

  suite = suite_create("path");
  testcase = tcase_create("base");

  tcase_add_checked_fixture(testcase, set_up, tear_down);

  tcase_add_test(testcase, path_have_base_test);
  tcase_add_test(testcase, path_get_base_test);
  tcase_add_test(testcase, path_set_base_test);
  tcase_add_test(testcase, path_clean_test);
  tcase_add_test(testcase, realpath_test);
  tcase_add_test(testcase, path_lookup_test);

  suite_add_tcase(suite, testcase);
  return suite;
}
