#pragma once

#include <sstream>

#include "logp/config.h"

namespace logp {

class printer {
  public:
    ~printer() {
        fprintf(stderr, "logp: %s\n", stringstream.str().c_str());
    }
    std::ostringstream &get() {
        return stringstream;
    }
  private:
    std::ostringstream stringstream;
};

#define PRINT_ERROR if (::conf.verbosity >= -1) logp::printer().get() << "error: "
#define PRINT_WARNING if (::conf.verbosity >= 0) logp::printer().get() << "warning: "
#define PRINT_INFO if (::conf.verbosity >= 1) logp::printer().get() << "info: "
#define PRINT_DEBUG if (::conf.verbosity >= 2) logp::printer().get() << "debug: "

}
