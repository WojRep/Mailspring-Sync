//
//  mail-selection-test.cpp — #325
//
//  Zadania na poczcie (ChangeFolderTask itd.) z klienta niosą ZAWSZE oba pola:
//  `threadIds` i `messageIds` (puste tablice, gdy nieużywane). inflateMessages
//  wybierało `threadIds`, gdy pole ISTNIAŁO — więc zadanie na samych
//  wiadomościach (threadIds: []) nie robiło nic. Przy przenoszeniu między
//  kontami oryginały do Kosza idą po messageIds (tylko skopiowane wiadomości).
//

#include "MailSelection.hpp"

static int failures = 0;

static void check(bool ok, const char * name) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) failures++;
}

int main() {
    check(mailSelectionFor(json{{"threadIds", {"t1"}}, {"messageIds", json::array()}}) == MailSelection::Threads,
          "niepuste threadIds -> wątki");
    check(mailSelectionFor(json{{"threadIds", json::array()}, {"messageIds", {"m1", "m2"}}}) == MailSelection::Messages,
          "puste threadIds + niepuste messageIds -> wiadomości");
    check(mailSelectionFor(json{{"messageIds", {"m1"}}}) == MailSelection::Messages, "same messageIds -> wiadomości");
    check(mailSelectionFor(json{{"threadIds", json::array()}, {"messageIds", json::array()}}) == MailSelection::None,
          "oba puste -> nic");
    check(mailSelectionFor(json::object()) == MailSelection::None, "brak pól -> nic");
    check(mailSelectionFor(json{{"threadIds", "t1"}}) == MailSelection::None, "zły typ -> nic");
    printf("%s (%d błędów)\n", failures == 0 ? "OK" : "BŁĄD", failures);
    return failures == 0 ? 0 : 1;
}
