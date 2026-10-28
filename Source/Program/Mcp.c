// The MCP server, `filey --mcp`, which an agent starts and talks JSON-RPC to over the standard input and output, and the window's half of it:
// the server answers what it can alone, the tools it offers above all, and hands each tool's call to the window over the control channel, whose answer, as JSON, is what the agent gets. The window listens only while its settings have `mcp = on`.
// -----------------------------------------------------------------------------

// `text` as a JSON string, in its quotes, its quotes, backslashes and control characters escaped.
function String Prog_JsonQuote(Arena *arena, String text)
{
    StrBuilder out = {0};
    StrBuildChar(arena, &out, '"');
    for (u64 i = 0; i < text.length; ++i) {
        u8 c = (u8)text.value[i];
        if (c == '"' || c == '\\') StrBuildChar(arena, &out, '\\'), StrBuildChar(arena, &out, (char)c);
        else if (c == '\n') StrBuildStr(arena, &out, S("\\n"));
        else if (c == '\r') StrBuildStr(arena, &out, S("\\r"));
        else if (c == '\t') StrBuildStr(arena, &out, S("\\t"));
        else if (c < 0x20) StrBuildStr(arena, &out, StrFormat(arena, "\\u%04x", (u32)c));
        else if (c < 0x80) StrBuildChar(arena, &out, (char)c);
        else {
            // A character whole, and a byte that starts none, as a name on Linux may hold, as the replacement character, which keeps the JSON valid.
            u64 next = i;
            StrDecodeUtf8(text, &next);
            if (next - i > 1) StrBuildStr(arena, &out, StrFromTo(text, i, (i64)next)), i = next - 1;
            else StrBuildStr(arena, &out, S("\\ufffd"));
        }
    }
    StrBuildChar(arena, &out, '"');
    return out.buffer;
}

function u32 Prog__JsonHex(String text, u64 at)
{
    u32 value = 0;
    for (u64 i = at; i < at + 4 && i < text.length; ++i) {
        u8 c = (u8)text.value[i];
        value = value * 16 + (c >= '0' && c <= '9' ? (u32)(c - '0') : c >= 'a' && c <= 'f' ? (u32)(c - 'a' + 10) : c >= 'A' && c <= 'F' ? (u32)(c - 'A' + 10) : 0);
    }
    return value;
}

// A JSON string's characters, its escapes undone; empty for what is no string.
function String Prog_JsonText(Arena *arena, const Prog__Json *node)
{
    if (!node || node->kind != '"') return (String){0};
    String raw = node->text;
    if (!StrIndexChar(raw, '\\', SSF_None).exists) return raw;
    StrBuilder out = {0};
    for (u64 i = 0; i < raw.length; ++i) {
        char c = raw.value[i];
        if (c != '\\' || i + 1 >= raw.length) {
            StrBuildChar(arena, &out, c);
            continue;
        }
        char e = raw.value[++i];
        if (e == 'n') StrBuildChar(arena, &out, '\n');
        else if (e == 't') StrBuildChar(arena, &out, '\t');
        else if (e == 'r') StrBuildChar(arena, &out, '\r');
        else if (e == 'b') StrBuildChar(arena, &out, '\b');
        else if (e == 'f') StrBuildChar(arena, &out, '\f');
        else if (e == 'u' && i + 4 < raw.length) {
            u32 code = Prog__JsonHex(raw, i + 1);
            i += 4;
            // A pair of surrogates is one character past the first plane.
            if (code >= 0xD800 && code < 0xDC00 && i + 6 < raw.length && raw.value[i + 1] == '\\' && raw.value[i + 2] == 'u') {
                u32 low = Prog__JsonHex(raw, i + 3);
                if (low >= 0xDC00 && low < 0xE000) code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00), i += 6;
            }
            u8 bytes[4];
            // Half of a pair alone is no character, so it is the replacement character.
            u32 count = StrEncodeUtf8((rune)code, bytes);
            if (!count) count = StrEncodeUtf8(0xFFFD, bytes);
            StrBuildBytes(arena, &out, bytes, count);
        } else StrBuildChar(arena, &out, e);
    }
    return out.buffer;
}

function b32 Prog__JsonTrue(const Prog__Json *node) { return node && node->kind == 'v' && StrIsEqual(node->text, S("true")); }

function String Prog__ToolError(Arena *arena, String message) { return StrFormat(arena, "{\"error\":%S}", Prog_JsonQuote(arena, message)); }

// The names of the shown folder's items `names` lists, a JSON array of strings, selected, the keyboard on the first; those the filter hides are left out, as they never are selected.
function u64 Prog__SelectNames(Prog_State *state, Arena *arena, const Prog__Json *names)
{
    struct { MapHeader header; struct { String key; b32 value; } *v; } wanted = {0};
    for (u64 i = 0; names && names->kind == '[' && i < names->count; ++i) {
        String name = Prog_JsonText(arena, names->items[i]);
        if (name.length) MapsPut(arena, wanted, name, true);
    }
    u64 first = UINT64_MAX;
    state->selectedCount = 0;
    for (u64 i = 0; i < state->directory.count; ++i) {
        Prog_Entry *entry = &state->directory.entries[i];
        entry->selected = MapsGet(wanted, b32, entry->file.name) && Prog_ViewPosition(state, i) != UINT64_MAX;
        state->selectedCount += entry->selected != 0;
        if (entry->selected && first == UINT64_MAX) first = i;
    }
    if (first != UINT64_MAX) {
        state->hasFocus = state->hasAnchor = true;
        state->focusedRow = state->anchorRow = first;
        state->revealFocus = true;
    }
    return state->selectedCount;
}

// What the active panel shows: its folder, the keyboard's item, the selection, the tabs and the panels.
function String Prog__ToolState(Prog_State *state, Arena *arena)
{
    Prog__UsePanel(state, state->activePanel);
    StrBuilder out = {0};
    String folder = state->directory.path;
    b32 thisPC = StrIsEqual(folder, Prog_ThisPC);
    StrBuildStr(arena, &out, StrFormat(arena, "{\"folder\":%S,\"title\":%S,\"loading\":%s,\"items\":%" PRIu64 ",\"shown\":%" PRIu64,
                                       Prog_JsonQuote(arena, thisPC ? folder : P_NativePath(arena, folder)), Prog_JsonQuote(arena, Prog_PathName(state, folder)),
                                       state->loading ? "true" : "false", state->directory.count, state->viewCount));
    String focused = state->hasFocus && state->focusedRow < state->directory.count ? state->directory.entries[state->focusedRow].file.name : (String){0};
    StrBuildStr(arena, &out, StrFormat(arena, ",\"focused\":%S", focused.length ? Prog_JsonQuote(arena, focused) : S("null")));
    // At most a thousand of the selected, which the count says the whole of.
    StrBuildStr(arena, &out, StrFormat(arena, ",\"selectedCount\":%" PRIu64 ",\"selected\":[", state->selectedCount));
    u64 listed = 0;
    for (u64 i = 0; i < state->directory.count && listed < 1000; ++i) {
        if (!state->directory.entries[i].selected) continue;
        StrBuildStr(arena, &out, StrFormat(arena, "%s%S", listed++ ? "," : "", Prog_JsonQuote(arena, state->directory.entries[i].file.name)));
    }
    StrBuildStr(arena, &out, S("],\"tabs\":["));
    for (u32 t = 0; t < state->tabCount; ++t) {
        String path = Prog_TabPath(state, t);
        String shown = StrIsEqual(path, Prog_ThisPC) ? path : P_NativePath(arena, path);
        StrBuildStr(arena, &out, StrFormat(arena, "%s{\"folder\":%S,\"active\":%s}", t ? "," : "", Prog_JsonQuote(arena, shown), t == state->activeTab ? "true" : "false"));
    }
    StrBuildStr(arena, &out, StrFormat(arena, "],\"panels\":%u,\"activePanel\":%u}", state->panelCount, state->activePanel));
    return out.buffer;
}

// Shows `path` in the active panel: a folder in the shown tab, or a new one with `newTab`, with the items `select` names selected once it is listed; a file in its folder, selected.
function String Prog__ToolOpen(Prog_State *state, Arena *arena, const Prog__Json *arguments)
{
    String given = Prog_JsonText(arena, Prog__JsonGet(arguments, S("path")));
    if (!given.length) return Prog__ToolError(arena, NoTr("open_path needs a path."));
    String path = StrIsEqual(given, Prog_ThisPC) ? given : P_AbsolutePath(arena, given);
    if (!StrIsEqual(path, Prog_ThisPC) && !P_PathExists(path)) return Prog__ToolError(arena, StrFormat(arena, "%S does not exist.", given));
    Prog__UsePanel(state, state->activePanel);
    b32 newTab = Prog__JsonTrue(Prog__JsonGet(arguments, S("newTab")));
    if (!StrIsEqual(path, Prog_ThisPC) && !P_IsDirectory(path)) {
        Prog_ShowInFolder(state, path, newTab);
        return S("{\"opened\":\"file\"}");
    }
    if (newTab) Prog_OpenTab(state, path, state->activeTab + 1, true);
    else Prog_RequestDirectory(state, path, true, 0);
    // What to select comes with the listing, as a paste's new items do.
    const Prog__Json *select = Prog__JsonGet(arguments, S("select"));
    if (select && select->kind == '[' && select->count) {
        ArenaClear(&state->pickArena);
        state->pickNames = (ArrayStr){0};
        for (u64 i = 0; i < select->count; ++i) {
            String name = Prog_JsonText(arena, select->items[i]);
            if (name.length) ArrayPush(&state->pickArena, state->pickNames, StrCopy(&state->pickArena, name));
        }
    }
    return S("{\"opened\":\"folder\"}");
}

// Runs the command `name` names, as its hotkey would, or lists the commands, those `query` finds by their label and description.
function String Prog__ToolCommand(Prog_State *state, Arena *arena, const Prog__Json *arguments, b32 run)
{
    Prog__UsePanel(state, state->activePanel);
    if (run) {
        String name = Prog_JsonText(arena, Prog__JsonGet(arguments, S("name")));
        Prog_CommandKind kind = Prog__CommandNamed(name);
        if (!kind || !Prog_Commands[kind].label.length) return Prog__ToolError(arena, StrFormat(arena, "No command is named %S; list_commands lists them.", name));
        if (!Prog_CommandAvailable(state, kind)) return Prog__ToolError(arena, StrFormat(arena, "%S does nothing here now.", name));
        Prog_QueueCommand(state, kind, 0, 0, UINT64_MAX);
        return StrFormat(arena, "{\"ran\":%S}", Prog_JsonQuote(arena, name));
    }
    String query = Prog_JsonText(arena, Prog__JsonGet(arguments, S("query")));
    StrBuilder out = {0};
    StrBuildStr(arena, &out, S("{\"commands\":["));
    u32 listed = 0;
    for (u32 kind = Prog_CommandKind_None + 1; kind < Prog_CommandKind_Count; ++kind) {
        const Prog_CommandDesc *desc = &Prog_Commands[kind];
        if (!desc->label.length || !Prog_CommandApplies((Prog_CommandKind)kind)) continue;
        if (query.length && Prog_FuzzyScore(desc->label, desc->tooltip, query, NULL, NULL) <= 0 && !Prog_FilterMatch(desc->aliases, query, NULL)) continue;
        // The label without its search words, which follow a caret.
        String label = Prog_CommandLabel((Prog_CommandKind)kind);
        StrBuildStr(arena, &out, StrFormat(arena, "%s{\"name\":%S,\"label\":%S,\"description\":%S,\"available\":%s}", listed++ ? "," : "", Prog_JsonQuote(arena, desc->name),
                                           Prog_JsonQuote(arena, label), Prog_JsonQuote(arena, Prog_CommandTooltip((Prog_CommandKind)kind)),
                                           Prog_CommandAvailable(state, (Prog_CommandKind)kind) ? "true" : "false"));
    }
    StrBuildStr(arena, &out, S("]}"));
    return out.buffer;
}

// What Go To finds for `query`: the drives, places, bookmarks and recents, then the drive index's files and folders, if it has been made, as the best `limit`, best first.
function String Prog__ToolSearch(Prog_State *state, Arena *arena, const Prog__Json *arguments)
{
    String query = Prog_JsonText(arena, Prog__JsonGet(arguments, S("query")));
    if (!query.length) return Prog__ToolError(arena, NoTr("search needs a query."));
    u64 limit = Min(Max(Prog__JsonCount(Prog__JsonGet(arguments, S("limit")), 20), (u64)1), (u64)200);
    typedef struct { String path; i32 score; b32 file; } Hit;
    Hit *hits = ArenaPushArray(arena, limit + 1, Hit);
    u64 count = 0;
    // Kept in order, the best first, each new one put in its place and the worst let go.
#define Prog__Hit(p, s, f) do { \
        i32 score_ = (s); u64 at_ = count; \
        while (at_ > 0 && hits[at_ - 1].score < score_) --at_; \
        if (at_ < limit) { \
            MemMove(&hits[at_ + 1], &hits[at_], (Min(count, limit - 1) - at_) * sizeof(Hit)); \
            hits[at_] = (Hit){ (p), score_, (f) }; \
            count = Min(count + 1, limit); \
        } \
    } while (0)
    for (u64 i = 0; i < state->drives.header.count; ++i) {
        i32 score = Prog__GoToScore(state, state->drives.v[i].name, state->drives.v[i].path, query);
        if (score > 0) Prog__Hit(state->drives.v[i].path, score, false);
    }
    const ArrayStr *lists[] = { &state->bookmarks, &state->recents };
    for (u32 l = 0; l < CArrayCount(lists); ++l) {
        for (u64 i = 0; i < lists[l]->header.count; ++i) {
            String path = lists[l]->v[i];
            i32 score = Prog__GoToScore(state, Prog__FolderName(path), path, query);
            if (score > 0) Prog__Hit(path, score, false);
        }
    }
    for (u64 i = 0; i < state->places.header.count; ++i) {
        String path = state->places.v[i].path;
        i32 score = Prog__GoToScore(state, state->places.v[i].name, path, query);
        if (score > 0 && !StrIsEqual(path, Prog_ThisPC)) Prog__Hit(path, score, false);
    }
    const Prog_Index *index = state->indexDone || !state->savedIndex.count ? &state->index : &state->savedIndex;
    // The index in slices, each searched by every lane at once, as Go To does over frames, here for at most a quarter of a second, which the window waits for.
    // A search cut short says so.
    u64 until = PlatformNowMicroseconds() + 250000;
    b32 complete = true;
    for (u64 first = 0; index->count && first < index->count;) {
        if (PlatformNowMicroseconds() >= until) {
            complete = false;
            break;
        }
        Prog__GoToLook look = { .state = state, .index = index, .text = query, .first = first, .count = Min(index->count - first, (u64)1 << 20), .until = until,
                                .floor = Prog_GoToRanks };
        u64 blocks = (look.count + Prog_GoToBlock - 1) / Prog_GoToBlock;
        Scratch scratch = GetScratchEx(arena);
        look.found = ArenaPushArrayNoZero(scratch.arena, look.count, Prog__GoToFound);
        look.foundCount = ArenaPushArrayNoZero(scratch.arena, blocks, u32);
        P_RunLanes(Max(1u, Min(P_ProcessorCount(), (u32)Prog_WalkLanes)), Prog__GoToLane, &look);
        // Only the blocks the lanes took before the time was up have their finds.
        u64 taken = Min((u64)atomic_load(&look.next), blocks);
        complete = complete && taken == blocks;
        for (u64 block = 0; block < taken; ++block) {
            for (u32 i = 0; i < look.foundCount[block]; ++i) {
                Prog__GoToFound found = look.found[block * Prog_GoToBlock + i];
                if (Prog_IndexGone(index, found.entry) || (count == limit && hits[count - 1].score >= found.score)) continue;
                Prog__Hit(Prog_IndexPath(arena, index, found.entry), found.score, (index->flags[found.entry] & Prog_IndexFlag_File) != 0);
            }
        }
        ReleaseScratch(scratch);
        if (taken < blocks) break;
        first += look.count;
    }
#undef Prog__Hit
    StrBuilder out = {0};
    StrBuildStr(arena, &out, StrFormat(arena, "{\"indexed\":%s,\"complete\":%s,\"results\":[", index->count ? "true" : "false", complete ? "true" : "false"));
    for (u64 i = 0; i < count; ++i) {
        String shown = StrIsEqual(hits[i].path, Prog_ThisPC) ? hits[i].path : P_NativePath(arena, hits[i].path);
        StrBuildStr(arena, &out, StrFormat(arena, "%s{\"path\":%S,\"kind\":\"%s\"}", i ? "," : "", Prog_JsonQuote(arena, shown), hits[i].file ? "file" : "folder"));
    }
    StrBuildStr(arena, &out, S("]}"));
    return out.buffer;
}

// The window's answer to a request on the control channel: a tools/call's params, {"name": tool, "arguments": {...}}, answered with the tool's result as JSON, or {"error": why} for a call that failed.
function String Prog_ControlAnswer(Prog_State *state, Arena *arena, String request)
{
    const char *at = request.value;
    Prog__Json *params = Prog__JsonValue(arena, &at, request.value + request.length, 0);
    if (!params || params->kind != '{') return Prog__ToolError(arena, NoTr("The request is no JSON object."));
    String tool = Prog_JsonText(arena, Prog__JsonGet(params, S("name")));
    const Prog__Json *arguments = Prog__JsonGet(params, S("arguments"));
    if (StrIsEqual(tool, S("get_state"))) return Prog__ToolState(state, arena);
    if (StrIsEqual(tool, S("open_path"))) return Prog__ToolOpen(state, arena, arguments);
    if (StrIsEqual(tool, S("select"))) {
        Prog__UsePanel(state, state->activePanel);
        u64 selected = Prog__SelectNames(state, arena, Prog__JsonGet(arguments, S("names")));
        return StrFormat(arena, "{\"selected\":%" PRIu64 "}", selected);
    }
    if (StrIsEqual(tool, S("run_command"))) return Prog__ToolCommand(state, arena, arguments, true);
    if (StrIsEqual(tool, S("list_commands"))) return Prog__ToolCommand(state, arena, arguments, false);
    if (StrIsEqual(tool, S("search"))) return Prog__ToolSearch(state, arena, arguments);
    return Prog__ToolError(arena, StrFormat(arena, "No tool is named %S.", tool));
}

// The tools, as tools/list gives them.
#define Prog_McpTools \
    "[{\"name\":\"get_state\",\"description\":\"What Filey shows in its active panel: the folder, the focused item, the selected items (names relative to the folder), the tabs and the panels.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}," \
    "{\"name\":\"open_path\",\"description\":\"Shows a folder in Filey, in the active tab or a new one, optionally with some of its items selected; a file opens its folder with it selected.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"An absolute path.\"}," \
    "\"newTab\":{\"type\":\"boolean\",\"description\":\"Open in a new tab rather than the active one.\"}," \
    "\"select\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Names of items in the folder to select once it is listed.\"}},\"required\":[\"path\"]}}," \
    "{\"name\":\"select\",\"description\":\"Selects items of the folder Filey shows by their names, replacing the selection.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"names\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},\"required\":[\"names\"]}}," \
    "{\"name\":\"list_commands\",\"description\":\"Lists Filey's commands with their names, labels, descriptions and whether each would do anything now; a query narrows them.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"}}}}," \
    "{\"name\":\"run_command\",\"description\":\"Runs one of Filey's commands by its name, as its hotkey would, on the selection or the focused item where it acts on items. Destructive ones still ask in Filey's window.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"name\":{\"type\":\"string\",\"description\":\"A command's name from list_commands, such as ToggleTreeView.\"}},\"required\":[\"name\"]}}," \
    "{\"name\":\"search\",\"description\":\"Finds files and folders by name as Filey's Go To does: drives, places, bookmarks, recents, and the drive index if it has been built.\"," \
    "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"query\":{\"type\":\"string\"},\"limit\":{\"type\":\"integer\",\"description\":\"At most this many, 20 unless given.\"}},\"required\":[\"query\"]}}]"

// Whether the settings in `configDirectory` turn the control channel on, read without the rest of them.
function b32 Prog_McpEnabled(Arena *arena, String configDirectory)
{
    return Prog__SettingOn(Prog_SettingFromFile(arena, configDirectory, S("mcp")));
}

// The answer to one JSON-RPC message from the agent, empty for a notification, which has none. A tools/call goes to the window for `configDirectory`, which is started when it is not running and the settings allow it, unless `mayStart` is false.
function String Prog_McpRespond(Arena *arena, String configDirectory, String message, b32 mayStart)
{
    const char *at = message.value;
    Prog__Json *root = Prog__JsonValue(arena, &at, message.value + message.length, 0);
    if (!root || root->kind != '{') return S("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-32700,\"message\":\"Parse error\"}}");
    const Prog__Json *idNode = Prog__JsonGet(root, S("id"));
    String method = Prog_JsonText(arena, Prog__JsonGet(root, S("method")));
    if (!idNode) return (String){0};
    // The id goes back as it came, which JSON-RPC has a string or a number; anything else, as an object, is answered with null.
    String id = idNode->kind == '"' || idNode->kind == 'v' ? idNode->raw : S("null");
    const Prog__Json *params = Prog__JsonGet(root, S("params"));
    String result = {0};
    if (StrIsEqual(method, S("initialize"))) {
        String version = Prog_JsonText(arena, Prog__JsonGet(params, S("protocolVersion")));
        result = StrFormat(arena, "{\"protocolVersion\":%S,\"capabilities\":{\"tools\":{}},\"serverInfo\":{\"name\":\"filey\",\"version\":%S},"
                                  "\"instructions\":%S}",
                           Prog_JsonQuote(arena, version.length ? version : S("2025-06-18")), Prog_JsonQuote(arena, S(FILEY_VERSION)),
                           Prog_JsonQuote(arena, S("Filey is the user's file manager. get_state says what they are looking at and have selected; open_path and select "
                                                   "show them what you made or found; run_command does what Filey's own commands do.")));
    } else if (StrIsEqual(method, S("ping"))) {
        result = S("{}");
    } else if (StrIsEqual(method, S("tools/list"))) {
        result = S("{\"tools\":" Prog_McpTools "}");
    } else if (StrIsEqual(method, S("tools/call"))) {
        if (!params || params->kind != '{') return StrFormat(arena, "{\"jsonrpc\":\"2.0\",\"id\":%S,\"error\":{\"code\":-32602,\"message\":\"tools/call needs params\"}}", id);
        // A relative path is the agent's, which the window, in a folder of its own, would take as another; it goes as absolute.
        String request = params->raw;
        const Prog__Json *arguments = Prog__JsonGet(params, S("arguments")), *pathNode = Prog__JsonGet(arguments, S("path"));
        String given = Prog_JsonText(arena, pathNode);
        if (given.length && !StrIsEqual(given, Prog_ThisPC)) {
            String absolute = P_NativePath(arena, P_AbsolutePath(arena, given));
            u64 from = (u64)(pathNode->raw.value - request.value);
            request = StrFormat(arena, "%S%S%S", StrFromTo(request, 0, (i64)from), Prog_JsonQuote(arena, absolute), StrFrom(request, from + pathNode->raw.length));
        }
        String reply = {0};
        b32 reached = false;
        b32 answered = P_ControlAsk(arena, configDirectory, request, &reply, 60000, &reached);
        String why = {0};
        if (!answered && reached) {
            // One that listens and does not answer is busy, not gone; another is not started for it.
            why = S("Filey did not answer in time.");
        } else if (!answered && !Prog_McpEnabled(arena, configDirectory)) {
            why = S("Filey's MCP access is off. Turn on Agent Access in Filey's Options (Startup and Window), or add `mcp = on` to its Settings.conf.");
        } else if (!answered && mayStart && P_StartSelf()) {
            // A window started now listens once it has read its settings.
            for (u32 tries = 0; tries < 40 && !answered && !reached; ++tries) {
                P_Sleep(250);
                answered = P_ControlAsk(arena, configDirectory, request, &reply, 60000, &reached);
            }
        }
        if (!answered && !why.length) why = reached ? S("Filey did not answer in time.") : S("Filey is not running and could not be started.");
        b32 failed = !answered || (reply.length > 9 && StrIsEqual(StrFromTo(reply, 0, 9), S("{\"error\":")));
        String text = answered ? reply : why;
        result = StrFormat(arena, "{\"content\":[{\"type\":\"text\",\"text\":%S}],\"isError\":%s}", Prog_JsonQuote(arena, text), failed ? "true" : "false");
    } else {
        return StrFormat(arena, "{\"jsonrpc\":\"2.0\",\"id\":%S,\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}", id);
    }
    return StrFormat(arena, "{\"jsonrpc\":\"2.0\",\"id\":%S,\"result\":%S}", id, result);
}

// Serves MCP over the standard input and output, a message a line, until the input ends.
function i32 Prog_McpServe(String configDirectory)
{
    Arena arena = {0};
    for (;;) {
        ArenaClear(&arena);
        String line;
        if (!P_StdinReadLine(&arena, &line)) break;
        if (!Prog__TrimSpaces(line).length) continue;
        String answer = Prog_McpRespond(&arena, configDirectory, line, true);
        if (answer.length) P_StdoutWrite(StrFormat(&arena, "%S\n", answer));
    }
    ArenaRelease(&arena);
    return 0;
}
