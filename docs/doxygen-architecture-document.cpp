/**
\mainpage MythTV Architecture

\section intro Introduction

Over the last couple years %MythTV has grown into a rather large
application. The purpose of these pages is to provide a portal 
into the code for developers to get their heads around it. 
This is intended for both those new to %MythTV and experienced 
with it to get familiar with different aspects of the code base.

If you are just looking for the code formatting standards, 
see the official %MythTV wiki article 
<a href="http://www.mythtv.org/wiki/index.php/Coding_Standards">
coding standards</a>. If you are looking for the 
<a href="http://svn.mythtv.org/trac/wiki/TicketHowTo">bug tracker</a>,
it can be found on the official pages. 
If you haven't already, you should subscribe to
the <a href="http://www.mythtv.org/mailman/listinfo/mythtv-dev/">
developer mailing list</a> and the <a href="http://www.mythtv.org/mailman/listinfo/mythtv-commits/">SVN commits mailing list</a>

If you just stumbled onto the developer pages
by accident, maybe you want to go to the official
<a href="http://www.mythtv.org/modules.php?name=MythInstall">%MythTV
Installation page</a>. There is also a good unofficial 
<a href="http://wilsonet.com/mythtv/fcmyth.php">Fedora %MythTV installation</a> page,
and a 
<a href="http://dev.gentoo.org/~cardoe/mythtv/">Gentoo %MythTV installation</a> page.

If you are new to Qt programming it is essential that you keep in mind
that almost all Qt objects are not thread-safe, including QString.
Almost all Qt container objects, including QString, make shallow copies
on assignment, the two copies of the object must only be used in one
thread unless you use a lock on the object. You can use the 
<a href="http://doc.trolltech.com/3.1/qdeepcopy.html">QDeepCopy</a>
template on most Qt containers to make a copy you can use in another
thread.

There are also special dangers when 
\ref qobject_dangers "using QObject" outside the Qt event thread.

The is also short HOWTO on \ref profiling_mythtv available in addition
to documentation on the code itself.

\section libs Libraries

%MythTV is divided up into eleven libraries:
<dl>
  <dt>libmyth                <dd>Core %MythTV library. Used by the Plugins.
      The \ref database_subsystem "database",
      \ref audio_subsystem "audio",
      \ref lcd_subsystem "LCD",
      \ref osd_subsystem "OSD",
      \ref lirc_subsystem "LIRC", and the
      \ref myth_network_protocol "myth network protocol" are supported by libmyth.
  <dt>libmythtv              <dd>%MythTV %TV functionality library.
      The 
      \ref recorder_subsystem "recorders", \ref video_subsystem "video" and 
      \ref av_player_subsystem "A/V players" are supported by libmythtv.
  <dt>libmythui              <dd>Main user interface rendering library
  <dt>libavcodec/libavformat/libavutil
      <dd>This is the ffmpeg A/V decoding library (aka avlib).
      <a href="http://ffmpeg.mplayerhq.hu/documentation.html">Documented Externally</a>.
  <dt>libmythmpeg2           <dd>Alternate MPEG-1/2 A/V decoding library.
      <a href="http://libmpeg2.sourceforge.net/">External Website</a>.
  <dt>libmythsamplerate      <dd>Audio resampling library
      <a href="http://www.mega-nerd.com/SRC/api.html">Documented Externally</a>.
      We use this to support a different output sample rates than the sample
      rate used in the audio streams we play.
  <dt>libmythsoundtouch      <dd>Pitch preserving audio resampling library.
      <a href="http://www.surina.net/soundtouch/">External Website</a>.
      We use this for the time-stretch feature.
  <dt>libmythdvdnav
      <dd>Used for navigating DVD menus when using the internal player
  <dt>libmythfreemheg        <dd>UK interactive %TV viewer
  <dt>libmythlivemedia       <dd>Support for the FreeBox recorder device
  <dt>libmythupnp            <dd>Initial uPnP (universal Plug and Play) support
</dl>
Two libraries libmythmpeg2 and libmythsamplerate appear redundant, but
libmpeg2 decodes MPEG-2 more quickly than ffmpeg on some systems, and
libmythsamplerate resamples audio with better quality when we only need
to match the hardware sample rate to the A/V streams audio sample rate.

\section db Database Schema
The database schema is documented here \ref db_schema.

\section apps Applications
%MythTV contains 14 applications:

<dl>
  <dt>mythbackend      <dd>This is the backend which runs the recorders.
  <dt>mythfrontend
      <dd>This is the frontend which is the main application
          for viewing programs and using the %MythTV plugins.
  <dt>mythtv-setup     <dd>This is the program which sets up the database
                           to use a machine as a backend server.
  <dt>mythtv           <dd>This was an "External Player" used to play videos
                           from within mythfrontend. Setting the player command
                           to "internal" does the same thing now. This is handy
                           for testing the audio and videoout code, though.
  <dt>mythtvosd
      <dd>This is used externally by programs that want to pop-up an
          <i>on screen display</i> in %MythTV while one is watching a recording.
  <dt>mythfilldatabase
      <dd>This is used both internally and externally to fetch program listings.
          <a href="http://tms.tribune.com/">Tribune Media</a> provides
          listings in exchange for demographic information in the USA,
          and Australia uses a
          <a href="http://minnie.tuhs.org/twiki/bin/view/TVGuide">
          community-driven TV guide</a> originally developed for OzTiVo.
          Other markets are served by the
          <a href="http://membled.com/work/apps/xmltv/xmltv">XMLTV</a>
          web spiders.
  <dt>mythtranscode    <dd>This is used both internally and externally to
                           transcode videos from one format to another. 
                           This is used to shrink HDTV programs to lower
                           quality recordings that match the hardware the
                           user has.
  <dt>mythjobqueue     <dd>This is used internally by mythfrontend to schedule
                           jobs such as commercial flagging and transcoding.
  <dt>mythcommflag     <dd>This is used internally by mythfrontend to flag
                           commercials.
  <dt>mythepg          <dd>This is used internally by mythfrontend to find
                           upcoming programs to record based on the channel
                           and time.
  <dt>mythprogfind     <dd>This is used internally by mythfrontend to find 
                           programs to record based on the first letter of
                           the program name.
  <dt>mythuitest       <dd>This is a test program for libmythui development.
  <dt>mythlcdserver    <dd>This is an interface between a number of Myth
                           clients and a small text display (LCDProc server).
  <dt>mythwelcome/mythshutdown
      <dd>These programs manage Power Saving (shutdown/wakeup) on your Myth PCs.
</dl>

\section fe_plugins Frontend Plugins
<dl>
  <dt>mytharchive <dd>Creates themed Video DVDs from recordings
                      (and other video files).
  <dt>mythbrowser <dd>Provides a simple web browser.
  <dt>mythcontrols<dd>Editor for Myth Key bindings, \e et \e c.
  <dt>mythflix    <dd>
  <dt>mythgallery <dd>A simple picture viewer for your %TV.
  <dt>mythgame    <dd>Launches the xmame classic game system emulator.
  <dt>mythmovies  <dd>
  <dt>mythmusic   <dd>A simple music player for your %TV.
  <dt>mythnews    <dd>Browses RSS news feeds.
  <dt>mythphone   <dd>SIP based video phone.
  <dt>mythvideo   <dd>Launch DVD players, and a Video Browser for other files
                      (non-%MythTV recordings).
  <dt>mythweather <dd>Presents your local weather report.
  <dt>mythzoneminder<dd>
</dl>

\section be_plugins Backend Plugins
<dl>
  <dt>mythweb     <dd>Provides a PHP based web pages to control mythbackend.
</dl>

\section spt_scripts Support Scripts
These tools are in the contrib directory of the source tree:
<dl>
  <dt>osx-packager.pl   <dd>Downloads and builds all dependencies, then the
                            source, of %MythTV and all the official plugins,
                            on Mac OS 10.3 thru 10.5
  <dt>win32-packager.pl <dd>Similar tool for Windows XP and Vista
</dl>
 */

/** \defgroup database_subsystem    Database Subsystem
    \todo No one is working on documenting the database subsystem.

There are a few classes that deal directly with the database: 
MythContext, MSqlDatabase, DBStorage, MDBManager, SimpleDBStorage.

And one function UpgradeTVDatabaseSchema() located in dbcheck.cpp.

MythTV's Configurable widgets also do save and restore their values
from the database automagically when used in %MythTV's window classes.
 */

/** \defgroup audio_subsystem       Audio Subsystem
    \todo Ed W will be documenting the audio subsystem.
 */

/** \defgroup lcd_subsystem         LCD Subsystem
    \todo No one is working on documenting the LCD Subsystem
 */

/** \defgroup osd_subsystem         OSD Subsystem
    \todo No one is working on documenting the OSD Subsystem
 */

/** \defgroup lirc_subsystem        LIRC Subsystem
    \todo No one is working on documenting the LIRC Subsystem
 */

/** \defgroup video_subsystem       Video Subsystem
    \todo No one is working on documenting the video subsystem
 */

/** \defgroup recorder_subsystem    Recorder Subsystem
This line is filler that is ignored by Doxygen.

TVRec is the main class for handling recording. 
It is passed a ProgramInfo for the current and next recordings,
and in turn creates three main worker classes:
<dl>
  <dt>RecorderBase <dd>Recordings from device into RingBuffer.
  <dt>ChannelBase  <dd>Changes the channel and other recording device attributes. Optional.
  <dt>RingBuffer   <dd>Handles A/V file I/O, including streaming.
</dl>

%TVRec also presents the public face of a recordings to the 
\ref myth_network_protocol, and hence the rest of %MythTV. This
means that any call to the %RecorderBase, %RingBuffer, or
%ChannelBase is marshalled via methods in the %TVRec.

RecorderBase contains classes for recording %MythTV's 
specialized Nuppel Video files, as well as classes for 
handling various hardware encoding devices such as MPEG2,
HDTV, DVB and Firewire recorders.
ChannelBase meanwhile only contains three subclasses, %Channel
for handling v4l and v4l2 devices, and %DVBChannel and
%FirewireChannel, for handling DVB and Firewire respectively.
Other channel changing hardware use ChannelBase's external
channel changing program support.
Finally, RingBuffer does all reading and writing of A/V files
for both TV (playback) and %TVRec, including streaming
over network connections to the frontend's %RingBuffer.

%TVRec has four active states, the first three of which
correspond to the same states in %TV: kState_WatchingLiveTV,
kState_WatchingPreRecorded, kState_WatchingRecording, and
kState_RecordingOnly.
When watching "Live TV" the recorder records whatever the 
frontend requests and streams it out using the %RingBuffer,
this may be to disk or over the network.
When watching pre-recorded programs %TVRec simply streams a
file on disk out using the %RingBuffer. 
When just watching a recording, %TVRec continues a recording
started as recording-only while simultaneously streaming out
using the %RingBuffer.
Finally, when in the recording-only mode the recording
is only saved to disk using %RingBuffer and no streaming
to the frontend is performed.

%TVRec also has three additional states: kState_Error,
kState_None, kState_ChangingState. The error state allows
%MythTV to know when something has gone wrong. The null or
none state means the recorder is not doing anything and
is ready for commands. Finally, the "ChangingState" state
tells us that a state change request is pending, so other
state changing commands should not be issued.

\todo Check if the TVRec is actually still passed a ProgramInfo for the current and next recordings

\todo Document the massive changes Daniel has made in the Vid and MultiRec branches
 */

/** \defgroup av_player_subsystem   A/V Player Subsystem
This line is filler that is ignored by Doxygen.

TV is the main class for handling playback.
It instantiates several important classes:
<dl>
<dt>NuppelVideoPlayer <dd>Decodes audio and video from the RingBuffer, then plays and displays them, resp.
<dt>RemoteEncoder     <dd>Communicates with TVRec on the backend.
<dt>OSD               <dd>Creates on-screen-display for NuppelVideoPlayer to display.
<dt>RingBuffer        <dd>Handles A/V file I/O, including streaming.
</dl>

NuppelVideoPlayer is a bit of a misnomer. This class plays all video formats
that %MythTV can handle. It creates AudioOutput to handle the audio, a FilterChain
to perform post-processing on the video, a DecoderBase class to do the actual
video decoding, and a VideoOutput class to do the actual video output. See
that class for more on what it does.

%TV has three active states that correspond to the same states in TVRec:
kState_WatchingLiveTV, kState_WatchingPreRecorded, kState_WatchingRecording.
When watching "LiveTV" the %TVRec via RemoteEncoder responds to channel
changing and other commands from %TV while streaming the recording to 
%TV's %RingBuffer.
When watching a pre-recorded stream, a recording is streamed from TVRec's
%RingBuffer to %TV's %RingBuffer, but no channel changing commands can be
sent to TVRec.
When watching an 'in-progress' recording things work pretty much as when
watching a pre-recorded stream, except %TV must be prevented from seeking
too far ahead in the program, and keyframe and commercial flags must be 
synced from the recorder periodically.

%TV also has three additional states: kState_Error,
kState_None, kState_ChangingState. The error state allows
%MythTV to know when something has gone wrong. The null or
none state means the player is not doing anything and
is ready for commands. Finally, the "ChangingState" state
tells us that a state change request is pending, so other
state changing commands should not be issued.

 */

/** \defgroup plugin_arch   Plugin Architecture
This line is filler that is ignored by Doxygen.

MythPlugins are shared object files (<I>i.e.</I> libraries) which are loaded
from a specific directory (<I>%e.g.</I> /usr/local/lib/mythtv/plugins).
Currently, all plugins are written in the C++ language, but there is nothing
preventing other languages being used (the functions are in the C name space).

int mythplugin_init(const char *libversion); is invoked whenever mythfrontend
is started. This typically handles upgrading any database records - it will be
the first method called after a new version of the plugin has been installed.

int mythplugin_run(); is invoked when the action attribute of a menu item
requests it via the PLUGIN directive. This is when the user chooses to enter
the plugin from the main menu or the appropriate submenu.

int mythplugin_config(); is invoked when the action attribute of a menu item
requests it via the CONFIGPLUGIN directive. This should be when the users
wishes to change the plugin's settings (in the main Setup menu).

Other plugin functions are listed in the file \link mythpluginapi.h \endlink
(see also the \link MythPlugin \endlink
 and \link MythPluginManager \endlink classes.)

*/

/** \defgroup mtd                   MTD (the Myth Transcoding Daemon)
This line is filler that is ignored by Doxygen.

The %MTD is a simple program that is used by the MythVideo plugin.

It currently \b only transcodes tracks from DVDs, but could in the
future also rip CD/VCD tracks, transcode %TV recordings, \e et \e cetera.

The DVD Rip screen will offer to start %MTD if it is not running.
It does \b not currently shut it down afterwards, though.

By default, the %MTD listens on port 2442, for commands of this format:

\verbatim
halt
quit
shutdown
\endverbatim
- Tells the %MTD to wait for all jobs to finish, stop listening, and exit.

\verbatim
hello
\endverbatim
- Send a trivial response if the %MTD is listening.

\verbatim
status
\endverbatim
<B>\verbatim
status dvd summary 1
status dvd job 0 overall 0.0262568 ISO copy of UNTITLED
status dvd job 0 subjob 0.0262568 Ripping to file ~ 00:08/05:04
status dvd complete
\endverbatim</B>
- Sends a response listing the status of all jobs.

\verbatim
media
\endverbatim
<B>\verbatim
media dvd summary 1 UNTITLED
media dvd title 1 1 1 0 5 27 8
media dvd title-audio 1 1 2 nl ac3 2Ch
media dvd complete
\endverbatim</B>
- Sends a detailed list of titles/tracks on the current disk.

\verbatim
job dvd TITLE AUDIOTRK QUALITY AC3 SUBTITLETRK DESTPATH
\endverbatim
\verbatim
e.g. job dvd 1 1 -1 0 -1 /myth/video/UNTITLED
\endverbatim
- Starts a transcoding job. TITLE, AUDIOTRK and SUBTITLETRK are integers
  to select the track details. AC3 is 0 or 1. DESTPATH is the output file.
  QUALITY is one of enum \ref RIP_QUALITIES (-1, 0 or 1).

\verbatim
abort dvd job NNNN
\endverbatim
- Stops the job identified by the job number NNNN.

\verbatim
use dvd PATH
\endverbatim
- Check dvd PATH is usable, and if so set the current drive to it.

\verbatim
no dvd
\endverbatim
- Forget about the current dvd path, ending any jobs that are trying to read it.

\verbatim
no dvd PATH
\endverbatim
- End any jobs that are reading from dvd PATH, and if it is the current drive,
  forget about it.
 */

/** \defgroup myth_media            Myth Media Manager
This line is filler that is ignored by Doxygen.

The Myth Media Manager is a thread in the frontend which looks for any
changes to removable media, and sends events to any Frontend Plugins
which are interested in that media.

At startup, it creates a list of MythMediaDevice objects (currently
MythHDD or MythCDROM and its subclasses) for each removable device
configured on the system. A runtime loop then monitors each of these
via its checkMedia() method.

When any of these devices change status, a MediaEvent object is created.
If the device has a status which is usable, the window jumps to the main menu
(to allow the registered jump to work correctly), and the event is dispatched
to the relevant plugin's registered media handler. If the device status is
unusable (<I>%e.g.</I> ejected), the plugin's media handler is called directly
(so it can "forget" about this device).

The following tables show
typical status transitions for CD/DVD and USB flash drive devices:

\verbatim
      CD/DVD                     USB/FireWire HDD
State        Action           State         Action
--------------------------------------------------
NODISK                        NODISK
             eject                          attach
OPEN                          USEABLE
       insert disk, close                   detach
USEABLE                       UNPLUGGED
             mount
MOUNTED
            unmount
NOTMOUNTED
--------------------------------------------------
\endverbatim
 */

/** \defgroup myth_network_protocol Myth Network Protocol
This line is filler that is ignored by Doxygen.

The MythTV backend process currently opens sockets for three different types
of commands; a custom protocol (by default at port 6543),
an HTML server (by default <A HREF="http://127.0.0.1:6544">
http://127.0.0.1:6544
</A> for the status, and other services under that, like
< HREF="http://127.0.0.1:6544/Myth/GetMusic?Id=1">
http://127.0.0.1:6544/Myth/GetMusic?Id=1
</A>), and a UPnP media server (several ports that I cannot
remember right now that throw around lots of little bits of XML :-)

The custom protocol is an ASCII encoded length and command string.
Command sequences can be easily sent to the backend using telnet.
<I>%e.g.</I> \verbatim telnet 127.0.0.1 6543
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.\endverbatim
<B>\verbatim21      MYTH_PROTO_VERSION 36 23     ANN Playback hostname 1   10   QUERY_LOAD       4DONE\endverbatim</B>
\verbatim13      ACCEPT[]:[]362       OK34      0.919922[]:[]0.908203[]:[]0.856445Connection closed by foreign host.\endverbatim
The command string is prefixed by 8 characters, containing the length
of the forthcoming command. This can be justified in any way
(as the above example shows)

The backend responds with a length, and the response to the command.
This can be numbers (up to 32 bit, represented in ASCII),
a single string, or an ASCII encoding of a QStringList.
The 5 byte sequence "[]:[]" seperates items in the stringlist.
Any 64 bit numbers are represented as a stringlist of two 32bit words
(MSB first).

\section commands Commands

There are three main types of networking interactions in MythTV;
identification commands (which tell the backend about this client),
query commands that are sent to the master backend
(<I>%e.g.</I> listing recordings or viewing guide data), and
file streaming commands (when a frontend is watching or editing a recording).

Until a client is identified to the backend (via the ANN commands),
any of the query or file streaming commands will silently fail.

The following summarises some of these commands.
For a full understanding of all the commands, either read the source code
(programs/mythbackend/mainserver.cpp), or look on the Wiki
(http://www.mythtv.org/wiki/index.php/Myth_Protocol_Command_List).

 */

/** \defgroup myth_startup Myth startup sequence
This line is filler that is ignored by Doxygen.

Most MythTV programs follow a common sequence:
<ol>
  <li>Process (parse) command-line arguments</li>
  <li>Create a MythContext object, which stores paths for later location
      of runtime assets (filters/fonts/plugins/themes/translations)</li>
  <li>(optionally) Create a UPnP client or server</li>
  <li>Initialise the MythContext, which:</li>
  <ul>
    <li>Tries to find a database on localhost,
        or on the host specified in mysql.txt,</li>
    <li>Tries to locate exactly one backend host via UPnP,
        to find its database,</li>
    <li>If possible, displays a list of all backends located via UPnP
        for the user to choose from, or</li>
    <li>Fails</li>
  </ul>
  <li>Create the main window/screen, display themed menus, <i>et c.</i></li>
</ol>
(examine program/*/main.cpp, and libs/libmyth/mythcontext.cpp,
for further detail).

<p>

The "runtime assets" mentioned above are stored in a number of well-known
locations. The following methods in MythContext allow programs and plugins
to access these assets:
<ol>
  <li>GetInstallPrefix() returns the value of MCP's m_installprefix variable,
      which is either the runtime env. var. $MYTHTVDIR or the compile-time var.
      RUNPREFIX. If these are relative paths, it is initialised relative to the
      binary location. The value is used thus:
  <ul>
    <li>GetInstallPrefix() + /share/mythtv/ = GetShareDir(), GetFontsDir()</li>
    <li>GetInstallPrefix() + /share/mythtv/themes/ = GetThemesParentDir()</li>
    <li>GetInstallPrefix() + /share/mythtv/i18n/   = GetTranslationsDir()</li>
    <li>GetInstallPrefix() + /share/mythtv/mytharchive</li>
    <li>GetInstallPrefix() + /share/mythtv/mytharchive/themes</li>
    <li>GetInstallPrefix() + /share/mythtv/mytharchive/scripts</li>
    <li>GetInstallPrefix() + /share/mythtv/mythflix/scripts</li>
    <li>GetInstallPrefix() + /share/mythtv/mythnews</li>
    <li>GetInstallPrefix() + /share/mythtv/mythvideo/scripts</li>
    <li>GetInstallPrefix() + /share/mythtv/mythweather</li>
    <li>GetInstallPrefix() + /share/mythtv/mythweather/scripts</li>
    <li>GetInstallPrefix() + /bin/ignyte</li>
    <li>GetInstallPrefix() + /bin/mythfilldatabase</li>
    <li>GetInstallPrefix() + /bin/mtd</li>
    <li>GetInstallPrefix() + /lib/mythtv/ = GetLibraryDir()</li>
    <li>GetInstallPrefix() + /lib/mythtv/plugins/ = GetPluginsDir()</li>
    <li>GetInstallPrefix() + /lib/mythtv/filters/ = GetFiltersDir()</li>
  </ul></li>

  <li>GetConfDir() returns the value of the runtime env. var. $MYTHCONFDIR,
      or $HOME/.mythtv.</li>

  <li>mysql.txt is loaded from GetShareDir(), GetInstallPrefix() + /etc/mythtv,
      GetConfDir(), and the current directory. Later files override the values
      from earlier ones.</li>
</ol>

 */

/** \defgroup qobject_dangers QObject is dangerous for your health
This line is filler that is ignored by Doxygen.

QObject derived classes can be quite useful, they can send and receive
signals, get keyboard events, translate strings into another language
and use QTimers.

But they also can't be \ref qobject_delete "deleted" easily, they 
are not \ref qobject_threadsafe "thread-safe", can't participate
as equals in multiple \ref qobject_inheritence "inheritence", can
not be used at all in virtual \ref qobject_inheritence "inheritence".

\section qobject_delete Deleting QObjects

The problem with deleting QObjects has to do with signals, and events.
These can arrive from any thread, and often arrive from the Qt event
thread even if you have not explicitly requested them.

If you have not explicitly connected any signals or slots, and the 
only events you might get are dispatched with qApp->postEvent(),
not qApp->sendEvent(), then it is safe to delete the QObject in the
Qt event thread. If your QObject is a GUI object you've passed 
given to Qt for it to delete all is good. If your object is only 
deleted in response to a posted event or a signal from the Qt event
thread (say from a QTimer()) then you are OK as well.

If your class may be deleted outside a Qt thread, but it does not
explicitly connect any signals or slots, and the only events it
might get are dispatched with qApp->postEvent(), then you can 
safely use deleteLater() to queue your object up for deletion
in the event thread.

%MythTV is a very much a multithreaded program so you may need to have
a QObject that may get events from another thread if you send signals
but do not get any signals and or events from outside the Qt event
thread, then you can use deleteLater() or delete depending on whether
you are deleteing the QObject in the event thread, see above. But
if you are not in the Qt event thread then you need to call disconnect()
in the thread that is sending the signals, before calling deleteLater().
This prevents your object from sending events after you consider it 
deleted (and begin to delete other QObjects that may still be getting
signals from your object.)

What about if you are getting events via qApp->sendEvent() or a signal
from another thread? In this case things get complicated, so we highly
discourage this in %MythTV and prefer that you use callbacks if at all
possible. But in say you need to do this, then you need to disconnect
all the signals being sent to your QObject. But disconnect() itself
is not atomic. That is you may still get a signal after you disconnect
it. What you need to is disconnect the signal from your slot in the 
thread that is sending the signal. This prevents the signal from
being emitted while you are disconnecting. Doing this gracefully is
left as an excersize for the reader.

Ok, so the above is not entirely complete, for instance you could wrap
every signal emit in an instance lock and reimplement a disconnect
that uses that instance lock. But if you've figured this or another
solution out already all you need to know is that almost all Qt
objects are reenterant but not thread-safe, and that QObjects recieve
events and signals from the Qt event thread and from other threads
if you use qApp->sendEvent() or send signals to it from another thread.

\section qobject_threadsafe QObject thread-safety

The only thread-safe classes in Qt are QThread and the mutex classes.
when you call any non-static QObject method you must have a lock on
it or make sure only thread ever calls that function.

QObject itself is pretty safe because when you assign one QObject to
another you make a copy. Qt's container classes, including QString
do not make a copy but rather pass a reference counted pointer to
the data when you assign one to another. This reference counter is
not protected by a mutex so you can not use regular assignment when
passing a QString or other Qt container from one thread to another.

In order to use these classes safely you must use 
<a href="http://doc.trolltech.com/3.1/qdeepcopy.html">QDeepCopy</a>
when passing them from one thread to another. This is thankfully quite
easy to use.

\code
  QString original = "hello world";
  QString unsafe = original;
  QString safe = QDeepCopy<QString>(original);
\endcode

In this case safe and original can be used by two seperate
threads, while unsafe can only be used in the same thread 
as originalStr.

The QDeepCopy template will work on the most popular Qt containers,
for the ones it doesn't support you will have to copy manually.

\section qobject_inheritence QObject inheritence

You can not inherit more than one QObject derived class.
This is because of how signals and slots are implemented. What
happens is that each slot is transformed into an integer that
is used to find the right function pointer quickly when a signal
is sent. But if you have more than one QObject in the inheritence
tree the integers will alias so that signals intended for the
second slot in parent class B might get sent to the second slot
in parent class A instead. Badness ensues.

For the similar reason you can not inherit a QObject derived
class virtually, in this case the badness is a little more
complicated. The result is the same however, signals arrive
at the wrong slot. Usually, what happens is that the developer
tries to inherit two QObject derived classes but gets linking
errors, so they make one or more of the classes virtual, or
inherit a QObject non-virtually but then also inherit one
or more QObject derived classes virtually. The linker doesn't
complain, but strange unexplainable behaviour ensues when
the application is run and signals start showing up on the
wrong slots.

*/

/** \defgroup profiling_mythtv    Profiling MythTV
This line is filler that is ignored by Doxygen.

You can use any sampling profiler to profile %MythTV, the most
popular GPL one is OProfile and this section tells you how to
use it to profile %MythTV. For Intel also makes VTune, AMD
makes CodeAnalyst, and Apple makes Shark. These all work 
similarly to oprofile, so the %MythTV portions are the same.

This do not describe how to install oprofile. There are a
few cotchas on installation: You must have APIC enabled to
use performance counters. This is usually the default, but
you may want to check this in your kernel configuration. You
also need the oprofile module, this is part of the kernel
config in Linux 2.6+, but must be compiled seperately for
earlier kernels. Finally, some RedHat kernels does not work
with the standard oprofile, so you must use the RedHat
version of oprofile, or compile your own kernel.


\section prof_compile Compiling MythTV for profiling

The first thing you need to do is compile with profiling in
mind. %MythTV has three compile types "release", "debug" and
"profile". The "release" type enables optimizations that 
make debugging practically impossible, while "debug" has so
few optimizations that profiling it usually won't tell you 
where the real performance problems are. The "profile" compile
type enables the all the optimizations in "release" but also
enables debugging symbols, so that you know which functions
are the CPU hogs.

So we configure thusly:
\code
  make distclean
  ./configure --compile-type=profile --enable-proc-opt [+your normal opts]
\endcode

The "make distclean" clears out all your old binaries and the the configure
sets the compile type approriately. You may not want the "--enable-proc-opt"
if you plan on making a package for other CPUs. This only makes about a
10% difference in playback speed because MMX and CMOV are enabled for 
CPUs that support them regardless of this option.

Next, you compile and install %MythTV as you normally would.
\code
  qmake; make -j2 -k
  su
  make install
\endcode


\section prof_init Initializing profiler

Most sampling profilers, OProfile included, require a kernel module so
this module must be loaded. In oprofile's case the module gets confused
when it has been loaded a long time, so it is best to begin each session
by shutting down the OProfile server and reloading the module:

\code
  opcontrol --deinit
  opcontrol --init
\endcode

You also will want to tell the profiler whether to profile the
kernel as well, if you want the kernel profiled, and assuming
your compiled kernel is in /usr/src/linux, issue this command:
\code
  opcontrol --vmlinux=/usr/src/linux/vmlinux
\endcode
If you don't want it in your profile report, use this command:
\code
  opcontrol --no-vmlinux
\endcode
Normally, you don't need it when profiling %MythTV, but there are
times when the kernel is a bottleneck. For example when writing
streams to disk is taking too long.

Now all CPU's are not profiled the same way. In the days of olde,
a sampling profiler would hook into the clock interupt and record
the instuction pointer's location at the instant the interrupt was
triggered. But the wall clock timer still ticks as often as it did
when CPU's ran at 5 Mhz, and now they run almost a thousand times
faster. Such a profile would now take a thousand times longer than
it did in 1985.

So modern processors include explicit support for profiling. For,
example the Pentium Pro has the INST_RETIRED counter, the Pentium 4
has the INSTR_RETIRED counter, the AMD64 has the RETIRED_INSNS
counter, etc. For a full list see the 
<a href="http://oprofile.sourceforge.net/docs/">OProfile documentation</a>.
You need to find a performance counter you can use for your CPU,
after you've run "opcontrol --init" you can run the following command:
\code
  opcontrol --list-events
\endcode
And it will list all the performance counters available with your
CPU. You need to pick the best one for what you want to measure,
for overall performance you want something line INSTR_RETIRED.

For an Intel Pentium 4, the following will work
\code
  opcontrol -e INSTR_RETIRED:6000:0x0f:0:1
\endcode

This tells OProfile to schedule an interrupt on every 6000th 
instruction that finishes and matches the flag 0x0f. You
could also count cycles when not in idle for the same type of
measurements. The last two numbers tell OProfile it should not 
profile the kernel and should profile userspace, if you want 
to profile the kernel as well you need to replace the zero with 
a one.

It is possible to profile in oprofile the old fashioned way, by
using the real-time-clock. But you must compile your Linux kernel
WITHOUT RTC support, and run %MythTV significantly longer to get
enough samples for a good report.


\section prof_run Running Profiler

Normally, oprofile will tell you about everything running in
its reports. You can limit what information is collected by 
running a opcontrol command such as:
\code
  opcontrol --image=/usr/local/bin/mythfrontend
\endcode
This tells OProfile to only profile the frontend.

Now you want to start %MythTV as usual:

Then begin doing whatever you want to profile, then issue these
commands:
\code
  opcontrol --reset ; opcontrol --start
\endcode

After a few seconds/minutes your should have collected enough data.
Then you want to issue the following commands:
\code
  opcontrol --shutdown
  opreport --merge tgid -l --image-path=/usr/local/bin > report.txt
\endcode

Now you can peruse the report at your pleasure.

This should be enough to get you started with finding and fixing
performance problems in %MythTV, but there is more information
available from the
<a href=http://oprofile.sourceforge.net/doc/index.html>OProfile manual</a>.
This includes information on constructing callgraphs, producing
an annotated versions of the code, and saving results.

*/

// The file ac3.h in ffmpeg contains no Doxygen markup,
// but for some reason generates the following modules:
// * Coded elements
// * Derived values
//
// They add nothing useful to this programmer doco, so I camouflage these
// by manually defining the block here. Same thing for the few modules
// in ffmpeg that are documented; Macroblock and VC9 bitplanes from vc1.c,
// Monkey Audio from apedec.c, multithreaded slicing from h264.h,

/**
 @defgroup coded   .
 @defgroup derived .
 @defgroup block   .
 @defgroup std_mb  .
 @defgroup rangecoder .
 @defgroup multithreading .
 @defgroup bitplane .
 */
