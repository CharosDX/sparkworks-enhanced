// Sparkworks Enhanced: logic nodes Random Timer, Counter, Time, Random and Message in an own editor category, and trigger areas named
// "... (N)", "(=N)", "(<N)", "(>N)", "(@Name)", "(!Name)", "(Kiste N%)" or "(Kiste offen)". In enshrouded_server.exe
// only the parts that run, save and send are installed (no editor). Settings: config.ini, log: debug.log, both next
// to the DLL.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <intrin.h>

// Daytime update function (prologue); its server call is redirected to remember the time object.
static const char UPDATE_FUNCTION_PATTERN[] =
    "40 53 57 48 81 EC B8 00 00 00 48 8B DA 48 8B F9 48 85 C9 75";
#define SYSTEM_CALL_SEARCH 0x100  // the update call sits 0x71 bytes into the system function
// notify_logic_state_change: read of ServerToggleState.state, detoured to see every logic entity.
static const char STATE_READ_PATTERN[] = "48 8B 45 F7 0F B6 18 3A 5E 05 74";
#define STATE_READ_LENGTH 7  // bytes replaced by the detour (mov + movzx)
// Same function: confirms [rbp-0x51] is the LogicGraphNodeComponent.
static const char NODE_CHECK_PATTERN[] = "48 8B 45 AF 81 78 10 28 05 14 B3";
#define NOTIFY_FUNCTION_SIZE 0x1a0
// logic_trigger_shape: start of the loop over the area's query results.
static const char COUNT_RESET_PATTERN[] = "48 8B 75 68 45 32 FF 4C 8B 65 70";
#define COUNT_RESET_LENGTH 7  // bytes replaced by the detour (mov + xor)
// A result inside a sphere area; box areas jump to the same "mov r15b, 1" from COUNT_BOX_PATTERN.
static const char COUNT_MATCH_PATTERN[] = "0F 2F 7E 04 0F 86 ?? ?? ?? ?? 41 B7 01";
#define COUNT_MATCH_LENGTH 10  // bytes replaced by the detour (comiss + jbe); the mov is a jump target and stays
static const char COUNT_BOX_PATTERN[] = "44 0F 2F E1 0F 83 ?? ?? ?? ?? 48 83 C6 10 49 3B F4";
#define COUNT_BOX_JUMP 4  // offset of the jae in COUNT_BOX_PATTERN
// After the loop: the area's result (r15b) is compared with its state.
static const char TRIGGER_RESULT_PATTERN[] = "48 8B 45 98 44 38 38 0F 84 ?? ?? ?? ?? 48 8D 95";
#define TRIGGER_RESULT_LENGTH 7  // bytes replaced by the detour (mov + cmp)
#define TRIGGER_FUNCTION_SIZE 0xb00
#define WORLD_FROM_CONTEXT 0x30   // world = *(*system context + 0x30), as the game's world getter does
// Chest areas: interaction_offer lists every interactive object. Its view is read once more before the game's pass.
static const uint8_t OFFER_START[] = {0x4C, 0x89, 0x74, 0x24, 0x20};  // mov [rsp+0x20], r14
#define OFFER_VIEW_INIT 0x1f  // call view_init in interaction_offer
#define OFFER_VIEW_NEXT 0x31  // call view_next in interaction_offer
// lookup(out, storage - its offset, component type hash, entity): out[0] = the component or NULL. The offset is
// the lea's disp32 at HASH_LOOKUP_OFFSET (the function adds it itself).
static const char HASH_LOOKUP_PATTERN[] =
    "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8D BA ?? ?? ?? ?? 44 89 44 24 40 48 8B F1 48 8D 54 24 40 "
    "48 8B 0F 41 8B D9 48 81 C1 88 00 00 00";
#define HASH_LOOKUP_OFFSET 18

// Node types (game: 0..16, mod: 26..30; commands carry 5 bits). Graphs compile to {u16 op, u16 arg} for a stack VM; the mod adds
// ops 0x100..0x104, which keep the stack depth. Editor, compiler, VM and save code get hooks at the sites below.
static const char MENU_PATTERN[] =
    "B2 02 48 8B CB E8 ?? ?? ?? ?? B2 01 48 8B CB E8 ?? ?? ?? ?? B2 0A EB ?? 33 D2 48 8B CB E8";
#define MENU_CALL 29
static const char CATEGORY_PATTERN[] = "80 F9 10 77 1F 48 8D 15 ?? ?? ?? ?? 0F B6 C1 8B 84 82";
static const char NAME_PATTERN[] = "48 81 C1 90 A1 00 00 80 FA 10 0F 87 ?? ?? ?? ?? 4C 8D 05";
#define NAME_SITE 7
#define NAME_FUNCTION 0x17  // the name lookup function starts this far before NAME_PATTERN
static const char COMPILE_PATTERN[] = "0F B6 02 49 8B D8 48 8B F9 83 F8 10 0F 87 ?? ?? ?? ?? 48 8D 0D";
#define COMPILE_SITE 9
// Compiler helpers: load_input(ctx, input, info) and emit(ctx, op | arg << 16).
static const char COMPILE_HELPERS_PATTERN[] =
    "49 8B 50 30 48 8B CF 8B 12 E8 ?? ?? ?? ?? 84 C0 0F 85 ?? ?? ?? ?? C7 44 24 38 14 00 00 00 48 8B CF 8B 54 24 38 "
    "33 F6 E8";
#define LOAD_INPUT_CALL 9
#define EMIT_CALL 39
// Node header (title, subtitle, colour): switch on the node type.
static const char HEADER_PATTERN[] = "48 8B 98 C8 00 00 00 41 0F B6 06 48 81 C3 90 A1 00 00 83 F8 10 0F 87";
#define HEADER_SITE 18
static const char VM_PATTERN[] = "0F B7 04 88 83 F8 14 77 ?? 41 8B 8C 80";
#define VM_SITE 4
// Switches over the types 0..12 with settings: properties panel, save writer, save loader ("cmp type, 0xc; ja none",
// jump table offset at *_TABLE).
static const char PROPS_PATTERN[] = "0F B6 06 45 33 E4 0F 29 B4 24 B0 01 00 00 83 F8 0C 0F 87 ?? ?? ?? ?? 8B 8C 87";
#define PROPS_SITE 14
#define PROPS_TABLE 26
static const char WRITER_PATTERN[] = "48 C1 E3 04 49 03 5F 28 83 F8 0C 0F 87 ?? ?? ?? ?? 41 8B 8C 85";
#define WRITER_SITE 8
#define WRITER_TABLE 21
static const char LOADER_PATTERN[] =
    "8B 7E 08 FF C7 41 83 FC 0C 0F 87 ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 43 8B 8C A0";
#define LOADER_SITE 5
#define LOADER_TABLE 26
// The loader's cases end with "jmp common", which starts with these bytes.
static const uint8_t LOADER_COMMON[] = {0x4C, 0x8B, 0x75, 0x60};
#define LOADER_CASE_SCAN 0x100
// Create node: "cmp ebx, 0x10; ja none" before the switch that gives types with settings their slot.
static const char CREATE_PATTERN[] = "41 0F B6 5F 04 83 FB 10 77 ?? 48 8D 15";
#define CREATE_SITE 5
#define CREATE_SLOT_CASE 0x20  // from CREATE_SITE: the case that allocates the slot
static const uint8_t CREATE_SLOT_BYTES[] = {0x48, 0x8B, 0xD5, 0x48, 0x8D, 0x4C, 0x24, 0x20, 0xE8};
// Number of input ports of a node type ("cmp eax, 0x10; ja one"); the counter has two.
static const char INPUTS_PATTERN[] = "0F B6 01 83 F8 10 77 ?? 48 8D 15 ?? ?? ?? ?? 8B 8C 82";
#define INPUTS_SITE 3
// Input port style: DFlipFlop and Toggle mark their second input; the counter looks like Toggle.
static const char PORT_STYLE_PATTERN[] = "45 0F B6 11 41 83 EA 08 74 ?? 41 83 FA 01 74";
#define PORT_STYLE_LENGTH 8  // movzx r10d, byte [r9]; sub r10d, 8
#define TYPE_TOGGLE 9
// Node drawing: two more "movzx ecx, byte [rax]; sub ecx, 8" checks for the port labels Oben/Unten.
static const char LABEL_PATTERN_A[] = "0F B6 08 83 E9 08 74 0F 83 F9 01 74 0A C7 44 24 44";
static const char LABEL_PATTERN_B[] = "0F B6 08 83 E9 08 74 1B 83 F9 01 74 16";
#define LABEL_LENGTH 6
// Writer: reads the node's type byte.
static const char WRITER_TYPE_PATTERN[] = "F3 0F 11 34 01 48 8B 47 10 41 0F B6 1C B6 48 FF C0";
#define WRITER_TYPE_SITE 9
#define TYPE_TIMED_TRIGGER 10
#define TYPE_READ_REGISTER 12
// As far from the game's types as the 5-bit command field allows.
#define NODE_RANDOM_TIMER 26
#define NODE_COUNTER 27
#define NODE_TIME 28
#define NODE_RANDOM 29
#define NODE_MESSAGE 30
#define NODE_FIRST NODE_RANDOM_TIMER
#define NODE_LAST NODE_MESSAGE
#define CATEGORY_MOD 4      // own category "Sparkworks Enhanced" (the game has 0..3)
#define CATEGORY_SAVING 3   // last category the game draws in the column
// {u32 colour 0xAABBGGRR, f32 intensity}; set from config.ini (default dark gold).
#define DEFAULT_COLOUR "#A87D12"
static uint64_t mod_colour;
static const char MOD_CATEGORY_NAME[] = "Sparkworks Enhanced";
// Category column: the call that draws the last category (saving states).
static const char COLUMN_PATTERN[] = "44 0F B6 C6 B2 03 48 8B CF E8 ?? ?? ?? ?? 48 8B";
#define COLUMN_CALL 9
// category_entry: branch for unknown categories, and where it continues with text and colour.
#define ENTRY_UNKNOWN 0xb5
#define ENTRY_TEXT_DONE 0x12d     // continues with the text in [rsp+0x40] and the colour in rbx
static const uint8_t ENTRY_UNKNOWN_BYTES[] = {0x0F, 0x10, 0x05};
// Node submenu: the jne taken for categories >= 4.
static const char SUBMENU_PATTERN[] = "83 F9 01 0F 85 ?? ?? ?? ?? B2 09 48 8B CB E8";
#define SUBMENU_JUMP 3
// Node drag preview: colour of an unknown category.
static const char DRAG_PATTERN[] = "44 89 A5 D8 01 00 00 C7 85 DC 01 00 00 00 00 80 3F 48 8B 85 D8 01 00 00 EB";
#define DRAG_JUMP 24
// Properties panel: type check before the description call (the call ends 8 bytes before skip).
static const char DESC_CALL_PATTERN[] = "0F B6 06 48 8D 3D ?? ?? ?? ?? 83 F8 10 77 ?? 0F B6 84 07";
#define DESC_CALL_SITE 10
// Description function: "ja unknown" for types > 16.
static const char DESC_PATTERN[] = "48 81 C2 90 A1 00 00 41 80 F8 10 0F 87";
#define DESC_JUMP 11
#define DESC_EMPTY_CHECK 0x182    // from DESC_JUMP: "je end" when the description reference is empty
#define DESC_DRAW 0x194           // from DESC_JUMP: draws the text in [rsp+0x40]
#define OP_POP 2            // game op: drops arg values
#define OP_FALSE 5
#define OP_TRUE 6
#define OP_TIME 0x100       // top of stack AND time condition(arg): mode in bits 0-1, quarter hours from/to in 2-8/9-15
#define OP_RANDOM 0x101     // top = random gate/hold (arg: chance % in bits 0-6, hold in bit 7)
#define OP_MESSAGE 0x102    // top unchanged; on a rising edge a center message (arg = choice | strand << 3) is queued
#define OP_COUNTER 0x103    // below top = counter(count input below top, reset input on top; arg = N | slot << 6);
                            // the compiler pops the reset value afterwards
#define OP_DELAY 0x104      // top = random delay or clock (arg: index into timer_configs)
#define RANDOM_GATE 0
#define RANDOM_HOLD 1
#define RANDOM_DELAY 2   // Random Timer modes; 0 = fresh node (delay, 1-4 s)
#define RANDOM_PULSE 3
#define RANDOM_DEFAULT_CHANCE 50
#define TIME_MODE_NIGHT 0
#define TIME_MODE_DAY 1
#define TIME_MODE_SPAN 2
#define QUARTER (15 * MINUTE)
#define NODE_INPUTS 0x30    // compiler node info: input id array, count (u64) at +0x38, 16 setting bytes at +0x78
#define NODE_INPUT_COUNT 0x38
#define NODE_SETTINGS 0x78
// The mod's panel marks the settings it sends. A vanilla TimedTrigger's 16 bytes are two times in ns, so byte 15 is
// always 0: changes to mod nodes without the mark come from players without the mod and are ignored.
#define SETTINGS_MARK_AT 15
#define SETTINGS_MARK 0xC5
#define COMPILE_TOO_MANY_INPUTS 7
#define COMPILE_EMIT_FAILED 0xb

typedef struct {
    const char *text;
    uint64_t length;
    uint64_t hash;  // FNV-1a 64 of text, filled when the texts are set up
} NodeText;
#define NODE_TEXT(s) {s, sizeof(s) - 1, 0}

// Node names read by the stubs (current language).
#define NODE_COUNT (NODE_LAST - NODE_FIRST + 1)
static NodeText node_texts[NODE_COUNT] = {
    {"Random Timer", 12, 0},
    {"Counter", 7, 0},
    {"Time", 4, 0},
    {"Random", 6, 0},
    {"Message", 7, 0},
};

#define TIME_OF_DAY 0x48  // ns since midnight
#define TIME_IS_DAY 0x58
#define MINUTE 60000000000LL
#define DAY (24 * 60 * MINUTE)
#define BLOCK_DYNAMIC_NODE 0x10
#define MAX_PATTERN 64
#define MAX_WANTED 256  // different mod names (counts, player lists, chest conditions) in one world
#define VERSION "1.0.0"
#define MAX_NAME 65  // the editor allows 64 characters
#define MAX_PLAYERS 8  // players listed in one "(@Name, ...)" or "(!Name, ...)"
#define NAME_RECORD_CAPACITY 0x100  // name records: key, text pointer, length, capacity, 0, inline text
#define TYPE_CUSTOM_PLAYER_STRING 0x0e699564u  // fnv1a32("keen::ecs::CustomPlayerString"): u64 key of the name
#define NAME_SLOTS 16384          // name keys of one world (power of two)
#define PENDING_NAMES 64          // unknown names waiting for their text
#define NAME_RETRY_MS 500         // an unknown name's text is looked for again this often ...
#define NAME_TRIES 6              // ... this many times, then the name counts as an ordinary one
#define LOAD_SCAN_DELAY_MS 2000   // after a world loads, its names are read this much later
#define AREA_RECHECK_MS 250       // an active area's name is read again this often (renames)

// How a trigger area compares its count with N.
enum { COMPARE_AT_LEAST, COMPARE_EQUAL, COMPARE_FEWER, COMPARE_MORE };
// Chest areas: "(Kiste 80%)" / "(Chest 80%)" fill level, "(Kiste offen)" / "(Chest open)" opened.
enum { CHEST_NONE, CHEST_FILL, CHEST_OPENED };

// A trigger area name found in memory: "(N)", "(=N)", "(<N)", "(>N)", "(@Name, ...)" or "(!Name, ...)".
typedef struct {
    uint64_t key;
    char name[MAX_NAME];
    uint8_t required;  // N of the count, 0 = no count
    uint8_t compare;   // COMPARE_*
    uint8_t player_count;           // player filter: number of listed players, 0 = no filter
    uint64_t players[MAX_PLAYERS];  // player filter: FNV-1a 64 hashes of the listed character names
    uint8_t exclude;                // player filter: 1 = everyone except the listed players
    uint8_t chest;                  // CHEST_*
    uint8_t percent;                // chest fill level, compared with `compare`
} Wanted;

// Every object name of the world: its key (fnv1a64 of the text, as in CustomPlayerString) and what it means for a
// trigger area. The name thread fills the other table and swaps; readers use the table they got.
typedef struct {
    uint64_t keys[NAME_SLOTS];   // 0 = free
    int16_t meaning[NAME_SLOTS]; // index into mods, -1 = an ordinary name
    Wanted mods[MAX_WANTED];
    int mod_count;
    int chest_count;             // mod names with a chest condition: chests are only read while there are some
} NameTable;

typedef void (*UpdateFn)(uint8_t *time, int64_t now);

static UpdateFn original_update;
static uint8_t *volatile server_time;
static volatile uint8_t node_night;  // read by OP_TIME
static int last_is_day = -1;
// Matches so far in the trigger area being evaluated, per thread (areas may be evaluated on several threads).
static __declspec(thread) LONG match_count;
extern ULONG _tls_index;
static void log_line(const char *format, ...);
static int debug_log;  // config.ini debug=1: also routine events (compiles, messages, time of day)
#define debug_line(...) do { if (debug_log) log_line(__VA_ARGS__); } while (0)

static uint64_t random_state = 0x9e3779b97f4a7c15ull;  // mixed with the start time in install()
static NameTable name_tables[2];
static NameTable *volatile name_table = &name_tables[0];
static HANDLE name_event;
static struct { uint64_t key; int tries; } pending_names[PENDING_NAMES];
static int pending_name_count;
static SRWLOCK pending_lock = SRWLOCK_INIT;
// After a world loads, its names are read first: until then mod areas act like vanilla ones and counters do not
// count. Afterwards an area with a new (renamed) name stays off until its text is found (~0.5 s).
static volatile LONG areas_settling = 1, names_ready, load_scan, load_generation;
static char log_path[MAX_PATH];
static char settings_path[MAX_PATH];  // config.ini
static int log_fresh = 1;
static SRWLOCK log_lock = SRWLOCK_INIT;

static void log_line(const char *format, ...)
{
    if (!log_path[0]) return;
    char line[512];
    SYSTEMTIME time;
    GetLocalTime(&time);
    int used = sprintf_s(line, sizeof(line), "%02d:%02d:%02d ", time.wHour, time.wMinute, time.wSecond);
    if (used < 0) return;
    va_list args;
    va_start(args, format);
    _vsnprintf_s(line + used, sizeof(line) - used - 2, _TRUNCATE, format, args);  // too long: cut, never abort
    va_end(args);
    used += (int)strlen(line + used);
    line[used++] = '\r';
    line[used++] = '\n';
    AcquireSRWLockExclusive(&log_lock);
    HANDLE file = CreateFileA(log_path, log_fresh ? GENERIC_WRITE : FILE_APPEND_DATA, FILE_SHARE_READ, NULL,
        log_fresh ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        log_fresh = 0;
        DWORD done;
        WriteFile(file, line, (DWORD)used, &done, NULL);
        CloseHandle(file);
    }
    ReleaseSRWLockExclusive(&log_lock);
}

static void reset_counters(void);
static volatile uint8_t foreign_change;  // set by the node data stub when a change without the mod's mark is ignored
#define WORLD_GAP_MS 10000  // no server update for this long: the world was left (menu, loading)

static void server_update(uint8_t *time, int64_t now)
{
    original_update(time, now);
    if (!time) return;
    // A newly loaded world (other time object, or the server stopped meanwhile): counters start at 0.
    static ULONGLONG last_tick;
    ULONGLONG tick = GetTickCount64();
    if (!last_tick || time != server_time || tick - last_tick > WORLD_GAP_MS) {
        if (last_tick) {
            reset_counters();
            log_line("world loaded, counters start at 0");
        }
        InterlockedIncrement(&load_generation);
        names_ready = 0;
        areas_settling = 1;
        load_scan = 1;
        if (name_event) SetEvent(name_event);
    }
    last_tick = tick;
    server_time = time;
    if (foreign_change) {
        foreign_change = 0;
        log_line("node: a change from a player without this mod version was ignored");
    }
    int is_day = time[TIME_IS_DAY] != 0;
    node_night = (uint8_t)!is_day;
    if (debug_log) {  // in-game time every 2 hours
        static int last_slot = -1;
        int minutes = (int)((uint64_t)(*(int64_t *)(time + TIME_OF_DAY) % DAY) / MINUTE);
        if (minutes / 120 != last_slot) {
            last_slot = minutes / 120;
            log_line("in-game time %02d:%02d", minutes / 60, minutes % 60);
        }
    }
    if (is_day == last_is_day) return;
    debug_line("it is now %s", is_day ? "day" : "night");
    last_is_day = is_day;
}

// Logic id -> entity id of every logic object, for the Message node's recipients.
typedef uint32_t *(*CurrentEntityFn)(void *context, uint32_t *out);
static CurrentEntityFn current_entity;
#define ENTITY_SLOTS 8192
static volatile LONG64 logic_entities[ENTITY_SLOTS];  // logic id << 32 | entity id, 0 = free

static void remember_entity(uint32_t logic_id, uint32_t entity)
{
    if (!logic_id || !entity) return;
    LONG64 value = (LONG64)((uint64_t)logic_id << 32 | entity);
    uint32_t at = (logic_id * 2654435761u) % ENTITY_SLOTS;
    for (int probe = 0; probe < 64; probe++, at = (at + 1) % ENTITY_SLOTS) {
        LONG64 old = logic_entities[at];
        if (old == value) return;
        if (old && (uint32_t)((uint64_t)old >> 32) == logic_id) {  // same object, other entity
            InterlockedExchange64(&logic_entities[at], value);
            return;
        }
        if (!old) {
            old = InterlockedCompareExchange64(&logic_entities[at], value, 0);
            if (!old || old == value) return;
            if ((uint32_t)((uint64_t)old >> 32) == logic_id) {  // another thread took it for this object
                InterlockedExchange64(&logic_entities[at], value);
                return;
            }
        }
    }
}

static uint32_t entity_of(uint32_t logic_id)
{
    uint32_t at = (logic_id * 2654435761u) % ENTITY_SLOTS;
    for (int probe = 0; probe < 64; probe++, at = (at + 1) % ENTITY_SLOTS) {
        uint64_t value = (uint64_t)logic_entities[at];
        if (!value) return 0;
        if ((uint32_t)(value >> 32) == logic_id) return (uint32_t)value;
    }
    return 0;
}

// Every logic entity's state read: remembers logic id -> entity for the Message node.
static void on_logic_state(uint8_t *state, uint8_t *node, uint8_t *block)
{
    if (!state || !node || !current_entity || !block) return;
    __try {
        uint32_t *logic = *(uint32_t **)(block + BLOCK_DYNAMIC_NODE), entity = 0;
        void *context = *(void **)block;
        if (logic && *logic && context) {
            current_entity(context, &entity);
            remember_entity(*logic, entity);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static int listed(const uint64_t *players, int player_count, uint64_t key)
{
    for (int i = 0; i < player_count; i++) {
        if (players[i] == key) return 1;
    }
    return 0;
}

// ---- Trigger area names ----

// Slot of a key in a name table, or of the free slot where it would go (the table is never filled past 3/4).
static uint32_t name_slot(const NameTable *table, uint64_t key)
{
    uint32_t at = (uint32_t)(key ^ key >> 32) & (NAME_SLOTS - 1);
    for (int probe = 0; probe < NAME_SLOTS && table->keys[at] && table->keys[at] != key; probe++) {
        at = (at + 1) & (NAME_SLOTS - 1);
    }
    return at;
}

// An unknown name: its text is looked for by the name thread.
static void request_name(uint64_t key)
{
    int added = 0;
    AcquireSRWLockExclusive(&pending_lock);
    int at = 0;
    while (at < pending_name_count && pending_names[at].key != key) at++;
    if (at == pending_name_count && pending_name_count < PENDING_NAMES) {
        pending_names[pending_name_count].key = key;
        pending_names[pending_name_count++].tries = 0;
        added = 1;
    }
    ReleaseSRWLockExclusive(&pending_lock);
    if (added && name_event) SetEvent(name_event);
}

// ---- Chest areas ----
// interaction_offer's view is read every CHEST_SCAN_EVERY_MS (all its parallel jobs within CHEST_SCAN_WINDOW_MS):
// every object with InventoryEmptyState is a chest. Its slots are summed over its linked 8-slot inventories, and a
// new InteractionOfferRuntime accept time means someone opened it.
#define TYPE_INVENTORY 0xb19528a9u          // 8 ItemStack {u32 item, u32 count, u32 ...}
#define TYPE_INVENTORY_SETUP 0xc24df82au    // u32 linked inventories[16], ..., u8 generic slot count at 0x51
#define TYPE_INVENTORY_EMPTY 0x37c4cfe7u    // only chests have it (not stations)
#define TYPE_CURRENT_TRANSFORM 0x027ac564u  // i64 position[3] (32.32), float rotation[4] at 0x18
#define INVENTORY_SLOTS 8
#define ITEM_STACK_SIZE 12
#define SETUP_LINKS 16
#define SETUP_GENERIC_SLOTS 0x51
#define OFFER_ITER_SIZE 0x70
#define OFFER_ITER_RUNTIME 0x40  // InteractionOfferRuntime: i64 last accept time first
#define VIEW_CURSOR 8            // u32 in the system context, counted up by view_next
#define VIEW_RECORDS 0x830       // context data: one 16-byte record per object, entity id first
#define TRIGGER_ITER_TRANSFORM 8  // logic_trigger_shape iterator: CurrentTransform of the area
#define TRIGGER_ITER_SHAPE 0x10   // LogicTriggerShape: u8 shape (1 = box), float range[3] at 4
#define CHEST_SLOTS 4096
#define CHEST_SCAN_EVERY_MS 500
#define CHEST_SCAN_WINDOW_MS 40
#define CHEST_STALE_MS 3000  // a chest not seen for this long is gone
#define CHEST_PULSE_MS 1000  // "opened" stays on this long
#define CHEST_FOOTPRINT 0.6  // a chest right under an area: centre within this of the area's centre (x and z)
#define CHEST_BELOW 1.6      // ... and at most this far below it

typedef struct {
    uint32_t entity;
    uint16_t filled, slots;
    double at[3];
    int64_t accepted;  // last accept time of its interaction
    ULONGLONG opened;  // tick of the last opening seen
    ULONGLONG seen;    // tick of the last scan that saw it
} Chest;

typedef void **(*HashLookupFn)(void **out, uint8_t *world, uint32_t type, uint32_t entity);
typedef void (*ChestViewInitFn)(void *context, uint8_t *iterator, uint32_t size);
typedef uint8_t (*ChestViewNextFn)(void *context, uint8_t *iterator, uint32_t size);
typedef uint64_t (*ChestSystemFn)(void *context, void *a, void *b, void *c);
static HashLookupFn hash_lookup;
static ChestViewInitFn chest_view_init;
static ChestViewNextFn chest_view_next;
static ChestSystemFn original_offer_system;
static int32_t storage_offset;
static uint8_t *volatile chest_storage;  // component storage (world + storage_offset), from logic_trigger_shape
static int chests_active;                // interaction_offer is hooked: chests are read
static Chest chests[CHEST_SLOTS];
static uint16_t used_chests[CHEST_SLOTS];  // slots that ever held a chest (slots are reused, never emptied)
static int used_chest_count;
static SRWLOCK chest_lock = SRWLOCK_INIT;

static uint8_t *lookup(uint8_t *world, uint32_t type, uint32_t entity)
{
    void *out[2] = {0, 0};
    hash_lookup(out, world, type, entity);
    return (uint8_t *)out[0];
}

static void chest_seen(uint32_t entity, const double *at, int filled, int slots, int64_t accepted, ULONGLONG now)
{
    uint32_t start = (entity * 2654435761u) % CHEST_SLOTS, free_slot = CHEST_SLOTS, found = CHEST_SLOTS;
    AcquireSRWLockExclusive(&chest_lock);
    for (uint32_t i = 0, at_slot = start; i < 64; i++, at_slot = (at_slot + 1) % CHEST_SLOTS) {
        Chest *c = &chests[at_slot];
        if (c->entity == entity) {
            found = at_slot;
            break;
        }
        if (free_slot == CHEST_SLOTS && (!c->entity || now - c->seen >= CHEST_STALE_MS)) free_slot = at_slot;
        if (!c->entity) break;
    }
    uint32_t slot = found != CHEST_SLOTS ? found : free_slot;
    if (slot != CHEST_SLOTS) {
        Chest *c = &chests[slot];
        if (!c->entity) used_chests[used_chest_count++] = (uint16_t)slot;
        // Only a change between two recent scans is an opening.
        int known = found != CHEST_SLOTS && now - c->seen < CHEST_STALE_MS;
        if (known && accepted != c->accepted) c->opened = now;
        if (!known) c->opened = 0;
        c->entity = entity;
        c->filled = (uint16_t)filled;
        c->slots = (uint16_t)slots;
        memcpy(c->at, at, sizeof(c->at));
        c->accepted = accepted;
        c->seen = now;
    }
    ReleaseSRWLockExclusive(&chest_lock);
}

static void read_chest(uint8_t *world, uint32_t entity, uint8_t *runtime, ULONGLONG now)
{
    if (!entity || !lookup(world, TYPE_INVENTORY_EMPTY, entity)) return;
    uint8_t *setup = lookup(world, TYPE_INVENTORY_SETUP, entity);
    uint8_t *transform = lookup(world, TYPE_CURRENT_TRANSFORM, entity);
    if (!setup || !transform) return;
    int filled = 0, inventories = 0;
    for (int l = 0; l < SETUP_LINKS; l++) {
        uint32_t link = ((uint32_t *)setup)[l];
        uint8_t *inventory = link ? lookup(world, TYPE_INVENTORY, link) : NULL;
        if (!inventory) continue;
        inventories++;
        for (int s = 0; s < INVENTORY_SLOTS; s++) filled += *(uint32_t *)(inventory + s * ITEM_STACK_SIZE) != 0;
    }
    int slots = setup[SETUP_GENERIC_SLOTS];
    if (!slots || slots > inventories * INVENTORY_SLOTS) slots = inventories * INVENTORY_SLOTS;
    if (filled > slots) filled = slots;
    double at[3];
    for (int i = 0; i < 3; i++) at[i] = (double)((int64_t *)transform)[i] * (1.0 / 4294967296.0);
    chest_seen(entity, at, filled, slots, runtime ? *(int64_t *)runtime : 0, now);
}

// Reads this job's share of interaction_offer's view; the cursor is put back for the game's own pass.
static void scan_chests(void *context)
{
    static volatile LONG64 window;
    ULONGLONG now = GetTickCount64(), start = (ULONGLONG)window;
    if (now - start >= CHEST_SCAN_WINDOW_MS) {
        if (now - start < CHEST_SCAN_EVERY_MS) return;
        InterlockedCompareExchange64(&window, (LONG64)now, (LONG64)start);
    }
    uint8_t *world = chest_storage - storage_offset;
    __declspec(align(16)) uint8_t it[OFFER_ITER_SIZE];
    uint32_t *cursor = (uint32_t *)((uint8_t *)context + VIEW_CURSOR), saved = *cursor;
    *cursor = 0;
    __try {
        uint8_t *data = *(uint8_t **)context;
        chest_view_init(context, it, OFFER_ITER_SIZE);
        for (int n = 0; n < 100000 && chest_view_next(context, it, OFFER_ITER_SIZE); n++) {
            uint8_t *records = *(uint8_t **)(data + VIEW_RECORDS);
            if (!records || !*cursor) continue;
            uint32_t entity = *(uint32_t *)(records + (size_t)(*cursor - 1) * 16);
            read_chest(world, entity, *(uint8_t **)(it + OFFER_ITER_RUNTIME), now);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    *cursor = saved;
}

static void message_fallback(void *context);

static uint64_t offer_system(void *context, void *a, void *b, void *c)
{
    message_fallback(context);
    if (name_table->chest_count && chest_storage) scan_chests(context);
    return original_offer_system(context, a, b, c);
}

// 2 = point inside the area, 1 = a chest centre right under it, 0 = neither. Box ranges are full sizes in the
// area's rotated frame, a sphere's range[0] is its radius.
static int chest_place(const double *centre, const float *rotation, int box, const float *range, const double *point)
{
    double d[3] = {point[0] - centre[0], point[1] - centre[1], point[2] - centre[2]};
    if (!box) {
        if (d[0] * d[0] + d[1] * d[1] + d[2] * d[2] <= (double)range[0] * range[0]) return 2;
    } else {
        // Into the area's frame: rotate by the conjugate of its rotation (x, y, z, w).
        double u[3] = {-rotation[0], -rotation[1], -rotation[2]}, w = rotation[3];
        double t[3] = {2 * (u[1] * d[2] - u[2] * d[1]), 2 * (u[2] * d[0] - u[0] * d[2]),
            2 * (u[0] * d[1] - u[1] * d[0])};
        double local[3] = {d[0] + w * t[0] + (u[1] * t[2] - u[2] * t[1]),
            d[1] + w * t[1] + (u[2] * t[0] - u[0] * t[2]), d[2] + w * t[2] + (u[0] * t[1] - u[1] * t[0])};
        if (fabs(local[0]) <= range[0] * 0.5 && fabs(local[1]) <= range[1] * 0.5
            && fabs(local[2]) <= range[2] * 0.5) {
            return 2;
        }
    }
    return fabs(d[0]) <= CHEST_FOOTPRINT && fabs(d[2]) <= CHEST_FOOTPRINT && d[1] < 0 && d[1] >= -CHEST_BELOW;
}

static int fill_matches(int filled, int slots, int percent, int compare)
{
    if (!slots) return 0;
    if (compare == COMPARE_EQUAL) return (filled * 100 + slots / 2) / slots == percent;
    if (compare == COMPARE_FEWER) return filled * 100 < percent * slots;
    if (compare == COMPARE_MORE) return filled * 100 > percent * slots;
    return filled * 100 >= percent * slots;
}

// A chest area's result: the chests inside it, or else the highest chest right under it.
static int chest_area(int mode, int percent, int compare, uint8_t *iterator)
{
    if (!iterator) return 0;
    uint8_t *transform = *(uint8_t **)(iterator + TRIGGER_ITER_TRANSFORM);
    uint8_t *shape = *(uint8_t **)(iterator + TRIGGER_ITER_SHAPE);
    if (!transform || !shape) return 0;
    double centre[3];
    for (int i = 0; i < 3; i++) centre[i] = (double)((int64_t *)transform)[i] * (1.0 / 4294967296.0);
    const float *rotation = (const float *)(transform + 0x18), *range = (const float *)(shape + 4);
    ULONGLONG now = GetTickCount64();
    int filled = 0, slots = 0, opened = 0, under = -1;
    double under_height = -1e300;
    AcquireSRWLockShared(&chest_lock);
    for (int u = 0; u < used_chest_count; u++) {
        int i = used_chests[u];
        const Chest *c = &chests[i];
        if (!c->entity || now - c->seen >= CHEST_STALE_MS) continue;
        int place = chest_place(centre, rotation, shape[0] == 1, range, c->at);
        if (place == 2) {
            filled += c->filled;
            slots += c->slots;
            opened |= c->opened && now - c->opened < CHEST_PULSE_MS;
        } else if (place == 1 && c->at[1] > under_height) {
            under = i;
            under_height = c->at[1];
        }
    }
    if (!slots && under >= 0) {
        filled = chests[under].filled;
        slots = chests[under].slots;
        opened = chests[under].opened && now - chests[under].opened < CHEST_PULSE_MS;
    }
    ReleaseSRWLockShared(&chest_lock);
    return mode == CHEST_OPENED ? opened : fill_matches(filled, slots, percent, compare);
}

// Name hash of a player entity (CustomPlayerString), 0 for others.
static uint64_t player_key(uint32_t entity)
{
    uint8_t *storage = chest_storage;
    uint8_t *name = storage && hash_lookup ? lookup(storage - storage_offset, TYPE_CUSTOM_PLAYER_STRING, entity) : NULL;
    return name ? *(uint64_t *)name : 0;
}

// The trigger area being evaluated: the view record at cursor - 1 (entity id first). Its name is read from its
// CustomPlayerString and looked up in the name table; per thread the last areas are kept for AREA_RECHECK_MS. Only
// areas linked in a graph are evaluated by the game, and only while something is inside.
typedef struct {
    uint32_t entity;
    ULONGLONG checked;
    const NameTable *table;
    const Wanted *mod;  // NULL = an ordinary area
    int hold;           // a new name whose text is not known yet: the area stays off
} Area;

#define AREA_CACHE 8  // areas remembered per thread
static const Area *current_area(uint8_t **context)
{
    static __declspec(thread) Area areas[AREA_CACHE];
    static __declspec(thread) unsigned next_area;
    uint32_t cursor = *(uint32_t *)((uint8_t *)context + VIEW_CURSOR);
    uint8_t *records = *(uint8_t **)(*context + VIEW_RECORDS);
    uint32_t entity = records && cursor ? *(uint32_t *)(records + (size_t)(cursor - 1) * 16) : 0;
    const NameTable *table = name_table;
    ULONGLONG now = GetTickCount64();
    Area *area = NULL;
    for (int i = 0; i < AREA_CACHE && !area; i++) {
        if (areas[i].entity == entity && areas[i].checked) area = &areas[i];
    }
    if (area && area->table == table && now - area->checked < AREA_RECHECK_MS) return area;
    if (!area) area = &areas[next_area++ % AREA_CACHE];
    area->entity = entity;
    area->table = table;
    area->checked = now;
    area->mod = NULL;
    area->hold = 0;
    uint8_t *storage = *(uint8_t **)(*context + WORLD_FROM_CONTEXT);
    if (storage) chest_storage = storage;
    uint8_t *name = entity && storage && hash_lookup
        ? lookup(storage - storage_offset, TYPE_CUSTOM_PLAYER_STRING, entity) : NULL;
    uint64_t key = name ? *(uint64_t *)name : 0;
    uint32_t slot = key ? name_slot(table, key) : 0;
    if (!key) {
        // no name: an ordinary area
    } else if (table->keys[slot] == key) {
        if (table->meaning[slot] >= 0) area->mod = &table->mods[table->meaning[slot]];
    } else if (names_ready) {
        area->hold = 1;  // renamed or new: off until the name thread knows the text
        request_name(key);
    }
    if (debug_log) {  // debug=1: each active area's entity, name key and meaning, when it changes
        static struct { uint32_t entity; uint64_t key; } seen[64];
        static SRWLOCK seen_lock = SRWLOCK_INIT;
        AcquireSRWLockExclusive(&seen_lock);
        int at = 0;
        while (at < 64 && seen[at].entity && seen[at].entity != entity) at++;
        int report = at < 64 && (seen[at].entity != entity || seen[at].key != key);
        if (report) {
            seen[at].entity = entity;
            seen[at].key = key;
        }
        ReleaseSRWLockExclusive(&seen_lock);
        if (report) {
            log_line("area %u (name %016llx): %s", entity, (unsigned long long)key,
                !name ? "no name component" : !key ? "empty name"
                : area->mod ? area->mod->name : area->hold ? "unknown, held" : names_ready ? "ordinary name"
                : "names not read yet");
        }
    }
    return area;
}

// An entity is inside a trigger area: returns whether the area switches on.
static int on_trigger_match(uint8_t *state, uint32_t entity, uint8_t **context)
{
    (void)state;
    const Area *area;
    __try {
        area = current_area(context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 1;
    }
    if (area->hold) return 0;
    const Wanted *mod = area->mod;
    if (!mod || !(mod->required || mod->player_count)) return 1;
    if (mod->player_count) {
        uint64_t key;
        __try {
            key = player_key(entity);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            key = 0;
        }
        int in_list = key && listed(mod->players, mod->player_count, key);
        if (mod->exclude ? in_list : !in_list) return 0;
    }
    if (!mod->required) return 1;
    ++match_count;
    // Exact and "fewer" counts look at every entity and decide in on_trigger_result.
    return mod->compare == COMPARE_AT_LEAST && match_count >= mod->required;
}

// The area's result after all entities: chest areas and exact and "fewer" counts decide here, others keep the game's
// result. iterator = logic_trigger_shape's view iterator, context = its system context.
static int on_trigger_result(uint8_t *state, int matched, uint8_t *iterator, uint8_t **context)
{
    (void)state;
    const Area *area;
    __try {
        area = current_area(context);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return matched;
    }
    if (area->hold) return 0;
    const Wanted *mod = area->mod;
    if (!mod) return matched;
    if (mod->chest) {
        __try {
            return hash_lookup && chests_active && chest_area(mod->chest, mod->percent, mod->compare, iterator);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return 0;
        }
    }
    if (!mod->required) return matched;
    if (mod->compare == COMPARE_EQUAL) return match_count == mod->required;
    if (mod->compare == COMPARE_FEWER) return match_count >= 1 && match_count < mod->required;
    return matched;
}

static uint32_t random_below(uint32_t limit)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return (uint32_t)(random_state % limit);
}

static int roll(int chance)
{
    return (int)random_below(100) < chance;
}

static uint64_t fnv1a64(const char *text)
{
    uint64_t hash = 0xcbf29ce484222325ull;
    for (; *text; text++) hash = (hash ^ (uint8_t)*text) * 0x100000001b3ull;
    return hash;
}

static int is_readable(DWORD protect)
{
    // Uncached and write-combined pages are very slow to read.
    if (protect & (PAGE_GUARD | PAGE_NOACCESS | PAGE_NOCACHE | PAGE_WRITECOMBINE)) return 0;
    return (protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
        PAGE_EXECUTE_READWRITE)) != 0;
}

// Steps through committed readable private memory; returns the next region or NULL.
static uint8_t *next_region(uint8_t **cursor, uint8_t **end)
{
    MEMORY_BASIC_INFORMATION info;
    while (VirtualQuery(*cursor, &info, sizeof(info)) == sizeof(info)) {
        *cursor = (uint8_t *)info.BaseAddress + info.RegionSize;
        if (info.State != MEM_COMMIT || info.Type != MEM_PRIVATE || !is_readable(info.Protect)) continue;
        *end = *cursor;
        return info.BaseAddress;
    }
    return NULL;
}

// The N of a name ending in "(N)", "(=N)", "(<N)" or "(>N)" ("(>N)" is stored as at least N + 1); 0 for other
// names. N has one or two digits.
static int required_entities(const char *text, size_t length, uint8_t *compare)
{
    *compare = COMPARE_AT_LEAST;
    if (length < 3 || text[length - 1] != ')') return 0;
    size_t at = length - 1;
    int value = 0, scale = 1;
    while (at > 0 && text[at - 1] >= '0' && text[at - 1] <= '9' && scale <= 10) {
        value += (text[--at] - '0') * scale;
        scale *= 10;
    }
    if (scale == 1 || at == 0) return 0;
    char sign = text[at - 1];
    if (sign == '(') return value;
    if (at < 2 || text[at - 2] != '(') return 0;
    if (sign == '=') *compare = COMPARE_EQUAL;
    else if (sign == '<') *compare = COMPARE_FEWER;
    else if (sign == '>') value++;
    else return 0;
    return value <= 255 ? value : 0;
}

// Reads "(@Name)", "(@Name, @Other)" or the same with "!" (exclude) at the end of a name; "@" and "!" may also be
// full-width. All entries need the same sign.
static int parse_players(const char *text, size_t length, uint64_t *players, uint8_t *player_count, uint8_t *exclude)
{
    const char *open = strrchr(text, '(');
    if (length < 4 || text[length - 1] != ')' || !open) return 0;
    const char *at = open + 1, *end = text + length - 1;
    int count = 0;
    while (at < end) {
        while (at < end && *at == ' ') at++;
        int sign;
        if (at < end && (*at == '@' || *at == '!')) {
            sign = *at == '!';
            at++;
        } else if (end - at >= 3 && (memcmp(at, "\xEF\xBC\xA0", 3) == 0 || memcmp(at, "\xEF\xBC\x81", 3) == 0)) {
            sign = at[2] == '\x81';
            at += 3;
        } else {
            return 0;
        }
        if (count && sign != *exclude) return 0;
        *exclude = (uint8_t)sign;
        const char *stop = at;
        while (stop < end && *stop != ',') stop++;
        const char *last = stop;
        while (last > at && last[-1] == ' ') last--;
        if (last == at || count >= MAX_PLAYERS || last - at >= MAX_NAME) return 0;
        char name[MAX_NAME];
        memcpy(name, at, (size_t)(last - at));
        name[last - at] = 0;
        players[count++] = fnv1a64(name);
        at = stop < end ? stop + 1 : stop;
    }
    *player_count = (uint8_t)count;
    return count > 0;
}

// Reads "(Kiste 80%)", "(Kiste <20%)", "(Kiste >50%)", "(Kiste =0%)", "(Kiste offen)" and the same with "Chest"
// and "open" at the end of a name.
static int parse_chest(const char *text, size_t length, uint8_t *percent, uint8_t *compare)
{
    const char *open = strrchr(text, '(');
    if (length < 4 || text[length - 1] != ')' || !open) return CHEST_NONE;
    const char *at = open + 1, *end = text + length - 1;
    if (end - at < 6 || (_strnicmp(at, "kiste", 5) != 0 && _strnicmp(at, "chest", 5) != 0)) return CHEST_NONE;
    at += 5;
    while (at < end && *at == ' ') at++;
    while (end > at && end[-1] == ' ') end--;
    size_t rest = (size_t)(end - at);
    if ((rest == 5 && _strnicmp(at, "offen", 5) == 0) || (rest == 4 && _strnicmp(at, "open", 4) == 0)) {
        return CHEST_OPENED;
    }
    *compare = COMPARE_AT_LEAST;
    if (at < end && (*at == '<' || *at == '>' || *at == '=')) {
        *compare = *at == '<' ? COMPARE_FEWER : *at == '>' ? COMPARE_MORE : COMPARE_EQUAL;
        at++;
        while (at < end && *at == ' ') at++;
    }
    int value = 0, digits = 0;
    while (at < end && *at >= '0' && *at <= '9' && digits < 3) {
        value = value * 10 + (*at++ - '0');
        digits++;
    }
    while (at < end && *at == ' ') at++;
    if (!digits || value > 100 || at + 1 != end || *at != '%') return CHEST_NONE;
    *percent = (uint8_t)value;
    return CHEST_FILL;
}

// One pass over memory: every name record (key, text pointer to its inline text, length, capacity, 0, text) whose
// key is the hash of its text goes into the table, mod names with their meaning.
static void read_names(NameTable *table)
{
    memset(table->keys, 0, sizeof(table->keys));
    table->mod_count = 0;
    table->chest_count = 0;
    int stored = 0;
    uint8_t *cursor = NULL, *start, *end;
    while ((start = next_region(&cursor, &end)) != NULL) {
        __try {
            for (uint64_t *p = (uint64_t *)start; (uint8_t *)(p + 6) <= end; p++) {
                if (p[3] != NAME_RECORD_CAPACITY || p[2] >= NAME_RECORD_CAPACITY || p[1] != (uint64_t)(p + 5)) continue;
                uint32_t slot = name_slot(table, p[0]);
                if (table->keys[slot] == p[0] || !p[0] || stored >= NAME_SLOTS / 4 * 3) continue;
                char text[NAME_RECORD_CAPACITY];
                memcpy(text, p + 5, (size_t)p[2]);
                text[p[2]] = 0;
                if (fnv1a64(text) != p[0]) continue;
                Wanted entry = {0};
                entry.required = (uint8_t)required_entities(text, (size_t)p[2], &entry.compare);
                int mod = entry.required
                    || parse_players(text, (size_t)p[2], entry.players, &entry.player_count, &entry.exclude);
                if (!mod) {
                    entry.chest = (uint8_t)parse_chest(text, (size_t)p[2], &entry.percent, &entry.compare);
                    mod = entry.chest != CHEST_NONE;
                }
                table->keys[slot] = p[0];
                table->meaning[slot] = -1;
                stored++;
                if (!mod || table->mod_count >= MAX_WANTED) continue;
                entry.key = p[0];
                strncpy_s(entry.name, MAX_NAME, text, _TRUNCATE);
                table->chest_count += entry.chest != CHEST_NONE;
                table->meaning[slot] = (int16_t)table->mod_count;
                table->mods[table->mod_count++] = entry;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
        }
    }
    static volatile LONG warned;
    if (stored >= NAME_SLOTS / 4 * 3 && !InterlockedExchange(&warned, 1)) {
        log_line("%d object names in memory, newer ones are not read", stored);
    }
}

// Names whose text was never found (this world): they stay ordinary in every later table.
#define GIVEN_UP_NAMES 256
static uint64_t given_up[GIVEN_UP_NAMES];
static int given_up_count;

// Reads all names into the free table and swaps it in. Pending (unknown) names found now are done; others are
// tried again a few times and then count as ordinary names. new_world forgets the names given up on.
static void update_names(int new_world)
{
    NameTable *old = name_table, *fresh = old == &name_tables[0] ? &name_tables[1] : &name_tables[0];
    if (new_world) given_up_count = 0;
    read_names(fresh);
    for (int i = 0; i < given_up_count; i++) {
        uint32_t slot = name_slot(fresh, given_up[i]);
        if (fresh->keys[slot]) continue;
        fresh->keys[slot] = given_up[i];
        fresh->meaning[slot] = -1;
    }
    AcquireSRWLockExclusive(&pending_lock);
    int kept = 0;
    for (int i = 0; i < pending_name_count; i++) {
        uint64_t key = pending_names[i].key;
        uint32_t slot = name_slot(fresh, key);
        if (fresh->keys[slot] == key) continue;
        if (++pending_names[i].tries < NAME_TRIES) {
            pending_names[kept++] = pending_names[i];
            continue;
        }
        fresh->keys[slot] = key;  // text never found: the area acts like an ordinary one
        fresh->meaning[slot] = -1;
        if (given_up_count < GIVEN_UP_NAMES) given_up[given_up_count++] = key;
        log_line("name %016llx of a trigger area not found in memory, the area acts normally",
            (unsigned long long)key);
    }
    pending_name_count = kept;
    ReleaseSRWLockExclusive(&pending_lock);
    for (int i = 0; i < fresh->mod_count; i++) {
        uint32_t slot = name_slot(old, fresh->mods[i].key);
        if (old->keys[slot] != fresh->mods[i].key || old->meaning[slot] < 0) {
            log_line("area name \"%s\" known", fresh->mods[i].name);
        }
    }
    name_table = fresh;  // the old table is refilled at the next update, long after readers are done with it
}

static DWORD WINAPI name_thread(LPVOID unused)
{
    (void)unused;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    for (;;) {
        WaitForSingleObject(name_event, pending_name_count ? NAME_RETRY_MS : INFINITE);
        int loading = InterlockedExchange(&load_scan, 0);
        LONG generation = load_generation;
        if (loading) Sleep(LOAD_SCAN_DELAY_MS);  // let the world's names arrive
        ULONGLONG start = GetTickCount64();
        update_names(loading);
        if (!loading) continue;
        if (generation != load_generation) {  // another world loaded meanwhile: read again for that one
            load_scan = 1;
            SetEvent(name_event);
            continue;
        }
        names_ready = 1;
        areas_settling = 0;
        debug_line("names read (%d mod names, %llu ms), counters count again", name_table->mod_count,
            GetTickCount64() - start);
    }
}

static uint8_t *alloc_near(uint8_t *image, SIZE_T image_size)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    uintptr_t granularity = info.dwAllocationGranularity;
    uintptr_t start = ((uintptr_t)image + image_size + granularity) & ~(granularity - 1);
    for (uintptr_t address = start; address < (uintptr_t)image + 0x70000000; address += granularity) {
        void *page = VirtualAlloc((void *)address, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (page) return page;
    }
    return NULL;
}

static int parse_pattern(const char *text, uint8_t *bytes, uint8_t *mask)
{
    int length = 0;
    while (*text) {
        if (*text == ' ') { text++; continue; }
        if (length >= MAX_PATTERN || !text[1]) return 0;
        if (text[0] == '?' && text[1] == '?') {
            bytes[length] = 0;
            mask[length] = 0;
        } else {
            char hex[3] = {text[0], text[1], 0};
            char *end;
            bytes[length] = (uint8_t)strtoul(hex, &end, 16);
            if (*end) return 0;
            mask[length] = 1;
        }
        length++;
        text += 2;
    }
    return length;
}

static uint8_t *find_unique(uint8_t *start, uint8_t *end, const char *pattern)
{
    uint8_t bytes[MAX_PATTERN], mask[MAX_PATTERN];
    int length = parse_pattern(pattern, bytes, mask);
    uint8_t *found = NULL;
    if (length == 0 || end - start < length) return NULL;
    for (uint8_t *at = start; at <= end - length; at++) {
        int match = 1;
        for (int i = 0; i < length && match; i++) match = !mask[i] || at[i] == bytes[i];
        if (!match) continue;
        if (found) return NULL;
        found = at;
    }
    return found;
}

static uint8_t *call_target(uint8_t *call)
{
    if (call[0] != 0xE8) return NULL;
    return call + 5 + *(int32_t *)(call + 1);
}

static void write_jump(uint8_t *stub, void *target)
{
    stub[0] = 0xFF;
    stub[1] = 0x25;
    *(uint32_t *)(stub + 2) = 0;
    *(void **)(stub + 6) = target;
}

// State read: on_logic_state(state, node component, iterator), then the replaced instructions.
static void write_state_stub(uint8_t *stub, uint8_t *back)
{
    static const uint8_t CODE[] = {
        0x48, 0x8B, 0x45, 0xF7,                    // mov rax, [rbp-9]
        0x51, 0x52, 0x41, 0x50, 0x41, 0x51,        // push rcx, rdx, r8, r9
        0x41, 0x52, 0x41, 0x53,                    // push r10, r11
        0x48, 0x89, 0xC1,                          // mov rcx, rax
        0x48, 0x8B, 0x55, 0xAF,                    // mov rdx, [rbp-0x51]
        0x4C, 0x8D, 0x45, 0xA7,                    // lea r8, [rbp-0x59]
        0x48, 0x83, 0xEC, 0x20,                    // sub rsp, 0x20
        0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,        // call [rip+disp] -> on_logic_state
        0x48, 0x83, 0xC4, 0x20,                    // add rsp, 0x20
        0x41, 0x5B, 0x41, 0x5A,                    // pop r11, r10
        0x41, 0x59, 0x41, 0x58, 0x5A, 0x59,        // pop r9, r8, rdx, rcx
        0x48, 0x8B, 0x45, 0xF7,                    // mov rax, [rbp-9]
        0x0F, 0xB6, 0x18,                          // movzx ebx, byte [rax]
        0xFF, 0x25, 0x00, 0x00, 0x00, 0x00,        // jmp [rip+disp] -> back
    };
    enum { CALL_AT = 29, JMP_AT = 56, TARGETS_AT = 64 };
    memcpy(stub, CODE, sizeof(CODE));
    *(int32_t *)(stub + CALL_AT + 2) = TARGETS_AT - (CALL_AT + 6);
    *(int32_t *)(stub + JMP_AT + 2) = (TARGETS_AT + 8) - (JMP_AT + 6);
    *(void **)(stub + TARGETS_AT) = (void *)on_logic_state;
    *(void **)(stub + TARGETS_AT + 8) = (void *)back;
}

// A match in logic_trigger_shape (box areas enter at MATCH_COMMON): on_trigger_match decides between the next
// result and "mov r15b, 1" (area on).
enum { MATCH_COMMON = 10 };
static void write_match_stub(uint8_t *stub, uint8_t *next, uint8_t *after)
{
    static const uint8_t CODE[] = {
        0x0F, 0x2F, 0x7E, 0x04,                    // comiss xmm7, [rsi+4]
        0x0F, 0x86, 0x00, 0x00, 0x00, 0x00,        // jbe next
        0x48, 0x8B, 0x4D, 0x98,                    // mov rcx, [rbp-0x68]
        0x8B, 0x16,                                // mov edx, [rsi]  (entity of the query result)
        0x4D, 0x8B, 0xC6,                          // mov r8, r14  (system context)
        0x48, 0x83, 0xEC, 0x20,                    // sub rsp, 0x20
        0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,        // call [rip+disp] -> on_trigger_match
        0x48, 0x83, 0xC4, 0x20,                    // add rsp, 0x20
        0x84, 0xC0,                                // test al, al
        0x0F, 0x84, 0x00, 0x00, 0x00, 0x00,        // je next
        0x41, 0xB7, 0x01,                          // mov r15b, 1
        0xE9, 0x00, 0x00, 0x00, 0x00,              // jmp after
    };
    enum { JBE_AT = 4, CALL_AT = 23, JE_AT = 35, JMP_AT = 44, TARGET_AT = 56 };
    memcpy(stub, CODE, sizeof(CODE));
    *(int32_t *)(stub + JBE_AT + 2) = (int32_t)(next - (stub + JBE_AT + 6));
    *(int32_t *)(stub + CALL_AT + 2) = TARGET_AT - (CALL_AT + 6);
    *(int32_t *)(stub + JE_AT + 2) = (int32_t)(next - (stub + JE_AT + 6));
    *(int32_t *)(stub + JMP_AT + 1) = (int32_t)(after - (stub + JMP_AT + 5));
    *(void **)(stub + TARGET_AT) = (void *)on_trigger_match;
}

// Start of the results loop: the replaced instructions, then this thread's match_count = 0 (no call: xmm0 and rax
// are live). A thread's TLS block is [gs:0x58][_tls_index]; match_count has the same offset in every block.
static void write_reset_stub(uint8_t *stub, uint8_t *back)
{
    static const uint8_t CODE[] = {
        0x48, 0x8B, 0x75, 0x68,                    // mov rsi, [rbp+0x68]
        0x45, 0x32, 0xFF,                          // xor r15b, r15b
        0x50, 0x51,                                // push rax, rcx
        0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0, 0, 0,  // mov rax, gs:[0x58]
        0xB9, 0, 0, 0, 0,                          // mov ecx, _tls_index
        0x48, 0x8B, 0x04, 0xC8,                    // mov rax, [rax+rcx*8]
        0xC7, 0x80, 0, 0, 0, 0, 0, 0, 0, 0,        // mov dword [rax+offset], 0
        0x59, 0x58,                                // pop rcx, rax
        0xE9, 0x00, 0x00, 0x00, 0x00,              // jmp back
    };
    enum { INDEX_AT = 19, OFFSET_AT = 29, JMP_AT = 39 };
    uint8_t *block = ((uint8_t **)__readgsqword(0x58))[_tls_index];
    memcpy(stub, CODE, sizeof(CODE));
    *(uint32_t *)(stub + INDEX_AT) = _tls_index;
    *(int32_t *)(stub + OFFSET_AT) = (int32_t)((uint8_t *)&match_count - block);
    *(int32_t *)(stub + JMP_AT + 1) = (int32_t)(back - (stub + JMP_AT + 5));
}

// Result of a trigger area: r15b = on_trigger_result(state, r15b, iterator, context), then the replaced compare (its
// flags reach the je).
static void write_result_stub(uint8_t *stub, uint8_t *back)
{
    static const uint8_t CODE[] = {
        0x48, 0x8B, 0x4D, 0x98,                    // mov rcx, [rbp-0x68]
        0x41, 0x0F, 0xB6, 0xD7,                    // movzx edx, r15b
        0x4C, 0x8D, 0x44, 0x24, 0x50,              // lea r8, [rsp+0x50]  (view iterator)
        0x4D, 0x8B, 0xCE,                          // mov r9, r14  (system context)
        0x48, 0x83, 0xEC, 0x20,                    // sub rsp, 0x20
        0xFF, 0x15, 0x00, 0x00, 0x00, 0x00,        // call [rip+disp] -> on_trigger_result
        0x48, 0x83, 0xC4, 0x20,                    // add rsp, 0x20
        0x41, 0x88, 0xC7,                          // mov r15b, al
        0x48, 0x8B, 0x45, 0x98,                    // mov rax, [rbp-0x68]
        0x44, 0x38, 0x38,                          // cmp [rax], r15b
        0xE9, 0x00, 0x00, 0x00, 0x00,              // jmp back
    };
    enum { CALL_AT = 20, JMP_AT = 40, TARGET_AT = 48 };
    memcpy(stub, CODE, sizeof(CODE));
    *(int32_t *)(stub + CALL_AT + 2) = TARGET_AT - (CALL_AT + 6);
    *(int32_t *)(stub + JMP_AT + 1) = (int32_t)(back - (stub + JMP_AT + 5));
    *(void **)(stub + TARGET_AT) = (void *)on_trigger_result;
}

static int redirect_call(uint8_t *call, uint8_t *stub)
{
    int64_t rel = (int64_t)(stub - (call + 5));
    if (rel < INT32_MIN || rel > INT32_MAX) return 0;
    DWORD old;
    if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *(int32_t *)(call + 1) = (int32_t)rel;
    VirtualProtect(call, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), call, 5);
    return 1;
}

// Replaces `length` bytes at `site` with "jmp stub" plus nops.
static int write_detour(uint8_t *site, int length, uint8_t *stub)
{
    int64_t rel = (int64_t)(stub - (site + 5));
    if (rel < INT32_MIN || rel > INT32_MAX) return 0;
    DWORD old;
    if (!VirtualProtect(site, length, PAGE_EXECUTE_READWRITE, &old)) return 0;
    site[0] = 0xE9;
    *(int32_t *)(site + 1) = (int32_t)rel;
    for (int i = 5; i < length; i++) site[i] = 0x90;
    VirtualProtect(site, length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, length);
    return 1;
}

static int section(uint8_t *image, IMAGE_NT_HEADERS64 *nt, const char *name, uint8_t **start, uint8_t **end)
{
    IMAGE_SECTION_HEADER *header = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; i++, header++) {
        if (strncmp((const char *)header->Name, name, 8) == 0) {
            *start = image + header->VirtualAddress;
            *end = *start + header->Misc.VirtualSize;
            return 1;
        }
    }
    return 0;
}

// Engine systems are registered as {name, name length, function}; returns the function of the named system.
static uint8_t *system_function(uint8_t *data, uint8_t *data_end, const char *name)
{
    size_t length = strlen(name);
    uint8_t *text = NULL;
    for (uint8_t *at = data; at + length + 1 <= data_end; at++) {
        if (at[-1] == 0 && memcmp(at, name, length + 1) == 0) { text = at; break; }
    }
    if (!text) return NULL;
    for (uint8_t *at = (uint8_t *)(((uintptr_t)data + 7) & ~(uintptr_t)7); at + 24 <= data_end; at += 8) {
        if (*(uint8_t **)at == text && *(uint64_t *)(at + 8) == length) return *(uint8_t **)(at + 16);
    }
    return NULL;
}

static uint8_t *find_update_call(uint8_t *function, uint8_t *target)
{
    if (!function) return NULL;
    for (uint8_t *at = function; at < function + SYSTEM_CALL_SEARCH; at++) {
        if (call_target(at) == target) return at;
    }
    return NULL;
}

static int inside(uint8_t *at, uint8_t *function, size_t size)
{
    return at && function && at > function && at < function + size;
}

// ---- Node hooks ----

// Tiny code writer for the node stubs.
typedef struct { uint8_t *at; } Asm;

static void asm_hex(Asm *a, const char *hex)
{
    uint8_t bytes[MAX_PATTERN], mask[MAX_PATTERN];
    int length = parse_pattern(hex, bytes, mask);
    memcpy(a->at, bytes, length);
    a->at += length;
}

static void asm_rel32(Asm *a, const uint8_t *target)
{
    *(int32_t *)a->at = (int32_t)(target - (a->at + 4));
    a->at += 4;
}

static void asm_u64(Asm *a, uint64_t value)
{
    memcpy(a->at, &value, 8);
    a->at += 8;
}

// Writes a short jump displacement placeholder and returns it for asm_land.
static uint8_t *asm_rel8(Asm *a)
{
    *a->at = 0;
    return a->at++;
}

static void asm_land(Asm *a, uint8_t *rel8)
{
    *rel8 = (uint8_t)(a->at - (rel8 + 1));
}

// "cmp reg, value" from its opcode as hex: opcode 81 takes an imm32, the byte compares an imm8.
static void asm_compare(Asm *a, const char *compare, uint32_t value)
{
    asm_hex(a, compare);
    if (strstr(compare, "81 ")) {
        memcpy(a->at, &value, 4);
        a->at += 4;
    } else {
        *a->at++ = (uint8_t)value;
    }
}

// Jumps to *low / the returned rel8 unless first <= reg <= last (unsigned).
static uint8_t *asm_range(Asm *a, const char *compare, uint32_t first, uint32_t last, uint8_t **low)
{
    asm_compare(a, compare, first);
    asm_hex(a, "72");
    *low = asm_rel8(a);
    asm_compare(a, compare, last);
    asm_hex(a, "77");
    return asm_rel8(a);
}

// Jumps away unless the register holds a mod type.
static uint8_t *asm_new_type_check(Asm *a, const char *compare, uint8_t **other_low)
{
    return asm_range(a, compare, NODE_FIRST, NODE_LAST, other_low);
}

static int fits_rel32(const uint8_t *from, const uint8_t *to)
{
    int64_t rel = (int64_t)(to - from);
    return rel > INT32_MIN / 2 && rel < INT32_MAX / 2;
}

typedef void (*ButtonFn)(void *editor, uint8_t type);
typedef uint8_t (*LoadInputFn)(void *ctx, uint32_t input, uint8_t *info);
typedef uint8_t (*EmitFn)(void *ctx, uint32_t instruction);
static ButtonFn original_button;
static LoadInputFn load_input;
static EmitFn emit;

typedef struct {
    uint8_t *site, *stub;
    int length;  // bytes replaced by the detour; 0 = call redirect
} Hook;

// Patch order: the loader before the writer, the VM before the compiler.
enum { HOOK_VM, HOOK_COMPILE, HOOK_CATEGORY, HOOK_NAME, HOOK_HEADER, HOOK_PROPS, HOOK_LOADER_DONE,
    HOOK_WRITER, HOOK_WRITER_TYPE, HOOK_CREATE, HOOK_DRAG, HOOK_ENTRY, HOOK_SUBMENU, HOOK_MENU, HOOK_COLUMN, HOOK_DESC,
    HOOK_DESC_CALL, HOOK_PUZZLE, HOOK_INPUTS, HOOK_PORT_STYLE, HOOK_LABEL_A, HOOK_LABEL_B, HOOK_NODE_DATA, HOOK_COUNT };
static Hook node_hooks[HOOK_COUNT];

// Own category: one more column entry and its node buttons.
static void update_language(void);
typedef void (*CategoryEntryFn)(void *editor, uint8_t category, uint8_t flag);
static CategoryEntryFn original_category_entry;

// Wraps the column's last category_entry call (saving states): adds the mod's category after it.
static void category_entries(void *editor, uint8_t category, uint8_t flag)
{
    original_category_entry(editor, category, flag);
    if (category == CATEGORY_SAVING) original_category_entry(editor, CATEGORY_MOD, flag);
}

// Node submenu for categories the game does not know: the mod's buttons.
static void mod_buttons(void *editor)
{
    update_language();
    original_button(editor, NODE_TIME);
    original_button(editor, NODE_RANDOM);
    original_button(editor, NODE_RANDOM_TIMER);
    original_button(editor, NODE_COUNTER);
    original_button(editor, NODE_MESSAGE);
}

// The game's own buttons, with the language kept current.
static void node_button(void *editor, uint8_t type)
{
    update_language();
    original_button(editor, type);
}

// ---- Node state slots ----
// Random, Random Timer and Message nodes keep state per VM instruction address. Every compile gives new addresses,
// so slots unused for SLOT_STALE_MS are taken again (also a base out of range: its nodes start fresh when it loads).
#define SLOT_STALE_MS 30000
typedef struct { const void *instruction; ULONGLONG used; } SlotKey;  // first member of every slot

// Slot of a node in a table (caller holds the table's lock); *fresh = 1 when the slot was (re)assigned.
static uint32_t node_slot(void *table, size_t stride, uint32_t size, const void *instruction, ULONGLONG now,
    int *fresh)
{
    uint32_t start = (uint32_t)(((uintptr_t)instruction >> 2) % size), at = start, empty = size, reuse = size;
    for (uint32_t probe = 0; probe < size; probe++, at = (at + 1) % size) {
        SlotKey *key = (SlotKey *)((uint8_t *)table + at * stride);
        if (key->instruction == instruction) {
            key->used = now;
            *fresh = 0;
            return at;
        }
        if (!key->instruction) {
            empty = at;
            break;
        }
        if (reuse == size && now - key->used >= SLOT_STALE_MS) reuse = at;
    }
    uint32_t slot = empty < size ? empty : reuse < size ? reuse : start;
    if (slot == start && empty == size && reuse == size) {
        static volatile LONG warned;
        if (!InterlockedExchange(&warned, 1)) log_line("%u nodes of one kind in use at once, some share their state", size);
    }
    SlotKey *key = (SlotKey *)((uint8_t *)table + slot * stride);
    key->instruction = instruction;
    key->used = now;
    *fresh = 1;
    return slot;
}

// ---- Random node runtime ----
// Per node: last input and last roll.
#define RANDOM_SLOTS 1024
static struct { SlotKey key; uint8_t input, value; } random_slots[RANDOM_SLOTS];
static SRWLOCK random_lock = SRWLOCK_INIT;

// Rolls when the input turns on. Gate: passes the input while the roll won. Hold: outputs the roll.
static uint8_t random_output(const void *instruction, uint8_t input, uint32_t arg)
{
    int chance = arg & 0x7f, hold = (arg >> 7) & 1, fresh;
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&random_lock);
    uint32_t at = node_slot(random_slots, sizeof(random_slots[0]), RANDOM_SLOTS, instruction, now, &fresh);
    if (fresh) {
        random_slots[at].input = 0;
        random_slots[at].value = 0;
    }
    if (input && !random_slots[at].input) random_slots[at].value = (uint8_t)roll(chance);
    random_slots[at].input = input;
    uint8_t value = random_slots[at].value;
    ReleaseSRWLockExclusive(&random_lock);
    return hold ? value : (uint8_t)(input && value);
}

// Random Timer times in ms (From, To, fixed signal duration; 0 = like the input).
#define TIMER_DEFAULT_FROM 1000
#define TIMER_DEFAULT_TO 4000
#define TIMER_MAX_MS 3600000u     // From and To up to 1 h
#define TIMER_MAX_SIGNAL 299999u  // the signal duration is saved in the sub-millisecond part
#define TIMER_CONFIGS 65536  // the op argument has 16 bits

// Compiled timer settings; the op argument is an index into this table.
typedef struct { uint32_t from, to, signal, pulse; } TimerConfig;
static TimerConfig timer_configs[TIMER_CONFIGS];
static int timer_config_count;

static uint32_t timer_config_index(TimerConfig config)
{
    AcquireSRWLockExclusive(&random_lock);
    int at = 0;
    while (at < timer_config_count && memcmp(&timer_configs[at], &config, sizeof(config)) != 0) at++;
    if (at == timer_config_count && at < TIMER_CONFIGS) timer_configs[timer_config_count++] = config;
    int full = at >= TIMER_CONFIGS;
    if (full) at = 0;
    ReleaseSRWLockExclusive(&random_lock);
    static volatile LONG warned;
    if (full && !InterlockedExchange(&warned, 1)) {
        log_line("%d different Random Timer settings used, newer ones use the first until restart", TIMER_CONFIGS);
    }
    return (uint32_t)at;
}

// Per timer node: last input, phase, and when the current phase ends (GetTickCount64 ms).
enum { DELAY_IDLE, DELAY_WAITING, DELAY_ON };
static struct { SlotKey key; uint8_t input, phase; ULONGLONG until; } delay_slots[RANDOM_SLOTS];

// Delay: a signal waits a random time between From and To; then it follows the input (signal 0) or stays on
// for the fixed signal time, ignoring new signals until it is done.
// Clock: while the input is on, off and on phases alternate, each lasting a random time between From and To.
static uint8_t delay_output(const void *instruction, uint8_t input, uint32_t arg, ULONGLONG now)
{
    int fresh;
    AcquireSRWLockExclusive(&random_lock);
    TimerConfig config = timer_configs[arg < TIMER_CONFIGS ? arg : 0];
    uint32_t low = config.from < config.to ? config.from : config.to;
    uint32_t high = config.from < config.to ? config.to : config.from, signal = config.signal;
    uint32_t at = node_slot(delay_slots, sizeof(delay_slots[0]), RANDOM_SLOTS, instruction, now, &fresh);
    if (fresh) {
        delay_slots[at].input = 0;
        delay_slots[at].phase = DELAY_IDLE;
    }
    int rising = input && !delay_slots[at].input;
    delay_slots[at].input = input;
    if (config.pulse) {
        if (!input) delay_slots[at].phase = DELAY_IDLE;
        else if (delay_slots[at].phase == DELAY_IDLE || now >= delay_slots[at].until) {
            delay_slots[at].phase = delay_slots[at].phase == DELAY_WAITING ? DELAY_ON : DELAY_WAITING;
            delay_slots[at].until = now + low + random_below(high - low + 1);
        }
        uint8_t on = delay_slots[at].phase == DELAY_ON;
        ReleaseSRWLockExclusive(&random_lock);
        return on;
    }
    if (rising && delay_slots[at].phase == DELAY_IDLE) {
        delay_slots[at].phase = DELAY_WAITING;
        delay_slots[at].until = now + low + random_below(high - low + 1);
    }
    if (delay_slots[at].phase == DELAY_WAITING) {
        if (!signal && !input) delay_slots[at].phase = DELAY_IDLE;
        else if (now >= delay_slots[at].until) {
            delay_slots[at].phase = DELAY_ON;
            delay_slots[at].until = now + signal;  // from now: a late check still shows the whole signal
        }
    }
    if (delay_slots[at].phase == DELAY_ON && (signal ? now >= delay_slots[at].until : !input)) {
        delay_slots[at].phase = DELAY_IDLE;
    }
    uint8_t value = delay_slots[at].phase == DELAY_ON;
    ReleaseSRWLockExclusive(&random_lock);
    return value;
}

// ---- Message node runtime ----
// OP_MESSAGE queues a message when its input turns on; the wrapped puzzle_completed_event system (or, while it is
// idle, interaction_offer) sends it as a PuzzleCompletedEvent {player ids[16] +8, puzzle type +0x48, unlocked +0x49}.
#define MESSAGE_CHOICES 8           // saved choice = puzzle type * 2 + was unlocked
#define MESSAGE_OPTIONS 3           // dropdown entries: door, alarm raised, alarm resolved (no player-built barriers)
#define PUZZLE_EVENT_TYPE 0x5a3baf30u
#define PUZZLE_EVENT_SIZE 0x50
#define PUZZLE_EVENT_PLAYERS 8
#define PUZZLE_EVENT_KIND 0x48
#define PUZZLE_EVENT_UNLOCKED 0x49
#define PUZZLE_ITER_SIZE 0x68
#define PUZZLE_ITER_PLAYERS 0x48
#define PUZZLE_ITER_EVENTS 0x60
#define PLAYER_SLOTS 16
#define PLAYER_STRIDE 0xa0
#define MAX_PENDING_MESSAGES 16
// Dropdown texts (current language) and the saved choice of each option.
static NodeText message_texts[MESSAGE_OPTIONS] = {
    NODE_TEXT("Door opened"), NODE_TEXT("Alarm raised"), NODE_TEXT("Alarm resolved"),
};
static const uint8_t OPTION_CHOICE[MESSAGE_OPTIONS] = {1, 6, 7};

// Dropdown option of a saved choice (0, 1, 6 or 7).
static uint32_t message_option(uint32_t choice)
{
    return choice >= 6 ? choice - 5 : 0;
}
typedef uint64_t (*SystemFn)(void *context, void *a, void *b, void *c);
typedef void (*ViewInitFn)(void *context, uint8_t *iterator, uint32_t size);
typedef uint16_t (*EventIndexFn)(void *writer);
typedef void *(*EventQueueFn)(void *writer);
typedef uint8_t *(*EventNewFn)(void *queue, uint32_t type, uint32_t size, uint16_t index);
static SystemFn original_puzzle_system;  // trampoline into the original system
static ViewInitFn view_init;
static EventIndexFn event_index;
static EventQueueFn event_queue;
static EventNewFn event_new;
#define MESSAGE_MAX_AGE_MS 5000  // a message that could not be sent this long is dropped
static uint16_t pending_messages[MAX_PENDING_MESSAGES];  // op argument: choice | strand << 3
static ULONGLONG pending_times[MAX_PENDING_MESSAGES];   // when it was queued
static int pending_message_count;
static SRWLOCK message_lock = SRWLOCK_INIT;
#define MESSAGE_SLOTS 1024
static struct { SlotKey key; uint8_t input; } message_slots[MESSAGE_SLOTS];

// Strand = logic ids of the world objects linked to a Message node. OP_MESSAGE's arg = choice | strand index << 3;
// index 0 = no objects (everyone gets the message).
#define MAX_STRANDS 8192
#define STRAND_OBJECTS 32
typedef struct { uint8_t count; uint32_t ids[STRAND_OBJECTS]; } Strand;
static Strand strands[MAX_STRANDS];
static volatile LONG strand_count = 1;
static SRWLOCK strand_lock = SRWLOCK_INIT;
// Graph being compiled, read from the compiler context and checked against the node info.
#define COMPILER_GRAPH 0x1438
#define GRAPH_NODES 0x08
#define GRAPH_SLOTS 0x28
#define NODE_RECORD 20          // u8 type, u32 id at +4, u32 settings slot at +0x10 (1-based, 0 = none)
#define LINK_RECORD 12          // u32 from node id, u32 to node id
#define INFO_LINKS 0x00
#define INFO_NODES 0x10
#define TYPE_LOGIC_ENTITY 0
#define MAX_GRAPH_NODES 256

static uint32_t strand_of(void *ctx, uint8_t *info, uint32_t node_id)
{
    uint8_t *nodes = *(uint8_t **)(info + INFO_NODES), *links = *(uint8_t **)(info + INFO_LINKS);
    uint32_t node_count = *(uint32_t *)(info + INFO_NODES + 8), link_count = *(uint32_t *)(info + INFO_LINKS + 8);
    if (!nodes || node_count > MAX_GRAPH_NODES) return 0;
    uint8_t *slots = NULL;
    __try {
        uint8_t *graph = *(uint8_t **)((uint8_t *)ctx + COMPILER_GRAPH);
        if (graph && *(uint8_t **)(graph + GRAPH_NODES) == nodes
            && *(uint32_t *)(graph + GRAPH_NODES + 8) == node_count) {
            slots = *(uint8_t **)(graph + GRAPH_SLOTS);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        slots = NULL;
    }
    if (!slots) return 0;
    // Walk the links both ways from the Message node.
    uint32_t ids[MAX_GRAPH_NODES];
    int count = 0;
    ids[count++] = node_id;
    for (int done = 0; done < count; done++) {
        for (uint32_t l = 0; l < link_count; l++) {
            uint32_t from = *(uint32_t *)(links + l * LINK_RECORD), to = *(uint32_t *)(links + l * LINK_RECORD + 4);
            uint32_t other = from == ids[done] ? to : to == ids[done] ? from : 0;
            if (!other) continue;
            int known = 0;
            for (int i = 0; i < count && !known; i++) known = ids[i] == other;
            if (!known && count < MAX_GRAPH_NODES) ids[count++] = other;
        }
    }
    Strand strand = {0};
    for (uint32_t n = 0; n < node_count; n++) {
        uint8_t *node = nodes + n * NODE_RECORD;
        uint32_t slot = *(uint32_t *)(node + 0x10);
        if (node[0] != TYPE_LOGIC_ENTITY || !slot || strand.count >= STRAND_OBJECTS) continue;
        for (int i = 0; i < count; i++) {
            if (ids[i] != *(uint32_t *)(node + 4)) continue;
            uint32_t logic_id = *(uint32_t *)(slots + (slot - 1) * 16);
            if (logic_id) strand.ids[strand.count++] = logic_id;
            break;
        }
    }
    if (!strand.count) return 0;
    AcquireSRWLockExclusive(&strand_lock);
    uint32_t index = 0;
    for (LONG i = 1; i < strand_count && !index; i++) {
        if (strands[i].count == strand.count && memcmp(strands[i].ids, strand.ids, strand.count * 4) == 0) {
            index = (uint32_t)i;
        }
    }
    if (!index && strand_count < MAX_STRANDS) {
        index = (uint32_t)strand_count;
        strands[index] = strand;
        strand_count++;
    } else if (!index) {
        static volatile LONG warned;
        if (!InterlockedExchange(&warned, 1)) {
            log_line("%d Message object groups in use, newer Message nodes go to every player", MAX_STRANDS);
        }
    }
    ReleaseSRWLockExclusive(&strand_lock);
    return index;
}

// OP_MESSAGE: remembers the input per node and queues the node's message when the input turns on.
static void message_input(const void *instruction, uint8_t input, uint32_t arg)
{
    uint32_t choice = arg % MESSAGE_CHOICES;
    int fresh;
    AcquireSRWLockExclusive(&message_lock);
    uint32_t at = node_slot(message_slots, sizeof(message_slots[0]), MESSAGE_SLOTS, instruction, GetTickCount64(),
        &fresh);
    if (fresh) message_slots[at].input = input;  // new node (or recompiled): an input already on does not count
    int rising = input && !message_slots[at].input;
    message_slots[at].input = input;
    int dropped = 0;
    if (rising) {
        ULONGLONG now = GetTickCount64();
        if (pending_message_count == MAX_PENDING_MESSAGES) {  // full: old ones go first
            int kept = 0;
            for (int i = 0; i < pending_message_count; i++) {
                if (now - pending_times[i] >= MESSAGE_MAX_AGE_MS) continue;
                pending_messages[kept] = pending_messages[i];
                pending_times[kept++] = pending_times[i];
            }
            pending_message_count = kept;
        }
        if (pending_message_count < MAX_PENDING_MESSAGES) {
            pending_times[pending_message_count] = now;
            pending_messages[pending_message_count++] = (uint16_t)arg;
        } else {
            dropped = 1;
        }
    }
    ReleaseSRWLockExclusive(&message_lock);
    static volatile LONG warned;
    if (dropped && !InterlockedExchange(&warned, 1)) log_line("message: %d messages waiting, newer ones are dropped",
        MAX_PENDING_MESSAGES);
    if (rising) debug_line("message: queued %s (strand %u)", message_texts[message_option(choice)].text, arg >> 3);
}

typedef void **(*ComponentFn)(void **out, void *world, void *component, uint32_t entity);
static ComponentFn component_of;  // the puzzle system's position lookup (CurrentTransform view at iterator +0x50)
#define PUZZLE_ITER_TRANSFORM 0x50
#define MESSAGE_RANGE 200.0  // metres, covers the largest building area (160 x 160 m)
#define POSITION_SCALE (1.0 / 4294967296.0)  // world positions are 32.32 fixed point

// Position of an entity: through the puzzle view's CurrentTransform lookup, or (transform NULL) the component lookup.
static int position_of(void *world, void *transform, uint32_t entity, double *where)
{
    if (!entity) return 0;
    void *out[2] = {0, 0};
    __try {
        if (transform) {
            if (!world || !component_of) return 0;
            component_of(out, world, transform, entity);
        } else {
            if (!hash_lookup || !chest_storage) return 0;
            out[0] = lookup(chest_storage - storage_offset, TYPE_CURRENT_TRANSFORM, entity);
        }
        if (!out[0]) return 0;
        const int64_t *p = (const int64_t *)out[0];
        for (int i = 0; i < 3; i++) where[i] = (double)p[i] * POSITION_SCALE;
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Players within range of a strand object; all players if no object is located. *located = objects found.
static int message_recipients(uint32_t strand_index, void *world, void *transform, const uint32_t *players,
    int player_count, uint32_t *out, int *located)
{
    *located = 0;
    Strand strand = {0};
    if (strand_index && strand_index < MAX_STRANDS) {
        AcquireSRWLockShared(&strand_lock);
        strand = strands[strand_index];
        ReleaseSRWLockShared(&strand_lock);
    }
    double spots[STRAND_OBJECTS][3];
    int spot_count = 0;
    for (int i = 0; i < strand.count; i++) {
        if (position_of(world, transform, entity_of(strand.ids[i]), spots[spot_count])) spot_count++;
    }
    *located = spot_count;
    if (!spot_count) {
        memcpy(out, players, (size_t)player_count * 4);
        return player_count;
    }
    int count = 0;
    for (int p = 0; p < player_count; p++) {
        double at[3];
        if (!position_of(world, transform, players[p], at)) continue;
        for (int s2 = 0; s2 < spot_count; s2++) {
            double dx = at[0] - spots[s2][0], dy = at[1] - spots[s2][1], dz = at[2] - spots[s2][2];
            if (dx * dx + dy * dy + dz * dz <= MESSAGE_RANGE * MESSAGE_RANGE) {
                out[count++] = players[p];
                break;
            }
        }
    }
    return count;
}

static int player_ids(const uint8_t *players, uint32_t *ids)
{
    int count = 0;
    for (int i = 0; players && i < PLAYER_SLOTS; i++) {
        uint32_t id = *(const uint32_t *)(players + i * PLAYER_STRIDE);
        if (id) ids[count++] = id;
    }
    return count;
}

// Sends the waiting messages as PuzzleCompletedEvents through writer (a system context object).
static void send_core(const uint8_t *players, void *writer, void *world, void *transform)
{
    uint16_t messages[MAX_PENDING_MESSAGES];
    ULONGLONG now = GetTickCount64();
    int count = 0, expired = 0;
    AcquireSRWLockExclusive(&message_lock);
    for (int i = 0; i < pending_message_count; i++) {
        if (now - pending_times[i] < MESSAGE_MAX_AGE_MS) messages[count++] = pending_messages[i];
        else expired++;
    }
    pending_message_count = 0;
    ReleaseSRWLockExclusive(&message_lock);
    if (expired) debug_line("message: %d message(s) older than %d s dropped", expired, MESSAGE_MAX_AGE_MS / 1000);
    if (!count) return;
    if (!players || !writer) {
        log_line("message: no player list (%p) or event writer (%p), %d message(s) dropped", (const void *)players,
            writer, count);
        return;
    }
    uint32_t ids[PLAYER_SLOTS];
    int id_count = player_ids(players, ids);
    for (int m = 0; m < count; m++) {
        uint32_t choice = messages[m] % MESSAGE_CHOICES, strand = messages[m] >> 3, to[PLAYER_SLOTS];
        const char *name = message_texts[message_option(choice)].text;
        int located;
        int to_count = message_recipients(strand, world, transform, ids, id_count, to, &located);
        if (!to_count) {
            debug_line("message: %s - no player near its %d object(s)", name, located);
            continue;
        }
        uint16_t index = event_index(writer);
        uint8_t *event = event_new(event_queue(writer), PUZZLE_EVENT_TYPE, PUZZLE_EVENT_SIZE, index);
        if (!event) {
            log_line("message: event could not be created");
            continue;
        }
        memset(event + PUZZLE_EVENT_PLAYERS, 0, PLAYER_SLOTS * 4);
        memcpy(event + PUZZLE_EVENT_PLAYERS, to, (size_t)to_count * 4);
        event[PUZZLE_EVENT_KIND] = (uint8_t)(choice / 2);
        event[PUZZLE_EVENT_UNLOCKED] = (uint8_t)(choice % 2);
        debug_line("message: sent %s to %d of %d player(s) (%s)", name, to_count, id_count,
            located ? "near its objects" : "no objects located, everyone");
    }
}

static void send_messages(void *context)
{
    if (!*(volatile int *)&pending_message_count) return;  // nothing waiting: skip the view (runs every frame)
    __declspec(align(16)) uint8_t iterator[PUZZLE_ITER_SIZE + 0x18] = {0};
    view_init(context, iterator, PUZZLE_ITER_SIZE);
    send_core(*(uint8_t **)(iterator + PUZZLE_ITER_PLAYERS), *(void **)(iterator + PUZZLE_ITER_EVENTS),
        *(void **)(*(uint8_t **)context + WORLD_FROM_CONTEXT), *(void **)(iterator + PUZZLE_ITER_TRANSFORM));
}

// The game runs a system once per chunk of entities its view matches, so puzzle_completed_event runs only while a
// PuzzleCompletionNotifier is loaded. Otherwise interaction_offer sends the messages: an event is just appended to
// the running system's queue (its context object is the writer), and the player list is the first resource of the
// puzzle system's persistent registration object. Both take the waiting messages at once; the lock only keeps
// interaction_offer's parallel jobs from searching the player list together.
#define PUZZLE_IDLE_MS 1000
#define PLAYER_SEARCH_MS 5000    // a missing or outdated player list is searched for again this often
#define CONTEXT_RESOURCES 0xc0   // context object: the system's resource table (inside the system object)
#define CONTEXT_SYSTEM 0xc8      // context object: system object + SYSTEM_BACK
#define SYSTEM_BACK 0x168c
#define SYSTEM_FUNCTION 0x28     // system object: the system function
#define RESOURCE_COUNT 0x400     // resource table: u8 count after the pointers
static SRWLOCK puzzle_lock = SRWLOCK_INIT;
static volatile ULONGLONG puzzle_ran;
static const uint8_t *puzzle_system_function, *offer_function;  // set by install
static const uint8_t *fallback_players;  // BasicPlayerInfos

// Wrapper of the puzzle_completed_event system (server).
static uint64_t puzzle_system(void *context, void *a, void *b, void *c)
{
    static volatile LONG announced;
    if (!InterlockedExchange(&announced, 1)) debug_line("message: puzzle system running");
    puzzle_ran = GetTickCount64();
    send_messages(context);  // takes the waiting messages at once, so the fallback cannot send them twice
    return original_puzzle_system(context, a, b, c);
}

static int readable(const void *at, size_t size)
{
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(at, &info, sizeof(info)) || info.State != MEM_COMMIT || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        || !(info.Protect & (PAGE_READONLY | PAGE_READWRITE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE))) return 0;
    return (const uint8_t *)at + size <= (const uint8_t *)info.BaseAddress + info.RegionSize;
}

// A player list: readable, and every id in it has a position (the host is always in it).
static int plausible_players(const uint8_t *players)
{
    if (!readable(players, PLAYER_SLOTS * PLAYER_STRIDE)) return 0;
    uint32_t ids[PLAYER_SLOTS];
    int count = player_ids(players, ids);
    for (int i = 0; i < count; i++) {
        double at[3];
        if (!position_of(NULL, NULL, ids[i], at)) return 0;
    }
    return count > 0;
}

// The puzzle system's player list, found through the offer system object (context object + CONTEXT_SYSTEM): the
// puzzle system object is the one in the same allocation whose function is puzzle_completed_event. There are two
// (server and client world, same list); the one nearest to the offer system object is taken.
static const uint8_t *find_players(const uint8_t *object)
{
    if (!readable(object + CONTEXT_RESOURCES, 16)) return NULL;
    const uint8_t *offer = *(uint8_t *const *)(object + CONTEXT_SYSTEM) - SYSTEM_BACK;
    size_t table = (size_t)(*(uint8_t *const *)(object + CONTEXT_RESOURCES) - offer);
    if (!readable(offer + SYSTEM_FUNCTION, 8) || *(uint8_t *const *)(offer + SYSTEM_FUNCTION) != offer_function
        || table > 0x4000) {
        log_line("message: interaction system object not as expected (%p, table +%zx)", (const void *)offer, table);
        return NULL;
    }
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(offer, &info, sizeof(info))) return NULL;
    void *allocation = info.AllocationBase;
    const uint8_t *found = NULL;
    int candidates = 0, valid = 0;
    uintptr_t nearest = UINTPTR_MAX;
    for (const uint8_t *region = allocation; VirtualQuery(region, &info, sizeof(info))
         && info.AllocationBase == allocation; region = (const uint8_t *)info.BaseAddress + info.RegionSize) {
        if (!readable(info.BaseAddress, info.RegionSize)) continue;
        const uint8_t *start = info.BaseAddress, *end = start + info.RegionSize;
        for (const uint8_t *p = start + SYSTEM_FUNCTION; p + 8 <= end; p += 8) {
            if (*(uint8_t *const *)p != puzzle_system_function) continue;
            candidates++;
            const uint8_t *system = p - SYSTEM_FUNCTION;
            if (!readable(system + table, RESOURCE_COUNT + 1) || !system[table + RESOURCE_COUNT]) continue;
            const uint8_t *players = *(uint8_t *const *)(system + table);
            if (!plausible_players(players)) continue;
            valid++;
            uintptr_t distance = system > offer ? (uintptr_t)(system - offer) : (uintptr_t)(offer - system);
            if (distance < nearest) {
                nearest = distance;
                found = players;
            }
        }
    }
    static int missing_logged;
    if (found) {
        debug_line("message: player list found (%d candidate(s), %d valid)", candidates, valid);
        missing_logged = 0;
    } else if (!missing_logged) {
        log_line("message: player list not found (%d candidate(s), %d valid), messages wait for the puzzle system",
            candidates, valid);
        missing_logged = 1;
    }
    return found;
}

// interaction_offer job: sends waiting messages while the puzzle system is idle.
static void message_fallback(void *context)
{
    if (!*(volatile int *)&pending_message_count || !puzzle_system_function || !offer_function) return;
    if (GetTickCount64() - puzzle_ran < PUZZLE_IDLE_MS) return;
    if (!TryAcquireSRWLockExclusive(&puzzle_lock)) return;
    const uint8_t *object = *(uint8_t **)context;
    // The list belongs to the loaded world: after a world change (or a too early first try) it is searched again.
    static ULONGLONG searched;
    int usable = fallback_players && plausible_players(fallback_players);
    ULONGLONG now = GetTickCount64();
    if (!usable && now - searched >= PLAYER_SEARCH_MS) {
        searched = now;
        fallback_players = find_players(object);
        usable = fallback_players != NULL;
    }
    if (usable) {
        static volatile LONG announced;
        if (!InterlockedExchange(&announced, 1)) debug_line("message: sending through the interaction system");
        send_core(fallback_players, (void *)object, NULL, NULL);
    }
    ReleaseSRWLockExclusive(&puzzle_lock);
}

// ---- Counter node runtime ----
// State per node id (kept across recompiles): count and last input. A reset input that is on holds the count at 0.
// OP_COUNTER's arg = N | slot << 6.
#define COUNTER_SLOTS 1024
static struct { uint32_t node; uint16_t count; uint8_t input, fresh; } counters[COUNTER_SLOTS];
static LONG counter_next;
static SRWLOCK counter_lock = SRWLOCK_INIT;

// Counts go back to 0; slots keep their node, so compiled graphs stay valid. Inputs already on do not count.
static void reset_counters(void)
{
    AcquireSRWLockExclusive(&counter_lock);
    for (int i = 0; i < COUNTER_SLOTS; i++) {
        counters[i].count = 0;
        counters[i].fresh = 1;
    }
    ReleaseSRWLockExclusive(&counter_lock);
}

static uint32_t counter_slot(uint32_t node)
{
    AcquireSRWLockExclusive(&counter_lock);
    uint32_t slot = COUNTER_SLOTS;
    for (uint32_t i = 0; i < COUNTER_SLOTS && slot == COUNTER_SLOTS; i++) {
        if (counters[i].node == node) slot = i;
    }
    if (slot == COUNTER_SLOTS) {
        if (counter_next == COUNTER_SLOTS) log_line("%d counters compiled, older ones share their count", COUNTER_SLOTS);
        slot = (uint32_t)(counter_next++ % COUNTER_SLOTS);
        counters[slot].node = node;
        counters[slot].count = 0;
        counters[slot].fresh = 1;
    }
    ReleaseSRWLockExclusive(&counter_lock);
    return slot;
}

static uint8_t counter_output(uint32_t slot, uint8_t input, uint8_t reset, uint32_t target)
{
    if (slot >= COUNTER_SLOTS) return 0;
    AcquireSRWLockExclusive(&counter_lock);
    if (counters[slot].fresh) {  // an input already on when the node appears does not count
        counters[slot].fresh = 0;
        counters[slot].input = input;
    }
    if (reset) counters[slot].count = 0;
    else if (input && !counters[slot].input && counters[slot].count < 0xffff && !areas_settling) counters[slot].count++;
    counters[slot].input = input;
    uint8_t on = counters[slot].count >= target;
    ReleaseSRWLockExclusive(&counter_lock);
    return on;
}

// Current count of a counter node, -1 if this game has none (e.g. a guest).
static int counter_count(uint32_t node)
{
    int count = -1;
    AcquireSRWLockShared(&counter_lock);
    for (uint32_t i = 0; i < COUNTER_SLOTS && count < 0; i++) {
        if (counters[i].node == node && !counters[i].fresh) count = counters[i].count;
    }
    ReleaseSRWLockShared(&counter_lock);
    return count;
}

static uint8_t time_condition(uint32_t arg);

// The mod's VM ops on the top of the stack.
static void node_op(uint32_t instruction, uint8_t *top, const void *at)
{
    uint32_t op = instruction & 0xffff, arg = instruction >> 16;
    if (op == OP_TIME) *top = (uint8_t)(*top && time_condition(arg));
    else if (op == OP_COUNTER) top[-1] = counter_output(arg >> 6, top[-1], *top, arg & 0x3f);
    else if (op == OP_MESSAGE) message_input(at, *top, arg);
    else if (op == OP_DELAY) *top = delay_output(at, *top, arg, GetTickCount64());
    else *top = random_output(at, *top, arg);
}

static int quarter_of(uint64_t ns)
{
    return (int)((ns / QUARTER) % 96);
}

// OP_TIME: night, day or a span of quarter hours (may pass midnight).
static uint8_t time_condition(uint32_t arg)
{
    int mode = arg & 3;
    if (mode == TIME_MODE_NIGHT) return node_night;
    if (mode == TIME_MODE_DAY) return !node_night;
    uint8_t *time = server_time;
    if (!time) return 0;
    int from = (arg >> 2) & 0x7f, to = (arg >> 9) & 0x7f, now;
    __try {  // right after a world change the object may still be the old world's
        now = quarter_of((uint64_t)(*(int64_t *)(time + TIME_OF_DAY) % DAY));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
    return from <= to ? (now >= from && now < to) : (now >= from || now < to);
}

// Time settings: mode, from, to (half hours 0..47).
enum { SETTING_MODE, SETTING_FROM, SETTING_TO };
#define HALF_HOURS 48

static void read_time_settings(const uint8_t *settings, int *mode, int *from, int *to)
{
    *mode = settings[SETTING_MODE] <= TIME_MODE_SPAN ? settings[SETTING_MODE] : TIME_MODE_NIGHT;
    *from = settings[SETTING_FROM] < HALF_HOURS ? settings[SETTING_FROM] : 0;
    *to = settings[SETTING_TO] < HALF_HOURS ? settings[SETTING_TO] : 0;
}

static uint32_t time_arg(const uint8_t *settings)
{
    int mode, from, to;
    read_time_settings(settings, &mode, &from, &to);
    if (mode != TIME_MODE_SPAN) return (uint32_t)mode;
    return TIME_MODE_SPAN | (uint32_t)(from * 2) << 2 | (uint32_t)(to * 2) << 9;
}

// Counter settings: N (1..COUNTER_MAX, 0 = default 1).
#define COUNTER_MAX 50

static uint32_t counter_target(const uint8_t *settings)
{
    return settings[0] >= 1 && settings[0] <= COUNTER_MAX ? settings[0] : 1;
}

// Random settings: mode, chance in percent (0 = default). Random Timer: mode, -, from and to step, signal
// (0 = like the input, else duration step + 1).
enum { RANDOM_SETTING_MODE, RANDOM_SETTING_CHANCE };
enum { TIMER_SETTING_MODE = 0, TIMER_SETTING_FROM = 4, TIMER_SETTING_TO = 8, TIMER_SETTING_SIGNAL = 12 };

static void read_random_settings(const uint8_t *settings, int *mode, int *chance)
{
    *mode = settings[RANDOM_SETTING_MODE] == RANDOM_HOLD ? RANDOM_HOLD : RANDOM_GATE;
    int value = settings[RANDOM_SETTING_CHANCE];
    *chance = value >= 1 && value <= 100 ? value : RANDOM_DEFAULT_CHANCE;
}

static int timer_mode(const uint8_t *settings)
{
    return settings[TIMER_SETTING_MODE] == RANDOM_PULSE ? RANDOM_PULSE : RANDOM_DELAY;
}

static uint32_t clamp_ms(uint32_t value, uint32_t max)
{
    return value < max ? value : max;
}

// Random Timer times in ms; a fresh node (mode 0) has the defaults.
static void read_timer_settings(const uint8_t *settings, uint32_t *from, uint32_t *to, uint32_t *signal)
{
    if (settings[TIMER_SETTING_MODE] < RANDOM_DELAY) {
        *from = TIMER_DEFAULT_FROM;
        *to = TIMER_DEFAULT_TO;
        *signal = 0;
        return;
    }
    *from = clamp_ms(*(const uint32_t *)(settings + TIMER_SETTING_FROM), TIMER_MAX_MS);
    *to = clamp_ms(*(const uint32_t *)(settings + TIMER_SETTING_TO), TIMER_MAX_MS);
    *signal = clamp_ms(*(const uint32_t *)(settings + TIMER_SETTING_SIGNAL) & 0xffffff, TIMER_MAX_SIGNAL);  // not the mark
}

static void write_timer_settings(uint8_t *settings, int mode, uint32_t from, uint32_t to, uint32_t signal)
{
    settings[TIMER_SETTING_MODE] = (uint8_t)mode;
    *(uint32_t *)(settings + TIMER_SETTING_FROM) = clamp_ms(from, TIMER_MAX_MS);
    *(uint32_t *)(settings + TIMER_SETTING_TO) = clamp_ms(to, TIMER_MAX_MS);
    *(uint32_t *)(settings + TIMER_SETTING_SIGNAL) = clamp_ms(signal, TIMER_MAX_SIGNAL);
}

// Message settings: choice = puzzle type * 2 + unlocked (door 0/1, alarm raised 6, resolved 7; others = door).
static uint32_t message_choice(const uint8_t *settings)
{
    uint8_t choice = settings[0];
    return choice == 0 || choice == 6 || choice == 7 ? choice : 1;
}

static uint32_t random_arg(const uint8_t *settings)
{
    int mode, chance;
    read_random_settings(settings, &mode, &chance);
    return (uint32_t)chance | (uint32_t)mode << 7;
}

static void describe_arg(uint32_t arg, char *text, size_t size)
{
    int mode = arg & 3;
    if (mode != TIME_MODE_SPAN) {
        strcpy_s(text, size, mode == TIME_MODE_NIGHT ? "night" : "day");
        return;
    }
    int from = (arg >> 2) & 0x7f, to = (arg >> 9) & 0x7f;
    sprintf_s(text, size, "%02d:%02d-%02d:%02d", from / 4, from % 4 * 15, to / 4, to % 4 * 15);
}

// Compiles a mod node: its input (or true), then its op. Returns the compiler's error code, 0 = ok.
// Counter: count input (port 0), reset input (port 1, false if not connected), op, pop the reset value.
#define LINK_PORT 9  // link record: input port of the target node
static uint8_t compile_counter(void *ctx, uint8_t *info, uint32_t node_id)
{
    uint64_t inputs = *(uint64_t *)(info + NODE_INPUT_COUNT);
    if (inputs > 2) return COMPILE_TOO_MANY_INPUTS;
    uint32_t *input_ids = *(uint32_t **)(info + NODE_INPUTS);
    uint8_t *links = *(uint8_t **)(info + INFO_LINKS);
    for (uint8_t port = 0; port < 2; port++) {
        int found = 0;
        for (uint64_t i = 0; i < inputs && !found; i++) {
            if (links[input_ids[i] * LINK_RECORD + LINK_PORT] != port) continue;
            uint8_t error = load_input(ctx, input_ids[i], info);
            if (error) return error;
            found = 1;
        }
        if (!found && !emit(ctx, OP_FALSE)) return COMPILE_EMIT_FAILED;
    }
    uint32_t target = counter_target(info + NODE_SETTINGS);
    uint32_t arg = target | counter_slot(node_id) << 6;
    if (!emit(ctx, OP_COUNTER | arg << 16) || !emit(ctx, OP_POP | 1u << 16)) return COMPILE_EMIT_FAILED;
    static volatile LONG last_logged = -1;
    if (InterlockedExchange(&last_logged, (LONG)target) != (LONG)target) {
        debug_line("node: compiled \"Counter\" (%u input%s) -> %u", (unsigned)inputs, inputs == 1 ? "" : "s", target);
    }
    return 0;
}

static uint8_t compile_node(void *ctx, uint8_t *info, uint32_t type, uint8_t *node)
{
    if (type == NODE_COUNTER) return compile_counter(ctx, info, *(uint32_t *)(node + 4));
    uint32_t node_id = *(uint32_t *)(node + 4);
    uint64_t inputs = type == NODE_TIME ? 0 : *(uint64_t *)(info + NODE_INPUT_COUNT);  // Time is a source
    if (inputs > 1) return COMPILE_TOO_MANY_INPUTS;
    if (inputs == 1) {
        uint8_t error = load_input(ctx, **(uint32_t **)(info + NODE_INPUTS), info);
        if (error) return error;
    } else if (!emit(ctx, OP_TRUE)) {
        return COMPILE_EMIT_FAILED;
    }
    uint32_t op, arg;
    if (type == NODE_RANDOM_TIMER) {
        op = OP_DELAY;
        TimerConfig config = {0};
        read_timer_settings(info + NODE_SETTINGS, &config.from, &config.to, &config.signal);
        config.pulse = timer_mode(info + NODE_SETTINGS) == RANDOM_PULSE;
        arg = timer_config_index(config);
    } else if (type == NODE_RANDOM) {
        op = OP_RANDOM;
        arg = random_arg(info + NODE_SETTINGS);
    } else if (type == NODE_MESSAGE) {
        op = OP_MESSAGE;
        arg = message_choice(info + NODE_SETTINGS) | strand_of(ctx, info, node_id) << 3;
    } else {
        op = OP_TIME;
        arg = time_arg(info + NODE_SETTINGS);
    }
    if (!emit(ctx, op | arg << 16)) return COMPILE_EMIT_FAILED;
    // Log only changes; graphs recompile often.
    static volatile LONG last_logged[NODE_COUNT] = {-1, -1, -1, -1, -1};
    if (InterlockedExchange(&last_logged[type - NODE_FIRST], (LONG)arg) != (LONG)arg) {
        char what[96];
        if (op == OP_RANDOM) {
            sprintf_s(what, sizeof(what), "%u%% %s", arg & 0x7f, (arg >> 7) & 1 ? "hold" : "gate");
        } else if (op == OP_DELAY) {
            TimerConfig config = timer_configs[arg];
            sprintf_s(what, sizeof(what), "%s %u-%u ms, signal %u ms", config.pulse ? "clock" : "delay", config.from,
                config.to, config.signal);
        } else if (op == OP_MESSAGE) {
            uint32_t strand = arg >> 3;
            sprintf_s(what, sizeof(what), "%s, strand %u (%u objects)",
                message_texts[message_option(arg % MESSAGE_CHOICES)].text, strand, strand ? strands[strand].count : 0);
        } else {
            describe_arg(arg, what, sizeof(what));
        }
        debug_line("node: compiled \"%s\" (%u input%s) -> %s", node_texts[type - NODE_FIRST].text, (unsigned)inputs,
            inputs == 1 ? "" : "s", what);
    }
    return 0;
}

// ---- Properties panel ----
// Dropdowns like the game's register channel widget; its functions are read from that widget at these offsets.
enum {
    REG_GLOBAL = 0x19,        // mov rax, [rip+x]: UI/game global
    REG_GRAPH_VIEW = 0x5b,    // call graph_view(out, graph state)
    REG_NODE_DATA = 0x8a,     // call node_data(result, view, node): result+0 = not found, +8 = 16 setting bytes
    REG_NEW_COMMAND = 0xa3,   // call new_command(global): queued logic graph command
    REG_LIST_INIT = 0x10a,    // call list_init(list, title text)
    REG_SCALE_DEFAULT = 0x189, // movss xmm0, [rip+x]: 1.0
    REG_WIDTH_FACTOR = 0x191, // mulss xmm0, [rip+x]: 50.0
    REG_LIST_APPEND = 0x229,  // call list_append(list) -> entry
    REG_SCOPE_BEGIN = 0x255,  // call scope_begin(scope, 0); scope+8 = UI context
    REG_STATE_INIT = 0x25f,   // lea r9, [rip+x]: state initializer
    REG_STATE = 0x27e,        // call state(ui, id, size, init, 1) -> u32 selection
    REG_IS_NEW = 0x289,       // call is_new(state)
    REG_SET_WIDTH = 0x2c9,    // call set_width(ui, float)
    REG_SET_PADDING = 0x2e6,  // call set_padding(ui, float2)
    REG_DROPDOWN = 0x2f5,     // call dropdown(list, &selection, 0) -> changed
    REG_SCOPE_END = 0x34f,    // call scope_end(scope)
};
// List object offsets.
#define LIST_SIZE 0xd00
#define LIST_TITLE 0xc10
#define LIST_WIDTH 0xc40
#define LIST_ROWS 0xc68
#define LIST_FLAG_A 0xc71
#define LIST_FLAG_B 0xc73
#define LIST_ENTRY_TEXT 8
#define GAME_UI_STATE 0xd0      // global+0xd0 -> UI state (font scale)
#define FONT_SCALE_COUNT 0x33aa8
#define FONT_SCALE (0x336b0 + 0x2c8)
#define GRAPH_CONTEXT 0x38
#define GRAPH_STATE 0x466838
#define COMMAND_TYPE 0xc
#define COMMAND_NODE 0x10
#define COMMAND_ZERO 0x14
#define COMMAND_DATA_TYPE 0x18
#define COMMAND_DATA 0x20
#define COMMAND_SET_NODE_DATA 5
#define DATA_TIMED_DURATION 3   // 16 bytes, like TimedTrigger
#define DROPDOWN_ROWS 8
static const uint64_t DROPDOWN_STATE_ID = 0x5a3f7c1e9d2b4a60ull;  // + dropdown index; the register widget's differs

typedef void *(*GraphViewFn)(void *out, void *graph);
typedef void *(*NodeDataFn)(void *result, void *view, uint8_t *node);
typedef uint8_t *(*NewCommandFn)(void *global);
typedef void *(*ListInitFn)(uint8_t *list, const NodeText *title);
typedef uint8_t *(*ListAppendFn)(uint8_t *list);
typedef void *(*ScopeBeginFn)(uint8_t *scope, int id);
typedef void (*ScopeEndFn)(uint8_t *scope);
typedef uint32_t *(*StateFn)(void *ui, uint64_t id, uint32_t size, void *init, int flag);
typedef uint8_t (*IsNewFn)(uint32_t *state);
typedef void (*SetWidthFn)(void *ui, float width);
typedef void (*SetPaddingFn)(void *ui, uint64_t padding);
typedef uint8_t (*DropdownFn)(uint8_t *list, uint32_t *selection, int flags);
typedef void *(*BlockBeginFn)(uint8_t *block, const NodeText *name, float value);
typedef void (*GapFn)(uint8_t *block, uint64_t size);
typedef uint8_t (*DurationFn)(uint64_t *ns, const NodeText *label);

static struct {
    uint8_t **global;
    GraphViewFn graph_view;
    NodeDataFn node_data;
    NewCommandFn new_command;
    ListInitFn list_init;
    ListAppendFn list_append;
    ScopeBeginFn scope_begin;
    ScopeEndFn scope_end;
    void *state_init;
    StateFn state;
    IsNewFn is_new;
    SetWidthFn set_width;
    SetPaddingFn set_padding;
    DropdownFn dropdown;
    float scale_default, width_factor;
    BlockBeginFn block_begin;  // from the TimedTrigger panel case
    GapFn gap_fn;
    float gap;
    DurationFn duration;  // duration field (&ns, label) -> changed
} ui;

// Layout block name of the panel.
static NodeText panel_block_name = {"doSparkworksTimeNodeProperties", 30, 0};

// Layout calls in the TimedTrigger panel case.
enum {
    TIMED_BLOCK_BEGIN = 0xf3,  // call block_begin(block, name, 0.0f); block+8 = UI context
    TIMED_GAP_VALUE = 0x10d,   // movss xmm6, [rip+x]: 20.0
    TIMED_GAP = 0x124,         // call gap(block, float2)
    TIMED_DURATION = 0x16f,    // call duration(&delay, label)
};

static int find_panel_layout(uint8_t *timed_case)
{
    if (timed_case[TIMED_BLOCK_BEGIN] != 0xE8 || timed_case[TIMED_GAP] != 0xE8 || timed_case[TIMED_DURATION] != 0xE8
        || memcmp(timed_case + TIMED_GAP_VALUE, "\xF3\x0F\x10\x35", 4) != 0) {
        return 0;
    }
    ui.block_begin = (BlockBeginFn)call_target(timed_case + TIMED_BLOCK_BEGIN);
    ui.gap_fn = (GapFn)call_target(timed_case + TIMED_GAP);
    ui.duration = (DurationFn)call_target(timed_case + TIMED_DURATION);
    uint8_t *value = timed_case + TIMED_GAP_VALUE;
    ui.gap = *(float *)(value + 8 + *(int32_t *)(value + 4));
    panel_block_name.hash = fnv1a64(panel_block_name.text);
    return 1;
}

// Panel texts (current language).
static NodeText mode_texts[] = {NODE_TEXT("Night"), NODE_TEXT("Day"), NODE_TEXT("Time span")};
static NodeText panel_labels[] = {NODE_TEXT("Mode"), NODE_TEXT("From"), NODE_TEXT("To")};
static NodeText random_modes[] = {NODE_TEXT("Pass through"), NODE_TEXT("Hold"), NODE_TEXT("Delay"),
    NODE_TEXT("Clock")};
static NodeText random_labels[] = {NODE_TEXT("Mode"), NODE_TEXT("Chance"), NODE_TEXT("From"), NODE_TEXT("To"),
    NODE_TEXT("Signal")};
static const char *hold_word = "hold";
static const char *pulse_word = "clock";
static NodeText node_descriptions[NODE_COUNT];
static NodeText message_label = NODE_TEXT("Message");
static NodeText counter_label = NODE_TEXT("Count");
static NodeText empty_text = {"", 0, 0};

// Texts per language; the language is taken from the game's name of the Clock node.
enum { DESC_RANDOM = 0, DESC_TIME = 2, DESC_TIMER = 5, MODE_DESCRIPTIONS = 7 };
enum { LANGUAGE_ENGLISH, LANGUAGE_GERMAN, LANGUAGE_COUNT };
typedef struct {
    const char *name, *clock;  // clock = the game's Clock node name in this language (NULL = default)
    NodeText nodes[NODE_COUNT], modes[3], labels[3], random_modes[4], random_labels[5];
    const char *hold, *pulse;  // node subtitle suffixes of a holding Random / a clock Random Timer node
    NodeText descriptions[NODE_COUNT];  // shown at the top of the properties panel (nodes without modes)
    NodeText mode_descriptions[MODE_DESCRIPTIONS];  // per mode: Random 0-1, Time 2-4, Random Timer 5-6
    NodeText message_label;
    NodeText counter_label;
    NodeText messages[MESSAGE_OPTIONS];
    NodeText like_input;         // first entry of the delay's Signal dropdown
    char decimal;                // decimal separator in node subtitles
} Language;
static Language languages[LANGUAGE_COUNT] = {
    {"English", NULL, {NODE_TEXT("Random Timer"), NODE_TEXT("Counter"), NODE_TEXT("Time"), NODE_TEXT("Random"), NODE_TEXT("Message")},
        {NODE_TEXT("Night"), NODE_TEXT("Day"), NODE_TEXT("Time span")},
        {NODE_TEXT("Mode"), NODE_TEXT("From"), NODE_TEXT("To")},
        {NODE_TEXT("Pass through"), NODE_TEXT("Hold"), NODE_TEXT("Delay"), NODE_TEXT("Clock")},
        {NODE_TEXT("Mode"), NODE_TEXT("Chance"), NODE_TEXT("From"), NODE_TEXT("To"), NODE_TEXT("Signal")}, "hold", "clock",
        {NODE_TEXT(""),
            NODE_TEXT("Counts every signal that comes in at the upper input. From the chosen count on, it passes a "
                "signal on until a signal at the lower input sets it back to 0."),
            NODE_TEXT(""), NODE_TEXT(""),
            NODE_TEXT("Shows one of the game's messages in the middle of the screen every time a signal comes in. "
                "The signal is passed on unchanged.")},
        {NODE_TEXT("Pass through: rolls the dice for each signal. If it works, the signal is passed on for as long "
                "as it keeps coming in."),
            NODE_TEXT("Hold: rolls the dice for each signal. The result stays until the next signal comes in."),
            NODE_TEXT("Night: on at night (22:00 to 06:00, in-game time)."),
            NODE_TEXT("Day: on during the day (06:00 to 22:00, in-game time)."),
            NODE_TEXT("Time span: on between From and To (in-game time)."),
            NODE_TEXT("Delay: waits a random time between From and To for each signal, then passes it on."),
            NODE_TEXT("Clock: switches on and off at random intervals between From and To for as long as a signal "
                "comes in.")},
        NODE_TEXT("Message"), NODE_TEXT("Count"),
        {NODE_TEXT("Door opened"), NODE_TEXT("Alarm raised"), NODE_TEXT("Alarm resolved")},
        NODE_TEXT("Like the input"), '.'},
    {"German", "Uhr", {NODE_TEXT("Zufallstimer"), NODE_TEXT("Zähler"), NODE_TEXT("Zeit"), NODE_TEXT("Zufall"), NODE_TEXT("Meldung")},
        {NODE_TEXT("Nacht"), NODE_TEXT("Tag"), NODE_TEXT("Zeitspanne")},
        {NODE_TEXT("Modus"), NODE_TEXT("Von"), NODE_TEXT("Bis")},
        {NODE_TEXT("Durchlassen"), NODE_TEXT("Halten"), NODE_TEXT("Verzögern"), NODE_TEXT("Takt")},
        {NODE_TEXT("Modus"), NODE_TEXT("Chance"), NODE_TEXT("Von"), NODE_TEXT("Bis"), NODE_TEXT("Signal")}, "halten",
        "Takt",
        {NODE_TEXT(""),
            NODE_TEXT("Zählt jedes Signal, das am oberen Eingang ankommt. Ab der gewählten Anzahl gibt er ein Signal "
                "weiter, bis ein Signal am unteren Eingang ihn wieder auf 0 setzt."),
            NODE_TEXT(""), NODE_TEXT(""),
            NODE_TEXT("Zeigt jedes Mal, wenn ein Signal ankommt, eine Meldung des Spiels in der Bildschirmmitte. "
                "Das Signal wird unverändert weitergegeben.")},
        {NODE_TEXT("Durchlassen: Würfelt bei jedem Signal neu. Klappt es, wird das Signal weitergegeben, solange es "
                "ankommt."),
            NODE_TEXT("Halten: Würfelt bei jedem Signal neu. Das Ergebnis bleibt, bis das nächste Signal kommt."),
            NODE_TEXT("Nacht: An bei Nacht (22:00 bis 06:00 Spielzeit)."),
            NODE_TEXT("Tag: An bei Tag (06:00 bis 22:00 Spielzeit)."),
            NODE_TEXT("Zeitspanne: An zwischen Von und Bis (Spielzeit)."),
            NODE_TEXT("Verzögern: Wartet bei jedem Signal eine zufällige Zeit zwischen Von und Bis und gibt es "
                "dann weiter."),
            NODE_TEXT("Takt: Geht in zufälligen Abständen zwischen Von und Bis an und aus, solange ein Signal "
                "ankommt.")},
        NODE_TEXT("Meldung"), NODE_TEXT("Anzahl"),
        {NODE_TEXT("Tür geöffnet"), NODE_TEXT("Alarm ausgelöst"), NODE_TEXT("Alarm aufgehoben")},
        NODE_TEXT("Wie Eingang"), ','},
};
#define TYPE_CLOCK 2
#define LANGUAGE_CHECK_MS 3000
typedef NodeText *(*NameLookupFn)(NodeText *out, uint8_t type);
static NameLookupFn name_lookup;  // the game's node name lookup (patched for the new types)
static int language = -1;
static ULONGLONG language_checked;

static void hash_texts(NodeText *texts, int count)
{
    for (int i = 0; i < count; i++) texts[i].hash = fnv1a64(texts[i].text);
}

static void init_languages(void)
{
    for (int l = 0; l < LANGUAGE_COUNT; l++) {
        Language *lang = &languages[l];
        hash_texts(lang->nodes, NODE_COUNT);
        hash_texts(lang->modes, 3);
        hash_texts(lang->labels, 3);
        hash_texts(lang->random_modes, 4);
        hash_texts(lang->random_labels, 5);
        hash_texts(&lang->like_input, 1);
        hash_texts(lang->mode_descriptions, MODE_DESCRIPTIONS);
        hash_texts(lang->descriptions, NODE_COUNT);
        hash_texts(&lang->message_label, 1);
        hash_texts(lang->messages, MESSAGE_OPTIONS);
        hash_texts(&lang->counter_label, 1);
    }
    // English until update_language finds the game's language.
    const Language *english = &languages[LANGUAGE_ENGLISH];
    memcpy(node_texts, english->nodes, sizeof(node_texts));
    memcpy(mode_texts, english->modes, sizeof(mode_texts));
    memcpy(panel_labels, english->labels, sizeof(panel_labels));
    memcpy(random_modes, english->random_modes, sizeof(random_modes));
    memcpy(random_labels, english->random_labels, sizeof(random_labels));
    memcpy(message_texts, english->messages, sizeof(message_texts));
    memcpy(node_descriptions, english->descriptions, sizeof(node_descriptions));
    message_label = english->message_label;
    counter_label = english->counter_label;
}

// Checks the game language every few seconds and switches the texts.
static void update_language(void)
{
    ULONGLONG now = GetTickCount64();
    if (!name_lookup || (language >= 0 && now - language_checked < LANGUAGE_CHECK_MS)) return;
    language_checked = now;
    NodeText clock[2] = {{0}};
    name_lookup(clock, TYPE_CLOCK);
    int found = LANGUAGE_ENGLISH;
    for (int l = 0; l < LANGUAGE_COUNT; l++) {
        const char *name = languages[l].clock;
        size_t length = name ? strlen(name) : 0;
        if (name && clock[0].text && clock[0].length == length && memcmp(clock[0].text, name, length) == 0) {
            found = l;
        }
    }
    if (found == language) return;
    language = found;
    const Language *lang = &languages[found];
    memcpy(node_texts, lang->nodes, sizeof(lang->nodes));
    memcpy(mode_texts, lang->modes, sizeof(lang->modes));
    memcpy(panel_labels, lang->labels, sizeof(lang->labels));
    memcpy(random_modes, lang->random_modes, sizeof(lang->random_modes));
    memcpy(random_labels, lang->random_labels, sizeof(lang->random_labels));
    hold_word = lang->hold;
    pulse_word = lang->pulse;
    memcpy(node_descriptions, lang->descriptions, sizeof(node_descriptions));
    message_label = lang->message_label;
    memcpy(message_texts, lang->messages, sizeof(lang->messages));
    counter_label = lang->counter_label;
    log_line("node texts: %s", lang->name);
}

// Fixed signal times of the delay's Signal dropdown (entry 0 = like the input).
static const uint32_t SIGNAL_STEPS[] = {100, 200, 300, 500, 1000, 1500, 2000, 3000, 4000, 5000, 10000, 15000, 20000,
    30000, 45000, 60000, 120000};
#define SIGNAL_STEP_COUNT (int)(sizeof(SIGNAL_STEPS) / sizeof(SIGNAL_STEPS[0]))
static char signal_names[LANGUAGE_COUNT][SIGNAL_STEP_COUNT][12];
static NodeText signal_texts[LANGUAGE_COUNT][SIGNAL_STEP_COUNT + 1];
static void format_seconds(uint32_t ms, char decimal, char *text, size_t size);

static void init_signal_texts(void)
{
    for (int l = 0; l < LANGUAGE_COUNT; l++) {
        signal_texts[l][0] = languages[l].like_input;
        for (int i = 0; i < SIGNAL_STEP_COUNT; i++) {
            char number[8], *name = signal_names[l][i];
            if (SIGNAL_STEPS[i] >= 120000) {
                sprintf_s(name, sizeof(signal_names[l][i]), "%u min", SIGNAL_STEPS[i] / 60000);
            } else {
                format_seconds(SIGNAL_STEPS[i], languages[l].decimal, number, sizeof(number));
                sprintf_s(name, sizeof(signal_names[l][i]), "%s s", number);
            }
            signal_texts[l][i + 1] = (NodeText){name, strlen(name), fnv1a64(name)};
        }
    }
}

// Dropdown entry of a signal time: 0 = like the input, else the nearest step.
static uint32_t signal_entry(uint32_t ms)
{
    if (!ms) return 0;
    uint32_t best = 0;
    for (int i = 1; i < SIGNAL_STEP_COUNT; i++) {
        uint32_t d = SIGNAL_STEPS[i] > ms ? SIGNAL_STEPS[i] - ms : ms - SIGNAL_STEPS[i];
        uint32_t b = SIGNAL_STEPS[best] > ms ? SIGNAL_STEPS[best] - ms : ms - SIGNAL_STEPS[best];
        if (d < b) best = (uint32_t)i;
    }
    return best + 1;
}

#define CHANCE_STEPS 20  // 5 % .. 100 %
static char chance_names[CHANCE_STEPS][6];
static NodeText chance_texts[CHANCE_STEPS];
static char count_names[COUNTER_MAX][4];
static NodeText count_texts[COUNTER_MAX];
static char half_hour_names[HALF_HOURS][6];
static NodeText half_hour_texts[HALF_HOURS];

static void init_panel_texts(void)
{
    empty_text.hash = fnv1a64("");
    for (int i = 0; i < HALF_HOURS; i++) {
        sprintf_s(half_hour_names[i], sizeof(half_hour_names[i]), "%02d:%02d", i / 2, i % 2 * 30);
        half_hour_texts[i] = (NodeText){half_hour_names[i], 5, fnv1a64(half_hour_names[i])};
    }
    for (int i = 0; i < COUNTER_MAX; i++) {
        sprintf_s(count_names[i], sizeof(count_names[i]), "%d", i + 1);
        count_texts[i] = (NodeText){count_names[i], strlen(count_names[i]), fnv1a64(count_names[i])};
    }
    for (int i = 0; i < CHANCE_STEPS; i++) {
        sprintf_s(chance_names[i], sizeof(chance_names[i]), "%d%%", (i + 1) * 5);
        chance_texts[i] = (NodeText){chance_names[i], strlen(chance_names[i]), fnv1a64(chance_names[i])};
    }
    init_signal_texts();
}

static float font_scale(void)
{
    uint8_t *state = *(uint8_t **)(*ui.global + GAME_UI_STATE);
    return *(uint64_t *)(state + FONT_SCALE_COUNT) ? *(float *)(state + FONT_SCALE) : ui.scale_default;
}

// One dropdown. Returns 1 when the player picked an entry.
static int draw_dropdown(int index, const NodeText *label, const NodeText *texts, int count, uint32_t current,
    int refresh, uint32_t *selection)
{
    static uint8_t list[LIST_SIZE];  // UI runs on one thread; too big for comfortable stack use
    memset(list, 0, sizeof(list));
    ui.list_init(list, &empty_text);
    list[LIST_FLAG_A] = 1;
    memcpy(list + LIST_TITLE, label, sizeof(NodeText));
    list[LIST_FLAG_B] = 0;
    *(float *)(list + LIST_WIDTH) = font_scale() * ui.width_factor;
    list[LIST_ROWS] = DROPDOWN_ROWS;
    for (int i = 0; i < count; i++) {
        uint8_t *entry = ui.list_append(list);
        *(uint32_t *)entry = (uint32_t)i;
        memcpy(entry + LIST_ENTRY_TEXT, &texts[i], sizeof(NodeText));
    }
    uint8_t scope[0x100] = {0};
    ui.scope_begin(scope, 0);
    void *context = *(void **)(scope + 8);
    uint32_t *state = ui.state(context, DROPDOWN_STATE_ID + (uint64_t)index, 4, ui.state_init, 1);
    if (ui.is_new(state) || refresh) *state = current;
    ui.set_width(context, ui.width_factor);
    float padding[2] = {ui.scale_default, 0.0f};
    uint64_t packed;
    memcpy(&packed, padding, sizeof(packed));
    ui.set_padding(context, packed);
    int changed = ui.dropdown(list, state, 0) != 0;
    *selection = *state < (uint32_t)count ? *state : current;
    ui.scope_end(scope);
    return changed;
}

static uint32_t panel_node = UINT32_MAX;
static uint8_t panel_settings[16];

// Controls of a Time node; returns 1 on a change (new settings in next).
static int time_controls(uint8_t *block, uint64_t gap, const uint8_t *settings, int refresh, uint8_t *next)
{
    int mode, from, to;
    read_time_settings(settings, &mode, &from, &to);
    next[SETTING_MODE] = (uint8_t)mode;
    next[SETTING_FROM] = (uint8_t)from;
    next[SETTING_TO] = (uint8_t)to;
    uint32_t picked;
    int changed = 0;
    if (draw_dropdown(0, &panel_labels[0], mode_texts, 3, (uint32_t)mode, refresh, &picked)) {
        changed = 1;
        next[SETTING_MODE] = (uint8_t)picked;
        if (picked == TIME_MODE_SPAN && from == to) {  // fresh span: 18:00-23:00
            next[SETTING_FROM] = 36;
            next[SETTING_TO] = 46;
        }
    }
    ui.gap_fn(block, gap);
    if (next[SETTING_MODE] == TIME_MODE_SPAN) {
        if (draw_dropdown(1, &panel_labels[1], half_hour_texts, HALF_HOURS, next[SETTING_FROM], refresh, &picked)) {
            changed = 1;
            next[SETTING_FROM] = (uint8_t)picked;
        }
        ui.gap_fn(block, gap);
        if (draw_dropdown(2, &panel_labels[2], half_hour_texts, HALF_HOURS, next[SETTING_TO], refresh, &picked)) {
            changed = 1;
            next[SETTING_TO] = (uint8_t)picked;
        }
        ui.gap_fn(block, gap);
    }
    return changed;
}

// Controls of a Random node.
static int random_controls(uint8_t *block, uint64_t gap, const uint8_t *settings, int refresh, uint8_t *next)
{
    int mode, chance;
    read_random_settings(settings, &mode, &chance);
    next[RANDOM_SETTING_MODE] = (uint8_t)mode;
    next[RANDOM_SETTING_CHANCE] = (uint8_t)chance;
    uint32_t step = chance >= 5 ? (uint32_t)(chance / 5 - 1) : 0, picked;
    int changed = 0;
    if (draw_dropdown(3, &random_labels[0], random_modes, 2, (uint32_t)mode, refresh, &picked)) {
        changed = 1;
        next[RANDOM_SETTING_MODE] = (uint8_t)picked;
    }
    ui.gap_fn(block, gap);
    if (draw_dropdown(4, &random_labels[1], chance_texts, CHANCE_STEPS, step, refresh, &picked)) {
        changed = 1;
        next[RANDOM_SETTING_CHANCE] = (uint8_t)((picked + 1) * 5);
    }
    ui.gap_fn(block, gap);
    return changed;
}

// One duration field like the TimedTrigger's (minutes, seconds, milliseconds). Returns 1 on a change.
static int draw_duration(const NodeText *label, uint32_t *ms)
{
    uint64_t ns = (uint64_t)*ms * 1000000u;
    if (!ui.duration(&ns, label)) return 0;
    int64_t value = (int64_t)ns;
    *ms = value <= 0 ? 0 : (uint32_t)((value + 500000) / 1000000 < UINT32_MAX ? (value + 500000) / 1000000 : UINT32_MAX);
    return 1;
}

// Controls of a Random Timer node; its modes are random_modes[2..3].
static int timer_controls(uint8_t *block, uint64_t gap, const uint8_t *settings, int refresh, uint8_t *next)
{
    int mode = timer_mode(settings);
    uint32_t from, to, signal, picked;
    read_timer_settings(settings, &from, &to, &signal);
    int changed = 0;
    if (draw_dropdown(3, &random_labels[0], random_modes + RANDOM_DELAY, 2, (uint32_t)(mode - RANDOM_DELAY), refresh,
            &picked)) {
        changed = 1;
        mode = (int)picked + RANDOM_DELAY;
    }
    ui.gap_fn(block, gap);
    changed |= draw_duration(&random_labels[2], &from);
    ui.gap_fn(block, gap);
    changed |= draw_duration(&random_labels[3], &to);
    ui.gap_fn(block, gap);
    if (mode == RANDOM_DELAY) {
        int l = language >= 0 ? language : LANGUAGE_ENGLISH;
        if (draw_dropdown(10, &random_labels[4], signal_texts[l], SIGNAL_STEP_COUNT + 1, signal_entry(signal), refresh,
                &picked)) {
            changed = 1;
            signal = picked ? SIGNAL_STEPS[picked - 1] : 0;
        }
        ui.gap_fn(block, gap);
    }
    write_timer_settings(next, mode, from, to, signal);
    return changed;
}

// Controls of a Counter node.
static int counter_controls(uint8_t *block, uint64_t gap, const uint8_t *settings, int refresh, uint8_t *next)
{
    uint32_t target = counter_target(settings), picked;
    next[0] = (uint8_t)target;
    int changed = 0;
    if (draw_dropdown(6, &counter_label, count_texts, COUNTER_MAX, target - 1, refresh, &picked)) {
        changed = 1;
        next[0] = (uint8_t)(picked + 1);
    }
    ui.gap_fn(block, gap);
    return changed;
}

// Controls of a Message node.
static int message_controls(uint8_t *block, uint64_t gap, const uint8_t *settings, int refresh, uint8_t *next)
{
    uint32_t choice = message_choice(settings), picked;
    next[0] = (uint8_t)choice;
    int changed = 0;
    if (draw_dropdown(5, &message_label, message_texts, MESSAGE_OPTIONS, message_option(choice), refresh, &picked)) {
        changed = 1;
        next[0] = OPTION_CHOICE[picked];
    }
    ui.gap_fn(block, gap);
    return changed;
}

// Properties panel of a mod node; a change is sent as SetNodeData.
static void node_panel(uint8_t *node, uint32_t type)
{
    update_language();
    uint8_t *state = *(uint8_t **)(*ui.global + GAME_UI_STATE);
    uint8_t *game = *(uint8_t **)state;
    uint8_t *context = game ? *(uint8_t **)(game + GRAPH_CONTEXT) : NULL;
    if (!context) return;
    uint8_t view_out[0x40], view[0x40], result[0x40] = {0};
    memcpy(view, ui.graph_view(view_out, *(void **)(context + GRAPH_STATE)), sizeof(view));
    ui.node_data(result, view, node);
    uint8_t settings[16] = {0};
    if (!result[0]) memcpy(settings, result + 8, sizeof(settings));
    uint32_t id = *(uint32_t *)(node + 4);
    int refresh = id != panel_node || memcmp(settings, panel_settings, sizeof(settings)) != 0;
    panel_node = id;
    memcpy(panel_settings, settings, sizeof(settings));

    // Named block with gaps around the controls, like the time nodes' panel.
    uint8_t block[0x100] = {0};
    ui.block_begin(block, &panel_block_name, 0.0f);
    float padding[2] = {ui.scale_default, 0.0f}, gaps[2] = {ui.gap, ui.gap};
    uint64_t packed_padding, gap;
    memcpy(&packed_padding, padding, sizeof(packed_padding));
    memcpy(&gap, gaps, sizeof(gap));
    ui.set_padding(*(void **)(block + 8), packed_padding);
    ui.gap_fn(block, gap);
    uint8_t next[16] = {0};
    int changed = type == NODE_RANDOM ? random_controls(block, gap, settings, refresh, next)
        : type == NODE_RANDOM_TIMER ? timer_controls(block, gap, settings, refresh, next)
        : type == NODE_MESSAGE ? message_controls(block, gap, settings, refresh, next)
        : type == NODE_COUNTER ? counter_controls(block, gap, settings, refresh, next)
        : time_controls(block, gap, settings, refresh, next);
    ui.scope_end(block);
    if (!changed) return;
    uint8_t *command = ui.new_command(*ui.global);
    if (!command) return;
    command[COMMAND_TYPE] = COMMAND_SET_NODE_DATA;
    command[COMMAND_DATA_TYPE] = DATA_TIMED_DURATION;
    *(uint32_t *)(command + COMMAND_ZERO) = 0;
    *(uint32_t *)(command + COMMAND_NODE) = id;
    next[SETTINGS_MARK_AT] = SETTINGS_MARK;
    memcpy(command + COMMAND_DATA, next, sizeof(next));
}

static uint8_t *rip_target(uint8_t *instruction, int length)
{
    return instruction + length + *(int32_t *)(instruction + length - 4);
}

// Reads the UI functions from the register channel widget. Returns 0 if its code is not as expected.
static int find_ui_functions(uint8_t *widget)
{
    static const int CALLS[] = {REG_GRAPH_VIEW, REG_NODE_DATA, REG_NEW_COMMAND, REG_LIST_INIT, REG_LIST_APPEND,
        REG_SCOPE_BEGIN, REG_STATE, REG_IS_NEW, REG_SET_WIDTH, REG_SET_PADDING, REG_DROPDOWN, REG_SCOPE_END};
    for (size_t i = 0; i < sizeof(CALLS) / sizeof(CALLS[0]); i++) {
        if (widget[CALLS[i]] != 0xE8) return 0;
    }
    if (memcmp(widget + REG_GLOBAL, "\x48\x8B\x05", 3) != 0 || memcmp(widget + REG_STATE_INIT, "\x4C\x8D\x0D", 3) != 0
        || memcmp(widget + REG_SCALE_DEFAULT, "\xF3\x0F\x10\x05", 4) != 0
        || memcmp(widget + REG_WIDTH_FACTOR, "\xF3\x0F\x59\x05", 4) != 0) {
        return 0;
    }
    ui.global = (uint8_t **)rip_target(widget + REG_GLOBAL, 7);
    ui.graph_view = (GraphViewFn)call_target(widget + REG_GRAPH_VIEW);
    ui.node_data = (NodeDataFn)call_target(widget + REG_NODE_DATA);
    ui.new_command = (NewCommandFn)call_target(widget + REG_NEW_COMMAND);
    ui.list_init = (ListInitFn)call_target(widget + REG_LIST_INIT);
    ui.list_append = (ListAppendFn)call_target(widget + REG_LIST_APPEND);
    ui.scope_begin = (ScopeBeginFn)call_target(widget + REG_SCOPE_BEGIN);
    ui.state_init = rip_target(widget + REG_STATE_INIT, 7);
    ui.state = (StateFn)call_target(widget + REG_STATE);
    ui.is_new = (IsNewFn)call_target(widget + REG_IS_NEW);
    ui.set_width = (SetWidthFn)call_target(widget + REG_SET_WIDTH);
    ui.set_padding = (SetPaddingFn)call_target(widget + REG_SET_PADDING);
    ui.dropdown = (DropdownFn)call_target(widget + REG_DROPDOWN);
    ui.scope_end = (ScopeEndFn)call_target(widget + REG_SCOPE_END);
    ui.scale_default = *(float *)rip_target(widget + REG_SCALE_DEFAULT, 8);
    ui.width_factor = *(float *)rip_target(widget + REG_WIDTH_FACTOR, 8);
    init_panel_texts();
    return 1;
}

// Properties switch: mod types call node_panel(node, type).
static uint8_t *write_panel_stub(uint8_t *stub, uint8_t *site)
{
    uint8_t *none = site + 9 + *(int32_t *)(site + 5);
    Asm a = {stub};
    uint8_t *other, *other2 = asm_new_type_check(&a, "81 F8", &other);
    asm_hex(&a, "55 48 89 E5 48 83 E4 F0 48 83 EC 20");  // push rbp; mov rbp, rsp; and rsp, -16; sub rsp, 0x20
    asm_hex(&a, "48 89 F1 89 C2 FF 15");            // mov rcx, rsi; mov edx, eax; call [target]
    uint8_t *call_disp = a.at;
    a.at += 4;
    asm_hex(&a, "48 89 EC 5D E9");                  // mov rsp, rbp; pop rbp; jmp none
    asm_rel32(&a, none);
    asm_land(&a, other);
    asm_land(&a, other2);
    asm_hex(&a, "83 F8 0C 0F 87");                  // cmp eax, 0xc; ja none
    asm_rel32(&a, none);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 9);
    *(int32_t *)call_disp = (int32_t)(a.at - (call_disp + 4));
    asm_u64(&a, (uint64_t)(uintptr_t)node_panel);
    return a.at;
}

// Category lookup: mod types -> the mod's category.
static uint8_t *write_category_stub(uint8_t *stub, uint8_t *site)
{
    Asm a = {stub};
    uint8_t *low, *high = asm_new_type_check(&a, "80 F9", &low);
    asm_hex(&a, "B0");                              // mov al, CATEGORY_MOD; ret
    *a.at++ = CATEGORY_MOD;
    asm_hex(&a, "C3");
    asm_land(&a, low);
    asm_land(&a, high);
    asm_hex(&a, "80 F9 10 0F 87");                  // cmp cl, 0x10; ja zero
    asm_rel32(&a, site + 5 + (int8_t)site[4]);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 5);
    return a.at;
}

// Copies node_texts[eax - NODE_FIRST] to the destination given by the two store instructions.
static void asm_copy_text(Asm *a, const char *store_xmm, const char *store_hash)
{
    asm_hex(a, "2D");                               // sub eax, NODE_FIRST
    *(uint32_t *)a->at = NODE_FIRST;
    a->at += 4;
    asm_hex(a, "6B C0 18 48 B9");                   // imul eax, eax, 24; mov rcx, node_texts
    asm_u64(a, (uint64_t)(uintptr_t)node_texts);
    asm_hex(a, "48 01 C1 0F 10 01");                // add rcx, rax; movups xmm0, [rcx]
    asm_hex(a, store_xmm);                          // movups [dest], xmm0
    asm_hex(a, "48 8B 41 10");                      // mov rax, [rcx+0x10]
    asm_hex(a, store_hash);                         // mov [dest+0x10], rax
}

// Name lookup: mod types return their name.
static uint8_t *write_name_stub(uint8_t *stub, uint8_t *site)
{
    Asm a = {stub};
    uint8_t *low, *high = asm_new_type_check(&a, "80 FA", &low);
    asm_hex(&a, "0F B6 C2");                        // movzx eax, dl
    asm_copy_text(&a, "0F 11 03", "48 89 43 10");
    asm_hex(&a, "48 89 D8 48 83 C4 20 5B C3");      // mov rax, rbx; add rsp, 0x20; pop rbx; ret
    asm_land(&a, low);
    asm_land(&a, high);
    asm_hex(&a, "80 FA 10 0F 87");                  // cmp dl, 0x10; ja default
    asm_rel32(&a, site + 9 + *(int32_t *)(site + 5));
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 9);
    return a.at;
}

// Node header: title +0, subtitle +0x18, colour +0x30; result = settings lookup (+0 not found, +8 settings).
#define HEADER_SUBTITLE 0x18
#define HEADER_COLOUR 0x30
static char span_names[HALF_HOURS * HALF_HOURS][12];
static NodeText span_texts[HALF_HOURS * HALF_HOURS];  // filled on first use

static const NodeText *time_subtitle(const uint8_t *settings)
{
    int mode, from, to;
    read_time_settings(settings, &mode, &from, &to);
    if (mode != TIME_MODE_SPAN) return &mode_texts[mode];
    NodeText *text = &span_texts[from * HALF_HOURS + to];
    if (!text->text) {
        char *name = span_names[from * HALF_HOURS + to];
        sprintf_s(name, sizeof(span_names[0]), "%s-%s", half_hour_names[from], half_hour_names[to]);
        *text = (NodeText){name, strlen(name), fnv1a64(name)};
    }
    return text;
}

// Subtitles built per frame live in a ring that must outlast one frame of visible nodes.
#define SUBTITLE_RING 512

// Seconds with up to three decimals: "4", "1,5", "0,25".
static void format_seconds(uint32_t ms, char decimal, char *text, size_t size)
{
    uint32_t part = ms % 1000;
    if (!part) sprintf_s(text, size, "%u", ms / 1000);
    else if (part % 100 == 0) sprintf_s(text, size, "%u%c%u", ms / 1000, decimal, part / 100);
    else if (part % 10 == 0) sprintf_s(text, size, "%u%c%02u", ms / 1000, decimal, part / 10);
    else sprintf_s(text, size, "%u%c%03u", ms / 1000, decimal, part);
}

// "1-4 s", "2 s", "1-4 s (2 s)" or "1-4 s Takt", built per settings.
static const NodeText *delay_subtitle(const uint8_t *settings, int pulse)
{
    uint32_t from, to, signal;
    read_timer_settings(settings, &from, &to, &signal);
    if (from > to) {
        uint32_t swap = from;
        from = to;
        to = swap;
    }
    char decimal = languages[language >= 0 ? language : LANGUAGE_ENGLISH].decimal;
    static char names[SUBTITLE_RING][48];
    static NodeText texts[SUBTITLE_RING];
    static unsigned next;
    unsigned at = next++ % SUBTITLE_RING;
    char low[16], high[16], range[36];
    format_seconds(from, decimal, low, sizeof(low));
    format_seconds(to, decimal, high, sizeof(high));
    if (from == to) sprintf_s(range, sizeof(range), "%s s", low);
    else sprintf_s(range, sizeof(range), "%s-%s s", low, high);
    if (pulse) {
        sprintf_s(names[at], sizeof(names[at]), "%s %s", range, pulse_word);
    } else if (signal) {
        format_seconds(signal, decimal, low, sizeof(low));
        sprintf_s(names[at], sizeof(names[at]), "%s (%s s)", range, low);
    } else {
        strcpy_s(names[at], sizeof(names[at]), range);
    }
    texts[at] = (NodeText){names[at], strlen(names[at]), fnv1a64(names[at])};
    return &texts[at];
}

// "30%" or "30% hold".
static const NodeText *random_subtitle(const uint8_t *settings)
{
    static char names[2][101][24];
    static NodeText texts[2][101];
    static const char *built_with[2][101];
    int mode, chance;
    read_random_settings(settings, &mode, &chance);
    NodeText *text = &texts[mode][chance];
    if (built_with[mode][chance] != hold_word) {
        char *name = names[mode][chance];
        if (mode == RANDOM_HOLD) sprintf_s(name, sizeof(names[0][0]), "%d%% %s", chance, hold_word);
        else sprintf_s(name, sizeof(names[0][0]), "%d%%", chance);
        *text = (NodeText){name, strlen(name), fnv1a64(name)};
        built_with[mode][chance] = hold_word;
    }
    return text;
}

// "count / N" of a counter node, built per counter slot.
static const NodeText *counter_subtitle(const uint8_t *node, const uint8_t *settings)
{
    uint32_t target = counter_target(settings);
    int count = counter_count(*(const uint32_t *)(node + 4));
    if (count < 0) return &count_texts[target - 1];
    static char names[SUBTITLE_RING][16];
    static NodeText texts[SUBTITLE_RING];
    static unsigned next;
    unsigned at = next++ % SUBTITLE_RING;
    sprintf_s(names[at], sizeof(names[at]), "%d / %u", count, target);
    texts[at] = (NodeText){names[at], strlen(names[at]), fnv1a64(names[at])};
    return &texts[at];
}

static void node_header(uint8_t *header, uint8_t *node, const uint8_t *result, uint32_t type)
{
    update_language();
    *(uint64_t *)(header + HEADER_COLOUR) = mod_colour;
    memcpy(header, &node_texts[type - NODE_FIRST], sizeof(NodeText));
    uint8_t settings[16] = {0};
    if (!result[0]) memcpy(settings, result + 8, sizeof(settings));
    const NodeText *subtitle = type == NODE_RANDOM ? random_subtitle(settings)
        : type == NODE_RANDOM_TIMER ? delay_subtitle(settings, timer_mode(settings) == RANDOM_PULSE)
        : type == NODE_MESSAGE ? &message_texts[message_option(message_choice(settings))]
        : type == NODE_COUNTER ? counter_subtitle(node, settings) : time_subtitle(settings);
    memcpy(header + HEADER_SUBTITLE, subtitle, sizeof(NodeText));
}

// Header switch: mod types call node_header(header, node record, settings lookup, type).
#define HEADER_RESULT 0x68
static uint8_t *write_header_stub(uint8_t *stub, uint8_t *site)
{
    uint8_t *done = site + 9 + *(int32_t *)(site + 5);
    Asm a = {stub};
    uint8_t *low, *high = asm_new_type_check(&a, "81 F8", &low);
    asm_hex(&a, "4C 8D 44 24");                     // lea r8, [rsp+HEADER_RESULT]
    *a.at++ = HEADER_RESULT;
    asm_hex(&a, "55 48 89 E5 48 83 E4 F0 48 83 EC 20");  // push rbp; mov rbp, rsp; and rsp, -16; sub rsp, 0x20
    asm_hex(&a, "48 89 F9 4C 89 F2 41 89 C1 FF 15");  // mov rcx, rdi; mov rdx, r14; mov r9d, eax; call [target]
    uint8_t *call_disp = a.at;
    a.at += 4;
    asm_hex(&a, "48 89 EC 5D E9");                  // mov rsp, rbp; pop rbp; jmp done
    asm_rel32(&a, done);
    asm_land(&a, low);
    asm_land(&a, high);
    asm_hex(&a, "83 F8 10 0F 87");                  // cmp eax, 0x10; ja done
    asm_rel32(&a, done);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 9);
    *(int32_t *)call_disp = (int32_t)(a.at - (call_disp + 4));
    asm_u64(&a, (uint64_t)(uintptr_t)node_header);
    return a.at;
}

// Compiler switch: mod types call compile_node and return its result.
static uint8_t *write_compile_stub(uint8_t *stub, uint8_t *site)
{
    uint8_t *fallback = site + 9 + *(int32_t *)(site + 5);
    Asm a = {stub};
    uint8_t *low, *high = asm_new_type_check(&a, "81 F8", &low);
    asm_hex(&a, "49 89 D1");                        // mov r9, rdx (node record)
    asm_hex(&a, "48 89 F9 48 89 DA 41 89 C0 FF 15");  // mov rcx, rdi; mov rdx, rbx; mov r8d, eax; call [target]
    uint8_t *call_disp = a.at;
    a.at += 4;
    asm_hex(&a, "E9");                              // jmp exit (al = result)
    asm_rel32(&a, fallback + 2);
    asm_land(&a, low);
    asm_land(&a, high);
    asm_hex(&a, "83 F8 10 0F 87");                  // cmp eax, 0x10; ja default
    asm_rel32(&a, fallback);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 9);
    *(int32_t *)call_disp = (int32_t)(a.at - (call_disp + 4));
    asm_u64(&a, (uint64_t)(uintptr_t)compile_node);
    return a.at;
}

// VM dispatch: the mod's ops call node_op(instruction, &top, instruction address), then the next instruction.
static uint8_t *write_vm_stub(uint8_t *stub, uint8_t *site)
{
    uint8_t *abort = site + 5 + (int8_t)site[4];
    Asm a = {stub};
    uint8_t *other, *other2 = asm_range(&a, "81 F8", OP_TIME, OP_DELAY, &other);
    asm_hex(&a, "48 85 D2 0F 84");                  // test rdx, rdx; je abort
    asm_rel32(&a, abort);
    asm_hex(&a, "48 8B 44 24 20 48 8D 44 10 FF");   // mov rax, [rsp+0x20]; lea rax, [rax+rdx-1] (top of stack)
    asm_hex(&a, "51 52 41 50 41 51 41 52 41 53");   // push rcx, rdx, r8, r9, r10, r11
    asm_hex(&a, "48 83 EC 20 8B 0F 48 89 C2 49 89 F8 FF 15");  // sub rsp, 0x20; mov ecx, [rdi]; mov rdx, rax;
                                                    // mov r8, rdi; call [target]
    uint8_t *call_disp = a.at;
    a.at += 4;
    asm_hex(&a, "48 83 C4 20");                     // add rsp, 0x20
    asm_hex(&a, "41 5B 41 5A 41 59 41 58 5A 59");   // pop r11, r10, r9, r8, rdx, rcx
    asm_hex(&a, "E9");                              // jmp next instruction
    asm_rel32(&a, abort + 3);
    asm_land(&a, other);
    asm_land(&a, other2);
    asm_hex(&a, "83 F8 14 0F 87");                  // cmp eax, 0x14; ja abort
    asm_rel32(&a, abort);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 5);
    *(int32_t *)call_disp = (int32_t)(a.at - (call_disp + 4));
    asm_u64(&a, (uint64_t)(uintptr_t)node_op);
    return a.at;
}

// Input count: the counter returns 2, Time 0, other types go on as before.
static uint8_t *write_inputs_stub(uint8_t *stub, uint8_t *site)
{
    uint8_t *one = site + 5 + (int8_t)site[4];
    Asm a = {stub};
    asm_compare(&a, "81 F8", NODE_COUNTER);
    asm_hex(&a, "75 03 B0 02 C3");                  // jne time; mov al, 2; ret
    asm_compare(&a, "81 F8", NODE_TIME);
    asm_hex(&a, "75 03 30 C0 C3");                  // time: jne other; xor al, al; ret
    asm_hex(&a, "83 F8 10 0F 87");                  // other: cmp eax, 0x10; ja one
    asm_rel32(&a, one);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 5);
    return a.at;
}

// Port style: the counter's type counts as Toggle.
static uint8_t *write_port_style_stub(uint8_t *stub, uint8_t *site)
{
    Asm a = {stub};
    asm_hex(&a, "45 0F B6 11");                     // movzx r10d, byte [r9]
    asm_compare(&a, "41 81 FA", NODE_COUNTER);      // cmp r10d, NODE_COUNTER
    asm_hex(&a, "75 06 41 BA");                     // jne sub; mov r10d, Toggle
    *(uint32_t *)a.at = TYPE_TOGGLE;
    a.at += 4;
    asm_hex(&a, "41 83 EA 08 E9");                  // sub: sub r10d, 8; jmp back (its flags reach the je)
    asm_rel32(&a, site + PORT_STYLE_LENGTH);
    return a.at;
}

// Port label check: the counter's type counts as Toggle ("movzx ecx, byte [rax]; sub ecx, 8").
static uint8_t *write_label_stub(uint8_t *stub, uint8_t *site)
{
    Asm a = {stub};
    asm_hex(&a, "0F B6 08");                        // movzx ecx, byte [rax]
    asm_compare(&a, "81 F9", NODE_COUNTER);         // cmp ecx, NODE_COUNTER
    asm_hex(&a, "75 05 B9");                        // jne sub; mov ecx, Toggle
    *(uint32_t *)a.at = TYPE_TOGGLE;
    a.at += 4;
    asm_hex(&a, "83 E9 08 E9");                     // sub: sub ecx, 8; jmp back (its flags reach the je)
    asm_rel32(&a, site + LABEL_LENGTH);
    return a.at;
}

// Create node: mod types get a (zeroed) settings slot like the game's types with settings, so they are always
// saved disguised.
static uint8_t *write_create_stub(uint8_t *stub, uint8_t *site, uint8_t *slot_case)
{
    uint8_t *none = site + 5 + (int8_t)site[4];
    Asm a = {stub};
    uint8_t *low, *high = asm_new_type_check(&a, "81 FB", &low);
    asm_hex(&a, "E9");                              // jmp slot case
    asm_rel32(&a, slot_case);
    asm_land(&a, low);
    asm_land(&a, high);
    asm_hex(&a, "83 FB 10 0F 87");                  // cmp ebx, 0x10; ja none
    asm_rel32(&a, none);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 5);
    return a.at;
}

// Node data write (SetNodeData, host and local): "movups xmm0, [r14]; mov r8, rdi; mov rcx, rbx; movups [rdx], xmm0"
// with the node record in rsi, the new 16 bytes at r14 and the node's settings at rdx.
static const char NODE_DATA_PATTERN[] = "41 0F 10 06 4C 8B C7 48 8B CB 0F 11 02 48 8B D6 E8";
#define NODE_DATA_LENGTH 7

// Mod nodes keep their settings (xmm0 = [rdx]) when the new bytes lack the mark.
static uint8_t *write_node_data_stub(uint8_t *stub, uint8_t *site)
{
    Asm a = {stub};
    asm_hex(&a, "0F B6 06");                        // movzx eax, byte [rsi]
    uint8_t *low, *high = asm_new_type_check(&a, "83 F8", &low);
    asm_hex(&a, "41 80 7E");                        // cmp byte [r14+SETTINGS_MARK_AT], SETTINGS_MARK
    *a.at++ = SETTINGS_MARK_AT;
    *a.at++ = SETTINGS_MARK;
    asm_hex(&a, "74");                              // je plain
    uint8_t *marked = asm_rel8(&a);
    asm_hex(&a, "0F 10 02 48 B8");                  // movups xmm0, [rdx]; mov rax, &foreign_change
    asm_u64(&a, (uint64_t)(uintptr_t)&foreign_change);
    asm_hex(&a, "C6 00 01 EB");                     // mov byte [rax], 1; jmp done
    uint8_t *skip = asm_rel8(&a);
    asm_land(&a, low);
    asm_land(&a, high);
    asm_land(&a, marked);
    asm_hex(&a, "41 0F 10 06");                     // plain: movups xmm0, [r14]
    asm_land(&a, skip);
    asm_hex(&a, "4C 8B C7 E9");                     // done: mov r8, rdi; jmp back
    asm_rel32(&a, site + NODE_DATA_LENGTH);
    return a.at;
}

// ---- Disguise in saves and graph transfers ----
// Mod nodes with settings are saved as TimedTriggers with the settings in the sub-millisecond part of their times,
// which the editor cannot produce. Without the mod they load as 1 s TimedTriggers.
// Time: delay = 1 s + DISGUISE_MARK + mode, duration = 1 s + DISGUISE_MARK + from * 64 + to.
#define DISGUISE_BASE 1000000000ull
#define DISGUISE_MARK 700000ull
#define DISGUISE_SPAN 4096ull
static __declspec(thread) uint64_t disguised[2];

// Random: delay = 1 s + DISGUISE_MARK + DISGUISE_RANDOM + mode, duration = 1 s + DISGUISE_MARK + chance.
// Random Timer: delay = From + DISGUISE_MARK + DISGUISE_TIMER + mode - 2, duration = To + DISGUISE_MARK + signal ms,
// so without the mod it is a TimedTrigger with From and To.
#define DISGUISE_TIMER 64
#define MS 1000000ull
#define DISGUISE_RANDOM 16
// Message: delay = 1 s + DISGUISE_MARK + DISGUISE_MESSAGE + choice, duration = 1 s + DISGUISE_MARK.
#define DISGUISE_MESSAGE 32
// Counter: delay = 1 s + DISGUISE_MARK + DISGUISE_COUNTER, duration = 1 s + DISGUISE_MARK + N.
#define DISGUISE_COUNTER 48

static const uint8_t *disguise_settings(const uint8_t *settings, uint32_t type)
{
    if (type == NODE_COUNTER) {
        disguised[0] = DISGUISE_BASE + DISGUISE_MARK + DISGUISE_COUNTER;
        disguised[1] = DISGUISE_BASE + DISGUISE_MARK + counter_target(settings);
        return (const uint8_t *)disguised;
    }
    if (type == NODE_MESSAGE) {
        disguised[0] = DISGUISE_BASE + DISGUISE_MARK + DISGUISE_MESSAGE + message_choice(settings);
        disguised[1] = DISGUISE_BASE + DISGUISE_MARK;
        return (const uint8_t *)disguised;
    }
    if (type == NODE_RANDOM_TIMER) {
        uint32_t from, to, signal;
        read_timer_settings(settings, &from, &to, &signal);
        disguised[0] = from * MS + DISGUISE_MARK + DISGUISE_TIMER + (uint64_t)(timer_mode(settings) - RANDOM_DELAY);
        disguised[1] = to * MS + DISGUISE_MARK + signal;
        return (const uint8_t *)disguised;
    }
    if (type == NODE_RANDOM) {
        int mode, chance;
        read_random_settings(settings, &mode, &chance);
        disguised[0] = DISGUISE_BASE + DISGUISE_MARK + DISGUISE_RANDOM + (uint64_t)mode;
        disguised[1] = DISGUISE_BASE + DISGUISE_MARK + (uint64_t)chance;
        return (const uint8_t *)disguised;
    }
    int mode, from, to;
    read_time_settings(settings, &mode, &from, &to);
    disguised[0] = DISGUISE_BASE + DISGUISE_MARK + (uint64_t)mode;
    disguised[1] = DISGUISE_BASE + DISGUISE_MARK + (uint64_t)from * 64 + (uint64_t)to;
    return (const uint8_t *)disguised;
}

// Turns a disguised TimedTrigger back into its mod node (node[0] = type, data = delay, duration).
static void undisguise_node(uint8_t *node, uint8_t *data)
{
    if (node[0] != TYPE_TIMED_TRIGGER) return;
    uint64_t delay = *(uint64_t *)data, duration = *(uint64_t *)(data + 8);
    uint64_t timer_mark = delay % MS, signal = duration % MS;
    if ((timer_mark == DISGUISE_MARK + DISGUISE_TIMER || timer_mark == DISGUISE_MARK + DISGUISE_TIMER + 1)
        && signal >= DISGUISE_MARK && signal - DISGUISE_MARK <= TIMER_MAX_SIGNAL) {
        node[0] = NODE_RANDOM_TIMER;
        memset(data, 0, 16);
        write_timer_settings(data, RANDOM_DELAY + (int)(timer_mark - DISGUISE_MARK - DISGUISE_TIMER),
            (uint32_t)(delay / MS < TIMER_MAX_MS ? delay / MS : TIMER_MAX_MS),
            (uint32_t)(duration / MS < TIMER_MAX_MS ? duration / MS : TIMER_MAX_MS), (uint32_t)(signal - DISGUISE_MARK));
        return;
    }
    if (delay < DISGUISE_BASE + DISGUISE_MARK || duration < DISGUISE_BASE + DISGUISE_MARK) return;
    uint64_t mode = delay - DISGUISE_BASE - DISGUISE_MARK, span = duration - DISGUISE_BASE - DISGUISE_MARK;
    if (mode == DISGUISE_COUNTER && span >= 1 && span <= COUNTER_MAX) {
        node[0] = NODE_COUNTER;
        memset(data, 0, 16);
        data[0] = (uint8_t)span;
        return;
    }
    if (mode >= DISGUISE_MESSAGE && mode < DISGUISE_MESSAGE + MESSAGE_CHOICES && span == 0) {
        node[0] = NODE_MESSAGE;
        memset(data, 0, 16);
        data[0] = (uint8_t)(mode - DISGUISE_MESSAGE);
        return;
    }
    if ((mode == DISGUISE_RANDOM + RANDOM_GATE || mode == DISGUISE_RANDOM + RANDOM_HOLD) && span >= 1 && span <= 100) {
        node[0] = NODE_RANDOM;
        memset(data, 0, 16);
        data[RANDOM_SETTING_MODE] = (uint8_t)(mode - DISGUISE_RANDOM);
        data[RANDOM_SETTING_CHANCE] = (uint8_t)span;
        return;
    }
    if (mode > TIME_MODE_SPAN || span >= DISGUISE_SPAN || span / 64 >= HALF_HOURS || span % 64 >= HALF_HOURS) return;
    node[0] = NODE_TIME;
    memset(data, 0, 16);
    data[SETTING_MODE] = (uint8_t)mode;
    data[SETTING_FROM] = (uint8_t)(span / 64);
    data[SETTING_TO] = (uint8_t)(span % 64);
}

// Writer's type byte: a mod node with settings is written as TimedTrigger.
static uint8_t *write_writer_type_stub(uint8_t *stub, uint8_t *site)
{
    Asm a = {stub};
    asm_hex(&a, "41 0F B6 1C B6");                  // movzx ebx, byte [r14+rsi*4]
    uint8_t *back1, *back3 = asm_new_type_check(&a, "81 FB", &back1);
    asm_hex(&a, "41 83 7C B6 10 00 74");            // cmp dword [r14+rsi*4+0x10], 0; je back
    uint8_t *back2 = asm_rel8(&a);
    asm_hex(&a, "BB");                              // mov ebx, TimedTrigger
    *(uint32_t *)a.at = TYPE_TIMED_TRIGGER;
    a.at += 4;
    asm_land(&a, back1);
    asm_land(&a, back2);
    asm_land(&a, back3);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 5);
    return a.at;
}

// Writer's settings switch: mod nodes write disguise_settings through the TimedTrigger case.
static uint8_t *write_writer_stub(uint8_t *stub, uint8_t *site, uint8_t *timed_case)
{
    uint8_t *none = site + 9 + *(int32_t *)(site + 5);
    Asm a = {stub};
    uint8_t *other, *other2 = asm_new_type_check(&a, "81 F8", &other);
    asm_hex(&a, "55 48 89 E5 48 83 E4 F0 48 83 EC 20");  // push rbp; mov rbp, rsp; and rsp, -16; sub rsp, 0x20
    asm_hex(&a, "48 89 D9 89 C2 FF 15");            // mov rcx, rbx; mov edx, eax; call [target]
    uint8_t *call_disp = a.at;
    a.at += 4;
    asm_hex(&a, "48 89 EC 5D 48 89 C3 E9");         // mov rsp, rbp; pop rbp; mov rbx, rax; jmp TimedTrigger case
    asm_rel32(&a, timed_case);
    asm_land(&a, other);
    asm_land(&a, other2);
    asm_hex(&a, "83 F8 0C 0F 87");                  // cmp eax, 0xc; ja none
    asm_rel32(&a, none);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 9);
    *(int32_t *)call_disp = (int32_t)(a.at - (call_disp + 4));
    asm_u64(&a, (uint64_t)(uintptr_t)disguise_settings);
    return a.at;
}

// End of the loader's TimedTrigger case: undisguise_node(node record, settings).
static uint8_t *write_loader_done_stub(uint8_t *stub, uint8_t *common)
{
    Asm a = {stub};
    asm_hex(&a, "48 8D 4D E8 48 8D 55 D0");         // lea rcx, [rbp-0x18]; lea rdx, [rbp-0x30]
    asm_hex(&a, "55 48 89 E5 48 83 E4 F0 48 83 EC 20 FF 15");  // push rbp; mov rbp, rsp; align; call [target]
    uint8_t *call_disp = a.at;
    a.at += 4;
    asm_hex(&a, "48 89 EC 5D E9");                  // mov rsp, rbp; pop rbp; jmp common
    asm_rel32(&a, common);
    *(int32_t *)call_disp = (int32_t)(a.at - (call_disp + 4));
    asm_u64(&a, (uint64_t)(uintptr_t)undisguise_node);
    return a.at;
}

// category_entry for unknown categories: the mod's name and colour.
static NodeText mod_category_text = {MOD_CATEGORY_NAME, sizeof(MOD_CATEGORY_NAME) - 1, 0};

static uint8_t *write_entry_stub(uint8_t *stub, uint8_t *text_done)
{
    Asm a = {stub};
    asm_hex(&a, "48 B8");                           // mov rax, &mod_category_text
    asm_u64(&a, (uint64_t)(uintptr_t)&mod_category_text);
    asm_hex(&a, "0F 10 00 0F 11 44 24 40");         // movups xmm0, [rax]; movups [rsp+0x40], xmm0
    asm_hex(&a, "48 8B 40 10 48 89 44 24 50");      // mov rax, [rax+0x10]; mov [rsp+0x50], rax
    asm_hex(&a, "48 BB");                           // mov rbx, mod_colour
    asm_u64(&a, mod_colour);
    asm_hex(&a, "E9");                              // jmp text done
    asm_rel32(&a, text_done);
    return a.at;
}

// Node submenu for categories >= 4: mod_buttons.
static uint8_t *write_submenu_stub(uint8_t *stub, uint8_t *end)
{
    Asm a = {stub};
    asm_hex(&a, "55 48 89 E5 48 83 E4 F0 48 83 EC 20 48 89 D9 FF 15");  // align; mov rcx, rbx; call [target]
    uint8_t *call_disp = a.at;
    a.at += 4;
    asm_hex(&a, "48 89 EC 5D E9");                  // mov rsp, rbp; pop rbp; jmp end
    asm_rel32(&a, end);
    *(int32_t *)call_disp = (int32_t)(a.at - (call_disp + 4));
    asm_u64(&a, (uint64_t)(uintptr_t)mod_buttons);
    return a.at;
}

// Drag preview colour for unknown categories.
static uint8_t *write_drag_stub(uint8_t *stub, uint8_t *next)
{
    Asm a = {stub};
    asm_hex(&a, "48 B8");                           // mov rax, mod_colour
    asm_u64(&a, mod_colour);
    asm_hex(&a, "E9");                              // jmp next
    asm_rel32(&a, next);
    return a.at;
}

// Nodes with modes describe the mode of the node in the panel (from the panel's last frame).
static const NodeText *node_description(uint32_t type)
{
    update_language();
    const NodeText *modes = languages[language >= 0 ? language : LANGUAGE_ENGLISH].mode_descriptions;
    int mode, a, b;
    if (type == NODE_RANDOM_TIMER) return &modes[DESC_TIMER + timer_mode(panel_settings) - RANDOM_DELAY];
    if (type == NODE_RANDOM) {
        read_random_settings(panel_settings, &mode, &a);
        return &modes[DESC_RANDOM + mode];
    }
    if (type == NODE_TIME) {
        read_time_settings(panel_settings, &mode, &a, &b);
        return &modes[DESC_TIME + mode];
    }
    return &node_descriptions[type - NODE_FIRST];
}

// Description function: mod types draw their own text.
static uint8_t *write_desc_stub(uint8_t *stub, uint8_t *unknown, uint8_t *draw, uint8_t *end)
{
    Asm a = {stub};
    uint8_t *low, *high = asm_new_type_check(&a, "41 80 F8", &low);
    asm_hex(&a, "55 48 89 E5 48 83 E4 F0 48 83 EC 20 41 0F B6 C8 FF 15");  // align; movzx ecx, r8b; call [target]
    uint8_t *call_disp = a.at;
    a.at += 4;
    asm_hex(&a, "48 89 EC 5D");                     // mov rsp, rbp; pop rbp
    asm_hex(&a, "48 83 78 08 00 0F 84");            // cmp qword [rax+8], 0; je end
    asm_rel32(&a, end);
    asm_hex(&a, "0F 10 00 0F 11 44 24 40");         // movups xmm0, [rax]; movups [rsp+0x40], xmm0
    asm_hex(&a, "48 8B 40 10 48 89 44 24 50 E9");   // mov rax, [rax+0x10]; mov [rsp+0x50], rax; jmp draw
    asm_rel32(&a, draw);
    asm_land(&a, low);
    asm_land(&a, high);
    asm_hex(&a, "E9");                              // other: jmp unknown
    asm_rel32(&a, unknown);
    *(int32_t *)call_disp = (int32_t)(a.at - (call_disp + 4));
    asm_u64(&a, (uint64_t)(uintptr_t)node_description);
    return a.at;
}

// Properties panel: mod types also call the description function.
static uint8_t *write_desc_call_stub(uint8_t *stub, uint8_t *site)
{
    uint8_t *skip = site + 5 + (int8_t)site[4];
    Asm a = {stub};
    uint8_t *low, *high = asm_new_type_check(&a, "81 F8", &low);
    asm_hex(&a, "E9");                              // jmp the description call
    asm_rel32(&a, skip - 8);
    asm_land(&a, low);
    asm_land(&a, high);
    asm_hex(&a, "83 F8 10 0F 87");                  // cmp eax, 0x10; ja skip
    asm_rel32(&a, skip);
    asm_hex(&a, "E9");                              // jmp back
    asm_rel32(&a, site + 5);
    return a.at;
}

static uint8_t *table_case(uint8_t *image, uint8_t *table_disp, int index)
{
    return image + *(int32_t *)(image + *(int32_t *)table_disp + 4 * index);
}

// Message node: event and lookup functions from the puzzle code, and a detour at the puzzle system's start.
static const char PUZZLE_EVENT_PATTERN[] =
    "49 8B 4E 60 E8 ?? ?? ?? ?? 49 8B 4E 60 0F B7 D8 E8 ?? ?? ?? ?? 44 0F B7 CB BA 30 AF 3B 5A "
    "41 B8 50 00 00 00 48 8B C8 E8";
#define PUZZLE_EVENT_INDEX_CALL 4
#define PUZZLE_EVENT_QUEUE_CALL 16
#define PUZZLE_EVENT_NEW_CALL 39
static const uint8_t PUZZLE_SYSTEM_START[] = {0x48, 0x89, 0x7C, 0x24, 0x10};
static const uint8_t PUZZLE_VIEW_INIT[] = {0x41, 0xB8, 0x68, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x54, 0x24, 0x30, 0x48, 0x8B,
    0xF9, 0xE8};
#define PUZZLE_VIEW_INIT_AT 0x12
// Position lookup in the puzzle helper.
static const char PUZZLE_POSITION_PATTERN[] = "4D 8B 46 50 48 8D 8C 24 B0 00 00 00 48 8B D0 44 8B CB E8";
#define PUZZLE_POSITION_CALL 18
// notify_logic_state_change calls current_entity at +0x70.
#define NOTIFY_ENTITY_CALL 0x70
static const uint8_t CURRENT_ENTITY_START[] = {0x8B, 0x41, 0x08, 0x4C, 0x8B, 0xC2};

static const char *install_message(uint8_t *image, uint8_t *code, uint8_t *code_end, uint8_t *stub, uint8_t **stub_end)
{
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + ((IMAGE_DOS_HEADER *)image)->e_lfanew);
    uint8_t *data, *data_end;
    if (!section(image, nt, ".rdata", &data, &data_end)) return "no .rdata";
    uint8_t *system = system_function(data, data_end, "puzzle_completed_event");
    uint8_t *helper = find_unique(code, code_end, PUZZLE_EVENT_PATTERN);
    uint8_t *position = find_unique(code, code_end, PUZZLE_POSITION_PATTERN);
    uint8_t *notify = system_function(data, data_end, "notify_logic_state_change");
    uint8_t *entity = notify && notify[NOTIFY_ENTITY_CALL] == 0xE8 ? call_target(notify + NOTIFY_ENTITY_CALL) : NULL;
    if (position && entity && memcmp(entity, CURRENT_ENTITY_START, sizeof(CURRENT_ENTITY_START)) == 0) {
        component_of = (ComponentFn)call_target(position + PUZZLE_POSITION_CALL);
        current_entity = (CurrentEntityFn)entity;
    } else {
        log_line("node: object positions not found, messages go to every player");
    }
    if (!system || !helper || memcmp(system, PUZZLE_SYSTEM_START, sizeof(PUZZLE_SYSTEM_START)) != 0
        || memcmp(system + PUZZLE_VIEW_INIT_AT, PUZZLE_VIEW_INIT, sizeof(PUZZLE_VIEW_INIT)) != 0) {
        log_line("node: puzzle event code not as expected (system %p, helper %p)", (void *)system, (void *)helper);
        return "unexpected puzzle event code";
    }
    view_init = (ViewInitFn)call_target(system + PUZZLE_VIEW_INIT_AT + sizeof(PUZZLE_VIEW_INIT) - 1);
    event_index = (EventIndexFn)call_target(helper + PUZZLE_EVENT_INDEX_CALL);
    event_queue = (EventQueueFn)call_target(helper + PUZZLE_EVENT_QUEUE_CALL);
    event_new = (EventNewFn)call_target(helper + PUZZLE_EVENT_NEW_CALL);
    // Stub jumps to puzzle_system; the trampoline runs the moved instruction and continues in the system.
    node_hooks[HOOK_PUZZLE] = (Hook){system, stub, sizeof(PUZZLE_SYSTEM_START)};
    write_jump(stub, (void *)puzzle_system);
    uint8_t *trampoline = stub + 16;
    memcpy(trampoline, PUZZLE_SYSTEM_START, sizeof(PUZZLE_SYSTEM_START));
    Asm a = {trampoline + sizeof(PUZZLE_SYSTEM_START)};
    asm_hex(&a, "E9");
    asm_rel32(&a, system + sizeof(PUZZLE_SYSTEM_START));
    original_puzzle_system = (SystemFn)trampoline;
    puzzle_system_function = system;
    *stub_end = a.at;
    return NULL;
}

// enshrouded_server.exe: no editor, so only the hooks that run graphs, save them and send messages are installed.
static int dedicated_server;

// Editor only: own category (column, entry, submenu, drag colour) and the descriptions in the properties panel.
static const char *install_editor_category(uint8_t *code, uint8_t *code_end, uint8_t **stub)
{
    Hook *h = node_hooks;
    uint8_t *at = *stub;
    uint8_t *column = find_unique(code, code_end, COLUMN_PATTERN);
    uint8_t *submenu = find_unique(code, code_end, SUBMENU_PATTERN);
    uint8_t *drag = find_unique(code, code_end, DRAG_PATTERN);
    uint8_t *entry = column ? call_target(column + COLUMN_CALL) : NULL;
    if (!column || !submenu || !drag || !entry || drag[DRAG_JUMP] != 0xEB
        || memcmp(entry + ENTRY_UNKNOWN, ENTRY_UNKNOWN_BYTES, sizeof(ENTRY_UNKNOWN_BYTES)) != 0
        || *(uint64_t *)(entry + ENTRY_UNKNOWN + 9) != 0xcbf29ce484222325ull) {
        log_line("node: category code not as expected (column %p, submenu %p, drag %p, entry %p), not installed",
            (void *)column, (void *)submenu, (void *)drag, (void *)entry);
        return "unexpected category code";
    }
    original_category_entry = (CategoryEntryFn)entry;
    mod_category_text.hash = fnv1a64(MOD_CATEGORY_NAME);
    uint8_t *submenu_jump = submenu + SUBMENU_JUMP;
    uint8_t *submenu_end = submenu_jump + 6 + *(int32_t *)(submenu_jump + 2);
    uint8_t *drag_next = drag + DRAG_JUMP + 2 + (int8_t)drag[DRAG_JUMP + 1];
    h[HOOK_ENTRY] = (Hook){entry + ENTRY_UNKNOWN, at, 7};
    at = write_entry_stub(at, entry + ENTRY_TEXT_DONE);
    h[HOOK_SUBMENU] = (Hook){submenu_jump, at, -1};
    at = write_submenu_stub(at, submenu_end);
    h[HOOK_DRAG] = (Hook){drag, at, 7};
    at = write_drag_stub(at, drag_next);
    h[HOOK_COLUMN] = (Hook){column + COLUMN_CALL, at, 0};
    write_jump(at, (void *)category_entries);
    at += 16;
    // Descriptions at the top of the properties panel.
    uint8_t *desc_call = find_unique(code, code_end, DESC_CALL_PATTERN);
    uint8_t *desc = find_unique(code, code_end, DESC_PATTERN);
    if (!desc_call || !desc) return "description code not found";
    desc_call += DESC_CALL_SITE;
    uint8_t *desc_jump = desc + DESC_JUMP;
    uint8_t *desc_unknown = desc_jump + 6 + *(int32_t *)(desc_jump + 2);
    uint8_t *empty_check = desc_jump + DESC_EMPTY_CHECK;
    uint8_t *desc_skip = desc_call + 5 + (int8_t)desc_call[4];
    if (desc_call[3] != 0x77 || memcmp(desc_skip - 8, "\x48\x8B\xCE\xE8", 4) != 0 || empty_check[0] != 0x0F
        || empty_check[1] != 0x84 || memcmp(desc_jump + DESC_DRAW, "\x48\x8B\x05", 3) != 0) {
        log_line("node: description code not as expected, not installed");
        return "unexpected description code";
    }
    uint8_t *desc_end = empty_check + 6 + *(int32_t *)(empty_check + 2);
    h[HOOK_DESC] = (Hook){desc_jump, at, -1};
    at = write_desc_stub(at, desc_unknown, desc_jump + DESC_DRAW, desc_end);
    h[HOOK_DESC_CALL] = (Hook){desc_call, at, 5};
    at = write_desc_call_stub(at, desc_call);
    *stub = at;
    return NULL;
}

// Editor only: port style and the Oben/Unten labels of the counter.
static const char *install_editor_ports(uint8_t *code, uint8_t *code_end, uint8_t **stub)
{
    Hook *h = node_hooks;
    uint8_t *at = *stub;
    uint8_t *port_style = find_unique(code, code_end, PORT_STYLE_PATTERN);
    if (!port_style) {
        log_line("node: port style code not found, not installed");
        return "port code not found";
    }
    h[HOOK_PORT_STYLE] = (Hook){port_style, at, PORT_STYLE_LENGTH};
    at = write_port_style_stub(at, port_style);
    uint8_t *label_a = find_unique(code, code_end, LABEL_PATTERN_A);
    uint8_t *label_b = find_unique(code, code_end, LABEL_PATTERN_B);
    if (!label_a || !label_b) {
        log_line("node: label code not as expected, not installed");
        return "unexpected label code";
    }
    h[HOOK_LABEL_A] = (Hook){label_a, at, LABEL_LENGTH};
    at = write_label_stub(at, label_a);
    h[HOOK_LABEL_B] = (Hook){label_b, at, LABEL_LENGTH};
    at = write_label_stub(at, label_b);
    *stub = at;
    return NULL;
}

// Installs the node hooks into `stubs` (one page, writable). Returns NULL or a reason for the log.
static const char *install_nodes(uint8_t *image, uint8_t *code, uint8_t *code_end, uint8_t *stubs)
{
    static const uint8_t ABORT_NEXT[] = {0x40, 0xB6, 0x01, 0x41, 0xFF, 0xC5};  // mov sil, 1; inc r13d
    static const uint8_t COMPILE_EXIT[] = {0x32, 0xC0, 0x48, 0x8B, 0x5C, 0x24};  // xor al, al; mov rbx, [rsp+..]
    int editor = !dedicated_server;
    uint8_t *menu = editor ? find_unique(code, code_end, MENU_PATTERN) : NULL;
    uint8_t *category = editor ? find_unique(code, code_end, CATEGORY_PATTERN) : NULL;
    uint8_t *name = editor ? find_unique(code, code_end, NAME_PATTERN) : NULL;
    uint8_t *compile = find_unique(code, code_end, COMPILE_PATTERN);
    uint8_t *helpers = find_unique(code, code_end, COMPILE_HELPERS_PATTERN);
    uint8_t *vm = find_unique(code, code_end, VM_PATTERN);
    uint8_t *header = editor ? find_unique(code, code_end, HEADER_PATTERN) : NULL;
    uint8_t *props = editor ? find_unique(code, code_end, PROPS_PATTERN) : NULL;
    uint8_t *writer = find_unique(code, code_end, WRITER_PATTERN);
    uint8_t *loader = find_unique(code, code_end, LOADER_PATTERN);
    if ((editor && (!menu || !category || !name || !header || !props)) || !compile || !helpers || !vm || !writer
        || !loader) {
        log_line("node: game code not found (menu %p, category %p, name %p, compile %p, helpers %p, vm %p, "
            "header %p, props %p, writer %p, loader %p)", (void *)menu, (void *)category, (void *)name,
            (void *)compile, (void *)helpers, (void *)vm, (void *)header, (void *)props, (void *)writer,
            (void *)loader);
        return "not found";
    }
    if (editor) original_button = (ButtonFn)call_target(menu + MENU_CALL);
    load_input = (LoadInputFn)call_target(helpers + LOAD_INPUT_CALL);
    emit = (EmitFn)call_target(helpers + EMIT_CALL);
    uint8_t *abort = vm + VM_SITE + 5 + (int8_t)vm[VM_SITE + 4];
    uint8_t *fallback = compile + COMPILE_SITE + 9 + *(int32_t *)(compile + COMPILE_SITE + 5);
    if ((editor && !original_button) || !load_input || !emit || memcmp(abort, ABORT_NEXT, sizeof(ABORT_NEXT)) != 0
        || memcmp(fallback, COMPILE_EXIT, sizeof(COMPILE_EXIT)) != 0) {
        log_line("node: unexpected game code, not installed");
        return "unexpected code";
    }
    init_languages();
    // Start of the name lookup function.
    if (!editor) {
    } else if (memcmp(name - NAME_FUNCTION, "\x40\x53\x48\x83\xEC\x20", 6) == 0) {
        name_lookup = (NameLookupFn)(name - NAME_FUNCTION);
    } else {
        log_line("node: name lookup not as expected, the new nodes have no names in the editor");
    }
    // Register channel case of the properties switch.
    uint8_t *register_case = editor ? table_case(image, props + PROPS_TABLE, TYPE_READ_REGISTER) : NULL;
    if (editor && (memcmp(register_case, "\x0F\xB6\xD3\x48\x8B\xCE\xE8", 7) != 0
        || !find_ui_functions(call_target(register_case + 6)))) {
        log_line("node: register widget not as expected, not installed");
        return "unexpected widget code";
    }
    if (editor && !find_panel_layout(table_case(image, props + PROPS_TABLE, TYPE_TIMED_TRIGGER))) {
        log_line("node: time panel layout not as expected, not installed");
        return "unexpected panel code";
    }
    uint8_t *writer_case = table_case(image, writer + WRITER_TABLE, TYPE_TIMED_TRIGGER);
    uint8_t *loader_case = table_case(image, loader + LOADER_TABLE, TYPE_TIMED_TRIGGER);
    // Loader: common continuation and the TimedTrigger case's jump to it.
    uint8_t *loader_none = loader + LOADER_SITE + 10 + *(int32_t *)(loader + LOADER_SITE + 6);
    uint8_t *loader_common = loader_none - sizeof(LOADER_COMMON);
    uint8_t *loader_done = NULL;
    for (uint8_t *at = loader_case; at < loader_case + LOADER_CASE_SCAN && !loader_done; at++) {
        if (at[0] == 0xE9 && at + 5 + *(int32_t *)(at + 1) == loader_common) loader_done = at;
    }
    uint8_t *writer_type = find_unique(code, code_end, WRITER_TYPE_PATTERN);
    if (memcmp(loader_common, LOADER_COMMON, sizeof(LOADER_COMMON)) != 0 || !loader_done || !writer_type) {
        log_line("node: save code not as expected (common %p, case end %p, writer type %p), not installed",
            (void *)loader_common, (void *)loader_done, (void *)writer_type);
        return "unexpected save code";
    }
    Hook *h = node_hooks;  // hooks left zero (no site) are not patched
    uint8_t *at = stubs + 512;
    if (editor) {
        h[HOOK_MENU] = (Hook){menu + MENU_CALL, at, 0};
        write_jump(at, (void *)node_button);
        at += 16;
        h[HOOK_CATEGORY] = (Hook){category, at, 5};
        at = write_category_stub(at, category);
        if (name_lookup) {  // the stub returns through the function's own epilogue (push rbx; sub rsp, 0x20)
            h[HOOK_NAME] = (Hook){name + NAME_SITE, at, 9};
            at = write_name_stub(at, name + NAME_SITE);
        }
    }
    h[HOOK_COMPILE] = (Hook){compile + COMPILE_SITE, at, 9};
    at = write_compile_stub(at, compile + COMPILE_SITE);
    h[HOOK_VM] = (Hook){vm + VM_SITE, at, 5};
    at = write_vm_stub(at, vm + VM_SITE);
    if (editor) {
        h[HOOK_HEADER] = (Hook){header + HEADER_SITE, at, 9};
        at = write_header_stub(at, header + HEADER_SITE);
        h[HOOK_PROPS] = (Hook){props + PROPS_SITE, at, 9};
        at = write_panel_stub(at, props + PROPS_SITE);
    }
    h[HOOK_WRITER] = (Hook){writer + WRITER_SITE, at, 9};
    at = write_writer_stub(at, writer + WRITER_SITE, writer_case);
    h[HOOK_WRITER_TYPE] = (Hook){writer_type + WRITER_TYPE_SITE, at, 5};
    at = write_writer_type_stub(at, writer_type + WRITER_TYPE_SITE);
    h[HOOK_LOADER_DONE] = (Hook){loader_done, at, 5};
    at = write_loader_done_stub(at, loader_common);
    uint8_t *create = find_unique(code, code_end, CREATE_PATTERN);
    if (!create || memcmp(create + CREATE_SITE + CREATE_SLOT_CASE, CREATE_SLOT_BYTES, sizeof(CREATE_SLOT_BYTES)) != 0) {
        log_line("node: create node code not as expected, not installed");
        return "unexpected create code";
    }
    h[HOOK_CREATE] = (Hook){create + CREATE_SITE, at, 5};
    at = write_create_stub(at, create + CREATE_SITE, create + CREATE_SITE + CREATE_SLOT_CASE);
    if (editor) {
        const char *error = install_editor_category(code, code_end, &at);
        if (error) return error;
    }
    uint8_t *inputs = find_unique(code, code_end, INPUTS_PATTERN);
    if (!inputs) {
        log_line("node: port code not found, not installed");
        return "port code not found";
    }
    h[HOOK_INPUTS] = (Hook){inputs + INPUTS_SITE, at, 5};
    at = write_inputs_stub(at, inputs + INPUTS_SITE);
    if (editor) {
        const char *error = install_editor_ports(code, code_end, &at);
        if (error) return error;
    }
    uint8_t *node_data = find_unique(code, code_end, NODE_DATA_PATTERN);
    if (!node_data) {
        log_line("node: node data code not found, not installed");
        return "node data code not found";
    }
    h[HOOK_NODE_DATA] = (Hook){node_data, at, NODE_DATA_LENGTH};
    at = write_node_data_stub(at, node_data);
    const char *message_error = install_message(image, code, code_end, at, &at);
    if (message_error) return message_error;
    if (at > stubs + 4096) return "stubs too large";
    for (int i = 0; i < HOOK_COUNT; i++) {
        if (h[i].site && !fits_rel32(h[i].site, h[i].stub)) return "stubs out of range";
    }
    return NULL;
}

// Points a "jcc rel32" (0F 8x rel32) at a new target.
static int retarget_jcc(uint8_t *jump, uint8_t *target)
{
    int64_t rel = (int64_t)(target - (jump + 6));
    if (jump[0] != 0x0F || rel < INT32_MIN || rel > INT32_MAX) return 0;
    DWORD old;
    if (!VirtualProtect(jump + 2, 4, PAGE_EXECUTE_READWRITE, &old)) return 0;
    *(int32_t *)(jump + 2) = (int32_t)rel;
    VirtualProtect(jump + 2, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), jump, 6);
    return 1;
}

// Patches all node hooks in enum order.
static int patch_nodes(void)
{
    for (int i = 0; i < HOOK_COUNT; i++) {
        Hook *h = &node_hooks[i];
        if (!h->site) continue;  // not used in this process (editor hooks on the server)
        int ok = h->length > 0 ? write_detour(h->site, h->length, h->stub)
            : h->length == 0 ? redirect_call(h->site, h->stub) : retarget_jcc(h->site, h->stub);
        if (!ok) return 0;
    }
    return 1;
}

// Trigger area editor: the X/Y/Z sliders are built with min 0.5 m ([rbp-0x30]) and max 8 m ([rbp-0x2c]). Only the
// editor limits the size; the max becomes AREA_MAX_SIZE.
static const char AREA_SLIDER_PATTERN[] = "C7 45 D0 00 00 00 3F C7 45 D4 00 00 00 41 E8";
#define AREA_SLIDER_MAX 10  // the max's immediate
#define AREA_SIZE_DEFAULT 16
#define AREA_SIZE_MIN 8      // the game's own limit
#define AREA_SIZE_MAX 64
static int area_size = AREA_SIZE_DEFAULT;  // config.ini area_size

static void patch_area_size(uint8_t *code, uint8_t *code_end)
{
    if (area_size == AREA_SIZE_MIN) return;
    uint8_t *site = find_unique(code, code_end, AREA_SLIDER_PATTERN);
    DWORD old;
    if (!site || !VirtualProtect(site + AREA_SLIDER_MAX, 4, PAGE_EXECUTE_READWRITE, &old)) {
        log_line("trigger area size: editor code not found, stays at 8 m");
        return;
    }
    float size = (float)area_size;
    memcpy(site + AREA_SLIDER_MAX, &size, 4);
    VirtualProtect(site + AREA_SLIDER_MAX, 4, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 16);
    log_line("trigger area size: up to %.0f m", size);
}

static void read_settings(void);

static void install(void)
{
    uint8_t *image = (uint8_t *)GetModuleHandleA(NULL);
    IMAGE_NT_HEADERS64 *nt = (IMAGE_NT_HEADERS64 *)(image + ((IMAGE_DOS_HEADER *)image)->e_lfanew);
    char exe[MAX_PATH] = "";
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    const char *exe_name = strrchr(exe, '\\') ? strrchr(exe, '\\') + 1 : exe;
    dedicated_server = _stricmp(exe_name, "enshrouded_server.exe") == 0;
    log_line("Sparkworks Enhanced " VERSION ", %s build %08lx", dedicated_server ? "dedicated server" : "game",
        nt->FileHeader.TimeDateStamp);
    read_settings();
    uint8_t *code, *code_end, *data, *data_end;
    if (!section(image, nt, ".text", &code, &code_end) || !section(image, nt, ".rdata", &data, &data_end)) {
        log_line("sections not found, mod NOT active");
        return;
    }
    uint8_t *update = find_unique(code, code_end, UPDATE_FUNCTION_PATTERN);
    uint8_t *server_call = find_update_call(system_function(data, data_end, "server_update_daytime"), update);
    uint8_t *notify = system_function(data, data_end, "notify_logic_state_change");
    uint8_t *state_read = find_unique(code, code_end, STATE_READ_PATTERN);
    uint8_t *node_check = find_unique(code, code_end, NODE_CHECK_PATTERN);
    if (!update || !server_call || !inside(state_read, notify, NOTIFY_FUNCTION_SIZE)
        || !inside(node_check, notify, NOTIFY_FUNCTION_SIZE)) {
        log_line("game code not found (update %p, server call %p, notify %p, state read %p, node check %p), "
            "mod NOT active", (void *)update, (void *)server_call, (void *)notify, (void *)state_read,
            (void *)node_check);
        return;
    }
    uint8_t *stubs = alloc_near(image, nt->OptionalHeader.SizeOfImage);
    if (!stubs) {
        log_line("no memory near the game image, mod NOT active");
        return;
    }
    uint8_t *trigger = system_function(data, data_end, "logic_trigger_shape");
    uint8_t *count_reset = find_unique(code, code_end, COUNT_RESET_PATTERN);
    uint8_t *count_match = find_unique(code, code_end, COUNT_MATCH_PATTERN);
    uint8_t *count_box = find_unique(code, code_end, COUNT_BOX_PATTERN);
    uint8_t *trigger_result = find_unique(code, code_end, TRIGGER_RESULT_PATTERN);
    hash_lookup = (HashLookupFn)find_unique(code, code_end, HASH_LOOKUP_PATTERN);
    int counting = hash_lookup && inside(count_reset, trigger, TRIGGER_FUNCTION_SIZE)
        && inside(count_match, trigger, TRIGGER_FUNCTION_SIZE) && inside(count_box, trigger, TRIGGER_FUNCTION_SIZE)
        && inside(trigger_result, trigger, TRIGGER_FUNCTION_SIZE);
    uint8_t *box_jump = counting ? count_box + COUNT_BOX_JUMP : NULL;
    if (counting) {  // box matches land on the sphere's match; a box miss continues where a sphere miss does
        counting = box_jump + 6 + *(int32_t *)(box_jump + 2) == count_match + COUNT_MATCH_LENGTH
            && count_match + COUNT_MATCH_LENGTH + *(int32_t *)(count_match + 6) == box_jump + 6;
    }
    enum { STATE_STUB = 64, MATCH_STUB = 160, RESET_STUB = 232, RESULT_STUB = 280, OFFER_STUB = 384 };
    uint8_t *offer = system_function(data, data_end, "interaction_offer");
    if (hash_lookup) storage_offset = *(int32_t *)((uint8_t *)hash_lookup + HASH_LOOKUP_OFFSET);
    int chests_ready = counting && offer && memcmp(offer, OFFER_START, sizeof(OFFER_START)) == 0
        && offer[OFFER_VIEW_INIT] == 0xE8 && offer[OFFER_VIEW_NEXT] == 0xE8;
    if (chests_ready) {
        chest_view_init = (ChestViewInitFn)call_target(offer + OFFER_VIEW_INIT);
        chest_view_next = (ChestViewNextFn)call_target(offer + OFFER_VIEW_NEXT);
        // Stub jumps to offer_system; the trampoline runs the moved instruction and continues in the system.
        write_jump(stubs + OFFER_STUB, (void *)offer_system);
        uint8_t *trampoline = stubs + OFFER_STUB + 16;
        memcpy(trampoline, OFFER_START, sizeof(OFFER_START));
        trampoline[sizeof(OFFER_START)] = 0xE9;
        *(int32_t *)(trampoline + sizeof(OFFER_START) + 1) =
            (int32_t)((offer + sizeof(OFFER_START)) - (trampoline + sizeof(OFFER_START) + 5));
        original_offer_system = (ChestSystemFn)trampoline;
        offer_function = offer;
    }
    original_update = (UpdateFn)update;
    write_jump(stubs, (void *)server_update);
    write_state_stub(stubs + STATE_STUB, state_read + STATE_READ_LENGTH);
    if (counting) {
        uint8_t *next = count_match + COUNT_MATCH_LENGTH + *(int32_t *)(count_match + 6);
        write_match_stub(stubs + MATCH_STUB, next, count_match + COUNT_MATCH_LENGTH + 3);
        write_reset_stub(stubs + RESET_STUB, count_reset + COUNT_RESET_LENGTH);
        write_result_stub(stubs + RESULT_STUB, trigger_result + TRIGGER_RESULT_LENGTH);
    }
    DWORD old;
    if (!VirtualProtect(stubs, 4096, PAGE_EXECUTE_READ, &old)) {
        log_line("stub memory could not be made executable, mod NOT active");
        return;
    }
    FlushInstructionCache(GetCurrentProcess(), stubs, 4096);
    if (redirect_call(server_call, stubs) + write_detour(state_read, STATE_READ_LENGTH, stubs + STATE_STUB) != 2) {
        log_line("patching failed, mod may be partly active");
        return;
    }
    // Box areas jump straight to the match stub.
    counting = counting && retarget_jcc(box_jump, stubs + MATCH_STUB + MATCH_COMMON)
        && write_detour(count_match, COUNT_MATCH_LENGTH, stubs + MATCH_STUB)
        && write_detour(count_reset, COUNT_RESET_LENGTH, stubs + RESET_STUB)
        && write_detour(trigger_result, TRIGGER_RESULT_LENGTH, stubs + RESULT_STUB);
    if (chests_ready && !write_detour(offer, sizeof(OFFER_START), stubs + OFFER_STUB)) chests_ready = 0;
    chests_active = chests_ready;
    if (!chests_ready) log_line("interaction code not as expected: chest areas do nothing");
    if (!counting) log_line("trigger area hooks NOT found: trigger area names do nothing");
    const char *nodes_error = "no stub memory";
    DWORD node_protect;
    if (VirtualProtect(stubs, 4096, PAGE_EXECUTE_READWRITE, &node_protect)) {
        nodes_error = install_nodes(image, code, code_end, stubs);
        VirtualProtect(stubs, 4096, node_protect, &node_protect);
        FlushInstructionCache(GetCurrentProcess(), stubs, 4096);
    }
    if (!nodes_error && !patch_nodes()) nodes_error = "patching failed";
    if (!dedicated_server) patch_area_size(code, code_end);  // the server does not limit the size
    if (nodes_error) log_line("nodes NOT active: %s", nodes_error);
    else if (dedicated_server) log_line("nodes active on the server: Random Timer, Counter, Time, Random, Message (no editor)");
    else log_line("nodes active: Random Timer, Counter, Time, Random, Message (logic editor category \"Sparkworks Enhanced\")");
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    random_state ^= (uint64_t)counter.QuadPart * 0x2545f4914f6cdd1dull;
    if (!random_state) random_state = 1;
    name_event = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (name_event) {
        CloseHandle(CreateThread(NULL, 0, name_thread, NULL, 0, NULL));
    } else {
        log_line("name thread could not start: trigger area names do nothing");
    }
    if (!counting || !name_event) areas_settling = 0;  // no names will be read: counters must not wait for them
    log_line("ready%s%s", counting ? ": trigger areas" : "", chests_ready ? ", chests" : "");
}

// config.ini next to the DLL. The archive doesn't contain it, so an update keeps the player's choices; missing
// settings are appended with their comment (the [settings] section is the file's only one).
static const struct { const char *key, *text; } SETTINGS[] = {
    {"colour", "; Colour of the Sparkworks Enhanced nodes as #RRGGBB.\r\ncolour=" DEFAULT_COLOUR "\r\n"},
    {"area_size", "; Largest trigger area size in metres the editor allows (8 to 64, the game's own is 8).\r\n"
        "; Large areas check more objects: keep them as small as the job allows.\r\narea_size=16\r\n"},
    {"debug", "; 1 = the log also shows routine events (compiled graphs, messages, in-game time every 2 hours).\r\n"
        "debug=0\r\n"},
};

static void read_settings(void)
{
    const char *path = settings_path;
    char text[32] = DEFAULT_COLOUR;
    if (path[0]) {
        HANDLE file = CreateFileA(path, FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD done;
            static const char HEADER[] = "; Sparkworks Enhanced settings. Restart the game after changing them.\r\n"
                "[settings]\r\n";
            if (GetLastError() != ERROR_ALREADY_EXISTS) WriteFile(file, HEADER, sizeof(HEADER) - 1, &done, NULL);
            CloseHandle(file);
        }
        // Missing settings are appended (new ones after an update).
        for (size_t i = 0; i < sizeof(SETTINGS) / sizeof(SETTINGS[0]); i++) {
            char value[8];
            GetPrivateProfileStringA("settings", SETTINGS[i].key, "\x01", value, sizeof(value), path);
            if (strcmp(value, "\x01") != 0) continue;  // present (even if empty)
            file = CreateFileA(path, FILE_APPEND_DATA, 0, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file == INVALID_HANDLE_VALUE) break;
            DWORD done;
            WriteFile(file, SETTINGS[i].text, (DWORD)strlen(SETTINGS[i].text), &done, NULL);
            CloseHandle(file);
        }
        GetPrivateProfileStringA("settings", "colour", DEFAULT_COLOUR, text, sizeof(text), path);
        debug_log = GetPrivateProfileIntA("settings", "debug", 0, path) != 0;
        int size = (int)GetPrivateProfileIntA("settings", "area_size", AREA_SIZE_DEFAULT, path);
        area_size = size < AREA_SIZE_MIN ? AREA_SIZE_MIN : size > AREA_SIZE_MAX ? AREA_SIZE_MAX : size;
        if (area_size != size) {
            log_line("area_size %d in config.ini is outside 8..64, using %d", size, area_size);
        }
    }
    const char *hex = text[0] == '#' ? text + 1 : text;
    char *end;
    unsigned long rgb = strtoul(hex, &end, 16);
    if (end - hex != 6) {
        log_line("colour \"%s\" in config.ini is not #RRGGBB, using " DEFAULT_COLOUR, text);
        rgb = strtoul(DEFAULT_COLOUR + 1, NULL, 16);
    }
    uint32_t abgr = 0xff000000u | (rgb & 0xff) << 16 | (rgb & 0xff00) | (rgb >> 16 & 0xff);
    mod_colour = abgr | 0x3f800000ull << 32;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        DWORD length = GetModuleFileNameA(instance, log_path, MAX_PATH);
        char *slash = (length > 0 && length < MAX_PATH) ? strrchr(log_path, '\\') : NULL;
        if (slash) {
            strcpy_s(settings_path, MAX_PATH, log_path);
            strcpy_s(settings_path + (slash + 1 - log_path), MAX_PATH - (slash + 1 - log_path), "config.ini");
            strcpy_s(slash + 1, MAX_PATH - (slash + 1 - log_path), "debug.log");
        } else {
            log_path[0] = 0;
        }
        install();
    }
    return TRUE;
}
