/*
 * The new Tracker for TikuOS.
 *
 * Authors: Ambuj Varshney <ambuj@tiku-os.org>
 *
 * tiku_strings.h - the words the interface is made of.
 *
 * Every line a person reads is named rather than written in the code, so
 * the words can be changed without a compiler and translated without
 * touching a source file.  A key -- "menu.about" -- stands for a line;
 * the lines themselves live in strings/<lang>.xml, and the English is
 * baked in as well so the interface has words even with no file at all.
 *
 * The device can now draw Greek, Cyrillic and CJK (see the font work),
 * so a translation is a thing a reader actually sees, not just stores.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef TIKU_STRINGS_H_
#define TIKU_STRINGS_H_

/**
 * @brief The line named @p key, in the language in force.
 *
 * Never NULL: a key with no translation falls back to the baked English,
 * and a key nobody has defined at all falls back to the key itself, so a
 * missing string is visible rather than a blank.
 */
const char *tiku_str(const char *key);

/**
 * @brief Load the translation for @p lang from @p dir, if there is one.
 *
 * @param dir  Where the .xml files are; NULL asks the usual places.
 * @param lang The language, e.g. "fr"; NULL reads it from the environment
 *             ($TIKU_LANG, else $LANG).
 * @return the language actually in force ("en" when nothing was loaded).
 */
const char *tiku_strings_init(const char *dir, const char *lang);

/** @brief Read one .xml catalogue over the current one.  @return count. */
int tiku_strings_load_file(const char *path);

/** @brief Forget any loaded translation, back to the baked English. */
void tiku_strings_reset(void);

/** @brief How many lines the baked English catalogue holds. */
int tiku_strings_builtin_count(void);

#endif /* TIKU_STRINGS_H_ */
