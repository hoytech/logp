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
    virtual void process_option(int arg, int option_index, char *optarg) =0;
    virtual void execute() =0;

  protected:
    void print_usage_and_exit();

    char **my_argv;
    std::vector<struct option> my_long_options;
};

}}
