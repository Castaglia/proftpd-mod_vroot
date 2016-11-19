/*
 * ProFTPD - mod_vroot testsuite
 * Copyright (c) 2016 TJ Saunders <tj@castaglia.org>
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

/* Alias tests. */

#include "tests.h"
#include "alias.h"

static pool *p = NULL;

static void set_up(void) {
  if (p == NULL) {
    p = make_sub_pool(NULL);
  }

  vroot_alias_init(p);

  if (getenv("TEST_VERBOSE") != NULL) {
    pr_trace_set_levels("vroot.alias", 1, 20);
  }
}

static void tear_down(void) {
  if (getenv("TEST_VERBOSE") != NULL) {
    pr_trace_set_levels("vroot.alias", 0, 0);
  }

  vroot_alias_free();

  if (p) {
    destroy_pool(p);
    p = NULL;
  } 
}

START_TEST (alias_init_test) {
  int res;

  res = vroot_alias_init(NULL);
  fail_unless(res < 0, "Failed to handle null pool");
  fail_unless(errno == EINVAL, "Expected EINVAL (%d), got %s (%d)", EINVAL,
    strerror(errno), errno);
}
END_TEST

START_TEST (alias_count_test) {
  unsigned int res;

  res = vroot_alias_count();
  fail_unless(res == 0, "Expected 0, got %u", res);
}
END_TEST

START_TEST (alias_exists_test) {
  int res;
  const char *path;

  res = vroot_alias_exists(NULL);
  fail_unless(res == FALSE, "Failed to handle null path");

  path = "/foo/bar";
  res = vroot_alias_exists(path);
  fail_unless(res == FALSE, "Expected FALSE for path '%s', got TRUE", path);
}
END_TEST

START_TEST (alias_add_test) {
  int res;
  const char *dst, *src;

  res = vroot_alias_add(NULL, NULL);
  fail_unless(res < 0, "Failed to handle null dst");
  fail_unless(errno == EINVAL, "Expected EINVAL (%d), got %s (%d)", EINVAL,
    strerror(errno), errno);

  dst = "foo";
  res = vroot_alias_add(dst, NULL);
  fail_unless(res < 0, "Failed to handle null src");
  fail_unless(errno == EINVAL, "Expected EINVAL (%d), got %s (%d)", EINVAL,
    strerror(errno), errno);

  src = "bar";
  res = vroot_alias_add(dst, src);
  fail_unless(res == 0, "Failed to add alias '%s => %s': %s", src, dst,
    strerror(errno));
}
END_TEST

START_TEST (alias_get_test) {
  const char *alias, *path;

  alias = vroot_alias_get(NULL);
  fail_unless(alias == NULL, "Failed to handle null path");
  fail_unless(errno == EINVAL, "Expected EINVAL (%d), got %s (%d)", EINVAL,
    strerror(errno), errno);

  path = "/foo/bar";
  alias = vroot_alias_get(path);
  fail_unless(alias == NULL, "Expected null for path '%s', got '%s'", path,
    alias);
  fail_unless(errno == ENOENT, "Expected ENOENT (%d), got %s (%d)", ENOENT,
    strerror(errno), errno);
}
END_TEST

START_TEST (alias_do_test) {
  int res;

  res = vroot_alias_do(NULL, NULL);
  fail_unless(res < 0, "Failed to handle null callback");
  fail_unless(errno == EINVAL, "Expected EINVAL (%d), got %s (%d)", EINVAL,
    strerror(errno), errno);
}
END_TEST

Suite *tests_get_alias_suite(void) {
  Suite *suite;
  TCase *testcase;

  suite = suite_create("alias");
  testcase = tcase_create("base");

  tcase_add_checked_fixture(testcase, set_up, tear_down);

  tcase_add_test(testcase, alias_init_test);
  tcase_add_test(testcase, alias_count_test);
  tcase_add_test(testcase, alias_exists_test);
  tcase_add_test(testcase, alias_add_test);
  tcase_add_test(testcase, alias_get_test);
  tcase_add_test(testcase, alias_do_test);

  suite_add_tcase(suite, testcase);
  return suite;
}
