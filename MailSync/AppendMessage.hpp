//
//  AppendMessage.hpp — #325
//
//  Czyste funkcje zadania `AppendMessageTask` (wgranie pliku .eml do folderu
//  konta docelowego przy przenoszeniu wiadomości między kontami). Tylko nagłówek:
//  nie wymaga zmian w projekcie Xcode, testowany w Tests/append-message-test.cpp.
//

#ifndef AppendMessage_hpp
#define AppendMessage_hpp

#include <string>
#include <ctime>
#include "json.hpp"
#include "SyncException.hpp"
#include <MailCore/MCMessageConstants.h>

using json = nlohmann::json;
using namespace std;

struct AppendRequest {
    string folderPath;
    string filepath;
    bool unread = false;
    bool starred = false;
    /** (time_t) -1 = APPEND bez daty (serwer ustawia bieżącą). */
    time_t date = (time_t) -1;
};

/** Parametry z danych zadania. Brak folderu albo pliku = błąd bez ponawiania. */
inline AppendRequest parseAppendRequest(const json & data) {
    auto str = [&](const char * key) -> string {
        if (data.count(key) && data[key].is_string()) return data[key].get<string>();
        return "";
    };
    AppendRequest r;
    r.folderPath = str("folderPath");
    r.filepath = str("filepath");
    if (r.folderPath.empty() || r.filepath.empty()) {
        throw SyncException("invalid-append-task", "AppendMessageTask requires folderPath and filepath.", false);
    }
    r.unread = data.count("unread") && data["unread"].is_boolean() && data["unread"].get<bool>();
    r.starred = data.count("starred") && data["starred"].is_boolean() && data["starred"].get<bool>();
    if (data.count("date") && data["date"].is_number()) {
        double d = data["date"].get<double>();
        if (d > 0) r.date = (time_t) d;
    }
    return r;
}

/** Flagi IMAP kopii: przeczytana -> \Seen, gwiazdka -> \Flagged. */
inline mailcore::MessageFlag appendFlagsFor(bool unread, bool starred) {
    int flags = mailcore::MessageFlagNone;
    if (!unread) flags |= mailcore::MessageFlagSeen;
    if (starred) flags |= mailcore::MessageFlagFlagged;
    return (mailcore::MessageFlag) flags;
}

#endif /* AppendMessage_hpp */
