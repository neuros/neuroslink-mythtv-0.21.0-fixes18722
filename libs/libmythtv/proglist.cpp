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

#include <iostream>
#include <map>
#include <vector>
#include <algorithm>
#include <cassert>
using namespace std;

#include "proglist.h"
#include "scheduledrecording.h"
#include "customedit.h"
#include "dialogbox.h"
#include "mythcontext.h"
#include "remoteutil.h"
#include "mythdbcon.h"
#include "channelutil.h"

ProgLister::ProgLister(ProgListType pltype,
                       const QString &view, const QString &from,
                       MythMainWindow *parent,
                       const char *name)
            : MythDialog(parent, name)
{
    type = pltype;
    addTables = from;
    startTime = QDateTime::currentDateTime();
    searchTime = startTime;

    dayFormat = gContext->GetSetting("DateFormat");
    hourFormat = gContext->GetSetting("TimeFormat");
    timeFormat = gContext->GetSetting("ShortDateFormat") + " " + hourFormat;
    fullDateFormat = dayFormat + " " + hourFormat;
    channelOrdering = gContext->GetSetting("ChannelOrdering", "channum");
    channelFormat = gContext->GetSetting("ChannelFormat", "<num> <sign>");

    switch (pltype)
    {
        case plTitleSearch:   searchtype = kTitleSearch;   break;
        case plKeywordSearch: searchtype = kKeywordSearch; break;
        case plPeopleSearch:  searchtype = kPeopleSearch;  break;
        case plPowerSearch:   searchtype = kPowerSearch;   break;
        case plSQLSearch:     searchtype = kPowerSearch;   break;
        case plStoredSearch:  searchtype = kPowerSearch;   break;
        default:              searchtype = kNoSearch;      break;
    }

    allowEvents = true;
    allowUpdates = true;
    updateAll = false;
    refillAll = false;
    titleSort = false;
    reverseSort = false;
    useGenres = false;

    fullRect = QRect(0, 0, size().width(), size().height());
    viewRect = QRect(0, 0, 0, 0);
    listRect = QRect(0, 0, 0, 0);
    infoRect = QRect(0, 0, 0, 0);
    theme = new XMLParse();
    theme->SetWMult(wmult);
    theme->SetHMult(hmult);

    if (!theme->LoadTheme(xmldata, "programlist"))
    {
        DialogBox *dlg = new DialogBox(
            gContext->GetMainWindow(),
            QObject::tr(
                "The theme you are using does not contain the "
                "%1 element. Please contact the theme creator "
                "and ask if they could please update it.<br><br>"
                "The next screen will be empty. "
                "Escape out of it to return to the menu.")
            .arg("'programlist'"));

        dlg->AddButton("OK");
        dlg->exec();
        dlg->deleteLater();

        return;
    }

    LoadWindow(xmldata);

    LayerSet *container = theme->GetSet("selector");
    assert(container);
    UIListType *ltype = (UIListType *)container->GetType("proglist");
    if (ltype)
        listsize = ltype->GetItems();

    choosePopup = NULL;
    chooseListBox = NULL;
    chooseLineEdit = NULL;
    chooseEditButton = NULL;
    chooseOkButton = NULL;
    chooseDeleteButton = NULL;
    chooseRecordButton = NULL;
    chooseDay = NULL;
    chooseHour = NULL;

    powerPopup = NULL;
    powerTitleEdit = NULL;
    powerSubtitleEdit = NULL;
    powerDescEdit = NULL;
    powerCatType = NULL;
    powerGenre = NULL;
    powerStation = NULL;

    curView = -1;
    fillViewList(view);

    curItem = -1;
    fillItemList();

    if (curView < 0)
        QApplication::postEvent(this, new MythEvent("CHOOSE_VIEW"));

    updateBackground();

    setNoErase();

    gContext->addListener(this);
    gContext->addCurrentLocation("ProgLister");
}

ProgLister::~ProgLister()
{
    itemList.clear();
    gContext->removeListener(this);
    gContext->removeCurrentLocation();
    delete theme;
}

void ProgLister::keyPressEvent(QKeyEvent *e)
{
    if (!allowEvents)
        return;

    allowEvents = false;
    bool handled = false;

    QStringList actions;
    gContext->GetMainWindow()->TranslateKeyPress("TV Frontend", e, actions);

    for (unsigned int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        handled = true;

        if (action == "UP")
            cursorUp(false);
        else if (action == "DOWN")
            cursorDown(false);
        else if (action == "PAGEUP")
            cursorUp(true);
        else if (action == "PAGEDOWN")
            cursorDown(true);
        else if (action == "PREVVIEW")
            prevView();
        else if (action == "NEXTVIEW")
            nextView();
        else if (action == "MENU")
            chooseView();
        else if (action == "SELECT" || action == "RIGHT")
            select();
        else if (action == "LEFT")
            accept();
        else if (action == "INFO")
            edit();
        else if (action == "CUSTOMEDIT")
            customEdit();
        else if (action == "DELETE")
            remove();
        else if (action == "UPCOMING")
            upcoming();
        else if (action == "DETAILS")
            details();
        else if (action == "TOGGLERECORD")
            quickRecord();
        else if (action == "1")
        {
            if (titleSort == true)
            {
                titleSort = false;
                reverseSort = false;
            }
            else
            {
                reverseSort = !reverseSort;
            }
            refillAll = true;
        }
        else if (action == "2")
        {
            if (titleSort == false)
            {
                titleSort = true;
                reverseSort = false;
            }
            else
            {
                reverseSort = !reverseSort;
            }
            refillAll = true;
        }
        else
            handled = false;
    }

    if (!handled)
        MythDialog::keyPressEvent(e);

    if (refillAll)
    {
        allowUpdates = false;
        do
        {
            refillAll = false;
            fillItemList();
        } while (refillAll);
        allowUpdates = true;
        update(fullRect);
    }

    allowEvents = true;
}

void ProgLister::LoadWindow(QDomElement &element)
{
    QString name;
    int context;
    QRect area;

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement e = child.toElement();
        if (!e.isNull())
        {
            if (e.tagName() == "font")
                theme->parseFont(e);
            else if (e.tagName() == "container")
            {
                theme->parseContainer(e, name, context, area);
                if (name.lower() == "view")
                    viewRect = area;
                if (name.lower() == "selector")
                    listRect = area;
                if (name.lower() == "program_info")
                    infoRect = area;
            }
            else
            {
                VERBOSE(VB_IMPORTANT,
                        QString("ProgLister::LoadWindow(): Error, unknown "
                                "element '%1'. Ignoring.").arg(e.tagName()));
            }
        }
    }
}

void ProgLister::updateBackground(void)
{
    QPixmap bground(size());
    bground.fill(this, 0, 0);

    QPainter tmp(&bground);

    LayerSet *container = theme->GetSet("background");
    if (container)
    {
        UITextType *ltype = (UITextType *)container->GetType("sched");
        if (ltype)
        {
            QString value;
            switch (type)
            {
                case plTitle: value = tr("Program Listings"); break;
                case plNewListings: value = tr("New Title Search"); break;
                case plTitleSearch: value = tr("Title Search"); break;
                case plKeywordSearch: value = tr("Keyword Search"); break;
                case plPeopleSearch: value = tr("People Search"); break;
                case plStoredSearch: value = tr("Stored Search"); break;
                case plPowerSearch: value = tr("Power Search"); break;
                case plSQLSearch: value = tr("Power Search"); break;
                case plRecordid: value = tr("Rule Search"); break;
                case plCategory: value = tr("Category Search"); break;
                case plChannel: value = tr("Channel Search"); break;
                case plMovies: value = tr("Movie Search"); break;
                case plTime: value = tr("Time Search"); break;
                default: value = tr("Unknown Search"); break;
            }
            ltype->SetText(value);
        }
        container->Draw(&tmp, 0, 1);
    }

    tmp.end();

    setPaletteBackgroundPixmap(bground);
}

void ProgLister::paintEvent(QPaintEvent *e)
{
    if (!allowUpdates)
    {
        updateAll = true;
        return;
    }

    QRect r = e->rect();
    QPainter p(this);
 
    
    if (updateAll || r.intersects(listRect))
        updateList(&p);
    if (updateAll || r.intersects(infoRect))
        updateInfo(&p);
    if (updateAll || r.intersects(viewRect))
        updateView(&p);
    
    updateAll = false;
}

void ProgLister::cursorDown(bool page)
{
    if (curItem < (int)itemList.count() - 1)
    {
        curItem += (page ? listsize : 1);
        if (curItem > (int)itemList.count() - 1)
            curItem = itemList.count() - 1;
        update(fullRect);
    }
}

void ProgLister::cursorUp(bool page)
{
    if (curItem > 0)
    {
        curItem -= (page ? listsize : 1);
        if (curItem < 0)
            curItem = 0;
        update(fullRect);
    }
}

void ProgLister::prevView(void)
{
    if (type == plTime)
    {
        searchTime = searchTime.addSecs(-3600);
        curView = 0;
        viewList[curView] = searchTime.toString(fullDateFormat);
        viewTextList[curView] = viewList[curView];
        refillAll = true;
        return;
    }

    if (viewList.count() < 2)
        return;

    curView--;
    if (curView < 0)
        curView = viewList.count() - 1;

    curItem = -1;
    refillAll = true;
}

void ProgLister::nextView(void)
{
    if (type == plTime)
    {
        searchTime = searchTime.addSecs(3600);
        curView = 0;
        viewList[curView] = searchTime.toString(fullDateFormat);
        viewTextList[curView] = viewList[curView];
        refillAll = true;
        return;
    }
    if (viewList.count() < 2)
        return;

    curView++;
    if (curView >= (int)viewList.count())
        curView = 0;

    curItem = -1;
    refillAll = true;
}

void ProgLister::setViewFromList(int item)
{
    int view = item;

    if (!choosePopup || (!chooseListBox && !chooseEditButton))
        return;

    if (type == plTitleSearch || type == plKeywordSearch || 
        type == plPeopleSearch)
    {
        view--;
        if (view < 0)
        {
            if (chooseLineEdit)
                chooseLineEdit->setFocus();
            return;
        }
    }
    if (type == plPowerSearch)
    {
        view--;
        if (view < 0)
        {
            if (chooseEditButton)
                powerEdit();
            return;
        }
    }

    choosePopup->AcceptItem(item);

    if (view == curView)
        return;

    curView = view;

    curItem = -1;
    refillAll = true;
}

void ProgLister::chooseEditChanged(void)
{
    if (!chooseOkButton || !chooseRecordButton || !chooseLineEdit)
        return;

    chooseOkButton->setEnabled(chooseLineEdit->text().
                               stripWhiteSpace().length() > 0);
    chooseRecordButton->setEnabled(chooseLineEdit->text().
                                   stripWhiteSpace().length() > 0);
}

void ProgLister::chooseListBoxChanged(void)
{
    if (!chooseListBox)
        return;

    int view = chooseListBox->currentItem() - 1;

    if (chooseLineEdit)
    {
        if (view < 0)
            chooseLineEdit->setText("");
        else
            chooseLineEdit->setText(viewList[view]);

        chooseDeleteButton->setEnabled(view >= 0);
    }
    else if (chooseEditButton)
    {
        chooseDeleteButton->setEnabled(view >= 0);
        chooseRecordButton->setEnabled(view >= 0);
    }
}

void ProgLister::updateKeywordInDB(const QString &text)
{
    int oldview = chooseListBox->currentItem() - 1;
    int newview = viewList.findIndex(text);

    QString qphrase = NULL;

    if (newview < 0 || newview != oldview)
    {
        if (oldview >= 0)
        {
            qphrase = viewList[oldview].utf8();

            MSqlQuery query(MSqlQuery::InitCon());
            query.prepare("DELETE FROM keyword "
                          "WHERE phrase = :PHRASE AND searchtype = :TYPE;");
            query.bindValue(":PHRASE", qphrase);
            query.bindValue(":TYPE", searchtype);
            query.exec();
        }
        if (newview < 0)
        {
            qphrase = text.utf8();

            MSqlQuery query(MSqlQuery::InitCon());
            query.prepare("REPLACE INTO keyword (phrase, searchtype)"
                          "VALUES(:PHRASE, :TYPE );");
            query.bindValue(":PHRASE", qphrase);
            query.bindValue(":TYPE", searchtype);
            query.exec();
        }
    }
}

void ProgLister::setViewFromEdit(void)
{
    if (!choosePopup || !chooseListBox || !chooseLineEdit)
        return;

    QString text = chooseLineEdit->text();

    if (text.stripWhiteSpace().length() == 0)
        return;

    updateKeywordInDB(text);
  
    choosePopup->accept();

    fillViewList(text);

    curItem = -1;
    refillAll = true;
}

void ProgLister::setViewFromPowerEdit()
{
    if (!powerPopup || !choosePopup || !chooseListBox)
        return;

    QString text = "";
    text =     powerTitleEdit->text().replace(":","%").replace("*","%") + ":";
    text += powerSubtitleEdit->text().replace(":","%").replace("*","%") + ":";
    text +=     powerDescEdit->text().replace(":","%").replace("*","%") + ":";

    if (powerCatType->currentItem() > 0)
        text += typeList[powerCatType->currentItem()];
    text += ":";
    if (powerGenre->currentItem() > 0)
        text += genreList[powerGenre->currentItem()];
    text += ":";
    if (powerStation->currentItem() > 0)
        text += stationList[powerStation->currentItem()];

    if (text == ":::::")
        return;

    updateKeywordInDB(text);

    powerPopup->accept();

    fillViewList(text);

    curView = viewList.findIndex(text);

    curItem = -1;
    refillAll = true;
}

void ProgLister::addSearchRecord(void)
{
    if (!choosePopup || !chooseListBox)
        return;

    QString text = "";
    bool genreflag = false;

    if (chooseLineEdit)
        text = chooseLineEdit->text();
    else if (chooseEditButton)
        text = chooseListBox->currentText();
    else
        return;

    QString what = text;

    if (text.stripWhiteSpace().length() == 0)
        return;

    if (searchtype == kNoSearch)
    {
        VERBOSE(VB_IMPORTANT, "Unknown search in ProgLister");
        return;
    }

    if (searchtype == kPowerSearch)
    {
        if (text == "" || text == ":::::")
            return;

        MSqlBindings bindings;
        genreflag = powerStringToSQL(text.utf8(), what, bindings);

        if (what == "")
            return;

        MSqlEscapeAsAQuery(what, bindings);
    }

    ScheduledRecording *record = new ScheduledRecording();

    if (genreflag)
    {
        QString fromgenre = QString("LEFT JOIN programgenres ON "
                "program.chanid = programgenres.chanid AND "
                "program.starttime = programgenres.starttime ");
        record->loadBySearch(searchtype, text, fromgenre, what);
    }
    else
    {
        record->loadBySearch(searchtype, text, what);
    }

    record->exec();
    record->deleteLater();

    chooseListBox->setFocus();
    setViewFromEdit();
}

void ProgLister::deleteKeyword(void)
{
    if (!chooseDeleteButton || !chooseListBox)
        return;

    int view = chooseListBox->currentItem() - 1;

    if (view < 0)
        return;

    QString text = viewList[view];
    QString qphrase = text.utf8();

    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("DELETE FROM keyword "
                  "WHERE phrase = :PHRASE AND searchtype = :TYPE;");
    query.bindValue(":PHRASE", qphrase);
    query.bindValue(":TYPE", searchtype);
    query.exec();

    chooseListBox->removeItem(view + 1);
    viewList.remove(text);
    viewTextList.remove(text);

    if (view < curView)
        curView--;
    else if (view == curView)
        curView = -1;

    if (view >= (int)chooseListBox->count() - 1)
        view = chooseListBox->count() - 2;

    chooseListBox->setSelected(view + 1, true);

    if (viewList.count() < 1 && chooseLineEdit)
        chooseLineEdit->setFocus();
    else
        chooseListBox->setFocus();
}

void ProgLister::setViewFromTime(void)
{
    if (!choosePopup || !chooseDay || !chooseHour)
        return;

    int dayOffset = chooseDay->currentItem() - 1;
    searchTime.setDate(startTime.addDays(dayOffset).date());

    QTime m_hr;
    m_hr.setHMS(chooseHour->currentItem(), 0, 0);
    searchTime.setTime(m_hr);

    curView = 0;
    viewList[curView] = searchTime.toString(fullDateFormat);
    viewTextList[curView] = viewList[curView];

    choosePopup->accept();

    curItem = -1;
    refillAll = true;
}

void ProgLister::chooseView(void)
{
    if (type == plChannel || type == plCategory || type == plMovies ||
        type == plNewListings || type == plStoredSearch)
    {
        if (viewList.count() < 1)
            return;

        choosePopup = new MythPopupBox(gContext->GetMainWindow(), "");

        QString msg;
        switch (type)
        {
            case plMovies: msg = tr("Select Rating"); break;
            case plChannel: msg = tr("Select Channel"); break;
            case plCategory: msg = tr("Select Category"); break;
            case plNewListings: msg = tr("Select List"); break;
            case plStoredSearch: msg = QString("%1\n%2")
                    .arg(tr("Select a search stored from"))
                    .arg(tr("Custom Record")); break;
            default: msg = tr("Select"); break;
        }
        choosePopup->addLabel(msg);

        chooseListBox = new MythListBox(choosePopup);
        chooseListBox->setScrollBar(false);
        chooseListBox->setBottomScrollBar(false);
        chooseListBox->insertStringList(viewTextList);
        if (curView < 0)
            chooseListBox->setCurrentItem(0);
        else
            chooseListBox->setCurrentItem(curView);
        choosePopup->addWidget(chooseListBox);

        if (type == plCategory)
            choosePopup->addLabel(tr("0 .. 9 moves to Nx10 percent in list"));

        connect(chooseListBox, SIGNAL(accepted(int)),
                this,          SLOT(setViewFromList(int)));

        chooseListBox->setFocus();
        choosePopup->ExecPopup();

        chooseListBox = NULL; // deleted by popup delete

        choosePopup->hide();
        choosePopup->deleteLater();
        choosePopup = NULL;
    }
    else if (type == plTitleSearch || type == plKeywordSearch ||
             type == plPeopleSearch)
    {
        int oldView = curView;

        choosePopup = new MythPopupBox(gContext->GetMainWindow(), "");
        choosePopup->addLabel(tr("Select Phrase"));

        chooseListBox = new MythListBox(choosePopup);
        chooseListBox->setScrollBar(false);
        chooseListBox->setBottomScrollBar(false);
        chooseListBox->insertItem(tr("<New Phrase>"));
        chooseListBox->insertStringList(viewTextList);
        if (curView < 0)
            chooseListBox->setCurrentItem(0);
        else
            chooseListBox->setCurrentItem(curView + 1);
        choosePopup->addWidget(chooseListBox);

        chooseLineEdit = new MythRemoteLineEdit(choosePopup);
        if (curView < 0)
            chooseLineEdit->setText("");
        else
            chooseLineEdit->setText(viewList[curView]);
        choosePopup->addWidget(chooseLineEdit);

        chooseOkButton = new MythPushButton(choosePopup);
        chooseOkButton->setText(tr("OK"));
        choosePopup->addWidget(chooseOkButton);

        chooseDeleteButton = new MythPushButton(choosePopup);
        chooseDeleteButton->setText(tr("Delete"));
        choosePopup->addWidget(chooseDeleteButton);

        chooseRecordButton = new MythPushButton(choosePopup);
        chooseRecordButton->setText(tr("Record"));
        choosePopup->addWidget(chooseRecordButton);

        chooseOkButton->setEnabled(chooseLineEdit->text()
                                   .stripWhiteSpace().length() > 0);
        chooseDeleteButton->setEnabled(curView >= 0);
        chooseRecordButton->setEnabled(chooseLineEdit->text()
                                       .stripWhiteSpace().length() > 0);

        connect(chooseListBox, SIGNAL(accepted(int)),
                this,          SLOT(setViewFromList(int)));
        connect(chooseListBox, SIGNAL(menuButtonPressed(int)), chooseLineEdit, SLOT(setFocus()));
        connect(chooseListBox, SIGNAL(selectionChanged()), this, SLOT(chooseListBoxChanged()));
        connect(chooseLineEdit, SIGNAL(textChanged()), this, SLOT(chooseEditChanged()));
        connect(chooseOkButton, SIGNAL(clicked()), this, SLOT(setViewFromEdit()));
        connect(chooseDeleteButton, SIGNAL(clicked()), this, SLOT(deleteKeyword()));
        connect(chooseRecordButton, SIGNAL(clicked()), this, SLOT(addSearchRecord()));

        if (viewList.count() < 1)
            chooseLineEdit->setFocus();
        else
            chooseListBox->setFocus();
        choosePopup->ExecPopup();

        chooseLineEdit     = NULL; // deleted by popup delete
        chooseOkButton     = NULL; // deleted by popup delete
        chooseDeleteButton = NULL; // deleted by popup delete
        chooseRecordButton = NULL; // deleted by popup delete
        chooseListBox      = NULL; // deleted by popup delete

        choosePopup->hide();
        choosePopup->deleteLater();
        choosePopup = NULL;

        if (viewList.count() < 1 || (oldView < 0 && curView < 0))
            reject();
        else if (curView < 0)
        {
            curView = 0;
            curItem = -1;
            refillAll = true;
        }
    }
    else if (type == plPowerSearch)
    {
        int oldView = curView;

        choosePopup = new MythPopupBox(gContext->GetMainWindow(), "");
        choosePopup->addLabel(tr("Select Search"));

        chooseListBox = new MythListBox(choosePopup);
        chooseListBox->setScrollBar(false);
        chooseListBox->setBottomScrollBar(false);
        chooseListBox->insertItem(tr("<New Search>"));
        chooseListBox->insertStringList(viewTextList);
        if (curView < 0)
            chooseListBox->setCurrentItem(0);
        else
            chooseListBox->setCurrentItem(curView + 1);
        choosePopup->addWidget(chooseListBox);

        chooseEditButton = new MythPushButton(choosePopup);
        chooseEditButton->setText(tr("Edit"));
        choosePopup->addWidget(chooseEditButton);

        chooseDeleteButton = new MythPushButton(choosePopup);
        chooseDeleteButton->setText(tr("Delete"));
        choosePopup->addWidget(chooseDeleteButton);

        chooseRecordButton = new MythPushButton(choosePopup);
        chooseRecordButton->setText(tr("Record"));
        choosePopup->addWidget(chooseRecordButton);

        chooseDeleteButton->setEnabled(curView >= 0);
        chooseRecordButton->setEnabled(curView >= 0);

        connect(chooseListBox, SIGNAL(accepted(int)),
                this,          SLOT(setViewFromList(int)));
        connect(chooseListBox, SIGNAL(menuButtonPressed(int)),chooseEditButton,
                               SLOT(setFocus()));
        connect(chooseListBox, SIGNAL(selectionChanged()), this,
                               SLOT(chooseListBoxChanged()));
        connect(chooseEditButton, SIGNAL(clicked()), this, 
                                  SLOT(powerEdit()));
        connect(chooseDeleteButton, SIGNAL(clicked()), this,
                                    SLOT(deleteKeyword()));
        connect(chooseRecordButton, SIGNAL(clicked()), this,
                                    SLOT(addSearchRecord()));

        if (viewList.count() < 1)
            chooseEditButton->setFocus();
        else
            chooseListBox->setFocus();
        choosePopup->ExecPopup();

        chooseEditButton   = NULL; // deleted by popup delete
        chooseDeleteButton = NULL; // deleted by popup delete
        chooseRecordButton = NULL; // deleted by popup delete
        chooseListBox      = NULL; // deleted by popup delete

        choosePopup->hide();
        choosePopup->deleteLater();
        choosePopup = NULL;

        if (viewList.count() < 1 || (oldView < 0 && curView < 0))
            reject();
        else if (curView < 0)
        {
            curView = 0;
            curItem = -1;
            refillAll = true;
        }
    }
    else if (type == plTime)
    {
        choosePopup = new MythPopupBox(gContext->GetMainWindow(), "");
        choosePopup->addLabel(tr("Select Time"));

        chooseDay = new MythComboBox(false, choosePopup);

        for(int m_index = -1; m_index <= 14; m_index++)
        {
            chooseDay->insertItem(startTime.addDays(m_index)
                                  .toString(dayFormat));
            if (startTime.addDays(m_index).toString("MMdd") ==
                                searchTime.toString("MMdd"))
                chooseDay->setCurrentItem(chooseDay->count() - 1);
        }
        choosePopup->addWidget(chooseDay);

        chooseHour = new MythComboBox(false, choosePopup);

        QTime m_hr;
        for(int m_index = 0; m_index < 24; m_index++)
        {
            m_hr.setHMS(m_index, 0, 0);
            chooseHour->insertItem(m_hr.toString(hourFormat));
            if (m_hr.toString("hh") == searchTime.toString("hh"))
                chooseHour->setCurrentItem(m_index);
        }
        choosePopup->addWidget(chooseHour);

        chooseOkButton = new MythPushButton(choosePopup);
        chooseOkButton->setText(tr("OK"));
        choosePopup->addWidget(chooseOkButton);

        connect(chooseOkButton,
                SIGNAL(clicked()), this, SLOT(setViewFromTime()));

        chooseOkButton->setFocus();
        choosePopup->ExecPopup();

        chooseDay      = NULL; // deleted by popup delete
        chooseHour     = NULL; // deleted by popup delete
        chooseOkButton = NULL; // deleted by popup delete

        choosePopup->hide();
        choosePopup->deleteLater();
        choosePopup = NULL;
    }
}

void ProgLister::powerEdit()
{
    int view = chooseListBox->currentItem() - 1;
    QString text = ":::::";

    if (view >= 0)
        text = viewList[view];

    QStringList field = QStringList::split( ":", text, true);

    if (field.count() != 6)
    {
        VERBOSE(VB_IMPORTANT, QString("Error. PowerSearch %1 has %2 fields")
                .arg(text).arg(field.count()));
    }

    powerPopup = new MythPopupBox(gContext->GetMainWindow(), "");
    powerPopup->addLabel(tr("Edit Power Search Fields"));

    powerPopup->addLabel(tr("Optional title phrase:"));
    powerTitleEdit = new MythRemoteLineEdit(powerPopup);
    powerPopup->addWidget(powerTitleEdit);

    powerPopup->addLabel(tr("Optional subtitle phrase:"));
    powerSubtitleEdit = new MythRemoteLineEdit(powerPopup);
    powerPopup->addWidget(powerSubtitleEdit);

    powerPopup->addLabel(tr("Optional description phrase:"));
    powerDescEdit = new MythRemoteLineEdit(powerPopup);
    powerPopup->addWidget(powerDescEdit);

    powerCatType = new MythComboBox(false, powerPopup);
    powerCatType->insertItem(tr("(Any Program Type)"));
    typeList.clear();
    typeList << "";
    powerCatType->insertItem(tr("Movies"));
    typeList << "movie";
    powerCatType->insertItem(tr("Series"));
    typeList << "series";
    powerCatType->insertItem(tr("Show"));
    typeList << "tvshow";
    powerCatType->insertItem(tr("Sports"));
    typeList << "sports";
    powerCatType->setCurrentItem(typeList.findIndex(field[3]));
    powerPopup->addWidget(powerCatType);

    powerGenre = new MythComboBox(false, powerPopup);
    powerGenre->insertItem(tr("(Any Genre)"));
    genreList.clear();
    genreList << "";

    MSqlQuery query(MSqlQuery::InitCon());

    query.prepare("SELECT genre FROM programgenres GROUP BY genre;");
    query.exec();

    if (query.isActive() && query.size())
    {
        while (query.next())
        {
            QString category = query.value(0).toString();
            if (category <= " " || category == NULL)
                continue;
            category = QString::fromUtf8(query.value(0).toString());
            powerGenre->insertItem(category);
            genreList << category;
            if (category == field[4])
                powerGenre->setCurrentItem(powerGenre->count() - 1);
        }
    }
    powerPopup->addWidget(powerGenre);

    powerStation = new MythComboBox(false, powerPopup);
    powerStation->insertItem(tr("(Any Station)"));
    stationList.clear();
    stationList << "";

    DBChanList channels = ChannelUtil::GetChannels(0, true, "callsign");
    ChannelUtil::SortChannels(channels, channelOrdering, true);

    for (uint i = 0; i < channels.size(); i++)
    {
        QString chantext = QDeepCopy<QString>(channelFormat);
        chantext
            .replace("<num>",  channels[i].channum)
            .replace("<sign>", channels[i].callsign)
            .replace("<name>", channels[i].name);

        viewList << QString::number(channels[i].chanid);
        viewTextList << chantext;

        powerStation->insertItem(chantext);
        stationList << channels[i].callsign;
        if (channels[i].callsign == field[5])
            powerStation->setCurrentItem(powerStation->count() - 1);
    }

    powerPopup->addWidget(powerStation);

    powerOkButton = new MythPushButton(powerPopup);
    powerOkButton->setText(tr("OK"));
    powerPopup->addWidget(powerOkButton);

    connect(powerOkButton, SIGNAL(clicked()), this, 
                           SLOT(setViewFromPowerEdit()));

    powerTitleEdit->setText(field[0]);
    powerSubtitleEdit->setText(field[1]);
    powerDescEdit->setText(field[2]);

    powerTitleEdit->setFocus();
    choosePopup->accept();
    powerPopup->ExecPopup();

    powerTitleEdit    = NULL; // deleted by popup delete
    powerSubtitleEdit = NULL; // deleted by popup delete
    powerDescEdit     = NULL; // deleted by popup delete
    powerCatType      = NULL; // deleted by popup delete
    powerGenre        = NULL; // deleted by popup delete
    powerStation      = NULL; // deleted by popup delete
    powerOkButton     = NULL; // deleted by popup delete

    powerPopup->hide();
    powerPopup->deleteLater();
    powerPopup = NULL;
}

bool ProgLister::powerStringToSQL(const QString &qphrase, QString &output,
                                  MSqlBindings &bindings)
{
    int ret = 0;
    output = "";
    QString curfield;

    QStringList field = QStringList::split(":", qphrase, true);

    if (field.count() != 6)
    {
        VERBOSE(VB_IMPORTANT, QString("Error. PowerSearch %1 has %2 fields")
                .arg(qphrase).arg(field.count()));
        return ret;
    }

    if (field[0])
    {
        curfield = "%" + field[0] + "%";
        output += "program.title LIKE :POWERTITLE ";
        bindings[":POWERTITLE"] = curfield;
    }

    if (field[1])
    {
        if (output > "")
            output += "\nAND ";

        curfield = "%" + field[1] + "%";
        output += "program.subtitle LIKE :POWERSUB ";
        bindings[":POWERSUB"] = curfield;
    }

    if (field[2])
    {
        if (output > "")
            output += "\nAND ";

        curfield = "%" + field[2] + "%";
        output += "program.description LIKE :POWERDESC ";
        bindings[":POWERDESC"] = curfield;
    }

    if (field[3])
    {
        if (output > "")
            output += "\nAND ";

        output += "program.category_type = :POWERCATTYPE ";
        bindings[":POWERCATTYPE"] = field[3];
    }

    if (field[4])
    {
        if (output > "")
            output += "\nAND ";

        output += "programgenres.genre = :POWERGENRE ";
        bindings[":POWERGENRE"] = field[4];
        ret = 1;
    }

    if (field[5])
    {
        if (output > "")
            output += "\nAND ";

        output += "channel.callsign = :POWERCALLSIGN ";
        bindings[":POWERCALLSIGN"] = field[5];
    }
    return ret;
}

void ProgLister::quickRecord()
{
    ProgramInfo *pi = itemList.at(curItem);

    if (!pi)
        return;

    pi->ToggleRecord();
}

void ProgLister::select()
{
    ProgramInfo *pi = itemList.at(curItem);

    if (!pi)
        return;

    pi->EditRecording();
}

void ProgLister::edit()
{
    ProgramInfo *pi = itemList.at(curItem);

    if (!pi)
        return;

    pi->EditScheduled();
}

void ProgLister::customEdit()
{
    ProgramInfo *pi = itemList.at(curItem);

    if (!pi)
        return;

    CustomEdit *ce = new CustomEdit(gContext->GetMainWindow(),
                                    "customedit", pi);
    ce->exec();
    delete ce;
}

void ProgLister::remove()
{
    ProgramInfo *pi = itemList.at(curItem);

    if (!pi || pi->recordid <= 0)
        return;

    ScheduledRecording *record = new ScheduledRecording();
    int recid = pi->recordid;
    record->loadByID(recid);

    QString message =
        tr("Delete '%1' %2 rule?").arg(record->getRecordTitle())
                                  .arg(pi->RecTypeText());

    bool ok = MythPopupBox::showOkCancelPopup(gContext->GetMainWindow(), "",
                                              message, false);

    if (ok)
    {
        record->remove();
        ScheduledRecording::signalChange(recid);
    }
    record->deleteLater();
}

void ProgLister::upcoming()
{
    ProgramInfo *pi = itemList.at(curItem);

    if (!pi || type == plTitle)
        return;

    ProgLister *pl = new ProgLister(plTitle, pi->title, "",
                                   gContext->GetMainWindow(), "proglist");
    pl->exec();
    delete pl;
}

void ProgLister::details()
{
    ProgramInfo *pi = itemList.at(curItem);

    if (pi)
        pi->showDetails();
}

void ProgLister::fillViewList(const QString &view)
{
    viewList.clear();
    viewTextList.clear();

    if (type == plChannel) // list by channel
    {
        DBChanList channels = ChannelUtil::GetChannels(0, true,
                                                       "channum, chanid");
        ChannelUtil::SortChannels(channels, channelOrdering, true);

        for (uint i = 0; i < channels.size(); i++)
        {
            QString chantext = QDeepCopy<QString>(channelFormat);
            chantext
                .replace("<num>",  channels[i].channum)
                .replace("<sign>", channels[i].callsign)
                .replace("<name>", channels[i].name);

            viewList << QString::number(channels[i].chanid);
            viewTextList << chantext;
        }

        if (!view.isEmpty())
            curView = viewList.findIndex(view);
    }
    else if (type == plCategory) // list by category
    {
        QString startstr = startTime.toString("yyyy-MM-ddThh:mm:50");
        MSqlQuery query(MSqlQuery::InitCon()); 
        query.prepare("SELECT g1.genre, g2.genre "
                      "FROM program "
                      "JOIN programgenres g1 ON "
                      "  program.chanid = g1.chanid AND "
                      "  program.starttime = g1.starttime "
                      "LEFT JOIN programgenres g2 ON "
                      "  g1.chanid = g2.chanid AND "
                      "  g1.starttime = g2.starttime "
                      "WHERE program.endtime > :PGILSTART "
                      "GROUP BY g1.genre, g2.genre;");
        query.bindValue(":PGILSTART", startstr);
        query.exec();

        if (query.isActive() && query.size())
        {
            QString lastGenre1;

            while (query.next())
            {
                QString genre1 = query.value(0).toString().utf8();
                if (genre1 <= " ")
                    continue;

                if (genre1 != lastGenre1)
                {
                    viewList << genre1;
                    viewTextList << genre1;
                    lastGenre1 = genre1;
                }

                QString genre2 = query.value(1).toString().utf8();
                if (genre2 <= " " || genre2 == genre1)
                    continue;

                viewList << genre1 + ":/:" + genre2;
                viewTextList << "    " + genre1 + " / " + genre2;
            }

            useGenres = true;
        }
        else
        {
            query.prepare("SELECT category "
                          "FROM program "
                          "WHERE program.endtime > :PGILSTART "
                          "GROUP BY category;");
            query.bindValue(":PGILSTART", startstr);
            query.exec();

            if (query.isActive() && query.size())
            {
                while (query.next())
                {
                    QString category = query.value(0).toString();
                    if (category <= " " || category == NULL)
                        continue;
                    category = QString::fromUtf8(query.value(0).toString());
                    viewList << category;
                    viewTextList << category;
                }
            }

            useGenres = false;
        }

        if (view != "")
            curView = viewList.findIndex(view);
    }
    else if (type == plTitleSearch || type == plKeywordSearch ||
             type == plPeopleSearch || type == plPowerSearch)
    {
        MSqlQuery query(MSqlQuery::InitCon()); 
        query.prepare("SELECT phrase FROM keyword "
                      "WHERE searchtype = :SEARCHTYPE;");
        query.bindValue(":SEARCHTYPE", searchtype);
        query.exec();

        if (query.isActive() && query.size())
        {
            while (query.next())
            {
                QString phrase = query.value(0).toString();
                if (phrase <= " ")
                    continue;
                phrase = QString::fromUtf8(query.value(0).toString());
                viewList << phrase;
                viewTextList << phrase;
            }
        }
        if (view != "")
        {
            curView = viewList.findIndex(view);

            if (curView < 0)
            {
                QString qphrase = view.utf8();

                MSqlQuery query(MSqlQuery::InitCon()); 
                query.prepare("REPLACE INTO keyword (phrase, searchtype)"
                              "VALUES(:VIEW, :SEARCHTYPE );");
                query.bindValue(":VIEW", qphrase);
                query.bindValue(":SEARCHTYPE", searchtype);
                query.exec();

                viewList << qphrase;
                viewTextList << qphrase;

                curView = viewList.count() - 1;
            }
        }
        else
            curView = -1;
    }
    else if (type == plTitle)
    {
        if (view != "")
        {
            viewList << view;
            viewTextList << view;
            curView = 0;
        }
        else
            curView = -1;
    }
    else if (type == plNewListings)
    {
        viewList << "all";
        viewTextList << tr("All");

        viewList << "premieres";
        viewTextList << tr("Premieres");

        viewList << "movies";
        viewTextList << tr("Movies");

        viewList << "series";
        viewTextList << tr("Series");

        viewList << "specials";
        viewTextList << tr("Specials");
        curView = 0;
    }
    else if (type == plMovies)
    {
        viewList << ">= 0.0";
        viewTextList << tr("All");
        viewList << "= 0.0";
        viewTextList << tr("Unrated");
        viewList << ">= 1.0";
        viewTextList << "****";
        viewList << ">= 0.875 AND program.stars < 1.0";
        viewTextList << "***/";
        viewList << ">= 0.75 AND program.stars < 0.875";
        viewTextList << "***";
        viewList << ">= 0.625 AND program.stars < 0.75";
        viewTextList << "**/";
        viewList << ">= 0.5 AND program.stars < 0.625";
        viewTextList << "**";
        viewList << ">= 0.375 AND program.stars < 0.5";
        viewTextList << "*/";
        viewList << ">= 0.25 AND program.stars < 0.375";
        viewTextList << "*";
        viewList << ">= 0.125 AND program.stars < 0.25";
        viewTextList << "/";
        viewList << ">= 0.875";
        viewTextList << tr("At least ***/");
        viewList << ">= 0.75";
        viewTextList << tr("At least ***");
        viewList << ">= 0.625";
        viewTextList << tr("At least **/");
        viewList << ">= 0.5";
        viewTextList << tr("At least **");
        viewList << ">= 0.375";
        viewTextList << tr("At least */");
        viewList << ">= 0.25";
        viewTextList << tr("At least *");
        viewList << ">= 0.125";
        viewTextList << tr("At least /");
        curView = 0;
    }
    else if (type == plTime)
    {
        curView = 0;
        viewList[curView] = searchTime.toString(fullDateFormat);
        viewTextList[curView] = viewList[curView];
    }
    else if (type == plSQLSearch)
    {
        curView = 0;
        viewList << view;
        viewTextList << tr("Power Recording Rule");
    }
    else if (type == plRecordid)
    {
        curView = 0;

        MSqlQuery query(MSqlQuery::InitCon()); 
        query.prepare("SELECT title FROM record "
                      "WHERE recordid = :RECORDID");
        query.bindValue(":RECORDID", view);
        query.exec();

        if (query.isActive() && query.size())
        {
            if (query.next())
            {
                QString title = query.value(0).toString();
                title = QString::fromUtf8(query.value(0).toString());
                viewList << view;
                viewTextList << title;
            }
        }
    }
    else if (type == plStoredSearch) // stored searches
    {
        MSqlQuery query(MSqlQuery::InitCon()); 
        query.prepare("SELECT rulename FROM customexample "
                      "WHERE search > 0 ORDER BY rulename;");
        query.exec();

        if (query.isActive() && query.size())
        {
            while (query.next())
            {
                QString rulename = query.value(0).toString();
                if (rulename <= " " || rulename == NULL)
                    continue;
                rulename = QString::fromUtf8(query.value(0).toString());
                viewList << rulename;
                viewTextList << rulename;
            }
        }
        if (view != "")
            curView = viewList.findIndex(view);
    }

    if (curView >= (int)viewList.count())
        curView = viewList.count() - 1;
}

class plTitleSort
{
    public:
        plTitleSort(void) {;}

        bool operator()(const ProgramInfo *a, const ProgramInfo *b) 
        {
            if (a->sortTitle != b->sortTitle)
                    return (a->sortTitle < b->sortTitle);

            if (a->recstatus == b->recstatus)
                return a->startts < b->startts;

            if (a->recstatus == rsRecording) return true;
            if (b->recstatus == rsRecording) return false;

            if (a->recstatus == rsWillRecord) return true;
            if (b->recstatus == rsWillRecord) return false;

            return a->startts < b->startts;
        }
};

class plTimeSort
{
    public:
        plTimeSort(void) {;}

        bool operator()(const ProgramInfo *a, const ProgramInfo *b) 
        {
            if (a->startts == b->startts)
                return (a->chanid < b->chanid);

            return (a->startts < b->startts);
        }
};

void ProgLister::fillItemList(void)
{
    if (curView < 0)
         return;

    bool oneChanid = false;
    QString where = "";
    QString startstr = startTime.toString("yyyy-MM-ddThh:mm:50");
    QString qphrase = viewList[curView].utf8();

    MSqlBindings bindings;
    bindings[":PGILSTART"] = startstr;
    bindings[":PGILPHRASE"] = qphrase;
    bindings[":PGILLIKEPHRASE"] = QString("%") + qphrase + "%";

    if (type == plTitle) // per title listings
    {
        where = "WHERE channel.visible = 1 "
                "  AND program.endtime > :PGILSTART "
                "  AND program.title = :PGILPHRASE ";
    }
    else if (type == plNewListings) // what's new list
    {
        where = "LEFT JOIN oldprogram ON "
                "  oldprogram.oldtitle = program.title "
                "WHERE channel.visible = 1 "
                "  AND program.endtime > :PGILSTART "
                "  AND oldprogram.oldtitle IS NULL "
                "  AND program.manualid = 0 ";

        if (qphrase == "premieres")
        {
            where += "  AND ( ";
            where += "    ( program.originalairdate=DATE(program.starttime) ";
            where += "      AND (program.category = 'Special' ";
            where += "        OR program.programid LIKE 'EP%0001')) ";
            where += "    OR (program.category_type='movie' ";
            where += "      AND program.stars > 0.5 ";
            where += "      AND program.airdate >= YEAR(NOW()) - 2) ";
            where += "  ) ";
        }
        else if (qphrase == "movies")
        {
            where += "  AND program.category_type = 'movie' ";
        }
        else if (qphrase == "series")
        {
            where += "  AND program.category_type = 'series' ";
        }
        else if (qphrase == "specials")
        {
            where += "  AND program.category_type = 'tvshow' ";
        }
        else
        {
            where += "  AND (program.category_type <> 'movie' ";
            where += "  OR program.airdate >= YEAR(NOW()) - 3) ";
        }
    }
    else if (type == plTitleSearch) // keyword search
    {
        where = "WHERE channel.visible = 1 "
                "  AND program.endtime > :PGILSTART "
                "  AND program.title LIKE :PGILLIKEPHRASE ";
    }
    else if (type == plKeywordSearch) // keyword search
    {
        where = "WHERE channel.visible = 1 "
                "  AND program.endtime > :PGILSTART "
                "  AND (program.title LIKE :PGILLIKEPHRASE "
                "    OR program.subtitle LIKE :PGILLIKEPHRASE "
                "    OR program.description LIKE :PGILLIKEPHRASE ) ";
    }
    else if (type == plPeopleSearch) // people search
    {
        where = ", people, credits WHERE channel.visible = 1 "
                "  AND program.endtime > :PGILSTART "
                "  AND people.name LIKE :PGILPHRASE "
                "  AND credits.person = people.person "
                "  AND program.chanid = credits.chanid "
                "  AND program.starttime = credits.starttime";
    }
    else if (type == plPowerSearch) // complex search
    {
        QString powerWhere;
        MSqlBindings powerBindings;

        bool genreflag = powerStringToSQL(qphrase, powerWhere, powerBindings);

        if (powerWhere != "")
        {
            if (genreflag)
                where = QString("LEFT JOIN programgenres ON "
                        "program.chanid = programgenres.chanid AND "
                        "program.starttime = programgenres.starttime ");

            where += QString("WHERE channel.visible = 1 "
                    "  AND program.endtime > :PGILSTART "
                    "  AND ( ") + powerWhere + " ) ";
            MSqlAddMoreBindings(bindings, powerBindings);
        }
    }
    else if (type == plSQLSearch) // complex search
    {
        qphrase.remove(QRegExp("^\\s*AND\\s+", false));
        where = QString("WHERE channel.visible = 1 "
                        "  AND program.endtime > :PGILSTART "
                        "  AND ( %1 ) ").arg(qphrase);
        if (addTables > "")
            where = addTables + " " + where;
    }
    else if (type == plChannel) // list by channel
    {
        oneChanid = true;
        where = "WHERE channel.visible = 1 "
                "  AND program.endtime > :PGILSTART "
                "  AND channel.chanid = :PGILPHRASE ";
    }
    else if (type == plCategory) // list by category
    {
        if (!useGenres)
        {
            where = "WHERE channel.visible = 1 "
                    "  AND program.endtime > :PGILSTART "
                    "  AND program.category = :PGILPHRASE ";
        }
        else if (viewList[curView].find(":/:") < 0)
        {
            where = "JOIN programgenres g ON "
                    "  program.chanid = g.chanid AND "
                    "  program.starttime = g.starttime AND "
                    "  genre = :PGILPHRASE "
                    "WHERE channel.visible = 1 "
                    "  AND program.endtime > :PGILSTART ";
        }
        else
        {
            where = "JOIN programgenres g1 ON "
                    "  program.chanid = g1.chanid AND "
                    "  program.starttime = g1.starttime AND "
                    "  g1.genre = :GENRE1 "
                    "JOIN programgenres g2 ON "
                    "  program.chanid = g2.chanid AND "
                    "  program.starttime = g2.starttime AND "
                    "  g2.genre = :GENRE2 "
                    "WHERE channel.visible = 1 "
                    "  AND program.endtime > :PGILSTART ";
            bindings[":GENRE1"] = viewList[curView].section(":/:", 0, 0);
            bindings[":GENRE2"] = viewList[curView].section(":/:", 1, 1);
        }
    }
    else if (type == plMovies) // list movies
    {
        where = "WHERE channel.visible = 1 "
                "  AND program.endtime > :PGILSTART "
                "  AND program.category_type = 'movie' "
                "  AND program.stars "+qphrase+" ";
    }
    else if (type == plTime) // list by time
    {
        bindings[":PGILSEARCHTIME"] = searchTime.toString("yyyy-MM-dd hh:00:00");
        where = "WHERE channel.visible = 1 "
                "  AND program.starttime >= :PGILSEARCHTIME ";
        if (titleSort)
            where += "  AND program.starttime < DATE_ADD(:PGILSEARCHTIME, "
                     "INTERVAL '1' HOUR) ";
    }
    else if (type == plRecordid) // list by recordid
    {
        where = "JOIN recordmatch ON "
                " (program.starttime = recordmatch.starttime "
                "  AND program.chanid = recordmatch.chanid) "
                "WHERE channel.visible = 1 "
                "  AND program.endtime > :PGILSTART "
                "  AND recordmatch.recordid = :PGILPHRASE ";
    }
    else if (type == plStoredSearch) // stored search
    {
        QString fromc, wherec;
        MSqlQuery query(MSqlQuery::InitCon()); 
        query.prepare("SELECT fromclause, whereclause FROM customexample "
                      "WHERE rulename = :RULENAME;");
        query.bindValue(":RULENAME", qphrase);
        query.exec();

        if (query.isActive() && query.size())
        {
            query.next();
            fromc  = query.value(0).toString();
            wherec = query.value(1).toString();

            where = QString("WHERE channel.visible = 1 "
                            "  AND program.endtime > :PGILSTART "
                            "  AND ( %1 ) ").arg(wherec);
            if (fromc > "")
                where = fromc + " " + where;
        }
    }

    schedList.FromScheduler();
    itemList.FromProgram(where, bindings, schedList, oneChanid);

    ProgramInfo *s;
    vector<ProgramInfo *> sortedList;

    while (itemList.count())
    {
        s = itemList.take();
        if (type == plTitle)
            s->sortTitle = s->subtitle;
        else
            s->sortTitle = s->title;

        s->sortTitle.remove(QRegExp("^(The |A |An )"));
        sortedList.push_back(s);
    }

    if (type == plNewListings || titleSort)
    {
        // Prune to one per title
        sort(sortedList.begin(), sortedList.end(), plTitleSort());

        QString curtitle = "";
        vector<ProgramInfo *>::iterator i = sortedList.begin();
        while (i != sortedList.end())
        {
            ProgramInfo *p = *i;
            if (p->sortTitle != curtitle)
            {
                curtitle = p->sortTitle;
                i++;
            }
            else
            {
                delete p;
                i = sortedList.erase(i);
            }
        }
    }
    if (!titleSort)
        sort(sortedList.begin(), sortedList.end(), plTimeSort());

    if (reverseSort)
    {
        vector<ProgramInfo *>::reverse_iterator r = sortedList.rbegin();
        for (; r != sortedList.rend(); r++)
            itemList.append(*r);
    }
    else
    {
        vector<ProgramInfo *>::iterator i = sortedList.begin();
        for (; i != sortedList.end(); i++)
            itemList.append(*i);
    }

    if (curItem < 0 && itemList.count() > 0)
        curItem = 0;
    else if (curItem >= (int)itemList.count())
        curItem = itemList.count() - 1;
}

void ProgLister::updateView(QPainter *p)
{
    QRect pr = viewRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);

    LayerSet *container = NULL;

    container = theme->GetSet("view");
    if (container)
    {  
        UITextType *uitype = (UITextType *)container->GetType("curview");
        if (uitype && curView >= 0)
            uitype->SetText(viewTextList[curView]);

        container->Draw(&tmp, 4, 0);
        container->Draw(&tmp, 5, 0);
        container->Draw(&tmp, 6, 0);
        container->Draw(&tmp, 7, 0);
        container->Draw(&tmp, 8, 0);
    }

    tmp.end();
    p->drawPixmap(pr.topLeft(), pix);
}

void ProgLister::updateList(QPainter *p)
{
    QRect pr = listRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);

    QString tmptitle;

    LayerSet *container = theme->GetSet("selector");
    if (container)
    {
        UIListType *ltype = (UIListType *)container->GetType("proglist");
        if (ltype)
        {
            ltype->ResetList();
            ltype->SetActive(true);

            QStringList starMap;
            QString starstr = "";
            for (int i = 0; i <= 4; i++)
            {
                starMap << starstr;
                starMap << starstr + "/";
                starstr += "*";
            }

            int skip;
            if ((int)itemList.count() <= listsize || curItem <= listsize/2)
                skip = 0;
            else if (curItem >= (int)itemList.count() - listsize + listsize/2)
                skip = itemList.count() - listsize;
            else
                skip = curItem - listsize / 2;
            ltype->SetUpArrow(skip > 0);
            ltype->SetDownArrow(skip + listsize < (int)itemList.count());

            int i;
            for (i = 0; i < listsize; i++)
            {
                if (i + skip >= (int)itemList.count())
                    break;

                ProgramInfo *pi = itemList.at(i+skip);

                ltype->SetItemText(i, 1, pi->startts.toString(timeFormat));
                ltype->SetItemText(i, 2, pi->ChannelText(channelFormat));

                if (pi->stars > 0.0)
                    tmptitle = QString("%1 (%2, %3 )")
                                       .arg(pi->title).arg(pi->year)
                                       .arg(starMap[(int) (pi->stars * 8)]);
                else if (pi->subtitle == "")
                    tmptitle = pi->title;
                else
                {
                    if (type == plTitle)
                        tmptitle = pi->subtitle;
                    else
                        tmptitle = QString("%1 - \"%2\"")
                                           .arg(pi->title)
                                           .arg(pi->subtitle);
                }

                ltype->SetItemText(i, 3, tmptitle);
                ltype->SetItemText(i, 4, pi->RecStatusChar());

                if (pi->recstatus == rsConflict ||
                    pi->recstatus == rsOffLine)
                    ltype->EnableForcedFont(i, "conflicting");
                else if (pi->recstatus == rsRecording)
                    ltype->EnableForcedFont(i, "recording");
                else if (pi->recstatus == rsWillRecord)
                    ltype->EnableForcedFont(i, "record");

                if (i + skip == curItem)
                    ltype->SetItemCurrent(i);
            }
        }
    }

    if (itemList.count() == 0)
        container = theme->GetSet("noprograms_list");

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

void ProgLister::updateInfo(QPainter *p)
{
    QRect pr = infoRect;
    QPixmap pix(pr.size());
    pix.fill(this, pr.topLeft());
    QPainter tmp(&pix);

    LayerSet *container = NULL;
    ProgramInfo *pi = itemList.at(curItem);

    if (pi)
    {
        container = theme->GetSet("program_info");
        if (container)
        {  
            QMap<QString, QString> infoMap;
            pi->ToMap(infoMap);
            container->ClearAllText();
            container->SetText(infoMap);
        }
    }
    else
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

void ProgLister::customEvent(QCustomEvent *e)
{
    if ((MythEvent::Type)(e->type()) != MythEvent::MythEventMessage)
        return;

    MythEvent *me = (MythEvent *)e;
    QString message = me->Message();
    if (message != "SCHEDULE_CHANGE" && message != "CHOOSE_VIEW")
        return;

    if (message == "CHOOSE_VIEW")
    {
        chooseView();
        if (curView < 0)
        {
            reject();
            return;
        }
    }

    refillAll = true;

    if (!allowEvents)
        return;

    allowEvents = false;

    allowUpdates = false;
    do
    {
        refillAll = false;
        fillItemList();
    } while (refillAll);
    allowUpdates = true;
    update(fullRect);

    allowEvents = true;
}

