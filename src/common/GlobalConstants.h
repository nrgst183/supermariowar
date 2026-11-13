#ifndef GLOBALCONSTANTS_H
#define GLOBALCONSTANTS_H

//------------- global definitions -------------

constexpr int FILEBUFSIZE = 1024;

//----------------------------------------------------------------
// DON'T CHANGE THE ORDER xxx_R xxx_L xxx_R xxx_L !!!
//----------------------------------------------------------------
// because this is used to determine the direction the player was facing in the last frame
// if the last sprite was facing right (spr & 0x1) is 0
// because the last bit of every PGFX_[action]_R is 0
// the last bit of PGFX_[action]_L is 1
// IF YOU CHANGE THE ORDER OF THE PGFX_L/R STUFF THIS WON'T WORK!

// see CPlayer::draw() on how this is used

constexpr int PGFX_STANDING_R = 0;
constexpr int PGFX_STANDING_L = 1;
constexpr int PGFX_RUNNING_R = 2;
constexpr int PGFX_RUNNING_L = 3;
constexpr int PGFX_JUMPING_R = 4;
constexpr int PGFX_JUMPING_L = 5;
constexpr int PGFX_STOPPING_R = 6;
constexpr int PGFX_STOPPING_L = 7;
constexpr int PGFX_DEADFLYING = 8;
constexpr int PGFX_DEAD = 9;
//--------------------------
constexpr int PGFX_LAST = 10;

constexpr int PANNOUNCER_SOUND_LAST = 20;

constexpr int PH = 25;                                  //Player height
constexpr int PW = 22;                                  //Player width
constexpr int HALFPH = 12;
constexpr int HALFPW = 11;
constexpr int PHOFFSET = 4;
constexpr int PWOFFSET = 5;

constexpr float VELMOVING = 4.0f;                       //velocity (speed) for moving left, right
constexpr float VELSLOWMOVING = 2.2f;                   //velocity when slow down powerup is in effect
constexpr float VELMOVING_CHICKEN = 2.9f;               //speed of the chicken in the gamemode capturethechicken
constexpr float VELMOVINGADD = 0.5f;
constexpr float VELMOVINGADDICE = VELMOVINGADD / 4;
constexpr float VELTURBOMOVING = 5.5f;
constexpr float VELJUMP = 9.0f;                         //velocity for jumping
constexpr float VELSLOWJUMP = 7.0f;                     //velocity for jumping when slow down powerup is in effect
constexpr float VELTURBOJUMP = 10.2f;                   //velocity for turbo jumping
constexpr float VELSUPERJUMP = 13.0f;                   //super jump (jumping off of orange note blocks)
constexpr float VELPUSHBACK = 5.0f;
constexpr float VELMAXBREAKBLOCK = 3.0f;
constexpr float VELNOTEBLOCKBOUNCE = 3.0f;
constexpr float VELNOTEBLOCKREPEL = 5.0f;
constexpr float VELBLOCKBOUNCE = 3.0f;
constexpr float VELBLOCKREPEL = 3.0f;
constexpr float VELPOWERUPBOUNCE = 8.1f;
constexpr float FIREBALLBOUNCE = 5.0f;
constexpr float HAMMERTHROW = 8.5f;
constexpr float VELMOVINGFRICTION = 0.2f;
constexpr float VELICEFRICTION = 0.06f;
constexpr float VELAIRFRICTION = 0.06f;
constexpr float VELSTOPJUMP = 5.0f;
constexpr float BOUNCESTRENGTH = 0.5f;
constexpr float TAGGEDBOOST = 1.0f;
constexpr float VELSUPERSTOMP = 10.0f;
constexpr float VELTAILSHAKE = 1.0f;
constexpr float VELKURIBOBOUNCE = 3.0f;
constexpr float VELENEMYBOUNCE = 7.0f;

constexpr float GRAVITATION = 0.40f;

constexpr float MAXVELY = 20.0f;
constexpr float MAXSIDEVELY = 10.0f;

constexpr int CRUNCHVELOCITY = 2;
constexpr int CRUNCHAMOUNT = 16;
constexpr int CRUNCHMAX = 32;


constexpr int MAPWIDTH = 20;                            //width of the map
constexpr int MAPHEIGHT = 15;                           //height of the map
constexpr int MAPLAYERS = 4;                            //number of layers the map has
constexpr int TILESIZE = 32;                            //size of the tiles, should be dynamically read
constexpr int TILESIZE_SHIFT = 5;                       //log2(TILESIZE): converts tile coordinates to pixels
constexpr int PREVIEWTILESIZE = 16;                     //size of the preview tiles
constexpr int THUMBTILESIZE = 8;                        //size of the thumbnail tiles

constexpr int MAXWARPS = 32;
constexpr int MAXMAPITEMS = 32;
constexpr int MAXMAPHAZARDS = 30;
constexpr int NUMMAPHAZARDPARAMS = 5;

constexpr int NUMSPAWNAREATYPES = 6;
constexpr int MAXSPAWNAREAS = 128;
constexpr int MAXDRAWAREAS = 128;

constexpr int MAX_BONUS_CHESTS = 5;
constexpr int MAX_WORLD_BONUSES_AWARDED = 3;

constexpr int TILESETSIZE = 960;                        //30*32 Tiles by 32 pixel in a 1024*1024 bmp
constexpr int TILESETHEIGHT = 30;
constexpr int TILESETWIDTH = 32;

constexpr int TILESETANIMATED = -1;
constexpr int TILESETNONE = -2;
constexpr int TILESETUNKNOWN = -3;

constexpr int TILEANIMATIONSIZE = 38;                   //number of tile animations

constexpr int MAXMUSICCATEGORY = 11;
constexpr int MAXWORLDMUSICCATEGORY = 9;
constexpr int WORLDMUSICBONUS = MAXWORLDMUSICCATEGORY;
constexpr int WORLDMUSICSLEEP = MAXWORLDMUSICCATEGORY + 1;
constexpr int WORLDMUSICWORLDS = MAXWORLDMUSICCATEGORY + 2;
constexpr int MAXCATEGORYTRACKS = 64;

constexpr int MAXEYECANDY = 192;
constexpr int CORPSESTAY = 200;

constexpr int WAITTIME = 16;                            //delay between frames (default:16)
constexpr int MAXAWARDS = 10;
constexpr int MINAWARDSNEEDED = 3;

constexpr int MAXTOURNAMENTGAMES = 10;
constexpr int MAXWORLDITEMS = 32;

constexpr int MAXRACEGOALS = 8;

constexpr float TWO_PI = 6.2831853f;
constexpr float PI = 3.1415292f;
constexpr float THREE_HALF_PI = 4.7123890f;
constexpr float THREE_QUARTER_PI = 2.3561945f;
constexpr float HALF_PI = 1.5707963f;
constexpr float QUARTER_PI = 0.7853982f;

constexpr int NUM_POWERUPS = 26;
constexpr int NUM_WORLD_POWERUPS = 15;
constexpr int NUM_WORLD_SCORE_BONUSES = 20;
constexpr int NUM_WORLD_ITEMS = NUM_POWERUPS + NUM_WORLD_POWERUPS + NUM_WORLD_SCORE_BONUSES;
constexpr int NUM_BLOCK_SETTINGS = NUM_POWERUPS;
constexpr int NUM_POWERUP_PRESETS = 17;

constexpr int HEALTH_POWERUP = -1;
constexpr int TIME_POWERUP = -2;
constexpr int JAIL_KEY_POWERUP = -3;
constexpr int COIN_POWERUP = -4;
constexpr int MINIGAME_COIN = -5;
constexpr int SECRET1_POWERUP = -6;
constexpr int SECRET2_POWERUP = -7;
constexpr int SECRET3_POWERUP = -8;
constexpr int SECRET4_POWERUP = -9;
constexpr int NO_POWERUP = -10;

constexpr int NUM_AUTO_FILTERS = 12;

constexpr int NUMSTOMPENEMIES = 9;
constexpr int NUMSURVIVALENEMIES = 3;
constexpr int NUMFRENZYCARDS = 19;

constexpr int MAX_PLAYERS = 4;


#endif // GLOBALCONSTANTS_H
