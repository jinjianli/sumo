/****************************************************************************/
/// @file    MSLink.cpp
/// @author  Daniel Krajzewicz
/// @date    Sept 2002
/// @version $Id$
///
// A connnection between lanes
/****************************************************************************/
// SUMO, Simulation of Urban MObility; see http://sumo.sourceforge.net/
// copyright : (C) 2001-2007
//  by DLR (http://www.dlr.de/) and ZAIK (http://www.zaik.uni-koeln.de/AFS)
/****************************************************************************/
//
//   This program is free software; you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation; either version 2 of the License, or
//   (at your option) any later version.
//
/****************************************************************************/

// ===========================================================================
// included modules
// ===========================================================================
#ifdef _MSC_VER
#include <windows_config.h>
#else
#include <config.h>
#endif

#include "MSLink.h"
#include "MSLane.h"
#include <iostream>
#include <cassert>

#ifdef CHECK_MEMORY_LEAKS
#include <foreign/nvwa/debug_new.h>
#endif // CHECK_MEMORY_LEAKS


// ===========================================================================
// member method definitions
// ===========================================================================
#ifndef HAVE_INTERNAL_LANES
MSLink::MSLink(MSLane* succLane, bool yield,
               LinkDirection dir, LinkState state,
               SUMOReal length) throw()
        :
        myLane(succLane),
        myPrio(!yield), myApproaching(0),
        myRequest(0), myRequestIdx(0), myRespond(0), myRespondIdx(0),
        myState(state), myDirection(dir),  myLength(length)
{}
#else
MSLink::MSLink(MSLane* succLane, MSLane *via, bool yield,
               LinkDirection dir, LinkState state, bool internalEnd,
               SUMOReal length) throw()
        :
        myLane(succLane),
        myPrio(!yield), myApproaching(0),
        myRequest(0), myRequestIdx(0), myRespond(0), myRespondIdx(0),
        myState(state), myDirection(dir), myLength(length),
        myJunctionInlane(via),myIsInternalEnd(internalEnd)
{}
#endif


MSLink::~MSLink() throw()
{}


void
MSLink::setRequestInformation(MSLogicJunction::Request *request, size_t requestIdx,
                              MSLogicJunction::Respond *respond, size_t respondIdx) throw()
{
    assert(myRequest==0);
    assert(myRespond==0);
    myRequest = request;
    myRequestIdx = requestIdx;
    myRespond = respond;
    myRespondIdx = respondIdx;
}


void
MSLink::setApproaching(MSVehicle *approaching) throw()
{
    if (myRequest==0) {
        return;
    }
    myApproaching = approaching;
    myRequest->set(myRequestIdx);
}


void
MSLink::setPriority(bool prio) throw()
{
    myPrio = prio;
}


bool
MSLink::opened() const throw()
{
    if (myRespond==0) {
        // this is the case for internal lanes ending at a junction's end
        // (let the vehicle always leave the junction)
        return true;
    }
    return myRespond->test(myRespondIdx);
}


void
MSLink::deleteRequest() throw()
{
    if (myRequest==0) {
        std::cout << "Buggy" << std::endl;
        return ; // !!! should never happen, was sometimes the case in possibly buggy networks
    }
    myRequest->reset(myRequestIdx);
    if (myRespond==0) {
        std::cout << "Buggy" << std::endl;
        return ; // !!! should never happen, was sometimes the case in possibly buggy networks
    }
    myRespond->reset(myRespondIdx);
}


MSLink::LinkState
MSLink::getState() const throw()
{
    return myState;
}


MSLink::LinkDirection
MSLink::getDirection() const throw()
{
    return myDirection;
}


void
MSLink::setTLState(LinkState state) throw()
{
    myState = state;
}


MSLane *
MSLink::getLane() const throw()
{
    return myLane;
}


bool
MSLink::havePriority() const throw()
{
    return myPrio;
}


#ifdef HAVE_INTERNAL_LANES
MSLane * const
MSLink::getViaLane() const throw()
{
    return myJunctionInlane;
}
#endif


#ifdef HAVE_INTERNAL_LANES
void
MSLink::resetInternalPriority() throw()
{
    myPrio = opened();
    if (myJunctionInlane!=0&&myLane!=0) {
        if (myState==MSLink::LINKSTATE_TL_GREEN) {
            if (myIsInternalEnd&&myJunctionInlane->getID()[0]==':') {
                if (myRequest->test(myRequestIdx)) {
                    myRespond->set(myRespondIdx, true);
                }
            }
        }
    }
}
#endif


size_t
MSLink::getRespondIndex() const throw()
{
    return myRespondIdx;
}



/****************************************************************************/

