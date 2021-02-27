#pragma once

#define IMGUI_ENABLE
#define MAX_LOADSTRING ( 100 )

#include <combaseapi.h>
#include <comdef.h>
#include <wrl/client.h>
#include <shobjidl.h>
#include <queue>
#include <algorithm>
#include "resource.h"
#include "..\wintendoCore\common.h"
#include "..\wintendoCore\NesSystem.h"
#include "..\wintendoCore\input.h"
#include "..\wintendoCore\timer.h"
#ifdef IMGUI_ENABLE
#pragma warning(push, 0)        
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx12.h"
#include "imgui/imgui_memory_editor/imgui_memory_editor.h"
#pragma warning(pop)
#endif