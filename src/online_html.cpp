#include "online_html.hpp"
#include "online.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace reader { namespace online {
namespace {
bool space(char c) { return std::isspace(static_cast<unsigned char>(c)); }
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
void utf8(std::string& s, unsigned n) {
    if (!n || n > 0x10ffff || (n >= 0xd800 && n <= 0xdfff)) return;
    if (n < 128) s += static_cast<char>(n);
    else if (n < 2048) { s += static_cast<char>(0xc0 | (n >> 6)); s += static_cast<char>(0x80 | (n & 63)); }
    else if (n < 65536) { s += static_cast<char>(0xe0 | (n >> 12)); s += static_cast<char>(0x80 | ((n >> 6) & 63)); s += static_cast<char>(0x80 | (n & 63)); }
    else { s += static_cast<char>(0xf0 | (n >> 18)); s += static_cast<char>(0x80 | ((n >> 12) & 63)); s += static_cast<char>(0x80 | ((n >> 6) & 63)); s += static_cast<char>(0x80 | (n & 63)); }
}
std::string entities(const std::string& s) {
    std::string out;
    const std::map<std::string,std::string> named = {{"amp","&"},{"lt","<"},{"gt",">"},{"quot","\""},{"apos","'"},{"nbsp"," "},{"rsquo","’"},{"lsquo","‘"},{"rdquo","”"},{"ldquo","“"},{"mdash","—"},{"ndash","–"},{"hellip","…"}};
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '&') {
            const auto end = s.find(';', i + 1);
            if (end != std::string::npos && end - i < 20) {
                const auto key = s.substr(i + 1, end - i - 1);
                const auto found = named.find(key);
                if (found != named.end()) { out += found->second; i = end; continue; }
                if (key.size() > 1 && key[0] == '#') {
                    char* stop = nullptr;
                    const bool hex = key.size() > 2 && (key[1] == 'x' || key[1] == 'X');
                    auto n = std::strtoul(key.c_str() + (hex ? 2 : 1), &stop, hex ? 16 : 10);
                    if (stop && !*stop && n <= 0x10ffff) { utf8(out, static_cast<unsigned>(n)); i = end; continue; }
                }
            }
        }
        out += s[i];
    }
    return out;
}
struct Token { std::size_t begin, end; std::string tag; bool close = false, single = false; std::map<std::string,std::string> attrs; };
std::vector<Token> tokens(const std::string& html) {
    if (html.size() > max_response_bytes) throw std::runtime_error("HTML exceeds response limit.");
    std::vector<Token> out;
    for (std::size_t at = 0; at < html.size();) {
        at = html.find('<', at); if (at == std::string::npos) break;
        if (html.compare(at, 4, "<!--") == 0) {
            auto end = html.find("-->", at + 4);
            Token comment; comment.begin = at; comment.end = end == std::string::npos ? html.size() : end + 3;
            comment.tag = "!"; comment.single = true; out.push_back(comment);
            if (out.size() > 40000) throw std::runtime_error("HTML has too many elements.");
            at = comment.end; continue;
        }
        Token t; t.begin = at++;
        if (at < html.size() && html[at] == '/') { t.close = true; ++at; }
        const auto start = at;
        while (at < html.size() && (std::isalnum(static_cast<unsigned char>(html[at])) || html[at] == '-' || html[at] == ':')) ++at;
        if (at == start) continue;
        t.tag = lower(html.substr(start, at - start));
        while (at < html.size() && html[at] != '>') {
            if (space(html[at]) || html[at] == '/') { ++at; continue; }
            const auto a = at;
            while (at < html.size() && !space(html[at]) && html[at] != '=' && html[at] != '>') ++at;
            const auto key = lower(html.substr(a, at - a));
            while (at < html.size() && space(html[at])) ++at;
            std::string value;
            if (at < html.size() && html[at] == '=') {
                ++at; while (at < html.size() && space(html[at])) ++at;
                const char quote = at < html.size() && (html[at] == '\'' || html[at] == '"') ? html[at++] : 0;
                const auto v = at;
                while (at < html.size() && (quote ? html[at] != quote : !space(html[at]) && html[at] != '>')) ++at;
                value = entities(html.substr(v, at - v));
                if (quote && at < html.size()) ++at;
            }
            if (t.attrs.size() < 64) t.attrs[key] = std::move(value);
        }
        t.end = at < html.size() ? ++at : at;
        t.single = t.tag == "br" || t.tag == "img" || t.tag == "meta" || t.tag == "link" || t.tag == "input" || t.tag == "hr" || t.tag == "source" || (t.end >= 2 && html[t.end - 2] == '/');
        out.push_back(t);
        if (out.size() > 40000) throw std::runtime_error("HTML has too many elements.");
        if (!t.close && (t.tag == "script" || t.tag == "style")) {
            // Raw text may contain '<' or fake markup. Skip to its closing tag.
            auto end = html.find("</" + t.tag, at);
            at = end == std::string::npos ? html.size() : end;
        }
    }
    return out;
}
}
std::string html_text(const std::string& html) {
    const auto ts = tokens(html);
    std::string out;
    std::size_t at = 0;
    bool hidden = false;
    for (const auto& t : ts) {
        if (!hidden && t.begin >= at) out += html.substr(at, t.begin - at);
        if (t.tag == "script" || t.tag == "style") hidden = !t.close;
        if (!hidden && (t.tag == "p" || t.tag == "br" || t.tag == "div" || t.tag == "h1" || t.tag == "h2" || t.tag == "li")) out += '\n';
        at = t.end;
    }
    if (!hidden) out += html.substr(at);
    out = entities(out);
    std::string clean;
    bool pending = false;
    for (char c : out) {
        if (c == '\n' || c == '\r') { if (!clean.empty() && clean.back() != '\n') clean += '\n'; pending = false; }
        else if (space(c)) pending = true;
        else { if (pending && !clean.empty() && clean.back() != '\n') clean += ' '; clean += c; pending = false; }
    }
    while (!clean.empty() && clean.back() == '\n') clean.pop_back();
    return clean;
}
std::vector<HtmlNode> html_select(const std::string& html, const std::string& tag, const std::string& attr, const std::string& value) {
    const auto ts = tokens(html);
    std::vector<std::size_t> ends(ts.size());
    std::map<std::string,std::vector<std::size_t>> stacks;
    for (std::size_t i=0;i<ts.size();++i) {
        ends[i] = ts[i].end;
        auto& stack = stacks[ts[i].tag];
        if (ts[i].close) {
            if (!stack.empty()) { ends[stack.back()] = ts[i].begin; stack.pop_back(); }
        } else if (!ts[i].single) stack.push_back(i);
    }
    std::vector<HtmlNode> out;
    std::size_t selected_bytes = 0;
    for (std::size_t i = 0; i < ts.size(); ++i) {
        const auto& t = ts[i];
        if (t.close || (!tag.empty() && t.tag != tag)) continue;
        const auto a = t.attrs.find(attr);
        if (!attr.empty() && (a == t.attrs.end() || (!value.empty() && (attr == "class" ? (" " + a->second + " ").find(" " + value + " ") == std::string::npos : a->second != value)))) continue;
        const auto end = ends[i];
        selected_bytes += end - t.end;
        for (const auto& a : t.attrs) selected_bytes += a.first.size() + a.second.size();
        if (selected_bytes > 8 * 1024 * 1024) throw std::runtime_error("Selected HTML exceeds memory limit.");
        HtmlNode node; node.tag = t.tag; node.attrs = t.attrs;
        node.html = html.substr(t.end, end - t.end); node.text = html_text(node.html);
        out.push_back(std::move(node));
        if (out.size() >= 20000) throw std::runtime_error("Too many matching HTML elements.");
    }
    return out;
}
} }
