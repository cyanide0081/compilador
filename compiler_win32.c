#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>

#include "compiler.c"

typedef struct {
    isize size;
    isize len;
    u16 *data; // Stored as UTF-16 for smoother interop with WIN32
} TextBuf;

typedef struct {
    TextBuf editor;
    TextBuf lines;
    TextBuf logger;
    TextBuf file_path;
} GlobalBufs;
static GlobalBufs g_bufs;

typedef struct {
    i32 first_visible_line;
    i32 last_visible_line;
    i32 min_x;
    i32 min_y;
    isize splitter_top;
    isize elapsed_ms;
    HCURSOR resize_cursor;
    b8 scratch_file;
    b8 resizing_splitter;
    const u16 *members;
} GlobalState;
static GlobalState g_state = {
    .scratch_file = true
};

#define PAGE_SIZE CY_PAGE_SIZE

#define ASSERT(cond) CY_ASSERT(cond)
#define STATIC_ASSERT(cond) CY_STATIC_ASSERT(cond)
#define UTF16_STATIC_LENGTH(str) CY_STATIC_STR_LEN(str)

static inline isize round_up(isize size, isize target)
{
    ASSERT((target & (target - 1)) == 0); // is power of 2

    uintptr mod = size & (target - 1);
    return mod ? size + target - mod : (size == 0) ? target : size;
}

static inline isize utf16_length(const u16* buf)
{
    if (buf == NULL) {
        return 0;
    }

    isize len = 0;
    while (*buf++ != '\0') len += 1;

    return len;
}

static inline u16 *utf16_concat(
    u16 *dst,
    const u16 *src,
    isize len
) {
    isize size = len * sizeof(*dst);
    CopyMemory(dst, src, size);
    return dst + len;
}

static inline void *page_alloc(isize bytes)
{
    return VirtualAlloc(
        NULL, bytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE
    );
}

static inline void page_free(void *memory)
{
    if (memory != NULL) VirtualFree(memory, 0, MEM_RELEASE);
}

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

    const u16 concat[] = L": ";
    isize msg_len = utf16_length(preface);
    isize concat_len = UTF16_STATIC_LENGTH(concat);
    isize error_msg_len = utf16_length(error_msg);
    isize final_msg_len = msg_len + concat_len + error_msg_len;
    u16 *final_msg = page_alloc((final_msg_len + 1) * sizeof(*final_msg));
    if (final_msg == NULL) {
        ExitProcess(GetLastError());
    }

    u16 *end = utf16_concat(final_msg, preface, msg_len);
    end = utf16_concat(end, concat, concat_len);
    end = utf16_concat(end, error_msg, error_msg_len);

    LocalFree(error_msg);
    return final_msg;
}

static inline void Win32ErrorDialog(const u16 *msg)
{
    u16 *final_msg = Win32BuildErrorMessage(msg);
    MessageBoxW(NULL, final_msg, L"Erro", MB_OK | MB_ICONWARNING);
    page_free(final_msg);
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

static inline u16 *Win32UTF8toUTF16(const char *str, isize len, isize *len_out)
{
    isize len_utf16 = MultiByteToWideChar(
        CP_UTF8, 0,
        str, len,
        NULL, 0
    );
    isize size_utf16 = (len_utf16 + 1) * sizeof(u16);
    u16 *str_utf16 = page_alloc(size_utf16);
    if (str_utf16 == NULL) {
        return NULL;
    }

    isize res = MultiByteToWideChar(
        CP_UTF8, 0,
        str, len + 1,
        str_utf16, len_utf16 + 1
    );
    if (res == 0) {
        Win32ErrorDialog(L"Erro ao converter texto para UTF-16");
        *len_out = 0;
        return NULL;
    }

    str_utf16[len_utf16] = '\0';
    *len_out = len_utf16;
    return str_utf16;
}

static inline CyString Win32UTF16toUTF8(const u16 *str, isize len)
{
    isize len_utf8 = WideCharToMultiByte(
        CP_UTF8, 0,
        str, len,
        NULL, 0,
        NULL, NULL
    );
    CyString str_utf8 = cy_string_create_reserve(cy_heap_allocator(), len_utf8);
    if (str_utf8 == NULL) {
        return NULL;
    }

    isize res = WideCharToMultiByte(
        CP_UTF8, 0,
        str, len + 1,
        str_utf8, len_utf8 + 1,
        NULL, NULL
    );
    if (res == 0) {
        Win32ErrorDialog(L"Erro ao converter texto para UTF-8");
        cy_string_free(str_utf8);
        return NULL;
    }

    cy__string_set_len(str_utf8, len_utf8);
    str_utf8[len_utf8] = '\0';
    return str_utf8;
}

static TextBuf text_buf_alloc(isize chars)
{
    isize size = round_up((chars + 1) * sizeof(u16), PAGE_SIZE);
    u16 *buf = page_alloc(size);
    if (buf == NULL) {
        WIN32_FATAL_MEM_ERROR_DIALOG();
    }

    return (TextBuf){
        .size = size,
        .data = buf,
    };
}

static void text_buf_resize(TextBuf *buf, isize new_chars)
{
    isize new_size = round_up((new_chars + 1) * sizeof(*buf->data), PAGE_SIZE);
    b32 below_threshold = (new_size * 2 < buf->size);
    if (new_size <= buf->size && !below_threshold) {
        return;
    }

    u16 *new_buf = page_alloc(new_size);
    if (new_buf == NULL) {
        WIN32_FATAL_MEM_ERROR_DIALOG();
    }

    OutputDebugStringW(L"Resized Text Buffer\n");

    isize old_bytes = buf->len * sizeof(*buf->data);
    isize copy_size = old_bytes <= new_size ? old_bytes : new_size;

    CopyMemory(new_buf, buf->data, copy_size);
    page_free(buf->data);

    buf->data = new_buf;
    buf->size = new_size;
}

static inline void text_buf_free(TextBuf *buf)
{
    page_free(buf->data);
    FillMemory(buf, sizeof(*buf), 0);
}

#if 0
static inline isize text_buf_cap(TextBuf *buf)
{
    return buf->size / sizeof(*buf->data);
}
#endif

static inline void text_buf_copy_from(TextBuf *buf, u16 *src)
{
    isize len = utf16_length(src);
    text_buf_resize(buf, len);
    CopyMemory(buf->data, src, len * sizeof(*src));
    buf->data[len] = L'\0';
    buf->len = len;
}

static inline void text_buf_recalc_len(TextBuf *buf)
{
    buf->len = utf16_length(buf->data);
}

static inline void text_buf_clear(TextBuf *buf)
{
    FillMemory(buf->data, buf->size, 0);
    buf->len = 0;
}

static void utf16_insert_dots(u16 *str, b32 null_terminate)
{
#if defined(_M_X64) || defined(__x86_64__)
    u16 save = str[3];
    *((UINT64*)str) = *((UINT64*)L"..."); // 64-bit write of "..."

    if (!null_terminate) {
        str[3] = save;
    }
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
static void text_buf_trim_start(TextBuf *buf, isize max_cells) {
    if (buf->data == NULL) {
        return;
    }

    UINT16 *str = buf->data;
    isize len = buf->len;
    enum {
        MIN_CELLS = 3,
    };
    if (len < MIN_CELLS) {
        text_buf_resize(buf, MIN_CELLS);
    }
    if (max_cells < MIN_CELLS) {
        utf16_insert_dots(str, true);
        return;
    }

    // TODO(cya): port wcswidth code to here so we can get a way more accurate
    // estimated cell width when calculating the length of the string
    int offset = 0, cells = 0;
    for (offset = len - 1; (offset >= 0) && (cells < (int)max_cells); cells++) {
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
        utf16_insert_dots(str + offset, false);

        isize bytes_to_move = new_len * sizeof(*str);
        MoveMemory(str, str + offset, bytes_to_move);

        isize bytes_to_clear = (len - new_len) * sizeof(*str);
        FillMemory(str + new_len, bytes_to_clear, 0);
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

static void Win32SetupControls(HWND parent)
{
    g_client = Win32GetClientDimensions(parent);

    enum {
        WS_DEFAULT = WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
        WS_SCROLLABLE = WS_DEFAULT | WS_BORDER | WS_VSCROLL | WS_HSCROLL,
        WS_TEXT_AREA =
            WS_SCROLLABLE | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
    };

    g_controls.toolbar = CreateWindowW(
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

    g_controls.line_numbers = CreateWindowW(
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
    g_controls.text_editor = CreateWindowW(
        WC_EDITW,
        L"#",
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
    SendMessageW(g_controls.text_editor, EM_SETTABSTOPS, 1, (LPARAM)&tab_width);

    {
        int y = 2 +
            HIWORD(SendMessageW(g_controls.text_editor, EM_POSFROMCHAR, 0, 0));
        SetWindowPos(
            g_controls.line_numbers, HWND_TOP,
            0, y + TOOLBAR_HEIGHT, 0, 0,
            SWP_NOSIZE
        );
        SetWindowTextW(g_controls.text_editor, NULL);
    }

    g_controls.log_area = CreateWindowW(
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
    SendMessageW(g_controls.log_area, EM_SETTABSTOPS, 1, (LPARAM)&tab_width);

    g_controls.statusbar = CreateWindowW(
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

static isize int_to_utf16(isize n, isize max_digits, u16 *buf, isize cap)
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

    for (isize i = 0; i < digits && i < (int)cap; i++) {
        dividend /= 10;
        buf[i] = L'0' + n / dividend;
        n %= dividend;
    }

    return digits;
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
    u16 *str;
    isize len;
} String16;

typedef struct {
    String16 string;
    isize cur_line_offset;
    isize crlf_len;
} LineScanner;

static LineScanner line_scanner_build(String16 *string)
{
    isize offset = 0, crlf_len = 0;
    u16 *end = string->str;
    while (offset < string->len) {
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

        end = string->str + offset;
    }

    return (LineScanner){
        .string = *string,
        .crlf_len = crlf_len,
    };
}

static String16 line_scanner_get_line(LineScanner *scanner)
{
    u16 *buf = scanner->string.str;
    isize offset = scanner->cur_line_offset;
    isize len = scanner->string.len;
    if (offset >= len) {
        return (String16){0};
    }

    u16 *line = buf + offset;
    u16 *curr = line;
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
        .str = line,
        .len = line_len,
    };
}

#define TE_NEWLINE L"\r\n"

static void utf16_convert_newlines(LineScanner *scanner, u16 *dst)
{
    String16 line = line_scanner_get_line(scanner);
    u16 *end = utf16_concat(dst, line.str, line.len);
    while (line.str != NULL) {
        end = utf16_concat(end, TE_NEWLINE, UTF16_STATIC_LENGTH(TE_NEWLINE));
        line = line_scanner_get_line(scanner);
        end = utf16_concat(end, line.str, line.len);
    }
}

static void text_buf_convert_newlines(TextBuf *dst, u16 *src, isize len)
{
    LineScanner scanner = line_scanner_build(&(String16){
        .str = src,
        .len = len,
    });

    text_buf_clear(dst);
    text_buf_resize(dst, scanner.crlf_len);
    utf16_convert_newlines(&scanner, dst->data);
}

static void Win32UpdateLineNumbers(void)
{
    isize first_visible_line = 1 + SendMessageW(
        g_controls.text_editor, EM_GETFIRSTVISIBLELINE, 0, 0
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

    text_buf_clear(&g_bufs.lines);
    text_buf_resize(&g_bufs.lines, len);

    u16 *buf = g_bufs.lines.data;

    isize pos = 0;
    for (isize i = first_visible_line; i <= last_visible_line; i++) {
        int digits = int_to_utf16(i, MAX_LINE_DIGITS, buf + pos, len - pos);
        pos += digits;

        buf[pos++] = L'\r';
        buf[pos++] = L'\n';
    }

    text_buf_recalc_len(&g_bufs.lines);
    SetWindowTextW(g_controls.line_numbers, buf);
}

#define MIN(a, b) (a < b ? a : b)
#define MAX(a, b) (a > b ? a : b)

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
    if (edit_height - SCROLLBAR_SIZE < min_height) {
        edit_height = min_height + SCROLLBAR_SIZE;
        splitter_top = edit_y + edit_height;
        log_y = splitter_top + SPLITTER_HEIGHT;
        log_height = height - STATUSBAR_HEIGHT -
            (splitter_top + SPLITTER_HEIGHT);
    } else if (log_height - SCROLLBAR_SIZE < min_height) {
        log_height = min_height + SCROLLBAR_SIZE;
        splitter_top = height - STATUSBAR_HEIGHT - log_height - SPLITTER_HEIGHT;
        log_y = splitter_top + SPLITTER_HEIGHT;
        edit_height = splitter_top - TOOLBAR_HEIGHT;
    }

    isize max_height = height - TEXT_AREA_MIN_HEIGHT -
        SCROLLBAR_SIZE - STATUSBAR_HEIGHT - TOOLBAR_HEIGHT;
    if (log_height > max_height) {
        log_height = max_height;
        log_y = height - STATUSBAR_HEIGHT - log_height;
        splitter_top = log_y - SPLITTER_HEIGHT;
        edit_height = splitter_top - TOOLBAR_HEIGHT;
        edit_y = splitter_top - edit_height;
    } else if (edit_height > max_height) {
        edit_height = max_height;
        edit_y = TOOLBAR_HEIGHT;
        splitter_top = edit_y + edit_height;
        log_y = splitter_top + SPLITTER_HEIGHT;
        log_height = height - STATUSBAR_HEIGHT -
            SPLITTER_HEIGHT - edit_height - TOOLBAR_HEIGHT;
    }

    ASSERT(
        height == TOOLBAR_HEIGHT + edit_height +
            SPLITTER_HEIGHT + log_height + STATUSBAR_HEIGHT
    );

    const UINT SWP_FLAGS =
        SWP_NOCOPYBITS | SWP_NOREDRAW | SWP_NOZORDER | SWP_NOREPOSITION;
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

    isize top =
        MIN(splitter_top, g_state.splitter_top) - SCROLLBAR_SIZE - PADDING_PX;
    isize bottom =
        MAX(splitter_top, g_state.splitter_top) + SPLITTER_HEIGHT + PADDING_PX;
    RECT resize_rect = {
        .top = top,
        .bottom = bottom,
        .right = g_client.width,
    };
    g_state.splitter_top = splitter_top;

    RedrawWindow(parent, &resize_rect, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    InvalidateRect(parent, &resize_rect, TRUE);
}

static void Win32UpdateControls(HWND parent)
{
    SendMessageW(g_controls.toolbar, TB_AUTOSIZE, 0, 0);
    MoveWindow(
        g_controls.statusbar,
        0, g_client.height - STATUSBAR_HEIGHT,
        g_client.width, STATUSBAR_HEIGHT,
        FALSE
    );

    Win32ClientDimensions log_rect =
        Win32GetClientDimensions(g_controls.log_area);
    g_state.splitter_top = g_client.height - STATUSBAR_HEIGHT -
        log_rect.height - SCROLLBAR_SIZE - SPLITTER_HEIGHT;
    Win32ResizeTextAreas(parent, g_state.splitter_top);

    RedrawWindow(parent, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
    InvalidateRect(parent, NULL, TRUE);
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
    // TODO(cya): figure out a way to disable the BitBlts() for good
    // case WM_NCCALCSIZE: {
    //     b32 calc_valid_rects = w_param && g_state.resizing_splitter;
    //     if (calc_valid_rects) {
    //         NCCALCSIZE_PARAMS *nccs_params = (NCCALCSIZE_PARAMS*)l_param;
    //         DefSubclassProc(
    //             control, message,
    //             FALSE, (LPARAM)&nccs_params->rgrc[0]
    //         );
    //         nccs_params->rgrc[1] = nccs_params->rgrc[2];

    //         return WVR_VALIDRECTS;
    //     }
    // } break;
    case WM_PASTE: {
        if (!OpenClipboard(NULL)) {
            break;
        }

        HANDLE clipboard = GetClipboardData(CF_UNICODETEXT);
        if (clipboard == NULL) {
            goto clip_cleanup;
        }

        u16 *clip_text = (u16*)(GlobalLock(clipboard));
        GlobalUnlock(clipboard);
        if (clip_text == NULL) {
            goto clip_cleanup;
        }

        // NOTE(cya): clipboard content must be copied acording to MSDN
        isize clip_text_len = utf16_length(clip_text);
        isize clip_text_size = (clip_text_len + 1) * sizeof(*clip_text);

        u16 *copied_text = page_alloc(clip_text_size);
        utf16_concat(copied_text, clip_text, clip_text_len);

        LineScanner scanner = line_scanner_build(&(String16){
            .str = copied_text,
            .len = clip_text_len
        });
        isize new_size = (scanner.crlf_len + 1) * sizeof(*scanner.string.str);
        u16 *new_text = GlobalLock(GlobalAlloc(GHND, new_size));
        if (new_text == NULL) {
            page_free(copied_text);
            goto clip_cleanup;
        }

        utf16_convert_newlines(&scanner, new_text);
        page_free(copied_text);

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

static inline void Win32SetStatusbarText(TextBuf *buf)
{
    TextBuf display_buf = {0};
    isize prefix_len = UTF16_STATIC_LENGTH(L"\\\\?\\");
    text_buf_copy_from(&display_buf, buf->data + prefix_len);
    text_buf_trim_start(&display_buf, MAX_STATUSBAR_TEXT_LEN);
    SetWindowTextW(g_controls.statusbar, display_buf.data);
    text_buf_free(&display_buf);
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

static inline CyString cy_string_from_text_editor(void)
{
    isize new_len = GetWindowTextLengthW(g_controls.text_editor);
    text_buf_resize(&g_bufs.editor, new_len);
    text_buf_clear(&g_bufs.editor);
    GetWindowTextW(
        g_controls.text_editor,
        g_bufs.editor.data, new_len + 1
    );
    g_bufs.editor.len = new_len;

    return Win32UTF16toUTF8(g_bufs.editor.data, g_bufs.editor.len);
}

#define MOUSEMOVE_TIMEOUT_MS (1000 / 200)

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

            RECT window_rect;
            GetWindowRect(window, &window_rect);
            POINT client_point = {0};
            ClientToScreen(window, &client_point);
            isize titlebar_height = client_point.y - window_rect.top;

            isize min_y = window_rect.top + titlebar_height +
                    TOOLBAR_HEIGHT + TEXT_AREA_MIN_HEIGHT + SCROLLBAR_SIZE;
            isize max_y = window_rect.bottom - STATUSBAR_HEIGHT -
                TEXT_AREA_MIN_HEIGHT - SCROLLBAR_SIZE - SPLITTER_HEIGHT;
            ClipCursor(&(RECT){
                .top = min_y,
                .bottom = max_y,
                .left = window_rect.left,
                .right = window_rect.right
            });

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
            text_buf_clear(&g_bufs.file_path);

            SetWindowTextW(g_controls.text_editor, NULL);
            SetWindowTextW(g_controls.log_area, NULL);
            Win32UpdateLineNumbers();
            SetWindowTextW(g_controls.statusbar, SCRATCH_FILE_TEXT);
        } break;
        case BUTTON_FILE_OPEN: {
            u16 *file_filter =
                L"Todos os arquivos (*.*)\0*.*\0"
                "Arquivos de texto (*.txt)\0*.txt\0\0";
            u16 temp_buf[PAGE_SIZE] = {0};
            OPENFILENAMEW ofn = {
                .lStructSize = sizeof(OPENFILENAMEW),
                .hwndOwner = window,
                .Flags = OFN_FLAGS,
                .lpfnHook = Win32DialogHook,
                .lpstrFile = temp_buf,
                .nMaxFile = CY_STATIC_ARR_LEN(temp_buf),
                .lpstrFilter = file_filter,
                .nFilterIndex = 2,
            };
            if (!GetOpenFileNameW(&ofn)) {
                if (CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
                    WIN32_FILENAME_ERROR_DIALOG();
                }

                return 0;
            }

            HANDLE file = CreateFileW(
                ofn.lpstrFile,
                GENERIC_READ,
                FILE_SHARE_READ,
                NULL,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            if (file == INVALID_HANDLE_VALUE) {
                Win32ErrorDialog(L"Erro ao abrir arquivo");
                return 0;
            }

            u16 final_path[PAGE_SIZE] = {0};
            GetFinalPathNameByHandleW(
                file, final_path, CY_STATIC_ARR_LEN(final_path), 0
            );

            text_buf_copy_from(&g_bufs.file_path, final_path);
            g_state.scratch_file = false;

            LARGE_INTEGER file_size;
            GetFileSizeEx(file, &file_size);

            u16 *utf16_buf = NULL;
            isize utf8_len = file_size.QuadPart;
            char *utf8_buf = page_alloc(utf8_len + 1);
            if (utf8_buf == NULL) {
                Win32ErrorDialog(L"Erro ao alocar memória temporária");
            }

            isize bytes_read = 0;
            b32 read = ReadFile(
                file,
                utf8_buf,
                file_size.QuadPart,
                (LPDWORD)&bytes_read,
                NULL
            );
            ASSERT(bytes_read == (isize)file_size.QuadPart);

            CloseHandle(file);
            if (!read) {
                Win32ErrorDialog(L"Erro ao ler arquivo");
                goto fopen_cleanup;
            }

            isize utf16_len;
            utf16_buf = Win32UTF8toUTF16(utf8_buf, utf8_len, &utf16_len);

            text_buf_convert_newlines(&g_bufs.editor, utf16_buf, utf16_len);

            SetWindowTextW(g_controls.text_editor, g_bufs.editor.data);
            Win32SetLogAreaText(NULL);
            Win32SetStatusbarText(&g_bufs.file_path);
            Win32UpdateLineNumbers();

        fopen_cleanup:
            page_free(utf16_buf);
            page_free(utf8_buf);
        } break;
        case BUTTON_FILE_SAVE: {
            u16 temp_buf[PAGE_SIZE] = {0};
            if (!g_state.scratch_file) {
                u16 *path = g_bufs.file_path.data;
                isize size = g_bufs.file_path.len * sizeof(*path);
                cy_mem_copy(temp_buf, path, size);
            } else {
                u16 *file_filter =
                    L"Qualquer (*.*)\0*.*\0Arquivo de texto (*.txt)\0*.txt\0\0";
                OPENFILENAMEW ofn = {
                    .lStructSize = sizeof(OPENFILENAMEW),
                    .hwndOwner = window,
                    .Flags = OFN_FLAGS,
                    .lpfnHook = Win32DialogHook,
                    .lpstrFile = temp_buf,
                    .nMaxFile = CY_STATIC_ARR_LEN(temp_buf),
                    .lpstrFilter = file_filter,
                    .nFilterIndex = 2,
                };
                if (!GetSaveFileNameW(&ofn)) {
                    if (CommDlgExtendedError() == FNERR_BUFFERTOOSMALL) {
                        WIN32_FILENAME_ERROR_DIALOG();
                    }

                    return 0;
                }

                u16 *ext = ofn.lpstrFile + ofn.nFileExtension;
                if (ofn.nFilterIndex == 2 && *ext == 0) {
                    const u16 EXT[] = L"txt";
                    const isize ext_len = UTF16_STATIC_LENGTH(EXT);
                    const isize new_len =  + ext_len;

                    ASSERT(CY_STATIC_ARR_LEN(temp_buf) > new_len);

                    if (ofn.nFileExtension > 0 && *(ext - 1) != '.') {
                        *(ext++) = '.';
                    }

                    utf16_concat(ext, EXT, ext_len);
                }
            }

            CyString src_code = cy_string_from_text_editor();
            HANDLE file = CreateFileW(
                temp_buf,
                GENERIC_WRITE, FILE_SHARE_WRITE,
                NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                NULL
            );
            if (file == INVALID_HANDLE_VALUE) {
                Win32ErrorDialog(L"Erro ao salvar arquivo");
                goto fsave_cleanup;
            }

            u16 final_path[PAGE_SIZE] = {0};
            GetFinalPathNameByHandleW(
                file, final_path, CY_STATIC_ARR_LEN(final_path), 0
            );
            text_buf_copy_from(&g_bufs.file_path, final_path);
            text_buf_recalc_len(&g_bufs.file_path);
            g_state.scratch_file = false;

            isize bytes_written = 0;
            b32 written = WriteFile(
                file,
                src_code,
                cy_string_len(src_code),
                (LPDWORD)&bytes_written,
                NULL
            );
            CloseHandle(file);
            if (!written) {
                Win32ErrorDialog(L"Erro ao escrever texto no arquivo");
                goto fsave_cleanup;
            }

            Win32SetLogAreaText(NULL);
            Win32SetStatusbarText(&g_bufs.file_path);

        fsave_cleanup:
            page_free(src_code);
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
            // TODO(cya): prepare src text and print output to log area
            CyString src_code = cy_string_from_text_editor();
            CyString output = NULL;
            u16 *output_utf16 = NULL;

            if (src_code == NULL) {
                Win32ErrorDialog(L"Erro ao copiar texto do editor");
                goto compile_cleanup;
            }

            String src_view = cy_string_view_create(src_code);
            output = compile(src_view);

            isize output_utf16_len;
            output_utf16 = Win32UTF8toUTF16(
                output, cy_string_len(output), &output_utf16_len
            );
            Win32SetLogAreaText(output_utf16);

        compile_cleanup:
            cy_string_free(src_code);
            cy_string_free(output);
            page_free(output_utf16);
        } break;
        case BUTTON_DISPLAY_GROUP: {
            if (g_state.members == NULL) {
                HANDLE file = CreateFileW(
                    L"integrantes.txt",
                    GENERIC_READ,
                    FILE_SHARE_READ,
                    NULL,
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    NULL
                );

                const char prefix[] = "Integrantes:\r\n";
                isize prefix_len = CY_STATIC_STR_LEN(prefix);
                const char err_suffix[] = "(integrantes.txt não encontrado)";
                isize err_suffix_len = 4 + CY_STATIC_STR_LEN(err_suffix);
                isize members_cap = prefix_len + err_suffix_len;

                LARGE_INTEGER file_size = {0};
                if (file != INVALID_HANDLE_VALUE) {
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
                    members = cy_string_append_len(members, "    ", 4);
                    members = cy_string_append_c(members, err_suffix);
                }

                isize members_utf16_len;
                u16 *members_utf16 = Win32UTF8toUTF16(
                    members, members_cap, &members_utf16_len
                );
                g_state.members = members_utf16;
            }

            Win32SetLogAreaText(g_state.members);
        } break;
        default : {
            switch (HIWORD(w_param)) {
            case EN_UPDATE: {
                Win32UpdateLineNumbers();
            } break;
            case EN_MAXTEXT: {
                if ((HWND)l_param == g_controls.text_editor) {
                    isize new_limit =
                        2 * GetWindowTextLengthW(g_controls.text_editor);
                    SendMessageW(
                        g_controls.text_editor, EM_LIMITTEXT,
                        new_limit, 0
                    );
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

    g_bufs.lines = text_buf_alloc((MAX_LINE_DIGITS + 2) * 20);
    g_bufs.editor = text_buf_alloc(0x1000);
    g_bufs.file_path = text_buf_alloc(MAX_PATH);

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
        0,
        CLASS_NAME,
        L"compilador 2024-2",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
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
