/*
 *  Copyright 2004 - Taylor Jacob (rtjacob at earthlink.net)
 */

#ifndef EITFIXUP_H
#define EITFIXUP_H

#include <qregexp.h>

#include "eit.h"

typedef QMap<uint,uint> QMap_uint_t;

/// EIT Fix Up Functions
class EITFixUp
{
  protected:
     // max length of subtitle field in db.
     static const uint SUBTITLE_MAX_LEN = 128;
     // max number of words included in a subtitle
     static const uint kMaxToTitle = 14;
     // max number of words up to a period, question mark
     static const uint kDotToTitle = 9;
     // max number of question/exclamation marks
     static const uint kMaxQuestionExclamation = 2;
     // max number of difference in words between a period and a colon
     static const uint kMaxDotToColon = 5;

  public:
    enum FixUpType
    {
        kFixNone       = 0x0000,

        // Regular fixups
        kFixGenericDVB = 0x0001,
        kFixBell       = 0x0002,
        kFixUK         = 0x0004,
        kFixPBS        = 0x0008,
        kFixComHem     = 0x0010,
        kFixSubtitle   = 0x0020,
        kFixAUStar     = 0x0040,
        kFixMCA        = 0x0080,
        kFixRTL        = 0x0100,
        kFixFI         = 0x0200,
        kFixPremiere   = 0x0400,
        kFixHDTV       = 0x0800,
        kFixNL         = 0x1000,

        // Early fixups
        kEFixForceISO8859_1  = 0x2000,
        kEFixForceISO8859_15 = 0x4000,
    };

    EITFixUp();

    void Fix(DBEvent &event) const;

    static void TimeFix(QDateTime &dt)
    {
        int secs = dt.time().second();
        if (secs < 5)
            dt = dt.addSecs(-secs);
        if (secs > 55)
            dt = dt.addSecs(60 - secs);
    }

  private:
    void FixBellExpressVu(DBEvent &event) const; // Canada DVB-S
    void SetUKSubtitle(DBEvent &event) const;
    void FixUK(DBEvent &event) const;            // UK DVB-T
    void FixPBS(DBEvent &event) const;           // USA ATSC
    void FixComHem(DBEvent &event, bool parse_subtitle) const; // Sweden DVB-C
    void FixAUStar(DBEvent &event) const;        // Australia DVB-S
    void FixMCA(DBEvent &event) const;        // MultiChoice Africa DVB-S
    void FixRTL(DBEvent &event) const;        // RTL group DVB
    void FixFI(DBEvent &event) const;            // Finland DVB-T
    void FixPremiere(DBEvent &event) const;   // german pay-tv Premiere
    void FixNL(DBEvent &event) const;            // Netherlands DVB-C

    const QRegExp m_bellYear;
    const QRegExp m_bellActors;
    const QRegExp m_bellPPVTitleAllDay;
    const QRegExp m_bellPPVTitleHD;
    const QRegExp m_bellPPVSubtitleAllDay;
    const QRegExp m_bellPPVDescriptionAllDay;
    const QRegExp m_bellPPVDescriptionAllDay2;
    const QRegExp m_bellPPVDescriptionEventId;
    const QRegExp m_ukThen;
    const QRegExp m_ukNew;
    const QRegExp m_ukCEPQ;
    const QRegExp m_ukColonPeriod;
    const QRegExp m_ukDotSpaceStart;
    const QRegExp m_ukDotEnd;
    const QRegExp m_ukSpaceColonStart;
    const QRegExp m_ukSpaceStart;
    const QRegExp m_ukSeries;
    const QRegExp m_ukCC;
    const QRegExp m_ukYear;
    const QRegExp m_uk24ep;
    const QRegExp m_ukStarring;
    const QRegExp m_ukBBC7rpt;
    const QRegExp m_ukDescriptionRemove;
    const QRegExp m_ukTitleRemove;
    const QRegExp m_ukDoubleDotEnd;
    const QRegExp m_ukDoubleDotStart;
    const QRegExp m_ukTime;
    const QRegExp m_ukBBC34;
    const QRegExp m_ukYearColon;
    const QRegExp m_ukExclusionFromSubtitle;
    const QRegExp m_ukCompleteDots;
    const QRegExp m_comHemCountry;
    const QRegExp m_comHemDirector;
    const QRegExp m_comHemActor;
    const QRegExp m_comHemHost;
    const QRegExp m_comHemSub;
    const QRegExp m_comHemRerun1;
    const QRegExp m_comHemRerun2;
    const QRegExp m_comHemTT;
    const QRegExp m_comHemPersSeparator;
    const QRegExp m_comHemPersons;
    const QRegExp m_comHemSubEnd;
    const QRegExp m_comHemSeries1;
    const QRegExp m_comHemSeries2;
    const QRegExp m_comHemTSub;
    const QRegExp m_mcaIncompleteTitle;
    const QRegExp m_mcaCompleteTitlea;
    const QRegExp m_mcaCompleteTitleb;
    const QRegExp m_mcaSubtitle;
    const QRegExp m_mcaSeries;
    const QRegExp m_mcaCredits;
    const QRegExp m_mcaAvail;
    const QRegExp m_mcaActors;
    const QRegExp m_mcaActorsSeparator;
    const QRegExp m_mcaYear;
    const QRegExp m_mcaCC;
    const QRegExp m_mcaDD;
    const QRegExp m_RTLrepeat;
    const QRegExp m_RTLSubtitle;
    const QRegExp m_RTLSubtitle1;
    const QRegExp m_RTLSubtitle2;
    const QRegExp m_RTLSubtitle3;
    const QRegExp m_RTLSubtitle4;
    const QRegExp m_RTLSubtitle5;
    const QRegExp m_RTLEpisodeNo1;
    const QRegExp m_RTLEpisodeNo2;
    const QRegExp m_fiRerun;
    const QRegExp m_Stereo;
    const QRegExp m_dePremiereInfos;
    const QRegExp m_dePremiereOTitle;
    const QRegExp m_nlStereo;
    const QRegExp m_nlTxt;
    const QRegExp m_nlWide;
    const QRegExp m_nlRepeat;
    const QRegExp m_nlHD;
    const QRegExp m_nlSub;
    const QRegExp m_nlActors;
    const QRegExp m_nlPres;
    const QRegExp m_nlPersSeparator;
    const QRegExp m_nlRub;
    const QRegExp m_nlYear1;
    const QRegExp m_nlYear2;
    const QRegExp m_nlDirector;
    const QRegExp m_nlCat;
    const QRegExp m_nlOmroep;
};

#endif // EITFIXUP_H
