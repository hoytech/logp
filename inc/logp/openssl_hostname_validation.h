#pragma once

#include <openssl/x509v3.h>
#include <openssl/ssl.h>

namespace openssl {

bool validate_hostname(const char *hostname, const X509 *server_cert);

};
