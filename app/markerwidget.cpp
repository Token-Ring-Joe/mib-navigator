/*
    Copyright (C) 2004-2011 Martin Jolicoeur (snmpb1@gmail.com) 

    This file is part of the SnmpB project 
    (http://sourceforge.net/projects/snmpb)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <QtGui>
#include <QScrollBar>
#include <qpainter.h>
#include <QAbstractTextDocumentLayout>
#include "markerwidget.h"

namespace {
const int MarkerColumnWidth = 15;
const int NumberPadding = 6;
}

MarkerWidget::MarkerWidget(QWidget* parent ):QWidget( parent )
{
    setAttribute(Qt::WA_StaticContents);

    m_pixmap = QPixmap(":/icon/line-marker");

    setFixedWidth( MarkerColumnWidth );
}

MarkerWidget::~MarkerWidget()
{
}

void MarkerWidget::setTextEditor(QTextEdit* editor)
{
    m_editor = editor;

    connect( m_editor->verticalScrollBar(), SIGNAL( valueChanged( int ) ),
             this, SLOT( doRepaint() ) );
    connect( m_editor, SIGNAL( textChanged() ),
             this, SLOT( doRepaint() ) );
    connect( m_editor->document(), SIGNAL( blockCountChanged( int ) ),
             this, SLOT( updateGutterWidth() ) );

    updateGutterWidth();
    doRepaint();
}

int MarkerWidget::gutterWidth() const
{
    const int blocks = m_editor ? qMax(1, m_editor->document()->blockCount()) : 1;
    const int digits = qMax(3, QString::number(blocks).size());
    const QFontMetrics metrics(m_editor ? m_editor->document()->defaultFont() : font());
    return MarkerColumnWidth + digits * metrics.horizontalAdvance(QLatin1Char('9')) + 2 * NumberPadding;
}

void MarkerWidget::updateGutterWidth()
{
    const int width = gutterWidth();
    if (minimumWidth() != width)
        setFixedWidth( width );
    update();
}

void MarkerWidget::paintEvent( QPaintEvent* /*e*/ )
{
    QPainter painter( this );
    painter.fillRect( rect(), palette().color( QPalette::AlternateBase ) );
    if (!m_editor)
        return;

    // The gutter and the editor's viewport can sit at slightly different heights
    // because of the editor's frame.
    const int viewportTop = m_editor->viewport()->mapToGlobal( QPoint( 0, 0 ) ).y()
                          - mapToGlobal( QPoint( 0, 0 ) ).y();
    const int yOffset = m_editor->verticalScrollBar()->value();
    QAbstractTextDocumentLayout *layout = m_editor->document()->documentLayout();

    painter.setFont( m_editor->document()->defaultFont() );
    QColor numberColor = palette().color( QPalette::Text );
    numberColor.setAlpha( 150 );
    painter.setPen( numberColor );

    // Start at the first visible line instead of walking the whole document.
    QTextBlock block = m_editor->cursorForPosition( QPoint( 0, 0 ) ).block();
    int line = block.blockNumber() + 1;
    const int numberLeft = MarkerColumnWidth;
    const int numberWidth = width() - numberLeft - NumberPadding;

    for (; block.isValid(); block = block.next(), ++line)
    {
        const QRectF bounds = layout->blockBoundingRect( block );
        const int top = int( bounds.top() ) - yOffset + viewportTop;
        if (top > height())
            break;
        if (top + bounds.height() < 0)
            continue;

        // The number sits beside the first row of a wrapped line.
        const QTextLayout *textLayout = block.layout();
        const int firstRow = (textLayout && textLayout->lineCount() > 0)
            ? int( textLayout->lineAt( 0 ).height() ) : int( bounds.height() );

        // A line the editor wants marked (for example a problem location).
        if (block.userState() == 0xFF && !m_pixmap.isNull())
        {
            const int imageOffset = (firstRow - m_pixmap.height()) / 2;
            painter.drawPixmap( 0, top + imageOffset, m_pixmap );
        }

        painter.drawText( QRect( numberLeft, top, numberWidth, firstRow ),
                          Qt::AlignRight | Qt::AlignVCenter, QString::number( line ) );
    }

    painter.end();
}

void MarkerWidget::setMarker( int line )
{
    QTextBlock currentBlock = m_editor->document()->begin();
    QTextBlock foundBlock;
    int l = 1;
    int changed = 0, found = 0;

    // Loop through the blocks
    while(currentBlock.isValid())
    {
        if (l == line)
        {
            if (currentBlock.userState() != 0xFF)
            {
                changed = 1;
                currentBlock.setUserState(0xFF);
            }

            found = 1;
            foundBlock = currentBlock;
        }
        else
        {
            // Clear all other lines
            if (currentBlock.userState() == 0xFF)
            {
                changed = 1;
                currentBlock.setUserState(0x00);
            }
        }

        currentBlock = currentBlock.next();
        l++;
    };

    if (found)
    {
        // Change scrollbar to put the marker visible in the middle of the editor
        int halfViewPortHeight = m_editor->maximumViewportSize().height()/2;
        int yCoord = (int)foundBlock.layout()->position().y();
        int yAdjust = (yCoord < halfViewPortHeight)?yCoord : halfViewPortHeight;
        int halfLineHeight = (int)foundBlock.layout()->boundingRect().height()/2;

        m_editor->verticalScrollBar()->setValue(yCoord - yAdjust);

        // Set the cursor position to the marker line
        QPoint cursorPos(0, yAdjust+halfLineHeight);
        QTextCursor tc = m_editor->cursorForPosition(cursorPos);

        m_editor->setTextCursor(tc);

        // Finally, set the focus to the editor
        m_editor->setFocus(Qt::OtherFocusReason);
    }

    if (changed)
        doRepaint();
}

