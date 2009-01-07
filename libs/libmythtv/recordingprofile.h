#ifndef RECORDINGPROFILE_H
#define RECORDINGPROFILE_H

#include "libmyth/settings.h"
#include "libmyth/mythdbcon.h"
#include "libmyth/mythwidgets.h"

const QString availProfiles[] =
      {"Default", "Live TV", "High Quality", "Low Quality", "" };

class RecordingProfile;
class VideoCompressionSettings;
class AudioCompressionSettings;

class SelectManagedListItem;

// A parameter associated with the profile itself
class RecordingProfileStorage : public SimpleDBStorage
{
  protected:
    RecordingProfileStorage(Setting *_setting,
                            const RecordingProfile &parentProfile,
                            QString name) :
        SimpleDBStorage(_setting, "recordingprofiles", name),
        parent(parentProfile)
    {
        _setting->setName(name);
    }

    virtual QString whereClause(MSqlBindings& bindings);

    const RecordingProfile &parent;
};

class ImageSize;
class TranscodeResize;
class TranscodeLossless;
class TranscodeFilters;

class MPUBLIC RecordingProfile : public QObject, public ConfigurationWizard
{
  Q_OBJECT
  protected:
    class ID : public AutoIncrementDBSetting {
      public:
        ID():
            AutoIncrementDBSetting("recordingprofiles", "id") {
            setVisible(false);
        };

        // Should never be called because this setting is not visible
        virtual QWidget* configWidget(ConfigurationGroup *cg,
                                      QWidget* parent = NULL,
                                      const char* widgetName = NULL) {
            (void)cg; (void)parent; (void)widgetName;
            return NULL;
        };
    };

    class Name: public LineEditSetting, public RecordingProfileStorage
    {
      public:
        Name(const RecordingProfile &parent):
            LineEditSetting(this, false),
            RecordingProfileStorage(this, parent, "name")
        {
            setEnabled(false);
            setLabel(QObject::tr("Profile name"));
        }

      public slots:
        virtual void setValue(const QString &newValue)
        {
            bool editable = (newValue != "Default") && (newValue != "Live TV");
            setRW(editable);
            setEnabled(editable);

            LineEditSetting::setValue(newValue);
        }
    };

  public:
    // initializers
    RecordingProfile(QString profName = NULL);
    virtual void loadByID(int id);
    virtual bool loadByType(QString name, QString cardtype);
    virtual bool loadByGroup(QString name, QString group);
    virtual DialogCode exec(void);

    // sets
    void setCodecTypes();
    void setName(const QString& newName)
        { name->setValue(newName); }

    // gets
    const ImageSize& getImageSize(void) const { return *imageSize;       }
    int     getProfileNum(void)         const { return id->intValue();   }
    QString getName(void)               const { return name->getValue(); }
    QString groupType(void)             const;

    // static functions
    static QString getName(int id);    
    static void fillSelections(SelectSetting* setting,
                               int group, bool foldautodetect = false);
    static void fillSelections(SelectManagedListItem* setting,
                               int group);                           

    // constants
    static const int TranscoderAutodetect = 0;  ///< sentinel value
    static const int TranscoderGroup = 6;       ///< hard-coded DB value

  private slots:
    void ResizeTranscode(bool resize); 
    void SetLosslessTranscode(bool lossless);
    void FiltersChanged(const QString &val);

  private:
    ID                       *id;
    Name                     *name;
    ImageSize                *imageSize;
    TranscodeResize          *tr_resize;
    TranscodeLossless        *tr_lossless;
    TranscodeFilters         *tr_filters;
    VideoCompressionSettings *videoSettings;
    AudioCompressionSettings *audioSettings;
    QString                   profileName;
    bool                      isEncoder;
};

class RecordingProfileEditor :
    public QObject, public ConfigurationDialog
{
    Q_OBJECT

  public:
    RecordingProfileEditor(int id, QString profName);

    virtual DialogCode exec(void);
    virtual void load();
    virtual void save() { };
    virtual void save(QString /*destination*/) { }

  protected slots:
    void open(int id);

  protected:
    ListBoxSetting *listbox;
    int             group;
    QString         labelName;
};

#endif
