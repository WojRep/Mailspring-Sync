//
//  dav-auth-policy-test.cpp — bezpieczeństwo poświadczeń CalDAV/CardDAV
//
//  Czyste funkcje DAVAuthPolicy.hpp: (1) żadnego nagłówka Authorization po
//  niezaszyfrowanym http; (2) hasło IMAP (bez osobnego loginu CalDAV) tylko do
//  hostów z zaufanym związkiem z kontem — host IMAP, jawne ustawienia, cel
//  przekierowania https z .well-known w tej samej domenie; (3) jawne dane
//  CalDAV i OAuth (Bearer) działają jak dotąd, ale też nie po http.
//

#include "DAVAuthPolicy.hpp"

#include <cstdio>

static int failures = 0;
static void check(bool ok, const char * name, const string & detail = "") {
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail.empty() ? "" : " — ", detail.c_str());
    if (!ok) failures++;
}

int main() {
    // Konto jak tech@actuna.pl: serwer IMAP kaszub.actuna.pl, bez ustawień CalDAV.
    DAVCredentials imapOnly;
    imapOnly.imapUser = "user";
    imapOnly.imapPass = "pass";
    const string basicUserPass = "Authorization: Basic dXNlcjpwYXNz"; // base64("user:pass")
    const set<string> anchors = davAnchorHosts("kaszub.actuna.pl", "", {});

    int bearerCalls = 0;
    auto bearer = [&bearerCalls]() { bearerCalls++; return string("TOKEN"); };

    // --- parsowanie hosta z adresu
    check(davURLHost("https://Kaszub.Actuna.PL:8443/dav/") == "kaszub.actuna.pl", "host: małe litery, bez portu",
          davURLHost("https://Kaszub.Actuna.PL:8443/dav/"));
    check(davURLHost("https://kaszub.actuna.pl@evil.example/x") == "evil.example", "host: część userinfo nie myli",
          davURLHost("https://kaszub.actuna.pl@evil.example/x"));
    check(davURLHost("kaszub.actuna.pl.") == "kaszub.actuna.pl", "host: goły host z kropką na końcu");
    check(davURLScheme("HTTP://a.b/") == "http", "schemat: małe litery");

    // --- (1) http nigdy nie dostaje nagłówka
    check(davAuthorizationHeader("http://kaszub.actuna.pl/.well-known/caldav", imapOnly, anchors, bearer).empty(),
          "http:// do hosta IMAP → brak nagłówka");
    DAVCredentials explicitDav = imapOnly;
    explicitDav.davUser = "cal";
    explicitDav.davPass = "secret";
    check(davAuthorizationHeader("http://dav.example.org/cal/", explicitDav, anchors, bearer).empty(),
          "http:// z jawnymi danymi CalDAV → brak nagłówka");
    DAVCredentials oauth = imapOnly;
    oauth.oauth = true;
    check(davAuthorizationHeader("http://apidata.googleusercontent.com/caldav/v2/", oauth, anchors, bearer).empty() &&
              bearerCalls == 0,
          "http:// z OAuth → brak nagłówka i bez pobierania tokenu");

    // --- (2) hasło IMAP tylko do zaufanych hostów
    check(davAuthorizationHeader("https://actuna.pl/", imapOnly, anchors, bearer).empty(),
          "domena e-mail (serwer WWW) niebędąca hostem IMAP → brak hasła IMAP");
    check(davAuthorizationHeader("https://kaszub.actuna.pl/SOGo/dav/", imapOnly, anchors, bearer) == basicUserPass,
          "host IMAP → hasło IMAP", davAuthorizationHeader("https://kaszub.actuna.pl/SOGo/dav/", imapOnly, anchors, bearer));
    check(davAuthorizationHeader("https://evilactuna.pl/", imapOnly, anchors, bearer).empty(),
          "host z tym samym ogonem bez kropki → brak hasła");
    const set<string> withExplicitHost = davAnchorHosts("kaszub.actuna.pl", "https://dav.example.org:8443/", {});
    check(davAuthorizationHeader("https://dav.example.org:8443/cal/", imapOnly, withExplicitHost, bearer) == basicUserPass,
          "host z jawnego ustawienia CalDAV → hasło IMAP");
    const set<string> withExtra = davAnchorHosts("kaszub.actuna.pl", "", {"https://cal.example.net/u/home/"});
    check(davAuthorizationHeader("https://cal.example.net/u/home/", imapOnly, withExtra, bearer) == basicUserPass,
          "host z ręcznie dodanego kalendarza → hasło IMAP");

    // --- przekierowania z .well-known
    check(davAcceptWellKnownRedirect("http://mail.actuna.pl/SOGo/dav/").empty(),
          "przekierowanie na http → odrzucone");
    check(davAcceptWellKnownRedirect("https://mail.actuna.pl/SOGo/dav/") == "https://mail.actuna.pl/SOGo/dav/",
          "przekierowanie na https → przyjęte");
    check(davRedirectTargetTrusted("https://mail.actuna.pl/SOGo/dav/", anchors),
          "przekierowanie https w tej samej domenie co host IMAP → zaufane");
    check(!davRedirectTargetTrusted("https://evil.example/dav/", anchors),
          "przekierowanie https na obcą domenę → niezaufane");
    check(!davRedirectTargetTrusted("http://mail.actuna.pl/dav/", anchors),
          "przekierowanie http w tej samej domenie → niezaufane");
    check(davAuthorizationHeader("https://evil.example/dav/", imapOnly, anchors, bearer).empty(),
          "obca domena z przekierowania nie dostaje hasła IMAP");

    // --- host domeny adresu e-mail nigdy nie dostaje hasła IMAP, chyba że sam jest kotwicą
    check(!davRedirectTargetTrusted("https://actuna.pl/remote.php/dav/", anchors, "actuna.pl"),
          "przekierowanie na host domeny e-mail (actuna.pl, IMAP kaszub.actuna.pl) → niezaufane");
    check(!davRedirectTargetTrusted("https://ACTUNA.pl.:443/dav/", anchors, "Actuna.PL"),
          "j.w. po normalizacji hosta (wielkość liter, port, kropka końcowa)");
    check(davRedirectTargetTrusted("https://mail.actuna.pl/SOGo/dav/", anchors, "actuna.pl"),
          "przekierowanie na inny host tej samej domeny → nadal zaufane");
    check(davAuthorizationHeader("https://actuna.pl/remote.php/dav/", explicitDav, anchors, bearer) ==
              "Authorization: Basic Y2FsOnNlY3JldA==",
          "jawne dane CalDAV na host domeny e-mail → działają");
    const set<string> imapIsEmailDomain = davAnchorHosts("actuna.pl", "", {});
    check(davAuthorizationHeader("https://actuna.pl/dav/", imapOnly, imapIsEmailDomain, bearer) == basicUserPass,
          "host IMAP równy domenie e-mail → hasło IMAP (kotwica)");
    check(davRedirectTargetTrusted("https://actuna.pl/dav/", imapIsEmailDomain, "actuna.pl"),
          "przekierowanie na domenę e-mail będącą hostem IMAP → zaufane");
    const set<string> explicitIsEmailDomain = davAnchorHosts("kaszub.actuna.pl", "actuna.pl", {});
    check(davRedirectTargetTrusted("https://actuna.pl/dav/", explicitIsEmailDomain, "actuna.pl"),
          "przekierowanie na domenę e-mail będącą jawnym caldav_host → zaufane");

    // --- reguła „ta sama domena” i jej bezpieczne granice
    check(davSameSite("mail.actuna.pl", "kaszub.actuna.pl"), "rodzeństwo pod actuna.pl → ta sama domena");
    check(davSameSite("actuna.pl", "kaszub.actuna.pl"), "domena nadrzędna → ta sama domena");
    check(!davSameSite("evil.com.pl", "firma.com.pl"), "firma.com.pl nie otwiera całego com.pl");
    check(davSameSite("mail.firma.com.pl", "imap.firma.com.pl"), "imap.firma.com.pl → firma.com.pl");
    check(!davSameSite("example.org", "imap.example.co.uk"), "inna domena → nie");
    check(!davSameSite("10.0.0.2", "10.0.0.1") && davSameSite("10.0.0.1", "10.0.0.1"), "adres IP → tylko dokładnie");
    check(!davSameSite("com", "example.com"), "sama domena najwyższego poziomu → nigdy");

    // --- (3) jawne dane CalDAV i OAuth jak dotąd
    check(davAuthorizationHeader("https://caldav.icloud.com/", explicitDav, anchors, bearer) ==
              "Authorization: Basic Y2FsOnNlY3JldA==", // base64("cal:secret")
          "jawne dane CalDAV → wysyłane jak dotąd (https)");
    check(davAuthorizationHeader("https://apidata.googleusercontent.com/caldav/v2/", oauth, anchors, bearer) ==
              "Authorization: Bearer TOKEN" && bearerCalls == 1,
          "OAuth → Bearer jak dotąd (https)");

    printf("%s (%d błędów)\n", failures == 0 ? "OK" : "BŁĄD", failures);
    return failures == 0 ? 0 : 1;
}
