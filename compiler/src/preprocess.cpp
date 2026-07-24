/*
 * MVX — a native compiler and runtime for Pick/MultiValue BASIC.
 * Copyright (C) 2026 Gordon Heydon.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.  There is NO WARRANTY, to
 * the extent permitted by law; see the LICENSE file for details.
 *
 * SPDX-License-Identifier: GPL-2.0-only
 */

#include "preprocess.h"
#include "parser.h"   // CompileError

#include <cctype>
#include <set>
#include <sstream>
#include <vector>

namespace mvx {

namespace {

std::string upper(std::string s) {
    for (char &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
bool identStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
bool identChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

// Replace whole-word macro names with their values, leaving string
// literals ("..." and '...') untouched.
std::string substitute(const std::string &line,
                       const std::map<std::string, std::string> &macros) {
    if (macros.empty()) return line;
    std::string out;
    out.reserve(line.size());
    char quote = 0;
    for (size_t i = 0, n = line.size(); i < n;) {
        char c = line[i];
        if (quote) {
            out += c;
            if (c == quote) quote = 0;
            i++;
        } else if (c == '"' || c == '\'') {
            quote = c;
            out += c;
            i++;
        } else if (identStart(c)) {
            size_t j = i;
            while (j < n && identChar(line[j])) j++;
            std::string id = line.substr(i, j - i);
            auto it = macros.find(id);
            out += (it != macros.end()) ? it->second : id;
            i = j;
        } else {
            out += c;
            i++;
        }
    }
    return out;
}

} // namespace

std::string preprocess(const std::string &src, const std::string &item,
                       const std::map<std::string, std::string> &predefined) {
    std::set<std::string> defined;
    std::map<std::string, std::string> macros;
    for (const auto &kv : predefined) {
        defined.insert(kv.first);
        if (!kv.second.empty()) macros[kv.first] = kv.second;
    }

    struct Frame {
        bool parentActive;
        bool active;
        bool taken;   // has any branch of this conditional been active
    };
    std::vector<Frame> stack;
    auto active = [&]() { return stack.empty() ? true : stack.back().active; };

    std::ostringstream out;
    std::istringstream in(src);
    std::string line;
    int lineno = 0;
    while (std::getline(in, line)) {
        lineno++;
        size_t p = 0;
        while (p < line.size() && (line[p] == ' ' || line[p] == '\t')) p++;

        if (p < line.size() && line[p] == '$') {
            size_t d = p + 1;
            while (d < line.size() && (line[d] == ' ' || line[d] == '\t')) d++;
            size_t w = d;
            while (w < line.size() && identChar(line[w])) w++;
            std::string dir = upper(line.substr(d, w - d));

            size_t a = w;
            while (a < line.size() && (line[a] == ' ' || line[a] == '\t')) a++;
            std::string rest = line.substr(a);
            while (!rest.empty() && (rest.back() == '\r' || rest.back() == ' ' ||
                                     rest.back() == '\t'))
                rest.pop_back();
            std::string name, value;
            size_t k = 0;
            while (k < rest.size() && rest[k] != ' ' && rest[k] != '\t')
                name += rest[k++];
            while (k < rest.size() && (rest[k] == ' ' || rest[k] == '\t')) k++;
            value = rest.substr(k);

            if (dir == "DEFINE") {
                if (active()) {
                    if (name.empty())
                        throw CompileError(item, lineno, "$DEFINE needs a name");
                    defined.insert(name);
                    if (!value.empty()) macros[name] = value;
                    else macros.erase(name);
                }
            } else if (dir == "UNDEFINE" || dir == "UNDEF") {
                if (active()) { defined.erase(name); macros.erase(name); }
            } else if (dir == "IFDEF" || dir == "IFNDEF") {
                if (name.empty())
                    throw CompileError(item, lineno, "$" + dir + " needs a name");
                bool parent = active();
                bool cond = defined.count(name) > 0;
                if (dir == "IFNDEF") cond = !cond;
                bool on = parent && cond;
                stack.push_back({parent, on, on});
            } else if (dir == "ELSE") {
                if (stack.empty())
                    throw CompileError(item, lineno, "$ELSE without $IFDEF");
                Frame &f = stack.back();
                f.active = f.parentActive && !f.taken;
                if (f.active) f.taken = true;
            } else if (dir == "ENDIF") {
                if (stack.empty())
                    throw CompileError(item, lineno, "$ENDIF without $IFDEF");
                stack.pop_back();
            } else {
                throw CompileError(item, lineno, "unknown directive $" + dir);
            }
            out << "\n";                       // blank the directive line
            continue;
        }

        if (active()) out << substitute(line, macros) << "\n";
        else out << "\n";                      // blank the inactive line
    }
    if (!stack.empty())
        throw CompileError(item, lineno, "unterminated $IFDEF / $IFNDEF");
    return out.str();
}

} // namespace mvx
