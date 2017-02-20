#pragma once

#include "logp/cmd/base.h"

namespace logp { namespace cmd {

class ps : public base {
  public:
    const char *usage();
    struct option *get_long_options();
    void process_option(int arg);
    void execute();

  private:
    bool follow = false;
};

}}
