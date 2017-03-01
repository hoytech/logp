#!/usr/bin/env perl

use strict;

use lib 'logp-buildlib/lib/';
use LogPeriodic::BuildLib;


my $cmd = shift || die "need command";

if ($cmd eq 'dist-linux') {
  my $version = LogPeriodic::BuildLib::get_version('logp');
  ok_to_release($version);

  sys(q{make clean});
  sys(q{make -j 4 LDFLAGS="-Wl,-rpath=/usr/logp/lib/ -static-libgcc -static-libstdc++ -s" XCXXFLAGS="-DSSL_PIN_CAFILE"});

  LogPeriodic::BuildLib::fpm({
    types => [qw/ deb rpm /],
    name => 'logp',
    version => $version,
    files => {
      'logp' => '/usr/bin/logp',
      '/lib/x86_64-linux-gnu/libssl.so.1.0.0' => '/usr/logp/lib/libssl.so.1.0.0',
      '/lib/x86_64-linux-gnu/libcrypto.so.1.0.0' => '/usr/logp/lib/libcrypto.so.1.0.0',
      'ssl/letsencrypt.pem' => '/usr/logp/ssl/letsencrypt.pem',
    },
    deps_deb => [qw/
    /],
    deps_rpm => [qw/
    /],
    description => 'Command-line client for Log Periodic',
  });
} else {
  die "unknown command: $cmd";
}



sub ok_to_release {
  my $version = shift;

  if (length(`git diff`)) {
    die "won't release from tree with uncomitted changes";
  }

  if ($version =~ /-/) {
    die "won't release untagged version: $version";
  }
}
