/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "cl_local.h"

#define STAT_PICS       11
#define STAT_MINUS      (STAT_PICS-1)  // num frame for '-' stats digit

static struct {
    qboolean    initialized;        // ready to draw

    qhandle_t   crosshair_pic;
    int         crosshair_width, crosshair_height;
    color_t     crosshair_color;

    qhandle_t   pause_pic;
    int         pause_width, pause_height;

    qhandle_t   loading_pic;
    int         loading_width, loading_height;
    qboolean    draw_loading;

    qhandle_t   sb_pics[2][STAT_PICS];
    qhandle_t   inven_pic;
    qhandle_t   field_pic;

    qhandle_t   backtile_pic;

    qhandle_t   net_pic;
    qhandle_t   font_pic;

    int         hud_width, hud_height;
} scr;

static cvar_t   *scr_viewsize;
static cvar_t   *scr_centertime;
static cvar_t   *scr_showpause;
#ifdef _DEBUG
static cvar_t   *scr_showstats;
static cvar_t   *scr_showpmove;
#endif
static cvar_t   *scr_showturtle;

static cvar_t   *scr_draw2d;
static cvar_t   *scr_lag_x;
static cvar_t   *scr_lag_y;
static cvar_t   *scr_lag_draw;
static cvar_t   *scr_lag_max;
static cvar_t   *scr_alpha;

static cvar_t   *scr_demobar;
static cvar_t   *scr_font;
static cvar_t   *scr_scale;

static cvar_t   *scr_crosshair;

static cvar_t   *ch_red;
static cvar_t   *ch_green;
static cvar_t   *ch_blue;
static cvar_t   *ch_alpha;

#ifdef _DEBUG
cvar_t      *scr_netgraph;
cvar_t      *scr_timegraph;
cvar_t      *scr_debuggraph;

static cvar_t   *scr_graphheight;
static cvar_t   *scr_graphscale;
static cvar_t   *scr_graphshift;
#endif

vrect_t     scr_vrect;      // position of render window on screen
glconfig_t  scr_glconfig;

static const char *const sb_nums[2][STAT_PICS] = {
    { "num_0", "num_1", "num_2", "num_3", "num_4", "num_5",
    "num_6", "num_7", "num_8", "num_9", "num_minus" },
    { "anum_0", "anum_1", "anum_2", "anum_3", "anum_4", "anum_5",
    "anum_6", "anum_7", "anum_8", "anum_9", "anum_minus" }
};

const color_t colorTable[8] = {
    {   0,   0,   0, 255 },
    { 255,   0,   0, 255 },
    {   0, 255,   0, 255 },
    { 255, 255,   0, 255 },
    {   0,   0, 255, 255 },
    {   0, 255, 255, 255 },
    { 255,   0, 255, 255 },
    { 255, 255, 255, 255 }
};

/*
===============================================================================

UTILS

===============================================================================
*/

#define SCR_DrawString( x, y, flags, string ) \
    SCR_DrawStringEx( x, y, flags, MAX_STRING_CHARS, string, scr.font_pic )

/*
==============
SCR_DrawStringEx
==============
*/
int SCR_DrawStringEx( int x, int y, int flags, size_t maxlen,
                      const char *s, qhandle_t font )
{
    size_t len = strlen( s );
    
    if( len > maxlen ) {
        len = maxlen;
    }
        
    if( ( flags & UI_CENTER ) == UI_CENTER ) {
        x -= len * CHAR_WIDTH / 2;
    } else if( flags & UI_RIGHT ) {
        x -= len * CHAR_WIDTH;
    }

    return R_DrawString( x, y, flags, maxlen, s, font );
}


/*
==============
SCR_DrawStringMulti
==============
*/
void SCR_DrawStringMulti( int x, int y, int flags, size_t maxlen,
                          const char *s, qhandle_t font )
{
    char    *p;
    size_t  len;

    while( *s ) {
        p = strchr( s, '\n' );
        if( !p ) {
            SCR_DrawStringEx( x, y, flags, maxlen, s, font );
            break;
        }

        len = p - s;
        if( len > maxlen ) {
            len = maxlen;
        }
        SCR_DrawStringEx( x, y, flags, len, s, font );

        y += CHAR_HEIGHT;
        s = p + 1;
    }
}


/*
=================
SCR_FadeAlpha
=================
*/
float SCR_FadeAlpha( unsigned startTime, unsigned visTime, unsigned fadeTime ) {
    float alpha;
    unsigned timeLeft, delta = cls.realtime - startTime;

    if( delta >= visTime ) {
        return 0;
    }

    if( fadeTime > visTime ) {
        fadeTime = visTime;
    }

    alpha = 1;
    timeLeft = visTime - delta;
    if( timeLeft < fadeTime ) {
        alpha = ( float )timeLeft / fadeTime;
    }

    return alpha;
}

qboolean SCR_ParseColor( const char *s, color_t color ) {
    int i;
    int c[8];

    if( *s == '#' ) {
        s++;
        for( i = 0; s[i]; i++ ) {
            c[i] = Q_charhex( s[i] );
            if( c[i] == -1 ) {
                return qfalse;
            }
        }
        switch( i ) {
        case 3:
            color[0] = c[0] | ( c[0] << 4 );
            color[1] = c[1] | ( c[1] << 4 );
            color[2] = c[2] | ( c[2] << 4 );
            color[3] = 255;
            break;
        case 6:
            color[0] = c[1] | ( c[0] << 4 );
            color[1] = c[3] | ( c[2] << 4 );
            color[2] = c[5] | ( c[4] << 4 );
            color[3] = 255;
            break;
        case 8:
            color[0] = c[1] | ( c[0] << 4 );
            color[1] = c[3] | ( c[2] << 4 );
            color[2] = c[5] | ( c[4] << 4 );
            color[3] = c[7] | ( c[6] << 4 );
            break;
        default:
            return qfalse;
        }
        return qtrue;
    } else {
        i = Com_ParseColor( s, COLOR_WHITE );
        if( i == COLOR_NONE ) {
            return qfalse;
        }

        FastColorCopy( colorTable[i], color );
        return qtrue;
    }
}

/*
===============================================================================

BAR GRAPHS

===============================================================================
*/

#ifdef _DEBUG
/*
==============
CL_AddNetgraph

A new packet was just parsed
==============
*/
void CL_AddNetgraph (void)
{
    int     i;
    int     in;
    int     ping;

    if (!scr.initialized)
        return;

    // if using the debuggraph for something else, don't
    // add the net lines
    if (scr_debuggraph->integer || scr_timegraph->integer)
        return;

    for (i=0 ; i<cls.netchan->dropped ; i++)
        SCR_DebugGraph (30, 0x40);

    //for (i=0 ; i<cl.surpressCount ; i++)
    //  SCR_DebugGraph (30, 0xdf);

    // see what the latency was on this packet
    in = cls.netchan->incoming_acknowledged & CMD_MASK;
    ping = cls.realtime - cl.history[in].sent;
    ping /= 30;
    if (ping > 30)
        ping = 30;
    SCR_DebugGraph (ping, 0xd0);
}


typedef struct
{
    float   value;
    int     color;
} graphsamp_t;

static  int         current;
static  graphsamp_t values[2048];

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph (float value, int color)
{
    values[current&2047].value = value;
    values[current&2047].color = color;
    current++;
}

/*
==============
SCR_DrawDebugGraph
==============
*/
void SCR_DrawDebugGraph (void)
{
    int     a, x, y, w, i, h;
    float   v;
    int     color;

    //
    // draw the graph
    //
    w = scr_glconfig.vidWidth;

    x = w-1;
    y = scr_glconfig.vidHeight;
    R_DrawFill (x, y-scr_graphheight->value,
        w, scr_graphheight->value, 8);

    for (a=0 ; a<w ; a++)
    {
        i = (current-1-a+2048) & 2047;
        v = values[i].value;
        color = values[i].color;
        v = v*scr_graphscale->value + scr_graphshift->value;
        
        if (v < 0)
            v += scr_graphheight->value * (1+(int)(-v/scr_graphheight->value));
        h = (int)v % (int)scr_graphheight->value;
        R_DrawFill (x, y - h, 1,    h, color);
        x--;
    }
}
#endif

static void draw_percent_bar( int percent ) {
    char buffer[16];
    int x, w;
    size_t len;

    scr.hud_height -= CHAR_HEIGHT;

    w = scr.hud_width * percent / 100;

    R_DrawFill( 0, scr.hud_height, w, CHAR_HEIGHT, 4 );
    R_DrawFill( w, scr.hud_height, scr.hud_width - w, CHAR_HEIGHT, 0 );

    len = Q_scnprintf( buffer, sizeof( buffer ), "%d%%", percent );
    x = ( scr.hud_width - len * CHAR_WIDTH ) / 2;
    R_DrawString( x, scr.hud_height, 0, MAX_STRING_CHARS, buffer, scr.font_pic );
}

static void draw_demo_bar( void ) {
#if USE_MVD_CLIENT  
    int percent;
#endif

    if( !scr_demobar->integer ) {
        return;
    }

    if( cls.demo.playback ) {
        if( cls.demo.file_size ) {
            draw_percent_bar( cls.demo.file_percent );
        }
        return;
    }

#if USE_MVD_CLIENT  
    if( sv_running->integer != ss_broadcast ) {
        return;
    }

    if( ( percent = MVD_GetDemoPercent() ) == -1 ) {
        return;
    }

    draw_percent_bar( percent );
#endif
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

static char     scr_centerstring[MAX_STRING_CHARS];
static unsigned scr_centertime_start;   // for slow victory printing
static int      scr_center_lines;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint( const char *str ) {
    const char  *s;

    scr_centertime_start = cls.realtime;
    if( !strcmp( scr_centerstring, str ) ) {
        return;
    }

    Q_strlcpy( scr_centerstring, str, sizeof( scr_centerstring ) );

    // count the number of lines for centering
    scr_center_lines = 1;
    s = str;
    while( *s ) {
        if( *s == '\n' )
            scr_center_lines++;
        s++;
    }

    // echo it to the console
    Com_Printf( "%s\n", scr_centerstring );
    Con_ClearNotify_f();
}

static void draw_center_string( void ) {
    int y;
    float alpha;

    Cvar_ClampValue( scr_centertime, 0.3f, 10.0f );

    alpha = SCR_FadeAlpha( scr_centertime_start, scr_centertime->value * 1000, 300 );
    if( !alpha ) {
        return;
    }

    R_SetColor( DRAW_COLOR_ALPHA, ( byte * )&alpha );

    y = scr.hud_height / 4 - scr_center_lines * 8 / 2;

    SCR_DrawStringMulti( scr.hud_width / 2, y, UI_CENTER,
        MAX_STRING_CHARS, scr_centerstring, scr.font_pic );

    R_SetColor( DRAW_COLOR_CLEAR, NULL );
}

/*
===============================================================================

LAGOMETER

===============================================================================
*/

#define LAG_WIDTH     48
#define LAG_HEIGHT    48

#define LAG_CRIT_BIT    ( 1 << 31 )
#define LAG_WARN_BIT    ( 1 << 30 )

#define LAG_BASE    0xD5
#define LAG_WARN    0xDC
#define LAG_CRIT    0xF2

static struct {
    unsigned samples[LAG_WIDTH];
    unsigned head;
} lag;

void SCR_LagClear( void ) {
    lag.head = 0;
}

void SCR_LagSample( void ) {
    int i = cls.netchan->incoming_acknowledged & CMD_MASK;
    client_history_t *h = &cl.history[i];
    unsigned ping;

    h->rcvd = cls.realtime;
    if( !h->cmdNumber || h->rcvd < h->sent ) {
        return;
    }

    ping = h->rcvd - h->sent;
    for( i = 0; i < cls.netchan->dropped; i++ ) {
        lag.samples[lag.head % LAG_WIDTH] = ping | LAG_CRIT_BIT;
        lag.head++;
    }

    if( cl.frameflags & FF_SURPRESSED ) {
        ping |= LAG_WARN_BIT;
    }
    lag.samples[lag.head % LAG_WIDTH] = ping;
    lag.head++;
}

static void draw_ping_graph( int x, int y ) {
    int i, j, v, c, max = Cvar_ClampInteger( scr_lag_max, 16, 480 );

    for( i = 0; i < LAG_WIDTH; i++ ) {
        j = lag.head - i - 1;
        if( j < 0 ) {
            break;
        }

        v = lag.samples[j % LAG_WIDTH];

        if( v & LAG_CRIT_BIT ) {
            c = LAG_CRIT;
        } else if( v & LAG_WARN_BIT ) {
            c = LAG_WARN;
        } else {
            c = LAG_BASE;
        }

        v &= ~(LAG_WARN_BIT|LAG_CRIT_BIT);
        v = v * LAG_HEIGHT / max;
        if( v > LAG_HEIGHT ) {
            v = LAG_HEIGHT;
        }

        R_DrawFill( x + LAG_WIDTH - i - 1, y + LAG_HEIGHT - v, 1, v, c );
    }
}

static void draw_lagometer( void ) {
    int x = scr_lag_x->integer;
    int y = scr_lag_y->integer;

    if( x < 0 ) {
        x += scr.hud_width - LAG_WIDTH + 1;
    }
    if( y < 0 ) {
        y += scr.hud_height - LAG_HEIGHT + 1;
    }

    // draw ping graph
    if( scr_lag_draw->integer ) {
        if( scr_lag_draw->integer > 1 ) {
            R_DrawFill( x, y, LAG_WIDTH, LAG_HEIGHT, 4 );
        }
        draw_ping_graph( x, y );
    }

    // draw phone jack
    if( cls.netchan && cls.netchan->outgoing_sequence - cls.netchan->incoming_acknowledged >= CMD_BACKUP ) {
        if( ( cls.realtime >> 8 ) & 3 ) {
            R_DrawStretchPic( x, y, LAG_WIDTH, LAG_HEIGHT, scr.net_pic );
        }
    }
}


/*
===============================================================================

DRAW OBJECTS

===============================================================================
*/

typedef struct {
    list_t  entry;
    int     x, y;
    cvar_t *cvar;
    cmd_macro_t *macro;
    int flags;
    color_t color;
} drawobj_t;

#define FOR_EACH_DRAWOBJ( obj ) \
    LIST_FOR_EACH( drawobj_t, obj, &scr_objects, entry )
#define FOR_EACH_DRAWOBJ_SAFE( obj, next ) \
    LIST_FOR_EACH_SAFE( drawobj_t, obj, next, &scr_objects, entry )

static LIST_DECL( scr_objects );

static void SCR_Color_g( genctx_t *ctx ) {
    int color;

    for( color = 0; color < 10; color++ ) {
        if( !Prompt_AddMatch( ctx, colorNames[color] ) ) {
            break;
        }
    }
}

static void SCR_Draw_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        Cvar_Variable_g( ctx );
        Cmd_Macro_g( ctx );
    } else if( argnum == 4 ) {
        SCR_Color_g( ctx );
    }
}

// draw cl_fps -1 80
static void SCR_Draw_f( void ) {
    int x, y;
    const char *s, *c;
    drawobj_t *obj;
    cmd_macro_t *macro;
   // int stat;
    color_t color = { 0, 0, 0, 0 };
    int flags = UI_IGNORECOLOR;
    int argc = Cmd_Argc();

    if( argc == 1 ) {
        if( LIST_EMPTY( &scr_objects ) ) {
            Com_Printf( "No draw strings registered.\n" );
            return;
        }
        Com_Printf( "Name               X    Y\n"
                    "--------------- ---- ----\n" );
        FOR_EACH_DRAWOBJ( obj ) {
            s = obj->macro ? obj->macro->name : obj->cvar->name;
            Com_Printf( "%-15s %4d %4d\n", s, obj->x, obj->y );
        }
        return;
    }

    if( argc < 4 ) {
        Com_Printf( "Usage: %s <name> <x> <y> [color]\n", Cmd_Argv( 0 ) );
        return;
    }

    s = Cmd_Argv( 1 );
    x = atoi( Cmd_Argv( 2 ) );
    if( x < 0 ) {
        flags |= UI_RIGHT;
    }
    y = atoi( Cmd_Argv( 3 ) );

    if( argc > 4 ) {
        c = Cmd_Argv( 4 );
        if( !strcmp( c, "alt" ) ) {
            flags |= UI_ALTCOLOR;
        } else {
            if( !SCR_ParseColor( c, color ) ) {
                Com_Printf( "Unknown color '%s'\n", c );
                return;
            }
            flags &= ~UI_IGNORECOLOR;
        }
    }

    obj = Z_Malloc( sizeof( *obj ) );
    obj->x = x;
    obj->y = y;
    obj->flags = flags;
    FastColorCopy( color, obj->color );

    macro = Cmd_FindMacro( s );
    if( macro ) {
        obj->cvar = NULL;
        obj->macro = macro;
    } else {
        obj->cvar = Cvar_Ref( s );
        obj->macro = NULL;
    }

    List_Append( &scr_objects, &obj->entry );
}

static void SCR_Draw_g( genctx_t *ctx ) {
    drawobj_t *obj;
    const char *s;

    if( LIST_EMPTY( &scr_objects ) ) {
        return;
    }

    Prompt_AddMatch( ctx, "all" );
    
    FOR_EACH_DRAWOBJ( obj ) {
        s = obj->macro ? obj->macro->name : obj->cvar->name;
        if( !Prompt_AddMatch( ctx, s ) ) {
            break;
        }
    }
}

static void SCR_UnDraw_c( genctx_t *ctx, int argnum ) {
    if( argnum == 1 ) {
        SCR_Draw_g( ctx );
    }
}

static void SCR_UnDraw_f( void ) {
    char *s;
    drawobj_t *obj, *next;
    cmd_macro_t *macro;
    cvar_t *cvar;
    qboolean deleted;

    if( Cmd_Argc() != 2 ) {
        Com_Printf( "Usage: %s <name>\n", Cmd_Argv( 0 ) );
        return;
    }

    if( LIST_EMPTY( &scr_objects ) ) {
        Com_Printf( "No draw strings registered.\n" );
        return;
    }

    s = Cmd_Argv( 1 );
    if( !strcmp( s, "all" ) ) {
        FOR_EACH_DRAWOBJ_SAFE( obj, next ) {
            Z_Free( obj );
        }
        List_Init( &scr_objects );
        Com_Printf( "Deleted all draw strings.\n" );
        return;
    }

    cvar = NULL;
    macro = Cmd_FindMacro( s );
    if( !macro ) {
        cvar = Cvar_Ref( s );
    }

    deleted = qfalse;
    FOR_EACH_DRAWOBJ_SAFE( obj, next ) {
        if( obj->macro == macro && obj->cvar == cvar ) {
            List_Remove( &obj->entry );
            Z_Free( obj );
            deleted = qtrue;
        }
    }

    if( !deleted ) {
        Com_Printf( "Draw string '%s' not found.\n", s );
    }
}

static void draw_objects( void ) {
    char buffer[MAX_QPATH];
    int x, y;
    drawobj_t *obj;

    FOR_EACH_DRAWOBJ( obj ) {
        x = obj->x;
        y = obj->y;
        if( x < 0 ) {
            x += scr.hud_width + 1;
        }
        if( y < 0 ) {
            y += scr.hud_height - CHAR_HEIGHT + 1;
        }
        if( !( obj->flags & UI_IGNORECOLOR ) ) {
            R_SetColor( DRAW_COLOR_RGBA, obj->color );
        }
        if( obj->macro ) {
            obj->macro->function( buffer, sizeof( buffer ) );
            SCR_DrawString( x, y, obj->flags, buffer );
        } else {
            SCR_DrawString( x, y, obj->flags, obj->cvar->string );
        }
        R_SetColor( DRAW_COLOR_CLEAR, NULL );
    }
}

/*
===============================================================================

DEBUG STUFF

===============================================================================
*/

static void draw_turtle( void ) {
    int x = 8;
    int y = scr.hud_height - 88;

#define DF( f ) if( cl.frameflags & FF_ ## f ) { \
                    SCR_DrawString( x, y, UI_ALTCOLOR, #f ); \
                    y += 8; \
                }

    DF( SURPRESSED )
    DF( CLIENTPRED )
    else
    DF( CLIENTDROP )
    DF( SERVERDROP )
    DF( BADFRAME )
    DF( OLDFRAME )
    DF( OLDENT )
    DF( NODELTA )

#undef DF
}

#ifdef _DEBUG

static void draw_stats( void ) {
    char buffer[MAX_QPATH];
    int i, j;
    int x, y;

    j = scr_showstats->integer;
    if( j > MAX_STATS ) {
        j = MAX_STATS;
    }
    x = CHAR_WIDTH;
    y = ( scr.hud_height - j * CHAR_HEIGHT ) / 2;
    for( i = 0; i < j; i++ ) {
        Q_snprintf( buffer, sizeof( buffer ), "%2d: %d", i, cl.frame.ps.stats[i] );
        if( cl.oldframe.ps.stats[i] != cl.frame.ps.stats[i] ) {
            R_SetColor( DRAW_COLOR_RGBA, colorRed );
        }
        R_DrawString( x, y, 0, MAX_STRING_CHARS, buffer, scr.font_pic );
        R_SetColor( DRAW_COLOR_CLEAR, NULL );
        y += CHAR_HEIGHT;
    }
}

static void draw_pmove( void ) {
    static const char * const types[] = {
        "NORMAL", "SPECTATOR", "DEAD", "GIB", "FREEZE"
    };
    static const char * const flags[] = {
        "DUCKED", "JUMP_HELD", "ON_GROUND",
        "TIME_WATERJUMP", "TIME_LAND", "TIME_TELEPORT",
        "NO_PREDICTION", "TELEPORT_BIT"
    };
    int x = CHAR_WIDTH;
    int y = ( scr.hud_height - 2 * CHAR_HEIGHT ) / 2;
    unsigned i, j;

    i = cl.frame.ps.pmove.pm_type;
    if( i > PM_FREEZE ) {
        i = PM_FREEZE;
    }
    R_DrawString( x, y, 0, MAX_STRING_CHARS, types[i], scr.font_pic );
    y += CHAR_HEIGHT;

    j = cl.frame.ps.pmove.pm_flags;
    for( i = 0; i < 8; i++ ) {
        if( j & ( 1 << i ) ) {
            x = R_DrawString( x, y, 0, MAX_STRING_CHARS, flags[i], scr.font_pic );
            x += CHAR_WIDTH;
        }
    }
}

#endif

//============================================================================

// Sets scr_vrect, the coordinates of the rendered window
static void calc_vrect( void ) {
    int     size;

    // bound viewsize
    size = Cvar_ClampInteger( scr_viewsize, 40, 100 );
    scr_viewsize->modified = qfalse;

    scr_vrect.width = scr.hud_width * size / 100;
    scr_vrect.width &= ~7;

    scr_vrect.height = scr.hud_height * size / 100;
    scr_vrect.height &= ~1;

    scr_vrect.x = ( scr.hud_width - scr_vrect.width ) / 2;
    scr_vrect.y = ( scr.hud_height - scr_vrect.height ) / 2;
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
static void SCR_SizeUp_f( void ) {
    Cvar_SetInteger( scr_viewsize, scr_viewsize->integer + 10, FROM_CONSOLE );
}

/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void SCR_SizeDown_f( void ) {
    Cvar_SetInteger( scr_viewsize, scr_viewsize->integer - 10, FROM_CONSOLE );
}

/*
=================
SCR_Sky_f

Set a specific sky and rotation speed
=================
*/
static void SCR_Sky_f( void ) {
    float   rotate = 0;
    vec3_t  axis = { 0, 0, 1 };
    int     argc = Cmd_Argc();

    if( argc < 2 ) {
        Com_Printf ("Usage: sky <basename> [rotate] [axis x y z]\n");
        return;
    }

    if( argc > 2 )
        rotate = atof(Cmd_Argv(2));
    if( argc == 6 ) {
        axis[0] = atof(Cmd_Argv(3));
        axis[1] = atof(Cmd_Argv(4));
        axis[2] = atof(Cmd_Argv(5));
    }

    R_SetSky (Cmd_Argv(1), rotate, axis);
}

/*
================
SCR_TimeRefresh_f
================
*/
static void SCR_TimeRefresh_f (void) {
    int     i;
    unsigned    start, stop;
    float       time;

    if( cls.state != ca_active ) {
        Com_Printf( "No map loaded.\n" );
        return;
    }

    start = Sys_Milliseconds ();

    if (Cmd_Argc() == 2) {
        // run without page flipping
        R_BeginFrame();
        for (i=0 ; i<128 ; i++) {
            cl.refdef.viewangles[1] = i/128.0f*360.0f;
            R_RenderFrame (&cl.refdef);
        }
        R_EndFrame();
    } else {
        for (i=0 ; i<128 ; i++) {
            cl.refdef.viewangles[1] = i/128.0f*360.0f;

            R_BeginFrame();
            R_RenderFrame (&cl.refdef);
            R_EndFrame();
        }
    }

    stop = Sys_Milliseconds();
    time = (stop-start)*0.001f;
    Com_Printf ("%f seconds (%f fps)\n", time, 128.0f/time);
}


//============================================================================

static void scr_crosshair_changed( cvar_t *self ) {
    char buffer[16];

    if( scr_crosshair->integer > 0 ) {
        Q_snprintf( buffer, sizeof( buffer ), "ch%i", scr_crosshair->integer );
        scr.crosshair_pic = R_RegisterPic( buffer );
        R_GetPicSize( &scr.crosshair_width, &scr.crosshair_height, scr.crosshair_pic );

        scr.crosshair_color[0] = (byte)(ch_red->value * 255);
        scr.crosshair_color[1] = (byte)(ch_green->value * 255);
        scr.crosshair_color[2] = (byte)(ch_blue->value * 255);
        scr.crosshair_color[3] = (byte)(ch_alpha->value * 255);
    } else {
        scr.crosshair_pic = 0;
    }
}

void SCR_ModeChanged( void ) {
    R_GetConfig( &scr_glconfig );
    IN_Activate();
#if USE_UI
    UI_ModeChanged();
#endif
    // video sync flag may have changed
    CL_UpdateFrameTimes();
}

/*
==================
SCR_RegisterMedia
==================
*/
void SCR_RegisterMedia( void ) {
    int     i, j;

    R_GetConfig( &scr_glconfig );

    for( i = 0; i < 2; i++ )
        for( j = 0; j < STAT_PICS; j++ )
            scr.sb_pics[i][j] = R_RegisterPic( sb_nums[i][j] );

    scr.inven_pic = R_RegisterPic( "inventory" );
    scr.field_pic = R_RegisterPic( "field_3" );

    scr.backtile_pic = R_RegisterPic( "backtile" );

    scr.pause_pic = R_RegisterPic( "pause" );
    R_GetPicSize( &scr.pause_width, &scr.pause_height, scr.pause_pic );

    scr.loading_pic = R_RegisterPic( "loading" );
    R_GetPicSize( &scr.loading_width, &scr.loading_height, scr.loading_pic );

    scr.net_pic = R_RegisterPic( "net" );
    scr.font_pic = R_RegisterFont( scr_font->string );

    scr_crosshair_changed( scr_crosshair );
}

static void scr_font_changed( cvar_t *self ) {
    scr.font_pic = R_RegisterFont( self->string );
}

static const cmdreg_t scr_cmds[] = {
    { "timerefresh", SCR_TimeRefresh_f },
    { "sizeup", SCR_SizeUp_f }, 
    { "sizedown", SCR_SizeDown_f },
    { "sky", SCR_Sky_f },
    { "draw", SCR_Draw_f, SCR_Draw_c },
    { "undraw", SCR_UnDraw_f, SCR_UnDraw_c },
    { NULL }
};

/*
==================
SCR_Init
==================
*/
void SCR_Init( void ) {
    scr_viewsize = Cvar_Get ("viewsize", "100", CVAR_ARCHIVE);
    scr_showpause = Cvar_Get ("scr_showpause", "1", 0);
    scr_centertime = Cvar_Get ("scr_centertime", "2.5", 0);
#ifdef _DEBUG
    scr_netgraph = Cvar_Get ("netgraph", "0", 0);
    scr_timegraph = Cvar_Get ("timegraph", "0", 0);
    scr_debuggraph = Cvar_Get ("debuggraph", "0", 0);
    scr_graphheight = Cvar_Get ("graphheight", "32", 0);
    scr_graphscale = Cvar_Get ("graphscale", "1", 0);
    scr_graphshift = Cvar_Get ("graphshift", "0", 0);
#endif
    scr_demobar = Cvar_Get( "scr_demobar", "1", CVAR_ARCHIVE );
    scr_font = Cvar_Get( "scr_font", "conchars", CVAR_ARCHIVE );
    scr_font->changed = scr_font_changed;
    scr_scale = Cvar_Get( "scr_scale", "1", CVAR_ARCHIVE );
    scr_crosshair = Cvar_Get ("crosshair", "0", CVAR_ARCHIVE);
    scr_crosshair->changed = scr_crosshair_changed;

    ch_red = Cvar_Get ("ch_red", "1", 0);
    ch_red->changed = scr_crosshair_changed;
    ch_green = Cvar_Get ("ch_green", "1", 0);
    ch_green->changed = scr_crosshair_changed;
    ch_blue = Cvar_Get ("ch_blue", "1", 0);
    ch_blue->changed = scr_crosshair_changed;
    ch_alpha = Cvar_Get ("ch_alpha", "1", 0);
    ch_alpha->changed = scr_crosshair_changed;

    scr_draw2d = Cvar_Get( "scr_draw2d", "2", 0 );
    scr_showturtle = Cvar_Get( "scr_showturtle", "1", 0 );
    scr_lag_x = Cvar_Get( "scr_lag_x", "-1", 0 );
    scr_lag_y = Cvar_Get( "scr_lag_y", "-1", 0 );
    scr_lag_draw = Cvar_Get( "scr_lag_draw", "0", 0 );
    scr_lag_max = Cvar_Get( "scr_lag_max", "200", 0 );
    scr_alpha = Cvar_Get( "scr_alpha", "1", 0 );
#ifdef _DEBUG
    scr_showstats = Cvar_Get( "scr_showstats", "0", 0 );
    scr_showpmove = Cvar_Get( "scr_showpmove", "0", 0 );
#endif

    Cmd_Register( scr_cmds );

    scr_glconfig.vidWidth = 640;
    scr_glconfig.vidHeight = 480;

    scr.initialized = qtrue;
}

void SCR_Shutdown( void ) {
    Cmd_Deregister( scr_cmds );
    scr.initialized = qfalse;
}

//=============================================================================

/*
================
SCR_BeginLoadingPlaque
================
*/
void SCR_BeginLoadingPlaque( void ) {
    if( !cls.state ) {
        return;
    }
    if( cls.disable_screen ) {
        return;
    }
#ifdef _DEBUG
    if( developer->integer ) {
        return;
    }
#endif
    // if at console or menu, don't bring up the plaque
    if( cls.key_dest & (KEY_CONSOLE|KEY_MENU) ) {
        return;
    }

    scr.draw_loading = qtrue;
    SCR_UpdateScreen();

    cls.disable_screen = Sys_Milliseconds();
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque( void ) {
    if( !cls.state ) {
        return;
    }
    cls.disable_screen = 0;
    Con_ClearNotify_f();
#if USE_CHATHUD
    SCR_ClearChatHUD_f();
#endif
}

// Clear any parts of the tiled background that were drawn on last frame
static void tile_clear( void ) {
    int top, bottom, left, right;

    //if( con.currentHeight == 1 )
    //  return;     // full screen console

    if( scr_viewsize->integer == 100 )
        return;     // full screen rendering

    top = scr_vrect.y;
    bottom = top + scr_vrect.height - 1;
    left = scr_vrect.x;
    right = left + scr_vrect.width - 1;
    
    // clear above view screen
    R_TileClear( 0, 0, scr_glconfig.vidWidth, top, scr.backtile_pic );

    // clear below view screen
    R_TileClear( 0, bottom, scr_glconfig.vidWidth,
        scr_glconfig.vidHeight - bottom, scr.backtile_pic );

    // clear left of view screen
    R_TileClear( 0, top, left, scr_vrect.height, scr.backtile_pic );
    
    // clear right of view screen
    R_TileClear( right, top, scr_glconfig.vidWidth - right,
        scr_vrect.height, scr.backtile_pic );
}

/*
===============================================================================

STAT PROGRAMS

===============================================================================
*/

#define ICON_WIDTH  24
#define ICON_HEIGHT 24
#define DIGIT_WIDTH 16
#define ICON_SPACE  8

#define HUD_DrawString( x, y, string ) \
    R_DrawString( x, y, 0, MAX_STRING_CHARS, string, scr.font_pic )

#define HUD_DrawAltString( x, y, string ) \
    R_DrawString( x, y, UI_ALTCOLOR, MAX_STRING_CHARS, string, scr.font_pic )

#define HUD_DrawCenterString( x, y, string ) \
    SCR_DrawStringMulti( x, y, UI_CENTER, MAX_STRING_CHARS, string, scr.font_pic )

#define HUD_DrawAltCenterString( x, y, string ) \
    SCR_DrawStringMulti( x, y, UI_CENTER|UI_ALTCOLOR, MAX_STRING_CHARS, string, scr.font_pic )

static void HUD_DrawNumber( int x, int y, int color, int width, int value ) {
    char    num[16], *ptr;
    int     l;
    int     frame;

    if( width < 1 )
        return;

    // draw number string
    if( width > 5 )
        width = 5;

    color &= 1;

    l = Q_scnprintf( num, sizeof( num ), "%i", value );
    if( l > width )
        l = width;
    x += 2 + DIGIT_WIDTH * ( width - l );

    ptr = num;
    while( *ptr && l ) {
        if( *ptr == '-' )
            frame = STAT_MINUS;
        else
            frame = *ptr - '0';

        R_DrawPic( x, y, scr.sb_pics[color][frame] );
        x += DIGIT_WIDTH;
        ptr++;
        l--;
    }
}

#define DISPLAY_ITEMS   17

static void draw_inventory( void ) {
    int     i;
    int     num, selected_num, item;
    int     index[MAX_ITEMS];
    char    string[MAX_STRING_CHARS];
    int     x, y;
    char    *bind;
    int     selected;
    int     top;

    selected = cl.frame.ps.stats[STAT_SELECTED_ITEM];

    num = 0;
    selected_num = 0;
    for( i = 0; i < MAX_ITEMS; i++ ) {
        if( i == selected ) {
            selected_num = num;
        }
        if( cl.inventory[i] ) {
            index[num++] = i;
        }
    }

    // determine scroll point
    top = selected_num - DISPLAY_ITEMS / 2;
    if( top > num - DISPLAY_ITEMS ) {
        top = num - DISPLAY_ITEMS;
    }
    if( top < 0 ) {
        top = 0;
    }

    x = ( scr.hud_width - 256 ) / 2;
    y = ( scr.hud_height - 240 ) / 2;

    R_DrawPic( x, y + 8, scr.inven_pic );
    y += 24;
    x += 24;

    HUD_DrawString( x, y, "hotkey ### item" );
    y += CHAR_HEIGHT;

    HUD_DrawString( x, y, "------ --- ----" );
    y += CHAR_HEIGHT;

    for( i = top; i < num && i < top + DISPLAY_ITEMS; i++ ) {
        item = index[i];
        // search for a binding
        Q_concat( string, sizeof( string ),
            "use ", cl.configstrings[CS_ITEMS + item], NULL );
        bind = Key_GetBinding( string );

        Q_snprintf( string, sizeof( string ), "%6s %3i %s",
            bind, cl.inventory[item], cl.configstrings[CS_ITEMS + item] );
        
        if( item != selected ) {
            HUD_DrawAltString( x, y, string );
        } else {    // draw a blinky cursor by the selected item
            HUD_DrawString( x, y, string );
            if( ( cls.realtime >> 8 ) & 1 ) {
                R_DrawChar( x - CHAR_WIDTH, y, 0, 15, scr.font_pic );
            }
        }
        
        y += CHAR_HEIGHT;
    }
}

static void draw_layout_string( const char *s ) {
    char    buffer[MAX_QPATH];
    int     x, y;
    int     value;
    char    *token;
    int     width;
    int     index;
    clientinfo_t    *ci;

    if( !s[0] )
        return;

    x = 0;
    y = 0;
    width = 3;

    while( s ) {
        token = COM_Parse( &s );
        if( token[2] == 0 ) {
            if( token[0] == 'x' ) {
                if( token[1] == 'l' ) {
                    token = COM_Parse( &s );
                    x = atoi( token );
                    continue;
                }

                if( token[1] == 'r' ) {
                    token = COM_Parse( &s );
                    x = scr.hud_width + atoi( token );
                    continue;
                }

                if( token[1] == 'v' ) {
                    token = COM_Parse( &s );
                    x = scr.hud_width / 2 - 160 + atoi( token );
                    continue;
                }
            }

            if( token[0] == 'y' ) {
                if( token[1] == 't' ) {
                    token = COM_Parse( &s );
                    y = atoi( token );
                    continue;
                }

                if( token[1] == 'b' ) {
                    token = COM_Parse( &s );
                    y = scr.hud_height + atoi( token );
                    continue;
                }

                if( token[1] == 'v' ) {
                    token = COM_Parse( &s );
                    y = scr.hud_height / 2 - 120 + atoi( token );
                    continue;
                }
            }
        }

        if( !strcmp( token, "pic" ) ) { 
            // draw a pic from a stat number
            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_STATS ) {
                Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
            }
            value = cl.frame.ps.stats[value];
            if( value < 0 || value >= MAX_IMAGES ) {
                Com_Error( ERR_DROP, "%s: invalid pic index", __func__ );
            }
            token = cl.configstrings[CS_IMAGES + value];
            if( token[0] ) {
                R_DrawPic( x, y, R_RegisterPic( token ) );
            }
            continue;
        }

        if( !strcmp( token, "client" ) ) {  
            // draw a deathmatch client block
            int     score, ping, time;

            token = COM_Parse( &s );
            x = scr.hud_width / 2 - 160 + atoi( token );
            token = COM_Parse( &s );
            y = scr.hud_height / 2 - 120 + atoi( token );

            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_CLIENTS ) {
                Com_Error( ERR_DROP, "%s: invalid client index", __func__ );
            }
            ci = &cl.clientinfo[value];

            token = COM_Parse( &s );
            score = atoi( token );

            token = COM_Parse( &s );
            ping = atoi( token );

            token = COM_Parse( &s );
            time = atoi( token );

            HUD_DrawString( x + 32, y, ci->name );
            Q_snprintf( buffer, sizeof( buffer ), "Score: %i", score ); 
            HUD_DrawString( x + 32, y + CHAR_HEIGHT, buffer );
            Q_snprintf( buffer, sizeof( buffer ), "Ping:  %i", ping ); 
            HUD_DrawString( x + 32, y + 2 * CHAR_HEIGHT, buffer );
            Q_snprintf( buffer, sizeof( buffer ), "Time:  %i", time ); 
            HUD_DrawString( x + 32, y + 3 * CHAR_HEIGHT, buffer );

            if( !ci->icon ) {
                ci = &cl.baseclientinfo;
            }
            R_DrawPic( x, y, ci->icon );
            continue;
        }

        if( !strcmp( token, "ctf" ) ) { 
            // draw a ctf client block
            int     score, ping;

            token = COM_Parse( &s );
            x = scr.hud_width / 2 - 160 + atoi( token );
            token = COM_Parse( &s );
            y = scr.hud_height / 2 - 120 + atoi( token );

            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_CLIENTS ) {
                Com_Error( ERR_DROP, "%s: invalid client index", __func__ );
            }
            ci = &cl.clientinfo[value];

            token = COM_Parse( &s );
            score = atoi( token );

            token = COM_Parse( &s );
            ping = atoi( token );
            if( ping > 999 )
                ping = 999;

            Q_snprintf( buffer, sizeof( buffer ), "%3d %3d %-12.12s",
                score, ping, ci->name );
            if( value == cl.frame.clientNum ) {
                HUD_DrawAltString( x, y, buffer );
            } else {
                HUD_DrawString( x, y, buffer );
            }
            continue;
        }

        if( !strcmp( token, "picn" ) ) {    
            // draw a pic from a name
            token = COM_Parse( &s );
            R_DrawPic( x, y, R_RegisterPic( token ) );
            continue;
        }

        if( !strcmp( token, "num" ) ) { 
            // draw a number
            token = COM_Parse( &s );
            width = atoi( token );
            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_STATS ) {
                Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
            }
            value = cl.frame.ps.stats[value];
            HUD_DrawNumber( x, y, 0, width, value );
            continue;
        }

        if( !strcmp( token, "hnum" ) ) {    
            // health number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_HEALTH];
            if( value > 25 )
                color = 0;  // green
            else if( value > 0 )
                color = ( cl.frame.number >> 2 ) & 1;       // flash
            else
                color = 1;

            if( cl.frame.ps.stats[STAT_FLASHES] & 1 )
                R_DrawPic( x, y, scr.field_pic );

            HUD_DrawNumber( x, y, color, width, value );
            continue;
        }

        if( !strcmp( token, "anum" ) ) {    
            // ammo number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_AMMO];
            if( value > 5 )
                color = 0;  // green
            else if( value >= 0 )
                color = ( cl.frame.number >> 2 ) & 1;       // flash
            else
                continue;   // negative number = don't show

            if( cl.frame.ps.stats[STAT_FLASHES] & 4 )
                R_DrawPic( x, y, scr.field_pic );

            HUD_DrawNumber( x, y, color, width, value );
            continue;
        }

        if( !strcmp( token, "rnum" ) ) {    
            // armor number
            int     color;

            width = 3;
            value = cl.frame.ps.stats[STAT_ARMOR];
            if( value < 1 )
                continue;

            color = 0;  // green

            if( cl.frame.ps.stats[STAT_FLASHES] & 2 )
                R_DrawPic( x, y, scr.field_pic );

            HUD_DrawNumber( x, y, color, width, value );
            continue;
        }

        if( !strcmp( token, "stat_string" ) ) {
            token = COM_Parse( &s );
            index = atoi( token );
            if( index < 0 || index >= MAX_STATS ) {
                Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
            }
            index = cl.frame.ps.stats[index];
            if( index < 0 || index >= MAX_CONFIGSTRINGS ) {
                Com_Error( ERR_DROP, "%s: invalid string index", __func__ );
            }
            HUD_DrawString( x, y, cl.configstrings[index] );
            continue;
        }

        if( !strcmp( token, "cstring" ) ) {
            token = COM_Parse( &s );
            HUD_DrawCenterString( x + 320 / 2, y, token );
            continue;
        }

        if( !strcmp( token, "cstring2" ) ) {
            token = COM_Parse( &s );
            HUD_DrawAltCenterString( x + 320 / 2, y, token );
            continue;
        }

        if( !strcmp( token, "string" ) ) {
            token = COM_Parse( &s );
            HUD_DrawString( x, y, token );
            continue;
        }

        if( !strcmp( token, "string2" ) ) {
            token = COM_Parse( &s );
            HUD_DrawAltString( x, y, token );
            continue;
        }

        if( !strcmp( token, "if" ) ) {
            token = COM_Parse( &s );
            value = atoi( token );
            if( value < 0 || value >= MAX_STATS ) {
                Com_Error( ERR_DROP, "%s: invalid stat index", __func__ );
            }
            value = cl.frame.ps.stats[value];
            if( !value ) {  // skip to endif
                while( strcmp( token, "endif" ) ) {
                    token = COM_Parse( &s );
                    if( !s ) {
                        break;
                    }
                }
            }
            continue;
        }
    }
}

static void draw_pause( void ) {
    int     x, y;

    if( !sv_paused->integer ) {
        return;
    }
    if( !cl_paused->integer ) {
        return;
    }

    if( !scr_showpause->integer ) {     // turn off for screenshots
        return;
    }

    x = ( scr.hud_width - scr.pause_width ) / 2;
    y = ( scr.hud_height - scr.pause_height ) / 2;
    R_DrawPic( x, y, scr.pause_pic );
}

static void draw_loading( void ) {
    int x = ( scr_glconfig.vidWidth - scr.loading_width ) / 2;
    int y = ( scr_glconfig.vidHeight - scr.loading_height ) / 2;

    R_DrawPic( x, y, scr.loading_pic );
}

static void draw_crosshair( void ) {
    int x = ( scr.hud_width - scr.crosshair_width ) / 2;
    int y = ( scr.hud_height - scr.crosshair_height ) / 2;

    R_SetColor( DRAW_COLOR_RGBA, scr.crosshair_color );
    R_DrawPic( x, y, scr.crosshair_pic );
    R_SetColor( DRAW_COLOR_CLEAR, NULL );
}

static void draw_2d( void ) {
#if USE_REF == REF_SOFT
    clipRect_t rc;

    // avoid DoS by making sure nothing is drawn out of bounds
    rc.left = 0;
    rc.top = 0;
    rc.right = scr.hud_width;
    rc.bottom = scr.hud_height;

    R_SetClipRect( DRAW_CLIP_MASK, &rc );
#endif

    R_SetColor( DRAW_COLOR_CLEAR, NULL );

    if( scr_crosshair->integer ) {
        draw_crosshair();
    }

    Cvar_ClampValue( scr_alpha, 0, 1 );
    R_SetColor( DRAW_COLOR_ALPHA, ( byte * )&scr_alpha->value );

    if( scr_draw2d->integer > 1 ) {
        draw_layout_string( cl.configstrings[CS_STATUSBAR] );
    }

    if( ( cl.frame.ps.stats[STAT_LAYOUTS] & 1 ) ||
        ( cls.demo.playback && Key_IsDown( K_F1 ) ) )
    {
        draw_layout_string( cl.layout );
    }

    if( cl.frame.ps.stats[STAT_LAYOUTS] & 2 ) {
        draw_inventory();
    }

    draw_center_string();

    draw_objects();

    draw_lagometer();

    R_SetColor( DRAW_COLOR_CLEAR, NULL );

    if( scr_showturtle->integer && cl.frameflags ) {
        draw_turtle();
    }

#ifdef _DEBUG
    if( scr_showstats->integer ) {
        draw_stats();
    }
    if( scr_showpmove->integer ) {
        draw_pmove();
    }
#endif

    draw_pause();

#if USE_REF == REF_SOFT
    R_SetClipRect( DRAW_CLIP_DISABLED, NULL );
#endif
}

static void draw_active_frame( void ) {
    float scale;

    if( cls.state < ca_active ) {
        // draw black background if not active
        R_DrawFill( 0, 0, scr_glconfig.vidWidth,
            scr_glconfig.vidHeight, 0 );
        return;
    }

    scr.hud_height = scr_glconfig.vidHeight;
    scr.hud_width = scr_glconfig.vidWidth;

    draw_demo_bar();

    calc_vrect();

    // clear any dirty part of the background
    tile_clear();

    // draw 3D game view
    V_RenderView();

    if( scr_scale->value != 1 ) {
        scale = 1.0f / Cvar_ClampValue( scr_scale, 1, 9 );
        R_SetScale( &scale );

        scr.hud_height *= scale;
        scr.hud_width *= scale;
    }

    // draw all 2D elements
    if( scr_draw2d->integer && !( cls.key_dest & KEY_MENU ) ) {
        draw_2d();
    }

    R_SetScale( NULL );
}

//=======================================================

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen( void ) {
    static int recursive;

    if( !scr.initialized ) {
        return;             // not initialized yet
    }

    // if the screen is disabled (loading plaque is up), do nothing at all
    if( cls.disable_screen ) {
        unsigned delta = Sys_Milliseconds() - cls.disable_screen;

        if( delta < 120*1000 ) {
            return;
        }

        cls.disable_screen = 0;
        Com_Printf( "Loading plaque timed out.\n" );
    }

    if( recursive > 1 ) {
        Com_Error( ERR_FATAL, "%s: recursively called", __func__ );
    }

    recursive++;

    R_BeginFrame();

#if USE_UI
    if( UI_IsTransparent() ) {
        // do 3D refresh drawing
        draw_active_frame();
    }

    // draw main menu
    UI_Draw( cls.realtime );
#else
    // do 3D refresh drawing
    draw_active_frame();
#endif

    // draw console
    Con_DrawConsole();

    // draw loading plaque
    if( scr.draw_loading ) {
        draw_loading();
        scr.draw_loading = qfalse;
    }

#ifdef _DEBUG
    // draw debug graphs
    if( scr_timegraph->integer )
        SCR_DebugGraph( cls.frametime*300, 0 );

    if( scr_debuggraph->integer || scr_timegraph->integer || scr_netgraph->integer ) {
        SCR_DrawDebugGraph();
    }
#endif

    R_EndFrame();

    recursive--;
}

