/*
    Qalculate (GTK+ UI)

    Copyright (C) 2003-2007, 2008, 2016  Hanna Knutsson (hanna.knutsson@protonmail.com)

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
*/


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <sys/stat.h>

#include "support.h"
#include "interface.h"
#include "callbacks.h"
#include "main.h"

MathStructure *mstruct, *matrix_mstruct, *parsed_mstruct, *parsed_tostruct, *displayed_mstruct;
string *parsed_to_str;
KnownVariable *vans[5];
GtkWidget *functions_window;
string selected_function_category;
MathFunction *selected_function;
GtkWidget *variables_window;
string selected_variable_category;
Variable *selected_variable;
string result_text, parsed_text;
GtkWidget *units_window;
string selected_unit_category;
string selected_unit_selector_category;
Unit *selected_unit, *selected_to_unit;
bool load_global_defs, fetch_exchange_rates_at_startup, first_time, showing_first_time_message, allow_multiple_instances;
cairo_surface_t *surface_result;
GdkPixbuf *pixbuf_result;
extern bool b_busy, b_busy_command, b_busy_result, b_busy_expression;
extern vector<string> recent_functions_pre;
extern vector<string> recent_variables_pre;
extern vector<string> recent_units_pre;
extern GtkWidget *expression;
extern GtkWidget *resultview;
extern PrintOptions printops;

MathFunction *f_answer;
MathFunction *f_expression;

GtkBuilder *main_builder, *argumentrules_builder, *csvimport_builder, *csvexport_builder, *setbase_builder, *datasetedit_builder, *datasets_builder, *decimals_builder;
GtkBuilder *functionedit_builder, *functions_builder, *matrixedit_builder, *matrix_builder, *namesedit_builder, *nbases_builder, *plot_builder, *precision_builder;
GtkBuilder *preferences_builder, *unit_builder, *unitedit_builder, *units_builder, *unknownedit_builder, *variableedit_builder, *variables_builder;
GtkBuilder *periodictable_builder, *simplefunctionedit_builder, *percentage_builder, *calendarconversion_builder;

Thread *view_thread, *command_thread;
string calc_arg;

bool do_timeout, check_expression_position;
gint expression_position;

QalculateDateTime last_version_check_date;

static GOptionEntry options[] = {
	{"new-instance", 'n', 0, G_OPTION_ARG_NONE, NULL, N_("Start a new instance of the application"), NULL},
	{"version", 'v', 0, G_OPTION_ARG_NONE, NULL, N_("Display the application version"), NULL},
	{G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, NULL, N_("Expression to calculate"), N_("EXPRESSION")},
	{NULL}
};

void create_application(GtkApplication *app) {

#ifdef _WIN32
	char exepath[MAX_PATH];
	GetModuleFileName(NULL, exepath, MAX_PATH);
	string datadir(exepath);
	gchar *icon_path;
	if(datadir.find("\\.libs") != string::npos) icon_path = g_build_filename(getPackageDataDir().c_str(), "data", "qalculate.png", NULL);
	else icon_path = g_build_filename(getPackageDataDir().c_str(), "pixmaps", "qalculate.png", NULL);
#else
	gchar *icon_path = g_build_filename(getPackageDataDir().c_str(), "pixmaps", "qalculate.png", NULL);
#endif
	gtk_window_set_default_icon_from_file(icon_path, NULL);
	g_free(icon_path);
	
	b_busy = false; 
	b_busy_result = false;
	b_busy_expression = false;
	b_busy_command = false;

	main_builder = NULL; argumentrules_builder = NULL; 
	csvimport_builder = NULL; datasetedit_builder = NULL; datasets_builder = NULL; decimals_builder = NULL; functionedit_builder = NULL; 
	functions_builder = NULL; matrixedit_builder = NULL; matrix_builder = NULL; namesedit_builder = NULL; nbases_builder = NULL; plot_builder = NULL; 
	precision_builder = NULL; preferences_builder = NULL; unit_builder = NULL; percentage_builder = NULL; 
	unitedit_builder = NULL; units_builder = NULL; unknownedit_builder = NULL; variableedit_builder = NULL; 
	variables_builder = NULL; csvexport_builder = NULL; setbase_builder = NULL; periodictable_builder = NULL, simplefunctionedit_builder = NULL;

	//create the almighty Calculator object
	new Calculator();
	
	CALCULATOR->setExchangeRatesWarningEnabled(false);
	
	//load application specific preferences
	load_preferences();

	mstruct = new MathStructure();
	displayed_mstruct = new MathStructure();
	parsed_mstruct = new MathStructure();
	parsed_tostruct = new MathStructure();
	parsed_tostruct->setUndefined();
	matrix_mstruct = new MathStructure();
	parsed_to_str = new string;

	bool canplot = CALCULATOR->canPlot();
	bool canfetch = CALCULATOR->canFetch();

	//create main window
	create_main_window();

	g_application_set_default(G_APPLICATION(app));
	gtk_window_set_application(GTK_WINDOW(gtk_builder_get_object(main_builder, "main_window")), app);	

	while(gtk_events_pending()) gtk_main_iteration();

	showing_first_time_message = first_time;

	if(!calc_arg.empty()) {
		gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_builder_get_object(main_builder, "expressionbuffer")), calc_arg.c_str(), -1);
	} else if(first_time) {		
		PangoLayout *layout = gtk_widget_create_pango_layout(GTK_WIDGET(gtk_builder_get_object(main_builder, "resultview")), NULL);
		GdkRGBA rgba;
#if GDK_MAJOR_VERSION > 3 || GDK_MINOR_VERSION >= 22
		GdkDrawingContext *gdc = gdk_window_begin_draw_frame(gtk_widget_get_window(GTK_WIDGET(gtk_builder_get_object(main_builder, "resultview"))), cairo_region_create());
		cairo_t *cr = gdk_drawing_context_get_cairo_context(gdc);
#else
		cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(GTK_WIDGET(gtk_builder_get_object(main_builder, "resultview"))));
#endif
		pango_layout_set_markup(layout, _("<span font=\"10\">Type a mathematical expression above, e.g. \"5 + 2 / 3\",\nand press the enter key.</span>"), -1);
		gtk_style_context_get_color(gtk_widget_get_style_context(GTK_WIDGET(gtk_builder_get_object(main_builder, "resultview"))), gtk_widget_get_state_flags(GTK_WIDGET(gtk_builder_get_object(main_builder, "resultview"))), &rgba);
		cairo_move_to(cr, 6, 0);
		gdk_cairo_set_source_rgba(cr, &rgba);
		pango_cairo_show_layout(cr, layout);
		g_object_unref(layout);
#if GDK_MAJOR_VERSION > 3 || GDK_MINOR_VERSION >= 22
		gdk_window_end_draw_frame(gtk_widget_get_window(GTK_WIDGET(gtk_builder_get_object(main_builder, "resultview"))), gdc);
#else
		cairo_destroy(cr);
#endif
	}

	while(gtk_events_pending()) gtk_main_iteration();	

	//exchange rates
	
	if(fetch_exchange_rates_at_startup && canfetch) {
		fetch_exchange_rates(5);
		while(gtk_events_pending()) gtk_main_iteration();
	}
	CALCULATOR->loadExchangeRates();

	string ans_str = _("ans");
	vans[0] = (KnownVariable*) CALCULATOR->addVariable(new KnownVariable(CALCULATOR->temporaryCategory(), ans_str, m_undefined, _("Last Answer"), false));
	vans[0]->addName(_("answer"));
	vans[0]->addName(ans_str + "1");
	vans[1] = (KnownVariable*) CALCULATOR->addVariable(new KnownVariable(CALCULATOR->temporaryCategory(), ans_str + "2", m_undefined, _("Answer 2"), false));
	vans[2] = (KnownVariable*) CALCULATOR->addVariable(new KnownVariable(CALCULATOR->temporaryCategory(), ans_str + "3", m_undefined, _("Answer 3"), false));
	vans[3] = (KnownVariable*) CALCULATOR->addVariable(new KnownVariable(CALCULATOR->temporaryCategory(), ans_str + "4", m_undefined, _("Answer 4"), false));
	vans[4] = (KnownVariable*) CALCULATOR->addVariable(new KnownVariable(CALCULATOR->temporaryCategory(), ans_str + "5", m_undefined, _("Answer 5"), false));

	//load global definitions
	if(load_global_defs && !CALCULATOR->loadGlobalDefinitions()) {
		g_print(_("Failed to load global definitions!\n"));
	}
	
	f_answer = CALCULATOR->addFunction(new AnswerFunction());
	f_expression = CALCULATOR->addFunction(new ExpressionFunction());

	//load local definitions
	CALCULATOR->loadLocalDefinitions();

	//reset
	functions_window = NULL;
	selected_function_category = _("All");
	selected_function = NULL;
	variables_window = NULL;
	selected_variable_category = _("All");
	selected_variable = NULL;
	units_window = NULL;
	selected_unit_category = _("All");
	selected_unit = NULL;
	selected_to_unit = NULL;
	result_text = "0";
	parsed_text = "0";
	surface_result = NULL;
	pixbuf_result = NULL;

	//check for calculation errros regularly
	do_timeout = true;
	g_timeout_add_seconds(1, on_display_errors_timeout, NULL);
	
	check_expression_position = true;
	expression_position = 1;
	//g_timeout_add(50, on_check_expression_position_timeout, NULL);
	
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object (main_builder, "menu_item_plot_functions")), canplot);
	gtk_widget_set_sensitive(GTK_WIDGET(gtk_builder_get_object (main_builder, "menu_item_fetch_exchange_rates")), canfetch);

	//create button menus after definitions have been loaded
	create_button_menus();

	//create dynamic menus
	generate_units_tree_struct();

	generate_functions_tree_struct();
	generate_variables_tree_struct();
	create_fmenu();
	create_vmenu();
	create_umenu();
	//create_pmenu();
	create_umenu2();
	create_pmenu2();

	update_unit_selector_tree();

	for(int i = ((int) recent_functions_pre.size()) - 1; i >= 0; i--) {
		function_inserted(CALCULATOR->getActiveFunction(recent_functions_pre[i]));
	}
	for(int i = ((int) recent_variables_pre.size()) - 1; i >= 0; i--) {
		variable_inserted(CALCULATOR->getActiveVariable(recent_variables_pre[i]));
	}
	for(int i = ((int) recent_units_pre.size()) - 1; i >= 0; i--) {
		Unit *u = CALCULATOR->getActiveUnit(recent_units_pre[i]);
		if(!u) u = CALCULATOR->getCompositeUnit(recent_units_pre[i]);
		unit_inserted(u);
	}

	update_completion();
	
	view_thread = new ViewThread;
	view_thread->start();
	command_thread = new CommandThread;
	
	if(!calc_arg.empty()) {
		execute_expression();
	} else if(!first_time) {
		int base = printops.base;
		printops.base = 10;
		setResult(NULL, false, false, false);
		printops.base = base;
	}
	gtk_widget_queue_draw(resultview);

	gchar *gstr = g_build_filename(getLocalDir().c_str(), "accelmap", NULL);
#ifndef _WIN32
	if(!fileExists(gstr)) {
		g_free(gstr);
		gstr = g_build_filename(getOldLocalDir().c_str(), "accelmap", NULL);
		gtk_accel_map_load(gstr);
		g_remove(gstr);
		g_rmdir(getOldLocalDir().c_str());
		g_free(gstr);
		return;
	}
#endif
	gtk_accel_map_load(gstr);
	g_free(gstr);
	
#ifdef _WIN32
	QalculateDateTime next_version_check_date(last_version_check_date);
	next_version_check_date.addDays(14);
	if(!next_version_check_date.isFutureDate()) g_idle_add(on_check_version_idle, NULL);
#endif
	
}

static void qalculate_activate(GtkApplication *app) {

	GList *list;

	list = gtk_application_get_windows (app);

	if(list) {
		gtk_window_present(GTK_WINDOW(list->data));
		return;
	}

	create_application(app);
}

static gint qalculate_handle_local_options(GtkApplication *app, GVariantDict *options_dict) {
	gboolean b = false;
	g_variant_dict_lookup(options_dict, "version", "b", &b);
	if(b) {
		g_printf(VERSION "\n");
		return 0;
	}
	g_variant_dict_lookup(options_dict, "new-instance", "b", &b);
	if(b) {
		g_application_set_flags(G_APPLICATION(app), G_APPLICATION_NON_UNIQUE);
	}
	allow_multiple_instances = false;
	FILE *file = NULL;
	gchar *gstr_oldfile = NULL;
	gchar *gstr_file = g_build_filename(getLocalDir().c_str(), "qalculate-gtk.cfg", NULL);
	file = fopen(gstr_file, "r");
	if(!file) {
#ifndef _WIN32
		gstr_oldfile = g_build_filename(getOldLocalDir().c_str(), "qalculate-gtk.cfg", NULL);
		file = fopen(gstr_oldfile, "r");
		if(!file) {
			g_free(gstr_oldfile);
#endif
			g_free(gstr_file);
			return -1;
#ifndef _WIN32
		}
#endif
	}	
	if(file) {
		char line[100];
		string stmp, svar;
		size_t i;
		while(true) {
			if(fgets(line, 100, file) == NULL)
				break;
			stmp = line;
			remove_blank_ends(stmp);
			if((i = stmp.find_first_of("=")) != string::npos) {
				svar = stmp.substr(0, i);
				remove_blank_ends(svar);
				if(svar == "allow_multiple_instances") {
					string svalue = stmp.substr(i + 1, stmp.length() - (i + 1));
					remove_blank_ends(svalue);
					allow_multiple_instances = s2i(svalue);
					break;
				}
			}
		}
		fclose(file);
		if(gstr_oldfile) {
			recursiveMakeDir(getLocalDir());
			move_file(gstr_oldfile, gstr_file);
			g_free(gstr_oldfile);
		}
	}
	g_free(gstr_file);
	if(allow_multiple_instances) g_application_set_flags(G_APPLICATION(app), G_APPLICATION_NON_UNIQUE);
	return -1;
}

static gint qalculate_command_line(GtkApplication *app, GApplicationCommandLine *cmd_line) {
	GVariantDict *options_dict = g_application_command_line_get_options_dict(cmd_line);
	gchar **remaining = NULL;
	g_variant_dict_lookup(options_dict, G_OPTION_REMAINING, "^as", &remaining);
	calc_arg = "";
	for(int i = 0; remaining && i < (int) g_strv_length(remaining); i++) {
		if(i > 1) {
			calc_arg += " ";
		}
		if(strlen(remaining[i]) >= 2 && ((remaining[i][0] == '\"' && remaining[i][strlen(remaining[i]) - 1] == '\"') || (remaining[i][0] == '\'' && remaining[i][strlen(remaining[i]) - 1] == '\''))) {
			calc_arg += remaining[i] + 1;
			calc_arg.erase(calc_arg.length() - 1);
		} else {
			calc_arg += remaining[i];
		}
	}
	if(main_builder) {
		gtk_window_present(GTK_WINDOW(gtk_builder_get_object(main_builder, "main_window")));
		if(!calc_arg.empty()) {
			gtk_text_buffer_set_text(GTK_TEXT_BUFFER(gtk_builder_get_object(main_builder, "expressionbuffer")), calc_arg.c_str(), -1);
			execute_expression();
		}
	} else {
		create_application(app);
	}
	return 0;
}


int main (int argc, char *argv[]) {
	
	GtkApplication *app;
	gint status;
	
#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, getPackageLocaleDir().c_str());
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	setlocale(LC_ALL, "");

	app = gtk_application_new("org.gtk.qalculate", G_APPLICATION_HANDLES_COMMAND_LINE);
	g_application_add_main_option_entries(G_APPLICATION(app), options);
	g_signal_connect(app, "activate", G_CALLBACK(qalculate_activate), NULL);
	g_signal_connect(app, "handle_local_options", G_CALLBACK(qalculate_handle_local_options), NULL);
	g_signal_connect(app, "command_line", G_CALLBACK(qalculate_command_line), NULL);

	status = g_application_run(G_APPLICATION(app), argc, argv);

	g_object_unref(app);

	return status;
	
}



