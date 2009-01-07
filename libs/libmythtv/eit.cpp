// -*- Mode: c++ -*-

// C++ includes
#include <algorithm>
using namespace std;

// Qt includes
#include <qdeepcopy.h>

// MythTV includes
#include "mythdbcon.h"
#include "eit.h"
#include "dvbdescriptors.h"
#include "programinfo.h" // for subtitle types and audio and video properties

DBPerson::DBPerson(Role _role, const QString &_name) :
    role(_role), name(QDeepCopy<QString>(_name))
{
}

QString DBPerson::GetRole(void) const
{
    static const char* roles[] =
    {
        "actor",     "director",    "producer", "executive_producer",
        "writer",    "guest_star",  "host",     "adapter",
        "presenter", "commentator", "guest",
    };
    if ((role < kActor) || (role > kGuest))
        return "guest";
    return roles[role];
}

uint DBPerson::InsertDB(MSqlQuery &query, uint chanid,
                        const QDateTime &starttime) const
{
    uint personid = GetPersonDB(query);
    if (!personid && InsertPersonDB(query))
        personid = GetPersonDB(query);

    return InsertCreditsDB(query, personid, chanid, starttime);
}

uint DBPerson::GetPersonDB(MSqlQuery &query) const
{
    query.prepare(
        "SELECT person "
        "FROM people "
        "WHERE name = :NAME");
    query.bindValue(":NAME", name.utf8());

    if (!query.exec())
        MythContext::DBError("get_person", query);
    else if (query.next())
        return query.value(0).toUInt();

    return 0;
}

uint DBPerson::InsertPersonDB(MSqlQuery &query) const
{
    query.prepare(
        "INSERT IGNORE INTO people (name) "
        "VALUES (:NAME);");
    query.bindValue(":NAME", name.utf8());

    if (query.exec())
        return 1;

    MythContext::DBError("insert_person", query);
    return 0;
}

uint DBPerson::InsertCreditsDB(MSqlQuery &query, uint personid, uint chanid,
                               const QDateTime &starttime) const
{
    if (!personid)
        return 0;

    query.prepare(
        "REPLACE INTO credits "
        "       ( person,  chanid,  starttime,  role) "
        "VALUES (:PERSON, :CHANID, :STARTTIME, :ROLE) ");
    query.bindValue(":PERSON",    personid);
    query.bindValue(":CHANID",    chanid);
    query.bindValue(":STARTTIME", starttime);
    query.bindValue(":ROLE",      GetRole().utf8());

    if (query.exec())
        return 1;

    MythContext::DBError("insert_credits", query);
    return 0;
}

void DBEvent::AddPerson(DBPerson::Role role, const QString &name)
{
    if (!credits)
        credits = new DBCredits;

    credits->push_back(DBPerson(role, name));
}

uint DBEvent::UpdateDB(MSqlQuery &query, int match_threshold) const
{
    vector<DBEvent> programs;
    uint count = GetOverlappingPrograms(query, programs);
    int  match = INT_MIN;
    int  i     = -1;

    if (count)
        match = GetMatch(programs, i);

    if ((match < match_threshold) && (i >= 0))
    {
        VERBOSE(VB_EIT, QString("match[%1]: %2 '%3' vs. '%4'")
                .arg(i).arg(match).arg(title).arg(programs[i].title));
    }

    if (match >= match_threshold)
        return UpdateDB(query, programs, i);
    else if (!count)
        return InsertDB(query);
    else
        return UpdateDB(query, programs, -1);
}

uint DBEvent::GetOverlappingPrograms(MSqlQuery &query,
                                     vector<DBEvent> &programs) const
{
    uint count = 0;
    query.prepare(
        "SELECT title,          subtitle,      description, "
        "       category,       category_type, "
        "       starttime,      endtime, "
        "       subtitletypes+0,audioprop+0,   videoprop+0, "
        "       seriesid,       programid, "
        "       partnumber,     parttotal, "
        "       syndicatedepisodenumber, "
        "       airdate,        originalairdate, "
        "       previouslyshown "
        "FROM program "
        "WHERE chanid   = :CHANID AND "
        "      manualid = 0       AND "
        "      ( ( starttime >= :STIME1 AND starttime <  :ETIME1 ) OR "
        "        ( endtime   >  :STIME2 AND endtime   <= :ETIME2 ) )");
    query.bindValue(":CHANID", chanid);
    query.bindValue(":STIME1", starttime);
    query.bindValue(":ETIME1", endtime);
    query.bindValue(":STIME2", starttime);
    query.bindValue(":ETIME2", endtime);

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("GetOverlappingPrograms 1", query);
        return 0;
    }

    while (query.next())
    {
        MythCategoryType category_type =
            string_to_myth_category_type(query.value(4).toString());

        DBEvent prog(chanid,
                     QString::fromUtf8(query.value(0).toString()),
                     QString::fromUtf8(query.value(1).toString()),
                     QString::fromUtf8(query.value(2).toString()),
                     QString::fromUtf8(query.value(3).toString()),
                     category_type,
                     query.value(5).toDateTime(), query.value(6).toDateTime(),
                     fixup,
                     query.value(7).toUInt(),
                     query.value(8).toUInt(),
                     query.value(9).toUInt(),
                     QString::fromUtf8(query.value(10).toString()),
                     QString::fromUtf8(query.value(11).toString())
                     );

        prog.partnumber = query.value(12).toUInt();
        prog.parttotal  = query.value(13).toUInt();
        prog.syndicatedepisodenumber = 
                          QString::fromUtf8(query.value(14).toString());
        prog.airdate    = query.value(15).toString();
        prog.originalairdate = query.value(16).toDate();

        prog.previouslyshown = query.value(17).toBool();

        programs.push_back(prog);
        count++;
    }

    return count;
}

static int score_match(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return 0;
    else if (a == b)
        return 1000;

    QString A = a.stripWhiteSpace().upper();
    QString B = b.stripWhiteSpace().upper();
    if (A == B)
        return 1000;

    QStringList al, bl;
    al.split("", A);
    if (!al.size())
        return 0;

    bl.split("", B);
    if (!bl.size())
        return 0;

    QStringList::const_iterator ait = al.begin();
    QStringList::const_iterator bit = bl.begin();
    int score = 0;
    for (; (ait != al.end()) && (bit != bl.end()); ait++)
    {
        QStringList::const_iterator bit2 = bit;
        int dist = 0;
        int bscore = 0;
        for (; bit2 != bl.end(); bit2++)
        {
            if (*ait == *bit)
            {
                bscore = max(1000, 2000 - (dist * 500));
                break;
            }
            dist++;
        }
        if (bscore && dist < 3)
        {
            for (int i = 0; (i < dist) && bit != bl.end(); i++)
                bit++;
        }
        score += bscore;
    }
    score /= al.size();

    return max(1000, score);
}

int DBEvent::GetMatch(const vector<DBEvent> &programs, int &bestmatch) const
{
    bestmatch = -1;
    int match_val = INT_MIN;

    for (uint i = 0; i < programs.size(); i++)
    {
        int mv = 0;
        mv -= abs(starttime.secsTo(programs[i].starttime));
        mv -= abs(endtime.secsTo(programs[i].endtime));
        mv += score_match(title, programs[i].title) * 10;
        mv += score_match(subtitle, programs[i].subtitle);
        mv += score_match(description, programs[i].description);

        if (mv > match_val)
        {
            bestmatch = i;
            match_val = mv;
        }
    }

    return match_val;
}

uint DBEvent::UpdateDB(MSqlQuery &q, const vector<DBEvent> &p, int match) const
{
    // adjust/delete overlaps;
    bool ok = true;
    for (uint i = 0; i < p.size(); i++)
    {
        if (i != (uint)match)
            ok &= MoveOutOfTheWayDB(q, p[i]);
    }

    // if we failed to move programs out of the way, don't insert new ones..
    if (!ok)
        return 0;

    // if no match, insert current item
    if ((match < 0) || ((uint)match >= p.size()))
        return InsertDB(q);

    // update matched item with current data
    return UpdateDB(q, p[match]);
}

uint DBEvent::UpdateDB(MSqlQuery &query, const DBEvent &match) const
{
    QString ltitle    = title;
    QString lsubtitle = subtitle;
    QString ldesc     = description;
    QString lcategory = category;
    QString lairdate  = airdate;
    QString lprogramId = AddAuthority(programId, query);
    QString lseriesId = AddAuthority(seriesId, query);
    QDate loriginalairdate = originalairdate;

    if (match.title.length() >= ltitle.length())
        ltitle = match.title;

    if (match.subtitle.length() >= lsubtitle.length())
        lsubtitle = match.subtitle;

    if (match.description.length() >= ldesc.length())
        ldesc = match.description;

    if (lcategory.isEmpty() && !match.category.isEmpty())
        lcategory = match.category;

    if (lairdate.isEmpty() && !match.airdate.isEmpty() && match.airdate != '0')
        lairdate = match.airdate;

    if (!loriginalairdate.isValid() && match.originalairdate.isValid())
        loriginalairdate = match.originalairdate;

    if (lprogramId.isEmpty() && !match.programId.isEmpty())
        lprogramId = match.programId;

    if (lseriesId.isEmpty() && !match.seriesId.isEmpty())
        lseriesId = match.seriesId;

    uint tmp = category_type;
    if (!category_type && match.category_type)
        tmp = match.category_type;

    QString lcattype = myth_category_type_to_string(tmp);

    unsigned char lsubtype = subtitleType | match.subtitleType;
    unsigned char laudio   = audioProps   | match.audioProps;
    unsigned char lvideo   = videoProps   | match.videoProps;

    uint lpartnumber =
        (!partnumber && match.partnumber) ? match.partnumber : partnumber;
    uint lparttotal =
        (!parttotal  && match.parttotal ) ? match.parttotal  : parttotal;

    bool lpreviouslyshown = previouslyshown | match.previouslyshown;

    QString lsyndicatedepisodenumber = syndicatedepisodenumber;
    if (lsyndicatedepisodenumber.isEmpty() && 
        !match.syndicatedepisodenumber.isEmpty())
        lsyndicatedepisodenumber = match.syndicatedepisodenumber;

    query.prepare(
        "UPDATE program "
        "SET title          = :TITLE,     subtitle      = :SUBTITLE, "
        "    description    = :DESC, "
        "    category       = :CAT,       category_type = :CATTYPE, "
        "    starttime      = :STARTTIME, endtime       = :ENDTIME, "
        "    closecaptioned = :CC,        subtitled     = :SUBTITLED, "
        "    stereo         = :STEREO,    hdtv          = :HDTV, "
        "    subtitletypes  = :SUBTYPE, "
        "    audioprop      = :AUDIOPROP, videoprop     = :VIDEOPROP, "
        "    partnumber     = :PARTNO,    parttotal     = :PARTTOTAL, "
        "    syndicatedepisodenumber = :SYNDICATENO, "
        "    airdate        = :AIRDATE,   originalairdate=:ORIGAIRDATE, "
        "    listingsource  = :LSOURCE, "
        "    seriesid       = :SERIESID,  programid     = :PROGRAMID, "
        "    previouslyshown = :PREVSHOWN "
        "WHERE chanid    = :CHANID AND "
        "      starttime = :OLDSTART ");

    query.bindValue(":CHANID",      chanid);
    query.bindValue(":OLDSTART",    match.starttime);
    query.bindValue(":TITLE",       ltitle.utf8());
    query.bindValue(":SUBTITLE",    lsubtitle.utf8());
    query.bindValue(":DESC",        ldesc.utf8());
    query.bindValue(":CAT",         lcategory.utf8());
    query.bindValue(":CATTYPE",     lcattype.utf8());
    query.bindValue(":STARTTIME",   starttime);
    query.bindValue(":ENDTIME",     endtime);
    query.bindValue(":CC",          lsubtype & SUB_HARDHEAR ? true : false);
    query.bindValue(":SUBTITLED",   lsubtype & SUB_NORMAL   ? true : false);
    query.bindValue(":STEREO",      laudio   & AUD_STEREO   ? true : false);
    query.bindValue(":HDTV",        lvideo   & VID_HDTV     ? true : false);
    query.bindValue(":SUBTYPE",     lsubtype);
    query.bindValue(":AUDIOPROP",   laudio);
    query.bindValue(":VIDEOPROP",   lvideo);
    query.bindValue(":PARTNO",      lpartnumber);
    query.bindValue(":PARTTOTAL",   lparttotal);
    query.bindValue(":SYNDICATENO", lsyndicatedepisodenumber.utf8());
    query.bindValue(":AIRDATE",     lairdate.isEmpty() ? "0000" : lairdate);
    query.bindValue(":ORIGAIRDATE", loriginalairdate);
    query.bindValue(":LSOURCE",     1);
    query.bindValue(":SERIESID",    lseriesId.utf8());
    query.bindValue(":PROGRAMID",   lprogramId.utf8());
    query.bindValue(":PREVSHOWN",   lpreviouslyshown);

    if (!query.exec())
    {
        MythContext::DBError("InsertDB", query);
        return 0;
    }

    if (credits)
    {
        for (uint i = 0; i < credits->size(); i++)
            (*credits)[i].InsertDB(query, chanid, starttime);
    }

    return 1;
}

static bool delete_program(MSqlQuery &query, uint chanid, const QDateTime &st)
{
    query.prepare(
        "DELETE from program "
        "WHERE chanid    = :CHANID AND "
        "      starttime = :STARTTIME");

    query.bindValue(":CHANID",    chanid);
    query.bindValue(":STARTTIME", st);

    if (!query.exec())
    {
        MythContext::DBError("delete_program", query);
        return false;
    }

    query.prepare(
        "DELETE from credits "
        "WHERE chanid    = :CHANID AND "
        "      starttime = :STARTTIME");

    query.bindValue(":CHANID",    chanid);
    query.bindValue(":STARTTIME", st);

    if (!query.exec())
    {
        MythContext::DBError("delete_credits", query);
        return false;
    }

    return true;
}

static bool change_program(MSqlQuery &query, uint chanid, const QDateTime &st,
                           const QDateTime &new_st, const QDateTime &new_end)
{
    query.prepare(
        "UPDATE program "
        "SET starttime = :NEWSTART, "
        "    endtime   = :NEWEND "
        "WHERE chanid    = :CHANID AND "
        "      starttime = :OLDSTART");

    query.bindValue(":CHANID",   chanid);
    query.bindValue(":OLDSTART", st);
    query.bindValue(":NEWSTART", new_st);
    query.bindValue(":NEWEND",   new_end);
    
    if (!query.exec())
    {
        MythContext::DBError("change_program", query);
        return false;
    }

    query.prepare(
        "UPDATE credits "
        "SET starttime = :NEWSTART "
        "WHERE chanid    = :CHANID AND "
        "      starttime = :OLDSTART");

    query.bindValue(":CHANID",   chanid);
    query.bindValue(":OLDSTART", st);
    query.bindValue(":NEWSTART", new_st);

    if (!query.exec())
    {
        MythContext::DBError("change_credits", query);
        return false;
    }

    return true;    
}

bool DBEvent::MoveOutOfTheWayDB(MSqlQuery &query, const DBEvent &prog) const
{
    if (prog.starttime >= starttime && prog.endtime <= endtime)
    {
        // inside current program
        return delete_program(query, chanid, prog.starttime);
    }
    else if (prog.starttime < starttime && prog.endtime > starttime)
    {
        // starts before, but ends during our program
        return change_program(query, chanid, prog.starttime,
                              prog.starttime, starttime);
    }
    else if (prog.starttime < endtime && prog.endtime > endtime)
    {
        // starts during, but ends after our program
        return change_program(query, chanid, prog.starttime,
                              endtime, prog.endtime);
    }
    // must be non-conflicting...
    return true;
}

uint DBEvent::InsertDB(MSqlQuery &query) const
{
    QString lprogramId = AddAuthority(programId, query);
    QString lseriesId  = AddAuthority(seriesId, query);

    query.prepare(
        "REPLACE INTO program ("
        "  chanid,         title,          subtitle,        description, "
        "  category,       category_type, "
        "  starttime,      endtime, "
        "  closecaptioned, stereo,         hdtv,            subtitled, "
        "  subtitletypes,  audioprop,      videoprop, "
        "  partnumber,     parttotal, "
        "  syndicatedepisodenumber, "
        "  airdate,        originalairdate,listingsource, "
        "  seriesid,       programid,      previouslyshown ) "
        "VALUES ("
        " :CHANID,        :TITLE,         :SUBTITLE,       :DESCRIPTION, "
        " :CATEGORY,      :CATTYPE, "
        " :STARTTIME,     :ENDTIME, "
        " :CC,            :STEREO,        :HDTV,           :SUBTITLED, "
        " :SUBTYPES,      :AUDIOPROP,     :VIDEOPROP, "
        " :PARTNUMBER,    :PARTTOTAL, "
        " :SYNDICATENO, "
        " :AIRDATE,       :ORIGAIRDATE,   :LSOURCE, "
        " :SERIESID,      :PROGRAMID,     :PREVSHOWN) ");

    QString cattype = myth_category_type_to_string(category_type);

    query.bindValue(":CHANID",      chanid);
    query.bindValue(":TITLE",       title.utf8());
    query.bindValue(":SUBTITLE",    subtitle.utf8());
    query.bindValue(":DESCRIPTION", description.utf8());
    query.bindValue(":CATEGORY",    category.utf8());
    query.bindValue(":CATTYPE",     cattype.utf8());
    query.bindValue(":STARTTIME",   starttime);
    query.bindValue(":ENDTIME",     endtime);
    query.bindValue(":CC",          subtitleType & SUB_HARDHEAR ? true : false);
    query.bindValue(":STEREO",      audioProps   & AUD_STEREO   ? true : false);
    query.bindValue(":HDTV",        videoProps   & VID_HDTV     ? true : false);
    query.bindValue(":SUBTITLED",   subtitleType & SUB_NORMAL   ? true : false);
    query.bindValue(":SUBTYPES",    subtitleType);
    query.bindValue(":AUDIOPROP",   audioProps);
    query.bindValue(":VIDEOPROP",   videoProps);
    query.bindValue(":PARTNUMBER",  partnumber);
    query.bindValue(":PARTTOTAL",   parttotal);
    query.bindValue(":SYNDICATENO", syndicatedepisodenumber.utf8());
    query.bindValue(":AIRDATE",     airdate.isEmpty() ? "0000" : airdate);
    query.bindValue(":ORIGAIRDATE", originalairdate);
    query.bindValue(":LSOURCE",     1);
    query.bindValue(":SERIESID",    lseriesId.utf8());
    query.bindValue(":PROGRAMID",   lprogramId.utf8());
    query.bindValue(":PREVSHOWN",   previouslyshown);

    if (!query.exec())
    {
        MythContext::DBError("InsertDB", query);
        return 0;
    }

    if (credits)
    {
        for (uint i = 0; i < credits->size(); i++)
            (*credits)[i].InsertDB(query, chanid, starttime);
    }

    return 1;
}

// If a series id or program id does not have an authority add the default.
QString DBEvent::AddAuthority(const QString& id, MSqlQuery &query) const
{
    if (id.length() == 0 || id[0] != '/')
        return id;

    query.prepare("SELECT default_authority "
        "FROM channel "
        "WHERE chanid    = :CHANID");

    query.bindValue(":CHANID",      chanid);

    if (!query.exec())
    {
        MythContext::DBError("AddAuthority", query);
        return id;
    }

    if (query.next())
        return query.value(0).toString() + id;
    else
        return id;
}
