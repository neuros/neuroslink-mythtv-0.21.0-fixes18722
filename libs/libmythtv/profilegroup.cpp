#include "recordingprofile.h"
#include "videosource.h"
#include "profilegroup.h"
#include "libmyth/mythcontext.h"
#include "libmyth/mythdbcon.h"
#include <qsqldatabase.h>
#include <qheader.h>
#include <qcursor.h>
#include <qlayout.h>
#include <iostream>
#include <qaccel.h>

QString ProfileGroupStorage::whereClause(MSqlBindings &bindings)
{
    QString idTag(":WHEREID");
    QString query("id = " + idTag);

    bindings.insert(idTag, parent.getProfileNum());

    return query;
}

QString ProfileGroupStorage::setClause(MSqlBindings &bindings)
{
    QString idTag(":SETID");
    QString colTag(":SET" + getColumn().upper());

    QString query("id = " + idTag + ", " + 
            getColumn() + " = " + colTag);

    bindings.insert(idTag, parent.getProfileNum());
    bindings.insert(colTag, setting->getValue().utf8());

    return query;
}

void ProfileGroup::HostName::fillSelections()
{
    QStringList hostnames;
    ProfileGroup::getHostNames(&hostnames);
    for(QStringList::Iterator it = hostnames.begin();
                 it != hostnames.end(); it++)
        this->addSelection(*it);
}

ProfileGroup::ProfileGroup()
{
    // This must be first because it is needed to load/save the other settings
    addChild(id = new ID());
    addChild(is_default = new Is_default(*this));

    ConfigurationGroup* profile = new VerticalConfigurationGroup(false);
    profile->setLabel(QObject::tr("ProfileGroup"));
    profile->addChild(name = new Name(*this));
    CardInfo *cardInfo = new CardInfo(*this);
    profile->addChild(cardInfo);
    CardType::fillSelections(cardInfo);
    host = new HostName(*this);
    profile->addChild(host);
    host->fillSelections();
    addChild(profile);
};

void ProfileGroup::loadByID(int profileId) {
    id->setValue(profileId);
    load();
}

void ProfileGroup::fillSelections(SelectSetting* setting) {
    QStringList cardtypes;
    QString transcodeID;

    MSqlQuery result(MSqlQuery::InitCon());

    result.prepare("SELECT DISTINCT cardtype FROM capturecard;");

    if (result.exec() && result.isActive() && result.size() > 0)
    {
        while (result.next())
        {
            cardtypes.append(result.value(0).toString());
        }
    }

    result.prepare("SELECT name,id,hostname,is_default,cardtype "
                      "FROM profilegroups;");

    if (result.exec() && result.isActive() && result.size() > 0)
        while (result.next())
        {
            // Only show default profiles that match installed cards
            if (result.value(3).toInt())
            {
               bool match = false;
               for(QStringList::Iterator it = cardtypes.begin();
                         it != cardtypes.end(); it++)
                   if (result.value(4).toString() == *it)
                       match = true;

               if (! match)
               {
                   if (result.value(4).toString() == "TRANSCODE")
                       transcodeID = result.value(1).toString();
                   continue;
               }
            }
            QString value = QString::fromUtf8(result.value(0).toString());
            if (result.value(2).toString() != NULL &&
                result.value(2).toString() != "")
                value += QString(" (%1)").arg(result.value(2).toString());
            setting->addSelection(value, result.value(1).toString());
        }
    if (! transcodeID.isNull())
        setting->addSelection(QObject::tr("Transcoders"), transcodeID);
}

QString ProfileGroup::getName(int group)
{
    MSqlQuery result(MSqlQuery::InitCon());
    QString querystr = QString("SELECT name from profilegroups WHERE id = %1")
                            .arg(group);
    result.prepare(querystr);

    if (result.exec() && result.isActive() && result.size() > 0)
    {
        result.next();
        return QString::fromUtf8(result.value(0).toString());
    }

    return NULL;
}

bool ProfileGroup::allowedGroupName(void)
{
    MSqlQuery result(MSqlQuery::InitCon());
    QString querystr = QString("SELECT DISTINCT id FROM profilegroups WHERE "
                            "name = '%1' AND hostname = '%2';")
                            .arg(getName()).arg(host->getValue());
    result.prepare(querystr);

    if (result.exec() && result.isActive() && result.size() > 0)
        return false;
    return true;
}

void ProfileGroup::getHostNames(QStringList *hostnames)
{
    hostnames->clear();

    MSqlQuery result(MSqlQuery::InitCon());

    result.prepare("SELECT DISTINCT hostname from capturecard");

    if (result.exec() && result.isActive() && result.size() > 0)
    {
        while (result.next())
            hostnames->append(result.value(0).toString());
    }
}

void ProfileGroupEditor::open(int id) {

    ProfileGroup* profilegroup = new ProfileGroup();

    bool isdefault = false;
    bool show_profiles = true;
    bool newgroup = false;
    int profileID;
    QString pgName;

    if (id != 0)
    {
        profilegroup->loadByID(id);
        pgName = profilegroup->getName();
        if (profilegroup->isDefault())
          isdefault = true;
    }
    else
    {
        pgName = QString(QObject::tr("New Profile Group Name"));
        profilegroup->setName(pgName);
        newgroup = true;
    }

    if (! isdefault)
    {
        if (profilegroup->exec(false) == QDialog::Accepted &&
            profilegroup->allowedGroupName())
        {
            profilegroup->save();
            profileID = profilegroup->getProfileNum();
            QValueList <int> found;
            
            MSqlQuery result(MSqlQuery::InitCon());
            QString querystr = QString("SELECT name FROM recordingprofiles WHERE "
                                    "profilegroup = %1").arg(profileID);
            result.prepare(querystr);

            if (result.exec() && result.isActive() && result.size() > 0)
            {
                while (result.next())
                {
                    for (int i = 0; availProfiles[i] != ""; i++)
                      if (result.value(0).toString() == availProfiles[i])
                          found.push_back(i);
                }
            }

            for(int i = 0; availProfiles[i] != ""; i++)
            {
                bool skip = false;
                for (QValueList <int>::Iterator j = found.begin();
                       j != found.end(); j++)
                    if (i == *j)
                        skip = true;
                if (! skip)
                {
                    result.prepare("INSERT INTO recordingprofiles "
                                   "(name, profilegroup) VALUES (:NAME, :PROFID);");
                    result.bindValue(":NAME", availProfiles[i]);
                    result.bindValue(":PROFID", profileID);
                    result.exec();
                }
            }
        }
        else if (newgroup)
            show_profiles = false;
    }

    if (show_profiles)
    {
        pgName = profilegroup->getName();
        profileID = profilegroup->getProfileNum();
        RecordingProfileEditor editor(profileID, pgName);
        editor.exec();
    }
    delete profilegroup;
};

void ProfileGroupEditor::load(void) 
{
    listbox->clearSelections();
    ProfileGroup::fillSelections(listbox);
    listbox->addSelection(QObject::tr("(Create new profile group)"), "0");
}

DialogCode ProfileGroupEditor::exec(void)
{
    DialogCode ret = kDialogCodeAccepted;
    redraw = true;

    while ((QDialog::Accepted == ret) || redraw)
    {
        redraw = false;

        load();

        dialog = new ConfigurationDialogWidget(gContext->GetMainWindow(),
                                               "ProfileGroupEditor");

        connect(dialog, SIGNAL(menuButtonPressed()), this, SLOT(callDelete()));

        int   width = 0,    height = 0;
        float wmult = 0.0f, hmult  = 0.0f;
        gContext->GetScreenSettings(width, wmult, height, hmult);

        QVBoxLayout *layout = new QVBoxLayout(dialog, (int)(20 * hmult));
        layout->addWidget(listbox->configWidget(NULL, dialog));

        dialog->Show();

        ret = dialog->exec();

        dialog->deleteLater();
        dialog = NULL;

        if (ret == QDialog::Accepted)
            open(listbox->getValue().toInt());
    }

    return kDialogCodeRejected;
}

void ProfileGroupEditor::callDelete(void)
{
    int id = listbox->getValue().toInt();
    
    MSqlQuery result(MSqlQuery::InitCon());
    QString querystr = QString("SELECT id FROM profilegroups WHERE "
                            "id = %1 AND is_default = 0;").arg(id);
    result.prepare(querystr);
    
    if (result.exec() && result.isActive() && result.size() > 0)
    {
        result.next();
        QString message = QObject::tr("Delete profile group:") + 
                          QString("\n'%1'?").arg(ProfileGroup::getName(id));

        DialogCode value = MythPopupBox::Show2ButtonPopup(
            gContext->GetMainWindow(),
            "", message, 
            QObject::tr("Yes, delete group"),
            QObject::tr("No, Don't delete group"), kDialogCodeButton1);

        if (kDialogCodeButton0 == value)
        {
            querystr = QString("DELETE codecparams FROM codecparams, "
                            "recordingprofiles WHERE " 
                            "codecparams.profile = recordingprofiles.id "
                            "AND recordingprofiles.profilegroup = %1").arg(id);
            result.prepare(querystr);
            result.exec();

            querystr = QString("DELETE FROM recordingprofiles WHERE "
                            "profilegroup = %1").arg(id);
            result.prepare(querystr);
            result.exec();

            querystr = QString("DELETE FROM profilegroups WHERE id = %1;").arg(id);
            result.prepare(querystr);
            result.exec();

            redraw = true;

            if (dialog)
                dialog->done(QDialog::Rejected);
        }
    }

}

