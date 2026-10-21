// Office documents as text for the inspector: a Word document's paragraphs and tables, and a spreadsheet's sheets as aligned columns.
// Both are zip archives of XML, read here with a small zip reader over the zlib decoder Models.c builds, so that neither Office nor a
// converter needs to be installed. The old binary .doc and .xls are not read.

#define Prog_OfficeMaxEntry Megabytes(64)
#define Prog_OfficeMaxRows 5000
#define Prog_OfficeMaxColumns 64
#define Prog_OfficeColumnWidth 40

// Little-endian fields of a zip record.
function u32 Prog__Zip16(const u8 *at) { return (u32)at[0] | (u32)at[1] << 8; }
function u32 Prog__Zip32(const u8 *at) { return (u32)at[0] | (u32)at[1] << 8 | (u32)at[2] << 16 | (u32)at[3] << 24; }

// The bytes of `name` inside the zip archive `path` of `size` bytes, from its central directory; empty when it has no such entry or it is
// stored in a way other than plain or deflated.
function String Prog__ZipEntry(Arena *arena, String path, u64 size, String name)
{
    String result = {0};
    Scratch scratch = GetScratchEx(arena);
    // The end of central directory record is in the last 22 bytes, after a comment of up to 64 kB.
    u64 tailSize = Min(size, (u64)65557), tailAt = size - tailSize;
    String tail = {0};
    if (size < 22 || !P_ReadFileAt(scratch.arena, path, tailAt, tailSize, &tail) || tail.length < 22) goto done;
    const u8 *t = (const u8 *)tail.value;
    u64 end = UINT64_MAX;
    for (u64 i = tail.length - 22 + 1; i-- > 0 && end == UINT64_MAX;) if (Prog__Zip32(t + i) == 0x06054B50) end = i;
    if (end == UINT64_MAX) goto done;
    u32 entries = Prog__Zip16(t + end + 10);
    u64 directorySize = Prog__Zip32(t + end + 12), directoryAt = Prog__Zip32(t + end + 16);
    String directory = {0};
    if (!P_ReadFileAt(scratch.arena, path, directoryAt, directorySize, &directory) || directory.length < directorySize) goto done;
    const u8 *d = (const u8 *)directory.value;
    for (u64 at = 0, i = 0; i < entries && at + 46 <= directory.length; ++i) {
        if (Prog__Zip32(d + at) != 0x02014B50) break;
        u32 method = Prog__Zip16(d + at + 10), nameLength = Prog__Zip16(d + at + 28), extra = Prog__Zip16(d + at + 30), comment = Prog__Zip16(d + at + 32);
        u64 packed = Prog__Zip32(d + at + 20), unpacked = Prog__Zip32(d + at + 24), local = Prog__Zip32(d + at + 42);
        String entry = StrMake((const char *)d + at + 46, Min((u64)nameLength, directory.length - at - 46));
        at += 46 + nameLength + extra + comment;
        if (!StrIsEqual(entry, name) || unpacked > Prog_OfficeMaxEntry || (method != 0 && method != 8)) continue;
        // The local header's name and extra field may differ in length from the central directory's.
        String header = {0};
        if (!P_ReadFileAt(scratch.arena, path, local, 30, &header) || header.length < 30) break;
        const u8 *h = (const u8 *)header.value;
        u64 dataAt = local + 30 + Prog__Zip16(h + 26) + Prog__Zip16(h + 28);
        String data = {0};
        if (!P_ReadFileAt(scratch.arena, path, dataAt, packed, &data) || data.length < packed) break;
        if (method == 0) {
            result = StrCopy(arena, data);
        } else {
            char *out = ArenaPushArrayNoZero(arena, Max(unpacked, (u64)1), char);
            i32 made = stbi_zlib_decode_noheader_buffer(out, (i32)unpacked, data.value, (i32)data.length);
            if (made >= 0) result = StrMake(out, (u64)made);
        }
        break;
    }
done:
    ReleaseScratch(scratch);
    return result;
}

// Appends the XML text `text`, with its entities spelled out, to `out`.
function void Prog__XmlText(Arena *arena, StrBuilder *out, String text)
{
    for (u64 i = 0; i < text.length;) {
        u64 amp = i;
        while (amp < text.length && text.value[amp] != '&') ++amp;
        if (amp > i) StrBuildStr(arena, out, StrFromTo(text, i, (i64)amp));
        if (amp >= text.length) break;
        Index semi = StrIndexChar(StrFrom(text, amp), ';', SSF_None);
        String name = semi.exists ? StrFromTo(text, amp + 1, (i64)(amp + semi.value)) : (String){0};
        rune c = StrIsEqual(name, S("amp")) ? '&' : StrIsEqual(name, S("lt")) ? '<' : StrIsEqual(name, S("gt")) ? '>' : StrIsEqual(name, S("quot")) ? '"'
               : StrIsEqual(name, S("apos")) ? '\'' : 0;
        if (!c && name.length > 1 && name.value[0] == '#') {
            b32 hex = name.value[1] == 'x' || name.value[1] == 'X';
            for (u64 k = hex ? 2 : 1; k < name.length; ++k) {
                char d = name.value[k];
                u32 digit = d >= '0' && d <= '9' ? (u32)(d - '0') : hex && (d | 0x20) >= 'a' && (d | 0x20) <= 'f' ? (u32)((d | 0x20) - 'a' + 10) : UINT32_MAX;
                if (digit == UINT32_MAX) { c = 0; break; }
                c = c * (hex ? 16 : 10) + (rune)digit;
            }
        }
        if (c) {
            u8 bytes[4];
            u32 length = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
            if (length == 1) bytes[0] = (u8)c;
            else if (length == 2) bytes[0] = (u8)(0xC0 | c >> 6), bytes[1] = (u8)(0x80 | (c & 0x3F));
            else if (length == 3) bytes[0] = (u8)(0xE0 | c >> 12), bytes[1] = (u8)(0x80 | ((c >> 6) & 0x3F)), bytes[2] = (u8)(0x80 | (c & 0x3F));
            else bytes[0] = (u8)(0xF0 | c >> 18), bytes[1] = (u8)(0x80 | ((c >> 12) & 0x3F)), bytes[2] = (u8)(0x80 | ((c >> 6) & 0x3F)), bytes[3] = (u8)(0x80 | (c & 0x3F));
            StrBuildStr(arena, out, StrMake((const char *)bytes, length));
            i = amp + semi.value + 1;
        } else {
            StrBuildStr(arena, out, S("&"));
            i = amp + 1;
        }
    }
}

// The tag at `xml`'s `at`, which is its '<': its name, as "w:p" or "/w:p", and where it ends, past its '>'.
function String Prog__XmlTag(String xml, u64 at, u64 *end, b32 *closed)
{
    u64 i = at + 1, nameEnd = i;
    while (nameEnd < xml.length && xml.value[nameEnd] != ' ' && xml.value[nameEnd] != '>' && xml.value[nameEnd] != '/' && xml.value[nameEnd] != '\t' &&
           xml.value[nameEnd] != '\n' && xml.value[nameEnd] != '\r') ++nameEnd;
    // A closing tag's name keeps its slash.
    if (nameEnd == i && nameEnd < xml.length && xml.value[nameEnd] == '/') {
        ++nameEnd;
        while (nameEnd < xml.length && xml.value[nameEnd] != '>' && xml.value[nameEnd] != ' ') ++nameEnd;
    }
    u64 close = nameEnd;
    while (close < xml.length && xml.value[close] != '>') ++close;
    *closed = close > at && xml.value[close - 1] == '/';
    *end = Min(close + 1, xml.length);
    return StrFromTo(xml, i, (i64)nameEnd);
}

// The value of the attribute `name` in the tag from `at` to `end`.
function String Prog__XmlAttribute(String xml, u64 at, u64 end, String name)
{
    String tag = StrFromTo(xml, at, (i64)end);
    for (u64 i = 0; i + name.length + 2 < tag.length; ++i) {
        if ((i && tag.value[i - 1] != ' ' && tag.value[i - 1] != '\t' && tag.value[i - 1] != '\n') || !StrIsEqual(StrFromTo(tag, i, (i64)(i + name.length)), name)) continue;
        if (tag.value[i + name.length] != '=' || tag.value[i + name.length + 1] != '"') continue;
        u64 from = i + name.length + 2, to = from;
        while (to < tag.length && tag.value[to] != '"') ++to;
        return StrFromTo(tag, from, (i64)to);
    }
    return (String){0};
}

// A Word document's body: a line a paragraph, with its tabs and breaks, and a table's cells parted by " | ", a line a row.
function String Prog__DocxText(Arena *arena, String xml)
{
    StrBuilder out = {0};
    u32 cells = 0;
    b32 rowText = false;
    u64 cellFrom = 0, cellText = 0;
    b32 inText = false;
    for (u64 i = 0; i < xml.length;) {
        if (xml.value[i] != '<') {
            u64 next = i;
            while (next < xml.length && xml.value[next] != '<') ++next;
            if (inText) Prog__XmlText(arena, &out, StrFromTo(xml, i, (i64)next));
            i = next;
            continue;
        }
        u64 end;
        b32 closed;
        String tag = Prog__XmlTag(xml, i, &end, &closed);
        if (StrIsEqual(tag, S("w:t"))) inText = !closed;
        else if (StrIsEqual(tag, S("/w:t"))) inText = false;
        else if (StrIsEqual(tag, S("w:tab"))) StrBuildStr(arena, &out, S("\t"));
        else if (StrIsEqual(tag, S("w:br")) || StrIsEqual(tag, S("w:cr"))) StrBuildStr(arena, &out, cells ? S(" ") : S("\n"));
        else if (StrIsEqual(tag, S("/w:p")) || (StrIsEqual(tag, S("w:p")) && closed)) StrBuildStr(arena, &out, cells ? S(" ") : S("\n"));
        else if (StrIsEqual(tag, S("w:tc")) && !closed) {
            cellFrom = out.buffer.length;
            if (rowText) StrBuildStr(arena, &out, S(" | "));
            cellText = out.buffer.length;
            ++cells;
        } else if (StrIsEqual(tag, S("/w:tc"))) {
            if (cells) --cells;
            // A cell with nothing but spaces takes back its separator, and the spaces its paragraphs ended in go.
            u64 end = out.buffer.length;
            while (end > cellText && out.buffer.value[end - 1] == ' ') --end;
            if (end == cellText) out.buffer.length = cellFrom;
            else out.buffer.length = end, rowText = true;
        } else if (StrIsEqual(tag, S("/w:tr"))) {
            StrBuildStr(arena, &out, S("\n"));
            rowText = false;
        }
        i = end;
    }
    return out.buffer;
}

// The column of a cell reference such as "AB12", from 0.
function u32 Prog__CellColumn(String reference)
{
    u32 result = 0;
    for (u64 i = 0; i < reference.length && reference.value[i] >= 'A' && reference.value[i] <= 'Z'; ++i) result = result * 26 + (u32)(reference.value[i] - 'A' + 1);
    return result ? result - 1 : 0;
}

typedef struct Prog__Cell { u32 row, column; String text; } Prog__Cell;
typedef struct Prog__ArrayCell { ArrayHeader header; Prog__Cell *v; } Prog__ArrayCell;

// Appends a sheet's cells to `out` as rows of columns padded to the widest of each, up to a width; numbers show as they are stored, so dates
// show as their day counts.
function void Prog__SheetText(Arena *arena, StrBuilder *out, String xml, const ArrayStr *shared)
{
    Scratch scratch = GetScratchEx(arena);
    Prog__ArrayCell cells = {0};
    u32 row = 0, columns = 0, rows = 0;
    for (u64 i = 0; i < xml.length;) {
        if (xml.value[i] != '<') { ++i; continue; }
        u64 end;
        b32 closed;
        String tag = Prog__XmlTag(xml, i, &end, &closed);
        if (StrIsEqual(tag, S("row"))) {
            String r = Prog__XmlAttribute(xml, i, end, S("r"));
            u64 number = 0;
            for (u64 k = 0; k < r.length && r.value[k] >= '0' && r.value[k] <= '9'; ++k) number = number * 10 + (u64)(r.value[k] - '0');
            row = number ? (u32)number - 1 : row + 1;
        } else if (StrIsEqual(tag, S("c")) && !closed) {
            u32 column = Prog__CellColumn(Prog__XmlAttribute(xml, i, end, S("r")));
            String type = Prog__XmlAttribute(xml, i, end, S("t"));
            // Its value or inline string, up to the cell's end.
            Index stop = StrIndexStr(StrFrom(xml, end), S("</c>"), SSF_None);
            String body = stop.exists ? StrFromTo(xml, end, (i64)(end + stop.value)) : (String){0};
            StrBuilder text = {0};
            for (u64 k = 0; k < body.length;) {
                if (body.value[k] != '<') { ++k; continue; }
                u64 tagEnd;
                b32 tagClosed;
                String inner = Prog__XmlTag(body, k, &tagEnd, &tagClosed);
                if ((StrIsEqual(inner, S("v")) || StrIsEqual(inner, S("t"))) && !tagClosed) {
                    Index valueEnd = StrIndexChar(StrFrom(body, tagEnd), '<', SSF_CountOnFail);
                    Prog__XmlText(scratch.arena, &text, StrFromTo(body, tagEnd, (i64)(tagEnd + valueEnd.value)));
                }
                k = tagEnd;
            }
            String value = text.buffer;
            if (StrIsEqual(type, S("s"))) {
                u64 index = 0;
                for (u64 k = 0; k < value.length && value.value[k] >= '0' && value.value[k] <= '9'; ++k) index = index * 10 + (u64)(value.value[k] - '0');
                value = index < shared->header.count ? shared->v[index] : (String){0};
            } else if (StrIsEqual(type, S("b"))) {
                value = StrIsEqual(value, S("1")) ? S("TRUE") : S("FALSE");
            }
            if (value.length && row < Prog_OfficeMaxRows && column < Prog_OfficeMaxColumns) {
                ArrayPush(scratch.arena, cells, ((Prog__Cell){ row, column, value }));
                columns = Max(columns, column + 1), rows = Max(rows, row + 1);
            }
            if (stop.exists) end += stop.value + 4;
        }
        i = end;
    }
    // Each column as wide as its widest cell, in letters.
    u32 *widths = ArenaPushArray(scratch.arena, Max(columns, 1u), u32);
    for (u64 c = 0; c < cells.header.count; ++c) {
        u32 letters = 0;
        for (u64 k = 0; k < cells.v[c].text.length;) { StrDecodeUtf8(cells.v[c].text, &k); ++letters; }
        widths[cells.v[c].column] = Min(Max(widths[cells.v[c].column], letters), (u32)Prog_OfficeColumnWidth);
    }
    // The cells come in row order; a row's gaps and the rows with nothing are left blank.
    u32 shownRow = 0;
    for (u64 c = 0; c < cells.header.count;) {
        u32 thisRow = cells.v[c].row;
        for (; shownRow < thisRow; ++shownRow) StrBuildStr(arena, out, S("\n"));
        u32 column = 0;
        for (; c < cells.header.count && cells.v[c].row == thisRow; ++c) {
            for (; column < cells.v[c].column; ++column) for (u32 k = 0; k < widths[column] + 2; ++k) StrBuildStr(arena, out, S(" "));
            String text = cells.v[c].text;
            // A line break in a cell would start a new row.
            u32 letters = 0;
            u64 cut = 0;
            for (; cut < text.length && letters < Prog_OfficeColumnWidth;) {
                u64 next = cut;
                StrDecodeUtf8(text, &next);
                if (text.value[cut] == '\n' || text.value[cut] == '\r') break;
                cut = next, ++letters;
            }
            StrBuildStr(arena, out, StrFromTo(text, 0, (i64)cut));
            for (u32 k = letters; k < widths[column] + 2; ++k) StrBuildStr(arena, out, S(" "));
            ++column;
        }
        StrBuildStr(arena, out, S("\n"));
        ++shownRow;
    }
    (void)rows;
    ReleaseScratch(scratch);
}

// A spreadsheet's sheets one after another, each under its name.
function String Prog__XlsxText(Arena *arena, String path, u64 size)
{
    Scratch scratch = GetScratchEx(arena);
    ArrayStr shared = {0};
    String strings = Prog__ZipEntry(scratch.arena, path, size, S("xl/sharedStrings.xml"));
    for (u64 i = 0; i < strings.length;) {
        Index si = StrIndexStr(StrFrom(strings, i), S("<si>"), SSF_None);
        if (!si.exists) break;
        u64 from = i + si.value + 4;
        Index stop = StrIndexStr(StrFrom(strings, from), S("</si>"), SSF_CountOnFail);
        String item = StrFromTo(strings, from, (i64)(from + stop.value));
        StrBuilder text = {0};
        // Its <t> runs, leaving out the phonetic guides of <rPh>.
        b32 guide = false;
        for (u64 k = 0; k < item.length;) {
            if (item.value[k] != '<') { ++k; continue; }
            u64 end;
            b32 closed;
            String tag = Prog__XmlTag(item, k, &end, &closed);
            if (StrIsEqual(tag, S("rPh"))) guide = !closed;
            else if (StrIsEqual(tag, S("/rPh"))) guide = false;
            else if (StrIsEqual(tag, S("t")) && !closed && !guide) {
                Index valueEnd = StrIndexChar(StrFrom(item, end), '<', SSF_CountOnFail);
                Prog__XmlText(scratch.arena, &text, StrFromTo(item, end, (i64)(end + valueEnd.value)));
            }
            k = end;
        }
        ArrayPush(scratch.arena, shared, text.buffer);
        i = from + stop.value;
    }
    // ponytail: the sheets are taken as sheet1.xml, sheet2.xml and on in the workbook's order, which is how Excel writes them; reading
    // xl/_rels/workbook.xml.rels would place files written otherwise.
    String workbook = Prog__ZipEntry(scratch.arena, path, size, S("xl/workbook.xml"));
    StrBuilder out = {0};
    u32 sheet = 0;
    for (u64 i = 0; i < workbook.length;) {
        Index at = StrIndexStr(StrFrom(workbook, i), S("<sheet "), SSF_None);
        if (!at.exists) break;
        u64 from = i + at.value;
        Index close = StrIndexChar(StrFrom(workbook, from), '>', SSF_CountOnFail);
        StrBuilder name = {0};
        Prog__XmlText(scratch.arena, &name, Prog__XmlAttribute(workbook, from, from + close.value, S("name")));
        String xml = Prog__ZipEntry(scratch.arena, path, size, StrFormat(scratch.arena, "xl/worksheets/sheet%u.xml", ++sheet));
        if (out.buffer.length) StrBuildStr(arena, &out, S("\n"));
        StrBuildStr(arena, &out, StrFormat(scratch.arena, "%S\n\n", name.buffer));
        Prog__SheetText(arena, &out, xml, &shared);
        i = from + close.value;
    }
    String result = out.buffer;
    ReleaseScratch(scratch);
    return result;
}

function b32 Prog_IsOffice(String path)
{
    return path.length >= 5 && (Prog__EqualFolded(StrFrom(path, path.length - 5), S(".docx")) || Prog__EqualFolded(StrFrom(path, path.length - 5), S(".xlsx")) ||
                                Prog__EqualFolded(StrFrom(path, path.length - 5), S(".docm")) || Prog__EqualFolded(StrFrom(path, path.length - 5), S(".xlsm")));
}

function String Prog_OfficeText(Arena *arena, String path, u64 size)
{
    b32 word = Prog__EqualFolded(StrFrom(path, path.length - 5), S(".docx")) || Prog__EqualFolded(StrFrom(path, path.length - 5), S(".docm"));
    if (!word) return Prog__XlsxText(arena, path, size);
    Scratch scratch = GetScratchEx(arena);
    String result = Prog__DocxText(arena, Prog__ZipEntry(scratch.arena, path, size, S("word/document.xml")));
    ReleaseScratch(scratch);
    return result;
}
