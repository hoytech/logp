#!/usr/bin/env perl

use strict;

use lib 'logp-buildlib/lib/';
use LogPeriodic::BuildLib;


my $cmd = shift || die "need command";

my $version;

if ($cmd =~ /^dist-/) {
  $version = LogPeriodic::BuildLib::get_version('logp');
  ok_to_release($version);
}

if ($cmd eq 'dist-linux') {
  sys(q{make clean});
  sys(q{make -j 4 XCXXFLAGS="-DSSL_PIN_CAFILE" XLDFLAGS="-Wl,-rpath=/usr/logp/lib/ -static-libgcc -static-libstdc++ -s" });

  LogPeriodic::BuildLib::fpm({
    types => [qw/ deb rpm /],
    name => 'logp',
    version => $version,
    files => {
      'logp' => '/usr/bin/logp',
      'logp_preload.so' => '/usr/logp/lib/logp_preload.so',
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
} elsif ($cmd eq 'dist-macos') {
  sys(q{make clean});
  ## FIXME: figure out how to pin CAFILE for Mac OS
  sys(q{ make -j 4 XCXXFLAGS="-I/usr/local/opt/openssl/include" XLDFLAGS="-L/usr/local/opt/openssl/lib" });
  mkdir('dist');
  sys(qq{ tar -czf dist/logp-$version.bottle.tar.gz logp });
} else {
  die "unknown command: $cmd";
}



sub ok_to_release {
  my $version = shift;

  return if $ENV{FORCE_RELEASE};

  if (length(`git diff`)) {
    die "won't release from tree with uncomitted changes (override with FORCE_RELEASE env var)";
  }

  if ($version =~ /-/) {
    die "won't release untagged version: $version (override with FORCE_RELEASE env var)";
  }
}
