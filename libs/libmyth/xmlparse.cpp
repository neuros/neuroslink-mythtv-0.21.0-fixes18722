#include <iostream>
using namespace std;

#include <cmath>
#include <cstdlib>

#include <qapplication.h>

#include "uilistbtntype.h"
#include "xmlparse.h"

#ifdef USING_MINGW
#undef LoadImage
#endif

MPUBLIC QMap<QString, fontProp> globalFontMap;

XMLParse::XMLParse(void)
{
    allTypes = new vector<LayerSet *>;
}

XMLParse::~XMLParse()
{
    vector<LayerSet *>::iterator i = allTypes->begin();
    for (; i != allTypes->end(); i++)
    {
        LayerSet *type = (*i);
        if (type)
            delete type;
    }
    delete allTypes;
}

bool XMLParse::LoadTheme(QDomElement &ele, QString winName, QString specialfile)
{
    usetrans = gContext->GetNumSetting("PlayBoxTransparency", 1);

    fontSizeType = gContext->GetSetting("ThemeFontSizeType", "default");

    QValueList<QString> searchpath = gContext->GetThemeSearchPath();
    for (QValueList<QString>::const_iterator ii = searchpath.begin();
        ii != searchpath.end(); ii++)
    {
        QString themefile = *ii + specialfile + "ui.xml";
        if (doLoadTheme(ele, winName, themefile))
        {
            VERBOSE(VB_GENERAL, "XMLParse::LoadTheme using " << themefile);
            return true;
        }
    }
    
    return false;
}

bool XMLParse::doLoadTheme(QDomElement &ele, QString winName, QString themeFile)
{
    QDomDocument doc;
    QFile f(themeFile);

    if (!f.open(IO_ReadOnly))
    {    
        //cerr << "XMLParse::LoadTheme(): Can't open: " << themeFile << endl;
        return false;
    }
     
    QString errorMsg;
    int errorLine = 0;
    int errorColumn = 0;

    if (!doc.setContent(&f, false, &errorMsg, &errorLine, &errorColumn))
    {
        cerr << "Error parsing: " << themeFile << endl;
        cerr << "at line: " << errorLine << "  column: " << errorColumn << endl;
        cerr << errorMsg << endl;
        f.close();
        return false;
    }

    f.close();

    QDomElement docElem = doc.documentElement();
    QDomNode n = docElem.firstChild();
    while (!n.isNull())
    {
        QDomElement e = n.toElement();
        if (!e.isNull())
        {
            if (e.tagName() == "window")
            {
                QString name = e.attribute("name", "");
                if (name.isNull() || name.isEmpty())
                {
                    cerr << "Window needs a name\n";
                    return false;
                }

                if (name == winName)
                {
                    ele = e;
                    return true;
                }
            }
            else
            {
                cerr << "Unknown element: " << e.tagName() << endl;
                return false;
            }
        }
        n = n.nextSibling();
    }

    return false;
}

QString XMLParse::getFirstText(QDomElement &element)
{
    for (QDomNode dname = element.firstChild(); !dname.isNull();
         dname = dname.nextSibling())
    {
        QDomText t = dname.toText();
        if (!t.isNull())
            return t.data();
    }
    return "";
}

void XMLParse::parseFont(QDomElement &element)
{
    QString name;
    QString face;
    QString bold;
    QString ital;
    QString under;
    
    int size = -1;
    int sizeSmall = -1;
    int sizeBig = -1;
    QPoint shadowOffset = QPoint(0, 0);
    QString color = "#ffffff";
    QString dropcolor = "#000000";
    QString hint;
    QFont::StyleHint styleHint = QFont::Helvetica;    
    
    bool haveSizeSmall = false;
    bool haveSizeBig = false;
    bool haveSize = false;
    bool haveFace = false;
    bool haveColor = false;
    bool haveDropColor = false;
    bool haveBold = false;
    bool haveShadow = false;
    bool haveItal = false;
    bool haveUnder = false;
    
    fontProp *baseFont = NULL;
    
    name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Font needs a name\n";
        return;
    }

    QString base =  element.attribute("base", "");
    if (!base.isNull() && !base.isEmpty())
    {
        baseFont = GetFont(base);
        if (!baseFont)
        {
            cerr << "Specified base font '" << base << "'  does not exist for font " << face << endl;
            return;
        }
    }
    
    face = element.attribute("face", "");
    if (face.isNull() || face.isEmpty())
    {
        if (!baseFont)
        {
            cerr << "Font needs a face\n";
            return;
        }
    }
    else
    {
        haveFace = true;
    }
    
    hint = element.attribute("stylehint", "");
    if (!hint.isNull() && !hint.isEmpty())
    {
        styleHint = (QFont::StyleHint)hint.toInt();
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "size")
            {
                haveSize = true;
                size = getFirstText(info).toInt();
            }
            else if (info.tagName() == "size:small")
            {
                haveSizeSmall = true;
                sizeSmall = getFirstText(info).toInt();
            }
            else if (info.tagName() == "size:big")
            {
                haveSizeBig = true;
                sizeBig = getFirstText(info).toInt();
            }
            else if (info.tagName() == "color")
            {
                haveColor = true;
                color = getFirstText(info);
            }
            else if (info.tagName() == "dropcolor")
            {
                haveDropColor = true;
                dropcolor = getFirstText(info);
            }
            else if (info.tagName() == "shadow")
            {
                haveShadow = true;
                shadowOffset = parsePoint(getFirstText(info));
                shadowOffset.setX((int)(shadowOffset.x() * wmult));
                shadowOffset.setY((int)(shadowOffset.y() * hmult));
            }
            else if (info.tagName() == "bold")
            {
                haveBold = true;
                bold = getFirstText(info);
            }
            else if (info.tagName() == "italics")
            {
                haveItal = true;
                ital = getFirstText(info);
            }
            else if (info.tagName() == "underline")
            {
                haveUnder = true;
                under = getFirstText(info);
            }
            else
            {
                cerr << "Unknown tag " << info.tagName() << " in font\n";
                return;
            }
        }
    }
    
    fontProp *testFont = GetFont(name, false);
    if (testFont)
    {
        cerr << "Error: already have a font called: " << name << endl;
        return;
    }
    
    fontProp newFont;
    
    if (baseFont)
        newFont = *baseFont;

    if ( haveSizeSmall && fontSizeType == "small")
    {
        if (sizeSmall > 0)
            size = sizeSmall;
    }
    else if (haveSizeBig && fontSizeType == "big")
    {
        if (sizeBig > 0)
            size = sizeBig;
    }

    if (size < 0 && !baseFont)
    {
        cerr << "Error: font size must be > 0\n";
        return;
    }

    if (baseFont && !haveSize)
        size = baseFont->face.pointSize();
    else    
        size = GetMythMainWindow()->NormalizeFontSize(size);
    
    // If we don't have to, don't load the font.
    if (!haveFace && baseFont)
    {
        newFont.face = baseFont->face;
        if (haveSize)
            newFont.face.setPointSize(size);
    }
    else
    {
        QFont temp(face, size);
        temp.setStyleHint(styleHint, QFont::PreferAntialias);

        if (!temp.exactMatch())
            temp = QFont(QFontInfo(QApplication::font()).family(), size);

        newFont.face = temp;
    }
    
    if (baseFont && !haveBold)
        newFont.face.setBold(baseFont->face.bold());
    else        
    {
        if (bold.lower() == "yes")
            newFont.face.setBold(true);
        else
            newFont.face.setBold(false);
    }
    
    if (baseFont && !haveItal)
        newFont.face.setItalic(baseFont->face.italic());
    else        
    {
        if (ital.lower() == "yes")
            newFont.face.setItalic(true);
        else
            newFont.face.setItalic(false);
    }

    if (baseFont && !haveUnder)
        newFont.face.setUnderline(baseFont->face.underline());
    else        
    {
        if (under.lower() == "yes")
            newFont.face.setUnderline(true);
        else
            newFont.face.setUnderline(false);
    }    
    
    if (haveColor)
    {
        QColor foreColor(color);
        newFont.color = foreColor; 
    }
    
    if (haveDropColor)
    {
        QColor dropColor(dropcolor);
        newFont.dropColor = dropColor;
    }
    
    if (haveShadow)
        newFont.shadowOffset = shadowOffset;
    
    fontMap[name] = newFont;
}

void XMLParse::parseImage(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Image needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "Image needs an order\n";
        return;
    }

    QString filename = "";
    QPoint pos = QPoint(0, 0);

    QPoint scale = QPoint(-1, -1);
    QPoint skipin = QPoint(0, 0);

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "filename")
            {
                filename = getFirstText(info);
            }
            else if (info.tagName() == "position")
            {
                pos = parsePoint(getFirstText(info));
                pos.setX((int)(pos.x() * wmult));
                pos.setY((int)(pos.y() * hmult));
            }
            else if (info.tagName() == "staticsize")
            {
                scale = parsePoint(getFirstText(info));
            }
            else if (info.tagName() == "skipin")
            {
                skipin = parsePoint(getFirstText(info));
                skipin.setX((int)(skipin.x() * wmult));
                skipin.setY((int)(skipin.y() * hmult));
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in image\n";
                return;
            }
        }
    }

    UIImageType *image = new UIImageType(name, filename, order.toInt(), pos);
    image->SetScreen(wmult, hmult);
    if (scale.x() != -1 || scale.y() != -1)
        image->SetSize(scale.x(), scale.y());
    image->SetSkip(skipin.x(), skipin.y());
    QString flex = element.attribute("fleximage", "");
    if (!flex.isNull() && !flex.isEmpty())
    {
        if (flex.lower() == "yes")
            image->SetFlex(true);
        else
            image->SetFlex(false);
    }

    image->LoadImage();

    QString visible = element.attribute("visible", "");
    if (!visible.isNull() && !visible.isEmpty())
    {
        if (visible.lower() == "yes")
            image->show();
        else
            image->hide();
    }

    if (context != -1)
    {
        image->SetContext(context);
    }
    image->SetParent(container);
    container->AddType(image);
    container->bumpUpLayers(order.toInt());
}

bool XMLParse::parseAnimatedImage(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        VERBOSE(VB_IMPORTANT,
                "XMLParse::parseAnimatedImage(): image needs a name");
        return false;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        VERBOSE(VB_IMPORTANT,
                "XMLParse::parseAnimatedImage(): image needs a draw order");
        return false;
    }

    QString filename = "";
    QPoint pos = QPoint(0, 0);

    QPoint scale = QPoint(-1, -1);
    QPoint skipin = QPoint(0, 0);
    QString interval, startinterval, imagecount;

    bool ok = true;
    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "filename")
            {
                filename = getFirstText(info);
            }
            else if (info.tagName() == "position")
            {
                pos = parsePoint(getFirstText(info));
                pos.setX((int)(pos.x() * wmult));
                pos.setY((int)(pos.y() * hmult));
            }
            else if (info.tagName() == "staticsize")
            {
                scale = parsePoint(getFirstText(info));
            }
            else if (info.tagName() == "skipin")
            {
                skipin = parsePoint(getFirstText(info));
                skipin.setX((int)(skipin.x() * wmult));
                skipin.setY((int)(skipin.y() * hmult));
            }
            else if (info.tagName() == "interval")
            {
                interval = getFirstText(info);
            }
            else if (info.tagName() == "startinterval")
            {
                startinterval = getFirstText(info);
            }
            else if (info.tagName() == "imagecount")
            {
                imagecount = getFirstText(info);
            }
            else
            {
                VERBOSE(VB_IMPORTANT,
                        QString("XMLParse::parseAnimatedImage(): Unknown "
                                "tag (%1) in image").arg(info.tagName()));
                ok = false;
            }
        }
    }
    if (!ok)
        return ok;

    UIAnimatedImageType *image = new UIAnimatedImageType(name, filename, imagecount.toInt(),
        interval.toInt(), startinterval.toInt(), order.toInt(), pos);
    image->SetScreen(wmult, hmult);
    if (scale.x() != -1 || scale.y() != -1)
        image->SetSize(scale.x(), scale.y());
    image->SetSkip(skipin.x(), skipin.y());
    QString flex = element.attribute("fleximage", "");
    if (!flex.isNull() && !flex.isEmpty())
    {
        if (flex.lower() == "yes")
            image->SetFlex(true);
        else
            image->SetFlex(false);
    }

    //image->LoadImage();
    if (context != -1)
    {
        image->SetContext(context);
    }
    image->SetParent(container);
    container->AddType(image);
    container->bumpUpLayers(order.toInt());
    return true;
}

void XMLParse::parseRepeatedImage(LayerSet *container, QDomElement &element)
{
    int orientation = 0;
    int context = -1;
    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Repeated Image needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "Repeated Image needs an order\n";
        return;
    }

    QString filename = "";
    QPoint pos = QPoint(0, 0);

    QPoint scale = QPoint(-1, -1);
    QPoint skipin = QPoint(0, 0);

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "filename")
            {
                filename = getFirstText(info);
            }
            else if (info.tagName() == "position")
            {
                pos = parsePoint(getFirstText(info));
                pos.setX((int)(pos.x() * wmult));
                pos.setY((int)(pos.y() * hmult));
            }
            else if (info.tagName() == "staticsize")
            {
                scale = parsePoint(getFirstText(info));
            }
            else if (info.tagName() == "skipin")
            {
                skipin = parsePoint(getFirstText(info));
                skipin.setX((int)(skipin.x() * wmult));
                skipin.setY((int)(skipin.y() * hmult));
            }
            else if (info.tagName() == "orientation")
            {
                QString orient_string = getFirstText(info).lower();
                if (orient_string == "lefttoright")
                {
                    orientation = 0;
                }
                if (orient_string == "righttoleft")
                {
                    orientation = 1;
                }
                if (orient_string == "bottomtotop")
                {
                    orientation = 2;
                }
                if (orient_string == "toptobottom")
                {
                    orientation = 3;
                }
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in repeated image\n";
                return;
            }
        }
    }

    UIRepeatedImageType *image = new UIRepeatedImageType(name, filename, order.toInt(), pos);
    image->SetScreen(wmult, hmult);
    if (scale.x() != -1 || scale.y() != -1)
        image->SetSize(scale.x(), scale.y());
    image->SetSkip(skipin.x(), skipin.y());
    QString flex = element.attribute("fleximage", "");
    if (!flex.isNull() && !flex.isEmpty())
    {
        if (flex.lower() == "yes")
            image->SetFlex(true);
        else
            image->SetFlex(false);
    }

    image->LoadImage();
    if (context != -1)
    {
        image->SetContext(context);
    }
    image->setOrientation(orientation);
    image->SetParent(container);
    container->AddType(image);
    container->bumpUpLayers(order.toInt());
}

bool XMLParse::parseDefaultCategoryColors(QMap<QString, QString> &catColors)
{
    QFile f;
    QValueList<QString> searchpath = gContext->GetThemeSearchPath();
    for (QValueList<QString>::const_iterator ii = searchpath.begin();
        ii != searchpath.end(); ii++)
    {
        f.setName(*ii + "categories.xml");
        if (f.open(IO_ReadOnly))
            break;
    }
    if (f.handle() == -1)
    {
        VERBOSE(VB_IMPORTANT, "Error: Unable to open " << f.name());
        return false;
    }

    QDomDocument doc;
    QString errorMsg;
    int errorLine = 0;
    int errorColumn = 0;
    
    if (!doc.setContent(&f, false, &errorMsg, &errorLine, &errorColumn))
    {
        VERBOSE(VB_IMPORTANT, "Error parsing: " << f.name()
                << " line: " << errorLine << "  column: " << errorColumn
                << ": " << errorMsg);
        f.close();
        return false;
    }
    
    f.close();
        
    QDomElement element = doc.documentElement();
    for (QDomNode child = element.firstChild(); !child.isNull(); 
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull() && info.tagName() == "catcolor")
        {
            QString cat = "";
            QString col = "";
            cat = info.attribute("category");
            col = info.attribute("color");
            
            catColors[cat.lower()] = col;
        }
    }
    
    return true;
}

void XMLParse::parseGuideGrid(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QString align = "";
    QString font = "";
    QString color = "";
    QString seltype = "";
    QString selcolor = "";
    QString reccolor = "";
    QString concolor = "";
    QRect area;
    QPoint textoff = QPoint(0, 0);
    bool cutdown = true;
    bool multiline = false;
    QMap<QString, QString> catColors;
    QMap<int, QString> recImgs;
    QMap<int, QString> arrows;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Guide needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "Guide needs an order\n";
        return;
    }

    if (!parseDefaultCategoryColors(catColors))
    {
        //cerr << "No default category colors to parse." << endl;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "font")
            {
                font = getFirstText(info);
            }
            else if (info.tagName() == "solidcolor")
            {
                color = getFirstText(info);
                catColors["none"] = color;
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "align")
            {
                align = getFirstText(info);
            }
            else if (info.tagName() == "cutdown")
            {
                if (getFirstText(info).lower() == "no")
                   cutdown = false;
            }
            else if (info.tagName() == "textoffset")
            {
                textoff = parsePoint(getFirstText(info));
                textoff.setX((int)(textoff.x() * wmult));
                textoff.setY((int)(textoff.y() * hmult));
            }
            else if (info.tagName() == "recordingcolor")
            {
                reccolor = getFirstText(info);
            }
            else if (info.tagName() == "conflictingcolor")
            {
                concolor = getFirstText(info);
            }
            else if (info.tagName() == "multiline")
            {
                if (getFirstText(info).lower() == "yes")
                   multiline = true;
            }
            else if (info.tagName() == "selector")
            {
                QString typ = "";
                QString col = "";
                typ = info.attribute("type");
                col = info.attribute("color");

                selcolor = col;
                seltype = typ;
            }
            else if (info.tagName() == "recordstatus")
            {
                QString typ = "";
                QString img = "";
                int inttype = 0;
                typ = info.attribute("type");
                img = info.attribute("image");

                if (typ == "SingleRecord")
                    inttype = 1;
                else if (typ == "TimeslotRecord")
                    inttype = 2;
                else if (typ == "ChannelRecord")
                    inttype = 3;
                else if (typ == "AllRecord")
                    inttype = 4;
                else if (typ == "WeekslotRecord")
                    inttype = 5;
                else if (typ == "FindOneRecord")
                    inttype = 6;
                else if (typ == "OverrideRecord")
                    inttype = 7;

                recImgs[inttype] = img;
            }
            else if (info.tagName() == "arrow")
            {
                QString dir = "";
                QString imag = "";
                dir = info.attribute("direction");
                imag = info.attribute("image");

                if (dir == "left")
                    arrows[0] = imag;
                else
                    arrows[1] = imag;
            }
            else if (info.tagName() == "catcolor")
            {
                QString cat = "";
                QString col = "";
                cat = info.attribute("category");
                col = info.attribute("color");

                catColors[cat.lower()] = col;
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in bar\n";
                return;
            }
        }
    }
    fontProp *testfont = GetFont(font);
    if (!testfont)
    {
        cerr << "Unknown font: " << font << " in guidegrid: " << name << endl;
        return;
    }

    UIGuideType *guide = new UIGuideType(name, order.toInt());
    guide->SetScreen(wmult, hmult);
    guide->SetFont(testfont);
    guide->SetSolidColor(color);
    guide->SetCutDown(cutdown);
    guide->SetArea(area);
    guide->SetCategoryColors(catColors);
    guide->SetTextOffset(textoff);
    if (concolor == "")
        concolor = reccolor;
    guide->SetRecordingColors(reccolor, concolor);
    guide->SetSelectorColor(selcolor);
    for (int i = 1; i <= 7; i++)
        guide->LoadImage(i, recImgs[i]);
    if (seltype.lower() == "box")
        guide->SetSelectorType(1);
    else
        guide->SetSelectorType(2); // solid

    guide->SetArrow(0, arrows[0]);
    guide->SetArrow(1, arrows[1]);

    int jst = Qt::AlignLeft | Qt::AlignTop;
    if (multiline == true)
        jst = Qt::WordBreak;

    if (!align.isNull() && !align.isEmpty())
    {
        if (align.lower() == "center")
            guide->SetJustification(Qt::AlignCenter | jst);
        else if (align.lower() == "right")
            guide->SetJustification(Qt::AlignRight | jst);
        else if (align.lower() == "left")
            guide->SetJustification(Qt::AlignLeft | jst);
        else if (align.lower() == "allcenter")
            guide->SetJustification(Qt::AlignHCenter | Qt::AlignVCenter | jst);
        else if (align.lower() == "vcenter")
            guide->SetJustification(Qt::AlignVCenter | jst);
        else if (align.lower() == "hcenter")
            guide->SetJustification(Qt::AlignHCenter | jst);
    }
    else
        guide->SetJustification(jst);

    align = "";

    if (context != -1)
    {
        guide->SetContext(context);
    }
    container->AddType(guide);
}

void XMLParse::parseImageGrid(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QString align = "";
    QString activeFont = "";
    QString inactiveFont = "";
    QString selectedFont = "";
    QString color = "";
    QString textposition = "bottom";
    QRect area;
    int textheight = 0;
    int rowcount = 3;
    int columncount = 3;
    int padding = 10;
    bool cutdown = true;
    bool multiline = false;
    bool showChecks = false;
    bool showSelected = false;
    bool showScrollArrows = false;
    QString defaultImage = "";
    QString normalImage = "";
    QString selectedImage = "";
    QString highlightedImage = "";

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Image Grid needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "Image Grid needs an order\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "activefont")
            {
                activeFont = getFirstText(info);
            }
            else if (info.tagName() == "inactivefont")
            {
                inactiveFont = getFirstText(info);
            }

            else if (info.tagName() == "selectedfont")
            {
                selectedFont = getFirstText(info);
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "columncount")
            {
                columncount = getFirstText(info).toInt();
            }
            else if (info.tagName() == "rowcount")
            {
                rowcount = getFirstText(info).toInt();
            }
            else if (info.tagName() == "padding")
            {
                padding = getFirstText(info).toInt();
            }
            else if (info.tagName() == "align")
            {
                align = getFirstText(info);
            }
            else if (info.tagName() == "cutdown")
            {
                if (getFirstText(info).lower() == "no")
                    cutdown = false;
            }
            else if (info.tagName() == "showchecks")
            {
                if (getFirstText(info).lower() == "yes")
                    showChecks = true;
            }
            else if (info.tagName() == "showselected")
            {
                if (getFirstText(info).lower() == "yes")
                    showSelected = true;
            }
            else if (info.tagName() == "showscrollarrows")
            {
                if (getFirstText(info).lower() == "yes")
                    showScrollArrows = true;
            }
            else if (info.tagName() == "textheight")
            {
                textheight = getFirstText(info).toInt();
            }
            else if (info.tagName() == "textposition")
            {
                textposition = getFirstText(info);
            }
            else if (info.tagName() == "multiline")
            {
                if (getFirstText(info).lower() == "yes")
                    multiline = true;
            }
            else if (info.tagName() == "image")
            {
                QString imgname = "";
                QString file = "";

                imgname = info.attribute("function", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image in a image grid needs a function\n";
                    return;
                }

                file = info.attribute("filename", "");
                if (file.isNull() || file.isEmpty())
                {
                    cerr << "Image in a image grid needs a filename\n";
                    return;
                }

                if (imgname.lower() == "normal")
                {
                    normalImage = file;
                }

                if (imgname.lower() == "selected")
                {
                    selectedImage = file;
                }

                if (imgname.lower() == "highlighted")
                {
                    highlightedImage = file;
                }

                if (imgname.lower() == "default")
                {
                    defaultImage = file;
                }

            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in bar\n";
                return;
            }
        }
    }
    fontProp *font1 = GetFont(activeFont);
    if (!font1)
    {
        cerr << "Unknown font: " << activeFont << " in image grid: " << name << endl;
        return;
    }

    fontProp *font2 = GetFont(selectedFont);
    if (!font2)
    {
        cerr << "Unknown font: " << selectedFont << " in image grid: " << name << endl;
        return;
    }

    fontProp *font3 = GetFont(inactiveFont);
    if (!font2)
    {
        cerr << "Unknown font: " << inactiveFont << " in image grid: " << name << endl;
        return;
    }

    UIImageGridType *grid = new UIImageGridType(name, order.toInt());
    grid->SetScreen(wmult, hmult);
    grid->setActiveFont(font1);
    grid->setSelectedFont(font2);
    grid->setInactiveFont(font3);
    grid->setCutDown(cutdown);
    grid->setShowChecks(showChecks);
    grid->setShowSelected(showSelected);
    grid->setShowScrollArrows(showScrollArrows);
    grid->setArea(area);
    grid->setTextHeight(textheight);
    grid->setPadding(padding);
    grid->setRowCount(rowcount);
    grid->setColumnCount(columncount);
    grid->setDefaultImage(defaultImage);
    grid->setNormalImage(normalImage);
    grid->setSelectedImage(selectedImage);
    grid->setHighlightedImage(highlightedImage);

    int jst = Qt::AlignLeft | Qt::AlignTop;
    if (multiline == true)
        jst = Qt::WordBreak;

    if (!align.isNull() && !align.isEmpty())
    {
        if (align.lower() == "center")
            grid->setJustification(Qt::AlignCenter | jst);
        else if (align.lower() == "right")
            grid->setJustification(Qt::AlignRight | jst);
        else if (align.lower() == "allcenter")
            grid->setJustification(Qt::AlignHCenter | Qt::AlignVCenter | jst);
        else if (align.lower() == "vcenter")
            grid->setJustification(Qt::AlignVCenter | jst);
        else if (align.lower() == "hcenter")
            grid->setJustification(Qt::AlignHCenter | jst);
    }
    else
        grid->setJustification(jst);

    align = "";

    if (textposition == "top")
        grid->setTextPosition(UIImageGridType::textPosTop);
    else
        grid->setTextPosition(UIImageGridType::textPosBottom);

    if (context != -1)
    {
        grid->SetContext(context);
    }
    container->AddType(grid);
    grid->calculateScreenArea();
    grid->recalculateLayout();
}

void XMLParse::parseBar(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QString align = "";
    QString orientation = "horizontal";
    QString filename = "";
    QString font = "";
    QPoint textoff = QPoint(0, 0);
    QPoint iconoff = QPoint(0, 0);
    QPoint iconsize = QPoint(0, 0);
    QRect area;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Bar needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "Bar needs an order\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "orientation")
            {
                orientation = getFirstText(info);
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "imagefile")
            {
                filename = getFirstText(info);
            }
            else if (info.tagName() == "font")
            {
                font = getFirstText(info);
            }
            else if (info.tagName() == "align")
            {
                align = getFirstText(info);
            }
            else if (info.tagName() == "textoffset")
            {
                textoff = parsePoint(getFirstText(info));
                textoff.setX((int)(textoff.x() * wmult));
                textoff.setY((int)(textoff.y() * hmult));
            }
            else if (info.tagName() == "iconoffset")
            {
                iconoff = parsePoint(getFirstText(info));
                iconoff.setX((int)(iconoff.x() * wmult));
                iconoff.setY((int)(iconoff.y() * hmult));
            }
            else if (info.tagName() == "iconsize")
            {
                iconsize = parsePoint(getFirstText(info));
                iconsize.setX((int)(iconsize.x() * wmult));
                iconsize.setY((int)(iconsize.y() * hmult));
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in bar\n";
                return;
            }
        }
    }
    fontProp *testfont = GetFont(font);
    if (!testfont)
    {
        cerr << "Unknown font: " << font << " in bar: " << name << endl;
        return;
    }

    UIBarType *bar = new UIBarType(name, filename, order.toInt(), area);
    bar->SetScreen(wmult, hmult);
    bar->SetFont(testfont);
    bar->SetTextOffset(textoff);
    bar->SetIconOffset(iconoff);
    bar->SetIconSize(iconsize);
    if (orientation == "horizontal")
       bar->SetOrientation(1);
    else if (orientation == "vertical")
       bar->SetOrientation(2);

    if (!align.isNull() && !align.isEmpty())
    {
        if (align.lower() == "center")
            bar->SetJustification(Qt::AlignCenter);
        else if (align.lower() == "right")
            bar->SetJustification(Qt::AlignRight);
        else if (align.lower() == "left")
            bar->SetJustification(Qt::AlignLeft);
        else if (align.lower() == "allcenter")
            bar->SetJustification(Qt::AlignHCenter | Qt::AlignVCenter);
        else if (align.lower() == "vcenter")
            bar->SetJustification(Qt::AlignVCenter);
        else if (align.lower() == "hcenter")
            bar->SetJustification(Qt::AlignHCenter);

    }
    align = "";

    if (context != -1)
    {
        bar->SetContext(context);
    }
    container->AddType(bar);
}



fontProp *XMLParse::GetFont(const QString &text, bool checkGlobal)
{
    fontProp *ret;
    if (fontMap.contains(text))
        ret = &fontMap[text];
    else if (checkGlobal && globalFontMap.contains(text))
        ret = &globalFontMap[text];
    else
        ret = NULL;
    return ret;
}

void XMLParse::normalizeRect(QRect &rect)
{
    rect.setWidth((int)(rect.width() * wmult));
    rect.setHeight((int)(rect.height() * hmult));
    rect.moveTopLeft(QPoint((int)(rect.x() * wmult),
                             (int)(rect.y() * hmult)));
    rect = rect.normalize();
}

QPoint XMLParse::parsePoint(QString text)
{
    int x, y;
    QPoint retval(0, 0);
    if (sscanf(text.data(), "%d,%d", &x, &y) == 2)
        retval = QPoint(x, y);
    return retval;
}

QRect XMLParse::parseRect(QString text)
{
    int x, y, w, h;
    QRect retval(0, 0, 0, 0);
    if (sscanf(text.data(), "%d,%d,%d,%d", &x, &y, &w, &h) == 4)
        retval = QRect(x, y, w, h);

    return retval;
}


void XMLParse::parseContainer(QDomElement &element, QString &newname, int &context, QRect &area)
{
    context = -1;
    QString debug = "";
    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Container needs a name\n";
        return;
    }

    LayerSet *container = GetSet(name);
    if (container)
    {
        cerr << "Container: " << name << " already exists\n";
        return;
    }
    newname = name;

    container = new LayerSet(name);

    layerMap[name] = container;

    bool ok = true;
    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "debug")
            {
                debug = getFirstText(info);
                if (debug.lower() == "yes")
                    container->SetDebug(true);
            }
            else if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "image")
            {
                parseImage(container, info);
            }
            else if (info.tagName() == "animatedimage")
            {
                if (!parseAnimatedImage(container, info))
                    ok = false;
            }
            else if (info.tagName() == "repeatedimage")
            {
                parseRepeatedImage(container, info);
            }
            else if (info.tagName() == "listarea")
            {
                parseListArea(container, info);
            }
            else if (info.tagName() == "listbtnarea")
            {
                parseListBtnArea(container, info);
            }
            else if (info.tagName() == "listtreearea")
            {
                parseListTreeArea(container, info);
            }
            else if (info.tagName() == "textarea")
            {
                parseTextArea(container, info);
            }
            else if (info.tagName() == "richtextarea")
            {
                parseRichTextArea(container, info);
            }
            else if (info.tagName() == "multitextarea")
            {
                parseMultiTextArea(container, info);
            }
            else if (info.tagName() == "remoteedit")
            {
                parseRemoteEdit(container, info);
            }
            else if (info.tagName() == "statusbar")
            {
                parseStatusBar(container, info);
            }
            else if (info.tagName() == "managedtreelist")
            {
                parseManagedTreeList(container, info);
            }
            else if (info.tagName() == "pushbutton")
            {
                parsePushButton(container, info);
            }
            else if (info.tagName() == "textbutton")
            {
                parseTextButton(container, info);
            }
            else if (info.tagName() == "checkbox")
            {
                parseCheckBox(container, info);
            }
            else if (info.tagName() == "selector")
            {
                parseSelector(container, info);
            }
            else if (info.tagName() == "blackhole")
            {
                parseBlackHole(container, info);
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
                container->SetAreaRect(area);
            }
            else if (info.tagName() == "bar")
            {
                parseBar(container, info);
            }
            else if (info.tagName() == "keyboard")
            {
                parseKeyboard(container, info);
            }
            else if (info.tagName() == "guidegrid")
            {
                parseGuideGrid(container, info);
            }
            else if (info.tagName() == "imagegrid")
            {
                parseImageGrid(container, info);
            }
            else
            {
                VERBOSE(VB_IMPORTANT,
                        QString("Container %1 contains unknown child: %2")
                        .arg(name).arg(info.tagName()));
                ok = false;
            }
        }
    }
    if (!ok)
    {
        VERBOSE(VB_IMPORTANT, QString("Could not parse container '%1'. "
                                      "Ignoring.").arg(name));
        return;
    }

    if (context != -1)
        container->SetContext(context);

//    container->SetAreaRect(area);
    allTypes->push_back(container);
}

void XMLParse::parseTextArea(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QRect area = QRect(0, 0, 0, 0);
    QRect altArea = QRect(0, 0, 0, 0);
    QPoint shadowOffset = QPoint(0, 0);
    QString font = "";
    QString cutdown = "";
    QString value = "";
    QString statictext = "";
    QString multiline = "";
    int draworder = 0;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Text area needs a name\n";
        return;
    }

    QString layerNum = element.attribute("draworder", "");
    if (layerNum.isNull() && layerNum.isEmpty())
    {
        cerr << "Text area needs a draworder\n";
        return;
    }
    draworder = layerNum.toInt();

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "altarea")
            {
                altArea = parseRect(getFirstText(info));
                normalizeRect(altArea);
            }
            else if (info.tagName() == "font")
            {
                font = getFirstText(info);
            }
            else if (info.tagName() == "value")
            {
                if ((value.isNull() || value.isEmpty()) &&
                    info.attribute("lang","") == "")
                {
                    value = qApp->translate("ThemeUI", getFirstText(info));
                }
                else if (info.attribute("lang","").lower() ==
                         gContext->GetLanguageAndVariant())
                {
                    value = getFirstText(info);
                }
                else if (info.attribute("lang","").lower() ==
                         gContext->GetLanguage())
                {
                    value = getFirstText(info);
                }
            }
            else if (info.tagName() == "cutdown")
            {
                cutdown = getFirstText(info);
            }
            else if (info.tagName() == "multiline")
            {
                multiline = getFirstText(info);
            }
            else if (info.tagName() == "shadow")
            {
                shadowOffset = parsePoint(getFirstText(info));
                shadowOffset.setX((int)(shadowOffset.x() * wmult));
                shadowOffset.setY((int)(shadowOffset.y() * hmult));
            }
            else
            {
                cerr << "Unknown tag in textarea: " << info.tagName() << endl;
                return;
            }
        }
    }

    fontProp *testfont = GetFont(font);
    if (!testfont)
    {
        cerr << "Unknown font: " << font << " in textarea: " << name << endl;
        return;
    }

    UITextType *text = new UITextType(name, testfont, value, draworder, area,
                                      altArea);
    text->SetScreen(wmult, hmult);
    if (context != -1)
    {
        text->SetContext(context);
    }
    if (multiline.lower() == "yes")
        text->SetJustification(Qt::WordBreak);
    if (!value.isNull() && !value.isEmpty())
        text->SetText(value);
    if (cutdown.lower() == "no")
        text->SetCutDown(false);

    QString align = element.attribute("align", "");
    if (!align.isNull() && !align.isEmpty())
    {
        int jst = (Qt::AlignTop | Qt::AlignLeft);
        if (multiline.lower() == "yes")
        {
            jst = Qt::WordBreak;
        }
        if (align.lower() == "center")
            text->SetJustification(jst | Qt::AlignCenter);
        else if (align.lower() == "right")
            text->SetJustification(jst | Qt::AlignRight);
        else if (align.lower() == "left")
            text->SetJustification(jst | Qt::AlignLeft);
        else if (align.lower() == "allcenter")
            text->SetJustification(jst | Qt::AlignHCenter | Qt::AlignVCenter);
        else if (align.lower() == "vcenter")
            text->SetJustification(jst | Qt::AlignVCenter);
        else if (align.lower() == "hcenter")
            text->SetJustification(jst | Qt::AlignHCenter);
    }
    align = "";
    text->SetParent(container);
    text->calculateScreenArea();
    container->AddType(text);
}

void XMLParse::parseRichTextArea(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QRect area = QRect(0, 0, 0, 0);
    QRect textArea = QRect(0, 0, 0, 0);
    QString font = "";
    QString value = "";
    QString bgImageReg = "", bgImageSel = "";
    int draworder = 0;

    bool   bShowArrows = true;

    QPoint upArrowSelPos = QPoint(0,0);
    QPoint dnArrowSelPos = QPoint(0,0);
    QPoint upArrowRegPos = QPoint(0,0);
    QPoint dnArrowRegPos = QPoint(0,0);

    QPixmap *upArrowSel = NULL;
    QPixmap *dnArrowSel = NULL;
    QPixmap *upArrowReg = NULL;
    QPixmap *dnArrowReg = NULL;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Rich Text area needs a name\n";
        return;
    }

    QString layerNum = element.attribute("draworder", "");
    if (layerNum.isNull() && layerNum.isEmpty())
    {
        cerr << "Rich Text area needs a draworder\n";
        return;
    }
    draworder = layerNum.toInt();

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "textarea")
            {
                textArea = parseRect(getFirstText(info));
                normalizeRect(textArea);
            }
            else if (info.tagName() == "font")
            {
                font = getFirstText(info);
            }
            else if (info.tagName() == "showscrollarrows") 
            {
                if (getFirstText(info).lower() == "no")
                    bShowArrows = false;
            }
            else if (info.tagName() == "backgroundsel")
            {
                bgImageSel = getFirstText(info);
            }
            else if (info.tagName() == "backgroundreg")
            {
                bgImageReg = getFirstText(info);
            }
            else if (info.tagName() == "value")
            {
                if ((value.isNull() || value.isEmpty()) &&
                     info.attribute("lang","") == "")
                {
                    value = qApp->translate("ThemeUI", getFirstText(info));
                }
                else if (info.attribute("lang","").lower() ==
                         gContext->GetLanguageAndVariant())
                {
                    value = getFirstText(info);
                }
                else if (info.attribute("lang","").lower() ==
                         gContext->GetLanguage())
                {
                    value = getFirstText(info);
                }
            }
            else if (info.tagName() == "image")
            {
                QString imgname = "";
                QString imgpoint = "";
                QString imgfile = "";

                imgname = info.attribute("function", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image needs a function\n";
                    return;
                }

                imgfile = info.attribute("filename", "");
                if (imgfile.isNull() || imgfile.isEmpty())
                {
                    cerr << "Image needs a filename\n";
                    return;
                }

                imgpoint = info.attribute("location", "");
                if (imgpoint.isNull() && imgpoint.isEmpty())
                {
                    cerr << "Image needs a location\n";
                    return;
                }

                if (imgname.lower() == "uparrow-reg")
                {
                    upArrowReg = gContext->LoadScalePixmap(imgfile);
                    upArrowRegPos = parsePoint(imgpoint);
                    upArrowRegPos.setX((int)(upArrowRegPos.x() * wmult));
                    upArrowRegPos.setY((int)(upArrowRegPos.y() * hmult));
                }
                if (imgname.lower() == "downarrow-reg")
                {
                    dnArrowReg = gContext->LoadScalePixmap(imgfile);
                    dnArrowRegPos = parsePoint(imgpoint);
                    dnArrowRegPos.setX((int)(dnArrowRegPos.x() * wmult));
                    dnArrowRegPos.setY((int)(dnArrowRegPos.y() * hmult));
                }
                if (imgname.lower() == "uparrow-sel")
                {
                    upArrowSel = gContext->LoadScalePixmap(imgfile);
                    upArrowSelPos = parsePoint(imgpoint);
                    upArrowSelPos.setX((int)(upArrowSelPos.x() * wmult));
                    upArrowSelPos.setY((int)(upArrowSelPos.y() * hmult));
                }
                if (imgname.lower() == "downarrow-sel")
                {
                    dnArrowSel = gContext->LoadScalePixmap(imgfile);
                    dnArrowSelPos = parsePoint(imgpoint);
                    dnArrowSelPos.setX((int)(dnArrowSelPos.x() * wmult));
                    dnArrowSelPos.setY((int)(dnArrowSelPos.y() * hmult));
                }
            }
            else
            {
                cerr << "Unknown tag in richtextarea: " << info.tagName() << endl;
                return;
            }
        }
    }

    fontProp *testfont = GetFont(font);
    if (!testfont)
    {
        cerr << "Unknown font: " << font << " in richtextarea: " << name << endl;
        return;
    }

    UIRichTextType *text = new UIRichTextType(name, testfont, value, draworder, 
                                              area, textArea);
    text->SetScreen(wmult, hmult);
    if (context != -1)
    {
        text->SetContext(context);
    }
    if (!value.isNull() && !value.isEmpty())
        text->SetText(value);

    if (upArrowReg)
    {
        text->SetImageUpArrowReg(*upArrowReg, upArrowRegPos);
        delete upArrowReg;
    }
    if (upArrowSel)
    {
        text->SetImageUpArrowSel(*upArrowSel, upArrowSelPos);
        delete upArrowSel;
    }
    if (dnArrowReg)
    {
        text->SetImageDnArrowReg(*dnArrowReg, dnArrowRegPos);
        delete dnArrowReg;
    }
    if (dnArrowSel)
    {
        text->SetImageDnArrowSel(*dnArrowSel, dnArrowSelPos);
        delete dnArrowSel;
    }

    text->SetShowScrollArrows(bShowArrows);
    text->SetBackgroundImages(bgImageReg, bgImageSel);
    text->SetParent(container);
    text->calculateScreenArea();
    container->AddType(text);
}

void XMLParse::parseMultiTextArea(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QRect area = QRect(0, 0, 0, 0);
    QRect altArea = QRect(0, 0, 0, 0);
    QPoint shadowOffset = QPoint(0, 0);
    QString font = "";
    QString cutdown = "";
    QString value = "";
    QString statictext = "";
    QString multiline = "";
    int padding = -1;
    int drop_delay = -1;
    int drop_pause = -1;
    int scroll_delay = -1;
    int scroll_pause = -1;
    int draworder = 0;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Multitext area needs a name\n";
        return;
    }

    QString layerNum = element.attribute("draworder", "");
    if (layerNum.isNull() && layerNum.isEmpty())
    {
        cerr << "Multitext area needs a draworder\n";
        return;
    }
    draworder = layerNum.toInt();

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "padding")
            {
                padding = getFirstText(info).toInt();
            }
            else if (info.tagName() == "dropdelay")
            {
                drop_delay = getFirstText(info).toInt();
            }
            else if (info.tagName() == "droppause")
            {
                drop_pause = getFirstText(info).toInt();
            }
            else if (info.tagName() == "scrolldelay")
            {
                scroll_delay = getFirstText(info).toInt();
            }
            else if (info.tagName() == "scrollpause")
            {
                scroll_pause = getFirstText(info).toInt();
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "altarea")
            {
                altArea = parseRect(getFirstText(info));
                normalizeRect(altArea);
            }
            else if (info.tagName() == "font")
            {
                font = getFirstText(info);
            }
            else if (info.tagName() == "cutdown")
            {
                cutdown = getFirstText(info);
            }
            else if (info.tagName() == "shadow")
            {
                shadowOffset = parsePoint(getFirstText(info));
                shadowOffset.setX((int)(shadowOffset.x() * wmult));
                shadowOffset.setY((int)(shadowOffset.y() * hmult));
            }
            else
            {
                cerr << "Unknown tag in multitext area: "
                     << info.tagName()
                     << endl;
                return;
            }
        }
    }

    fontProp *testfont = GetFont(font);
    if (!testfont)
    {
        cerr << "Unknown font: " << font << " in multitextarea: " << name << endl;
        return;
    }

    UIMultiTextType *multitext = new UIMultiTextType(name, testfont, draworder,
                                      area, altArea);
    multitext->SetScreen(wmult, hmult);
    if (context != -1)
    {
        multitext->SetContext(context);
    }

    if (padding > -1)
    {
        multitext->setMessageSpacePadding(padding);
    }
    if (drop_delay > -1)
    {
        multitext->setDropTimingLength(drop_delay);
    }
    if (drop_pause > -1)
    {
        multitext->setDropTimingPause(drop_pause);
    }
    if (scroll_delay > -1)
    {
        multitext->setScrollTimingLength(scroll_delay);
    }
    if (scroll_pause > -1)
    {
        multitext->setScrollTimingPause(scroll_pause);
    }

    multitext->SetParent(container);
    multitext->calculateScreenArea();
    container->AddType(multitext);
}

void XMLParse::parseRemoteEdit(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QRect area = QRect(0, 0, 0, 0);
    QString font = "";
    QString value = "";
    int draworder = 0;
    QColor selectedColor = QColor(0, 255, 255);
    QColor unselectedColor = QColor(100, 100, 100);
    QColor specialColor = QColor(255, 0, 0);
    
    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "RemoteEdit needs a name\n";
        return;
    }

    QString layerNum = element.attribute("draworder", "");
    if (layerNum.isNull() && layerNum.isEmpty())
    {
        cerr << "RemoteEdit needs a draworder\n";
        return;
    }
    draworder = layerNum.toInt();

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "font")
            {
                font = getFirstText(info);
            }
            else if (info.tagName() == "value")
            {
                if ((value.isNull() || value.isEmpty()) &&
                    info.attribute("lang","") == "")
                {
                    value = qApp->translate("ThemeUI", getFirstText(info));
                }
                else if (info.attribute("lang","").lower() ==
                         gContext->GetLanguageAndVariant())
                {
                    value = getFirstText(info);
                }
                else if (info.attribute("lang","").lower() ==
                         gContext->GetLanguage())
                {
                    value = getFirstText(info);
                }
            }
            else if (info.tagName() == "charcolors")
            {
                QString selected = "";
                QString unselected = "";
                QString special = "";
                selected = info.attribute("selected");
                unselected = info.attribute("unselected");
                special = info.attribute("special");
                
                if (selected != "")
                    selectedColor = QColor(selected);           
    
                if (unselected != "")
                    unselectedColor = QColor(unselected);           
                
                if (special != "")
                    specialColor = QColor(special);         
            }

            else
            {
                cerr << "Unknown tag in RemoteEdit: " << info.tagName() << endl;
                return;
            }
        }
    }

    fontProp *testfont = GetFont(font);
    if (!testfont)
    {
        cerr << "Unknown font: " << font << " in RemoteEdit: " << name << endl;
        return;
    }

    UIRemoteEditType *edit = new UIRemoteEditType(name, testfont, value, draworder, area);
    edit->SetScreen(wmult, hmult);
    if (context != -1)
    {
        edit->SetContext(context);
    }
    if (!value.isNull() && !value.isEmpty())
        edit->setText(value);

    edit->SetParent(container);
    edit->setCharacterColors(unselectedColor, selectedColor, specialColor);
    edit->calculateScreenArea();
    container->AddType(edit);
}

void XMLParse::parseListArea(LayerSet *container, QDomElement &element)
{
    int context = -1;
    int item_cnt = 0;
    QRect area = QRect(0, 0, 0, 0);
    QString force_color = "";
    QString act_font = "", in_font = "";
    QString statictext = "";
    int padding = 0;
    int draworder = 0;
    QMap<int, int> columnWidths;
    QMap<int, int> columnContexts;
    QMap<QString, QString> fontFunctions;
    QMap<QString, fontProp> theFonts;
    int colCnt = -1;

    QRect fill_select_area = QRect(0, 0, 0, 0);
    QColor fill_select_color = QColor(255, 255, 255);
    int fill_type = -1;

    QPoint uparrow_loc;
    QPoint dnarrow_loc;
    QPoint select_loc;
    QPoint rightarrow_loc;
    QPoint leftarrow_loc;

    QPixmap *uparrow_img = NULL;
    QPixmap *dnarrow_img = NULL;
    QPixmap *select_img = NULL;
    QPixmap *right_img = NULL;
    QPixmap *left_img = NULL;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "List area needs a name\n";
        return;
    }

    QString layerNum = element.attribute("draworder", "");
    if (layerNum.isNull() && layerNum.isEmpty())
    {
        cerr << "List area needs a draworder\n";
        return;
    }
    draworder = layerNum.toInt();

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "activefont")
            {
                act_font = getFirstText(info);
            }
            else if (info.tagName() == "inactivefont")
            {
                in_font = getFirstText(info);
            }
            else if (info.tagName() == "items")
            {
                item_cnt = getFirstText(info).toInt();
            }
            else if (info.tagName() == "columnpadding")
            {
                padding = getFirstText(info).toInt();
            }
            else if (info.tagName() == "fcnfont")
            {
                QString fontname = "";
                QString fontfcn = "";

                fontname = info.attribute("name", "");
                fontfcn = info.attribute("function", "");

                if (fontname.isNull() || fontname.isEmpty())
                {
                    cerr << "FcnFont needs a name\n";
                    return;
                }

                if (fontfcn.isNull() || fontfcn.isEmpty())
                {
                    cerr << "FcnFont needs a function\n";
                    return;
                }
                fontFunctions[fontfcn] = fontname;
            }
            else if (info.tagName() == "fill")
            {
                QString fillfcn = "";
                QString fillcolor = "";
                QString fillarea = "";
                QString filltype = "";

                fillfcn = info.attribute("function", "");
                fillcolor = info.attribute("color", "#ffffff");
                fillarea = info.attribute("area", "");
                filltype = info.attribute("type", "");

                if (fillfcn.isNull() || fillfcn.isEmpty())
                {
                    cerr << "Fill needs a function\n";
                    return;
                }
                if (fillcolor.isNull() || fillcolor.isEmpty())
                {
                    cerr << "Fill needs a color\n";
                    return;
                }
                if (filltype == "5050")
                    fill_type = 1;
                if (fill_type == -1)
                    fill_type = 1;

                if (fillarea.isNull() || fillarea.isEmpty())
                {
                    fill_select_area = area;
                }
                else
                {
                    fill_select_area = parseRect(fillarea);
                    normalizeRect(fill_select_area);
                }
                fill_select_color = QColor(fillcolor);

            }
            else if (info.tagName() == "image")
            {
                QString imgname = "";
                QString imgpoint = "";
                QString imgfile = "";

                imgname = info.attribute("function", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image needs a function\n";
                    return;
                }

                imgfile = info.attribute("filename", "");
                if (imgfile.isNull() || imgfile.isEmpty())
                {
                    cerr << "Image needs a filename\n";
                    return;
                }


                imgpoint = info.attribute("location", "");
                if (imgpoint.isNull() && imgpoint.isEmpty())
                {
                    cerr << "Image needs a location\n";
                    return;
                }

                if (imgname.lower() == "selectionbar")
                {
                    select_img = gContext->LoadScalePixmap(imgfile);
                    select_loc = parsePoint(imgpoint);
                    select_loc.setX((int)(select_loc.x() * wmult));
                    select_loc.setY((int)(select_loc.y() * hmult));
                }
                if (imgname.lower() == "uparrow")
                {
                    uparrow_img = gContext->LoadScalePixmap(imgfile);
                    uparrow_loc = parsePoint(imgpoint);
                    uparrow_loc.setX((int)(uparrow_loc.x() * wmult));
                    uparrow_loc.setY((int)(uparrow_loc.y() * hmult));
                }
                if (imgname.lower() == "downarrow")
                {
                    dnarrow_img = gContext->LoadScalePixmap(imgfile);
                    dnarrow_loc = parsePoint(imgpoint);
                    dnarrow_loc.setX((int)(dnarrow_loc.x() * wmult));
                    dnarrow_loc.setY((int)(dnarrow_loc.y() * hmult));
                }
                if (imgname.lower() == "leftarrow")
                {
                    left_img = gContext->LoadScalePixmap(imgfile);
                    leftarrow_loc = parsePoint(imgpoint);
                    leftarrow_loc.setX((int)(leftarrow_loc.x() * wmult));
                    leftarrow_loc.setY((int)(leftarrow_loc.y() * hmult));

                }
                else if (imgname.lower() == "rightarrow")
                {
                    right_img = gContext->LoadScalePixmap(imgfile);
                    rightarrow_loc = parsePoint(imgpoint);
                    rightarrow_loc.setX((int)(rightarrow_loc.x() * wmult));
                    rightarrow_loc.setY((int)(rightarrow_loc.y() * hmult));
                }
            }
            else if (info.tagName() == "column")
            {
                QString colnum = "";
                QString colwidth = "";
                QString colcontext = "";
                colnum = info.attribute("number", "");
                if (colnum.isNull() || colnum.isEmpty())
                {
                    cerr << "Column needs a number\n";
                    return;
                }

                colwidth = info.attribute("width", "");
                if (colwidth.isNull() && colwidth.isEmpty())
                {
                    cerr << "Column needs a width\n";
                    return;
                }

                colcontext = info.attribute("context", "");
                if (!colcontext.isNull() && !colcontext.isEmpty())
                {
                    columnContexts[colnum.toInt()] = colcontext.toInt();
                }
                else
                    columnContexts[colnum.toInt()] = -1;

                columnWidths[colnum.toInt()] = (int)(wmult*colwidth.toInt());
                if (colnum.toInt() > colCnt)
                    colCnt = colnum.toInt();
            }
            else
            {
                cerr << "Unknown tag in listarea: " << info.tagName() << endl;
                return;
            }
        }
    }

    UIListType *list = new UIListType(name, area, draworder);
    list->SetCount(item_cnt);
    list->SetScreen(wmult, hmult);
    list->SetColumnPad(padding);
    if (select_img)
    {
        list->SetImageSelection(*select_img, select_loc);
        delete select_img;
    }

    if (uparrow_img)
    {
        list->SetImageUpArrow(*uparrow_img, uparrow_loc);
        delete uparrow_img;
    }

    if (dnarrow_img)
    {
        list->SetImageDownArrow(*dnarrow_img, dnarrow_loc);
        delete dnarrow_img;
    }

    if (right_img)
    {
        list->SetImageRightArrow(*right_img, rightarrow_loc);
        delete right_img;
    }

    if (left_img)
    {
        list->SetImageLeftArrow(*left_img, leftarrow_loc);
        delete left_img;
    }

    typedef QMap<QString,QString> fontdata;
    fontdata::Iterator it;
    for ( it = fontFunctions.begin(); it != fontFunctions.end(); ++it )
    {
        fontProp *testFont = GetFont(it.data());
        if (testFont)
            theFonts[it.data()] = *testFont;
    }
    if (theFonts.size() > 0)
        list->SetFonts(fontFunctions, theFonts);
    if (fill_type != -1)
        list->SetFill(fill_select_area, fill_select_color, fill_type);
    if (context != -1)
    {
        list->SetContext(context);
    }

    for (int i = 0; i <= colCnt; i++)
    {
         if (columnWidths[i] > 0)
            list->SetColumnWidth(i, columnWidths[i]);
        if (columnContexts[i] != -1)
            list->SetColumnContext(i, columnContexts[i]);
        else
            list->SetColumnContext(i, context);
    }

    if (colCnt == -1)
        list->SetColumnContext(1, -1);

    list->SetParent(container);
    list->calculateScreenArea();
    container->AddType(list);

    // the selection image is only drawn on layer 8
    container->bumpUpLayers(8);
}

LayerSet *XMLParse::GetSet(const QString &text)
{
    LayerSet *ret = NULL;
    if (layerMap.contains(text))
        ret = layerMap[text];

    return ret;
}

void XMLParse::parseStatusBar(LayerSet *container, QDomElement &element)
{
    int context = -1;
    int orientation = 0;
    int imgFillSpace = 0;
    QPixmap *imgFiller = NULL;
    QPixmap *imgContainer = NULL;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "StatusBar needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "StatusBar needs an order\n";
        return;
    }

    QPoint pos = QPoint(0, 0);

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "container")
            {
                QString confile = getFirstText(info);
                QString flex = info.attribute("fleximage", "");
                if (!flex.isNull() && !flex.isEmpty())
                {
                    if (flex.lower() == "yes")
                    {
                        int pathStart = confile.findRev('/');
                        if (usetrans == 1)
                        {
                            if (pathStart < 0 )
                                confile = "trans-" + confile;
                            else
                                confile.replace(pathStart, 1, "/trans-");
                        }
                        else
                        {
                            if (pathStart < 0 )
                                confile = "solid-" + confile;
                            else
                                confile.replace(pathStart, 1, "/solid-");
                        }
                        
                        imgContainer = gContext->LoadScalePixmap(confile);
                    }
                    else
                        imgContainer = gContext->LoadScalePixmap(confile);
                }
                else
                {
                    imgContainer = gContext->LoadScalePixmap(confile);
                }
            }
            else if (info.tagName() == "position")
            {
                pos = parsePoint(getFirstText(info));
                pos.setX((int)(pos.x() * wmult));
                pos.setY((int)(pos.y() * hmult));
            }
            else if (info.tagName() == "fill")
            {
                QString fillfile = getFirstText(info);
                imgFillSpace = info.attribute("whitespace", "").toInt();

                QString flex = info.attribute("fleximage", "");
                if (!flex.isNull() && !flex.isEmpty())
                {
                    if (flex.lower() == "yes")
                    {
                        if (usetrans == 1)
                            fillfile = "trans-" + fillfile;
                        else
                            fillfile = "solid-" + fillfile;
                        
                        imgFiller = gContext->LoadScalePixmap(fillfile);
                     }
                     else
                        imgFiller = gContext->LoadScalePixmap(fillfile);
                }
                else
                {
                    imgFiller = gContext->LoadScalePixmap(fillfile);
                }
            }
            else if (info.tagName() == "orientation")
            {
                QString orient_string = getFirstText(info).lower();
                if (orient_string == "lefttoright")
                {
                    orientation = 0;
                }
                if (orient_string == "righttoleft")
                {
                    orientation = 1;
                }
                if (orient_string == "bottomtotop")
                {
                    orientation = 2;
                }
                if (orient_string == "toptobottom")
                {
                    orientation = 3;
                }
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in statusbar\n";
                return;
            }
        }
    }

    UIStatusBarType *sb = new UIStatusBarType(name, pos, order.toInt());
    sb->SetScreen(wmult, hmult);
    sb->SetFiller(imgFillSpace);
    if (imgContainer)
    {
        sb->SetContainerImage(*imgContainer);
        delete imgContainer;
    }
    if (imgFiller)
    {
        sb->SetFillerImage(*imgFiller);
        delete imgFiller;
    }
    if (context != -1)
    {
        sb->SetContext(context);
    }
    sb->SetParent(container);
    sb->calculateScreenArea();
    sb->setOrientation(orientation);
    container->AddType(sb);
    container->bumpUpLayers(order.toInt());
}

struct TreeIcon { int i; QPixmap *img;};
void XMLParse::parseManagedTreeList(LayerSet *container, QDomElement &element)
{
    QRect area;
    QRect binarea;
    int bins = 1;
    int context = -1;
    int selectPadding = 0;
    bool selectScale = true;

    QPixmap *uparrow_img = NULL;
    QPixmap *downarrow_img = NULL;
    QPixmap *leftarrow_img = NULL;
    QPixmap *rightarrow_img = NULL;
    QPixmap *icon_img = NULL;
    QPixmap *select_img = NULL;
    QPoint selectPoint(0,0);
    QPoint upArrowPoint(0,0);
    QPoint downArrowPoint(0,0);
    QPoint rightArrowPoint(0,0);
    QPoint leftArrowPoint(0,0);
    

    //
    //  A Map to store the geometry of
    //  the bins as we parse
    //

    typedef QMap<int, QRect> CornerMap;
    QPtrList<TreeIcon> iconList;
    iconList.setAutoDelete(true);
    CornerMap bin_corners;
    bin_corners.clear();

    //
    //  Some maps to store fonts as we parse
    //

    QMap<QString, QString> fontFunctions;
    QMap<QString, fontProp> theFonts;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "ManagedTreeList needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "ManagedTreeList needs an order\n";
        return;
    }

    QString bins_string = element.attribute("bins", "");
    if (bins_string.toInt() > 0)
    {
        bins = bins_string.toInt();
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "image")
            {
                QString imgname = "";
                QString file = "";
                int     imgnumber = -1;

                imgname = info.attribute("function", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image needs a function\n";
                    return;
                }

                file = info.attribute("filename", "");
                if (file.isNull() || file.isEmpty())
                {
                    cerr << "Image needs a filename\n";
                    return;
                }

                QString imgNumStr = info.attribute("number", "");
                imgnumber = atoi(imgNumStr);

                if (info.tagName() == "context")
                {
                    context = getFirstText(info).toInt();
                }

                if (imgname.lower() == "selectionbar")
                {
                    QString imgpoint = "";
                    QString imgPad = "";
                    QString imgScale = "";
                    imgpoint = info.attribute("location", "");
                    if (!imgpoint.isNull() && !imgpoint.isEmpty())
                    {
                        selectPoint = parsePoint(imgpoint);
                        selectPoint.setX((int)(selectPoint.x() * wmult));
                        selectPoint.setY((int)(selectPoint.y() * hmult));    
                    }

                    imgPad = info.attribute("padding", "");
                    if (!imgPad.isNull() && !imgPad.isEmpty())
                    {
                        selectPadding = (int)(imgPad.toInt() * hmult);
                    }

                    imgScale = info.attribute("scale", "");
                    if (imgScale.lower() == "no")
                    {
                        selectScale = false;
                    }

                    select_img = gContext->LoadScalePixmap(file);
                    if (!select_img)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                else if (imgname.lower() == "uparrow")
                {
                    QString imgpoint = "";
                    imgpoint = info.attribute("location", "");
                    if (!imgpoint.isNull() && !imgpoint.isEmpty())
                    {
                        upArrowPoint = parsePoint(imgpoint);
                        upArrowPoint.setX((int)(upArrowPoint.x() * wmult));
                        upArrowPoint.setY((int)(upArrowPoint.y() * hmult));    
                    }
                    
                    uparrow_img = gContext->LoadScalePixmap(file);
                    if (!uparrow_img)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                else if (imgname.lower() == "downarrow")
                {
                    QString imgpoint = "";
                    imgpoint = info.attribute("location", "");
                    if (!imgpoint.isNull() && !imgpoint.isEmpty())
                    {
                        downArrowPoint = parsePoint(imgpoint);
                        downArrowPoint.setX((int)(downArrowPoint.x() * wmult));
                        downArrowPoint.setY((int)(downArrowPoint.y() * hmult));    
                    }
                    downarrow_img = gContext->LoadScalePixmap(file);
                    if (!downarrow_img)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                else if (imgname.lower() == "leftarrow")
                {
                    QString imgpoint = "";
                    imgpoint = info.attribute("location", "");
                    if (!imgpoint.isNull() && !imgpoint.isEmpty())
                    {
                        leftArrowPoint = parsePoint(imgpoint);
                        leftArrowPoint.setX((int)(leftArrowPoint.x() * wmult));
                        leftArrowPoint.setY((int)(leftArrowPoint.y() * hmult));
                    }
                    leftarrow_img = gContext->LoadScalePixmap(file);
                    if (!leftarrow_img)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                else if (imgname.lower() == "rightarrow")
                {
                    QString imgpoint = "";
                    imgpoint = info.attribute("location", "");
                    if (!imgpoint.isNull() && !imgpoint.isEmpty())
                    {
                        rightArrowPoint = parsePoint(imgpoint);
                        rightArrowPoint.setX((int)(rightArrowPoint.x() * wmult));
                        rightArrowPoint.setY((int)(rightArrowPoint.y() * hmult));
                    }
                    rightarrow_img = gContext->LoadScalePixmap(file);
                    if (!rightarrow_img)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                else if ((imgname.lower() == "icon") && (imgnumber != -1))
                {
                    icon_img = gContext->LoadScalePixmap(file);
                    if (!icon_img)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                    else
                    {
                        TreeIcon *icn = new TreeIcon;
                        icn->img = icon_img;
                        icn->i = imgnumber;
                        iconList.append(icn);
                    }
                }
                else
                {
                    cerr << "xmlparse.o: I don't know what to do with an image tag who's function is " << imgname << endl;
                }
            }
            else if (info.tagName() == "bin")
            {
                QString whichbin_string = info.attribute("number", "");
                int whichbin = whichbin_string.toInt();
                if (whichbin < 1)
                {
                    cerr << "xmlparse.o: Bad setting for bin number in bin tag" << endl;
                    return;
                }
                if (whichbin > bins + 1)
                {
                    cerr << "xmlparse.o: Attempt to set bin with a reference larger than number of bins" << endl;
                    return;
                }
                for (QDomNode child = info.firstChild(); !child.isNull();
                child = child.nextSibling())
                {
                    QDomElement info = child.toElement();
                    if (!info.isNull())
                    {
                        if (info.tagName() == "area")
                        {
                            binarea = parseRect(getFirstText(info));
                            normalizeRect(binarea);
                            bin_corners[whichbin] = binarea;
                        }
                        else if (info.tagName() == "fcnfont")
                        {
                            QString fontname = "";
                            QString fontfcn = "";

                            fontname = info.attribute("name", "");
                            fontfcn = info.attribute("function", "");

                            if (fontname.isNull() || fontname.isEmpty())
                            {
                                cerr << "FcnFont needs a name\n";
                                return;
                            }

                            if (fontfcn.isNull() || fontfcn.isEmpty())
                            {
                                cerr << "FcnFont needs a function\n";
                                return;
                            }
                            QString a_string = QString("bin%1-%2").arg(whichbin).arg(fontfcn);
                            fontFunctions[a_string] = fontname;
                        }
                        else
                        {
                            cerr << "Unknown tag in bin: " << info.tagName() << endl;
                            return;
                        }
                    }
                }
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in ManagedTreeList\n";
                return;
            }
        }
    }


    UIManagedTreeListType *mtl = new UIManagedTreeListType(name);
    mtl->SetScreen(wmult, hmult);
    mtl->setArea(area);
    mtl->setBins(bins);
    mtl->setBinAreas(bin_corners);
    mtl->SetOrder(order.toInt());
    mtl->SetParent(container);
    mtl->SetContext(context);

    //
    //  Perform moegreen/jdanner font magic
    //

    typedef QMap<QString,QString> fontdata;
    fontdata::Iterator it;
    for ( it = fontFunctions.begin(); it != fontFunctions.end(); ++it )
    {
        fontProp *testFont = GetFont(it.data());
        if (testFont)
            theFonts[it.data()] = *testFont;
    }

    if (theFonts.size() > 0)
        mtl->setFonts(fontFunctions, theFonts);

    if (select_img)
    {
        mtl->setSelectScale(selectScale);
        mtl->setSelectPadding(selectPadding);
        mtl->setSelectPoint(selectPoint);
        mtl->setHighlightImage(*select_img);
        delete select_img;
    }

    if (!uparrow_img)
        uparrow_img = new QPixmap();
    if (!downarrow_img)
        downarrow_img = new QPixmap();
    if (!leftarrow_img)
        leftarrow_img = new QPixmap();
    if (!rightarrow_img)
        rightarrow_img = new QPixmap();

    mtl->setUpArrowOffset(upArrowPoint);
    mtl->setDownArrowOffset(downArrowPoint);
    mtl->setLeftArrowOffset(leftArrowPoint);
    mtl->setRightArrowOffset(rightArrowPoint);
    mtl->setArrowImages(*uparrow_img, *downarrow_img, *leftarrow_img,
                        *rightarrow_img);

    delete uparrow_img;
    delete downarrow_img;
    delete leftarrow_img;
    delete rightarrow_img;

    // Add in the icon images
    TreeIcon *icon;
    while ((icon=iconList.first()) != 0)
    {
        mtl->addIcon(icon->i, icon->img);
        iconList.remove();
    }
    
    mtl->makeHighlights();
    mtl->calculateScreenArea();
    container->AddType(mtl);
}

void XMLParse::parsePushButton(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QPoint pos = QPoint(0, 0);
    QPixmap *image_on = NULL;
    QPixmap *image_off = NULL;
    QPixmap *image_pushed = NULL;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "PushButton needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "PushButton needs an order\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "position")
            {
                pos = parsePoint(getFirstText(info));
                pos.setX((int)(pos.x() * wmult));
                pos.setY((int)(pos.y() * hmult));
            }
            else if (info.tagName() == "image")
            {
                QString imgname = "";
                QString file = "";

                imgname = info.attribute("function", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image in a push button needs a function\n";
                    return;
                }

                file = info.attribute("filename", "");
                if (file.isNull() || file.isEmpty())
                {
                    cerr << "Image in a push button needs a filename\n";
                    return;
                }

                if (imgname.lower() == "on")
                {
                    image_on = gContext->LoadScalePixmap(file);
                    if (!image_on)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "off")
                {
                    image_off = gContext->LoadScalePixmap(file);
                    if (!image_off)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "pushed")
                {
                    image_pushed = gContext->LoadScalePixmap(file);
                    if (!image_pushed)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in PushButton\n";
                return;
            }
        }
    }

    if (!image_on)
        image_on = new QPixmap();
    if (!image_off)
        image_off = new QPixmap();
    if (!image_pushed)
        image_pushed = new QPixmap();

    UIPushButtonType *pbt = new UIPushButtonType(name, *image_on, *image_off,
                                                 *image_pushed);

    delete image_on;
    delete image_off;
    delete image_pushed;

    pbt->SetScreen(wmult, hmult);
    pbt->setPosition(pos);
    pbt->SetOrder(order.toInt());
    if (context != -1)
    {
        pbt->SetContext(context);
    }
    pbt->SetParent(container);
    pbt->calculateScreenArea();
    container->AddType(pbt);
}

void XMLParse::parseTextButton(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QString font = "";
    QPoint pos = QPoint(0, 0);
    QPixmap *image_on = NULL;
    QPixmap *image_off = NULL;
    QPixmap *image_pushed = NULL;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "TextButton needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "TextButton needs an order\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "position")
            {
                pos = parsePoint(getFirstText(info));
                pos.setX((int)(pos.x() * wmult));
                pos.setY((int)(pos.y() * hmult));
            }
            else if (info.tagName() == "font")
            {
                font = getFirstText(info);
            }
            else if (info.tagName() == "image")
            {
                QString imgname = "";
                QString file = "";

                imgname = info.attribute("function", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image in a text button needs a function\n";
                    return;
                }

                file = info.attribute("filename", "");
                if (file.isNull() || file.isEmpty())
                {
                    cerr << "Image in a text button needs a filename\n";
                    return;
                }

                if (imgname.lower() == "on")
                {
                    image_on = gContext->LoadScalePixmap(file);
                    if (!image_on)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "off")
                {
                    image_off = gContext->LoadScalePixmap(file);
                    if (!image_off)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "pushed")
                {
                    image_pushed = gContext->LoadScalePixmap(file);

                    if (!image_pushed)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in TextButton\n";
                return;
            }
        }
    }

    fontProp *testfont = GetFont(font);
    if (!testfont)
    {
        cerr << "Unknown font: " << font << " in textbutton: " << name << endl;
        return;
    }

    if (!image_on)
        image_on = new QPixmap();
    if (!image_off)
        image_off = new QPixmap();
    if (!image_pushed)
        image_pushed = new QPixmap();

    UITextButtonType *tbt = new UITextButtonType(name, *image_on, *image_off,
                                                 *image_pushed);

    delete image_on;
    delete image_off;
    delete image_pushed;

    tbt->SetScreen(wmult, hmult);
    tbt->setPosition(pos);
    tbt->setFont(testfont);
    tbt->SetOrder(order.toInt());
    if (context != -1)
    {
        tbt->SetContext(context);
    }
    tbt->SetParent(container);
    tbt->calculateScreenArea();
    container->AddType(tbt);
}

void XMLParse::parseCheckBox(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QPoint pos = QPoint(0, 0);
    QPixmap *image_checked = NULL;
    QPixmap *image_unchecked = NULL;
    QPixmap *image_checked_high = NULL;
    QPixmap *image_unchecked_high = NULL;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "CheckBox needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "CheckBox needs an order\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "position")
            {
                pos = parsePoint(getFirstText(info));
                pos.setX((int)(pos.x() * wmult));
                pos.setY((int)(pos.y() * hmult));
            }
            else if (info.tagName() == "image")
            {
                QString imgname = "";
                QString file = "";

                imgname = info.attribute("function", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image in a CheckBox needs a function\n";
                    return;
                }

                file = info.attribute("filename", "");
                if (file.isNull() || file.isEmpty())
                {
                    cerr << "Image in a CheckBox needs a filename\n";
                    return;
                }

                if (imgname.lower() == "checked")
                {
                    image_checked = gContext->LoadScalePixmap(file);
                    if (!image_checked)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "unchecked")
                {
                    image_unchecked = gContext->LoadScalePixmap(file);
                    if (!image_unchecked)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "checked_high")
                {
                    image_checked_high = gContext->LoadScalePixmap(file);
                    if (!image_checked_high)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "unchecked_high")
                {
                    image_unchecked_high = gContext->LoadScalePixmap(file);
                    if (!image_unchecked_high)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in CheckBox\n";
                return;
            }
        }
    }

    if (!image_checked)
        image_checked = new QPixmap();
    if (!image_unchecked)
        image_unchecked = new QPixmap();
    if (!image_checked_high)
        image_checked_high = new QPixmap();
    if (!image_unchecked_high)
        image_unchecked_high = new QPixmap();

    UICheckBoxType *cbt = new UICheckBoxType(name,
                                             *image_checked, *image_unchecked,
                                             *image_checked_high, *image_unchecked_high);

    delete image_checked;
    delete image_unchecked;
    delete image_checked_high;
    delete image_unchecked_high;

    cbt->SetScreen(wmult, hmult);
    cbt->setPosition(pos);
    cbt->SetOrder(order.toInt());
    if (context != -1)
    {
        cbt->SetContext(context);
    }
    cbt->SetParent(container);
    cbt->calculateScreenArea();
    container->AddType(cbt);
}

void XMLParse::parseSelector(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QString font = "";
    QRect area;
    QPixmap *image_on = NULL;
    QPixmap *image_off = NULL;
    QPixmap *image_pushed = NULL;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "Selector needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "Selector needs an order\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "font")
            {
                font = getFirstText(info);
            }
            else if (info.tagName() == "image")
            {
                QString imgname = "";
                QString file = "";

                imgname = info.attribute("function", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image in a selector needs a function\n";
                    return;
                }

                file = info.attribute("filename", "");
                if (file.isNull() || file.isEmpty())
                {
                    cerr << "Image in a selector needs a filename\n";
                    return;
                }

                if (imgname.lower() == "on")
                {
                    image_on = gContext->LoadScalePixmap(file);
                    if (!image_on)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "off")
                {
                    image_off = gContext->LoadScalePixmap(file);
                    if (!image_off)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
                if (imgname.lower() == "pushed")
                {
                    image_pushed = gContext->LoadScalePixmap(file);

                    if (!image_pushed)
                    {
                        cerr << "xmparse.o: I can't find a file called " << file << endl ;
                    }
                }
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in Selector\n";
                return;
            }
        }
    }

    fontProp *testfont = GetFont(font);
    if (!testfont)
    {
        cerr << "Unknown font: " << font << " in Selector: " << name << endl;
        return;
    }

    if (!image_on)
        image_on = new QPixmap();
    if (!image_off)
        image_off = new QPixmap();
    if (!image_pushed)
        image_pushed = new QPixmap();

    UISelectorType *st = new UISelectorType(name, *image_on, *image_off,
                                                 *image_pushed, area);

    delete image_on;
    delete image_off;
    delete image_pushed;

    st->SetScreen(wmult, hmult);
    st->setPosition(area.topLeft());
    st->setFont(testfont);
    st->SetOrder(order.toInt());
    if (context != -1)
    {
        st->SetContext(context);
    }
    st->SetParent(container);
    st->calculateScreenArea();
    container->AddType(st);
}


void XMLParse::parseBlackHole(LayerSet *container, QDomElement &element)
{
    QRect area;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "BlackHole needs a name\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else
            {
                cerr << "Unknown: " << info.tagName() << " in Black Hole\n";
                return;
            }
        }
    }


    UIBlackHoleType *bh = new UIBlackHoleType(name);
    bh->SetScreen(wmult, hmult);
    bh->setArea(area);
    bh->SetParent(container);
    bh->calculateScreenArea();
    container->AddType(bh);
}

void XMLParse::parseListBtnArea(LayerSet *container, QDomElement &element)
{
    int context = -1;
    QRect   area = QRect(0,0,0,0);
    QString fontActive;
    QString fontInactive;
    QString align = QString::null;
    bool    showArrow = true;
    bool    showScrollArrows = false;
    int     draworder = 0;
    QColor  grUnselectedBeg(Qt::black);
    QColor  grUnselectedEnd(80,80,80);
    uint    grUnselectedAlpha(100);
    QColor  grSelectedBeg(82,202,56);
    QColor  grSelectedEnd(52,152,56);
    uint    grSelectedAlpha(255);
    int     spacing = 2;
    int     margin = 3;

    QString name = element.attribute("name", "");
    if (name.isEmpty()) {
        std::cerr << "ListBtn area needs a name" << std::endl;
        return;
    }

    QString layerNum = element.attribute("draworder", "");
    if (layerNum.isNull() || layerNum.isEmpty())
    {
        cerr << "ListBtn area needs a draworder\n";
        return;
    }

    draworder = layerNum.toInt();
    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling()) {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "fcnfont")
            {
                QString fontName = info.attribute("name", "");
                QString fontFcn  = info.attribute("function", "");

                if (fontFcn.lower() == "active")
                    fontActive = fontName;
                else if (fontFcn.lower() == "inactive")
                    fontInactive = fontName;
                else {
                    std::cerr << "Unknown font function for listbtn area: "
                              << fontFcn
                              << std::endl;
                    return;
                }
            }
            else if (info.tagName() == "showarrow") {
                if (getFirstText(info).lower() == "no")
                    showArrow = false;
            }
            else if (info.tagName() == "align")
            {
                align = getFirstText(info);
            }
            else if (info.tagName() == "showscrollarrows") {
                if (getFirstText(info).lower() == "yes")
                    showScrollArrows = true;
            }
            else if (info.tagName() == "gradient") {

                if (info.attribute("type","").lower() == "selected") {
                    grSelectedBeg = QColor(info.attribute("start"));
                    grSelectedEnd = QColor(info.attribute("end"));
                    grSelectedAlpha = info.attribute("alpha","255").toUInt();
                }
                else if (info.attribute("type","").lower() == "unselected") {
                    grUnselectedBeg = QColor(info.attribute("start"));
                    grUnselectedEnd = QColor(info.attribute("end"));
                    grUnselectedAlpha = info.attribute("alpha","100").toUInt();
                }
                else {
                    std::cerr << "Unknown type for gradient in listbtn area"
                              << std::endl;
                    return;
                }

                if (!grSelectedBeg.isValid() || !grSelectedEnd.isValid() ||
                    !grUnselectedBeg.isValid() || !grUnselectedEnd.isValid()) {
                    std::cerr << "Unknown color for gradient in listbtn area"
                              << std::endl;
                    return;
                }

                if (grSelectedAlpha > 255 || grUnselectedAlpha > 255) {
                    std::cerr << "Incorrect alpha for gradient in listbtn area"
                              << std::endl;
                    return;
                }
            }
            else if (info.tagName() == "spacing") {
                spacing = getFirstText(info).toInt();
            }
            else if (info.tagName() == "margin") {
                margin = getFirstText(info).toInt();
            }
            else
            {
                std::cerr << "Unknown tag in listbtn area: "
                          << info.tagName() << endl;
                return;
            }

        }
    }

    int jst = Qt::AlignLeft | Qt::AlignVCenter;

    if (!align.isEmpty())
    {
        if (align.lower() == "center")
            jst = Qt::AlignCenter | Qt::AlignVCenter;
        else if (align.lower() == "right")
            jst = Qt::AlignRight  | Qt::AlignVCenter;
    	else if (align.lower() == "left")
            jst = Qt::AlignLeft   | Qt::AlignVCenter;
    }

    fontProp *fpActive = GetFont(fontActive);
    if (!fpActive)
    {
        cerr << "Unknown font: " << fontActive
             << " in listbtn area: " << name << endl;
        return;
    }

    fontProp *fpInactive = GetFont(fontInactive);
    if (!fpInactive)
    {
        cerr << "Unknown font: " << fontInactive
             << " in listbtn area: " << name << endl;
        return;
    }


    UIListBtnType *l = new UIListBtnType(name, area, draworder, showArrow,
                                         showScrollArrows);
    l->SetScreen(wmult, hmult);
    l->SetFontActive(fpActive);
    l->SetFontInactive(fpInactive);
    l->SetJustification(jst);
    l->SetItemRegColor(grUnselectedBeg, grUnselectedEnd, grUnselectedAlpha);
    l->SetItemSelColor(grSelectedBeg, grSelectedEnd, grSelectedAlpha);
    l->SetSpacing((int)(spacing*hmult));
    l->SetMargin((int)(margin*wmult));
    l->SetParent(container);
    l->SetContext(context);
    l->calculateScreenArea();

    container->AddType(l);
    container->bumpUpLayers(0);
}

void XMLParse::parseListTreeArea(LayerSet *container, QDomElement &element)
{
    int     context = -1;
    QRect   area = QRect(0,0,0,0);
    QRect   listsize = QRect(0,0,0,0);
    int     leveloffset = 0;
    QString fontActive;
    QString fontInactive;
    bool    showArrow = true;
    bool    showScrollArrows = false;
    int     draworder = 0;
    QColor  grUnselectedBeg(Qt::black);
    QColor  grUnselectedEnd(80,80,80);
    uint    grUnselectedAlpha(100);
    QColor  grSelectedBeg(82,202,56);
    QColor  grSelectedEnd(52,152,56);
    uint    grSelectedAlpha(255);
    int     spacing = 2;
    int     margin = 3;

    QString name = element.attribute("name", "");
    if (name.isEmpty()) {
        std::cerr << "ListTreeArea area needs a name" << std::endl;
        return;
    }

    QString layerNum = element.attribute("draworder", "");
    if (layerNum.isNull() || layerNum.isEmpty())
    {
        cerr << "ListTreeArea needs a draworder\n";
        return;
    }

    draworder = layerNum.toInt();
    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling()) {
        QDomElement info = child.toElement();
        if (!info.isNull())
        {
            if (info.tagName() == "area")
            {
                area = parseRect(getFirstText(info));
                normalizeRect(area);
            }
            else if (info.tagName() == "context")
            {
                context = getFirstText(info).toInt();
            }
            else if (info.tagName() == "listsize")
            {
                listsize = parseRect(getFirstText(info));
                normalizeRect(listsize);
            }
            else if (info.tagName() == "leveloffset")
            {
                leveloffset = getFirstText(info).toInt();
            }
            else if (info.tagName() == "fcnfont")
            {
                QString fontName = info.attribute("name", "");
                QString fontFcn  = info.attribute("function", "");

                if (fontFcn.lower() == "active")
                    fontActive = fontName;
                else if (fontFcn.lower() == "inactive")
                    fontInactive = fontName;
                else {
                    std::cerr << "Unknown font function for ListTreeArea: "
                              << fontFcn
                              << std::endl;
                    return;
                }
            }
            else if (info.tagName() == "showarrow") {
                if (getFirstText(info).lower() == "no")
                    showArrow = false;
            }
            else if (info.tagName() == "showscrollarrows") {
                if (getFirstText(info).lower() == "yes")
                    showScrollArrows = true;
            }
            else if (info.tagName() == "gradient") {

                if (info.attribute("type","").lower() == "selected") {
                    grSelectedBeg = QColor(info.attribute("start"));
                    grSelectedEnd = QColor(info.attribute("end"));
                    grSelectedAlpha = info.attribute("alpha","255").toUInt();
                }
                else if (info.attribute("type","").lower() == "unselected") {
                    grUnselectedBeg = QColor(info.attribute("start"));
                    grUnselectedEnd = QColor(info.attribute("end"));
                    grUnselectedAlpha = info.attribute("alpha","100").toUInt();
                }
                else {
                    std::cerr << "Unknown type for gradient in ListTreeArea"
                              << std::endl;
                    return;
                }

                if (!grSelectedBeg.isValid() || !grSelectedEnd.isValid() ||
                    !grUnselectedBeg.isValid() || !grUnselectedEnd.isValid()) {
                    std::cerr << "Unknown color for gradient in ListTreeArea area"
                              << std::endl;
                    return;
                }

                if (grSelectedAlpha > 255 || grUnselectedAlpha > 255) {
                    std::cerr << "Incorrect alpha for gradient in ListTreeArea area"
                              << std::endl;
                    return;
                }
            }
            else if (info.tagName() == "spacing") {
                spacing = getFirstText(info).toInt();
            }
            else if (info.tagName() == "margin") {
                margin = getFirstText(info).toInt();
            }
            else
            {
                std::cerr << "Unknown tag in ListTreeArea: "
                          << info.tagName() << endl;
                return;
            }

        }
    }

    fontProp *fpActive = GetFont(fontActive);
    if (!fpActive)
    {
        cerr << "Unknown font: " << fontActive
             << " in ListTreeArea: " << name << endl;
        return;
    }

    fontProp *fpInactive = GetFont(fontInactive);
    if (!fpInactive)
    {
        cerr << "Unknown font: " << fontInactive
             << " in ListTreeArea: " << name << endl;
        return;
    }


    UIListTreeType *l = new UIListTreeType(name, area, listsize, leveloffset,
                                           draworder);

    l->SetScreen(wmult, hmult);
    l->SetFontActive(fpActive);
    l->SetFontInactive(fpInactive);
    l->SetItemRegColor(grUnselectedBeg, grUnselectedEnd, grUnselectedAlpha);
    l->SetItemSelColor(grSelectedBeg, grSelectedEnd, grSelectedAlpha);
    l->SetSpacing((int)(spacing*hmult));
    l->SetMargin((int)(margin*wmult));
    l->SetParent(container);
    l->calculateScreenArea();
    l->SetContext(context);
    
    container->AddType(l);
    container->bumpUpLayers(0);
}

void XMLParse::parseKey(LayerSet *container, QDomElement &element)
{
    QPixmap *normalImage = NULL, *focusedImage = NULL;
    QPixmap *downImage = NULL, *downFocusedImage = NULL;
    QString normalFontName = "", focusedFontName = "";
    QString downFontName = "", downFocusedFontName = "";
    QString name, order, type;
    QString normalChar = "", shiftChar = "";
    QString altChar = "", shiftaltChar = "";
    QString moveLeft = "", moveRight = "";
    QString moveUp = "", moveDown = "";
    QPoint pos = QPoint(0, 0);

    name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "key needs a name\n";
        return;
    }

    type = element.attribute("type", "");
    if (type.isNull() || type.isEmpty())
    {
        cerr << "key needs a type\n";
        return;
    }

    order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "key needs an order\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement e = child.toElement();
        if (!e.isNull())
        {
            if (e.tagName() == "position")
            {
                pos = parsePoint(getFirstText(e));
                pos.setX((int)(pos.x() * wmult));
                pos.setY((int)(pos.y() * hmult));

            }
            else if (e.tagName() == "char")
            {
                normalChar = e.attribute("normal", "");
                shiftChar = e.attribute("shift", "");
                altChar = e.attribute("alt", "");
                shiftaltChar = e.attribute("altshift", "");
            }
            else if (e.tagName() == "move")
            {
                moveLeft = e.attribute("left", "");
                moveRight = e.attribute("right", "");
                moveUp = e.attribute("up", "");
                moveDown = e.attribute("down", "");
            }
            else if (e.tagName() == "image")
            {
                QString imgname = "";
                QString imgfunction = "";

                imgfunction = e.attribute("function", "");
                if (imgfunction.isNull() || imgfunction.isEmpty())
                {
                    cerr << "Image in a key needs a function\n";
                    return;
                }

                imgname = e.attribute("filename", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image in a key needs a filename\n";
                    return;
                }

                if (imgfunction.lower() == "normal")
                {
                    normalImage = gContext->LoadScalePixmap(imgname);
                    if (!normalImage)
                    {
                        cerr << "xmparse.o: I can't find a file called " 
                             << imgname << endl;
                    }
                }
                else if (imgfunction.lower() == "focused")
                {
                    focusedImage = gContext->LoadScalePixmap(imgname);
                    if (!focusedImage)
                    {
                        cerr << "xmparse.o: I can't find a file called " 
                             << imgname << endl;
                    }
                }
                else if (imgfunction.lower() == "down")
                {
                    downImage = gContext->LoadScalePixmap(imgname);

                    if (!downImage)
                    {
                        cerr << "xmparse.o: I can't find a file called " 
                             << imgname << endl;
                    }
                }
                else if (imgfunction.lower() == "downfocused")
                {
                    downFocusedImage = gContext->LoadScalePixmap(imgname);

                    if (!downFocusedImage)
                    {
                        cerr << "xmparse.o: I can't find a file called " 
                             << imgname << endl;
                    }
                }
                else
                {
                    std::cerr << "Unknown image function in key type: "
                              << imgfunction << endl;
                    return;
                }
            }
            else if (e.tagName() == "fcnfont")
            {
                QString fontName = e.attribute("name", "");
                QString fontFcn  = e.attribute("function", "");

                if (fontFcn.lower() == "normal")
                    normalFontName = fontName;
                else if (fontFcn.lower() == "focused")
                    focusedFontName = fontName;
                else if (fontFcn.lower() == "down")
                    downFontName = fontName;
                else if (fontFcn.lower() == "downfocused")
                    downFocusedFontName = fontName;
                else
                {
                    cerr << "Unknown font function in key type: "
                         << fontFcn << endl;
                    return;
                }
            }
            else
            {
                cerr << "Unknown: " << e.tagName() << " in key\n";
                return;
            }
        }
    }

    fontProp *normalFont = GetFont(normalFontName);
    fontProp *focusedFont = GetFont(focusedFontName);
    fontProp *downFont = GetFont(downFontName);
    fontProp *downFocusedFont = GetFont(downFocusedFontName);

    UIKeyType *key = new UIKeyType(name);
    key->SetScreen(wmult, hmult);
    key->SetParent(container);
    key->SetOrder(order.toInt());
    key->SetType(type);
    key->SetChars(normalChar, shiftChar, altChar, shiftaltChar);
    key->SetMoves(moveLeft, moveRight, moveUp, moveDown);
    key->SetPosition(pos);
    key->SetImages(normalImage, focusedImage, downImage, downFocusedImage);
    key->SetFonts(normalFont, focusedFont, downFont, downFocusedFont);

    container->AddType(key);
}

void XMLParse::parseKeyboard(LayerSet *container, QDomElement &element)
{
    QString normalFontName = "", focusedFontName = ""; 
    QString downFontName = "", downFocusedFontName = "";
    int context = -1;
    QRect area;
    QPixmap *normalImage = NULL, *focusedImage = NULL; 
    QPixmap *downImage = NULL, *downFocusedImage = NULL;

    QString name = element.attribute("name", "");
    if (name.isNull() || name.isEmpty())
    {
        cerr << "keyboard needs a name\n";
        return;
    }

    QString order = element.attribute("draworder", "");
    if (order.isNull() || order.isEmpty())
    {
        cerr << "keyboard needs an order\n";
        return;
    }

    for (QDomNode child = element.firstChild(); !child.isNull();
         child = child.nextSibling())
    {
        QDomElement e = child.toElement();
        if (!e.isNull())
        {
            if (e.tagName() == "key")
            {
                parseKey(container, e);
            }
            else if (e.tagName() == "area")
            {
                area = parseRect(getFirstText(e));
                normalizeRect(area);
            }
            else if (e.tagName() == "context")
            {
                context = getFirstText(e).toInt();
            }

            else if (e.tagName() == "image")
            {
                QString imgname = "";
                QString imgfunction = "";

                imgfunction = e.attribute("function", "");
                if (imgfunction.isNull() || imgfunction.isEmpty())
                {
                    cerr << "Image in a keyboard needs a function\n";
                    return;
                }

                imgname = e.attribute("filename", "");
                if (imgname.isNull() || imgname.isEmpty())
                {
                    cerr << "Image in a keyboard needs a filename\n";
                    return;
                }

                if (imgfunction.lower() == "normal")
                {
                    normalImage = gContext->LoadScalePixmap(imgname);
                    if (!normalImage)
                    {
                        cerr << "xmparse.o: I can't find a file called " << imgname << endl ;
                    }
                }
                else if (imgfunction.lower() == "focused")
                {
                    focusedImage = gContext->LoadScalePixmap(imgname);
                    if (!focusedImage)
                    {
                        cerr << "xmparse.o: I can't find a file called " << imgname << endl ;
                    }
                }
                else if (imgfunction.lower() == "down")
                {
                    downImage = gContext->LoadScalePixmap(imgname);

                    if (!downImage)
                    {
                        cerr << "xmparse.o: I can't find a file called " << imgname << endl ;
                    }
                }
                else if (imgfunction.lower() == "downfocused")
                {
                    downFocusedImage = gContext->LoadScalePixmap(imgname);

                    if (!downFocusedImage)
                    {
                        cerr << "xmparse.o: I can't find a file called " << imgname << endl ;
                    }
                }
                else
                {
                    std::cerr << "Unknown image function in keyboard type: "
                              << imgfunction
                              << std::endl;
                    return;
                }
            }
            else if (e.tagName() == "fcnfont")
            {
                QString fontName = e.attribute("name", "");
                QString fontFcn  = e.attribute("function", "");

                if (fontFcn.lower() == "normal")
                    normalFontName = fontName;
                else if (fontFcn.lower() == "focused")
                    focusedFontName = fontName;
                else if (fontFcn.lower() == "down")
                    downFontName = fontName;
                else if (fontFcn.lower() == "downfocused")
                    downFocusedFontName = fontName;
                else 
                {
                    std::cerr << "Unknown font function in keyboard type: "
                              << fontFcn
                              << std::endl;
                    return;
                }
            }
            else
            {
                cerr << "Unknown: " << e.tagName() << " in keyboard\n";
                return;
            }
        }
    }

    if (normalFontName == "")
    {
      cerr << "Keyboard need a normal font" << endl;
      return;
    }

    if (focusedFontName == "")
        focusedFontName = normalFontName;

    if (downFontName == "")
        downFontName = normalFontName;

    if (downFocusedFontName == "")
        downFocusedFontName = normalFontName;

    fontProp *normalFont = GetFont(normalFontName);
    if (!normalFont)
    {
      cerr << "Unknown font: " << normalFontName
           << " in Keyboard: " << name << endl;
      return;
    }

    fontProp *focusedFont = GetFont(focusedFontName);
    if (!focusedFont)
    {
        cerr << "Unknown font: " << focusedFontName
                << " in Keyboard: " << name << endl;
        return;
    }

    fontProp *downFont = GetFont(downFontName);
    if (!downFont)
    {
        cerr << "Unknown font: " << downFontName
                << " in Keyboard: " << name << endl;
        return;
    }

    fontProp *downFocusedFont = GetFont(downFocusedFontName);
    if (!downFocusedFont)
    {
        cerr << "Unknown font: " << downFocusedFontName
                << " in Keyboard: " << name << endl;
        return;
    }

    UIKeyboardType *kbd = new UIKeyboardType(name, order.toInt());
    kbd->SetScreen(wmult, hmult);
    kbd->SetParent(container);
    kbd->SetContext(context);
    kbd->SetArea(area);
    kbd->calculateScreenArea();

    container->AddType(kbd);

    vector<UIType *>::iterator i = container->getAllTypes()->begin();
    for (; i != container->getAllTypes()->end(); i++)
    {
        UIType *type = (*i);
        if (UIKeyType *keyt = dynamic_cast<UIKeyType*>(type))
        {
            kbd->AddKey(keyt);
            keyt->SetDefaultImages(normalImage, focusedImage, downImage, downFocusedImage);
            keyt->SetDefaultFonts(normalFont, focusedFont, downFont, downFocusedFont);
            keyt->calculateScreenArea();
        }
    }
}

// vim:set sw=4 expandtab:
