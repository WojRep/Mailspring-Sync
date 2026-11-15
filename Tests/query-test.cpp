//
//  query-test.cpp — #327
//
//  Query trzymał warunki w obiekcie JSON z kluczem = nazwa kolumny, więc
//  `.gte("remoteUID", a).lt("remoteUID", b)` dawało samo `remoteUID < b`.
//  Przy obsłudze VANISHED (QRESYNC) zakres „zniknęły UID 591–622” odłączał
//  WSZYSTKIE wiadomości poniżej 622 — tak zniknęło ok. 2000 wiadomości z INBOX.
//

#include "Query.hpp"
#include <SQLiteCpp/SQLiteCpp.h>
#include <set>

static int failures = 0;
static void check(bool ok, const char * name, const string & detail = "") {
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) failures++;
}

static set<int> select(SQLite::Database & db, Query q) {
    SQLite::Statement st(db, "SELECT remoteUID FROM Message" + q.getSQL());
    q.bind(st);
    set<int> out;
    while (st.executeStep()) out.insert(st.getColumn(0).getInt());
    return out;
}

int main() {
    SQLite::Database db(":memory:", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    db.exec("CREATE TABLE Message (remoteFolderId TEXT, remoteUID INTEGER)");
    for (int uid : {1, 100, 588, 589, 590, 622, 623, 700, 7000}) {
        db.exec("INSERT INTO Message VALUES ('INBOX', " + to_string(uid) + ")");
    }
    db.exec("INSERT INTO Message VALUES ('Trash', 5)");

    Query q1 = Query().equal("remoteFolderId", string("INBOX")).gte("remoteUID", 591).lte("remoteUID", 622);
    string sql = q1.getSQL();
    check(sql.find("remoteUID >= ?") != string::npos && sql.find("remoteUID <= ?") != string::npos,
          "dwa warunki na tej samej kolumnie zostają w SQL", sql);
    auto r1 = select(db, q1);
    check(r1 == set<int>({622}), "zakres 591–622 dotyka tylko UID z zakresu (nie wszystkiego poniżej)",
          to_string(r1.size()) + " wierszy");

    auto r2 = select(db, Query().equal("remoteFolderId", string("INBOX")).gte("remoteUID", 1).lte("remoteUID", 588));
    check(r2 == set<int>({1, 100, 588}), "zakres 1–588 nie dotyka 589, 590, 623, 7000", to_string(r2.size()));

    auto r3 = select(db, Query().equal("remoteFolderId", string("INBOX")).gte("remoteUID", 7000));
    check(r3 == set<int>({7000}), "zakres otwarty 7000–*");

    auto r4 = select(db, Query().equal("remoteFolderId", string("Trash")));
    check(r4 == set<int>({5}), "pojedynczy warunek bez zmian");

    printf("%s (%d błędów)\n", failures == 0 ? "OK" : "BŁĄD", failures);
    return failures == 0 ? 0 : 1;
}
