#ifndef SR_ITEMS_H
#define SR_ITEMS_H
#include <qsqldatabase.h>
#include "mythcontext.h"
#include "mythdbcon.h"
#include "managedlist.h"
#include "scheduledrecording.h"
#include "recordingprofile.h"
#include "playgroup.h"
#include "storagegroup.h"

class SimpleSRStorage : public SimpleDBStorage
{
  protected:
    SimpleSRStorage(Setting *_setting, ScheduledRecording *_parent,
                    QString name) :
        SimpleDBStorage(_setting, "record", name), parent(_parent)
    {
        parent->addChild(_setting);
        _setting->setName(name);
    }

        virtual QString setClause(MSqlBindings& bindings)
        {
            QString recordidTag(":SETRECORDID");
            QString colTag(":SET" + getColumn().upper());

            QString query("recordid = " + recordidTag + ", " + 
                    getColumn() + " = " + colTag);

            bindings.insert(recordidTag, parent->getRecordID());
            bindings.insert(colTag, setting->getValue().utf8());

            return query;
        }

        virtual QString whereClause(MSqlBindings& bindings)
        {
            QString recordidTag(":WHERERECORDID");
            QString query("recordid = " + recordidTag);

            bindings.insert(recordidTag, parent->getRecordID());

            return query;
        }

        ScheduledRecording *parent;
};


class SRSetting: public ManagedListSetting
{
    protected:
        SRSetting(ScheduledRecording *_parent, QString name, ManagedList* _parentList=NULL)
            : ManagedListSetting("record", name, _parentList),
              parent(_parent)
        {
            parent->addChild(this);
            setName(name);
        }

        virtual QString setClause(MSqlBindings& bindings)
        {
            QString recordidTag(":SETRECORDID");
            QString colTag(":SET" + getColumn().upper());

            QString query("recordid = " + recordidTag + ", " + 
                    getColumn() + " = " + colTag);

            bindings.insert(recordidTag, parent->getRecordID());
            bindings.insert(colTag, getValue().utf8());

            return query;
        }

        virtual QString whereClause(MSqlBindings& bindings)
        {
            QString recordidTag(":WHERERECORDID");
            
            QString query("recordid = " + recordidTag);

            bindings.insert(recordidTag, parent->getRecordID());

            return query;
        }

        ScheduledRecording *parent;
};



class SRSelectSetting : public SelectManagedListSetting
{
    protected:
        SRSelectSetting(ScheduledRecording *_parent, const QString& listName,  const QString& listText,
                        ManagedListGroup* _group, QString _column, ManagedList* _parentList=NULL)
            : SelectManagedListSetting(listName, listText, _group, "record", _column, _parentList),
              parent(_parent)
        {
            parent->addChild(this);
            setName(_column);
        }

        virtual QString setClause(MSqlBindings& bindings)
        {
            QString recordidTag(":SETRECORDID");
            QString colTag(":SET" + getColumn().upper());

            QString query("recordid = " + recordidTag + ", " + 
                    getColumn() + " = " + colTag);

            bindings.insert(recordidTag, parent->getRecordID());
            bindings.insert(colTag, getValue().utf8());

            return query;
        }

        virtual QString whereClause(MSqlBindings& bindings)
        {
            QString recordidTag(":WHERERECORDID");
            
            QString query("recordid = " + recordidTag);

            bindings.insert(recordidTag, parent->getRecordID());

            return query;
        }

        ScheduledRecording *parent;
};

class SRBoolSetting : public BoolManagedListSetting
{
    public:
        SRBoolSetting(ScheduledRecording *_parent, const QString& trueText, const QString& falseText,
                      const QString& ItemName, QString _column, ManagedListGroup* _group,
                      ManagedList* _parentList=NULL)
            : BoolManagedListSetting(trueText, falseText, ItemName, "record", _column,
                                     _group, _parentList), parent(_parent)
        {
            parent->addChild(this);
            setName(_column);
        }

        virtual QString setClause(MSqlBindings& bindings)
        {
            QString recordidTag(":SETRECORDID");
            QString colTag(":SET" + getColumn().upper());

            QString query("recordid = " + recordidTag + ", " + getColumn()
                    + " = " + colTag);

            bindings.insert(recordidTag, parent->getRecordID());
            bindings.insert(colTag, getValue().utf8());

            return query;
        }

        virtual QString whereClause(MSqlBindings& bindings)
        {
            QString recordidTag(":WHERERECORDID");
            
            QString query("recordid = " + recordidTag);

            bindings.insert(recordidTag, parent->getRecordID());

            return query;
        }

        ScheduledRecording *parent;
};


class SRBoundedIntegerSetting : public BoundedIntegerManagedListSetting
{
    public:
        SRBoundedIntegerSetting(int _min, int _max, int _bigStep, int _step, ScheduledRecording *_parent,
                                const QString& ItemName, QString _column, ManagedListGroup* _group,
                                ManagedList* _parentList=NULL,  bool _invert = false)
            : BoundedIntegerManagedListSetting(_min, _max, _bigStep, _step, ItemName, "record",
                                               _column, _group, _parentList, _invert),
              parent(_parent)

        {
            parent->addChild(this);
            setName(_column);
        }

        virtual QString setClause(MSqlBindings& bindings)
        {
            QString recordidTag(":SETRECORDID");
            QString colTag(":SET" + getColumn().upper());

            QString query("recordid = " + recordidTag + ", " + 
                    getColumn() + " = " + colTag);

            bindings.insert(recordidTag, parent->getRecordID());
            bindings.insert(colTag, getValue().utf8());

            return query;
        }

        virtual QString whereClause(MSqlBindings& bindings)
        {
            QString recordidTag(":WHERERECORDID");
            
            QString query("recordid = " + recordidTag);

            bindings.insert(recordidTag, parent->getRecordID());

            return query;
        }

        ScheduledRecording *parent;
};


class SRRecordingType : public SRSelectSetting
{
    public:
        SRRecordingType(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "typeList", QString("[ %1 ]").arg(QObject::tr("Select Recording Schedule")),
                              _group, "type", _list)
        {
            _parent->setRecTypeObj(this);
        }


        void addNormalSelections(bool haschannel, bool ismanual)
        {
            addSelection(QObject::tr("Do not record this program"), kNotRecording);

            if (haschannel)
                addSelection(QObject::tr("Record only this showing"), kSingleRecord);
            if (!ismanual)
                addSelection(QObject::tr("Record one showing of this title"), kFindOneRecord);

            if (haschannel)
                addSelection(QObject::tr("Record in this timeslot every week"), kWeekslotRecord);
            if (!ismanual)
                addSelection(QObject::tr("Record one showing of this title every week"), kFindWeeklyRecord);

            if (haschannel)
                addSelection(QObject::tr("Record in this timeslot every day"),  kTimeslotRecord);
            if (!ismanual)
                addSelection(QObject::tr("Record one showing of this title every day"), kFindDailyRecord);

            if (haschannel && !ismanual)
                addSelection(QObject::tr("Record at any time on this channel"), kChannelRecord);
            if (!ismanual)
                addSelection(QObject::tr("Record at any time on any channel"), kAllRecord);
        }

        void addOverrideSelections(void)
        {
            addSelection(QObject::tr("Record this showing with normal options"), kNotRecording);
            addSelection(QObject::tr("Record this showing with override options"), kOverrideRecord);
            addSelection(QObject::tr("Do not allow this showing to be recorded"), kDontRecord);
        }



};

class SRStartOffset : public SRBoundedIntegerSetting
{
    public:
        SRStartOffset(ScheduledRecording *_parent, ManagedListGroup* _group, ManagedList* _list)
                     : SRBoundedIntegerSetting( -480, 480, 10, 1, _parent, "startoffsetList", "startoffset",
                                                _group, _list, true)
        {
            setTemplates(QObject::tr("Start recording %1 minutes late"),
                         QObject::tr("Start recording %1 minute late"),
                         QObject::tr("Start recording on time"),
                         QObject::tr("Start recording %1 minute early"),
                         QObject::tr("Start recording %1 minutes early"));

            _parent->setStartOffsetObj(this);
        }
};



class SREndOffset : public SRBoundedIntegerSetting
{
    public:
        SREndOffset(ScheduledRecording *_parent, ManagedListGroup* _group, ManagedList* _list)
                     : SRBoundedIntegerSetting( -480, 480, 10, 1, _parent, "endoffsetList", "endoffset",
                                                _group, _list)
        {
            setTemplates(QObject::tr("End recording %1 minutes early"), 
                         QObject::tr("End recording %1 minute early"),
                         QObject::tr("End recording on time"), QObject::tr("End recording %1 minute late"),
                         QObject::tr("End recording %1 minutes late"));

            _parent->setEndOffsetObj(this);
        }
};


class SRDupIn : public SRSelectSetting
{
    public:
        SRDupIn(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "dupInList", "[ Check for duplicates in ]", _group, "dupin", _list )
        {
            addSelection(QObject::tr("Look for duplicates in current and previous recordings"), kDupsInAll);
            addSelection(QObject::tr("Look for duplicates in current recordings only"), kDupsInRecorded);
            addSelection(QObject::tr("Look for duplicates in previous recordings only"), kDupsInOldRecorded);
            addSelection(QObject::tr("Exclude unidentified episodes"), kDupsExGeneric | kDupsInAll);
            if (gContext->GetNumSetting("HaveRepeats", 0))
            {
                addSelection(QObject::tr("Exclude old episodes"), kDupsExRepeats | kDupsInAll);
                addSelection(QObject::tr("Record new episodes only"), kDupsNewEpi | kDupsInAll);
                addSelection(QObject::tr("Record new episode first showings"), kDupsFirstNew | kDupsInAll);
            }
            setValue(kDupsInAll);
            _parent->setDupInObj(this);
        }
};


class SRDupMethod: public SRSelectSetting
{
    public:
        SRDupMethod(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "dupMethodList", QObject::tr("[ Match duplicates with ]"), _group,
                              "dupmethod", _list)
        {
            addSelection(QObject::tr("Match duplicates using subtitle & description"), kDupCheckSubDesc);
            addSelection(QObject::tr("Match duplicates using subtitle then description"), kDupCheckSubThenDesc);
            addSelection(QObject::tr("Match duplicates using subtitle"), kDupCheckSub);
            addSelection(QObject::tr("Match duplicates using description"), kDupCheckDesc);
            addSelection(QObject::tr("Don't match duplicates"), kDupCheckNone);
            setValue(kDupCheckSubDesc);
            _parent->setDupMethodObj(this);
        }
};


class SRRecSearchType : public IntegerSetting, public SimpleSRStorage
{
  public:
    SRRecSearchType(ScheduledRecording *parent) :
        IntegerSetting(this), SimpleSRStorage(this, parent, "search")
    {
        setVisible(false);
    }
};


class SRProfileSelector : public SRSelectSetting
{
    public:
        SRProfileSelector(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "profileList", QObject::tr("[ Select recording Profile ]"), _group,
                              "profile", _list )
        {
            _parent->setProfileObj(this);
        }


        virtual void load() {
            fillSelections();
            SRSelectSetting::load();
        }

        virtual void fillSelections() {
            clearSelections();
            RecordingProfile::fillSelections(selectItem, 0);
        }
};

class SRSchedOptionsGroup : public ManagedListGroup
{
    Q_OBJECT

    public:
        SRSchedOptionsGroup(ScheduledRecording *_rec, ManagedList* _list, ManagedListGroup* _group, 
                            QObject* _parent);

        void setEnabled(bool isScheduled, bool multiEpisode);

    public slots:
        void itemChanged(ManagedListItem*);

    protected:

        friend class SRRootGroup;
        class SRRecPriority* recPriority;
        class SRStartOffset* startOffset;
        class SREndOffset* endOffset;
        class SRDupMethod* dupMethItem;
        class SRDupIn* dupLocItem;
        class SRInput* prefInput;
        class SRInactive* inactive;

        ScheduledRecording *schedRec;
};

class SRJobQueueGroup : public ManagedListGroup
{
    Q_OBJECT
    
    public:
        SRJobQueueGroup(ScheduledRecording *_rec, ManagedList* _list, ManagedListGroup* _group, QObject* _parent);
        
    protected:

        friend class SRRootGroup;
        class SRAutoTranscode* autoTranscode;
        class SRTranscoderSelector* transcoder;
        class SRAutoCommFlag* autoCommFlag;
        class SRAutoUserJob1* autoUserJob1;
        class SRAutoUserJob2* autoUserJob2;
        class SRAutoUserJob3* autoUserJob3;
        class SRAutoUserJob4* autoUserJob4;

        ScheduledRecording *schedRec;
};

class SRSchedInfoGroup : public ManagedListGroup
{
    Q_OBJECT
    
    public:
        SRSchedInfoGroup(ScheduledRecording *_rec, ManagedList* _list, ManagedListGroup* _group, QObject* _parent);
        
    protected:

        friend class SRRootGroup;
        ManagedListItem* detailsButton;
        ManagedListItem* previousButton;
        ManagedListItem* upcomingButton;
        ManagedListItem* upcomingRuleButton;

        ScheduledRecording *schedRec;
};


class SRStorageOptionsGroup : public ManagedListGroup
{
    Q_OBJECT

    public:
        SRStorageOptionsGroup(ScheduledRecording *_rec, ManagedList* _list, ManagedListGroup* _group, 
                              QObject* _parent);

        void setEnabled(bool isScheduled, bool multiEpisode);

    public slots:
        void itemChanged(ManagedListItem*);

    protected:

        friend class RootSRGroup;
        class SRProfileSelector* profile;
        class SRRecGroup* recGroup;
        class SRStorageGroup* storageGroup;
        class SRPlayGroup* playGroup;
        class SRAutoExpire* autoExpire;
        class SRMaxEpisodes* maxEpisodes;
        class SRMaxNewest* maxNewest;

        ScheduledRecording *schedRec;
};


class SRChannel : public ChannelSetting, public SimpleSRStorage
{
  public:
    SRChannel(ScheduledRecording *parent) :
        ChannelSetting(this),
        SimpleSRStorage(this, parent, "chanid") { setVisible(false); }
};

class SRStation : public LineEditSetting, public SimpleSRStorage
{
  public:
    SRStation(ScheduledRecording *parent) :
        LineEditSetting(this),
        SimpleSRStorage(this, parent, "station") { setVisible(false); }
};

class SRTitle : public LineEditSetting, public SimpleSRStorage
{
  public:
    SRTitle(ScheduledRecording *parent) :
        LineEditSetting(this),
        SimpleSRStorage(this, parent, "title") { setVisible(false); }
};

class SRSubtitle : public LineEditSetting, public SimpleSRStorage
{
  public:
    SRSubtitle(ScheduledRecording *parent) :
        LineEditSetting(this),
        SimpleSRStorage(this, parent, "subtitle") { setVisible(false); }
};

class SRDescription : public LineEditSetting, public SimpleSRStorage
{
  public:
    SRDescription(ScheduledRecording *parent) :
        LineEditSetting(this),
        SimpleSRStorage(this, parent, "description") { setVisible(false); }
};

class SRStartTime : public TimeSetting, public SimpleSRStorage
{
  public:
    SRStartTime(ScheduledRecording *parent) :
        TimeSetting(this),
        SimpleSRStorage(this, parent, "starttime") { setVisible(false); }
};

class SRStartDate : public DateSetting, public SimpleSRStorage
{
  public:
    SRStartDate(ScheduledRecording *parent) :
        DateSetting(this),
        SimpleSRStorage(this, parent, "startdate") { setVisible(false); }
};

class SREndTime : public TimeSetting, public SimpleSRStorage
{
  public:
    SREndTime(ScheduledRecording *parent) :
        TimeSetting(this),
        SimpleSRStorage(this, parent, "endtime") { setVisible(false); }
};

class SREndDate : public DateSetting, public SimpleSRStorage
{
  public:
    SREndDate(ScheduledRecording *parent) :
        DateSetting(this),
        SimpleSRStorage(this, parent, "enddate") { setVisible(false); }
};


class SRCategory : public LineEditSetting, public SimpleSRStorage
{
  public:
    SRCategory(ScheduledRecording *parent) :
        LineEditSetting(this),
        SimpleSRStorage(this, parent, "category") { setVisible(false); }
};

class SRSeriesid : public LineEditSetting, public SimpleSRStorage
{
  public:
    SRSeriesid(ScheduledRecording *parent) :
        LineEditSetting(this),
        SimpleSRStorage(this, parent, "seriesid") { setVisible(false); }
};

class SRProgramid : public LineEditSetting, public SimpleSRStorage
{
  public:
    SRProgramid(ScheduledRecording *parent) :
        LineEditSetting(this),
        SimpleSRStorage(this, parent, "programid") { setVisible(false); }
};

class SRFindDay : public IntegerSetting, public SimpleSRStorage
{
  public:
    SRFindDay(ScheduledRecording *parent) :
        IntegerSetting(this),
        SimpleSRStorage(this, parent, "findday") { setVisible(false); }
};

class SRFindTime : public TimeSetting, public SimpleSRStorage
{
  public:
    SRFindTime(ScheduledRecording *parent) :
        TimeSetting(this),
        SimpleSRStorage(this, parent, "findtime") { setVisible(false); }
};

class SRFindId : public IntegerSetting, public SimpleSRStorage
{
  public:
    SRFindId(ScheduledRecording *parent) :
        IntegerSetting(this),
        SimpleSRStorage(this, parent, "findid") { setVisible(false); }
};

class SRParentId : public IntegerSetting, public SimpleSRStorage
{
  public:
    SRParentId(ScheduledRecording *parent) :
        IntegerSetting(this),
        SimpleSRStorage(this, parent, "parentid") { setVisible(false); }
};

class SRAutoTranscode: public SRSelectSetting 
{
    public:
        SRAutoTranscode(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "autoTranscodeList", "[ Automatically Transcode ]", _group, 
                              "autotranscode", _list) 
        {
            addSelection(QObject::tr("Transcode new recordings"), 1);
            addSelection(QObject::tr("Do not Transcode new recordings"), 0);
            setValue(0);
            _parent->setAutoTranscodeObj(this);
        }
};

class SRTranscoderSelector: public SRSelectSetting {
    public:
        SRTranscoderSelector(ScheduledRecording *_parent, ManagedList* _list,
                             ManagedListGroup* _group)
            : SRSelectSetting(_parent, "transcoderList",
                              QObject::tr("[ Select transcoder ]"), _group,
                              "transcoder", _list )
        {
            _parent->setTranscoderObj(this);
        }


        virtual void load() {
            fillSelections();
            SRSelectSetting::load();
        }

        virtual void fillSelections() {
            clearSelections();
            RecordingProfile::fillSelections(selectItem,
                RecordingProfile::TranscoderGroup);
        }
};

class SRAutoCommFlag: public SRSelectSetting 
{
    public:
        SRAutoCommFlag(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "autoCommFlagList", "[ Automatically Commercial Flag ]", _group, 
                              "autocommflag", _list) 
        {
            addSelection(QObject::tr("Commercial Flag new recordings"), 1);
            addSelection(QObject::tr("Do not Commercial Flag new recordings"), 0);
            setValue(1);
            _parent->setAutoCommFlagObj(this);
        }
};

class SRAutoUserJob1: public SRSelectSetting 
{
    public:
        SRAutoUserJob1(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "autoUserJob1List", "[ Automatically Run User Job #1 ]", _group, 
                              "autouserjob1", _list) 
        {
            QString desc = gContext->GetSetting("UserJobDesc1");
            addSelection(QObject::tr("Run '%1'").arg(desc), 1);
            addSelection(QObject::tr("Do not run '%1' for new recordings").arg(desc), 0);
            setValue(0);
            _parent->setAutoUserJob1Obj(this);
        }
};

class SRAutoUserJob2: public SRSelectSetting 
{
    public:
        SRAutoUserJob2(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "autoUserJob2List", "[ Automatically Run User Job #2 ]", _group, 
                              "autouserjob2", _list) 
        {
            QString desc = gContext->GetSetting("UserJobDesc2");
            addSelection(QObject::tr("Run '%1'").arg(desc), 1);
            addSelection(QObject::tr("Do not run '%1' for new recordings").arg(desc), 0);
            setValue(0);
            _parent->setAutoUserJob2Obj(this);
        }
};

class SRAutoUserJob3: public SRSelectSetting 
{
    public:
        SRAutoUserJob3(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "autoUserJob3List", "[ Automatically Run User Job #3 ]", _group, 
                              "autouserjob3", _list) 
        {
            QString desc = gContext->GetSetting("UserJobDesc3");
            addSelection(QObject::tr("Run '%1'").arg(desc), 1);
            addSelection(QObject::tr("Do not run '%1' for new recordings").arg(desc), 0);
            setValue(0);
            _parent->setAutoUserJob3Obj(this);
        }
};

class SRAutoUserJob4: public SRSelectSetting 
{
    public:
        SRAutoUserJob4(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "autoUserJob4List", "[ Automatically Run User Job #4 ]", _group, 
                              "autouserjob4", _list) 
        {
            QString desc = gContext->GetSetting("UserJobDesc4");
            addSelection(QObject::tr("Run '%1'").arg(desc), 1);
            addSelection(QObject::tr("Do not run '%1' for new recordings").arg(desc), 0);
            setValue(0);
            _parent->setAutoUserJob4Obj(this);
        }
};

class SRAutoExpire: public SRBoolSetting
{
    public:
        SRAutoExpire(ScheduledRecording *_parent, ManagedListGroup* _group, ManagedList* _list)
                    : SRBoolSetting(_parent, QObject::tr("Allow auto expire"), 
                                     QObject::tr("Don't allow auto expire"),
                                    "autoExpireItem", "autoexpire", _group, _list )
        {
            _parent->setAutoExpireObj(this);
        }
};

class SRInactive: public SRBoolSetting
{
    public:
        SRInactive(ScheduledRecording *_parent, ManagedListGroup* _group, ManagedList* _list)
                    : SRBoolSetting(_parent, QObject::tr("This recording rule is inactive"), 
                                     QObject::tr("This recording rule is active"),
                                    "inactiveItem", "inactive", _group, _list )
        {
            _parent->setInactiveObj(this);
        }
};

class SRMaxNewest: public SRBoolSetting
{
    public:
        SRMaxNewest(ScheduledRecording *_parent, ManagedListGroup* _group, ManagedList* _list)
                   : SRBoolSetting(_parent, QObject::tr("Delete oldest if this would exceed the max episodes"),
                                   QObject::tr("Don't record if this would exceed the max episodes"),
                                   "maxnewestItem", "maxnewest", _group, _list )
        {
            setValue(false);
            _parent->setMaxNewestObj(this);
        }
};




class SRMaxEpisodes : public SRBoundedIntegerSetting
{
    public:
    SRMaxEpisodes(ScheduledRecording *_parent, ManagedListGroup* _group, ManagedList* _list)
                 : SRBoundedIntegerSetting( 0, 100, 5, 1, _parent, "maxepisodesList", "maxepisodes",
                                            _group, _list)
    {
        setTemplates("", "", QObject::tr("No episode limit"), 
                     QObject::tr("Keep only one episode."), QObject::tr("Keep at most %1 episodes"));
        _parent->setMaxEpisodesObj(this);
    }
};


class SRRecPriority: public SRBoundedIntegerSetting
{
    public:
        SRRecPriority(ScheduledRecording *_parent, ManagedListGroup* _group, ManagedList* _list)
                    : SRBoundedIntegerSetting( -99, 99, 5, 1, _parent, "recpriorityList", "recpriority",
                                               _group, _list)
        {
            setTemplates( QObject::tr("Reduce priority by %1"), QObject::tr("Reduce priority by %1"),
                          QObject::tr("Normal recording priority"),
                          QObject::tr("Raise priority by %1"), QObject::tr("Raise priority by %1") );
            setValue(0);
            _parent->setRecPriorityObj(this);
        }
};


class SRRecGroup: public SRSelectSetting
{
    Q_OBJECT

    public:
        SRRecGroup(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "recgroupList", QString("[ %1 ]")
                                                       .arg(QObject::tr("Select Recording Group")),
                              _group, "recgroup", _list )
        {
            setValue("Default");
            _parent->setRecGroupObj(this);
            connect(selectItem, SIGNAL(goingBack()), this, SLOT(onGoingBack()));
        }


        virtual void load() {
            fillSelections();
            SRSelectSetting::load();
        }

        virtual QString getValue(void) const {
            if (settingValue == "__NEW_GROUP__")
                return QString("Default");
            else
                return settingValue;
        }

        virtual void fillSelections()
        {
            QStringList groups;
            QStringList::Iterator it;
            QString value, dispValue;
            bool foundDefault = false;

            addButton(QString("[ %1 ]").arg(QObject::tr("Create a new recording group")), "__NEW_GROUP__");
            connect(selectItem, SIGNAL(buttonPressed(ManagedListItem*, ManagedListItem*)), this,
                                       SLOT(showNewRecGroup()));

            MSqlQuery query(MSqlQuery::InitCon());
            query.prepare("SELECT DISTINCT recgroup FROM recorded");

            if (query.exec() && query.isActive() && query.size() > 0)
                while (query.next())
                {
                    value = QString::fromUtf8(query.value(0).toString());
                    groups += value;

                    if (value == "Default")
                        foundDefault = true;
                }


            query.prepare("SELECT DISTINCT recgroup FROM record");

            if (query.exec() && query.isActive() && query.size() > 0)
                while (query.next())
                {
                    value = QString::fromUtf8(query.value(0).toString());
                    groups += value;

                    if (value == "Default")
                        foundDefault = true;
                }

            groups.sort();
            for (it = groups.begin(); it != groups.end(); ++it)
            {
                if (!foundDefault && *it > QObject::tr("Default"))
                {
                    addSelection(QObject::tr(
                                 "Include in the \"%1\" recording group")
                                 .arg(QObject::tr("Default")), "Default");
                    foundDefault = true;
                }

                if (*it == "Default")
                    dispValue = QObject::tr("Default");
                else
                    dispValue = *it;

                addSelection(QObject::tr("Include in the \"%1\" recording group")
                                         .arg(dispValue), *it);
            }

        }


    public slots:
        void showNewRecGroup();
        void onGoingBack();
};

class SRStorageGroup: public SRSelectSetting
{
    Q_OBJECT

    public:
        SRStorageGroup(ScheduledRecording *_parent, ManagedList* _list,
                       ManagedListGroup* _group)
            : SRSelectSetting(_parent, "storagegroupList", QString("[ %1 ]")
                              .arg(QObject::tr("Select Storage Group")),
                              _group, "storagegroup", _list )
        {
            setValue("Default");
            _parent->setStorageGroupObj(this);
        }


        virtual void load() {
            fillSelections();
            SRSelectSetting::load();
        }

        virtual QString getValue(void) const {
            return settingValue;
        }

        virtual void fillSelections()
        {
            QStringList groups = StorageGroup::getRecordingsGroups();
            QStringList::Iterator it;
            QString value, dispValue;
            bool foundDefault = false;

            for (it = groups.begin(); it != groups.end(); ++it)
            {
                if (*it == "Default")
                    foundDefault = true;
            }

            for (it = groups.begin(); it != groups.end(); ++it)
            {
                if (!foundDefault && *it > QObject::tr("Default"))
                {
                    addSelection(QObject::tr(
                                 "Store in the \"%1\" storage group")
                                 .arg(QObject::tr("Default")), "Default");
                    foundDefault = true;
                }

                if (*it == "Default")
                    dispValue = QObject::tr("Default");
                else if (*it == "LiveTV")
                    dispValue = QObject::tr("LiveTV");
                else
                    dispValue = *it;

                addSelection(QObject::tr("Store in the \"%1\" storage group")
                                         .arg(dispValue), *it);
            }

        }
};

class SRPlayGroup: public SRSelectSetting
{
    Q_OBJECT

    public:
        SRPlayGroup(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "playgroupList", QString("[ %1 ]")
                              .arg(QObject::tr("Select Playback Group")),
                              _group, "playgroup", _list )
        {
            setValue("Default");
            _parent->setPlayGroupObj(this);
        }

        virtual void load() {
            fillSelections();
            SRSelectSetting::load();
        }

        virtual QString getValue(void) const {
            return settingValue;
        }

        virtual void fillSelections();
};

class SRInput: public SRSelectSetting
{
    Q_OBJECT

    public:
        SRInput(ScheduledRecording *_parent, ManagedList* _list, ManagedListGroup* _group)
            : SRSelectSetting(_parent, "inputList", QString("[ %1 ]")
                              .arg(QObject::tr("Select Preferred Input")),
                              _group, "prefinput", _list )
        {
            setValue(0);
            _parent->setInputObj(this);
        }

        virtual void load() {
            fillSelections();
            SRSelectSetting::load();
        }

        virtual QString getValue(void) const {
            return settingValue;
        }

        virtual void fillSelections();
};

#endif

/* vim: set expandtab tabstop=4 shiftwidth=4: */
