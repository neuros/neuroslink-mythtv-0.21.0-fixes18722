#ifndef MYTHUI_CLOCK_H_
#define MYTHUI_CLOCK_H_

#include <qstring.h>
#include <qdatetime.h>
#include <qvaluevector.h>

#include "mythuitype.h"
#include "mythuitext.h"

class MythFontProperties;

/** \class MythUIClock
 *
 * \brief A simple text clock widget.
 *
 * Updates once a second and inherits from MythUIText, so it supports all the
 * text styles and decorations offered by that class.
 *
 * Basic manipulation of the clock format is supported using any of the
 * following in the "format" element of the theme:
 *  %TIME% - The time, in a format defined in the locale settings
 *  %DATE% - Long date format, as defined in the locale settings
 *  %SHORTDATE% - Short date format, as defined in the locale settings
 *
 */
class MythUIClock : public MythUIText
{
  public:
    MythUIClock(MythUIType *parent, const char *name);
   ~MythUIClock();

    virtual void Pulse(void);

  protected:
    virtual bool ParseElement(QDomElement &element);
    virtual void CopyFrom(MythUIType *base);
    virtual void CreateCopy(MythUIType *parent);

    QDateTime m_Time;
    QDateTime m_nextUpdate;

    QString m_Format;
    QString m_TimeFormat;
    QString m_DateFormat;
    QString m_ShortDateFormat;

    bool m_SecsFlash;
    bool m_Flash;
};

#endif
