// File icons from the freedesktop icon theme.

function String P_FileIconKey(Arena *arena, String path, P_FileKind kind)
{
    if (kind == P_FileKind_Directory) {
        return S("/folder");
    }
    Index slash = StrIndexChar(path, '/', SSF_SearchBackwards | SSF_IndexPlusOne | SSF_CountOnFail);
    String name = StrFrom(path, slash.exists ? slash.value : 0);
    Index dot = StrIndexChar(name, '.', SSF_SearchBackwards);
    if (!dot.exists || dot.value == 0 || dot.value + 1 == name.length) {
        return S("/file");
    }
    String extension = StrFormat(arena, ".%S", StrFrom(name, dot.value + 1));
    for (u64 i = 0; i < extension.length; ++i) {
        char c = extension.value[i];
        if (c >= 'A' && c <= 'Z') ((char *)extension.value)[i] = (char)(c + ('a' - 'A'));
    }
    return extension;
}

function b32 P_LoadFileIcon(Arena *arena, String path, P_FileKind kind, i32 size, P_Image *image)
{
    return false;
}
