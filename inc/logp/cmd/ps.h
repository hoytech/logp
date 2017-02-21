#pragma once

#include "logp/cmd/base.h"

namespace logp { namespace cmd {

class ps : public base {
  public:
    const char *usage();
    const char *getopt_string();
    struct option *get_long_options();
    void process_option(int arg, int option_index);
    void execute();

  private:
    bool follow = false;
};

}}
