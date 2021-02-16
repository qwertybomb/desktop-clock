#include <stdbool.h>

#ifndef UNICODE
#define UNICODE
#endif

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Shlobj.h>
#undef COBJMACROS
#undef WIN32_LEAN_AND_MEAN
#undef UNICODE

#include "sinf.c"

#define PI_f 3.1415926f
#define CIRCLE_TOTAL_POINTS 12
#define LINE_TOTAL_POINTS 6
#define TOTAL_POINTS (LINE_TOTAL_POINTS * 3 + CIRCLE_TOTAL_POINTS)

static inline float I_roundf(float const x)
{
    return (float)(int)(x + 0.5f);
}

static inline void *I_malloc(size_t const size)
{
    return HeapAlloc(GetProcessHeap(), 0, size);
}

static inline bool I_free(void *const pointer)
{
    return HeapFree(GetProcessHeap(), 0, pointer);
}

static IFolderView2 *I_get_folder_view(void)
{
    IShellWindows *shell_windows;
    if (FAILED(CoCreateInstance(&CLSID_ShellWindows, NULL, CLSCTX_ALL,
                                &IID_IShellWindows, (void **) &shell_windows)))
    {
        return NULL;
    }

    bool failed = false;

    long window_handle;
    IDispatch *window_dispatch;
    if (FAILED(IShellWindows_FindWindowSW(shell_windows,
                                          (&(VARIANT) {
                                              .vt = VT_I4,
                                              .intVal = CSIDL_DESKTOP,
                                          }), &(VARIANT) {.vt = VT_EMPTY},
                                          SWC_DESKTOP, &window_handle,
                                          SWFO_NEEDDISPATCH, &window_dispatch)))
    {
        failed = true;
        goto shell_window_cleanup;
    }

    IServiceProvider *service_provider;
    if (FAILED(IDispatch_QueryInterface(window_dispatch,
                                        &IID_IServiceProvider,
                                        (void **) &service_provider)))
    {
        failed = true;
        goto window_dispatch_cleanup;
    }

    IShellBrowser *shell_browser;
    if (FAILED((IServiceProvider_QueryService(service_provider,
                                              &SID_STopLevelBrowser,
                                              &IID_IShellBrowser,
                                              (void **) &shell_browser))))
    {
        failed = true;
        goto service_provider_cleanup;
    }

    IShellView *shell_view;
    if (IShellBrowser_QueryActiveShellView(shell_browser, &shell_view))
    {
        failed = true;
        goto shell_browser_cleanup;
    }

    IFolderView2 *folder_view;
    if (FAILED(IShellView_QueryInterface(shell_view,
                                         &IID_IFolderView2,
                                         (void **) &folder_view)))
    {
        return NULL;
    }

    IShellView_Release(shell_view);

    shell_browser_cleanup:
    IShellBrowser_Release(shell_browser);

    service_provider_cleanup:
    IServiceProvider_Release(service_provider);

    window_dispatch_cleanup:
    IShellDispatch_Release(window_dispatch);

    shell_window_cleanup:
    IShellWindows_Release(shell_windows);

    if (failed)
    {
        return NULL;
    }

    return folder_view;
}

typedef struct
{
    ITEMIDLIST **item_id_data;
    POINT *point_data;
    int size, capacity;
} IconArray;

static void I_IconArray_update(IconArray *const self,
                               IFolderView2 *const folder_view)
{
    // we only need to update if the number of icons changed
    int icon_count;
    IFolderView2_ItemCount(folder_view, SVGIO_ALLVIEW, &icon_count);
    if (icon_count == self->size) return;

    // destroy old icons
    for (int i = 0; i < self->size; ++i)
    {
        CoTaskMemFree(self->item_id_data[i]);
    }

    if (icon_count > self->capacity)
    {
        if (self->point_data != NULL && self->item_id_data != NULL)
        {
            I_free(self->point_data);
            I_free(self->item_id_data);
        }

        self->capacity = icon_count;
        self->point_data = I_malloc(sizeof *self->point_data * self->capacity);

        self->item_id_data = I_malloc(sizeof *self->item_id_data * self->capacity);
    }

    self->size = icon_count;

    for (int i = 0; i < icon_count; ++i)
    {
        IFolderView2_Item(folder_view, i, (void *) &self->item_id_data[i]);
    }
}

static void I_draw_circle(IconArray const icons, int *const icons_used,
                          int const width, int const height)
{
    float const circle_radius = (float) (height < width ? height : width) / 2.75f;
    for (int i = 0; i < CIRCLE_TOTAL_POINTS && *icons_used < icons.size; ++i)
    {
        float const angle = ((float) i / (float) CIRCLE_TOTAL_POINTS) * 2 * PI_f;
        icons.point_data[(*icons_used)++] = (POINT) {
            .x = width / 2 - (long) I_roundf(I_cosf(angle) * circle_radius),
            .y = height / 2 - (long) I_roundf(I_sinf(angle) * circle_radius),
        };

    }
}

static void I_draw_circle_line(IconArray const icons, int *const icons_used,
                               int const width, int const height,
                               float const angle)
{
    float const circle_radius = (float) (height < width ? height : width) / 2.75f;
    for (int i = 0; i < LINE_TOTAL_POINTS && *icons_used < icons.size; ++i)
    {
        float const percentage = (float) i / (float) LINE_TOTAL_POINTS;
        icons.point_data[(*icons_used)++] = (POINT) {
            .x = width / 2 - (long) I_roundf(I_cosf(angle) * circle_radius * percentage),
            .y = height / 2 - (long) I_roundf(I_sinf(angle) * circle_radius * percentage),
        };
    }
}

typedef struct
{
    float seconds;
    float minutes;
    float hours;
} TimeInfo;

TimeInfo I_get_local_time(void)
{
    SYSTEMTIME local_time;
    GetLocalTime(&local_time);

    TimeInfo const result = {
        .seconds = (float)local_time.wSecond / 60 + (float)local_time.wMilliseconds / 60000,
        .minutes = (float)local_time.wMinute / 60 + result.seconds / 60,
        .hours = result.minutes / 12 + (float)local_time.wHour / 12
    };

    return result;
}

static inline float I_time_to_angle(float const time)
{
    return (time + 0.25f) * PI_f * 2;
}

extern int _fltused;
int _fltused = 0;

#if defined(_MSC_VER) && !defined(__clang__)
#define REAL_MSVC
#endif

#ifdef REAL_MSVC
#pragma function(memset)
#endif
void *memset(void *dest, int c, size_t count)
{
    char *bytes = (char *)dest;
    while (count-- != 0)
    {
        *bytes++ = (char)c;
    }
    return dest;
}

static void I_add_icons(IFolderView2 *const folder_view,
                            int *const file_handle_count,
                            HANDLE *const file_handles)
{
    int icon_count;
    IFolderView2_ItemCount(folder_view, SVGIO_ALLVIEW, &icon_count);

    if (icon_count >= TOTAL_POINTS)
    {
        return; // we have enough icons
    }


    *file_handle_count = TOTAL_POINTS - icon_count;

    static wchar_t file_path[MAX_PATH + 1];

    wchar_t *string_pointer;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Desktop, 0,
                                    NULL, &string_pointer)))
    {
        ExitProcess(GetLastError());
    }

    lstrcpynW(file_path, string_pointer, MAX_PATH);

    int const file_path_length = lstrlenW(file_path);
    for (int i = 0; i < TOTAL_POINTS - icon_count; ++i)
    {
        GUID guid;
        CoCreateGuid(&guid);

        int const left = StringFromGUID2(&guid, file_path + file_path_length,
                                         MAX_PATH - file_path_length);

        file_path[file_path_length] = '\\';
        file_path[file_path_length + left - 2] = '\0';

        file_handles[i] = CreateFileW(file_path, DELETE, 0,
                                      NULL, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL |
                                      FILE_FLAG_DELETE_ON_CLOSE, NULL);

        // if the file already exists just try again
        if (file_handles[i] == INVALID_HANDLE_VALUE &&
            GetLastError() == ERROR_FILE_EXISTS)
        {
            --i;
            continue;
        }
    }

    CoTaskMemFree(string_pointer);
}

void entry(void)
{
    (void) _fltused;
    (void) entry;

    // initialize com
    if (SUCCEEDED(CoInitialize(NULL)) == FALSE)
    {
        MessageBoxW(NULL, L"could not create in initialize com", L"error", MB_OK);
        ExitProcess(GetLastError());
    }

    IFolderView2 *const folder_view = I_get_folder_view();
    if (folder_view == NULL)
    {
        MessageBoxW(NULL, L"could not create an IFolderView2", L"error", MB_OK);
        ExitProcess(GetLastError());
    }

    int file_handle_count = 0;
    static HANDLE file_handles[TOTAL_POINTS];

    // add icons if there is not enough
    I_add_icons(folder_view, &file_handle_count, file_handles);

    // update desktop after potentially adding files
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_FLUSH, NULL, NULL);

    // NOTE: the program could fail if someone removes an icon
    // however if we don't need to call IFolderView2_ItemCount we can be faster
    IconArray icon_array = {0};
    I_IconArray_update(&icon_array, folder_view);

    for (;;)
    {
        RECT desktop_rect;
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &desktop_rect, 0);

        TimeInfo const time_info = I_get_local_time();

        // prevent us from using more icons than we have
        int icons_drawn = 0;

        int const desktop_width = desktop_rect.right - desktop_rect.left;
        int const desktop_height = desktop_rect.bottom - desktop_rect.top;

        I_draw_circle(icon_array, &icons_drawn, desktop_width, desktop_height);

        // draw the second hand
        I_draw_circle_line(icon_array, &icons_drawn,
                           desktop_width, desktop_height,
                           I_time_to_angle(time_info.seconds));

        // draw the minute hand
        I_draw_circle_line(icon_array, &icons_drawn,
                           desktop_width, desktop_height,
                           I_time_to_angle(time_info.minutes));

        // draw the hour hand
        I_draw_circle_line(icon_array, &icons_drawn,
                           desktop_width, desktop_height,
                           I_time_to_angle(time_info.hours));

        IFolderView2_SelectAndPositionItems(folder_view, icons_drawn,
                                            (void *) icon_array.item_id_data,
                                            (void *) icon_array.point_data, SVSI_SELECT);

        if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0 &&
            (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0)
        {
            break;
        }
    }

    // delete the files created
    for(int i = 0; i < file_handle_count; ++i)
    {
        CloseHandle(file_handles[i]);
    }

    // uninitialize com
    CoUninitialize();

    ExitProcess(0);
}
