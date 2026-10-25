#include "framework.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// Идентификаторы команд меню и кнопок
#define IDM_MAPPING_START  32771
#define IDM_MAPPING_STOP   32772
#define IDM_MAPPING_READ   32773
#define ID_BTN_ACTION      2001
#define BUFFER_SIZE        2048

#ifndef SM_CYBUTTON
#define SM_CYBUTTON SM_CYSIZE
#endif

const LPCWSTR MAPPING_NAME = L"Local\\myFileMapping";
const LPCWSTR EVENT_NAME = L"Local\\myFileMappingEvent";

// -------------------------------------------------------------
// 1. ЛОГИКА СЕРВЕРА (SERVER)
// -------------------------------------------------------------
static HWND   g_hServerEdit = nullptr;
static HANDLE g_hMapFile = nullptr;
static HANDLE g_hEvent = nullptr;
static HANDLE g_hWatchThread = nullptr;
static volatile bool g_bRunning = false;
static wchar_t g_szServerLog[8192] = L"Лабораторна робота - Відображення (проекція) файлу в пам'ять.\r\n";

void AppendServerLog(const wchar_t* text)
{
    size_t currentLen = wcslen(g_szServerLog);
    if (currentLen + wcslen(text) + 4 < 8192)
    {
        wcscat_s(g_szServerLog, text);
        SetWindowTextW(g_hServerEdit, g_szServerLog);
        SendMessageW(g_hServerEdit, EM_SETSEL, (WPARAM)wcslen(g_szServerLog), (LPARAM)wcslen(g_szServerLog));
        SendMessageW(g_hServerEdit, EM_SCROLLCARET, 0, 0);
    }
}

void ReadSharedMemory()
{
    if (!g_hMapFile)
    {
        MessageBoxW(NULL, L"Сервер не запущено! Проекція пам'яті відсутня.", L"Map", MB_ICONWARNING);
        return;
    }

    LPVOID pBuf = MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, BUFFER_SIZE);
    if (pBuf == nullptr)
    {
        MessageBoxW(NULL, L"Подання проектованого файлу неможливе", L"Map", MB_OK);
        return;
    }

    wchar_t* pText = (wchar_t*)pBuf;
    if (wcslen(pText) > 0)
    {
        AppendServerLog(L"\r\n------------------------\r\n");
        AppendServerLog(pText);
        AppendServerLog(L"\r\n");
    }

    UnmapViewOfFile(pBuf);
}

// Поток автоматического отслеживания изменений (задание «со звездочкой»)
DWORD WINAPI ServerWatchThread(LPVOID lpParam)
{
    HWND hWnd = (HWND)lpParam;
    while (g_bRunning)
    {
        DWORD dwWait = WaitForSingleObject(g_hEvent, 500);
        if (dwWait == WAIT_OBJECT_0 && g_bRunning)
        {
            ReadSharedMemory();
            ResetEvent(g_hEvent);
        }
    }
    return 0;
}

LRESULT CALLBACK ServerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hServerEdit = CreateWindowW(L"EDIT", g_szServerLog,
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
            15, 55, 490, 560, hWnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        CreateWindowW(L"BUTTON", L"Запустити (Start)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            15, 12, 150, 32, hWnd, (HMENU)IDM_MAPPING_START, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Прочитати (Read)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            180, 12, 150, 32, hWnd, (HMENU)IDM_MAPPING_READ, NULL, NULL);
        CreateWindowW(L"BUTTON", L"Зупинити (Stop)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            345, 12, 160, 32, hWnd, (HMENU)IDM_MAPPING_STOP, NULL, NULL);

        HMENU hMenuBar = CreateMenu();
        HMENU hMenu = CreateMenu();
        AppendMenuW(hMenu, MF_STRING, IDM_MAPPING_START, L"Start");
        AppendMenuW(hMenu, MF_STRING, IDM_MAPPING_READ, L"Read");
        AppendMenuW(hMenu, MF_STRING, IDM_MAPPING_STOP, L"Stop");
        AppendMenuW(hMenuBar, MF_POPUP, (UINT_PTR)hMenu, L"FileMapping");
        SetMenu(hWnd, hMenuBar);
        break;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDM_MAPPING_START:
            if (g_hMapFile)
            {
                AppendServerLog(L"\r\n[Увага] Сервер FileMapping уже працює!\r\n");
                break;
            }

            g_hMapFile = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, BUFFER_SIZE, MAPPING_NAME);
            if (!g_hMapFile || g_hMapFile == INVALID_HANDLE_VALUE)
            {
                MessageBoxW(hWnd, L"Не можливо створити відображений у пам'яті об'єкт", L"Map", MB_OK);
                g_hMapFile = nullptr;
                break;
            }

            {
                LPVOID pBuf = MapViewOfFile(g_hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, BUFFER_SIZE);
                if (pBuf) { ZeroMemory(pBuf, BUFFER_SIZE); UnmapViewOfFile(pBuf); }
            }

            g_hEvent = CreateEventW(NULL, TRUE, FALSE, EVENT_NAME);
            g_bRunning = true;
            g_hWatchThread = CreateThread(NULL, 0, ServerWatchThread, hWnd, 0, NULL);

            AppendServerLog(L"\r\nСервер FileMapping запущено (Автовиявлення змін активне)\r\n");
            break;

        case IDM_MAPPING_READ:
            ReadSharedMemory();
            break;

        case IDM_MAPPING_STOP:
            if (g_hMapFile)
            {
                g_bRunning = false;
                if (g_hEvent) SetEvent(g_hEvent);
                if (g_hWatchThread)
                {
                    WaitForSingleObject(g_hWatchThread, 500);
                    CloseHandle(g_hWatchThread);
                    g_hWatchThread = nullptr;
                }
                if (g_hEvent) { CloseHandle(g_hEvent); g_hEvent = nullptr; }

                CloseHandle(g_hMapFile);
                g_hMapFile = nullptr;

                AppendServerLog(L"\r\nСервер FileMapping зупинено\r\n");
            }
            break;
        }
        break;
    }

    case WM_DESTROY:
        if (g_hMapFile)
        {
            g_bRunning = false;
            if (g_hEvent) SetEvent(g_hEvent);
            if (g_hWatchThread) { WaitForSingleObject(g_hWatchThread, 300); CloseHandle(g_hWatchThread); }
            if (g_hEvent) CloseHandle(g_hEvent);
            CloseHandle(g_hMapFile);
        }
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// -------------------------------------------------------------
// 2. ФУНКЦИЯ ЗАПИСИ В ОБЩУЮ ПАМЯТЬ
// -------------------------------------------------------------
bool WriteToSharedMemory(HWND hWndParent, const wchar_t* text, HWND hLogEdit)
{
    HANDLE hMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, MAPPING_NAME);
    if (!hMap || hMap == INVALID_HANDLE_VALUE)
    {
        MessageBoxW(hWndParent, L"Не можливо відкрити відображений у пам'яті об'єкт", L"Map", MB_OK);
        return false;
    }

    LPVOID pBuf = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, BUFFER_SIZE);
    if (!pBuf)
    {
        MessageBoxW(hWndParent, L"Подання проектованого файлу неможливе", L"Map", MB_OK);
        CloseHandle(hMap);
        return false;
    }

    size_t bytesToCopy = (wcslen(text) + 1) * sizeof(wchar_t);
    if (bytesToCopy > BUFFER_SIZE) bytesToCopy = BUFFER_SIZE;
    CopyMemory(pBuf, text, bytesToCopy);

    UnmapViewOfFile(pBuf);
    CloseHandle(hMap);

    HANDLE hEv = OpenEventW(EVENT_MODIFY_STATE, FALSE, EVENT_NAME);
    if (hEv)
    {
        SetEvent(hEv);
        CloseHandle(hEv);
    }

    wchar_t okMsg[1500];
    swprintf_s(okMsg, L"%s\r\n\r\n[Успішно записано у спільну пам'ять (проекцію)!]", text);
    SetWindowTextW(hLogEdit, okMsg);
    return true;
}

// -------------------------------------------------------------
// 3. ЛОГИКА КЛИЕНТА #1 (Вариант 7)
// -------------------------------------------------------------
static HWND g_hClient1Edit = nullptr;

void RefreshClient1Data(HWND hWnd)
{
    int cleanBoot = GetSystemMetrics(SM_CLEANBOOT);
    const wchar_t* bootStr = (cleanBoot == 0) ? L"Звичайний режим (Normal)" :
        (cleanBoot == 1) ? L"Безпечний режим (Safe Mode)" : L"Безпечний із мережею";

    int minWindowW = GetSystemMetrics(SM_CXMIN);

    HDC hdc = GetDC(hWnd);
    int screenW = GetDeviceCaps(hdc, HORZRES);
    ReleaseDC(hWnd, hdc);

    wchar_t buf[512];
    swprintf_s(buf, L"Дані від Клієнта №1 (Варіант 7):\r\n- Режим завантаження ОС (SM_CLEANBOOT) = %s (%d)\r\n- Мінімальна ширина вікна (SM_CXMIN) = %d px\r\n- Ширина екрану (GetDeviceCaps, HORZRES) = %d px",
        bootStr, cleanBoot, minWindowW, screenW);

    SetWindowTextW(g_hClient1Edit, buf);
}

LRESULT CALLBACK Client1WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hClient1Edit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
            12, 10, 440, 160, hWnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        CreateWindowW(L"BUTTON", L"Відправити дані у FileMapping", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 180, 440, 34, hWnd, (HMENU)ID_BTN_ACTION, NULL, NULL);

        RefreshClient1Data(hWnd);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_ACTION)
        {
            wchar_t text[1024];
            GetWindowTextW(g_hClient1Edit, text, 1024);
            WriteToSharedMemory(hWnd, text, g_hClient1Edit);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// -------------------------------------------------------------
// 4. ЛОГИКА КЛИЕНТА #2 (Вариант 7)
// -------------------------------------------------------------
static HWND g_hClient2Edit = nullptr;

void RefreshClient2Data(HWND hWnd)
{
    int btnH = GetSystemMetrics(SM_CYBUTTON);
    int minTrackW = GetSystemMetrics(SM_CXMINTRACK);

    HDC hdc = GetDC(hWnd);
    int screenH = GetDeviceCaps(hdc, VERTRES);
    ReleaseDC(hWnd, hdc);

    wchar_t buf[512];
    swprintf_s(buf, L"Дані від Клієнта №2 (Варіант 7):\r\n- Висота кнопок заголовка/меню (SM_CYBUTTON) = %d px\r\n- Ширина трекінгу (SM_CXMINTRACK) = %d px\r\n- Кількість рядків екрану (GetDeviceCaps, VERTRES) = %d px",
        btnH, minTrackW, screenH);

    SetWindowTextW(g_hClient2Edit, buf);
}

LRESULT CALLBACK Client2WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_hClient2Edit = CreateWindowW(L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_AUTOVSCROLL,
            12, 10, 440, 160, hWnd, NULL, ((LPCREATESTRUCT)lParam)->hInstance, NULL);

        CreateWindowW(L"BUTTON", L"Відправити дані у FileMapping", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            12, 180, 440, 34, hWnd, (HMENU)ID_BTN_ACTION, NULL, NULL);

        RefreshClient2Data(hWnd);
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BTN_ACTION)
        {
            wchar_t text[1024];
            GetWindowTextW(g_hClient2Edit, text, 1024);
            WriteToSharedMemory(hWnd, text, g_hClient2Edit);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

// -------------------------------------------------------------
// 5. ЗАПУСК ДОЧЕРНИХ КЛИЕНТОВ
// -------------------------------------------------------------
void SpawnProcess(const wchar_t* arg)
{
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    wchar_t cmdLine[MAX_PATH * 2];
    swprintf_s(cmdLine, L"\"%s\" %s", exePath, arg);

    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };

    if (CreateProcessW(NULL, cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
    {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

// -------------------------------------------------------------
// 6. ТОЧКА ВХОДА (wWinMain)
// -------------------------------------------------------------
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(nCmdShow);

    if (wcsstr(lpCmdLine, L"client1"))
    {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.lpfnWndProc = Client1WndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = L"Client1MappingClass";
        RegisterClassExW(&wcex);

        HWND hWnd = CreateWindowW(L"Client1MappingClass", L"Клієнт #1 (Варіант 7)",
            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 570, 40, 485, 275, NULL, NULL, hInstance, NULL);

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return (int)msg.wParam;
    }
    else if (wcsstr(lpCmdLine, L"client2"))
    {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.lpfnWndProc = Client2WndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = L"Client2MappingClass";
        RegisterClassExW(&wcex);

        HWND hWnd = CreateWindowW(L"Client2MappingClass", L"Клієнт #2 (Варіант 7)",
            WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, 570, 340, 485, 275, NULL, NULL, hInstance, NULL);

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return (int)msg.wParam;
    }
    else
    {
        WNDCLASSEXW wcex = { sizeof(wcex) };
        wcex.lpfnWndProc = ServerWndProc;
        wcex.hInstance = hInstance;
        wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
        wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wcex.lpszClassName = L"ServerMappingClass";
        RegisterClassExW(&wcex);

        HWND hWnd = CreateWindowW(L"ServerMappingClass", L"Сервер - FileMapping",
            WS_OVERLAPPEDWINDOW, 30, 40, 535, 680, NULL, NULL, hInstance, NULL);

        ShowWindow(hWnd, SW_SHOW);
        UpdateWindow(hWnd);

        // Автоматический запуск дочерних клиентов
        SpawnProcess(L"client1");
        SpawnProcess(L"client2");

        MSG msg;
        while (GetMessageW(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        return (int)msg.wParam;
    }
}