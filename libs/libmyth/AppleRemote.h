#ifndef APPLEREMOTE
#define APPLEREMOTE

#include <string>
#include <vector>
#include <map>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/hid/IOHIDLib.h>
#include <IOKit/hid/IOHIDKeys.h>
#include <CoreFoundation/CoreFoundation.h>

class AppleRemote
{
public:
    enum Event {
        VolumePlus = 0,
        VolumeMinus,
        Menu,
        Play,
        Right,
        Left,
        RightHold,
        LeftHold,
        MenuHold,
        PlaySleep,
        ControlSwitched
    };

    class Listener {
    public:
        virtual      ~Listener();
        virtual void appleRemoteButton(Event button, bool pressedDown) = 0;
    };

    static AppleRemote& instance();
    ~AppleRemote();

    bool      isListeningToRemote();
    void      setListener(Listener* listener);
    Listener* listener()                      { return _listener; }
    void      setOpenInExclusiveMode(bool in) { openInExclusiveMode = in; };
    bool      isOpenInExclusiveMode()         { return openInExclusiveMode; };
    void      startListening();
    void      stopListening();
    void      runLoop();

protected:
    AppleRemote(); // will be a singleton class

    static AppleRemote*      _instance;
    static const char* const AppleRemoteDeviceName;
    static const int         REMOTE_SWITCH_COOKIE;


private:
    bool                   openInExclusiveMode;
    IOHIDDeviceInterface** hidDeviceInterface;
    IOHIDQueueInterface**  queue;
    std::vector<int>       cookies;
    std::map< std::string, Event > cookieToButtonMapping;
    int                    remoteId;
    Listener*              _listener;

    void        _initCookieMap();
    io_object_t _findAppleRemoteDevice();
    bool        _initCookies();
    bool        _createDeviceInterface(io_object_t hidDevice);
    bool        _openDevice();

    static void QueueCallbackFunction(void* target, IOReturn result, 
                                      void* refcon, void* sender);
    void        _queueCallbackFunction(IOReturn result,
                                       void* refcon, void* sender);
    void        _handleEventWithCookieString(std::string cookieString,
                                             SInt32 sumOfValues);
};

#endif // APPLEREMOTE
