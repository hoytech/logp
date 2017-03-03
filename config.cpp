#include <stdlib.h>

#include <algorithm>
#include <iostream>
#include <string>
#include <stdexcept>
#include <fstream>
#include <vector>

#include "logp/config.h"
#include "logp/util.h"


namespace logp {



struct config_entry {
  std::string orig_line;
  std::string k;
  std::string v;
  uint64_t lineno;
};

using parsed_config_file = std::vector<config_entry>;

static parsed_config_file try_parse(std::ifstream &file) {
  std::string line;
  std::istringstream sin;

  parsed_config_file output;

  uint64_t curr_lineno = 0;

  while (std::getline(file, line)) {
    curr_lineno++;

    std::size_t start_of_key = line.find_first_not_of(" \t\v\r\f");
    if (start_of_key == std::string::npos || line[start_of_key] == '#') continue;

    std::size_t end_of_key = line.find_first_of(": \t\v\r\f", start_of_key);
    if (end_of_key == std::string::npos) throw logp::error("unable to find : separator on line ", curr_lineno);

    std::string key(line, start_of_key, end_of_key-start_of_key);

    std::size_t start_of_val = line.find_first_not_of(": \t\v\r\f", end_of_key);
    if (start_of_val == std::string::npos) throw logp::error("unable to find value on line ", curr_lineno);

    std::size_t end_of_val = line.find_last_not_of(" \t\v\r\f");

    std::string val(line, start_of_val, end_of_val-start_of_val+1);

    config_entry ent;
    ent.orig_line = std::move(line);
    ent.k = std::move(key);
    ent.v = std::move(val);
    ent.lineno = curr_lineno;

    output.emplace_back(std::move(ent));
  }

  return output;
}



static bool parse_bool(config_entry &e) {
    std::string v = e.v;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);

    if (v == "t" || v == "true" || v == "1") return true;
    else if (v == "f" || v == "false" || v == "0") return false;

    throw logp::error("unable to parse boolean value for '", e.k, "' on line ", e.lineno);
}



static void try_extract(std::string &path, parsed_config_file &parsed, config &c) {
    for (auto &ent : parsed) {
        if (ent.k == "endpoint") {
            c.endpoint = ent.v;
        } else if (ent.k == "apikey") {
            c.apikey = ent.v;
        } else if (ent.k == "tls_no_verify") {
            c.tls_no_verify = parse_bool(ent);
        } else {
            PRINT_INFO << "warning: Unrecognized config option (" + ent.k + ") in file " + path;
        }
    }
}



bool load_config_file(std::string path, config &c) {
    std::ifstream file;

    try {
        file.open(path);
    } catch (std::exception &e) {
        return false;
    }

    try {
        parsed_config_file parsed = try_parse(file);
        try_extract(path, parsed, c);
    } catch (std::exception &e) {
        PRINT_ERROR << "Unable to parse config file '" << path << "': " << e.what();
        exit(1);
    }

    return true;
}

}
