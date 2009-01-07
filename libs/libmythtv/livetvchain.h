#ifndef _LIVETVCHAIN_H_
#define _LIVETVCHAIN_H_

#include <qstring.h>
#include <qvaluelist.h>
#include <qdatetime.h>
#include <qmutex.h>
#include <qptrlist.h>

#include "mythexp.h"

class ProgramInfo;
class MythSocket;

struct MPUBLIC LiveTVChainEntry
{
    QString chanid;
    QDateTime starttime;
    QDateTime endtime;
    bool discontinuity; // if true, can't play smooth from last entry
    QString hostprefix;
    QString cardtype;
    QString channum;
    QString inputname;
};

class MPUBLIC LiveTVChain
{
  public:
    LiveTVChain();
   ~LiveTVChain();

    QString InitializeNewChain(const QString &seed);
    void LoadFromExistingChain(const QString &id);

    void SetHostPrefix(const QString &prefix);
    void SetCardType(const QString &type);

    void DestroyChain(void);

    void AppendNewProgram(ProgramInfo *pginfo, QString channum,
                          QString inputname, bool discont);
    void FinishedRecording(ProgramInfo *pginfo);
    void DeleteProgram(ProgramInfo *pginfo);

    void ReloadAll();

    // const gets
    QString GetID(void)  const { return m_id; }
    int  GetCurPos(void) const { return m_curpos; }
    int  ProgramIsAt(const QString &chanid, const QDateTime &starttime) const;
    int  ProgramIsAt(const ProgramInfo *pginfo) const;
    int  GetLengthAtCurPos(void);
    int  TotalSize(void) const;
    bool HasNext(void)   const;
    bool HasPrev(void)   const { return (m_curpos > 0); }
    ProgramInfo *GetProgramAt(int at) const;
    /// Returns true iff a switch is required but no jump is required
    bool NeedsToSwitch(void) const
        { return (m_switchid >= 0 && m_jumppos == 0); }
    /// Returns true iff a switch and jump are required
    bool NeedsToJump(void)   const
        { return (m_switchid >= 0 && m_jumppos != 0); }
    QString GetChannelName(int pos = -1) const;
    QString GetInputName(int pos = -1) const;
    QString GetCardType(int pos = -1) const;

    // sets/gets program to switch to
    void SetProgram(ProgramInfo *pginfo);
    void SwitchTo(int num);
    void SwitchToNext(bool up);
    void ClearSwitch(void);
    ProgramInfo *GetSwitchProgram(bool &discont, bool &newtype, int &newid);

    // sets/gets program to jump to
    void JumpTo(int num, int pos);
    void JumpToNext(bool up, int pos);
    int  GetJumpPos(void);

    // socket stuff
    void SetHostSocket(MythSocket *sock);
    bool IsHostSocket(MythSocket *sock);
    int HostSocketCount(void);
    void DelHostSocket(MythSocket *sock);
 
  private:
    void BroadcastUpdate();
    void GetEntryAt(int at, LiveTVChainEntry &entry) const;
    static ProgramInfo *EntryToProgram(const LiveTVChainEntry &entry);

    QString m_id;
    QValueList<LiveTVChainEntry> m_chain;
    int m_maxpos;
    mutable QMutex m_lock;

    QString m_hostprefix;
    QString m_cardtype;

    int m_curpos;
    QString m_cur_chanid;
    QDateTime m_cur_startts;

    int m_switchid;
    LiveTVChainEntry m_switchentry;

    int m_jumppos;

    QMutex m_sockLock;
    QPtrList<MythSocket> m_inUseSocks;
};

#endif
    
