#include <pebble.h> //einbinden vom SDK

#include "mytypes.h" //einbinden von Hilfsmakros
#include "rand.h"  //selbstgeschriebener Zufallsgenerator

Window *glbWindowP;  //globaler Pointer auf das Hauptfenster der App
Layer *glbBlobLayerP;  //Pointer auf die Zeichenebene, auf der die Blobs gerendert werden.
#define NUM_VALUE_FONTS 6

// Mehrere feste Groessen desselben Fonts.
// Pebble kann einen geladenen Font nicht stufenlos skalieren.
GFont glbValueFonts[NUM_VALUE_FONTS];


#define SETTINGS_PERSIST_KEY 1

typedef struct
{
	uint8_t background_argb;
	uint8_t blob_argb;
	uint8_t value_argb;
} LavalampSettings;

LavalampSettings glbSettings;

GColor glbBackgroundColor;
GColor glbBlobColor;
GColor glbValueColor;

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

// Werte der zehn Binaerpositionen.
const char *glbTargetLabels[NUM_CLOCKBITS] =
{
	"8", "4", "2", "1",
	"32", "16", "8", "4", "2", "1"
};

// Tracks which binary positions are active for the current time.
bool glbActiveTargets[NUM_CLOCKBITS] = { false };

// Anzahl der Partikel, die dem jeweiligen Zielblob zugeordnet wurden.
// Daraus wird spaeter die passende Schriftgroesse gewaehlt.
uint8_t glbTargetParticleCount[NUM_CLOCKBITS] = { 0 };

PT2 glbPartTargets[NUM_PART];  //Dieses Array enthält für jedes der zehn Partikel seine momentane Zielposition

int value_font_index_for_target(int target)
{
	const int particle_count = glbTargetParticleCount[target];
	const bool is_two_digit =
		glbTargetLabels[target][1] != '\0';

	if (is_two_digit)
	{
		// 16 und 32 brauchen deutlich weniger Schriftgroesse als eine
		// einzelne Ziffer. Sonst ragt der weisse Text aus dem schwarzen
		// Blob heraus und verschwindet auf dem weissen Hintergrund.
		if (particle_count <= 1)
			return 0;  // Modak 26
		if (particle_count == 2)
			return 1;  // Modak 32
		if (particle_count == 3)
			return 2;  // Modak 36
		return 3;      // Modak 42
	}

	// Einstellige Werte behalten die bisher gut wirkenden Groessen.
	if (particle_count <= 1)
		return 2;  // Modak 36
	if (particle_count == 2)
		return 3;  // Modak 42
	if (particle_count == 3)
		return 4;  // Modak 48
	return 5;      // Modak 54
}

	
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
	if (dist >= INT2FIX(KERNEL_RAD))
		return 0;

	// Distanz proportional auf die ursprüngliche 40-Pixel-Kurve abbilden,
	// dabei aber den Nachkommateil behalten.
	int scaled_dist =
		(dist * KERNEL_TABLE_RAD) / KERNEL_RAD;

	int kernel_index = scaled_dist >> FIXBITS;
	int fraction = scaled_dist & ((1 << FIXBITS) - 1);

	if (kernel_index >= KERNEL_TABLE_RAD - 1)
	{
		// Am äussersten Ende gegen null auslaufen lassen.
		int start_value = glbBlinnKernel[KERNEL_TABLE_RAD - 1];
		return start_value -
			FIXMULT(start_value, fraction);
	}

	int start_value = glbBlinnKernel[kernel_index];
	int end_value = glbBlinnKernel[kernel_index + 1];

	// Lineare Interpolation zwischen zwei benachbarten Kernelwerten.
	return start_value +
		FIXMULT(end_value - start_value, fraction);
}

int metadist(PT2 a, PT2 b)  //Einfluss eines Partikels auf einen Pixel
{
	int adist = ABS(a.x - b.x);
	int bdist = ABS(a.y - b.y);
	return FIXMULT( blinn(adist), blinn(bdist) );  //Überschreitet die Summe einen Grenzwert, wird der Pixel schwarz. So verschmelzen die Partikel zu zusammenhängenden Blobs.
}

static int blob_plist[NUM_PART];  //Hilfsarrays für das Rendern
static int blob_plistx[NUM_PART];  //Hilfsarrays für das Rendern

static int color_component(GColor color, int shift)
{
	return ((color.argb >> shift) & 0x03) * 85;
}

static GColor mix_colors(
	GColor from_color,
	GColor to_color,
	int amount,
	int maximum
)
{
	if (amount <= 0)
		return from_color;

	if (amount >= maximum)
		return to_color;

	int from_red = color_component(from_color, 4);
	int from_green = color_component(from_color, 2);
	int from_blue = color_component(from_color, 0);

	int to_red = color_component(to_color, 4);
	int to_green = color_component(to_color, 2);
	int to_blue = color_component(to_color, 0);

	int red =
		(from_red * (maximum - amount) + to_red * amount) /
		maximum;
	int green =
		(from_green * (maximum - amount) + to_green * amount) /
		maximum;
	int blue =
		(from_blue * (maximum - amount) + to_blue * amount) /
		maximum;

	return GColorFromRGB(red, green, blue);
}

static void apply_settings_colors()
{
	glbBackgroundColor =
		(GColor){ .argb = glbSettings.background_argb };
	glbBlobColor =
		(GColor){ .argb = glbSettings.blob_argb };
	glbValueColor =
		(GColor){ .argb = glbSettings.value_argb };

	if (glbWindowP)
	{
		window_set_background_color(
			glbWindowP,
			glbBackgroundColor
		);
	}

	if (glbBlobLayerP)
		layer_mark_dirty(glbBlobLayerP);
}

static void load_settings()
{
	glbSettings.background_argb = GColorWhite.argb;
	glbSettings.blob_argb = GColorBlack.argb;
	glbSettings.value_argb = GColorWhite.argb;

	if (
		persist_exists(SETTINGS_PERSIST_KEY) &&
		persist_get_size(SETTINGS_PERSIST_KEY) ==
			(int)sizeof(glbSettings)
	)
	{
		persist_read_data(
			SETTINGS_PERSIST_KEY,
			&glbSettings,
			sizeof(glbSettings)
		);
	}

	apply_settings_colors();
}

static void save_settings()
{
	persist_write_data(
		SETTINGS_PERSIST_KEY,
		&glbSettings,
		sizeof(glbSettings)
	);
}

static void inbox_received_handler(
	DictionaryIterator *iterator,
	void *context
)
{
	(void)context;

	Tuple *background_tuple =
		dict_find(iterator, MESSAGE_KEY_BackgroundColor);
	Tuple *blob_tuple =
		dict_find(iterator, MESSAGE_KEY_BlobColor);
	Tuple *value_tuple =
		dict_find(iterator, MESSAGE_KEY_ValueColor);

	if (background_tuple)
	{
		glbSettings.background_argb =
			GColorFromHEX(
				background_tuple->value->int32
			).argb;
	}

	if (blob_tuple)
	{
		glbSettings.blob_argb =
			GColorFromHEX(
				blob_tuple->value->int32
			).argb;
	}

	if (value_tuple)
	{
		glbSettings.value_argb =
			GColorFromHEX(
				value_tuple->value->int32
			).argb;
	}

	save_settings();
	apply_settings_colors();
}


// Echtes Kanten-Antialiasing nur innerhalb eines einzelnen Pixels.
// Die Blobfläche selbst bleibt scharf und vollständig schwarz.
#define AA_SAMPLE_OFFSET (INT2FIX(1) / 4)

// Nur ein sehr schmaler Bereich direkt an der Kontur wird geglättet.
// Der alte Wert 1/2 war viel zu breit und konnte die Form unruhig machen.
#define AA_CHECK_BAND (INT2FIX(1) / 10)

void bloblayer_update(Layer *me, GContext *ctx)  //raw()-Methode von Pebble SDK. ab hier wird gezeichnet
{
	(void) me;  //vermutlich ueberfluessig
	
	graphics_context_set_fill_color(
		ctx,
		glbBackgroundColor
	);
	graphics_context_set_stroke_color(
		ctx,
		glbBlobColor
	);

	graphics_fill_rect(
		ctx,
		layer_get_bounds(me),
		0,
		GCornerNone
	);
	
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
			
			PT2 cpos;
			cpos.x = INT2FIX(x);
			cpos.y = INT2FIX(y);

			int totaldist = 0;
			for (int part = startidx; part < endidx; part++)
			{
				totaldist += metadist(
					glbPart[blob_plist[part]].pos,
					cpos
				);
			}

			const int threshold = INT2FIX(1);

			if (totaldist >= threshold + AA_CHECK_BAND)
			{
				// Klar innerhalb des Blobs: vollständig schwarz.
				graphics_draw_pixel(ctx, GPoint(x, y));
			}
			else if (totaldist > threshold - AA_CHECK_BAND)
			{
				// Nur direkt an der mathematischen Aussenkante werden vier
				// Unterpixel geprüft. Dadurch wird ausschliesslich der
				// Treppeneffekt geglättet, ohne die Kontur weichzuzeichnen.
				int covered_samples = 0;

				const int sample_x[4] =
				{
					cpos.x - AA_SAMPLE_OFFSET,
					cpos.x + AA_SAMPLE_OFFSET,
					cpos.x - AA_SAMPLE_OFFSET,
					cpos.x + AA_SAMPLE_OFFSET
				};

				const int sample_y[4] =
				{
					cpos.y - AA_SAMPLE_OFFSET,
					cpos.y - AA_SAMPLE_OFFSET,
					cpos.y + AA_SAMPLE_OFFSET,
					cpos.y + AA_SAMPLE_OFFSET
				};

				for (int sample_index = 0;
				     sample_index < 4;
				     sample_index++)
				{
					PT2 sample_pos =
					{
						sample_x[sample_index],
						sample_y[sample_index]
					};

					int sample_dist = 0;

					for (int part = startidx;
					     part < endidx;
					     part++)
					{
						sample_dist += metadist(
							glbPart[blob_plist[part]].pos,
							sample_pos
						);
					}

					if (sample_dist > threshold)
						covered_samples++;
				}

				if (covered_samples == 4)
				{
					graphics_context_set_stroke_color(
						ctx,
						glbBlobColor
					);
					graphics_draw_pixel(ctx, GPoint(x, y));
				}
#if defined(PBL_COLOR)
				else if (covered_samples > 0)
				{
					// Teilabdeckung zwischen der gewählten
					// Hintergrund- und Blobfarbe mischen.
					GColor edge_color = mix_colors(
						glbBackgroundColor,
						glbBlobColor,
						covered_samples,
						4
					);

					graphics_context_set_stroke_color(
						ctx,
						edge_color
					);
					graphics_draw_pixel(
						ctx,
						GPoint(x, y)
					);

					graphics_context_set_stroke_color(
						ctx,
						glbBlobColor
					);
				}
#else
				else if (covered_samples >= 2)
				{
					graphics_draw_pixel(ctx, GPoint(x, y));
				}
#endif
			}
		}
	}

	// Die Werte werden erst gezeichnet, nachdem die Blobs ihre Zielpositionen
	// erreicht haben. Danach blenden sie in etwa 0,8 Sekunden ein.
	bool draw_labels = glbBlobsSettled && glbLabelBrightness > 0;

#if !defined(PBL_COLOR)
	// Schwarzweiss-Pebbles kennen keine echten Graustufen.
	draw_labels = glbBlobsSettled && glbLabelBrightness >= 255;
#endif

	if (draw_labels)
	{
		GColor label_color = mix_colors(
			glbBlobColor,
			glbValueColor,
			glbLabelBrightness,
			255
		);

		graphics_context_set_text_color(ctx, label_color);

#if defined(PBL_PLATFORM_EMERY)
		const int label_width = 104;
		const int max_label_height = 80;
#else
		const int label_width = 72;
		const int max_label_height = 58;
#endif

		for (int target = 0; target < NUM_CLOCKBITS; target++)
		{
			if (!glbActiveTargets[target])
				continue;

			int font_index = value_font_index_for_target(target);
			GFont value_font = glbValueFonts[font_index];

			// Die Schriftgroesse ändert sich mit der Blobgroesse.
			// Darum darf die Oberkante des Textfeldes nicht für alle Fonts
			// gleich sein: Kleine Fonts würden sonst höher, grosse tiefer sitzen.
			// Pebble misst hier für genau diesen Text und genau diesen Font
			// zunächst die tatsächliche Zeilenhöhe.
			GSize text_size =
				graphics_text_layout_get_content_size(
					glbTargetLabels[target],
					value_font,
					GRect(
						0,
						0,
						label_width,
						max_label_height
					),
					GTextOverflowModeFill,
					GTextAlignmentCenter
				);

			int draw_height = text_size.h;

			if (draw_height < 1)
				draw_height = max_label_height;

			// Nun wird die gemessene Textzeile um den Zielpunkt des Blobs
			// zentriert. Damit sitzt jede Schriftgroesse auf derselben Mitte.
			// Nach der dynamischen Zentrierung sitzt Modak optisch noch
			// minimal zu tief. Dieser gemeinsame Offset verschiebt alle
			// Schriftgroessen gleichmaessig 3 Pixel nach oben.
			const int optical_y_offset = -8;

			GRect text_bounds = GRect(
				glbTargets[target].x - label_width / 2,
				glbTargets[target].y - draw_height / 2
					+ optical_y_offset,
				label_width,
				draw_height + 2
			);

			graphics_draw_text(
				ctx,
				glbTargetLabels[target],
				value_font,
				text_bounds,
				GTextOverflowModeFill,
				GTextAlignmentCenter,
				NULL
			);
		}
	}

}
void handle_init() 
{
	glbValueFonts[0] = fonts_load_custom_font(
		resource_get_handle(RESOURCE_ID_FONT_MODAK_26)
	);
	glbValueFonts[1] = fonts_load_custom_font(
		resource_get_handle(RESOURCE_ID_FONT_MODAK_32)
	);
	glbValueFonts[2] = fonts_load_custom_font(
		resource_get_handle(RESOURCE_ID_FONT_MODAK_36)
	);
	glbValueFonts[3] = fonts_load_custom_font(
		resource_get_handle(RESOURCE_ID_FONT_MODAK_42)
	);
	glbValueFonts[4] = fonts_load_custom_font(
		resource_get_handle(RESOURCE_ID_FONT_MODAK_48)
	);
	glbValueFonts[5] = fonts_load_custom_font(
		resource_get_handle(RESOURCE_ID_FONT_MODAK_54)
	);

	glbWindowP = window_create();
	
	load_settings();

	window_stack_push(glbWindowP, true /* Animated */);
	window_set_background_color(
		glbWindowP,
		glbBackgroundColor
	);
	
	glbBlobLayerP = layer_create(
		layer_get_frame(window_get_root_layer(glbWindowP)));
	layer_set_update_proc(glbBlobLayerP, &bloblayer_update);
	layer_add_child(window_get_root_layer(glbWindowP), glbBlobLayerP);
	

	app_message_register_inbox_received(
		inbox_received_handler
	);
	app_message_open(128, 128);

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
	for (int font_index = 0;
	     font_index < NUM_VALUE_FONTS;
	     font_index++)
	{
		if (glbValueFonts[font_index])
		{
			fonts_unload_custom_font(
				glbValueFonts[font_index]
			);
			glbValueFonts[font_index] = NULL;
		}
	}

	app_message_deregister_callbacks();

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
	{
		glbActiveTargets[target] = false;
		glbTargetParticleCount[target] = 0;
	}
	
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
			
			int target = targets[i];
			glbPartTargets[partlist[i]] = glbTargets[target];
			glbTargetParticleCount[target]++;
		}
		// Remaining particles get a random target.
		for (int i = ntarget; i < NUM_PART; i++)
		{
			int t = rand_choice(ntarget);
			int target = targets[t];

			glbPartTargets[partlist[i]] = glbTargets[target];
			glbTargetParticleCount[target]++;
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
