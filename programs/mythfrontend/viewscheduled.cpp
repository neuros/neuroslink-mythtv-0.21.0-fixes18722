#include <qlayout.h>
#include <qpushbutton.h>
#include <qbuttongroup.h>
#include <qlabel.h>
#include <qcursor.h>
#include <qsqldatabase.h>
#include <qdatetime.h>
#include <qapplication.h>
#include <qregexp.h>
#include <qheader.h>

#include <stdlib.h>

#include <iostream>
using namespace std;

#include "viewscheduled.h"
#include "scheduledrecording.h"
#include "customedit.h"
#include "proglist.h"
#include "tv_play.h"

#include "exitcodes.h"
#include "dialogbox.h"
#include "mythcontext.h"
#include "remoteutil.h"

QWaitCondition vsbIsVisibleCond;

void *ViewScheduled::RunViewScheduled(void *player, bool showTV)
{
    qApp->lock();

    ViewScheduled *vsb = new ViewScheduled(gContext->GetMainWindow(),
                            "view scheduled", (TV*)player, showTV);
    vsb->Show();
    qApp->unlock();
    vsbIsVisibleCond.wait();
    delete vsb;

    return NULL;
}

ViewScheduled::ViewScheduled(MythMainWindow *parent, const char *name,
                            TV* player, bool showTV)
             : MythDialog(parent, name)
{
    dateformat = gContext->GetSetting("ShortDateFormat", "M/d");
    timeformat = gContext->GetSetting("TimeFormat", "h:mm AP");
    channelFormat = gContext->GetSetting("ChannelFormat", "<num> <sign>");
    showAll = !gContext->GetNumSetting("ViewSchedShowLevel", 0);

    fullRect = QRect(0, 0, size().width(), size().height());
    listRect = QRect(0, 0, 0, 0);
    infoRect = QRect(0, 0, 0, 0);
    conflictRect = QRect(0, 0, 0, 0);
    showLevelRect = QRect(0, 0, 0, 0);
    recStatusRect = QRect(0, 0, 0, 0);

    m_player = player;
    theme = new XMLParse();
    theme->SetWMult(wmult);
    theme->SetHMult(hmult);
    if (m_player && m_player->IsRunning() && showTV)
    {
        if (!theme->LoadTheme(xmldata, "conflict-video"))
            theme->LoadTheme(xmldata, "conflict");
    }
    else
        theme->LoadTheme(xmldata, "conflict");
    LoadWindow(xmldata);

    if (m_player)
        EmbedTVWindow();

    LayerSet *container = theme->GetSet("selector");
    if (container)
    {
        UIListType *ltype = (UIListType *)container->GetType("conflictlist");
        if (ltype)
            listsize = ltype->GetItems();
    }
    else
    {
        VERBOSE(VB_IMPORTANT, "ViewScheduled::ViewScheduled(): "
                "Failed to get selector object.");
        exit(FRONTEND_BUGGY_EXIT_NO_SELECTOR);
    }

    updateBackground();

    inEvent = false;
    inFill = false;
    needFill = false;

    curcard  = 0;
    maxcard  = 0;
    curinput = 0;
    maxinput = 0;
    listPos  = 0;
    FillList();
 
    setNoErase();

    gContext->addListener(this);
    gContext->addCurrentLocation("ViewScheduled");
}

ViewScheduled::~ViewScheduled()
{
    gContext->SaveSetting("ViewSchedShowLevel", !showAll);
    gContext->removeListener(this);
    gContext->removeCurrentLocation();

    delete theme;
}

void ViewScheduled::keyPressEvent(QKeyEvent *e)
{
    if (inEvent)
        return;

    inEvent = true;

    bool handled = false;
    QStringList actions;
    if (gContext->GetMainWindow()->TranslateKeyPress("TV Frontend", e, actions))
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "SELECT")
                selected();
            else if (action == "MENU" || action == "INFO")
                edit();
            else if (action == "CUSTOMEDIT")
                customEdit();
            else if (action == "DELETE")
                remove();
            else if (action == "UPCOMING")
                upcoming();
            else if (action == "DETAILS")
                details();
            else if (action == "ESCAPE" || action == "LEFT")
            {
                vsbIsVisibleCond.wakeAll();
                done(MythDialog::Accepted);
            }
            else if (action == "UP")
                cursorUp();
            else if (action == "DOWN")
                cursorDown();
            else if (action == "PAGEUP")
                pageUp();
            else if (action == "PAGEDOWN")
                pageDown();
            else if (action == "1")
                setShowAll(true);
            else if (action == "2")
                setShowAll(false);
            else if (action == "PREVVIEW" || action == "NEXTVIEW")
                setShowAll(!showAll);
            else if (action == "VIEWCARD")
                viewCards();
            else if (action == "VIEWINPUT")
                viewInputs();
            else
                handled = false;
        }
    }

    if (!handled)
        MythDialog::keyPressEvent(e);

    if (needFill)
    {
        do
        {
            needFill = false;
            FillList();
        } while (needFill);

        update(fullRect);
    }

    inEvent = false;
}

void ViewScheduled::LoadWindow(QDomElement &element)
{
    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement e = child.toElement();
        if (!e.isNull())
        {
            if (e.tagName() == "font")
                theme->parseFont(e);
            else if (e.tagName() == "container")
                parseContainer(e);
            else
            {
                VERBOSE(VB_IMPORTANT,
                        QString("ViewScheduled: Unknown child element: %1. "
                                "Ignoring.").arg(e.tagName()));
            }
        }
    }
}

void ViewScheduled::parseContainer(QDomElement &element)
{
    QRect area;
    QString name;
    int context;
    theme->parseContainer(element, name, context, area);

    if (name.lower() == "selector")
        listRect = area;
    if (name.lower() == "program_info")
        infoRect = area;
    if (name.lower() == "conflict_info")
        conflictRect = area;
    if (name.lower() == "showlevel_info")
        showLevelRect = area;
    if (name.lower() == "status_info")
        recStatusRect = area;
    if (name.lower() == "tv_video")
        tvRect = area;
}

void ViewScheduled::updateBackground(void)
{
    QPixmap bground(size());
    bground.fill(this, 0, 0);

    QPainter tmp(&bground);

    LayerSet *container = theme->GetSet("background");
    if (!container)
        return;

    container->Draw(&tmp, 0, 0);

    tmp.end();
    myBackground = bground;

    setPaletteBackgroundPixmap(myBackground);
}

void ViewScheduled::paintEvent(QPaintEvent *e)
{
    if (inFill)
        return;

    QRect r = e->rect();
    QPainter p(this);
 
    if (r.intersects(listRect))
        updateList(&p);
    if (r.intersects(infoRect))
        updateInfo(&p);
    if (r.intersects(conflictRect))
        updateConflict(&p);
    if (r.intersects(showLevelRect))
        updateShowLevel(&p);
    if (r.intersects(recStatusRect))
        updateRecStatus(&p);
}

void ViewScheduled::cursorDown(bool page)
{
    if (listPos < (int)recList.count() - 1)
    {
        listPos += (page ? listsize : 1);
        if (listPos > (int)recList.count() - 1)
            listPos = recList.count() - 1;
        update(fullRect);
    }
}

void ViewScheduled::cursorUp(bool page)
{
    if (listPos > 0)
    {
        listPos -= (page ? listsize : 1);
        if (listPos < 0)
            listPos = 0;
        update(fullRect);
    }
}

void ViewScheduled::FillList(void)
{
    inFill = true;

    QString callsign;
    QDateTime startts, recstartts;

    if (recList[listPos])
    {
        callsign = recList[listPos]->chansign;
        startts = recList[listPos]->startts;
        recstartts = recList[listPos]->recstartts;
    }

    recList.FromScheduler(conflictBool);

    QDateTime now = QDateTime::currentDateTime();

    QMap<int, int> toomanycounts;

    ProgramInfo *p = recList.first();
    while (p)
    {
        if ((p->recendts >= now || p->endts >= now) && 
            (showAll || p->recstatus <= rsWillRecord || 
             p->recstatus == rsDontRecord ||
             (p->recstatus == rsTooManyRecordings && 
              ++toomanycounts[p->recordid] <= 1) ||
             (p->recstatus > rsTooManyRecordings && 
              p->recstatus != rsRepeat &&
              p->recstatus != rsNeverRecord)))
        {
            cardref[p->cardid]++;
            if (p->cardid > maxcard)
                maxcard = p->cardid;

            inputref[p->inputid]++;
            if (p->inputid > maxinput)
                maxinput = p->inputid;

            p = recList.next();
        }
        else
        {
            recList.remove();
            p = recList.current();
        }
    }

    if (!callsign.isNull())
    {
        listPos = recList.count() - 1;
        int i;
        for (i = listPos; i >= 0; i--)
        {
            if (callsign == recList[i]->chansign &&
                startts == recList[i]->startts)
            {
                listPos = i;
                break;
            }
            else if (recstartts <= recList[i]->recstartts)
                listPos = i;
        }
    }

    if (conflictBool)
    {
        // Find first conflict and store in conflictDate field
        for (uint i = 0; i < recList.count(); i++)
        {
            ProgramInfo *p = recList[i];
            if (p->recstatus == rsConflict)
            {
                conflictDate = p->recstartts.date();
                break;
            }
        }
    }

    inFill = false;
}

void ViewScheduled::updateList(QPainter *p)
{
    QRect pr = listRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);
    
    LayerSet *container = theme->GetSet("selector");
    if (container)
    {
        UIListType *ltype = (UIListType *)container->GetType("conflictlist");
        if (ltype)
        {
            ltype->ResetList();
            ltype->SetActive(true);

            int listCount = recList.count();

            int skip;
            if (listCount <= listsize || listPos <= listsize / 2)
                skip = 0;
            else if (listPos >= listCount - listsize + listsize / 2)
                skip = listCount - listsize;
            else
                skip = listPos - listsize / 2;

            ltype->SetUpArrow(skip > 0);
            ltype->SetDownArrow(skip + listsize < listCount);

            int i;
            for (i = 0; i < listsize; i++)
            {
                if (i + skip >= listCount)
                    break;

                ProgramInfo *p = recList[skip+i];

                QString temp;

                temp = (p->recstartts).toString(dateformat);
                temp += " " + (p->recstartts).toString(timeformat);
                ltype->SetItemText(i, 1, temp);

                ltype->SetItemText(i, 2, p->ChannelText(channelFormat));

                temp = p->title;
                if ((p->subtitle).stripWhiteSpace().length() > 0)
                    temp += " - \"" + p->subtitle + "\"";
                ltype->SetItemText(i, 3, temp);

                temp = p->RecStatusChar();
                ltype->SetItemText(i, 4, temp);

                if (i + skip == listPos)
                    ltype->SetItemCurrent(i);

                if (p->recstatus == rsRecording)
                    ltype->EnableForcedFont(i, "recording");
                else if (p->recstatus == rsConflict ||
                         p->recstatus == rsOffLine ||
                         p->recstatus == rsAborted)
                    ltype->EnableForcedFont(i, "conflictingrecording");
                else if (p->recstatus == rsWillRecord)
                    {
                    if ((curcard == 0 && curinput == 0) || 
                        p->cardid == curcard || p->inputid == curinput)
                        ltype->EnableForcedFont(i, "record");
                    }
                else if (p->recstatus == rsRepeat ||
                         p->recstatus == rsOtherShowing ||
                         p->recstatus == rsNeverRecord ||
                         (p->recstatus != rsDontRecord &&
                          p->recstatus <= rsEarlierShowing))
                    ltype->EnableForcedFont(i, "disabledrecording"); 
            }
        }
    }

    if (recList.count() == 0)
        container = theme->GetSet("norecordings_list");

    if (container)
    {
       container->Draw(&tmp, 0, 0);
       container->Draw(&tmp, 1, 0);
       container->Draw(&tmp, 2, 0);
       container->Draw(&tmp, 3, 0);
       container->Draw(&tmp, 4, 0);
       container->Draw(&tmp, 5, 0);
       container->Draw(&tmp, 6, 0);
       container->Draw(&tmp, 7, 0);
       container->Draw(&tmp, 8, 0);
    }

    tmp.end();
    p->drawPixmap(pr.topLeft(), pix);
}

void ViewScheduled::updateConflict(QPainter *p)
{
    QRect pr = conflictRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);

    LayerSet *container = theme->GetSet("conflict_info");
    if (container)
    {
        UITextType *wtype = (UITextType *)container->GetType("warning");
        UITextType *stype = (UITextType *)container->GetType("status");

        /* if wtype doesn't exist in the theme, use stype instead */
        if (!wtype)
            wtype = stype;

        if (stype)
        {
            if (conflictBool)
            {
                // figure out caption based on conflictDate
                QString cstring = tr("Time Conflict");
                QDate now = QDate::currentDate();
                int daysToConflict = now.daysTo(conflictDate);

                if (daysToConflict == 0)
                    cstring = tr("Conflict Today");
                else if (daysToConflict > 0)
                    cstring = QString(tr("Conflict ")) + 
                            conflictDate.toString(dateformat);

                stype->SetText("");
                wtype->SetText(cstring);
            }
            else
            {
                wtype->SetText("");
                stype->SetText(tr("No Conflicts"));
            }
        }
    }

    if (container)
    {
        container->Draw(&tmp, 4, 0);
        container->Draw(&tmp, 5, 0);
        container->Draw(&tmp, 6, 0);
        container->Draw(&tmp, 7, 0);
        container->Draw(&tmp, 8, 0);
    }

    tmp.end();
    p->drawPixmap(pr.topLeft(), pix);
}

void ViewScheduled::updateShowLevel(QPainter *p)
{
    QRect pr = showLevelRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);

    LayerSet *container = theme->GetSet("showlevel_info");
    if (container)
    {
        UITextType *type = (UITextType *)container->GetType("showlevel");
        if (type)
        {
            if (showAll)
                type->SetText(tr("All"));
            else
                type->SetText(tr("Important"));
        }
    }

    if (container)
    {
        container->Draw(&tmp, 4, 0);
        container->Draw(&tmp, 5, 0);
        container->Draw(&tmp, 6, 0);
        container->Draw(&tmp, 7, 0);
        container->Draw(&tmp, 8, 0);
    }

    tmp.end();
    p->drawPixmap(pr.topLeft(), pix);
}

void ViewScheduled::updateInfo(QPainter *p)
{
    QRect pr = infoRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);
    QMap<QString, QString> infoMap;

    LayerSet *container = theme->GetSet("program_info");
    if (container)
    {
        ProgramInfo *p = recList[listPos];
        if (p)
        {
            p->ToMap(infoMap);
            container->ClearAllText();
            container->SetText(infoMap);
        }
    }

    if (recList.count() == 0)
        container = theme->GetSet("norecordings_info");

    if (container)
    {
        container->Draw(&tmp, 4, 0);
        container->Draw(&tmp, 5, 0);
        container->Draw(&tmp, 6, 0);
        container->Draw(&tmp, 7, 0);
        container->Draw(&tmp, 8, 0);
    }

    tmp.end();
    p->drawPixmap(pr.topLeft(), pix);
}

void ViewScheduled::updateRecStatus(QPainter *p)
{
    QRect pr = recStatusRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);
    QMap<QString, QString> infoMap;

    LayerSet *container = theme->GetSet("status_info");
    if (container)
    {
        ProgramInfo *p = recList[listPos];
        if (p)
        {
            p->ToMap(infoMap);
            container->ClearAllText();
            container->SetText(infoMap);
        }
    }

    if (container)
    {
        container->Draw(&tmp, 4, 0);
        container->Draw(&tmp, 5, 0);
        container->Draw(&tmp, 6, 0);
        container->Draw(&tmp, 7, 0);
        container->Draw(&tmp, 8, 0);
    }

    tmp.end();
    p->drawPixmap(pr.topLeft(), pix);
}

void ViewScheduled::edit()
{
    ProgramInfo *p = recList[listPos];
    if (!p)
        return;

    p->EditScheduled();
}

void ViewScheduled::customEdit()
{
    ProgramInfo *p = recList[listPos];
    if (!p)
        return;

    CustomEdit *ce = new CustomEdit(gContext->GetMainWindow(),
                                    "customedit", p);
    ce->exec();
    delete ce;
}

void ViewScheduled::remove()
{
    ProgramInfo *p = recList[listPos];
    if (!p)
        return;

    ScheduledRecording *record = new ScheduledRecording();
    int recid = p->recordid;
    record->loadByID(recid);

    QString message =
        tr("Delete '%1' %2 rule?").arg(record->getRecordTitle())
                                  .arg(p->RecTypeText());

    bool ok = MythPopupBox::showOkCancelPopup(gContext->GetMainWindow(), "",
                                              message, false);

    if (ok)
    {
        record->remove();
        ScheduledRecording::signalChange(recid);
    }
    record->deleteLater();

    EmbedTVWindow();
}

void ViewScheduled::upcoming()
{
    ProgramInfo *p = recList[listPos];

    if (!p)
        return;

    ProgLister *pl = new ProgLister(plTitle, p->title, "",
                                   gContext->GetMainWindow(), "proglist");
    pl->exec();
    delete pl;

    EmbedTVWindow();
}

void ViewScheduled::details()
{
    ProgramInfo *p = recList[listPos];

    if (p)
        p->showDetails();
 
    EmbedTVWindow();
}

void ViewScheduled::selected()
{
    ProgramInfo *p = recList[listPos];
    if (!p)
        return;

    p->EditRecording();

    EmbedTVWindow();
}

void ViewScheduled::customEvent(QCustomEvent *e)
{
    if ((MythEvent::Type)(e->type()) != MythEvent::MythEventMessage)
        return;

    MythEvent *me = (MythEvent *)e;
    QString message = me->Message();
    if (message != "SCHEDULE_CHANGE")
        return;

    if (inEvent)
    {
        needFill = true;
        return;
    }

    inEvent = true;

    do
    {
        needFill = false;
        FillList();
    } while (needFill);

    update(fullRect);

    inEvent = false;
}

void ViewScheduled::setShowAll(bool all)
{
    showAll = all;

    needFill = true;
}

void ViewScheduled::viewCards()
{
    curinput = 0;
    needFill = true;

    curcard++;
    while (curcard <= maxcard)
    {
        if (cardref[curcard] > 0)
            return;
        curcard++;
    }
    curcard = 0;
}

void ViewScheduled::viewInputs()
{
    curcard = 0;
    needFill = true;

    curinput++;
    while (curinput <= maxinput)
    {
        if (inputref[curinput] > 0)
            return;
        curinput++;
    }
    curinput = 0;
}

void ViewScheduled::EmbedTVWindow(void)
{
    if (m_player)
    {
        m_player->EmbedOutput(this->winId(), tvRect.x(), tvRect.y(),
                            tvRect.width(), tvRect.height());
    }
}
        
