#pragma once

#include <sstream>

#include "logp/config.h"

namespace logp {

class printer {
  public:
    ~printer() {
        fprintf(stderr, "logp: %s\n", os.str().c_str());
    }
    std::ostringstream &get() {
        return os;
    }
  private:
    std::ostringstream os;
};

#define PRINT_ERROR if (::conf.verbosity < -1) {} else logp::printer().get()
#define PRINT_INFO if (::conf.verbosity < 0) {} else logp::printer().get()
#define PRINT_DEBUG if (::conf.verbosity < 1) {} else logp::printer().get()

}
