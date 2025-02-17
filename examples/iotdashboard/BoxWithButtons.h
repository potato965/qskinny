/******************************************************************************
 * Copyright (C) 2021 Edelhirsch Software GmbH
 * This file may be used under the terms of the 3-clause BSD License
 *****************************************************************************/

#pragma once

#include "Box.h"

class BoxWithButtons : public Box
{
  public:
    QSK_SUBCONTROLS( Panel, ValuePanel, ValueText )

    BoxWithButtons( const QString& title, const QString& value,
        bool isBright, QQuickItem* parent = nullptr );
};
