#include "DisplayResX.h"
#include <iostream>
#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "util-x11.h"

#include <X11/extensions/Xrandr.h> // this has to be after util-x11.h (Qt bug)

static XRRScreenConfiguration *GetScreenConfig(Display*& display);

DisplayResX::DisplayResX(void)
{
    Initialize();
}

DisplayResX::~DisplayResX(void)
{
}

bool DisplayResX::GetDisplaySize(int &width_mm, int &height_mm) const
{
    QSize dim = MythXGetDisplayDimensions();
    if ((dim.width() > 0) && (dim.height() > 0))
    {
        width_mm  = dim.width();
        height_mm = dim.height();
        return true;
    }

    return false;
}

bool DisplayResX::SwitchToVideoMode(int width, int height, short desired_rate)
{
    short rate;
    DisplayResScreen desired_screen(width, height, 0, 0, -1.0, desired_rate);
    int idx = DisplayResScreen::FindBestMatch(m_video_modes_unsorted,
                                              desired_screen, rate);
    if (idx >= 0)
    {
        Display *display = NULL;
        XRRScreenConfiguration *cfg = GetScreenConfig(display);
        if (!cfg)
            return false;

        X11L;
        Rotation rot;
        XRRConfigCurrentConfiguration(cfg, &rot);
        
        Window root = DefaultRootWindow(display);
        Status status = XRRSetScreenConfigAndRate(display, cfg, root, idx,
                                                  rot, rate, CurrentTime);
        
        XRRFreeScreenConfigInfo(cfg);
        XCloseDisplay(display);
        X11U;

        if (RRSetConfigSuccess != status)
            cerr<<"DisplaResX: XRRSetScreenConfigAndRate() call failed."<<endl;
        return RRSetConfigSuccess == status;
    }
    cerr<<"DisplaResX: Desired Resolution and FrameRate not found."<<endl;
    return false;
}

const DisplayResVector& DisplayResX::GetVideoModes(void) const
{
    if (m_video_modes.size())
        return m_video_modes;

    Display *display = NULL;
    XRRScreenConfiguration *cfg = GetScreenConfig(display);
    if (!cfg)
        return m_video_modes;

    int num_sizes, num_rates;
    XRRScreenSize *sizes = NULL;
    X11S(sizes = XRRConfigSizes(cfg, &num_sizes));
    for (int i = 0; i < num_sizes; ++i)
    {
        short *rates = NULL;
        X11S(rates = XRRRates(display, DefaultScreen(display), i, &num_rates));
        DisplayResScreen scr(sizes[i].width, sizes[i].height,
                             sizes[i].mwidth, sizes[i].mheight,
                             rates, num_rates);
        m_video_modes.push_back(scr);
    }
    m_video_modes_unsorted = m_video_modes;
    sort(m_video_modes.begin(), m_video_modes.end());

    X11L;
    XRRFreeScreenConfigInfo(cfg);
    XCloseDisplay(display);
    X11U;

    return m_video_modes;
}

static XRRScreenConfiguration *GetScreenConfig(Display*& display)
{
    display = MythXOpenDisplay();
    if (!display)
    {
        cerr<<"DisplaResX: MythXOpenDisplay call failed"<<endl;
        return NULL;
    }

    X11L;

    Window root = RootWindow(display, DefaultScreen(display));

    XRRScreenConfiguration *cfg = NULL;
    int event_basep = 0, error_basep = 0;
    if (XRRQueryExtension(display, &event_basep, &error_basep))
        cfg = XRRGetScreenInfo(display, root);

    if (!cfg)
    {
        if (display)
        {
            XCloseDisplay(display);
            display = NULL;
        }
        cerr<<"DisplaResX: Unable to XRRgetScreenInfo"<<endl;
    }

    X11U;

    return cfg;
}
