//
//  MailSelection.hpp — #325
//
//  Czy zadanie na poczcie dotyczy wątków, czy pojedynczych wiadomości. Klient
//  wysyła zawsze oba pola (`threadIds`, `messageIds`), puste gdy nieużywane —
//  rozstrzyga NIEPUSTE pole. Tylko nagłówek (bez zmian w projekcie Xcode).
//

#ifndef MailSelection_hpp
#define MailSelection_hpp

#include "json.hpp"

using json = nlohmann::json;

enum class MailSelection { Threads, Messages, None };

inline MailSelection mailSelectionFor(const json & data) {
    auto nonEmpty = [&](const char * key) {
        return data.count(key) && data[key].is_array() && !data[key].empty();
    };
    if (nonEmpty("threadIds")) return MailSelection::Threads;
    if (nonEmpty("messageIds")) return MailSelection::Messages;
    return MailSelection::None;
}

#endif /* MailSelection_hpp */
