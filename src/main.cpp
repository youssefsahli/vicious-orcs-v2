/*
 * Licensed under BSD license.  See LICENCE.TXT  
 *
 * Produced by:	Jeff Lait
 *
 *      	Jacob's Matrix Development
 *
 * NAME:        main.cpp ( Jacob's Matrix, C++ )
 *
 * COMMENTS:
 */

#include <libtcod.hpp>
#include <stdio.h>
#include <time.h>

//#define USE_AUDIO

#define POPUP_DELAY 1000

#ifdef WIN32
#include <windows.h>
#include <process.h>
#define usleep(x) Sleep((x)*1000)
#pragma comment(lib, "SDL.lib")
#pragma comment(lib, "SDLmain.lib")

// I really hate windows.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#else
#include <unistd.h>
#endif

#include <fstream>
using namespace std;

int		 glbLevelStartMS = -1;

#include "rand.h"
#include "gfxengine.h"
#include "config.h"

#include <SDL.h>
#ifdef USE_AUDIO
#include <SDL_mixer.h>

Mix_Music		*glbTunes = 0;
#endif
bool			 glbMusicActive = false;

// Cannot be bool!
// libtcod does not accept bools properly.

volatile int		glbItemCache = -1;

SPELL_NAMES		glbLastSpell = SPELL_NONE;
int			glbLastItemIdx = 0;
int			glbLastItemAction = 0;

void endOfMusic()
{
    // Keep playing.
    // glbMusicActive = false;
}

void
setMusicVolume(int volume)
{
#ifdef USE_AUDIO
    volume = BOUND(volume, 0, 10);

    glbConfig->myMusicVolume = volume;

    Mix_VolumeMusic((MIX_MAX_VOLUME * volume) / 10);
#endif
}

void
stopMusic(bool atoncedamnit = false)
{
#ifdef USE_AUDIO
    if (glbMusicActive)
    {
	if (atoncedamnit)
	    Mix_HaltMusic();
	else
	    Mix_FadeOutMusic(2000);
	glbMusicActive = false;
    }
#endif
}

void
startMusic()
{
#ifdef USE_AUDIO
    if (glbMusicActive)
	stopMusic();

    if (!glbTunes)
	glbTunes = Mix_LoadMUS(glbConfig->musicFile());
    if (!glbTunes)
    {
	printf("Failed to load %s, error %s\n", 
		glbConfig->musicFile(), 
		Mix_GetError());
    }
    else
    {
	glbMusicActive = true;
	glbLevelStartMS = TCOD_sys_elapsed_milli();

	if (glbConfig->musicEnable())
	{
	    Mix_PlayMusic(glbTunes, -1);		// Infinite
	    Mix_HookMusicFinished(endOfMusic);

	    setMusicVolume(glbConfig->myMusicVolume);
	}
    }
#endif
}


#ifdef main
#undef main
#endif

#include "mob.h"
#include "map.h"
#include "text.h"
#include "speed.h"
#include "display.h"
#include "engine.h"
#include "panel.h"
#include "msg.h"
#include "firefly.h"
#include "item.h"
#include "chooser.h"

PANEL		*glbMessagePanel = 0;
DISPLAY		*glbDisplay = 0;
MAP		*glbMap = 0;
ENGINE		*glbEngine = 0;
PANEL		*glbPopUp = 0;
PANEL		*glbJacob = 0;
PANEL		*glbWeaponInfo = 0;
PANEL		*glbVictoryInfo = 0;
bool		 glbPopUpActive = false;
bool		 glbRoleActive = false;
FIREFLY		*glbHealthFire = 0;
FIREFLY		*glbManaFire = 0;
FIREFLY		*glbCoinStack = 0;
int		 glbLevel = 1;
int		 glbMaxLevel = 0;
PTRLIST<int>	 glbLevelTimes;
bool		 glbVeryFirstRun = true;

CHOOSER		*glbLevelBuildStatus = 0;

CHOOSER		*glbChooser = 0;
bool		 glbChooserActive = false;

int		 glbInvertX = -1, glbInvertY = -1;
int		 glbMeditateX = -1, glbMeditateY = -1;

#define DEATHTIME 15000


BUF
mstotime(int ms)
{
    int		sec, min;
    BUF		buf;

    sec = ms / 1000;
    ms -= sec * 1000;
    min = sec / 60;
    sec -= min * 60;

    if (min)
	buf.sprintf("%dm%d.%03ds", min, sec, ms);
    else if (sec)
	buf.sprintf("%d.%03ds", sec, ms);
    else
	buf.sprintf("0.%03ds", ms);

    return buf;
}

// Should call this in all of our loops.
void
redrawWorld()
{
    static MAP	*centermap = 0;
    static POS	 mapcenter;
    bool	 isblind = false;
    bool	 isvictory = false;

    gfx_updatepulsetime();

    // Wipe the world
    for (int y = 0; y < SCR_HEIGHT; y++)
	for (int x = 0; x < SCR_WIDTH; x++)
	    gfx_printchar(x, y, ' ');

    if (glbMap)
	glbMap->decRef();
    glbMap = glbEngine->copyMap();

    glbWeaponInfo->clear();

    glbMeditateX = -1;
    glbMeditateY = -1;
    int			avataralive = 0;
    if (glbMap && glbMap->avatar())
    {
	MOB		*avatar = glbMap->avatar();
	int		 timems;

	avataralive = avatar->alive();

	if (avatar->alive() && avatar->isMeditating())
	{
	    glbMeditateX = glbDisplay->x() + glbDisplay->width()/2;
	    glbMeditateY = glbDisplay->y() + glbDisplay->height()/2;
	}

	timems = TCOD_sys_elapsed_milli();

	if (centermap)
	    centermap->decRef();
	centermap = glbMap;
	centermap->incRef();
	mapcenter = avatar->meditatePos();

	isblind = avatar->hasItem(ITEM_BLIND);

	if (avatar->hasItem(ITEM_INVULNERABLE))
	    glbHealthFire->setFireType(FIRE_MONO);
	else if (avatar->hasItem(ITEM_POISON))
	    glbHealthFire->setFireType(FIRE_POISON);
	else
	    glbHealthFire->setFireType(FIRE_HEALTH);

	glbHealthFire->setRatioLiving(avatar->getHP() / (float) avatar->defn().max_hp);
	glbManaFire->setRatioLiving(avatar->getMP() / (float) avatar->defn().max_mp);
	{
	    int		ngold = 0;
	    ITEM	*gold = avatar->lookupItem(ITEM_GOLD);
	    if (gold)
		ngold = gold->getStackCount();

	    glbCoinStack->setParticleCount(BOUND(ngold, 0, 20 * 48));
	    glbCoinStack->setRatioLiving((float) (BOUND(ngold, 0, 20*48) / (20*48.)));
	}

	BUF		buf;
	ITEM		*item;

	item = avatar->lookupWeapon();
	glbWeaponInfo->setTextAttr(ATTR_WHITE);
	if (item)
	{
	    glbWeaponInfo->appendText(gram_capitalize(item->getName()));
	    glbWeaponInfo->newLine();
	    glbWeaponInfo->setTextAttr(ATTR_NORMAL);
	    glbWeaponInfo->appendText(item->getDetailedDescription());
	}
	else
	    glbWeaponInfo->appendText("No weapon\n");
	glbWeaponInfo->newLine();

	glbWeaponInfo->setTextAttr(ATTR_WHITE);
	item = avatar->lookupWand();
	if (item)
	{
	    BUF		name, longname;

	    name = gram_capitalize(item->getName());
	    ITEM	*ammo;
	    ammo = avatar->lookupItem((ITEM_NAMES) item->defn().ammo);
	    if (ammo)
	    {
		longname.sprintf("%s [%d]", name.buffer(), ammo->getStackCount());
	    }
	    else
	    {
		longname = name;
	    }
	    glbWeaponInfo->appendText(longname);
	    glbWeaponInfo->newLine();
	    glbWeaponInfo->setTextAttr(ATTR_NORMAL);
	    glbWeaponInfo->appendText(item->getDetailedDescription());
	}
	else
	    glbWeaponInfo->appendText("No ranged weapon\n");
	glbWeaponInfo->newLine();

	glbWeaponInfo->setTextAttr(ATTR_WHITE);
	item = avatar->lookupArmour();
	if (item)
	{
	    glbWeaponInfo->appendText(gram_capitalize(item->getName()));
	    glbWeaponInfo->newLine();
	    glbWeaponInfo->setTextAttr(ATTR_NORMAL);
	    glbWeaponInfo->appendText(item->getDetailedDescription());
	}
	else
	    glbWeaponInfo->appendText("No armour\n");

	glbWeaponInfo->newLine();

	glbWeaponInfo->setTextAttr(ATTR_WHITE);
	item = avatar->lookupRing();
	if (item)
	{
	    glbWeaponInfo->appendText(gram_capitalize(item->getName()));
	    glbWeaponInfo->newLine();
	    if (item->isMagicClassKnown())
	    {
		glbWeaponInfo->setTextAttr(ATTR_NORMAL);
		glbWeaponInfo->appendText(item->getDetailedDescription());
	    }
	}
    }
    
    glbDisplay->display(mapcenter, isblind);
    glbMessagePanel->redraw();

    glbJacob->clear();
    glbJacob->setTextAttr(ATTR_WHITE);
    BUF		buf;

    int		depth = 0;
    if (glbMap && glbMap->avatar())
    {
	MOB	*avatar = glbMap->avatar();

	switch (avatar->pos().roomType())
	{
	    case ROOMTYPE_VILLAGE:
		if (glbDelusion)
		    depth = 7;
		else
		    depth = 0;
		break;
	    case ROOMTYPE_PENULTIMATE:
		depth = 6;
		break;

	    default:
		depth = glb_roomtypedefs[avatar->pos().roomType()].depth;
		break;
	}
    }


    buf.sprintf(" Depth: %d\n\n Orcs: %d", 
		depth,
		glbMap ? glbMap->getNumOrcs() : 0);

    glbJacob->appendText(buf);

    glbJacob->redraw();

    glbWeaponInfo->redraw();

    if (isvictory)
	glbVictoryInfo->redraw();

    {
	FIRETEX		*tex = glbHealthFire->getTex();
	tex->redraw(0, 0);
	tex->decRef();
    }
    {
	FIRETEX		*tex = glbManaFire->getTex();
	tex->redraw(0, 1);
	tex->decRef();
    }
    {
	FIRETEX		*tex = glbCoinStack->getTex();
	tex->redraw(0, 2);
	tex->decRef();
    }
    {
	// we have to clear the RHS.
	static FIRETEX		 *cleartex = 0;
	if (!cleartex)
	{
	    cleartex = new FIRETEX(20, 48);
	    cleartex->buildFromConstant(false, 0, FIRE_HEALTH);
	}
	cleartex->redraw(60, 2);
    }

    if (glbMeditateX >= 0 && glbMeditateY >= 0)
    {
	gfx_printattrback(glbMeditateX, glbMeditateY, ATTR_AVATARMEDITATE);
    }

    if (glbInvertX >= 0 && glbInvertY >= 0)
    {
	gfx_printattr(glbInvertX, glbInvertY, ATTR_HILITE);
    }


    if (glbItemCache >= 0)
    {
	glbLevelBuildStatus->clear();

	glbLevelBuildStatus->appendChoice("   Building Distance Caches   ", glbItemCache);
	glbLevelBuildStatus->redraw();
    }

    if (glbChooserActive)
    {
	glbChooser->redraw();
    }

    if (glbPopUpActive)
	glbPopUp->redraw();

    TCODConsole::flush();
}

// Pops up the given text onto the screen.
// The key used to dismiss the text is the return code.
int
popupText(const char *buf, int delayms = 0)
{
    int		key = 0;
    int		startms = TCOD_sys_elapsed_milli();

    glbPopUp->clear();
    glbPopUp->resize(50, 48);
    glbPopUp->move(15, 1);

    // A header matches the footer.
    glbPopUp->appendText("\n");

    // Must make active first because append text may trigger a 
    // redraw if it is long!
    glbPopUpActive = true;
    glbPopUp->appendText(buf);

    // In case we drew due to too long text
    glbPopUp->erase();

    // Determine the size of the popup and resize.
    glbPopUp->shrinkToFit();

    while (!TCODConsole::isWindowClosed())
    {
	redrawWorld();
	key = gfx_getKey(false);
	if (delayms)
	{
	    int		currentms = (int)TCOD_sys_elapsed_milli() - startms;
	    float	ratio = currentms / (float)delayms;
	    ratio = BOUND(ratio, 0, 1);

	    glbPopUp->fillRect(0, glbPopUp->height()-1, (int)(glbPopUp->width()*ratio+0.5), 1, ATTR_WAITBAR);
	}
	if (key)
	{
	    // Don't let them abort too soon.
	    if (((int)TCOD_sys_elapsed_milli() - startms) >= delayms)
		break;
	}
    }

    glbPopUpActive = false;
    glbPopUp->erase();

    glbPopUp->resize(50, 30);
    glbPopUp->move(15, 10);

    redrawWorld();

    return key;
}

int
popupText(BUF buf, int delayms = 0)
{
    return popupText(buf.buffer(), delayms);
}

//
// Freezes the game until a key is pressed.
// If key is not direction, false returned and it is eaten.
// else true returned with dx/dy set.  Can be 0,0.
bool
awaitDirection(int &dx, int &dy)
{
    int			key;

    while (!TCODConsole::isWindowClosed())
    {
	redrawWorld();

	key = gfx_getKey(false);

	if (gfx_cookDir(key, dx, dy))
	{
	    return true;
	}
	if (key)
	    return false;
    }

    return false;
}

void
doExamine()
{
    int		x = glbDisplay->x() + glbDisplay->width()/2;
    int		y = glbDisplay->y() + glbDisplay->height()/2;
    int		key;
    int		dx, dy;
    POS		p;
    BUF		buf;
    bool	blind;

    glbInvertX = x;
    glbInvertY = y;
    blind = false;
    if (glbMap && glbMap->avatar())
	blind = glbMap->avatar()->hasItem(ITEM_BLIND);
    while (!TCODConsole::isWindowClosed())
    {
	redrawWorld();

	key = gfx_getKey(false);

	if (gfx_cookDir(key, dx, dy))
	{
	    // Space we want to select.
	    if (!dx && !dy)
		break;
	    x += dx;
	    y += dy;
	    x = BOUND(x, glbDisplay->x(), glbDisplay->x()+glbDisplay->width()-1);
	    y = BOUND(y, glbDisplay->y(), glbDisplay->y()+glbDisplay->height()-1);

	    glbInvertX = x;
	    glbInvertY = y;
	    p = glbDisplay->lookup(x, y);

	    if (p.tile() != TILE_INVALID && p.isFOV())
	    {
		p.describeSquare(blind);
	    }

	    msg_newturn();
	}

	// Any other key stops the look.
	if (key)
	    break;
    }

    // Check if we left on a mob.
    // Must re look up as displayWorld may have updated the map.
    p = glbDisplay->lookup(x, y);
    if (p.mob())
    {
	popupText(p.mob()->getLongDescription());
    }
    else if (p.item())
    {
	popupText(p.item()->getLongDescription());
    }

    glbInvertX = -1;
    glbInvertY = -1;
}

bool
doThrow(int itemno)
{
    int			dx, dy;

    msg_report("Throw in which direction?  ");
    if (awaitDirection(dx, dy) && (dx || dy))
    {
	msg_report(rand_dirtoname(dx, dy));
	msg_newturn();
    }
    else
    {
	msg_report("Cancelled.  ");
	msg_newturn();
	return false;
    }

    // Do the throw
    glbEngine->queue().append(COMMAND(ACTION_THROW, dx, dy, itemno));

    return true;
}

void
buildAction(ITEM *item, ACTION_NAMES *actions)
{
    glbChooser->clear();

    glbChooser->setTextAttr(ATTR_NORMAL);
    int			choice = 0;
    ACTION_NAMES	*origaction = actions;
    if (!item)
    {
	glbChooser->appendChoice("Nothing to do with nothing.");
	*actions++ = ACTION_NONE;
    }
    else
    {
	glbChooser->appendChoice("[ ] Do nothing");
	if (glbLastItemAction == ACTION_NONE)
	    choice = (int)(actions - origaction);
	*actions++ = ACTION_NONE;

	glbChooser->appendChoice("[X] Examine");
	if (glbLastItemAction == ACTION_EXAMINE)
	    choice = (int)(actions - origaction);
	*actions++ = ACTION_EXAMINE;

	if (item->isRanged())
	{
	    glbChooser->appendChoice("[B] Break");
	    if (glbLastItemAction == ACTION_BREAK)
		choice = (int)(actions - origaction);
	    *actions++ = ACTION_BREAK;
	}
	if (item->isPotion())
	{
	    glbChooser->appendChoice("[Q] Quaff");
	    if (glbLastItemAction == ACTION_QUAFF)
		choice = (int)(actions - origaction);
	    *actions++ = ACTION_QUAFF;
	    glbChooser->appendChoice("[T] Throw");
	    if (glbLastItemAction == ACTION_THROW)
		choice = (int)(actions - origaction);
	    *actions++ = ACTION_THROW;
	}
	if (item->isFood())
	{
	    glbChooser->appendChoice("[E] Eat");
	    if (glbLastItemAction == ACTION_EAT)
		choice = (int)(actions - origaction);
	    *actions++ = ACTION_EAT;
	}
	if (item->isRing())
	{
	    glbChooser->appendChoice("[W] Wear");
	    if (glbLastItemAction == ACTION_WEAR)
		choice = (int)(actions - origaction);
	    *actions++ = ACTION_WEAR;
	}
	if (!item->defn().isflag)
	{
	    glbChooser->appendChoice("[D] Drop");
	    if (glbLastItemAction == ACTION_DROP)
		choice = (int)(actions - origaction);
	    *actions++ = ACTION_DROP;
	}
    }
    glbChooser->setChoice(choice);

    glbChooserActive = true;
}

bool
useItem(MOB *mob, ITEM *item, int itemno)
{
    ACTION_NAMES		actions[NUM_ACTIONS+1];
    bool			done = false;
    int				key;

    buildAction(item, actions);
    glbChooserActive = true;

    // Run the chooser...
    while (!TCODConsole::isWindowClosed())
    {
	redrawWorld();
	key = gfx_getKey(false);

	glbChooser->processKey(key);

	if (key)
	{
	    if (key == 'd' || key == 'D')
	    {
		glbEngine->queue().append(COMMAND(ACTION_DROP, itemno));
		done = true;
		break;
	    }
	    else if (key == 'q' || key == 'Q')
	    {
		glbEngine->queue().append(COMMAND(ACTION_QUAFF, itemno));
		done = true;
		break;
	    }
	    else if (key == 'e' || key == 'E')
	    {
		glbEngine->queue().append(COMMAND(ACTION_EAT, itemno));
		done = true;
		break;
	    }
	    else if (key == 'b' || key == 'B')
	    {
		glbEngine->queue().append(COMMAND(ACTION_BREAK, itemno));
		done = true;
		break;
	    }
	    else if (key == 'w' || key == 'W')
	    {
		glbEngine->queue().append(COMMAND(ACTION_WEAR, itemno));
		done = true;
		break;
	    }
	    else if (key == 'x' || key == 'X')
	    {
		popupText(item->getLongDescription());
		done = false;
		break;
	    }
	    else if (key == 't' || key == 'T')
	    {
		glbChooserActive = false;
		doThrow(itemno);
		done = true;
		break;
	    }
	    // User selects this?
	    else if (key == '\n' || key == ' ')
	    {
		ACTION_NAMES	action;

		action = actions[glbChooser->getChoice()];
		glbLastItemAction = action;
		switch (action)
		{
		    case ACTION_DROP:
			glbEngine->queue().append(COMMAND(ACTION_DROP, itemno));
			done = true;
			break;
		    case ACTION_QUAFF:
			glbEngine->queue().append(COMMAND(ACTION_QUAFF, itemno));
			done = true;
			break;
		    case ACTION_BREAK:
			glbEngine->queue().append(COMMAND(ACTION_BREAK, itemno));
			done = true;
			break;
		    case ACTION_WEAR:
			glbEngine->queue().append(COMMAND(ACTION_WEAR, itemno));
			done = true;
			break;
		    case ACTION_EAT:
			glbEngine->queue().append(COMMAND(ACTION_EAT, itemno));
			done = true;
			break;
		    case ACTION_SEARCH:
			glbEngine->queue().append(COMMAND(ACTION_SEARCH, itemno));
			done = true;
			break;
		    case ACTION_MEDITATE:
			glbEngine->queue().append(COMMAND(ACTION_MEDITATE, itemno));
			done = true;
			break;
		    case ACTION_EXAMINE:
			popupText(item->getLongDescription());
			done = false;
			break;
		    case ACTION_THROW:
			glbChooserActive = false;
			doThrow(itemno);
			done = true;
			break;
		}
		break;
	    }
	    else if (key == '\x1b')
	    {
		// Esc on options is to go back to play.
		// Play...
		break;
	    }
	    else
	    {
		// Ignore other options.
	    }
	}
    }
    glbChooserActive = false;

    return done;
}

void
buildInventory(MOB *mob)
{
    glbChooser->clear();

    glbChooser->setTextAttr(ATTR_NORMAL);
    if (!mob)
    {
	glbChooser->appendChoice("The dead own nothing.");
    }
    else
    {
	if (!mob->inventory().entries())
	{
	    glbChooser->appendChoice("nothing");
	}
	else
	{
	    for (int i = 0; i < mob->inventory().entries(); i++)
	    {
		ITEM 		*item = mob->inventory()(i);
		BUF		 name = item->getName();
		if (item->isEquipped())
		{
		    BUF		equip;
		    equip.sprintf("%s (equipped)", name.buffer());
		    name.strcpy(equip);
		}

		glbChooser->appendChoice(name);
	    }
	}
    }
    glbChooser->setChoice(glbLastItemIdx);

    glbChooserActive = true;
}

void
inventoryMenu()
{
    MOB		*avatar = (glbMap ? glbMap->avatar() : 0);
    int		key;
    bool	setlast = false;
    buildInventory(avatar);

    // Run the chooser...
    while (!TCODConsole::isWindowClosed())
    {
	redrawWorld();
	key = gfx_getKey(false);

	glbChooser->processKey(key);

	if (key)
	{
	    int		itemno = glbChooser->getChoice();
	    bool	done = false;
	    // User selects this?
	    if (key == '\n' || key == ' ')
	    {
		done = true;

		glbLastItemIdx = glbChooser->getChoice();
		setlast = true;
		if (itemno >= 0 && itemno < avatar->inventory().entries())
		{
		    done = useItem(avatar, avatar->inventory()(itemno), itemno);
		}
		// Finish the inventory menu.
		if (done)
		{
		    break;
		}
		else
		{
		    buildInventory(avatar);
		    setlast = false;
		}
	    }
	    else if (key == '\x1b' || key == 'i')
	    {
		// Esc on options is to go back to play.
		// Play...
		break;
	    }
	    else if (itemno >= 0 && itemno < avatar->inventory().entries())
	    {
		if (key == 'd' || key == 'D')
		{
		    glbEngine->queue().append(COMMAND(ACTION_DROP, itemno));
		    done = true;
		}
		else if (key == 'q' || key == 'Q')
		{
		    glbEngine->queue().append(COMMAND(ACTION_QUAFF, itemno));
		    done = true;
		}
		else if (key == 'e' || key == 'E')
		{
		    glbEngine->queue().append(COMMAND(ACTION_EAT, itemno));
		    done = true;
		}
		else if (key == 'b' || key == 'B')
		{
		    glbEngine->queue().append(COMMAND(ACTION_BREAK, itemno));
		    done = true;
		}
		else if (key == 'w' || key == 'W')
		{
		    glbEngine->queue().append(COMMAND(ACTION_WEAR, itemno));
		    done = true;
		}
		else if (key == 'x' || key == 'X')
		{
		    if (itemno >= 0 && itemno < avatar->inventory().entries())
		    {
			popupText(avatar->inventory()(itemno)->getLongDescription());
		    }
		}
		else if (key == 't' || key == 'T')
		{
		    glbLastItemIdx = glbChooser->getChoice();
		    setlast = true;
		    glbChooserActive = false;
		    doThrow(itemno);
		    done = true;
		    break;
		}
	    }
	    else
	    {
		// Ignore other options.
	    }

	    if (done)
		break;
	}
    }
    if (!setlast)
	glbLastItemIdx = glbChooser->getChoice();
    glbChooserActive = false;
}

void
castSpell(MOB *mob, SPELL_NAMES spell)
{
    int			dx = 0;
    int			dy = 0;

    if (glb_spelldefs[spell].needsdir)
    {
	msg_report("Cast in which direction?  ");
	if (awaitDirection(dx, dy) && (dx || dy))
	{
	    msg_report(rand_dirtoname(dx, dy));
	    msg_newturn();
	}
	else
	{
	    msg_report("Cancelled.  ");
	    msg_newturn();
	    return;
	}
    }
    glbLastSpell = spell;
    glbEngine->queue().append(COMMAND(ACTION_CAST, dx, dy, spell));
}

void
buildZapList(MOB *mob, PTRLIST<SPELL_NAMES> &list)
{
    glbChooser->clear();
    list.clear();
    int				chosen = 0;

    glbChooser->setTextAttr(ATTR_NORMAL);
    if (!mob)
    {
	glbChooser->appendChoice("The dead can cast nothing.");
	list.append(SPELL_NONE);
    }
    else
    {
	SPELL_NAMES		spell;

	FOREACH_SPELL(spell)
	{
	    ITEM_NAMES		itemname;
	    itemname = (ITEM_NAMES) glb_spelldefs[spell].item;
	    if (itemname != ITEM_NONE &&
		mob->hasItem(itemname))
	    {
		BUF		buf;

		buf.sprintf("Cast %s", glb_spelldefs[spell].name);
		glbChooser->appendChoice(buf);
		list.append(spell);
		if (spell == glbLastSpell)
		    chosen = list.entries()-1;
	    }
	}
    }
    if (!glbChooser->entries())
    {
	glbChooser->appendChoice("You know no magic spells.");
	list.append(SPELL_NONE);
    }

    glbChooser->setChoice(chosen);

    glbChooserActive = true;
}

void
zapMenu()
{
    PTRLIST<SPELL_NAMES>	spelllist;
    MOB		*avatar = (glbMap ? glbMap->avatar() : 0);
    int				key;

    buildZapList(avatar, spelllist);

    // Run the chooser...
    while (!TCODConsole::isWindowClosed())
    {
	redrawWorld();
	key = gfx_getKey(false);

	glbChooser->processKey(key);

	if (key)
	{
	    int		itemno = glbChooser->getChoice();
	    bool	done = false;
	    // User selects this?
	    if (key == '\n' || key == ' ' || key == 'z' || key == '+')
	    {
		SPELL_NAMES		spell = spelllist(itemno);

		if (spell != SPELL_NONE)
		{
		    glbChooserActive = false;
		    castSpell(avatar, spell);
		}
		break;
	    }
	    else if (key == '\x1b' || key == 'i')
	    {
		// Esc on options is to go back to play.
		// Play...
		break;
	    }
	    else
	    {
		// Ignore other options.
	    }

	    if (done)
		break;
	}
    }
    glbChooserActive = false;
}

void
buildOptionsMenu(OPTION_NAMES d)
{
    OPTION_NAMES	option;
    glbChooser->clear();

    glbChooser->setTextAttr(ATTR_NORMAL);
    FOREACH_OPTION(option)
    {
	glbChooser->appendChoice(glb_optiondefs[option].name);
    }
    glbChooser->setChoice(d);

    glbChooserActive = true;
}

bool
optionsMenu()
{
    int			key;
    bool		done = false;

    buildOptionsMenu(OPTION_INSTRUCTIONS);

    // Run the chooser...
    while (!TCODConsole::isWindowClosed())
    {
	redrawWorld();
	key = gfx_getKey(false);

	glbChooser->processKey(key);

	if (key)
	{
	    // User selects this?
	    if (key == '\n' || key == ' ')
	    {
		if (glbChooser->getChoice() == OPTION_INSTRUCTIONS)
		{
		    // Instructions.
		    popupText(text_lookup("game", "help"));
		}
		else if (glbChooser->getChoice() == OPTION_PLAY)
		{
		    // Play...
		    break;
		}
#if 0
		else if (glbChooser->getChoice() == OPTION_VOLUME)
		{
		    int			i;
		    BUF			buf;
		    // Volume
		    glbChooser->clear();

		    for (i = 0; i <= 10; i++)
		    {
			buf.sprintf("%3d%% Volume", (10 - i) * 10);

			glbChooser->appendChoice(buf);
		    }
		    glbChooser->setChoice(10 - glbConfig->myMusicVolume);

		    while (!TCODConsole::isWindowClosed())
		    {
			redrawWorld();
			key = gfx_getKey(false);

			glbChooser->processKey(key);

			setMusicVolume(10 - glbChooser->getChoice());
			if (key)
			{
			    break;
			}
		    }
		    buildOptionsMenu(OPTION_VOLUME);
		}
#endif
		else if (glbChooser->getChoice() == OPTION_COINS)
		{
		    glbConfig->myCoinsDynamic = !glbConfig->myCoinsDynamic;
		    glbCoinStack->setBarGraph(!glbConfig->myCoinsDynamic);
		}
		else if (glbChooser->getChoice() == OPTION_FULLSCREEN)
		{
		    glbConfig->myFullScreen = !glbConfig->myFullScreen;
		    // This is intentionally unrolled to work around a
		    // bool/int problem in libtcod
		    if (glbConfig->myFullScreen)
			TCODConsole::setFullscreen(true);
		    else
			TCODConsole::setFullscreen(false);
		}
		else if (glbChooser->getChoice() == OPTION_QUIT)
		{
		    // Quit
		    done = true;
		    break;
		}
	    }
	    else if (key == '\x1b')
	    {
		// Esc on options is to go back to play.
		// Play...
		break;
	    }
	    else
	    {
		// Ignore other options.
	    }
	}
    }
    glbChooserActive = false;

    return done;
}

bool
reloadLevel(bool alreadybuilt)
{
    if (TCODConsole::isWindowClosed())
	return true;

    glbVeryFirstRun = false;

    glbLevel = 1;
    msg_newturn();
    glbMessagePanel->clear();
    glbMessagePanel->appendText("> ");
    if (!alreadybuilt)
	glbEngine->queue().append(COMMAND(ACTION_RESTART, glbLevel));

    startMusic();

    // Wait for the map to finish.
    if (glbEngine->awaitRebuild())
    {
	popupText(text_lookup("welcome", "Back"), POPUP_DELAY);
    }
    else
    {
	popupText(text_lookup("welcome", "You1"), POPUP_DELAY);
    }

    // Redrawing the world updates our glbMap so everyone will see it.
    redrawWorld();

    return false;
}

// Let the avatar watch the end...
void
deathCoolDown()
{
    int		coolstartms = TCOD_sys_elapsed_milli();
    int		lastenginems = coolstartms;
    int		curtime;
    bool	done = false;
    double	ratio;

    while (!done)
    {
	curtime = TCOD_sys_elapsed_milli();

	// Queue wait events, one per 100 ms.
	while (lastenginems + 200 < curtime)
	{
	    glbEngine->queue().append(COMMAND(ACTION_WAIT));
	    lastenginems += 200;
	}

	// Set our fade.
	ratio = (curtime - coolstartms) / 5000.;
	ratio = 1.0 - ratio;
	if (ratio < 0)
	{
	    done = true;
	    ratio = 0;
	}

	TCOD_console_set_fade( (u8) (255 * ratio), TCOD_black);

	// Keep updating
	redrawWorld();
    }

    gfx_clearKeyBuf();

    TCOD_console_set_fade(255,  TCOD_black);
}

void
shutdownEverything()
{
    glbConfig->save("../orc.cfg");

    // Save the game.  A no-op if dead.
    glbEngine->queue().append(COMMAND(ACTION_SAVE));
    glbEngine->awaitSave();

    stopMusic(true);
#ifdef USE_AUDIO
    if (glbTunes)
	Mix_FreeMusic(glbTunes);
#endif

    gfx_shutdown();

#ifdef USE_AUDIO
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
#endif

    SDL_Quit();
}

void
buildShopMenu(MOB_NAMES keeper, PTRLIST<COMMAND> &list)
{
    glbChooser->clear();
    list.clear();
    BUF			buf;
    MOB		*avatar = (glbMap ? glbMap->avatar() : 0);
    bool	valid = false;

    if (!avatar || !avatar->alive())
	return;

    glbChooser->setPrequelAttr((ATTR_NAMES) glb_mobdefs[keeper].attr);
    switch (keeper)
    {
	case MOB_HEALER:
	{
	    int		hpneed;

	    glbChooser->setPrequel(text_lookup("shop", "healer"));

	    hpneed = avatar->getMaxHP() - avatar->getHP();

	    if (avatar->hasItem(ITEM_POISON))
	    {
		buf.sprintf("%d gold - Cure poison", 10);
		glbChooser->appendChoice(buf);
		list.append(COMMAND(ACTION_SHOP, SHOP_REMOVEITEM, 
				    10, ITEM_POISON));
	    }
	    // Can the player even get here while still blind?
	    if (avatar->hasItem(ITEM_BLIND))
	    {
		buf.sprintf("%d gold - Cure blindness", 20);
		glbChooser->appendChoice(buf);
		list.append(COMMAND(ACTION_SHOP, SHOP_REMOVEITEM, 
				    10, ITEM_BLIND));
	    }

	    valid = true;
	    if (hpneed)
	    {
		buf.sprintf("%d gold - Full recovery", hpneed);
		glbChooser->appendChoice(buf);
		list.append(COMMAND(ACTION_SHOP, SHOP_HEAL, hpneed, hpneed));
	    }
	    hpneed /= 2;
	    if (hpneed)
	    {
		buf.sprintf("%d gold - Half recovery", hpneed);
		glbChooser->appendChoice(buf);
		list.append(COMMAND(ACTION_SHOP, SHOP_HEAL, hpneed, hpneed));
	    }
	    hpneed /= 2;
	    if (hpneed)
	    {
		buf.sprintf("%d gold - Quarter recovery", hpneed);
		glbChooser->appendChoice(buf);
		list.append(COMMAND(ACTION_SHOP, SHOP_HEAL, hpneed, hpneed));
	    }

	    glbChooser->appendChoice("0 gold - A few moments peace and quiet");
	    list.append(COMMAND(ACTION_SHOP, SHOP_HEAL, 0, 0));

	    break;
	}
	case MOB_ELDER:
	{
	    glbChooser->setPrequel(text_lookup("shop", "elder"));
	    valid = true;

	    glbChooser->appendChoice("Tell me of the cave?");
	    list.append(COMMAND(ACTION_SHOP, SHOP_GOSSIP, 0, GOSSIP_INTRO));

	    if (avatar->hasItem(ITEM_BOOK_INTRO))
	    {
		glbChooser->appendChoice("What of the island?");
		list.append(COMMAND(ACTION_SHOP, SHOP_GOSSIP, 0, GOSSIP_MEDITATE));
	    }

	    if (glbQuestActive[QUEST_ICYPASS])
	    {
		glbChooser->appendChoice("Is the pass clearing?");
		list.append(COMMAND(ACTION_SHOP, SHOP_GOSSIP, 0, GOSSIP_IMPASS));
	    }

	    if (glbQuestActive[QUEST_COLD])
	    {
		glbChooser->appendChoice("What of the cold?");
		list.append(COMMAND(ACTION_SHOP, SHOP_GOSSIP, 0, GOSSIP_COLD));
	    }
	    if (glbQuestActive[QUEST_SICKNESS])
	    {
		glbChooser->appendChoice("What of this sickness?");
		list.append(COMMAND(ACTION_SHOP, SHOP_GOSSIP, 0, GOSSIP_SICKNESS));
	    }

	    if (glbFinalPax)
	    {
		glbChooser->appendChoice("Where are the orcs?");
		list.append(COMMAND(ACTION_SHOP, SHOP_GOSSIP, 0, GOSSIP_ORCS));
	    }

	    // Identification requests
	    for (int itemno = 0; itemno < avatar->inventory().entries(); itemno++)
	    {
		ITEM	*item = avatar->inventory()(itemno);

		if (item->isPotion() && !item->isMagicClassKnown())
		{
		    BUF itemname;
		    itemname = item->getName();
		    buf.sprintf("%d gold - Identify %s",
				10,
				itemname.buffer());
		    glbChooser->appendChoice(buf.buffer());
		    list.append(COMMAND(ACTION_SHOP, SHOP_ID, 10, itemno));
		}
		if (item->isRing() && !item->isMagicClassKnown())
		{
		    BUF itemname;
		    itemname = item->getName();
		    buf.sprintf("%d gold - Identify %s",
				20,
				itemname.buffer());
		    glbChooser->appendChoice(buf.buffer());
		    list.append(COMMAND(ACTION_SHOP, SHOP_ID, 20, itemno));
		}
	    }
	    break;
	}
	case MOB_SMITH:
	{
	    valid = true;
	    glbChooser->setPrequel(text_lookup("shop", "smith"));

	    for (int itemno = 0; itemno < avatar->inventory().entries(); itemno++)
	    {
		ITEM	*item = avatar->inventory()(itemno);

		if ((item->isMelee() || item->isArmour()) && item->isBroken())
		{
		    BUF itemname;
		    itemname = item->getName();
		    buf.sprintf("%d gold - Repair %s",
				item->defn().repaircost,
				itemname.buffer());
		    glbChooser->appendChoice(buf.buffer());
		    list.append(COMMAND(ACTION_SHOP, SHOP_FIX, item->defn().repaircost, itemno));
		}
	    }

	    break;
	}
	case MOB_FLETCHER:
	{
	    valid = true;
	    glbChooser->setPrequel(text_lookup("shop", "fletcher"));

	    // ALways sell arrows
	    glbChooser->appendChoice("6 gold - A dozen arrows");
	    list.append(COMMAND(ACTION_SHOP, SHOP_BUY, 6, ITEM_ARROW));

	    for (int itemno = 0; itemno < avatar->inventory().entries(); itemno++)
	    {
		ITEM	*item = avatar->inventory()(itemno);

		if (item->isRanged() && item->isBroken())
		{
		    BUF itemname;
		    itemname = item->getName();
		    buf.sprintf("%d gold - Repair %s",
				item->defn().repaircost,
				itemname.buffer());
		    glbChooser->appendChoice(buf.buffer());
		    list.append(COMMAND(ACTION_SHOP, SHOP_FIX, item->defn().repaircost, itemno));
		}
	    }

	    break;
	}
    }


    if (valid)
    {
	glbChooser->appendChoice("No thank you.");
	list.append(COMMAND(ACTION_NONE));
	glbChooser->setChoice(list.entries()-1);
	glbChooserActive = true;
    }
    else
    {
	glbChooser->clear();
	list.clear();
    }
}

void
runShop(MOB_NAMES shopkeeper)
{
    PTRLIST<COMMAND>	shoppinglist;
    int			key;

    buildShopMenu(shopkeeper, shoppinglist);
    
    if (shoppinglist.entries() == 0)
	return;

    // Run the chooser...
    while (!TCODConsole::isWindowClosed())
    {
	redrawWorld();
	key = gfx_getKey(false);

	glbChooser->processKey(key);

	if (key)
	{
	    int		itemno = glbChooser->getChoice();
	    bool	done = false;
	    // User selects this?
	    if (key == '\n' || key == ' ' || key == 'z')
	    {
		COMMAND		cmd = shoppinglist(itemno);
		if (cmd.action() != ACTION_NONE)
		    glbEngine->queue().append(cmd);
		break;
	    }
	    else if (key == '\x1b' || key == 'i')
	    {
		// Esc on options is to go back to play.
		// Play...
		break;
	    }
	    else
	    {
		// Ignore other options.
	    }

	    if (done)
		break;
	}
    }
    glbChooserActive = false;
}

void
artMessage()
{
    const char		*inname = glbFinalPax ? "../data/level.dat" : "../data/game.dat";
    const char		*outname = glbFinalPax ? "../NOORCS.TXT" : "../ARTMSG.TXT";

#ifdef WIN32
    ifstream		is(inname, ios::in | ios::binary);
#else
    ifstream		is(inname);
#endif
    ofstream		os(outname, ios::out);

    // Randomly generated by Slash.
    rand_setseed(470607);

    while (is)
    {
	char	c;

	is.read(&c, 1);
	if (!is)
	    break;

	c ^= rand_choice(256);

	// Make sure it is a sane character.
	c &= 0x7f;
	if (!c)
	    continue;

	os.write(&c, 1);
    }

    // Reseed our generator
    rand_setseed((long) time(0));
}


#ifdef WIN32
int WINAPI
WinMain(HINSTANCE hINstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCMdSHow)
#else
int 
main(int argc, char **argv)
#endif
{
    bool		done = false;

    // Clamp frame rate.
    TCOD_sys_set_fps(60);

    glbConfig = new CONFIG();
    glbConfig->load("../orc.cfg");

    // Dear Microsoft,
    // The following code in optimized is both a waste and seems designed
    // to ensure that if we call a bool where the callee pretends it is
    // an int we'll end up with garbage data.
    //
    // 0040BF4C  movzx       eax,byte ptr [eax+10h] 

    // TCODConsole::initRoot(SCR_WIDTH, SCR_HEIGHT, "Vicious Orcs", myFullScreen);
    // 0040BF50  xor         ebp,ebp 
    // 0040BF52  cmp         eax,ebp 
    // 0040BF54  setne       cl   
    // 0040BF57  mov         dword ptr [esp+28h],eax 
    // 0040BF5B  push        ecx  

    // My work around is to constantify the fullscreen and hope that
    // the compiler doesn't catch on.
    if (glbConfig->myFullScreen)
	TCODConsole::initRoot(SCR_WIDTH, SCR_HEIGHT, "Vicious Orcs", true);
    else
	TCODConsole::initRoot(SCR_WIDTH, SCR_HEIGHT, "Vicious Orcs", false);

    // TCOD doesn't do audio.
#ifdef USE_AUDIO
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    if (Mix_OpenAudio(22050, AUDIO_S16, 2, 4096))
    {
	printf("Failed to open audio!\n");
	exit(1);
    }
#endif

    SDL_InitSubSystem(SDL_INIT_JOYSTICK);

    setMusicVolume(glbConfig->musicVolume());

    rand_setseed((long) time(0));

    gfx_init();
    MAP::init();

    text_init();
    spd_init();

    glbDisplay = new DISPLAY(0, 0, 80, 40);

    glbMessagePanel = new PANEL(40, 10);
    msg_registerpanel(glbMessagePanel);
    glbMessagePanel->move(20, 40);

    glbPopUp = new PANEL(50, 30);
    glbPopUp->move(15, 10);
    glbPopUp->setBorder(true, ' ', ATTR_BORDER);
    // And so the mispelling is propagated...
    glbPopUp->setRigthMargin(1);
    glbPopUp->setIndent(1);

    glbJacob = new PANEL(15, 6);
    glbJacob->move(65, 2);
    glbJacob->setTransparent(true);

    glbWeaponInfo = new PANEL(20, 20);
    glbWeaponInfo->move(1, 2);
    glbWeaponInfo->setTransparent(true);

    glbVictoryInfo = new PANEL(26, 2);
    glbVictoryInfo->move(28, 7);
    glbVictoryInfo->setIndent(2);
    glbVictoryInfo->setBorder(true, ' ', ATTR_VICTORYBORDER);

    glbLevel = 1;

    glbChooser = new CHOOSER();
    glbChooser->move(40, 25, CHOOSER::JUSTCENTER, CHOOSER::JUSTCENTER);
    glbChooser->setBorder(true, ' ', ATTR_BORDER);
    glbChooser->setIndent(1);
    glbChooser->setRigthMargin(1);

    glbLevelBuildStatus = new CHOOSER();
    glbLevelBuildStatus->move(40, 25, CHOOSER::JUSTCENTER, CHOOSER::JUSTCENTER);
    glbLevelBuildStatus->setBorder(true, ' ', ATTR_BORDER);
    glbLevelBuildStatus->setIndent(1);
    glbLevelBuildStatus->setRigthMargin(1);
    glbLevelBuildStatus->setAttr(ATTR_NORMAL, ATTR_HILITE, ATTR_HILITE);

    glbEngine = new ENGINE(glbDisplay);

    glbHealthFire = new FIREFLY(0, 80, 1, FIRE_HEALTH, true);
    glbHealthFire->setBarGraph(true);
    glbManaFire = new FIREFLY(0, 80, 1, FIRE_MANA, true);
    glbManaFire->setBarGraph(true);
    glbCoinStack = new FIREFLY(0, 20, 48, FIRE_GOLD, false);
    glbCoinStack->setBarGraph(!glbConfig->myCoinsDynamic);

    // Run our first level immediately so it can be built or 
    // loaded from disk while people wait.
    glbEngine->queue().append(COMMAND(ACTION_RESTART, 1));

    done = optionsMenu();
    if (done)
    {
	shutdownEverything();
	return 0;
    }

    done = reloadLevel(true);
    if (done)
    {
	shutdownEverything();
	return 0;
    }

    do
    {
	int		key;
	int		dx, dy;

	redrawWorld();

	while (1)
	{
	    BUF		pop;

	    pop = glbEngine->getNextPopup();
	    if (!pop.isstring())
		break;

	    popupText(pop, POPUP_DELAY);
	}

	while (1)
	{
	    MOB_NAMES	shopper;

	    shopper = glbEngine->getNextShopper();
	    if (shopper == MOB_NONE)
		break;

	    runShop(shopper);
	}


	if (glbMap && glbMap->avatar() &&
	    !glbMap->avatar()->alive())
	{
	    // You have died...
	    deathCoolDown();

	    popupText(text_lookup("game", "lose"));

	    stopMusic();

	    done = optionsMenu();
	    if (done)
	    {
		shutdownEverything();
		return 0;
	    }
	    // Don't prep a reload.
	    done = reloadLevel(false);
	}

	key = gfx_getKey(false);

	if (gfx_cookDir(key, dx, dy))
	{
	    // Direction.
	    glbEngine->queue().append(COMMAND(ACTION_BUMP, dx, dy));
	}

	switch (key)
	{
	    case 'Q':
		done = true;
		break;

	    case 'R':
		done = reloadLevel(false);
		break;

	    case '/':
	    case 'e':
		glbEngine->queue().append(COMMAND(ACTION_ROTATE, 1));
		break;
	    case '*':
	    case 'r':
		glbEngine->queue().append(COMMAND(ACTION_ROTATE, 3));
		break;

	    case 'm':
		glbEngine->queue().append(COMMAND(ACTION_MEDITATE));
		break;

	    case 's':
		glbEngine->queue().append(COMMAND(ACTION_SEARCH));
		break;

	    case 'i':
		inventoryMenu();
		break;

	    case 'f':
	    case '0':
		msg_report("Shoot in what direction?  ");
		if (awaitDirection(dx, dy) && (dx || dy))
		{
		    msg_report(rand_dirtoname(dx, dy));
		    msg_newturn();
		    glbEngine->queue().append(COMMAND(ACTION_FIRE, dx, dy));
		}
		else
		{
		    msg_report("Cancelled.  ");
		    msg_newturn();
		}
		break;

	    case 'a':
		popupText(text_lookup("game", "about"));
		break;

	    case 'W':
	    {
		popupText(text_lookup("welcome", "You1"));
		break;
	    }

	    case 'z':
	    case '+':
	    {
		zapMenu();
		break;
	    }

	    case 'P':
		glbConfig->myFullScreen = !glbConfig->myFullScreen;
		// This is intentionally unrolled to work around a
		// bool/int problem in libtcod
		if (glbConfig->myFullScreen)
		    TCODConsole::setFullscreen(true);
		else
		    TCODConsole::setFullscreen(false);
		break;
	
	    case GFX_KEYF1:
	    case '?':
		popupText(text_lookup("game", "help"));
		break;

	    case 'v':
		if (glbMap && glbMap->avatar())
		{
		    if (glbMap->avatar()->alive() &&
			glbMap->getNumOrcs() == 0)
		    {
			// Traditional win.
			if (!glbFinalPax)
			    popupText(text_lookup("game", "victory"));
			else
			    popupText(text_lookup("game", "cleanvictory"));

			// Three possible cases.  Only two of which
			// get art messages.
			if (glbFinalPax || glbWasDeluded)
			{
			    artMessage();
			}

			stopMusic();
			done = reloadLevel(false);
			if (!done)
			    done = optionsMenu();
		    }
		    else
		    {
			msg_report("You haven't achieved victory conditions.  ");
		    }
		}
		break;

	    case 'x':
		doExamine();
		break;

#if 0
	    case 'W':
		glbEngine->queue().append(COMMAND(ACTION_CREATEITEM, ITEM_BOW));

		break;
#endif

	    case '\x1b':
		// If meditating, abort.
		if (glbMap && glbMap->avatar() && 
		    glbMap->avatar()->alive() && glbMap->avatar()->isMeditating())
		{
		    glbEngine->queue().append(COMMAND(ACTION_MEDITATE));
		    break;
		}
		// FALL THROUGH
	    case 'O':
		done = optionsMenu();
		break;
	}
    } while (!done && !TCODConsole::isWindowClosed());

    shutdownEverything();

    return 0;
}
