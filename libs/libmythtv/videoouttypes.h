// -*- Mode: c++ -*-

#ifndef _VIDEOOUT_TYPES_H_
#define _VIDEOOUT_TYPES_H_

#include <qdeepcopy.h>
#include <qstring.h>
#include <qobject.h>

typedef enum PIPLocation
{
    kPIPTopLeft = 0,
    kPIPBottomLeft,
    kPIPTopRight,
    kPIPBottomRight,
    kPIP_END
} PIPLocation;

typedef enum ZoomDirection
{
    kZoomHome = 0,
    kZoomIn,
    kZoomOut,
    kZoomUp,
    kZoomDown,
    kZoomLeft,
    kZoomRight,
    kZoomAspectUp,
    kZoomAspectDown,
} ZoomDirection;

typedef enum AspectOverrideMode
{
    kAspect_Toggle = -1,
    kAspect_Off = 0,
    kAspect_4_3,
    kAspect_16_9,
    kAspect_14_9, // added after 16:9 so as not to upset existing setups.
    kAspect_END
} AspectOverrideMode;

typedef enum AdjustFillMode
{
    kAdjustFill_Toggle = -1,
    kAdjustFill_Off = 0,
    kAdjustFill_Half,
    kAdjustFill_Full,
    kAdjustFill_Stretch,
    kAdjustFill_END
} AdjustFillMode;

typedef enum LetterBoxColour
{
    kLetterBoxColour_Toggle = -1,
    kLetterBoxColour_Black = 0,
    kLetterBoxColour_Gray25,
    kLetterBoxColour_END
} LetterBoxColour;

typedef enum FrameScanType
{
    kScan_Ignore       = -1,
    kScan_Detect       =  0,
    kScan_Interlaced   =  1, // == XVMC_TOP_PICTURE
    kScan_Intr2ndField =  2, // == XVMC_BOTTOM_PICTURE
    kScan_Progressive  =  3, // == XVMC_FRAME_PICTURE
} FrameScanType;

typedef enum PictureAttribute
{
    kPictureAttribute_None = 0,
    kPictureAttribute_MIN = 1,
    kPictureAttribute_Brightness = 1,
    kPictureAttribute_Contrast,
    kPictureAttribute_Colour,
    kPictureAttribute_Hue,
    kPictureAttribute_Volume,
    kPictureAttribute_MAX
} PictureAttribute;

typedef enum PictureAttributeSupported
{
    kPictureAttributeSupported_None       = 0x00,
    kPictureAttributeSupported_Brightness = 0x01,
    kPictureAttributeSupported_Contrast   = 0x02,
    kPictureAttributeSupported_Colour     = 0x04,
    kPictureAttributeSupported_Hue        = 0x08,
    kPictureAttributeSupported_Volume     = 0x10,
} PictureAttributeSupported;

inline bool is_interlaced(FrameScanType scan)
{
    return (kScan_Interlaced == scan) || (kScan_Intr2ndField == scan);
}

inline bool is_progressive(FrameScanType scan)
{
    return (kScan_Progressive == scan);
}

inline QString toString(FrameScanType scan, bool brief = false)
{
    QString ret = QObject::tr("Unknown");
    switch (scan)
    {
        case kScan_Ignore:
            ret = QObject::tr("Ignore"); break;
        case kScan_Detect:
            ret = QObject::tr("Detect"); break;
        case kScan_Interlaced:
            if (brief)
                ret = QObject::tr("Interlaced");
            else
                ret = QObject::tr("Interlaced (Normal)");
            break;
        case kScan_Intr2ndField:
            if (brief)
                ret = QObject::tr("Interlaced");
            else
                ret = QObject::tr("Interlaced (Reversed)");
            break;
        case kScan_Progressive:
            ret = QObject::tr("Progressive"); break;
        default:
            break;
    }
    return QDeepCopy<QString>(ret);
}

inline QString toString(PIPLocation location)
{
    QString ret = QString::null;
    switch (location)
    {
        case kPIPTopLeft:     ret = QObject::tr("Top Left");     break;
        case kPIPBottomLeft:  ret = QObject::tr("Bottom Left");  break;
        case kPIPTopRight:    ret = QObject::tr("Top Right");    break;
        case kPIPBottomRight: ret = QObject::tr("Bottom Right"); break;
        case kPIP_END: break;
    }
    return QDeepCopy<QString>(ret);
}

inline QString toString(AspectOverrideMode aspectmode) 
{ 
    QString ret = QObject::tr("Off"); 
    switch (aspectmode)
    {
        case kAspect_4_3:  ret = QObject::tr("4:3");  break;
        case kAspect_14_9: ret = QObject::tr("14:9"); break; 
        case kAspect_16_9: ret = QObject::tr("16:9"); break;
        case kAspect_Toggle:
        case kAspect_Off:
        case kAspect_END: break;
    }
    return QDeepCopy<QString>(ret);
}

inline QString toString(LetterBoxColour letterboxcolour)
{ 
    QString ret = QObject::tr("Black");
    switch (letterboxcolour)
    {
        case kLetterBoxColour_Gray25: ret = QObject::tr("Gray"); break;
        case kLetterBoxColour_Black:
        case kLetterBoxColour_Toggle:
        case kLetterBoxColour_END: break;
    }
    return QDeepCopy<QString>(ret);
}

inline QString toXString(LetterBoxColour letterboxcolour)
{ 
    QString ret = "gray0";
    switch (letterboxcolour)
    {
        case kLetterBoxColour_Gray25: ret = "gray25"; break;
        case kLetterBoxColour_Black:
        case kLetterBoxColour_Toggle:
        case kLetterBoxColour_END: break;
    }
    return QDeepCopy<QString>(ret);
}

inline float get_aspect_override(AspectOverrideMode aspectmode, float orig)
{
    float ret = orig;
    switch (aspectmode)
    {
        case kAspect_4_3:  ret = 4.0f  / 3.0f; break;
        case kAspect_14_9: ret = 14.0f / 9.0f; break; 
        case kAspect_16_9: ret = 16.0f / 9.0f; break;
        case kAspect_Toggle:
        case kAspect_Off:
        case kAspect_END: break;
    }
    return ret;
}

inline QString toString(AdjustFillMode aspectmode) 
{ 
    QString ret = QObject::tr("Off");
    switch (aspectmode)
    {
        case kAdjustFill_Half:    ret = QObject::tr("Half");    break;
        case kAdjustFill_Full:    ret = QObject::tr("Full");    break;
        case kAdjustFill_Stretch: ret = QObject::tr("Stretch"); break;
        case kAdjustFill_Toggle:
        case kAdjustFill_Off:
        case kAdjustFill_END: break;
    }
    return QDeepCopy<QString>(ret);
}

inline QString toString(PictureAttribute pictureattribute)
{
    QString ret = QObject::tr("None");
    switch (pictureattribute)
    {
      case kPictureAttribute_None: break;
      case kPictureAttribute_Brightness:
          ret = QObject::tr("Brightness"); break;
      case kPictureAttribute_Contrast:
          ret = QObject::tr("Contrast");   break;
      case kPictureAttribute_Colour:
          ret = QObject::tr("Color");      break;
      case kPictureAttribute_Hue:
          ret = QObject::tr("Hue");        break;
      case kPictureAttribute_Volume:
          ret = QObject::tr("Volume");     break;
      case kPictureAttribute_MAX:
          ret = "MAX";                     break;
    }
    return QDeepCopy<QString>(ret);
}

inline QString toDBString(PictureAttribute pictureattribute)
{
    QString ret = QString::null;
    switch (pictureattribute)
    {
      case kPictureAttribute_None: break;
      case kPictureAttribute_Brightness:
          ret = "brightness"; break;
      case kPictureAttribute_Contrast:
          ret = "contrast";   break;
      case kPictureAttribute_Colour:
          ret = "colour";     break;
      case kPictureAttribute_Hue:
          ret = "hue";        break;
      case kPictureAttribute_Volume:
      case kPictureAttribute_MAX: break;
    }

    if (ret.isEmpty())
        return QString::null;

    return QDeepCopy<QString>(ret);
}

inline QString toXVString(PictureAttribute pictureattribute)
{
    QString ret = QString::null;
    switch (pictureattribute)
    {
      case kPictureAttribute_None: break;
      case kPictureAttribute_Brightness:
          ret = "XV_BRIGHTNESS"; break;
      case kPictureAttribute_Contrast:
          ret = "XV_CONTRAST";   break;
      case kPictureAttribute_Colour:
          ret = "XV_SATURATION"; break;
      case kPictureAttribute_Hue:
          ret = "XV_HUE";        break;
      case kPictureAttribute_Volume:
      case kPictureAttribute_MAX: break;
    }

    if (ret.isEmpty())
        return QString::null;

    return QDeepCopy<QString>(ret);
}

inline QString toString(PictureAttributeSupported supported)
{
    QString ret = "";

    if (kPictureAttributeSupported_Brightness & supported)
        ret += "Brightness, ";
    if (kPictureAttributeSupported_Contrast & supported)
        ret += "Contrast, ";
    if (kPictureAttributeSupported_Colour & supported)
        ret += "Colour, ";
    if (kPictureAttributeSupported_Hue & supported)
        ret += "Hue, ";
    if (kPictureAttributeSupported_Volume & supported)
        ret += "Volume, ";

    return (ret.isEmpty()) ? "" : ret.left(ret.length() - 2);
}

inline PictureAttributeSupported toMask(PictureAttribute pictureattribute)
{
    PictureAttributeSupported ret = kPictureAttributeSupported_None;
    switch (pictureattribute)
    {
        case kPictureAttribute_None: break;
        case kPictureAttribute_Brightness:
            ret = kPictureAttributeSupported_Brightness; break;
        case kPictureAttribute_Contrast:
            ret = kPictureAttributeSupported_Contrast; break;
        case kPictureAttribute_Colour:
            ret = kPictureAttributeSupported_Colour; break;
        case kPictureAttribute_Hue:
            ret = kPictureAttributeSupported_Hue; break;
        case kPictureAttribute_Volume:
            ret = kPictureAttributeSupported_Volume; break;
        case kPictureAttribute_MAX: break;
    }
    return ret;
}

inline PictureAttribute next(PictureAttributeSupported supported,
                             PictureAttribute          attribute)
{
    int i = ((int) attribute + 1) % (int) kPictureAttribute_MAX;
    for (int j = 0; j < kPictureAttribute_MAX;
         (i = (i+1) % kPictureAttribute_MAX), j++)
    {
        if (toMask((PictureAttribute) i) & supported)
            return (PictureAttribute) i;
    }
    return kPictureAttribute_None;
}

#endif // _VIDEOOUT_TYPES_H_
