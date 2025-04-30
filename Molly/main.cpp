#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#define WINDOW_CLASS_NAME L"SpriteWindowClass"

#include <windows.h>
#include <gdiplus.h>
#include <gl/GL.h>
#include <gl/GLU.h>
#include <ole2.h>
#include <vector>
#include <mutex>
#include <random>
#include <thread>
#include <commctrl.h>
#include <algorithm>

using namespace Gdiplus;

// Function prototype
DWORD WINAPI winth(int);

// Globals
bool useGDI = false;
int max = 1;
int cur = 0;
bool forkieMode = false;
bool forkieThreadRunning = false;
bool forkieModeEnabled = false;
int forkieSpawnDelay = 333;
std::mutex activeSpritesMutex;
std::vector<HWND> activeSprites;
std::mutex imageMutex;
bool opts = false;

struct SpriteInstance {
    int id;
    HINSTANCE hInstance;
    HWND hwnd;
    HDC hdc;
    HDC memDC;
    HBITMAP hBitmap;
    Image* gifImage;
    ULONG_PTR gdiplusToken;
    int frameCount;
    int WIDTH;
    int HEIGHT;
    bool pause;
    bool dragging;
    std::vector<GLuint> textureIDs;

    SpriteInstance(int spriteID = 0)
        : id(spriteID), hInstance(nullptr), hwnd(nullptr), hdc(nullptr),
          memDC(nullptr), hBitmap(nullptr), gifImage(nullptr), gdiplusToken(0),
          frameCount(0), WIDTH(0), HEIGHT(0), pause(false), dragging(false) {}
};

POINT lastMousePos;

// Right-click menu
void opmenu(SpriteInstance* sprite) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Close");
    AppendMenuW(menu, sprite->pause ? MF_STRING : MF_CHECKED, 2, L"Play");
    AppendMenuW(menu, MF_STRING, 3, L"Options");
    AppendMenuW(menu, MF_STRING, 4, L"GL Info");
    AppendMenuW(menu, MF_STRING, 5, L"Exit");
    if (forkieModeEnabled) {
        AppendMenuW(menu, forkieMode ? MF_CHECKED : MF_STRING, 6, L"Toggle Forkie Mode");
    }

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, sprite->hwnd, NULL);
    DestroyMenu(menu);
}

// Show OpenGL version
void ShowGLVersion() {
    const char* version = (const char*)glGetString(GL_VERSION);
    if (!version) {
        MessageBoxW(NULL, L"Failed to get GL version! >w<", L"Error", MB_OK | MB_ICONERROR);
        return;
    }
    int len = MultiByteToWideChar(CP_ACP, 0, version, -1, NULL, 0);
    std::wstring wVersion(len, 0);
    MultiByteToWideChar(CP_ACP, 0, version, -1, &wVersion[0], len);
    MessageBoxW(NULL, wVersion.c_str(), L"OpenGL Version", MB_OK);
}

// Window procedure
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SpriteInstance* sprite = reinterpret_cast<SpriteInstance*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    switch (msg) {
    case WM_LBUTTONDOWN:
        SetCapture(hwnd);
        sprite->dragging = true;
        GetCursorPos(&lastMousePos);
        return 0;

    case WM_MOUSEMOVE:
        if (sprite->dragging) {
            POINT curPos;
            GetCursorPos(&curPos);
            int dx = curPos.x - lastMousePos.x;
            int dy = curPos.y - lastMousePos.y;
            RECT rect;
            GetWindowRect(hwnd, &rect);
            MoveWindow(hwnd, rect.left + dx, rect.top + dy, sprite->WIDTH, sprite->HEIGHT, TRUE);
            lastMousePos = curPos;
        }
        return 0;

    case WM_LBUTTONUP:
        sprite->dragging = false;
        ReleaseCapture();
        return 0;

    case WM_RBUTTONUP:
        opmenu(sprite);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 1: PostMessage(sprite->hwnd, WM_CLOSE, 0, 0); break;
        case 2: sprite->pause = !sprite->pause; break;
        case 3: opts = true; break;
        case 4: ShowGLVersion(); break;
        case 5: ExitProcess(0); break;
        case 6: forkieMode = !forkieMode; break;
        }
        return 0;

    case WM_DESTROY:
        {
            std::lock_guard<std::mutex> lock(imageMutex);
            cur--;
            {
                std::lock_guard<std::mutex> lock(activeSpritesMutex);
                auto it = std::find(activeSprites.begin(), activeSprites.end(), sprite->hwnd);
                if (it != activeSprites.end()) {
                    activeSprites.erase(it);
                }
            }
            GdiplusShutdown(sprite->gdiplusToken);
            delete sprite->gifImage;
            DeleteDC(sprite->memDC);
            DeleteObject(sprite->hBitmap);
            if (!useGDI) {
                glDeleteTextures(sprite->frameCount, sprite->textureIDs.data());
                HGLRC hglrc = wglGetCurrentContext();
                if (hglrc) {
                    wglMakeCurrent(NULL, NULL);
                    wglDeleteContext(hglrc);
                }
            }
            ReleaseDC(sprite->hwnd, sprite->hdc);
            delete sprite;
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Options dialog
INT_PTR CALLBACK OptionsDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    int newMax;
    BOOL translated;

    switch (message) {
    case WM_INITDIALOG:
        SetDlgItemInt(hwndDlg, 203, max, FALSE);
        SendDlgItemMessageW(hwndDlg, 204, BM_SETCHECK, forkieModeEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
        SendDlgItemMessageW(hwndDlg, 205, TBM_SETRANGE, TRUE, MAKELONG(50, 1000));
        SendDlgItemMessageW(hwndDlg, 205, TBM_SETPOS, TRUE, forkieSpawnDelay);
        SendDlgItemMessageW(hwndDlg, 205, TBM_SETTICFREQ, 50, 0);
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 106: // OK
            newMax = GetDlgItemInt(hwndDlg, 203, &translated, FALSE);
            if (translated && newMax >= 0) {
                std::lock_guard<std::mutex> lock(imageMutex);
                max = newMax;
                if (!forkieMode) {
                    std::lock_guard<std::mutex> spritesLock(activeSpritesMutex);
                    for (auto it = activeSprites.begin(); it != activeSprites.end();) {
                        HWND hwnd = *it;
                        SpriteInstance* s = reinterpret_cast<SpriteInstance*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                        if (s && s->id > max) {
                            PostMessage(hwnd, WM_CLOSE, 0, 0);
                            it = activeSprites.erase(it);
                            cur--;
                        } else {
                            ++it;
                        }
                    }
                    int spritesToSpawn = max - cur;
                    for (int i = 0; i < spritesToSpawn; i++) {
                        std::thread t(winth, cur + 1);
                        t.detach();
                    }
                }
            }
            forkieModeEnabled = (SendDlgItemMessageW(hwndDlg, 204, BM_GETCHECK, 0, 0) == BST_CHECKED);
            forkieSpawnDelay = SendDlgItemMessageW(hwndDlg, 205, TBM_GETPOS, 0, 0);
            EndDialog(hwndDlg, 106);
            return TRUE;
        case 105: // Cancel
            EndDialog(hwndDlg, 105);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hwndDlg, 0);
        return TRUE;
    }
    return FALSE;
}

// Load Molly GIF
void LoadGIFFromResource(SpriteInstance* sprite) {
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(101), L"GIF");
    if (!resource) {
        MessageBoxW(NULL, L"Couldn’t find Molly GIF! >.<", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100;
        sprite->HEIGHT = 100;
        sprite->frameCount = 1;
        return;
    }

    HGLOBAL hGlobal = LoadResource(NULL, resource);
    void* pData = LockResource(hGlobal);
    DWORD size = SizeofResource(NULL, resource);
    IStream* stream;
    CreateStreamOnHGlobal(NULL, TRUE, &stream);
    stream->Write(pData, size, NULL);
    sprite->gifImage = Image::FromStream(stream);
    stream->Release();

    if (!sprite->gifImage || sprite->gifImage->GetLastStatus() != Ok) {
        MessageBoxW(NULL, L"Failed to load Molly GIF! T_T", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100;
        sprite->HEIGHT = 100;
        sprite->frameCount = 1;
        return;
    }

    GUID dimension = FrameDimensionTime;
    sprite->frameCount = sprite->gifImage->GetFrameCount(&dimension);
    sprite->WIDTH = sprite->gifImage->GetWidth();
    sprite->HEIGHT = sprite->gifImage->GetHeight();

    if (sprite->WIDTH <= 0 || sprite->HEIGHT <= 0) {
        MessageBoxW(NULL, L"Invalid GIF size! >w<", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100;
        sprite->HEIGHT = 100;
    }
}

// Initialize OpenGL
void InitOpenGL(SpriteInstance* sprite) {
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int xPos, yPos;

    if (forkieMode) {
        thread_local std::mt19937 rng(std::random_device{}());
        std::uniform_int_distribution<int> distX(0, screenWidth - 1);
        std::uniform_int_distribution<int> distY(0, screenHeight - 1);
        xPos = distX(rng);
        yPos = distY(rng);
        if (xPos + sprite->WIDTH > screenWidth) xPos = screenWidth - sprite->WIDTH;
        if (yPos + sprite->HEIGHT > screenHeight) yPos = screenHeight - sprite->HEIGHT;
        if (xPos < 0) xPos = 0;
        if (yPos < 0) yPos = 0;
    } else {
        int effectiveWidth = static_cast<int>(sprite->WIDTH * 0.7);
        int spritesPerRow = screenWidth / effectiveWidth;
        if (spritesPerRow == 0) spritesPerRow = 1;
        int row = (sprite->id - 1) / spritesPerRow;
        int col = (sprite->id - 1) % spritesPerRow;
        xPos = screenWidth - effectiveWidth - (col * effectiveWidth) - 32;
        yPos = screenHeight - sprite->HEIGHT - (row * sprite->HEIGHT);
        if (xPos < 0) xPos = 0;
        if (yPos < 0) yPos = 0;
        if (xPos + sprite->WIDTH > screenWidth) xPos = screenWidth - sprite->WIDTH;
        if (yPos + sprite->HEIGHT > screenHeight) yPos = screenHeight - sprite->HEIGHT;
    }

    sprite->hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        WINDOW_CLASS_NAME,
        L"Molly McGee Sprite",
        WS_POPUP,
        xPos, yPos, sprite->WIDTH, sprite->HEIGHT,
        NULL, NULL, sprite->hInstance, NULL
    );
    if (!sprite->hwnd) {
        MessageBoxW(NULL, L"Window creation failed! >.<", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    SetWindowLongPtr(sprite->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(sprite));
    SetLayeredWindowAttributes(sprite->hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    sprite->hdc = GetDC(sprite->hwnd);

    if (!useGDI) {
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;

        int pixelFormat = ChoosePixelFormat(sprite->hdc, &pfd);
        SetPixelFormat(sprite->hdc, pixelFormat, &pfd);
        HGLRC hglrc = wglCreateContext(sprite->hdc);
        wglMakeCurrent(sprite->hdc, hglrc);

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        sprite->textureIDs.resize(sprite->frameCount);
        glGenTextures(sprite->frameCount, sprite->textureIDs.data());
    }
}

// Load frame to texture
void LoadFrameToTexture(SpriteInstance* sprite, int frame) {
    GUID dimension = FrameDimensionTime;
    sprite->gifImage->SelectActiveFrame(&dimension, frame);
    Gdiplus::Graphics graphics(sprite->memDC);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
    graphics.DrawImage(sprite->gifImage, 0, 0, sprite->WIDTH, sprite->HEIGHT);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = sprite->WIDTH;
    bmi.bmiHeader.biHeight = -sprite->HEIGHT;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    std::vector<BYTE> pixels(sprite->WIDTH * sprite->HEIGHT * 4);
    GetDIBits(sprite->memDC, sprite->hBitmap, 0, sprite->HEIGHT, pixels.data(), &bmi, DIB_RGB_COLORS);

    glBindTexture(GL_TEXTURE_2D, sprite->textureIDs[frame]);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sprite->WIDTH, sprite->HEIGHT, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
}

// Render GIF
void RenderGifOverlay(SpriteInstance* sprite, int frame) {
    HDC hdc = GetDC(sprite->hwnd);
    if (!hdc) return;

    if (useGDI) {
        GUID dimension = FrameDimensionTime;
        sprite->gifImage->SelectActiveFrame(&dimension, frame);
        Gdiplus::Graphics graphics(sprite->memDC);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.DrawImage(sprite->gifImage, 0, 0, sprite->WIDTH, sprite->HEIGHT);
        BitBlt(hdc, 0, 0, sprite->WIDTH, sprite->HEIGHT, sprite->memDC, 0, 0, SRCCOPY);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, sprite->textureIDs[frame]);
        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
        glEnd();
        SwapBuffers(hdc); // Fixed typo
    }
    ReleaseDC(sprite->hwnd, hdc);
}

// Check OpenGL version
void CheckGLVersionOrFallbackToGDI() {
    HDC hdc;
    HGLRC hglrc;
    HWND hwnd;
    WNDCLASSW wc = { 0 };
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DummyGLClass";
    RegisterClassW(&wc);
    hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);
    hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = { sizeof(PIXELFORMATDESCRIPTOR), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER, PFD_TYPE_RGBA, 32 };
    int pf = ChoosePixelFormat(hdc, &pfd);
    SetPixelFormat(hdc, pf, &pfd);
    hglrc = wglCreateContext(hdc);
    wglMakeCurrent(hdc, hglrc);

    const char* version = (const char*)glGetString(GL_VERSION);
    if (!version || atof(version) < 1.2) {
        useGDI = true;
        MessageBoxW(NULL, L"OpenGL too old! Using GDI+~", L"Warning", MB_OK | MB_ICONWARNING);
    }

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hglrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
}

// Sprite thread
DWORD WINAPI winth(int id) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    {
        std::lock_guard<std::mutex> lock(imageMutex);
        cur++;
    }

    SpriteInstance* sprite = new SpriteInstance(id);
    sprite->gdiplusToken = gdiplusToken;
    sprite->hInstance = GetModuleHandle(NULL);

    LoadGIFFromResource(sprite);
    if (!sprite->gifImage) {
        GdiplusShutdown(sprite->gdiplusToken);
        delete sprite;
        return 1;
    }

    InitOpenGL(sprite);
    if (!sprite->hwnd || !sprite->hdc) {
        GdiplusShutdown(sprite->gdiplusToken);
        delete sprite->gifImage;
        delete sprite;
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(activeSpritesMutex);
        activeSprites.push_back(sprite->hwnd);
    }

    sprite->memDC = CreateCompatibleDC(NULL);
    sprite->hBitmap = CreateCompatibleBitmap(GetDC(sprite->hwnd), sprite->WIDTH, sprite->HEIGHT);
    SelectObject(sprite->memDC, sprite->hBitmap);

    if (!useGDI) {
        for (int frame = 0; frame < sprite->frameCount; frame++) {
            LoadFrameToTexture(sprite, frame);
        }
    }

    ShowWindow(sprite->hwnd, SW_SHOW);
    UpdateWindow(sprite->hwnd);

    int frame = 0;
    MSG msg;
    DWORD lastTick = GetTickCount();
    while (true) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        DWORD currentTick = GetTickCount();
        if (currentTick - lastTick >= 100) {
            if (!sprite->pause) {
                frame = (frame + 1) % sprite->frameCount;
                RenderGifOverlay(sprite, frame);
            }
            lastTick = currentTick;
        }
        Sleep(1);
    }
    return 0;
}

// Main
int main() {
    InitCommonControls();
    CheckGLVersionOrFallbackToGDI();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.style = CS_DBLCLKS;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = WINDOW_CLASS_NAME;
    RegisterClassW(&wc);

    // Spawn three Molly sprites
    //for (int i = 1; i <= 3; i++) {
        std::thread t(winth, 1);
        t.detach();
    //}

    // Forkie Mode thread
    std::thread forkieThread([]() {
        while (true) {
            if (forkieMode && !forkieThreadRunning) {
                forkieThreadRunning = true;
                while (forkieMode) {
                    {
                        std::lock_guard<std::mutex> lock(imageMutex);
                        std::thread t(winth, cur + 1);
                        t.detach();
                        cur++;
                    }
                    Sleep(forkieSpawnDelay);
                }
                forkieThreadRunning = false;
            }
            Sleep(100);
        }
    });
    forkieThread.detach();

    while (true) {
        if (opts) {
            DialogBox(GetModuleHandle(NULL), MAKEINTRESOURCE(104), NULL, OptionsDialogProc);
            opts = false;
        }
        Sleep(1000);
    }
    return 0;
}