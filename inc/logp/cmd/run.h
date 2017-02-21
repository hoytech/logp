#pragma once

#include "logp/cmd/base.h"

namespace logp { namespace cmd {

class run : public base {
  public:
    const char *usage();
    const char *getopt_string();
    struct option *get_long_options();
    void process_option(int arg);
    void execute();

  private:
};

}}
