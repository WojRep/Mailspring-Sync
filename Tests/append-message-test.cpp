//
//  append-message-test.cpp — #325
//
//  Przeniesienie wiadomości między kontami: zadanie `AppendMessageTask` wgrywa
//  plik .eml do folderu konta docelowego (IMAP APPEND). Test czystych funkcji
//  z AppendMessage.hpp: odczyt parametrów zadania (brak folderu / pliku =
//  SyncException przez wartość, bez ponawiania), flagi IMAP, data.
//  Budowanie i uruchomienie: Tests/run-tests.sh
//

#include "AppendMessage.hpp"
#include "SyncException.hpp"

static int failures = 0;

static void check(bool ok, const char * name, const string & detail = "") {
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) failures++;
}

static void testParsesFullRequest() {
    json data = {{"folderPath", "Rodzina/Wakacje"}, {"filepath", "/tmp/m1.eml"},
                 {"unread", true}, {"starred", true}, {"date", 1700000000}};
    AppendRequest r = parseAppendRequest(data);
    check(r.folderPath == "Rodzina/Wakacje", "folderPath", r.folderPath);
    check(r.filepath == "/tmp/m1.eml", "filepath", r.filepath);
    check(r.unread && r.starred, "unread + starred");
    check(r.date == (time_t) 1700000000, "date");
}

static void testMissingFieldsThrowByValue() {
    const char * name = "brak folderPath/filepath -> SyncException przez wartość, bez ponawiania";
    for (json data : {json{{"filepath", "/tmp/x.eml"}}, json{{"folderPath", "INBOX"}}, json::object()}) {
        try {
            parseAppendRequest(data);
            check(false, name, "nie rzucono wyjątku dla " + data.dump());
        } catch (SyncException & e) {
            check(e.key == "invalid-append-task" && !e.isRetryable(), name, "key=" + e.key);
        } catch (...) {
            check(false, name, "inny typ wyjątku");
        }
    }
}

static void testFlags() {
    check(appendFlagsFor(false, false) == mailcore::MessageFlagSeen, "przeczytana -> \\Seen");
    check(appendFlagsFor(true, false) == mailcore::MessageFlagNone, "nieprzeczytana -> bez flag");
    check(appendFlagsFor(true, true) == mailcore::MessageFlagFlagged, "gwiazdka -> \\Flagged");
    check(appendFlagsFor(false, true) == (mailcore::MessageFlagSeen | mailcore::MessageFlagFlagged),
          "przeczytana + gwiazdka");
}

static void testDefaults() {
    json data = {{"folderPath", "INBOX"}, {"filepath", "/tmp/a.eml"}};
    AppendRequest r = parseAppendRequest(data);
    check(!r.unread && !r.starred, "domyślnie przeczytana, bez gwiazdki");
    check(r.date == (time_t) -1, "brak daty -> -1 (APPEND bez daty)");
    json zero = {{"folderPath", "INBOX"}, {"filepath", "/tmp/a.eml"}, {"date", 0}};
    check(parseAppendRequest(zero).date == (time_t) -1, "data 0 -> -1");
}

int main() {
    testParsesFullRequest();
    testMissingFieldsThrowByValue();
    testFlags();
    testDefaults();
    printf("%s (%d błędów)\n", failures == 0 ? "OK" : "BŁĄD", failures);
    return failures == 0 ? 0 : 1;
}
