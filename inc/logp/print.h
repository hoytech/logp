#pragma once

#include <sstream>

#include "logp/config.h"
#include "logp/util.h"

namespace logp {

class printer {
  public:
    printer(int colour_=0) : colour(colour_) {}
    ~printer() {
        std::string s(stringstream.str());
        if (colour == 1) s = logp::util::colour_red(s);
        fprintf(stderr, "logp: %s\n", s.c_str());
    }
    std::ostringstream &get() {
        return stringstream;
    }
  private:
    std::ostringstream stringstream;
    int colour = 0;
};

#define PRINT_ERROR if (::conf.verbosity >= -1) logp::printer(1).get() << "error: "
#define PRINT_WARNING if (::conf.verbosity >= 0) logp::printer().get() << "warning: "
#define PRINT_INFO if (::conf.verbosity >= 1) logp::printer().get() << "info: "
#define PRINT_DEBUG if (::conf.verbosity >= 2) logp::printer().get() << "debug: "

}
