#include <qapplication.h>
#include <qsqldatabase.h>
#include <qfile.h>
#include <qfileinfo.h>
#include <qmap.h>
#include <unistd.h>
#include <stdlib.h>
#include <qdir.h>
#include <qtextcodec.h>
#include <qwidget.h>
#include <fcntl.h>
#include <signal.h>
#include <cerrno>
#include <pthread.h>

#include <iostream>
using namespace std;

#include "mythconfig.h"
#include "tv.h"
#include "proglist.h"
#include "progfind.h"
#include "manualbox.h"
#include "manualschedule.h"
#include "playbackbox.h"
#include "previouslist.h"
#include "customedit.h"
#include "viewscheduled.h"
#include "programrecpriority.h"
#include "channelrecpriority.h"
#include "custompriority.h"
#include "globalsettings.h"
#include "profilegroup.h"
#include "playgroup.h"
#include "networkcontrol.h"
#include "DVDRingBuffer.h"

#include "libmyth/compat.h"  // For SIG* on MinGW
#include "exitcodes.h"
#include "programinfo.h"
#include "mythcontext.h"
#include "mythdbcon.h"
#include "dialogbox.h"
#include "guidegrid.h"
#include "mythplugin.h"
#include "remoteutil.h"
#include "xbox.h"
#include "dbcheck.h"
#include "mythmediamonitor.h"
#include "statusbox.h"
#include "lcddevice.h"
#include "langsettings.h"
#include "dbutil.h"

#include "myththemedmenu.h"
#include "myththemebase.h"
#include "mediarenderer.h"
#include "mythscreenstack.h"
#include "mythmainwindow.h"
#include "mythappearance.h"

#define NO_EXIT  0
#define QUIT     1
#define HALT     2
#define REBOOT   3

static MythThemedMenu *menu;
static MythThemeBase *themeBase;

XBox *xbox = NULL;
QString logfile = "";

MediaRenderer   *g_pUPnp       = NULL;

void startAppearWiz(void)
{
    MythScreenStack *mainStack = GetMythMainWindow()->GetMainStack();

    MythAppearance *mythappearance = new MythAppearance(mainStack, "mythappearance");

    if (mythappearance->Create())
        mainStack->AddScreen(mythappearance);
}


void startGuide(void)
{
    uint chanid = 0;
    QString channum = gContext->GetSetting("DefaultTVChannel");
    channum = (channum.isEmpty()) ? "3" : channum;
    GuideGrid::Run(chanid, channum);
}

void startFinder(void)
{
    RunProgramFind();
}

void startSearchTitle(void)
{
    ProgLister searchTitle(plTitleSearch, "", "",
                         gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchTitle.exec();
    qApp->lock();
}

void startSearchKeyword(void)
{
    ProgLister searchKeyword(plKeywordSearch, "", "",
                        gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchKeyword.exec();
    qApp->lock();
}

void startSearchPeople(void)
{
    ProgLister searchPeople(plPeopleSearch, "", "",
                         gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchPeople.exec();
    qApp->lock();
}

void startSearchPower(void)
{
    ProgLister searchPower(plPowerSearch, "", "",
                         gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchPower.exec();
    qApp->lock();
}

void startSearchStored(void)
{
    ProgLister searchStored(plStoredSearch, "", "",
                         gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchStored.exec();
    qApp->lock();
}

void startSearchChannel(void)
{
    ProgLister searchChannel(plChannel, "", "",
                             gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchChannel.exec();
    qApp->lock();
}

void startSearchCategory(void)
{
    ProgLister searchCategory(plCategory, "", "",
                            gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchCategory.exec();
    qApp->lock();
}

void startSearchMovie(void)
{
    ProgLister searchMovie(plMovies, "", "",
                           gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchMovie.exec();
    qApp->lock();
}

void startSearchNew(void)
{
    ProgLister searchNew(plNewListings, "", "",
                         gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchNew.exec();
    qApp->lock();
}

void startSearchTime(void)
{
    ProgLister searchTime(plTime, "", "",
                         gContext->GetMainWindow(), "proglist");

    qApp->unlock();
    searchTime.exec();
    qApp->lock();
}

void startManaged(void)
{
    ViewScheduled vsb(gContext->GetMainWindow(), "view scheduled");

    qApp->unlock();
    vsb.exec();
    qApp->lock();
}

void startProgramRecPriorities(void)
{
    ProgramRecPriority rsb(gContext->GetMainWindow(), "recpri scheduled");

    qApp->unlock();
    rsb.exec();
    qApp->lock();
}

void startChannelRecPriorities(void)
{
    ChannelRecPriority rch(gContext->GetMainWindow(), "recpri channels");

    qApp->unlock();
    rch.exec();
    qApp->lock();
}

void startCustomPriority(void)
{
    CustomPriority custom(gContext->GetMainWindow(), "custom priority");

    qApp->unlock();
    custom.exec();
    qApp->lock();
}

void startPlayback(void)
{
    PlaybackBox pbb(PlaybackBox::Play, gContext->GetMainWindow(), 
                    "tvplayselect");

    qApp->unlock();
    pbb.exec();
    qApp->lock();
}

void startDelete(void)
{
    PlaybackBox delbox(PlaybackBox::Delete, gContext->GetMainWindow(), 
                       "tvplayselect");
   
    qApp->unlock();
    delbox.exec();
    qApp->lock();
}

void startPrevious(void)
{
    PreviousList previous(gContext->GetMainWindow(), "previous list");

    qApp->unlock();
    previous.exec();
    qApp->lock();
}

void startCustomEdit(void)
{
    CustomEdit custom(gContext->GetMainWindow(), "custom record");

    qApp->unlock();
    custom.exec();
    qApp->lock();
}

void startManual(void)
{
    ManualBox manbox(gContext->GetMainWindow(), "manual box");

    qApp->unlock();
    manbox.exec();
    qApp->lock();
}

void startManualSchedule(void)
{
    ManualSchedule mansched(gContext->GetMainWindow(), "manual schedule");

    qApp->unlock();
    mansched.exec();
    qApp->lock();
}

void startTVInGuide(void)
{
    TV::StartTV(NULL, true);
}

void startTVNormal(void)
{
    TV::StartTV(NULL, false);
}

void showStatus(void)
{
    StatusBox statusbox(gContext->GetMainWindow(), "status box");
    if (statusbox.IsErrored())
    {
        MythPopupBox::showOkPopup(
            gContext->GetMainWindow(), QObject::tr("Theme Error"),
            QString(QObject::tr(
                        "Your theme does not contain elements required "
                        "to display the status screen.")));
    }
    else
    {
        qApp->unlock();
        statusbox.exec();
        qApp->lock();
    }
}

void TVMenuCallback(void *data, QString &selection)
{
    (void)data;
    QString sel = selection.lower();

    if (sel.left(9) == "settings ")
    {
        gContext->addCurrentLocation("Setup");
        gContext->ActivateSettingsCache(false);
    }

    if (sel == "tv_watch_live")
        startTVNormal();
    else if (sel == "tv_watch_live_epg")
        startTVInGuide();
    else if (sel == "tv_watch_recording")
        startPlayback();
    else if (sel == "tv_schedule")
        startGuide();
    else if (sel == "tv_delete")
        startDelete();
    else if (sel == "tv_manual")
        startManual();
    else if (sel == "tv_manualschedule")
        startManualSchedule();
    else if (sel == "tv_custom_record")
        startCustomEdit();
    else if (sel == "tv_fix_conflicts")
        startManaged();
    else if (sel == "tv_set_recpriorities")
        startProgramRecPriorities();
    else if (sel == "tv_progfind")
        startFinder();
    else if (sel == "tv_search_title")
        startSearchTitle();
    else if (sel == "tv_search_keyword")
        startSearchKeyword();
    else if (sel == "tv_search_people")
        startSearchPeople();
    else if (sel == "tv_search_power")
        startSearchPower();
    else if (sel == "tv_search_stored")
        startSearchStored();
    else if (sel == "tv_search_channel")
        startSearchChannel();
    else if (sel == "tv_search_category")
        startSearchCategory();
    else if (sel == "tv_search_movie")
        startSearchMovie();
    else if (sel == "tv_search_new")
        startSearchNew();
    else if (sel == "tv_search_time")
        startSearchTime();
    else if (sel == "tv_previous")
        startPrevious();
    else if (sel == "settings appearance") 
    {
        AppearanceSettings *settings = new AppearanceSettings();
        DialogCode res = settings->exec();
        delete settings;

        if (kDialogCodeRejected != res)
        {
            qApp->processEvents();
            GetMythMainWindow()->JumpTo("Reload Theme");
        }
    } 

    else if (sel == "screensetupwizard")
    {
       startAppearWiz();
    }

    else if (sel == "settings recording") 
    {
        ProfileGroupEditor editor;
        editor.exec();
    } 
    else if (sel == "settings playgroup") 
    {
        PlayGroupEditor editor;
        editor.exec();
    } 
    else if (sel == "settings general") 
    {
        GeneralSettings settings;
        settings.exec();
    } 
    else if (sel == "settings maingeneral") 
    {
        MainGeneralSettings mainsettings;
        mainsettings.exec();
        menu->ReloadExitKey();
        QStringList strlist = QString("REFRESH_BACKEND");
        gContext->SendReceiveStringList(strlist);
    } 
    else if (sel == "settings playback") 
    {
        PlaybackSettings settings;
        settings.exec();
    } 
    else if (sel == "settings osd") 
    {
        OSDSettings settings;
        settings.exec();
    } 
    else if (sel == "settings epg") 
    {
        EPGSettings settings;
        settings.exec();
    } 
    else if (sel == "settings generalrecpriorities") 
    {
        GeneralRecPrioritiesSettings settings;
        settings.exec();
    } 
    else if (sel == "settings channelrecpriorities") 
    {
        startChannelRecPriorities();
    }
    else if (sel == "settings custompriority") 
    {
        startCustomPriority();
    }
    else if (xbox && sel == "settings xboxsettings")
    {
        XboxSettings settings;
        settings.exec();

        xbox->GetSettings();
    }
    else if (sel == "tv_status")
        showStatus();

    if (sel.left(9) == "settings ")
    {
        gContext->removeCurrentLocation();

        gContext->ActivateSettingsCache(true);
        RemoteSendMessage("CLEAR_SETTINGS_CACHE");

        if (sel == "settings general" ||
            sel == "settings generalrecpriorities")
            ScheduledRecording::signalChange(0);
    }
}

int handleExit(void)
{
    if (gContext->GetNumSetting("NoPromptOnExit", 1) == 0)
        return QUIT;

    // first of all find out, if this is a frontend only host...
    bool frontendOnly = gContext->IsFrontendOnly();

    // how do you want to quit today?
    int  exitMenuStyle = gContext->GetNumSetting("OverrideExitMenu", 0);

    QString title = QObject::tr("Do you really want to exit MythTV?");

    DialogBox *dlg = new DialogBox(gContext->GetMainWindow(), title);

    dlg->AddButton(QObject::tr("No"));
    DialogCode result = kDialogCodeRejected;

    int ret = NO_EXIT;
    switch (exitMenuStyle)
    {
        case 0:
            dlg->AddButton(QObject::tr("Yes, Exit now"));
            if (frontendOnly)
                dlg->AddButton(QObject::tr("Yes, Exit and Shutdown"));
            result = dlg->exec();
            switch (result)
            {
                case kDialogCodeButton0: ret = NO_EXIT; break;
                case kDialogCodeButton1: ret = QUIT;    break;
                case kDialogCodeButton2: ret = HALT;    break;
                default:                 ret = NO_EXIT; break;
            }
            break;
        case 1:
            dlg->AddButton(QObject::tr("Yes, Exit now"));
            result = dlg->exec();
            switch (result)
            {
                case kDialogCodeButton0: ret = NO_EXIT; break;
                case kDialogCodeButton1: ret = QUIT;    break;
                default:                 ret = NO_EXIT; break;
            }
            break;
        case 2:
            dlg->AddButton(QObject::tr("Yes, Exit now"));
            dlg->AddButton(QObject::tr("Yes, Exit and Shutdown"));
            result = dlg->exec();
            switch (result)
            {
                case kDialogCodeButton0: ret = NO_EXIT; break;
                case kDialogCodeButton1: ret = QUIT;    break;
                case kDialogCodeButton2: ret = HALT;    break;
                default:                 ret = NO_EXIT; break;
            }
            break;
        case 3:
            dlg->AddButton(QObject::tr("Yes, Exit now"));
            dlg->AddButton(QObject::tr("Yes, Exit and Reboot"));
            dlg->AddButton(QObject::tr("Yes, Exit and Shutdown"));
            result = dlg->exec();
            switch (result)
            {
                case kDialogCodeButton0: ret = NO_EXIT; break;
                case kDialogCodeButton1: ret = QUIT;    break;
                case kDialogCodeButton2: ret = REBOOT;  break;
                case kDialogCodeButton3: ret = HALT;    break;
                default:                 ret = NO_EXIT; break;
            }
            break;
        case 4:
            dlg->AddButton(QObject::tr("Yes, Exit and Shutdown"));
            result = dlg->exec();
            switch (result)
            {
                case kDialogCodeButton0: ret = NO_EXIT; break;
                case kDialogCodeButton1: ret = HALT;    break;
                default:                 ret = NO_EXIT; break;
            }
            break;
        case 5:
            dlg->AddButton(QObject::tr("Yes, Exit and Reboot"));
            result = dlg->exec();
            switch (result)
            {
                case kDialogCodeButton0: ret = NO_EXIT; break;
                case kDialogCodeButton1: ret = REBOOT;  break;
                default:                 ret = NO_EXIT; break;
            }
            break;
        case 6:
            dlg->AddButton(QObject::tr("Yes, Exit and Reboot"));
            dlg->AddButton(QObject::tr("Yes, Exit and Shutdown"));
            result = dlg->exec();
            switch (result)
            {
                case kDialogCodeButton0: ret = NO_EXIT; break;
                case kDialogCodeButton1: ret = REBOOT;  break;
                case kDialogCodeButton2: ret = HALT;    break;
                default:                 ret = NO_EXIT; break;
            }
            break;
    }

    dlg->deleteLater();
    dlg = NULL;

    return ret;
}

void haltnow()
{
    QString halt_cmd = gContext->GetSetting("HaltCommand", 
                                            "sudo /sbin/halt -p");
    if (!halt_cmd.isEmpty())
        system(halt_cmd.ascii());
}

void rebootnow() 
{ 
    QString reboot_cmd = gContext->GetSetting("RebootCommand", 
                                            "sudo /sbin/reboot"); 
    if (!reboot_cmd.isEmpty()) 
        system(reboot_cmd.ascii()); 
}

bool RunMenu(QString themedir)
{
    menu = new MythThemedMenu(themedir.ascii(), "mainmenu.xml", 
                              GetMythMainWindow()->GetMainStack(), "mainmenu");
    menu->setCallback(TVMenuCallback, gContext);
   
    if (menu->foundTheme())
    {
        GetMythMainWindow()->GetMainStack()->AddScreen(menu);
        return true;
    }

    cerr << "Couldn't find theme " << themedir << endl;
    return false;
}   

// If any settings are missing from the database, this will write
// the default values
void WriteDefaults() 
{
    PlaybackSettings ps;
    ps.load();
    ps.save();
    OSDSettings os;
    os.load();
    os.save();
    GeneralSettings gs;
    gs.load();
    gs.save();
    EPGSettings es;
    es.load();
    es.save();
    AppearanceSettings as;
    as.load();
    as.save();
    MainGeneralSettings mgs;
    mgs.load();
    mgs.save();
    GeneralRecPrioritiesSettings grs;
    grs.load();
    grs.save();
}

QString RandTheme(QString &themename)
{
    QDir themes(gContext->GetThemesParentDir());
    themes.setFilter(QDir::Dirs);

    const QFileInfoList *fil = themes.entryInfoList(QDir::Dirs);

    QFileInfoListIterator it( *fil);
    QFileInfo *theme;
    QStringList themelist;

    srand(time(NULL));

    for ( ; it.current() !=0; ++it)
    {
        theme = it.current();
        if (theme->fileName() == "." || theme->fileName() =="..")
            continue;

        QFileInfo xml(theme->absFilePath() + "/theme.xml");

        if (!xml.exists())
            continue;

        // We don't want the same one as last time.
        if (theme->fileName() != themename)
            themelist.append(theme->fileName());
    }

    if (themelist.size()) 
        themename = themelist[rand() % themelist.size()];

    gContext->SaveSetting("Theme", themename);

    return themename;
}

int internal_play_media(const QString &mrl, const QString &plot, 
                        const QString &title, const QString &director, 
                        int lenMins, const QString &year) 
{
    int res = -1; 
  
    QFile checkFile(mrl);
    if (!checkFile.exists() && !mrl.startsWith("dvd:"))
    {
        QString errorText = QObject::tr("Failed to open \n '%1' in %2 \n"
                                        "Check if the video exists")
                                        .arg(mrl.section("/", -1))
                                        .arg(mrl.section("/", 0, -2));
        MythPopupBox::showOkPopup(gContext->GetMainWindow(),
                                    "Open Failed",
                                    errorText);
        return res;
    }

    ProgramInfo *pginfo = new ProgramInfo();
    pginfo->recstartts = QDateTime::currentDateTime()
            .addSecs((0 - (lenMins + 1)) * 60 );
    pginfo->recendts = QDateTime::currentDateTime().addSecs(-60);
    pginfo->startts.setDate(QDate::fromString(QString("%1-01-01").arg(year),
                                              Qt::ISODate));
    pginfo->lenMins = lenMins;
    pginfo->isVideo = true;
    pginfo->pathname = mrl;

    QDir d(mrl + "/VIDEO_TS");
    if (mrl.findRev(".iso", -1, false) == (int)mrl.length() - 4 ||
        mrl.findRev(".img", -1, false) == (int)mrl.length() - 4 ||
        d.exists())
    {
        pginfo->pathname = QString("dvd:%1").arg(mrl);
    }
    
    pginfo->description = plot;
    
    if (director.length())
        pginfo->subtitle = QString( "%1: %2" ).arg(QObject::tr("Directed By")).arg(director);
    
    pginfo->title = title;

    if (pginfo->pathname.startsWith("dvd:"))
    {
        bool allowdvdbookmark = gContext->GetNumSetting("EnableDVDBookmark", 0);
        pginfo->setIgnoreBookmark(!allowdvdbookmark);
        if (allowdvdbookmark && 
            gContext->GetNumSetting("DVDBookmarkPrompt", 0))
        {
            RingBuffer *tmprbuf = new RingBuffer(pginfo->pathname, false);
            QString name;
            QString serialid;
            if (tmprbuf->isDVD() &&
                 tmprbuf->DVD()->GetNameAndSerialNum(name, serialid))
            {
                QStringList fields = pginfo->GetDVDBookmark(serialid, false);
                if (!fields.empty())
                {
                    QStringList::Iterator it = fields.begin();
                    long long pos = (long long)(atoi((*++it).ascii()) & 0xffffffffLL);
                    if (pos > 0)
                    {
                        QString msg = QObject::tr("DVD contains a bookmark");
                        QString btn0msg = QObject::tr("Play from bookmark");
                        QString btn1msg = QObject::tr("Play from beginning");
        
                        DialogCode ret = MythPopupBox::Show2ButtonPopup(
                            gContext->GetMainWindow(),
                            "", msg,
                            btn0msg,
                            btn1msg,
                            kDialogCodeButton0);
                        if (kDialogCodeButton1 == ret)
                            pginfo->setIgnoreBookmark(true);
                        else if (kDialogCodeRejected == ret)
                        {
                            delete tmprbuf;
                            delete pginfo;
                            return res;
                        }
                    }
                }
            }
            delete tmprbuf;
        }
    }

    TV::StartTV(pginfo);
    
    res = 0;
    
    sleep(1);
    delete pginfo;
    
    return res;
}

void gotoMainMenu(void)
{
    // If we got to this callback, we're back on the menu.  So, send a CTRL-L
    // to cause the menu to reload
    QKeyEvent *event =
        new QKeyEvent(QEvent::KeyPress, Qt::Key_L, 0, Qt::ControlButton);
    QApplication::postEvent((QObject*)(gContext->GetMainWindow()), event);
}

void reloadTheme(void)
{
    LanguageSettings::reload();

    gContext->LoadQtConfig();
    gContext->GetMainWindow()->Init();
    gContext->UpdateImageCache();

    themeBase->Reload();
    menu->ReloadTheme();

    if (!menu->foundTheme())
        exit(FRONTEND_BUGGY_EXIT_NO_THEME);

    LCD::SetupLCD();
    if (LCD *lcd = LCD::Get()) 
    {
        lcd->setupLEDs(RemoteGetRecordingMask);
        lcd->resetServer();
    }
}

void InitJumpPoints(void)
{
    REG_JUMP("Reload Theme", "", "", reloadTheme);
    REG_JUMP("Main Menu", "", "", gotoMainMenu);
    REG_JUMPLOC("Program Guide", "", "", startGuide, "GUIDE");
    REG_JUMPLOC("Program Finder", "", "", startFinder, "FINDER");
    //REG_JUMP("Search Listings", "", "", startSearch);
    REG_JUMPLOC("Manage Recordings / Fix Conflicts", "", "", 
                startManaged, "VIEWSCHEDULED");
    REG_JUMP("Program Recording Priorities", "", "", startProgramRecPriorities);
    REG_JUMP("Channel Recording Priorities", "", "", startChannelRecPriorities);
    REG_JUMP("TV Recording Playback", "", "", startPlayback);
    REG_JUMP("TV Recording Deletion", "", "", startDelete);
    REG_JUMP("Live TV", "", "", startTVNormal);
    REG_JUMP("Live TV In Guide", "", "", startTVInGuide);
    REG_JUMP("Manual Record Scheduling", "", "", startManual);
    REG_JUMP("Status Screen", "", "", showStatus);
    REG_JUMP("Previously Recorded", "", "", startPrevious);

    REG_KEY("qt", "DELETE", "Delete", "D");
    REG_KEY("qt", "EDIT", "Edit", "E");

    TV::InitKeys();

    TV::SetFuncPtr("playbackbox", (void *)PlaybackBox::RunPlaybackBox);
    TV::SetFuncPtr("viewscheduled", (void *)ViewScheduled::RunViewScheduled);
}


void signal_USR1_handler(int){
      VERBOSE(VB_GENERAL, "SIG USR1 received, reloading theme");	
      RemoteSendMessage("CLEAR_SETTINGS_CACHE");
      gContext->ActivateSettingsCache(false);
      qApp->processEvents();
      GetMythMainWindow()->JumpTo("Reload Theme");
      gContext->removeCurrentLocation();
      gContext->ActivateSettingsCache(true);
}

int internal_media_init() 
{
    REG_MEDIAPLAYER("Internal", "MythTV's native media player.", 
                    internal_play_media);
    return 0;
}

static void *run_priv_thread(void *data)
{
    VERBOSE(VB_PLAYBACK, QString("user: %1 effective user: %2 run_priv_thread")
                            .arg(getuid()).arg(geteuid()));

    (void)data;
    while (true) 
    {
        gContext->waitPrivRequest();
        
        for (MythPrivRequest req = gContext->popPrivRequest(); 
             true; req = gContext->popPrivRequest()) 
        {
            bool done = false;
            switch (req.getType()) 
            {
            case MythPrivRequest::MythRealtime:
                if (gContext->GetNumSetting("RealtimePriority", 1))
                {
                    pthread_t *target_thread = (pthread_t *)(req.getData());
                    // Raise the given thread to realtime priority
                    struct sched_param sp = {1};
                    if (target_thread)
                    {
                        int status = pthread_setschedparam(
                            *target_thread, SCHED_FIFO, &sp);
                        if (status) 
                        {
                            // perror("pthread_setschedparam");
                            VERBOSE(VB_GENERAL, "Realtime priority would require SUID as root.");
                        }
                        else
                            VERBOSE(VB_GENERAL, "Using realtime priority.");
                    }
                    else
                    {
                        VERBOSE(VB_IMPORTANT, "Unexpected NULL thread ptr "
                                "for MythPrivRequest::MythRealtime");
                    }
                }
                else
                    VERBOSE(VB_GENERAL, "The realtime priority setting is not enabled.");
                break;
            case MythPrivRequest::MythExit:
                pthread_exit(NULL);
                break;
            case MythPrivRequest::PrivEnd:
                done = true; // queue is empty
                break;
            }
            if (done)
                break; // from processing the current queue
        }
    }
    return NULL; // will never happen
}

void CleanupMyOldInUsePrograms(void)
{
    MSqlQuery query(MSqlQuery::InitCon());

    query.prepare("DELETE FROM inuseprograms "
                  "WHERE hostname = :HOSTNAME and recusage = 'player' ;");
    query.bindValue(":HOSTNAME", gContext->GetHostName());
    query.exec();
}

void PrintHelp(void)
{

    cerr << "Valid options are: " << endl <<
#ifdef USING_X11
            "-display X-server              Create GUI on X-server, not localhost\n" <<
#endif
            "-geometry or --geometry WxH    Override window size settings\n" <<
            "-geometry WxH+X+Y              Override window size and position\n" <<
            "-l or --logfile filename       Writes STDERR and STDOUT messages to filename" << endl <<
            "-r or --reset                  Resets frontend appearance settings and language" << endl <<
            "-w or --windowed               Run in windowed mode" << endl <<
            "-nw or --no-windowed           Run in non-windowed mode " << endl <<
            "-O or " << endl <<
            "  --override-setting KEY=VALUE Force the setting named 'KEY' to value 'VALUE'" << endl <<
            "                               This option may be repeated multiple times" << endl <<
            "-G or " << endl <<
            "  --get-setting KEY[,KEY2,etc] Returns the current database setting for 'KEY'" << endl <<
            "                               Use a comma seperated list to return multiple values" << endl <<
            "-v or --verbose debug-level    Use '-v help' for level info" << endl <<
            "-p or --prompt                 Always prompt for Mythbackend selection." << endl <<
            "-d or --disable-autodiscovery  Never prompt for Mythbackend selection." << endl <<

            "-u or --upgrade-schema         Allow mythfrontend to upgrade the database schema" << endl <<
            "--version                      Version information" << endl <<
            "<plugin>                       Initialize and run this plugin" << endl <<
            endl <<
            "Environment Variables:" << endl <<
            "$MYTHTVDIR                     Set the installation prefix" << endl <<
            "$MYTHCONFDIR                   Set the config dir (instead of ~/.mythtv)" << endl;

}

int log_rotate(int report_error)
{
    int new_logfd = open(logfile, O_WRONLY|O_CREAT|O_APPEND, 0664);

    if (new_logfd < 0) {
        /* If we can't open the new logfile, send data to /dev/null */
        if (report_error) {
            cerr << "cannot open logfile " << logfile << endl;
            return -1;
        }

        new_logfd = open("/dev/null", O_WRONLY);

        if (new_logfd < 0) {
            /* There's not much we can do, so punt. */
            return -1;
        }
    }

    while (dup2(new_logfd, 1) < 0 && errno == EINTR);
    while (dup2(new_logfd, 2) < 0 && errno == EINTR);
    while (close(new_logfd) < 0   && errno == EINTR);

    return 0;
}

void log_rotate_handler(int)
{
    log_rotate(0);
}

void CheckSchemaVersion(bool upgradeAllowed)
{
    gContext->ActivateSettingsCache(false);
    int versionCheck = CompareTVDatabaseSchemaVersion();
    bool expertMode = (gContext->GetNumSetting("DBSchemaAutoUpgrade") == -1);

    if (versionCheck < 0 && !upgradeAllowed && !expertMode)
    {
        VERBOSE(VB_IMPORTANT, "Unexpected DB Schema version.  Waiting to see "
                "if DB is being upgraded.");

        bool backupRunning = false;
        MythTimer elapsedTimer;
        elapsedTimer.start();
        while (versionCheck && (elapsedTimer.elapsed() < 5000))
        {
            sleep(1);

            versionCheck = CompareTVDatabaseSchemaVersion();

            if (DBUtil::IsBackupInProgress())
            {
                elapsedTimer.restart();
                if (!backupRunning)
                {
                    VERBOSE(VB_IMPORTANT, "Waiting for Database Backup to "
                            "complete.");
                    backupRunning = true;
                }
            }
        }

        if (versionCheck)
            VERBOSE(VB_IMPORTANT, "Timed out waiting.");
        else
            VERBOSE(VB_IMPORTANT,
                    "Schema version was upgraded while we were waiting.");
    }

    if (versionCheck < 0 && !upgradeAllowed && !expertMode)
    {
        VERBOSE(VB_IMPORTANT, "This version of MythTV requires an updated "
                "database schema. Please run mythtv-setup or mythbackend "
                "to update your database.");
        exit(FRONTEND_EXIT_DB_OUTOFDATE);
    }
    else if ((versionCheck > 0) && !expertMode)
    {
        VERBOSE(VB_IMPORTANT, "The schema version of your existing database "
                "is newer than this version of MythTV understands. Please "
                "ensure that you have selected the proper database server or "
                "upgrade this and all other frontends and backends to the "
                "same MythTV version and revision.");
        exit(FRONTEND_EXIT_DB_OUTOFDATE);
    }
    if ((upgradeAllowed || expertMode) && !UpgradeTVDatabaseSchema())
    {
        VERBOSE(VB_IMPORTANT,
                "Couldn't upgrade database to new schema, exiting.");
        exit(FRONTEND_EXIT_DB_OUTOFDATE);
    }
    gContext->ActivateSettingsCache(true);
}

int main(int argc, char **argv)
{
    bool bPromptForBackend    = false;
    bool bBypassAutoDiscovery = false;
    bool upgradeAllowed = false;

    QString geometry = QString::null;
    QString display  = QString::null;

    for(int argpos = 0; argpos < argc; ++argpos)
    {
        if (!strcmp(argv[argpos],"--version"))
        {
            extern const char *myth_source_version;
            extern const char *myth_source_path;
            cout << "Please include all output in bug reports." << endl;
            cout << "MythTV Version   : " << myth_source_version << endl;
            cout << "MythTV Branch    : " << myth_source_path << endl;
            cout << "Library API      : " << MYTH_BINARY_VERSION << endl;
            cout << "Network Protocol : " << MYTH_PROTO_VERSION << endl;
#ifdef MYTH_BUILD_CONFIG
            cout << "Options compiled in:" <<endl;
            cout << MYTH_BUILD_CONFIG << endl;
#endif
            return FRONTEND_EXIT_OK;
        }
#ifdef USING_X11
    // Remember any -display or -geometry argument
    // which QApplication init will remove.
        else if (!strcmp(argv[argpos],"-geometry"))
            geometry = argv[argpos+1];
        else if (!strcmp(argv[argpos],"-display"))
            display = argv[argpos+1];
#endif // USING_X11
    }

#ifdef Q_WS_MACX
    // Without this, we can't set focus to any of the CheckBoxSetting, and most
    // of the MythPushButton widgets, and they don't use the themed background.
    QApplication::setDesktopSettingsAware(FALSE);
#endif
    QApplication a(argc, argv);

    QString pluginname = "";
    QMap<QString, QString> settingsOverride;

    QFileInfo finfo(a.argv()[0]);

    QString binname = finfo.baseName();

    bool ResetSettings = false;

    if (binname.lower() != "mythfrontend")
        pluginname = binname;

    for (int argpos = 1; argpos < a.argc(); ++argpos)
    {
        if (!strcmp(a.argv()[argpos],"-display") ||
            !strcmp(a.argv()[argpos],"--display"))
        {
            if (a.argc()-1 > argpos)
            {
                display = a.argv()[argpos+1];
                if (display.startsWith("-"))
                {
                    cerr << "Invalid or missing argument to -display option\n";
                    return FRONTEND_EXIT_INVALID_CMDLINE;
                }
                else
                    ++argpos;
            }
            else
            {
                cerr << "Missing argument to -display option\n";
                return FRONTEND_EXIT_INVALID_CMDLINE;
            }
        }
        else if (!strcmp(a.argv()[argpos],"-h") ||
                !strcmp(a.argv()[argpos],"--help") ||
                !strcmp(a.argv()[argpos],"--usage"))
        {
            PrintHelp();
            return FRONTEND_EXIT_OK;
        }
        else if (!strcmp(a.argv()[argpos],"--prompt") ||
                 !strcmp(a.argv()[argpos],"-p" ))
        {
            bPromptForBackend = true;
        }
        else if (!strcmp(a.argv()[argpos],"--disable-autodiscovery") ||
                 !strcmp(a.argv()[argpos],"-d" ))
        {
            bBypassAutoDiscovery = true;
        }
        else if (!strcmp(a.argv()[argpos],"--verbose") ||
                 !strcmp(a.argv()[argpos],"-v"))
        {
            if (a.argc()-1 > argpos)
            {
                if (parse_verbose_arg(a.argv()[argpos+1]) ==
                        GENERIC_EXIT_INVALID_CMDLINE)
                    return FRONTEND_EXIT_INVALID_CMDLINE;

                ++argpos;
            } else
            {
                cerr << "Missing argument to -v/--verbose option\n";
                return FRONTEND_EXIT_INVALID_CMDLINE;
            }
        }
    }

    if (!display.isEmpty())
    {
        MythContext::SetX11Display(display);
    }

    gContext = new MythContext(MYTH_BINARY_VERSION);
    g_pUPnp  = new MediaRenderer();

    if (!gContext->Init(true, g_pUPnp, bPromptForBackend, bBypassAutoDiscovery))
    {
        VERBOSE(VB_IMPORTANT, "Failed to init MythContext, exiting.");
        return FRONTEND_EXIT_NO_MYTHCONTEXT;
    }

    for(int argpos = 1; argpos < a.argc(); ++argpos)
    {
        if (!strcmp(a.argv()[argpos],"-l") ||
            !strcmp(a.argv()[argpos],"--logfile"))
        {
            if (a.argc()-1 > argpos)
            {
                logfile = a.argv()[argpos+1];
                if (logfile.startsWith("-"))
                {
                    cerr << "Invalid or missing argument to -l/--logfile option\n";
                    return FRONTEND_EXIT_INVALID_CMDLINE;
                }
                else
                {
                    ++argpos;
                }
            }
            else
            {
                cerr << "Missing argument to -l/--logfile option\n";
                return FRONTEND_EXIT_INVALID_CMDLINE;
            }
        } else if (!strcmp(a.argv()[argpos],"-v") ||
                   !strcmp(a.argv()[argpos],"--verbose"))
        {
            // Arg processing for verbose already done (before MythContext)
            ++argpos;
        }
        else if (!strcmp(a.argv()[argpos],"-r") ||
                 !strcmp(a.argv()[argpos],"--reset"))
        {
            ResetSettings = true;
        }
        else if (!strcmp(a.argv()[argpos],"-w") ||
                 !strcmp(a.argv()[argpos],"--windowed"))
        {
            settingsOverride["RunFrontendInWindow"] = "1";
        }
        else if (!strcmp(a.argv()[argpos],"-nw") ||
                 !strcmp(a.argv()[argpos],"--no-windowed"))
        {
            settingsOverride["RunFrontendInWindow"] = "0";
        }
        else if (!strcmp(a.argv()[argpos],"-O") ||
                 !strcmp(a.argv()[argpos],"--override-setting"))
        {
            if (a.argc()-1 > argpos)
            {
                QString tmpArg = a.argv()[argpos+1];
                if (tmpArg.startsWith("-"))
                {
                    cerr << "Invalid or missing argument to -O/--override-setting option\n";
                    return BACKEND_EXIT_INVALID_CMDLINE;
                } 
 
                QStringList pairs = QStringList::split(",", tmpArg);
                for (unsigned int index = 0; index < pairs.size(); ++index)
                {
                    QStringList tokens = QStringList::split("=", pairs[index]);
                    tokens[0].replace(QRegExp("^[\"']"), "");
                    tokens[0].replace(QRegExp("[\"']$"), "");
                    tokens[1].replace(QRegExp("^[\"']"), "");
                    tokens[1].replace(QRegExp("[\"']$"), "");
                    settingsOverride[tokens[0]] = tokens[1];
                }
            }
            else
            {
                cerr << "Invalid or missing argument to -O/--override-setting option\n";
                return BACKEND_EXIT_INVALID_CMDLINE;
            }

            ++argpos;
        }
        else if (!strcmp(a.argv()[argpos],"-G") ||
                 !strcmp(a.argv()[argpos],"--get-setting"))
        {
            if (a.argc()-1 > argpos)
            {
                QString tmpArg = a.argv()[argpos+1];
                if (tmpArg.startsWith("-"))
                {
                    cerr << "Invalid or missing argument to -G/--get-setting option\n";
                    return FRONTEND_EXIT_INVALID_CMDLINE;
                } 
 
                QStringList pairs = QStringList::split(",", tmpArg);
                QString value;
                for (unsigned int index = 0; index < pairs.size(); ++index)
                {
                    value = gContext->GetSetting(pairs[index]);
                    cout << "\tSettings Value : " << pairs[index];
                    cout <<  " = " << value << endl;
                }
                return FRONTEND_EXIT_OK;
            }
            else
            {
                cerr << "Invalid or missing argument to -G/--get-setting option\n";
                return FRONTEND_EXIT_INVALID_CMDLINE;
            }

            ++argpos;
        }
        else if (!strcmp(a.argv()[argpos],"-geometry") ||
                 !strcmp(a.argv()[argpos],"--geometry"))
        {
            if (a.argc()-1 > argpos)
            {
                geometry = a.argv()[argpos+1];
                if (geometry.startsWith("-"))
                {
                    cerr << "Invalid or missing argument to -geometry option\n";
                    return FRONTEND_EXIT_INVALID_CMDLINE;
                }
                else
                    ++argpos;
            }
            else
            {
                cerr << "Missing argument to -geometry option\n";
                return FRONTEND_EXIT_INVALID_CMDLINE;
            }
        }
        else if (!strcmp(a.argv()[argpos],"--prompt") ||
                 !strcmp(a.argv()[argpos],"-p" ))
        {
        }
        else if (!strcmp(a.argv()[argpos],"--disable-autodiscovery") ||
                 !strcmp(a.argv()[argpos],"-d" ))
        {
        }
        else if (!strcmp(a.argv()[argpos],"--upgrade-schema") ||
                 !strcmp(a.argv()[argpos],"-u" ))
        {
            upgradeAllowed = true;
        }
        else if ((argpos + 1 == a.argc()) &&
                    !QString(a.argv()[argpos]).startsWith("-"))
        {
            pluginname = a.argv()[argpos];
        }
        else
        {
            if (!(!strcmp(a.argv()[argpos],"-h") ||
                !strcmp(a.argv()[argpos],"--help") ||
                !strcmp(a.argv()[argpos],"--usage")))
                cerr << "Invalid argument: " << a.argv()[argpos] << endl;
            PrintHelp();
            return FRONTEND_EXIT_INVALID_CMDLINE;
        }
    }

    if (logfile != "")
    {
        if (log_rotate(1) < 0)
            cerr << "cannot open logfile; using stdout/stderr" << endl;
        else
            signal(SIGHUP, &log_rotate_handler);
    }

    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        cerr << "Unable to ignore SIGPIPE\n";

    QString fileprefix = MythContext::GetConfDir();

    QDir dir(fileprefix);
    if (!dir.exists())
        dir.mkdir(fileprefix);

    if (ResetSettings)
    {
       AppearanceSettings as;
       as.save();

       MSqlQuery query(MSqlQuery::InitCon());
       query.prepare("update settings set data='EN' "
                     "WHERE hostname = :HOSTNAME and value='Language' ;");
       query.bindValue(":HOSTNAME", gContext->GetHostName());
       query.exec();

       return FRONTEND_EXIT_OK;
    }

    if (!geometry.isEmpty() && !gContext->ParseGeometryOverride(geometry))
    {
        VERBOSE(VB_IMPORTANT,
                QString("Illegal -geometry argument '%1' (ignored)")
                .arg(geometry));
    }

    if (settingsOverride.size())
    {
        QMap<QString, QString>::iterator it;
        for (it = settingsOverride.begin(); it != settingsOverride.end(); ++it)
        {
            VERBOSE(VB_IMPORTANT, QString("Setting '%1' being forced to '%2'")
                                          .arg(it.key()).arg(it.data()));
            gContext->OverrideSettingForSession(it.key(), it.data());
        }
    }

    // Create priveleged thread, then drop privs
    pthread_t priv_thread;
    bool priv_thread_created = true;

    VERBOSE(VB_PLAYBACK, QString("user: %1 effective user: %2 before "
                            "privileged thread").arg(getuid()).arg(geteuid()));
    int status = pthread_create(&priv_thread, NULL, run_priv_thread, NULL);
    VERBOSE(VB_PLAYBACK, QString("user: %1 effective user: %2 after "
                            "privileged thread").arg(getuid()).arg(geteuid()));
    if (status) 
    {
        VERBOSE(VB_IMPORTANT, QString("Warning: ") +
                "Failed to create priveledged thread." + ENO);
        priv_thread_created = false;
    }
    setuid(getuid());

    CheckSchemaVersion(upgradeAllowed);

    VERBOSE(VB_IMPORTANT, QString("%1 version: %2 www.mythtv.org")
                            .arg(binname).arg(MYTH_BINARY_VERSION));

    VERBOSE(VB_IMPORTANT, QString("Enabled verbose msgs: %1").arg(verboseString));

    LCD::SetupLCD();
    if (LCD *lcd = LCD::Get())
        lcd->setupLEDs(RemoteGetRecordingMask);

    LanguageSettings::load("mythfrontend");

    WriteDefaults();

    QString themename = gContext->GetSetting("Theme", "G.A.N.T");
    bool randomtheme = gContext->GetNumSetting("RandomTheme", 0);

    if (randomtheme)
        themename = RandTheme(themename);

    QString themedir = gContext->FindThemeDir(themename);
    if (themedir == "")
    {
        cerr << "Couldn't find theme " << themename << endl;
        return FRONTEND_EXIT_NO_THEME;
    }

    gContext->LoadQtConfig();

    MythMainWindow *mainWindow = GetMythMainWindow();
    mainWindow->Init();
    gContext->SetMainWindow(mainWindow);

    gContext->UpdateImageCache();
    themeBase = new MythThemeBase();

    LanguageSettings::prompt();

    InitJumpPoints();

    internal_media_init();

    CleanupMyOldInUsePrograms();

    MythPluginManager *pmanager = new MythPluginManager();
    gContext->SetPluginManager(pmanager);

    if (gContext->GetNumSetting("EnableXbox") == 1)
    {
        xbox = new XBox();
        xbox->GetSettings();
    }
    else
        xbox = NULL;

    // this direct connect is only necessary, if the user wants to use the
    // auto shutdown/wakeup feature
    if (gContext->GetNumSetting("idleTimeoutSecs",0) > 0)
        gContext->ConnectToMasterServer();

    qApp->lock();

    if (pluginname != "")
    {
        if (pmanager->run_plugin(pluginname))
        {
            qApp->setMainWidget(mainWindow);
            qApp->exec();

            qApp->unlock();
            return FRONTEND_EXIT_OK;
        }
        else 
        {
            pluginname = "myth" + pluginname;
            if (pmanager->run_plugin(pluginname))
            {
                qApp->setMainWidget(mainWindow);
                qApp->exec();

                qApp->unlock();
                return FRONTEND_EXIT_OK;
            }
        }
    }

    qApp->unlock();

    MediaMonitor *mon = MediaMonitor::GetMediaMonitor();
    if (mon)
        mon->StartMonitoring();

    NetworkControl *networkControl = NULL;
    if (gContext->GetNumSetting("NetworkControlEnabled", 0))
    {
        int networkPort = gContext->GetNumSetting("NetworkControlPort", 6545);
        networkControl = new NetworkControl(networkPort);
        if (!networkControl->ok())
            VERBOSE(VB_IMPORTANT,
                    QString("NetworkControl failed to bind to port %1.")
                    .arg(networkPort));
    }

    gContext->addCurrentLocation("MainMenu");

    int exitstatus = NO_EXIT;

    do
    {
        themename = gContext->GetSetting("Theme", "blue");
        themedir = gContext->FindThemeDir(themename);
        if (themedir == "")
        {
            cerr << "Couldn't find theme " << themename << endl;
            return FRONTEND_EXIT_NO_THEME;
        }

        if (!RunMenu(themedir))
            break;

	// Setup handler for USR1 signals to reload theme
	signal(SIGUSR1, &signal_USR1_handler);

        qApp->setMainWidget(mainWindow);
        qApp->exec();
    } while (!(exitstatus = handleExit()));

    if (exitstatus == HALT)
        haltnow();

    if (exitstatus == REBOOT)
        rebootnow();

    pmanager->DestroyAllPlugins();

    if (mon)
    {
        mon->StopMonitoring();
        delete mon;
    }

    if (priv_thread_created)
    {
        void *value;
        gContext->addPrivRequest(MythPrivRequest::MythExit, NULL);
        pthread_join(priv_thread, &value);
    }

    signal(SIGHUP, SIG_DFL);

    if (networkControl)
        delete networkControl;

    DestroyMythMainWindow();
    delete themeBase;
    delete gContext;
    // This takes a few seconds, so inform the user:
    VERBOSE(VB_GENERAL, "Deleting UPnP client...");
    delete g_pUPnp;

    return FRONTEND_EXIT_OK;

}
/* vim: set expandtab tabstop=4 shiftwidth=4: */
