#!/usr/bin/env perl

use strict;

use lib 'logp-buildlib/lib/';
use LogPeriodic::BuildLib;


my $cmd = shift || die "need command";

my $version;

if ($cmd =~ /^dist-/ || $cmd =~ /^upload-/) {
  $version = LogPeriodic::BuildLib::get_version('logp');
  ok_to_release($version);
}

if ($cmd eq 'dist-linux') {
  sys(q{make clean}) unless $ENV{NO_CLEAN};
  sys(q{make -j 4 XCXXFLAGS="-DSSL_PIN_CAFILE" XLDFLAGS="-Wl,-rpath=/usr/logp/lib/ -static-libgcc -static-libstdc++ -s" });
  sys(q{strip logp logp_preload.so});

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

  sys(q{mkdir dist/amd64});
  sys(q{mv dist/*.deb dist/amd64});

  sys(q{dpkg-scanpackages dist/amd64/ > dist/amd64/Packages});
  sys(q{apt-ftparchive release dist/amd64/ > dist/amd64/Release});
  sys(q{gpg -ab < dist/amd64/Release > dist/amd64/Release.gpg});

  sys(q{gpg --export -a 'Doug Hoyte' > dist/logp-gpg-key.public});
  sys(q{createrepo --database dist/});
} elsif ($cmd eq 'dist-macos') {
  sys(q{make clean});
  ## FIXME: figure out how to pin CAFILE for Mac OS
  sys(q{ make -j 4 XCXXFLAGS="-I/usr/local/opt/openssl/include" XLDFLAGS="-L/usr/local/opt/openssl/lib" });
  mkdir('dist');
  sys(qq{ tar -czf dist/logp-$version.bottle.tar.gz logp });
} elsif ($cmd eq 'upload-s3') {
  sys(q{aws s3 sync --delete dist/ s3://logp/});
  sys(qq{aws s3 cp s3://logp/logp_${version}_amd64.deb s3://logp/logp_latest_amd64.deb});
  sys(qq{aws s3 cp s3://logp/logp-${version}-1.x86_64.rpm s3://logp/logp-latest.x86_64.rpm});
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
