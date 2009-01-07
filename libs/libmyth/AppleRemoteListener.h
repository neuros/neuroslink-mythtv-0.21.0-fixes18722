#ifndef APPLEREMOTELISTENER
#define APPLEREMOTELISTENER

#include "AppleRemote.h"

#include <iostream>

class AppleRemoteListener: public AppleRemote::Listener
{
public:
    AppleRemoteListener(QObject* mainWindow_);

    // virtual 
    void appleRemoteButton(AppleRemote::Event button, bool pressedDown);

private:
    QObject *mainWindow;
};

#endif // APPLEREMOTELISTENER
