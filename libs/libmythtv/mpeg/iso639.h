// -*- Mode: c++ -*-
#ifndef _ISO_639_2_H_
#define _ISO_639_2_H_

#include <qmap.h>
#include <qstring.h>
#include <qstringlist.h>
#include <vector>
#include "mythexp.h"
using namespace std;

extern MPUBLIC QMap<int, QString> _iso639_key_to_english_name;

/** \file iso639.h
 *  \brief ISO 639-1 and ISO 639-2 support functions
 *
 *   ISO 639-1 is the two letter standard for specifying a language.
 *   This is used by MythTV for naming the themes and for initializing
 *   the Qt translation system.
 *
 *   ISO 639-2 is the three letter standard for specifying a language.
 *   This is used by MythTV for selecting subtitles and audio streams
 *   during playback, and for selecting which languages to collect
 *   EIT program guide information in.
 *
 *   In many contexts, such as with themes, these language codes can
 *   be appended with an underscore and a 2 digit IETF region code.
 *   So for Brazilian Portugese you could use: "por_BR", or "pt_BR".
 *   Or, you could specify just the language, Portugese: "por", or "pt".
 */

/// Converts a 2 or 3 character iso639 string to a language name in English.
QString     iso639_str_toName(const unsigned char *iso639);
/// Converts a canonical key to language name in English
QString     iso639_key_toName(int iso639_2);
void        iso639_clear_language_list(void);
QStringList iso639_get_language_list(void);
vector<int> iso639_get_language_key_list(void);
int         iso639_key_to_canonical_key(int iso639_2);
MPUBLIC QString     iso639_str2_to_str3(const QString &str2);

static inline QString iso639_key_to_str3(int code)
{
    char str[4];
    str[0] = (code>>16) & 0xFF;
    str[1] = (code>>8)  & 0xFF;
    str[2] = code & 0xFF;
    str[3] = 0;
    return QString(str);
}

static inline int iso639_str3_to_key(const unsigned char *iso639_2)
{
    return (iso639_2[0]<<16)|(iso639_2[1]<<8)|iso639_2[2];
}

static inline int iso639_str3_to_key(const char *iso639_2)
{
    return iso639_str3_to_key((const unsigned char*)iso639_2);
}

static inline int iso639_str2_to_key2(const unsigned char *iso639_1)
{
    return (iso639_1[0]<<8)|iso639_1[1];
}

static inline int iso639_str2_to_key2(const char *iso639_1)
{
    return iso639_str2_to_key2((const unsigned char*)iso639_1);
}

static inline QString iso639_str_to_canonoical_str(const QString &str3)
{
    int key = iso639_str3_to_key(str3.ascii());
    int can =  iso639_key_to_canonical_key(key);
    return iso639_key_to_str3(can);
}

#endif // _ISO_639_2_H_
