// ANSI C headers
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cerrno>

// POSIX C headers
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

// Qt headers
#include <qsocket.h>
#include <qfile.h>
#include <qapplication.h>
#include <qdatetime.h>
#include <qfileinfo.h>

using namespace std;

#include "exitcodes.h"
#include "RingBuffer.h"
#include "mythcontext.h"
#include "remotefile.h"
#include "remoteencoder.h"
#include "ThreadedFileWriter.h"
#include "livetvchain.h"
#include "DVDRingBuffer.h"
#include "util.h"
#include "compat.h"

#ifndef O_STREAMING
#define O_STREAMING 0
#endif

#ifndef O_LARGEFILE
#define O_LARGEFILE 0
#endif

#ifndef O_BINARY
#define O_BINARY 0
#endif

const uint RingBuffer::kBufferSize = 3 * 1024 * 1024;

#define CHUNK 32768 /* readblocksize increments */

#define PNG_MIN_SIZE   20 /* header plus one empty chunk */
#define NUV_MIN_SIZE  204 /* header size? */
#define MPEG_MIN_SIZE 376 /* 2 TS packets */

#define LOC QString("RingBuf(%1): ").arg(filename)
#define LOC_ERR QString("RingBuf(%1) Error: ").arg(filename)

/* should be minimum of the above test sizes */
const uint RingBuffer::kReadTestSize = PNG_MIN_SIZE;

/** \class RingBuffer
 *  \brief Implements a file/stream reader/writer.
 *
 *  This class, despite its name, no-longer provides a ring buffer.
 *  It can buffer reads and provide support for streaming files.
 *  It also provides a wrapper for the ThreadedFileWriter which
 *  makes sure that the file reader does not read past where the 
 *  wrapped TFW has written new data.
 *
 */

/** \fn RingBuffer::RingBuffer(const QString&, bool, bool, uint)
 *  \brief Creates a RingBuffer instance.
 *
 *   You can explicitly disable the readahead thread by setting
 *   readahead to false, or by just not calling Start(void).
 *
 *  \param lfilename    Name of file to read or write.
 *  \param write        If true an encapsulated writer is created
 *  \param readahead    If false a call to Start(void) will not
 *                      a pre-buffering thread, otherwise Start(void)
 *                      will start a pre-buffering thread.
 *  \param read_retries How often to retry reading the file
 *                      before giving up.
 */
RingBuffer::RingBuffer(const QString &lfilename,
                       bool write, bool readahead,
                       uint read_retries)
    : filename(QDeepCopy<QString>(lfilename)),
      tfw(NULL),                fd2(-1),
      writemode(false),
      readpos(0),               writepos(0),
      stopreads(false),         recorder_num(0),
      remoteencoder(NULL),      remotefile(NULL),
      startreadahead(readahead),readAheadBuffer(NULL),
      readaheadrunning(false),  readaheadpaused(false),
      pausereadthread(false),
      rbrpos(0),                rbwpos(0),
      internalreadpos(0),       ateof(false),
      readsallowed(false),      wantseek(false), setswitchtonext(false),
      rawbitrate(4000),         playspeed(1.0f),
      fill_threshold(65536),    fill_min(-1),
      readblocksize(CHUNK),     wanttoread(0),
      numfailures(0),           commserror(false),
      dvdPriv(NULL),            oldfile(false),
      livetvchain(NULL),        ignoreliveeof(false),
      readAdjust(0)
{
    pthread_rwlock_init(&rwlock, NULL);

    if (write)
    {
        tfw = new ThreadedFileWriter(
            filename, O_WRONLY|O_TRUNC|O_CREAT|O_LARGEFILE, 0644);

        if (!tfw->Open())
        {
            delete tfw;
            tfw = NULL;
        }
        writemode = true;
        return;
    }

    if (read_retries != (uint)-1)
        OpenFile(filename, read_retries);
}

/** \fn check_permissions(const QString&)
 *  \brief Returns false iff file exists and has incorrect permissions.
 *  \param filename File (including path) that we want to know about
 */
bool check_permissions(const QString &filename)
{
    QFileInfo fileInfo(filename);
    if (fileInfo.exists() && !fileInfo.isReadable())
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "File exists but is not readable by MythTV!");
        return false;
    }
    return true;
}

/** \fn RingBuffer::OpenFile(const QString&, uint)
 *  \brief Opens a file for reading.
 *
 *  \param lfilename  Name of file to read
 *  \param retryCount How often to retry reading the file before giving up
 */
void RingBuffer::OpenFile(const QString &lfilename, uint retryCount)
{
    VERBOSE(VB_PLAYBACK, LOC + QString("OpenFile(%1, %1)")
            .arg(lfilename).arg(retryCount));

    uint openAttempts = retryCount + 1;

    filename = lfilename;

    if (remotefile)
    {
        delete remotefile;
    }

    if (fd2 >= 0)
    {
        close(fd2);
        fd2 = -1;
    }

    bool is_local = false;
    bool is_dvd = false;
    (void) is_dvd; // not used when frontend is disabled.

    if ((filename.left(1) == "/") ||
        (QFile::exists(filename)))
        is_local = true;
#ifdef USING_FRONTEND
    else if (filename.left(4) == "dvd:")
    {
        is_dvd = true;
        dvdPriv = new DVDRingBufferPriv();
        startreadahead = false;

        if (filename.left(6) == "dvd://")  // 'Play DVD' sends "dvd:/" + dev
            filename.remove(0,5);          //             e.g. "dvd://dev/sda"
        else                               // Less correct URI "dvd:" + path
            filename.remove(0,4);          //             e.g. "dvd:/videos/ET"

        if (QFile::exists(filename))
            VERBOSE(VB_PLAYBACK, "OpenFile() trying DVD at " + filename);
        else
        {
            filename = "/dev/dvd";
        }
    }
#endif // USING_FRONTEND

    if (is_local)
    {
        char buf[kReadTestSize];
        int timetowait = 500 * openAttempts;
        int lasterror = 0;

        MythTimer openTimer;
        openTimer.start();

        while (openTimer.elapsed() < timetowait)
        {
            lasterror = 0;
            fd2 = open(filename.local8Bit(),
                       O_RDONLY|O_LARGEFILE|O_STREAMING|O_BINARY);
                
            if (fd2 < 0)
            {
                if (!check_permissions(filename))
                    break;

                lasterror = 1;
                usleep(1000);
            }
            else
            {
                int ret = read(fd2, buf, kReadTestSize);
                if (ret != (int)kReadTestSize)
                {
                    lasterror = 2;
                    close(fd2);
                    fd2 = -1;
                    usleep(1000);
                }
                else
                {
                    lseek(fd2, 0, SEEK_SET);
#ifdef HAVE_POSIX_FADVISE
                    posix_fadvise(fd2, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
                    openAttempts = 0;
                    break;
                }
            }
        }

        switch (lasterror)
        {
            case 1:
                VERBOSE(VB_IMPORTANT, LOC +
                        QString("Could not open %1.").arg(filename));
                break;
            case 2:
                VERBOSE(VB_IMPORTANT, LOC +
                        QString("Invalid file (fd %1) when opening '%2'.")
                        .arg(fd2).arg(filename));
                break;
            default:
                break;
        }


        QFileInfo fileInfo(filename);
        if (fileInfo.lastModified().secsTo(QDateTime::currentDateTime()) >
            30 * 60)
        {
            oldfile = true;
        }
    }
#ifdef USING_FRONTEND
    else if (is_dvd)
    {
        dvdPriv->OpenFile(filename);
        readblocksize = DVD_BLOCK_SIZE * 62;
    }
#endif // USING_FRONTEND
    else
    {
        remotefile = new RemoteFile(filename);
        if (!remotefile->isOpen())
        {
            VERBOSE(VB_IMPORTANT,
                    QString("RingBuffer::RingBuffer(): Failed to open remote "
                            "file (%1)").arg(filename));
            delete remotefile;
            remotefile = NULL;
        }
    }

    setswitchtonext = false;
    ateof = false;
    commserror = false;
    numfailures = 0;

    rawbitrate = 4000;
    CalcReadAheadThresh();
}

/** \fn RingBuffer::IsOpen(void) const
 *  \brief Returns true if the file is open for either reading or writing.
 */
bool RingBuffer::IsOpen(void) const
{ 
#ifdef USING_FRONTEND
    return tfw || (fd2 > -1) || remotefile || (dvdPriv && dvdPriv->IsOpen());
#else // if !USING_FRONTEND
    return tfw || (fd2 > -1) || remotefile;
#endif // !USING_FRONTEND
}

/** \fn RingBuffer::~RingBuffer(void)
 *  \brief Shuts down any threads and closes any files.
 */
RingBuffer::~RingBuffer(void)
{
    KillReadAheadThread();

    pthread_rwlock_wrlock(&rwlock);

    if (remotefile)
    {
        delete remotefile;
    }

    if (tfw)
    {
        delete tfw;
        tfw = NULL;
    }

    if (fd2 >= 0)
    {
        close(fd2);
        fd2 = -1;
    }
    
#ifdef USING_FRONTEND
    if (dvdPriv)
    {
        delete dvdPriv;
    }
#endif // USING_FRONTEND    

    pthread_rwlock_unlock(&rwlock);
    pthread_rwlock_destroy(&rwlock);
}

/** \fn RingBuffer::Start(void)
 *  \brief Starts the read-ahead thread.
 *
 *   If this RingBuffer is not in write-mode, the RingBuffer constructor 
 *   was called with a usereadahead of true, and the read-ahead thread
 *   is not already running.
 */
void RingBuffer::Start(void)
{
    if (!writemode && !readaheadrunning && startreadahead)
        StartupReadAheadThread();
}

/** \fn RingBuffer::Reset(bool, bool, bool)
 *  \brief Resets the read-ahead thread and our position in the file
 */
void RingBuffer::Reset(bool full, bool toAdjust, bool resetInternal)
{
    wantseek = true;
    pthread_rwlock_wrlock(&rwlock);
    wantseek = false;
    numfailures = 0;
    commserror = false;
    setswitchtonext = false;

    writepos = 0;
    readpos = (toAdjust) ? (readpos - readAdjust) : 0;
    readAdjust = 0;

    if (full)
        ResetReadAhead(readpos);

    if (resetInternal)
        internalreadpos = readpos;

    pthread_rwlock_unlock(&rwlock);
}

/** \fn RingBuffer::safe_read(int, void*, uint)
 *  \brief Reads data from the file-descriptor.
 *
 *   This will re-read the file forever until the
 *   end-of-file is reached or the buffer is full.
 *
 *  \param fd   File descriptor to read from
 *  \param data Pointer to where data will be written
 *  \param sz   Number of bytes to read
 *  \return Returns number of bytes read
 */
int RingBuffer::safe_read(int fd, void *data, uint sz)
{
    int ret;
    unsigned tot = 0;
    unsigned errcnt = 0;
    unsigned zerocnt = 0;

    if (fd < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Invalid file descriptor in 'safe_read()'");
        return 0;
    }

    if (stopreads)
        return 0;

    while (tot < sz)
    {
        ret = read(fd, (char *)data + tot, sz - tot);
        if (ret < 0)
        {
            if (errno == EAGAIN)
                continue;

            VERBOSE(VB_IMPORTANT,
                    LOC_ERR + "File I/O problem in 'safe_read()'" + ENO);

            errcnt++;
            numfailures++;
            if (errcnt == 3)
                break;
        }
        else if (ret > 0)
        {
            tot += ret;
        }

        if (ret == 0) // EOF returns 0
        {
            if (tot > 0)
                break;

            zerocnt++;

            // 3 second timeout with usleep(60000), 
            // or 0.12 seconds if it's an old, unmodified file.
            if (zerocnt >= ((oldfile) ? 2 : (livetvchain) ? 6 : 40))
            {
                break;
            }
        }
        if (stopreads)
            break;
        if (tot < sz)
            usleep(60000);
    }
    return tot;
}

/** \fn RingBuffer::safe_read(RemoteFile*, void*, uint)
 *  \brief Reads data from the RemoteFile.
 *
 *  \param rf   RemoteFile to read from
 *  \param data Pointer to where data will be written
 *  \param sz   Number of bytes to read
 *  \return Returns number of bytes read
 */
int RingBuffer::safe_read(RemoteFile *rf, void *data, uint sz)
{
    int ret = 0;

    ret = rf->Read(data, sz);
    if (ret < 0)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "RingBuffer::safe_read(RemoteFile* ...): read failed");

        rf->Seek(internalreadpos - readAdjust, SEEK_SET);
        ret = 0;
        numfailures++;
     }

    return ret;
}

/** \fn RingBuffer::UpdateRawBitrate(uint)
 *  \brief Set the raw bit rate, to allow RingBuffer adjust effective bitrate.
 *  \param raw_bitrate Streams average number of kilobits per second when
 *                     playspeed is 1.0
 */
void RingBuffer::UpdateRawBitrate(uint raw_bitrate)
{
    QMutexLocker locker(&bitratelock);
    rawbitrate = raw_bitrate;
    CalcReadAheadThresh();
}

/** \fn RingBuffer::GetBitrate(void) const
 *  \brief Returns effective bits per second (in thousands).
 *
 *   NOTE: This is reported in telecom kilobytes, to get
 *         the bits per second multiply by 1000, not 1024.
 */
uint RingBuffer::GetBitrate(void) const
{
    QMutexLocker locker(&bitratelock);
    uint tmp = (uint) max(abs(rawbitrate * playspeed), 0.5f * rawbitrate);
    return min(rawbitrate * 3, tmp);
}

/** \fn RingBuffer::GetReadBlockSize(void) const
 *  \brief Returns size of each disk read made by read ahead thread (in bytes).
 */
uint RingBuffer::GetReadBlockSize(void) const
{
    QMutexLocker locker(&bitratelock);
    return readblocksize;
}

/** \fn RingBuffer::UpdatePlaySpeed(float)
 *  \brief Set the play speed, to allow RingBuffer adjust effective bitrate.
 *  \param play_speed Speed to set. (1.0 for normal speed)
 */
void RingBuffer::UpdatePlaySpeed(float play_speed)
{
    QMutexLocker locker(&bitratelock);
    playspeed = play_speed;
    CalcReadAheadThresh();
}

/** \fn RingBuffer::CalcReadAheadThresh(void)
 *  \brief Calculates fill_min, fill_threshold, and readblocksize
 *         from the estimated effective bitrate of the stream.
 */
void RingBuffer::CalcReadAheadThresh(void)
{
    uint estbitrate;

    pthread_rwlock_wrlock(&rwlock);
    wantseek       = false;
    readsallowed   = false;
    readblocksize  = CHUNK;

    // loop without sleeping if the buffered data is less than this
    fill_threshold = CHUNK * 2;
    fill_min       = 1;

#ifdef USING_FRONTEND
    if (dvdPriv)
    {
        const uint KB32  =  32*1024;
        const uint KB64  =  64*1024;
        const uint KB128 = 128*1024;
        const uint KB256 = 256*1024;
        const uint KB512 = 512*1024;

        estbitrate     = (uint) max(abs(rawbitrate * playspeed),
                                    0.5f * rawbitrate);
        estbitrate     = min(rawbitrate * 3, estbitrate);
        readblocksize  = (estbitrate > 2500)  ? KB64  : KB32;
        readblocksize  = (estbitrate > 5000)  ? KB128 : readblocksize;
        readblocksize  = (estbitrate > 9000)  ? KB256 : readblocksize;
        readblocksize  = (estbitrate > 18000) ? KB512 : readblocksize;

        // minumum seconds of buffering before allowing read
        float secs_min = 0.1;

        // set the minimum buffering before allowing ffmpeg read
        fill_min        = (uint) ((estbitrate * secs_min) * 0.125f);
        // make this a multiple of ffmpeg block size..
        fill_min        = ((fill_min / KB32) + 1) * KB32;
    }
#endif // USING_FRONTEND    

    pthread_rwlock_unlock(&rwlock);

    VERBOSE(VB_PLAYBACK, LOC +
            QString("CalcReadAheadThresh(%1 KB)\n\t\t\t -> "
                    "threshhold(%2 KB) min read(%3 KB) blk size(%4 KB)")
            .arg(estbitrate).arg(fill_threshold/1024)
            .arg(fill_min/1024).arg(readblocksize/1024));
}

/** \fn RingBuffer::ReadBufFree(void) const
 *  \brief Returns number of bytes available for reading into buffer.
 */
int RingBuffer::ReadBufFree(void) const
{
    QMutexLocker locker(&readAheadLock);
    return ((rbwpos >= rbrpos) ? rbrpos + kBufferSize : rbrpos) - rbwpos - 1;
}

/** \fn RingBuffer::ReadBufAvail(void) const
 *  \brief Returns number of bytes available for reading from buffer.
 */
int RingBuffer::ReadBufAvail(void) const
{
    QMutexLocker locker(&readAheadLock);
    return (rbwpos >= rbrpos) ?
        rbwpos - rbrpos : kBufferSize - rbrpos + rbwpos;
}

/** \fn RingBuffer::ResetReadAhead(long long)
 *  \brief Restart the read-ahead threat at the 'newinternal' position.
 *
 *   This is called after a Seek(long long, int) so that the read-ahead
 *   buffer doesn't contain any stale data, and so that it will read 
 *   any new data from the new position in the file.
 *
 *   WARNING: Must be called with rwlock in write lock state.
 *
 *  \param newinternal Position in file to start reading data from
 */
void RingBuffer::ResetReadAhead(long long newinternal)
{
    readAheadLock.lock();
    readblocksize = CHUNK;
    rbrpos = 0;
    rbwpos = 0;
    internalreadpos = newinternal;
    ateof = false;
    readsallowed = false;
    setswitchtonext = false;
    readAheadLock.unlock();
}

/** \fn RingBuffer::StartupReadAheadThread(void)
 *  \brief Creates the read-ahead thread, and waits for it to start.
 *
 *  \sa Start(void).
 */
void RingBuffer::StartupReadAheadThread(void)
{
    readaheadrunning = false;

    readAheadRunningCondLock.lock();
    pthread_create(&reader, NULL, StartReader, this);
    readAheadRunningCond.wait(&readAheadRunningCondLock);
    readAheadRunningCondLock.unlock();
}

/** \fn RingBuffer::KillReadAheadThread(void)
 *  \brief Stops the read-ahead thread, and waits for it to stop.
 */
void RingBuffer::KillReadAheadThread(void)
{
    if (!readaheadrunning)
        return;

    readaheadrunning = false;
    pthread_join(reader, NULL);
}

/** \fn RingBuffer::StopReads(void)
 *  \brief ????
 *  \sa StartReads(void), Pause(void)
 */
void RingBuffer::StopReads(void)
{
    stopreads = true;
    availWait.wakeAll();
}

/** \fn RingBuffer::StartReads(void)
 *  \brief ????
 *  \sa StopReads(void), Unpause(void)
 */
void RingBuffer::StartReads(void)
{
    stopreads = false;
}

/** \fn RingBuffer::Pause(void)
 *  \brief Pauses the read-ahead thread. Calls StopReads(void).
 *  \sa Unpause(void), WaitForPause(void)
 */
void RingBuffer::Pause(void)
{
    pausereadthread = true;
    StopReads();
}

/** \fn RingBuffer::Unpause(void)
 *  \brief Unpauses the read-ahead thread. Calls StartReads(void).
 *  \sa Pause(void)
 */
void RingBuffer::Unpause(void)
{
    StartReads();
    pausereadthread = false;
}

/** \fn RingBuffer::WaitForPause(void)
 *  \brief Waits for Pause(void) to take effect.
 */
void RingBuffer::WaitForPause(void)
{
    if (!readaheadrunning)
        return;

    if  (!readaheadpaused)
    {
        while (!pauseWait.wait(1000))
            VERBOSE(VB_IMPORTANT,
                    LOC + "Waited too long for ringbuffer pause..");
    }
}

/** \fn RingBuffer::StartReader(void*)
 *  \brief Thunk that calls ReadAheadThread(void)
 */
void *RingBuffer::StartReader(void *type)
{
    RingBuffer *rbuffer = (RingBuffer *)type;
    rbuffer->ReadAheadThread();
    return NULL;
}

/** \fn RingBuffer::ReadAheadThread(void)
 *  \brief Read-ahead run function.
 */
void RingBuffer::ReadAheadThread(void)
{
    long long totfree = 0;
    int ret = -1;
    int used = 0;
    int loops = 0;

    struct timeval lastread, now;
    gettimeofday(&lastread, NULL);
    const int KB640 = 640*1024;
    int readtimeavg = 300;
    int readinterval;

    pausereadthread = false;

    readAheadBuffer = new char[kBufferSize + KB640];

    ResetReadAhead(0);
    totfree = ReadBufFree();

    readaheadrunning = true;
    readAheadRunningCondLock.lock();
    readAheadRunningCond.wakeAll();
    readAheadRunningCondLock.unlock();
    while (readaheadrunning)
    {
        if (pausereadthread || writemode)
        {
            readaheadpaused = true;
            pauseWait.wakeAll();
            usleep(5000);
            totfree = ReadBufFree();
            continue;
        }

        if (readaheadpaused)
        {
            totfree = ReadBufFree();
            readaheadpaused = false;
        }

        if (totfree < readblocksize)
        {
            usleep(50000);
            totfree = ReadBufFree();
            ++loops;
            // break out if we've spent lots of time here, just in case things
            // are waiting on a wait condition that never got triggered.
            if (readsallowed && loops < 10)
                continue;
        }
        loops = 0;

        pthread_rwlock_rdlock(&rwlock);
        if (totfree > readblocksize && !commserror && !ateof && !setswitchtonext)
        {
            // limit the read size
            totfree = readblocksize;

            // adapt blocksize
            gettimeofday(&now, NULL);
            readinterval = (now.tv_sec  - lastread.tv_sec ) * 1000 +
                           (now.tv_usec - lastread.tv_usec) / 1000;

            readtimeavg = (readtimeavg * 9 + readinterval) / 10;

            if (readtimeavg < 200 && readblocksize < KB640)
            {
                readblocksize += CHUNK;
                //VERBOSE(VB_PLAYBACK,
                //    QString("Avg read interval was %1 msec. %2K block size")
                //            .arg(readtimeavg).arg(readblocksize/1024));
                readtimeavg = 300;
            }
            else if (readtimeavg > 400 && readblocksize > CHUNK)
            {
                readblocksize -= CHUNK;
                //VERBOSE(VB_PLAYBACK,
                //    QString("Avg read interval was %1 msec. %2K block size")
                //            .arg(readtimeavg).arg(readblocksize/1024));
                readtimeavg = 300;
            }
            lastread = now;

            if (rbwpos + totfree > kBufferSize)
                totfree = kBufferSize - rbwpos;

            if (internalreadpos == 0)
                totfree = fill_min;

            if (remotefile)
            {
                if (livetvchain && livetvchain->HasNext())
                    remotefile->SetTimeout(true);

                ret = safe_read(remotefile, readAheadBuffer + rbwpos,
                                totfree);
                internalreadpos += ret;
            }
#ifdef USING_FRONTEND
            else if (dvdPriv)
            {                        
                ret = dvdPriv->safe_read(readAheadBuffer + rbwpos, totfree);
                internalreadpos += ret;
            }
#endif // USING_FRONTEND
            else
            {
                ret = safe_read(fd2, readAheadBuffer + rbwpos, totfree);
                internalreadpos += ret;
            }

            readAheadLock.lock();
            if (ret > 0 )
                rbwpos = (rbwpos + ret) % kBufferSize;
            readAheadLock.unlock();

            if (ret == 0 && !stopreads)
            {
                if (livetvchain)
                {
                    if (!setswitchtonext && !ignoreliveeof && 
                        livetvchain->HasNext())
                    {
                        livetvchain->SwitchToNext(true);
                        setswitchtonext = true;
                    }
                }
                else
                    ateof = true;
            }
        }

        if (numfailures > 5)
            commserror = true;

        totfree = ReadBufFree();
        used = kBufferSize - totfree;

        if (ateof || commserror)
        {
            readsallowed = true;
            totfree = 0;
        }

        if (!readsallowed && (used >= fill_min || setswitchtonext))
        {
            readsallowed = true;
            //VERBOSE(VB_PLAYBACK, QString("reads allowed (%1 %2)").arg(used)
            //                                                    .arg(fill_min));
        }
        //else if (!readsallowed)
        //    VERBOSE(VB_PLAYBACK, QString("buffering (%1 %2 %3)").arg(used)
        //                                                        .arg(fill_min)
        //                                                        .arg(ret));

        if (readsallowed && used < fill_min && !ateof && !setswitchtonext)
        {
            readsallowed = false;
            //VERBOSE(VB_GENERAL, QString ("rebuffering (%1 %2)").arg(used)
            //                                                   .arg(fill_min));
        }

        if (readsallowed || stopreads)
            readsAllowedWait.wakeAll();

        availWaitMutex.lock();
        if (commserror || ateof || stopreads || setswitchtonext ||
            (wanttoread <= used && wanttoread > 0))
        {
            availWait.wakeAll();
        }
        availWaitMutex.unlock();

        pthread_rwlock_unlock(&rwlock);

        if ((used >= fill_threshold || wantseek || ateof || setswitchtonext) &&
            !pausereadthread)
        {
            usleep(500);
        }
    }

    delete [] readAheadBuffer;
    readAheadBuffer = NULL;
    rbrpos = 0;
    rbwpos = 0;
}

long long RingBuffer::SetAdjustFilesize(void)
{
    readAdjust += internalreadpos;
    return readAdjust;
}

int RingBuffer::Peek(void *buf, int count)
{
    // really only works with readahead, but DVD doesn't readahead
    if (!readaheadrunning)
    {
        int ret = Read(buf, count);
        Seek(0, SEEK_SET);
        return ret;
    }

    return ReadFromBuf(buf, count, true);
}

/**
 *  \brief Reads from the read-ahead buffer, this is called by
 *         Read(void*, int) when the read-ahead thread is running.
 *  \param buf   Pointer to where data will be written
 *  \param count Number of bytes to read
 *  \param peek  If true, don't increment read count
 *  \return Returns number of bytes read
 */
int RingBuffer::ReadFromBuf(void *buf, int count, bool peek)
{
    if (commserror)
        return 0;

    bool readone = false;
    int readErr = 0;
    
    if (readaheadpaused && stopreads)
    {
        readone = true;
        Unpause();
    }
    else
    {
        while (!readsallowed && !stopreads)
        {
            if (!readsAllowedWait.wait(1000))
            {
                 VERBOSE(VB_IMPORTANT,
                         LOC + "Taking too long to be allowed to read..");
                 readErr++;

                 // HACK Sometimes the readhead thread gets borked on startup.
                 if ((readErr > 4 && readErr % 2) && (rbrpos ==0))
                 {
                    VERBOSE(VB_IMPORTANT, "restarting readhead thread..");
                    KillReadAheadThread();
                    StartupReadAheadThread();
                 }                    
 
                 if (readErr > 10)
                 {
                     VERBOSE(VB_IMPORTANT, LOC_ERR + "Took more than "
                             "10 seconds to be allowed to read, aborting.");
                     wanttoread = 0;
                     stopreads = true;
                     return 0;
                 }
            }
        }
    }
    
    int avail = ReadBufAvail();

    if (ateof && avail < count)
        count = avail;
    
    MythTimer t;
    t.start();
    while (avail < count && !stopreads)
    {
        availWaitMutex.lock();
        wanttoread = count;
        if (!availWait.wait(&availWaitMutex, 250))
        {
            int elapsed = t.elapsed();
            if  (/*((elapsed > 500)  && (elapsed < 750))  ||*/
                 ((elapsed > 1000) && (elapsed < 1250)) ||
                 ((elapsed > 2000) && (elapsed < 2250)) ||
                 ((elapsed > 4000) && (elapsed < 4250)) ||
                 ((elapsed > 8000) && (elapsed < 8250)))
            {
                VERBOSE(VB_IMPORTANT, LOC + "Waited " +
                        QString("%1").arg((elapsed / 500) * 0.5f, 3, 'f', 1) +
                        " seconds for data to become available...");
                if (livetvchain)
                {
                    VERBOSE(VB_IMPORTANT, "Checking to see if there's a "
                                          "new livetv program to switch to..");
                    livetvchain->ReloadAll();
                }
            }

            bool quit = livetvchain && (livetvchain->NeedsToSwitch() || 
                                        livetvchain->NeedsToJump() ||
                                        setswitchtonext);

            if (elapsed > 16000 || quit)
            {
                if (!quit)
                    VERBOSE(VB_IMPORTANT, LOC_ERR + "Waited " +
                            QString("%1").arg(elapsed/1000) +
                            " seconds for data, aborting.");
                else
                    VERBOSE(VB_IMPORTANT, LOC + "Timing out wait due to "
                            "impending livetv switch.");

                ateof = true;
                wanttoread = 0;
                stopreads = true;
                availWaitMutex.unlock();
                return 0;
            }
        }

        wanttoread = 0;
        availWaitMutex.unlock();

        avail = ReadBufAvail();
        if ((ateof || setswitchtonext) && avail < count)
            count = avail;

        if (commserror)
            return 0;
    }

    if ((ateof || stopreads) && avail < count)
        count = avail;

    if (rbrpos + count > (int) kBufferSize)
    {
        int firstsize = kBufferSize - rbrpos;
        int secondsize = count - firstsize;

        memcpy(buf, readAheadBuffer + rbrpos, firstsize);
        memcpy((char *)buf + firstsize, readAheadBuffer, secondsize);
    }
    else
        memcpy(buf, readAheadBuffer + rbrpos, count);

    if (!peek)
    {
        readAheadLock.lock();
        rbrpos = (rbrpos + count) % kBufferSize;
        readAheadLock.unlock();
    }

    if (readone)
    {
        Pause();
        WaitForPause();
    }

    return count;
}

/** \fn RingBuffer::Read(void*, int)
 *  \brief This is the public method for reading from a file,
 *         it calls the appropriate read method if the file
 *         is remote or buffered, or a DVD.
 *  \param buf   Pointer to where data will be written
 *  \param count Number of bytes to read
 *  \return Returns number of bytes read
 */
int RingBuffer::Read(void *buf, int count)
{
    int ret = -1;
    if (writemode)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR +
                "Attempt to read from a write only file");
        return ret;
    }

    pthread_rwlock_rdlock(&rwlock);

    if (!readaheadrunning)
    {
        if (remotefile)
        {
            ret = safe_read(remotefile, buf, count);
            readpos += ret;
        }
#ifdef USING_FRONTEND
        else if (dvdPriv)
        {                        
            ret = dvdPriv->safe_read(buf, count);
            readpos += ret;
        }
#endif // USING_FRONTEND
        else
        {
            ret = safe_read(fd2, buf, count);
            readpos += ret;
        }
    }
    else
    {
        ret = ReadFromBuf(buf, count);
        readpos += ret;
    }

    pthread_rwlock_unlock(&rwlock);
    return ret;
}

/** \fn RingBuffer::IsIOBound(void) const
 *  \brief Returns true if a RingBuffer::Read(void*,int) is likely to block.
 */
bool RingBuffer::IsIOBound(void) const
{
    bool ret = false;
    int used, free;
    pthread_rwlock_rdlock(&rwlock);

    if (!tfw)
    {
        pthread_rwlock_unlock(&rwlock);
        return ret;
    }

    used = tfw->BufUsed();
    free = tfw->BufFree();

    ret = (used * 5 > free);

    pthread_rwlock_unlock(&rwlock);
    return ret;
}

/** \fn RingBuffer::Write(const void*, uint)
 *  \brief Writes buffer to ThreadedFileWriter::Write(const void*,uint)
 *  \return Bytes written, or -1 on error.
 */
int RingBuffer::Write(const void *buf, uint count)
{
    int ret = -1;
    if (!writemode)
    {
        VERBOSE(VB_IMPORTANT, LOC_ERR + "Tried to write to a read only file.");
        return ret;
    }

    if (!tfw)
        return ret;

    pthread_rwlock_rdlock(&rwlock);

    ret = tfw->Write(buf, count);
    writepos += ret;

    pthread_rwlock_unlock(&rwlock);
    return ret;
}

/** \fn RingBuffer::Sync(void)
 *  \brief Calls ThreadedFileWriter::Sync(void)
 */
void RingBuffer::Sync(void)
{
    if (tfw)
        tfw->Sync();
}

/** \fn RingBuffer::Seek(long long, int)
 *  \brief Seeks to a particular position in the file.
 */
long long RingBuffer::Seek(long long pos, int whence)
{
    wantseek = true;
    pthread_rwlock_wrlock(&rwlock);
    wantseek = false;

    // optimize nop seeks
    if ((whence == SEEK_SET && pos == readpos) ||
        (whence == SEEK_CUR && pos == 0))
    {
        pthread_rwlock_unlock(&rwlock);
        return readpos;
    }

    long long ret = -1;
    if (remotefile)
        ret = remotefile->Seek(pos, whence, readpos);
#ifdef USING_FRONTEND
    else if (dvdPriv)
    {
        dvdPriv->NormalSeek(pos);
    }
#endif // USING_FRONTEND
    else
    {
        if (whence == SEEK_SET)
#ifdef USING_MINGW
            ret = lseek64(fd2, pos, whence);
#else
            ret = lseek(fd2, pos, whence);
#endif
        else
        {
            long long realseek = readpos + pos;
#ifdef USING_MINGW
            ret = lseek64(fd2, realseek, SEEK_SET);
#else
            ret = lseek(fd2, realseek, SEEK_SET);
#endif
        }
    }

    if (whence == SEEK_SET)
        readpos = ret;
    else if (whence == SEEK_CUR)
        readpos += pos;

    if (readaheadrunning)
        ResetReadAhead(readpos);

    readAdjust = 0;

    pthread_rwlock_unlock(&rwlock);

    return ret;
}

/** \fn RingBuffer::WriterSeek(long long, int)
 *  \brief Calls ThreadedFileWriter::Seek(long long,int).
 */
long long RingBuffer::WriterSeek(long long pos, int whence)
{
    long long ret = -1;

    if (tfw)
    {
        ret = tfw->Seek(pos, whence);
        writepos = ret;
    }

    return ret;
}

/** \fn RingBuffer::WriterFlush(void)
 *  \brief Calls ThreadedFileWriter::Flush(void) and 
 *         ThreadedFileWriter::Sync(void).
 */
void RingBuffer::WriterFlush(void)
{
    if (tfw)
    {
        tfw->Flush();
        tfw->Sync();
    }
}

/** \fn RingBuffer::SetWriteBufferSize(int)
 *  \brief Calls ThreadedFileWriter::SetWriteBufferSize(int)
 */
void RingBuffer::SetWriteBufferSize(int newSize)
{
    tfw->SetWriteBufferSize(newSize);
}

/** \fn RingBuffer::SetWriteBufferMinWriteSize(int)
 *  \brief Calls ThreadedFileWriter::SetWriteBufferMinWriteSize(int)
 */
void RingBuffer::SetWriteBufferMinWriteSize(int newMinSize)
{
    tfw->SetWriteBufferMinWriteSize(newMinSize);
}

/** \fn RingBuffer::GetReadPosition(void) const
 *  \brief Returns how far into the file we have read.
 */
long long RingBuffer::GetReadPosition(void) const
{
#ifdef USING_FRONTEND
    if (dvdPriv)
        return dvdPriv->GetReadPosition();
#endif // USING_FRONTEND

    return readpos;
}

/** \fn RingBuffer::GetWritePosition(void) const
 *  \brief Returns how far into a ThreadedFileWriter file we have written.
 */
long long RingBuffer::GetWritePosition(void) const
{
    return writepos;
}

/** \fn RingBuffer::GetRealFileSize(void) const
 *  \brief Returns the size of the file we are reading/writing,
 *         or -1 if the query fails.
 */
long long RingBuffer::GetRealFileSize(void) const
{
    if (remotefile)
        return remotefile->GetFileSize();

    struct stat st;
    if (stat(filename.ascii(), &st) == 0)
        return st.st_size;
    return -1;
}

/** \fn RingBuffer::LiveMode(void) const
 *  \brief Returns true if this RingBuffer has been assigned a LiveTVChain.
 *  \sa SetLiveMode(LiveTVChain*)
 */
bool RingBuffer::LiveMode(void) const
{
    return (livetvchain);
}

/** \fn RingBuffer::SetLiveMode(LiveTVChain*)
 *  \brief Assigns a LiveTVChain to this RingBuffer
 *  \sa LiveMode(void)
 */
void RingBuffer::SetLiveMode(LiveTVChain *chain)
{
    livetvchain = chain;
}

bool RingBuffer::InDVDMenuOrStillFrame(void)
{
#ifdef USING_FRONTEND
    if (dvdPriv)
        return (dvdPriv->IsInMenu() || dvdPriv->InStillFrame());
#endif // USING_FRONTEND
    return false;
}

/* vim: set expandtab tabstop=4 shiftwidth=4: */
