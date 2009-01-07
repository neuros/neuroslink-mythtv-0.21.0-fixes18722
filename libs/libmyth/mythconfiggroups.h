// -*- Mode: c++ -*-

#ifndef MYTH_CONFIG_GROUPS_H
#define MYTH_CONFIG_GROUPS_H

// MythTV headers
#include "mythexp.h"
#include "mythstorage.h"

#define MYTHCONFIG
#include "settings.h"
#undef MYTHCONFIG

class MPUBLIC ConfigurationGroup : public Setting, public Storage
{
    Q_OBJECT

  public:
    ConfigurationGroup(bool luselabel   = true,  bool luseframe  = true,
                       bool lzeroMargin = false, bool lzeroSpace = false) :
        Setting(this),
        uselabel(luselabel),     useframe(luseframe),
        zeroMargin(lzeroMargin), zeroSpace(lzeroSpace)
    {
    }

    virtual void deleteLater(void);

    void addChild(Configurable *child)
    {
        children.push_back(child);
    };

    virtual Setting *byName(const QString &name);

    virtual void load();

    virtual void save();
    virtual void save(QString destination);

    void setUseLabel(bool useit) { uselabel = useit; }
    void setUseFrame(bool useit) { useframe = useit; }

    void setOptions(bool luselabel   = true,  bool luseframe  = true,
                    bool lzeroMargin = false, bool lzeroSpace = false)
    {
        uselabel = luselabel; useframe = luseframe;
        zeroMargin = lzeroMargin; zeroSpace = lzeroSpace;
    }

  signals:
    void changeHelpText(QString);
    
  protected:
    virtual ~ConfigurationGroup();

  protected:
    typedef vector<Configurable*> childList;
    childList children;
    bool uselabel;
    bool useframe;
    bool zeroMargin;
    bool zeroSpace;
};

class MPUBLIC VerticalConfigurationGroup : public ConfigurationGroup
{
  public:
    VerticalConfigurationGroup(
        bool luselabel   = true,  bool luseframe  = true,
        bool lzeroMargin = false, bool lzeroSpace = false) :
        ConfigurationGroup(luselabel, luseframe, lzeroMargin, lzeroSpace),
        widget(NULL), confgrp(NULL), layout(NULL)
    {
    }

    virtual void deleteLater(void);

    virtual QWidget *configWidget(ConfigurationGroup *cg,
                                  QWidget            *parent,
                                  const char         *widgetName);
    virtual void widgetInvalid(QObject *obj);

    bool replaceChild(Configurable *old_child, Configurable *new_child);
    void repaint(void);

  protected:
    /// You need to call deleteLater to delete QObject
    virtual ~VerticalConfigurationGroup() { }

  private:
    vector<QWidget*>    childwidget;
    QGroupBox          *widget;
    ConfigurationGroup *confgrp;
    QVBoxLayout        *layout;
};

class MPUBLIC HorizontalConfigurationGroup : public ConfigurationGroup
{
  public:
    HorizontalConfigurationGroup(
        bool luselabel   = true,  bool luseframe  = true,
        bool lzeroMargin = false, bool lzeroSpace = false) :
        ConfigurationGroup(luselabel, luseframe, lzeroMargin, lzeroSpace)
    {
    }

    virtual QWidget *configWidget(ConfigurationGroup *cg,
                                  QWidget            *parent,
                                  const char         *widgetName);

  protected:
    /// You need to call deleteLater to delete QObject
    virtual ~HorizontalConfigurationGroup() { }
};

class MPUBLIC GridConfigurationGroup : public ConfigurationGroup
{
  public:
    GridConfigurationGroup(uint col,
                           bool uselabel   = true,  bool useframe  = true,
                           bool zeroMargin = false, bool zeroSpace = false) :
        ConfigurationGroup(uselabel, useframe, zeroMargin, zeroSpace), 
        columns(col)
    {
    }

    virtual QWidget *configWidget(ConfigurationGroup *cg,
                                  QWidget            *parent,
                                  const char         *widgetName);

  protected:
    /// You need to call deleteLater to delete QObject
    virtual ~GridConfigurationGroup() { }

  private:
    uint columns;
};

class MPUBLIC StackedConfigurationGroup : public ConfigurationGroup
{
    Q_OBJECT

  public:
    StackedConfigurationGroup(
        bool uselabel   = true,  bool useframe  = true,
        bool zeroMargin = false, bool zeroSpace = false) :
        ConfigurationGroup(uselabel, useframe, zeroMargin, zeroSpace),
        widget(NULL), confgrp(NULL), top(0), saveAll(true)
    {
    }

    virtual void deleteLater(void);

    virtual QWidget *configWidget(ConfigurationGroup *cg, QWidget *parent,
                                  const char *widgetName = 0);
    virtual void widgetInvalid(QObject *obj);

    void raise(Configurable *child);
    virtual void save(void);
    virtual void save(QString destination);

    // save all children, or only the top?
    void setSaveAll(bool b) { saveAll = b; };

    void addChild(Configurable*);
    void removeChild(Configurable*);

  signals:
    void raiseWidget(int);

  protected:
    /// You need to call deleteLater to delete QObject
    virtual ~StackedConfigurationGroup();

  protected:
    vector<QWidget*>    childwidget;
    QWidgetStack       *widget;
    ConfigurationGroup *confgrp;
    uint                top;
    bool                saveAll;
};

class MPUBLIC TriggeredConfigurationGroup : public ConfigurationGroup
{
    Q_OBJECT

  public:
    TriggeredConfigurationGroup(
        bool uselabel         = true,  bool useframe        = true,
        bool zeroMargin       = false, bool zeroSpace       = false,
        bool stack_uselabel   = true,  bool stack_useframe  = true,
        bool stack_zeroMargin = false, bool stack_zeroSpace = false) :
        ConfigurationGroup(uselabel, useframe, zeroMargin, zeroSpace),
        stackUseLabel(stack_uselabel),     stackUseFrame(stack_useframe),
        stackZeroMargin(stack_zeroMargin), stackZeroSpace(stack_zeroSpace),
        isVertical(true),                  isSaveAll(true),
        configLayout(NULL),                configStack(NULL),
        trigger(NULL),                     widget(NULL)
    {
    }

    // Commands

    virtual void addChild(Configurable *child);

    void addTarget(QString triggerValue, Configurable *target);
    void removeTarget(QString triggerValue);

    virtual QWidget *configWidget(ConfigurationGroup *cg, 
                                  QWidget            *parent,
                                  const char         *widgetName);
    virtual void widgetInvalid(QObject *obj);

    virtual Setting *byName(const QString &settingName);

    virtual void load(void);
    virtual void save(void);
    virtual void save(QString destination);

    void repaint(void);

    // Sets

    void SetVertical(bool vert);

    virtual void setSaveAll(bool b)
    {
        if (configStack)
            configStack->setSaveAll(b);
        isSaveAll = b;
    }

    void setTrigger(Configurable *_trigger);

  protected slots:
    virtual void triggerChanged(const QString &value)
    {
        if (configStack)
            configStack->raise(triggerMap[value]);
    }

  protected:
    /// You need to call deleteLater to delete QObject
    virtual ~TriggeredConfigurationGroup() { }
    void VerifyLayout(void);

  protected:
    bool stackUseLabel;
    bool stackUseFrame;
    bool stackZeroMargin;
    bool stackZeroSpace;
    bool isVertical;
    bool isSaveAll;
    ConfigurationGroup          *configLayout;
    StackedConfigurationGroup   *configStack;
    Configurable                *trigger;
    QMap<QString,Configurable*>  triggerMap;
    QWidget                     *widget;
};
    
class MPUBLIC TabbedConfigurationGroup : public ConfigurationGroup
{
    Q_OBJECT

  public:
    TabbedConfigurationGroup() :
        ConfigurationGroup(true, true, false, false) { }

    virtual QWidget *configWidget(ConfigurationGroup *cg,
                                  QWidget            *parent,
                                  const char         *widgetName);
};

class MPUBLIC JumpPane : public VerticalConfigurationGroup
{
    Q_OBJECT

  public:
    JumpPane(const QStringList &labels, const QStringList &helptext);

  signals:
    void pressed(QString);
};

#endif // MYTH_CONFIG_GROUPS_H
