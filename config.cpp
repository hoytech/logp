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



struct parse_stack_frame {
    ssize_t indentation;
    nlohmann::json &e;
};


nlohmann::json try_parse(std::ifstream &file) {
    std::string line;

    uint64_t curr_lineno = 0;

    std::vector<parse_stack_frame> stack;

    nlohmann::json tree({});
    stack.emplace_back(parse_stack_frame{ 0, tree });

    while (std::getline(file, line)) {
        curr_lineno++;

        size_t start_of_token = line.find_first_not_of(" \t\v\r\f");
        if (start_of_token == std::string::npos || line[start_of_token] == '#') continue;
        ssize_t indentation = static_cast<ssize_t>(start_of_token);

        if (stack.back().indentation == -1) {
            if (indentation > stack[stack.size() - 1].indentation) {
                stack.back().indentation = indentation;
            } else {
                throw logp::error("under-indentation on line ", curr_lineno);
            }
        }

        if (indentation > stack.back().indentation) throw logp::error("over-indentation on line ", curr_lineno);
        while (indentation < stack.back().indentation) {
            stack.pop_back();
        }

        if (indentation != stack.back().indentation) throw logp::error("inconsistent indentation on line ", curr_lineno);

        size_t end_of_token = line.find_first_of(": \t\v\r\f", start_of_token);
        if (end_of_token == std::string::npos) throw logp::error("unable to parse token on line ", curr_lineno);
        std::string token(line, start_of_token, end_of_token-start_of_token);

        size_t start_of_val = line.find_first_not_of(": \t\v\r\f", end_of_token);
        std::string val;

        if (start_of_val != std::string::npos) {
            size_t end_of_val = line.find_last_not_of(" \t\v\r\f");
            val = std::string(line, start_of_val, end_of_val-start_of_val+1);
        }

        if (token == "-") {
            if (!val.size()) throw logp::error("empty array element on line ", curr_lineno);
            stack.back().e.push_back(val);
        } else {
            if (val.size()) {
                stack.back().e[token] = val;
            } else {
                stack.back().e[token] = nullptr;
                stack.emplace_back(parse_stack_frame{ -1, stack.back().e[token] });
            }
        }
    }

    return tree;
}


bool load_config_file(std::string path, config &c) {
    std::ifstream file;

    try {
        file.open(path);
    } catch (std::exception &e) {
        return false;
    }

    try {
        c.tree = try_parse(file);
    } catch (std::exception &e) {
        PRINT_ERROR << "Unable to parse config file '" << path << "': " << e.what();
        exit(1);
    }

    return true;
}



nlohmann::json *config::get_node(std::string name, nlohmann::json &j) {
    auto first_dot_pos = name.find_first_of('.');
    if (first_dot_pos == std::string::npos) {
        if (j.count(name)) return &j[name];
        return static_cast<nlohmann::json*>(nullptr);
    } else {
        std::string name_first = name.substr(0, first_dot_pos);
        if (j.count(name_first)) return get_node(name.substr(first_dot_pos+1), j[name_first]);
        return static_cast<nlohmann::json*>(nullptr);
    }
}


bool config::get_bool(std::string name, bool default_val) {
    auto *node = get_node(name);
    if (!node) return default_val;

    if (!node->is_string()) throw logp::error("unexpected value for config '", name, "': ", *node);

    std::string v = *node;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);

    if (v == "t" || v == "true" || v == "1") return true;
    else if (v == "f" || v == "false" || v == "0") return false;

    throw logp::error("couldn't parse config '", name, "' as boolean: ", v);
}


std::string config::get_str(std::string name, std::string default_val) {
    auto *node = get_node(name, tree);
    if (!node) return default_val;

    if (!node->is_string()) throw logp::error("non-string value for config '", name, "': ", *node);

    return *node;
}


uint64_t config::get_uint64(std::string name, uint64_t default_val) {
    auto *node = get_node(name, tree);
    if (!node) return default_val;

    if (!node->is_string()) throw logp::error("unexpected value for config '", name, "': ", *node);
    return std::stoull(node->get<std::string>());
}


}
