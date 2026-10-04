/* sh_locale.c -- the language the game runs in: the console's, or the one
 * config.ini names.
 *
 * On Android the game asks Java: the "getlanguage" command answers
 * Locale.getDefault().getLanguage() ("pt", "de"), and the GameActivity glue
 * reads the language and country of the AConfiguration. Its string tables
 * (assets/locale): English, German, Spanish, French, Italian, Japanese,
 * Korean, Russian, Chinese; anything else plays in English in the game
 * itself.
 *
 * Here the console's language (set:sys's language code: "en-US", "pt-BR",
 * "ja", "zh-Hans"...) is turned into those two, unless [game] language in
 * config.ini is something other than "auto" (the same kind of code).
 * Worked out once, at the first question. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "sh.h"
#include "util.h"

static char g_lang[4] = "en", g_country[4] = "US";
static int g_done;

/* The country a language alone stands for (the console's plain codes). */
static const char *default_country(const char *lang) {
  static const char *const k[][2] = {{"en", "US"}, {"ja", "JP"}, {"fr", "FR"}, {"de", "DE"},
                                     {"it", "IT"}, {"es", "ES"}, {"ko", "KR"}, {"nl", "NL"},
                                     {"pt", "PT"}, {"ru", "RU"}, {"zh", "CN"}};
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    if (!strcmp(lang, k[i][0]))
      return k[i][1];
  return "US";
}

/* "pt-BR", "ja", "zh-Hans", "es-419": 1 when it is a code. */
static int parse(const char *code) {
  char lang[4] = "", rest[8] = "";
  const char *dash = strchr(code, '-');
  const size_t n = dash ? (size_t)(dash - code) : strlen(code);
  if (n < 2 || n > 3)
    return 0;
  memcpy(lang, code, n);
  if (dash)
    snprintf(rest, sizeof rest, "%s", dash + 1);
  for (char *p = lang; *p; p++)
    if (*p >= 'A' && *p <= 'Z')
      *p += 'a' - 'A';
  /* the scripts of Chinese, as Android's regions */
  if (!strcmp(rest, "Hans"))
    strcpy(rest, "CN");
  else if (!strcmp(rest, "Hant"))
    strcpy(rest, "TW");
  if (strlen(rest) < 2 || strlen(rest) > 3)
    snprintf(rest, sizeof rest, "%s", default_country(lang));
  snprintf(g_lang, sizeof g_lang, "%s", lang);
  snprintf(g_country, sizeof g_country, "%s", rest);
  return 1;
}

static void work_out(void) {
  if (g_done)
    return;
  g_done = 1;
  const char *want = dcr_config()->language;
  if (want[0] && strcmp(want, "auto")) {
    if (parse(want)) {
      debugPrintf("[locale] %s (config.ini): language %s, country %s\n", want, g_lang, g_country);
      return;
    }
    debugPrintf("[locale] config.ini's language \"%s\" is not a language code: the console's\n", want);
  }
  u64 code = 0;
  Result rc = setInitialize();
  if (R_SUCCEEDED(rc)) {
    rc = setGetSystemLanguage(&code);
    setExit();
  }
  char text[9] = "";
  memcpy(text, &code, 8);
  if (R_SUCCEEDED(rc) && parse(text))
    debugPrintf("[locale] the console's language %s: language %s, country %s\n", text, g_lang, g_country);
  else
    debugPrintf("[locale] the console's language could not be read (0x%x): English\n", (unsigned)rc);
}

const char *sh_language(void) {
  work_out();
  return g_lang;
}

const char *sh_country(void) {
  work_out();
  return g_country;
}
