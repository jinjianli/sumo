/****************************************************************************/
/// @file    MSVehicle.cpp
/// @author  Christian Roessel
/// @author  Jakob Erdmann
/// @author  Bjoern Hendriks
/// @author  Daniel Krajzewicz
/// @author  Thimor Bohn
/// @author  Friedemann Wesner
/// @author  Laura Bieker
/// @author  Clemens Honomichl
/// @author  Michael Behrisch
/// @author  Axel Wegener
/// @author  Christoph Sommer
/// @date    Mon, 05 Mar 2001
/// @version $Id$
///
// Representation of a vehicle in the micro simulation
/****************************************************************************/
// SUMO, Simulation of Urban MObility; see http://sumo.sourceforge.net/
// Copyright (C) 2001-2012 DLR (http://www.dlr.de/) and contributors
/****************************************************************************/
//
//   This file is part of SUMO.
//   SUMO is free software: you can redistribute it and/or modify
//   it under the terms of the GNU General Public License as published by
//   the Free Software Foundation, either version 3 of the License, or
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

#include <iostream>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <utils/options/OptionsCont.h>
#include <utils/common/ToString.h>
#include <utils/common/FileHelpers.h>
#include <utils/common/DijkstraRouterTT.h>
#include <utils/common/RandHelper.h>
#include <utils/common/HelpersHBEFA.h>
#include <utils/common/HelpersHarmonoise.h>
#include <utils/common/StringUtils.h>
#include <utils/common/StdDefs.h>
#include <utils/geom/Line.h>
#include <utils/iodevices/OutputDevice.h>
#include <utils/iodevices/BinaryInputDevice.h>
#include <microsim/MSVehicleControl.h>
#include <microsim/MSGlobals.h>
#include "trigger/MSBusStop.h"
#include "devices/MSDevice_Person.h"
#include "MSEdgeWeightsStorage.h"
#include "MSLCM_DK2004.h"
#include "MSMoveReminder.h"
#include "MSPerson.h"
#include "MSPersonControl.h"
#include "MSLane.h"
#include "MSVehicle.h"
#include "MSEdge.h"
#include "MSVehicleType.h"
#include "MSNet.h"
#include "MSRoute.h"
#include "MSLinkCont.h"

#ifdef _MESSAGES
#include "MSMessageEmitter.h"
#endif

#ifdef HAVE_INTERNAL
#include <mesosim/MESegment.h>
#include <mesosim/MELoop.h>
#include "MSGlobals.h"
#endif

#ifdef CHECK_MEMORY_LEAKS
#include <foreign/nvwa/debug_new.h>
#endif // CHECK_MEMORY_LEAKS

//#define DEBUG_VEHICLE_GUI_SELECTION 1
#ifdef DEBUG_VEHICLE_GUI_SELECTION
#undef ID_LIST
#include <utils/gui/div/GUIGlobalSelection.h>
#include <guisim/GUIVehicle.h>
#include <guisim/GUILane.h>
#endif

#define BUS_STOP_OFFSET 0.5


// ===========================================================================
// static value definitions
// ===========================================================================
std::vector<MSLane*> MSVehicle::myEmptyLaneVector;


// ===========================================================================
// method definitions
// ===========================================================================
/* -------------------------------------------------------------------------
 * methods of MSVehicle::State
 * ----------------------------------------------------------------------- */
MSVehicle::State::State(const State& state) {
    myPos = state.myPos;
    mySpeed = state.mySpeed;
}


MSVehicle::State&
MSVehicle::State::operator=(const State& state) {
    myPos   = state.myPos;
    mySpeed = state.mySpeed;
    return *this;
}


bool
MSVehicle::State::operator!=(const State& state) {
    return (myPos   != state.myPos ||
            mySpeed != state.mySpeed);
}


SUMOReal
MSVehicle::State::pos() const {
    return myPos;
}


MSVehicle::State::State(SUMOReal pos, SUMOReal speed) :
    myPos(pos), mySpeed(speed) {}


/* -------------------------------------------------------------------------
 * methods of MSVehicle::Influencer
 * ----------------------------------------------------------------------- */
#ifndef NO_TRACI
MSVehicle::Influencer::Influencer()
    : mySpeedAdaptationStarted(true), myConsiderSafeVelocity(true),
      myConsiderMaxAcceleration(true), myConsiderMaxDeceleration(true) {}


MSVehicle::Influencer::~Influencer() {}


void
MSVehicle::Influencer::setSpeedTimeLine(const std::vector<std::pair<SUMOTime, SUMOReal> >& speedTimeLine) {
    mySpeedAdaptationStarted = true;
    mySpeedTimeLine = speedTimeLine;
}


void
MSVehicle::Influencer::setLaneTimeLine(const std::vector<std::pair<SUMOTime, unsigned int> >& laneTimeLine) {
    myLaneTimeLine = laneTimeLine;
}


SUMOReal
MSVehicle::Influencer::influenceSpeed(SUMOTime currentTime, SUMOReal speed, SUMOReal vSafe, SUMOReal vMin, SUMOReal vMax) {
    // keep original speed
    myOriginalSpeed = speed;
    // remove leading commands which are no longer valid
    while (mySpeedTimeLine.size() == 1 || (mySpeedTimeLine.size() > 1 && currentTime > mySpeedTimeLine[1].first)) {
        mySpeedTimeLine.erase(mySpeedTimeLine.begin());
    }
    // do nothing if the time line does not apply for the current time
    if (mySpeedTimeLine.size() < 2 || currentTime < mySpeedTimeLine[0].first) {
        return speed;
    }
    // compute and set new speed
    if (!mySpeedAdaptationStarted) {
        mySpeedTimeLine[0].second = speed;
        mySpeedAdaptationStarted = true;
    }
    currentTime += DELTA_T;
    const SUMOReal td = STEPS2TIME(currentTime - mySpeedTimeLine[0].first) / STEPS2TIME(mySpeedTimeLine[1].first + DELTA_T - mySpeedTimeLine[0].first);
    speed = mySpeedTimeLine[0].second - (mySpeedTimeLine[0].second - mySpeedTimeLine[1].second) * td;
    if (myConsiderSafeVelocity) {
        speed = MIN2(speed, vSafe);
    }
    if (myConsiderMaxAcceleration) {
        speed = MIN2(speed, vMax);
    }
    if (myConsiderMaxDeceleration) {
        speed = MAX2(speed, vMin);
    }
    return speed;
}


MSVehicle::ChangeRequest
MSVehicle::Influencer::checkForLaneChanges(SUMOTime currentTime, const MSEdge& currentEdge, unsigned int currentLaneIndex) {
    // remove leading commands which are no longer valid
    while (myLaneTimeLine.size() == 1 || (myLaneTimeLine.size() > 1 && currentTime > myLaneTimeLine[1].first)) {
        myLaneTimeLine.erase(myLaneTimeLine.begin());
    }
    // do nothing if the time line does not apply for the current time
    if (myLaneTimeLine.size() < 2 || currentTime < myLaneTimeLine[0].first) {
        return REQUEST_NONE;
    }
    unsigned int destinationLaneIndex = myLaneTimeLine[1].second;
    if ((unsigned int)currentEdge.getLanes().size() <= destinationLaneIndex) {
        return REQUEST_NONE;
    }
    if (currentLaneIndex > destinationLaneIndex) {
        return REQUEST_RIGHT;
    } else if (currentLaneIndex < destinationLaneIndex) {
        return REQUEST_LEFT;
    } else {
        return REQUEST_HOLD;
    }
}


void
MSVehicle::Influencer::setConsiderSafeVelocity(bool value) {
    myConsiderSafeVelocity = value;
}


void
MSVehicle::Influencer::setConsiderMaxAcceleration(bool value) {
    myConsiderMaxAcceleration = value;
}


void
MSVehicle::Influencer::setConsiderMaxDeceleration(bool value) {
    myConsiderMaxDeceleration = value;
}
#endif


/* -------------------------------------------------------------------------
 * MSVehicle-methods
 * ----------------------------------------------------------------------- */
MSVehicle::~MSVehicle() {
    delete myLaneChangeModel;
    // other
    delete myEdgeWeights;
    for (std::vector<MSLane*>::iterator i = myFurtherLanes.begin(); i != myFurtherLanes.end(); ++i) {
        (*i)->resetPartialOccupation(this);
    }
    myFurtherLanes.clear();
    for (DriveItemVector::iterator i = myLFLinkLanes.begin(); i != myLFLinkLanes.end(); ++i) {
        if ((*i).myLink != 0) {
            (*i).myLink->removeApproaching(this);
        }
    }
    //
    if (myType->amVehicleSpecific()) {
        delete myType;
    }
#ifndef NO_TRACI
    delete myInfluencer;
#endif
}


MSVehicle::MSVehicle(SUMOVehicleParameter* pars,
                     const MSRoute* route,
                     const MSVehicleType* type,
                     SUMOReal speedFactor,
                     int /*vehicleIndex*/) :
    MSBaseVehicle(pars, route, type, speedFactor),
    myLastLaneChangeOffset(0),
    myWaitingTime(0),
    myState(0, 0), //
    myLane(0),
    myShadowLane(0),
    myLastBestLanesEdge(0),
    myPersonDevice(0),
    myAcceleration(0),
    mySignals(0),
    myAmOnNet(false),
    myAmRegisteredAsWaitingForPerson(false),
    myHaveToWaitOnNextLink(false),
    myLaneChangeCompletion(1.0),
    myLaneChangeDirection(0),
    myLaneChangeMidpointPassed(false),
    myEdgeWeights(0)
#ifndef NO_TRACI
    , myInfluencer(0)
#endif
{
    for (std::vector<SUMOVehicleParameter::Stop>::iterator i = pars->stops.begin(); i != pars->stops.end(); ++i) {
        if (!addStop(*i)) {
            throw ProcessError("Stop for vehicle '" + pars->id +
                               "' on lane '" + i->lane + "' is not downstream the current route.");
        }
    }
    for (std::vector<SUMOVehicleParameter::Stop>::const_iterator i = route->getStops().begin(); i != route->getStops().end(); ++i) {
        if (!addStop(*i)) {
            throw ProcessError("Stop for vehicle '" + pars->id +
                               "' on lane '" + i->lane + "' is not downstream the current route.");
        }
    }
    const MSLane* const depLane = (*myCurrEdge)->getDepartLane(*this);
    if (depLane == 0) {
        throw ProcessError("Invalid departlane definition for vehicle '" + pars->id + "'.");
    }
    if (pars->departSpeedProcedure == DEPART_SPEED_GIVEN && pars->departSpeed > depLane->getSpeedLimit()) {
        throw ProcessError("Departure speed for vehicle '" + pars->id +
                           "' is too high for the departure lane '" + depLane->getID() + "'.");
    }
    if (pars->departSpeedProcedure == DEPART_SPEED_GIVEN && pars->departSpeed > type->getMaxSpeed()) {
        throw ProcessError("Departure speed for vehicle '" + pars->id +
                           "' is too high for the vehicle type '" + type->getID() + "'.");
    }
#ifdef _MESSAGES
    myLCMsgEmitter = MSNet::getInstance()->getMsgEmitter("lanechange");
    myBMsgEmitter = MSNet::getInstance()->getMsgEmitter("break");
    myHBMsgEmitter = MSNet::getInstance()->getMsgEmitter("heartbeat");
#endif
    myLaneChangeModel = new MSLCM_DK2004(*this);
    myCFVariables = type->getCarFollowModel().createVehicleVariables();
}


void
MSVehicle::onRemovalFromNet(const MSMoveReminder::Notification reason) {
    workOnMoveReminders(myState.myPos - SPEED2DIST(myState.mySpeed), myState.myPos, myState.mySpeed);
    for (DriveItemVector::iterator i = myLFLinkLanes.begin(); i != myLFLinkLanes.end(); ++i) {
        if ((*i).myLink != 0) {
            (*i).myLink->removeApproaching(this);
        }
    }
    leaveLane(reason);
}


// ------------ interaction with the route
bool
MSVehicle::hasArrived() const {
    return myCurrEdge == myRoute->end() - 1 && myState.myPos > myArrivalPos - POSITION_EPS;
}


bool
MSVehicle::replaceRoute(const MSRoute* newRoute, bool onInit) {
    const MSEdgeVector& edges = newRoute->getEdges();
    // assert the vehicle may continue (must not be "teleported" or whatever to another position)
    if (!onInit && !newRoute->contains(*myCurrEdge)) {
        return false;
    }

    // rebuild in-vehicle route information
    if (onInit) {
        myCurrEdge = newRoute->begin();
    } else {
        myCurrEdge = find(edges.begin(), edges.end(), *myCurrEdge);
    }
    // check whether the old route may be deleted (is not used by anyone else)
    newRoute->addReference();
    myRoute->release();
    // assign new route
    myRoute = newRoute;
    myLastBestLanesEdge = 0;
    if (!onInit) {
        getBestLanes(true);
    }
    // update arrival definition
    calculateArrivalPos();
    // save information that the vehicle was rerouted
    myNumberReroutes++;
    MSNet::getInstance()->informVehicleStateListener(this, MSNet::VEHICLE_STATE_NEWROUTE);
    // recheck stops
    for (std::list<Stop>::iterator iter = myStops.begin(); iter != myStops.end();) {
        if (find(edges.begin(), edges.end(), &iter->lane->getEdge()) == edges.end()) {
            iter = myStops.erase(iter);
        } else {
            iter->edge = find(edges.begin(), edges.end(), &iter->lane->getEdge());
            ++iter;
        }
    }
    return true;
}


bool
MSVehicle::willPass(const MSEdge* const edge) const {
    return find(myCurrEdge, myRoute->end(), edge) != myRoute->end();
}


unsigned int
MSVehicle::getRoutePosition() const {
    return (unsigned int) std::distance(myRoute->begin(), myCurrEdge);
}


void
MSVehicle::resetRoutePosition(unsigned int index) {
    myCurrEdge = myRoute->begin() + index;
    // !!! hack
    myArrivalPos = (*(myRoute->end() - 1))->getLanes()[0]->getLength();
}



const MSEdgeWeightsStorage&
MSVehicle::getWeightsStorage() const {
    return _getWeightsStorage();
}


MSEdgeWeightsStorage&
MSVehicle::getWeightsStorage() {
    return _getWeightsStorage();
}


MSEdgeWeightsStorage&
MSVehicle::_getWeightsStorage() const {
    if (myEdgeWeights == 0) {
        myEdgeWeights = new MSEdgeWeightsStorage();
    }
    return *myEdgeWeights;
}


// ------------ Interaction with move reminders
void
MSVehicle::workOnMoveReminders(SUMOReal oldPos, SUMOReal newPos, SUMOReal newSpeed) {
    // This erasure-idiom works for all stl-sequence-containers
    // See Meyers: Effective STL, Item 9
    for (MoveReminderCont::iterator rem = myMoveReminders.begin(); rem != myMoveReminders.end();) {
        if (!rem->first->notifyMove(*this, oldPos + rem->second, newPos + rem->second, newSpeed)) {
            rem = myMoveReminders.erase(rem);
        } else {
            ++rem;
        }
    }
}


void
MSVehicle::adaptLaneEntering2MoveReminder(const MSLane& enteredLane) {
    // save the old work reminders, patching the position information
    //  add the information about the new offset to the old lane reminders
    const SUMOReal oldLaneLength = myLane->getLength();
    for (MoveReminderCont::iterator rem = myMoveReminders.begin(); rem != myMoveReminders.end(); ++rem) {
        rem->second += oldLaneLength;
    }
    for (std::vector< MSMoveReminder* >::const_iterator rem = enteredLane.getMoveReminders().begin(); rem != enteredLane.getMoveReminders().end(); ++rem) {
        addReminder(*rem);
    }
}


// ------------ Other getter methods
Position
MSVehicle::getPosition() const {
    if (myLane == 0) {
        return Position(-1000, -1000);
    }
    Position result = myLane->getShape().positionAtLengthPosition(
            myLane->interpolateLanePosToGeometryPos(getPositionOnLane()));
    if (isChangingLanes()) {
        const Position other = myShadowLane->getShape().positionAtLengthPosition(
                myShadowLane->interpolateLanePosToGeometryPos(getPositionOnLane()));
        Line line = myLaneChangeMidpointPassed ?  Line(other, result) : Line(result, other);
        return line.getPositionAtDistance(myLaneChangeCompletion * line.length());
    } 
    return result;
}


SUMOReal
MSVehicle::getAngle() const {
    Position p1 = myLane->getShape().positionAtLengthPosition(myState.pos());
    Position p2 = myFurtherLanes.size() > 0
                  ? myFurtherLanes.back()->getShape().positionAtLengthPosition(myFurtherLanes.back()->getPartialOccupatorEnd())
                  : myLane->getShape().positionAtLengthPosition(myState.pos() - myType->getLength());
    if (p1 != p2) {
        return atan2(p1.x() - p2.x(), p2.y() - p1.y()) * 180. / PI;
    } else {
        return -myLane->getShape().rotationDegreeAtLengthPosition(getPositionOnLane());
    }
}


// ------------
bool
MSVehicle::addStop(const SUMOVehicleParameter::Stop& stopPar, SUMOTime untilOffset) {
    Stop stop;
    stop.lane = MSLane::dictionary(stopPar.lane);
    stop.busstop = MSNet::getInstance()->getBusStop(stopPar.busstop);
    stop.startPos = stopPar.startPos;
    stop.endPos = stopPar.endPos;
    stop.duration = stopPar.duration;
    stop.until = stopPar.until;
    if (stop.until != -1) {
        stop.until += untilOffset;
    }
    stop.triggered = stopPar.triggered;
    stop.parking = stopPar.parking;
    stop.reached = false;
    if (stop.startPos < 0 || stop.endPos > stop.lane->getLength()) {
        return false;
    }
    stop.edge = find(myCurrEdge, myRoute->end(), &stop.lane->getEdge());
    MSRouteIterator prevStopEdge = myCurrEdge;
    SUMOReal prevStopPos = myState.myPos;
    // where to insert the stop
    std::list<Stop>::iterator iter = myStops.begin();
    if (stopPar.index == STOP_INDEX_END || stopPar.index >= static_cast<int>(myStops.size())) {
        if (myStops.size() > 0) {
            prevStopEdge = myStops.back().edge;
            prevStopPos = myStops.back().endPos;
            iter = myStops.end();
            stop.edge = find(prevStopEdge, myRoute->end(), &stop.lane->getEdge());
        }
    } else {
        if (stopPar.index == STOP_INDEX_FIT) {
            while (iter != myStops.end() && (iter->edge < stop.edge ||
                                             (iter->endPos < stop.endPos && iter->edge == stop.edge))) {
                prevStopEdge = iter->edge;
                prevStopPos = iter->endPos;
                ++iter;
            }
        } else {
            int index = stopPar.index;
            while (index > 0) {
                prevStopEdge = iter->edge;
                prevStopPos = iter->endPos;
                ++iter;
                --index;
            }
            stop.edge = find(prevStopEdge, myRoute->end(), &stop.lane->getEdge());
        }
    }
    if (stop.edge == myRoute->end() || prevStopEdge > stop.edge ||
            (prevStopEdge == stop.edge && prevStopPos > stop.endPos)) {
        return false;
    }
    if (myCurrEdge == stop.edge && myState.myPos > stop.endPos - getCarFollowModel().brakeGap(myState.mySpeed)) {
        return false;
    }
    myStops.insert(iter, stop);
    return true;
}


bool
MSVehicle::isStopped() const {
    return !myStops.empty() && myStops.begin()->reached;
}


bool
MSVehicle::isParking() const {
    return isStopped() && myStops.begin()->parking;
}


SUMOReal
MSVehicle::processNextStop(SUMOReal currentVelocity) {
    if (myStops.empty()) {
        // no stops; pass
        return currentVelocity;
    }
    Stop& stop = myStops.front();
    if (stop.reached) {
        // ok, we have already reached the next stop
        // any waiting persons may board now
        bool boarded = MSNet::getInstance()->getPersonControl().boardAnyWaiting(&myLane->getEdge(), this);
        if (boarded) {
            if (stop.busstop != 0) {
                const std::vector<MSPerson*>& persons = myPersonDevice->getPersons();
                for (std::vector<MSPerson*>::const_iterator i = persons.begin(); i != persons.end(); ++i) {
                    stop.busstop->removePerson(*i);
                }
            }
            // the triggering condition has been fulfilled. Maybe we want to wait a bit longer for additional riders (car pooling)
            stop.triggered = false;
            if (myAmRegisteredAsWaitingForPerson) {
                MSNet::getInstance()->getVehicleControl().unregisterOneWaitingForPerson();
                myAmRegisteredAsWaitingForPerson = false;
            }
        }
        if (stop.duration <= 0 && !stop.triggered) {
            // we have waited long enough and fulfilled any passenger-requirements
            if (stop.busstop != 0) {
                // inform bus stop about leaving it
                stop.busstop->leaveFrom(this);
            }
            // the current stop is no longer valid
            MSNet::getInstance()->getVehicleControl().removeWaiting(&myLane->getEdge(), this);
            myStops.pop_front();
            // do not count the stopping time towards gridlock time.
            // Other outputs use an independent counter and are not affected.
            myWaitingTime = 0;
            // maybe the next stop is on the same edge; let's rebuild best lanes
            getBestLanes(true);
            // continue as wished...
        } else {
            // we have to wait some more time
            if (stop.triggered && !myAmRegisteredAsWaitingForPerson) {
                // we can only register after waiting for one step. otherwise we might falsely signal a deadlock
                MSNet::getInstance()->getVehicleControl().registerOneWaitingForPerson();
                myAmRegisteredAsWaitingForPerson = true;
            }
            stop.duration -= DELTA_T;
            return 0;
        }
    } else {
        // is the next stop on the current lane?
        if (stop.edge == myCurrEdge) {
            // get the stopping position
            SUMOReal endPos = stop.endPos;
            bool busStopsMustHaveSpace = true;
            if (stop.busstop != 0) {
                // on bus stops, we have to wait for free place if they are in use...
                endPos = stop.busstop->getLastFreePos(*this);
                if (endPos - 5. < stop.busstop->getBeginLanePosition()) { // !!! explicit offset
                    busStopsMustHaveSpace = false;
                }
            }
            if (myState.pos() + getVehicleType().getMinGap() >= endPos - BUS_STOP_OFFSET && busStopsMustHaveSpace) {
                // ok, we may stop (have reached the stop)
                stop.reached = true;
                MSNet::getInstance()->getVehicleControl().addWaiting(&myLane->getEdge(), this);
                // compute stopping time
                if (stop.until >= 0) {
                    if (stop.duration == -1) {
                        stop.duration = stop.until - MSNet::getInstance()->getCurrentTimeStep();
                    } else {
                        stop.duration = MAX2(stop.duration, stop.until - MSNet::getInstance()->getCurrentTimeStep());
                    }
                }
                if (stop.busstop != 0) {
                    // let the bus stop know the vehicle
                    stop.busstop->enter(this, myState.pos() + getVehicleType().getMinGap(), myState.pos() - myType->getLength());
                }
            }
            // decelerate
            return getCarFollowModel().stopSpeed(this, endPos - myState.pos());
        }
    }
    return currentVelocity;
}


void
MSVehicle::planMove(SUMOTime t, MSVehicle* pred, MSVehicle* neigh, SUMOReal lengthsInFront) {
#ifdef _MESSAGES
    if (myHBMsgEmitter != 0) {
        if (isOnRoad()) {
            SUMOReal timeStep = MSNet::getInstance()->getCurrentTimeStep();
            myHBMsgEmitter->writeHeartBeatEvent(myParameter->id, timeStep, myLane, myState.pos(), myState.speed(), getPosition().x(), getPosition().y());
        }
    }
#endif
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    if (gSelected.isSelected(GLO_VEHICLE, static_cast<const GUIVehicle*>(this)->getGlID())) {
        int bla = 0;
    }
#endif
    // remove information about approaching links, will be reset later in this step
    for (DriveItemVector::iterator i = myLFLinkLanes.begin(); i != myLFLinkLanes.end(); ++i) {
        if ((*i).myLink != 0) {
            (*i).myLink->removeApproaching(this);
        }
    }
    myLFLinkLanes.clear();
    //
    const MSCFModel& cfModel = getCarFollowModel();
    // vBeg is the initial maximum velocity of this vehicle in this step
    SUMOReal v = MIN2(cfModel.maxNextSpeed(myState.mySpeed, this), myLane->getVehicleMaxSpeed(this));
#ifndef NO_TRACI
    if (myInfluencer != 0) {
        SUMOReal vMin = MAX2(SUMOReal(0), getVehicleType().getCarFollowModel().getSpeedAfterMaxDecel(myState.mySpeed));
        SUMOReal vMax = getVehicleType().getCarFollowModel().maxNextSpeed(myState.mySpeed, this);
        v = myInfluencer->influenceSpeed(MSNet::getInstance()->getCurrentTimeStep(), v, v, vMin, vMax);
        // !!! recheck - why is it done, here?
    }
#endif

    SUMOReal vehicleLength = getVehicleType().getLength();
    SUMOReal maxV = cfModel.maxNextSpeed(myState.mySpeed, this);
    SUMOReal dist = SPEED2DIST(maxV) + cfModel.brakeGap(maxV);
    const std::vector<MSLane*>& bestLaneConts = getBestLanesContinuation();
    assert(bestLaneConts.size() > 0);
#ifdef HAVE_INTERNAL_LANES
    bool hadNonInternal = false;
#else
    bool hadNonInternal = true;
#endif
    SUMOReal seen = myLane->getLength() - myState.myPos; // the distance already "seen"; in the following always up to the end of the current "lane"
    SUMOReal seenNonInternal = 0;
    cfModel.leftVehicleVsafe(this, neigh, v);
    SUMOReal vLinkPass = MIN2(estimateSpeedAfterDistance(seen, v), myLane->getVehicleMaxSpeed(this)); // upper bound
    unsigned int view = 0;
    bool firstLane = true;
    int lastLink = -1;
    std::pair<MSVehicle*, SUMOReal> leaderInfo = pred != 0 ? std::pair<MSVehicle*, SUMOReal>(pred, gap2pred(*pred)) : std::pair<MSVehicle*, SUMOReal>((MSVehicle*) 0, 0);
    // iterator over subsequent lanes and fill myLFLinkLanes until stopping distance or stopped
    MSLane* lane = myLane; 
    while (true) {
        SUMOReal laneStopOffset = lane->getLength() > getVehicleType().getMinGap() ? getVehicleType().getMinGap() : POSITION_EPS;
        SUMOReal stopDist = MAX2(SUMOReal(0), seen - laneStopOffset);
        // check leader on lane
        //  leader is given for the first edge only
        if (!firstLane) {
            leaderInfo = lane->getLastVehicleInformation();
            leaderInfo.second = leaderInfo.second + seen - lane->getLength() - getVehicleType().getMinGap();
            vLinkPass = MIN2(estimateSpeedAfterDistance(lane->getLength(), v), lane->getVehicleMaxSpeed(this)); // upper bound
        } else if (leaderInfo.first == 0) {
            // we still have to account vehicles lapping into the lane we are currently at
            if (myLane->getPartialOccupator() != 0) {
                leaderInfo = std::pair<MSVehicle*, SUMOReal>(myLane->getPartialOccupator(), myLane->getPartialOccupatorEnd() - myState.myPos - getVehicleType().getMinGap());
            }
        }
        adaptToLeader(leaderInfo, seen, lastLink, lane, v, vLinkPass);

        // process stops
        if (!myStops.empty() && &myStops.begin()->lane->getEdge() == &lane->getEdge()) {
            // we are approaching a stop on the edge; must not drive further
            const Stop& stop = *myStops.begin();
            SUMOReal stopDist = stop.busstop == 0 ? seen + stop.endPos - lane->getLength() : seen + stop.busstop->getLastFreePos(*this) - POSITION_EPS - lane->getLength();
            SUMOReal stopSpeed = cfModel.stopSpeed(this, stopDist);
            if (lastLink > 0) {
                myLFLinkLanes[lastLink].adaptLeaveSpeed(stopSpeed);
            }
            v = MIN2(v, stopSpeed);
            myLFLinkLanes.push_back(DriveProcessItem(0, v, v, false, 0, 0, stopDist));
            break;
        }

        // move to next lane
        //  get the next link used
        MSLinkCont::const_iterator link = myLane->succLinkSec(*this, view + 1, *lane, bestLaneConts);
        //  check whether the vehicle is on its final edge
        bool onAppropriateLane = !lane->isLinkEnd(link); // !!! wird "appropriate" noch benutzt?
        bool routeEnds = myCurrEdge + view + 1 == myRoute->end();
        if (routeEnds) {
            const SUMOReal arrivalSpeed = (myParameter->arrivalSpeedProcedure == ARRIVAL_SPEED_GIVEN ?
                                           myParameter->arrivalSpeed : lane->getVehicleMaxSpeed(this));
            // subtract the arrival speed from the remaining distance so we get one additional driving step with arrival speed
            const SUMOReal distToArrival = seen + myArrivalPos - lane->getLength() - SPEED2DIST(arrivalSpeed);
            const SUMOReal va = cfModel.freeSpeed(this, getSpeed(), distToArrival, arrivalSpeed);
            v = MIN2(v, va);
            if (lastLink > 0) {
                myLFLinkLanes[lastLink].adaptLeaveSpeed(va);
            }
            myLFLinkLanes.push_back(DriveProcessItem(0, v, v, false, 0, 0, seen));
            break;
        }
        // check whether the lane is a dead end
        if (!onAppropriateLane) {
            if (!routeEnds) {
                SUMOReal va = MIN2(cfModel.stopSpeed(this, stopDist), lane->getVehicleMaxSpeed(this));
                if (lastLink > 0) {
                    myLFLinkLanes[lastLink].adaptLeaveSpeed(va);
                }
                v = MIN2(va, v);
            }
            myLFLinkLanes.push_back(DriveProcessItem(0, v, v, false, 0, 0, seen));
            break;
        }
        // check whether we need to slow down in order to finish a continuous lane change
        if (isChangingLanes() and myLaneChangeMidpointPassed) {
            // shadow is on the old lane. can it continue?
            // XXX
        }

        const bool yellow = (*link)->getState() == LINKSTATE_TL_YELLOW_MAJOR || (*link)->getState() == LINKSTATE_TL_YELLOW_MINOR;
        const bool red = (*link)->getState() == LINKSTATE_TL_RED;
        const bool setRequest = v > 0; // even if red, if we cannot break we should issue a request
        const SUMOReal vLinkWait = MIN2(v, cfModel.stopSpeed(this, stopDist));
        if ((yellow || red) && seen > cfModel.brakeGap(myState.mySpeed) - myState.mySpeed * cfModel.getHeadwayTime()) {
            // the vehicle is able to brake in front of a yellow/red traffic light
            myLFLinkLanes.push_back(DriveProcessItem(*link, vLinkWait, vLinkWait, false, t + TIME2STEPS(seen / vLinkWait), vLinkWait, stopDist));
            break;
        }

#ifdef HAVE_INTERNAL_LANES
        // we want to pass the link but need to check for foes on internal lanes
        std::pair<MSVehicle*, SUMOReal> linkLeaderInfo = (*link)->getLeaderInfo(myLeaderForLink, seen - getVehicleType().getMinGap());
        if (linkLeaderInfo.first != 0) {
            // the vehicle to enter the junction first has priority
            if (!linkLeaderInfo.first->hasLinkLeader(this)) {
                myLeaderForLink[*link] = linkLeaderInfo.first->getID();
                myLinkLeaders.insert(linkLeaderInfo.first->getID());
                adaptToLeader(linkLeaderInfo, seen, lastLink, lane, v, vLinkPass);
            }
        }
#endif

        SUMOReal va = firstLane ? v : lane->getVehicleMaxSpeed(this);
        if (lastLink > 0) {
            myLFLinkLanes[lastLink].adaptLeaveSpeed(va);
        } //if(!myLFLinkLanes.empty()) { myLFLinkLanes.back().accelV = va; }
        lastLink = (int)myLFLinkLanes.size();
        // vehicles should decelerate when approaching a minor link
        if (!(*link)->havePriority() && stopDist > getCarFollowModel().getMaxDecel()) {
            // vehicle decelerates just enough to be able to stop if necessary and then accelerates 
            const SUMOReal arrivalSpeed = getCarFollowModel().getMaxDecel() + getCarFollowModel().getMaxAccel();
            const SUMOReal v1 = MAX2(vLinkWait, arrivalSpeed);
            // now + time spent decelerating + time spent at full speed
            const SUMOTime arrivalTime = t + TIME2STEPS((v1 - arrivalSpeed) / getCarFollowModel().getMaxDecel() 
                    + (seen - (v1*v1 - arrivalSpeed*arrivalSpeed) * 0.5 / getCarFollowModel().getMaxDecel()) / vLinkWait); 
            myLFLinkLanes.push_back(DriveProcessItem(*link, vLinkWait, vLinkWait, setRequest, arrivalTime, arrivalSpeed, stopDist));
        } else {
            if (vLinkPass >= v) {
                const SUMOReal accelTime = (vLinkPass - v) / getCarFollowModel().getMaxAccel();
                const SUMOReal accelWay = accelTime * (vLinkPass + v) * 0.5;
                const SUMOTime arrivalTime = t + TIME2STEPS(accelTime + MAX2(SUMOReal(0), seen - accelWay) / vLinkPass);
                myLFLinkLanes.push_back(DriveProcessItem(*link, v, vLinkWait, setRequest, arrivalTime, vLinkPass, stopDist));
            } else {
                const SUMOReal decelTime = (v - vLinkPass) / getCarFollowModel().getMaxDecel();
                const SUMOReal decelWay = decelTime * (vLinkPass + v) * 0.5;
                const SUMOTime arrivalTime = t + TIME2STEPS(decelTime + MAX2(SUMOReal(0), seen - decelWay) / vLinkPass);
                myLFLinkLanes.push_back(DriveProcessItem(*link, v, vLinkWait, setRequest, arrivalTime, vLinkPass, stopDist));
            }
        }
        // get the following lane
        lane = (*link)->getViaLaneOrLane();
#ifdef HAVE_INTERNAL_LANES
        if ((*link)->getViaLane() == 0) {
            hadNonInternal = true;
            ++view;
        }
#else
        ++view;
#endif
        const SUMOReal leaveSpeed = estimateLeaveSpeed(*link, vLinkPass);
        myLFLinkLanes[lastLink].adaptLeaveSpeed(leaveSpeed);

        firstLane = false;
        if (!setRequest || ((v <= 0 || seen > dist) && hadNonInternal && seenNonInternal > vehicleLength * 2)) {
            break;
        }
        // the link was passed
        // compute the velocity to use when the link is not blocked by other vehicles
        //  the vehicle shall be not faster when reaching the next lane than allowed
        va = MAX2(lane->getVehicleMaxSpeed(this), cfModel.freeSpeed(this, getSpeed(), seen, lane->getVehicleMaxSpeed(this)));
        v = MIN2(va, v);
        seenNonInternal += lane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL ? 0 : lane->getLength();
        seen += lane->getLength();
    }
    checkRewindLinkLanes(lengthsInFront);
}


void
MSVehicle::adaptToLeader(std::pair<MSVehicle*, SUMOReal> leaderInfo, SUMOReal seen, int lastLink, MSLane* lane, SUMOReal& v, SUMOReal& vLinkPass) {
    if (leaderInfo.first != 0) {
        const MSCFModel& cfModel = getCarFollowModel();
        SUMOReal vsafeLeader = 0;
        if (leaderInfo.second >= 0) {
            vsafeLeader = cfModel.followSpeed(this, getSpeed(), leaderInfo.second, leaderInfo.first->getSpeed(), leaderInfo.first->getCarFollowModel().getMaxDecel());
        } else {
            // the leading, in-lapping vehicle is occupying the complete next lane
            // stop before entering this lane
            vsafeLeader = cfModel.stopSpeed(this, seen - lane->getLength() - POSITION_EPS);
        }
        if (lastLink > 0) {
            myLFLinkLanes[lastLink].adaptLeaveSpeed(vsafeLeader);
        }
        v = MIN2(v, vsafeLeader);
        vLinkPass = MIN2(vLinkPass, vsafeLeader);
    }
}



SUMOReal 
MSVehicle::estimateLeaveSpeed(MSLink* link, SUMOReal vLinkPass) {
    // estimate leave speed for passing time computation
    // l=linkLength, a=accel, t=continuousTime, v=vLeave
    // l=v*t + 0.5*a*t^2, solve for t and multiply with a, then add v
    return MIN2(link->getViaLaneOrLane()->getVehicleMaxSpeed(this),
            estimateSpeedAfterDistance(link->getLength(), vLinkPass));
}


SUMOReal 
MSVehicle::estimateSpeedAfterDistance(SUMOReal dist, SUMOReal v) {
    // dist=v*t + 0.5*accel*t^2, solve for t and multiply with accel, then add v
    return MIN2(getVehicleType().getMaxSpeed(), 
            (SUMOReal)sqrt(2 * dist * getVehicleType().getCarFollowModel().getMaxAccel() + v * v));
}

bool
MSVehicle::executeMove() {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    if (gSelected.isSelected(GLO_VEHICLE, static_cast<const GUIVehicle*>(this)->getGlID())) {
        int bla = 0;
    }
#endif
    // get vsafe
    SUMOReal vSafe = 0;
    myHaveToWaitOnNextLink = false;

    assert(myLFLinkLanes.size() != 0);
    DriveItemVector::iterator i;
    bool braking = false;
    bool lastWasGreenCont = false;
    for (i = myLFLinkLanes.begin(); i != myLFLinkLanes.end(); ++i) {
        MSLink* link = (*i).myLink;
        // the vehicle must change the lane on one of the next lanes
        if (link != 0 && (*i).mySetRequest) {
            const LinkState ls = link->getState();
            // vehicles should brake when running onto a yellow light if the distance allows to halt in front
            const bool yellow = ls == LINKSTATE_TL_YELLOW_MAJOR || ls == LINKSTATE_TL_YELLOW_MINOR;
            const SUMOReal brakeGap = getCarFollowModel().brakeGap(myState.mySpeed) - getCarFollowModel().getHeadwayTime() * myState.mySpeed;
            if (yellow && ((*i).myDistance > brakeGap || myState.mySpeed < ACCEL2SPEED(getCarFollowModel().getMaxDecel()))) {
                vSafe = (*i).myVLinkWait;
                braking = true;
                lastWasGreenCont = false;
                link->removeApproaching(this);
                break;
            }
            //
            const bool opened = yellow || link->opened((*i).myArrivalTime, (*i).myArrivalSpeed, (*i).getLeaveSpeed(), getVehicleType().getLengthWithGap());
            // vehicles should decelerate when approaching a minor link
            // XXX check if this is still necessary
            if (opened && !lastWasGreenCont && !link->havePriority() && (*i).myDistance > getCarFollowModel().getMaxDecel()) {
                vSafe = (*i).myVLinkWait;
                braking = true;
                lastWasGreenCont = false;
                if (ls == LINKSTATE_EQUAL) {
                    link->removeApproaching(this);
                }
                break; // could be revalidated
            }
            // have waited; may pass if opened...
            if (opened) {
                vSafe = (*i).myVLinkPass;
                lastWasGreenCont = link->isCont() && (ls == LINKSTATE_TL_GREEN_MAJOR);
            } else {
                lastWasGreenCont = false;
                vSafe = (*i).myVLinkWait;
                braking = true;
                if (ls == LINKSTATE_EQUAL) {
                    link->removeApproaching(this);
                }
                break;
            }
        } else {
            vSafe = (*i).myVLinkWait;
            braking = true;
            break;
        }
    }
    if (braking) {
        myHaveToWaitOnNextLink = true;
    }

    SUMOReal vNext = getCarFollowModel().moveHelper(this, vSafe);
    //if (vNext > vSafe) {
    //    WRITE_WARNING("vehicle '" + getID() + "' cannot brake hard enough to reach safe speed " 
    //            + toString(vSafe) + ", moving at " + toString(vNext) + " instead. time=" 
    //            + time2string(MSNet::getInstance()->getCurrentTimeStep()) + ".");
    //}
    vNext = MAX2(vNext, (SUMOReal) 0.);
#ifndef NO_TRACI
    if (myInfluencer != 0) {
        const SUMOReal vMax = getVehicleType().getCarFollowModel().maxNextSpeed(myState.mySpeed, this);
        const SUMOReal vMin = MAX2(SUMOReal(0), getVehicleType().getCarFollowModel().getSpeedAfterMaxDecel(myState.mySpeed));
        vNext = myInfluencer->influenceSpeed(MSNet::getInstance()->getCurrentTimeStep(), vNext, vSafe, vMin, vMax);
    }
#endif
    // visit waiting time
    if (vNext <= 0.1) {
        myWaitingTime += DELTA_T;
        braking = true;
    } else {
        myWaitingTime = 0;
    }
    if (myState.mySpeed < vNext) {
        braking = false;
    }
    if (braking) {
        switchOnSignal(VEH_SIGNAL_BRAKELIGHT);
    } else {
        switchOffSignal(VEH_SIGNAL_BRAKELIGHT);
    }
    // call reminders after vNext is set
    const SUMOReal pos = myState.myPos;

#ifdef _MESSAGES
    if (myHBMsgEmitter != 0) {
        if (isOnRoad()) {
            SUMOReal timeStep = MSNet::getInstance()->getCurrentTimeStep();
            myHBMsgEmitter->writeHeartBeatEvent(myParameter->id, timeStep, myLane, myState.pos(), myState.speed(), getPosition().x(), getPosition().y());
        }
    }
    if (myBMsgEmitter != 0) {
        if (vNext < myState.mySpeed) {
            SUMOReal timeStep = MSNet::getInstance()->getCurrentTimeStep();
            myBMsgEmitter->writeBreakEvent(myParameter->id, timeStep, myLane, myState.pos(), myState.speed(), getPosition().x(), getPosition().y());
        }
    }
#endif
    // update position and speed
    myAcceleration = vNext - myState.mySpeed;
    myState.myPos += SPEED2DIST(vNext);
    myState.mySpeed = vNext;
    std::vector<MSLane*> passedLanes;
    for (std::vector<MSLane*>::reverse_iterator i = myFurtherLanes.rbegin(); i != myFurtherLanes.rend(); ++i) {
        passedLanes.push_back(*i);
    }
    if (passedLanes.size() == 0 || passedLanes.back() != myLane) {
        passedLanes.push_back(myLane);
    }
    bool moved = false;
    // move on lane(s)
    if (myState.myPos <= myLane->getLength()) {
        // we are staying at our lane
        //  there is no need to go over succeeding lanes
        workOnMoveReminders(pos, pos + SPEED2DIST(vNext), vNext);
    } else {
        // we are moving at least to the next lane (maybe pass even more than one)
        if (myCurrEdge != myRoute->end() - 1) {
            MSLane* approachedLane = myLane;
            // move the vehicle forward
            for (i = myLFLinkLanes.begin(); i != myLFLinkLanes.end() && approachedLane != 0 && myState.myPos > approachedLane->getLength(); ++i) {
                leaveLane(MSMoveReminder::NOTIFICATION_JUNCTION);
                MSLink* link = (*i).myLink;
                // check whether the vehicle was allowed to enter lane
                //  otherwise it is decelareted and we do not need to test for it's
                //  approach on the following lanes when a lane changing is performed
                // proceed to the next lane
                if (link != 0) {
                    approachedLane = link->getViaLaneOrLane();
                } else {
                    approachedLane = 0;
                }
                if (approachedLane != myLane && approachedLane != 0) {
                    myState.myPos -= myLane->getLength();
                    assert(myState.myPos > 0);
                    enterLaneAtMove(approachedLane);
                    myLane = approachedLane;
#ifdef HAVE_INTERNAL_LANES
                    // erase leader for the past link
                    if (myLeaderForLink.find(link) != myLeaderForLink.end()) {
                        myLinkLeaders.erase(myLeaderForLink[link]);
                        myLeaderForLink.erase(link);
                    }
#endif
                    moved = true;
                    if (approachedLane->getEdge().isVaporizing()) {
                        break;
                    }
                }
                passedLanes.push_back(approachedLane);
            }
        }
    }
    // clear previously set information
    for (std::vector<MSLane*>::iterator i = myFurtherLanes.begin(); i != myFurtherLanes.end(); ++i) {
        (*i)->resetPartialOccupation(this);
    }
    myFurtherLanes.clear();
    if (!hasArrived() && !myLane->getEdge().isVaporizing()) {
        if (myState.myPos > myLane->getLength()) {
            WRITE_WARNING("Vehicle '" + getID() + "' performs emergency stop on lane '" + myLane->getID() + " at position " +
                          toString(myState.myPos) + " (decel=" + toString(myAcceleration - myState.mySpeed) + "), time="
                          + time2string(MSNet::getInstance()->getCurrentTimeStep()) + ".");
            myState.myPos = myLane->getLength();
            myState.mySpeed = 0;
        }
        if (myState.myPos - getVehicleType().getLength() < 0 && passedLanes.size() > 0) {
            SUMOReal leftLength = getVehicleType().getLength() - myState.myPos;
            std::vector<MSLane*>::reverse_iterator i = passedLanes.rbegin() + 1;
            while (leftLength > 0 && i != passedLanes.rend()) {
                myFurtherLanes.push_back(*i);
                leftLength -= (*i)->setPartialOccupation(this, leftLength);
                ++i;
            }
        }
        if (isChangingLanes()) {
            continueLaneChangeManeuver(moved);
        }
        setBlinkerInformation();
    }
    return moved;
}


SUMOReal
MSVehicle::getSpaceTillLastStanding(MSLane* l, bool& foundStopped) {
    SUMOReal lengths = 0;
    const MSLane::VehCont& vehs = l->getVehiclesSecure();
    for (MSLane::VehCont::const_iterator i = vehs.begin(); i != vehs.end(); ++i) {
        if ((*i)->getSpeed() < .1) {
            foundStopped = true;
            SUMOReal ret = (*i)->getPositionOnLane() - (*i)->getVehicleType().getLengthWithGap() - lengths;
            l->releaseVehicles();
            return ret;
        }
        lengths += (*i)->getVehicleType().getLengthWithGap();
    }
    l->releaseVehicles();
    return l->getLength() - lengths;
}


void
MSVehicle::checkRewindLinkLanes(SUMOReal lengthsInFront) {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    if (gSelected.isSelected(GLO_VEHICLE, static_cast<const GUIVehicle*>(this)->getGlID())) {
        int bla = 0;
        if (MSNet::getInstance()->getCurrentTimeStep() == 152000) {
            bla = 0;
        }
    }
#endif
#ifdef HAVE_INTERNAL_LANES
    if (MSGlobals::gUsingInternalLanes) {
        int removalBegin = -1;
        bool hadVehicle = false;
        SUMOReal seenSpace = -lengthsInFront;

        std::vector<SUMOReal> availableSpace;
        std::vector<bool> hadVehicles;
        bool foundStopped = false;

        for (unsigned int i = 0; i < myLFLinkLanes.size(); ++i) {
            // skip unset links
            DriveProcessItem& item = myLFLinkLanes[i];
            if (item.myLink == 0 || foundStopped) {
                availableSpace.push_back(seenSpace);
                hadVehicles.push_back(hadVehicle);
                continue;
            }
            // get the next lane, determine whether it is an internal lane
            MSLane* approachedLane = item.myLink->getViaLane();
            if (approachedLane != 0) {
                if (item.myLink->isCrossing()/* && item.myLink->willHaveBlockedFoe()*/) {
                    seenSpace = seenSpace - approachedLane->getVehLenSum();
                    hadVehicle |= approachedLane->getVehicleNumber() != 0;
                } else {
                    seenSpace = seenSpace + getSpaceTillLastStanding(approachedLane, foundStopped);// - approachedLane->getVehLenSum() + approachedLane->getLength();
                    hadVehicle |= approachedLane->getVehicleNumber() != 0;
                }
                availableSpace.push_back(seenSpace);
                hadVehicles.push_back(hadVehicle);
                continue;
            }
            approachedLane = item.myLink->getLane();
            MSVehicle* last = approachedLane->getLastVehicle();
            if (last == 0) {
                last = approachedLane->getPartialOccupator();
                if (last != 0) {
                    SUMOReal m = MAX2(seenSpace, seenSpace + approachedLane->getPartialOccupatorEnd() + last->getCarFollowModel().brakeGap(last->getSpeed()));
                    availableSpace.push_back(m);
                    hadVehicle = true;
                    seenSpace = seenSpace + getSpaceTillLastStanding(approachedLane, foundStopped);// - approachedLane->getVehLenSum() + approachedLane->getLength();
                    if (last->myHaveToWaitOnNextLink) {
                        foundStopped = true;
                    }
                } else {
//                    seenSpace = seenSpace - approachedLane->getVehLenSum() + approachedLane->getLength();
//                    availableSpace.push_back(seenSpace);
                    availableSpace.push_back(seenSpace + getSpaceTillLastStanding(approachedLane, foundStopped));
                    if (!foundStopped) {
                        seenSpace = seenSpace - approachedLane->getVehLenSum() + approachedLane->getLength();
                    } else {
                        seenSpace = availableSpace.back();
                    }
                }
            } else {
                if (last->signalSet(VEH_SIGNAL_BRAKELIGHT)) {
                    SUMOReal lastBrakeGap = last->getCarFollowModel().brakeGap(approachedLane->getLastVehicle()->getSpeed());
                    SUMOReal lastGap = last->getPositionOnLane() - last->getVehicleType().getLengthWithGap() + lastBrakeGap - last->getSpeed() * last->getCarFollowModel().getHeadwayTime();
                    SUMOReal m = MAX2(seenSpace, seenSpace + lastGap);
                    availableSpace.push_back(m);
                    seenSpace = seenSpace + getSpaceTillLastStanding(approachedLane, foundStopped);// - approachedLane->getVehLenSum() + approachedLane->getLength();
                } else {
//                    seenSpace = seenSpace - approachedLane->getVehLenSum() + approachedLane->getLength();
//                    availableSpace.push_back(seenSpace);
                    availableSpace.push_back(seenSpace + getSpaceTillLastStanding(approachedLane, foundStopped));
                    if (!foundStopped) {
                        seenSpace = seenSpace - approachedLane->getVehLenSum() + approachedLane->getLength();
                    } else {
                        seenSpace = availableSpace.back();
                    }
                }
                if (last->myHaveToWaitOnNextLink) {
                    foundStopped = true;
                }
                hadVehicle = true;
            }
            hadVehicles.push_back(hadVehicle);
        }
#ifdef DEBUG_VEHICLE_GUI_SELECTION
        if (gSelected.isSelected(GLO_VEHICLE, static_cast<const GUIVehicle*>(this)->getGlID())) {
            int bla = 0;
        }
#endif
        for (int i = (int)(myLFLinkLanes.size() - 1); i > 0; --i) {
            DriveProcessItem& item = myLFLinkLanes[i - 1];
            const bool opened = item.myLink != 0 && (item.myLink->havePriority() ||
                                item.myLink->opened(item.myArrivalTime, item.myArrivalSpeed,
                                                    item.getLeaveSpeed(), getVehicleType().getLengthWithGap()));
            bool allowsContinuation = item.myLink == 0 || item.myLink->isCont() || !hadVehicles[i] || opened;
            if (!opened && item.myLink != 0) {
                if (i > 1) {
                    DriveProcessItem& item2 = myLFLinkLanes[i - 2];
                    if (item2.myLink != 0 && item2.myLink->isCont()) {
                        allowsContinuation = true;
                    }
                }
            }
            if (allowsContinuation) {
                availableSpace[i - 1] = availableSpace[i];
            }
        }

        for (unsigned int i = 0; hadVehicle && i < myLFLinkLanes.size() && removalBegin < 0; ++i) {
            // skip unset links
            DriveProcessItem& item = myLFLinkLanes[i];
            if (item.myLink == 0) {
                continue;
            }
            /*
            SUMOReal impatienceCorrection = MAX2(SUMOReal(0), SUMOReal(SUMOReal(myWaitingTime)));
            if (seenSpace<getVehicleType().getLengthWithGap()-impatienceCorrection/10.&&nextSeenNonInternal!=0) {
                removalBegin = lastLinkToInternal;
            }
            */

            SUMOReal leftSpace = availableSpace[i] - getVehicleType().getLengthWithGap();
            if (leftSpace < 0/* && item.myLink->willHaveBlockedFoe()*/) {
                SUMOReal impatienceCorrection = 0;
                /*
                if(item.myLink->getState()==LINKSTATE_MINOR) {
                    impatienceCorrection = MAX2(SUMOReal(0), STEPS2TIME(myWaitingTime));
                }
                */
                if (leftSpace < -impatienceCorrection / 10.) {
                    removalBegin = i;
                }
                //removalBegin = i;
            }
        }
        if (removalBegin != -1 && !(removalBegin == 0 && myLane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL)) {
            while (removalBegin < (int)(myLFLinkLanes.size())) {
                const SUMOReal brakeGap = getCarFollowModel().brakeGap(myState.mySpeed) - getCarFollowModel().getHeadwayTime() * myState.mySpeed;
                myLFLinkLanes[removalBegin].myVLinkPass = myLFLinkLanes[removalBegin].myVLinkWait;
                if (myLFLinkLanes[removalBegin].myDistance >= brakeGap || (myLFLinkLanes[removalBegin].myDistance > 0 && myState.mySpeed < ACCEL2SPEED(getCarFollowModel().getMaxDecel()))) {
                    myLFLinkLanes[removalBegin].mySetRequest = false;
                }
                ++removalBegin;
            }
        }
    }
#else
    UNUSED_PARAMETER(lengthsInFront);
#endif
    for (DriveItemVector::iterator i = myLFLinkLanes.begin(); i != myLFLinkLanes.end(); ++i) {
        if ((*i).myLink != 0) {
            const SUMOReal leaveSpeed = estimateLeaveSpeed((*i).myLink, (*i).myArrivalSpeed);
            (*i).myLink->setApproaching(this, (*i).myArrivalTime, (*i).myArrivalSpeed, leaveSpeed, (*i).mySetRequest);
        }
    }
}


void
MSVehicle::activateReminders(const MSMoveReminder::Notification reason) {
    for (MoveReminderCont::iterator rem = myMoveReminders.begin(); rem != myMoveReminders.end();) {
        if (rem->first->getLane() != 0 && rem->first->getLane() != getLane()) {
            ++rem;
        } else {
            if (rem->first->notifyEnter(*this, reason)) {
                ++rem;
            } else {
                rem = myMoveReminders.erase(rem);
            }
        }
    }
}


bool
MSVehicle::enterLaneAtMove(MSLane* enteredLane, bool onTeleporting) {
    myAmOnNet = !onTeleporting;
    // vaporizing edge?
    /*
    if (enteredLane->getEdge().isVaporizing()) {
        // yep, let's do the vaporization...
        myLane = enteredLane;
        return true;
    }
    */
    // move mover reminder one lane further
    adaptLaneEntering2MoveReminder(*enteredLane);
    // set the entered lane as the current lane
    myLane = enteredLane;

    // internal edges are not a part of the route...
    if (enteredLane->getEdge().getPurpose() != MSEdge::EDGEFUNCTION_INTERNAL) {
        assert(&enteredLane->getEdge() == *(myCurrEdge + 1));
        ++myCurrEdge;
    }
    if (!onTeleporting) {
        // may be optimized: compute only, if the current or the next have more than one lane...!!!
        getBestLanes(true);
        activateReminders(MSMoveReminder::NOTIFICATION_JUNCTION);
#ifndef NO_TRACI
        if (myInfluencer != 0) {
            myLaneChangeModel->requestLaneChange(myInfluencer->checkForLaneChanges(MSNet::getInstance()->getCurrentTimeStep(), **myCurrEdge, getLaneIndex()));
        }
#endif
    } else {
        activateReminders(MSMoveReminder::NOTIFICATION_TELEPORT);
        // normal move() isn't called so reset position here
        myState.myPos = 0;
    }
    return hasArrived();
}


void
MSVehicle::enterLaneAtLaneChange(MSLane* enteredLane) {
    myAmOnNet = true;
#ifdef _MESSAGES
    if (myLCMsgEmitter != 0) {
        SUMOReal timeStep = MSNet::getInstance()->getCurrentTimeStep();
        myLCMsgEmitter->writeLaneChangeEvent(myParameter->id, timeStep, myLane, myState.pos(), myState.speed(), enteredLane, getPosition().x(), getPosition().y());
    }
#endif
    myLane = enteredLane;
    // need to update myCurrentLaneInBestLanes
    getBestLanes();
    // switch to and activate the new lane's reminders
    // keep OldLaneReminders
    for (std::vector< MSMoveReminder* >::const_iterator rem = enteredLane->getMoveReminders().begin(); rem != enteredLane->getMoveReminders().end(); ++rem) {
        addReminder(*rem);
    }
    activateReminders(MSMoveReminder::NOTIFICATION_LANE_CHANGE);
    /*
        for (std::vector<MSLane*>::iterator i = myFurtherLanes.begin(); i != myFurtherLanes.end(); ++i) {
            (*i)->resetPartialOccupation(this);
        }
        myFurtherLanes.clear();
    */
    if (myState.myPos - getVehicleType().getLength() < 0) {
        // we have to rebuild "further lanes"
        const MSRoute& route = getRoute();
        MSRouteIterator i = myCurrEdge;
        MSLane* lane = myLane;
        SUMOReal leftLength = getVehicleType().getLength() - myState.myPos;
        while (i != route.begin() && leftLength > 0) {
            /* const MSEdge* const prev = */ *(--i);
            lane = lane->getLogicalPredecessorLane();
            if (lane == 0) {
                break;
            }
            myFurtherLanes.push_back(lane);
            leftLength -= (lane)->setPartialOccupation(this, leftLength);
            /*
            const std::vector<MSLane::IncomingLaneInfo> &incomingLanes = lane->getIncomingLanes();
            for (std::vector<MSLane::IncomingLaneInfo>::const_iterator j = incomingLanes.begin(); j != incomingLanes.end(); ++j) {
                if (&(*j).lane->getEdge() == prev) {
            #ifdef HAVE_INTERNAL_LANES
                    (*j).lane->setPartialOccupation(this, leftLength);
            #else
                    leftLength -= (*j).length;
                    (*j).lane->setPartialOccupation(this, leftLength);
            #endif
                    leftLength -= (*j).lane->getLength();
                    break;
                }
            }
            */
        }
    }
#ifndef NO_TRACI
    // check if further changes are necessary
    if (myInfluencer != 0) {
        myLaneChangeModel->requestLaneChange(myInfluencer->checkForLaneChanges(MSNet::getInstance()->getCurrentTimeStep(), **myCurrEdge, getLaneIndex()));
    }
#endif
}


void
MSVehicle::enterLaneAtInsertion(MSLane* enteredLane, SUMOReal pos, SUMOReal speed, MSMoveReminder::Notification notification) {
    myState = State(pos, speed);
    assert(myState.myPos >= 0);
    assert(myState.mySpeed >= 0);
    myWaitingTime = 0;
    myLane = enteredLane;
    // set and activate the new lane's reminders
    for (std::vector< MSMoveReminder* >::const_iterator rem = enteredLane->getMoveReminders().begin(); rem != enteredLane->getMoveReminders().end(); ++rem) {
        addReminder(*rem);
    }
    activateReminders(notification);
    std::string msg;
    if (MSGlobals::gCheckRoutes && !hasValidRoute(msg)) {
        throw ProcessError("Vehicle '" + getID() + "' has no valid route. " + msg);
    }
    myAmOnNet = true;
    // build the list of lanes the vehicle is lapping into
    SUMOReal leftLength = myType->getLength() - pos;
    MSLane* clane = enteredLane;
    while (leftLength > 0) {
        clane = clane->getLogicalPredecessorLane();
        if (clane == 0) {
            break;
        }
        myFurtherLanes.push_back(clane);
        leftLength -= (clane)->setPartialOccupation(this, leftLength);
    }
}


void
MSVehicle::leaveLane(const MSMoveReminder::Notification reason) {
    for (MoveReminderCont::iterator rem = myMoveReminders.begin(); rem != myMoveReminders.end();) {
        if (rem->first->notifyLeave(*this, myState.myPos + rem->second, reason)) {
            ++rem;
        } else {
            rem = myMoveReminders.erase(rem);
        }
    }
    if (reason != MSMoveReminder::NOTIFICATION_JUNCTION) {
        for (std::vector<MSLane*>::iterator i = myFurtherLanes.begin(); i != myFurtherLanes.end(); ++i) {
            (*i)->resetPartialOccupation(this);
        }
        myFurtherLanes.clear();
    }
    if (reason >= MSMoveReminder::NOTIFICATION_TELEPORT) {
        myAmOnNet = false;
    }
}


MSAbstractLaneChangeModel&
MSVehicle::getLaneChangeModel() {
    return *myLaneChangeModel;
}


const MSAbstractLaneChangeModel&
MSVehicle::getLaneChangeModel() const {
    return *myLaneChangeModel;
}


const std::vector<MSVehicle::LaneQ>&
MSVehicle::getBestLanes(bool forceRebuild, MSLane* startLane) const {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    if (gSelected.isSelected(GLO_VEHICLE, static_cast<const GUIVehicle*>(this)->getGlID())) {
        int bla = 0;
        myLastBestLanesEdge = 0;
    }
#endif

    if (startLane == 0) {
        startLane = myLane;
    }
    assert(startLane != 0);
    // update occupancy and current lane index, only, if the vehicle has not moved to a new lane
    // (never for internal lanes)
    if ((myLastBestLanesEdge == &startLane->getEdge() && !forceRebuild) || 
            (startLane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL && myBestLanes.size() > 0)) {
        std::vector<LaneQ>& lanes = *myBestLanes.begin();
        std::vector<LaneQ>::iterator i;
        for (i = lanes.begin(); i != lanes.end(); ++i) {
            SUMOReal nextOccupation = 0;
            for (std::vector<MSLane*>::const_iterator j = (*i).bestContinuations.begin() + 1; j != (*i).bestContinuations.end(); ++j) {
                nextOccupation += (*j)->getVehLenSum();
            }
            (*i).nextOccupation = nextOccupation;
            if ((*i).lane == startLane) {
                myCurrentLaneInBestLanes = i;
            }
        }
        return *myBestLanes.begin();
    }
    // start rebuilding
    myLastBestLanesEdge = &startLane->getEdge();
    myBestLanes.clear();

    // get information about the next stop
    const MSEdge* nextStopEdge = 0;
    const MSLane* nextStopLane = 0;
    SUMOReal nextStopPos = 0;
    if (!myStops.empty()) {
        const Stop& nextStop = myStops.front();
        nextStopLane = nextStop.lane;
        nextStopEdge = &nextStopLane->getEdge();
        nextStopPos = nextStop.startPos;
    }
    if (myParameter->arrivalLaneProcedure == ARRIVAL_LANE_GIVEN && nextStopEdge == 0) {
        nextStopEdge = *(myRoute->end() - 1);
        nextStopLane = nextStopEdge->getLanes()[myParameter->arrivalLane];
        nextStopPos = myArrivalPos;
    }
    if (nextStopEdge != 0) {
        // make sure that the "wrong" lanes get a penalty. (penalty needs to be
        // large enough to overcome a magic threshold in MSLCM_DK2004.cpp:383)
        nextStopPos = MIN2((SUMOReal)nextStopPos, (SUMOReal)(nextStopEdge->getLength() - 2 * POSITION_EPS));
    }

    // go forward along the next lanes;
    int seen = 0;
    SUMOReal seenLength = 0;
    bool progress = true;
    for (MSRouteIterator ce = myCurrEdge; progress;) {
        std::vector<LaneQ> currentLanes;
        const std::vector<MSLane*>* allowed = 0;
        const MSEdge* nextEdge = 0;
        if (ce != myRoute->end() && ce + 1 != myRoute->end()) {
            nextEdge = *(ce + 1);
            allowed = (*ce)->allowedLanes(*nextEdge, myType->getVehicleClass());
        }
        const std::vector<MSLane*>& lanes = (*ce)->getLanes();
        for (std::vector<MSLane*>::const_iterator i = lanes.begin(); i != lanes.end(); ++i) {
            LaneQ q;
            MSLane* cl = *i;
            q.lane = cl;
            q.bestContinuations.push_back(cl);
            q.bestLaneOffset = 0;
            q.length = cl->getLength();
            q.allowsContinuation = allowed == 0 || find(allowed->begin(), allowed->end(), cl) != allowed->end();
            currentLanes.push_back(q);
        }
        //
        if (nextStopEdge == *ce) {
            progress = false;
            for (std::vector<LaneQ>::iterator q = currentLanes.begin(); q != currentLanes.end(); ++q) {
                if (nextStopLane != 0 && nextStopLane != (*q).lane) {
                    (*q).allowsContinuation = false;
                    (*q).length = nextStopPos;
                }
            }
        }

        myBestLanes.push_back(currentLanes);
        ++seen;
        seenLength += currentLanes[0].lane->getLength();
        ++ce;
        progress &= (seen <= 4 || seenLength < 3000);
        progress &= seen <= 8;
        progress &= ce != myRoute->end();
        /*
        if(progress) {
        	progress &= (currentLanes.size()!=1||(*ce)->getLanes().size()!=1);
        }
        */
    }

    // we are examining the last lane explicitly
    if (myBestLanes.size() != 0) {
        SUMOReal bestLength = -1;
        int bestThisIndex = 0;
        int index = 0;
        std::vector<LaneQ>& last = myBestLanes.back();
        for (std::vector<LaneQ>::iterator j = last.begin(); j != last.end(); ++j, ++index) {
            if ((*j).length > bestLength) {
                bestLength = (*j).length;
                bestThisIndex = index;
            }
        }
        index = 0;
        for (std::vector<LaneQ>::iterator j = last.begin(); j != last.end(); ++j, ++index) {
            if ((*j).length < bestLength) {
                (*j).bestLaneOffset = bestThisIndex - index;
            }
        }
    }

    // go backward through the lanes
    // track back best lane and compute the best prior lane(s)
    for (std::vector<std::vector<LaneQ> >::reverse_iterator i = myBestLanes.rbegin() + 1; i != myBestLanes.rend(); ++i) {
        std::vector<LaneQ>& nextLanes = (*(i - 1));
        std::vector<LaneQ>& clanes = (*i);
        MSEdge& cE = clanes[0].lane->getEdge();
        int index = 0;
        SUMOReal bestConnectedLength = -1;
        SUMOReal bestLength = -1;
        for (std::vector<LaneQ>::iterator j = nextLanes.begin(); j != nextLanes.end(); ++j, ++index) {
            if ((*j).lane->isApproachedFrom(&cE) && bestConnectedLength < (*j).length) {
                bestConnectedLength = (*j).length;
            }
            if (bestLength < (*j).length) {
                bestLength = (*j).length;
            }
        }
        if (bestConnectedLength > 0) {
            int bestThisIndex = 0;
            index = 0;
            for (std::vector<LaneQ>::iterator j = clanes.begin(); j != clanes.end(); ++j, ++index) {
                LaneQ bestConnectedNext;
                bestConnectedNext.length = -1;
                if ((*j).allowsContinuation) {
                    for (std::vector<LaneQ>::const_iterator m = nextLanes.begin(); m != nextLanes.end(); ++m) {
                        if ((*m).lane->isApproachedFrom(&cE, (*j).lane)) {
                            if (bestConnectedNext.length < (*m).length || (bestConnectedNext.length == (*m).length && abs(bestConnectedNext.bestLaneOffset) > abs((*m).bestLaneOffset))) {
                                bestConnectedNext = *m;
                            }
                        }
                    }
                    if (bestConnectedNext.length == bestConnectedLength && abs(bestConnectedNext.bestLaneOffset) < 2) {
                        (*j).length += bestLength;
                    } else {
                        (*j).length += bestConnectedNext.length;
                    }
                }
                if (clanes[bestThisIndex].length < (*j).length || (clanes[bestThisIndex].length == (*j).length && abs(abs(clanes[bestThisIndex].bestLaneOffset > (*j).bestLaneOffset)))) {
                    bestThisIndex = index;
                }
                copy(bestConnectedNext.bestContinuations.begin(), bestConnectedNext.bestContinuations.end(), back_inserter((*j).bestContinuations));
            }

            index = 0;
            for (std::vector<LaneQ>::iterator j = clanes.begin(); j != clanes.end(); ++j, ++index) {
                if ((*j).length < clanes[bestThisIndex].length || ((*j).length == clanes[bestThisIndex].length && abs((*j).bestLaneOffset) < abs(clanes[bestThisIndex].bestLaneOffset))) {
                    (*j).bestLaneOffset = bestThisIndex - index;
                } else {
                    (*j).bestLaneOffset = 0;
                }
            }

        } else {

            int bestThisIndex = 0;
            int bestNextIndex = 0;
            int bestDistToNeeded = (int) clanes.size();
            index = 0;
            for (std::vector<LaneQ>::iterator j = clanes.begin(); j != clanes.end(); ++j, ++index) {
                if ((*j).allowsContinuation) {
                    int nextIndex = 0;
                    for (std::vector<LaneQ>::const_iterator m = nextLanes.begin(); m != nextLanes.end(); ++m, ++nextIndex) {
                        if ((*m).lane->isApproachedFrom(&cE, (*j).lane)) {
                            if (bestDistToNeeded > abs((*m).bestLaneOffset)) {
                                bestDistToNeeded = abs((*m).bestLaneOffset);
                                bestThisIndex = index;
                                bestNextIndex = nextIndex;
                            }
                        }
                    }
                }
            }
            clanes[bestThisIndex].length += nextLanes[bestNextIndex].length;
            copy(nextLanes[bestNextIndex].bestContinuations.begin(), nextLanes[bestNextIndex].bestContinuations.end(), back_inserter(clanes[bestThisIndex].bestContinuations));
            index = 0;
            for (std::vector<LaneQ>::iterator j = clanes.begin(); j != clanes.end(); ++j, ++index) {
                if ((*j).length < clanes[bestThisIndex].length || ((*j).length == clanes[bestThisIndex].length && abs((*j).bestLaneOffset) < abs(clanes[bestThisIndex].bestLaneOffset))) {
                    (*j).bestLaneOffset = bestThisIndex - index;
                } else {
                    (*j).bestLaneOffset = 0;
                }
            }

        }

    }

    // update occupancy and current lane index
    std::vector<LaneQ>& currLanes = *myBestLanes.begin();
    std::vector<LaneQ>::iterator i;
    for (i = currLanes.begin(); i != currLanes.end(); ++i) {
        SUMOReal nextOccupation = 0;
        for (std::vector<MSLane*>::const_iterator j = (*i).bestContinuations.begin() + 1; j != (*i).bestContinuations.end(); ++j) {
            nextOccupation += (*j)->getVehLenSum();
        }
        (*i).nextOccupation = nextOccupation;
        if ((*i).lane == startLane) {
            myCurrentLaneInBestLanes = i;
        }
    }
    return *myBestLanes.begin();
}


const std::vector<MSLane*>&
MSVehicle::getBestLanesContinuation() const {
    if (myBestLanes.empty() || myBestLanes[0].empty()) {
        return myEmptyLaneVector;
    }
    assert((*myCurrentLaneInBestLanes).lane == myLane 
            || myLane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL);
    return (*myCurrentLaneInBestLanes).bestContinuations;
}


const std::vector<MSLane*>&
MSVehicle::getBestLanesContinuation(const MSLane* const l) const {
    if (myBestLanes.size() == 0) {
        return myEmptyLaneVector;
    }
    for (std::vector<LaneQ>::const_iterator i = myBestLanes[0].begin(); i != myBestLanes[0].end(); ++i) {
        if ((*i).lane == l) {
            return (*i).bestContinuations;
        }
    }
    return myEmptyLaneVector;
}



SUMOReal
MSVehicle::getDistanceToPosition(SUMOReal destPos, const MSEdge* destEdge) {
#ifdef DEBUG_VEHICLE_GUI_SELECTION
    SUMOReal distance = 1000000.;
#else
    SUMOReal distance = std::numeric_limits<SUMOReal>::max();
#endif
    if (isOnRoad() && destEdge != NULL) {
        if (&myLane->getEdge() == *myCurrEdge) {
            // vehicle is on a normal edge
            distance = myRoute->getDistanceBetween(getPositionOnLane(), destPos, *myCurrEdge, destEdge);
        } else {
            // vehicle is on inner junction edge
            distance = myLane->getLength() - getPositionOnLane();
            distance += myRoute->getDistanceBetween(0, destPos, *(myCurrEdge + 1), destEdge);
        }
    }
    return distance;
}


SUMOReal
MSVehicle::getHBEFA_CO2Emissions() const {
    return HelpersHBEFA::computeCO2(myType->getEmissionClass(), myState.speed(), myAcceleration);
}


SUMOReal
MSVehicle::getHBEFA_COEmissions() const {
    return HelpersHBEFA::computeCO(myType->getEmissionClass(), myState.speed(), myAcceleration);
}


SUMOReal
MSVehicle::getHBEFA_HCEmissions() const {
    return HelpersHBEFA::computeHC(myType->getEmissionClass(), myState.speed(), myAcceleration);
}


SUMOReal
MSVehicle::getHBEFA_NOxEmissions() const {
    return HelpersHBEFA::computeNOx(myType->getEmissionClass(), myState.speed(), myAcceleration);
}


SUMOReal
MSVehicle::getHBEFA_PMxEmissions() const {
    return HelpersHBEFA::computePMx(myType->getEmissionClass(), myState.speed(), myAcceleration);
}


SUMOReal
MSVehicle::getHBEFA_FuelConsumption() const {
    return HelpersHBEFA::computeFuel(myType->getEmissionClass(), myState.speed(), myAcceleration);
}


SUMOReal
MSVehicle::getHarmonoise_NoiseEmissions() const {
    return HelpersHarmonoise::computeNoise(myType->getEmissionClass(), myState.speed(), myAcceleration);
}


void
MSVehicle::addPerson(MSPerson* person) {
    if (myPersonDevice == 0) {
        myPersonDevice = MSDevice_Person::buildVehicleDevices(*this, myDevices);
        myMoveReminders.push_back(std::make_pair(myPersonDevice, 0.));
    }
    myPersonDevice->addPerson(person);
    if (myStops.size() > 0 && myStops.front().reached && myStops.front().triggered) {
        myStops.front().duration = 0;
    }
}


unsigned int
MSVehicle::getPersonNumber() const {
    unsigned int boarded = myPersonDevice == 0 ? 0 : myPersonDevice->size();
    return boarded + myParameter->personNumber;
}


void
MSVehicle::setBlinkerInformation() {
    switchOffSignal(VEH_SIGNAL_BLINKER_RIGHT | VEH_SIGNAL_BLINKER_LEFT);
    int state = getLaneChangeModel().getOwnState();
    if ((state & LCA_LEFT) != 0) {
        switchOnSignal(VEH_SIGNAL_BLINKER_LEFT);
    } else if ((state & LCA_RIGHT) != 0) {
        switchOnSignal(VEH_SIGNAL_BLINKER_RIGHT);
    } else {
        const MSLane* lane = getLane();
        MSLinkCont::const_iterator link = lane->succLinkSec(*this, 1, *lane, getBestLanesContinuation());
        if (link != lane->getLinkCont().end() && lane->getLength() - getPositionOnLane() < lane->getVehicleMaxSpeed(this) * (SUMOReal) 7.) {
            switch ((*link)->getDirection()) {
                case LINKDIR_TURN:
                case LINKDIR_LEFT:
                case LINKDIR_PARTLEFT:
                    switchOnSignal(VEH_SIGNAL_BLINKER_LEFT);
                    break;
                case LINKDIR_RIGHT:
                case LINKDIR_PARTRIGHT:
                    switchOnSignal(VEH_SIGNAL_BLINKER_RIGHT);
                    break;
                default:
                    break;
            }
        }
    }

}


void
MSVehicle::replaceVehicleType(MSVehicleType* type) {
    if (myType->amVehicleSpecific()) {
        delete myType;
    }
    myType = type;
}

unsigned int
MSVehicle::getLaneIndex() const {
    std::vector<MSLane*>::const_iterator laneP = std::find((*myCurrEdge)->getLanes().begin(), (*myCurrEdge)->getLanes().end(), myLane);
    return (unsigned int) std::distance((*myCurrEdge)->getLanes().begin(), laneP);
}


bool 
MSVehicle::startLaneChangeManeuver(MSLane* source, MSLane* target, int direction) {
    target->enteredByLaneChange(this);
    if (MSGlobals::gLaneChangeDuration > DELTA_T) {
        //std::cout << getID() << " started continuous lane change\n";
        myLaneChangeCompletion = 0;
        myShadowLane = target;
        myLaneChangeMidpointPassed = false;
        myLaneChangeDirection = direction;
        continueLaneChangeManeuver(false);
        return true;
    } else {
        //std::cout << getID() << " performed instant lane change\n";
        leaveLane(MSMoveReminder::NOTIFICATION_LANE_CHANGE);
        source->leftByLaneChange(this);
        enterLaneAtLaneChange(target);
        myLastLaneChangeOffset = 0;
        getLaneChangeModel().changed();
        return false;
    }
}


void 
MSVehicle::continueLaneChangeManeuver(bool moved) {
    if (moved) {
        // move shadow to next lane
        removeLaneChangeShadow();
        const int shadowDirection = myLaneChangeMidpointPassed ? -myLaneChangeDirection : myLaneChangeDirection;
        MSLane* shadowCandidate = myLane->getParallelLane(shadowDirection);
        if (shadowCandidate == 0) {
            WRITE_WARNING("Vehicle '" + getID() + "' could not finish continuous lane change time=" + 
                    time2string(MSNet::getInstance()->getCurrentTimeStep()) + ".");
            // abort lane change
            myLaneChangeCompletion = 1;
            return;
        } else {
            myShadowLane = shadowCandidate;
        }
    }
    myLaneChangeCompletion = MIN2(SUMOReal(1), myLaneChangeCompletion + (SUMOReal)DELTA_T / (SUMOReal)MSGlobals::gLaneChangeDuration);
    //std::cout << getID() << " continues lane change (completion=" << myLaneChangeCompletion << ")\n";
    if (myLaneChangeCompletion >= 0.5 and !myLaneChangeMidpointPassed) {
        //std::cout << "     midpoint reached\n";
        // maneuver midpoint reached, swap myLane and myShadowLane
        myLaneChangeMidpointPassed = true;
        MSLane* tmp = myLane;
        //std::cout << "before myLane=" << myLane->getID() << " myCurrentLaneInBestLanes=" << (*myCurrentLaneInBestLanes).lane->getID() << "\n";
        leaveLane(MSMoveReminder::NOTIFICATION_LANE_CHANGE);
        enterLaneAtLaneChange(myShadowLane);
        myShadowLane = tmp;
        //std::cout << "after enterLaneAtLaneChange myCurrentLaneInBestLanes=" << (*myCurrentLaneInBestLanes).lane->getID() << "\n";
        if (myLane->getEdge().getPurpose() == MSEdge::EDGEFUNCTION_INTERNAL) {
            // internal lanes do not appear in bestLanes so we need to update
            // myCurrentLaneInBestLanes explicityly
            getBestLanes(false, myLane->getLogicalPredecessorLane());
            //std::cout << "after manual update myCurrentLaneInBestLanes=" << (*myCurrentLaneInBestLanes).lane->getID() << "\n";
        }
        //std::cout << "after leaveLane myCurrentLaneInBestLanes=" << (*myCurrentLaneInBestLanes).lane->getID() << "\n";
        myLastLaneChangeOffset = 0;
        getLaneChangeModel().changed();
    } else if (!isChangingLanes()) {
        //std::cout << "     finished\n";
        assert(myLaneChangeMidpointPassed);
        // @todo leave the source lane as soon as the vehicle no longer intersects it
        //       physically (considering lane width and vehicle width)
        myShadowLane->removeVehicle(this, MSMoveReminder::NOTIFICATION_LANE_CHANGE);
        myShadowLane = 0;
    }
}


void 
MSVehicle::removeLaneChangeShadow() {
    if (isChangingLanes()) {
        //std::cout << "removing " << getID() << " shadow from " << getLaneChangeOtherLane(previousLane)->getID() << "\n";
        myShadowLane->removeVehicle(this, MSMoveReminder::NOTIFICATION_LANE_CHANGE);
        myShadowLane = 0;
    }
}


#ifndef NO_TRACI
bool
MSVehicle::addTraciStop(MSLane* lane, SUMOReal pos, SUMOReal /*radius*/, SUMOTime duration) {
    //if the stop exists update the duration
    for (std::list<Stop>::iterator iter = myStops.begin(); iter != myStops.end(); iter++) {
        if (iter->lane == lane && fabs(iter->endPos - pos) < POSITION_EPS) {
            if (duration == 0 && !iter->reached) {
                myStops.erase(iter);
            } else {
                iter->duration = duration;
            }
            return true;
        }
    }

    SUMOVehicleParameter::Stop newStop;
    newStop.lane = lane->getID();
    newStop.busstop = MSNet::getInstance()->getBusStopID(lane, pos);
    newStop.startPos = pos - POSITION_EPS;
    newStop.endPos = pos;
    newStop.duration = duration;
    newStop.until = -1;
    newStop.triggered = false;
    newStop.parking = false;
    newStop.index = STOP_INDEX_END;
    return addStop(newStop);
}


MSVehicle::Influencer&
MSVehicle::getInfluencer() {
    if (myInfluencer == 0) {
        myInfluencer = new Influencer();
    }
    return *myInfluencer;
}


SUMOReal
MSVehicle::getSpeedWithoutTraciInfluence() const {
    if (myInfluencer != 0) {
        return myInfluencer->getOriginalSpeed();
    }
    return myState.mySpeed;
}


#endif


/****************************************************************************/
