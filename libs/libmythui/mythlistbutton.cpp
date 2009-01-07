#include <qapplication.h>
#include <qpainter.h>
#include <qpixmap.h>

#include <cassert>

#include "mythcontext.h"
#include "mythlistbutton.h"
#include "mythmainwindow.h"
#include "mythcontext.h"
#include "mythfontproperties.h"
#include "mythuistatetype.h"
#include "mythuibutton.h"

MythListButton::MythListButton(MythUIType *parent, const char *name)
              : MythUIType(parent, name)
{
    m_showArrow = true;
    m_showScrollArrows = false;

    Const();
}

MythListButton::MythListButton(MythUIType *parent, const char *name, 
                               const QRect& area, bool showArrow, 
                               bool showScrollArrows)
              : MythUIType(parent, name)
{
    m_Area             = area;

    m_showArrow        = showArrow;
    m_showScrollArrows = showScrollArrows;

    Const();
}

void MythListButton::Const(void)
{
    m_layout = LayoutVertical;

    m_active           = false;
    m_drawFromBottom   = false;

    m_itemList.setAutoDelete(false);
    m_topItem = 0;
    m_selItem = 0;
    m_selIterator = new QPtrListIterator<MythListButtonItem>(m_itemList);
    m_topIterator = new QPtrListIterator<MythListButtonItem>(m_itemList);
    m_selPosition = 0;
    m_topPosition = 0;
    m_itemCount = 0;

    m_initialized      = false;
    m_clearing         = false;
    m_itemHorizSpacing = 0;
    m_itemVertSpacing  = 0;
    m_itemMargin       = 0;
    m_itemHeight       = 0;
    m_itemsVisible     = 0;
    m_columns          = 0;
    m_rows             = 0;

    m_upArrow = m_downArrow = NULL;

    m_textFlags = Qt::AlignLeft | Qt::AlignVCenter;

    m_fontActive = new MythFontProperties();
    m_fontInactive = new MythFontProperties();

    arrowPix = checkNonePix = checkHalfPix = checkFullPix = NULL;
    itemRegPix = itemSelActPix = itemSelInactPix = NULL;

    SetCanTakeFocus(true);

    connect(this, SIGNAL(TakingFocus()), this, SLOT(Select()));
    connect(this, SIGNAL(LosingFocus()), this, SLOT(Deselect()));
}

MythListButton::~MythListButton()
{
    m_clearing = true;
    MythListButtonItem* item = 0;
    for (item = m_itemList.first(); item; item = m_itemList.next())
        delete item;
    m_itemList.clear();

    delete m_topIterator;
    delete m_selIterator;

    if (m_fontActive)
       delete m_fontActive; 
    if (m_fontInactive)
       delete m_fontInactive; 

    if (itemRegPix)
        itemRegPix->DownRef();
    if (itemSelActPix)
        itemSelActPix->DownRef();
    if (itemSelInactPix)
        itemSelInactPix->DownRef();
    if (arrowPix)
        arrowPix->DownRef();
    if (checkNonePix)
        checkNonePix->DownRef();
    if (checkHalfPix)
        checkHalfPix->DownRef();
    if (checkFullPix)
        checkFullPix->DownRef();
}

void MythListButton::SetFontActive(const MythFontProperties &font)
{
    *m_fontActive   = font;
}

void MythListButton::SetFontInactive(const MythFontProperties &font)
{
    *m_fontInactive = font;
}

void MythListButton::SetSpacing(int spacing)
{
    m_itemHorizSpacing = NormX(spacing);
    m_itemVertSpacing = NormY(spacing);
}

void MythListButton::SetMargin(int margin)
{
    m_itemMargin = margin;    
}

void MythListButton::SetDrawFromBottom(bool draw)
{
    m_drawFromBottom = draw;
}

void MythListButton::SetTextFlags(int flags)
{
    m_textFlags = flags;
}

void MythListButton::SetActive(bool active)
{
    m_active = active;
    if (m_initialized)
        SetPositionArrowStates();
}

void MythListButton::Reset()
{
    m_clearing = true;

    MythListButtonItem* item = 0;
    for (item = m_itemList.first(); item; item = m_itemList.next())
        delete item;

    m_clearing = false;
    m_itemList.clear();
    
    m_topItem     = 0;
    m_selItem     = 0;
    m_selPosition = 0;
    m_topPosition = 0;
    m_itemCount   = 0;
    m_selIterator->toFirst(); 
    m_topIterator->toFirst();

    SetPositionArrowStates();
}

void MythListButton::SetPositionArrowStates(void)
{
    if (!m_initialized)
        Init();

    if (m_ButtonList.size() > 0)
    {
        int button = 0;
    
        if (m_drawFromBottom && m_itemCount < (int)m_itemsVisible)
            button = m_itemsVisible - m_itemCount;

        for (int i = 0; i < button; i++)
             m_ButtonList[i]->SetVisible(false);

        QPtrListIterator<MythListButtonItem> it = (*m_topIterator);
        while (it.current() && button < (int)m_itemsVisible)
        {
            MythUIButton *realButton = m_ButtonList[button];
            MythListButtonItem *buttonItem = it.current();

            buttonItem->SetToRealButton(realButton, true);
            realButton->SetVisible(true);

            button++;
            ++it;
        }

        for (; button < (int)m_itemsVisible; button++)
            m_ButtonList[button]->SetVisible(false);
    }

    if (!m_showScrollArrows || !m_downArrow || !m_upArrow)
        return;

    if (m_itemCount == 0)
    {
        m_downArrow->DisplayState(MythUIStateType::Off);
        m_upArrow->DisplayState(MythUIStateType::Off);
    }
    else
    {
        if (m_topItem != m_itemList.first())
            m_upArrow->DisplayState(MythUIStateType::Full);
        else
            m_upArrow->DisplayState(MythUIStateType::Off);
    
        if (m_topPosition + (int)m_itemsVisible < m_itemCount)
            m_downArrow->DisplayState(MythUIStateType::Full);
        else
            m_downArrow->DisplayState(MythUIStateType::Off);
    }
}

void MythListButton::InsertItem(MythListButtonItem *item)
{
    MythListButtonItem* lastItem = m_itemList.last();
    m_itemList.append(item);

    m_itemCount++;

    if (!lastItem) 
    {
        m_topItem = item;
        m_selItem = item;
        m_selIterator->toFirst();
        m_topIterator->toFirst();
        m_selPosition = m_topPosition = 0;
        emit itemSelected(item);
    }

    SetPositionArrowStates();
}

void MythListButton::RemoveItem(MythListButtonItem *item)
{
    if (m_clearing)
        return;
    
    if (m_itemList.findRef(item) == -1)
        return;

    if (item == m_topItem)
    {
        if (m_topItem != m_itemList.last())
        {
            ++(*m_topIterator);
            ++m_topPosition;
            m_topItem = m_topIterator->current();
        }
        else if (m_topItem != m_itemList.first())
        {
            --(*m_topIterator);
            --m_topPosition;
            m_topItem = m_topIterator->current();
        }
        else
        {
            m_topItem = NULL;
            m_topPosition = 0;
            m_topIterator->toFirst();
        }
    }

    if (item == m_selItem)
    {
        if (m_selItem != m_itemList.last())
        {
            ++(*m_selIterator);
            ++m_selPosition;
            m_selItem = m_selIterator->current();
        }
        else if (m_selItem != m_itemList.first())
        {
            --(*m_selIterator);
            --m_selPosition;
            m_selItem = m_selIterator->current();
        }
        else
        {
            m_selItem = NULL;
            m_selPosition = 0;
            m_selIterator->toFirst();
        }
    }

    m_itemList.remove(item);
    m_itemCount--;

    SetPositionArrowStates();

    if (m_selItem) 
        emit itemSelected(m_selItem);
}

void MythListButton::SetItemCurrent(MythListButtonItem* item)
{
    m_selIterator->toFirst();
    MythListButtonItem *cur;
    bool found = false;
    m_selPosition = 0;
    while ((cur = m_selIterator->current()) != 0)
    {
        if (cur == item)
        {
            found = true;
            break;
        }

        ++(*m_selIterator);
        ++m_selPosition;
    }

    if (!found)
    {
        m_selIterator->toFirst();
        m_selPosition = 0;
    }

    m_itemCount = m_selPosition;
    m_topItem = item;
    m_selItem = item;
    (*m_topIterator) = (*m_selIterator);

    SetPositionArrowStates();

    emit itemSelected(m_selItem);
}

void MythListButton::SetItemCurrent(int current)
{
    MythListButtonItem* item = m_itemList.at(current);
    if (!item)
        item = m_itemList.first();

    SetItemCurrent(item);
}

MythListButtonItem* MythListButton::GetItemCurrent()
{
    return m_selItem;
}

MythListButtonItem* MythListButton::GetItemFirst()
{
    return m_itemList.first();    
}

MythListButtonItem* MythListButton::GetItemNext(MythListButtonItem *item)
{
    if (m_itemList.findRef(item) == -1)
        return 0;

    return m_itemList.next();
}

int MythListButton::GetCount()
{
    return m_itemCount;
}

bool MythListButton::IsEmpty()
{
    if (m_itemCount > 0)
        return false;
    else
        return true;
}

QPtrListIterator<MythListButtonItem> MythListButton::GetIterator(void)
{
    return QPtrListIterator<MythListButtonItem>(m_itemList);
}

MythListButtonItem* MythListButton::GetItemAt(int pos)
{
    return m_itemList.at(pos);    
}

int MythListButton::GetItemPos(MythListButtonItem* item)
{
    if (!item)
        return -1;
    return m_itemList.findRef(item);    
}

void MythListButton::MoveUp(MovementUnit unit)
{
    int pos = m_selPosition;
    if (pos == -1)
        return;

    switch (unit)
    {
        case MoveItem:
            if (!m_selIterator->atFirst())
            {
                --(*m_selIterator);
                --m_selPosition;
            }
            break;
        case MoveColumn:
            if ((pos+1) - (((pos+1)/m_columns)*m_columns) != 1)
            {
                --(*m_selIterator);
                --m_selPosition;
            }
            break;
        case MoveRow:
            if ((pos - m_columns) >= 0)
            {
                for (int i = 0; i < m_columns; i++)
                {
                    --(*m_selIterator);
                    --m_selPosition;
                }
            }
            break;
        case MovePage:
            if (pos > (int)m_itemsVisible)
            {
                for (int i = 0; i < (int)m_itemsVisible; i++)
                {
                    --(*m_selIterator);
                    --m_selPosition;
                }
                break;
            }
            // fall through
        case MoveMax:
            m_selIterator->toFirst();
            m_selPosition = 0;
            break;
    }

    if (!m_selIterator->current())
        return;

    m_selItem = m_selIterator->current();

    if (m_selPosition <= m_topPosition)
    {
        m_topItem = m_selItem;
        (*m_topIterator) = (*m_selIterator);
        m_topPosition = m_selPosition;
    }

    SetPositionArrowStates();

    emit itemSelected(m_selItem);
}

void MythListButton::MoveDown(MovementUnit unit)
{
    int pos = m_selPosition;
    if (pos == -1)
        return;

    switch (unit)
    {
        case MoveItem:
            if (!m_selIterator->atLast())
            {
                ++(*m_selIterator);
                ++m_selPosition;
            }
            break;
        case MoveColumn:
            if ((pos+1) - (((pos+1)/m_columns)*m_columns) > 0)
            {
                ++(*m_selIterator);
                ++m_selPosition;
            }
            break;
        case MoveRow:
            if ((pos + m_columns) < m_itemCount)
            {
                for (int i = 0; i < m_columns; i++)
                {
                    ++(*m_selIterator);
                    ++m_selPosition;
                }
            }
            break;
        case MovePage:
            //m_selPosition = m_topPosition;
            if ((pos + (int)m_itemsVisible) <= m_itemCount - 1)
            {
                for (int i = 0; i < (int)m_itemsVisible; i++)
                {
                    ++(*m_selIterator);
                    ++m_selPosition;
                }
                break;
            }
            break;
        case MoveMax:
            m_selIterator->toLast();
            m_selPosition = m_itemCount - 1;
            break;
    }

    if (!m_selIterator->current())
    {
        VERBOSE(VB_IMPORTANT, "m_selIterator has left the building.");
        return;
    }

    m_selItem = m_selIterator->current();

    // while (m_topPosition < m_selPosition)
    while (m_topPosition + (int)m_itemsVisible < m_selPosition + 1)
    {
        ++(*m_topIterator);
        ++m_topPosition;
    }

    m_topItem = m_topIterator->current();

    SetPositionArrowStates();

    emit itemSelected(m_selItem);
}

bool MythListButton::MoveToNamedPosition(const QString &position_name)
{
    if (m_selPosition < 0)
    {
        return false;
    }

    if (!m_selIterator->toFirst())
    {
        return false;
    }
    m_selPosition = 0;

    bool found_it = false;
    while(m_selIterator->current())
    {
        if (m_selIterator->current()->text() == position_name)
        {
            found_it = true;
            break;
        }
        ++(*m_selIterator);
        ++m_selPosition;
    }

    if (!found_it)
    {
        m_selPosition = -1;
        return false;
    }

    m_selItem = m_selIterator->current();

    while (m_topPosition + (int)m_itemsVisible < m_selPosition + 1) 
    {
        ++(*m_topIterator);
        ++m_topPosition;
    }
   
    m_topItem = m_topIterator->current();

    SetPositionArrowStates();

    emit itemSelected(m_selItem);

    return true;
}

bool MythListButton::MoveItemUpDown(MythListButtonItem *item, bool flag)
{
    if (item != m_selItem)
    {
        cerr << "Can't move non-selected item\n";
        return false;
    }

    if (item == m_itemList.getFirst() && flag)
        return false;
    if (item == m_itemList.getLast() && !flag)
        return false;

    int oldpos = m_selPosition;
    int insertat = 0;
    bool dolast = false;

    if (flag)
    {
        insertat = m_selPosition - 1;
        if (item == m_itemList.getLast())
            dolast = true;
        else
            ++m_selPosition;

        if (item == m_topItem)
            ++m_topPosition;
    }
    else
        insertat = m_selPosition + 1;

    if (m_itemList.current() == item)
    {
        m_itemList.take();
        //  cout << "speedy\n";
    }
    else
        m_itemList.take(oldpos);

    m_itemList.insert(insertat, item);

    if (flag)
    {
        MoveUp();
        if (!dolast)
            MoveUp();
    }
    else
        MoveDown();

    return true;
}

void MythListButton::Init()
{
    if (m_initialized)
        return;

    m_initialized = true;

    QRect arrowsRect;
    if (m_showScrollArrows && m_upArrow) 
        arrowsRect = PlaceArrows(m_upArrow->GetArea().size());
    else
        arrowsRect = QRect(0, 0, 0, 0);

    if (m_upArrow)
        m_upArrow->SetVisible(m_showScrollArrows);
    if (m_downArrow)
        m_downArrow->SetVisible(m_showScrollArrows);

    m_contentsRect = CalculateContentsRect(arrowsRect);

    QFontMetrics fm(m_fontActive->face());
    QSize sz1 = fm.size(Qt::SingleLine, "XXXXX");
    fm = QFontMetrics(m_fontInactive->face());
    QSize sz2 = fm.size(Qt::SingleLine, "XXXXX");
    m_itemHeight = QMAX(sz1.height(), sz2.height()) + (int)(2 * m_itemMargin);
    m_itemWidth = m_contentsRect.width();

    // If we have a background image and it's not a 10x10 gradient,
    // use it to define the button size
    if (itemRegPix && (itemRegPix->height() > 10))
        m_itemHeight = itemRegPix->height();
    if (itemRegPix && (itemRegPix->width() > 10))
        m_itemWidth = itemRegPix->width();

    CalculateVisibleItems();

    if (itemRegPix)
        itemRegPix->Resize(QSize(ItemWidth(), m_itemHeight));
    if (itemSelActPix)
        itemSelActPix->Resize(QSize(ItemWidth(), m_itemHeight));
    if (itemSelInactPix)
        itemSelInactPix->Resize(QSize(ItemWidth(), m_itemHeight));

    int col = 1;
    int row = 1;

    for (int i = 0; i < (int)m_itemsVisible; i++)
    {
        QString name = QString("buttonlist button %1").arg(i);
        MythUIButton *button = new MythUIButton(this, name);
        if (itemRegPix)
        {
            button->SetBackgroundImage(MythUIButton::Normal, itemRegPix);
            button->SetBackgroundImage(MythUIButton::Active, itemRegPix);
        }

        if (itemSelInactPix)
            button->SetBackgroundImage(MythUIButton::SelectedInactive,
                                   itemSelInactPix);

        if (itemSelActPix)
            button->SetBackgroundImage(MythUIButton::Selected, itemSelActPix);

        button->SetPaddingMargin(m_itemMargin);

        if (checkNonePix)
        {
            button->SetCheckImage(MythUIStateType::Off, checkNonePix);
            button->SetCheckImage(MythUIStateType::Half, checkHalfPix);
            button->SetCheckImage(MythUIStateType::Full, checkFullPix);
        }

        if (arrowPix)
            button->SetRightArrowImage(arrowPix);

        button->SetFont(MythUIButton::Normal, *m_fontActive);
        button->SetFont(MythUIButton::Disabled, *m_fontInactive);

        if (col > m_columns)
        {
            col = 1;
            row++;
        }

        button->SetPosition(GetButtonPosition(col, row));

        col++;

        m_ButtonList.push_back(button);
    }

    SetPositionArrowStates();
}

bool MythListButton::keyPressEvent(QKeyEvent *e)
{
    QStringList actions;
    bool handled = false;
    GetMythMainWindow()->TranslateKeyPress("Global", e, actions);

    for (unsigned int i = 0; i < actions.size() && !handled; i++)
    {
        QString action = actions[i];
        handled = true;

        if (action == "UP")
        {
            if ((m_layout == LayoutVertical) || (m_layout == LayoutGrid))
                MoveUp(MoveRow);
            else
                handled = false;
        }
        else if (action == "DOWN")
        {
            if ((m_layout == LayoutVertical) || (m_layout == LayoutGrid))
                MoveDown(MoveRow);
            else
                handled = false;
        }
        else if (action == "RIGHT")
        {
            if (m_layout == LayoutHorizontal)
                MoveDown(MoveItem);
            else if (m_layout == LayoutGrid)
                MoveDown(MoveColumn);
            else
                handled = false;
        }
        else if (action == "LEFT")
        {
            if (m_layout == LayoutHorizontal)
                MoveUp(MoveItem);
            else if (m_layout == LayoutGrid)
                MoveUp(MoveColumn);
            else
                handled = false;
        }
        else if (action == "PAGEUP")
        {
            MoveUp(MovePage);
        }
        else if (action == "PAGEDOWN")
        {
            MoveDown(MovePage);
        }
        else if (action == "SELECT")
        {
            emit itemSelected(GetItemCurrent());
            emit itemClicked(GetItemCurrent());
        }
        else
            handled = false;
    }

    return handled;
}

/** \brief Mouse click/movement handler, recieves mouse gesture events from the
 *         QApplication event loop. Should not be used directly.
 *
 *  \param uitype The mythuitype receiving the event
 *  \param event Mouse event
 */
void MythListButton::gestureEvent(MythUIType *uitype, MythGestureEvent *event)
{
    if (event->gesture() == MythGestureEvent::Click)
    {
        MoveToNamedPosition(dynamic_cast<MythUIButton *>(uitype)->GetText());
        emit itemClicked(GetItemCurrent());
    }
}

/** \brief Mouse click/movement handler, recieves mouse gesture events from the
 *         QApplication event loop. Should not be used directly.
 *
 *  \param p QPoint coordinates
 *
 *  \return MythUIType accepting focus found at this coordinates
 */
MythUIType *MythListButton::GetChildAt(const QPoint &p)
{
    if (GetArea().contains(p))
    {
        /* Unlike the assumption made elsewhere, we don't want to return
           this UItype if it accepts focus, we want the MythUIButtons that
           it contains */

        if (m_ChildrenList.isEmpty())
            return NULL;

        /* check all children */
        QValueVector<MythUIType*>::iterator it;
        for (it = m_ChildrenList.end()-1; it != m_ChildrenList.begin()-1; it--)
        {
            MythUIType *child = (*it)->GetChildAt(p - GetArea().topLeft());
            if (child != NULL)
            {
                return child;
            }
        }
    }
    return NULL;
}

QPoint MythListButton::GetButtonPosition(int column, int row) const
{
    QPoint position;

    if (m_layout == LayoutHorizontal)
    {
        position = QPoint((column - 1) * (ItemWidth() + m_itemHorizSpacing),
                    GetArea().height() / 2 - m_itemHeight / 2);
    }
    else if (m_layout == LayoutVertical)
    {
        position = QPoint(0, (row - 1) * (m_itemHeight + m_itemVertSpacing));
    }
    else if (m_layout == LayoutGrid)
    {
        position = QPoint((column - 1) * (ItemWidth() + m_itemHorizSpacing),
                    (row - 1) * (m_itemHeight + m_itemVertSpacing));
    }

    return position;
}

void MythListButton::LoadPixmap(MythImage **pix, QDomElement &element)
{
    if (*pix)
        (*pix)->DownRef();
    *pix = NULL;

    QString filename = element.attribute("filename");
    if (!filename.isEmpty())
    {
        QImage *p = gContext->LoadScaleImage(filename);
        *pix = MythImage::FromQImage(&p);
        return;
    }

    QColor startcol = QColor(element.attribute("gradientstart", "#505050"));
    QColor endcol = QColor(element.attribute("gradientend", "#000000"));
    int alpha = element.attribute("gradientalpha", "100").toInt();

    *pix = MythImage::Gradient(QSize(10, 10), startcol, endcol, alpha);
}

const QRect MythListButton::PlaceArrows(const QSize &arrowSize)
{
    if (m_layout == LayoutHorizontal)
    {
        int x = GetArea().width() - arrowSize.width() - 1;
        int ytop = GetArea().height() / 2 - m_itemMargin / 2 - arrowSize.height();
        int ybottom = GetArea().height() / 2 + m_itemMargin / 2;

        m_upArrow->SetPosition(QPoint(x, ybottom));
        m_downArrow->SetPosition(QPoint(x, ytop));

        /* rightmost rectangle (full height) */
        QRect arrowsRect(x, 0, arrowSize.width() + 1, m_Area.height());

        return QRect(x, 0, arrowSize.width() + 1, m_Area.height());
    }

    int y = m_Area.height() - arrowSize.height() - 1;
    m_upArrow->SetPosition(QPoint(0, y));
    m_downArrow->SetPosition(QPoint(arrowSize.width() + m_itemMargin, y));

    return QRect(0, y, m_Area.width(), arrowSize.height());
}

QRect MythListButton::CalculateContentsRect(const QRect &arrowsRect) const
{
    QRect contectRect;

    if (m_layout == LayoutHorizontal)
    {
        contectRect = QRect(0, 0,
                    GetArea().width() - arrowsRect.width() - 2 * m_itemMargin,
                    GetArea().height());
    }
    else if ((m_layout == LayoutVertical) || (m_layout == LayoutGrid))
    {
        contectRect = QRect(0, 0, GetArea().width(), GetArea().height() -
                    arrowsRect.height() - 2 * m_itemMargin);
    }

    return contectRect;
}

void MythListButton::CalculateVisibleItems(void)
{
    int y = 0;
    int x = 0;
    m_itemsVisible = 0;
    m_rows = 0;
    m_columns = 0;

    if ((m_layout == LayoutHorizontal) || (m_layout == LayoutGrid))
    {
        while (x <= m_contentsRect.width() - m_itemWidth)
        {
            x += m_itemWidth + m_itemHorizSpacing;
            m_columns++;
        }
    }

    if ((m_layout == LayoutVertical) || (m_layout == LayoutGrid))
    {
        while (y <= m_contentsRect.height() - m_itemHeight) 
        {
            y += m_itemHeight + m_itemVertSpacing;
            m_rows++;
        }
    }

    if (m_rows == 0)
        m_rows = 1;

    if (m_columns == 0)
        m_columns = 1;

    m_itemsVisible = m_columns * m_rows;
}

bool MythListButton::ParseElement(QDomElement &element)
{
    if (element.tagName() == "area")
        m_Area = parseRect(element);
    else if (element.tagName() == "layout")
    {
        QString layout = getFirstText(element).lower();

        if (layout == "grid")
            m_layout = LayoutGrid;
        else if (layout == "horizontal")
            m_layout = LayoutHorizontal;
        else
            m_layout = LayoutVertical;
    }
    else if (element.tagName() == "inactivefont")
    {
        QString fontname = getFirstText(element);
        MythFontProperties *fp = GetFont(fontname);
        if (!fp)
            fp = GetGlobalFontMap()->GetFont(fontname);
        if (fp)
            *m_fontInactive = *fp;
    }
    else if (element.tagName() == "activefont")
    {
        QString fontname = getFirstText(element);
        MythFontProperties *fp = GetFont(fontname);
        if (!fp)
            fp = GetGlobalFontMap()->GetFont(fontname);
        if (fp)
            *m_fontActive = *fp;
    }
    else if (element.tagName() == "scrollarrows")
    {
        m_showScrollArrows = parseBool(element.attribute("show", ""));
        if (m_showScrollArrows)
        {
            if (m_upArrow)
                delete m_upArrow;
            if (m_downArrow)
                delete m_downArrow;
            ParseChildren(element, this);
            m_upArrow = dynamic_cast<MythUIStateType *>(GetChild("upscrollarrow"));
            m_downArrow = dynamic_cast<MythUIStateType *>(GetChild("downscrollarrow"));
        }
    }
    else if (element.tagName() == "showarrow")
        m_showArrow = parseBool(element);
    else if (element.tagName() == "arrow")
        LoadPixmap(&arrowPix, element);
    else if (element.tagName() == "check-empty")
        LoadPixmap(&checkNonePix, element);
    else if (element.tagName() == "check-half")
        LoadPixmap(&checkHalfPix, element);
    else if (element.tagName() == "check-full")
        LoadPixmap(&checkFullPix, element);
    else if (element.tagName() == "regular-background")
        LoadPixmap(&itemRegPix, element);
    else if (element.tagName() == "selected-background")
        LoadPixmap(&itemSelActPix, element);
    else if (element.tagName() == "inactive-background")
        LoadPixmap(&itemSelInactPix, element);
    else if (element.tagName() == "spacing")
    {
        m_itemHorizSpacing = NormX(getFirstText(element).toInt());
        m_itemVertSpacing = NormY(getFirstText(element).toInt());
    }
    else if (element.tagName() == "margin")
        m_itemMargin = NormX(getFirstText(element).toInt());
    else if (element.tagName() == "drawfrombottom")
        m_drawFromBottom = parseBool(element);
    else if (element.tagName() == "multiline")
    {
        if (parseBool(element))
            m_textFlags |= Qt::WordBreak;
        else
            m_textFlags &= ~Qt::WordBreak;
    }
    else if (element.tagName() == "textflags")
    {
        QString align = getFirstText(element).lower();

        m_textFlags = m_textFlags & Qt::WordBreak;

        if (align == "center")
            m_textFlags |= Qt::AlignCenter;
        else if (align == "right")
            m_textFlags |= Qt::AlignRight;
        else if (align == "left")
            m_textFlags |= Qt::AlignLeft;
        else if (align == "allcenter")
            m_textFlags |= Qt::AlignHCenter | Qt::AlignVCenter;
        else if (align == "vcenter")
            m_textFlags |= Qt::AlignVCenter;
        else if (align == "hcenter")
            m_textFlags |= Qt::AlignHCenter;
    }
    else
        return MythUIType::ParseElement(element);

    return true;
}

void MythListButton::CreateCopy(MythUIType *parent)
{
    MythListButton *lb = new MythListButton(parent, name());
    lb->CopyFrom(this);
}

void MythListButton::CopyFrom(MythUIType *base)
{
    MythListButton *lb = dynamic_cast<MythListButton *>(base);
    if (!lb)
    {
        VERBOSE(VB_IMPORTANT, QString("MythListButton CopyFrom ERR: "
                                        "Copy Failed "
                                        "base '%1' is not a mythlistbutton")
                                        .arg(base->name()));
        return;
    }

    m_layout = lb->m_layout;
    m_order = lb->m_order;
    m_rect = lb->m_rect;
    m_contentsRect = lb->m_contentsRect;

    m_itemHeight = lb->m_itemHeight;
    m_itemHorizSpacing = lb->m_itemHorizSpacing;
    m_itemVertSpacing = lb->m_itemVertSpacing;
    m_itemMargin = lb->m_itemMargin;
    m_itemsVisible = lb->m_itemsVisible;
    m_itemWidth = lb->m_itemWidth;

    arrowPix = lb->arrowPix;
    checkNonePix = lb->checkNonePix;
    checkHalfPix = lb->checkHalfPix;
    checkFullPix = lb->checkFullPix;
    itemRegPix = lb->itemRegPix;
    itemSelActPix = lb->itemSelActPix;
    itemSelInactPix = lb->itemSelInactPix;

    if (itemRegPix)
        itemRegPix->UpRef();
    if (itemSelActPix)
        itemSelActPix->UpRef();
    if (itemSelInactPix)
        itemSelInactPix->UpRef();
    if (arrowPix)
        arrowPix->UpRef();
    if (checkNonePix)
        checkNonePix->UpRef();
    if (checkHalfPix)
        checkHalfPix->UpRef();
    if (checkFullPix)
        checkFullPix->UpRef();

    m_active = lb->m_active;
    m_showScrollArrows = lb->m_showScrollArrows;
    m_showArrow = lb->m_showArrow;

    *m_fontActive = *lb->m_fontActive;
    *m_fontInactive = *lb->m_fontInactive;

    m_drawoffset = lb->m_drawoffset;
    m_drawFromBottom = lb->m_drawFromBottom;

    m_textFlags = lb->m_textFlags;

    m_clearing = false;
    m_topItem = m_selItem = NULL;

    m_selPosition = m_topPosition = m_itemCount = 0;

    MythUIType::CopyFrom(base);

    m_upArrow = dynamic_cast<MythUIStateType *>(GetChild("upscrollarrow"));
    m_downArrow = dynamic_cast<MythUIStateType *>(GetChild("downscrollarrow"));

    for (int i = 0; i < (int)m_itemsVisible; i++)
    {
        MythUIType *deltype;
        QString name = QString("buttonlist button %1").arg(i);
        if ((deltype = GetChild(name)))
            delete deltype;
    }

    m_ButtonList.clear();

    m_initialized = false;
}

void MythListButton::Finalize(void)
{
    MythUIType::Finalize();
}

//////////////////////////////////////////////////////////////////////////////

MythListButtonItem::MythListButtonItem(MythListButton* lbtype, 
                                       const QString& text,
                                       MythImage *image, bool checkable,
                                       CheckState state, bool showArrow)
{
    assert(lbtype);
    
    m_parent    = lbtype;
    m_text      = text;
    m_image     = image;
    m_checkable = checkable;
    m_state     = state;
    m_showArrow = showArrow;
    m_data      = 0;
    m_overrideInactive = false;

    if (state >= NotChecked)
        m_checkable = true;

    m_parent->InsertItem(this);
}

MythListButtonItem::~MythListButtonItem()
{
    if (m_parent)
        m_parent->RemoveItem(this);
}

QString MythListButtonItem::text() const
{
    return m_text;
}

void MythListButtonItem::setText(const QString &text)
{
    m_text = text;
}

const MythImage* MythListButtonItem::image() const
{
    return m_image;
}

void MythListButtonItem::setImage(MythImage *image)
{
    m_image = image;
}

bool MythListButtonItem::checkable() const
{
    return m_checkable;
}

MythListButtonItem::CheckState MythListButtonItem::state() const
{
    return m_state;
}

MythListButton* MythListButtonItem::parent() const
{
    return m_parent;
}

void MythListButtonItem::setChecked(MythListButtonItem::CheckState state)
{
    if (!m_checkable)
        return;
    m_state = state;
}

void MythListButtonItem::setCheckable(bool flag)
{
    m_checkable = flag;
}

void MythListButtonItem::setDrawArrow(bool flag)
{
    m_showArrow = flag;
}

void MythListButtonItem::setData(void *data)
{
    m_data = data;    
}

void *MythListButtonItem::getData()
{
    return m_data;
}

void MythListButtonItem::setOverrideInactive(bool flag)
{
    m_overrideInactive = flag;
}

bool MythListButtonItem::getOverrideInactive(void)
{
    return m_overrideInactive;
}

bool MythListButtonItem::moveUpDown(bool flag)
{
    return m_parent->MoveItemUpDown(this, flag);
}

void MythListButtonItem::SetToRealButton(MythUIButton *button, bool active_on)
{
    button->SetText(m_text, m_parent->m_textFlags);
    button->SetButtonImage(m_image);

    if (m_state == NotChecked)
        button->SetCheckState(MythUIStateType::Off);
    else if (m_state == HalfChecked)
        button->SetCheckState(MythUIStateType::Half);
    else
        button->SetCheckState(MythUIStateType::Full);

    button->EnableCheck(m_checkable);

    if (this == m_parent->m_selItem)
    {
        if (m_parent->m_active && !m_overrideInactive && active_on)
            button->SelectState(MythUIButton::Selected);
        else
        {
            if (active_on)
                button->SelectState(MythUIButton::SelectedInactive);
            else
                button->SelectState(MythUIButton::Active);
        }

        button->EnableRightArrow(m_parent->m_showArrow || m_showArrow);
    }
    else
    {
        button->SelectState(MythUIButton::Normal);
        button->EnableRightArrow(false);
    }
}

