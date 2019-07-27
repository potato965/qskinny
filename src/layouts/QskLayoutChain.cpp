/******************************************************************************
 * QSkinny - Copyright (C) 2016 Uwe Rathmann
 * This file may be used under the terms of the QSkinny License, Version 1.0
 *****************************************************************************/

#include "QskLayoutChain.h"
#include "QskLayoutConstraint.h"

#include <qvarlengtharray.h>
#include <qvector.h>

#ifdef QSK_LAYOUT_COMPAT
#include <cmath>
#endif

#include <qdebug.h>

QskLayoutChain::QskLayoutChain()
{
}

QskLayoutChain::~QskLayoutChain()
{
}

void QskLayoutChain::invalidate()
{
    m_cells.clear();
    m_constraint = -2;
}

void QskLayoutChain::reset( int count, qreal constraint )
{
    m_cells.fill( CellData(), count );
    m_constraint = constraint;
    m_sumStretches = 0;
    m_validCells = 0;
}

void QskLayoutChain::narrowCell( int index, const CellData& newCell )
{
    if ( !newCell.isValid )
        return;

    auto& cell = m_cells[ index ];

    if ( !cell.isValid )
    {
        cell = newCell;
        cell.stretch = qMax( cell.stretch, 0 );
        m_validCells++;
    }
    else
    {
        cell.canGrow &= newCell.canGrow;
        if ( newCell.stretch >= 0 )
            cell.stretch = qMax( cell.stretch, newCell.stretch );

        if ( !newCell.hint.isDefault() )
        {
            cell.hint.setSizes(
                qMax( cell.hint.minimum(), newCell.hint.minimum() ),
                qMax( cell.hint.preferred(), newCell.hint.preferred() ),
                qMin( cell.hint.maximum(), newCell.hint.maximum() )
            );

            cell.hint.normalize();
        }
    }
}

void QskLayoutChain::expandCell( int index, const CellData& newCell )
{
    if ( !newCell.isValid )
        return;

    auto& cell = m_cells[ index ];

    if ( !cell.isValid )
    {
        cell = newCell;
        cell.stretch = qMax( cell.stretch, 0 );
        m_validCells++;
    }
    else
    {
        cell.canGrow |= newCell.canGrow;
        cell.stretch = qMax( cell.stretch, newCell.stretch );

        cell.hint.setSizes(
            qMax( cell.hint.minimum(), newCell.hint.minimum() ),
            qMax( cell.hint.preferred(), newCell.hint.preferred() ),
            qMax( cell.hint.maximum(), newCell.hint.maximum() )
        );
    }
}

void QskLayoutChain::expandCells(
    int index, int count, const CellData& multiCell )
{
    QskLayoutChain chain;
    chain.reset( count, -1 );

    for ( int i = 0; i < count; i++ )
    {
        chain.expandCell( i, m_cells[ index + i ] );

        auto& cell = chain.m_cells[ i ];
#if 1
        // what to do now ??
        if ( !cell.isValid )
        {
            cell.isValid = true;
            cell.canGrow = multiCell.canGrow;
            cell.stretch = qMax( cell.stretch, 0 );
        }
#endif
    }
    chain.m_validCells = count;

    QVarLengthArray< QskLayoutHint > hints( count );

    const auto& hint = multiCell.hint;
    const auto chainHint = chain.boundingHint();

    if ( hint.minimum() > chainHint.minimum() )
    {
        const auto segments = chain.segments( hint.minimum() );
        for ( int i = 0; i < count; i++ )
            hints[i].setMinimum( segments[i].length );
    }

    if ( hint.preferred() > chainHint.preferred() )
    {
        const auto segments = chain.segments( hint.preferred() );
        for ( int i = 0; i < count; i++ )
            hints[i].setPreferred( segments[i].length );
    }

    if ( hint.maximum() < chainHint.maximum() )
    {
        const auto segments = chain.segments( hint.maximum() );
        for ( int i = 0; i < count; i++ )
            hints[i].setMaximum( segments[i].length );
    }

    for ( int i = 0; i < count; i++ )
    {
        auto cell = multiCell;
        cell.hint = hints[i];

        expandCell( index + i, cell );
    }
}

void QskLayoutChain::finish()
{
    qreal minimum = 0.0;
    qreal preferred = 0.0;
    qreal maximum = 0.0;

    m_sumStretches = 0;
    m_validCells = 0;

    if ( !m_cells.empty() )
    {
        const auto maxMaximum = QskLayoutConstraint::unlimited;

        for ( auto& cell : m_cells )
        {
            if ( !cell.isValid )
                continue;

            minimum += cell.hint.minimum();
            preferred += cell.hint.preferred();

            if ( maximum < maxMaximum )
            {
                if ( cell.stretch == 0 && !cell.canGrow )
                {
                    maximum += cell.hint.preferred();
                }
                else
                {
                    if ( cell.hint.maximum() == maxMaximum )
                        maximum = maxMaximum;
                    else
                        maximum += cell.hint.maximum();
                }
            }

            m_sumStretches += cell.stretch;
            m_validCells++;
        }

        const qreal spacing = ( m_validCells - 1 ) * m_spacing;

        minimum += spacing;
        preferred += spacing;

        if ( maximum < maxMaximum )
            maximum += spacing;
    }

    m_boundingHint.setMinimum( minimum );
    m_boundingHint.setPreferred( preferred );
    m_boundingHint.setMaximum( maximum );
}

bool QskLayoutChain::setSpacing( qreal spacing )
{
    if ( m_spacing != spacing )
    {
        m_spacing = spacing;
        return true;
    }

    return false;
}

QskLayoutChain::Segments QskLayoutChain::segments( qreal size ) const
{
    if ( m_validCells == 0 )
        return Segments();

    Segments segments;

    if ( size <= m_boundingHint.minimum() )
    {
        segments = distributed( Qt::MinimumSize, 0.0, 0.0 );
    }
    else if ( size < m_boundingHint.preferred() )
    {
        segments = minimumExpanded( size );
    }
    else if ( size <= m_boundingHint.maximum() )
    {
        segments = preferredStretched( size );
    }
    else
    {
        const qreal padding = size - m_boundingHint.maximum();

        qreal offset = 0.0;
        qreal extra = 0.0;;

        switch( m_extraSpacingAt )
        {
            case Leading:
                offset = padding;
                break;

            case Trailing:
                break;

            case Leading | Trailing:
                offset = 0.5 * padding;
                break;

            default:
                extra = padding / m_validCells;
        }

        segments = distributed( Qt::MaximumSize, offset, extra );
    }

    return segments;
}

QskLayoutChain::Segments QskLayoutChain::distributed(
    int which, qreal offset, const qreal extra ) const
{
    qreal fillSpacing = 0.0;

    Segments segments( m_cells.size() );

    for ( int i = 0; i < segments.count(); i++ )
    {
        const auto& cell = m_cells[i];
        auto& segment = segments[i];

        if ( !cell.isValid )
        {
            segment.start = offset;
            segment.length = 0.0;
        }
        else
        {
            offset += fillSpacing;
            fillSpacing = m_spacing;

            segment.start = offset;
            segment.length = cell.hint.size( which ) + extra;

            offset += segment.length;
        }
    }

    return segments;
}

QskLayoutChain::Segments QskLayoutChain::minimumExpanded( qreal size ) const
{
    Segments segments( m_cells.size() );

    qreal fillSpacing = 0.0;
    qreal offset = 0.0;

    /*
        We have different options how to distribute the available space

        - according to the preferred sizes

        - items with a larger preferred size are stretchier: this is
          what QSK_LAYOUT_COMPAT does ( compatible with QGridLayoutEngine )

        - somehow using the stretch factors
     */

#ifdef QSK_LAYOUT_COMPAT

    /*
         Code does not make much sense, but this is what QGridLayoutEngine does.
         The implementation is intended to help during the migration, but is supposed
         to be removed then TODO ...
     */
    qreal sumFactors = 0.0;
    QVarLengthArray< qreal > factors( m_cells.size() );

    const qreal desired = m_boundingHint.preferred() - m_boundingHint.minimum();
    const qreal available = size - m_boundingHint.minimum();

    for ( int i = 0; i < m_cells.size(); i++ )
    {
        const auto& cell = m_cells[i];
        if ( !cell.isValid )
        {
            factors[i] = 0.0;
        }
        else
        {
            const qreal l = cell.hint.preferred() - cell.hint.minimum();

            factors[i] = l * std::pow( available / desired, l / desired );
            sumFactors += factors[i];
        }
    }

    for ( int i = 0; i < m_cells.size(); i++ )
    {
        const auto& cell = m_cells[i];
        auto& segment = segments[i];

        if ( !cell.isValid )
        {
            segment.start = offset;
            segment.length = 0.0;
        }
        else
        {
            offset += fillSpacing;
            fillSpacing = m_spacing;

            segment.start = offset;
            segment.length = cell.hint.minimum()
                + available * ( factors[i] / sumFactors );

            offset += segment.length;
        }
    }
#else
    const qreal factor = ( size - m_boundingHint.minimum() ) /
        ( m_boundingHint.preferred() - m_boundingHint.minimum() );

    for ( int i = 0; i < m_cells.count(); i++ )
    {
        const auto& cell = m_cells[i];
        auto& segment = segments[i];

        if ( !cell.isValid )
        {
            segment.start = offset;
            segment.length = 0.0;
        }
        else
        {
            offset += fillSpacing;
            fillSpacing = m_spacing;

            segment.start = offset;
            segment.length = cell.hint.minimum()
                + factor * ( cell.hint.preferred() - cell.hint.minimum() );

            offset += segment.length;
        }
    }
#endif

    return segments;
}

QskLayoutChain::Segments QskLayoutChain::preferredStretched( qreal size ) const
{
    const int count = m_cells.size();

    qreal sumFactors = 0.0;

    QVarLengthArray< qreal > factors( count );
    Segments segments( count );

    for ( int i = 0; i < count; i++ )
    {
        const auto& cell = m_cells[i];

        if ( !cell.isValid )
        {
            segments[i].length = 0.0;
            factors[i] = -1.0;
            continue;
        }

        if ( cell.hint.preferred() >= cell.hint.maximum() )
        {
            factors[i] = 0.0;
        }
        else
        {
            if ( m_sumStretches == 0 )
                factors[i] = cell.canGrow ? 1.0 : 0.0;
            else
                factors[i] = cell.stretch;

        }

        sumFactors += factors[i];
    }

    auto sumSizes = size - ( m_validCells - 1 ) * m_spacing;

    Q_FOREVER
    {
        bool done = true;

        for ( int i = 0; i < count; i++ )
        {
            if ( factors[i] < 0.0 )
                continue;

            const auto size = sumSizes * factors[i] / sumFactors;

            const auto& hint = m_cells[i].hint;
            const auto boundedSize =
                qBound( hint.preferred(), size, hint.maximum() );

            if ( boundedSize != size )
            {
                segments[i].length = boundedSize;
                sumSizes -= boundedSize;
                sumFactors -= factors[i];
                factors[i] = -1.0;

                done = false;
            }
        }

        if ( done )
            break;
    }

    qreal offset = 0;
    qreal fillSpacing = 0.0;

    for ( int i = 0; i < count; i++ )
    {
        const auto& cell = m_cells[i];
        auto& segment = segments[i];

        const auto& factor = factors[i];

        if ( cell.isValid )
        {
            offset += fillSpacing;
            fillSpacing = m_spacing;
        }

        segment.start = offset;

        if ( factor >= 0.0 )
        {
            if ( factor > 0.0 )
                segment.length = sumSizes * factor / sumFactors;
            else
                segment.length = cell.hint.preferred();
        }

        offset += segment.length;
    }

    return segments;
}

#ifndef QT_NO_DEBUG_STREAM

#include <qdebug.h>

QDebug operator<<( QDebug debug, const QskLayoutChain::Segment& segment )
{
    QDebugStateSaver saver( debug );
    debug.nospace();

    debug << "( " << segment.start << ", " << segment.end() << " )";

    return debug;
}

QDebug operator<<( QDebug debug, const QskLayoutChain::CellData& cell )
{
    QDebugStateSaver saver( debug );
    debug.nospace();

    if ( !cell.isValid )
    {
        debug << "( " << "Invalid " << " )";
    }
    else
    {
        debug << "( " << cell.hint << ", "
            << cell.stretch << ", " << cell.canGrow << " )";
    }

    return debug;
}

#endif

