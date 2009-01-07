#include "mythcontext.h"
#include "CommDetector2.h"
#include "FrameAnalyzer.h"

using namespace commDetector2;

namespace frameAnalyzer {

bool
rrccinrect(int rr, int cc, int rrow, int rcol, int rwidth, int rheight)
{
    return rr >= rrow && cc >= rcol &&
        rr < rrow + rheight && cc < rcol + rwidth;
}

void
frameAnalyzerReportMap(const FrameAnalyzer::FrameMap *frameMap, float fps,
        const char *comment)
{
    for (FrameAnalyzer::FrameMap::const_iterator ii = frameMap->begin();
            ii != frameMap->end();
            ++ii)
    {
        long long   bb, ee, len;

        /*
         * QMap'd as 0-based index, but display as 1-based index to match "Edit
         * Recording" OSD.
         */
        bb = ii.key() + 1;
        if (ii.data())
        {
            ee = bb + ii.data();
            len = ee - bb;

            VERBOSE(VB_COMMFLAG, QString("%1: %2-%3 (%4-%5, %6)")
                    .arg(comment)
                    .arg(bb, 6).arg(ee - 1, 6)
                    .arg(frameToTimestamp(bb, fps))
                    .arg(frameToTimestamp(ee - 1, fps))
                    .arg(frameToTimestamp(len, fps)));
        }
        else
        {
            VERBOSE(VB_COMMFLAG, QString("%1: %2 (%3)")
                    .arg(comment)
                    .arg(bb, 6)
                    .arg(frameToTimestamp(bb, fps)));
        }
    }
}

void
frameAnalyzerReportMapms(const FrameAnalyzer::FrameMap *frameMap, float fps,
        const char *comment)
{
    for (FrameAnalyzer::FrameMap::const_iterator ii = frameMap->begin();
            ii != frameMap->end();
            ++ii)
    {
        long long   bb, ee, len;

        /*
         * QMap'd as 0-based index, but display as 1-based index to match "Edit
         * Recording" OSD.
         */
        bb = ii.key() + 1;
        if (ii.data())
        {
            ee = bb + ii.data();
            len = ee - bb;

            VERBOSE(VB_COMMFLAG, QString("%1: %2-%3 (%4-%5, %6)")
                    .arg(comment)
                    .arg(bb, 6).arg(ee - 1, 6)
                    .arg(frameToTimestamp(bb, fps))
                    .arg(frameToTimestamp(ee - 1, fps))
                    .arg(frameToTimestampms(len, fps)));
        }
        else
        {
            VERBOSE(VB_COMMFLAG, QString("%1: %2 (%3)")
                    .arg(comment)
                    .arg(bb, 6)
                    .arg(frameToTimestamp(bb, fps)));
        }
    }
}

long long
frameAnalyzerMapSum(const FrameAnalyzer::FrameMap *frameMap)
{
    long long sum = 0;
    for (FrameAnalyzer::FrameMap::const_iterator ii = frameMap->begin();
            ii != frameMap->end();
            ++ii)
        sum += ii.data();
    return sum;
}

bool
removeShortBreaks(FrameAnalyzer::FrameMap *breakMap, float fps, int minbreaklen,
    bool verbose)
{
    /*
     * Remove any breaks that are less than "minbreaklen" units long.
     *
     * Return whether or not any breaks were actually removed.
     */
    FrameAnalyzer::FrameMap::Iterator   bb;
    bool                                removed;

    removed = false;

    /* Don't remove the initial commercial break, no matter how short. */
    bb = breakMap->begin();
    if (bb != breakMap->end() && bb.key() == 0)
        ++bb;
    while (bb != breakMap->end())
    {
        if (bb.data() >= minbreaklen)
        {
            ++bb;
            continue;
        }

        /* Don't remove the final commercial break, no matter how short. */
        FrameAnalyzer::FrameMap::Iterator bb1 = bb;
        ++bb;
        if (bb == breakMap->end())
            continue;

        if (verbose)
        {
            long long start = bb1.key();
            long long end = start + bb1.data() - 1;
            VERBOSE(VB_COMMFLAG, QString("Removing break %1-%2 (%3-%4)")
                .arg(frameToTimestamp(start, fps))
                .arg(frameToTimestamp(end, fps))
                .arg(start + 1).arg(end + 1));
        }
        breakMap->remove(bb1);
        removed = true;
    }

    return removed;
}

bool
removeShortSegments(FrameAnalyzer::FrameMap *breakMap, long long nframes,
    float fps, int minseglen, bool verbose)
{
    /*
     * Remove any segments that are less than "minseglen" units long.
     *
     * Return whether or not any segments were actually removed.
     */
    FrameAnalyzer::FrameMap::Iterator   bb, bbnext;
    bool                                removed;

    removed = false;


    for (bb = breakMap->begin(); bb != breakMap->end(); bb = bbnext)
    {
        /* Never remove initial segment (beginning at frame 0). */
        if (bb == breakMap->begin() && bb != breakMap->end() &&
                bb.key() != 0 && bb.key() < minseglen)
            ++bb;

        bbnext = bb;
        ++bbnext;
        long long brkb = bb.key();
        long long segb = brkb + bb.data();
        long long sege = bbnext == breakMap->end() ? nframes : bbnext.key();
        long long seglen = sege - segb;
        if (seglen >= minseglen)
            continue;

        /* Merge break with next break. */
        if (bbnext == breakMap->end())
        {
            if (segb != nframes)
            {
                /* Extend break "bb" to end of recording. */
                if (verbose)
                {
                    long long old1 = brkb;
                    long long old2 = segb - 1;
                    long long new1 = brkb;
                    long long new2 = nframes - 1;
                    VERBOSE(VB_COMMFLAG,
                        QString("Removing segment %1-%2 (%3-%4)")
                        .arg(frameToTimestamp(segb + 1, fps))
                        .arg(frameToTimestamp(sege + 1, fps))
                        .arg(segb + 1).arg(sege + 1));
                    VERBOSE(VB_COMMFLAG,
                        QString("Replacing break %1-%2 (%3-%4)"
                        " with %5-%6 (%7-%8, EOF)")
                        .arg(frameToTimestamp(old1 + 1, fps))
                        .arg(frameToTimestamp(old2 + 1, fps))
                        .arg(old1 + 1).arg(old2 + 1)
                        .arg(frameToTimestamp(new1 + 1, fps))
                        .arg(frameToTimestamp(new2 + 1, fps))
                        .arg(new1 + 1).arg(new2 + 1));
                }
                breakMap->replace(brkb, nframes - brkb);
                removed = true;
            }
        }
        else
        {
            /* Extend break "bb" to cover "bbnext"; delete "bbnext". */
            if (verbose)
            {
                long long old1 = brkb;
                long long old2 = segb - 1;
                long long new1 = brkb;
                long long new2 = bbnext.key() + bbnext.data() - 1;
                VERBOSE(VB_COMMFLAG,
                    QString("Removing segment %1-%2 (%3-%4)")
                    .arg(frameToTimestamp(segb + 1, fps))
                    .arg(frameToTimestamp(sege + 1, fps))
                    .arg(segb + 1).arg(sege + 1));
                VERBOSE(VB_COMMFLAG,
                    QString("Replacing break %1-%2 (%3-%4)"
                    " with %5-%6 (%7-%8)")
                    .arg(frameToTimestamp(old1 + 1, fps))
                    .arg(frameToTimestamp(old2 + 1, fps))
                    .arg(old1 + 1).arg(old2 + 1)
                    .arg(frameToTimestamp(new1 + 1, fps))
                    .arg(frameToTimestamp(new2 + 1, fps))
                    .arg(new1 + 1).arg(new2 + 1));
            }
            breakMap->replace(brkb, bbnext.key() + bbnext.data() - brkb);

            bb = bbnext;
            ++bbnext;
            if (verbose)
            {
                long long start = bb.key();
                long long end = start + bb.data() - 1;
                VERBOSE(VB_COMMFLAG, QString("Removing break %1-%2 (%3-%4)")
                    .arg(frameToTimestamp(start + 1, fps))
                    .arg(frameToTimestamp(end + 1, fps))
                    .arg(start + 1).arg(end + 1));
            }
            breakMap->remove(bb);

            removed = true;
        }
    }

    return removed;
}

FrameAnalyzer::FrameMap::const_iterator
frameMapSearchForwards(const FrameAnalyzer::FrameMap *frameMap, long long mark,
    long long markend)
{
    /*
     * Search forwards to find the earliest block "block" such that
     *
     *          mark <= blockbegin < markend
     *
     * Return frameMap->constEnd() if there is no such block.
     */
    FrameAnalyzer::FrameMap::const_iterator block = frameMap->constBegin();

    for (; block != frameMap->constEnd(); ++block)
    {
        const long long bb = block.key();
        const long long ee = bb + block.data();
        if (mark < ee)
        {
            if (mark <= bb && bb < markend)
                return block;
            break;
        }
    }
    return frameMap->constEnd();
}

FrameAnalyzer::FrameMap::const_iterator
frameMapSearchBackwards(const FrameAnalyzer::FrameMap *frameMap,
    long long markbegin, long long mark)
{
    /*
     * Search backards to find the latest block "block" such that
     *
     *          markbegin < blockend <= mark
     *
     * Return frameMap->constEnd() if there is no such block.
     */
    FrameAnalyzer::FrameMap::const_iterator block = frameMap->constEnd();
    for (--block; block != frameMap->constBegin(); --block)
    {
        const long long bb = block.key();
        const long long ee = bb + block.data();
        if (bb < mark)
        {
            if (markbegin < ee && ee <= mark)
                return block;
            break;
        }
    }
    return frameMap->constEnd();
}

};  /* namespace */

/* vim: set expandtab tabstop=4 shiftwidth=4: */
