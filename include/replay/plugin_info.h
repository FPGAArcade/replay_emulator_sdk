#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Replay Plugin Info and Metadata
//
// Plugins describe themselves with a nullptr-terminated array of tags: display name, description, version,
// author, menu path, button label, icon, hotkey, category, auto-load preference. All tag values are strings;
// the tag id says what the string means. A plugin provides whatever combination it needs.
//
// The types come from <replay/plugin/plugin_info_types.h>. What lives here are the pieces the IDL has no
// construct for: symbol visibility and the tag-array literal macros.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/core/platform.h>
#include <flowi/core/string.h>
#include <flowi/vfs/vfs_plugin.h>
#include <replay/plugin/plugin_info_types.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Plugin entry points must be visible to the host.
#if PLATFORM_WINDOWS
#define RP_PLUGIN_EXPORT __declspec(dllexport)
#elif COMPILER_GCC || COMPILER_CLANG
#define RP_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define RP_PLUGIN_EXPORT
#endif

// Plugin-local symbols must not conflict with symbols in the host.
#if COMPILER_GCC || COMPILER_CLANG
#define RP_PLUGIN_LOCAL __attribute__((visibility("hidden")))
#else
#define RP_PLUGIN_LOCAL
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper Macros
//
// Convenience macros for defining tag arrays.

// Define a tag entry
// Use S_() macro for MSVC-compatible static initialization
#define RP_PLUGIN_TAG(val, tag_type) { .value = S_(val), .tag = (tag_type) }

// Terminate a tag array
#define RP_PLUGIN_TAG_END { 0 }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Usage Examples
//
// Example 1: Minimal plugin with just name and version
//   static RpPluginInfo s_plugin_info[] = {
//       RP_PLUGIN_TAG("My Vfs Plugin", RpPluginTag_UserName),
//       RP_PLUGIN_TAG("1.0.0", RpPluginTag_Version),
//       RP_PLUGIN_TAG_END
//   };
//
//   static FlVfsPlugin s_my_vfs_plugin = {
//       // ...
//       .plugin_info = s_plugin_info,
//       // ...
//   };
//
// Example 2: Full plugin metadata with all tags
//   static RpPluginInfo s_plugin_info[] = {
//       RP_PLUGIN_TAG("System Monitor", RpPluginTag_UserName),
//       RP_PLUGIN_TAG("Real-time CPU and memory monitoring", RpPluginTag_Description),
//       RP_PLUGIN_TAG("Monitor", RpPluginTag_ButtonName),
//       RP_PLUGIN_TAG("Tools/Debug", RpPluginTag_MenuPath),
//       RP_PLUGIN_TAG("1.0.0", RpPluginTag_Version),
//       RP_PLUGIN_TAG("ReplayDev", RpPluginTag_Author),
//       RP_PLUGIN_TAG("Debug", RpPluginTag_Category),
//       RP_PLUGIN_TAG("monitor.png", RpPluginTag_Icon),
//       RP_PLUGIN_TAG("Ctrl+Shift+M", RpPluginTag_Hotkey),
//       RP_PLUGIN_TAG("0", RpPluginTag_AutoLoad),
//       RP_PLUGIN_TAG_END
//   };
//
// Example 3: VFS Driver plugin
//   static RpPluginInfo s_vfs_plugin_info[] = {
//       RP_PLUGIN_TAG("Local Filesystem", RpPluginTag_UserName),
//       RP_PLUGIN_TAG("Native local filesystem access", RpPluginTag_Description),
//       RP_PLUGIN_TAG("System", RpPluginTag_Category),
//       RP_PLUGIN_TAG("1.0.0", RpPluginTag_Version),
//       RP_PLUGIN_TAG_END
//   };
//
//   static FlVfsPlugin s_vfs_plugin = {
//       .api_version = FL_VFS_PLUGIN_API_VERSION,
//       .plugin_name = S("LocalFS"),
//       .plugin_info = s_vfs_plugin_info,
//       // ... operations ...
//   };
