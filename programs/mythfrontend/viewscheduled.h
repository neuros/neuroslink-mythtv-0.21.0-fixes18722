#ifndef VIEWSCHEDULED_H_
#define VIEWSCHEDULED_H_

#include <qdatetime.h>
#include <qdom.h>
#include "mythwidgets.h"
#include "mythdialogs.h"
#include "uitypes.h"
#include "xmlparse.h"
#include "programinfo.h"

class TV;
class Timer;

class ViewScheduled : public MythDialog
{
    Q_OBJECT
  public:
    ViewScheduled(MythMainWindow *parent, const char *name = 0,
                    TV *player = NULL, bool showTV = false);
    ~ViewScheduled();
    static void * RunViewScheduled(void *player, bool);

  protected slots:
    void edit();
    void customEdit();
    void remove();
    void upcoming();
    void details();
    void selected();
    void cursorDown(bool page = false);
    void cursorUp(bool page = false);
    void pageDown() { cursorDown(true); }
    void pageUp() { cursorUp(true); }

  protected:
    void paintEvent(QPaintEvent *);
    void keyPressEvent(QKeyEvent *e);
    void customEvent(QCustomEvent *e);

  private:
    void FillList(void);
    void setShowAll(bool all);
    void viewCards(void);
    void viewInputs(void);

    void updateBackground(void);
    void updateList(QPainter *);
    void updateConflict(QPainter *);
    void updateShowLevel(QPainter *);
    void updateInfo(QPainter *);
    void updateRecStatus(QPainter *);

    void LoadWindow(QDomElement &);
    void parseContainer(QDomElement &);
    void EmbedTVWindow(void);
    XMLParse *theme;
    QDomElement xmldata;

    QPixmap myBackground;

    bool conflictBool;
    QDate conflictDate;
    QString dateformat;
    QString timeformat;
    QString channelFormat;

    QRect listRect;
    QRect infoRect;
    QRect conflictRect;
    QRect showLevelRect;
    QRect recStatusRect;
    QRect fullRect;
    QRect tvRect;

    int listsize;

    bool showAll;

    bool inEvent;
    bool inFill;
    bool needFill;

    int listPos;
    ProgramList recList;

    QMap<int, int> cardref;
    int maxcard;
    int curcard;

    QMap<int, int> inputref;
    int maxinput;
    int curinput;

    TV *m_player;
};

#endif
