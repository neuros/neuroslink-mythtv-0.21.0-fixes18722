#ifndef MYTHTHEMEDMENU_H_
#define MYTHTHEMEDMENU_H_

#include "mythscreentype.h"

class MythMainWindow;
class MythThemedMenuPrivate;
class MythThemedMenuState;

/// \brief Themed menu class, used for main menus in %MythTV frontend
class MythThemedMenu : public MythScreenType
{
    Q_OBJECT
  public:
    MythThemedMenu(const char *cdir, const char *menufile,
                   MythScreenStack *parent, const char *name, 
                   bool allowreorder = true, MythThemedMenuState *state = NULL);
   ~MythThemedMenu();

    bool foundTheme(void);

    void setCallback(void (*lcallback)(void *, QString &), void *data);
    void setKillable(void);

    QString getSelection(void);

    void ReloadTheme(void);
    void ReloadExitKey(void);
    virtual void aboutToShow(void);

  protected:
    virtual bool keyPressEvent(QKeyEvent *e);
    virtual void gestureEvent(MythUIType *origtype, MythGestureEvent *ge);

  private:
    void Init(const char *cdir, const char *menufile);

    MythThemedMenuPrivate *d;
};

#endif
