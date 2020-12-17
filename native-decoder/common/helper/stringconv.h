#pragma once

#include <string>

namespace common {

class StringConv {
public:
    static std::wstring StringToWString(const std::string &str) {
        std::wstring wstr(str.length(), L' ');
        std::copy(str.begin(), str.end(), wstr.begin());
        return wstr;
    }

    //只拷贝低字节至string中
    static std::string WStringToString(const std::wstring &wstr) {
        std::string str(wstr.length(), ' ');
        std::copy(wstr.begin(), wstr.end(), str.begin());
        return str;
    }
};

} // namespace common