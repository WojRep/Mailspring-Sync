// Test stub: Query.cpp uses only MailUtils::qmarks (the real MailUtils.cpp needs the whole engine).
#include "MailUtils.hpp"
string MailUtils::qmarks(size_t count) {
    if (count == 0) return "";
    string q{"?"};
    for (size_t i = 1; i < count; i++) q += ",?";
    return q;
}
