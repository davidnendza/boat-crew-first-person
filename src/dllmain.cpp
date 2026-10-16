// BoatCrewMod: first-person control of a crewmate in Boat Crew.
//
// Built as version.dll and placed next to "Boat Crew.exe". UnityPlayer.dll imports version.dll, so the game loads
// this copy, which forwards every export to the real one in System32 and starts one thread. That thread waits for
// the IL2CPP runtime, looks up the game's classes by name through the IL2CPP C API that GameAssembly.dll exports,
// and patches one Unity message, PlayerCameraControlManager.LateUpdate, to call the original and then run OnFrame.
// Nothing is hard-coded by address, so a game update only breaks it if the code itself is renamed.
//
//   Insert (BoatCrewMod/config.ini)  toggle first person for the crewmate you have selected: camera at their head,
//                                    mouse look, WASD walks them along the deck, Ctrl runs. Their own AI is held
//                                    off; the rest of the crew carry on. Esc frees the mouse for the menu. Clicking
//                                    another crewmate switches to them. Press the hotkey again to hand control back.
//
// Why not BepInEx or MelonLoader: both depend on Il2CppInterop, which crashes on Unity 6000.3 (IL2CPP metadata
// v39) as of this writing. See README.md for how each part works. Remove the mod by deleting version.dll.

#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

// ---- forward the real version.dll ------------------------------------------------------------------------
#define FWD(n) __pragma(comment(linker, "/export:" #n "=C:\\Windows\\System32\\version." #n))
FWD(GetFileVersionInfoA) FWD(GetFileVersionInfoW) FWD(GetFileVersionInfoExA) FWD(GetFileVersionInfoExW)
FWD(GetFileVersionInfoSizeA) FWD(GetFileVersionInfoSizeW) FWD(GetFileVersionInfoSizeExA) FWD(GetFileVersionInfoSizeExW)
FWD(VerFindFileA) FWD(VerFindFileW) FWD(VerInstallFileA) FWD(VerInstallFileW)
FWD(VerLanguageNameA) FWD(VerLanguageNameW) FWD(VerQueryValueA) FWD(VerQueryValueW)

// ---- log ---------------------------------------------------------------------------------------------------
static FILE* g_log = nullptr;
static void Log(const char* fmt, ...) {
  if (!g_log) return;
  SYSTEMTIME t; GetLocalTime(&t);
  fprintf(g_log, "[%02d:%02d:%02d.%03d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
  va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
  fputc('\n', g_log); fflush(g_log);
}

// ---- IL2CPP C API (exported by GameAssembly.dll) -------------------------------------------------------
typedef void* (*fn_domain_get)();
typedef void* (*fn_thread_attach)(void*);
typedef void** (*fn_domain_get_assemblies)(void*, size_t*);
typedef void* (*fn_assembly_get_image)(void*);
typedef const char* (*fn_image_get_name)(void*);
typedef void* (*fn_class_from_name)(void*, const char*, const char*);
typedef void* (*fn_class_get_method_from_name)(void*, const char*, int);
typedef void* (*fn_class_get_field_from_name)(void*, const char*);
typedef size_t (*fn_field_get_offset)(void*);
typedef void (*fn_field_static_get_value)(void*, void*);
typedef void* (*fn_runtime_invoke)(void*, void*, void**, void**);
typedef void* (*fn_object_unbox)(void*);
typedef void* (*fn_object_get_class)(void*);
typedef const char* (*fn_class_get_name)(void*);
typedef void* (*fn_class_get_methods)(void*, void**);
typedef const char* (*fn_method_get_name)(void*);
typedef uint32_t (*fn_method_get_param_count)(void*);
typedef const void* (*fn_method_get_param)(void*, uint32_t);
typedef const char* (*fn_type_get_name)(const void*);
typedef void* (*fn_class_get_fields)(void*, void**);
typedef const char* (*fn_field_get_name)(void*);
typedef const void* (*fn_field_get_type)(void*);
typedef void (*fn_field_static_set_value)(void*, void*);
typedef void* (*fn_string_new)(const char*);
typedef uint32_t (*fn_gchandle_new)(void*, int);

static fn_domain_get il2cpp_domain_get;
static fn_thread_attach il2cpp_thread_attach;
static fn_domain_get_assemblies il2cpp_domain_get_assemblies;
static fn_assembly_get_image il2cpp_assembly_get_image;
static fn_image_get_name il2cpp_image_get_name;
static fn_class_from_name il2cpp_class_from_name;
static fn_class_get_method_from_name il2cpp_class_get_method_from_name;
static fn_class_get_field_from_name il2cpp_class_get_field_from_name;
static fn_field_get_offset il2cpp_field_get_offset;
static fn_field_static_get_value il2cpp_field_static_get_value;
static fn_runtime_invoke il2cpp_runtime_invoke;
static fn_object_unbox il2cpp_object_unbox;
static fn_object_get_class il2cpp_object_get_class;
static fn_class_get_name il2cpp_class_get_name;
static fn_class_get_methods il2cpp_class_get_methods;
static fn_method_get_name il2cpp_method_get_name;
static fn_method_get_param_count il2cpp_method_get_param_count;
static fn_method_get_param il2cpp_method_get_param;
static fn_type_get_name il2cpp_type_get_name;
static fn_class_get_fields il2cpp_class_get_fields;
static fn_field_get_name il2cpp_field_get_name;
static fn_field_get_type il2cpp_field_get_type;
static fn_field_static_set_value il2cpp_field_static_set_value;
static fn_string_new il2cpp_string_new;
static fn_gchandle_new il2cpp_gchandle_new;

static HMODULE g_ga = nullptr;
static void* g_domain = nullptr;

static bool Resolve() {
  bool ok = true;
#define R(name) name = (decltype(name))GetProcAddress(g_ga, #name); if (!name) { Log("missing export: " #name); ok = false; }
  R(il2cpp_domain_get) R(il2cpp_thread_attach) R(il2cpp_domain_get_assemblies) R(il2cpp_assembly_get_image)
  R(il2cpp_image_get_name) R(il2cpp_class_from_name) R(il2cpp_class_get_method_from_name) R(il2cpp_class_get_field_from_name)
  R(il2cpp_field_get_offset) R(il2cpp_field_static_get_value) R(il2cpp_runtime_invoke) R(il2cpp_object_unbox)
  R(il2cpp_object_get_class) R(il2cpp_class_get_name) R(il2cpp_class_get_methods) R(il2cpp_method_get_name)
  R(il2cpp_method_get_param_count) R(il2cpp_method_get_param) R(il2cpp_type_get_name) R(il2cpp_string_new) R(il2cpp_gchandle_new) R(il2cpp_class_get_fields) R(il2cpp_field_get_name) R(il2cpp_field_get_type) R(il2cpp_field_static_set_value)
#undef R
  return ok;
}

// A runtime that is not ready yet can fault, so probes run behind SEH (these helpers hold no C++ objects).
static bool DomainReady(void** domain) {
  __try {
    void* d = il2cpp_domain_get();
    if (!d) return false;
    size_t n = 0;
    void** a = il2cpp_domain_get_assemblies(d, &n);
    if (!a || n < 10) return false;
    *domain = d; return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void* FindClass(const char* ns, const char* name) {
  size_t n = 0;
  void** asms = il2cpp_domain_get_assemblies(g_domain, &n);
  for (size_t i = 0; i < n; i++) {
    void* img = il2cpp_assembly_get_image(asms[i]);
    if (!img) continue;
    void* k = il2cpp_class_from_name(img, ns, name);
    if (k) return k;
  }
  return nullptr;
}

// ---- small math ------------------------------------------------------------------------------------------
struct V3 { float x, y, z; };
struct Q4 { float x, y, z, w; };
static const float kDeg = 57.29577951f, kRad = 0.01745329252f;

static Q4 QMul(Q4 a, Q4 b) {
  return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
static Q4 QEuler(float pitchDeg, float yawDeg) {                // Unity order: yaw about Y applied after pitch about X
  float hp = pitchDeg * kRad * 0.5f, hy = yawDeg * kRad * 0.5f;
  Q4 qx = { sinf(hp), 0, 0, cosf(hp) }, qy = { 0, sinf(hy), 0, cosf(hy) };
  return QMul(qy, qx);
}
static V3 QForward(Q4 q) {                                       // q * (0, 0, 1)
  V3 v = { 0, 0, 1 }, u = { q.x, q.y, q.z };
  V3 c1 = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
  V3 t = { c1.x + q.w * v.x, c1.y + q.w * v.y, c1.z + q.w * v.z };
  V3 c2 = { u.y * t.z - u.z * t.y, u.z * t.x - u.x * t.z, u.x * t.y - u.y * t.x };
  return { v.x + 2 * c2.x, v.y + 2 * c2.y, v.z + 2 * c2.z };
}

// ---- first person state ----------------------------------------------------------------------------------
static void *m_getMain, *m_getTransform, *m_getPosition, *m_getRotation, *m_setPosRot;   // MethodInfo*
static void *m_getTimeScale = nullptr, *m_getVisible = nullptr, *m_setVisible = nullptr;  // optional: pause detection, cursor
static bool g_looking = false;                                   // mouse look is currently driving the camera
static Q4 g_lastWritten = { 0, 0, 0, 1 };                        // the camera rotation we wrote last frame
static bool g_haveWritten = false;
static void* m_getBounds = nullptr;                              // Renderer.get_bounds
static int g_offCrewVisual = -1, g_offBodyArr = -1, g_offChest = -1, g_offCamTarget = -1, g_offCamFocus = -1;
static void* g_fieldPatherBlock = nullptr;                       // PlayerCrewPather.block (static int): the click-to-move input switch
static int g_savedBlock = 0;
static int g_geomFrames = -1;                                    // frames since possession, for the geometry log
static bool g_camFromBounds = false;                             // config.ini: camera=bounds (visible mesh) or head (headEnd transform, default)
// Boat steering input (PlayerShipController.movement / movementStick are InputActionReferences). Switched off while in
// first person so WASD no longer drives the boat, and back on when leaving.
static void *m_getAction = nullptr, *m_actEnable = nullptr, *m_actDisable = nullptr, *m_actEnabled = nullptr;
static void* g_fieldShipInstance = nullptr;
static int g_offMove = -1, g_offMoveStick = -1;
static bool g_boatInputOff = false;
static void* g_fieldSelected;                                    // FieldInfo* of PlayerCrewSelector.selectedCrewMember
static int g_offHeadEnd = -1, g_offDirection = -1;               // CrewMember.headEnd / .direction (Transform fields)
static bool g_ready = false, g_fp = false;
static void* g_crew = nullptr;                                   // crewmate we are looking through
static float g_yaw = 0, g_pitch = 0;
static int g_hotkey = VK_INSERT;                                 // from config.ini
static void* g_fieldConsole = nullptr;                           // ConsoleUtility.Console.s_Console (static)
static int g_offConsoleInput = -1, g_offConsoleVisible = -1;
static POINT g_center = { 0, 0 };
static int g_faults = 0;
static unsigned g_frame = 0;
static float g_sens = 0.12f;                                     // degrees of turn per mouse count (config.ini: sensitivity)
static void* g_fieldCamMode = nullptr;                           // PlayerCameraControlManager.controlMode (static)
static void* m_getLockState = nullptr;                           // Cursor.get_lockState, for the log
static bool g_menuLatch = false, g_sawPaused = false;            // Esc frees the mouse without leaving first person
// Taking control of a crewmate: the game's own switches keep its AI from moving it (blockReasons is checked in the
// per-frame crew updates, blockMovement in followPath), and we place it ourselves with setPositionSelfAndPawn.
static void *m_setPosSelfPawn = nullptr, *m_pathCancel = nullptr, *m_setSpeed = nullptr, *m_setRotation = nullptr, *m_raycast = nullptr;
static void *m_hsAdd = nullptr, *m_hsRemove = nullptr;            // HashSet<string>.Add / .Remove, resolved from the live object
static int g_offBlockMove = -1, g_offBlockReasons = -1, g_offVisual = -1, g_deckMask = 0;
static void* g_fieldPatherInst = nullptr;                         // PlayerCrewPather.instance (static)
static int g_offDeck = -1;                                        // PlayerCrewPather.deck (LayerMask)
static int g_runKey = VK_LCONTROL;                                // config.ini: runkey (Shift is the game's own magnify)
static void* g_reasonStr = nullptr;                               // "BoatCrewMod", kept alive with a GC handle
static LARGE_INTEGER g_qpcFreq = {}, g_qpcLast = {};
static int g_walkLogs = 0;
static float g_walkSpeed = 1.5f, g_runSpeed = 3.0f;              // metres per second (config.ini: walkspeed, runspeed)
static float g_eyeForward = 0.18f;                               // metres the camera sits ahead of headEnd (config.ini: eyeforward)
static void *m_getSpeedValue = nullptr, *m_getWalkingSpeed = nullptr, *m_getStance = nullptr;
static int g_offAnim = -1;                                        // CrewMember.crewMemberAnimation
static Q4 g_rootWritten = { 0, 0, 0, 1 };                        // the root rotation we wrote last frame
static bool g_rootHave = false;
static int g_rootDrift = 0, g_rootChecks = 0;
static DWORD g_lastProbe = 0;
static int g_offPawn = -1, g_offPawnBlock = -1, g_offTask = -1, g_offWorkingOn = -1, g_offPawnWorking = -1, g_offPawnStall = -1;
static void *m_pawnStalled = nullptr, *m_pawnTask = nullptr, *m_isRunning = nullptr, *m_isWorking = nullptr, *m_isProxyStalled = nullptr;
static void* m_setLockState = nullptr;                           // Cursor.set_lockState
static bool g_cursorHidden = false; static int g_lockBefore = 0;
static const float kStepLimit = 0.6f;                             // refuse a step if the deck under it is this much higher or lower
static volatile bool g_rawActive = false;                        // raw mouse deltas are being accumulated for the camera
static volatile LONG g_rawDx = 0, g_rawDy = 0, g_rawEvents = 0;

// ---- settings ----------------------------------------------------------------------------------------------
static int KeyFromName(const char* s) {
  if ((s[0] == 'F' || s[0] == 'f') && s[1] >= '1' && s[1] <= '9') { int n = atoi(s + 1); if (n >= 1 && n <= 12) return VK_F1 + n - 1; }
  if (!_stricmp(s, "Ctrl") || !_stricmp(s, "LeftCtrl")) return VK_LCONTROL;
  if (!_stricmp(s, "RightCtrl")) return VK_RCONTROL;
  if (!_stricmp(s, "Shift") || !_stricmp(s, "LeftShift")) return VK_LSHIFT;
  if (!_stricmp(s, "CapsLock")) return VK_CAPITAL;
  if (!_stricmp(s, "Space")) return VK_SPACE;
  if (!_stricmp(s, "Insert")) return VK_INSERT;
  if (!_stricmp(s, "Delete")) return VK_DELETE;
  if (!_stricmp(s, "Home")) return VK_HOME;
  if (!_stricmp(s, "End")) return VK_END;
  if (!_stricmp(s, "PageUp")) return VK_PRIOR;
  if (!_stricmp(s, "PageDown")) return VK_NEXT;
  if (!_stricmp(s, "Pause")) return VK_PAUSE;
  if (!_stricmp(s, "ScrollLock")) return VK_SCROLL;
  if (!_stricmp(s, "NumpadMultiply")) return VK_MULTIPLY;
  if (!_strnicmp(s, "0x", 2)) return (int)strtol(s, nullptr, 16);
  return 0;
}
static void LoadConfig(const char* folder) {
  char path[MAX_PATH]; snprintf(path, sizeof path, "%s\\config.ini", folder);
  FILE* f = fopen(path, "r");
  if (!f) {
    f = fopen(path, "w");
    if (f) {
      fputs("# BoatCrewMod settings\n"
            "# hotkey: key that toggles first person. Examples: Insert, F9, Home, Pause, 0x75 (a virtual-key code).\n"
            "# Pick a key the game does not use. The game's own debug console has a toggle key too (see mod.log).\n"
            "hotkey=Insert\n"
            "# sensitivity: degrees of turn per mouse count (raw input). Lower is slower.\n"
            "sensitivity=0.12\n"
            "# runkey: hold to run while walking with WASD. Shift is the game's own magnify, so it defaults to Ctrl.\n"
            "runkey=Ctrl\n"
            "# walkspeed / runspeed: metres per second while holding WASD (and the run key).\n"
            "walkspeed=1.5\nrunspeed=3.0\n"
            "# eyeforward: how far ahead of the head bone the camera sits, in metres, so the face is not in view.\n"
            "eyeforward=0.18\n"
            "# camera: head (the head bone, default) or bounds (the top of the visible body mesh).\n"
            "camera=head\n", f);
      fclose(f);
    }
    return;
  }
  char line[256];
  while (fgets(line, sizeof line, f)) {
    if (line[0] == '#') continue;
    char* eq = strchr(line, '='); if (!eq) continue;
    *eq = 0; char* v = eq + 1;
    for (char* e = v + strlen(v); e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' '); ) *--e = 0;
    if (!_stricmp(line, "sensitivity")) { float s = (float)atof(v); if (s > 0.001f && s < 5.0f) g_sens = s; else Log("config.ini: sensitivity '%s' is out of range, keeping %.3f", v, g_sens); }
    if (!_stricmp(line, "camera")) g_camFromBounds = _stricmp(v, "bounds") == 0;
    if (!_stricmp(line, "walkspeed")) { float s = (float)atof(v); if (s > 0.1f && s < 20.f) g_walkSpeed = s; }
    if (!_stricmp(line, "runspeed")) { float s = (float)atof(v); if (s > 0.1f && s < 20.f) g_runSpeed = s; }
    if (!_stricmp(line, "eyeforward")) { float s = (float)atof(v); if (s > -1.f && s < 1.f) g_eyeForward = s; }
    if (!_stricmp(line, "runkey")) { int k = KeyFromName(v); if (k) g_runKey = k; else Log("config.ini: unknown runkey '%s', keeping the default", v); }
    if (!_stricmp(line, "hotkey")) { int k = KeyFromName(v); if (k) g_hotkey = k; else Log("config.ini: unknown hotkey '%s', keeping the default", v); }
  }
  fclose(f);
}

static bool InvokeStruct(void* method, void* obj, void** args, void* out, size_t size) {
  void* exc = nullptr;
  void* r = il2cpp_runtime_invoke(method, obj, args, &exc);
  if (exc || !r) return false;
  if (out) memcpy(out, il2cpp_object_unbox(r), size);
  return true;
}
static void* Invoke(void* method, void* obj) {                  // reference-typed result, no arguments
  void* exc = nullptr;
  void* r = il2cpp_runtime_invoke(method, obj, nullptr, &exc);
  return exc ? nullptr : r;
}
static void* SelectedCrew() { void* c = nullptr; il2cpp_field_static_get_value(g_fieldSelected, &c); return c; }
static void* Field(void* obj, int off) { return *(void**)((char*)obj + off); }

static bool WeAreForeground(HWND* wnd) {
  HWND h = GetForegroundWindow(); DWORD pid = 0;
  if (!h) return false;
  GetWindowThreadProcessId(h, &pid);
  if (pid != GetCurrentProcessId()) return false;
  *wnd = h; return true;
}

static void Recenter(HWND wnd) {
  RECT rc; GetClientRect(wnd, &rc);
  POINT c = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
  ClientToScreen(wnd, &c); g_center = c; SetCursorPos(c.x, c.y);
}

// Hiding the cursor through Unity's Cursor.visible fought with the game, which turns it back on, and flickered.
// Instead the game window's WM_SETCURSOR is answered here while we are looking around, with no cursor image.
// Mouse movement comes from the game window's own WM_INPUT messages. Registering our own raw-input window instead
// replaced Unity's registration (Windows allows one per process) and broke the game's mouse in targeting mode.
static void AccumulateRaw(LPARAM l) {
  UINT size = 0;
  GetRawInputData((HRAWINPUT)l, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
  if (!size || size > 256) return;
  BYTE buf[256];
  if (GetRawInputData((HRAWINPUT)l, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != size) return;
  RAWINPUT* ri = (RAWINPUT*)buf;
  if (ri->header.dwType != RIM_TYPEMOUSE || (ri->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) return;
  InterlockedIncrement(&g_rawEvents);
  if (g_rawActive) {
    InterlockedExchangeAdd(&g_rawDx, ri->data.mouse.lLastX);
    InterlockedExchangeAdd(&g_rawDy, ri->data.mouse.lLastY);
  }
}
static WNDPROC g_origProc = nullptr;
static volatile bool g_hideCursor = false;
static LRESULT CALLBACK CursorProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
  if (msg == WM_INPUT) AccumulateRaw(l);                          // observe only; the game still gets the message
  if (msg == WM_SETCURSOR && g_hideCursor && LOWORD(l) == HTCLIENT) { SetCursor(nullptr); return TRUE; }
  return CallWindowProcW(g_origProc, h, msg, w, l);
}
static void HookWindowCursor(HWND wnd) {
  if (g_origProc || !wnd) return;                                // runs on the game's main thread, which owns the window
  g_origProc = (WNDPROC)SetWindowLongPtrW(wnd, GWLP_WNDPROC, (LONG_PTR)CursorProc);
  Log(g_origProc ? "cursor hook installed on the game window" : "could not hook the game window's cursor (error %lu)", g_origProc ? 0UL : GetLastError());
}

static bool CursorVisible() {
  if (!m_getVisible) return true;
  void* exc = nullptr; void* r = il2cpp_runtime_invoke(m_getVisible, nullptr, nullptr, &exc);
  return exc || !r ? true : *(unsigned char*)il2cpp_object_unbox(r) != 0;
}
static void SetCursorVisible(bool visible) {
  if (!m_setVisible) return;
  unsigned char v = visible ? 1 : 0; void* args[1] = { &v }; void* exc = nullptr;
  il2cpp_runtime_invoke(m_setVisible, nullptr, args, &exc);
}
static float TimeScale() {
  float ts = 1.0f;
  if (m_getTimeScale) InvokeStruct(m_getTimeScale, nullptr, nullptr, &ts, sizeof ts);
  return ts;
}
static void* ShipController() { void* s = nullptr; if (g_fieldShipInstance) il2cpp_field_static_get_value(g_fieldShipInstance, &s); return s; }
static void SetOneAction(void* actionRef, bool enable) {
  if (!actionRef) return;
  void* action = Invoke(m_getAction, actionRef);
  if (!action) return;
  void* exc = nullptr; void* r = il2cpp_runtime_invoke(m_actEnabled, action, nullptr, &exc);
  bool isOn = !exc && r && *(unsigned char*)il2cpp_object_unbox(r) != 0;
  if (enable && !isOn) il2cpp_runtime_invoke(m_actEnable, action, nullptr, &exc);
  if (!enable && isOn) il2cpp_runtime_invoke(m_actDisable, action, nullptr, &exc);
}
static void SetBoatInput(bool enable) {                          // false = boat ignores WASD
  if (!m_getAction || !m_actEnable || !m_actDisable || !m_actEnabled || g_offMove < 0) return;
  void* ship = ShipController();
  if (!ship) return;
  SetOneAction(Field(ship, g_offMove), enable);
  if (g_offMoveStick >= 0) SetOneAction(Field(ship, g_offMoveStick), enable);
  g_boatInputOff = !enable;
}
static void ReleaseCrew(void* crew);
static void PossessCrew(void* crew);
static void LeaveFirstPerson(const char* why) {
  if (!g_fp) return;
  if (g_cursorHidden) { while (ShowCursor(TRUE) < 0) {} g_cursorHidden = false; }
  if (g_looking && m_setLockState) { void* a1[1] = { &g_lockBefore }; void* exc = nullptr; il2cpp_runtime_invoke(m_setLockState, nullptr, a1, &exc); }
  ReleaseCrew(g_crew);
  g_fp = false; g_crew = nullptr; g_looking = false;
  g_hideCursor = false; g_menuLatch = false; g_rawActive = false;
  SetBoatInput(true);
  Log("first person OFF (%s)", why);
}

static bool StartFirstPerson(HWND wnd) {
  void* crew = SelectedCrew();
  if (!crew) { Log("hotkey: no crewmate is selected (click one first)"); return false; }
  void* dir = Field(crew, g_offDirection);
  Q4 q = { 0, 0, 0, 1 };
  if (dir) InvokeStruct(m_getRotation, dir, nullptr, &q, sizeof q);
  V3 f = QForward(q);
  g_yaw = atan2f(f.x, f.z) * kDeg; g_pitch = 0;
  g_crew = crew; g_fp = true;
  PossessCrew(crew);
  HookWindowCursor(wnd);
  SetBoatInput(false);
  Recenter(wnd);
  Log("first person ON: crewmate %p, yaw %.1f", crew, g_yaw);
  return true;
}

// The game's debug console toggles on a key stored in a saved setting. Report it so a hotkey clash can be seen.
static bool ConsoleVisible() {
  void* con = nullptr;
  if (g_fieldConsole) il2cpp_field_static_get_value(g_fieldConsole, &con);
  return con && g_offConsoleVisible >= 0 && *((unsigned char*)con + g_offConsoleVisible) != 0;
}
static void ProbeConsole() {
  void* con = nullptr;
  if (g_fieldConsole) il2cpp_field_static_get_value(g_fieldConsole, &con);
  if (!con) { Log("console probe: no ConsoleUtility.Console instance yet"); return; }
  void* input = g_offConsoleInput >= 0 ? Field(con, g_offConsoleInput) : nullptr;
  if (!input) { Log("console probe: instance %p has no input component", con); return; }
  void* k = il2cpp_object_get_class(input);
  void* fToggle = il2cpp_class_get_field_from_name(k, "ToggleKey");
  int toggle = fToggle ? *(int*)((char*)input + il2cpp_field_get_offset(fToggle)) : -1;
  // Input System Key values: F1 = 94 ... F12 = 105; Backslash = 10. KeyCode: F1 = 282; Backslash = 92.
  Log("console probe: input class %s, ToggleKey = %d (%s)", il2cpp_class_get_name(k), toggle,
      (toggle >= 94 && toggle <= 105) ? "an F key, as Input System Key" : (toggle >= 282 && toggle <= 293) ? "an F key, as KeyCode" : (toggle == 10 || toggle == 92) ? "Backslash" : "see key tables");
}

// ---- stat data for the log ---------------------------------------------------------------------------------
// The game's character constants (base values and per-level amounts) and, for a crewmate we take over, each
// attribute's raw, trait, effective and live value, so the effect of a stat can be put into numbers.
static bool g_constLogged = false;
static bool LogConstants() {
  void* k = FindClass("Core", "CharacterConstants");
  if (!k) { Log("CharacterConstants not found"); return false; }
  Log("---- CharacterConstants (static fields, read from the running game)");
  void* iter = nullptr; bool anyNonZero = false;
  while (void* f = il2cpp_class_get_fields(k, &iter)) {
    const char* name = il2cpp_field_get_name(f);
    const char* tn = il2cpp_type_get_name(il2cpp_field_get_type(f));
    union { int i; float fl; unsigned char raw[16]; } v; memset(&v, 0, sizeof v);
    il2cpp_field_static_get_value(f, &v);
    if (strstr(tn, "Int32")) { Log("  %-42s = %d", name, v.i); if (v.i) anyNonZero = true; }
    else if (strstr(tn, "Single")) { Log("  %-42s = %g", name, v.fl); if (v.fl != 0.f) anyNonZero = true; }
    else Log("  %-42s : %s", name, tn);
  }
  const char* props[] = { "get_healthPerLevel", "get_healthPerIncrement" };
  for (const char* pn : props) {
    void* m = il2cpp_class_get_method_from_name(k, pn, 0);
    if (!m) continue;
    void* exc = nullptr; void* r = il2cpp_runtime_invoke(m, nullptr, nullptr, &exc);
    if (!exc && r) Log("  %-42s = %d", pn + 4, *(int*)il2cpp_object_unbox(r));
  }
  return anyNonZero;                                              // false while the class has not initialised yet
}
static void LogCharacter(void* crew) {
  void* crewCls = il2cpp_object_get_class(crew);
  void* fChar = il2cpp_class_get_field_from_name(crewCls, "_character");
  void* ch = fChar ? Field(crew, (int)il2cpp_field_get_offset(fChar)) : nullptr;
  if (!ch) { Log("character: no CrewCharacter on crewmate %p", crew); return; }
  void* cls = il2cpp_object_get_class(ch);
  Log("---- crewmate %p: attribute raw / trait / effective / live", crew);
  const char* attrs[] = { "RESILIENCE", "AGILITY", "HANDEYECOORDINATION", "COOPERATIVENESS", "ADAPTABILITY", "GRIT" };
  for (const char* a : attrs) {
    char nm[96]; snprintf(nm, sizeof nm, "Attribute_Raw_%s", a);
    void* fr = il2cpp_class_get_field_from_name(cls, nm);
    int raw = fr ? *(int*)((char*)ch + il2cpp_field_get_offset(fr)) : -1;
    float val[3] = { -1, -1, -1 }; const char* kinds[3] = { "Trait", "Effective", "Live" };
    for (int i = 0; i < 3; i++) {
      snprintf(nm, sizeof nm, "get_Attribute_%s_%s", kinds[i], a);
      void* m = il2cpp_class_get_method_from_name(cls, nm, 0);
      if (m) InvokeStruct(m, ch, nullptr, &val[i], sizeof(float));
    }
    Log("  %-20s raw %3d   trait %7.3f   effective %7.3f   live %7.3f", a, raw, val[0], val[1], val[2]);
  }
}

// ---- where the visible body is ---------------------------------------------------------------------------------
static V3 QRotate(Q4 q, V3 v) {
  V3 u = { q.x, q.y, q.z };
  V3 c1 = { u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x };
  V3 t = { c1.x + q.w * v.x, c1.y + q.w * v.y, c1.z + q.w * v.z };
  V3 c2 = { u.y * t.z - u.z * t.y, u.z * t.x - u.x * t.z, u.x * t.y - u.y * t.x };
  return { v.x + 2 * c2.x, v.y + 2 * c2.y, v.z + 2 * c2.z };
}
static bool WorldPos(void* transform, V3* out) { return transform && InvokeStruct(m_getPosition, transform, nullptr, out, sizeof(V3)); }

// World bounds of the crewmate's first body mesh (the model you can see), as center and half-size.
static bool BodyBounds(void* crew, V3* center, V3* ext) {
  if (!m_getBounds || g_offCrewVisual < 0 || g_offBodyArr < 0) return false;
  void* vis = Field(crew, g_offCrewVisual);
  void* arr = vis ? Field(vis, g_offBodyArr) : nullptr;
  if (!arr || *(uintptr_t*)((char*)arr + 0x18) == 0) return false;                  // Il2CppArray: length at 0x18, items from 0x20
  void* mesh = *(void**)((char*)arr + 0x20);
  if (!mesh) return false;
  float b[6];
  if (!InvokeStruct(m_getBounds, mesh, nullptr, b, sizeof b)) return false;
  *center = { b[0], b[1], b[2] }; *ext = { b[3], b[4], b[5] };
  return true;
}

// Each interesting transform, as an offset from the crewmate's root in the body's own frame (right, up, forward), so
// the camera anchor can be chosen from facts. Logged a few times after taking control.
static void LogGeometry(void* crew) {
  void* rootT = Invoke(m_getTransform, crew);
  V3 root;
  if (!WorldPos(rootT, &root)) return;
  void* visT = g_offVisual >= 0 ? Field(crew, g_offVisual) : nullptr;
  Q4 q = { 0, 0, 0, 1 };
  if (visT) InvokeStruct(m_getRotation, visT, nullptr, &q, sizeof q);
  Q4 inv = { -q.x, -q.y, -q.z, q.w };
  struct { const char* name; void* t; } list[] = {
    { "visual", visT }, { "headEnd", g_offHeadEnd >= 0 ? Field(crew, g_offHeadEnd) : nullptr }, { "chest", g_offChest >= 0 ? Field(crew, g_offChest) : nullptr },
    { "cameraTarget", g_offCamTarget >= 0 ? Field(crew, g_offCamTarget) : nullptr }, { "cameraFocus", g_offCamFocus >= 0 ? Field(crew, g_offCamFocus) : nullptr } };
  Log("geometry (right, up, forward in metres from the crewmate root; body yaw %.0f):", atan2f(QForward(q).x, QForward(q).z) * kDeg);
  for (auto& e : list) {
    V3 p; if (!WorldPos(e.t, &p)) { Log("   %-13s unavailable", e.name); continue; }
    V3 d = QRotate(inv, { p.x - root.x, p.y - root.y, p.z - root.z });
    Log("   %-13s %6.2f %6.2f %6.2f", e.name, d.x, d.y, d.z);
  }
  V3 c, e2;
  if (BodyBounds(crew, &c, &e2)) {
    V3 d = QRotate(inv, { c.x - root.x, c.y - root.y, c.z - root.z });
    Log("   body mesh     centre %6.2f %6.2f %6.2f, half-size %5.2f %5.2f %5.2f", d.x, d.y, d.z, e2.x, e2.y, e2.z);
  } else Log("   body mesh     bounds unavailable");
}

// ---- taking control and walking -------------------------------------------------------------------------------
static bool KeyDown(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

static void* FindMethodByParam(void* klass, const char* name, int nparams, int paramIndex, const char* typeFragment) {
  void* iter = nullptr; void* found = nullptr;
  while (void* m = il2cpp_class_get_methods(klass, &iter)) {
    if (strcmp(il2cpp_method_get_name(m), name) != 0 || (int)il2cpp_method_get_param_count(m) != nparams) continue;
    if (strstr(il2cpp_type_get_name(il2cpp_method_get_param(m, paramIndex)), typeFragment)) found = m;
  }
  return found;
}

static void SetReasonIn(void* set, bool add);
static void SetBlockReason(void* crew, bool add) {
  if (g_offBlockReasons >= 0) SetReasonIn(Field(crew, g_offBlockReasons), add);
  void* pawn = g_offPawn >= 0 ? Field(crew, g_offPawn) : nullptr;             // the AI pawn has its own block list
  if (pawn && g_offPawnBlock >= 0) SetReasonIn(Field(pawn, g_offPawnBlock), add);
}
static void SetReasonIn(void* set, bool add) {
  if (!set || !g_reasonStr) return;
  if (!m_hsAdd) {
    void* k = il2cpp_object_get_class(set);
    m_hsAdd = il2cpp_class_get_method_from_name(k, "Add", 1);
    m_hsRemove = il2cpp_class_get_method_from_name(k, "Remove", 1);
  }
  void* m = add ? m_hsAdd : m_hsRemove;
  if (!m) return;
  void* args[1] = { g_reasonStr }; void* exc = nullptr;
  il2cpp_runtime_invoke(m, set, args, &exc);
}

static void PossessCrew(void* crew) {
  void* pather = nullptr;
  if (g_fieldPatherInst) il2cpp_field_static_get_value(g_fieldPatherInst, &pather);
  if (pather && g_offDeck >= 0) g_deckMask = *(int*)((char*)pather + g_offDeck);
  Log("deck layer mask 0x%X (PlayerCrewPather %p)", g_deckMask, pather);
  SetBlockReason(crew, true);
  if (m_pathCancel) { void* exc = nullptr; il2cpp_runtime_invoke(m_pathCancel, crew, nullptr, &exc); }
  QueryPerformanceCounter(&g_qpcLast);
  Log("took control of crewmate %p (AI movement blocked)", crew);
  g_geomFrames = 0; g_rootHave = false; g_rootDrift = 0; g_rootChecks = 0;
  {                                                               // which transform carries the facing? Yaw of each, before we write any
    void* rootT = Invoke(m_getTransform, crew);
    void* visT = g_offVisual >= 0 ? Field(crew, g_offVisual) : nullptr;
    void* dirT = g_offDirection >= 0 ? Field(crew, g_offDirection) : nullptr;
    auto yawOf = [](void* tr) { Q4 q = { 0, 0, 0, 1 }; if (tr) InvokeStruct(m_getRotation, tr, nullptr, &q, sizeof q); V3 f = QForward(q); return atan2f(f.x, f.z) * kDeg; };
    float bm = g_offBlockMove >= 0 ? *(float*)((char*)crew + g_offBlockMove) : -1.f;
    float sv = -1.f; if (m_getSpeedValue) InvokeStruct(m_getSpeedValue, crew, nullptr, &sv, sizeof sv);
    Log("facing before our writes: root yaw %.0f, visual yaw %.0f, direction yaw %.0f; blockMovement was %.2f; game speedValue %.3f", yawOf(rootT), yawOf(visT), yawOf(dirT), bm, sv);
  }
  if (g_fieldPatherBlock) { il2cpp_field_static_get_value(g_fieldPatherBlock, &g_savedBlock); Log("PlayerCrewPather.block was %d; holding it at 1 while in first person", g_savedBlock); }
  __try { LogCharacter(crew); if (!g_constLogged) g_constLogged = LogConstants(); }
  __except (EXCEPTION_EXECUTE_HANDLER) { Log("stat logging faulted"); }
}
static void ReleaseCrew(void* crew) {
  if (!crew) return;
  if (g_fieldPatherBlock) il2cpp_field_static_set_value(g_fieldPatherBlock, &g_savedBlock);
  g_geomFrames = -1;
  if (g_offBlockMove >= 0) *(float*)((char*)crew + g_offBlockMove) = 0.0f;
  SetBlockReason(crew, false);
  if (m_setSpeed) { float zero = 0.f; unsigned char force = 1; void* args[2] = { &zero, &force }; void* exc = nullptr; il2cpp_runtime_invoke(m_setSpeed, crew, args, &exc); }
  if (m_pathCancel) { void* exc = nullptr; il2cpp_runtime_invoke(m_pathCancel, crew, nullptr, &exc); }
  Log("released crewmate %p (AI may move it again)", crew);
}

// The deck surface straight below a point, from the game's own deck layer.
static bool GroundBelow(V3 origin, V3* hit) {
  if (!m_raycast) return false;
  V3 dir = { 0, -1, 0 };
  float maxDist = 3.0f; int mask = g_deckMask ? g_deckMask : -1;                    // no deck layer known: any collider
  unsigned char buf[96] = {};                                      // RaycastHit (point, normal, ...); the point comes first
  void* args[5] = { &origin, &dir, buf, &maxDist, &mask };
  void* exc = nullptr;
  void* r = il2cpp_runtime_invoke(m_raycast, nullptr, args, &exc);
  if (exc || !r || !*(unsigned char*)il2cpp_object_unbox(r)) return false;
  *hit = *(V3*)buf; return true;
}

// Runs every frame while we control a crewmate. WASD moves them relative to where you look; no nav mesh involved.
static void UpdateWalk(bool active) {
  if (!g_crew) return;
  if (g_offBlockMove >= 0) *(float*)((char*)g_crew + g_offBlockMove) = 0.0f;   // blockReasons keeps the AI off; a high blockMovement looked like "stuck" and may animate walking in place
  LARGE_INTEGER now; QueryPerformanceCounter(&now);
  float dt = g_qpcFreq.QuadPart ? (float)((double)(now.QuadPart - g_qpcLast.QuadPart) / (double)g_qpcFreq.QuadPart) : 0.016f;
  g_qpcLast = now;
  if (dt > 0.05f) dt = 0.05f; if (dt < 0.f) dt = 0.f;

  float fwd = (active && KeyDown('W') ? 1.f : 0.f) - (active && KeyDown('S') ? 1.f : 0.f);
  float side = (active && KeyDown('D') ? 1.f : 0.f) - (active && KeyDown('A') ? 1.f : 0.f);
  bool moving = fwd != 0.f || side != 0.f;
  float speed = 0.f;

  void* crewT = Invoke(m_getTransform, g_crew);
  V3 pos;
  if (crewT && InvokeStruct(m_getPosition, crewT, nullptr, &pos, sizeof pos) && moving && m_setPosSelfPawn) {
    float yr = g_yaw * kRad, fx = sinf(yr), fz = cosf(yr), rx = cosf(yr), rz = -sinf(yr);   // forward and right on the ground plane
    float dx = fx * fwd + rx * side, dz = fz * fwd + rz * side, len = sqrtf(dx * dx + dz * dz);
    speed = KeyDown(g_runKey) ? g_runSpeed : g_walkSpeed;
    V3 target = { pos.x + dx / len * speed * dt, pos.y, pos.z + dz / len * speed * dt };
    V3 probe = { target.x, pos.y + 1.0f, target.z }, ground;
    bool onDeck = GroundBelow(probe, &ground);
    bool ok = onDeck && fabsf(ground.y - pos.y) <= kStepLimit;
    if (ok) { target.y = ground.y; void* args[1] = { &target }; void* exc = nullptr; il2cpp_runtime_invoke(m_setPosSelfPawn, g_crew, args, &exc); if (exc) ok = false; }
    if (g_walkLogs < 8) { g_walkLogs++; Log("walk: from (%.2f %.2f %.2f) step %.3f m, ground %s%.2f, %s", pos.x, pos.y, pos.z, speed * dt, onDeck ? "y=" : "none ", onDeck ? ground.y : 0.f, ok ? "moved" : "refused"); }
    if (!ok) speed = 0.f;
  }
  // Face where you look by turning the crewmate's root. The visual child keeps its own local rotation (overwriting it
  // turned the model 90 degrees away from the camera). If the game rewrites the root between frames, the log says so.
  if (crewT && m_setRotation && m_getRotation) {
    if (g_rootHave) {
      Q4 now;
      if (InvokeStruct(m_getRotation, crewT, nullptr, &now, sizeof now)) {
        float d = fabsf(now.x * g_rootWritten.x + now.y * g_rootWritten.y + now.z * g_rootWritten.z + now.w * g_rootWritten.w);
        if (d > 1.f) d = 1.f;
        g_rootChecks++;
        if (2.f * acosf(d) * kDeg > 1.5f) { g_rootDrift++; if (g_rootDrift <= 4) Log("the game turned the crewmate's root between frames (%.1f degrees)", 2.f * acosf(d) * kDeg); }
        if (g_rootChecks == 600) Log("root rotation check after 600 frames: the game moved it on %d frames", g_rootDrift);
      }
    }
    if (active) { Q4 q = QEuler(0.f, g_yaw); void* args[1] = { &q }; void* exc = nullptr; il2cpp_runtime_invoke(m_setRotation, crewT, args, &exc); g_rootWritten = q; g_rootHave = true; }
  }
  // The crewmate's own speed field is given its normal game value while we move (so the animation runs at its usual
  // pace), and zero when we stand still.
  float gameSpeed = 0.f;
  if (speed > 0.f) { gameSpeed = 1.f; if (m_getSpeedValue) InvokeStruct(m_getSpeedValue, g_crew, nullptr, &gameSpeed, sizeof gameSpeed); if (KeyDown(g_runKey)) gameSpeed *= 1.6f; }
  if (m_setSpeed) { unsigned char force = 1; void* args[2] = { &gameSpeed, &force }; void* exc = nullptr; il2cpp_runtime_invoke(m_setSpeed, g_crew, args, &exc); }
  DWORD nowMs = GetTickCount();
  if (nowMs - g_lastProbe > 2000) {                                // what the animation sees, every 2 s
    g_lastProbe = nowMs;
    void* anim = g_offAnim >= 0 ? Field(g_crew, g_offAnim) : nullptr;
    float ws = -1.f, st = -1.f, bm = g_offBlockMove >= 0 ? *(float*)((char*)g_crew + g_offBlockMove) : -1.f;
    if (anim && m_getWalkingSpeed) InvokeStruct(m_getWalkingSpeed, anim, nullptr, &ws, sizeof ws);
    if (anim && m_getStance) InvokeStruct(m_getStance, anim, nullptr, &st, sizeof st);
    float spd = -1.f; { void* k = il2cpp_object_get_class(g_crew); void* f = il2cpp_class_get_field_from_name(k, "speed"); if (f) spd = *(float*)((char*)g_crew + il2cpp_field_get_offset(f)); }
    char taskName[64] = "none";
    void* task = g_offTask >= 0 ? Field(g_crew, g_offTask) : nullptr;
    if (task) {                                                   // AvailableTask.name is a managed string: length at 0x10, UTF-16 chars at 0x14
      void* s = *(void**)((char*)task + 0x10);
      if (s) { int n = *(int*)((char*)s + 0x10); if (n > 63) n = 63; const wchar_t* w = (const wchar_t*)((char*)s + 0x14); for (int i = 0; i < n; i++) taskName[i] = (char)w[i]; taskName[n] = 0; }
      else strcpy(taskName, "(unnamed)");
    }
    int workingOn = g_offWorkingOn >= 0 ? *(int*)((char*)g_crew + g_offWorkingOn) : -1;
    auto boolOf = [](void* m, void* obj) { if (!m) return -1; void* exc = nullptr; void* r = il2cpp_runtime_invoke(m, obj, nullptr, &exc); return (exc || !r) ? -1 : (int)*(unsigned char*)il2cpp_object_unbox(r); };
    void* pawn = g_offPawn >= 0 ? Field(g_crew, g_offPawn) : nullptr;
    int pawnStalled = pawn ? boolOf(m_pawnStalled, pawn) : -1, pawnWorking = (pawn && g_offPawnWorking >= 0) ? *((unsigned char*)pawn + g_offPawnWorking) : -1;
    float pawnStall = (pawn && g_offPawnStall >= 0) ? *(float*)((char*)pawn + g_offPawnStall) : -1.f;
    int pawnHasTask = pawn && m_pawnTask ? (Invoke(m_pawnTask, pawn) != nullptr) : -1;
    Log("probe: keys %s, speed %.3f, WalkingSpeed %.3f, Stance %.2f | task '%s', workingOn %d, running %d, working %d, proxyStalled %d | pawn: task %d, stalled %d, working %d, stallTime %.1f",
        moving ? "held" : "none", spd, ws, st, taskName, workingOn, boolOf(m_isRunning, g_crew), boolOf(m_isWorking, g_crew), boolOf(m_isProxyStalled, g_crew), pawnHasTask, pawnStalled, pawnWorking, pawnStall);
  }
}

// Runs once per frame, after the game's own camera update, on the game's main thread.
static void OnFrame() {
  g_frame++;
  static bool prev = false;
  HWND wnd = nullptr;
  bool fg = WeAreForeground(&wnd);
  bool down = fg && (GetAsyncKeyState(g_hotkey) & 0x8000) != 0;
  static bool prevEsc = false;
  bool esc = fg && (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
  if (down && !prev) {
    Log("hotkey pressed (virtual key 0x%X); console visible before our handling: %d", g_hotkey, ConsoleVisible());
    if (g_fp) LeaveFirstPerson("hotkey");
    else StartFirstPerson(wnd);
  }
  if (esc && !prevEsc && g_fp) { g_menuLatch = !g_menuLatch; g_sawPaused = false; Log("Esc: mouse %s", g_menuLatch ? "freed for the menu" : "captured again"); }
  prev = down; prevEsc = esc;
  if (g_frame == 1 || g_frame == 600) Log("frame hook alive: %u frames so far", g_frame);
  if (g_frame == 900) ProbeConsole();
  if (!g_constLogged && g_frame >= 900 && (g_frame % 600) == 0) g_constLogged = LogConstants();
  if (!g_fp) return;

  SetBoatInput(false);                                            // keep it off: the game may re-enable actions on its own
  if (g_fieldPatherBlock) { int one = 1; il2cpp_field_static_set_value(g_fieldPatherBlock, &one); }   // no click-to-move preview or orders
  if (g_geomFrames >= 0 && (g_geomFrames == 5 || g_geomFrames == 240 || g_geomFrames == 900)) LogGeometry(g_crew);
  if (g_geomFrames >= 0) g_geomFrames++;
  if (SelectedCrew() != g_crew && SelectedCrew()) { ReleaseCrew(g_crew); g_crew = SelectedCrew(); PossessCrew(g_crew); Log("switched to newly selected crewmate %p", g_crew); }
  void* head = Field(g_crew, g_offHeadEnd);
  void* cam = Invoke(m_getMain, nullptr);
  if (!head || !cam) { if ((g_frame % 300) == 0) Log("no head (%p) or camera (%p)", head, cam); return; }
  void* camT = Invoke(m_getTransform, cam);
  if (!camT) return;

  // Mouse look only while the game is focused, running and no console is open. In a pause menu or the console the
  // cursor is released and shown; the camera simply stays on the head.
  bool paused = TimeScale() <= 0.01f;
  if (g_menuLatch) {                                              // Esc opened the menu; it closes by Esc again, or when the game un-pauses
    if (paused) g_sawPaused = true;
    else if (g_sawPaused) { g_menuLatch = false; Log("menu closed: mouse captured again"); }
  }
  bool look = fg && !paused && !g_menuLatch && !ConsoleVisible();
  int lockState = 0;
  if (m_getLockState) InvokeStruct(m_getLockState, nullptr, nullptr, &lockState, sizeof lockState);
  bool locked = lockState != 0;                                    // the game locks the cursor itself in targeting mode
  if (look) {
    if (!g_looking) { g_rawActive = true; InterlockedExchange(&g_rawDx, 0); InterlockedExchange(&g_rawDy, 0); }   // first frame: no stored movement
    LONG dx = InterlockedExchange(&g_rawDx, 0), dy = InterlockedExchange(&g_rawDy, 0);
    static bool loggedSource = false;
    if (!loggedSource && g_looking && (dx || dy)) { loggedSource = true; Log("mouse look is reading raw input from the game window (%ld events so far)", (long)g_rawEvents); }
    if (g_rawEvents == 0 && g_looking && !locked) {                // fallback: no raw messages reach the window
      POINT p; GetCursorPos(&p);
      dx = p.x - g_center.x; dy = p.y - g_center.y;
      static bool loggedFallback = false;
      if (!loggedFallback && (dx || dy)) { loggedFallback = true; Log("no raw input seen; using the cursor's distance from the centre"); }
    }
    g_yaw += dx * g_sens;
    g_pitch += dy * g_sens;
    if (g_pitch > 85) g_pitch = 85; if (g_pitch < -85) g_pitch = -85;
    if (!locked) Recenter(wnd);                                    // keeps the cursor (and the game's pointer ray) at the screen centre
    if (!g_looking) {                                              // entering look: remember the game's lock state, hide the cursor
      g_lockBefore = lockState;
      int c = 0; while ((c = ShowCursor(FALSE)) >= 0) {} g_cursorHidden = true;
      Log("cursor hidden (ShowCursor count %d, game lockState was %d)", c, g_lockBefore);
    }
    if (m_setLockState) { int one = 1; void* a1[1] = { &one }; void* exc = nullptr; il2cpp_runtime_invoke(m_setLockState, nullptr, a1, &exc); }   // Unity's own hide-and-lock, as in targeting mode
    g_looking = true;
    g_hideCursor = true; SetCursor(nullptr);
  } else {
    g_hideCursor = false; g_rawActive = false;
    if (g_looking) {
      g_looking = false;
      if (g_cursorHidden) { int c = 0; while ((c = ShowCursor(TRUE)) < 0) {} g_cursorHidden = false; }
      if (m_setLockState) { void* a1[1] = { &g_lockBefore }; void* exc = nullptr; il2cpp_runtime_invoke(m_setLockState, nullptr, a1, &exc); }
      Log("mouse released (menu, pause, console open or window unfocused)");
    }
  }
  UpdateWalk(look);
  if (g_fieldCamMode) {                                           // log when the game changes camera mode (targeting etc.)
    static int lastMode = -1; int mode = -1;
    il2cpp_field_static_get_value(g_fieldCamMode, &mode);
    if (mode != lastMode) {
      int lock = -1;
      if (m_getLockState) InvokeStruct(m_getLockState, nullptr, nullptr, &lock, sizeof lock);
      Log("camera mode %d -> %d (0 free, 1 targeting, 2 rocket, 3 torpedo, 4 special, 5 custom, 6 magnified, 7 squadron); cursor lockState %d", lastMode, mode, lock);
      lastMode = mode;
    }
  }
  // The head position is read last, after the walk update has moved and turned the crewmate this frame. Reading it
  // first made the camera lag the body by a frame.
  V3 pos;
  if (!InvokeStruct(m_getPosition, head, nullptr, &pos, sizeof pos)) return;
  Q4 rot = QEuler(g_pitch, g_yaw);
  {
    V3 f = QForward(QEuler(0.f, g_yaw));                           // horizontal look direction
    if (g_camFromBounds) {                                        // the top of the visible model, instead of the head bone
      V3 c, e;
      if (BodyBounds(g_crew, &c, &e)) pos = { c.x, c.y + e.y - 0.14f, c.z };
    }
    pos.x += f.x * g_eyeForward; pos.z += f.z * g_eyeForward;      // ahead of the face, so the head mesh is behind the camera
  }
  void* args[2] = { &pos, &rot };
  InvokeStruct(m_setPosRot, camT, args, nullptr, 0);
  g_lastWritten = rot; g_haveWritten = true;
  static bool first = true;
  if (first) { first = false; Log("camera placed at head: %.2f %.2f %.2f", pos.x, pos.y, pos.z); }
}

// ---- the hook: patch MethodInfo.methodPointer (its first field) of a Unity message ----------------------
typedef void (*fn_message)(void* self, void* method);
static fn_message g_original = nullptr;

// Before the game's own camera update runs, check whether the camera still has the rotation we wrote last frame. If
// something else turned it in between, the view would stick or lag, and the log says by how much.
static void CheckCameraOverwritten() {
  if (!g_fp || !g_haveWritten || !m_getRotation) return;
  void* cam = Invoke(m_getMain, nullptr);
  void* camT = cam ? Invoke(m_getTransform, cam) : nullptr;
  Q4 q;
  if (!camT || !InvokeStruct(m_getRotation, camT, nullptr, &q, sizeof q)) return;
  float dot = fabsf(q.x * g_lastWritten.x + q.y * g_lastWritten.y + q.z * g_lastWritten.z + q.w * g_lastWritten.w);
  if (dot > 1.f) dot = 1.f;
  float angle = 2.f * acosf(dot) * kDeg;
  static int n = 0, total = 0;
  total++;
  if (angle > 1.5f) {
    n++;
    if (n <= 6) Log("camera was changed by something else since our last write: %.1f degrees off (%d of %d frames so far)", angle, n, total);
  }
  if (total == 600) Log("camera overwrite check after 600 frames: %d frames had the camera moved by the game", n);
}

static void Hooked_LateUpdate(void* self, void* method) {
  if (g_ready && g_fp) { __try { CheckCameraOverwritten(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
  g_original(self, method);
  if (!g_ready) return;
  __try { OnFrame(); }
  __except (EXCEPTION_EXECUTE_HANDLER) {
    g_fp = false; g_crew = nullptr;
    if (++g_faults <= 3) Log("fault inside the frame code; first person turned off (%d)", g_faults);
    if (g_faults >= 20) g_ready = false;
  }
}

static bool InstallHook() {
  void* klass = FindClass("Core.Player", "PlayerCameraControlManager");
  if (!klass) { Log("PlayerCameraControlManager not found"); return false; }
  void* mi = il2cpp_class_get_method_from_name(klass, "LateUpdate", 0);
  if (!mi) { Log("LateUpdate not found"); return false; }
  void** slot = (void**)mi;
  g_original = (fn_message)*slot;
  DWORD old = 0;
  if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { Log("VirtualProtect failed"); return false; }
  *slot = (void*)&Hooked_LateUpdate;
  VirtualProtect(slot, sizeof(void*), old, &old);
  Log("hooked PlayerCameraControlManager.LateUpdate: MethodInfo %p, original %p (rva 0x%llX)", mi, g_original, (unsigned long long)((uintptr_t)g_original - (uintptr_t)g_ga));
  return true;
}

static bool Setup() {
  void* cam = FindClass("UnityEngine", "Camera");
  void* comp = FindClass("UnityEngine", "Component");
  void* tr = FindClass("UnityEngine", "Transform");
  void* sel = FindClass("Core.Player", "PlayerCrewSelector");
  void* crew = FindClass("Core", "CrewMember");
  if (!cam || !comp || !tr || !sel || !crew) { Log("class lookup failed: cam=%p comp=%p tr=%p sel=%p crew=%p", cam, comp, tr, sel, crew); return false; }
  m_getMain = il2cpp_class_get_method_from_name(cam, "get_main", 0);
  m_getTransform = il2cpp_class_get_method_from_name(comp, "get_transform", 0);
  m_getPosition = il2cpp_class_get_method_from_name(tr, "get_position", 0);
  m_getRotation = il2cpp_class_get_method_from_name(tr, "get_rotation", 0);
  m_setPosRot = il2cpp_class_get_method_from_name(tr, "SetPositionAndRotation", 2);
  void* timeCls = FindClass("UnityEngine", "Time");
  void* curCls = FindClass("UnityEngine", "Cursor");
  if (timeCls) m_getTimeScale = il2cpp_class_get_method_from_name(timeCls, "get_timeScale", 0);
  if (curCls) {
    m_getVisible = il2cpp_class_get_method_from_name(curCls, "get_visible", 0);
    m_setVisible = il2cpp_class_get_method_from_name(curCls, "set_visible", 1);
    m_getLockState = il2cpp_class_get_method_from_name(curCls, "get_lockState", 0);
    m_setLockState = il2cpp_class_get_method_from_name(curCls, "set_lockState", 1);
  }
  void* patherCls = FindClass("Core.Player", "PlayerCrewPather");
  if (patherCls) g_fieldPatherBlock = il2cpp_class_get_field_from_name(patherCls, "block");
  void* rendCls = FindClass("UnityEngine", "Renderer");
  if (rendCls) m_getBounds = il2cpp_class_get_method_from_name(rendCls, "get_bounds", 0);
  void* visCls = FindClass("Core", "CrewMemberVisual");
  if (visCls) { void* fb = il2cpp_class_get_field_from_name(visCls, "body"); if (fb) g_offBodyArr = (int)il2cpp_field_get_offset(fb); }
  if (patherCls) {
    void* fPather = il2cpp_class_get_field_from_name(patherCls, "instance");
    void* fDeck = il2cpp_class_get_field_from_name(patherCls, "deck");              // LayerMask the crew walk on
    g_fieldPatherInst = fPather;                                                    // the instance does not exist yet on the menu:
    g_offDeck = fDeck ? (int)il2cpp_field_get_offset(fDeck) : -1;                   // read the mask when control starts
  }
  void* phys = FindClass("UnityEngine", "Physics");
  if (phys) m_raycast = FindMethodByParam(phys, "Raycast", 5, 2, "RaycastHit");
  void* crewCls2 = FindClass("Core", "CrewMember");
  if (crewCls2) {
    m_setPosSelfPawn = il2cpp_class_get_method_from_name(crewCls2, "setPositionSelfAndPawn", 1);
    m_pathCancel = il2cpp_class_get_method_from_name(crewCls2, "pathCancel", 0);
    m_setSpeed = il2cpp_class_get_method_from_name(crewCls2, "setSpeed", 2);
    m_getSpeedValue = il2cpp_class_get_method_from_name(crewCls2, "get_speedValue", 0);
    m_isRunning = il2cpp_class_get_method_from_name(crewCls2, "get_isRunning", 0);
    m_isWorking = il2cpp_class_get_method_from_name(crewCls2, "isWorking", 0);
    m_isProxyStalled = il2cpp_class_get_method_from_name(crewCls2, "isProxyStalled", 0);
    { void* f; if ((f = il2cpp_class_get_field_from_name(crewCls2, "proxyPawn"))) g_offPawn = (int)il2cpp_field_get_offset(f);
      if ((f = il2cpp_class_get_field_from_name(crewCls2, "task"))) g_offTask = (int)il2cpp_field_get_offset(f);
      if ((f = il2cpp_class_get_field_from_name(crewCls2, "workingOn"))) g_offWorkingOn = (int)il2cpp_field_get_offset(f); }
    void* pawnCls = FindClass("Cotanius.Modules.ProxyPawn", "Pawn");
    if (pawnCls) {
      void* f;
      if ((f = il2cpp_class_get_field_from_name(pawnCls, "moveBlockReasons"))) g_offPawnBlock = (int)il2cpp_field_get_offset(f);
      if ((f = il2cpp_class_get_field_from_name(pawnCls, "working"))) g_offPawnWorking = (int)il2cpp_field_get_offset(f);
      if ((f = il2cpp_class_get_field_from_name(pawnCls, "stallTime"))) g_offPawnStall = (int)il2cpp_field_get_offset(f);
      m_pawnStalled = il2cpp_class_get_method_from_name(pawnCls, "get_isStalled", 0);
      m_pawnTask = il2cpp_class_get_method_from_name(pawnCls, "get_currentTask", 0);
    }
    Log("pawn lookups: Pawn=%p proxyPawn@0x%X moveBlockReasons@0x%X working@0x%X stallTime@0x%X isStalled=%p currentTask=%p | task@0x%X workingOn@0x%X", pawnCls, g_offPawn, g_offPawnBlock, g_offPawnWorking, g_offPawnStall, m_pawnStalled, m_pawnTask, g_offTask, g_offWorkingOn);
    void* fan = il2cpp_class_get_field_from_name(crewCls2, "crewMemberAnimation");
    if (fan) g_offAnim = (int)il2cpp_field_get_offset(fan);
    void* animCls = FindClass("Core", "CrewMemberAnimation");
    if (animCls) { m_getWalkingSpeed = il2cpp_class_get_method_from_name(animCls, "get_WalkingSpeed", 0); m_getStance = il2cpp_class_get_method_from_name(animCls, "get_Stance", 0); }
    void* fbm = il2cpp_class_get_field_from_name(crewCls2, "blockMovement");
    void* fbr = il2cpp_class_get_field_from_name(crewCls2, "blockReasons");
    void* fvis = il2cpp_class_get_field_from_name(crewCls2, "visual");
    void* fcv = il2cpp_class_get_field_from_name(crewCls2, "crewMemberVisual");
    void* fch = il2cpp_class_get_field_from_name(crewCls2, "chest");
    void* fct = il2cpp_class_get_field_from_name(crewCls2, "cameraTarget");
    void* fcf = il2cpp_class_get_field_from_name(crewCls2, "cameraFocus");
    if (fcv) g_offCrewVisual = (int)il2cpp_field_get_offset(fcv);
    if (fch) g_offChest = (int)il2cpp_field_get_offset(fch);
    if (fct) g_offCamTarget = (int)il2cpp_field_get_offset(fct);
    if (fcf) g_offCamFocus = (int)il2cpp_field_get_offset(fcf);
    if (fbm) g_offBlockMove = (int)il2cpp_field_get_offset(fbm);
    if (fbr) g_offBlockReasons = (int)il2cpp_field_get_offset(fbr);
    if (fvis) g_offVisual = (int)il2cpp_field_get_offset(fvis);
  }
  m_setRotation = il2cpp_class_get_method_from_name(tr, "set_rotation", 1);
  g_reasonStr = il2cpp_string_new("BoatCrewMod");
  if (g_reasonStr) il2cpp_gchandle_new(g_reasonStr, 0);                              // a strong handle so the string is never collected
  QueryPerformanceFrequency(&g_qpcFreq);
  Log("animation lookups: get_speedValue=%p crewMemberAnimation@0x%X get_WalkingSpeed=%p get_Stance=%p; walk %.2f run %.2f m/s, eye forward %.2f m", m_getSpeedValue, g_offAnim, m_getWalkingSpeed, m_getStance, g_walkSpeed, g_runSpeed, g_eyeForward);
  Log("control lookups: setPositionSelfAndPawn=%p pathCancel=%p setSpeed=%p Physics.Raycast=%p set_rotation=%p blockMovement@0x%X blockReasons@0x%X visual@0x%X deck layer mask 0x%X",
      m_setPosSelfPawn, m_pathCancel, m_setSpeed, m_raycast, m_setRotation, g_offBlockMove, g_offBlockReasons, g_offVisual, g_deckMask);
  void* camCls = FindClass("Core.Player", "PlayerCameraControlManager");
  if (camCls) g_fieldCamMode = il2cpp_class_get_field_from_name(camCls, "controlMode");
  Log("optional lookups: Time.get_timeScale=%p Cursor.get_visible=%p Cursor.set_visible=%p", m_getTimeScale, m_getVisible, m_setVisible);
  void* shipCls = FindClass("Core.Player", "PlayerShipController");
  void* refCls = FindClass("UnityEngine.InputSystem", "InputActionReference");
  void* actCls = FindClass("UnityEngine.InputSystem", "InputAction");
  if (shipCls && refCls && actCls) {
    g_fieldShipInstance = il2cpp_class_get_field_from_name(shipCls, "instance");
    void* fm = il2cpp_class_get_field_from_name(shipCls, "movement");
    void* fs = il2cpp_class_get_field_from_name(shipCls, "movementStick");
    if (fm) g_offMove = (int)il2cpp_field_get_offset(fm);
    if (fs) g_offMoveStick = (int)il2cpp_field_get_offset(fs);
    m_getAction = il2cpp_class_get_method_from_name(refCls, "get_action", 0);
    m_actEnable = il2cpp_class_get_method_from_name(actCls, "Enable", 0);
    m_actDisable = il2cpp_class_get_method_from_name(actCls, "Disable", 0);
    m_actEnabled = il2cpp_class_get_method_from_name(actCls, "get_enabled", 0);
  }
  Log("boat input lookups: ship=%p instance=%p movement@0x%X stick@0x%X get_action=%p Enable=%p Disable=%p get_enabled=%p",
      shipCls, g_fieldShipInstance, g_offMove, g_offMoveStick, m_getAction, m_actEnable, m_actDisable, m_actEnabled);
  g_fieldSelected = il2cpp_class_get_field_from_name(sel, "selectedCrewMember");
  void* fHead = il2cpp_class_get_field_from_name(crew, "headEnd");
  void* fDir = il2cpp_class_get_field_from_name(crew, "direction");
  if (!m_getMain || !m_getTransform || !m_getPosition || !m_getRotation || !m_setPosRot || !g_fieldSelected || !fHead || !fDir) {
    Log("lookup failed: get_main=%p get_transform=%p get_position=%p get_rotation=%p SetPositionAndRotation=%p selected=%p headEnd=%p direction=%p",
        m_getMain, m_getTransform, m_getPosition, m_getRotation, m_setPosRot, g_fieldSelected, fHead, fDir);
    return false;
  }
  void* conCls = FindClass("ConsoleUtility", "Console");
  if (conCls) {
    g_fieldConsole = il2cpp_class_get_field_from_name(conCls, "s_Console");
    void* fi = il2cpp_class_get_field_from_name(conCls, "consoleInput");
    void* fv = il2cpp_class_get_field_from_name(conCls, "bConsoleVisible");
    if (fi) g_offConsoleInput = (int)il2cpp_field_get_offset(fi);
    if (fv) g_offConsoleVisible = (int)il2cpp_field_get_offset(fv);
  } else Log("ConsoleUtility.Console not found (console probe disabled)");
  g_offHeadEnd = (int)il2cpp_field_get_offset(fHead);
  g_offDirection = (int)il2cpp_field_get_offset(fDir);
  Log("setup ok: CrewMember.headEnd at 0x%X, .direction at 0x%X", g_offHeadEnd, g_offDirection);
  return true;
}

static DWORD WINAPI InitThread(LPVOID) {
  char dir[MAX_PATH]; GetModuleFileNameA(nullptr, dir, MAX_PATH);
  char* slash = strrchr(dir, '\\'); if (slash) *slash = 0;
  char folder[MAX_PATH]; snprintf(folder, sizeof folder, "%s\\BoatCrewMod", dir);
  CreateDirectoryA(folder, nullptr);
  char path[MAX_PATH]; snprintf(path, sizeof path, "%s\\mod.log", folder);
  g_log = fopen(path, "a");                                    // append: other processes must never wipe the log
  Log("---- BoatCrewMod 0.2. pid=%lu", GetCurrentProcessId());
  LoadConfig(folder);
  Log("first-person hotkey: virtual key 0x%X (change it in BoatCrewMod/config.ini)", g_hotkey);

  for (int i = 0; i < 1200 && !(g_ga = GetModuleHandleA("GameAssembly.dll")); i++) Sleep(100);
  if (!g_ga) { Log("GameAssembly.dll was never loaded; giving up"); return 0; }
  if (!Resolve()) { Log("some il2cpp exports are missing; giving up"); return 0; }

  for (int i = 0; i < 1200; i++) { if (DomainReady(&g_domain)) break; Sleep(250); }
  if (!g_domain) { Log("the IL2CPP runtime never became ready; giving up"); return 0; }
  Log("runtime ready. Waiting 5 s for startup to settle");
  Sleep(5000);

  __try {
    il2cpp_thread_attach(g_domain);
    if (Setup() && InstallHook()) {
      g_ready = true;
      Log("ready. Select a crewmate and press the hotkey for first person. sensitivity %.3f", g_sens);
    }
  } __except (EXCEPTION_EXECUTE_HANDLER) { Log("setup faulted; the mod stays idle"); }
  return 0;
}

// The game folder also holds helper programs (UnityCrashHandler64.exe) that load this DLL too. Only the game itself
// should run the mod.
static bool IsGameProcess() {
  char exe[MAX_PATH]; GetModuleFileNameA(nullptr, exe, MAX_PATH);
  const char* name = strrchr(exe, '\\'); name = name ? name + 1 : exe;
  return _stricmp(name, "Boat Crew.exe") == 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(inst);
    if (IsGameProcess()) CreateThread(nullptr, 0, InitThread, nullptr, 0, nullptr);   // never do real work inside DllMain
  }
  return TRUE;
}
