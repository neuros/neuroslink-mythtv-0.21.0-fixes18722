#include <iostream>
using namespace std;

#include <qpixmap.h>
#include <qimage.h>
#include <qapplication.h>

#include "progdetails.h"
#include "mythcontext.h"
#include "mythdialogs.h"
#include "uitypes.h"

ProgDetails::ProgDetails(MythMainWindow *parent,
                         QString windowName,
                         QString details)
    : MythThemedDialog(parent, windowName)
{
    m_details = details;

    wireUpTheme();
    assignFirstFocus();

    if (m_richText)
    {
        m_richText->SetText(m_details);
        m_richText->SetBackground(&my_background);
    }
}

void ProgDetails::setDetails(const QString &details)
{
    m_details = details;

    if (m_richText)
    {
        m_richText->SetText(m_details);
    }
}

QString ProgDetails::themeText(const QString &fontName, const QString &text, int size)
{
    if (size < 1) size = 1;
    if (size > 7) size = 7;

    XMLParse *theme = getTheme();
    if (!theme)
        return text;

    fontProp *font = getFont(fontName);

    if (!font)
        return text;

    QString res = QString("<font color=\"%1\" face=\"%2\" size=\"%3\"</font>")
            .arg(font->color.name())
            .arg(font->face.family())
            .arg(size);

    bool bItalic = font->face.italic();
    bool bBold = font->face.bold();
    bool bUnderline = font->face.underline();

    if (bItalic)
        res += "<i>";
    if (bBold)
        res += "<b>";
    if (bUnderline)
        res += "<u>";

    res += text;

    if (bItalic)
        res += "</i>";
    if (bBold)
        res += "</b>";
    if (bUnderline)
        res += "</u>";

    return res;
}

ProgDetails::~ProgDetails(void)
{
}

void ProgDetails::keyPressEvent(QKeyEvent *e)
{
    bool handled = false;
    QStringList actions;
    if (gContext->GetMainWindow()->TranslateKeyPress("qt", e, actions))
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;
            if (action == "ESCAPE" || action == "SELECT")
                reject();
            else if (action == "UP")
            {
                if (getCurrentFocusWidget() == m_richText)
                    m_richText->ScrollUp();
                else
                    nextPrevWidgetFocus(false);
            }
            else if (action == "DOWN")
            {
                if (getCurrentFocusWidget() == m_richText)
                    m_richText->ScrollDown();
                else
                    nextPrevWidgetFocus(true);
            }
            else if (action == "LEFT")
            {
                nextPrevWidgetFocus(false);
            }
            else if (action == "RIGHT")
            {
                nextPrevWidgetFocus(true);
            }
            else if (action == "PAGEUP")
            {
                if (getCurrentFocusWidget() == m_richText)
                    m_richText->ScrollPageUp();
                else
                    nextPrevWidgetFocus(false);
            }
            else if (action == "PAGEDOWN")
            {
                if (getCurrentFocusWidget() == m_richText)
                    m_richText->ScrollPageDown();
                else
                    nextPrevWidgetFocus(true);
            }

            else
                handled = false;
        }
    }
}

void ProgDetails::wireUpTheme()
{
    m_okButton = getUITextButtonType("ok_button");
    if (m_okButton)
    {
        m_okButton->setText(tr("OK"));
        connect(m_okButton, SIGNAL(pushed()), this, SLOT(accept()));
    }

    m_richText = getUIRichTextType("richtext");

    buildFocusList();
}
