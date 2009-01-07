// -*- Mode: c++ -*-

#ifndef MYTHCONFIG
#include "mythconfigdialogs.h"
#include "mythconfiggroups.h"
#endif // MYTHCONFIG

#ifndef SETTINGS_H
#define SETTINGS_H

// C++ headers
#include <vector>
using namespace std;

// Qt headers
#include <qobject.h>
#include <qstring.h>
#include <qdeepcopy.h>

// MythTV headers
#include "mythexp.h"
#include "mythwidgets.h"
#include "mythdialogs.h"
#include "mythdbcon.h"
#include "mythstorage.h"

class QWidget;
class ConfigurationGroup;
class QDir;
class QWidgetStack;
class Setting;

class MPUBLIC Configurable : public QObject
{
    Q_OBJECT

  public:
    /// Create and return a QWidget for configuring this entity
    /// Note: Any class calling this should call widgetInvalid()
    ///       before configWidget() is called on the class again,
    ///       and before the class is deleted; just before removing
    ///       the instance from a layout or scheduling the delete
    ///       of a parent container is a good time. Some UI classes
    ///       depend on this for properly updating the UI.
    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent,
                                  const char* widgetName = 0);
    /// Tell any Configurable keeping a pointer to a widget,
    /// that the pointer returned by an earlier configWidget
    /// call is invalid.
    /// Note: It is possible that this may be called after 
    ///       configWidget() has been called another time
    ///       so you must check the pointer param.
    virtual void widgetInvalid(QObject*) { }

    // A name for looking up the setting
    void setName(QString str) {
        configName = QDeepCopy<QString>(str);
        if (label == QString::null)
            setLabel(str);
    };
    QString getName(void) const { return QDeepCopy<QString>(configName); };
    virtual Setting* byName(const QString &name) = 0;

    // A label displayed to the user
    void setLabel(QString str) { label = QDeepCopy<QString>(str); }
    QString getLabel(void) const { return QDeepCopy<QString>(label); }
    void setLabelAboveWidget(bool l = true) { labelAboveWidget = l; }

    virtual void setHelpText(const QString &str)
        { helptext = QDeepCopy<QString>(str); }
    QString getHelpText(void) const { return QDeepCopy<QString>(helptext); }

    void setVisible(bool b) { visible = b; };
    bool isVisible(void) const { return visible; };

    virtual void setEnabled(bool b) { enabled = b; }
    bool isEnabled() { return enabled; }

    Storage *GetStorage(void) { return storage; }

  public slots:
    virtual void enableOnSet(const QString &val);
    virtual void enableOnUnset(const QString &val);
    virtual void widgetDeleted(QObject *obj);

  protected:
    Configurable(Storage *_storage) :
        labelAboveWidget(false), enabled(true), storage(_storage),
        configName(""), label(""), helptext(""), visible(true) { }
    virtual ~Configurable() { }

  protected:
    bool labelAboveWidget; 
    bool enabled;
    Storage *storage;
    QString configName;
    QString label;
    QString helptext;
    bool visible;
};

class MPUBLIC Setting : public Configurable
{
    Q_OBJECT

  public:
    // Gets
    bool isChanged(void) const { return changed; }
    virtual QString getValue(void) const;

    // non-const Gets
    virtual Setting *byName(const QString &name)
        { return (name == configName) ? this : NULL; }

    // Sets
    void SetChanged(bool c) { changed = c;       }
    void setUnchanged(void) { SetChanged(false); }
    void setChanged(void)   { SetChanged(true);  }

  public slots:
    virtual void setValue(const QString &newValue);

  signals:
    void valueChanged(const QString&);

  protected:
    Setting(Storage *_storage) : Configurable(_storage), changed(false) {};
    virtual ~Setting() {};

  protected:
    QString settingValue;
    bool    changed;
};

///////////////////////////////////////////////////////////////////////////////

// Read-only display of a setting
class MPUBLIC LabelSetting : public Setting
{
  protected:
    LabelSetting(Storage *_storage) : Setting(_storage) { }
  public:
    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                  const char* widgetName = 0);
};

class MPUBLIC LineEditSetting : public Setting
{
  protected:
    LineEditSetting(Storage *_storage, bool readwrite = true) :
        Setting(_storage), bxwidget(NULL), edit(NULL),
        rw(readwrite), password_echo(false) { }

  public:
    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                  const char* widgetName = 0);
    virtual void widgetInvalid(QObject *obj);

    void setRW(bool readwrite = true)
    {
        rw = readwrite;
        if (edit)
            edit->setRW(rw);
    }

    void setRO(void) { setRW(false); }

    virtual void setEnabled(bool b);
    virtual void setVisible(bool b);
    virtual void SetPasswordEcho(bool b);

    virtual void setHelpText(const QString &str);

  private:
    QWidget      *bxwidget;
    MythLineEdit *edit;
    bool rw;
    bool password_echo;
};

// TODO: set things up so that setting the value as a string emits
// the int signal also
class MPUBLIC IntegerSetting : public Setting
{
    Q_OBJECT

  protected:
    IntegerSetting(Storage *_storage) : Setting(_storage) { }

  public:
    int intValue(void) const {
        return settingValue.toInt();
    };
public slots:
    virtual void setValue(int newValue) {
        Setting::setValue(QString::number(newValue));
        emit valueChanged(newValue);
    };
signals:
    void valueChanged(int newValue);
};

class MPUBLIC BoundedIntegerSetting : public IntegerSetting
{
  protected:
    BoundedIntegerSetting(Storage *_storage, int _min, int _max, int _step) :
        IntegerSetting(_storage), min(_min), max(_max), step(_step) { }

  public:
    virtual void setValue(int newValue);

  protected:
    int min;
    int max;
    int step;
};

class MPUBLIC SliderSetting: public BoundedIntegerSetting {
protected:
    SliderSetting(Storage *_storage, int min, int max, int step) :
        BoundedIntegerSetting(_storage, min, max, step) { }
public:
    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                  const char* widgetName = 0);
};

class MPUBLIC SpinBoxSetting: public BoundedIntegerSetting
{
    Q_OBJECT

  public:
    SpinBoxSetting(Storage *_storage, int min, int max, int step, 
                   bool allow_single_step = false,
                   QString special_value_text = "");

    virtual QWidget *configWidget(ConfigurationGroup *cg, QWidget *parent, 
                                  const char *widgetName = 0);
    virtual void widgetInvalid(QObject *obj);

    virtual void setValue(int newValue);

    void setFocus(void);
    void clearFocus(void);
    bool hasFocus(void) const;

    void SetRelayEnabled(bool enabled) { relayEnabled = enabled; }
    bool IsRelayEnabled(void) const { return relayEnabled; }

    virtual void setHelpText(const QString &str);

  signals:
    void valueChanged(const QString &name, int newValue);

  private slots:
    void relayValueChanged(int newValue);

  private:
    QWidget     *bxwidget;
    MythSpinBox *spinbox;
    bool         relayEnabled;
    bool         sstep;
    QString      svtext;
};

class MPUBLIC SelectSetting : public Setting
{
    Q_OBJECT

  protected:
    SelectSetting(Storage *_storage) :
        Setting(_storage), current(0), isSet(false) { }

  public:
    virtual int  findSelection(  const QString &label,
                                 QString        value  = QString::null) const;
    virtual void addSelection(   const QString &label,
                                 QString        value  = QString::null,
                                 bool           select = false);
    virtual bool removeSelection(const QString &label,
                                 QString        value  = QString::null);

    virtual void clearSelections(void);

    virtual void fillSelectionsFromDir(const QDir& dir, bool absPath=true);

signals:
    void selectionAdded(const QString& label, QString value);
    void selectionRemoved(const QString &label, const QString &value);
    void selectionsCleared(void);

public slots:

    virtual void setValue(const QString& newValue);
    virtual void setValue(int which);

    virtual QString getSelectionLabel(void) const;
    virtual int getValueIndex(QString value);

protected:
    typedef vector<QString> selectionList;
    selectionList labels;
    selectionList values;
    unsigned current;
    bool isSet;
};

class MPUBLIC SelectLabelSetting : public SelectSetting
{
  protected:
    SelectLabelSetting(Storage *_storage) : SelectSetting(_storage) { }

  public:
    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                  const char* widgetName = 0);
};

class MPUBLIC ComboBoxSetting: public SelectSetting {
    Q_OBJECT

protected:
    ComboBoxSetting(Storage *_storage, bool _rw = false, int _step = 1) :
        SelectSetting(_storage), rw(_rw),
        bxwidget(NULL), widget(NULL), step(_step) { }

public:
    virtual void setValue(QString newValue);
    virtual void setValue(int which);

    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                  const char* widgetName = 0);
    virtual void widgetInvalid(QObject *obj);

    void setFocus() { if (widget) widget->setFocus(); }

    virtual void setEnabled(bool b);
    virtual void setVisible(bool b);

    virtual void setHelpText(const QString &str);

public slots:
    void addSelection(const QString &label,
                      QString value = QString::null,
                      bool select = false);
    bool removeSelection(const QString &label,
                         QString value = QString::null);

private:
    bool rw;
    QWidget      *bxwidget;
    MythComboBox *widget;

protected:
    int step;
};

class MPUBLIC ListBoxSetting: public SelectSetting {
    Q_OBJECT
public:
    ListBoxSetting(Storage *_storage) :
        SelectSetting(_storage), bxwidget(NULL), widget(NULL),
        selectionMode(MythListBox::Single) { }

    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                  const char* widgetName = 0);
    virtual void widgetInvalid(QObject *obj);

    void setFocus() { if (widget) widget->setFocus(); }
    void setSelectionMode(MythListBox::SelectionMode mode);
    void setCurrentItem(int i) { if (widget) widget->setCurrentItem(i); }
    void setCurrentItem(const QString& str)  { if (widget) widget->setCurrentItem(str); }
    int currentItem() { if (widget) return widget->currentItem();
                         else return -1; }

    virtual void setEnabled(bool b);

    virtual void clearSelections(void);

    virtual void setHelpText(const QString &str);

signals:
    void accepted(int);
    void menuButtonPressed(int);
    void editButtonPressed(int);
    void deleteButtonPressed(int);

  public slots:
    void addSelection(const QString &label,
                      QString        value  = QString::null,
                      bool           select = false);

    void setValueByIndex(int index);
protected:
    QWidget     *bxwidget;
    MythListBox *widget;
    MythListBox::SelectionMode selectionMode;
};

class MPUBLIC RadioSetting : public SelectSetting
{
public:
    RadioSetting(Storage *_storage) : SelectSetting(_storage) { }
    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                  const char* widgetName = 0);
};

class MPUBLIC ImageSelectSetting: public SelectSetting {
    Q_OBJECT
public:
    ImageSelectSetting(Storage *_storage) :
        SelectSetting(_storage),
        bxwidget(NULL), imagelabel(NULL), combo(NULL),
        m_hmult(1.0f), m_wmult(1.0f) { }
    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, 
                                  const char* widgetName = 0);
    virtual void widgetInvalid(QObject *obj);
    virtual void deleteLater(void);
    virtual void setHelpText(const QString &str);

    virtual void addImageSelection(const QString& label,
                                   QImage* image,
                                   QString value=QString::null,
                                   bool select=false);

protected slots:
    void imageSet(int);

  protected:
    void Teardown(void);
    virtual ~ImageSelectSetting();

protected:
    vector<QImage*> images;
    QWidget *bxwidget;
    QLabel *imagelabel;
    MythComboBox *combo;
    float m_hmult, m_wmult;
};

class MPUBLIC BooleanSetting : public Setting
{
    Q_OBJECT

  public:
    BooleanSetting(Storage *_storage) : Setting(_storage) {}

    bool boolValue(void) const {
        return getValue().toInt() != 0;
    };
public slots:
    virtual void setValue(bool check) {
        if (check)
            Setting::setValue("1");
        else
            Setting::setValue("0");
        emit valueChanged(check);
    };
signals:
    void valueChanged(bool);
};

class MPUBLIC CheckBoxSetting: public BooleanSetting {
public:
    CheckBoxSetting(Storage *_storage) :
        BooleanSetting(_storage), widget(NULL) { }
    virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent,
                                  const char* widgetName = 0);
    virtual void widgetInvalid(QObject*);

    virtual void setEnabled(bool b);

    virtual void setHelpText(const QString &str);

protected:
    MythCheckBox *widget;
};

class MPUBLIC PathSetting : public ComboBoxSetting
{
public:
    PathSetting(Storage *_storage, bool _mustexist):
        ComboBoxSetting(_storage, true), mustexist(_mustexist) { }

    // TODO: this should support globbing of some sort
    virtual void addSelection(const QString& label,
                              QString value=QString::null,
                              bool select=false);

    // Use a combobox for now, maybe a modified file dialog later
    //virtual QWidget* configWidget(ConfigurationGroup *cg, QWidget* parent, const char* widgetName = 0);

protected:
    bool mustexist;
};

class MPUBLIC HostnameSetting : public Setting
{
  public:
    HostnameSetting(Storage *_storage);
};

class MPUBLIC ChannelSetting : public SelectSetting
{
  public:
    ChannelSetting(Storage *_storage) : SelectSetting(_storage)
    {
        setLabel("Channel");
    };

    static void fillSelections(SelectSetting* setting);
    virtual void fillSelections() {
        fillSelections(this);
    };
};

class QDate;
class MPUBLIC DateSetting : public Setting
{
    Q_OBJECT

  public:
    DateSetting(Storage *_storage) : Setting(_storage) { }

    QDate dateValue(void) const;

  public slots:
    void setValue(const QDate& newValue);
};

class QTime;
class MPUBLIC TimeSetting : public Setting
{
    Q_OBJECT

  public:
    TimeSetting(Storage *_storage) : Setting(_storage) { }
    QTime timeValue(void) const;

  public slots:
    void setValue(const QTime& newValue);
};

class MPUBLIC AutoIncrementDBSetting :
    public IntegerSetting, public DBStorage
{
  public:
    AutoIncrementDBSetting(QString _table, QString _column) :
        IntegerSetting(this), DBStorage(this, _table, _column)
    {
        setValue(0);
    }

    virtual void load() { };
    virtual void save();
    virtual void save(QString destination);
};

class MPUBLIC ButtonSetting: public Setting
{
    Q_OBJECT

  public:
    ButtonSetting(Storage *_storage, QString _name = "button") :
        Setting(_storage), name(QDeepCopy<QString>(_name)), button(NULL) { }

    virtual QWidget* configWidget(ConfigurationGroup* cg, QWidget* parent,
                                  const char* widgetName=0);
    virtual void widgetInvalid(QObject *obj);

    virtual void setEnabled(bool b);

    virtual void setHelpText(const QString &);

signals:
    void pressed();
    void pressed(QString name);

protected slots:
    void SendPressedString();

protected:
    QString name;
    MythPushButton *button;
};

class MPUBLIC ProgressSetting : public IntegerSetting
{
  public:
    ProgressSetting(Storage *_storage, int _totalSteps) :
        IntegerSetting(_storage), totalSteps(_totalSteps) { }

    QWidget* configWidget(ConfigurationGroup* cg, QWidget* parent,
                          const char* widgetName = 0);

private:
    int totalSteps;
};

///////////////////////////////////////////////////////////////////////////////

class MPUBLIC TransButtonSetting :
    public ButtonSetting, public TransientStorage
{
  public:
    TransButtonSetting(QString name = "button") :
        ButtonSetting(this, name), TransientStorage() { }
};

class MPUBLIC TransLabelSetting :
    public LabelSetting, public TransientStorage
{
  public:
    TransLabelSetting() : LabelSetting(this), TransientStorage() { }
};

class MPUBLIC TransLineEditSetting :
    public LineEditSetting, public TransientStorage
{
  public:
    TransLineEditSetting(bool rw = true) :
        LineEditSetting(this, rw), TransientStorage() { }
};

class MPUBLIC TransCheckBoxSetting :
    public CheckBoxSetting, public TransientStorage
{
  public:
    TransCheckBoxSetting() : CheckBoxSetting(this), TransientStorage() { }
};

class MPUBLIC TransComboBoxSetting :
    public ComboBoxSetting, public TransientStorage
{
  public:
    TransComboBoxSetting(bool rw = false, int _step = 1) :
        ComboBoxSetting(this, rw, _step), TransientStorage() { }
};

class MPUBLIC TransSpinBoxSetting :
    public SpinBoxSetting, public TransientStorage
{
  public:
    TransSpinBoxSetting(int minv, int maxv, int step,
                        bool allow_single_step = false,
                        QString special_value_text = "") :
        SpinBoxSetting(this, minv, maxv, step,
                       allow_single_step, special_value_text) { }
};

class MPUBLIC TransListBoxSetting :
    public ListBoxSetting, public TransientStorage
{
  public:
    TransListBoxSetting() : ListBoxSetting(this), TransientStorage() { }
};


///////////////////////////////////////////////////////////////////////////////

class MPUBLIC HostSlider : public SliderSetting, public HostDBStorage
{
  public:
    HostSlider(const QString &name, int min, int max, int step) :
        SliderSetting(this, min, max, step),
        HostDBStorage(this, name) { }
};

class MPUBLIC HostSpinBox: public SpinBoxSetting, public HostDBStorage
{
  public:
    HostSpinBox(const QString &name, int min, int max, int step, 
                  bool allow_single_step = false) :
        SpinBoxSetting(this, min, max, step, allow_single_step),
        HostDBStorage(this, name) { }
};

class MPUBLIC HostCheckBox : public CheckBoxSetting, public HostDBStorage
{
  public:
    HostCheckBox(const QString &name) :
        CheckBoxSetting(this), HostDBStorage(this, name) { }
    virtual ~HostCheckBox() { ; }
};

class MPUBLIC HostComboBox : public ComboBoxSetting, public HostDBStorage
{
  public:
    HostComboBox(const QString &name, bool rw = false) :
        ComboBoxSetting(this, rw), HostDBStorage(this, name) { }
    virtual ~HostComboBox() { ; }
};

class MPUBLIC HostRefreshRateComboBox : public HostComboBox
{
    Q_OBJECT
  public:
    HostRefreshRateComboBox(const QString &name, bool rw = false) :
        HostComboBox(name, rw) { }
    virtual ~HostRefreshRateComboBox() { ; }

  public slots:
    virtual void ChangeResolution(const QString& resolution);

  private:
    static const vector<short> GetRefreshRates(const QString &resolution);
};

class MPUBLIC HostTimeBox : public ComboBoxSetting, public HostDBStorage
{
  public:
    HostTimeBox(const QString &name, const QString &defaultTime = "00:00",
                const int interval = 1) :
        ComboBoxSetting(this, false, 30 / interval),
        HostDBStorage(this, name)
    {
        int hour;
        int minute;
        QString timeStr;

        for (hour = 0; hour < 24; hour++)
        {
            for (minute = 0; minute < 60; minute += interval)
            {
                timeStr = timeStr.sprintf("%02d:%02d", hour, minute);
                addSelection(timeStr, QDeepCopy<QString>(timeStr),
                             timeStr == defaultTime);
            }
        }
    }
};

class MPUBLIC HostLineEdit: public LineEditSetting, public HostDBStorage
{
  public:
    HostLineEdit(const QString &name, bool rw = true) :
        LineEditSetting(this, rw), HostDBStorage(this, name) { }
};

class MPUBLIC HostImageSelect : public ImageSelectSetting, public HostDBStorage
{
  public:
    HostImageSelect(const QString &name) :
        ImageSelectSetting(this), HostDBStorage(this, name) { }
};

///////////////////////////////////////////////////////////////////////////////

class MPUBLIC GlobalSlider : public SliderSetting, public GlobalDBStorage
{
  public:
    GlobalSlider(const QString &name, int min, int max, int step) :
        SliderSetting(this, min, max, step), GlobalDBStorage(this, name) { }
};

class MPUBLIC GlobalSpinBox : public SpinBoxSetting, public GlobalDBStorage
{
  public:
    GlobalSpinBox(const QString &name, int min, int max, int step,
                   bool allow_single_step = false) :
        SpinBoxSetting(this, min, max, step, allow_single_step),
        GlobalDBStorage(this, name) { }
};

class MPUBLIC GlobalCheckBox : public CheckBoxSetting, public GlobalDBStorage
{
  public:
    GlobalCheckBox(const QString &name) :
        CheckBoxSetting(this), GlobalDBStorage(this, name) { }
};

class MPUBLIC GlobalComboBox : public ComboBoxSetting, public GlobalDBStorage
{
  public:
    GlobalComboBox(const QString &name, bool rw = false) :
        ComboBoxSetting(this, rw), GlobalDBStorage(this, name) { }
};

class MPUBLIC GlobalLineEdit : public LineEditSetting, public GlobalDBStorage
{
  public:
    GlobalLineEdit(const QString &name, bool rw = true) :
        LineEditSetting(this, rw), GlobalDBStorage(this, name) { }
};

class MPUBLIC GlobalImageSelect :
   public ImageSelectSetting, public GlobalDBStorage
{
  public:
    GlobalImageSelect(const QString &name) :
        ImageSelectSetting(this), GlobalDBStorage(this, name) { }
};

class MPUBLIC GlobalTimeBox : public ComboBoxSetting, public GlobalDBStorage
{
  public:
    GlobalTimeBox(const QString &name, const QString &defaultTime = "00:00",
                  const int interval = 1) :
        ComboBoxSetting(this, false, 30 / interval),
        GlobalDBStorage(this, name)
    {
        int hour;
        int minute;
        QString timeStr;

        for (hour = 0; hour < 24; hour++)
        {
            for (minute = 0; minute < 60; minute += interval)
            {
                timeStr = timeStr.sprintf("%02d:%02d", hour, minute);
                addSelection(timeStr, QDeepCopy<QString>(timeStr),
                             timeStr == defaultTime);
            }
        }
    }
};

#endif // SETTINGS_H
