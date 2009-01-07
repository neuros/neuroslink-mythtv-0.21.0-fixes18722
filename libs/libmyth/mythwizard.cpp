/****************************************************************************
** 
**
** Implementation of MythWizard class.
**
** Created : 990124
**
** Copyright (C) 1999-2000 Trolltech AS.  All rights reserved.
**
** This file is part of the dialogs module of the Qt GUI Toolkit.
**
** This file may be distributed under the terms of the Q Public License
** as defined by Trolltech AS of Norway and appearing in the file
** LICENSE.QPL included in the packaging of this file.
**
** This file may be distributed and/or modified under the terms of the
** GNU General Public License version 2 as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL included in the
** packaging of this file.
**
** Licensees holding valid Qt Enterprise Edition or Qt Professional Edition
** licenses may use this file in accordance with the Qt Commercial License
** Agreement provided with the Software.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
** See http://www.trolltech.com/pricing.html or email sales@trolltech.com for
**   information about Qt Commercial License Agreements.
** See http://www.trolltech.com/qpl/ for QPL licensing information.
** See http://www.trolltech.com/gpl/ for GPL licensing information.
**
** Contact info@trolltech.com if any conditions of this licensing are
** not clear to you.
**
**********************************************************************/

#include "mythwizard.h"

#include "qlayout.h"
#include "qpushbutton.h"
#include "qcursor.h"
#include "qlabel.h"
#include "qwidgetstack.h"
#include "qapplication.h"
#include "qptrlist.h"
#include "qpainter.h"

#include "mythcontext.h"

class MythWizardPrivate
{
public:
    struct Page {
        Page( QWidget * widget, const QString & title ):
            w( widget ), t( title ),
            backEnabled( TRUE ), nextEnabled( TRUE ), finishEnabled( FALSE ),
            appropriate( TRUE )
        {}
        QWidget * w;
        QString t;
        bool backEnabled;
        bool nextEnabled;
        bool finishEnabled;
        bool appropriate;
    };

    QVBoxLayout * v;
    Page * current;
    QWidgetStack * ws;
    QPtrList<Page> pages;
    QLabel * title;
    MythPushButton * backButton;
    MythPushButton * nextButton;
    MythPushButton * finishButton;
    MythPushButton * cancelButton;

    QGroupBox *helpgroup;
    QLabel *help;

    QFrame * hbar1, * hbar2;

    Page * page( const QWidget * w )
    {
        if ( !w )
            return 0;
        int i = pages.count();
        while( --i >= 0 && pages.at( i ) && pages.at( i )->w != w ) { }
        return i >= 0 ? pages.at( i ) : 0;
    }

};

MythWizard::MythWizard(MythMainWindow *parent, const char *name)
          : MythDialog(parent, name)
{
    d = new MythWizardPrivate();
    d->current = 0; // not quite true, but...
    d->ws = new QWidgetStack( this, "qt_widgetstack" );
    d->pages.setAutoDelete( TRUE );
    d->title = new QLabel( this, "title label" );
    d->title->setBackgroundOrigin(QWidget::WindowOrigin);

    // create in nice tab order
    d->nextButton = new MythPushButton( this, "next" );
    d->finishButton = new MythPushButton( this, "finish" );
    d->backButton = new MythPushButton( this, "back" );
    d->cancelButton = new MythPushButton( this, "cancel" );

    d->ws->installEventFilter( this );

    d->helpgroup = 0;
    d->help = 0;
    d->v = 0;
    d->hbar1 = 0;
    d->hbar2 = 0;

    d->cancelButton->setText( tr( "&Cancel" ) );
    d->backButton->setText( tr( "< &Back" ) );
    d->nextButton->setText( tr( "&Next >" ) );
    d->finishButton->setText( tr( "&Finish" ) );

    d->nextButton->setDefault( TRUE );

    connect( d->backButton, SIGNAL(clicked()),
             this, SLOT(back()) );
    connect( d->nextButton, SIGNAL(clicked()),
             this, SLOT(next()) );
    connect( d->finishButton, SIGNAL(clicked()),
             this, SLOT(accept()) );
    connect( d->cancelButton, SIGNAL(clicked()),
             this, SLOT(reject()) );
}

MythWizard::~MythWizard()
{
    delete d;
}

void MythWizard::Show()
{
    if ( d->current )
        showPage( d->current->w );
    else if ( pageCount() > 0 )
        showPage( d->pages.at( 0 )->w );
    else
        showPage( 0 );

    MythDialog::Show();
}

void MythWizard::setFont( const QFont & font )
{
    QApplication::postEvent( this, new QEvent( QEvent::LayoutHint ) );
    setFont( font );
}

void MythWizard::addPage( QWidget * page, const QString & title )
{
    if ( !page )
        return;
    if ( d->page( page ) ) {
        qWarning( "MythWizard::addPage(): already added %s/%s to %s/%s",
                  page->className(), page->name(),
                  className(), name() );
        return;
    }
    int i = d->pages.count();

    if ( i > 0 )
        d->pages.at( i - 1 )->nextEnabled = TRUE;

    MythWizardPrivate::Page * p = new MythWizardPrivate::Page( page, title );
    p->backEnabled = ( i > 0 );
    d->ws->addWidget( page, i );
    d->pages.append( p );
}

void MythWizard::insertPage( QWidget * page, const QString & title, int index )
{
    if ( !page )
        return;
    if ( d->page( page ) ) {
        qWarning( "MythWizard::insertPage(): already added %s/%s to %s/%s",
                  page->className(), page->name(),
                  className(), name() );
        return;
    }

    if ( index < 0  || index > (int)d->pages.count() )
        index = d->pages.count();

    if ( index > 0 && ( index == (int)d->pages.count() ) )
        d->pages.at( index - 1 )->nextEnabled = TRUE;

    MythWizardPrivate::Page * p = new MythWizardPrivate::Page( page, title );
    p->backEnabled = ( index > 0 );
    p->nextEnabled = ( index < (int)d->pages.count() );

    d->ws->addWidget( page, index );
    d->pages.insert( index, p );
}

void MythWizard::showPage( QWidget * page )
{
    MythWizardPrivate::Page * p = d->page( page );
    if ( p ) {
        int i;
        for( i = 0; i < (int)d->pages.count() && d->pages.at( i ) != p; i++ );
        bool notFirst( FALSE );

        if ( i ) {
            i--;
            while( ( i >= 0 ) && !notFirst ) {
                notFirst |= appropriate( d->pages.at( i )->w );
                i--;
            }
        }
        setBackEnabled( notFirst );
        setNextEnabled( TRUE );
        d->ws->raiseWidget( page );
        d->current = p;
    }

    layOut();
    updateButtons();
    emit selected( p ? p->t : QString::null );

    if (indexOf(page) == pageCount()-1) {
        // last page
        finishButton()->setEnabled(TRUE);
        finishButton()->setFocus();
    } else {
        nextButton()->setFocus();
    }
}

int MythWizard::pageCount() const
{
    return d->pages.count();
}

int MythWizard::indexOf( QWidget* page ) const
{
    MythWizardPrivate::Page * p = d->page( page );
    if ( !p ) return -1;

    return d->pages.find( p );
}

void MythWizard::back()
{
    int i = 0;

    while( i < (int)d->pages.count() && d->pages.at( i ) &&
           d->current && d->pages.at( i )->w != d->current->w )
        i++;

    i--;
    while( i >= 0 &&
           ( !d->pages.at( i ) || !appropriate( d->pages.at( i )->w ) ) )
        i--;

    if ( i >= 0 )
       if ( d->pages.at( i ) )
            showPage( d->pages.at( i )->w );
}

void MythWizard::next()
{
    int i = 0;
    while( i < (int)d->pages.count() && d->pages.at( i ) &&
           d->current && d->pages.at( i )->w != d->current->w )
        i++;
    i++;
    while( i <= (int)d->pages.count()-1 &&
           ( !d->pages.at( i ) || !appropriate( d->pages.at( i )->w ) ) )
        i++;
    while ( i > 0 && (i >= (int)d->pages.count() || !d->pages.at( i ) ) )
        i--;
    if ( d->pages.at( i ) )
        showPage( d->pages.at( i )->w );
}

void MythWizard::setBackEnabled( bool enable )
{
    d->backButton->setEnabled( enable );
}

void MythWizard::setNextEnabled( bool enable )
{
    d->nextButton->setEnabled( enable );
}

void MythWizard::setBackEnabled( QWidget * page, bool enable )
{
    MythWizardPrivate::Page * p = d->page( page );
    if ( !p )
        return;

    p->backEnabled = enable;
    updateButtons();
}

void MythWizard::setNextEnabled( QWidget * page, bool enable )
{
    MythWizardPrivate::Page * p = d->page( page );
    if ( !p )
        return;

    p->nextEnabled = enable;
    updateButtons();
}

void MythWizard::setFinishEnabled( QWidget * page, bool enable )
{
    MythWizardPrivate::Page * p = d->page( page );
    if ( !p )
        return;

    p->finishEnabled = enable;
    updateButtons();
}

bool MythWizard::appropriate( QWidget * page ) const
{
    MythWizardPrivate::Page * p = d->page( page );
    return p ? p->appropriate : TRUE;
}

void MythWizard::setAppropriate( QWidget * page, bool appropriate )
{
    MythWizardPrivate::Page * p = d->page( page );
    if ( p )
        p->appropriate = appropriate;
}

void MythWizard::updateButtons()
{
    if ( !d->current )
        return;

    int i;
    for( i = 0; i < (int)d->pages.count() && d->pages.at( i ) != d->current; i++ );
    bool notFirst( FALSE );
    if ( i ) {
        i--;
        while( ( i >= 0 ) && !notFirst ) {
            notFirst |= appropriate( d->pages.at( i )->w );
            i--;
        }
    }
    setBackEnabled( d->current->backEnabled && notFirst );
    setNextEnabled( d->current->nextEnabled );
    d->finishButton->setEnabled( d->current->finishEnabled );

    if ( ( d->current->finishEnabled && !d->finishButton->isVisible() ) ||
         ( d->current->backEnabled && !d->backButton->isVisible() ) ||
         ( d->current->nextEnabled && !d->nextButton->isVisible() ) )
        layOut();
}

QWidget * MythWizard::currentPage() const
{
    return d->ws->visibleWidget();
}

QString MythWizard::title( QWidget * page ) const
{
    MythWizardPrivate::Page * p = d->page( page );
    return p ? p->t : QString::null;
}

void MythWizard::setTitle( QWidget *page, const QString &title )
{
    MythWizardPrivate::Page * p = d->page( page );
    if ( p )
        p->t = title;
    if ( page == currentPage() )
        d->title->setText( title );
}

QFont MythWizard::titleFont() const
{
    return d->title->font();
}

void MythWizard::setTitleFont( const QFont & font )
{
    d->title->setFont( font );
}

MythPushButton * MythWizard::backButton() const
{
    return d->backButton;
}

MythPushButton * MythWizard::nextButton() const
{
    return d->nextButton;
}

MythPushButton * MythWizard::finishButton() const
{
    return d->finishButton;
}

MythPushButton * MythWizard::cancelButton() const
{
    return d->cancelButton;
}

void MythWizard::layOutButtonRow( QHBoxLayout * layout )
{
    bool hasEarlyFinish = FALSE;

    int i = d->pages.count() - 2;
    while ( !hasEarlyFinish && i >= 0 ) {
        if ( d->pages.at( i ) && d->pages.at( i )->finishEnabled )
            hasEarlyFinish = TRUE;
        i--;
    }

    QBoxLayout * h = new QBoxLayout( QBoxLayout::LeftToRight );
    layout->addLayout( h );

    h->addWidget( d->cancelButton );

    h->addStretch( 42 );

    h->addWidget( d->backButton );

    h->addSpacing( 6 );

    if ( hasEarlyFinish ) {
        d->nextButton->show();
        d->finishButton->show();
        h->addWidget( d->nextButton );
        h->addSpacing( 12 );
        h->addWidget( d->finishButton );
    } else if ( d->pages.count() == 0 ||
                d->current->finishEnabled ||
                d->current == d->pages.at( d->pages.count()-1 ) ) {
        d->nextButton->hide();
        d->finishButton->show();
        h->addWidget( d->finishButton );
    } else {
        d->nextButton->show();
        d->finishButton->hide();
        h->addWidget( d->nextButton );
    }

    // if last page is disabled - show finished btn. at lastpage-1
    i = d->pages.count()-1;
    if ( i >= 0 && !appropriate( d->pages.at( i )->w ) &&
         d->current == d->pages.at( d->pages.count()-2 ) ) {
        d->nextButton->hide();
        d->finishButton->show();
        h->addWidget( d->finishButton );
    }
}

void MythWizard::layOutTitleRow( QHBoxLayout * layout, const QString & title )
{
    d->title->setText( title );
    layout->addWidget( d->title, 10 );
}

void MythWizard::layOut()
{
    delete d->v;
    d->v = new QVBoxLayout( this, 6, 0, "top-level layout" );

    QHBoxLayout * l;
    l = new QHBoxLayout( 6 );
    d->v->addLayout( l, 0 );
    layOutTitleRow( l, d->current ? d->current->t : QString::null );

    if ( ! d->hbar1 ) {
        d->hbar1 = new QFrame( this, "<hr>", 0 );
        d->hbar1->setBackgroundOrigin(QWidget::WindowOrigin);
        d->hbar1->setFrameStyle( QFrame::Sunken + QFrame::HLine );
        d->hbar1->setFixedHeight( 12 );
    }

    d->v->addWidget( d->hbar1 );

    d->v->addWidget( d->ws, 10 );

    if (!d->helpgroup)
    {
        d->helpgroup = new QGroupBox(this, "help-group-box");
        d->helpgroup->setBackgroundOrigin(QWidget::WindowOrigin);

        d->help = new QLabel(d->helpgroup, "help text");
        d->help->setBackgroundOrigin(QWidget::WindowOrigin);

        d->help->setAlignment(Qt::WordBreak | Qt::AlignLeft | Qt::AlignTop);

        d->help->setMinimumWidth(screenwidth - (int)(40 * wmult));
        d->help->setMaximumHeight((int)(80 * hmult));
        d->help->setMinimumHeight((int)(80 * hmult));
    
        QVBoxLayout *helplayout = new QVBoxLayout(d->helpgroup, 10);
        helplayout->add(d->help);
    }
    else
    {
        d->help->setText("");
    }

    d->v->addWidget(d->helpgroup);

    if ( ! d->hbar2 ) {
        d->hbar2 = new QFrame( this, "<hr>", 0 );
        d->hbar2->setBackgroundOrigin(QWidget::WindowOrigin);
        d->hbar2->setFrameStyle( QFrame::Sunken + QFrame::HLine );
        d->hbar2->setFixedHeight( 12 );
    }
    d->v->addWidget( d->hbar2 );

    l = new QHBoxLayout( 6 );
    d->v->addLayout( l );
    layOutButtonRow( l );
    d->v->activate();
}

bool MythWizard::eventFilter( QObject * o, QEvent * e )
{
    if ( o == d->ws && e && e->type() == QEvent::ChildRemoved ) {
        QChildEvent * c = (QChildEvent*)e;
        if ( c->child() && c->child()->isWidgetType() )
            removePage( (QWidget *)c->child() );
    }
    return QWidget::eventFilter( o, e );
}

void MythWizard::keyPressEvent(QKeyEvent* e) 
{
    bool handled = false;
    QStringList actions;
    if (gContext->GetMainWindow()->TranslateKeyPress("qt", e, actions))
    {
        for (unsigned int i = 0; i < actions.size() && !handled; i++)
        {
            QString action = actions[i];
            handled = true;

            if (action == "SELECT")
            {
                if (indexOf(currentPage()) == pageCount()-1)
                    accept();
                else
                    next();
            }
            else if (action == "ESCAPE")
            {
                if (indexOf(currentPage()) == 0)
                    reject();
                else
                {
                    back();
                    QApplication::postEvent(gContext->GetMainWindow(), 
                                            new ExitToMainMenuEvent());
                }
            }
            else
                handled = false;
        }
    }

    if (!handled)
        MythDialog::keyPressEvent(e);
}

void MythWizard::removePage( QWidget * page )
{
    if ( !page )
        return;

    int i = d->pages.count();
    QWidget* cp = currentPage();
    while( --i >= 0 && d->pages.at( i ) && d->pages.at( i )->w != page ) { }
    if ( i < 0 )
        return;
    MythWizardPrivate::Page * p = d->pages.at( i );
    d->pages.removeRef( p );
    d->ws->removeWidget( page );

    if ( cp == page ) {
        i--;
        if ( i < 0 )
            i = 0;
        if ( pageCount() > 0 )
            showPage( MythWizard::page( i ) );
    }
}

QWidget* MythWizard::page( int index ) const
{
    if ( index >= pageCount() || index < 0 )
      return 0;

    return d->pages.at( index )->w;
}

void MythWizard::setHelpText(QString helptext)
{
    if (!d->help)
        return;

    d->help->setText(helptext);
    d->help->setMinimumWidth(screenwidth - (int)(40 * wmult));
    d->help->setMaximumHeight((int)(80 * hmult));
}

MythJumpWizard::MythJumpWizard(MythMainWindow *parent, const char *name) :
    MythWizard(parent, name)
{
}

MythJumpWizard::~MythJumpWizard()
{
}
