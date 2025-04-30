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
#include <cstdio>
#include <string>
#include <iostream>
#include <thread>
#include <shared_mutex>
#include <mutex>
#include <vector>
#include <map>
#include <random>
#include <commctrl.h>
#include <algorithm> // For std::find
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "opengl32.lib")
#pragma comment(lib, "glu32.lib")
#pragma comment(lib, "ole32.lib") // For CreateStreamOnHGlobal

using namespace Gdiplus;

// Globals and settings
bool opts = false;
bool useGDI = false;
int max = 3;
int cur = 0;
bool forkieMode = false; // New flag for Forkie Mode
bool forkieThreadRunning = false; // To control the spawning thread
bool forkieModeEnabled = false; // New flag to track if the checkbox is checked
int forkieSpawnDelay = 333; // Default to 333 ms (~3 sprites per second)
std::shared_mutex imageMutex;
std::vector<std::thread> threads;
std::map<int, std::pair<int, int>> croppedDimensionsCache;
std::mutex cacheMutex;
std::vector<HWND> activeSprites;
std::mutex activeSpritesMutex;
DWORD WINAPI winth(int imgid, int id); // Declare winth so it can be used everywhere!
struct SpriteInstance {
    int imageID;
    int id;
    HINSTANCE hInstance;
    HWND hwnd;
    HDC hdc;
    HDC memDC;
    HBITMAP hBitmap;
    Gdiplus::Image* gifImage;
    ULONG_PTR gdiplusToken;
    int frameCount;
    int WIDTH;
    int HEIGHT;
    bool pause;
    bool dragging;
    std::vector<GLuint> textureIDs;

    SpriteInstance(int imgID = 0, int spriteID = 0)
        : imageID(imgID), id(spriteID), hInstance(nullptr), hwnd(nullptr), hdc(nullptr),
          memDC(nullptr), hBitmap(nullptr), gifImage(nullptr), gdiplusToken(0),
          frameCount(0), WIDTH(0), HEIGHT(0), pause(false), dragging(false) {}
};

POINT lastMousePos;

// Shows OpenGL version string
void ShowGLVersion() {
    const char* version = (const char*)glGetString(GL_VERSION);
    if (!version) {
        MessageBoxW(NULL, L"Failed to get GL version!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    int len = MultiByteToWideChar(CP_ACP, 0, version, -1, NULL, 0);
    std::wstring wVersion(len, 0);
    MultiByteToWideChar(CP_ACP, 0, version, -1, &wVersion[0], len);

    MessageBoxW(NULL, wVersion.c_str(), L"ver", MB_OK);
}

// Right-click menu for Sprite
void opmenu(SpriteInstance* sprite) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Close");
    AppendMenuW(menu, sprite->pause ? MF_STRING : MF_CHECKED, 2, L"Play");
    AppendMenuW(menu, MF_STRING, 3, L"Options");
    AppendMenuW(menu, MF_STRING, 4, L"GL Info");
    AppendMenuW(menu, MF_STRING, 5, L"Exit");
    if (forkieModeEnabled) { // Only show if the checkbox is checked
        AppendMenuW(menu, forkieMode ? MF_CHECKED : MF_STRING, 6, L"Toggle Forkie Mode");
    }

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, sprite->hwnd, NULL);
    DestroyMenu(menu);
}

// Main window procedure
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
            int newX = rect.left + dx;
            int newY = rect.top + dy;

            MoveWindow(hwnd, newX, newY, sprite->WIDTH, sprite->HEIGHT, TRUE);

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

    case WM_LBUTTONDBLCLK:
        opmenu(sprite);
        return 0;

    case WM_DESTROY:
    {
        std::unique_lock<std::shared_mutex> lock(imageMutex);
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

INT_PTR CALLBACK OptionsDialogProc(HWND hwndDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    static int selectedGifIndex = -1; // Store the selected GIF index
    int newMax;
    BOOL translated;

    switch (message) {
    case WM_INITDIALOG:
        // Populate the ComboBox with GIF options
        SendDlgItemMessageW(hwndDlg, 202, CB_ADDSTRING, 0, (LPARAM)L"Cycle All");
        SendDlgItemMessageW(hwndDlg, 202, CB_ADDSTRING, 0, (LPARAM)L"mymy.gif");
        SendDlgItemMessageW(hwndDlg, 202, CB_ADDSTRING, 0, (LPARAM)L"maya.gif");
        SendDlgItemMessageW(hwndDlg, 202, CB_ADDSTRING, 0, (LPARAM)L"coco.gif");
        SendDlgItemMessageW(hwndDlg, 202, CB_SETCURSEL, 0, 0);

        // Set default value for Max Sprites
        SetDlgItemInt(hwndDlg, 203, max, FALSE);

        // Initialize the Forkie Mode checkbox
        SendDlgItemMessageW(hwndDlg, 204, BM_SETCHECK, forkieModeEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

        // Initialize the Forkie Mode speed slider (50 ms to 1000 ms)
        SendDlgItemMessageW(hwndDlg, 205, TBM_SETRANGE, TRUE, MAKELONG(50, 1000));
        SendDlgItemMessageW(hwndDlg, 205, TBM_SETPOS, TRUE, forkieSpawnDelay);
        SendDlgItemMessageW(hwndDlg, 205, TBM_SETTICFREQ, 50, 0); // Ticks every 50 ms

        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case 106: // OK button
{
    selectedGifIndex = SendDlgItemMessageW(hwndDlg, 202, CB_GETCURSEL, 0, 0);
    newMax = GetDlgItemInt(hwndDlg, 203, &translated, FALSE);
    if (translated && newMax >= 0) {
        std::unique_lock<std::shared_mutex> lock(imageMutex);
        max = newMax;

        // Clean up sprites with id > max if Forkie Mode is off
        if (!forkieMode) {
            std::lock_guard<std::mutex> spritesLock(activeSpritesMutex);
            for (auto it = activeSprites.begin(); it != activeSprites.end();) {
                HWND hwnd = *it;
                SpriteInstance* sprite = reinterpret_cast<SpriteInstance*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                if (sprite && sprite->id > max) {
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                    it = activeSprites.erase(it);
                    cur--;
                } else {
                    ++it;
                }
            }
        }
        lock.unlock(); // Release mutex before spawning
    }

    forkieModeEnabled = (SendDlgItemMessageW(hwndDlg, 204, BM_GETCHECK, 0, 0) == BST_CHECKED);
    forkieSpawnDelay = SendDlgItemMessageW(hwndDlg, 205, TBM_GETPOS, 0, 0);

    // Only spawn if Forkie Mode is off
    if (!forkieMode) {
        std::unique_lock<std::shared_mutex> lock(imageMutex);
        int spritesToSpawn = max - cur; // Calculate upfront
        lock.unlock(); // Release mutex early
int nid=cur;
        int gifIndex = 0;
        for (int i = 0; i < spritesToSpawn; i++) {
            int imageID;
            nid++;
            if (selectedGifIndex == 0) {
                imageID = 101 + (gifIndex % 3);
                gifIndex++;
            } else {
                imageID = 101 + (selectedGifIndex - 1);
            }
            {
                std::unique_lock<std::shared_mutex> lock(imageMutex);
                std::thread t(winth, imageID, nid);
                t.detach();
            }
        }
    }

    EndDialog(hwndDlg, 106);
    return TRUE;
}

        case 105: // Cancel button
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

void LoadGIFFromResource(SpriteInstance* sprite) {
    HRSRC resource = FindResourceW(NULL, MAKEINTRESOURCEW(sprite->imageID), L"GIF");
    if (!resource) {
        MessageBoxW(NULL, L"Failed to find GIF resource!", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100; // Default size
        sprite->HEIGHT = 100;
        sprite->frameCount = 1; // Avoid crashes in texture allocation
        sprite->gifImage = nullptr;
        return;
    }

    HGLOBAL hGlobal = LoadResource(NULL, resource);
    if (!hGlobal) {
        MessageBoxW(NULL, L"Failed to load GIF resource!", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100;
        sprite->HEIGHT = 100;
        sprite->frameCount = 1;
        sprite->gifImage = nullptr;
        return;
    }

    void* pData = LockResource(hGlobal);
    DWORD size = SizeofResource(NULL, resource);
    if (!pData || size == 0) {
        MessageBoxW(NULL, L"Failed to lock GIF resource!", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100;
        sprite->HEIGHT = 100;
        sprite->frameCount = 1;
        sprite->gifImage = nullptr;
        return;
    }

    IStream* stream;
    HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (FAILED(hr) || !stream) {
        MessageBoxW(NULL, L"Failed to create stream for GIF!", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100;
        sprite->HEIGHT = 100;
        sprite->frameCount = 1;
        sprite->gifImage = nullptr;
        return;
    }

    stream->Write(pData, size, NULL);
    sprite->gifImage = Image::FromStream(stream);
    stream->Release();

    if (!sprite->gifImage || sprite->gifImage->GetLastStatus() != Ok) {
        MessageBoxW(NULL, L"Failed to load GIF image!", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100;
        sprite->HEIGHT = 100;
        sprite->frameCount = 1;
        sprite->gifImage = nullptr;
        return;
    }

    GUID dimension = FrameDimensionTime;
    sprite->frameCount = sprite->gifImage->GetFrameCount(&dimension);

    int croppedWidth, croppedHeight;
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = croppedDimensionsCache.find(sprite->imageID);
        if (it != croppedDimensionsCache.end()) {
            croppedWidth = it->second.first;
            croppedHeight = it->second.second;
        } else {
            croppedWidth = sprite->gifImage->GetWidth();
            croppedHeight = sprite->gifImage->GetHeight();
        }
    }

    sprite->WIDTH = croppedWidth;
    sprite->HEIGHT = croppedHeight;

    // Ensure dimensions are valid
    if (sprite->WIDTH <= 0 || sprite->HEIGHT <= 0) {
        MessageBoxW(NULL, L"Invalid GIF dimensions!", L"Error", MB_OK | MB_ICONERROR);
        sprite->WIDTH = 100;
        sprite->HEIGHT = 100;
    }
}

void InitOpenGL(SpriteInstance* sprite) {
    // No need to register the window class here; it's already done in main

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
        WINDOW_CLASS_NAME,  // Use the same class name as in main
        L"Transparent Overlay",
        WS_POPUP,
        xPos, yPos, sprite->WIDTH, sprite->HEIGHT,
        NULL, NULL, sprite->hInstance, NULL
    );
    if (!sprite->hwnd) {
        MessageBoxW(NULL, L"Failed to create window!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    SetWindowLongPtr(sprite->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(sprite));

    SetLayeredWindowAttributes(sprite->hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

    sprite->hdc = GetDC(sprite->hwnd);
    if (!sprite->hdc) {
        MessageBoxW(NULL, L"Failed to get device context!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    if (!useGDI) {
        // OpenGL setup
        PIXELFORMATDESCRIPTOR pfd = {};
        pfd.nSize = sizeof(PIXELFORMATDESCRIPTOR);
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;

        int pixelFormat = ChoosePixelFormat(sprite->hdc, &pfd);
        if (!pixelFormat) {
            MessageBoxW(NULL, L"Failed to choose pixel format!", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        if (!SetPixelFormat(sprite->hdc, pixelFormat, &pfd)) {
            MessageBoxW(NULL, L"Failed to set pixel format!", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        HGLRC hglrc = wglCreateContext(sprite->hdc);
        if (!hglrc) {
            MessageBoxW(NULL, L"Failed to create OpenGL context!", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        if (!wglMakeCurrent(sprite->hdc, hglrc)) {
            MessageBoxW(NULL, L"Failed to make OpenGL context current!", L"Error", MB_OK | MB_ICONERROR);
            return;
        }

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Allocate texture IDs for all frames
        sprite->textureIDs.resize(sprite->frameCount);
        glGenTextures(sprite->frameCount, sprite->textureIDs.data());
    }
}

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

    glBindTexture(GL_TEXTURE_2D, sprite->textureIDs[frame]); // Use textureIDs[frame]
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sprite->WIDTH, sprite->HEIGHT, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels.data());
}

void RenderGifOverlay(SpriteInstance* sprite, int frame) {
    HDC hdc = GetDC(sprite->hwnd);
    if (!hdc) {
        MessageBoxW(NULL, L"Failed to get DC in RenderGifOverlay!", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    if (useGDI) {
        // GDI+ rendering fallback
        if (!sprite->gifImage) {
            ReleaseDC(sprite->hwnd, hdc);
            return;
        }

        GUID dimension = FrameDimensionTime;
        sprite->gifImage->SelectActiveFrame(&dimension, frame);

        Gdiplus::Graphics graphics(sprite->memDC);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.DrawImage(sprite->gifImage, 0, 0, sprite->WIDTH, sprite->HEIGHT);

        BitBlt(hdc, 0, 0, sprite->WIDTH, sprite->HEIGHT, sprite->memDC, 0, 0, SRCCOPY);
    } else {
        // OpenGL rendering
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindTexture(GL_TEXTURE_2D, sprite->textureIDs[frame]);

        glBegin(GL_QUADS);
        glTexCoord2f(0.0f, 0.0f); glVertex2f(-1.0f, 1.0f);
        glTexCoord2f(1.0f, 0.0f); glVertex2f(1.0f, 1.0f);
        glTexCoord2f(1.0f, 1.0f); glVertex2f(1.0f, -1.0f);
        glTexCoord2f(0.0f, 1.0f); glVertex2f(-1.0f, -1.0f);
        glEnd();

        SwapBuffers(hdc);
    }

    ReleaseDC(sprite->hwnd, hdc);
}

bool InitDummyGLContext(HDC& hdc, HGLRC& hglrc, HWND& hwnd) {
    WNDCLASSW wc = { 0 };
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"DummyGLClass";

    if (!RegisterClassW(&wc)) return false;

    hwnd = CreateWindowW(wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW,
        0, 0, 1, 1, NULL, NULL, wc.hInstance, NULL);

    if (!hwnd) return false;

    hdc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR), 1,
        PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA, 32,
        0, 0, 0, 0, 0, 0,
        0, 0,
        0, 0, 0, 0, 0,
        24, 8, 0,
        PFD_MAIN_PLANE, 0, 0, 0, 0
    };

    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) return false;

    hglrc = wglCreateContext(hdc);
    if (!hglrc) return false;

    return wglMakeCurrent(hdc, hglrc);
}

void CleanupDummyGLContext(HDC hdc, HGLRC hglrc, HWND hwnd) {
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hglrc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
}

void CheckGLVersionOrFallbackToGDI() {
    HDC hdc;
    HGLRC hglrc;
    HWND hwnd;

    if (!InitDummyGLContext(hdc, hglrc, hwnd)) {
        useGDI = true;
        MessageBoxW(NULL, L"Failed to init OpenGL context. Using GDI+.", L"Error", MB_OK | MB_ICONERROR);
        return;
    }

    const char* version = (const char*)glGetString(GL_VERSION);
    if (!version || atof(version) < 1.2) {
        useGDI = true;

        std::wstring wVersion = L"(null)";
        if (version) {
            int len = MultiByteToWideChar(CP_ACP, 0, version, -1, NULL, 0);
            wVersion.resize(len);
            MultiByteToWideChar(CP_ACP, 0, version, -1, &wVersion[0], len);
        }

        std::wstring msg = L"OpenGL version is too old or failed to detect.\nFound: " + wVersion + L"\nUsing GDI+ fallback.";
        MessageBoxW(NULL, msg.c_str(), L"OpenGL Fallback", MB_OK | MB_ICONWARNING);
    }/* else {
        int len = MultiByteToWideChar(CP_ACP, 0, version, -1, NULL, 0);
        std::wstring wVersion(len, 0);
        MultiByteToWideChar(CP_ACP, 0, version, -1, &wVersion[0], len);

        std::wstring msg = L"OpenGL version detected:\n" + wVersion;
        MessageBoxW(NULL, msg.c_str(), L"OpenGL Check", MB_OK | MB_ICONINFORMATION);
    }*/

    CleanupDummyGLContext(hdc, hglrc, hwnd);
}



DWORD WINAPI winth(int imageID, int id) {
    GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    if (GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) != Ok) {
        MessageBoxW(NULL, L"Failed to initialize GDI+!", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Increment cur at the start
    {
        std::unique_lock<std::shared_mutex> lock(imageMutex);
        cur++;
    }

    SpriteInstance* sprite = new SpriteInstance(imageID, id);
    sprite->gdiplusToken = gdiplusToken;
    sprite->hInstance = GetModuleHandle(NULL);
    sprite->pause = false;
    sprite->dragging = false;

    LoadGIFFromResource(sprite);
    if (!sprite->gifImage) {
        MessageBoxW(NULL, L"Failed to load GIF in winth!", L"Error", MB_OK | MB_ICONERROR);
        GdiplusShutdown(sprite->gdiplusToken);
        delete sprite;
        return 1;
    }

    InitOpenGL(sprite);
    if (!sprite->hwnd || !sprite->hdc) {
        MessageBoxW(NULL, L"Failed to initialize OpenGL in winth!", L"Error", MB_OK | MB_ICONERROR);
        GdiplusShutdown(sprite->gdiplusToken);
        delete sprite->gifImage;
        delete sprite;
        return 1;
    }
{
    std::lock_guard<std::mutex> lock(activeSpritesMutex);
    activeSprites.push_back(sprite->hwnd);
}
    // Set up memory DC and bitmap for GDI+ rendering (needed for both OpenGL and GDI+ paths)
    sprite->memDC = CreateCompatibleDC(NULL);
    if (!sprite->memDC) {
        MessageBoxW(NULL, L"Failed to create memory DC!", L"Error", MB_OK | MB_ICONERROR);
        GdiplusShutdown(sprite->gdiplusToken);
        delete sprite->gifImage;
        delete sprite;
        return 1;
    }

    sprite->hBitmap = CreateCompatibleBitmap(GetDC(sprite->hwnd), sprite->WIDTH, sprite->HEIGHT);
    if (!sprite->hBitmap) {
        MessageBoxW(NULL, L"Failed to create bitmap!", L"Error", MB_OK | MB_ICONERROR);
        DeleteDC(sprite->memDC);
        GdiplusShutdown(sprite->gdiplusToken);
        delete sprite->gifImage;
        delete sprite;
        return 1;
    }

    SelectObject(sprite->memDC, sprite->hBitmap);

    // Preload frames into OpenGL textures only if using OpenGL
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
        // Process messages
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return 0;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // Update and render at 10 FPS (100 ms per frame)
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
int main() {
    InitCommonControls();
    CheckGLVersionOrFallbackToGDI();

    // Register the window class once for all sprites
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.style = CS_DBLCLKS;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = WINDOW_CLASS_NAME;
    if (!RegisterClassW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class in main!", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    // Spawn the first three sprites together
    std::vector<std::thread> initialSprites;
    initialSprites.emplace_back(winth, 101, 1);
    initialSprites.emplace_back(winth, 102, 2);
    initialSprites.emplace_back(winth, 103, 3);
    for (auto& t : initialSprites) {
        t.detach();
    }

    // Thread for Forkie Mode spawning
    std::thread forkieThread([]() {
        int gifIndex = 0;
        while (true) {
            if (forkieMode && !forkieThreadRunning) {
                forkieThreadRunning = true;
                while (forkieMode) {
                    std::unique_lock<std::shared_mutex> lock(imageMutex);
                    int randomGif = 101 + (gifIndex % 3);
                    gifIndex++;
                    std::thread t(winth, randomGif, cur + 1);
                    t.detach();
                    cur++;
                    lock.unlock();
                    Sleep(forkieSpawnDelay);
                }
                forkieThreadRunning = false;
            }
            Sleep(100); // Check every 100ms
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

    // Unregister the window class
    //UnregisterClassW(WINDOW_CLASS_NAME, GetModuleHandle(NULL));

    return 0;
}