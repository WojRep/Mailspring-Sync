//
//  dav-log-guard-test.cpp — #275: strażnik regresji logowania CalDAV/CardDAV
//
//  Przeszukuje źródła DAV (DAVWorker.cpp, DAVUtils.cpp, DavXML.cpp) i zgłasza
//  każde wywołanie logu (`logger->…(…)`) oraz każdy `SyncException(…)`, które
//  przekazują dalej surową wartość wyglądającą na treść vCard/ICS, URL/href
//  zasobu, ETag/ctag, token synchronizacji albo ładunek XML.
//
//  Argument jest dozwolony, gdy przechodzi przez funkcję z DAVLogRedaction.hpp
//  (davRedact…, davLogHandle) albo jest licznikiem (`….size()`).
//
//  Ścieżka do katalogu MailSync: argv[1].
//

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace std;

static int failures = 0;

static string lower(string s) {
    for (auto & c : s) c = (char)tolower((unsigned char)c);
    return s;
}
static string trim(const string & s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    return a == string::npos ? "" : s.substr(a, b - a + 1);
}

// Argumenty wywołania od nawiasu otwierającego `open`; puste, gdy nie da się sparsować.
static vector<string> callArgs(const string & src, size_t open, size_t & endOut) {
    vector<string> args;
    string cur;
    int depth = 0;
    bool inStr = false, inChar = false;
    for (size_t i = open; i < src.size(); i++) {
        char c = src[i];
        if (inStr || inChar) {
            cur += c;
            if (c == '\\' && i + 1 < src.size()) { cur += src[++i]; continue; }
            if (inStr && c == '"') inStr = false;
            if (inChar && c == '\'') inChar = false;
            continue;
        }
        if (c == '"') { inStr = true; cur += c; continue; }
        if (c == '\'') { inChar = true; cur += c; continue; }
        if (c == '(' || c == '[' || c == '{') {
            depth++;
            if (depth == 1) continue;
        } else if (c == ')' || c == ']' || c == '}') {
            depth--;
            if (depth == 0) {
                args.push_back(trim(cur));
                endOut = i;
                return args;
            }
        } else if (c == ',' && depth == 1) {
            args.push_back(trim(cur));
            cur.clear();
            continue;
        }
        cur += c;
    }
    endOut = src.size();
    return {};
}

// Usuwa literały napisów i wywołania davRedact…(…) / davLogHandle(…) — to, co
// zostanie, to wartości przekazywane do logu bez redakcji.
static string unredactedPart(const string & arg) {
    string out;
    for (size_t i = 0; i < arg.size(); i++) {
        if (arg[i] == '"') {
            for (i++; i < arg.size() && arg[i] != '"'; i++) {
                if (arg[i] == '\\') i++;
            }
            continue;
        }
        if (arg.compare(i, 9, "davRedact") == 0 || arg.compare(i, 12, "davLogHandle") == 0) {
            size_t open = arg.find('(', i);
            if (open == string::npos) break;
            int depth = 0;
            size_t j = open;
            for (; j < arg.size(); j++) {
                if (arg[j] == '(') depth++;
                else if (arg[j] == ')' && --depth == 0) break;
            }
            i = j;
            continue;
        }
        out += arg[i];
    }
    return out;
}

static bool sensitive(const string & raw) {
    const string arg = trim(unredactedPart(raw));
    if (arg.empty()) return false;
    if (arg.size() >= 7 && arg.compare(arg.size() - 7, 7, ".size()") == 0) return false;
    if (arg == "false" || arg == "true") return false;
    const string l = lower(arg);
    static const char * needles[] = {"ics", "vcard", "vcf", "etag", "ctag", "href", "url", "token",
                                     "payload", "entry", "debuginfo", "xml", "resp", "body"};
    for (const char * n : needles) {
        if (l.find(n) != string::npos) return true;
    }
    return false;
}

static int lineOf(const string & src, size_t pos) {
    int line = 1;
    for (size_t i = 0; i < pos && i < src.size(); i++) if (src[i] == '\n') line++;
    return line;
}

static void scanFile(const string & path, const string & label) {
    ifstream in(path);
    if (!in) {
        printf("FAIL nie można otworzyć %s\n", path.c_str());
        failures++;
        return;
    }
    stringstream ss;
    ss << in.rdbuf();
    const string src = ss.str();

    int offenders = 0;
    for (const string & pattern : {string("logger->"), string("SyncException(")}) {
        size_t pos = 0;
        while ((pos = src.find(pattern, pos)) != string::npos) {
            size_t open = src.find('(', pos + pattern.size() - (pattern.back() == '(' ? 1 : 0));
            pos += pattern.size();
            if (open == string::npos) break;
            // logger->info( — nawias bezpośrednio po nazwie metody
            if (pattern == "logger->") {
                size_t k = pos;
                while (k < src.size() && (isalnum((unsigned char)src[k]) || src[k] == '_')) k++;
                if (k != open) continue;
            }
            size_t end = 0;
            vector<string> args = callArgs(src, open, end);
            for (size_t a = 0; a < args.size(); a++) {
                if (pattern == "logger->" && a == 0) continue; // format
                if (sensitive(args[a])) {
                    printf("FAIL %s:%d przekazuje do logu surowe: %s\n", label.c_str(), lineOf(src, open),
                           args[a].c_str());
                    offenders++;
                }
            }
        }
    }
    if (!offenders) printf("PASS %s: logi i wyjątki bez surowych vCard/ICS/URL/ETag\n", label.c_str());
    failures += offenders;
}

int main(int argc, char ** argv) {
    if (argc < 2) {
        printf("FAIL użycie: dav-log-guard-test <katalog MailSync>\n");
        return 1;
    }
    const string dir = argv[1];

    // --- samokontrola reguły (żeby strażnik nie był zawsze zielony)
    {
        auto selfCheck = [](bool ok, const char * name) {
            printf("%s %s\n", ok ? "PASS" : "FAIL", name);
            if (!ok) failures++;
        };
        selfCheck(sensitive("icsData"), "reguła: icsData jest wrażliwe");
        selfCheck(sensitive("vcardString"), "reguła: vcardString jest wrażliwe");
        selfCheck(sensitive("existingEtag"), "reguła: existingEtag jest wrażliwe");
        selfCheck(sensitive("ab->url()"), "reguła: ab->url() jest wrażliwe");
        selfCheck(sensitive("href"), "reguła: href jest wrażliwe");
        selfCheck(!sensitive("davRedactURL(url)"), "reguła: davRedactURL(url) dozwolone");
        selfCheck(!sensitive("davRedactETag(etag)"), "reguła: davRedactETag(etag) dozwolone");
        selfCheck(!sensitive("deletedIcsUIDs.size()"), "reguła: licznik .size() dozwolony");
        selfCheck(!sensitive("e.key"), "reguła: e.key (kod HTTP) dozwolony");
        selfCheck(!sensitive("host"), "reguła: host dozwolony");
        selfCheck(!sensitive("\"etag-conflict\""), "reguła: literał dozwolony");
        selfCheck(!sensitive("\"payload \" + davRedactPayload(xml)"), "reguła: literał + davRedactPayload dozwolone");
        selfCheck(sensitive("\"payload \" + xml"), "reguła: literał + surowy xml wrażliwe");
    }

    scanFile(dir + "/DAVWorker.cpp", "DAVWorker.cpp");
    scanFile(dir + "/DAVUtils.cpp", "DAVUtils.cpp");
    scanFile(dir + "/DavXML.cpp", "DavXML.cpp");

    printf("%s (%d błędów)\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
