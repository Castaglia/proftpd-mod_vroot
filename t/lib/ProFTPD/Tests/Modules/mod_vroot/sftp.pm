package ProFTPD::Tests::Modules::mod_vroot::sftp;

use lib qw(t/lib);
use base qw(ProFTPD::TestSuite::Child);
use strict;

use Cwd;
use Digest::MD5;
use File::Copy;
use File::Path qw(mkpath rmtree);
use File::Spec;
use IO::Handle;
use POSIX qw(:fcntl_h);

use ProFTPD::TestSuite::FTP;
use ProFTPD::TestSuite::Utils qw(:auth :config :running :test :testsuite);

$| = 1;

my $order = 0;

my $TESTS = {
  vroot_alias_file_sftp_read => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_file_sftp_write_no_overwrite => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_file_sftp_write => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_file_sftp_stat => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_file_sftp_lstat => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_file_sftp_realpath => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_file_sftp_remove => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_dir_sftp_readdir => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_dir_sftp_rmdir => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_symlink_sftp_stat => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_symlink_sftp_lstat => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_symlink_sftp_realpath => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

  vroot_alias_file_scp_download => {
    order => ++$order,
    test_class => [qw(forking mod_sftp scp)],
  },

  vroot_alias_file_scp_upload => {
    order => ++$order,
    test_class => [qw(forking mod_sftp scp)],
  },

  vroot_sftp_log_extlog_retr => {
    order => ++$order,
    test_class => [qw(bug forking mod_sftp sftp)],
  },

  vroot_sftp_log_xferlog_retr => {
    order => ++$order,
    test_class => [qw(bug forking mod_sftp sftp)],
  },

  vroot_sftp_log_extlog_stor => {
    order => ++$order,
    test_class => [qw(bug forking mod_sftp sftp)],
  },

  vroot_sftp_log_xferlog_stor => {
    order => ++$order,
    test_class => [qw(bug forking mod_sftp sftp)],
  },

  vroot_scp_log_extlog_retr => {
    order => ++$order,
    test_class => [qw(bug forking mod_sftp scp)],
  },

  vroot_scp_log_xferlog_retr => {
    order => ++$order,
    test_class => [qw(bug forking mod_sftp scp)],
  },

  vroot_scp_log_extlog_stor => {
    order => ++$order,
    test_class => [qw(bug forking mod_sftp scp)],
  },

  vroot_scp_log_xferlog_stor => {
    order => ++$order,
    test_class => [qw(bug forking mod_sftp scp)],
  },

  vroot_alias_dir_sftp_publickey_issue30 => {
    order => ++$order,
    test_class => [qw(forking mod_sftp sftp)],
  },

};

sub new {
  return shift()->SUPER::new(@_);
}

sub list_tests {
  return testsuite_get_runnable_tests($TESTS);

#    XXX test file aliases where the alias includes directories which do not
#    exist.  Should we allow traversal of these kinds of aliases (if so, what
#    real directory do we use for perms, ownership?  What would a CWD into
#    such a path component mean?), or only allow retrieval/storage to that
#    alias but not traversal?
}

sub set_up {
  my $self = shift;
  $self->SUPER::set_up(@_);

  # Make sure that mod_sftp does not complain about permissions on the hostkey
  # files.

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  unless (chmod(0400, $rsa_host_key, $dsa_host_key)) {
    die("Can't set perms on $rsa_host_key, $dsa_host_key: $!");
  }
}

# Support routines

sub create_test_dir {
  my $setup = shift;
  my $sub_dir = shift;

  mkpath($sub_dir);

  # Make sure that, if we're running as root, that the sub directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $sub_dir)) {
      die("Can't set perms on $sub_dir to 0755: $!");
    }

    unless (chown($setup->{uid}, $setup->{gid}, $sub_dir)) {
      die("Can't set owner of $sub_dir to $setup->{uid}/$setup->{gid}: $!");
    }
  }
}

sub create_test_file {
  my $setup = shift;
  my $test_file = shift;

  if (open(my $fh, "> $test_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $test_file: $!");
    }

    # Make sure that, if we're running as root, that the test file has
    # permissions/privs set for the account we create
    if ($< == 0) {
      unless (chown($setup->{uid}, $setup->{gid}, $test_file)) {
        die("Can't set owner of $test_file to $setup->{uid}/$setup->{gid}: $!");
      }
    }

  } else {
    die("Can't open $test_file: $!");
  }
}

# Test cases

sub vroot_alias_file_sftp_read {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  if (open(my $fh, "> $src_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $src_file: $!");
    }

  } else {
    die("Can't open $src_file: $!");
  }

  my $dst_file = '~/bar.txt';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $fh = $sftp->open('bar.txt', O_RDONLY);
      unless ($fh) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't open bar.txt: [$err_name] ($err_code)");
      }

      my $buf;
      my $size = 0;

      my $res = $fh->read($buf, 8192);
      while ($res) {
        $size += $res;
        $res = $fh->read($buf, 8192);
      }

      # To issue the FXP_CLOSE, we have to explicit destroy the filehandle
      $fh = undef;

      my $expected = 14;
      $self->assert($expected == $size,
        test_msg("Expected $expected, got $size"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_file_sftp_write_no_overwrite {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  create_test_file($setup, $src_file);

  my $dst_file = '~/bar.txt';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $fh = $sftp->open('bar.txt', O_WRONLY|O_TRUNC, 0644);
      if ($fh) {
        die("OPEN bar.txt succeeded unexpectedly");
      }

      my ($err_code, $err_name) = $sftp->error();

      my $expected = 'SSH_FX_PERMISSION_DENIED';
      $self->assert($expected eq $err_name,
        test_msg("Expected '$expected', got '$err_name'"));

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_alias_file_sftp_write {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  create_test_file($setup, $src_file);

  my $dst_file = '~/bar.txt';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    AllowOverwrite => 'on',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $fh = $sftp->open('bar.txt', O_WRONLY|O_TRUNC, 0644);
      unless ($fh) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't open bar.txt: [$err_name] ($err_code)");
      }

      my $count = 20;
      for (my $i = 0; $i < $count; $i++) {
        print $fh "ABCD" x 4096;
      }

      # To issue the FXP_CLOSE, we have to explicit destroy the filehandle
      $fh = undef;

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_alias_file_sftp_stat {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  if (open(my $fh, "> $src_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $src_file: $!");
    }

  } else {
    die("Can't open $src_file: $!");
  }

  my $dst_file = '~/bar.txt';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $attrs = $sftp->stat('bar.txt', 1);
      unless ($attrs) {
        my ($err_code, $err_name) = $sftp->error();
        die("FXP_STAT bar.txt failed: [$err_name] ($err_code)");
      }

      my $expected = 14;
      my $file_size = $attrs->{size};
      $self->assert($expected == $file_size,
        test_msg("Expected $expected, got $file_size"));

      $expected = $<;
      my $file_uid = $attrs->{uid};
      $self->assert($expected == $file_uid,
        test_msg("Expected '$expected', got '$file_uid'"));

      $expected = $(;
      my $file_gid = $attrs->{gid};
      $self->assert($expected == $file_gid,
        test_msg("Expected '$expected', got '$file_gid'"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_file_sftp_lstat {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  if (open(my $fh, "> $src_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $src_file: $!");
    }

  } else {
    die("Can't open $src_file: $!");
  }

  my $dst_file = '~/bar.txt';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $attrs = $sftp->stat('bar.txt', 0);
      unless ($attrs) {
        my ($err_code, $err_name) = $sftp->error();
        die("FXP_STAT bar.txt failed: [$err_name] ($err_code)");
      }

      my $expected = 14;
      my $file_size = $attrs->{size};
      $self->assert($expected == $file_size,
        test_msg("Expected $expected, got $file_size"));

      $expected = $<;
      my $file_uid = $attrs->{uid};
      $self->assert($expected == $file_uid,
        test_msg("Expected '$expected', got '$file_uid'"));

      $expected = $(;
      my $file_gid = $attrs->{gid};
      $self->assert($expected == $file_gid,
        test_msg("Expected '$expected', got '$file_gid'"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_file_sftp_realpath {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  if (open(my $fh, "> $src_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $src_file: $!");
    }

  } else {
    die("Can't open $src_file: $!");
  }

  my $dst_file = '~/bar.txt';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $real_path = $sftp->realpath('bar.txt');
      unless ($real_path) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't get real path for 'bar.txt': [$err_name] ($err_code)");
      }

      my $expected;

      $expected = '/bar.txt';
      $self->assert($expected eq $real_path,
        test_msg("Expected '$expected', got '$real_path'"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_file_sftp_remove {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  if (open(my $fh, "> $src_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $src_file: $!");
    }

  } else {
    die("Can't open $src_file: $!");
  }

  my $dst_file = '~/bar.txt';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $res = $sftp->unlink('bar.txt');
      if ($res) {
        die("REMOVE bar.txt succeeded unexpectedly");
      }

      my ($err_code, $err_name) = $sftp->error();

      my $expected = 'SSH_FX_PERMISSION_DENIED';
      $self->assert($expected eq $err_name,
        test_msg("Expected '$expected', got '$err_name'"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_dir_sftp_readdir {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_dir = File::Spec->rel2abs("$tmpdir/foo.d");
  mkpath($src_dir);

  my $dst_dir = '~/bar.d';

  my $test_file1 = File::Spec->rel2abs("$tmpdir/foo.d/a.txt");
  if (open(my $fh, "> $test_file1")) {
    close($fh);

  } else {
    die("Can't open $test_file1: $!");
  }

  my $test_file2 = File::Spec->rel2abs("$tmpdir/foo.d/b.txt");
  if (open(my $fh, "> $test_file2")) {
    close($fh);

  } else {
    die("Can't open $test_file2: $!");
  }

  my $test_file3 = File::Spec->rel2abs("$tmpdir/foo.d/c.txt");
  if (open(my $fh, "> $test_file3")) {
    close($fh);

  } else {
    die("Can't open $test_file3: $!");
  }

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        DefaultRoot => '~',

        VRootAlias => "$src_dir $dst_dir",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $dir = $sftp->opendir('bar.d');
      unless ($dir) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't open directory 'bar.d': [$err_name] ($err_code)");
      }

      my $res = {};

      my $file = $dir->read();
      while ($file) {
        $res->{$file->{name}} = $file;
        $file = $dir->read();
      }

      my $expected = {
        '.' => 1,
        '..' => 1,
        'a.txt' => 1,
        'b.txt' => 1,
        'c.txt' => 1,
      };

      # To issue the FXP_CLOSE, we have to explicit destroy the dirhandle
      $dir = undef;

      $ssh2->disconnect();

      my $ok = 1;
      my $mismatch;

      my $seen = [];
      foreach my $name (keys(%$res)) {
        push(@$seen, $name);

        unless (defined($expected->{$name})) {
          $mismatch = $name;
          $ok = 0;
          last;
        }
      }

      unless ($ok) {
        die("Unexpected name '$mismatch' appeared in READDIR data")
      }

      # Now remove from $expected all of the paths we saw; if there are
      # any entries remaining in $expected, something went wrong.
      foreach my $name (@$seen) {
        delete($expected->{$name});
      }

      my $remaining = scalar(keys(%$expected));
      $self->assert(0 == $remaining,
        test_msg("Expected 0, got $remaining"));
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_dir_sftp_rmdir {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_dir = File::Spec->rel2abs("$tmpdir/foo.d");
  mkpath($src_dir);

  my $dst_dir = '~/bar.d';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        DefaultRoot => '~',

        VRootAlias => "$src_dir $dst_dir",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $res = $sftp->rmdir('/bar.d');
      if ($res) {
        die("RMDIR bar.d succeeded unexpectedly");
      }

      my ($err_code, $err_name) = $sftp->error();

      my $expected = 'SSH_FX_PERMISSION_DENIED';
      $self->assert($expected eq $err_name,
        test_msg("Expected '$expected', got '$err_name'"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_symlink_sftp_stat {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  if (open(my $fh, "> $src_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $src_file: $!");
    }

  } else {
    die("Can't open $src_file: $!");
  }

  my $cwd = getcwd();

  unless (chdir($home_dir)) {
    die("Can't chdir to $home_dir: $!");
  }

  unless (symlink("./foo.txt", "foo.lnk")) {
    die("Can't symlink './foo.txt' to 'foo.lnk': $!");
  }

  unless (chdir($cwd)) {
    die("Can't chdir to $cwd: $!");
  }

  my $src_symlink = File::Spec->rel2abs("$tmpdir/foo.lnk");
  my $dst_file = '~/bar.lnk';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        VRootOptions => 'allowSymlinks',
        DefaultRoot => '~',

        VRootAlias => "$src_symlink $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $attrs = $sftp->stat('bar.lnk', 1);
      unless ($attrs) {
        my ($err_code, $err_name) = $sftp->error();
        die("FXP_STAT bar.txt failed: [$err_name] ($err_code)");
      }

      my $expected = 14;
      my $file_size = $attrs->{size};
      $self->assert($expected == $file_size,
        test_msg("Expected $expected, got $file_size"));

      $expected = $<;
      my $file_uid = $attrs->{uid};
      $self->assert($expected == $file_uid,
        test_msg("Expected '$expected', got '$file_uid'"));

      $expected = $(;
      my $file_gid = $attrs->{gid};
      $self->assert($expected == $file_gid,
        test_msg("Expected '$expected', got '$file_gid'"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_symlink_sftp_lstat {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  if (open(my $fh, "> $src_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $src_file: $!");
    }

  } else {
    die("Can't open $src_file: $!");
  }

  my $cwd = getcwd();

  unless (chdir($home_dir)) {
    die("Can't chdir to $home_dir: $!");
  }

  unless (symlink("./foo.txt", "foo.lnk")) {
    die("Can't symlink './foo.txt' to 'foo.lnk': $!");
  }

  unless (chdir($cwd)) {
    die("Can't chdir to $cwd: $!");
  }

  my $src_symlink = File::Spec->rel2abs("$tmpdir/foo.lnk");
  my $dst_file = '~/bar.lnk';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        VRootOptions => 'allowSymlinks',
        DefaultRoot => '~',

        VRootAlias => "$src_symlink $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $attrs = $sftp->stat('bar.lnk', 0);
      unless ($attrs) {
        my ($err_code, $err_name) = $sftp->error();
        die("FXP_STAT bar.txt failed: [$err_name] ($err_code)");
      }

      my $expected = 14;
      my $file_size = $attrs->{size};
      $self->assert($expected == $file_size,
        test_msg("Expected $expected, got $file_size"));

      $expected = $<;
      my $file_uid = $attrs->{uid};
      $self->assert($expected == $file_uid,
        test_msg("Expected '$expected', got '$file_uid'"));

      $expected = $(;
      my $file_gid = $attrs->{gid};
      $self->assert($expected == $file_gid,
        test_msg("Expected '$expected', got '$file_gid'"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_symlink_sftp_realpath {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};

  my $config_file = "$tmpdir/vroot.conf";
  my $pid_file = File::Spec->rel2abs("$tmpdir/vroot.pid");
  my $scoreboard_file = File::Spec->rel2abs("$tmpdir/vroot.scoreboard");

  my $log_file = File::Spec->rel2abs('tests.log');

  my $auth_user_file = File::Spec->rel2abs("$tmpdir/vroot.passwd");
  my $auth_group_file = File::Spec->rel2abs("$tmpdir/vroot.group");

  my $user = 'proftpd';
  my $passwd = 'test';
  my $home_dir = File::Spec->rel2abs($tmpdir);
  my $uid = 500;
  my $gid = 500;

  # Make sure that, if we're running as root, that the home directory has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chmod(0755, $home_dir)) {
      die("Can't set perms on $home_dir to 0755: $!");
    }

    unless (chown($uid, $gid, $home_dir)) {
      die("Can't set owner of $home_dir to $uid/$gid: $!");
    }
  }

  auth_user_write($auth_user_file, $user, $passwd, $uid, $gid, $home_dir,
    '/bin/bash');
  auth_group_write($auth_group_file, 'ftpd', $gid, $user);

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  if (open(my $fh, "> $src_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $src_file: $!");
    }

  } else {
    die("Can't open $src_file: $!");
  }

  my $cwd = getcwd();

  unless (chdir($home_dir)) {
    die("Can't chdir to $home_dir: $!");
  }

  unless (symlink("./foo.txt", "foo.lnk")) {
    die("Can't symlink './foo.txt' to 'foo.lnk': $!");
  }

  unless (chdir($cwd)) {
    die("Can't chdir to $cwd: $!");
  }

  my $src_symlink = File::Spec->rel2abs("$tmpdir/foo.lnk");
  my $dst_file = '~/bar.lnk';

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $pid_file,
    ScoreboardFile => $scoreboard_file,
    SystemLog => $log_file,
    TraceLog => $log_file,
    Trace => 'fsio:10',

    AuthUserFile => $auth_user_file,
    AuthGroupFile => $auth_group_file,
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $log_file",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $log_file,
        VRootOptions => 'allowSymlinks',
        DefaultRoot => '~',

        VRootAlias => "$src_symlink $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($config_file, $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($user, $passwd)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $real_path = $sftp->realpath('bar.lnk');
      unless ($real_path) {
        my ($err_code, $err_name) = $sftp->error();
        die("FXP_REALPATH bar.lnk failed: [$err_name] ($err_code)");
      }

      my $expected = '/bar.lnk';
      $self->assert($expected eq $real_path,
        test_msg("Expected '$expected', got '$real_path'"));

      $ssh2->disconnect();
    };

    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($config_file, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($pid_file);

  $self->assert_child_ok($pid);

  if ($ex) {
    die($ex);
  }

  unlink($log_file);
}

sub vroot_alias_file_scp_download {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  create_test_file($setup, $src_file);

  my $dst_file = '~/bar.txt';
  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $res = $ssh2->scp_get('/bar.txt', $test_file);
      unless ($res) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't download bar.txt from server: [$err_name] ($err_code) $err_str");
      }

      $ssh2->disconnect();

      unless (-f $test_file) {
        die("$test_file file does not exist as expected");
      }
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_alias_file_scp_upload {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $src_file = File::Spec->rel2abs("$tmpdir/foo.txt");
  create_test_file($setup, $src_file);

  my $dst_file = '~/bar.txt';
  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    AllowOverwrite => 'on',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',

        VRootAlias => "$src_file $dst_file",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $res = $ssh2->scp_put($setup->{config_file}, '/bar.txt');
      unless ($res) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't upload bar.txt to server: [$err_name] ($err_code) $err_str");
      }

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_sftp_log_extlog_retr {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  if (open(my $fh, "> $test_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $test_file: $!");
    }

  } else {
    die("Can't open $test_file: $!");
  }

  my $ext_log = File::Spec->rel2abs("$tmpdir/custom.log");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'command:20 event:20 extlog:20 fsio:10 jot:20 sftp:20 scp:20 vroot:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    LogFormat => 'custom "%f"',
    ExtendedLog => "$ext_log READ custom",

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;
  require Net::SSH2;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $fh = $sftp->open('test.txt', O_RDONLY);
      unless ($fh) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't open test.txt: [$err_name] ($err_code)");
      }

      my $buf;
      my $size = 0;

      my $res = $fh->read($buf, 8192);
      while ($res) {
        $size += $res;
        $res = $fh->read($buf, 8192);
      }

      # To issue the FXP_CLOSE, we have to explicit destroy the filehandle
      $fh = undef;

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  eval {
    if (open(my $fh, "< $ext_log")) {
      my $line = <$fh>;
      close($fh);
      chomp($line);

      if ($ENV{TEST_VERBOSE}) {
        print STDERR "$line\n";
      }

      if ($^O eq 'darwin') {
        # MacOSX-specific hack, due to how it handles tmp files
        $test_file = ('/private' . $test_file);
      }

      $self->assert($test_file eq $line,
        test_msg("Expected '$test_file', got '$line'"));

    } else {
      die("Can't read $ext_log: $!");
    }
  };
  if ($@) {
    $ex = $@;
  }

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_sftp_log_xferlog_retr {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  if (open(my $fh, "> $test_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $test_file: $!");
    }

  } else {
    die("Can't open $test_file: $!");
  }

  my $xfer_log = File::Spec->rel2abs("$tmpdir/xfer.log");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10 sftp:20 scp:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    TransferLog => $xfer_log,

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;
  require Net::SSH2;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $fh = $sftp->open('test.txt', O_RDONLY);
      unless ($fh) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't open test.txt: [$err_name] ($err_code)");
      }

      my $buf;
      my $size = 0;

      my $res = $fh->read($buf, 8192);
      while ($res) {
        $size += $res;
        $res = $fh->read($buf, 8192);
      }

      # To issue the FXP_CLOSE, we have to explicit destroy the filehandle
      $fh = undef;

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  eval {
    if (open(my $fh, "< $xfer_log")) {
      my $line = <$fh>;
      chomp($line);
      close($fh);

      if ($ENV{TEST_VERBOSE}) {
        print STDERR "$line\n";
      }

      my $expected = '^\S+\s+\S+\s+\d+\s+\d+:\d+:\d+\s+\d+\s+\d+\s+(\S+)\s+(\d+)\s+(\S+)\s+(\S+)\s+_\s+o\s+r\s+(\S+)\s+sftp\s+0\s+\*\s+c$';

      $self->assert(qr/$expected/, $line, "Expected '$expected', got '$line'");

      if ($line =~ /$expected/) {
        my $remote_host = $1;
        my $filesz = $2;
        my $filename = $3;
        my $xfer_type = $4;
        my $user_name = $5;

        if ($^O eq 'darwin') {
          # MacOSX-specific hack, due to how it handles tmp files
          $test_file = ('/private' . $test_file);
        }

        $expected = '127.0.0.1';
        $self->assert($expected eq $remote_host,
          "Expected '$expected', got '$remote_host'");

        $expected = -s $test_file;
        $self->assert($expected == $filesz,
          "Expected '$expected', got '$filesz'");

        $expected = $test_file;
        $self->assert($expected eq $filename,
          "Expected '$expected', got '$filename'");

        $expected = 'b';
        $self->assert($expected eq $xfer_type,
          "Expected '$expected', got '$xfer_type'");

        $expected = $setup->{user};
        $self->assert($expected eq $user_name,
          "Expected '$expected', got '$user_name'");
      }

    } else {
      die("Can't read $xfer_log: $!");
    }
  };
  if ($@) {
    $ex = $@;
  }

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_sftp_log_extlog_stor {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  my $ext_log = File::Spec->rel2abs("$tmpdir/ext.log");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10 jot:20 sftp:20 scp:20 vroot:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    LogFormat => 'custom "%f"',
    ExtendedLog => "$ext_log WRITE custom",

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;
  require Net::SSH2;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $fh = $sftp->open('test.txt', O_WRONLY|O_CREAT, 0644);
      unless ($fh) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't open test.txt: [$err_name] ($err_code)");
      }

      my $buf = "Hello, World!\n";
      print $fh $buf;

      # To issue the FXP_CLOSE, we have to explicit destroy the filehandle
      $fh = undef;

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  eval {
    if (open(my $fh, "< $ext_log")) {
      my $line = <$fh>;
      close($fh);
      chomp($line);

      if ($ENV{TEST_VERBOSE}) {
        print STDERR "# $line\n";
      }

      if ($^O eq 'darwin') {
        # MacOSX-specific hack, due to how it handles tmp files
        $test_file = ('/private' . $test_file);
      }

      $self->assert($test_file eq $line,
        test_msg("Expected '$test_file', got '$line'"));

    } else {
      die("Can't read $ext_log: $!");
    }
  };
  if ($@) {
    $ex = $@;
  }

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_sftp_log_xferlog_stor {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  my $xfer_log = File::Spec->rel2abs("$tmpdir/xfer.log");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10 jot:20 sftp:20 scp:20 vroot:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    TransferLog => $xfer_log,

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;
  require Net::SSH2;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $fh = $sftp->open('test.txt', O_WRONLY|O_CREAT, 0644);
      unless ($fh) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't open test.txt: [$err_name] ($err_code)");
      }

      my $buf = "Hello, World!\n";
      print $fh $buf;

      # To issue the FXP_CLOSE, we have to explicit destroy the filehandle
      $fh = undef;

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  eval {
    if (open(my $fh, "< $xfer_log")) {
      my $line = <$fh>;
      chomp($line);
      close($fh);

      if ($ENV{TEST_VERBOSE}) {
        print STDERR "$line\n";
      }

      my $expected = '^\S+\s+\S+\s+\d+\s+\d+:\d+:\d+\s+\d+\s+\d+\s+(\S+)\s+(\d+)\s+(\S+)\s+(\S+)\s+_\s+i\s+r\s+(\S+)\s+sftp\s+0\s+\*\s+c$';

      $self->assert(qr/$expected/, $line, "Expected '$expected', got '$line'");

      if ($line =~ /$expected/) {
        my $remote_host = $1;
        my $filesz = $2;
        my $filename = $3;
        my $xfer_type = $4;
        my $user_name = $5;

        if ($^O eq 'darwin') {
          # MacOSX-specific hack, due to how it handles tmp files
          $test_file = ('/private' . $test_file);
        }

        $expected = '127.0.0.1';
        $self->assert($expected eq $remote_host,
          "Expected '$expected', got '$remote_host'");

        $expected = -s $test_file;
        $self->assert($expected == $filesz,
          "Expected '$expected', got '$filesz'");

        $expected = $test_file;
        $self->assert($expected eq $filename,
          "Expected '$expected', got '$filename'");

        $expected = 'b';
        $self->assert($expected eq $xfer_type,
          "Expected '$expected', got '$xfer_type'");

        $expected = $setup->{user};
        $self->assert($expected eq $user_name,
          "Expected '$expected', got '$user_name'");
      }

    } else {
      die("Can't read $xfer_log: $!");
    }
  };
  if ($@) {
    $ex = $@;
  }

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_scp_log_extlog_retr {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  if (open(my $fh, "> $test_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $test_file: $!");
    }

  } else {
    die("Can't open $test_file: $!");
  }

  my $ext_log = File::Spec->rel2abs("$tmpdir/ext.log");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10 sftp:20 scp:20 vroot:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    LogFormat => 'custom "%f"',
    ExtendedLog => "$ext_log READ custom",

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;
  require Net::SSH2;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->scp_get('test.txt', '/dev/null')) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't download 'test.txt' from server: [$err_name] ($err_code) $err_str");
      }

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  eval {
    if (open(my $fh, "< $ext_log")) {
      my $line = <$fh>;
      chomp($line);
      close($fh);

      if ($ENV{TEST_VERBOSE}) {
        print STDERR "$line\n";
      }

      if ($^O eq 'darwin') {
        # MacOSX-specific hack, due to how it handles tmp files
        $test_file = ('/private' . $test_file);
      }

      $self->assert($test_file eq $line,
        test_msg("Expected '$test_file', got '$line'"));

    } else {
      die("Can't read $ext_log: $!");
    }
  };
  if ($@) {
    $ex = $@;
  }

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_scp_log_xferlog_retr {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  if (open(my $fh, "> $test_file")) {
    print $fh "Hello, World!\n";
    unless (close($fh)) {
      die("Can't write $test_file: $!");
    }

  } else {
    die("Can't open $test_file: $!");
  }

  my $xfer_log = File::Spec->rel2abs("$tmpdir/xfer.log");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10 sftp:20 scp:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    TransferLog => $xfer_log,

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;
  require Net::SSH2;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->scp_get('test.txt', '/dev/null')) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't download 'test.txt' from server: [$err_name] ($err_code) $err_str");
      }

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  eval {
    if (open(my $fh, "< $xfer_log")) {
      my $line = <$fh>;
      chomp($line);
      close($fh);

      if ($ENV{TEST_VERBOSE}) {
        print STDERR "$line\n";
      }

      my $expected = '^\S+\s+\S+\s+\d+\s+\d+:\d+:\d+\s+\d+\s+\d+\s+(\S+)\s+(\d+)\s+(\S+)\s+(\S+)\s+_\s+o\s+r\s+(\S+)\s+scp\s+0\s+\*\s+c$';

      $self->assert(qr/$expected/, $line, "Expected '$expected', got '$line'");

      if ($line =~ /$expected/) {
        my $remote_host = $1;
        my $filesz = $2;
        my $filename = $3;
        my $xfer_type = $4;
        my $user_name = $5;

        if ($^O eq 'darwin') {
          # MacOSX-specific hack, due to how it handles tmp files
          $test_file = ('/private' . $test_file);
        }

        $expected = '127.0.0.1';
        $self->assert($expected eq $remote_host,
          "Expected '$expected', got '$remote_host'");

        $expected = -s $test_file;
        $self->assert($expected == $filesz,
          "Expected '$expected', got '$filesz'");

        $expected = $test_file;
        $self->assert($expected eq $filename,
          "Expected '$expected', got '$filename'");

        $expected = 'b';
        $self->assert($expected eq $xfer_type,
          "Expected '$expected', got '$xfer_type'");

        $expected = $setup->{user};
        $self->assert($expected eq $user_name,
          "Expected '$expected', got '$user_name'");
      }

    } else {
      die("Can't read $xfer_log: $!");
    }
  };
  if ($@) {
    $ex = $@;
  }

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_scp_log_extlog_stor {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  my $ext_log = File::Spec->rel2abs("$tmpdir/ext.log");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10 sftp:20 scp:20 vroot:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    LogFormat => 'custom "%f"',
    ExtendedLog => "$ext_log WRITE custom",

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;
  require Net::SSH2;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->scp_put($setup->{config_file}, 'test.txt')) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't upload $setup->{config_file} to server: [$err_name] ($err_code) $err_str");
      }

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  eval {
    if (open(my $fh, "< $ext_log")) {
      my $line = <$fh>;
      chomp($line);
      close($fh);

      if ($ENV{TEST_VERBOSE}) {
        print STDERR "$line\n";
      }

      if ($^O eq 'darwin') {
        # MacOSX-specific hack, due to how it handles tmp files
        $test_file = ('/private' . $test_file);
      }

      $self->assert($test_file eq $line,
        test_msg("Expected '$test_file', got '$line'"));

    } else {
      die("Can't read $ext_log: $!");
    }
  };
  if ($@) {
    $ex = $@;
  }

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_scp_log_xferlog_stor {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $test_file = File::Spec->rel2abs("$tmpdir/test.txt");
  my $xfer_log = File::Spec->rel2abs("$tmpdir/xfer.log");

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'fsio:10 sftp:20 scp:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    TransferLog => $xfer_log,

    IfModules => {
      'mod_delay.c' => {
        DelayEngine => 'off',
      },

      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  my $ex;
  require Net::SSH2;

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_password($setup->{user}, $setup->{passwd})) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->scp_put($setup->{config_file}, 'test.txt')) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't upload $setup->{config_file} to server: [$err_name] ($err_code) $err_str");
      }

      $ssh2->disconnect();
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  eval {
    if (open(my $fh, "< $xfer_log")) {
      my $line = <$fh>;
      chomp($line);
      close($fh);

      if ($ENV{TEST_VERBOSE}) {
        print STDERR "$line\n";
      }

      my $expected = '^\S+\s+\S+\s+\d+\s+\d+:\d+:\d+\s+\d+\s+\d+\s+(\S+)\s+(\d+)\s+(\S+)\s+(\S+)\s+_\s+i\s+r\s+(\S+)\s+scp\s+0\s+\*\s+c$';

      $self->assert(qr/$expected/, $line, "Expected '$expected', got '$line'");

      if ($line =~ /$expected/) {
        my $remote_host = $1;
        my $filesz = $2;
        my $filename = $3;
        my $xfer_type = $4;
        my $user_name = $5;

        if ($^O eq 'darwin') {
          # MacOSX-specific hack, due to how it handles tmp files
          $test_file = ('/private' . $test_file);
        }

        $expected = '127.0.0.1';
        $self->assert($expected eq $remote_host,
          "Expected '$expected', got '$remote_host'");

        $expected = -s $test_file;
        $self->assert($expected == $filesz,
          "Expected '$expected', got '$filesz'");

        $expected = $test_file;
        $self->assert($expected eq $filename,
          "Expected '$expected', got '$filename'");

        $expected = 'b';
        $self->assert($expected eq $xfer_type,
          "Expected '$expected', got '$xfer_type'");

        $expected = $setup->{user};
        $self->assert($expected eq $user_name,
          "Expected '$expected', got '$user_name'");
      }

    } else {
      die("Can't read $xfer_log: $!");
    }
  };
  if ($@) {
    $ex = $@;
  }

  test_cleanup($setup->{log_file}, $ex);
}

sub vroot_alias_dir_sftp_publickey_issue30 {
  my $self = shift;
  my $tmpdir = $self->{tmpdir};
  my $setup = test_setup($tmpdir, 'vroot');

  my $src_dir = File::Spec->rel2abs("$tmpdir/foo.d");
  create_test_dir($setup, $src_dir);

  my $dst_dir = '~/bar.d';

  my $test_file1 = File::Spec->rel2abs("$tmpdir/foo.d/a.txt");
  create_test_file($setup, $test_file1);

  my $test_file2 = File::Spec->rel2abs("$tmpdir/foo.d/b.txt");
  create_test_file($setup, $test_file2);

  my $test_file3 = File::Spec->rel2abs("$tmpdir/foo.d/c.txt");
  create_test_file($setup, $test_file3);

  my $rsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_rsa_key");
  my $dsa_host_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/ssh_host_dsa_key");

  my $rsa_priv_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/test_rsa_key");
  my $rsa_pub_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/test_rsa_key.pub");
  my $rsa_rfc4716_key = File::Spec->rel2abs("$ENV{PROFTPD_TEST_DIR}/tests/t/etc/modules/mod_sftp/authorized_rsa_keys2");

  my $authorized_keys = File::Spec->rel2abs("$tmpdir/.authorized_keys");
  unless (copy($rsa_rfc4716_key, $authorized_keys)) {
    die("Can't copy $rsa_rfc4716_key to $authorized_keys: $!");
  }

  # Make sure that, if we're running as root, that the authorized_keys file has
  # permissions/privs set for the account we create
  if ($< == 0) {
    unless (chown($setup->{uid}, $setup->{gid}, $authorized_keys)) {
      die("Can't set owner of $authorized_keys to $setup->{uid}/$setup->{gid}: $!");
    }
  }

  $ENV{TEST_VERBOSE} = 1;

  my $config = {
    PidFile => $setup->{pid_file},
    ScoreboardFile => $setup->{scoreboard_file},
    SystemLog => $setup->{log_file},
    TraceLog => $setup->{log_file},
    Trace => 'auth:20 fsio:20 ssh2:20 vroot:20',

    AuthUserFile => $setup->{auth_user_file},
    AuthGroupFile => $setup->{auth_group_file},
    AuthOrder => 'mod_auth_file.c',

    IfModules => {
      'mod_sftp.c' => [
        "SFTPEngine on",
        "SFTPLog $setup->{log_file}",
        "SFTPHostKey $rsa_host_key",
        "SFTPHostKey $dsa_host_key",
        "SFTPAuthorizedUserKeys file:~/.authorized_keys",
      ],

      'mod_vroot.c' => {
        VRootEngine => 'on',
        VRootLog => $setup->{log_file},
        DefaultRoot => '~',

        VRootAlias => "$src_dir $dst_dir",
      },

      'mod_delay.c' => {
        DelayEngine => 'off',
      },
    },
  };

  my ($port, $config_user, $config_group) = config_write($setup->{config_file},
    $config);

  # Open pipes, for use between the parent and child processes.  Specifically,
  # the child will indicate when it's done with its test by writing a message
  # to the parent.
  my ($rfh, $wfh);
  unless (pipe($rfh, $wfh)) {
    die("Can't open pipe: $!");
  }

  require Net::SSH2;

  my $ex;

  # Ignore SIGPIPE
  local $SIG{PIPE} = sub { };

  # Fork child
  $self->handle_sigchld();
  defined(my $pid = fork()) or die("Can't fork: $!");
  if ($pid) {
    eval {
      my $ssh2 = Net::SSH2->new();

      sleep(1);

      unless ($ssh2->connect('127.0.0.1', $port)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't connect to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      unless ($ssh2->auth_publickey($setup->{user}, $rsa_pub_key,
          $rsa_priv_key)) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't login to SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $sftp = $ssh2->sftp();
      unless ($sftp) {
        my ($err_code, $err_name, $err_str) = $ssh2->error();
        die("Can't use SFTP on SSH2 server: [$err_name] ($err_code) $err_str");
      }

      my $dir = $sftp->opendir('/');
      unless ($dir) {
        my ($err_code, $err_name) = $sftp->error();
        die("Can't open directory '.': [$err_name] ($err_code)");
      }

      my $res = {};

      my $file = $dir->read();
      while ($file) {
        if ($ENV{TEST_VERBOSE}) {
          print STDERR "# READDIR: $file->{name}\n";
        }

        $res->{$file->{name}} = $file;
        $file = $dir->read();
      }

      my $expected = {
        '.' => 1,
        '..' => 1,
        '.authorized_keys' => 1,
        'foo.d' => 1,
        'bar.d' => 1,
        'vroot.conf' => 1,
        'vroot.group' => 1,
        'vroot.passwd' => 1,
        'vroot.pid' => 1,
        'vroot.scoreboard' => 1,
        'vroot.scoreboard.lck' => 1,
      };

      # To issue the FXP_CLOSE, we have to explicitly destroy the dirhandle
      $dir = undef;

      $ssh2->disconnect();

      my $ok = 1;
      my $mismatch;

      my $seen = [];
      foreach my $name (keys(%$res)) {
        push(@$seen, $name);

        unless (defined($expected->{$name})) {
          $mismatch = $name;
          $ok = 0;
          last;
        }
      }

      unless ($ok) {
        die("Unexpected name '$mismatch' appeared in READDIR data")
      }

      # Now remove from $expected all of the paths we saw; if there are
      # any entries remaining in $expected, something went wrong.
      foreach my $name (@$seen) {
        delete($expected->{$name});
      }

      my $remaining = scalar(keys(%$expected));
      $self->assert(0 == $remaining,
        test_msg("Expected 0, got $remaining"));
    };
    if ($@) {
      $ex = $@;
    }

    $wfh->print("done\n");
    $wfh->flush();

  } else {
    eval { server_wait($setup->{config_file}, $rfh) };
    if ($@) {
      warn($@);
      exit 1;
    }

    exit 0;
  }

  # Stop server
  server_stop($setup->{pid_file});
  $self->assert_child_ok($pid);

  test_cleanup($setup->{log_file}, $ex);
  delete($ENV{TEST_VERBOSE});
}

1;
