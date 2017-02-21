#pragma once

#include <getopt.h>
#include <string.h>


namespace logp { namespace cmd {

class base {
  public:
    base() {}
    void parse_params(int argc, char **argv);

    virtual const char *usage() =0;
    virtual const char *getopt_string() =0;
    virtual struct option *get_long_options() =0;
    virtual void process_option(int arg) =0;
    virtual void execute() =0;

  protected:
    void print_usage_and_exit();

    char **my_argv;
};

}}
