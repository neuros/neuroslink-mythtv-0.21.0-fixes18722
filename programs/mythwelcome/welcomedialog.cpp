// ANSI C
#include <cstdlib>

// POSIX
#include <unistd.h>

// qt
#include <qapplication.h>

// myth
#include "mythcontext.h"
#include "mythdbcon.h"
#include "lcddevice.h"
#include "tv.h"
#include "programinfo.h"
#include "uitypes.h"
#include "compat.h"

#include "welcomedialog.h"
#include "welcomesettings.h"

#define UPDATE_STATUS_INTERVAL   30000
#define UPDATE_SCREEN_INTERVAL   15000


WelcomeDialog::WelcomeDialog(MythMainWindow *parent,
                                 QString window_name,
                                 QString theme_filename,
                                 const char* name)
                :MythThemedDialog(parent, window_name, theme_filename, name)
{
    LCD::SetupLCD();

    if (class LCD *lcd = LCD::Get())
        lcd->switchToTime();

    gContext->addListener(this);

    m_installDir = gContext->GetInstallPrefix();
    m_preRollSeconds = gContext->GetNumSetting("RecordPreRoll");
    m_idleWaitForRecordingTime =
                       gContext->GetNumSetting("idleWaitForRecordingTime", 15);

    m_timeFormat = gContext->GetSetting("TimeFormat", "h:mm AP");
    m_dateFormat = gContext->GetSetting("MythWelcomeDateFormat", "dddd\\ndd MMM yyyy");
    m_dateFormat.replace("\\n", "\n");

    // if idleTimeoutSecs is 0, the user disabled the auto-shutdown feature
    m_bWillShutdown = (gContext->GetNumSetting("idleTimeoutSecs", 0) != 0);
    m_secondsToShutdown = -1;

    wireUpTheme();
    assignFirstFocus();

    m_updateStatusTimer = new QTimer(this);
    connect(m_updateStatusTimer, SIGNAL(timeout()), this,
                                 SLOT(updateStatus()));
    m_updateStatusTimer->start(UPDATE_STATUS_INTERVAL);

    m_updateScreenTimer = new QTimer(this);
    connect(m_updateScreenTimer, SIGNAL(timeout()), this,
                                 SLOT(updateScreen()));

    m_timeTimer = new QTimer(this);
    connect(m_timeTimer, SIGNAL(timeout()), this,
                                 SLOT(updateTime()));
    m_timeTimer->start(1000);

    m_tunerList.setAutoDelete(true);
    m_scheduledList.setAutoDelete(true);

    popup = NULL;

    checkConnectionToServer();
}

void WelcomeDialog::startFrontend(void)
{
    QString startFECmd = gContext->GetSetting("MythWelcomeStartFECmd",
                         m_installDir + "/bin/mythfrontend");

    myth_system(startFECmd.ascii());
}

void WelcomeDialog::startFrontendClick(void)
{
    // this makes sure the button appears to click properly
    QTimer::singleShot(500, this, SLOT(startFrontend()));
}

DialogCode WelcomeDialog::exec(void)
{
    // mythshutdown --startup returns 0 for automatic startup
    //                                1 for manual startup 
    int state = system(m_installDir + "/bin/mythshutdown --startup");

    if (WIFEXITED(state))
        state = WEXITSTATUS(state);

    VERBOSE(VB_GENERAL, "mythshutdown --startup returned: " << state);

    bool bAutoStartFrontend = gContext->GetNumSetting("AutoStartFrontend", 1);

    if (state == 1 && bAutoStartFrontend)
        startFrontend();

    // update status now
    updateAll();

    setResult(kDialogCodeRejected);

    Show();

    do
    {
        qApp->processEvents();
        usleep(250000);
    } while (!result());

    return kDialogCodeAccepted;
}

void WelcomeDialog::customEvent(QCustomEvent *e)
{
    if ((MythEvent::Type)(e->type()) == MythEvent::MythEventMessage)
    {
        MythEvent *me = (MythEvent *) e;

        if (me->Message().left(21) == "RECORDING_LIST_CHANGE")
        {
            VERBOSE(VB_GENERAL, "MythWelcome received a RECORDING_LIST_CHANGE event");

            QMutexLocker lock(&m_RecListUpdateMuxtex);

            if (pendingRecListUpdate())
            {
                VERBOSE(VB_GENERAL, "            [deferred to pending handler]");
            }
            else
            {
                // we can't query the backend from inside a customEvent
                QTimer::singleShot(500, this, SLOT(updateRecordingList()));
                setPendingRecListUpdate(true);
            }
        }
        else if (me->Message().left(15) == "SCHEDULE_CHANGE")
        {
            VERBOSE(VB_GENERAL, "MythWelcome received a SCHEDULE_CHANGE event");

            QMutexLocker lock(&m_SchedUpdateMuxtex);

            if (pendingSchedUpdate())
            {
                VERBOSE(VB_GENERAL, "            [deferred to pending handler]");
            }
            else
            {
                QTimer::singleShot(500, this, SLOT(updateScheduledList()));
                setPendingSchedUpdate(true);
            }
        }
        else if (me->Message().left(18) == "SHUTDOWN_COUNTDOWN")
        {
            //VERBOSE(VB_GENERAL, "MythWelcome received a SHUTDOWN_COUNTDOWN event");
            QString secs = me->Message().mid(19);
            m_secondsToShutdown = secs.toInt();
            updateStatusMessage();
            updateScreen();
        }
        else if (me->Message().left(12) == "SHUTDOWN_NOW")
        {
            VERBOSE(VB_GENERAL, "MythWelcome received a SHUTDOWN_NOW event");
            if (gContext->IsFrontendOnly())
            {
                // does the user want to shutdown this frontend only machine
                // when the BE shuts down?
                if (gContext->GetNumSetting("ShutdownWithMasterBE", 0) == 1)
                {
                     VERBOSE(VB_GENERAL, "MythWelcome is shutting this computer down now");
                     QString poweroff_cmd = gContext->GetSetting("MythShutdownPowerOff", "");
                     if (poweroff_cmd != "")
                         system(poweroff_cmd.ascii());
                }
            }
        }
    }
}

void WelcomeDialog::keyPressEvent(QKeyEvent *e)
{
    bool handled = false;

    QStringList actions;
    gContext->GetMainWindow()->TranslateKeyPress("Welcome", e, actions);

    for (unsigned int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        handled = true;

        if (action == "ESCAPE")
        {
            return; // eat escape key
        }
        else if (action == "MENU")
        {
            showPopup();
        }
        else if (action == "NEXTVIEW")
        {
            accept();
        }
        else if (action == "SELECT")
        {
            activateCurrent();
        }
        else if (action == "INFO")
        {
            MythWelcomeSettings settings;
            if (kDialogCodeAccepted == settings.exec())
            {
                RemoteSendMessage("CLEAR_SETTINGS_CACHE");
                updateStatus();
                updateScreen();

                m_dateFormat = gContext->GetSetting("MythWelcomeDateFormat", "dddd\\ndd MMM yyyy");
                m_dateFormat.replace("\\n", "\n");
            }
        }
        else if (action == "SHOWSETTINGS")
        {
            MythShutdownSettings settings;
            if (kDialogCodeAccepted == settings.exec())
                RemoteSendMessage("CLEAR_SETTINGS_CACHE");
        }
        else if (action == "0")
        {
            int statusCode = system(m_installDir + "/bin/mythshutdown --status 0");
            if (WIFEXITED(statusCode))
                statusCode = WEXITSTATUS(statusCode);

            // is shutdown locked by a user
            if (statusCode & 16)
                system(m_installDir + "/bin/mythshutdown --unlock");
            else
                system(m_installDir + "/bin/mythshutdown --lock");

            updateStatusMessage();
            updateScreen();
        }
        else if (action == "STARTXTERM")
        {
            QString cmd = gContext->GetSetting("MythShutdownXTermCmd", "");
            if (cmd != "")
            {
                system(cmd);
            }
        }
        else if (action == "STARTSETUP")
        {
            system(m_installDir + "/bin/mythtv-setup");
        }
        else
            handled = false;
    }

    if (!handled)
        MythThemedDialog::keyPressEvent(e);
}

UITextType* WelcomeDialog::getTextType(QString name)
{
    UITextType* type = getUITextType(name);

    if (!type)
    {
        cout << "ERROR: Failed to find '" << name <<  "' UI element in theme file\n"
             << "Bailing out!" << endl;
        exit(0);
    }

    return type;
}

void WelcomeDialog::wireUpTheme()
{
    m_status_text = getTextType("status_text");
    m_recording_text = getTextType("recording_text");
    m_scheduled_text = getTextType("scheduled_text");
    m_time_text = getTextType("time_text");
    m_date_text = getTextType("date_text");

    m_warning_text = getTextType("conflicts_text");
    m_warning_text->hide();

    m_startfrontend_button = getUITextButtonType("startfrontend_button");
    if (m_startfrontend_button)
    {
        m_startfrontend_button->setText(tr("Start Frontend"));
        connect(m_startfrontend_button, SIGNAL(pushed()), this, 
                                      SLOT(startFrontendClick()));
    }
    else
    {
        cout << "ERROR: Failed to find 'startfrontend_button' "
             << "UI element in theme file\n"
             << "Bailing out!" << endl;
        exit(0);
    }

    buildFocusList();
}

void WelcomeDialog::closeDialog()
{
    done(kDialogCodeAccepted);
}

WelcomeDialog::~WelcomeDialog()
{
    gContext->removeListener(this);

    if (m_updateStatusTimer)
        delete m_updateStatusTimer;

    if (m_updateScreenTimer)
        delete m_updateScreenTimer;

    if (m_timeTimer)
        delete m_timeTimer;
}

void WelcomeDialog::updateTime(void)
{
    QString s = QTime::currentTime().toString(m_timeFormat);

    if (s != m_time_text->GetText())
        m_time_text->SetText(s);

    s = QDateTime::currentDateTime().toString(m_dateFormat);

    if (s != m_date_text->GetText())
        m_date_text->SetText(s);
}

void WelcomeDialog::updateStatus(void)
{
    checkConnectionToServer();

    updateStatusMessage();
}

void WelcomeDialog::updateScreen(void)
{
    QString status;

    if (!gContext->IsConnectedToMaster())
    {
        m_recording_text->SetText(tr("Cannot connect to server!"));
        m_scheduled_text->SetText(tr("Cannot connect to server!"));
        m_warning_text->hide();
    }
    else
    {
        // update recording 
        if (m_isRecording)
        {
            if (m_screenTunerNo >= m_tunerList.count())
                m_screenTunerNo = 0;

            TunerStatus *tuner = m_tunerList.at(m_screenTunerNo);

            if (tuner->isRecording)
            {
                status = QObject::tr("Tuner %1 is recording:\n")
                    .arg(tuner->id);
                status += QDeepCopy<QString>(tuner->channame);
                status += "\n" + QDeepCopy<QString>(tuner->title);
                if (!tuner->subtitle.isEmpty()) 
                    status += "\n("+QDeepCopy<QString>(tuner->subtitle)+")";
                status += "\n" + tuner->startTime.toString(m_timeFormat) + 
                          " " + tr("to") + " " + tuner->endTime.toString(m_timeFormat);
            }
            else
            {
                status = QObject::tr("Tuner %1 is not recording")
                    .arg(tuner->id);
            }

            if (m_screenTunerNo < m_tunerList.count() - 1)
                m_screenTunerNo++;
            else
                m_screenTunerNo = 0;
        }
        else
            status = tr("There are no recordings currently taking place");

        m_recording_text->SetText(status);

        // update scheduled
        if (m_scheduledList.count() > 0)
        {
            if (m_screenScheduledNo >= m_scheduledList.count())
                m_screenScheduledNo = 0;

            ProgramDetail *prog = m_scheduledList.at(m_screenScheduledNo);

            //status = QString("%1 of %2\n").arg(m_screenScheduledNo + 1)
            //                               .arg(m_scheduledList.count());
            status = prog->channel + "\n";
            status += prog->title;
            if (prog->subtitle != "")
                status += "\n(" + prog->subtitle + ")";

            QString dateFormat = gContext->GetSetting("DateFormat", "ddd dd MMM yyyy");
            status += "\n" + prog->startTime.toString(dateFormat + " (" + m_timeFormat) +
                " " + tr("to") + " " +  prog->endTime.toString(m_timeFormat + ")");

            if (m_screenScheduledNo < m_scheduledList.count() - 1)
                m_screenScheduledNo++;
            else
                m_screenScheduledNo = 0;
        }
        else
            status = tr("There are no scheduled recordings");

        m_scheduled_text->SetText(status);
    }

    // update status message
    if (m_statusList.count() == 0)
        status = tr("Please Wait ...");
    else
    {
        if (m_statusListNo >= m_statusList.count())
            m_statusListNo = 0;

        status = m_statusList[m_statusListNo];
        if (m_statusList.count() > 1)
            status += "...";
        m_status_text->SetText(status);

        if (m_statusListNo < m_statusList.count() - 1)
            m_statusListNo++;
        else
            m_statusListNo = 0;
    }

    m_updateScreenTimer->start(UPDATE_SCREEN_INTERVAL, true);
}

// taken from housekeeper.cpp
void WelcomeDialog::runMythFillDatabase()
{
    QString command;

    QString mfpath = gContext->GetSetting("MythFillDatabasePath",
                                          "mythfilldatabase");
    QString mfarg = gContext->GetSetting("MythFillDatabaseArgs", "");
    QString mflog = gContext->GetSetting("MythFillDatabaseLog",
                                         "/var/log/mythfilldatabase.log");

    if (mflog == "")
        command = QString("%1 %2").arg(mfpath).arg(mfarg);
    else
        command = QString("%1 %2 >>%3 2>&1").arg(mfpath).arg(mfarg).arg(mflog);

    command += "&";

    VERBOSE(VB_GENERAL, "Grabbing EPG data using command:\n" << command);

    myth_system(command.ascii());
}

void WelcomeDialog::updateAll(void)
{
    updateRecordingList();
    updateScheduledList();
}

bool WelcomeDialog::updateRecordingList()
{
    {
        // clear pending flag early in case something happens while
        // we're updating
        QMutexLocker lock(&m_RecListUpdateMuxtex);
        setPendingRecListUpdate(false);
    }

    m_tunerList.clear();
    m_isRecording = false;
    m_screenTunerNo = 0;

    if (!gContext->IsConnectedToMaster())
        return false;

    m_isRecording = RemoteGetRecordingStatus(&m_tunerList, true);

    return true;
}

bool WelcomeDialog::updateScheduledList()
{
    {
        // clear pending flag early in case something happens while
        // we're updating
        QMutexLocker lock(&m_SchedUpdateMuxtex);
        setPendingSchedUpdate(false);
    }

    m_scheduledList.clear();
    m_screenScheduledNo = 0;

    if (!gContext->IsConnectedToMaster())
    {
        updateStatusMessage();
        return false;
    }

    m_nextRecordingStart = QDateTime();

    ProgramList *progList = new ProgramList(true);
    ProgramInfo *progInfo;

    if (progList->FromScheduler(m_hasConflicts))
    {
        if (progList->count() > 0)
        {
            // find the earliest scheduled recording
            for (progInfo = progList->first(); progInfo; progInfo = progList->next())
            {
                if (progInfo->recstatus == rsWillRecord)
                {
                    if (m_nextRecordingStart.isNull() ||
                            m_nextRecordingStart > progInfo->recstartts)
                    {
                        m_nextRecordingStart = progInfo->recstartts;
                    }
                }
            }

            // save the details of the earliest recording(s)
            for (progInfo = progList->first(); progInfo; progInfo = progList->next())
            {
                if (progInfo->recstatus == rsWillRecord)
                {
                    if (progInfo->recstartts == m_nextRecordingStart)
                    {
                        ProgramDetail *prog = new ProgramDetail;
                        prog->channel = progInfo->channame;
                        prog->title = progInfo->title;
                        prog->subtitle = progInfo->subtitle;
                        prog->startTime = progInfo->recstartts;
                        prog->endTime = progInfo->recendts;
                        m_scheduledList.append(prog);
                    }
                }
            }
        }
    }

    delete progList;

    updateStatus();
    updateScreen();

    return true;
}

void WelcomeDialog::updateStatusMessage(void)
{
    m_statusList.clear();

    QDateTime curtime = QDateTime::currentDateTime();

    if (!m_isRecording && !m_nextRecordingStart.isNull() && 
            curtime.secsTo(m_nextRecordingStart) - 
            m_preRollSeconds < m_idleWaitForRecordingTime * 60)
    {
         m_statusList.append(tr("MythTV is about to start recording."));
    }

    if (m_isRecording)
    {
        m_statusList.append(tr("MythTV is busy recording."));
    }

    int statusCode = system(m_installDir + "/bin/mythshutdown --status 0");
    if (WIFEXITED(statusCode))
        statusCode = WEXITSTATUS(statusCode);

    if (statusCode & 1)
        m_statusList.append(tr("MythTV is busy transcoding."));
    if (statusCode & 2)
        m_statusList.append(tr("MythTV is busy flagging commercials."));
    if (statusCode & 4)
        m_statusList.append(tr("MythTV is busy grabbing EPG data."));
    if (statusCode & 16)
        m_statusList.append(tr("MythTV is locked by a user."));
    if (statusCode & 32)
        m_statusList.append(tr("MythTV has running or pending jobs."));
    if (statusCode & 64)
        m_statusList.append(tr("MythTV is in a daily wakeup/shutdown period."));
    if (statusCode & 128)
        m_statusList.append(tr("MythTV is about to start a wakeup/shutdown period."));

    if (m_statusList.count() == 0)
    {
        if (m_bWillShutdown && m_secondsToShutdown != -1)
            m_statusList.append(tr("MythTV is idle and will shutdown in %1 seconds.")
                    .arg(m_secondsToShutdown));
        else
            m_statusList.append(tr("MythTV is idle."));
    }

    if (m_hasConflicts)
        m_warning_text->show();
    else
        m_warning_text->hide();
}

bool WelcomeDialog::checkConnectionToServer(void)
{
    m_updateStatusTimer->stop();

    bool bRes = false;

    if (gContext->IsConnectedToMaster())
        bRes = true;
    else
    {
        if (gContext->ConnectToMasterServer(false))
        {
            bRes = true;
            updateAll();
        }
        else
           updateScreen();
    }

    if (bRes)
        m_updateStatusTimer->start(UPDATE_STATUS_INTERVAL);
    else
        m_updateStatusTimer->start(5000);

    return bRes;
}

void WelcomeDialog::showPopup(void)
{
    if (popup)
        return;

    popup = new MythPopupBox(gContext->GetMainWindow(), "Menu");

    QButton *topButton;
    QLabel *label = popup->addLabel(tr("Menu"), MythPopupBox::Large, false);
    label->setAlignment(Qt::AlignCenter | Qt::WordBreak);

    int statusCode = system(m_installDir + "/bin/mythshutdown --status 0");
    if (WIFEXITED(statusCode))
        statusCode = WEXITSTATUS(statusCode);

    if (statusCode & 16)
        topButton = popup->addButton(tr("Unlock Shutdown"), this,
                         SLOT(unlockShutdown()));
    else
        topButton = popup->addButton(tr("Lock Shutdown"), this,
                         SLOT(lockShutdown()));

    popup->addButton(tr("Run mythfilldatabase"), this,
                         SLOT(runEPGGrabber()));
    popup->addButton(tr("Shutdown Now"), this,
                         SLOT(shutdownNow()));
    popup->addButton(tr("Exit"), this,
                         SLOT(closeDialog()));
    popup->addButton(tr("Cancel"), popup, SLOT(reject()));

    popup->ShowPopup(this, SLOT(donePopup(int)));

    topButton->setFocus();
}

void WelcomeDialog::donePopup(int r)
{
    if (MythDialog::Rejected == r)
        cancelPopup();
}

void WelcomeDialog::cancelPopup(void)
{
  if (!popup)
      return;

  popup->hide();
  popup->deleteLater();
  popup = NULL;
  setActiveWindow();
}

void WelcomeDialog::lockShutdown(void)
{
    cancelPopup();
    system(m_installDir + "/bin/mythshutdown --lock");
    updateStatusMessage();
    updateScreen();
}

void WelcomeDialog::unlockShutdown(void)
{
    cancelPopup();
    system(m_installDir + "/bin/mythshutdown --unlock");
    updateStatusMessage();
    updateScreen();
}

void WelcomeDialog::runEPGGrabber(void)
{
    cancelPopup();
    runMythFillDatabase();
    sleep(1);
    updateStatusMessage();
    updateScreen();
}

void WelcomeDialog::shutdownNow(void)
{
    cancelPopup();

    // if this is a frontend only machine just shut down now
    if (gContext->IsFrontendOnly())
    {
        VERBOSE(VB_GENERAL, "MythWelcome is shutting this computer down now");
        QString poweroff_cmd = gContext->GetSetting("MythShutdownPowerOff", "");
        if (poweroff_cmd != "")
            system(poweroff_cmd.ascii());
        return;
    }

    // don't shutdown if we are recording
    if (m_isRecording)
    {
        MythPopupBox::showOkPopup(gContext->GetMainWindow(), "Cannot shutdown",
                tr("Cannot shutdown because MythTV is currently recording"));
        return;
    }

    QDateTime curtime = QDateTime::currentDateTime();

    // don't shutdown if we are about to start recording
    if (!m_nextRecordingStart.isNull() && 
            curtime.secsTo(m_nextRecordingStart) -
            m_preRollSeconds < m_idleWaitForRecordingTime * 60)
    {
        MythPopupBox::showOkPopup(gContext->GetMainWindow(), "Cannot shutdown",
                tr("Cannot shutdown because MythTV is about to start recording"));
        return;
    }

    // don't shutdown if we are about to start a wakeup/shutdown period
    int statusCode = system(m_installDir + "/bin/mythshutdown --status 0");
    if (WIFEXITED(statusCode))
        statusCode = WEXITSTATUS(statusCode);

    if (statusCode & 128)
    {
        MythPopupBox::showOkPopup(gContext->GetMainWindow(), "Cannot shutdown",
                tr("Cannot shutdown because MythTV is about to start "
                "a wakeup/shutdown period."));
        return;
    }

    // set the wakeup time for the next scheduled recording
    if (!m_nextRecordingStart.isNull())
    {
        QDateTime restarttime = m_nextRecordingStart.addSecs((-1) * m_preRollSeconds);

        int add = gContext->GetNumSetting("StartupSecsBeforeRecording", 240);
        if (add)
            restarttime = restarttime.addSecs((-1) * add);

        QString wakeup_timeformat = gContext->GetSetting("WakeupTimeFormat",
                                                            "yyyy-MM-ddThh:mm");
        QString setwakeup_cmd = gContext->GetSetting("SetWakeuptimeCommand",
                                                        "echo \'Wakeuptime would "
                                                        "be $time if command "
                                                        "set.\'");

        if (wakeup_timeformat == "time_t")
        {
            QString time_ts;
            setwakeup_cmd.replace("$time",
                                  time_ts.setNum(restarttime.toTime_t()));
        }
        else
            setwakeup_cmd.replace("$time",
                                  restarttime.toString(wakeup_timeformat));

        if (!setwakeup_cmd.isEmpty())
            system(setwakeup_cmd.ascii());
    }

    // run command to set wakeuptime in bios and shutdown the system
    system("sudo " + m_installDir + "/bin/mythshutdown --shutdown");
}

