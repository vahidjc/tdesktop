/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

extern "C" {
#undef signals
#include <libappindicator/app-indicator.h>
#include <gtk/gtk.h>
#define signals public
} // extern "C"

#include <unity/unity/unity.h>

namespace Platform {
namespace Libs {

void start();

template <typename Function>
bool load(QLibrary &lib, const char *name, Function &func) {
	func = nullptr;
	if (!lib.isLoaded()) {
		return false;
	}

    func = reinterpret_cast<Function>(lib.resolve(name));
    if (func) {
        return true;
    }
	LOG(("Error: failed to load '%1' function!").arg(name));
    return false;
}

typedef gboolean (*f_gtk_init_check)(int *argc, char ***argv);
extern f_gtk_init_check gtk_init_check;

typedef GtkWidget* (*f_gtk_menu_new)(void);
extern f_gtk_menu_new gtk_menu_new;

typedef GType (*f_gtk_menu_get_type)(void) G_GNUC_CONST;
extern f_gtk_menu_get_type gtk_menu_get_type;

typedef GtkWidget* (*f_gtk_menu_item_new_with_label)(const gchar *label);
extern f_gtk_menu_item_new_with_label gtk_menu_item_new_with_label;

typedef void (*f_gtk_menu_item_set_label)(GtkMenuItem *menu_item, const gchar *label);
extern f_gtk_menu_item_set_label gtk_menu_item_set_label;

typedef void (*f_gtk_menu_shell_append)(GtkMenuShell *menu_shell, GtkWidget *child);
extern f_gtk_menu_shell_append gtk_menu_shell_append;

typedef GType (*f_gtk_menu_shell_get_type)(void) G_GNUC_CONST;
extern f_gtk_menu_shell_get_type gtk_menu_shell_get_type;

typedef void (*f_gtk_widget_show)(GtkWidget *widget);
extern f_gtk_widget_show gtk_widget_show;

typedef GtkWidget* (*f_gtk_widget_get_toplevel)(GtkWidget *widget);
extern f_gtk_widget_get_toplevel gtk_widget_get_toplevel;

typedef gboolean (*f_gtk_widget_get_visible)(GtkWidget *widget);
extern f_gtk_widget_get_visible gtk_widget_get_visible;

typedef void (*f_gtk_widget_set_sensitive)(GtkWidget *widget, gboolean sensitive);
extern f_gtk_widget_set_sensitive gtk_widget_set_sensitive;

typedef GTypeInstance* (*f_g_type_check_instance_cast)(GTypeInstance *instance, GType iface_type);
extern f_g_type_check_instance_cast g_type_check_instance_cast;

template <typename Result, typename Object>
inline Result *g_type_cic_helper(Object *instance, GType iface_type) {
	return reinterpret_cast<Result*>(g_type_check_instance_cast(reinterpret_cast<GTypeInstance*>(instance), iface_type));
}
template <typename Object>
inline GtkMenu *gtk_menu_cast(Object *obj) {
	return g_type_cic_helper<GtkMenu, Object>(obj, gtk_menu_get_type());
}
template <typename Object>
inline GtkMenuShell *gtk_menu_shell_cast(Object *obj) {
	return g_type_cic_helper<GtkMenuShell, Object>(obj, gtk_menu_get_type());
}

typedef gulong (*f_g_signal_connect_data)(gpointer instance, const gchar *detailed_signal, GCallback c_handler, gpointer data, GClosureNotify destroy_data, GConnectFlags connect_flags);
extern f_g_signal_connect_data g_signal_connect_data;
inline gulong g_signal_connect_helper(gpointer instance, const gchar *detailed_signal, GCallback c_handler, gpointer data) {
	return g_signal_connect_data(instance, detailed_signal, c_handler, data, NULL, (GConnectFlags)0);
}

typedef AppIndicator* (*f_app_indicator_new)(const gchar *id, const gchar *icon_name, AppIndicatorCategory category);
extern f_app_indicator_new app_indicator_new;

typedef void (*f_app_indicator_set_status)(AppIndicator *self, AppIndicatorStatus status);
extern f_app_indicator_set_status app_indicator_set_status;

typedef void (*f_app_indicator_set_menu)(AppIndicator *self, GtkMenu *menu);
extern f_app_indicator_set_menu app_indicator_set_menu;

typedef void (*f_app_indicator_set_icon_full)(AppIndicator *self, const gchar *icon_name, const gchar *icon_desc);
extern f_app_indicator_set_icon_full app_indicator_set_icon_full;

typedef gboolean (*f_gdk_init_check)(gint *argc, gchar ***argv);
extern f_gdk_init_check gdk_init_check;

typedef GdkPixbuf* (*f_gdk_pixbuf_new_from_data)(const guchar *data, GdkColorspace colorspace, gboolean has_alpha, int bits_per_sample, int width, int height, int rowstride, GdkPixbufDestroyNotify destroy_fn, gpointer destroy_fn_data);
extern f_gdk_pixbuf_new_from_data gdk_pixbuf_new_from_data;

typedef GtkStatusIcon* (*f_gtk_status_icon_new_from_pixbuf)(GdkPixbuf *pixbuf);
extern f_gtk_status_icon_new_from_pixbuf gtk_status_icon_new_from_pixbuf;

typedef void (*f_gtk_status_icon_set_from_pixbuf)(GtkStatusIcon *status_icon, GdkPixbuf *pixbuf);
extern f_gtk_status_icon_set_from_pixbuf gtk_status_icon_set_from_pixbuf;

typedef GtkStatusIcon* (*f_gtk_status_icon_new_from_file)(const gchar *filename);
extern f_gtk_status_icon_new_from_file gtk_status_icon_new_from_file;

typedef void (*f_gtk_status_icon_set_from_file)(GtkStatusIcon *status_icon, const gchar *filename);
extern f_gtk_status_icon_set_from_file gtk_status_icon_set_from_file;

typedef void (*f_gtk_status_icon_set_title)(GtkStatusIcon *status_icon, const gchar *title);
extern f_gtk_status_icon_set_title gtk_status_icon_set_title;

typedef void (*f_gtk_status_icon_set_tooltip_text)(GtkStatusIcon *status_icon, const gchar *title);
extern f_gtk_status_icon_set_tooltip_text gtk_status_icon_set_tooltip_text;

typedef void (*f_gtk_status_icon_set_visible)(GtkStatusIcon *status_icon, gboolean visible);
extern f_gtk_status_icon_set_visible gtk_status_icon_set_visible;

typedef gboolean (*f_gtk_status_icon_is_embedded)(GtkStatusIcon *status_icon);
extern f_gtk_status_icon_is_embedded gtk_status_icon_is_embedded;

typedef gboolean (*f_gtk_status_icon_get_geometry)(GtkStatusIcon *status_icon, GdkScreen **screen, GdkRectangle *area, GtkOrientation *orientation);
extern f_gtk_status_icon_get_geometry gtk_status_icon_get_geometry;

typedef void (*f_gtk_status_icon_position_menu)(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, gpointer user_data);
extern f_gtk_status_icon_position_menu gtk_status_icon_position_menu;

typedef void (*f_gtk_menu_popup)(GtkMenu *menu, GtkWidget *parent_menu_shell, GtkWidget *parent_menu_item, GtkMenuPositionFunc func, gpointer data, guint button, guint32 activate_time);
extern f_gtk_menu_popup gtk_menu_popup;

typedef guint32 (*f_gtk_get_current_event_time)(void);
extern f_gtk_get_current_event_time gtk_get_current_event_time;

typedef gpointer (*f_g_object_ref_sink)(gpointer object);
extern f_g_object_ref_sink g_object_ref_sink;

typedef void (*f_g_object_unref)(gpointer object);
extern f_g_object_unref g_object_unref;

typedef guint (*f_g_idle_add)(GSourceFunc function, gpointer data);
extern f_g_idle_add g_idle_add;

typedef void (*f_unity_launcher_entry_set_count)(UnityLauncherEntry* self, gint64 value);
extern f_unity_launcher_entry_set_count unity_launcher_entry_set_count;

typedef void (*f_unity_launcher_entry_set_count_visible)(UnityLauncherEntry* self, gboolean value);
extern f_unity_launcher_entry_set_count_visible unity_launcher_entry_set_count_visible;

typedef UnityLauncherEntry* (*f_unity_launcher_entry_get_for_desktop_id)(const gchar* desktop_id);
extern f_unity_launcher_entry_get_for_desktop_id unity_launcher_entry_get_for_desktop_id;

} // namespace Libs
} // namespace Platform
