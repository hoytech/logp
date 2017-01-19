#pragma once

#include <openssl/x509v3.h>
#include <openssl/ssl.h>

namespace openssl {

int validate_hostname(const char *hostname, const X509 *server_cert);

};
