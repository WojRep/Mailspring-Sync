//
//  MetadataWorker.hpp
//  MailSync
//
//  Created by Ben Gotow on 7/31/17.
//  Copyright © 2017 Foundry 376. All rights reserved.
//
//  Use of this file is subject to the terms and conditions defined
//  in 'LICENSE.md', which is part of the Mailspring-Sync package.
//

#ifndef DAVWorker_hpp
#define DAVWorker_hpp

#include "Account.hpp"
#include "Identity.hpp"
#include "ContactBook.hpp"
#include "MailStore.hpp"
#include "DavXML.hpp"
#include "Event.hpp"
#include "Calendar.hpp"
#include "DAVAuthPolicy.hpp"

#include <stdio.h>

#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#include "spdlog/spdlog.h"

using namespace std;

typedef string ETAG;

class DAVWorker {
    MailStore * store;
    shared_ptr<spdlog::logger> logger;

    string calHost;
    string calPrincipal;

    // In-memory discovery cache for CardDAV
    // Persists for lifetime of worker (days), reset on process restart
    bool contactsDiscoveryComplete = false;
    shared_ptr<ContactBook> cachedAddressBook = nullptr;
    int contactsValidationFailures = 0;

    // In-memory discovery cache for CalDAV
    bool calendarsDiscoveryComplete = false;
    string cachedCalendarHomeURL = "";

    bool validateCachedAddressBook();

    // Rate limiting state (shared for CalDAV and CardDAV)
    // Implements RFC 6585 (429) and RFC 7231 (Retry-After) compliance
    int backoffMs = 0;                      // Current backoff delay in milliseconds
    int consecutiveSuccesses = 0;           // Used to gradually reduce backoff
    time_t rateLimitedUntil = 0;            // Blocked until this time (from Retry-After)

    void applyRateLimitDelay();
    void recordRequestSuccess();
    void recordRateLimitResponse(int httpCode, const string& retryAfter);
    int parseRetryAfter(const string& retryAfter);

    // Bezpieczeństwo poświadczeń (DAVAuthPolicy.hpp):
    // - hosty celów przekierowań https z .well-known przyjęte jako zaufane
    //   dla hasła IMAP (ta sama domena co host IMAP lub jawne ustawienie);
    // - hosty, które po https odpowiedziały 401 — dopiero tym wysyłamy
    //   nagłówek Authorization od razu (bez ponownej rundy bez danych);
    // - hosty, o których już zalogowaliśmy wstrzymanie danych (bez spamu w logu).
    set<string> admittedRedirectHosts;
    set<string> authChallengedHosts;
    set<string> credentialsWithheldLogged;

    set<string> accountAnchorHosts();
    set<string> imapCredentialHosts();
    void admitWellKnownRedirect(const string & fromHost, const string & redirectURL);
    // Najpierw zapytanie bez danych; Authorization dopiero po 401 z tego samego
    // hosta po https (i tylko gdy polityka na to pozwala). `attempt` dostaje
    // pełny nagłówek albo "" i musi zbudować świeże zapytanie curl.
    string performWithDAVAuth(const string & url, const function<string(const string & authHeader)> & attempt);

public:
    shared_ptr<Account> account;

    DAVWorker(shared_ptr<Account> account);
    
    void run();
    
    shared_ptr<ContactBook> resolveAddressBook();
    string resolveCalendarHomeURL();

    // #267: kandydaci na host CalDAV, w kolejności prób — jawne ustawienie
    // konta, domena adresu e-mail (RFC 6764), host IMAP. Bez usług trzecich:
    // odpytujemy wyłącznie serwer użytkownika. Domena adresu e-mail NIE dostaje
    // hasła IMAP (DAVAuthPolicy.hpp) — tylko jej przekierowanie https w tej
    // samej domenie co host IMAP może zostać uznane za zaufane.
    vector<string> calendarHostCandidates();
    // Pełny łańcuch discovery na JEDNYM hoście: .well-known/caldav →
    // current-user-principal (RFC 5397) → calendar-home-set (RFC 4791 §6.2.1).
    // Zwraca URL home-setu albo "" gdy host nie obsługuje CalDAV.
    string discoverCalendarHomeAt(const string & caldavHost);

    // Returns the full https:// URL for a calendar collection path.
    // For Gmail, prepends calHost. For all other accounts, resolves
    // against the discovered cachedCalendarHomeURL.
    string resolvedCalendarURL(const string & calPath);
    
    void writeAndResyncContact(shared_ptr<Contact> contact);
    void deleteContact(shared_ptr<Contact> contact);
    
    void runContacts();
    void runForAddressBook(shared_ptr<ContactBook> ab);
    bool runForAddressBookWithSyncToken(shared_ptr<ContactBook> ab, int retryCount = 0);

    void ingestContactDeletions(shared_ptr<ContactBook> ab, vector<ETAG> deleted);
    shared_ptr<Contact> ingestAddressDataNode(shared_ptr<DavXML> doc, xmlNodePtr node, bool & isGroup);
    void rebuildContactGroup(shared_ptr<Contact> contact);

    void runCalendars();
    // #269: kalendarze wskazane ręcznie (settings.caldav_extra_urls). Osobna
    // ścieżka, bo uruchamiana ZANIM discovery home-setu — jawny adres podaje się
    // właśnie wtedy, gdy discovery nic nie znajduje i runCalendars() kończy wcześnie.
    void runExtraCalendars();
    void runOneExtraCalendar(const string & url);
    void runForCalendar(string id, string name, string path);
    bool runForCalendarWithSyncToken(string calendarId, string url, shared_ptr<Calendar> calendar, int retryCount = 0);

    void writeAndResyncEvent(shared_ptr<Event> event);
    void deleteEvent(shared_ptr<Event> event);

    // Nagłówek Authorization dla KONKRETNEGO adresu albo "" gdy nie wolno
    // wysłać żadnych danych (http, albo hasło IMAP do niezaufanego hosta).
    const string getAuthorizationHeader(const string & url);

    shared_ptr<DavXML> performXMLRequest(string path, string method, string payload = "", string depth = "1");
    string performVCardRequest(string _url, string method, string vcard = "", ETAG existingEtag = "");
    string performICSRequest(string url, string method, string icsData, ETAG existingEtag = "");
    
};

#endif /* DAVWorker_hpp */
