#pragma once
#include <charconv>
#include <cstddef>
#include <string>
#include <string_view>

// Mailbox text tokens: lowercase hex of the UTF-8 bytes, `-` for the empty string, at most 550 bytes.
// Shared by the `.windows` parser and LiveControls (notification targets).
namespace hextoken {
inline constexpr char digits[]="0123456789abcdef";
inline constexpr std::size_t maxToken=1100;
inline bool validHex(std::string_view token) {
    return token=="-" || (!token.empty() && token.size()<=maxToken && token.size()%2==0 &&
        token.find_first_not_of(digits)==std::string_view::npos);
}
// Invalid tokens and `-` decode to the empty string.
inline std::string decodeHex(std::string_view token) {
    if(token=="-" || !validHex(token)) return {};
    std::string text;
    for(std::size_t i=0;i<token.size();i+=2) {
        unsigned value=0; std::from_chars(token.data()+i, token.data()+i+2, value, 16);
        text+=char(value);
    }
    return text;
}
inline std::string encodeHex(std::string_view text) {
    if(text.empty()) return "-";
    std::string hex;
    for(unsigned char c:text) { hex+=digits[c>>4]; hex+=digits[c&15]; }
    return hex;
}
}
