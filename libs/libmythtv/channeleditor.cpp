#include <qsqldatabase.h>
#include "settings.h"
#include "mythcontext.h"
#include "mythdbcon.h"
#include "mythwidgets.h"
#include "channeleditor.h"

#include <qapplication.h>
#include <qlayout.h>
#include <qdialog.h>
#include <qcursor.h>

#include <mythwidgets.h>
#include <mythdialogs.h>
#include <mythwizard.h>

#include "channelsettings.h"
#include "transporteditor.h"
#include "sourceutil.h"

#include "scanwizard.h"
#include "importicons.h"

ChannelWizard::ChannelWizard(int id, int default_sourceid)
    : ConfigurationWizard()
{
    setLabel(QObject::tr("Channel Options"));

    // Must be first.
    addChild(cid = new ChannelID());
    cid->setValue(id);

    ChannelOptionsCommon *common =
        new ChannelOptionsCommon(*cid, default_sourceid);
    addChild(common);

    ChannelOptionsFilters *filters =
        new ChannelOptionsFilters(*cid);
    addChild(filters);

    int cardtypes = countCardtypes();
    bool hasDVB = cardTypesInclude("DVB");

    // add v4l options if no dvb or if dvb and some other card type
    // present
    QString cardtype = getCardtype();
    if (!hasDVB || cardtypes > 1 || id == 0) {
        ChannelOptionsV4L* v4l = new ChannelOptionsV4L(*cid);
        addChild(v4l);
    }
}

QString ChannelWizard::getCardtype() {
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT cardtype"
                  " FROM capturecard, cardinput, channel"
                  " WHERE channel.chanid = :CHID "
                  " AND channel.sourceid = cardinput.sourceid"
                  " AND cardinput.cardid = capturecard.cardid");
    query.bindValue(":CHID", cid->getValue()); 

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        query.next();
        return query.value(0).toString();
    }
    else
    {
        return "";
    }
}

bool ChannelWizard::cardTypesInclude(const QString& thecardtype) {
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT count(cardtype)"
        " FROM capturecard, cardinput, channel"
        " WHERE channel.chanid= :CHID "
        " AND channel.sourceid = cardinput.sourceid"
        " AND cardinput.cardid = capturecard.cardid"
        " AND capturecard.cardtype= :CARDTYPE ");
    query.bindValue(":CHID", cid->getValue());
    query.bindValue(":CARDTYPE", thecardtype);

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        query.next();
        int count = query.value(0).toInt();

        if (count > 0)
            return true;
        else
            return false;
    } else
        return false;
}

int ChannelWizard::countCardtypes() {
    MSqlQuery query(MSqlQuery::InitCon());
    query.prepare("SELECT count(DISTINCT cardtype)"
        " FROM capturecard, cardinput, channel"
        " WHERE channel.chanid = :CHID "
        " AND channel.sourceid = cardinput.sourceid"
        " AND cardinput.cardid = capturecard.cardid");
    query.bindValue(":CHID", cid->getValue());

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        query.next();
        return query.value(0).toInt();
    } else
        return 0;
}

int ChannelListSetting::fillSelections(void) 
{
    QString currentValue = getValue();
    uint    currentIndex = max(getValueIndex(currentValue), 0);
    clearSelections();
    addSelection(QObject::tr("(New Channel)"), "0",
                 0 == currentValue.toUInt());

    bool fAllSources = true;

    QString querystr = "SELECT channel.name,channum,chanid ";

    if ((currentSourceID.isEmpty()) ||
        (currentSourceID == "Unassigned") ||
        (currentSourceID == "All"))
    {
        querystr += ",videosource.name FROM channel "
                    "LEFT JOIN videosource ON "
                    "(channel.sourceid = videosource.sourceid) ";
        fAllSources = true;
    }
    else
    {
        querystr += QString("FROM channel WHERE sourceid='%1' ")
                           .arg(currentSourceID);
        fAllSources = false;
    }
        
    if (currentSortMode == QObject::tr("Channel Name"))
    {
        querystr += " ORDER BY channel.name";
    }
    else if (currentSortMode == QObject::tr("Channel Number"))
    {
        querystr += " ORDER BY channum + 0";
    }

    MSqlQuery query(MSqlQuery::InitCon()); 
    query.prepare(querystr);

    uint selidx = 0, idx = 1;
    if (query.exec() && query.isActive() && query.size() > 0)
    {
        for (; query.next() ; idx++) 
        {
            QString name = QString::fromUtf8(query.value(0).toString());
            QString channum = query.value(1).toString();
            QString chanid = query.value(2).toString();
            QString sourceid = "Unassigned";

            if (fAllSources && !query.value(3).toString().isNull())
            {
                sourceid = query.value(3).toString();
                if (currentSourceID == "Unassigned")
                    continue;
            }

            if (channum == "" && currentHideMode) 
                continue;

            if (name == "") 
                name = "(Unnamed : " + chanid + ")";

            if (currentSortMode == QObject::tr("Channel Name")) 
            {
                if (channum != "") 
                    name += " (" + channum + ")";
            }
            else if (currentSortMode == QObject::tr("Channel Number")) 
            {
                if (channum != "")
                    name = channum + ". " + name;
                else
                    name = "???. " + name;
            }

            if ((currentSourceID == "") && (currentSourceID != "Unassigned"))
                name += " (" + sourceid  + ")";

            bool sel = (chanid == currentValue);
            selidx = (sel) ? idx : selidx;
            addSelection(name, chanid, sel);
        }
    }

    // Make sure we select the current item, or the following one after
    // deletion, with wrap around to "(New Channel)" after deleting last item.
    setCurrentItem((!selidx && currentIndex < idx) ? currentIndex : selidx);
    return idx;
}

class SourceSetting : public ComboBoxSetting, public Storage
{
  public:
    SourceSetting() : ComboBoxSetting(this)
    {
        setLabel(QObject::tr("Video Source"));
        addSelection(QObject::tr("(All)"),"All");
    };

    void load() 
    {
        MSqlQuery query(MSqlQuery::InitCon());
        query.prepare("SELECT name, sourceid FROM videosource");

        if (query.exec() && query.isActive() && query.size() > 0)
        {
            while(query.next())
            {
                addSelection(query.value(0).toString(),
                             query.value(1).toString());
            } 
        } 
        addSelection(QObject::tr("(Unassigned)"),"Unassigned");
    }
    void save(void) { }
    void save(QString /*destination*/) { }
};

class SortMode : public ComboBoxSetting, public TransientStorage
{
  public:
    SortMode() : ComboBoxSetting(this)
    {
        setLabel(QObject::tr("Sort Mode"));
        addSelection(QObject::tr("Channel Name"));
        addSelection(QObject::tr("Channel Number"));
    };
};

class NoChanNumHide : public CheckBoxSetting, public TransientStorage
{
  public:
    NoChanNumHide() : CheckBoxSetting(this)
        { setLabel(QObject::tr("Hide channels without channel number.")); }
};

ChannelEditor::ChannelEditor() : ConfigurationDialog()
{
    setLabel(tr("Channels"));

    addChild(list = new ChannelListSetting());

    SortMode           *sort   = new SortMode();
    source                     = new SourceSetting();
    TransButtonSetting *del    = new TransButtonSetting();
    NoChanNumHide      *hide   = new NoChanNumHide();

    del->setLabel(tr("Delete Channels"));
    del->setHelpText(
        tr("Delete all channels on currently selected source[s]."));

    HorizontalConfigurationGroup *src =
        new HorizontalConfigurationGroup(false, false, true, true);
    src->addChild(source);
    src->addChild(del);

    sort->setValue(sort->getValueIndex(list->getSortMode()));
    source->setValue(max(source->getValueIndex(list->getSourceID()), 0));
    hide->setValue(list->getHideMode());

    addChild(sort);
    addChild(src);
    addChild(hide);

    buttonScan = new TransButtonSetting();
    buttonScan->setLabel(QObject::tr("Channel Scanner"));
    buttonScan->setHelpText(QObject::tr("Starts the channel scanner."));
    buttonScan->setEnabled(SourceUtil::IsAnySourceScanable());
    
    buttonImportIcon = new TransButtonSetting();
    buttonImportIcon->setLabel(QObject::tr("Icon Download"));
    buttonImportIcon->setHelpText(QObject::tr("Starts the icon downloader"));
    buttonImportIcon->setEnabled(SourceUtil::IsAnySourceScanable());

    buttonTransportEditor = new TransButtonSetting();
    buttonTransportEditor->setLabel(QObject::tr("Transport Editor"));
    buttonTransportEditor->setHelpText(
        QObject::tr("Allows you to edit the transports directly") + " " +
        QObject::tr("This is rarely required unless you are using "
                    "a satellite dish and must enter an initial "
                    "frequency to for the channel scanner to try."));

    HorizontalConfigurationGroup *h = 
        new HorizontalConfigurationGroup(false, false);
    h->addChild(buttonScan);
    h->addChild(buttonImportIcon);
    h->addChild(buttonTransportEditor);
    addChild(h);

    connect(source, SIGNAL(valueChanged(const QString&)),
            list, SLOT(setSourceID(const QString&)));
    connect(sort, SIGNAL(valueChanged(const QString&)),
            list, SLOT(setSortMode(const QString&)));
    connect(hide, SIGNAL(valueChanged(bool)),
            list, SLOT(setHideMode(bool)));
    connect(list, SIGNAL(accepted(int)),
            this, SLOT(edit(int)));
    connect(list, SIGNAL(menuButtonPressed(int)),
            this, SLOT(menu(int)));
    connect(buttonScan, SIGNAL(pressed()),
            this, SLOT(scan()));
    connect(buttonImportIcon,  SIGNAL(pressed()),
            this, SLOT(channelIconImport()));
    connect(buttonTransportEditor, SIGNAL(pressed()),
            this, SLOT(transportEditor()));
    connect(del,  SIGNAL(pressed()),
            this, SLOT(deleteChannels()));
}

void ChannelEditor::deleteChannels(void)
{
    const QString currentLabel    = source->getSelectionLabel();
    const QString currentSourceID = source->getValue();

    bool del_all = currentSourceID.isEmpty() || currentSourceID == "All";
    bool del_nul = currentSourceID == "Unassigned";

    QString chan_msg =
        (del_all) ? tr("Are you sure you would like to delete ALL channels?") :
        ((del_nul) ?
         tr("Are you sure you would like to delete all unassigned channels?") :
         tr("Are you sure you would like to delete the channels on %1?")
         .arg(currentLabel));

    DialogCode val = MythPopupBox::Show2ButtonPopup(
        gContext->GetMainWindow(), "", chan_msg,
        tr("Yes, delete the channels"),
        tr("No, don't"), kDialogCodeButton1);

    if (kDialogCodeButton0 != val)
        return;

    MSqlQuery query(MSqlQuery::InitCon());
    if (del_all)
    {
        query.prepare("TRUNCATE TABLE channel");
    }
    else if (del_nul)
    {
        query.prepare("SELECT sourceid "
                      "FROM videosource "
                      "GROUP BY sourceid");

        if (!query.exec() || !query.isActive())
        {
            MythContext::DBError("ChannelEditor Delete Channels", query);
            return;
        }

        QString tmp = "";
        while (query.next())
            tmp += "'" + query.value(0).toString() + "',";

        if (tmp.isEmpty())
        {
            query.prepare("TRUNCATE TABLE channel");
        }
        else
        {
            tmp = tmp.left(tmp.length() - 1);
            query.prepare(QString("DELETE FROM channel "
                                  "WHERE sourceid NOT IN (%1)").arg(tmp));
        }
    }
    else
    {
        query.prepare("DELETE FROM channel "
                      "WHERE sourceid = :SOURCEID");
        query.bindValue(":SOURCEID", currentSourceID);
    }

    if (!query.exec())
        MythContext::DBError("ChannelEditor Delete Channels", query);

    list->fillSelections();
}

MythDialog* ChannelEditor::dialogWidget(MythMainWindow* parent,
                                        const char* widgetName)
{
    dialog = ConfigurationDialog::dialogWidget(parent, widgetName);
    connect(dialog, SIGNAL(editButtonPressed()),   this, SLOT(edit()));
    connect(dialog, SIGNAL(deleteButtonPressed()), this, SLOT(del()));
    return dialog;
}

DialogCode ChannelEditor::exec(void)
{
    while (ConfigurationDialog::exec() == kDialogCodeAccepted)  {}
    return kDialogCodeRejected;
}

void ChannelEditor::edit()
{
    id = list->getValue().toInt();
    ChannelWizard cw(id, source->getValue().toUInt());
    cw.exec();
    
    list->fillSelections();
    list->setFocus();
}

void ChannelEditor::edit(int /*iSelected*/)
{ 
    edit();
}

void ChannelEditor::del()
{
    id = list->getValue().toInt();

    DialogCode val = MythPopupBox::Show2ButtonPopup(
        gContext->GetMainWindow(),
        "", tr("Are you sure you would like to delete this channel?"),
        tr("Yes, delete the channel"),
        tr("No, don't"), kDialogCodeButton1);

    if (kDialogCodeButton0 == val)
    {
        MSqlQuery query(MSqlQuery::InitCon());
        query.prepare("DELETE FROM channel WHERE chanid = :CHID ;");
        query.bindValue(":CHID", id);
        if (!query.exec() || !query.isActive())
            MythContext::DBError("ChannelEditor Delete Channel", query);

        list->fillSelections();
    }
}

void ChannelEditor::menu(int /*iSelected*/)
{
    id = list->getValue().toInt();
    if (id == 0)
       edit();
    else
    {
        DialogCode val = MythPopupBox::Show2ButtonPopup(
            gContext->GetMainWindow(),
            "", tr("Channel Menu"),
            tr("Edit.."), tr("Delete.."), kDialogCodeButton0);

        if (kDialogCodeButton0 == val)
            emit edit();
        else if (kDialogCodeButton1 == val)
            emit del();
        else
            list->setFocus();
    }
}

void ChannelEditor::scan(void)
{
#ifdef USING_BACKEND
    int val = source->getValue().toInt();
    uint sourceid = (val > 0) ? val : 0;
    ScanWizard *scanwizard = new ScanWizard(sourceid);
    scanwizard->exec(false, true);
    scanwizard->deleteLater();

    list->fillSelections();
    list->setFocus();
#else
    VERBOSE(VB_IMPORTANT,  "You must compile the backend "
            "to be able to scan for channels");
#endif
}

void ChannelEditor::transportEditor(void)
{
    uint sourceid = source->getValue().toUInt();

    TransportListEditor *editor = new TransportListEditor(sourceid);
    editor->exec();
    editor->deleteLater();

    list->fillSelections();
    list->setFocus();
}

void ChannelEditor::channelIconImport(void)
{
    if (list->fillSelections() == 0)
    {
        MythPopupBox::showOkPopup(gContext->GetMainWindow(), "",
                                        tr("Add some for channels first!"));
        return;
    }
    
    // Get selected channel name from database
    QString querystr = QString("SELECT channel.name FROM channel WHERE chanid='%1' ").arg(list->getValue());
    QString channelname = "";
    MSqlQuery query(MSqlQuery::InitCon()); 
    query.prepare(querystr);

    if (query.exec() && query.isActive() && query.size() > 0)
    {
        query.next();
        channelname = QString::fromUtf8(query.value(0).toString());
    }

    QStringList buttons;
    buttons.append(tr("Cancel"));
    buttons.append(tr("Download all icons.."));
    buttons.append(tr("Rescan for missing icons.."));
    if (!channelname.isEmpty())
        buttons.append(tr("Download icon for ") + channelname);

    int val = MythPopupBox::ShowButtonPopup(gContext->GetMainWindow(),
                                             "", "Channel Icon Import", buttons, kDialogCodeButton2);

    ImportIconsWizard *iconwizard;
    if (val == kDialogCodeButton0) // Cancel pressed
        return;
    else if (val == kDialogCodeButton1) // Import all icons pressed
        iconwizard = new ImportIconsWizard(false);
    else if (val == kDialogCodeButton2) // Rescan for missing pressed
        iconwizard = new ImportIconsWizard(true);
    else if (val == kDialogCodeButton3) // Import a single channel icon
        iconwizard = new ImportIconsWizard(true, channelname);
    else
        return;

    iconwizard->exec();
    iconwizard->deleteLater();

    list->fillSelections();
    list->setFocus();
}
