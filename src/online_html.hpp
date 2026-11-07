#pragma once
#include <string>
#include <map>
#include <vector>
namespace reader { namespace online {
struct HtmlNode {
    std::string tag, html, text;
    std::map<std::string, std::string> attrs;
};
std::string html_text(const std::string& html);
std::vector<HtmlNode> html_select(const std::string& html, const std::string& tag,
                                const std::string& attr, const std::string& value);
} }
