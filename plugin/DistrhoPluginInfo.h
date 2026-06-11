#pragma once

#define DISTRHO_PLUGIN_BRAND       "Dubplex"
#define DISTRHO_PLUGIN_NAME        "Moses"
#define DISTRHO_PLUGIN_URI         "https://github.com/lfabre/moses-dpf"
#define DISTRHO_PLUGIN_CLAP_ID     "fr.nexus.moses"

#define DISTRHO_PLUGIN_NUM_INPUTS  2
#define DISTRHO_PLUGIN_NUM_OUTPUTS 2

#define DISTRHO_PLUGIN_IS_RT_SAFE  1
#ifndef DISTRHO_PLUGIN_HAS_UI
  #define DISTRHO_PLUGIN_HAS_UI    0
#endif
#define DISTRHO_PLUGIN_WANT_PROGRAMS 0
#define DISTRHO_PLUGIN_WANT_STATE    0

#define DISTRHO_UI_USE_NANOVG     1
