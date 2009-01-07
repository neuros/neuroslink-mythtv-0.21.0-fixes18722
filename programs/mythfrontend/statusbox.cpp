#include <qlayout.h>
#include <qiconview.h>
#include <qsqldatabase.h>
#include <qwidgetstack.h>
#include <qvbox.h>
#include <qgrid.h>
#include <qregexp.h>
#include <qhostaddress.h>

#include <unistd.h>
#include <stdlib.h>

#include <iostream>
#include <cerrno>
using namespace std;

#include "config.h"
#include "statusbox.h"
#include "mythcontext.h"
#include "remoteutil.h"
#include "programinfo.h"
#include "tv.h"
#include "jobqueue.h"
#include "util.h"
#include "mythdbcon.h"
#include "cardutil.h"

#define REC_CAN_BE_DELETED(rec) \
    ((((rec)->programflags & FL_INUSEPLAYING) == 0) && \
     ((((rec)->programflags & FL_INUSERECORDING) == 0) || \
      ((rec)->recgroup != "LiveTV")))


/** \class StatusBox
 *  \brief Reports on various status items.
 *
 *  StatusBox reports on the listing status, that is how far
 *  into the future program listings exists. It also reports
 *  on the status of each tuner, the log entries, the status
 *  of the job queue, and the machine status.
 */

StatusBox::StatusBox(MythMainWindow *parent, const char *name)
    : MythDialog(parent, name), errored(false)
{
    // Set this value to the number of items in icon_list
    // to prevent scrolling off the bottom
    int item_count = 0;
    dateFormat = gContext->GetSetting("ShortDateFormat", "M/d");
    timeFormat = gContext->GetSetting("TimeFormat", "h:mm AP");
    timeDateFormat = timeFormat + " " + dateFormat;

    setNoErase();
    LoadTheme();
    if (IsErrored())
        return;

    updateBackground();

    icon_list->SetItemText(item_count++, QObject::tr("Listings Status"));
    icon_list->SetItemText(item_count++, QObject::tr("Schedule Status"));
    icon_list->SetItemText(item_count++, QObject::tr("Tuner Status"));
    icon_list->SetItemText(item_count++, QObject::tr("Log Entries"));
    icon_list->SetItemText(item_count++, QObject::tr("Job Queue"));
    icon_list->SetItemText(item_count++, QObject::tr("Machine Status"));
    icon_list->SetItemText(item_count++, QObject::tr("AutoExpire List"));
    itemCurrent = gContext->GetNumSetting("StatusBoxItemCurrent", 0);
    icon_list->SetItemCurrent(itemCurrent);
    icon_list->SetActive(true);

    QStringList strlist;
    strlist << "QUERY_IS_ACTIVE_BACKEND";
    strlist << gContext->GetHostName();

    gContext->SendReceiveStringList(strlist);

    if (QString(strlist[0]) == "FALSE")
        isBackend = false;
    else if (QString(strlist[0]) == "TRUE")
        isBackend = true;
    else
        isBackend = false;

    VERBOSE(VB_NETWORK, QString("QUERY_IS_ACTIVE_BACKEND=%1").arg(strlist[0]));

    max_icons = item_count;
    inContent = false;
    doScroll = false;
    contentPos = 0;
    contentTotalLines = 0;
    contentSize = 0;
    contentMid = 0;
    min_level = gContext->GetNumSetting("LogDefaultView",5);
    my_parent = parent;
    clicked();

    gContext->addCurrentLocation("StatusBox");
}

StatusBox::~StatusBox(void)
{
    gContext->SaveSetting("StatusBoxItemCurrent", itemCurrent);
    gContext->removeCurrentLocation();
}

void StatusBox::paintEvent(QPaintEvent *e)
{
    QRect r = e->rect();

    if (r.intersects(TopRect))
        updateTopBar();
    if (r.intersects(SelectRect))
        updateSelector();
    if (r.intersects(ContentRect))
        updateContent();
}

void StatusBox::updateBackground(void)
{
    QPixmap bground(size());
    bground.fill(this, 0, 0);

    QPainter tmp(&bground);

    LayerSet *container = theme->GetSet("background");
    if (container)
    {
        container->Draw(&tmp, 0, 0);
    }

    tmp.end();
    m_background = bground;

    setPaletteBackgroundPixmap(m_background);
}

void StatusBox::updateContent()
{
    QRect pr = ContentRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);
    QPainter p(this);

    // Normalize the variables here and set the contentMid
    contentSize = list_area->GetItems();
    if (contentSize > contentTotalLines)
        contentSize = contentTotalLines;
    contentMid = contentSize / 2;

    int startPos = 0;
    int highlightPos = 0;

    if (contentPos < contentMid)
    {
        startPos = 0;
        highlightPos = contentPos;
    }
    else if (contentPos >= (contentTotalLines - contentMid))
    {
        startPos = contentTotalLines - contentSize;
        highlightPos = contentSize - (contentTotalLines - contentPos);
    }
    else if (contentPos >= contentMid)
    {
        startPos = contentPos - contentMid;
        highlightPos = contentMid;
    }
 
    if (content  == NULL) return;
    LayerSet *container = content;

    list_area->ResetList();
    for (int x = startPos; (x - startPos) <= contentSize; x++)
    {
        if (contentLines.contains(x))
        {
            list_area->SetItemText(x - startPos, contentLines[x]);
            if (contentFont.contains(x))
                list_area->EnableForcedFont(x - startPos, contentFont[x]);
        }
    }

    list_area->SetItemCurrent(highlightPos);

    if (inContent)
    {
        helptext->SetText(contentDetail[contentPos]);
        update(TopRect);
    }

    list_area->SetUpArrow((startPos > 0) && (contentSize < contentTotalLines));
    list_area->SetDownArrow((startPos + contentSize) < contentTotalLines);

    container->Draw(&tmp, 0, 0);
    container->Draw(&tmp, 1, 0);
    container->Draw(&tmp, 2, 0);
    container->Draw(&tmp, 3, 0);
    container->Draw(&tmp, 4, 0);
    container->Draw(&tmp, 5, 0);
    container->Draw(&tmp, 6, 0);
    container->Draw(&tmp, 7, 0);
    container->Draw(&tmp, 8, 0);
    tmp.end();
    p.drawPixmap(pr.topLeft(), pix);
}

void StatusBox::updateSelector()
{
    QRect pr = SelectRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);
    QPainter p(this);
 
    if (selector == NULL) return;
    LayerSet *container = selector;

    container->Draw(&tmp, 0, 0);
    container->Draw(&tmp, 1, 0);
    container->Draw(&tmp, 2, 0);
    container->Draw(&tmp, 3, 0);
    container->Draw(&tmp, 4, 0);
    container->Draw(&tmp, 5, 0);
    container->Draw(&tmp, 6, 0);
    container->Draw(&tmp, 7, 0);
    container->Draw(&tmp, 8, 0);
    tmp.end();
    p.drawPixmap(pr.topLeft(), pix);
}

void StatusBox::updateTopBar()
{
    QRect pr = TopRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);
    QPainter p(this);

    if (topbar == NULL) return;
    LayerSet *container = topbar;

    container->Draw(&tmp, 0, 0);
    tmp.end();
    p.drawPixmap(pr.topLeft(), pix);
}

void StatusBox::LoadTheme()
{
    int screenheight = 0, screenwidth = 0;
    float wmult = 0, hmult = 0;

    gContext->GetScreenSettings(screenwidth, wmult, screenheight, hmult);

    theme = new XMLParse();
    theme->SetWMult(wmult);
    theme->SetHMult(hmult);
    if (!theme->LoadTheme(xmldata, "status", "status-"))
    {
        VERBOSE(VB_IMPORTANT, "StatusBox: Unable to load theme.");
        errored = true;
        return;
    }

    for (QDomNode child = xmldata.firstChild(); !child.isNull();
         child = child.nextSibling()) {

        QDomElement e = child.toElement();
        if (!e.isNull()) {
            if (e.tagName() == "font") {
                theme->parseFont(e);
            }
            else if (e.tagName() == "container") {
                QRect area;
                QString name;
                int context;
                theme->parseContainer(e, name, context, area);

                if (name.lower() == "topbar")
                    TopRect = area;
                if (name.lower() == "selector")
                    SelectRect = area;
                if (name.lower() == "content")
                    ContentRect = area;
            }
            else {
                QString msg =
                    QString(tr("The theme you are using contains an "
                               "unknown element ('%1').  It will be ignored"))
                    .arg(e.tagName());
                VERBOSE(VB_IMPORTANT, msg);
                errored = true;
            }
        }
    }

    selector = theme->GetSet("selector");
    if (!selector)
    {
        VERBOSE(VB_IMPORTANT, "StatusBox: Failed to get selector container.");
        errored = true;
    }

    icon_list = (UIListType*)selector->GetType("icon_list");
    if (!icon_list)
    {
        VERBOSE(VB_IMPORTANT, "StatusBox: Failed to get icon list area.");
        errored = true;
    }

    content = theme->GetSet("content");
    if (!content)
    {
        VERBOSE(VB_IMPORTANT, "StatusBox: Failed to get content container.");
        errored = true;
    }

    list_area = (UIListType*)content->GetType("list_area");
    if (!list_area)
    {
        VERBOSE(VB_IMPORTANT, "StatusBox: Failed to get list area.");
        errored = true;
    }

    topbar = theme->GetSet("topbar");
    if (!topbar)
    {
        VERBOSE(VB_IMPORTANT, "StatusBox: Failed to get topbar container.");
        errored = true;
    }

    helptext = (UITextType*)topbar->GetType("helptext");
    if (!helptext)
    {
        VERBOSE(VB_IMPORTANT, "StatusBox: Failed to get helptext area.");
        errored = true;
    }
}

void StatusBox::keyPressEvent(QKeyEvent *e)
{
    bool handled = false;
    QStringList actions;
    gContext->GetMainWindow()->TranslateKeyPress("Status", e, actions);

    for (unsigned int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        QString currentItem;
        QRegExp logNumberKeys( "^[12345678]$" );

        currentItem = icon_list->GetItemText(icon_list->GetCurrentItem());
        handled = true;

        if (action == "SELECT")
        {
            clicked();
        }
        else if (action == "MENU")
        {
            if ((inContent) &&
                (currentItem == QObject::tr("Log Entries")))
            {
                DialogCode retval = MythPopupBox::Show2ButtonPopup(
                    my_parent, QString("AckLogEntry"),
                    QObject::tr("Acknowledge all log entries at "
                                "this priority level or lower?"),
                    QObject::tr("Yes"), QObject::tr("No"),
                    kDialogCodeButton0);

                if (kDialogCodeButton0 == retval)
                {
                    MSqlQuery query(MSqlQuery::InitCon());
                    query.prepare("UPDATE mythlog SET acknowledged = 1 "
                                  "WHERE priority <= :PRIORITY ;");
                    query.bindValue(":PRIORITY", min_level);
                    query.exec();
                    doLogEntries();
                }
            }
            else if ((inContent) &&
                     (currentItem == QObject::tr("Job Queue")))
            {
                clicked();
            }
        }
        else if (action == "UP")
        {
            if (inContent)
            {
                if (contentPos > 0)
                    contentPos--;
                update(ContentRect);
            }
            else
            {
                if (icon_list->GetCurrentItem() > 0)
                    itemCurrent = icon_list->GetCurrentItem()-1;
                else
                    itemCurrent = max_icons - 1;
                icon_list->SetItemCurrent(itemCurrent);
                clicked();
                setHelpText();
                update(SelectRect);
            }

        }
        else if (action == "DOWN")
        {
            if (inContent)
            {
                if (contentPos < (contentTotalLines - 1))
                    contentPos++;
                update(ContentRect);
            }
            else
            {
                if (icon_list->GetCurrentItem() < (max_icons - 1))
                    itemCurrent = icon_list->GetCurrentItem()+1;
                else
                    itemCurrent = 0;
                icon_list->SetItemCurrent(itemCurrent);
                clicked();
                setHelpText();
                update(SelectRect);
            }
        }
        else if (action == "PAGEUP" && inContent) 
        {
            contentPos -= contentSize;
            if (contentPos < 0)
                contentPos = 0;
            update(ContentRect);
        }
        else if (action == "PAGEDOWN" && inContent)
        {
            contentPos += contentSize;
            if (contentPos > (contentTotalLines - 1))
                contentPos = contentTotalLines - 1;
            update(ContentRect);
        }
        else if ((action == "RIGHT") &&
                 (!inContent) &&
                 (((contentSize > 0) &&
                   (contentTotalLines > contentSize)) ||
                  (doScroll)))
        {
            clicked();
            inContent = true;
            contentPos = 0;
            icon_list->SetActive(false);
            list_area->SetActive(true);
            update(SelectRect);
            update(ContentRect);
        }
        else if (action == "LEFT")
        {
            if (inContent)
            {
                inContent = false;
                contentPos = 0;
                list_area->SetActive(false);
                icon_list->SetActive(true);
                setHelpText();
                update(SelectRect);
                update(ContentRect);
            }
            else
            {
                if (gContext->GetNumSetting("UseArrowAccels", 1))
                    accept();
            }
        }
        else if ((currentItem == QObject::tr("Log Entries")) &&
                 (logNumberKeys.search(action) == 0))
        {
            min_level = action.toInt();
            helptext->SetText(QObject::tr("Setting priority level to %1")
                                          .arg(min_level));
            update(TopRect);
            doLogEntries();
        }
        else
            handled = false;
    }

    if (!handled)
        MythDialog::keyPressEvent(e);
}

void StatusBox::setHelpText()
{
    if (inContent)
    {
        helptext->SetText(contentDetail[contentPos]);
    } else {
        topbar->ClearAllText();
        QString currentItem;

        currentItem = icon_list->GetItemText(icon_list->GetCurrentItem());

        if (currentItem == QObject::tr("Listings Status"))
            helptext->SetText(QObject::tr("Listings Status shows the latest "
                                          "status information from "
                                          "mythfilldatabase"));

        if (currentItem == QObject::tr("Schedule Status"))
            helptext->SetText(QObject::tr("Schedule Status shows current "
                                          "statistics from the scheduler."));

        if (currentItem == QObject::tr("Tuner Status"))
            helptext->SetText(QObject::tr("Tuner Status shows the current "
                                          "information about the state of "
                                          "backend tuner cards"));

        if (currentItem == QObject::tr("DVB Status"))
            helptext->SetText(QObject::tr("DVB Status shows the quality "
                                          "statistics of all DVB cards, if "
                                          "present"));

        if (currentItem == QObject::tr("Log Entries"))
            helptext->SetText(QObject::tr("Log Entries shows any unread log "
                                          "entries from the system if you "
                                          "have logging enabled"));
        if (currentItem == QObject::tr("Job Queue"))
            helptext->SetText(QObject::tr("Job Queue shows any jobs currently "
                                          "in Myth's Job Queue such as a "
                                          "commercial flagging job."));
        if (currentItem == QObject::tr("Machine Status"))
        {
            QString machineStr = QObject::tr("Machine Status shows "
                                             "some operating system "
                                             "statistics of this machine");
            if (!isBackend)
                machineStr.append(" " + QObject::tr("and the MythTV server"));

            helptext->SetText(machineStr);
        }

        if (currentItem == QObject::tr("AutoExpire List"))
            helptext->SetText(QObject::tr("The AutoExpire List shows all "
                "recordings which may be expired and the order of their "
                "expiration. Recordings at the top of the list will be "
                "expired first."));
    }
    update(TopRect);
}

void StatusBox::clicked()
{
    QString currentItem = icon_list->GetItemText(icon_list->GetCurrentItem());

    if (inContent)
    {
        if (currentItem == QObject::tr("Log Entries"))
        {
            DialogCode retval = MythPopupBox::Show2ButtonPopup(
                my_parent,
                QString("AckLogEntry"),
                QObject::tr("Acknowledge this log entry?"),
                QObject::tr("Yes"), QObject::tr("No"), kDialogCodeButton0);

            if (kDialogCodeButton0 == retval)
            {
                MSqlQuery query(MSqlQuery::InitCon());
                query.prepare("UPDATE mythlog SET acknowledged = 1 "
                              "WHERE logid = :LOGID ;");
                query.bindValue(":LOGID", contentData[contentPos]);
                query.exec();
                doLogEntries();
            }
        }
        else if (currentItem == QObject::tr("Job Queue"))
        {
            QStringList msgs;
            int jobStatus;

            jobStatus = JobQueue::GetJobStatus(
                                contentData[contentPos].toInt());

            if (jobStatus == JOB_QUEUED)
            {
                DialogCode retval = MythPopupBox::Show2ButtonPopup(
                    my_parent,
                    QString("JobQueuePopup"), QObject::tr("Delete Job?"),
                    QObject::tr("Yes"), QObject::tr("No"), kDialogCodeButton1);
                if (kDialogCodeButton0 == retval)
                {
                    JobQueue::DeleteJob(contentData[contentPos].toInt());
                    doJobQueueStatus();
                }
            }
            else if ((jobStatus == JOB_PENDING) ||
                     (jobStatus == JOB_STARTING) ||
                     (jobStatus == JOB_RUNNING))
            {
                msgs << QObject::tr("Pause");
                msgs << QObject::tr("Stop");
                msgs << QObject::tr("No Change");
                DialogCode retval = MythPopupBox::ShowButtonPopup(
                    my_parent,
                    QString("JobQueuePopup"),
                    QObject::tr("Job Queue Actions:"),
                    msgs, kDialogCodeButton2);
                if (kDialogCodeButton0 == retval)
                {
                    JobQueue::PauseJob(contentData[contentPos].toInt());
                    doJobQueueStatus();
                }
                else if (kDialogCodeButton1 == retval)
                {
                    JobQueue::StopJob(contentData[contentPos].toInt());
                    doJobQueueStatus();
                }
            }
            else if (jobStatus == JOB_PAUSED)
            {
                msgs << QObject::tr("Resume");
                msgs << QObject::tr("Stop");
                msgs << QObject::tr("No Change");
                DialogCode retval = MythPopupBox::ShowButtonPopup(
                    my_parent,
                    QString("JobQueuePopup"),
                    QObject::tr("Job Queue Actions:"),
                    msgs, kDialogCodeButton2);

                if (kDialogCodeButton0 == retval)
                {
                    JobQueue::ResumeJob(contentData[contentPos].toInt());
                    doJobQueueStatus();
                }
                else if (kDialogCodeButton1 == retval)
                {
                    JobQueue::StopJob(contentData[contentPos].toInt());
                    doJobQueueStatus();
                }
            }
            else if (jobStatus & JOB_DONE)
            {
                DialogCode retval = MythPopupBox::Show2ButtonPopup(
                    my_parent,
                    QString("JobQueuePopup"),
                    QObject::tr("Requeue Job?"),
                    QObject::tr("Yes"), QObject::tr("No"), kDialogCodeButton0);

                if (kDialogCodeButton0 == retval)
                {
                    JobQueue::ChangeJobStatus(contentData[contentPos].toInt(),
                                              JOB_QUEUED);
                    doJobQueueStatus();
                }
            }
        }
        else if (currentItem == QObject::tr("AutoExpire List"))
        {
            ProgramInfo* rec;

            rec = expList[contentPos];

            if (rec) 
            {
                QStringList msgs;

                msgs << QObject::tr("Delete Now");
                if ((rec)->recgroup == "LiveTV")
                    msgs << QObject::tr("Move to Default group");
                else if ((rec)->recgroup == "Deleted")
                    msgs << QObject::tr("Undelete");
                else
                    msgs << QObject::tr("Disable AutoExpire");
                msgs << QObject::tr("No Change");

                DialogCode retval = MythPopupBox::ShowButtonPopup(
                    my_parent,
                    QString("AutoExpirePopup"),
                    QObject::tr("AutoExpire Actions:"),
                    msgs, kDialogCodeButton2);

                if ((kDialogCodeButton0 == retval) && REC_CAN_BE_DELETED(rec))
                {
                    RemoteDeleteRecording(rec, false, false);
                }
                else if (kDialogCodeButton1 == retval)
                {
                    if ((rec)->recgroup == "Deleted")
                        RemoteUndeleteRecording(rec);
                    else
                    {
                        rec->SetAutoExpire(0);

                        if ((rec)->recgroup == "LiveTV")
                            rec->ApplyRecordRecGroupChange("Default");
                    }
                }

                // Update list, prevent selected item going off bottom
                doAutoExpireList();
                if (contentPos >= (int)expList.size())  
                    contentPos = max((int)expList.size()-1,0);
            }
        }
        return;
    }
    
    // Clear all visible content elements here
    // I'm sure there's a better way to do this but I can't find it
    content->ClearAllText();
    list_area->ResetList();
    contentLines.clear();
    contentDetail.clear();
    contentFont.clear();
    contentData.clear();

    if (currentItem == QObject::tr("Listings Status"))
        doListingsStatus();
    else if (currentItem == QObject::tr("Schedule Status"))
        doScheduleStatus();
    else if (currentItem == QObject::tr("Tuner Status"))
        doTunerStatus();
    else if (currentItem == QObject::tr("Log Entries"))
        doLogEntries();
    else if (currentItem == QObject::tr("Job Queue"))
        doJobQueueStatus();
    else if (currentItem == QObject::tr("Machine Status"))
        doMachineStatus();
    else if (currentItem == QObject::tr("AutoExpire List"))
        doAutoExpireList();
}

void StatusBox::doListingsStatus()
{
    QString mfdLastRunStart, mfdLastRunEnd, mfdLastRunStatus, mfdNextRunStart;
    QString querytext, Status, DataDirectMessage;
    int DaysOfData;
    QDateTime qdtNow, GuideDataThrough;
    int count = 0;

    contentLines.clear();
    contentDetail.clear();
    contentFont.clear();
    doScroll = false;

    qdtNow = QDateTime::currentDateTime();

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT max(endtime) FROM program WHERE manualid=0;");
    query.exec();

    if (query.isActive() && query.size())
    {
        query.next();
        GuideDataThrough = QDateTime::fromString(query.value(0).toString(),
                                                 Qt::ISODate);
    }

    mfdLastRunStart = gContext->GetSetting("mythfilldatabaseLastRunStart");
    mfdLastRunEnd = gContext->GetSetting("mythfilldatabaseLastRunEnd");
    mfdLastRunStatus = gContext->GetSetting("mythfilldatabaseLastRunStatus");
    mfdNextRunStart = gContext->GetSetting("MythFillSuggestedRunTime");
    DataDirectMessage = gContext->GetSetting("DataDirectMessage");

    mfdNextRunStart.replace("T", " ");

    extern const char *myth_source_version;
    contentLines[count++] = QObject::tr("Myth version:") + " " +
                                        MYTH_BINARY_VERSION + "   " +
                                        myth_source_version;
    contentLines[count++] = QObject::tr("Last mythfilldatabase guide update:");
    contentLines[count++] = QObject::tr("Started:   ") + mfdLastRunStart;

    if (mfdLastRunEnd >= mfdLastRunStart) //if end < start, it's still running.
        contentLines[count++] = QObject::tr("Finished: ") + mfdLastRunEnd;

    contentLines[count++] = QObject::tr("Result: ") + mfdLastRunStatus;


    if (mfdNextRunStart >= mfdLastRunStart) 
        contentLines[count++] = QObject::tr("Suggested Next: ") + 
                                mfdNextRunStart;

    DaysOfData = qdtNow.daysTo(GuideDataThrough);

    if (GuideDataThrough.isNull())
    {
        contentLines[count++] = "";
        contentLines[count++] = QObject::tr("There's no guide data available!");
        contentLines[count++] = QObject::tr("Have you run mythfilldatabase?");
    }
    else
    {
        contentLines[count++] = QObject::tr("There is guide data until ") + 
                                QDateTime(GuideDataThrough)
                                          .toString("yyyy-MM-dd hh:mm");

        if (DaysOfData > 0)
        {
            Status = QString("(%1 ").arg(DaysOfData);
            if (DaysOfData >1)
                Status += QObject::tr("days");
            else
                Status += QObject::tr("day");
            Status += ").";
            contentLines[count++] = Status;
        }
    }

    if (DaysOfData <= 3)
    {
        contentLines[count++] = QObject::tr("WARNING: is mythfilldatabase "
                                            "running?");
    }

    if (!DataDirectMessage.isNull())
    {
        contentLines[count++] = QObject::tr("DataDirect Status: "); 
        contentLines[count++] = DataDirectMessage;
    }
   
    contentTotalLines = count;
    update(ContentRect);
}

void StatusBox::doScheduleStatus()
{
    doScroll = true;
    contentLines.clear();
    contentDetail.clear();
    contentFont.clear();

    uint count = 0;

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT COUNT(*) FROM record WHERE search = 0");

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("StatusBox::doScheduleStatus()", query);
        contentTotalLines = 0;
        update(ContentRect);
        return;
    }
    else
    {
        query.next();
        QString rules = QString("%1 %2").arg(query.value(0).toInt())
                                        .arg(tr("standard rules are defined"));
        contentLines[count]  = rules;
        contentDetail[count] = rules;
        count++;
    }
    query.prepare("SELECT COUNT(*) FROM record WHERE search > 0");

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("StatusBox::doScheduleStatus()", query);
        contentTotalLines = 0;
        update(ContentRect);
        return;
    }
    else
    {
        query.next();
        QString rules = QString("%1 %2").arg(query.value(0).toInt())
                                        .arg(tr("search rules are defined"));
        contentLines[count]  = rules;
        contentDetail[count] = rules;
        count++;
    }

    QMap<RecStatusType, int> statusMatch;
    QMap<RecStatusType, QString> statusText;
    QMap<int, int> sourceMatch;
    QMap<int, QString> sourceText;
    QMap<int, int> inputMatch;
    QMap<int, QString> inputText;
    QString tmpstr;
    int maxSource = 0;
    int maxInput = 0;
    int hdflag = 0;

    query.prepare("SELECT MAX(sourceid) FROM videosource");
    if (query.exec() && query.isActive() && query.size())
    {
        query.next();
        maxSource = query.value(0).toInt();
    }

    query.prepare("SELECT sourceid,name FROM videosource");
    if (query.exec() && query.isActive() && query.size())
    {
        while (query.next())
            sourceText[query.value(0).toInt()] = query.value(1).toString();
    }

    query.prepare("SELECT MAX(cardinputid) FROM cardinput");
    if (query.exec() && query.isActive() && query.size())
    {
        query.next();
        maxInput = query.value(0).toInt();
    }

    query.prepare("SELECT cardinputid,cardid,inputname,displayname "
                  "FROM cardinput");
    if (query.exec() && query.isActive() && query.size())
    {
        while (query.next())
        {
            if (query.value(3).toString() > "")
                inputText[query.value(0).toInt()] = query.value(3).toString();
            else
                inputText[query.value(0).toInt()] = QString("%1: %2")
                                              .arg(query.value(1).toInt())
                                              .arg(query.value(2).toString());
        }
    }

    ProgramList schedList;
    schedList.FromScheduler();

    tmpstr = QString("%1 %2").arg(schedList.count()).arg("matching showings");
    contentLines[count]  = tmpstr;
    contentDetail[count] = tmpstr;
    count++;

    ProgramInfo *s;
    for (s = schedList.first(); s; s = schedList.next())
    {
        if (statusMatch[s->recstatus] < 1)
            statusText[s->recstatus] = s->RecStatusText();

        ++statusMatch[s->recstatus];

        if (s->recstatus == rsWillRecord || s->recstatus == rsRecording)
        {
            ++sourceMatch[s->sourceid];
            ++inputMatch[s->inputid];
            if (s->videoproperties & VID_HDTV)
                ++hdflag;
        }
    }
    QMap<int, RecStatusType> statusMap;
    int i = 0;
    statusMap[i++] = rsRecording;
    statusMap[i++] = rsWillRecord;
    statusMap[i++] = rsConflict;
    statusMap[i++] = rsTooManyRecordings;
    statusMap[i++] = rsLowDiskSpace;
    statusMap[i++] = rsLaterShowing;
    statusMap[i++] = rsNotListed;
    int j = i;

    for (i = 0; i < j; i++)
    {
        RecStatusType type = statusMap[i];

        if (statusMatch[type] > 0)
        {
            tmpstr = QString("%1 %2").arg(statusMatch[type])
                                     .arg(statusText[type]);
            contentLines[count]  = tmpstr;
            contentDetail[count] = tmpstr;
            count++;
        }
    }

    QString willrec = statusText[rsWillRecord];

    if (hdflag > 0)
    {
        tmpstr = QString("%1 %2 %3").arg(hdflag).arg(willrec)
                                    .arg(tr("marked as HDTV"));
        contentLines[count]  = tmpstr;
        contentDetail[count] = tmpstr;
        count++;
    }
    for (i = 1; i <= maxSource; i++)
    {
        if (sourceMatch[i] > 0)
        {
            tmpstr = QString("%1 %2 %3 %4 \"%5\"")
                             .arg(sourceMatch[i]).arg(willrec)
                             .arg(tr("from source")).arg(i).arg(sourceText[i]);
            contentLines[count]  = tmpstr;
            contentDetail[count] = tmpstr;
            count++;
        }
    }
    for (i = 1; i <= maxInput; i++)
    {
        if (inputMatch[i] > 0)
        {
            tmpstr = QString("%1 %2 %3 %4 \"%5\"")
                             .arg(inputMatch[i]).arg(willrec)
                             .arg(tr("on input")).arg(i).arg(inputText[i]);
            contentLines[count]  = tmpstr;
            contentDetail[count] = tmpstr;
            count++;
        }
    }
    contentTotalLines = count;
    update(ContentRect);
}

void StatusBox::doTunerStatus()
{
    doScroll = true;
    contentLines.clear();
    contentDetail.clear();
    contentFont.clear();

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare(
        "SELECT cardid, cardtype, videodevice "
        "FROM capturecard ORDER BY cardid");

    if (!query.exec() || !query.isActive())
    {
        MythContext::DBError("StatusBox::doTunerStatus()", query);
        contentTotalLines = 0;
        update(ContentRect);
        return;
    }

    uint count = 0;
    while (query.next())
    {
        int cardid = query.value(0).toInt();

        QString cmd = QString("QUERY_REMOTEENCODER %1").arg(cardid);
        QStringList strlist = cmd;
        strlist << "GET_STATE";

        gContext->SendReceiveStringList(strlist);
        int state = strlist[0].toInt();
  
        QString status = "";
        if (state == kState_Error)
            status = tr("is unavailable");
        else if (state == kState_WatchingLiveTV)
            status = tr("is watching live TV");
        else if (state == kState_RecordingOnly ||
                 state == kState_WatchingRecording)
            status = tr("is recording");
        else 
            status = tr("is not recording");

        QString tun = tr("Tuner %1 ").arg(cardid);
        QString devlabel = CardUtil::GetDeviceLabel(
            cardid, query.value(1).toString(), query.value(2).toString());

        contentLines[count]  = tun + status;
        contentDetail[count] = tun + devlabel + " " + status;

        if (state == kState_RecordingOnly ||
            state == kState_WatchingRecording)
        {
            strlist = QString("QUERY_RECORDER %1").arg(cardid);
            strlist << "GET_RECORDING";
            gContext->SendReceiveStringList(strlist);
            ProgramInfo *proginfo = new ProgramInfo;
            proginfo->FromStringList(strlist, 0);
   
            status += " " + proginfo->title;
            status += "\n";
            status += proginfo->subtitle;
            contentDetail[count] = tun + devlabel + " " + status;
        }
        count++;
    }
    contentTotalLines = count;
    update(ContentRect);
}

void StatusBox::doLogEntries(void)
{
    QString line;
    int count = 0;
 
    doScroll = true;

    contentLines.clear();
    contentDetail.clear();
    contentFont.clear();
    contentData.clear();

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT logid, module, priority, logdate, host, "
                  "message, details "
                  "FROM mythlog WHERE acknowledged = 0 "
                  "AND priority <= :PRIORITY ORDER BY logdate DESC;");
    query.bindValue(":PRIORITY", min_level);
    query.exec();

    if (query.isActive())
    {
        while (query.next())
        {
            line = QString("%1").arg(query.value(5).toString());
            contentLines[count] = line;

            if (query.value(6).toString() != "")
                line = tr("On %1 %2 from %3.%4\n%5\n%6")
                               .arg(query.value(3).toDateTime()
                                         .toString(dateFormat))
                               .arg(query.value(3).toDateTime()
                                         .toString(timeFormat))
                               .arg(query.value(4).toString())
                               .arg(query.value(1).toString())
                               .arg(query.value(5).toString())
                               .arg(QString::fromUtf8(query.value(6).toString()));
            else
                line = tr("On %1 %2 from %3.%4\n%5\nNo other details")
                               .arg(query.value(3).toDateTime()
                                         .toString(dateFormat))
                               .arg(query.value(3).toDateTime()
                                         .toString(timeFormat))
                               .arg(query.value(4).toString())
                               .arg(query.value(1).toString())
                               .arg(query.value(5).toString());
            contentDetail[count] = line;
            contentData[count++] = query.value(0).toString();
        }
    }

    if (!count)
    {
        doScroll = false;
        contentLines[count++] = QObject::tr("No items found at priority "
                                            "level %1 or lower.")
                                            .arg(min_level);
        contentLines[count++] = QObject::tr("Use 1-8 to change priority "
                                            "level.");
    }
      
    contentTotalLines = count;
    if (contentPos > (contentTotalLines - 1))
        contentPos = contentTotalLines - 1;

    update(ContentRect);
}

void StatusBox::doJobQueueStatus()
{
    QMap<int, JobQueueEntry> jobs;
    QMap<int, JobQueueEntry>::Iterator it;
    int count = 0;

    QString detail;

    JobQueue::GetJobsInQueue(jobs,
                             JOB_LIST_NOT_DONE | JOB_LIST_ERROR |
                             JOB_LIST_RECENT);

    doScroll = true;

    contentLines.clear();
    contentDetail.clear();
    contentFont.clear();
    contentData.clear();

    if (jobs.size())
    {
        for (it = jobs.begin(); it != jobs.end(); ++it)
        {
            QString chanid = it.data().chanid;
            QDateTime starttime = it.data().starttime;
            ProgramInfo *pginfo;

            pginfo = ProgramInfo::GetProgramFromRecorded(chanid, starttime);

            if (!pginfo)
                continue;

            detail = pginfo->title + "\n" +
                     pginfo->channame + " " + pginfo->chanstr +
                         " @ " + starttime.toString(timeDateFormat) + "\n" +
                     tr("Job:") + " " + JobQueue::JobText(it.data().type) +
                     "     " + tr("Status: ") +
                     JobQueue::StatusText(it.data().status);

            if (it.data().status != JOB_QUEUED)
                detail += " (" + it.data().hostname + ")";

            if (it.data().schedruntime > QDateTime::currentDateTime())
                detail += "\n" + tr("Scheduled Run Time:") + " " +
                    it.data().schedruntime.toString(timeDateFormat);
            else
                detail += "\n" + it.data().comment;

            contentLines[count] = pginfo->title + " @ " +
                                  starttime.toString(timeDateFormat);

            contentDetail[count] = detail;
            contentData[count] = QString("%1").arg(it.data().id);

            if (it.data().status == JOB_ERRORED)
                contentFont[count] = "error";
            else if (it.data().status == JOB_ABORTED)
                contentFont[count] = "warning";

            count++;

            delete pginfo;
        }
    }
    else
    {
        contentLines[count++] = QObject::tr("Job Queue is currently empty.");
        doScroll = false;
    }

    contentTotalLines = count;
    update(ContentRect);
}

// Some helper routines for doMachineStatus() that format the output strings

/** \fn sm_str(long long, int)
 *  \brief Returns a short string describing an amount of space, choosing
 *         one of a number of useful units, "TB", "GB", "MB", or "KB".
 *  \param sizeKB Number of kilobytes.
 *  \param prec   Precision to use if we have less than ten of whatever
 *                unit is chosen.
 */
static const QString sm_str(long long sizeKB, int prec=1)
{
    if (sizeKB>1024*1024*1024) // Terabytes
    {
        double sizeGB = sizeKB/(1024*1024*1024.0);
        return QString("%1 TB").arg(sizeGB, 0, 'f', (sizeGB>10)?0:prec);
    }
    else if (sizeKB>1024*1024) // Gigabytes
    {
        double sizeGB = sizeKB/(1024*1024.0);
        return QString("%1 GB").arg(sizeGB, 0, 'f', (sizeGB>10)?0:prec);
    }
    else if (sizeKB>1024) // Megabytes
    {
        double sizeMB = sizeKB/1024.0;
        return QString("%1 MB").arg(sizeMB, 0, 'f', (sizeMB>10)?0:prec);
    }
    // Kilobytes
    return QString("%1 KB").arg(sizeKB);
}

static const QString usage_str_kb(long long total,
                                  long long used,
                                  long long free)
{
    QString ret = QObject::tr("Unknown");
    if (total > 0.0 && free > 0.0)
    {
        double percent = (100.0*free)/total;
        ret = QObject::tr("%1 total, %2 used, %3 (or %4%) free.")
            .arg(sm_str(total)).arg(sm_str(used))
            .arg(sm_str(free)).arg(percent, 0, 'f', (percent >= 10.0) ? 0 : 2);
    }
    return ret;
}

static const QString usage_str_mb(float total, float used, float free)
{
    return usage_str_kb((long long)(total*1024), (long long)(used*1024),
                        (long long)(free*1024));
}

static void disk_usage_with_rec_time_kb(QStringList& out, long long total,
                                        long long used, long long free,
                                        const recprof2bps_t& prof2bps)
{
    const QString tail = QObject::tr(", using your %1 rate of %2 Kb/sec");

    out<<usage_str_kb(total, used, free);
    if (free<0)
        return;

    recprof2bps_t::const_iterator it = prof2bps.begin();
    for (; it != prof2bps.end(); ++it)
    {
        const QString pro =
                tail.arg(it.key()).arg((int)((float)it.data() / 1024.0));
        
        long long bytesPerMin = (it.data() >> 1) * 15;
        uint minLeft = ((free<<5)/bytesPerMin)<<5;
        minLeft = (minLeft/15)*15;
        uint hoursLeft = minLeft/60;
        if (hoursLeft > 3)
            out<<QObject::tr("%1 hours left").arg(hoursLeft) + pro;
        else if (minLeft > 90)
            out<<QObject::tr("%1 hours and %2 minutes left")
                .arg(hoursLeft).arg(minLeft%60) + pro;
        else
            out<<QObject::tr("%1 minutes left").arg(minLeft) + pro;
    }
}

static const QString uptimeStr(time_t uptime)
{
    int     days, hours, min, secs;
    QString str;

    str = QString("   " + QObject::tr("Uptime") + ": ");

    if (uptime == 0)
        return str + "unknown";

    days = uptime/(60*60*24);
    uptime -= days*60*60*24;
    hours = uptime/(60*60);
    uptime -= hours*60*60;
    min  = uptime/60;
    secs = uptime%60;

    if (days > 0)
    {
        char    buff[6];
        QString dayLabel;

        if (days == 1)
            dayLabel = QObject::tr("day");
        else
            dayLabel = QObject::tr("days");

        sprintf(buff, "%d:%02d", hours, min);

        return str + QString("%1 %2, %3").arg(days).arg(dayLabel).arg(buff);
    }
    else
    {
        char  buff[9];

        sprintf(buff, "%d:%02d:%02d", hours, min, secs);

        return str + buff;
    }
}

/** \fn StatusBox::getActualRecordedBPS(QString hostnames)
 *  \brief Fills in recordingProfilesBPS w/ average bitrate from recorded table
 */
void StatusBox::getActualRecordedBPS(QString hostnames)
{
    recordingProfilesBPS.clear();

    QString querystr;
    MSqlQuery query(MSqlQuery::InitCon());

    querystr =
        "SELECT sum(filesize) * 8 / "
            "sum(((unix_timestamp(endtime) - unix_timestamp(starttime)))) "
            "AS avg_bitrate "
        "FROM recorded WHERE hostname in (%1) "
            "AND (unix_timestamp(endtime) - unix_timestamp(starttime)) > 300;";

    query.prepare(querystr.arg(hostnames));

    if (query.exec() && query.isActive() && query.size() > 0 && query.next() &&
        query.value(0).toDouble() > 0)
    {
        recordingProfilesBPS[QObject::tr("average")] =
            (int)(query.value(0).toDouble());
    }

    querystr =
        "SELECT max(filesize * 8 / "
            "(unix_timestamp(endtime) - unix_timestamp(starttime))) "
            "AS max_bitrate "
        "FROM recorded WHERE hostname in (%1) "
            "AND (unix_timestamp(endtime) - unix_timestamp(starttime)) > 300;";

    query.prepare(querystr.arg(hostnames));

    if (query.exec() && query.isActive() && query.size() > 0 && query.next() &&
        query.value(0).toDouble() > 0)
    {
        recordingProfilesBPS[QObject::tr("maximum")] =
            (int)(query.value(0).toDouble());
    }
}

/** \fn StatusBox::doMachineStatus()
 *  \brief Show machine status.
 *  
 *   This returns statisics for master backend when using
 *   a frontend only machine. And returns info on the current
 *   system if frontend is running on a backend machine.
 *  \bug We should report on all backends and the current frontend.
 */
void StatusBox::doMachineStatus()
{
    int           count(0);
    int           totalM, usedM, freeM;    // Physical memory
    int           totalS, usedS, freeS;    // Virtual  memory (swap)
    time_t        uptime;
    int           detailBegin;
    QString       detailString;
    int           detailLoop;

    contentLines.clear();
    contentDetail.clear();
    contentFont.clear();
    doScroll = true;

    detailBegin = count;
    detailString = "";

    if (isBackend)
        contentLines[count] = QObject::tr("System") + ":";
    else
        contentLines[count] = QObject::tr("This machine") + ":";
    detailString += contentLines[count] + "\n";
    count++;

    // uptime
    if (!getUptime(uptime))
        uptime = 0;
    contentLines[count] = uptimeStr(uptime);

    // weighted average loads
    contentLines[count].append(".   " + QObject::tr("Load") + ": ");

#ifdef _WIN32
    contentLines[count].append(
        QObject::tr("unknown") + " - getloadavg() " + QObject::tr("failed"));
#else // if !_WIN32
    double loads[3];
    if (getloadavg(loads,3) == -1)
        contentLines[count].append(QObject::tr("unknown") +
                                   " - getloadavg() " + QObject::tr("failed"));
    else
    {
        char buff[30];

        sprintf(buff, "%0.2lf, %0.2lf, %0.2lf", loads[0], loads[1], loads[2]);
        contentLines[count].append(QString(buff));
    }
#endif // _WIN32

    detailString += contentLines[count] + "\n";
    count++;


    // memory usage
    if (getMemStats(totalM, freeM, totalS, freeS))
    {
        usedM = totalM - freeM;
        if (totalM > 0)
        {
            contentLines[count] = "   " + QObject::tr("RAM") +
                                  ": "  + usage_str_mb(totalM, usedM, freeM);
            detailString += contentLines[count] + "\n";
            count++;
        }
        usedS = totalS - freeS;
        if (totalS > 0)
        {
            contentLines[count] = "   " + QObject::tr("Swap") +
                                  ": "  + usage_str_mb(totalS, usedS, freeS);
            detailString += contentLines[count] + "\n";
            count++;
        }
    }

    for (detailLoop = detailBegin; detailLoop < count; detailLoop++)
        contentDetail[detailLoop] = detailString;

    detailBegin = count;
    detailString = "";

    if (!isBackend)
    {
        contentLines[count] = QObject::tr("MythTV server") + ":";
        detailString += contentLines[count] + "\n";
        count++;

        // uptime
        if (!RemoteGetUptime(uptime))
            uptime = 0;
        contentLines[count] = uptimeStr(uptime);

        // weighted average loads
        contentLines[count].append(".   " + QObject::tr("Load") + ": ");
        float loads[3];
        if (RemoteGetLoad(loads))
        {
            char buff[30];

            sprintf(buff, "%0.2f, %0.2f, %0.2f", loads[0], loads[1], loads[2]);
            contentLines[count].append(QString(buff));
        }
        else
            contentLines[count].append(QObject::tr("unknown"));

        detailString += contentLines[count] + "\n";
        count++;

        // memory usage
        if (RemoteGetMemStats(totalM, freeM, totalS, freeS))
        {
            usedM = totalM - freeM;
            if (totalM > 0)
            {
                contentLines[count] = "   " + QObject::tr("RAM") +
                                      ": "  + usage_str_mb(totalM, usedM, freeM);
                detailString += contentLines[count] + "\n";
                count++;
            }

            usedS = totalS - freeS;
            if (totalS > 0)
            {
                contentLines[count] = "   " + QObject::tr("Swap") +
                                      ": "  + usage_str_mb(totalS, usedS, freeS);
                detailString += contentLines[count] + "\n";
                count++;
            }
        }
    }

    for (detailLoop = detailBegin; detailLoop < count; detailLoop++)
        contentDetail[detailLoop] = detailString;

    detailBegin = count;
    detailString = "";

    // get free disk space
    QString hostnames;
    
    vector<FileSystemInfo> fsInfos = RemoteGetFreeSpace();
    for (uint i=0; i<fsInfos.size(); i++)
    {
        // For a single-directory installation just display the totals
        if ((fsInfos.size() == 2) && (i == 0) &&
            (fsInfos[i].directory != "TotalDiskSpace") &&
            (fsInfos[i+1].directory == "TotalDiskSpace"))
            i++;

        hostnames = QString("\"%1\"").arg(fsInfos[i].hostname);
        hostnames.replace(QRegExp(" "), "");
        hostnames.replace(QRegExp(","), "\",\"");

        getActualRecordedBPS(hostnames);

        QStringList list;
        disk_usage_with_rec_time_kb(list,
            fsInfos[i].totalSpaceKB, fsInfos[i].usedSpaceKB,
            fsInfos[i].totalSpaceKB - fsInfos[i].usedSpaceKB,
            recordingProfilesBPS);

        if (fsInfos[i].directory == "TotalDiskSpace")
        {
            contentLines[count] = QObject::tr("Total Disk Space:");
            detailString += contentLines[count] + "\n";
            count++;
        }
        else
        {
            contentLines[count] = 
                QObject::tr("MythTV Drive #%1:")
                            .arg(fsInfos[i].fsID);
            detailString += contentLines[count] + "\n";
            count++;

            QStringList tokens = QStringList::split(",", fsInfos[i].directory);

            if (tokens.size() > 1)
            {
                contentLines[count++] +=
                    QString("   ") + QObject::tr("Directories:");

                unsigned int curToken = 0;
                while (curToken < tokens.size())
                    contentLines[count++] =
                        QString("      ") + tokens[curToken++];
            }
            else
            {
                contentLines[count++] += QString("   " ) +
                    QObject::tr("Directory:") + " " + fsInfos[i].directory;
            }
        }

        QStringList::iterator it = list.begin();
        for (;it != list.end(); ++it)
        {
            contentLines[count] =  QString("   ") + (*it);
            detailString += contentLines[count] + "\n";
            count++;
        }

        for (detailLoop = detailBegin; detailLoop < count; detailLoop++)
            contentDetail[detailLoop] = detailString;

        detailBegin = count;
        detailString = "";
    }

    contentTotalLines = count;
    update(ContentRect);
}

/** \fn StatusBox::doAutoExpireList()
 *  \brief Show list of recordings which may AutoExpire
 */
void StatusBox::doAutoExpireList()
{
    int                   count(0);
    ProgramInfo*          pginfo;
    QString               contentLine;
    QString               detailInfo;
    QString               staticInfo;
    long long             totalSize(0);
    long long             liveTVSize(0);
    int                   liveTVCount(0);
    long long             deletedGroupSize(0);
    int                   deletedGroupCount(0);

    contentLines.clear();
    contentDetail.clear();
    contentFont.clear();
    doScroll = true;

    vector<ProgramInfo *>::iterator it;
    for (it = expList.begin(); it != expList.end(); it++)
        delete *it;
    expList.clear();

    RemoteGetAllExpiringRecordings(expList);

    for (it = expList.begin(); it != expList.end(); it++)
    {
        pginfo = *it;
       
        totalSize += pginfo->filesize;
        if (pginfo->recgroup == "LiveTV")
        {
            liveTVSize += pginfo->filesize;
            liveTVCount++;
        }
        else if (pginfo->recgroup == "Deleted")
        {
            deletedGroupSize += pginfo->filesize;
            deletedGroupCount++;
        }
    }

    staticInfo = tr("%1 recordings consuming %2 are allowed to expire")
                    .arg(expList.size()).arg(sm_str(totalSize / 1024)) + "\n";

    if (liveTVCount)
        staticInfo += tr("%1 of these are LiveTV and consume %2")
                        .arg(liveTVCount).arg(sm_str(liveTVSize / 1024)) + "\n";

    if (deletedGroupCount)
        staticInfo += tr("%1 of these are Deleted and consume %2")
                .arg(deletedGroupCount).arg(sm_str(deletedGroupSize / 1024)) + "\n";

    for (it = expList.begin(); it != expList.end(); it++)
    {
        pginfo = *it;
        contentLine = pginfo->recstartts.toString(dateFormat) + " - ";

        if ((pginfo->recgroup == "LiveTV") ||
            (pginfo->recgroup == "Deleted"))
            contentLine += "(" + tr(pginfo->recgroup) + ") ";
        else
            contentLine += "(" + pginfo->recgroup + ") ";

        contentLine += pginfo->title +
                       " (" + sm_str(pginfo->filesize / 1024) + ")";

        detailInfo = staticInfo;
        detailInfo += pginfo->recstartts.toString(timeDateFormat) + " - " +
                      pginfo->recendts.toString(timeDateFormat);

        detailInfo += " (" + sm_str(pginfo->filesize / 1024) + ")";

        if ((pginfo->recgroup == "LiveTV") ||
            (pginfo->recgroup == "Deleted"))
            detailInfo += " (" + tr(pginfo->recgroup) + ")";
        else
            detailInfo += " (" + pginfo->recgroup + ")";

        detailInfo += "\n" + pginfo->title;

        if (pginfo->subtitle != "")
            detailInfo += " - " + pginfo->subtitle + "";

        contentLines[count] = contentLine;
        contentDetail[count] = detailInfo;
        count++;
    }

    contentTotalLines = count;
    update(ContentRect);
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
