#include "include/recaster/recaster_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "recaster_plugin.h"

void RecasterPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  recaster::RecasterPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
