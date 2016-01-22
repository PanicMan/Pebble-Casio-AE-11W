#include <pebble.h>
#include "effect_layer.h"

enum ConfigKeys {
	JS_READY=0,
	C_INV=1,
	C_AUTO_SW=2,
	C_VIBR=3,
	C_VIBR_BT=4,
	C_SHOWSEC=5,
	C_DATEFMT=6,
	C_WEATHER=7,
	C_UNITS=8,
	C_CITYID=9,
	W_TIME=90,
	W_TEMP=91,
	W_ICON=92,
	W_CITY=93,
	W_COND=94
};

enum StorrageKeys {
	PK_SETTINGS = 0
};

typedef struct {
	bool inv, automode;
	bool vibr, vibr_bt;
	uint8_t datefmt, substep, showsec;
	bool weather, isunit;
	uint32_t cityid, w_time;
	int16_t w_temp;
	char w_icon[2], w_city[50], w_cond[50];
} __attribute__((__packed__)) CfgDta_t;

static CfgDta_t CfgData = {
	.inv = false,
	.automode = true,
	.vibr = false,
	.vibr_bt = true,
	.datefmt = 0,
	.substep = 1,
	.showsec = 1,
	.weather = true,
	.isunit = false,
	.cityid = 0,
	.w_time = 0,
	.w_temp = 0,
	.w_icon = " ",
	.w_city = "",
	.w_cond = ""
};

Window *window, *window_sec;
Layer *clock_layer, *second_layer, *multi_layer;
EffectLayer *inv_layer;

static uint8_t s_HH, s_MM, s_SS, s_MultiMode, s_CurrBatt;
static uint16_t s_SSSub;
static AppTimer *timer_subsec, *timer_weather;
static GBitmap *s_bmpBottom;
static bool s_RadioConn, s_BattChrg, w_UpdateRetry;
static GFont digitS, digitM, digitL, WeatherF;
static BitmapInfo s_biBackground;

static GPath *hour_arrow, *minute_arrow;
static const GPathInfo HOUR_HAND_POINTS = {	3, (GPoint []) { { 0, -8 }, { -2, -40 }, { 2, -40 } } };
static const GPathInfo MINUTE_HAND_POINTS = { 4, (GPoint []) { { -2, -43 }, { -2, -55 }, { 2, -55 }, { 2, -43 } } };

static const GPoint paHM1[] = { {71, 0}, {105, 1}, {142, 19}, {125, 59}, {142, 100}, {105, 119}, {71, 102}, {37, 119}, {1, 100}, {4, 59}, {1, 19}, {37, 1} };
static const GPoint paHM2[] = { {72, 19}, {93, 22}, {114, 35}, {139, 60}, {114, 84}, {93, 97}, {72, 116}, {50, 97}, {29, 84}, {18,60}, {29, 35}, {50, 22} };
static const GRect raHM1[] = { {{70, 0}, {4, 3}}, {{141, 58}, {3, 4}}, {{70, 118}, {4, 3}}, {{0, 58}, {3, 4}} };

static const uint32_t segments_bt[] = {100, 100, 100, 100, 400, 400, 100, 100, 100};
static const VibePattern vibe_pat_bt = {
	.durations = segments_bt,
	.num_segments = ARRAY_LENGTH(segments_bt),
};

//-----------------------------------------------------------------------------------------------------------------------
char *upcase(char *str) {
    for (int i = 0; str[i] != 0; i++) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            str[i] -= 'a' - 'A';
        }
    }
    return str;
}
//-----------------------------------------------------------------------------------------------------------------------
void fill4(BitmapInfo bi, int y, int x, uint8_t colorOld, uint8_t colorNew) {
	if (get_pixel(bi, y, x) == colorOld) {
		set_pixel(bi, y, x, colorNew);
		fill4(bi, y + 1, x, colorOld, colorNew); // unten
		fill4(bi, y - 1, x, colorOld, colorNew); // oben
		fill4(bi, y, x - 1, colorOld, colorNew); // links
		fill4(bi, y, x + 1, colorOld, colorNew); // rechts
	}
	return;
}
//-----------------------------------------------------------------------------------------------------------------------
static void update_all()
{
	if (window_stack_get_top_window() == window_sec)
	{
		Layer *window_layer = window_get_root_layer(window);
		layer_remove_from_parent(second_layer);
		layer_add_child(window_layer, second_layer);	
		window_stack_pop(false);
	}
	else
	{
		layer_mark_dirty(clock_layer);
		layer_mark_dirty(multi_layer);
	}
}
//-----------------------------------------------------------------------------------------------------------------------
static void clock_update_proc(Layer *layer, GContext *ctx) 
{
	app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Clock Layer Update");
	
	GRect bounds = layer_get_bounds(layer);
	const GPoint center = grect_center_point(&bounds);
#ifdef PBL_COLOR
	//graphics_context_set_stroke_width(ctx, 2);
#endif
	
	//Frame and center
	graphics_context_set_fill_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
	graphics_fill_rect(ctx, bounds, 0, GCornerNone);
	graphics_context_set_fill_color(ctx, CfgData.inv ? GColorBlack : GColorWhite);
	graphics_fill_rect(ctx, GRect(4, 4, bounds.size.w-8, bounds.size.h-8), 5, GCornersAll);
	graphics_context_set_fill_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
	graphics_fill_rect(ctx, GRect(center.x-7, center.y-7, 14, 14), 3, GCornersAll);
	graphics_context_set_fill_color(ctx, CfgData.inv ? GColorBlack : GColorWhite);
	graphics_fill_rect(ctx, GRect(center.x-3, center.y-3, 6, 6), 0, GCornerNone);
	
	//Hour markers
	graphics_context_set_stroke_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
	for (uint8_t h = 0; h<12; h++)
	{
		if (h == 0)
			graphics_context_set_fill_color(ctx, s_BattChrg ? COLOR_FALLBACK(GColorRed, CfgData.inv ? GColorBlack : GColorWhite) : CfgData.inv ? GColorBlack : GColorWhite);
		else if (h > 0 && h < 11)
			graphics_context_set_fill_color(ctx, 
				h <= s_CurrBatt ? 
					COLOR_FALLBACK(s_CurrBatt < 3 ? GColorRed : s_CurrBatt < 5 ?  CfgData.inv ? GColorChromeYellow : GColorYellow : CfgData.inv ? GColorIslamicGreen : GColorGreen, CfgData.inv ? GColorBlack : GColorWhite) : 
					COLOR_FALLBACK(CfgData.inv ? GColorBlack : GColorWhite, CfgData.inv ? GColorWhite : GColorBlack));
		else if (h == 11)
			graphics_context_set_fill_color(ctx, s_RadioConn ? COLOR_FALLBACK(CfgData.inv ? GColorIslamicGreen : GColorGreen, CfgData.inv ? GColorBlack : GColorWhite) : COLOR_FALLBACK(GColorRed, CfgData.inv ? GColorWhite : GColorBlack));
		
		if (h % 3 == 0) //0,3,6,9
		{
			graphics_fill_rect(ctx, raHM1[h/3], 0, GCornerNone);
			graphics_draw_rect(ctx, GRect(paHM1[h].x, paHM1[h].y, paHM2[h].x-paHM1[h].x+1, paHM2[h].y-paHM1[h].y+1));
		}
		else
		{
			graphics_draw_line(ctx, paHM1[h], paHM2[h]);
			graphics_fill_circle(ctx, GPoint(paHM1[h].x, paHM1[h].y), 2);
			//graphics_fill_rect(ctx, GRect(paHM1[h].x-2, paHM1[h].y-2, 4, 4), 1, GCornersAll);
			graphics_context_set_fill_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
			graphics_fill_rect(ctx, GRect(paHM2[h].x-1, paHM2[h].y-1, 3, 3), 1, GCornersAll);
		}
	}
	
	//Hour Arrow
	gpath_move_to(hour_arrow, center);
	gpath_rotate_to(hour_arrow, (TRIG_MAX_ANGLE * (((s_HH % 12) * 6) + (s_MM / 10))) / (12 * 6));
	gpath_draw_filled(ctx, hour_arrow);
	gpath_draw_outline(ctx, hour_arrow);
		
	//Minute = Hour Arrow + Minute Arrow
	gpath_rotate_to(hour_arrow, TRIG_MAX_ANGLE * s_MM / 60);
	gpath_draw_filled(ctx, hour_arrow);
	gpath_draw_outline(ctx, hour_arrow);
	gpath_move_to(minute_arrow, center);
	gpath_rotate_to(minute_arrow, TRIG_MAX_ANGLE * s_MM / 60);
	gpath_draw_filled(ctx, minute_arrow);
	gpath_draw_outline(ctx, minute_arrow);

	//Save Bitmap Data
	GBitmap *fb = graphics_capture_frame_buffer(ctx);
	BitmapInfo bitmap_info = fill_bitmapinfo(fb);
	
	//If no back bitmap, create one
	if (!s_biBackground.bitmap)
	{
		s_biBackground = fill_bitmapinfo(gbitmap_create_blank(bounds.size, bitmap_info.bitmap_format));
		app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Background Bitmap created: 0x%X, 0x%X, %d, %d", (unsigned int)s_biBackground.bitmap, (unsigned int)s_biBackground.bitmap_data, s_biBackground.bytes_per_row, s_biBackground.bitmap_format);
	}
	
	//Transfer Info to Background Image
	for (int y=0; y<bounds.size.h; y++)
		for (int x=0; x<bounds.size.w; x++)
			set_pixel(s_biBackground, y, x, get_pixel(bitmap_info, y, x));

	graphics_release_frame_buffer(ctx, fb);          
}
//-----------------------------------------------------------------------------------------------------------------------
static void secwnd_update_proc(Layer *layer, GContext *ctx) 
{
	//app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Second Windows Layer Update");
	if (s_biBackground.bitmap)
	{
		GSize bg_size = gbitmap_get_bounds(s_biBackground.bitmap).size;
		//app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Background Bitmap Size: %dx%d", bg_size.w, bg_size.h);
		graphics_draw_bitmap_in_rect(ctx, s_biBackground.bitmap, GRect(0, 0, bg_size.w, bg_size.h));
	}
	
	if (s_MultiMode == 0 && (s_SSSub % CfgData.substep) == 0)
	{
		graphics_context_set_fill_color(ctx, CfgData.inv ? GColorBlack : GColorWhite);
		graphics_fill_rect(ctx, GRect(99, 133, 26, 22), 0, GCornerNone);
	}
}
//-----------------------------------------------------------------------------------------------------------------------
static void second_update_proc(Layer *layer, GContext *ctx) 
{
	//app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Second Layer Update, Sec:%d, subSec:%d", s_SS, s_SSSub);
	GRect bounds = GRect(0, 0, 144, 121);
	GPoint center = grect_center_point(&bounds);
	
	graphics_context_set_stroke_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
	graphics_context_set_fill_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
	
	//Second Arrow
	gpath_move_to(hour_arrow, center);
	gpath_rotate_to(hour_arrow, TRIG_MAX_ANGLE * s_SSSub / (60*CfgData.substep));
	gpath_draw_filled(ctx, hour_arrow);
	gpath_draw_outline(ctx, hour_arrow);
	gpath_move_to(minute_arrow, center);
	gpath_rotate_to(minute_arrow, TRIG_MAX_ANGLE * s_SSSub / (60*CfgData.substep));
	gpath_draw_filled(ctx, minute_arrow);
	gpath_draw_outline(ctx, minute_arrow);
	
	//Seconds
	if (s_MultiMode == 0 && (s_SSSub % CfgData.substep) == 0)
	{
		bounds = GRect(0, 121, 144, 168-121);
		center = grect_center_point(&bounds);
		graphics_context_set_text_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
		
		char sTemp[] = "00";
		snprintf(sTemp, sizeof(sTemp), "%02d", (s_SSSub / CfgData.substep) % 60);
		GSize sz = graphics_text_layout_get_content_size(sTemp, digitM, bounds, GTextOverflowModeFill, GTextAlignmentCenter);
		graphics_draw_text(ctx, sTemp, digitM, GRect(center.x-sz.w/2+40, center.y-sz.h/2-5, sz.w, sz.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
	}
}
//-----------------------------------------------------------------------------------------------------------------------
static void multi_update_proc(Layer *layer, GContext *ctx) 
{
	app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Bottom Layer Update");
	GRect bounds = layer_get_bounds(layer);
	const GPoint center = grect_center_point(&bounds);
	
	//Frame
	graphics_context_set_fill_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
	graphics_fill_rect(ctx, bounds, 0, GCornerNone);
	graphics_context_set_fill_color(ctx, CfgData.inv ? GColorBlack : GColorWhite);
	graphics_fill_rect(ctx, GRect(4, 1, bounds.size.w-8, bounds.size.h-8), 5, GCornersAll);
	
	graphics_draw_bitmap_in_rect(ctx, s_bmpBottom, GRect(25, bounds.size.h-6, 102, 5));
	graphics_context_set_stroke_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
	graphics_context_set_text_color(ctx, CfgData.inv ? GColorWhite : GColorBlack);
	
	//Different Modes
	time_t tmAkt = time(NULL);
	struct tm *t = localtime(&tmAkt);
	char sTemp[] = "00:00:00";
	
	if (s_MultiMode == 0)
	{
		graphics_draw_line(ctx, GPoint(25, bounds.size.h-9), GPoint(42, bounds.size.h-9));
		
		//Hour+Minute
		if(clock_is_24h_style())
			strftime(sTemp, sizeof(sTemp), "%H:%M", t);
		else
			strftime(sTemp, sizeof(sTemp), "%I:%M", t);
		
		GSize sz = graphics_text_layout_get_content_size(sTemp, digitL, bounds, GTextOverflowModeFill, GTextAlignmentCenter);
		graphics_draw_text(ctx, sTemp, digitL, GRect(center.x-sz.w/2-15, center.y-sz.h/2-10, sz.w, sz.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

		//AM/PM/24h
		if(clock_is_24h_style())
			strcpy(sTemp, "24");
		else
			strcpy(sTemp, t->tm_hour < 12 ? "A" : "P");
		
		sz = graphics_text_layout_get_content_size(sTemp, digitS, bounds, GTextOverflowModeFill, GTextAlignmentCenter);
		graphics_draw_text(ctx, sTemp, digitS, GRect(8, 4, sz.w, sz.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
	} 
	else if (s_MultiMode == 1)
	{
		graphics_draw_line(ctx, GPoint(63, bounds.size.h-9), GPoint(79, bounds.size.h-9));
		
		//Date
		strftime(sTemp, sizeof(sTemp), 
			CfgData.datefmt == 1 ? "%d-%m" : 
			CfgData.datefmt == 2 ? "%d/%m" : 
			CfgData.datefmt == 3 ? "%m/%d" : 
			CfgData.datefmt == 4 ? "%m-%d" : "%d.%m", t);

		GSize sz = graphics_text_layout_get_content_size(sTemp, digitL, bounds, GTextOverflowModeFill, GTextAlignmentCenter);
		graphics_draw_text(ctx, sTemp, digitL, GRect(center.x-sz.w/2-15, center.y-sz.h/2-10, sz.w, sz.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

		//Weekday
		char sTemp2[] = "00";
		strftime(sTemp, sizeof(sTemp), "%a", t);
		upcase(sTemp);
		strncpy(sTemp2, sTemp, sizeof(sTemp2));
		sTemp2[2] = '\0';
		
		sz = graphics_text_layout_get_content_size(sTemp2, digitM, bounds, GTextOverflowModeFill, GTextAlignmentCenter);
		graphics_draw_text(ctx, sTemp2, digitM, GRect(center.x-sz.w/2+45, center.y-sz.h/2-5, sz.w, sz.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
	}
	else //if (s_MultiMode == 2)
	{
		graphics_draw_line(ctx, GPoint(95, bounds.size.h-9), GPoint(126, bounds.size.h-9));
		
		snprintf(sTemp, sizeof(sTemp), "%d", (int16_t)((double)CfgData.w_temp * (CfgData.isunit ? 1.8 : 1) + (CfgData.isunit ? 32 : 0))); //°C or °F?
		graphics_draw_text(ctx, sTemp, digitM, GRect(center.x-70, center.y-20 + (CfgData.weather ? 5 : 0), 55, 32), GTextOverflowModeFill, GTextAlignmentRight, NULL);
		graphics_draw_text(ctx, CfgData.isunit ? "`" : "_", WeatherF, GRect(center.x-13, center.y-26 + (CfgData.weather ? 5 : 0), 18, 32), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

		GRect rc = GRect(center.x+15, center.y-20 + (CfgData.weather ? 0 : 0), 34, 34);
		GSize sz = graphics_text_layout_get_content_size(CfgData.w_icon, WeatherF, rc, GTextOverflowModeFill, GTextAlignmentCenter);

		if (strcmp(CfgData.w_icon, "-") == 0) //Simulate Dark Clouds
		{
			graphics_draw_text(ctx, "!", WeatherF, GRect(rc.origin.x+rc.size.w/2-sz.w/2, rc.origin.y+rc.size.h/2-sz.h/2, sz.w, sz.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
			graphics_draw_text(ctx, "!", WeatherF, GRect(rc.origin.x+rc.size.w/2-sz.w/2+4, rc.origin.y+rc.size.h/2-sz.h/2-4, sz.w, sz.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);
			
			//Fill front Cloud, disabled, as it crash on real pebble
			//itmap *fb = graphics_capture_frame_buffer(ctx);
			//tmapInfo bitmap_info = fill_bitmapinfo(fb);
			//fill4(bitmap_info, 141, 103, get_pixel(bitmap_info, 141, 103), get_pixel(bitmap_info, 136, 100));
			//fill4(bitmap_info, 147, 100, get_pixel(bitmap_info, 147, 100), get_pixel(bitmap_info, 136, 100));
			//aphics_release_frame_buffer(ctx, fb);
		}
		else
			graphics_draw_text(ctx, CfgData.w_icon, WeatherF, GRect(rc.origin.x+rc.size.w/2-sz.w/2, rc.origin.y+rc.size.h/2-sz.h/2, sz.w, sz.h), GTextOverflowModeFill, GTextAlignmentCenter, NULL);

		//Condition
		if (CfgData.weather)
		{
			//snprintf(CfgData.w_cond, sizeof(CfgData.w_cond), "Langer Text %dx%d mit haufen anderen zeichen", sz.w, sz.h);
			sz = graphics_text_layout_get_content_size(CfgData.w_cond, digitS, GRect(10, 0, bounds.size.w-20, 10), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft);
			graphics_fill_rect(ctx, GRect(10, 1, sz.w, sz.h), 0, GCornerNone);
			graphics_draw_text(ctx, CfgData.w_cond, digitS, GRect(10, 0, sz.w, sz.h), GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
		}
	}
}
//-----------------------------------------------------------------------------------------------------------------------
static void timerCallbackSubSec(void *data) 
{
	s_SSSub++;
	timer_subsec = app_timer_register(1000/CfgData.substep, timerCallbackSubSec, NULL);

	if (CfgData.showsec != 0 && ((s_SSSub / CfgData.substep) % CfgData.showsec) == 0)
	{	
		if (!window_stack_contains_window(window_sec))
		{
			Layer *window_sec_layer = window_get_root_layer(window_sec);
			layer_remove_from_parent(second_layer);
			layer_add_child(window_sec_layer, second_layer);	
			window_stack_push(window_sec, false);
		}
		else
			layer_mark_dirty(second_layer);
	}
}
//-----------------------------------------------------------------------------------------------------------------------
void tick_handler(struct tm *tick_time, TimeUnits units_changed)
{
	s_HH = tick_time->tm_hour;
	s_MM = tick_time->tm_min;
	s_SS = tick_time->tm_sec;
	
	//Moduswechsel alle 10 sec
	if (CfgData.automode && (s_SS % 10) == 0)
	{
		s_MultiMode = s_MultiMode < 2 ? s_MultiMode+1 : 0;
	
		if (s_SS != 0) //Update done below
			update_all();
	}
	
	//alle 5 sec die zeigerposition korrigieren
	if ((CfgData.showsec != 0 && (s_SS % 5) == 0) || units_changed == MINUTE_UNIT)
		s_SSSub = s_SS*CfgData.substep; //Correct the Sec Hand
	
	if (s_SS == 0 || units_changed == MINUTE_UNIT)
	{
		update_all();
		
		//Hourly vibrate
		if (CfgData.vibr && tick_time->tm_min == 0)
			vibes_double_pulse();
	}
	else if (false && s_MultiMode == 2) //Weather Icon test
	{
		char Icons[] = "I\"!-$+F9=N#!-$,F9>h";
		CfgData.w_icon[0] = Icons[s_SS % 19];
		update_all();
	}
}
//-----------------------------------------------------------------------------------------------------------------------
void battery_state_service_handler(BatteryChargeState charge_state) 
{
	s_CurrBatt = charge_state.charge_percent/10;
	s_BattChrg = charge_state.is_charging;
	update_all();
}
//-----------------------------------------------------------------------------------------------------------------------
void bluetooth_connection_handler(bool connected)
{
	s_RadioConn = connected;
	update_all();
	
	if (!connected && CfgData.vibr_bt)
		vibes_enqueue_custom_pattern(vibe_pat_bt); 	
}
//-----------------------------------------------------------------------------------------------------------------------
static void tap_handler(AccelAxisType axis, int32_t direction) 
{
	if (CfgData.automode)
		return;

	s_MultiMode = s_MultiMode < 2 ? s_MultiMode+1 : 0;

	update_all();	
}
//-----------------------------------------------------------------------------------------------------------------------
static bool update_weather() 
{
	strcpy(CfgData.w_icon, "h");
	update_all();
	
	DictionaryIterator *iter;
	app_message_outbox_begin(&iter);

	if (iter == NULL) 
	{
		app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Iter is NULL!");
		return false;
	};

	Tuplet val_cityid = TupletInteger(C_CITYID, CfgData.cityid);
	dict_write_tuplet(iter, &val_cityid);
	dict_write_end(iter);

	app_message_outbox_send();
	app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Send message with data: c_cityid=%d", (int)CfgData.cityid);
	return true;
}
//-----------------------------------------------------------------------------------------------------------------------
static void timerCallbackWeather(void *data) 
{
	if (w_UpdateRetry && s_RadioConn)
	{
		update_weather();
		timer_weather = app_timer_register(30000, timerCallbackWeather, NULL); //Try again in 30 sec
	}
	else
	{
		w_UpdateRetry = true;
		timer_weather = app_timer_register(60000*60, timerCallbackWeather, NULL); //1h static update
	}
}
//-----------------------------------------------------------------------------------------------------------------------
static void update_configuration(void)
{	
	update_all();
	if (persist_exists(PK_SETTINGS))
		persist_read_data(PK_SETTINGS, &CfgData, sizeof(CfgData));
	
	Layer *window_layer = window_get_root_layer(window);
	layer_remove_from_parent(second_layer);
	if (CfgData.showsec != 0)
		layer_add_child(window_layer, second_layer);	

	layer_remove_from_parent(effect_layer_get_layer(inv_layer));
	if (CfgData.inv)
		layer_add_child(window_layer, effect_layer_get_layer(inv_layer));	
	
	//Get a time structure so that it doesn't start blank
	time_t tmAkt = time(NULL);
	//Manually call the tick handler when the window is loading
	tick_handler(localtime(&tmAkt), MINUTE_UNIT);

	//Set Battery state
	battery_state_service_handler(battery_state_service_peek());
	
	//Set Bluetooth state
	bluetooth_connection_handler(bluetooth_connection_service_peek());

	//Enable Light on Debug
	//light_enable(true);
}
//-----------------------------------------------------------------------------------------------------------------------
void in_received_handler(DictionaryIterator *received, void *context) 
{
	app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Received Data: ");
    time_t tmAkt = time(NULL);

	bool bSafeConfig = false;
	Tuple *akt_tuple = dict_read_first(received);
    while (akt_tuple)
    {
		int intVal = atoi(akt_tuple->value->cstring);
        app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "KEY %d=%d/%s/%d", (int16_t)akt_tuple->key, akt_tuple->value->int16 ,akt_tuple->value->cstring, intVal);

		switch (akt_tuple->key)	{
		case JS_READY:
			w_UpdateRetry = true;
			break;
		case C_INV:
			bSafeConfig = true;
			CfgData.inv = strcmp(akt_tuple->value->cstring, "yes") == 0;
			break;
		case C_AUTO_SW:
			CfgData.automode = strcmp(akt_tuple->value->cstring, "yes") == 0;
			break;
		case C_VIBR:
			CfgData.vibr = strcmp(akt_tuple->value->cstring, "yes") == 0;
			break;
		case C_VIBR_BT:
			CfgData.vibr_bt = strcmp(akt_tuple->value->cstring, "yes") == 0;
			break;
		case C_SHOWSEC:
			CfgData.showsec = 
				strcmp(akt_tuple->value->cstring, "nev") == 0 ? 0 : 
				strcmp(akt_tuple->value->cstring, "05s") == 0 ? 5 : 
				strcmp(akt_tuple->value->cstring, "10s") == 0 ? 10 : 
				strcmp(akt_tuple->value->cstring, "15s") == 0 ? 15 : 
				strcmp(akt_tuple->value->cstring, "30s") == 0 ? 30 : 1;
			CfgData.substep = strcmp(akt_tuple->value->cstring, "smo") == 0 ? 5 : 1;
			break;
		case C_DATEFMT:
			CfgData.datefmt = 
				strcmp(akt_tuple->value->cstring, "fra") == 0 ? 1 : 
				strcmp(akt_tuple->value->cstring, "eng") == 0 ? 2 : 
				strcmp(akt_tuple->value->cstring, "usa") == 0 ? 3 : 
				strcmp(akt_tuple->value->cstring, "iso") == 0 ? 4 : 0;
			break;
		case C_WEATHER:
			CfgData.weather = strcmp(akt_tuple->value->cstring, "yes") == 0;
			break;
		case C_UNITS:
			CfgData.isunit = strcmp(akt_tuple->value->cstring, "f") == 0;
			break;
		case C_CITYID:
			if ((int)CfgData.cityid != intVal) //City Changed, force reload weather
			{
				w_UpdateRetry = true;
				CfgData.w_time = 0;
			}

			CfgData.cityid = intVal;
			break;
		case W_TEMP:
			bSafeConfig = true;
			CfgData.w_time = tmAkt;
			CfgData.w_temp = intVal;
			w_UpdateRetry = false; //Update successful, usual update wait time
			break;
		case W_ICON:
			CfgData.w_icon[0] = akt_tuple->value->cstring[0];
			break;
		case W_CITY:
			strncpy(CfgData.w_city, akt_tuple->value->cstring, sizeof(CfgData.w_city));
			CfgData.w_city[sizeof(CfgData.w_city)-1] = '\n'; //Failsafe
			break;
		case W_COND:
			strncpy(CfgData.w_cond, akt_tuple->value->cstring, sizeof(CfgData.w_cond));
			CfgData.w_cond[sizeof(CfgData.w_cond)-1] = '\n'; //Failsafe
			break;
		}
		
		akt_tuple = dict_read_next(received);
	}
	
	//Save Configuration
	if (bSafeConfig)
	{
		int result = persist_write_data(PK_SETTINGS, &CfgData, sizeof(CfgData));
		app_log(APP_LOG_LEVEL_DEBUG, __FILE__, __LINE__, "Wrote %d bytes into settings", result);
		update_configuration();
	}
	
	//Update Weather
	if (w_UpdateRetry)
	{
		if (CfgData.w_time == 0 || (tmAkt - CfgData.w_time) > 60*60)
			timer_weather = app_timer_register(100, timerCallbackWeather, NULL);
		else
			timer_weather = app_timer_register((60*60-(tmAkt-CfgData.w_time))*1000, timerCallbackWeather, NULL);
	}
}
//-----------------------------------------------------------------------------------------------------------------------
void in_dropped_handler(AppMessageResult reason, void *ctx)
{
    app_log(APP_LOG_LEVEL_WARNING,
            __FILE__,
            __LINE__,
            "Message dropped, reason code %d",
            reason);
}
//-----------------------------------------------------------------------------------------------------------------------
void main_window_load(Window *window)
{
	//https://github.com/ron064/classio-battery-connection-Economy
	Layer *window_layer = window_get_root_layer(window);
	layer_set_update_proc(window_layer, NULL);
	GRect bounds = layer_get_bounds(window_layer);

	hour_arrow = gpath_create(&HOUR_HAND_POINTS);
	minute_arrow = gpath_create(&MINUTE_HAND_POINTS);
	
	//Init clock Layer
	clock_layer = layer_create(GRect(0, 0, bounds.size.w, 121));
	layer_set_update_proc(clock_layer, clock_update_proc);
	layer_add_child(window_layer, clock_layer);	
	
	//Init multi Layer
	s_bmpBottom = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BOTTOM);
	multi_layer = layer_create(GRect(0, 121, bounds.size.w, bounds.size.h-121));
	layer_set_update_proc(multi_layer, multi_update_proc);
	layer_add_child(window_layer, multi_layer);	
	
	//Init second Layer
	second_layer = layer_create(bounds);
	layer_set_update_proc(second_layer, second_update_proc);
	
	//Inverter Tayer
	inv_layer = effect_layer_create(GRect(25, bounds.size.h-6, 102, 5));
	effect_layer_add_effect(inv_layer, effect_invert_bw_only, NULL);

	//Set active Configuration
	update_configuration();
	
	//Start Second Timer
	timer_subsec = app_timer_register(1000/CfgData.substep, timerCallbackSubSec, NULL);
}
//-----------------------------------------------------------------------------------------------------------------------
void main_window_unload(Window *window)
{
	//Destroy Bitmaps
	gbitmap_destroy(s_bmpBottom);
	if (s_biBackground.bitmap)
		gbitmap_destroy(s_biBackground.bitmap);
	
	//Destroy Layers
	layer_destroy(multi_layer);
	layer_destroy(clock_layer);
}
//-----------------------------------------------------------------------------------------------------------------------
void handle_init(void) 
{
	s_MultiMode = 0;
	w_UpdateRetry = false; //Wait for JS_READY
	s_biBackground.bitmap = NULL;
	
	char* sLocale = setlocale(LC_TIME, ""), sLang[3];
	if (strncmp(sLocale, "en", 2) == 0)
		strcpy(sLang, "en");
	else if (strncmp(sLocale, "de", 2) == 0)
		strcpy(sLang, "de");
	else if (strncmp(sLocale, "es", 2) == 0)
		strcpy(sLang, "es");
	else if (strncmp(sLocale, "fr", 2) == 0)
		strcpy(sLang, "fr");
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Time locale is set to: %s/%s", sLocale, sLang);

	digitS = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_ARIAL_BOLD_9));
	digitM = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIGITAL_BOLD_27));
	digitL = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_DIGITAL_BOLD_37));
 	WeatherF = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_FONT_WEATHER_32));

	window = window_create();
	window_set_window_handlers(window, (WindowHandlers) {
		.load = main_window_load,
		.unload = main_window_unload,
	});
    window_stack_push(window, true);

	window_sec = window_create();
	window_set_background_color(window_sec, GColorClear);
	Layer *window_sec_layer = window_get_root_layer(window_sec);
	layer_set_update_proc(window_sec_layer, secwnd_update_proc);

	//Subscribe services
	tick_timer_service_subscribe(SECOND_UNIT, (TickHandler)tick_handler);
	battery_state_service_subscribe(&battery_state_service_handler);
	bluetooth_connection_service_subscribe(&bluetooth_connection_handler);
	accel_tap_service_subscribe(&tap_handler);
	
	//Subscribe messages
	app_message_register_inbox_received(in_received_handler);
    app_message_register_inbox_dropped(in_dropped_handler);
	app_message_open(512, 128);
}
//-----------------------------------------------------------------------------------------------------------------------
void handle_deinit(void) 
{
	app_timer_cancel(timer_subsec);
	tick_timer_service_unsubscribe();
	battery_state_service_unsubscribe();
	bluetooth_connection_service_unsubscribe();
	accel_tap_service_unsubscribe();

	layer_destroy(second_layer);
  	window_destroy(window_sec);
  	window_destroy(window);

	fonts_unload_custom_font(digitS);
	fonts_unload_custom_font(digitM);
	fonts_unload_custom_font(digitL);
	fonts_unload_custom_font(WeatherF);
}
//-----------------------------------------------------------------------------------------------------------------------
int main(void)
{
	handle_init();
	app_event_loop();
	handle_deinit();
}
//-----------------------------------------------------------------------------------------------------------------------