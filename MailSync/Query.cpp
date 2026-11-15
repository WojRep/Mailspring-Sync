//
//  Query.cpp
//  mailcore2
//
//  Created by Ben Gotow on 6/20/17.
//  Copyright © 2017 MailCore. All rights reserved.
//

#include "Query.hpp"
#include <SQLiteCpp/SQLiteCpp.h>

#include "json.hpp"
#include "MailUtils.hpp"
#include "SyncException.hpp"

using namespace nlohmann;
using namespace std;


// TODO: figure out how to use templates for this

Query::Query() noexcept : _clauses(json::array()), _limit(0), _orderBy(""), _orderDir("ASC") {
}

// #327 — warunki to LISTA {col, op, rhs}. Wcześniej obiekt JSON z kluczem =
// nazwa kolumny: `.gte("remoteUID", a).lt("remoteUID", b)` zostawiało samo
// `remoteUID < b`, przez co obsługa VANISHED odłączała wszystkie wiadomości
// poniżej zakresu. Ten sam operator na tej samej kolumnie nadal ZASTĘPUJE
// poprzedni (dotychczasowa semantyka), różne operatory łączą się przez AND.
Query & Query::_set(const string & col, const string & op, const json & rhs) {
    for (auto it = _clauses.begin(); it != _clauses.end(); ++it) {
        if ((*it)["col"] == col && (*it)["op"] == op) {
            _clauses.erase(it);
            break;
        }
    }
    _clauses.push_back({{"col", col}, {"op", op}, {"rhs", rhs}});
    return *this;
}

Query & Query::equal(string col, string val) {
    return _set(col, "=", val);
}

Query & Query::equal(string col, double val) {
    return _set(col, "=", val);
}

Query & Query::equal(string col, vector<string> & val) {
    if (val.size() > 999) {
        spdlog::get("logger")->warn("Attempting to construct WHERE {} IN () query with >999 values ({}), this will fail and should be reported.", col, val.size());
    }
    return _set(col, "=", val);
}

Query & Query::equal(string col, vector<uint32_t> & val) {
    return _set(col, "=", val);
}

Query & Query::gt(string col, double val) {
    return _set(col, ">", val);
}

Query & Query::gte(string col, double val) {
    return _set(col, ">=", val);
}

Query & Query::lt(string col, double val) {
    return _set(col, "<", val);
}

Query & Query::lte(string col, double val) {
    return _set(col, "<=", val);
}

Query & Query::limit(int l) {
    _limit = l;
    return *this;
}

Query & Query::orderBy(string col, string dir) {
    _orderBy = col;
    _orderDir = dir;
    return *this;
}

int Query::getLimit() {
    return _limit;
}

string Query::getSQL() {
    string result = "";

    if (_clauses.size() > 0) {
        result += " WHERE ";

        for (json::iterator it = _clauses.begin(); it != _clauses.end(); ++it) {
            if (it != _clauses.begin()) {
                result += " AND ";
            }
            string col = (*it)["col"].get<string>();
            string op = (*it)["op"].get<string>();
            json & rhs = (*it)["rhs"];
            
            if (rhs.is_array()) {
                if (op != "=") {
                    throw SyncException("query-builder", "Cannot use query operator " + op + " with an array of values", true);
                }
                if (rhs.size() == 0) {
                    result += "0 = 1";
                    continue;
                }
                result += col + " IN (" + MailUtils::qmarks(rhs.size()) + ")";
            } else {
                result += col + " " + op + " ?";
            }
        }
    }
    if (!_orderBy.empty()) {
        result += " ORDER BY " + _orderBy + " " + _orderDir;
    }
    return result;
}

void Query::bind(SQLite::Statement & query) {
    int ii = 1;
    for (json::iterator it = _clauses.begin(); it != _clauses.end(); ++it) {
        json & rhs = (*it)["rhs"];

        if (rhs.is_array()) {
            for (json::iterator at = rhs.begin(); at != rhs.end(); ++at) {
                if (at->is_number()) {
                    query.bind(ii++, at->get<double>());
                } else if (at->is_string()) {
                    query.bind(ii++, at->get<string>());
                } else {
                    throw SyncException("query-builder", "Unsure of how to bind json to sqlite", true);
                }
            }
        } else {
            if (rhs.is_number()) {
                query.bind(ii++, rhs.get<double>());
            } else if (rhs.is_string()) {
                query.bind(ii++, rhs.get<string>());
            } else {
                throw SyncException("query-builder", "Unsure of how to bind json to sqlite", true);
            }
        }
    }
}
