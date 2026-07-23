#include <pebble.h> //einbinden vom SDK

#include "mytypes.h" //einbinden von Hilfsmakros
#include "rand.h"  //selbstgeschriebener Zufallsgenerator

Window *glbWindowP;  //globaler Pointer auf das Hauptfenster der App
Layer *glbBlobLayerP;  //Pointer auf die Zeichenebene, auf der die Blobs gerendert werden.

bool glbLive  = false; //merkt sich, ob die schnelle Animation noch laeuft (batterieeffizienz)

#define WIDTH 144  //aufloesung der alten Pebble time
#define HEIGHT 168  //aufloesung der alten Pebble time
	
typedef struct //strukt mit 2 ganzzahlen
{
	int	x, y;
} PT2;

void handle_timer(void *data);  //Es gibt später eine Funktion namens handle_timer

#define FIXBITS 10  //zehn Bits für den Nachkommateil reserviert. Eine normale Zahl wird intern mit 1024 multipliziert

#define FIXMULT(a, b) (( (a)*(b) ) >> FIXBITS)  //Multipliziert zwei Fixed-Point-Zahlen. Nach der Multiplikation muss das Ergebnis wieder um zehn Bits zurückgeschoben werden
#define FIX2INT(a) ( ((a) + (1<<(FIXBITS-1))) >> FIXBITS)  //Wandelt Fixed Point zurück in eine Ganzzahl
#define INT2FIX(a) ((a) << FIXBITS)  //Wandelt eine Ganzzahl in Fixed Point um

void pt_add(PT2 *a, PT2 b)  //Diese Funktion addiert b zu a
{
	a->x += b.x;
	a->y += b.y;
}

void pt_sub(PT2 *a, PT2 b)  //Dasselbe Prinzip, nur als Subtraktion
{
	a->x -= b.x;
	a->y -= b.y;
}

void pt_mul(PT2 *a, PT2 b)  //Hier werden die X- und Y-Komponenten jeweils miteinander multipliziert
{
	a->x = FIXMULT(a->x, b.x);
	a->y = FIXMULT(a->y, b.y);
}

int pt_normalize(PT2 *a)  //Diese Funktion verkleinert einen Richtungsvektor auf eine standardisierte Länge
{
	// There exists a norm in which this makes sense.
	int xdist = ABS(a->x);
	int ydist = ABS(a->y);
	if (!xdist && !ydist)
		return 0;
	
	if (xdist < ydist)
	{
		int scale = INT2FIX(1) / ydist;
		a->x = FIXMULT(a->x, scale);
		if (a->y < 0)
			a->y = INT2FIX(-1);
		else
			a->y = INT2FIX(1);
		return scale;
	}
	else
	{
		int scale = INT2FIX(1) / xdist;
		a->y = FIXMULT(a->y, scale);
		if (a->x < 0)
			a->x = INT2FIX(-1);
		else
			a->x = INT2FIX(1);
		return scale;
	}
}
	
typedef struct //Ein PART besitzt position (x,y) und geschwindigkeit (x,y)
{
	PT2		pos;
	PT2		vel;
} PART;

#define NUM_PART 10  //Es gibt genau zehn Partikel
	
PART		glbPart[NUM_PART];

#define KERNEL_RAD 40  //Jedes Partikel beeinflusst Pixel in einem Radius von maximal 40 Pixeln
	
#define REFRESH_RATE 50  //Während der Animation erfolgt ungefähr alle 50 Millisekunden ein Update (20 bilder\sek)
#define INTEGRATE_TIMER_ID 1  //Aktuell nicht in verwendung
	
#define NUM_CLOCKBITS 10  //anzahl Clockbits
	
#define CENTER_X 72  //bildschirmmitte
#define CENTER_Y 84  //bildschirmmitte
#define GRID_SPACING 10  //Grid, mit dem die Blobs angeordnet werden
	
int glbTargetMinute = -1;  //speichert die zuletzt verarbeitete Minute. Der Startwert -1 ist absichtlich ungültig. Dadurch wird beim Start garantiert die aktuelle Uhrzeit verarbeitet
	
const PT2 glbTargets[NUM_CLOCKBITS] = //die blobpositionen im Grid
{
	// Hour bits, high to low
	{ CENTER_X - 4.5 * GRID_SPACING, CENTER_Y - 4 * GRID_SPACING },
	{ CENTER_X - 1.5 * GRID_SPACING, CENTER_Y - 6 * GRID_SPACING },
	{ CENTER_X + 1.5 * GRID_SPACING, CENTER_Y - 3 * GRID_SPACING },
	{ CENTER_X + 4.5 * GRID_SPACING, CENTER_Y - 5 * GRID_SPACING },
		
	// Minute bits, high to low
	{ CENTER_X - 5.5 * GRID_SPACING, CENTER_Y + 3 * GRID_SPACING },
	{ CENTER_X - 2.5 * GRID_SPACING, CENTER_Y + 1 * GRID_SPACING },
	{ CENTER_X - 1.5 * GRID_SPACING, CENTER_Y + 5 * GRID_SPACING },
	{ CENTER_X + 1.5 * GRID_SPACING, CENTER_Y + 5 * GRID_SPACING },
	{ CENTER_X + 2.5 * GRID_SPACING, CENTER_Y + 1 * GRID_SPACING },
	{ CENTER_X + 5.5 * GRID_SPACING, CENTER_Y + 3 * GRID_SPACING },
};

PT2 glbPartTargets[NUM_PART];  //Dieses Array enthält für jedes der zehn Partikel seine momentane Zielposition
	
const int glbBlinnKernel[KERNEL_RAD] =  //Lava-Lampen-Effekt
{
	2048,
	2037,
	2002,
	1946,
	1872,
	1779,
	1673,
	1555,
	1429,
	1298,
	1167,
	1037,
	911,
	791,
	680,
	577,
	485,
	403,
	331,
	269,
	245,
	216,
	171,
	134,
	104,
	80,
	61,
	45,
	34,
	25,
	18,
	13,
	9,
	6,
	4,
	3,
	2,
	1,
	1,
	0,
};

int
blinn(int dist)  //Einfluss nach Entfernung abrufen
{
	dist = FIX2INT(dist);
	if (dist >= KERNEL_RAD)
		return 0;
	return glbBlinnKernel[dist];
}

int metadist(PT2 a, PT2 b)  //Einfluss eines Partikels auf einen Pixel
{
	int adist = ABS(a.x - b.x);
	int bdist = ABS(a.y - b.y);
	return FIXMULT( blinn(adist), blinn(bdist) );  //Überschreitet die Summe einen Grenzwert, wird der Pixel schwarz. So verschmelzen die Partikel zu zusammenhängenden Blobs.
}

static int blob_plist[NUM_PART];  //Hilfsarrays für das Rendern
static int blob_plistx[NUM_PART];  //Hilfsarrays für das Rendern

void bloblayer_update(Layer *me, GContext *ctx)  //raw()-Methode von Pebble SDK. ab hier wird gezeichnet
{
	(void) me;  //vermutlich ueberfluessig
	
	// Useful for debugging that the timer shuts down
//	if (glbLive)
	if (1)  //immer true, daher ist else sinnlos
	{
		graphics_context_set_fill_color(ctx, GColorWhite);
		graphics_context_set_stroke_color(ctx, GColorBlack);
	}
	else
	{
		graphics_context_set_fill_color(ctx, GColorBlack);
		graphics_context_set_stroke_color(ctx, GColorWhite);
	}
	
	graphics_fill_rect(ctx, layer_get_frame(me), 0, 0);  //Hier wird die gesamte Zeichenfläche mit der aktuellen Füllfarbe ausgefüllt
	
	for (int y = 0; y < HEIGHT; y++)  //Der Bildschirm wird zeilenweise berechnet
	{
		int nlive = 0;  //Hier zählt es, wie viele Partikel für die aktuelle Y-Zeile relevant sind
		for (int part = 0; part < NUM_PART; part++)  //Jetzt werden alle zehn Partikel geprüft
		{
			int py = FIX2INT(glbPart[part].pos.y);
			py -= y;
			if (py < KERNEL_RAD && py > -KERNEL_RAD)
			{
				blob_plist[nlive++] = part;
			}
		}
		
		if (!nlive)  //Wenn kein Partikel diese Zeile beeinflussen kann, wird die restliche Berechnung für diese Zeile übersprungen
			continue;
		
		// Sort plist by x
		for (int i = 0; i < nlive-1; i++)  //Relevante Partikel nach X sortieren
		{
			for (int j = i+1; j < nlive; j++)
			{
				if (glbPart[blob_plist[i]].pos.x > glbPart[blob_plist[j]].pos.x)
				{
					// Out of place
					int tmp = blob_plist[j];
					blob_plist[j] = blob_plist[i];
					blob_plist[i] = tmp;
				}
			}
		}
		
		for (int i = 0; i < nlive; i++)
			blob_plistx[i] = FIX2INT(glbPart[blob_plist[i]].pos.x);
		
		int sx = blob_plistx[0] - KERNEL_RAD;  //Startpunkt der X-Schleife bestimmen
		if (sx < 0)
			sx = 0;
		
		int startidx, endidx;
		startidx = 0;
		endidx = startidx;
		
		for (int x = sx; x < WIDTH; x++)
		{
			// Update our search range!
			while (endidx < nlive)
			{
				if (blob_plistx[endidx] - KERNEL_RAD> x)
					break;
				endidx++;
			}
			while (startidx < nlive)
			{
				if (blob_plistx[startidx] + KERNEL_RAD > x)
					break;
				startidx++;
			}
			
			PT2		cpos;
			cpos.x = INT2FIX(x);
			cpos.y = INT2FIX(y);
			int		totaldist = 0;
			for (int part = startidx; part < endidx; part++)
			{
				totaldist += metadist(glbPart[blob_plist[part]].pos, cpos);
			}
			if (totaldist > INT2FIX(1))
				graphics_draw_pixel(ctx, GPoint(x, y));
		}
	}
}

void handle_init() 
{
	glbWindowP = window_create();
	
	window_stack_push(glbWindowP, true /* Animated */);
	window_set_background_color(glbWindowP, GColorWhite);
	
	glbBlobLayerP = layer_create(
		layer_get_frame(window_get_root_layer(glbWindowP)));
	layer_set_update_proc(glbBlobLayerP, &bloblayer_update);
	layer_add_child(window_get_root_layer(glbWindowP), glbBlobLayerP);
	
	rand_seed();
	for (int part = 0; part < NUM_PART; part++)
	{
		glbPart[part].pos.x = rand_choice(INT2FIX(WIDTH));
		glbPart[part].pos.y = rand_choice(INT2FIX(HEIGHT));
		glbPart[part].vel.x = INT2FIX(rand_range(-3, 3));
		glbPart[part].vel.y = INT2FIX(rand_range(-3, 3));
	}
	
	glbLive = true;
	app_timer_register(REFRESH_RATE, handle_timer, 0);
	
	layer_mark_dirty(glbBlobLayerP);
}

void handle_deinit() 
{
	window_destroy(glbWindowP);
	glbWindowP = 0;
	layer_destroy(glbBlobLayerP);
	glbBlobLayerP = 0;
}

void
part_bounce(PART *part)
{
	if (part->pos.x < 0)
	{
		part->vel.x = ABS(part->vel.x);
		part->pos.x = -part->pos.x;
	}
	else if (part->pos.x > INT2FIX(WIDTH))
	{
		part->vel.x = -ABS(part->vel.x);
		part->pos.x = INT2FIX(2*WIDTH)-part->pos.x;
	}
	if (part->pos.y < 0)
	{
		part->vel.y = ABS(part->vel.y);
		part->pos.y = -part->pos.y;
	}
	else if (part->pos.y > INT2FIX(HEIGHT))
	{
		part->vel.y = -ABS(part->vel.y);
		part->pos.y = INT2FIX(2*HEIGHT)-part->pos.y;
	}
}

void
part_forces(PART *part, int pidx)
{
	PT2 targetforce;
	targetforce = glbPartTargets[pidx];
	targetforce.x = INT2FIX(targetforce.x);
	targetforce.y = INT2FIX(targetforce.y);
	pt_sub(&targetforce, part->pos);
	int			len;
	len = MAX(ABS(targetforce.x), ABS(targetforce.y));
	pt_normalize(&targetforce);
	if (len < INT2FIX(4))
	{
		targetforce.x >>= 2;
		targetforce.y >>= 2;
	}
	
	pt_add(&part->vel, targetforce);
	
	PT2 dragforce;
	dragforce.x = 0.8 * (INT2FIX(1));
	dragforce.y = 0.8 * (INT2FIX(1));
	pt_mul(&part->vel, dragforce);
}

bool
part_integrate()
{
	// Integrate!
	for (int pidx= 0; pidx < NUM_PART; pidx++)
	{
		PART *part = &glbPart[pidx];
		pt_add(&part->pos, part->vel);
		
		part_bounce(part);
		
		part_forces(part, pidx);
		
//		part->pos.x = INT2FIX(glbPartTargets[pidx].x);
//		part->pos.y = INT2FIX(glbPartTargets[pidx].y);
	}
	
	// Check to see if we are on target!
	for (int pidx = 0; pidx < NUM_PART; pidx++)
	{
		if (FIX2INT(glbPart[pidx].pos.x) != glbPartTargets[pidx].x)
			return true;
		if (FIX2INT(glbPart[pidx].pos.y) != glbPartTargets[pidx].y)
			return true;
	}
	
	return false;
}

void
handle_tick(struct tm *tick_time, TimeUnits units_chnnged)
{
	// Set our targets.
	if (tick_time->tm_min == glbTargetMinute)
		return;
	
	glbTargetMinute = tick_time->tm_min;
	
	int 	ntarget = 0;
	static int		targets[NUM_CLOCKBITS];
	int hour = tick_time->tm_hour;
	
	if (hour > 12)
		hour -= 12;
	
	for (int bit = 0; bit < 4; bit++)
	{
		if (hour & (1 << (3-bit)))
			targets[ntarget++] = bit;
	}
	for (int bit = 0; bit < 6; bit++)
	{
		if (tick_time->tm_min & (1 << (5-bit)))
			targets[ntarget++] = bit + 4;
	}
	
	// Assign targets.
	// If no targets, it is midnight/noon, scatter!
	if (!ntarget)
	{
		for (int part = 0; part < NUM_PART; part++)
		{
			glbPartTargets[part].x = rand_choice((WIDTH));
			glbPartTargets[part].y = rand_choice((HEIGHT));
		}
	}
	else
	{
		// First assign each target to one particles.
		static int partlist[NUM_PART];
		for (int part = 0; part < NUM_PART; part++)
			partlist[part] = part;
		
		for (int i = 0; i < ntarget; i++)
		{
			// Find a random particle..
			int pidx = rand_choice(NUM_PART - i);
			
			// Swap...
			int tmp = partlist[pidx+i];
			partlist[pidx+i] = partlist[i];
			partlist[i] = tmp;
			
			glbPartTargets[partlist[i]] = glbTargets[targets[i]];
		}
		// Remaining particles get a random target.
		for (int i = ntarget; i < NUM_PART; i++)
		{
			int t = rand_choice(ntarget);
			glbPartTargets[partlist[i]] = glbTargets[targets[t]];
		}
	}
	
	if (!glbLive)
	{
		glbLive = true;
		app_timer_register(REFRESH_RATE, handle_timer, 0);
	}
	
	layer_mark_dirty(glbBlobLayerP);
}

void
handle_timer(void *data)
{
	glbLive = part_integrate();
		
	if (glbLive)
		app_timer_register(REFRESH_RATE, handle_timer, 0);
	layer_mark_dirty(glbBlobLayerP);
}


int 
main() 
{
	handle_init();
	tick_timer_service_subscribe(SECOND_UNIT, handle_tick);
	app_event_loop();
	handle_deinit();
}
