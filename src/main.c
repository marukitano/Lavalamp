#include <pebble.h> //einbinden vom SDK

#include "mytypes.h" //einbinden von Hilfsmakros
#include "rand.h"  //selbstgeschriebener Zufallsgenerator

Window *glbWindowP;  //globaler Pointer auf das Hauptfenster der App
Layer *glbBlobLayerP;  //Pointer auf die Zeichenebene, auf der die Blobs gerendert werden.

bool glbLive = false; //merkt sich, ob sich die Partikel noch bewegen
bool glbBlobsSettled = false; //true, sobald alle Blobs ihre Zielposition erreicht haben
bool glbTimerRunning = false; //verhindert, dass mehrere Animationstimer gleichzeitig laufen

// Helligkeit der Zahlen: 0 = unsichtbar, 255 = vollständig sichtbar.
int glbLabelBrightness = 0;

// Verwendet automatisch die Aufloesung der jeweiligen Pebble.
// Auf der Pebble Time 2 (Emery) sind das 200 x 228 Pixel.
#define WIDTH PBL_DISPLAY_WIDTH
#define HEIGHT PBL_DISPLAY_HEIGHT
	
typedef struct //strukt mit 2 ganzzahlen
{
	int	x, y;
} PT2;

void handle_timer(void *data);  //Es gibt später eine Funktion namens handle_timer

#define FIXBITS 10  //zehn Bits für den Nachkommateil reserviert. Eine normale Zahl wird intern mit 1024 multipliziert

#define FIXMULT(a, b) (( (a)*(b) ) >> FIXBITS)  //Multipliziert zwei Fixed-Point-Zahlen. Nach der Multiplikation muss das Ergebnis wieder um zehn Bits zurückgeschoben werden
#define FIX2INT(a) ( ((a) + (1<<(FIXBITS-1))) >> FIXBITS)  //Wandelt Fixed Point zurück in eine Ganzzahl
#define INT2FIX(a) ((a) * (1 << FIXBITS))  //Wandelt eine Ganzzahl in Fixed Point um

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

#define KERNEL_TABLE_RAD 40  //Die ursprüngliche Einflusskurve besitzt 40 Werte

// Der Blobradius wird proportional zur gesamten Displaygroesse skaliert.
// 144 x 168 ergibt weiterhin 40 Pixel, 200 x 228 ergibt 55 Pixel.
#define KERNEL_RAD \
	((KERNEL_TABLE_RAD * (WIDTH + HEIGHT) + 156) / 312)
	
#define REFRESH_RATE 50  //Während der Animation erfolgt ungefähr alle 50 Millisekunden ein Update (20 Bilder/Sekunde)
#define LABEL_FADE_STEP 16  //16 Schritte à 50 ms ergeben ungefähr 0,8 Sekunden Einblendzeit
#define INTEGRATE_TIMER_ID 1  //Aktuell nicht in Verwendung
	
#define NUM_CLOCKBITS 10  //anzahl Clockbits

// Die ursprünglichen Zielpositionen wurden für ein Display mit 144 x 168
// Pixeln entworfen. Diese Makros skalieren jede Position proportional auf
// die tatsächliche Displaygroesse.
#define ORIGINAL_WIDTH 144
#define ORIGINAL_HEIGHT 168
#define SCALE_X(value) ((value) * WIDTH / ORIGINAL_WIDTH)
#define SCALE_Y(value) ((value) * HEIGHT / ORIGINAL_HEIGHT)

int glbTargetMinute = -1;  //speichert die zuletzt verarbeitete Minute. Der Startwert -1 ist absichtlich ungültig. Dadurch wird beim Start garantiert die aktuelle Uhrzeit verarbeitet

const PT2 glbTargets[NUM_CLOCKBITS] = //proportional skalierte Blobpositionen
{
	// Stunden: 2^3, 2^2, 2^1, 2^0
	{ SCALE_X(27),  SCALE_Y(44)  },
	{ SCALE_X(57),  SCALE_Y(24)  },
	{ SCALE_X(87),  SCALE_Y(54)  },
	{ SCALE_X(117), SCALE_Y(34)  },

	// Minuten: 2^5, 2^4, 2^3, 2^2, 2^1, 2^0
	{ SCALE_X(17),  SCALE_Y(114) },
	{ SCALE_X(47),  SCALE_Y(94)  },
	{ SCALE_X(57),  SCALE_Y(134) },
	{ SCALE_X(87),  SCALE_Y(134) },
	{ SCALE_X(97),  SCALE_Y(94)  },
	{ SCALE_X(127), SCALE_Y(114) },
};

// Exponenten der zehn Binaerpositionen.
// Darstellung:
//   kleiner Ring = 2^0
//   ein Punkt    = 2^1
//   zwei Punkte  = 2^2
//   ...
const int glbTargetExponents[NUM_CLOCKBITS] =
{
	3, 2, 1, 0,
	5, 4, 3, 2, 1, 0
};

// Tracks which binary positions are active for the current time.
bool glbActiveTargets[NUM_CLOCKBITS] = { false };

PT2 glbPartTargets[NUM_PART];  //Dieses Array enthält für jedes der zehn Partikel seine momentane Zielposition
	
const int glbBlinnKernel[KERNEL_TABLE_RAD] =  //Lava-Lampen-Effekt
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

	// Die ursprüngliche 40-Pixel-Kurve wird auf den effektiven Radius
	// der jeweiligen Displaygroesse gestreckt.
	int kernel_index = (dist * KERNEL_TABLE_RAD) / KERNEL_RAD;

	if (kernel_index >= KERNEL_TABLE_RAD)
		kernel_index = KERNEL_TABLE_RAD - 1;

	return glbBlinnKernel[kernel_index];
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

	// Die Punktmarkierungen werden erst gezeichnet, nachdem die Partikel
	// ihre Ziele erreicht haben. Danach blenden sie in etwa 0,8 Sekunden ein.
	bool draw_labels = glbBlobsSettled && glbLabelBrightness > 0;

#if !defined(PBL_COLOR)
	// Schwarzweiss-Pebbles kennen keine echten Graustufen.
	draw_labels = glbBlobsSettled && glbLabelBrightness >= 255;
#endif

	if (draw_labels)
	{
#if defined(PBL_COLOR)
		GColor label_color = GColorFromRGB(
			glbLabelBrightness,
			glbLabelBrightness,
			glbLabelBrightness
		);
#else
		GColor label_color = GColorWhite;
#endif

		graphics_context_set_fill_color(ctx, label_color);
		graphics_context_set_stroke_color(ctx, label_color);

#if defined(PBL_PLATFORM_EMERY)
		const int dot_radius = 5;
		const int pattern_radius = 13;
#else
		const int dot_radius = 3;
		const int pattern_radius = 9;
#endif

		for (int target = 0; target < NUM_CLOCKBITS; target++)
		{
			if (!glbActiveTargets[target])
				continue;

			const int exponent = glbTargetExponents[target];
			const int center_x = glbTargets[target].x;
			const int center_y = glbTargets[target].y;

			if (exponent == 0)
			{
				// 2^0 wird als kleiner offener Ring dargestellt.
				graphics_draw_circle(
					ctx,
					GPoint(center_x, center_y),
					dot_radius
				);
				continue;
			}

			if (exponent == 1)
			{
				// Ein einzelner Punkt liegt genau in der Mitte.
				graphics_fill_circle(
					ctx,
					GPoint(center_x, center_y),
					dot_radius
				);
				continue;
			}

			if (exponent == 2)
			{
				// Zwei Punkte senkrecht übereinander.
				const int offset_y = pattern_radius * 3 / 4;

				graphics_fill_circle(
					ctx,
					GPoint(center_x, center_y - offset_y),
					dot_radius
				);
				graphics_fill_circle(
					ctx,
					GPoint(center_x, center_y + offset_y),
					dot_radius
				);
				continue;
			}

			if (exponent == 3)
			{
				// Drei Punkte bilden ein gleichmässiges Dreieck.
				graphics_fill_circle(
					ctx,
					GPoint(center_x, center_y - pattern_radius),
					dot_radius
				);
				graphics_fill_circle(
					ctx,
					GPoint(
						center_x - pattern_radius * 7 / 8,
						center_y + pattern_radius / 2
					),
					dot_radius
				);
				graphics_fill_circle(
					ctx,
					GPoint(
						center_x + pattern_radius * 7 / 8,
						center_y + pattern_radius / 2
					),
					dot_radius
				);
				continue;
			}

			if (exponent == 4)
			{
				// Vier Punkte bilden ein Quadrat.
				const int offset = pattern_radius * 7 / 10;

				graphics_fill_circle(
					ctx,
					GPoint(center_x - offset, center_y - offset),
					dot_radius
				);
				graphics_fill_circle(
					ctx,
					GPoint(center_x + offset, center_y - offset),
					dot_radius
				);
				graphics_fill_circle(
					ctx,
					GPoint(center_x - offset, center_y + offset),
					dot_radius
				);
				graphics_fill_circle(
					ctx,
					GPoint(center_x + offset, center_y + offset),
					dot_radius
				);
				continue;
			}

			// Fünf Punkte bilden ein regelmässiges Fünfeck.
			graphics_fill_circle(
				ctx,
				GPoint(center_x, center_y - pattern_radius),
				dot_radius
			);
			graphics_fill_circle(
				ctx,
				GPoint(
					center_x + pattern_radius * 12 / 13,
					center_y - pattern_radius * 4 / 13
				),
				dot_radius
			);
			graphics_fill_circle(
				ctx,
				GPoint(
					center_x + pattern_radius * 8 / 13,
					center_y + pattern_radius * 11 / 13
				),
				dot_radius
			);
			graphics_fill_circle(
				ctx,
				GPoint(
					center_x - pattern_radius * 8 / 13,
					center_y + pattern_radius * 11 / 13
				),
				dot_radius
			);
			graphics_fill_circle(
				ctx,
				GPoint(
					center_x - pattern_radius * 12 / 13,
					center_y - pattern_radius * 4 / 13
				),
				dot_radius
			);
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
	glbBlobsSettled = false;
	glbLabelBrightness = 0;
	app_timer_register(REFRESH_RATE, handle_timer, 0);
	glbTimerRunning = true;
	
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

	// Bei einer neuen Minute verschwinden die Zahlen sofort. Sie werden erst
	// wieder eingeblendet, wenn alle Partikel ihre neuen Ziele erreicht haben.
	glbBlobsSettled = false;
	glbLabelBrightness = 0;
	
	int 	ntarget = 0;
	static int		targets[NUM_CLOCKBITS];
	int hour = tick_time->tm_hour;

    for (int target = 0; target < NUM_CLOCKBITS; target++)
	    glbActiveTargets[target] = false;
	
	if (hour > 12)
		hour -= 12;
	
	for (int bit = 0; bit < 4; bit++)
{
	if (hour & (1 << (3-bit)))
	{
		targets[ntarget++] = bit;
		glbActiveTargets[bit] = true;
	}
}
	for (int bit = 0; bit < 6; bit++)
{
	if (tick_time->tm_min & (1 << (5-bit)))
	{
		int target = bit + 4;

		targets[ntarget++] = target;
		glbActiveTargets[target] = true;
	}
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
	
	glbLive = true;

	if (!glbTimerRunning)
	{
		app_timer_register(REFRESH_RATE, handle_timer, 0);
		glbTimerRunning = true;
	}
	
	layer_mark_dirty(glbBlobLayerP);
}

void
handle_timer(void *data)
{
	(void)data;

	// Der gerade ausgeführte Timer ist nun verbraucht.
	glbTimerRunning = false;

	bool continue_timer = false;

	if (!glbBlobsSettled)
	{
		glbLive = part_integrate();

		// Solange die Blobs unterwegs sind, werden überhaupt keine Zahlen gezeichnet.
		glbLabelBrightness = 0;

		if (glbLive)
		{
			continue_timer = true;
		}
		else
		{
			// Ab jetzt werden die Partikel nicht mehr weiter integriert.
			// Dadurch bleiben die Blobs während des Einblendens exakt stehen.
			glbBlobsSettled = true;
			glbLive = false;
			continue_timer = true;
		}
	}
	else if (glbLabelBrightness < 255)
	{
		// Erst bei vollständig stillstehenden Blobs langsam einblenden.
		glbLabelBrightness += LABEL_FADE_STEP;

		if (glbLabelBrightness > 255)
			glbLabelBrightness = 255;

		continue_timer = glbLabelBrightness < 255;
	}

	if (continue_timer)
	{
		app_timer_register(REFRESH_RATE, handle_timer, 0);
		glbTimerRunning = true;
	}

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
