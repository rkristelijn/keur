/**
 * @file scan/json_util.cpp
 * @brief Implementation of the minimal JSON field extractors. @implements
 *        KEUR-SCAN-DISPATCH
 */
#include "scan/json_util.h"

#include <re2/re2.h>

namespace keur::scan::detail {

std::string json_str(const std::string& slice, const std::string& key) {
    std::string out;
    // "key" : "..."  — capture the (possibly escaped) contents.
    RE2 re("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
    if (RE2::PartialMatch(slice, re, &out)) return out;
    return "";
}

int json_int(const std::string& slice, const std::string& key) {
    int out = 0;
    RE2 re("\"" + key + "\"\\s*:\\s*(-?[0-9]+)");
    if (RE2::PartialMatch(slice, re, &out)) return out;
    return 0;
}

std::vector<std::string> object_slices(const std::string& json, std::size_t from) {
    std::vector<std::string> out;
    int depth = 0;
    std::size_t start = std::string::npos;
    bool in_str = false;
    bool esc = false;
    for (std::size_t i = from; i < json.size(); ++i) {
        char c = json[i];
        if (in_str) {
            if (esc) esc = false;
            else if (c == '\\') esc = true;
            else if (c == '"') in_str = false;
            continue;
        }
        if (c == '"') { in_str = true; continue; }
        if (c == '{') { if (depth == 0) start = i; ++depth; }
        else if (c == '}') {
            --depth;
            if (depth == 0 && start != std::string::npos) {
                out.push_back(json.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return out;
}

}  // namespace keur::scan::detail
