#!/usr/bin/env perl

use strict;

use lib 'logp-buildlib/lib/';
use LogPeriodic::BuildLib;


my $cmd = shift || die "need command";

if ($cmd eq 'dist-linux') {
  sys(q{make realclean});
  sys(q{make -j 4 LDFLAGS="-static-libgcc -static-libstdc++ -s"});

  my $version = LogPeriodic::BuildLib::get_version('logp');

  LogPeriodic::BuildLib::fpm({
    types => [qw/ deb rpm /],
    name => 'logp',
    version => $version,
    files => {
      'logp' => '/usr/bin/logp',
    },
    deps => [qw/
      libssl1.0.0
    /],
    description => 'Command-line client for Log Periodic',
  });
} else {
  die "unknown command: $cmd";
}
