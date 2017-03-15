/****************************************************************************/
/// @file    TraCIDefs.h
/// @author  Daniel Krajzewicz
/// @author  Mario Krumnow
/// @author  Michael Behrisch
/// @date    30.05.2012
/// @version $Id$
///
// C++ TraCI client API implementation
/****************************************************************************/
// SUMO, Simulation of Urban MObility; see http://sumo.dlr.de/
// Copyright (C) 2012-2017 DLR (http://www.dlr.de/) and contributors
/****************************************************************************/
//
//   This file is part of SUMO.
//   SUMO is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
//   (at your option) any later version.
//
/****************************************************************************/
#ifndef TraCIDefs_h
#define TraCIDefs_h


// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include <vector>
#include <map>


// ===========================================================================
// global definitions
// ===========================================================================
#define DEFAULT_VIEW "View #0"
#define PRECISION 2


// ===========================================================================
// class and type definitions
// ===========================================================================
typedef long long int SUMOTime; // <utils/common/SUMOTime.h>
#define SUMOTime_MAX std::numeric_limits<SUMOTime>::max()


/// @name Structures definitions
/// @{

/** @struct TraCIPosition
    * @brief A 3D-position
    */
struct TraCIPosition {
    double x, y, z;
};

/** @struct TraCIPosition
    * @brief A color
    */
struct TraCIColor {
    int r, g, b, a;
};

/** @struct TraCIPositionVector
    * @brief A list of positions
    */
typedef std::vector<TraCIPosition> TraCIPositionVector;

/** @struct TraCIBoundary
    * @brief A 3D-bounding box
    */
struct TraCIBoundary {
    double xMin, yMin, zMin;
    double xMax, yMax, zMax;
};

struct TraCIValue {
    union {
        double scalar;
        TraCIPosition position;
        TraCIColor color;
    };
    std::string string;
    std::vector<std::string> stringList;
};

class TraCIPhase {
public:
    TraCIPhase(const SUMOTime _duration, const SUMOTime _duration1, const SUMOTime _duration2, const std::string& _phase)
        : duration(_duration), duration1(_duration1), duration2(_duration2), phase(_phase) {}
    ~TraCIPhase() {}

    SUMOTime duration, duration1, duration2;
    std::string phase;
};


class TraCILogic {
public:
    TraCILogic(const std::string& _subID, int _type, const std::map<std::string, double>& _subParameter, int _currentPhaseIndex, const std::vector<TraCIPhase>& _phases)
        : subID(_subID), type(_type), subParameter(_subParameter), currentPhaseIndex(_currentPhaseIndex), phases(_phases) {}
    ~TraCILogic() {}

    std::string subID;
    int type;
    std::map<std::string, double> subParameter;
    int currentPhaseIndex;
    std::vector<TraCIPhase> phases;
};

class TraCILink {
public:
    TraCILink(const std::string& _from, const std::string& _via, const std::string& _to)
        : from(_from), via(_via), to(_to) {}
    ~TraCILink() {}

    std::string from;
    std::string via;
    std::string to;
};

/// @}


#endif

/****************************************************************************/