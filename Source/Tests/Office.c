// Office text: a Word document's paragraphs, tabs, breaks, tables and entities, and a spreadsheet's cells as padded columns.
function void TestOffice(void)
{
    Arena arena = {0};
    String doc = Prog__DocxText(&arena, S("<w:document><w:body><w:p><w:r><w:t xml:space=\"preserve\">Sales &amp; </w:t></w:r><w:r><w:t>1&#8364;2&#x20AC;</w:t></w:r></w:p>"
                                          "<w:p><w:r><w:t>A</w:t></w:r><w:r><w:tab/></w:r><w:r><w:t>B</w:t></w:r><w:r><w:br/></w:r><w:r><w:t>C</w:t></w:r></w:p>"
                                          "<w:tbl><w:tr><w:tc><w:p><w:r><w:t>x</w:t></w:r></w:p></w:tc><w:tc><w:p/></w:tc><w:tc><w:p><w:r><w:t>y</w:t></w:r></w:p></w:tc></w:tr>"
                                          "<w:tr><w:tc><w:p/></w:tc><w:tc><w:p><w:r><w:t>z</w:t></w:r></w:p></w:tc></w:tr></w:tbl>"
                                          "<w:p/></w:body></w:document>"));
    Check(StrIsEqual(doc, S("Sales & 1\xE2\x82\xAC" "2\xE2\x82\xAC\nA\tB\nC\nx | y\nz\n\n")));
    ArrayStr shared = {0};
    ArrayPush(&arena, shared, S("Item"));
    ArrayPush(&arena, shared, S("Apple pie"));
    StrBuilder sheet = {0};
    Prog__SheetText(&arena, &sheet, S("<sheetData><row r=\"1\"><c r=\"A1\" t=\"s\"><v>0</v></c><c r=\"C1\"><v>4.5</v></c></row>"
                                      "<row r=\"3\"><c r=\"A3\" t=\"s\"><v>1</v></c><c r=\"B3\" t=\"b\"><v>1</v></c><c r=\"C3\" t=\"inlineStr\"><is><t>x&lt;y</t></is></c></row></sheetData>"),
                    &shared);
    Check(StrIsEqual(sheet.buffer, S("Item             4.5  \n\nApple pie  TRUE  x<y  \n")));
    Check(Prog__CellColumn(S("A1")) == 0 && Prog__CellColumn(S("AB12")) == 27 && Prog_IsOffice(S("C:/a/Report.DOCX")) && !Prog_IsOffice(S("/a/b.doc")));
    ArenaRelease(&arena);
}
