/***************************************************************************
                          NIVissimSingleTypeParser_Lichtsignalanlagendefinition.cpp

                             -------------------
    begin                : Wed, 18 Dec 2002
    copyright            : (C) 2001 by DLR/IVF http://ivf.dlr.de/
    author               : Daniel Krajzewicz
    email                : Daniel.Krajzewicz@dlr.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
namespace
{
    const char rcsid[] =
    "$Id$";
}
// $Log$
// Revision 1.6  2003/08/18 12:39:23  dkrajzew
// missing handling of some vissim3.7-structures added
//
// Revision 1.5  2003/06/18 11:35:30  dkrajzew
// message subsystem changes applied and some further work done; seems to be stable but is not perfect, yet
//
// Revision 1.4  2003/05/20 09:42:37  dkrajzew
// all data types implemented
//
// Revision 1.3  2003/04/09 15:53:22  dkrajzew
// netconvert-changes: further work on Vissim-import, documentation added
//
// Revision 1.2  2003/04/07 12:17:10  dkrajzew
// further work on traffic lights import
//
// Revision 1.1  2003/02/07 11:08:43  dkrajzew
// Vissim import added (preview)
//
//

/* =========================================================================
 * included modules
 * ======================================================================= */
#include <iostream>
#include <utils/convert/TplConvert.h>
#include <utils/common/MsgHandler.h>
#include "../NIVissimLoader.h"
#include "../tempstructs/NIVissimTL.h"
#include "NIVissimSingleTypeParser_Lichtsignalanlagendefinition.h"


/* =========================================================================
 * used namespaces
 * ======================================================================= */
using namespace std;


/* =========================================================================
 * method definitions
 * ======================================================================= */
NIVissimSingleTypeParser_Lichtsignalanlagendefinition::NIVissimSingleTypeParser_Lichtsignalanlagendefinition(NIVissimLoader &parent)
	: NIVissimLoader::VissimSingleTypeParser(parent)
{
}


NIVissimSingleTypeParser_Lichtsignalanlagendefinition::~NIVissimSingleTypeParser_Lichtsignalanlagendefinition()
{
}


bool
NIVissimSingleTypeParser_Lichtsignalanlagendefinition::parse(std::istream &from)
{
    //
	int id;
    from >> id;
    //
    string tag, name;
    tag = myRead(from);
    if(tag=="name") {
        name = readName(from);
        tag = myRead(from);
    }
    // type
    string type;
    type = myRead(from);
    if(type=="festzeit") {
        return parseFixedTime(id, name, from);
    } if(type=="vas") {
        return parseVAS(id, name, from);
    } if(type=="vsplus") {
        return parseRestActuated(id, name, from, type);
    } if(type=="trends") {
        return parseRestActuated(id, name, from, type);
    } if(type=="vap") {
        return parseRestActuated(id, name, from, type);
    } if(type=="tl") {
        return parseRestActuated(id, name, from, type);
    } if(type=="pos") {
        return parseRestActuated(id, name, from, type);
    } if(type=="nema") {
        return parseRestActuated(id, name, from, type);
    } if(type=="extern") {
        return parseRestActuated(id, name, from, type);
    }
    MsgHandler::getErrorInstance()->inform(
        string("Unsupported LSA-Type '") + type + string("' occured."));
    return false;
}


bool
NIVissimSingleTypeParser_Lichtsignalanlagendefinition::parseFixedTime(
        int id, std::string name, std::istream &from)
{
    string type = "festzeit";
    string tag;
    from >> tag;
    //
    double absdur;
    from >> absdur; // type-checking is missing!
    //
    tag = readEndSecure(from);
    double offset = 0;
    if(tag=="versatz") {
        from >> offset; // type-checking is missing!
    }
    if(tag!="szpkonfdatei"&&tag!="DATAEND"&&tag!="progdatei") {
        tag = readEndSecure(from);
        if(tag=="szpkonfdatei"||tag=="progdatei") {
            type = "festzeit_fake";
        }
    }
    return NIVissimTL::dictionary(id, type, name, absdur, offset);
}


bool
NIVissimSingleTypeParser_Lichtsignalanlagendefinition::parseVAS(
        int id, std::string name, std::istream &from)
{
    string tag;
    from >> tag;
    //
    double absdur;
    from >> absdur; // type-checking is missing!
    //
    tag = readEndSecure(from);
    double offset = 0;
    if(tag=="versatz") {
        from >> offset; // type-checking is missing!
    }
    return NIVissimTL::dictionary(id, "vas", name, absdur, offset);
}


bool
NIVissimSingleTypeParser_Lichtsignalanlagendefinition::parseRestActuated(
        int id, std::string name, std::istream &from, const std::string &type)
{
    string tag;
    from >> tag;
    //
    double absdur;
    from >> absdur; // type-checking is missing!
    //
    tag = readEndSecure(from);
    double offset = 0;
    if(tag=="versatz") {
        from >> offset; // type-checking is missing!
    }
    while(tag!="datei") {
        tag = myRead(from);
    }
    return NIVissimTL::dictionary(id, type, name, absdur, offset);
}

