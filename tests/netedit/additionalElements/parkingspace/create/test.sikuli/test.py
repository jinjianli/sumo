#!/usr/bin/env python
# Eclipse SUMO, Simulation of Urban MObility; see https://eclipse.org/sumo
# Copyright (C) 2009-2018 German Aerospace Center (DLR) and others.
# This program and the accompanying materials
# are made available under the terms of the Eclipse Public License v2.0
# which accompanies this distribution, and is available at
# http://www.eclipse.org/legal/epl-v20.html
# SPDX-License-Identifier: EPL-2.0

# @file    test.py
# @author  Pablo Alvarez Lopez
# @date    2016-11-25
# @version $Id$

# import common functions for netedit tests
import os
import sys

testRoot = os.path.join(os.environ.get('SUMO_HOME', '.'), 'tests')
neteditTestRoot = os.path.join(
    os.environ.get('TEXTTEST_HOME', testRoot), 'netedit')
sys.path.append(neteditTestRoot)
import neteditTestFunctions as netedit  # noqa

# Open netedit
neteditProcess, match = netedit.setupAndStart(neteditTestRoot)

# go to additional mode
netedit.additionalMode()

# select parkingArea
netedit.changeAdditional("parkingArea")

# create parkingArea in mode "reference left"
netedit.leftClick(match, 250, 250)

# select space
netedit.changeAdditional("space")

# try to create space without selecting Parking Area parent
netedit.leftClick(match, 250, 100)

# select parent
netedit.selectAdditionalChild(9, 0)

# create space
netedit.leftClick(match, 250, 100)

# set invalid z (dummy)
netedit.modifyAdditionalDefaultValue(2, "dummyZ")

# try to create space
netedit.leftClick(match, 260, 100)

# set invalid z (empty)
netedit.modifyAdditionalDefaultValue(2, "")

# try to create space
netedit.leftClick(match, 260, 100)

# set valid z (negative)
netedit.modifyAdditionalDefaultValue(2, "-2")

# create space
netedit.leftClick(match, 260, 100)

# set valid z (negative)
netedit.modifyAdditionalDefaultValue(2, "3.5")

# create space
netedit.leftClick(match, 270, 100)

# set invalid width (dummy)
netedit.modifyAdditionalDefaultValue(3, "dummyWidth")

# try to create area
netedit.leftClick(match, 280, 100)

# set invalid width (empty)
netedit.modifyAdditionalDefaultValue(3, "")

# try to create area
netedit.leftClick(match, 280, 100)

# set invalid width (negative)
netedit.modifyAdditionalDefaultValue(3, "-3")

# try to create area
netedit.leftClick(match, 280, 100)

# set valid width
netedit.modifyAdditionalDefaultValue(3, "2.5")

# create area
netedit.leftClick(match, 280, 100)

# set invalid height (dummy)
netedit.modifyAdditionalDefaultValue(4, "dummyHeight")

# try to create area
netedit.leftClick(match, 290, 100)

# set invalid height (empty)
netedit.modifyAdditionalDefaultValue(4, "")

# try to create area
netedit.leftClick(match, 290, 100)

# set invalid height (negative)
netedit.modifyAdditionalDefaultValue(4, "-4")

# try to create area
netedit.leftClick(match, 290, 100)

# set valid height
netedit.modifyAdditionalDefaultValue(4, "3.1")

# create area
netedit.leftClick(match, 290, 100)

# set invalid angle (dummy)
netedit.modifyAdditionalDefaultValue(5, "dummyHeight")

# try to create area
netedit.leftClick(match, 300, 100)

# set invalid angle (empty)
netedit.modifyAdditionalDefaultValue(5, "")

# try to create area
netedit.leftClick(match, 300, 100)

# set valid angle (negative)
netedit.modifyAdditionalDefaultValue(5, "-4")

# create area
netedit.leftClick(match, 300, 100)

# set valid angle (>360)
netedit.modifyAdditionalDefaultValue(5, "500")

# create area
netedit.leftClick(match, 310, 100)

# set valid angle
netedit.modifyAdditionalDefaultValue(5, "120")

# create area
netedit.leftClick(match, 320, 100)

# block movement
netedit.modifyAdditionalDefaultBoolValue(7)

# create area
netedit.leftClick(match, 330, 100)

# Check undo redo
netedit.undo(match, 10)
netedit.redo(match, 10)

# save additionals
netedit.saveAdditionals()

# save network
netedit.saveNetwork()

# quit netedit
netedit.quit(neteditProcess)
