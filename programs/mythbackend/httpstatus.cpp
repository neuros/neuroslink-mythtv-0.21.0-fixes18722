//////////////////////////////////////////////////////////////////////////////
// Program Name: httpstatus.cpp
//                                                                            
// Purpose - Html & XML status HttpServerExtension
//                                                                            
// Created By  : David Blain                    Created On : Oct. 24, 2005
// Modified By :                                Modified On:                  
//                                                                            
//////////////////////////////////////////////////////////////////////////////

#include "httpstatus.h"
#include "backendutil.h"
#include "mythxml.h"

#include "libmyth/mythcontext.h"
#include "libmyth/util.h"
#include "libmyth/mythdbcon.h"
#include "libmyth/compat.h"

#include <qtextstream.h>
#include <qdir.h>
#include <qfile.h>
#include <qregexp.h>
#include <qbuffer.h>
#include <qlocale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../../config.h"

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

HttpStatus::HttpStatus( QMap<int, EncoderLink *> *tvList, Scheduler *sched, AutoExpire *expirer, bool bIsMaster )
          : HttpServerExtension( "HttpStatus" )
{
    m_pEncoders = tvList;
    m_pSched    = sched;
    m_pExpirer  = expirer;
    m_bIsMaster = bIsMaster;

    m_nPreRollSeconds = gContext->GetNumSetting("RecordPreRoll", 0);
}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

HttpStatus::~HttpStatus()
{
}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

HttpStatusMethod HttpStatus::GetMethod( const QString &sURI )
{
    if (sURI == ""                     ) return( HSM_GetStatusHTML   );
    if (sURI == "GetStatusHTML"        ) return( HSM_GetStatusHTML   );
    if (sURI == "GetStatus"            ) return( HSM_GetStatusXML    );
    if (sURI == "xml"                  ) return( HSM_GetStatusXML    );

    return( HSM_Unknown );
}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

bool HttpStatus::ProcessRequest( HttpWorkerThread * /* pThread */, HTTPRequest *pRequest )
{
    try
    {
        if (pRequest)
        {
            if (pRequest->m_sBaseUrl != "/")
                return( false );

            switch( GetMethod( pRequest->m_sMethod ))
            {
                case HSM_GetStatusXML   : GetStatusXML   ( pRequest ); return true;
                case HSM_GetStatusHTML  : GetStatusHTML  ( pRequest ); return true; 

                default: 
                {
                    pRequest->m_eResponseType   = ResponseTypeHTML;
                    pRequest->m_nResponseStatus = 200;

                    break;
                }
            }
        }
    }
    catch( ... )
    {
        cerr << "HttpStatus::ProcessRequest() - Unexpected Exception" << endl;
    }

    return( false );
}           

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

void HttpStatus::GetStatusXML( HTTPRequest *pRequest )
{
    QDomDocument doc( "Status" );                        

    FillStatusXML( &doc );

    pRequest->m_eResponseType   = ResponseTypeXML;
    pRequest->m_mapRespHeaders[ "Cache-Control" ] = "no-cache=\"Ext\", max-age = 5000";
    pRequest->m_response << doc.toString();
}

/////////////////////////////////////////////////////////////////////////////
//                  
/////////////////////////////////////////////////////////////////////////////

void HttpStatus::GetStatusHTML( HTTPRequest *pRequest )
{
    pRequest->m_eResponseType = ResponseTypeHTML;
    pRequest->m_mapRespHeaders[ "Cache-Control" ] = "no-cache=\"Ext\", max-age = 5000";

    QDomDocument doc( "Status" );                        

    FillStatusXML( &doc );

    PrintStatus( pRequest->m_response, &doc );
}

void HttpStatus::FillStatusXML( QDomDocument *pDoc )
{
    QString   dateFormat   = gContext->GetSetting("DateFormat", "M/d/yyyy");

    if (dateFormat.find(QRegExp("yyyy")) < 0)
        dateFormat += " yyyy";

    QString   shortdateformat = gContext->GetSetting("ShortDateFormat", "M/d");
    QString   timeformat      = gContext->GetSetting("TimeFormat", "h:mm AP");
    QDateTime qdtNow          = QDateTime::currentDateTime();

    // Add Root Node.

    QDomElement root = pDoc->createElement("Status");
    pDoc->appendChild(root);

    root.setAttribute("date"    , qdtNow.toString(dateFormat));
    root.setAttribute("time"    , qdtNow.toString(timeformat)   );
    root.setAttribute("ISODate" , qdtNow.toString(Qt::ISODate)  );
    root.setAttribute("version" , MYTH_BINARY_VERSION           );
    root.setAttribute("protoVer", MYTH_PROTO_VERSION            );

    // Add all encoders, if any

    QDomElement encoders = pDoc->createElement("Encoders");
    root.appendChild(encoders);

    int  numencoders = 0;
    bool isLocal     = true;

    QMap<int, EncoderLink *>::Iterator iter = m_pEncoders->begin();

    for (; iter != m_pEncoders->end(); ++iter)
    {
        EncoderLink *elink = iter.data();

        if (elink != NULL)
        {
            isLocal = elink->IsLocal();

            QDomElement encoder = pDoc->createElement("Encoder");
            encoders.appendChild(encoder);

            encoder.setAttribute("id"            , elink->GetCardID()       );
            encoder.setAttribute("local"         , isLocal                  );
            encoder.setAttribute("connected"     , elink->IsConnected()     );
            encoder.setAttribute("state"         , elink->GetState()        );
            //encoder.setAttribute("lowOnFreeSpace", elink->isLowOnFreeSpace());

            if (isLocal)
                encoder.setAttribute("hostname", gContext->GetHostName());
            else
                encoder.setAttribute("hostname", elink->GetHostName());

            if (elink->IsConnected())
                numencoders++;

            switch (elink->GetState())
            {
                case kState_WatchingLiveTV:
                case kState_RecordingOnly:
                case kState_WatchingRecording:
                {
                    ProgramInfo *pInfo = elink->GetRecording();

                    if (pInfo)
                    {
                        MythXML::FillProgramInfo(pDoc, encoder, pInfo);
                        delete pInfo;
                    }

                    break;
                }

                default:
                    break;
            }
        }
    }

    encoders.setAttribute("count", numencoders);

    // Add upcoming shows

    QDomElement scheduled = pDoc->createElement("Scheduled");
    root.appendChild(scheduled);

    RecList recordingList;

    if (m_pSched)
        m_pSched->getAllPending(&recordingList);

    unsigned int iNum = 10;
    unsigned int iNumRecordings = 0;

    RecConstIter itProg = recordingList.begin();
    for (; (itProg != recordingList.end()) && iNumRecordings < iNum; itProg++)
    {
        if (((*itProg)->recstatus  <= rsWillRecord) &&
            ((*itProg)->recstartts >= QDateTime::currentDateTime()))
        {
            iNumRecordings++;
            MythXML::FillProgramInfo(pDoc, scheduled, *itProg);
        }
    }

    while (recordingList.size() > 0)
    {
        ProgramInfo *pginfo = recordingList.back();
        delete pginfo;
        recordingList.pop_back();
    }

    scheduled.setAttribute("count", iNumRecordings);

    // Add Job Queue Entries

    QDomElement jobqueue = pDoc->createElement("JobQueue");
    root.appendChild(jobqueue);

    QMap<int, JobQueueEntry> jobs;
    QMap<int, JobQueueEntry>::Iterator it;

    JobQueue::GetJobsInQueue(jobs,
                             JOB_LIST_NOT_DONE | JOB_LIST_ERROR |
                             JOB_LIST_RECENT);

    if (jobs.size())
    {
        for (it = jobs.begin(); it != jobs.end(); ++it)
        {
            ProgramInfo *pInfo;

            pInfo = ProgramInfo::GetProgramFromRecorded(it.data().chanid,
                                                        it.data().starttime);

            if (!pInfo)
                continue;

            QDomElement job = pDoc->createElement("Job");
            jobqueue.appendChild(job);

            job.setAttribute("id"        , it.data().id         );
            job.setAttribute("chanId"    , it.data().chanid     );
            job.setAttribute("startTime" , it.data().starttime.toString(Qt::ISODate));
            job.setAttribute("startTs"   , it.data().startts    );
            job.setAttribute("insertTime", it.data().inserttime.toString(Qt::ISODate));
            job.setAttribute("type"      , it.data().type       );
            job.setAttribute("cmds"      , it.data().cmds       );
            job.setAttribute("flags"     , it.data().flags      );
            job.setAttribute("status"    , it.data().status     );
            job.setAttribute("statusTime", it.data().statustime.toString(Qt::ISODate));
            job.setAttribute("schedTime" , it.data().schedruntime.toString(Qt::ISODate));
            job.setAttribute("args"      , it.data().args       );

            if (it.data().hostname == "")
                job.setAttribute("hostname", QObject::tr("master"));
            else
                job.setAttribute("hostname",it.data().hostname);

            QDomText textNode = pDoc->createTextNode(it.data().comment);
            job.appendChild(textNode);

            MythXML::FillProgramInfo(pDoc, job, pInfo);

            delete pInfo;
        }
    }

    jobqueue.setAttribute( "count", jobs.size() );

    // Add Machine information

    QDomElement mInfo   = pDoc->createElement("MachineInfo");
    QDomElement storage = pDoc->createElement("Storage"    );
    QDomElement load    = pDoc->createElement("Load"       );
    QDomElement guide   = pDoc->createElement("Guide"      );

    root.appendChild (mInfo  );
    mInfo.appendChild(storage);
    mInfo.appendChild(load   );
    mInfo.appendChild(guide  );

    // drive space   --------------------- 

    QStringList strlist;
    QString dirs = "";
    QString hostname;
    QString directory;
    QString isLocalstr;
    QString fsID;
    QString ids;
    long long iTotal = -1, iUsed = -1, iAvail = -1; 

    BackendQueryDiskSpace(strlist, m_pEncoders, true, m_bIsMaster);

    QStringList::const_iterator sit = strlist.begin();
    while (sit != strlist.end())
    {
        hostname   = *(sit++);
        directory  = *(sit++);
        isLocalstr = *(sit++);
        fsID       = *(sit++);
        sit++; // ignore dirID
        iTotal     = decodeLongLong(strlist, sit);
        iUsed      = decodeLongLong(strlist, sit);
        iAvail     = iTotal - iUsed;

        if (fsID == "-2")
            fsID = "total";

        if (ids == "")
            ids = fsID;
        else
            ids = ids + "," + fsID;

        storage.setAttribute("drive_" + fsID + "_total", (int)(iTotal>>10)); 
        storage.setAttribute("drive_" + fsID + "_used" , (int)(iUsed>>10)); 
        storage.setAttribute("drive_" + fsID + "_free" , (int)(iAvail>>10)); 
        storage.setAttribute("drive_" + fsID + "_dirs" , directory); 
    }
 
    storage.setAttribute("fsids", ids); 

    // load average ---------------------

    double rgdAverages[3];

    if (getloadavg(rgdAverages, 3) != -1)
    {
        load.setAttribute("avg1", rgdAverages[0]);
        load.setAttribute("avg2", rgdAverages[1]);
        load.setAttribute("avg3", rgdAverages[2]);
    }

    // Guide Data ---------------------

    QDateTime GuideDataThrough;

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT MAX(endtime) FROM program WHERE manualid = 0;");

    if (query.exec() && query.isActive() && query.size())
    {
        query.next();

        if (query.isValid())
            GuideDataThrough = QDateTime::fromString(query.value(0).toString(),
                                                     Qt::ISODate);
    }

    guide.setAttribute("start", gContext->GetSetting("mythfilldatabaseLastRunStart"));
    guide.setAttribute("end", gContext->GetSetting("mythfilldatabaseLastRunEnd"));
    guide.setAttribute("status", gContext->GetSetting("mythfilldatabaseLastRunStatus"));
    if (gContext->GetNumSetting("MythFillGrabberSuggestsTime", 0))
    {
        guide.setAttribute("next",
            gContext->GetSetting("MythFillSuggestedRunTime"));
    }

    if (!GuideDataThrough.isNull())
    {
        guide.setAttribute("guideThru", QDateTime(GuideDataThrough).toString(Qt::ISODate));
        guide.setAttribute("guideDays", qdtNow.daysTo(GuideDataThrough));
    }

    QDomText dataDirectMessage = pDoc->createTextNode(gContext->GetSetting("DataDirectMessage"));
    guide.appendChild(dataDirectMessage);

    // Add Miscellaneous information

    // TODO: Add GUI control/setting for info_script
    QString info_script = gContext->GetSetting("MiscStatusScript");
    if ((!info_script.isEmpty()) && (info_script != "none"))
    {
        QDomElement misc = pDoc->createElement("Miscellaneous");
        root.appendChild(misc);

        FILE *fp = popen(info_script.ascii(), "r");

        if (fp)
        {
            char buffer[256];
            QString input = "";

            while (fgets(buffer, sizeof(buffer), fp))
            {
                input.append(QString::fromLocal8Bit(buffer));
            }

            if (pclose(fp))
            {
                VERBOSE(VB_IMPORTANT, QString("Error running miscellaneous "
                        "status information script: %1").arg(info_script));
            }
            else
            {
                QStringList output = QStringList::split("\n", input, false);

                QStringList::iterator iter = output.begin();
                for (; iter != output.end(); iter++)
                {
                    QDomElement info = pDoc->createElement("Information");

                    QStringList list = QStringList::split("[]:[]", *iter, true);
                    unsigned int size = list.size();
                    unsigned int hasAttributes = 0;

                    // TODO: escape XML
                    if ((size > 0) && (!list[0].isEmpty()))
                    {
                        info.setAttribute("display", list[0]);
                        hasAttributes++;
                    }
                    if ((size > 1) && (!list[1].isEmpty()))
                    {
                        info.setAttribute("name", list[1]);
                        hasAttributes++;
                    }
                    if ((size > 2) && (!list[2].isEmpty()))
                    {
                        info.setAttribute("value", list[2]);
                        hasAttributes++;
                    }

                    if (hasAttributes > 0)
                        misc.appendChild(info);
                }
            }
        }
        else
        {
            VERBOSE(VB_IMPORTANT, QString("Failed to run miscellaneous status "
                    "information script: %1").arg(info_script));
        }
    }

}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

void HttpStatus::PrintStatus( QTextStream &os, QDomDocument *pDoc )
{
    
    QString shortdateformat = gContext->GetSetting("ShortDateFormat", "M/d");
    QString timeformat      = gContext->GetSetting("TimeFormat", "h:mm AP");

    os.setEncoding(QTextStream::UnicodeUTF8);

    QDateTime qdtNow = QDateTime::currentDateTime();

    QDomElement docElem = pDoc->documentElement();

    os << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" "
       << "\"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
       << "<html xmlns=\"http://www.w3.org/1999/xhtml\""
       << " xml:lang=\"en\" lang=\"en\">\r\n"
       << "<head>\r\n"
       << "  <meta http-equiv=\"Content-Type\""
       << "content=\"text/html; charset=UTF-8\" />\r\n"
       << "  <style type=\"text/css\" title=\"Default\" media=\"all\">\r\n"
       << "  <!--\r\n"
       << "  body {\r\n"
       << "    background-color:#fff;\r\n"
       << "    font:11px verdana, arial, helvetica, sans-serif;\r\n"
       << "    margin:20px;\r\n"
       << "  }\r\n"
       << "  h1 {\r\n"
       << "    font-size:28px;\r\n"
       << "    font-weight:900;\r\n"
       << "    color:#ccc;\r\n"
       << "    letter-spacing:0.5em;\r\n"
       << "    margin-bottom:30px;\r\n"
       << "    width:650px;\r\n"
       << "    text-align:center;\r\n"
       << "  }\r\n"
       << "  h2 {\r\n"
       << "    font-size:18px;\r\n"
       << "    font-weight:800;\r\n"
       << "    color:#360;\r\n"
       << "    border:none;\r\n"
       << "    letter-spacing:0.3em;\r\n"
       << "    padding:0px;\r\n"
       << "    margin-bottom:10px;\r\n"
       << "    margin-top:0px;\r\n"
       << "  }\r\n"
       << "  hr {\r\n"
       << "    display:none;\r\n"
       << "  }\r\n"
       << "  div.content {\r\n"
       << "    width:650px;\r\n"
       << "    border-top:1px solid #000;\r\n"
       << "    border-right:1px solid #000;\r\n"
       << "    border-bottom:1px solid #000;\r\n"
       << "    border-left:10px solid #000;\r\n"
       << "    padding:10px;\r\n"
       << "    margin-bottom:30px;\r\n"
       << "    -moz-border-radius:8px 0px 0px 8px;\r\n"
       << "  }\r\n"
       << "  div.schedule a {\r\n"
       << "    display:block;\r\n"
       << "    color:#000;\r\n"
       << "    text-decoration:none;\r\n"
       << "    padding:.2em .8em;\r\n"
       << "    border:thin solid #fff;\r\n"
       << "    width:350px;\r\n"
       << "  }\r\n"
       << "  div.schedule a span {\r\n"
       << "    display:none;\r\n"
       << "  }\r\n"
       << "  div.schedule a:hover {\r\n"
       << "    background-color:#F4F4F4;\r\n"
       << "    border-top:thin solid #000;\r\n"
       << "    border-bottom:thin solid #000;\r\n"
       << "    border-left:thin solid #000;\r\n"
       << "    cursor:default;\r\n"
       << "  }\r\n"
       << "  div.schedule a:hover span {\r\n"
       << "    display:block;\r\n"
       << "    position:absolute;\r\n"
       << "    background-color:#F4F4F4;\r\n"
       << "    color:#000;\r\n"
       << "    left:400px;\r\n"
       << "    margin-top:-20px;\r\n"
       << "    width:280px;\r\n"
       << "    padding:5px;\r\n"
       << "    border:thin dashed #000;\r\n"
       << "  }\r\n"
       << "  div.loadstatus {\r\n"
       << "    width:325px;\r\n"
       << "    height:7em;\r\n"
       << "  }\r\n"
       << "  .jobfinished { color: #0000ff; }\r\n"
       << "  .jobaborted { color: #7f0000; }\r\n"
       << "  .joberrored { color: #ff0000; }\r\n"
       << "  .jobrunning { color: #005f00; }\r\n"
       << "  .jobqueued  {  }\r\n"
       << "  -->\r\n"
       << "  </style>\r\n"
       << "  <title>MythTV Status - " 
       << docElem.attribute( "date", qdtNow.toString(shortdateformat)  )
       << " " 
       << docElem.attribute( "time", qdtNow.toString(timeformat) ) << " - "
       << docElem.attribute( "version", MYTH_BINARY_VERSION ) << "</title>\r\n"
       << "</head>\r\n"
       << "<body>\r\n\r\n"
       << "  <h1>MythTV Status</h1>\r\n";

    int nNumEncoders = 0;

    // encoder information ---------------------

    QDomNode node = docElem.namedItem( "Encoders" );

    if (!node.isNull())
        nNumEncoders = PrintEncoderStatus( os, node.toElement() );

    // upcoming shows --------------------------

    node = docElem.namedItem( "Scheduled" );

    if (!node.isNull())
        PrintScheduled( os, node.toElement());

    // Job Queue Entries -----------------------

    node = docElem.namedItem( "JobQueue" );

    if (!node.isNull())
        PrintJobQueue( os, node.toElement());


    // Machine information ---------------------

    node = docElem.namedItem( "MachineInfo" );

    if (!node.isNull())
        PrintMachineInfo( os, node.toElement());

    // Miscellaneous information ---------------

    node = docElem.namedItem( "Miscellaneous" );

    if (!node.isNull())
        PrintMiscellaneousInfo( os, node.toElement());

    os << "\r\n</body>\r\n</html>\r\n";

}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

int HttpStatus::PrintEncoderStatus( QTextStream &os, QDomElement encoders )
{
    QString timeformat   = gContext->GetSetting("TimeFormat", "h:mm AP");
    int     nNumEncoders = 0;

    if (encoders.isNull())
        return 0;

    os << "  <div class=\"content\">\r\n"
       << "    <h2>Encoder status</h2>\r\n";

    QDomNode node = encoders.firstChild();

    while (!node.isNull())
    {
        QDomElement e = node.toElement();

        if (!e.isNull())
        {
            if (e.tagName() == "Encoder")
            {
                QString sIsLocal  = (e.attribute( "local"    , "remote" )== "1")
                                                           ? "local" : "remote";
                QString sCardId   =  e.attribute( "id"       , "0"      );
                QString sHostName =  e.attribute( "hostname" , "Unknown");
                bool    bConnected=  e.attribute( "connected", "0"      ).toInt();

                bool bIsLowOnFreeSpace=e.attribute( "lowOnFreeSpace", "0").toInt();
                                     
                os << "    Encoder " << sCardId << " is " << sIsLocal 
                   << " on " << sHostName;

                if ((sIsLocal == "remote") && !bConnected)
                {
                    os << " (currently not connected).<br />";

                    node = node.nextSibling();
                    continue;
                }

                nNumEncoders++;

                TVState encState = (TVState) e.attribute( "state", "0").toInt();

                switch( encState )
                {   
                    case kState_WatchingLiveTV:
                        os << " and is watching Live TV";
                        break;

                    case kState_RecordingOnly:
                    case kState_WatchingRecording:
                        os << " and is recording";
                        break;

                    default:
                        os << " and is not recording.";
                        break;
                }

                // Display first Program Element listed under the encoder

                QDomNode tmpNode = e.namedItem( "Program" );

                if (!tmpNode.isNull())
                {
                    QDomElement program  = tmpNode.toElement();  

                    if (!program.isNull())
                    {
                        os << ": '" << program.attribute( "title", "Unknown" ) << "'";

                        // Get Channel information
                        
                        tmpNode = program.namedItem( "Channel" );

                        if (!tmpNode.isNull())
                        {
                            QDomElement channel = tmpNode.toElement();

                            if (!channel.isNull())
                                os <<  " on "  
                                   << channel.attribute( "callSign", "unknown" );
                        }

                        // Get Recording Information (if any)
                        
                        tmpNode = program.namedItem( "Recording" );

                        if (!tmpNode.isNull())
                        {
                            QDomElement recording = tmpNode.toElement();

                            if (!recording.isNull())
                            {
                                QDateTime endTs = QDateTime::fromString( 
                                         recording.attribute( "recEndTs", "" ),
                                         Qt::ISODate );

                                os << ". This recording will end "
                                   << "at " << endTs.toString(timeformat);
                            }
                        }
                    }

                    os << ".";
                }

                if (bIsLowOnFreeSpace)
                {
                    os << " <strong>WARNING</strong>:"
                       << " This backend is low on free disk space!";
                }

                os << "<br />\r\n";
            }
        }

        node = node.nextSibling();
    }

    os << "  </div>\r\n\r\n";

    return( nNumEncoders );
}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

int HttpStatus::PrintScheduled( QTextStream &os, QDomElement scheduled )
{
    QDateTime qdtNow          = QDateTime::currentDateTime();
    QString   shortdateformat = gContext->GetSetting("ShortDateFormat", "M/d");
    QString   timeformat      = gContext->GetSetting("TimeFormat", "h:mm AP");

    if (scheduled.isNull())
        return( 0 );

    int     nNumRecordings= scheduled.attribute( "count", "0" ).toInt();
    
    os << "  <div class=\"content\">\r\n"
       << "    <h2>Schedule</h2>\r\n";

    if (nNumRecordings == 0)
    {
        os << "    There are no shows scheduled for recording.\r\n"
           << "    </div>\r\n";
        return( 0 );
    }

    os << "    The next " << nNumRecordings << " show" << (nNumRecordings == 1 ? "" : "s" )
       << " that " << (nNumRecordings == 1 ? "is" : "are") 
       << " scheduled for recording:\r\n";

    os << "    <div class=\"schedule\">\r\n";

    // Iterate through all scheduled programs

    QDomNode node = scheduled.firstChild();

    while (!node.isNull())
    {
        QDomElement e = node.toElement();

        if (!e.isNull())
        {
            QDomNode recNode  = e.namedItem( "Recording" );
            QDomNode chanNode = e.namedItem( "Channel"   );

            if ((e.tagName() == "Program") && !recNode.isNull() && !chanNode.isNull())
            {
                QDomElement r =  recNode.toElement();
                QDomElement c =  chanNode.toElement();

                QString   sTitle       = e.attribute( "title"   , "" );    
                QString   sSubTitle    = e.attribute( "subTitle", "" );
                QDateTime startTs      = QDateTime::fromString( e.attribute( "startTime" ,"" ), Qt::ISODate );
                QDateTime endTs        = QDateTime::fromString( e.attribute( "endTime"   ,"" ), Qt::ISODate );
                QDateTime recStartTs   = QDateTime::fromString( r.attribute( "recStartTs","" ), Qt::ISODate );
//                QDateTime recEndTs     = QDateTime::fromString( r.attribute( "recEndTs"  ,"" ), Qt::ISODate );
                int       nPreRollSecs = r.attribute( "preRollSeconds", "0" ).toInt();
                int       nEncoderId   = r.attribute( "encoderId"     , "0" ).toInt();
                QString   sProfile     = r.attribute( "recProfile"    , ""  );
                QString   sChanName    = c.attribute( "channelName"   , ""  );
                QString   sDesc        = "";

                QDomText  text         = e.firstChild().toText();
                if (!text.isNull())
                    sDesc = text.nodeValue();

                // Build Time to recording start.

                int nTotalSecs = qdtNow.secsTo( recStartTs ) - nPreRollSecs;

                //since we're not displaying seconds
    
                nTotalSecs -= 60;          

                int nTotalDays  =  nTotalSecs / 86400;
                int nTotalHours = (nTotalSecs / 3600)
                                - (nTotalDays * 24);
                int nTotalMins  = (nTotalSecs / 60) % 60;

                QString sTimeToStart = "in";

                if ( nTotalDays > 1 )
                    sTimeToStart += QString(" %1 days,").arg( nTotalDays );
                else if ( nTotalDays == 1 )
                    sTimeToStart += (" 1 day,");

                if ( nTotalHours != 1)
                    sTimeToStart += QString(" %1 hours and").arg( nTotalHours );
                else if (nTotalHours == 1)
                    sTimeToStart += " 1 hour and";
 
                if ( nTotalMins != 1)
                    sTimeToStart += QString(" %1 minutes").arg( nTotalMins );
                else
                    sTimeToStart += " 1 minute";

                if ( nTotalHours == 0 && nTotalMins == 0)
                    sTimeToStart = "within one minute";

                if ( nTotalSecs < 0)
                    sTimeToStart = "soon";

                    // Output HTML

                os << "      <a href=\"#\">";
                if (shortdateformat.find("ddd") == -1) {
                    // If day-of-week not already present somewhere, prepend it.
                    os << recStartTs.addSecs(-nPreRollSecs).toString("ddd")
                        << " ";
                }
                os << recStartTs.addSecs(-nPreRollSecs).toString(shortdateformat) << " "
                   << recStartTs.addSecs(-nPreRollSecs).toString(timeformat) << " - ";

                if (nEncoderId > 0)
                    os << "Encoder " << nEncoderId << " - ";

                os << sChanName << " - " << sTitle << "<br />"
                   << "<span><strong>" << sTitle << "</strong> ("
                   << startTs.toString(timeformat) << "-"
                   << endTs.toString(timeformat) << ")<br />";

                if ( !sSubTitle.isNull() && !sSubTitle.isEmpty())
                    os << "<em>" << sSubTitle << "</em><br /><br />";

                os << sDesc << "<br /><br />"
                   << "This recording will start "  << sTimeToStart
                   << " using encoder " << nEncoderId << " with the '"
                   << sProfile << "' profile.</span></a><hr />\r\n";
            }
        }

        node = node.nextSibling();
    }
    os  << "    </div>\r\n";
    os << "  </div>\r\n\r\n";

    return( nNumRecordings );
}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

int HttpStatus::PrintJobQueue( QTextStream &os, QDomElement jobs )
{
    QString   shortdateformat = gContext->GetSetting("ShortDateFormat", "M/d");
    QString   timeformat      = gContext->GetSetting("TimeFormat", "h:mm AP");

    if (jobs.isNull())
        return( 0 );

    int nNumJobs= jobs.attribute( "count", "0" ).toInt();
    
    os << "  <div class=\"content\">\r\n"
       << "    <h2>Job Queue</h2>\r\n";

    if (nNumJobs != 0)
    {
        QString statusColor;
        QString jobColor;
        QString timeDateFormat;

        timeDateFormat = gContext->GetSetting("DateFormat", "ddd MMMM d") +
                         " " + gContext->GetSetting("TimeFormat", "h:mm AP");

        os << "    Jobs currently in Queue or recently ended:\r\n<br />"
           << "    <div class=\"schedule\">\r\n";

        
        QDomNode node = jobs.firstChild();

        while (!node.isNull())
        {
            QDomElement e = node.toElement();

            if (!e.isNull())
            {
                QDomNode progNode = e.namedItem( "Program"   );

                if ((e.tagName() == "Job") && !progNode.isNull() )
                {
                    QDomElement p =  progNode.toElement();

                    QDomNode recNode  = p.namedItem( "Recording" );
                    QDomNode chanNode = p.namedItem( "Channel"   );

                    QDomElement r =  recNode.toElement();
                    QDomElement c =  chanNode.toElement();

                    int    nType   = e.attribute( "type"  , "0" ).toInt();
                    int nStatus = e.attribute( "status", "0" ).toInt();

                    switch( nStatus )
                    {
                        case JOB_ABORTED:
                            statusColor = " class=\"jobaborted\"";
                            jobColor = "";
                            break;

                        case JOB_ERRORED:
                            statusColor = " class=\"joberrored\"";
                            jobColor = " class=\"joberrored\"";
                            break;

                        case JOB_FINISHED:
                            statusColor = " class=\"jobfinished\"";
                            jobColor = " class=\"jobfinished\"";
                            break;

                        case JOB_RUNNING:
                            statusColor = " class=\"jobrunning\"";
                            jobColor = " class=\"jobrunning\"";
                            break;

                        default:
                            statusColor = " class=\"jobqueued\"";
                            jobColor = " class=\"jobqueued\"";
                            break;
                    }

                    QString   sTitle       = p.attribute( "title"   , "" );       //.replace(QRegExp("\""), "&quot;");
                    QString   sSubTitle    = p.attribute( "subTitle", "" );
                    QDateTime startTs      = QDateTime::fromString( p.attribute( "startTime" ,"" ), Qt::ISODate );
                    QDateTime endTs        = QDateTime::fromString( p.attribute( "endTime"   ,"" ), Qt::ISODate );
                    QDateTime recStartTs   = QDateTime::fromString( r.attribute( "recStartTs","" ), Qt::ISODate );
                    QDateTime statusTime   = QDateTime::fromString( e.attribute( "statusTime","" ), Qt::ISODate );
                    QDateTime schedRunTime = QDateTime::fromString( e.attribute( "schedTime","" ), Qt::ISODate );
                    QString   sHostname    = e.attribute( "hostname", "master" );
                    QString   sComment     = "";

                    QDomText  text         = e.firstChild().toText();
                    if (!text.isNull())
                        sComment = text.nodeValue();

                    os << "<a href=\"#\">"
                       << recStartTs.toString("ddd") << " "
                       << recStartTs.toString(shortdateformat) << " "
                       << recStartTs.toString(timeformat) << " - "
                       << sTitle << " - <font" << jobColor << ">"
                       << JobQueue::JobText( nType ) << "</font><br />"
                       << "<span><strong>" << sTitle << "</strong> ("
                       << startTs.toString(timeformat) << "-"
                       << endTs.toString(timeformat) << ")<br />";

                    if ( !sSubTitle.isNull() && !sSubTitle.isEmpty())
                        os << "<em>" << sSubTitle << "</em><br /><br />";

                    os << "Job: " << JobQueue::JobText( nType ) << "<br />";

                    if (schedRunTime > QDateTime::currentDateTime())
                        os << "Scheduled Run Time: "
                           << schedRunTime.toString(timeDateFormat)
                           << "<br />";

                    os << "Status: <font" << statusColor << ">"
                       << JobQueue::StatusText( nStatus )
                       << "</font><br />"
                       << "Status Time: "
                       << statusTime.toString(timeDateFormat)
                       << "<br />";

                    if ( nStatus != JOB_QUEUED)
                        os << "Host: " << sHostname << "<br />";

                    if (!sComment.isNull() && !sComment.isEmpty())
                        os << "<br />Comments:<br />" << sComment << "<br />";

                    os << "</span></a><hr />\r\n";
                }
            }

            node = node.nextSibling();
        }
        os << "      </div>\r\n";
    }
    else
        os << "    Job Queue is currently empty.\r\n\r\n";

    os << "  </div>\r\n\r\n ";

    return( nNumJobs );

}

/////////////////////////////////////////////////////////////////////////////
//
/////////////////////////////////////////////////////////////////////////////

int HttpStatus::PrintMachineInfo( QTextStream &os, QDomElement info )
{
    QString   shortdateformat = gContext->GetSetting("ShortDateFormat", "M/d");
    QString   timeformat      = gContext->GetSetting("TimeFormat", "h:mm AP");
    QString   sRep;

    if (info.isNull())
        return( 0 );

    os << "<div class=\"content\">\r\n"
       << "    <h2>Machine information</h2>\r\n";

    // load average ---------------------

    QDomNode node = info.namedItem( "Load" );

    if (!node.isNull())
    {    
        QDomElement e = node.toElement();

        if (!e.isNull())
        {
            double dAvg1 = e.attribute( "avg1" , "0" ).toDouble();
            double dAvg2 = e.attribute( "avg2" , "0" ).toDouble();
            double dAvg3 = e.attribute( "avg3" , "0" ).toDouble();

            os << "    <div class=\"loadstatus\">\r\n"
               << "      This machine's load average:"
               << "\r\n      <ul>\r\n        <li>"
               << "1 Minute: " << dAvg1 << "</li>\r\n"
               << "        <li>5 Minutes: " << dAvg2 << "</li>\r\n"
               << "        <li>15 Minutes: " << dAvg3
               << "</li>\r\n      </ul>\r\n"
               << "    </div>\r\n";    
        }
    }

    // local drive space   ---------------------
    
    node = info.namedItem( "Storage" );

    if (!node.isNull())
    {    
        QDomElement e = node.toElement();

        if (!e.isNull())
        {
            QString ids = e.attribute("fsids", "");
            QStringList tokens = QStringList::split(",", ids);

            os << "      Disk Usage:<br />\r\n";
            os << "      <ul>\r\n";

            for (unsigned int i = 0; i < tokens.size(); i++)
            {
                // For a single-directory installation just display the totals
                if ((tokens.size() == 2) && (i == 0) &&
                    (tokens[i] != "total") &&
                    (tokens[i+1] == "total"))
                    i++;

                int nFree =
                    e.attribute("drive_" + tokens[i] + "_free" , "0" ).toInt();
                int nTotal =
                    e.attribute("drive_" + tokens[i] + "_total", "0" ).toInt();
                int nUsed =
                    e.attribute("drive_" + tokens[i] + "_used" , "0" ).toInt();
                QString nDirs =
                    e.attribute("drive_" + tokens[i] + "_dirs" , "" );

                nDirs.replace(QRegExp(","), ", ");

                if (tokens[i] == "total")
                {
                    os << "        <li>Total Disk Space:\r\n"
                       << "          <ul>\r\n";
                }
                else
                {
                    os << "        <li>MythTV Drive #" << tokens[i] << ":"
                       << "\r\n<br />\r\n";

                    if (nDirs.contains(","))
                        os << "          <ul><li>Directories: ";
                    else
                        os << "          <ul><li>Directory: ";

                    os << nDirs << "</li>\r\n";
                }

                QLocale c(QLocale::C);
                os << "            <li>Total Space: ";
                sRep = c.toString(nTotal) + " MB ";
                os << sRep << "</li>\r\n";

                os << "            <li>Space Used: ";
                sRep = c.toString(nUsed) + " MB ";
                os << sRep << "</li>\r\n";

                os << "            <li>Space Free: ";
                sRep = c.toString(nFree) + " MB ";
                os << sRep << "</li>\r\n";

                os << "          </ul>\r\n"
                   << "        </li>\r\n";
            }
            os << "      </ul>\r\n";
        }
    }

    // Guide Info ---------------------

    node = info.namedItem( "Guide" );

    if (!node.isNull())
    {    
        QDomElement e = node.toElement();

        if (!e.isNull())
        {
            QString datetimefmt = "yyyy-MM-dd hh:mm";
            int     nDays   = e.attribute( "guideDays", "0" ).toInt();
            QString sStart  = e.attribute( "start"    , ""  );
            QString sEnd    = e.attribute( "end"      , ""  );
            QString sStatus = e.attribute( "status"   , ""  );
            QDateTime next  = QDateTime::fromString( e.attribute( "next"     , ""  ), Qt::ISODate);
            QString sNext   = next.isNull() ? "" : next.toString(datetimefmt);
            QString sMsg    = "";

            QDateTime thru  = QDateTime::fromString( e.attribute( "guideThru", ""  ), Qt::ISODate);

            QDomText  text  = e.firstChild().toText();

            if (!text.isNull())
                sMsg = text.nodeValue();

            os << "    Last mythfilldatabase run started on " << sStart
               << " and ";

            if (sEnd < sStart)   
                os << "is ";
            else 
                os << "ended on " << sEnd << ". ";

            os << sStatus << "<br />\r\n";    

            if (!next.isNull() && sNext >= sStart)
            {
                os << "    Suggested next mythfilldatabase run: "
                    << sNext << ".<br />\r\n";
            }

            if (!thru.isNull())
            {
                os << "    There's guide data until "
                   << QDateTime( thru ).toString(datetimefmt);

                if (nDays > 0)
                    os << " (" << nDays << " day" << (nDays == 1 ? "" : "s" ) << ")";

                os << ".";

                if (nDays <= 3)
                    os << " <strong>WARNING</strong>: is mythfilldatabase running?";
            }
            else
                os << "    There's <strong>no guide data</strong> available! "
                   << "Have you run mythfilldatabase?";

            if (!sMsg.isNull() && !sMsg.isEmpty())
                os << "<br />\r\n    DataDirect Status: " << sMsg;
        }
    }
    os << "\r\n  </div>\r\n";

    return( 1 );
}

int HttpStatus::PrintMiscellaneousInfo( QTextStream &os, QDomElement info )
{
    if (info.isNull())
        return( 0 );

    // Miscellaneous information

    QDomNodeList nodes = info.elementsByTagName("Information");
    uint count = nodes.count();
    if (count > 0)
    {
        QString display, linebreak;
        //QString name, value;
        os << "<div class=\"content\">\r\n"
           << "    <h2>Miscellaneous</h2>\r\n";
        for (unsigned int i = 0; i < count; i++)
        {
            QDomNode node = nodes.item(i);
            if (node.isNull())
                continue;

            QDomElement e = node.toElement();
            if (e.isNull())
                continue;

            display = e.attribute("display", "");
            //name = e.attribute("name", "");
            //value = e.attribute("value", "");

            if (display.isEmpty())
                continue;

            // Only include HTML line break if display value doesn't already
            // contain breaks.
            if ((display.contains("<p>", false) > 0) ||
                (display.contains("<br", false) > 0))
                linebreak = "\r\n";
            else
                linebreak = "<br />\r\n";

            os << "    " << display << linebreak;
        }
        os << "</div>\r\n";
    }

    return( 1 );
}

// vim:set shiftwidth=4 tabstop=4 expandtab:
