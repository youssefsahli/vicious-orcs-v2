/*
 * Licensed under BSD license.  See LICENCE.TXT  
 *
 * Produced by:	Jeff Lait
 *
 *      	Jacob's Matrix Development
 *
 * NAME:        engine.cpp ( Jacob's Matrix, C++ )
 *
 * COMMENTS:
 *	Our game engine.  Grabs commands from its command
 *	queue and processes them as fast as possible.  Provides
 *	a mutexed method to get a recent map from other threads.
 *	Note that this will run on its own thread!
 */

#include <libtcod.hpp>

#include "engine.h"

#include "msg.h"
#include "rand.h"
#include "map.h"
#include "mob.h"
#include "speed.h"
#include "item.h"
#include "text.h"
#include <time.h>

// #define DO_TIMING

static void *
threadstarter(void *vdata)
{
    ENGINE	*engine = (ENGINE *) vdata;

    engine->mainLoop();

    return 0;
}

ENGINE::ENGINE(DISPLAY *display)
{
    myMap = myOldMap = 0;
    myDisplay = display;

    myThread = THREAD::alloc();
    myThread->start(threadstarter, this);
}

ENGINE::~ENGINE()
{
    if (myMap)
	myMap->decRef();
    if (myOldMap)
	myOldMap->decRef();
}

MAP *
ENGINE::copyMap()
{
    AUTOLOCK	a(myMapLock);

    if (myOldMap)
	myOldMap->incRef();

    return myOldMap;
}

void
ENGINE::updateMap()
{
    {
	AUTOLOCK	a(myMapLock);

	if (myOldMap)
	    myOldMap->decRef();

	myOldMap = myMap;
    }
    if (myOldMap)
    {
#ifdef DO_TIMING
    int 		movestart = TCOD_sys_elapsed_milli();
#endif
	myMap = new MAP(*myOldMap);
	myMap->incRef();
#ifdef DO_TIMING
    int 		moveend = TCOD_sys_elapsed_milli();
    cerr << "Map update " << moveend - movestart << endl;
#endif
    }
}

#define VERIFY_ALIVE() \
    if (avatar && !avatar->alive()) \
    {			\
	msg_report("Dead people can't move.  ");	\
	break;			\
    }

void redrawWorld();

bool
ENGINE::awaitRebuild()
{
    int		value = 0;

    while (!myRebuildQueue.remove(value))
    {
	redrawWorld();
    }

    myRebuildQueue.clear();

    return value ? true : false;
}

void
ENGINE::awaitSave()
{
    int		value;

    while (!mySaveQueue.remove(value))
    {
	redrawWorld();
    }

    mySaveQueue.clear();
}

BUF
ENGINE::getNextPopup()
{
    BUF		result;

    myPopupQueue.remove(result);

    return result;
}

void
ENGINE::popupText(const char *text)
{
    BUF		buf;
    
    buf.strcpy(text);

    myPopupQueue.append(buf);
}

MOB_NAMES
ENGINE::getNextShopper()
{
    MOB_NAMES	shopper = MOB_NONE;

    myShopQueue.remove(shopper);

    return shopper;
}

void
ENGINE::shopRequest(MOB_NAMES mob)
{
    myShopQueue.append(mob);
}

void
ENGINE::mainLoop()
{
    rand_setseed((long) time(0));

    COMMAND		cmd;
    MOB			*avatar;
    bool		timeused = false;
    bool		doheartbeat = true;

    while (1)
    {
	avatar = 0;
	if (myMap)
	    avatar = myMap->avatar();

	timeused = false;
	if (doheartbeat && avatar && avatar->aiForcedAction())
	{
	    cmd = COMMAND(ACTION_NONE);
	    timeused = true;
	}
	else
	{
	    if (doheartbeat)
		msg_newturn();

	    // Allow the other thread a chance to redraw.
	    // Possible we might want a delay here?
	    updateMap();
	    avatar = 0;
	    if (myMap)
		avatar = myMap->avatar();
	    cmd = queue().waitAndRemove();

	    doheartbeat = false;
	}

	switch (cmd.action())
	{
	    case ACTION_WAIT:
		// The dead are allowed to wait!
		timeused = true;
		break;

	    case ACTION_RESTART:
	    {
		int		loaded = 1;
		if (myMap)
		    myMap->decRef();

		myMap = MAP::load();

		if (!myMap)
		{
		    loaded = 0;
		    ITEM::initSystem();
		    avatar = MOB::create(MOB_AVATAR);
		    glbDelusion = 0;
		    glbWasDeluded = 0;
		    glbFinalPax = false;
		    myMap = new MAP(cmd.dx(), avatar, myDisplay);
		}

#if 0
		// Force lots of loads
		while (1)
		{
		    MAP		*temp;
		    avatar = MOB::create(MOB_AVATAR);
		    temp = new MAP(cmd.dx(), avatar, myDisplay);
		    temp->incRef();
		    temp->decRef();
		}
#endif

		myMap->incRef();
		myMap->setDisplay(myDisplay);
		myMap->rebuildFOV();
		myMap->cacheItemPos();

		// Flag we've rebuilt.
		myRebuildQueue.append(loaded);
		break;
	    }

	    case ACTION_SAVE:
	    {
		// Only save good games
		if (myMap && avatar && avatar->alive())
		{
		    myMap->save();
		}
		mySaveQueue.append(0);
		break;
	    }

	    case ACTION_REBOOTAVATAR:
	    {
		if (avatar)
		    avatar->gainHP(avatar->defn().max_hp);
		break;
	    }

	    case ACTION_BUMP:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionBump(cmd.dx(), cmd.dy());
		if (!timeused)
		    msg_newturn();

		break;
	    }
	
	    case ACTION_DROP:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionDrop(avatar->getItemFromNo(cmd.dx()));
		if (!timeused)
		    msg_newturn();
		break;
	    }
	
	    case ACTION_BREAK:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionBreak(avatar->getItemFromNo(cmd.dx()));
		if (!timeused)
		    msg_newturn();
		break;
	    }
	
	    case ACTION_WEAR:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionWear(avatar->getItemFromNo(cmd.dx()));
		if (!timeused)
		    msg_newturn();
		break;
	    }
	
	    case ACTION_EAT:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionEat(avatar->getItemFromNo(cmd.dx()));
		if (!timeused)
		    msg_newturn();
		break;
	    }
	
	    case ACTION_MEDITATE:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionMeditate();
		if (!timeused)
		    msg_newturn();
		break;
	    }
	
	    case ACTION_SEARCH:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionSearch();
		if (!timeused)
		    msg_newturn();
		break;
	    }

	    case ACTION_CREATEITEM:
	    {
		VERIFY_ALIVE()
		if (avatar)
		{
		    ITEM	*item = ITEM::create((ITEM_NAMES) cmd.dx());
		    avatar->addItem(item);
		}
		break;
	    }
	
	    case ACTION_QUAFF:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionQuaff(avatar->getItemFromNo(cmd.dx()));
		if (!timeused)
		    msg_newturn();
		break;
	    }

	    case ACTION_THROW:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionThrow(avatar->getItemFromNo(cmd.dz()),
					cmd.dx(), cmd.dy());
		if (!timeused)
		    msg_newturn();
		break;
	    }

	    case ACTION_CAST:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionCast((SPELL_NAMES)cmd.dz(),
					cmd.dx(), cmd.dy());
		if (!timeused)
		    msg_newturn();
		break;
	    }
	
	    case ACTION_SHOP:
	    {
		VERIFY_ALIVE()
		if (avatar)
		    timeused = avatar->actionShop((SHOP_NAMES)cmd.dx(),
					cmd.dy(), cmd.dz());
		if (!timeused)
		    msg_newturn();
		break;
	    }
	
	    case ACTION_ROTATE:
	    {
		if (avatar)
		    timeused = avatar->actionRotate(cmd.dx());
		break;
	    }

	    case ACTION_FIRE:
	    {
		VERIFY_ALIVE()
		if (avatar)
		{
		    timeused = avatar->actionFire(cmd.dx(), cmd.dy());
		}
		if (!timeused)
		    msg_newturn();
		break;
	    }

	    case ACTION_SUICIDE:
	    {
		if (avatar)
		{
		    if (avatar->alive())
		    {
			msg_report("Your time has run out!  ");
			// We want the flame to die.
			avatar->gainHP(-avatar->getHP());
		    }
		}
		break;
	    }
	}

	if (myMap && timeused)
	{
	    if (cmd.action() != ACTION_SEARCH &&
		cmd.action() != ACTION_NONE)
	    {
		// Depower searching
		if (avatar)
		    avatar->setSearchPower(0);
	    }

	    // Check for timeout
	    if (avatar && avatar->alive())
	    {
		if (glbDelusion && !myMap->getNumOrcs())
		{
		    popupText(text_lookup("game", "killorc"));
		    fadeFromWhite();
		    glbDelusion = 0;
		}
	    }

	    // We need to build the FOV for the monsters as they
	    // rely on the updated FOV to track, etc.
	    // Rebuild the FOV map
	    // Don't do this if no avatar, or the avatar is dead,
	    // as we want the old fov.
	    if (avatar && avatar->alive())
		myMap->rebuildFOV();

	    // Update the world.
	    myMap->doMoveNPC();
	    spd_inctime();

	    // Rebuild the FOV map
	    myMap->rebuildFOV();

	    doheartbeat = true;
	}

	// Allow the other thread a chance to redraw.
	// Possible we might want a delay here?
	updateMap();
    }
}
