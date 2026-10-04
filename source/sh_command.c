/* sh_command.c -- MainActivity.command(String): everything the engine asks
 * its Java side, as text.
 *
 * AndroidDevice (the engine's view of the phone) has one way to Java:
 * JavaMessenger::sendCommand calls MainActivity.command("name arg arg"),
 * which CommandHandler.handleCommand splits at the spaces and answers from a
 * table of names with a string: "true" / "false", a number, a word, or ""
 * (also for a name it does not know). The table below is that one
 * (classes3.dex, CommandHandler.setupCommands), answered as a phone does
 * that is offline and signed in to nothing:
 *
 *   the device      getosname, getmodelname, getCpuCount, getlanguage, istv
 *   the store       no billing service: nothing owned, nothing for sale
 *   ads             none loaded, none shown; no consent form
 *   Play Games      signed out: leaderboards, achievements, cloud saves
 *   remote config   never fetched: the game keeps its built-in values
 *   analytics       event2024, dropped
 *   quit            Activity.finish()
 *
 * Nothing is unlocked that the game sells: "isproductowned" is false, as on
 * a phone where the purchase was not made. MIT.
 */
#include <stdio.h>
#include <string.h>

#include "dcr_config.h"
#include "sh.h"
#include "util.h"

typedef const char *(*CmdFn)(const char *arg, char *out, size_t cap);

static const char *c_empty(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  return "";
}
static const char *c_false(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  return "false";
}
static const char *c_zero(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  return "0";
}

/* Build.VERSION.RELEASE, Build.MODEL */
static const char *c_osname(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  return "13";
}
static const char *c_model(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  return "Switch";
}

/* Runtime.availableProcessors() */
static const char *c_cpus(const char *arg, char *out, size_t cap) {
  (void)arg;
  snprintf(out, cap, "%d", dcr_config()->cpu_cores);
  return out;
}

/* Locale.getDefault().getLanguage() */
static const char *c_language(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  return sh_language();
}

/* UiModeManager.getCurrentModeType() == UI_MODE_TYPE_TELEVISION */
static const char *c_istv(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  return dcr_config()->tv_mode ? "true" : "false";
}

static const char *c_quit(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  debugPrintf("[command] quit: the game closes\n");
  sh_request_exit();
  return "";
}

static const char *c_visiturl(const char *arg, char *out, size_t cap) {
  (void)out, (void)cap;
  debugPrintf("[command] visiturl %s: no browser here\n", arg);
  return "";
}

/* AdRewarded.AdResult of an ad that was never shown */
static const char *c_adresult(const char *arg, char *out, size_t cap) {
  (void)arg, (void)out, (void)cap;
  return "fail";
}

static const struct {
  const char *name;
  CmdFn fn;
} k_commands[] = {
    /* the device */
    {"getosname", c_osname},
    {"getmodelname", c_model},
    {"getCpuCount", c_cpus},
    {"getlanguage", c_language},
    {"istv", c_istv},
    {"quit", c_quit},
    {"visiturl", c_visiturl},
    /* asked by the engine, but CommandHandler has no entry for it: "" there too */
    {"isphone", c_empty},
    /* the store (Google Play Billing) */
    {"isproductowned", c_false},
    {"hasrefreshedownedproducts", c_false},
    {"confirmownedproductsprocessed", c_empty},
    {"storeenabled", c_false},
    {"storepurchase", c_empty},
    {"storegetstatus", c_zero},
    {"storegeterror", c_zero},
    {"storerestore", c_empty},
    {"storeisrestored", c_false},
    {"storegetprice", c_empty},
    {"consumeProductDevTest", c_empty},
    /* ads (AdMob) and their consent form */
    {"isAdSystemInitialized", c_false},
    {"wasConsentPopupShown", c_false},
    {"showPrivacyOptions", c_empty},
    {"isadloaded", c_false},
    {"showad", c_empty},
    {"getadresult", c_adresult},
    {"isadfinished", c_false},
    {"confirmShouldShowAds", c_empty},
    {"refreshExpiredAds", c_empty},
    /* Play Games */
    {"issignedin", c_false},
    {"updateleaderboard", c_empty},
    {"incrementachievement", c_empty},
    {"showleaderboards", c_empty},
    {"showachievements", c_empty},
    {"cloudsave", c_empty},
    {"cloudload", c_empty},
    {"cloudget", c_empty},
    /* Firebase remote config and analytics */
    {"isRemoteConfigUpdated", c_false},
    {"getRemoteConfigData", c_empty},
    {"event2024", c_empty},
};
#define NCOMMANDS (sizeof k_commands / sizeof k_commands[0])

const char *sh_command(const char *line, char *out, size_t cap) {
  static uint8_t seen[NCOMMANDS];
  char name[48];
  const char *sp = strchr(line, ' ');
  const size_t n = sp ? (size_t)(sp - line) : strlen(line);
  snprintf(name, sizeof name, "%.*s", (int)(n < sizeof name - 1 ? n : sizeof name - 1), line);
  const char *arg = sp ? sp + 1 : "";
  for (unsigned i = 0; i < NCOMMANDS; i++) {
    if (strcmp(k_commands[i].name, name))
      continue;
    const char *r = k_commands[i].fn(arg, out, cap);
    /* the game polls some of these every frame: each is logged once */
    if (!seen[i] || dcr_config()->log_commands) {
      seen[i] = 1;
      debugPrintf("[command] %s -> \"%s\"\n", line, r);
    }
    return r;
  }
  /* CommandHandler answers "" for a name it has no entry for */
  static unsigned unknown;
  if (unknown++ < 32)
    debugPrintf("[command] UNKNOWN \"%s\" -> \"\"\n", line);
  return "";
}
