#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "compiler.c"

typedef CyString16View String16;

typedef struct {
    CyString16 lines;
    CyString16 members;
} GlobalBufs;
static GlobalBufs g_bufs;

typedef struct {
    CyAllocator page;
} GlobalAllocators;
static GlobalAllocators g_allocs;

typedef struct {
    i32 first_visible_line;
    i32 last_visible_line;
    i32 min_x;
    i32 min_y;
    isize splitter_top;
    isize elapsed_ms;
    HCURSOR resize_cursor;
    HANDLE file;
    b8 scratch_file;
    b8 resizing_splitter;
    u16 char_height;
} GlobalState;
static GlobalState g_state = {
    .scratch_file = true
};

#define PAGE_SIZE CY_PAGE_SIZE

#define ASSERT(cond) CY_ASSERT(cond)
#define STATIC_ASSERT(cond) CY_STATIC_ASSERT(cond)
#define UTF16_STATIC_LENGTH(str) CY_STATIC_STR_LEN(str)

static u16 *Win32BuildErrorMessage(const u16 *preface)
{
    u16 *error_msg = NULL;
    FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        GetLastError(),
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPWSTR)&error_msg,
        0, NULL
    );
    if (error_msg == NULL) {
        ExitProcess(GetLastError());
    }

    CyString16 final_msg = cy_string_16_create_reserve(g_allocs.page, 0x100);
    if (final_msg == NULL) {
        ExitProcess(GetLastError());
    }

    final_msg = cy_string_16_append_fmt(
        final_msg, L"%ls: %ls", preface, error_msg
    );

    LocalFree(error_msg);
    return cy_string_16_shrink(final_msg);
}

static inline void Win32ErrorDialog(const u16 *msg)
{
    CyString16 final_msg = Win32BuildErrorMessage(msg);
    MessageBoxW(NULL, final_msg, L"Erro", MB_OK | MB_ICONWARNING);
    cy_string_16_free(final_msg);
}

static inline void Win32FatalErrorDialog(const u16 *msg)
{
    u16 *final_msg = Win32BuildErrorMessage(msg);
    MessageBoxW(NULL, final_msg, L"Erro fatal", MB_OK | MB_ICONERROR);
    ExitProcess(GetLastError());
}

#define WIN32_STRIP_WINDOW_THEME(window) SetWindowTheme(window, L"", L"")
#define WIN32_FILENAME_ERROR_DIALOG() \
    Win32ErrorDialog(L"Erro ao coletar caminho para o arquivo")
#define WIN32_FATAL_MEM_ERROR_DIALOG() \
    Win32FatalErrorDialog(L"Erro ao alocar memória")

static CyString16 Win32UTF8toUTF16(CyString str)
{
    CY_VALIDATE_PTR(str);

    CyAllocator a = CY_STRING_HEADER(str)->alloc;
    isize len = cy_string_len(str);
    isize size_utf16 = MultiByteToWideChar(
        CP_UTF8, 0,
        str, len + 1,
        NULL, 0
    );
    isize len_utf16 = size_utf16 - 1;
    CyString16 str_utf16 = cy_string_16_create_reserve(a, size_utf16);
    CY_VALIDATE_PTR(str_utf16);

    isize res = MultiByteToWideChar(
        CP_UTF8, 0,
        str, len + 1,
        str_utf16, size_utf16 + 1
    );
    if (res == 0) {
        Win32ErrorDialog(L"Erro ao converter texto para UTF-16");
        cy_string_16_free(str_utf16);
        return NULL;
    }

    cy__string_16_set_len(str_utf16, len_utf16);
    return str_utf16;
}

static CyString Win32UTF16toUTF8(CyString16 str)
{
    CY_VALIDATE_PTR(str);

    CyAllocator a = CY_STRING_HEADER(str)->alloc;
    isize len = cy_string_16_len(str);
    isize size_utf8 = WideCharToMultiByte(
        CP_UTF8, 0,
        str, len + 1,
        NULL, 0,
        NULL, NULL
    );
    isize len_utf8 = size_utf8 - 1;
    CyString str_utf8 = cy_string_create_reserve(a, len_utf8);
    CY_VALIDATE_PTR(str_utf8);

    isize res = WideCharToMultiByte(
        CP_UTF8, 0,
        str, len + 1,
        str_utf8, size_utf8,
        NULL, NULL
    );
    if (res == 0) {
        Win32ErrorDialog(L"Erro ao converter texto para UTF-8");
        cy_string_free(str_utf8);
        return NULL;
    }

    cy__string_set_len(str_utf8, len_utf8);
    return str_utf8;
}

static void Win32AppendExtension(
    u16 *path_buf, isize ext_idx, isize cap, const u16 *ext
) {
    const isize ext_len = UTF16_STATIC_LENGTH(ext);
    const isize new_len = cy_wcs_len(path_buf) + ext_len;

    ASSERT(cap > new_len);

    u16 *end = path_buf + ext_idx;
    if (ext_idx > 0 && *(end - 1) != '.') {
        *(end++) = '.';
    }

    CopyMemory(end, ext, CY__U16S_TO_BYTES(ext_len));
}

static void utf16_from_int(isize n, isize max_digits, u16 *buf, isize cap)
{
    isize dividend = 10;
    isize digits = 1;
    while (n % dividend != n) {
        dividend *= 10;
        digits += 1;
    }

    while (digits > max_digits) {
        dividend /= 10;
        n %= dividend;
        digits -= 1;
    }

    for (isize i = 0; i < digits && i < cap; i++) {
        dividend /= 10;
        buf[i] = L'0' + n / dividend;
        n %= dividend;
    }
}

static void utf16_insert_dots(u16 *str, b32 null_terminate)
{
    enum {
        MIN_CAP = 3,
    };

    CY_ASSERT_NOT_NULL(str);

#if defined(CY_ARCH_64_BIT) // 64-bit write of "..."
    u16 buf[] = L"...";
    buf[3] = str[3] * !null_terminate;
    *((UINT64*)str) = *((UINT64*)buf);
#else
    str[0] = '.';
    str[1] = '.';
    str[2] = '.';
    if (null_terminate) {
        str[3] = '\0';
    }
#endif
}

// NOTE(cya): dumb UTF-16 parser for now (just considers characters coming
// from the surrogate range to be 2 cells wide and all others to be 1 cell wide)
// FIXME(cya): doesn't really parse the surrogates correctly yet
static void utf16_trim_start(u16 *str, isize len, isize max_cells) {
    enum {
        MIN_CELLS = 3,
    };

    CY_ASSERT_NOT_NULL(str);

    if (max_cells < MIN_CELLS) {
        utf16_insert_dots(str, true);
        return;
    }

    // TODO(cya): port wcswidth code to here so we can get a way more accurate
    // estimated cell width when calculating the length of the string
    isize offset = 0, cells = 0;
    for (offset = len - 1; (offset >= 0) && (cells < max_cells); cells++) {
        b32 is_surrogate = str[offset] >= 0xD800 && str[offset] <= 0xDFFF;
        if (!is_surrogate || offset < 2) {
            offset -= 1;
            continue;
        }

        cells += 1;
        offset -= 2;
    }

    offset += 1;

    if (cells < MIN_CELLS) {
        utf16_insert_dots(str, true);
        return;
    }

    isize new_len = len - offset;
    if (offset > 0) {
        isize bytes_to_move = CY__U16S_TO_BYTES(new_len);
        MoveMemory(str, str + offset, bytes_to_move);

        isize bytes_to_clear = CY__U16S_TO_BYTES(len - new_len);
        FillMemory(str + new_len, bytes_to_clear, 0);

        utf16_insert_dots(str, false);
    }
}

typedef struct {
    HWND toolbar;
    HWND line_numbers;
    HWND text_editor;
    HWND log_area;
    HWND statusbar;
} GlobalControls; // CommonControls (GUI components)
static GlobalControls g_controls;

typedef struct {
    isize width;
    isize height;
} Win32ClientDimensions;
static Win32ClientDimensions g_client;

static Win32ClientDimensions Win32GetClientDimensions(HWND window)
{
    RECT client_rect;
    GetClientRect(window, &client_rect);
    return (Win32ClientDimensions){
        .width = client_rect.right - client_rect.left,
        .height = client_rect.bottom - client_rect.top
    };
}

typedef enum {
    BUTTON_FILE_NEW,
    BUTTON_FILE_OPEN,
    BUTTON_FILE_SAVE,
    BUTTON_TEXT_COPY,
    BUTTON_TEXT_PASTE,
    BUTTON_TEXT_CUT,
    BUTTON_COMPILE,
    BUTTON_DISPLAY_GROUP,
    BUTTON_COUNT,
} ToolbarButtons;

STATIC_ASSERT(BUTTON_COUNT == 8);

#define MAX_LINE_DIGITS 3
#define PADDING_PX 4
#define TEXT_AREA_MIN_HEIGHT 100
#define SCROLLBAR_SIZE (20 - 3)
#define WINDOW_WIDTH 910
#define WINDOW_HEIGHT 600
#define TOOLBAR_HEIGHT 70
#define LINE_NUMBER_WIDTH 30
#define STATUSBAR_HEIGHT 25
#define LOG_AREA_HEIGHT 140
#define SPLITTER_HEIGHT 6
#define SCRATCH_FILE_TEXT L"[novo]"

LRESULT CALLBACK Win32TextEditorCallback(
    HWND control,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR subclass_id,
    DWORD_PTR ref_data
);

#define MAKE_BUTTON(bitmap_id, label, shortcut, cmd) (TBBUTTON){ \
    MAKELONG(bitmap_id, TOOLBAR_ICON_LIST_ID),                   \
    cmd, TBSTATE_ENABLED, BTNS_BUTTON, {0}, 0,                   \
    (INT_PTR)(label " [" shortcut "]")                           \
}

#define WIN32_Y_FROM_CHAR(control, _char) \
    (HIWORD(SendMessageW(control, EM_POSFROMCHAR, _char, 0)))

static void Win32SetupControls(HWND parent)
{
    g_client = Win32GetClientDimensions(parent);

    enum {
        WS_DEFAULT = WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
        WS_SCROLLABLE = WS_DEFAULT | WS_BORDER | WS_VSCROLL | WS_HSCROLL,
        WS_TEXT_AREA =
            WS_SCROLLABLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
    };

    g_controls.toolbar = CreateWindowExW(
        WS_EX_COMPOSITED,
        TOOLBARCLASSNAMEW,
        NULL,
        WS_CHILD | TBSTYLE_WRAPABLE,
        0, 0, 0, 0,
        parent,
        NULL,
        NULL,
        NULL
    );

    enum {
        TOOLBAR_ICON_LIST_ID = 0,
        TOOLBAR_ICON_SIZE_PX = 16,
    };

    HIMAGELIST toolbar_icon_list;
    toolbar_icon_list = ImageList_Create(
        TOOLBAR_ICON_SIZE_PX, TOOLBAR_ICON_SIZE_PX,
        ILC_COLOR16 |  ILC_MASK,
        BUTTON_COUNT,
        0
    );
    SendMessageW(
        g_controls.toolbar, TB_SETIMAGELIST,
        (WPARAM)TOOLBAR_ICON_LIST_ID, (LPARAM)toolbar_icon_list
    );
    SendMessageW(
        g_controls.toolbar, TB_LOADIMAGES,
        (WPARAM)IDB_STD_SMALL_COLOR, (LPARAM)HINST_COMMCTRL
    );

    TBBUTTON toolbar_buttons[BUTTON_COUNT] =  {
        MAKE_BUTTON(STD_FILENEW, L"Novo", L"Ctrl-N", BUTTON_FILE_NEW),
        MAKE_BUTTON(STD_FILEOPEN, L"Abrir", L"Ctrl-O", BUTTON_FILE_OPEN),
        MAKE_BUTTON(STD_FILESAVE, L"Salvar", L"Ctrl-S", BUTTON_FILE_SAVE),
        MAKE_BUTTON(STD_COPY, L"Copiar", L"Ctrl-C", BUTTON_TEXT_COPY),
        MAKE_BUTTON(STD_PASTE, L"Colar", L"Ctrl-V", BUTTON_TEXT_PASTE),
        MAKE_BUTTON(STD_CUT, L"Recortar", L"Ctrl-X", BUTTON_TEXT_CUT),
        MAKE_BUTTON(STD_PRINT, L"Compilar", L"F7", BUTTON_COMPILE),
        MAKE_BUTTON(STD_PROPERTIES, L"Equipe", L"F1", BUTTON_DISPLAY_GROUP),
    };

    SendMessageW(g_controls.toolbar, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(
        g_controls.toolbar, TB_ADDBUTTONS,
        BUTTON_COUNT, (LPARAM)&toolbar_buttons
    );
    SendMessageW(g_controls.toolbar, TB_SETINDENT, PADDING_PX, 0);

    int button_width =
        (g_client.width - LINE_NUMBER_WIDTH - 16) / BUTTON_COUNT;
    int button_height = TOOLBAR_HEIGHT - PADDING_PX * 2;
    SendMessageW(
        g_controls.toolbar, TB_SETBUTTONSIZE,
        0, MAKELPARAM(button_width, button_height)
    );
    SendMessageW(
        g_controls.toolbar, TB_SETPADDING,
        0, MAKELPARAM(SCROLLBAR_SIZE, 24)
    );
    SendMessageW(g_controls.toolbar, TB_AUTOSIZE, 0, 0);
    ShowWindow(g_controls.toolbar, TRUE);

    g_controls.line_numbers = CreateWindowExW(
        WS_EX_COMPOSITED,
        WC_STATICW,
        NULL,
        WS_DEFAULT | SS_RIGHT | SS_EDITCONTROL,
        0, 0, 0, 0,
        parent,
        NULL,
        NULL,
        NULL
    );

    isize editor_height = g_client.height - TOOLBAR_HEIGHT -
        STATUSBAR_HEIGHT - LOG_AREA_HEIGHT - SPLITTER_HEIGHT;
    g_controls.text_editor = CreateWindowExW(
        WS_EX_COMPOSITED,
        WC_EDITW,
        L"@\r\n@",
        WS_TEXT_AREA | ES_WANTRETURN | WS_CLIPSIBLINGS,
        0, 0, 0, editor_height,
        parent,
        NULL,
        NULL,
        NULL
    );
    SetWindowSubclass(
        g_controls.text_editor, Win32TextEditorCallback,
        0, (DWORD_PTR)parent
    );

    const isize EDIT_CHAR_LEN = 4;
    const isize tab_width = 4 * EDIT_CHAR_LEN;
    Edit_SetTabStops(g_controls.text_editor, 1, &tab_width);

    {
        i32 first_y = WIN32_Y_FROM_CHAR(g_controls.text_editor, 0);
        i32 second_y = WIN32_Y_FROM_CHAR(g_controls.text_editor, 3);

        g_state.char_height = second_y - first_y + 1;

        SetWindowPos(
            g_controls.line_numbers, HWND_TOP,
            0, 2 + first_y + TOOLBAR_HEIGHT, 0, 0,
            SWP_NOSIZE
        );
        Edit_SetText(g_controls.text_editor, NULL);
    }

    g_controls.log_area = CreateWindowExW(
        WS_EX_COMPOSITED,
        WC_EDITW,
        NULL,
        WS_TEXT_AREA | ES_READONLY,
        0, 0,
        0, LOG_AREA_HEIGHT,
        parent,
        NULL,
        NULL,
        NULL
    );
    Edit_SetTabStops(g_controls.log_area, 1, &tab_width);

    g_controls.statusbar = CreateWindowExW(
        WS_EX_COMPOSITED,
        STATUSCLASSNAMEW,
        SCRATCH_FILE_TEXT,
        WS_DEFAULT | SBARS_SIZEGRIP,
        0, 0, 0, 0,
        parent,
        NULL,
        NULL,
        NULL
    );

    LOGFONTW var_log_font = {
        .lfHeight = 13,
        .lfWidth = 5,
        .lfWeight = FW_NORMAL,
        .lfCharSet = DEFAULT_CHARSET,
        .lfOutPrecision = OUT_TT_PRECIS,
        .lfClipPrecision = CLIP_DEFAULT_PRECIS,
        .lfQuality = ANTIALIASED_QUALITY,
        .lfPitchAndFamily = DEFAULT_PITCH | FF_MODERN,
        .lfFaceName = L"Verdana"
    };
    HFONT var_font = CreateFontIndirectW(&var_log_font);
    SendMessageW(parent, WM_SETFONT, (WPARAM)var_font, TRUE);
    SendMessageW(g_controls.toolbar, WM_SETFONT, (WPARAM)var_font, TRUE);
    SendMessageW(g_controls.statusbar, WM_SETFONT, (WPARAM)var_font, TRUE);

    LOGFONTW mono_log_font = {
        .lfHeight = 16,
        .lfWeight = FW_NORMAL,
        .lfCharSet = DEFAULT_CHARSET,
        .lfOutPrecision = OUT_TT_PRECIS,
        .lfClipPrecision = CLIP_DEFAULT_PRECIS,
        .lfQuality = ANTIALIASED_QUALITY,
        .lfPitchAndFamily = DEFAULT_PITCH | FF_MODERN,
        .lfFaceName = L"Courier New"
    };
    HFONT mono_font = CreateFontIndirectW(&mono_log_font);
    SendMessageW(g_controls.line_numbers, WM_SETFONT, (WPARAM)mono_font, TRUE);
    SendMessageW(g_controls.text_editor, WM_SETFONT, (WPARAM)mono_font, TRUE);
    SendMessageW(g_controls.log_area, WM_SETFONT, (WPARAM)mono_font, TRUE);

    WIN32_STRIP_WINDOW_THEME(parent);
    WIN32_STRIP_WINDOW_THEME(g_controls.toolbar);
    WIN32_STRIP_WINDOW_THEME(g_controls.line_numbers);
    WIN32_STRIP_WINDOW_THEME(g_controls.text_editor);
    WIN32_STRIP_WINDOW_THEME(g_controls.log_area);
    WIN32_STRIP_WINDOW_THEME(g_controls.statusbar);
}

typedef enum {
    LE_CR,
    LE_LF,
    LE_CRLF,
    LE_OTHER = -1,
} LineEnding;

static inline LineEnding line_ending_identify(const u16 *str)
{
    u16 c = *str;
    switch (c) {
    case '\r': {
        u16 next = *(str + 1);
        if (next != '\0' && next == '\n') {
            return LE_CRLF;
        }

        return LE_CR;
    } break;
    case '\n': {
        return LE_LF;
    } break;
    default: {
        return LE_OTHER;
    } break;
    }
}

typedef struct {
    String16 string;
    isize cur_line_offset;
    isize crlf_len;
} LineScanner;

static LineScanner line_scanner_build(String16 str)
{
    isize offset = 0, crlf_len = 0;
    const u16 *end = str.text;
    while (offset < str.len) {
        switch (line_ending_identify(end)) {
        case LE_CRLF: {
            offset += 2;
            crlf_len += 2;
        } break;
        default: {
            offset += 1;
            crlf_len += 1;
        } break;
        }

        end = str.text + offset;
    }

    return (LineScanner){
        .string = str,
        .crlf_len = crlf_len,
    };
}

static String16 line_scanner_get_line(LineScanner *scanner)
{
    const u16 *buf = scanner->string.text;
    isize offset = scanner->cur_line_offset;
    isize len = scanner->string.len;
    if (offset >= len) {
        return (String16){0};
    }

    const u16 *line = buf + offset;
    const u16 *curr = line;
    isize line_len = 0;
    while (offset < len) {
        LineEnding end = line_ending_identify(curr);
        if (end != LE_OTHER) {
            offset += end == LE_CRLF ? 2 : 1;
            break;
        }

        line_len += 1;
        curr = buf + ++offset;
    }

    scanner->cur_line_offset = offset;
    return (String16){
        .text = line,
        .len = line_len,
    };
}

static CyString16 cy_string_16_convert_newlines(CyAllocator a, String16 src)
{
    LineScanner scanner = line_scanner_build(src);
    CyString16 res = cy_string_16_create_reserve(a, scanner.crlf_len);
    String16 line = line_scanner_get_line(&scanner);
    res = cy_string_16_append_view(res, line);
    while (line.text != NULL) {
        line = line_scanner_get_line(&scanner);
        res = cy_string_16_append_fmt(res, L"\r\n%.*ls", line.len, line.text);
    }

    return res;
}

static void Win32UpdateLineNumbers(void)
{
    isize first_visible_line = 1 + Edit_GetFirstVisibleLine(
        g_controls.text_editor
    );

    RECT text_rect = {0};
    SendMessageW(g_controls.text_editor, EM_GETRECT, 0, (LPARAM)&text_rect);
    LPARAM bottom = MAKELPARAM(0, text_rect.bottom - 1);
    LRESULT char_from_pos = SendMessageW(
        g_controls.text_editor, EM_CHARFROMPOS, 0, bottom
    );
    isize last_visible_line = 1 + HIWORD(char_from_pos);
    b32 has_changed = !(first_visible_line == g_state.first_visible_line &&
        last_visible_line == g_state.last_visible_line);
    if (!has_changed) {
        return;
    }

    // NOTE(cya): since CHAR_FROM_POS is a WORD (16-bit)
    if (first_visible_line > last_visible_line) {
        first_visible_line %= (U16_MAX + 1);
    }

    g_state.first_visible_line = first_visible_line;
    g_state.last_visible_line = last_visible_line;

    isize line_len = MAX_LINE_DIGITS + 2;
    isize len = line_len * (last_visible_line - first_visible_line + 1);

    cy_string_16_clear(g_bufs.lines);
    cy_string_16_resize(g_bufs.lines, len);

    u16 num_buf[MAX_LINE_DIGITS + 1] = {0};
    CyString16 buf = g_bufs.lines;
    for (isize i = first_visible_line; i <= last_visible_line; i++) {
        utf16_from_int(i, MAX_LINE_DIGITS, num_buf, CY_STATIC_ARR_LEN(num_buf));
        buf = cy_string_16_append_fmt(buf, L"%ls\r\n", num_buf);
    }

    SetWindowTextW(g_controls.line_numbers, buf);
}

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

#define REDRAW_FLAGS RDW_ERASE | RDW_FRAME | \
    RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW
#define SWP_FLAGS \
    SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOZORDER | SWP_NOREPOSITION

static void Win32ResizeTextAreas(HWND parent, isize splitter_top)
{
    isize height = g_client.height;

    isize edit_height = splitter_top - TOOLBAR_HEIGHT;
    isize edit_x = LINE_NUMBER_WIDTH + PADDING_PX;
    isize edit_width = g_client.width - edit_x - PADDING_PX;
    isize edit_y = splitter_top - edit_height;

    isize log_x = PADDING_PX;
    isize log_y = splitter_top + SPLITTER_HEIGHT;
    isize log_width = edit_width + LINE_NUMBER_WIDTH;
    isize log_height = (height - STATUSBAR_HEIGHT) -
        (splitter_top + SPLITTER_HEIGHT);

    isize min_height = TEXT_AREA_MIN_HEIGHT;
    b32 out_of_bounds = (edit_height - SCROLLBAR_SIZE < min_height) ||
        (log_height - SCROLLBAR_SIZE < min_height);
    if (out_of_bounds) {
        return;
    }

    ASSERT(
        height == TOOLBAR_HEIGHT + edit_height +
            SPLITTER_HEIGHT + log_height + STATUSBAR_HEIGHT
    );

    SendMessageW(parent, WM_SETREDRAW, FALSE, 0);
    HDWP window_pos = BeginDeferWindowPos(3);
    DeferWindowPos(
        window_pos,
        g_controls.text_editor,
        HWND_TOP,
        edit_x, edit_y,
        edit_width, edit_height,
        SWP_FLAGS
    );
    DeferWindowPos(
        window_pos,
        g_controls.line_numbers,
        HWND_TOP,
        0, 0,
        LINE_NUMBER_WIDTH, edit_height - SCROLLBAR_SIZE,
        SWP_FLAGS | SWP_NOMOVE
    );
    DeferWindowPos(
        window_pos,
        g_controls.log_area,
        HWND_TOP,
        log_x, log_y,
        log_width, log_height,
        SWP_FLAGS
    );
    EndDeferWindowPos(window_pos);
    SendMessageW(parent, WM_SETREDRAW, TRUE, 0);

    // TODO(cya): calc and account for line count
    isize log_line_count = Edit_GetLineCount(g_controls.log_area);
    isize resize_padding = g_state.char_height;
    isize top = MIN(splitter_top, g_state.splitter_top) -
        SCROLLBAR_SIZE - 1 - resize_padding;
    isize bottom = MAX(splitter_top, g_state.splitter_top) +
        SPLITTER_HEIGHT + 1 + resize_padding * log_line_count;

    g_state.splitter_top = splitter_top;

    RECT splitter_rect = {
        .top = top,
        .bottom = bottom,
        .right = g_client.width,
    };
    RedrawWindow(parent, &splitter_rect, NULL, REDRAW_FLAGS);

    RECT scroll_rect = {
        .top = edit_y,
        .bottom = log_y + log_height,
        .left = g_client.width - SCROLLBAR_SIZE - resize_padding,
        .right = g_client.width,
    };
    RedrawWindow(parent, &scroll_rect, NULL, REDRAW_FLAGS);
}

static void Win32UpdateControls(HWND parent)
{
    SendMessageW(parent, WM_SETREDRAW, FALSE, 0);
    SendMessageW(g_controls.toolbar, TB_AUTOSIZE, 0, 0);
    MoveWindow(
        g_controls.statusbar,
        0, g_client.height - STATUSBAR_HEIGHT,
        g_client.width, STATUSBAR_HEIGHT,
        FALSE
    );

    Win32ClientDimensions log_rect =
        Win32GetClientDimensions(g_controls.log_area);
    isize splitter_top = g_client.height - STATUSBAR_HEIGHT -
        log_rect.height - SCROLLBAR_SIZE - SPLITTER_HEIGHT;
    Win32ResizeTextAreas(parent, splitter_top);

    SendMessageW(parent, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(parent, NULL, NULL, REDRAW_FLAGS);
}

LRESULT CALLBACK Win32TextEditorCallback(
    HWND control,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR subclass_id,
    DWORD_PTR ref_data
) {
    (void)subclass_id, (void)ref_data;
    switch (message) {
    case WM_PAINT: {
        Win32UpdateLineNumbers();
    } break;
    case WM_PASTE: {
        if (!OpenClipboard(NULL)) {
            break;
        }

        HANDLE clipboard = GetClipboardData(CF_UNICODETEXT);
        if (clipboard == NULL) {
            goto clip_cleanup;
        }

        u16 *clip_text = GlobalLock(clipboard);
        GlobalUnlock(clipboard);
        if (clip_text == NULL) {
            goto clip_cleanup;
        }

        // NOTE(cya): clipboard content must be copied acording to MSDN
        CyString16 converted = cy_string_16_convert_newlines(
            g_allocs.page, cy_string_16_view_create_c(clip_text)
        );
        isize new_size = CY__U16S_TO_BYTES(cy_string_16_cap(converted) + 1);
        u16 *new_text = GlobalLock(GlobalAlloc(GHND, new_size));
        if (new_text == NULL) {
            cy_string_16_free(converted);
            goto clip_cleanup;
        }

        CopyMemory(new_text, converted, new_size);
        cy_string_16_free(converted);

        // NOTE(cya): no freeing here since the OS takes ownership
        GlobalUnlock(new_text);
        SetClipboardData(CF_UNICODETEXT, new_text);

    clip_cleanup:
        CloseClipboard();
    } break;
    }

    return DefSubclassProc(control, message, w_param, l_param);
}

static b32 Win32IsOverEditorSplitter(int y)
{
    const isize ADJUSTMENT = 4;

    Win32ClientDimensions editor_dimensions =
        Win32GetClientDimensions(g_controls.text_editor);
    isize editor_bottom =
        TOOLBAR_HEIGHT + editor_dimensions.height + SCROLLBAR_SIZE;

    isize y_top = editor_bottom - ADJUSTMENT;
    isize y_bottom = y_top + SPLITTER_HEIGHT + ADJUSTMENT;

    return (y >= y_top) && (y <= y_bottom);
}

static inline void Win32SetLogAreaText(const u16 *txt)
{
    SetWindowTextW(g_controls.log_area, txt);
    SendMessageW(g_controls.log_area, EM_SETSEL, 0, -1);
    SendMessageW(g_controls.log_area, EM_SETSEL, -1, -1);
    SendMessageW(g_controls.log_area, EM_SCROLLCARET, 0, 0);
}

#define MAX_STATUSBAR_TEXT_LEN 80

static inline void Win32SetStatusbarText(u16 *str)
{
    CY_ASSERT_NOT_NULL(str);

    isize prefix_len = UTF16_STATIC_LENGTH(L"\\\\?\\");
    isize len = cy_wcs_len(str);
    ASSERT(len > prefix_len);

    str += prefix_len;
    len -= prefix_len;
    utf16_trim_start(str, len, MAX_STATUSBAR_TEXT_LEN);

    SetWindowTextW(g_controls.statusbar, str);
}

UINT_PTR Win32DialogHook(
    HWND dialog,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    (void)w_param, (void)l_param;
    switch (message) {
    case WM_INITDIALOG: {
        WIN32_STRIP_WINDOW_THEME(dialog);
        return TRUE;
    } break;
    }

    return FALSE;
}

static inline CyString cy_string_from_text_editor(CyAllocator a)
{
    isize len = Edit_GetTextLength(g_controls.text_editor);
    CyString16 utf16 = cy_string_16_create_reserve(a, len);
    Edit_GetText(g_controls.text_editor, utf16, len + 1);
    cy__string_16_set_len(utf16, len);

    CyString utf8 = Win32UTF16toUTF8(utf16);
    cy_string_16_free(utf16);
    return utf8;
}

#define PATH_BUF_CAP 0x1000

static inline void file_path_from_handle(
    HANDLE file, u16 *buf_out, isize buf_size
) {
    GetFinalPathNameByHandleW(file, buf_out, buf_size, 0);
}

#define OFN_FLAGS OFN_ENABLEHOOK | OFN_HIDEREADONLY | \
    OFN_LONGNAMES | OFN_NONETWORKBUTTON

LRESULT CALLBACK Win32WindowCallback(
    HWND window,
    UINT message,
    WPARAM w_param,
    LPARAM l_param
) {
    static isize cursor_y;
    switch (message) {
    case WM_CREATE: {
        Win32SetupControls(window);
    } break;
    case WM_DESTROY:
    case WM_CLOSE: {
        PostQuitMessage(0);
    } break;
    case WM_CTLCOLOREDIT: {
        if ((HWND)l_param == g_controls.text_editor) {
            HDC dc = (HDC)w_param;
            COLORREF edit_bk = RGB(248, 248, 248);
            SetBkColor(dc, edit_bk);
            SetDCBrushColor(dc, edit_bk);

            return (LRESULT)GetStockObject(DC_BRUSH);
        }
    } break;
    case WM_CTLCOLORSTATIC: {
        if ((HWND)l_param == g_controls.line_numbers) {
            HDC dc = (HDC)w_param;
            SetBkMode(dc, TRANSPARENT);

            COLORREF rgb_gray = RGB(180, 180, 180);
            SetTextColor(dc, rgb_gray);

            return (LRESULT)CreateSolidBrush(GetSysColor(COLOR_MENU));
        }
    } break;
    case WM_SIZE: {
        g_client = (Win32ClientDimensions){
            .height = HIWORD(l_param),
            .width = LOWORD(l_param),
        };
        Win32UpdateControls(window);
    } break;
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO minmax_info = (LPMINMAXINFO)l_param;
        minmax_info->ptMinTrackSize.x = g_state.min_x;
        minmax_info->ptMinTrackSize.y = g_state.min_y;
    } break;
    case WM_LBUTTONDOWN: {
        isize y = GET_Y_LPARAM(l_param);
        g_state.resizing_splitter = Win32IsOverEditorSplitter(y);
        if (g_state.resizing_splitter) {
            SetCapture(window);
            SetCursor(g_state.resize_cursor);
        }
    } break;
    case WM_MOUSEMOVE: {
        isize y = GET_Y_LPARAM(l_param);
        if (Win32IsOverEditorSplitter(y)) {
            SetCursor(g_state.resize_cursor);
        }

        b32 mouse_down = w_param & MK_LBUTTON;
        if (mouse_down && g_state.resizing_splitter) {
            isize new_cursor_y = GET_Y_LPARAM(l_param);
            if (cursor_y != 0 && new_cursor_y == cursor_y) {
                break;
            }

            cursor_y = new_cursor_y;

            isize new_splitter_top = cursor_y - SPLITTER_HEIGHT / 2;
            Win32ResizeTextAreas(window, new_splitter_top);
        }
    } break;
    case WM_LBUTTONUP: {
        if (g_state.resizing_splitter) {
            g_state.resizing_splitter = false;
            ReleaseCapture();
            ClipCursor(NULL);
            Win32UpdateControls(window);
        }
    } break;
    case WM_COMMAND: {
        isize command = (HIWORD(w_param) == 1) ? LOWORD(w_param) : w_param;
        switch (command) {
        case BUTTON_FILE_NEW: {
            g_state.scratch_file = true;

            Win32UpdateLineNumbers();
            Edit_SetText(g_controls.text_editor, NULL);
            SetWindowTextW(g_controls.log_area, NULL);
            SetWindowTextW(g_controls.statusbar, SCRATCH_FILE_TEXT);
        } break;
        case BUTTON_FILE_OPEN: {
            u16 *file_filter =
                L"Todos os arquivos (*.*)\0*.*\0"
                "Arquivos de texto (*.txt)\0*.txt\0\0";
            u16 path_buf[PATH_BUF_CAP] = {0};
            OPENFILENAMEW ofn = {
                .lStructSize = sizeof(OPENFILENAMEW),
                .hwndOwner = window,
                .Flags = OFN_FLAGS,
                .lpfnHook = Win32DialogHook,
                .lpstrFile = path_buf,
                .nMaxFile = CY_STATIC_ARR_LEN(path_buf),
                .lpstrFilter = file_filter,
                .nFilterIndex = 2,
            };
            if (!GetOpenFileNameW(&ofn)) {
                if (CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
                    WIN32_FILENAME_ERROR_DIALOG();
                }

                return 0;
            }

            if (!g_state.scratch_file) {
                CloseHandle(g_state.file);
            }

            g_state.file = CreateFileW(
                ofn.lpstrFile,
                GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            if (g_state.file == INVALID_HANDLE_VALUE) {
                Win32ErrorDialog(L"Erro ao abrir arquivo");
                return 0;
            }

            u16 path_final[PATH_BUF_CAP] = {0};
            isize path_final_cap = CY_STATIC_ARR_LEN(path_final);
            file_path_from_handle(g_state.file, path_final, path_final_cap);

            g_state.scratch_file = false;

            LARGE_INTEGER file_size;
            GetFileSizeEx(g_state.file, &file_size);

            CyString16 utf16_buf = NULL, final_buf = NULL;
            isize utf8_len = file_size.QuadPart;
            CyString utf8_buf = cy_string_create_reserve(
                g_allocs.page, utf8_len
            );
            if (utf8_buf == NULL) {
                Win32ErrorDialog(L"Erro ao alocar memória temporária");
            }

            isize bytes_read = 0;
            b32 read = ReadFile(
                g_state.file,
                utf8_buf,
                file_size.QuadPart,
                (LPDWORD)&bytes_read,
                NULL
            );
            ASSERT(bytes_read == (isize)file_size.QuadPart);

            if (!read) {
                Win32ErrorDialog(L"Erro ao ler arquivo");
                goto fopen_cleanup;
            }

            cy__string_set_len(utf8_buf, utf8_len);
            utf16_buf = Win32UTF8toUTF16(utf8_buf);

            final_buf = cy_string_16_convert_newlines(
                g_allocs.page, cy_string_16_view_create(utf16_buf)
            );
            Edit_SetText(g_controls.text_editor, final_buf);

            Win32SetLogAreaText(NULL);
            Win32SetStatusbarText(path_final);
            Win32UpdateLineNumbers();

        fopen_cleanup:
            cy_string_16_free(final_buf);
            cy_string_16_free(utf16_buf);
            cy_string_free(utf8_buf);
        } break;
        case BUTTON_FILE_SAVE: {
            u16 path_buf[PAGE_SIZE] = {0};
            isize path_buf_cap = CY_STATIC_ARR_LEN(path_buf);
            if (!g_state.scratch_file) {
                file_path_from_handle(g_state.file, path_buf, path_buf_cap);
            } else {
                u16 *file_filter =
                    L"Qualquer (*.*)\0*.*\0Arquivo de texto (*.txt)\0*.txt\0\0";
                OPENFILENAMEW ofn = {
                    .lStructSize = sizeof(OPENFILENAMEW),
                    .hwndOwner = window,
                    .Flags = OFN_FLAGS,
                    .lpfnHook = Win32DialogHook,
                    .lpstrFile = path_buf,
                    .nMaxFile = path_buf_cap,
                    .lpstrFilter = file_filter,
                    .nFilterIndex = 2,
                };
                if (!GetSaveFileNameW(&ofn)) {
                    if (CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
                        WIN32_FILENAME_ERROR_DIALOG();
                    }

                    return 0;
                }

                isize idx = ofn.nFileExtension;
                if (ofn.nFilterIndex == 2 && *(path_buf + idx) == '\0') {
                    Win32AppendExtension(path_buf, idx, path_buf_cap, L"txt");
                }

                DWORD attr = GetFileAttributesW(path_buf);
                b32 exists = (attr != INVALID_FILE_ATTRIBUTES) &&
                    !(attr & FILE_ATTRIBUTE_DIRECTORY);
                if (exists) {
                    int res = MessageBoxW(
                        window,
                        L"Este arquivo já existe, deseja substituí-lo?",
                        L"Confirmar",
                        MB_YESNO | MB_ICONWARNING
                    );
                    if (res == IDNO) {
                        break;
                    }
                }

                g_state.file = CreateFileW(
                    path_buf,
                    GENERIC_READ | GENERIC_WRITE,
                    FILE_SHARE_READ | FILE_SHARE_WRITE,
                    NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                    NULL
                );
                if (g_state.file == INVALID_HANDLE_VALUE) {
                    Win32ErrorDialog(L"Erro ao salvar arquivo");
                    break;
                }
            }

            CyString src_code = cy_string_from_text_editor(cy_heap_allocator());

            u16 path_final[PATH_BUF_CAP] = {0};
            isize path_final_cap = CY_STATIC_ARR_LEN(path_final);
            file_path_from_handle(g_state.file, path_final, path_final_cap);

            g_state.scratch_file = false;

            SetFilePointer(g_state.file, 0, NULL, FILE_BEGIN);
            SetEndOfFile(g_state.file);

            isize bytes_written = 0;
            b32 written = WriteFile(
                g_state.file,
                src_code,
                cy_string_len(src_code),
                (LPDWORD)&bytes_written,
                NULL
            );
            if (!written) {
                Win32ErrorDialog(L"Erro ao escrever texto no arquivo");
                goto fsave_cleanup;
            }

            Win32SetLogAreaText(NULL);
            Win32SetStatusbarText(path_final);

        fsave_cleanup:
            cy_string_free(src_code);
        } break;
        case BUTTON_TEXT_COPY:  {
            SendMessageW(g_controls.text_editor, WM_COPY, 0, 0);
        } break;
        case BUTTON_TEXT_PASTE:  {
            SendMessageW(g_controls.text_editor, WM_PASTE, 0, 0);
        } break;
        case BUTTON_TEXT_CUT:  {
            SendMessageW(g_controls.text_editor, WM_CUT, 0, 0);
        } break;
        case BUTTON_COMPILE: {
            CyString msg = NULL;
            u16 *msg_utf16 = NULL;

            CyString src_code = cy_string_from_text_editor(cy_heap_allocator());
            if (src_code == NULL) {
                Win32ErrorDialog(L"Erro ao copiar texto do editor");
                goto compile_cleanup;
            }

            CompilerOutput output = compile(cy_string_view_create(src_code));
            msg = output.msg;

            // TODO(cya): write il code file

            msg_utf16 = Win32UTF8toUTF16(msg);
            Win32SetLogAreaText(msg_utf16);

        compile_cleanup:
            cy_string_16_free(msg_utf16);
            cy_string_free(msg);
            cy_string_free(src_code);
        } break;
        case BUTTON_DISPLAY_GROUP: {
            if (g_bufs.members == NULL) {
                HANDLE file = CreateFileW(
                    L"integrantes.txt",
                    GENERIC_READ, FILE_SHARE_READ,
                    NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                    NULL
                );

                const char prefix[] = "Integrantes:\r\n";
                isize prefix_len = CY_STATIC_STR_LEN(prefix);
                const char err_suffix[] = "(integrantes.txt não encontrado)";
                isize err_suffix_len = 4 + CY_STATIC_STR_LEN(err_suffix);
                isize members_cap = prefix_len + err_suffix_len;

                LARGE_INTEGER file_size = {0};
                if (g_state.file != INVALID_HANDLE_VALUE) {
                    GetFileSizeEx(file, &file_size);
                    members_cap = prefix_len + file_size.QuadPart;
                }

                CyString members = cy_string_create_reserve(
                    cy_heap_allocator(), members_cap
                );
                if (members == NULL) {
                    Win32ErrorDialog(L"Erro ao alocar memória temporária");
                    break;
                }

                members = cy_string_append_c(members, prefix);
                if (file != INVALID_HANDLE_VALUE) {
                    isize bytes_read = 0;
                    ReadFile(
                        file, members + prefix_len, members_cap - prefix_len,
                        (LPDWORD)&bytes_read, NULL
                    );
                    ASSERT(bytes_read == (isize)file_size.QuadPart);
                    CloseHandle(file);
                } else {
                    members = cy_string_append_fmt(members, "\t%s", err_suffix);
                }

                cy__string_set_len(members, cy_str_len(members));
                CyString16 members_utf16 = Win32UTF8toUTF16(members);
                g_bufs.members = members_utf16;
                cy_string_free(members);
            }

            Win32SetLogAreaText(g_bufs.members);
        } break;
        default : {
            switch (HIWORD(w_param)) {
            case EN_UPDATE: {
                Win32UpdateLineNumbers();
            } break;
            case EN_MAXTEXT: {
                if ((HWND)l_param == g_controls.text_editor) {
                    isize new_limit = 2 * Edit_GetTextLength(
                        g_controls.text_editor
                    );
                    Edit_LimitText(g_controls.text_editor, new_limit);
                }
            } break;
            }
        } break;
        }
    } break;
    }

    return DefWindowProcW(window, message, w_param, l_param);
}

int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE prev_instance,
    LPWSTR cmd_line,
    int cmd_show
) {
    (void)instance, (void)prev_instance, (void)cmd_line, (void)cmd_show;

    g_allocs.page = cy_page_allocator();
    g_bufs.lines = cy_string_16_create_reserve(
        g_allocs.page, (MAX_LINE_DIGITS + 2) * 20
    );

    const u16 *CLASS_NAME = L"compiler_gui_window";
    WNDCLASSW window_class = {
        .lpfnWndProc = Win32WindowCallback,
        .hInstance = instance,
        .lpszClassName = CLASS_NAME,
        .hbrBackground = (HBRUSH)CTLCOLOR_SCROLLBAR,
        .hIcon = LoadIconW(NULL, IDI_APPLICATION),
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
    };
    RegisterClassW(&window_class);

    RECT rect = {
        .bottom = WINDOW_HEIGHT,
        .right = WINDOW_WIDTH,
    };
    DWORD rect_styles = WS_CAPTION | WS_SYSMENU | WS_THICKFRAME |
        WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    AdjustWindowRect(&rect, rect_styles, FALSE);

    isize width = rect.right - rect.left;
    isize height = rect.bottom - rect.top;
    g_state.min_x = width;
    g_state.min_y = height;
    HWND window = CreateWindowExW(
        WS_EX_COMPOSITED,
        CLASS_NAME,
        L"compilador 2024-2",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width,
        height,
        NULL,
        NULL,
        instance,
        NULL
    );
    if (window == NULL) {
        Win32FatalErrorDialog(L"Erro ao criar janela");
    }

    // TODO(cya): add accelerator for 'compile to exe' button (CTRL+F7)
    ACCEL accels[BUTTON_COUNT] = {
        {FVIRTKEY | FCONTROL, 'N', BUTTON_FILE_NEW},
        {FVIRTKEY | FCONTROL, 'O', BUTTON_FILE_OPEN},
        {FVIRTKEY | FCONTROL, 'S', BUTTON_FILE_SAVE},
        {FVIRTKEY | FCONTROL, 'C', BUTTON_TEXT_COPY},
        {FVIRTKEY | FCONTROL, 'V', BUTTON_TEXT_PASTE},
        {FVIRTKEY | FCONTROL, 'X', BUTTON_TEXT_CUT},
        {FVIRTKEY, VK_F7, BUTTON_COMPILE},
        {FVIRTKEY, VK_F1, BUTTON_DISPLAY_GROUP},
    };
    HACCEL accel_table = CreateAcceleratorTableW(accels, BUTTON_COUNT);
    g_state.resize_cursor = LoadCursorW(NULL, IDC_SIZENS);

    MSG message;
    while (GetMessageW(&message, NULL, 0, 0) > 0) {
        if (!TranslateAcceleratorW(window, accel_table, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }

    return message.wParam;
}
