#pragma once

#include "logp/cmd/base.h"

namespace logp { namespace cmd {

class ping : public base {
  public:
    const char *usage();
    const char *getopt_string();
    struct option *get_long_options();
    void process_option(int arg, int option_index, char *optarg);
    void execute();

  private:
    uint64_t count = 0;
};

}}
