#include "MainWindow.h"

#include "yuanqin/core/ScoreParser.h"
#include "yuanqin/playback/GenshinWindowTarget.h"
#include "yuanqin/playback/MusicPlayer.h"
#include "yuanqin/playback/VirtualHidKeySender.h"

#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <utility>

namespace yuanqin::app {
namespace {

constexpr wchar_t kMainWindowClassName[] = L"YuanQinMainWindow";
constexpr int kHeaderHeight = 160;
constexpr int kFooterHeight = 86;
constexpr int kEditorControlId = 1001;
constexpr int kBpmControlId = 1002;
constexpr UINT kPlaybackProgressMessage = WM_APP + 11;
constexpr UINT kPlaybackCompleteMessage = WM_APP + 12;

constexpr COLORREF kDeepTeal = RGB(17, 73, 94);
constexpr COLORREF kOcean = RGB(31, 139, 174);
constexpr COLORREF kAqua = RGB(94, 194, 211);
constexpr COLORREF kPaleBlue = RGB(224, 242, 247);
constexpr COLORREF kLavender = RGB(151, 140, 205);
constexpr COLORREF kSilver = RGB(242, 247, 250);
constexpr COLORREF kMuted = RGB(102, 142, 157);
constexpr COLORREF kWhite = RGB(255, 255, 255);

void fillRoundedRect(HDC context, const RECT& rect, int radius, COLORREF color) {
    const HBRUSH brush = CreateSolidBrush(color);
    const HPEN pen = CreatePen(PS_SOLID, 1, color);
    const auto oldBrush = SelectObject(context, brush);
    const auto oldPen = SelectObject(context, pen);
    RoundRect(context, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(context, oldPen);
    SelectObject(context, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void drawDiamond(HDC context, int centerX, int centerY, int radius, COLORREF color) {
    const POINT points[]{{centerX, centerY - radius}, {centerX + radius, centerY},
                         {centerX, centerY + radius}, {centerX - radius, centerY}};
    const HBRUSH brush = CreateSolidBrush(color);
    const auto oldBrush = SelectObject(context, brush);
    const auto oldPen = SelectObject(context, GetStockObject(NULL_PEN));
    Polygon(context, points, 4);
    SelectObject(context, oldPen);
    SelectObject(context, oldBrush);
    DeleteObject(brush);
}

std::wstring fileNameFor(const std::filesystem::path& path) {
    const auto name = path.filename().wstring();
    return name.empty() ? L"未命名" : name;
}

std::filesystem::path scoreDirectory() {
    std::array<wchar_t, 32768> executablePath{};
    const DWORD length = GetModuleFileNameW(nullptr, executablePath.data(),
                                            static_cast<DWORD>(executablePath.size()));
    if (length > 0 && length < executablePath.size()) {
        const auto executableDirectory = std::filesystem::path(executablePath.data()).parent_path();
        const auto besideExecutable = executableDirectory / L"sheetMusic";
        if (std::filesystem::is_directory(besideExecutable)) {
            return besideExecutable;
        }
        const auto projectDirectory = executableDirectory.parent_path() / L"sheetMusic";
        if (std::filesystem::is_directory(projectDirectory)) {
            return projectDirectory;
        }
    }

    const auto workingDirectory = std::filesystem::current_path() / L"sheetMusic";
    return std::filesystem::is_directory(workingDirectory)
        ? workingDirectory
        : std::filesystem::current_path();
}

std::wstring formatTime(std::size_t tick, double bpm) {
    const auto seconds = static_cast<unsigned long long>(
        bpm > 0.0 ? static_cast<double>(tick) * 60.0 / bpm / 4.0 : 0.0);
    std::wostringstream text;
    text << seconds / 60 << L':' << std::setfill(L'0') << std::setw(2) << seconds % 60;
    return text.str();
}

}  // namespace

MainWindow::~MainWindow() {
    stopPlayback();
}

bool MainWindow::create(HINSTANCE instance, int showCommand) {
    instance_ = instance;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hIconSm = windowClass.hIcon;
    windowClass.lpszClassName = kMainWindowClassName;
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        return false;
    }
    if (!ui::ScoreEditor::registerWindowClass(instance)) {
        return false;
    }

    const int width = 1280;
    const int height = 840;
    const int x = std::max(0, (GetSystemMetrics(SM_CXSCREEN) - width) / 2);
    const int y = std::max(0, (GetSystemMetrics(SM_CYSCREEN) - height) / 2);
    window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST, kMainWindowClassName,
                              L"幽歌琴谱", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                              x, y, width, height, nullptr, nullptr, instance, this);
    if (!window_) {
        return false;
    }

    applyWindowOpacity();
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ShowWindow(window_, showCommand);
    UpdateWindow(window_);
    return true;
}

bool MainWindow::processShortcut(const MSG& message) {
    if (message.message != WM_KEYDOWN || !(GetKeyState(VK_CONTROL) & 0x8000)) {
        return false;
    }

    switch (message.wParam) {
        case 'N': invoke(ToolbarAction::NewFile); return true;
        case 'O': invoke(ToolbarAction::OpenFile); return true;
        case 'S':
            invoke((GetKeyState(VK_SHIFT) & 0x8000) ? ToolbarAction::SaveAs
                                                    : ToolbarAction::Save);
            return true;
        case 'W':
            if (!documents_.empty()) {
                closeDocument(activeDocument_);
            }
            return true;
        default:
            return false;
    }
}

HWND MainWindow::handle() const noexcept {
    return window_;
}

LRESULT CALLBACK MainWindow::windowProcedure(HWND window, UINT message, WPARAM wParam,
                                              LPARAM lParam) {
    auto* mainWindow = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lParam);
        mainWindow = static_cast<MainWindow*>(creation->lpCreateParams);
        mainWindow->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(mainWindow));
    }
    return mainWindow ? mainWindow->handleMessage(message, wParam, lParam)
                      : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT MainWindow::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            createFonts();
            if (!editor_.create(window_, instance_, kEditorControlId)) {
                return -1;
            }
            bpmEdit_ = CreateWindowExW(0, L"EDIT", L"80",
                                       WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                                       0, 0, 0, 0, window_,
                                       reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBpmControlId)),
                                       instance_, nullptr);
            if (!bpmEdit_) {
                return -1;
            }
            SendMessageW(bpmEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(interfaceFont_), TRUE);
            bpmEditBrush_ = CreateSolidBrush(RGB(232, 247, 249));
            editor_.setChangedCallback([this] { markActiveDocumentChanged(); });
            editor_.setSelectionChangedCallback(
                [this](std::size_t tickIndex) { onEditorSelectionChanged(tickIndex); });
            newDocument();
            layoutChildren();
            return 0;

        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lParam) == bpmEdit_) {
                const HDC context = reinterpret_cast<HDC>(wParam);
                SetTextColor(context, kDeepTeal);
                SetBkColor(context, RGB(232, 247, 249));
                return reinterpret_cast<LRESULT>(bpmEditBrush_);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == kBpmControlId && HIWORD(wParam) == EN_CHANGE) {
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {880, 620};
            return 0;
        }

        case WM_SIZE:
            layoutChildren();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEACTIVATE: {
            if (LOWORD(lParam) == HTCAPTION) {
                const HWND gameWindow = playback::GenshinWindowTarget::find();
                if (playbackRunning_ ||
                    (gameWindow && GetForegroundWindow() == gameWindow)) {
                    return MA_NOACTIVATE;
                }
            }
            POINT cursor{};
            if (GetCursorPos(&cursor)) {
                ScreenToClient(window_, &cursor);
                if (shouldHandleWithoutActivation(cursor)) {
                    // Playback controls must remain usable while an exclusive-fullscreen
                    // game owns the foreground window. The click is delivered, but this
                    // overlay does not become active and therefore does not minimize it.
                    return MA_NOACTIVATE;
                }
            }
            break;
        }

        case WM_PAINT:
            paint();
            return 0;

        case WM_LBUTTONDOWN: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            RECT opacity = opacityBounds();
            RECT opacityHit = opacity;
            InflateRect(&opacityHit, 0, 12);
            if (PtInRect(&opacityHit, point)) {
                draggingOpacity_ = true;
                setWindowOpacityFromX(point.x);
                SetCapture(window_);
                return 0;
            }
            const RECT playPause = playPauseBounds();
            if (PtInRect(&playPause, point)) {
                togglePlayback();
                return 0;
            }
            const RECT fromBeginning = playFromBeginningBounds();
            if (PtInRect(&fromBeginning, point)) {
                playFromBeginning();
                return 0;
            }
            const RECT progress = progressBounds();
            RECT progressHit = progress;
            InflateRect(&progressHit, 0, 12);
            if (PtInRect(&progressHit, point)) {
                draggingProgress_ = true;
                dragWasPlaying_ = playbackRunning_;
                draggedTick_ = progressTickFromX(point.x);
                editor_.setPlayheadTick(draggedTick_, false);
                SetCapture(window_);
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            for (const auto& item : toolbarItems()) {
                if (PtInRect(&item.bounds, point)) {
                    invoke(item.action);
                    return 0;
                }
            }
            for (const auto& item : tabItems()) {
                if (PtInRect(&item.closeBounds, point)) {
                    closeDocument(item.index);
                    return 0;
                }
                if (PtInRect(&item.bounds, point)) {
                    setActiveDocument(item.index);
                    return 0;
                }
            }
            const RECT dragArea = headerDragBounds();
            if (PtInRect(&dragArea, point)) {
                draggingWindow_ = GetCursorPos(&windowDragStartCursor_) != FALSE &&
                                  GetWindowRect(window_, &windowDragStartBounds_) != FALSE;
                if (draggingWindow_) {
                    SetCapture(window_);
                }
                return 0;
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (draggingWindow_) {
                POINT cursor{};
                if (GetCursorPos(&cursor)) {
                    const int x = windowDragStartBounds_.left +
                                  cursor.x - windowDragStartCursor_.x;
                    const int y = windowDragStartBounds_.top +
                                  cursor.y - windowDragStartCursor_.y;
                    SetWindowPos(window_, HWND_TOPMOST, x, y, 0, 0,
                                 SWP_NOSIZE | SWP_NOACTIVATE);
                }
                return 0;
            }
            if (draggingOpacity_) {
                setWindowOpacityFromX(GET_X_LPARAM(lParam));
                return 0;
            }
            if (draggingProgress_) {
                draggedTick_ = progressTickFromX(GET_X_LPARAM(lParam));
                editor_.setPlayheadTick(draggedTick_, false);
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            break;

        case WM_LBUTTONUP:
            if (draggingWindow_) {
                draggingWindow_ = false;
                ReleaseCapture();
                return 0;
            }
            if (draggingOpacity_) {
                setWindowOpacityFromX(GET_X_LPARAM(lParam));
                draggingOpacity_ = false;
                ReleaseCapture();
                return 0;
            }
            if (draggingProgress_) {
                draggedTick_ = progressTickFromX(GET_X_LPARAM(lParam));
                draggingProgress_ = false;
                ReleaseCapture();
                seekTo(draggedTick_, dragWasPlaying_);
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            draggingWindow_ = false;
            draggingOpacity_ = false;
            if (draggingProgress_) {
                draggingProgress_ = false;
                seekTo(draggedTick_, dragWasPlaying_);
                return 0;
            }
            break;

        case kPlaybackProgressMessage:
            handlePlaybackProgress(static_cast<std::uint64_t>(wParam),
                                   static_cast<std::size_t>(lParam));
            return 0;

        case kPlaybackCompleteMessage:
            handlePlaybackComplete(static_cast<std::uint64_t>(wParam), static_cast<int>(lParam));
            return 0;

        case WM_CLOSE:
            stopPlayback();
            for (std::size_t index = documents_.size(); index > 0; --index) {
                if (!confirmClose(index - 1)) {
                    return 0;
                }
            }
            DestroyWindow(window_);
            return 0;

        case WM_DESTROY:
            stopPlayback();
            if (bpmEditBrush_) {
                DeleteObject(bpmEditBrush_);
                bpmEditBrush_ = nullptr;
            }
            destroyFonts();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(window_, message, wParam, lParam);
}

void MainWindow::createFonts() {
    titleFont_ = CreateFontW(-30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             VARIABLE_PITCH, L"Microsoft YaHei UI");
    subtitleFont_ = CreateFontW(-12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Segoe UI");
    interfaceFont_ = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Microsoft YaHei UI");
    smallFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             VARIABLE_PITCH, L"Microsoft YaHei UI");
}

void MainWindow::destroyFonts() {
    for (auto** font : {&titleFont_, &subtitleFont_, &interfaceFont_, &smallFont_}) {
        if (*font) {
            DeleteObject(*font);
            *font = nullptr;
        }
    }
}

void MainWindow::layoutChildren() {
    if (!window_ || !editor_.handle() || !bpmEdit_) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    MoveWindow(editor_.handle(), 0, kHeaderHeight, client.right,
               std::max(0, static_cast<int>(client.bottom) - kHeaderHeight - kFooterHeight), TRUE);
    MoveWindow(bpmEdit_, 178, client.bottom - kFooterHeight + 43, 54, 27, TRUE);
}

RECT MainWindow::playPauseBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    return {25, client.bottom - kFooterHeight + 36, 67, client.bottom - 10};
}

RECT MainWindow::playFromBeginningBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    return {75, client.bottom - kFooterHeight + 36, 117, client.bottom - 10};
}

RECT MainWindow::progressBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    return {264, client.bottom - 36, std::max(284L, client.right - 92), client.bottom - 28};
}

RECT MainWindow::opacityBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    const LONG footerTop = client.bottom - kFooterHeight;
    return {std::max(430L, client.right - 170), footerTop + 13,
            client.right - 28, footerTop + 19};
}

RECT MainWindow::headerDragBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    return {18, 76, std::max(19L, client.right - 18), 104};
}

bool MainWindow::shouldHandleWithoutActivation(POINT clientPoint) const {
    RECT playPause = playPauseBounds();
    RECT fromBeginning = playFromBeginningBounds();
    RECT progress = progressBounds();
    RECT opacity = opacityBounds();
    const RECT headerDrag = headerDragBounds();
    InflateRect(&progress, 0, 12);
    InflateRect(&opacity, 0, 12);
    if (PtInRect(&headerDrag, clientPoint) || PtInRect(&playPause, clientPoint) ||
        PtInRect(&fromBeginning, clientPoint) ||
        PtInRect(&progress, clientPoint) || PtInRect(&opacity, clientPoint)) {
        return true;
    }

    if (!editor_.handle()) {
        return false;
    }
    RECT editorBounds{};
    GetWindowRect(editor_.handle(), &editorBounds);
    MapWindowPoints(nullptr, window_, reinterpret_cast<POINT*>(&editorBounds), 2);
    if (!PtInRect(&editorBounds, clientPoint)) {
        return false;
    }
    if (playbackRunning_) {
        return true;
    }

    // A single cell click may move the paused play position without taking
    // exclusive-fullscreen focus away from a currently foreground game.
    const HWND gameWindow = playback::GenshinWindowTarget::find();
    return gameWindow && GetForegroundWindow() == gameWindow;
}

std::vector<MainWindow::ToolbarItem> MainWindow::toolbarItems() const {
    RECT client{};
    GetClientRect(window_, &client);
    struct Definition { ToolbarAction action; const wchar_t* label; int width; };
    constexpr Definition definitions[]{{ToolbarAction::NewFile, L"＋ 新建", 84},
                                        {ToolbarAction::OpenFile, L"打开", 78},
                                        {ToolbarAction::Save, L"保存", 78},
                                        {ToolbarAction::SaveAs, L"另存为", 92}};
    constexpr int gap = 10;
    int total = -gap;
    for (const auto& definition : definitions) {
        total += definition.width + gap;
    }
    int x = client.right - 32 - total;
    std::vector<ToolbarItem> result;
    for (const auto& definition : definitions) {
        result.push_back({{x, 30, x + definition.width, 68}, definition.action, definition.label});
        x += definition.width + gap;
    }
    return result;
}

std::vector<MainWindow::TabItem> MainWindow::tabItems() const {
    std::vector<TabItem> result;
    if (documents_.empty()) {
        return result;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const int available = std::max(200, static_cast<int>(client.right) - 64);
    const int width = std::clamp(available / static_cast<int>(documents_.size()), 118, 218);
    int x = 30;
    for (std::size_t index = 0; index < documents_.size(); ++index) {
        RECT bounds{x, 108, std::min(static_cast<int>(client.right) - 26, x + width), 151};
        RECT close{bounds.right - 31, bounds.top + 8, bounds.right - 7, bounds.bottom - 8};
        result.push_back({bounds, close, index});
        x += width + 5;
        if (x >= client.right - 30) {
            break;
        }
    }
    return result;
}

void MainWindow::paint() {
    PAINTSTRUCT paintStruct{};
    const HDC windowContext = BeginPaint(window_, &paintStruct);
    RECT client{};
    GetClientRect(window_, &client);
    const HDC context = CreateCompatibleDC(windowContext);
    const HBITMAP bitmap = CreateCompatibleBitmap(windowContext, std::max(1L, client.right),
                                                   std::max(1L, client.bottom));
    const auto oldBitmap = SelectObject(context, bitmap);

    const HBRUSH background = CreateSolidBrush(kSilver);
    FillRect(context, &client, background);
    DeleteObject(background);

    TRIVERTEX vertices[2]{{0, 0, 0x1000, 0x5A00, 0x7300, 0},
                          {client.right, kHeaderHeight, 0x5200, 0xC900, 0xDA00, 0}};
    GRADIENT_RECT gradient{0, 1};
    GradientFill(context, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_H);
    SetBkMode(context, TRANSPARENT);

    // Abstract water-ring and crystal motifs inspired by the reference palette.
    const HPEN ringPen = CreatePen(PS_SOLID, 2, RGB(157, 225, 232));
    const auto oldPen = SelectObject(context, ringPen);
    const auto oldBrush = SelectObject(context, GetStockObject(NULL_BRUSH));
    Ellipse(context, 18, 10, 92, 84);
    Ellipse(context, 29, 21, 81, 73);
    Arc(context, 4, 30, 126, 132, 8, 92, 120, 37);
    SelectObject(context, oldBrush);
    SelectObject(context, oldPen);
    DeleteObject(ringPen);
    drawDiamond(context, 55, 47, 13, RGB(203, 195, 236));
    drawDiamond(context, 55, 47, 7, RGB(111, 219, 222));
    drawDiamond(context, client.right - 26, 92, 5, RGB(198, 192, 234));
    drawDiamond(context, client.right - 51, 86, 3, RGB(208, 244, 245));

    SelectObject(context, titleFont_);
    SetTextColor(context, kWhite);
    RECT titleRect{105, 19, 500, 55};
    DrawTextW(context, L"幽歌琴谱", -1, &titleRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(context, subtitleFont_);
    SetTextColor(context, RGB(205, 244, 246));
    RECT subtitleRect{107, 55, 530, 77};
    DrawTextW(context, L"VODYANITSA  ·  SCORE STUDIO", -1, &subtitleRect,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SelectObject(context, interfaceFont_);
    for (const auto& item : toolbarItems()) {
        fillRoundedRect(context, item.bounds, 14,
                        item.action == ToolbarAction::NewFile ? RGB(239, 236, 251) : RGB(234, 249, 250));
        SetTextColor(context, item.action == ToolbarAction::NewFile ? RGB(83, 71, 139) : kDeepTeal);
        RECT text = item.bounds;
        DrawTextW(context, item.label, -1, &text, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    for (const auto& item : tabItems()) {
        const bool active = item.index == activeDocument_;
        fillRoundedRect(context, item.bounds, 12, active ? RGB(249, 253, 255) : RGB(95, 175, 194));
        if (active) {
            RECT accent{item.bounds.left + 12, item.bounds.bottom - 4, item.bounds.right - 12,
                        item.bounds.bottom - 1};
            fillRoundedRect(context, accent, 3, kLavender);
        }
        SelectObject(context, interfaceFont_);
        SetTextColor(context, active ? kDeepTeal : RGB(232, 250, 251));
        RECT label{item.bounds.left + 14, item.bounds.top, item.closeBounds.left - 4, item.bounds.bottom};
        std::wstring title = documents_[item.index].displayName;
        if (documents_[item.index].modified) {
            title += L"  •";
        }
        DrawTextW(context, title.c_str(), -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SetTextColor(context, active ? RGB(110, 104, 158) : RGB(222, 246, 248));
        RECT close = item.closeBounds;
        DrawTextW(context, L"×", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(context, smallFont_);
    SetTextColor(context, RGB(181, 229, 235));
    RECT dragHint = headerDragBounds();
    DrawTextW(context, L"─  拖动窗口  ─", -1, &dragHint,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    RECT footer{0, client.bottom - kFooterHeight, client.right, client.bottom};
    const HBRUSH footerBrush = CreateSolidBrush(RGB(23, 83, 101));
    FillRect(context, &footer, footerBrush);
    DeleteObject(footerBrush);
    SelectObject(context, smallFont_);
    SetTextColor(context, RGB(215, 241, 244));
    RECT status{26, footer.top + 4, std::max(260L, client.right - 340), footer.top + 28};
    DrawTextW(context, statusMessage_.c_str(), -1, &status,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const RECT opacity = opacityBounds();
    const std::wstring opacityText = L"界面透明度 " +
                                     std::to_wstring(windowOpacityPercent_) + L"%";
    RECT opacityLabel{opacity.left - 158, footer.top + 2, opacity.left - 10, footer.top + 29};
    SetTextColor(context, RGB(215, 241, 244));
    DrawTextW(context, opacityText.c_str(), -1, &opacityLabel,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    fillRoundedRect(context, opacity, 6, RGB(69, 118, 133));
    const double opacityRatio = static_cast<double>(windowOpacityPercent_ - 25) / 75.0;
    RECT opacityFill = opacity;
    opacityFill.right = opacity.left + static_cast<LONG>(std::round(
        static_cast<double>(opacity.right - opacity.left) * opacityRatio));
    if (opacityFill.right > opacityFill.left) {
        fillRoundedRect(context, opacityFill, 6, RGB(91, 211, 211));
    }
    const int opacityHandleX = opacityFill.right;
    const HBRUSH opacityHandleBrush = CreateSolidBrush(RGB(226, 219, 249));
    const auto oldOpacityBrush = SelectObject(context, opacityHandleBrush);
    const auto oldOpacityPen = SelectObject(context, GetStockObject(NULL_PEN));
    Ellipse(context, opacityHandleX - 6, opacity.top - 4,
            opacityHandleX + 6, opacity.bottom + 4);
    SelectObject(context, oldOpacityPen);
    SelectObject(context, oldOpacityBrush);
    DeleteObject(opacityHandleBrush);

    const RECT playPause = playPauseBounds();
    const RECT fromBeginning = playFromBeginningBounds();
    fillRoundedRect(context, playPause, 14, playbackRunning_ ? RGB(205, 238, 239)
                                                             : RGB(231, 246, 248));
    fillRoundedRect(context, fromBeginning, 14, RGB(231, 246, 248));

    const HBRUSH iconBrush = CreateSolidBrush(playbackRunning_ ? RGB(82, 75, 142) : kOcean);
    const auto previousBrush = SelectObject(context, iconBrush);
    const auto previousPen = SelectObject(context, GetStockObject(NULL_PEN));
    if (playbackRunning_) {
        RECT leftBar{playPause.left + 13, playPause.top + 11,
                     playPause.left + 18, playPause.bottom - 11};
        RECT rightBar{playPause.right - 18, playPause.top + 11,
                      playPause.right - 13, playPause.bottom - 11};
        FillRect(context, &leftBar, iconBrush);
        FillRect(context, &rightBar, iconBrush);
    } else {
        const POINT triangle[]{{playPause.left + 15, playPause.top + 10},
                               {playPause.right - 12, (playPause.top + playPause.bottom) / 2},
                               {playPause.left + 15, playPause.bottom - 10}};
        Polygon(context, triangle, 3);
    }
    RECT startBar{fromBeginning.left + 10, fromBeginning.top + 10,
                  fromBeginning.left + 14, fromBeginning.bottom - 10};
    FillRect(context, &startBar, iconBrush);
    const POINT startTriangle[]{{fromBeginning.left + 18, fromBeginning.top + 10},
                                {fromBeginning.right - 9,
                                 (fromBeginning.top + fromBeginning.bottom) / 2},
                                {fromBeginning.left + 18, fromBeginning.bottom - 10}};
    Polygon(context, startTriangle, 3);
    SelectObject(context, previousPen);
    SelectObject(context, previousBrush);
    DeleteObject(iconBrush);

    SelectObject(context, smallFont_);
    SetTextColor(context, RGB(184, 226, 231));
    RECT bpmLabel{131, footer.top + 40, 174, footer.bottom - 10};
    DrawTextW(context, L"BPM", -1, &bpmLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const RECT progress = progressBounds();
    RECT progressTrack = progress;
    fillRoundedRect(context, progressTrack, 8, RGB(69, 118, 133));
    const std::size_t visibleTick = draggingProgress_ ? draggedTick_ : transportTick_;
    const double ratio = transportTotalTicks_ <= 1
        ? 0.0
        : std::clamp(static_cast<double>(visibleTick) /
                         static_cast<double>(transportTotalTicks_ - 1),
                     0.0, 1.0);
    RECT progressFill = progress;
    progressFill.right = progress.left + static_cast<LONG>(
        std::round(static_cast<double>(progress.right - progress.left) * ratio));
    if (progressFill.right > progressFill.left) {
        fillRoundedRect(context, progressFill, 8, RGB(91, 211, 211));
    }
    const int handleX = progress.left + static_cast<int>(
        std::round(static_cast<double>(progress.right - progress.left) * ratio));
    const HBRUSH handleBrush = CreateSolidBrush(RGB(226, 219, 249));
    const auto oldHandleBrush = SelectObject(context, handleBrush);
    const auto oldHandlePen = SelectObject(context, GetStockObject(NULL_PEN));
    Ellipse(context, handleX - 7, progress.top - 4, handleX + 7, progress.bottom + 4);
    SelectObject(context, oldHandlePen);
    SelectObject(context, oldHandleBrush);
    DeleteObject(handleBrush);

    const double bpm = playbackBpm();
    const std::wstring timeText = formatTime(std::min(visibleTick, transportTotalTicks_), bpm) +
                                  L" / " + formatTime(transportTotalTicks_, bpm);
    RECT timeRect{progress.right + 8, footer.top + 34, client.right - 10, footer.bottom - 8};
    SetTextColor(context, RGB(213, 239, 243));
    DrawTextW(context, timeText.c_str(), -1, &timeRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    BitBlt(windowContext, 0, 0, client.right, client.bottom, context, 0, 0, SRCCOPY);
    SelectObject(context, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(context);
    EndPaint(window_, &paintStruct);
}

void MainWindow::newDocument() {
    DocumentTab tab;
    tab.displayName = L"未命名 " + std::to_wstring(untitledCounter_++);
    documents_.push_back(std::move(tab));
    setActiveDocument(documents_.size() - 1);
    setStatus(L"已新建空白乐谱；选择任意拍位即可输入");
}

void MainWindow::openDocument() {
    const auto path = chooseOpenPath();
    if (!path) {
        return;
    }

    const auto normalized = std::filesystem::absolute(*path).lexically_normal();
    for (std::size_t index = 0; index < documents_.size(); ++index) {
        if (documents_[index].path && documents_[index].path->lexically_normal() == normalized) {
            setActiveDocument(index);
            setStatus(L"该乐谱已经打开");
            return;
        }
    }

    std::ifstream input(normalized, std::ios::binary);
    if (!input) {
        MessageBoxW(window_, L"无法读取选择的乐谱文件。", L"打开失败", MB_OK | MB_ICONERROR);
        return;
    }
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    DocumentTab tab;
    tab.document = ui::ScoreDocument::fromText(text);
    tab.path = normalized;
    tab.displayName = fileNameFor(normalized);
    documents_.push_back(std::move(tab));
    setActiveDocument(documents_.size() - 1);
    setStatus(L"乐谱已载入内存；编辑不会改动源文件，直到执行保存");
}

bool MainWindow::saveDocument(std::size_t index) {
    if (index >= documents_.size()) {
        return false;
    }
    if (!documents_[index].path) {
        return saveDocumentAs(index);
    }
    return writeDocument(index, *documents_[index].path);
}

bool MainWindow::saveDocumentAs(std::size_t index) {
    if (index >= documents_.size()) {
        return false;
    }
    const auto path = chooseSavePath(documents_[index]);
    return path && writeDocument(index, *path);
}

bool MainWindow::writeDocument(std::size_t index, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        MessageBoxW(window_, L"无法写入目标文件，请检查目录权限。", L"保存失败",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    const std::string content = documents_[index].document.toText();
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output) {
        MessageBoxW(window_, L"写入乐谱时发生错误。", L"保存失败", MB_OK | MB_ICONERROR);
        return false;
    }

    documents_[index].path = std::filesystem::absolute(path).lexically_normal();
    documents_[index].displayName = fileNameFor(path);
    documents_[index].modified = false;
    updateWindowTitle();
    setStatus(L"已保存到 " + documents_[index].path->wstring());
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

bool MainWindow::confirmClose(std::size_t index) {
    if (index >= documents_.size() || !documents_[index].modified) {
        return true;
    }
    const std::wstring prompt = L"是否保存“" + documents_[index].displayName + L"”的修改？";
    const int answer = MessageBoxW(window_, prompt.c_str(), L"尚未保存",
                                   MB_YESNOCANCEL | MB_ICONQUESTION);
    if (answer == IDCANCEL) {
        return false;
    }
    return answer == IDNO || saveDocument(index);
}

void MainWindow::closeDocument(std::size_t index) {
    if (index == activeDocument_ && playbackRunning_) {
        stopPlayback();
    }
    if (index >= documents_.size() || !confirmClose(index)) {
        return;
    }
    const bool closedBeforeActive = index < activeDocument_;
    documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(index));
    if (documents_.empty()) {
        newDocument();
        return;
    }
    if (closedBeforeActive) {
        --activeDocument_;
    }
    activeDocument_ = std::min(activeDocument_, documents_.size() - 1);
    setActiveDocument(activeDocument_);
}

void MainWindow::setActiveDocument(std::size_t index) {
    if (index >= documents_.size()) {
        return;
    }
    if (playbackRunning_) {
        stopPlayback();
    }
    activeDocument_ = index;
    editor_.setDocument(&documents_[activeDocument_].document);
    transportTick_ = 0;
    transportTotalTicks_ = activeScoreTickCount();
    editor_.setPlayheadTick(0, false);
    updateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::markActiveDocumentChanged() {
    if (activeDocument_ >= documents_.size()) {
        return;
    }
    documents_[activeDocument_].modified = true;
    transportTotalTicks_ = std::max<std::size_t>(1, activeScoreTickCount());
    updateWindowTitle();
    setStatus(L"修改仅保存在内存中 · Ctrl+S 保存 · Ctrl+Shift+S 另存为");
}

void MainWindow::onEditorSelectionChanged(std::size_t tickIndex) {
    transportTick_ = tickIndex;
    transportTotalTicks_ = std::max({std::size_t{1}, activeScoreTickCount(), tickIndex + 1});
    if (playbackRunning_) {
        seekTo(tickIndex, true);
    } else {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

double MainWindow::playbackBpm() const {
    if (!bpmEdit_) {
        return 80.0;
    }
    wchar_t text[32]{};
    GetWindowTextW(bpmEdit_, text, static_cast<int>(std::size(text)));
    const double value = std::wcstod(text, nullptr);
    return value > 0.0 ? value : 0.0;
}

std::size_t MainWindow::activeScoreTickCount() const {
    if (activeDocument_ >= documents_.size()) {
        return 1;
    }
    const std::size_t scoreTicks = documents_[activeDocument_].document.measureCount() *
                                   ui::kBeatsPerMeasure;
    return scoreTicks > 0 ? scoreTicks : editor_.displayedTickCount();
}

std::size_t MainWindow::progressTickFromX(int x) const {
    const RECT progress = progressBounds();
    const double ratio = std::clamp(
        static_cast<double>(x - progress.left) /
            static_cast<double>(std::max(1L, progress.right - progress.left)),
        0.0, 1.0);
    if (transportTotalTicks_ <= 1) {
        return 0;
    }
    return std::min(transportTotalTicks_ - 1,
                    static_cast<std::size_t>(std::llround(
                        ratio * static_cast<double>(transportTotalTicks_ - 1))));
}

void MainWindow::setWindowOpacityFromX(int x) {
    const RECT bounds = opacityBounds();
    const double ratio = std::clamp(
        static_cast<double>(x - bounds.left) /
            static_cast<double>(std::max(1L, bounds.right - bounds.left)),
        0.0, 1.0);
    windowOpacityPercent_ = 25 + static_cast<int>(std::lround(ratio * 75.0));
    applyWindowOpacity();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::applyWindowOpacity() {
    if (!window_) {
        return;
    }
    const BYTE alpha = static_cast<BYTE>(std::lround(
        255.0 * static_cast<double>(windowOpacityPercent_) / 100.0));
    SetLayeredWindowAttributes(window_, 0, alpha, LWA_ALPHA);
}

void MainWindow::startPlayback(std::size_t tickIndex) {
    if (activeDocument_ >= documents_.size()) {
        return;
    }

    const double bpm = playbackBpm();
    if (bpm <= 0.0) {
        setStatus(L"请输入大于 0 的 BPM");
        SetFocus(bpmEdit_);
        return;
    }

    auto parseResult = core::ScoreParser::parseText(
        documents_[activeDocument_].document.toText());
    if (!parseResult.success || !parseResult.score.hasNotes()) {
        setStatus(L"当前乐谱没有可播放的音符");
        return;
    }

    const std::size_t requestedTick = tickIndex;
    transportTotalTicks_ = parseResult.score.tickCount();
    if (transportTotalTicks_ == 0) {
        return;
    }

    if (requestedTick >= transportTotalTicks_) {
        if (playbackRunning_) {
            stopPlayback();
        }
        transportTick_ = requestedTick;
        transportTotalTicks_ = std::max({transportTotalTicks_, activeScoreTickCount(),
                                         requestedTick + 1});
        editor_.setPlayheadTick(requestedTick, false);
        setStatus(L"当前位置之后没有可播放的音符");
        return;
    }
    tickIndex = requestedTick;

    bool hasRemainingNote = false;
    for (std::size_t index = tickIndex; index < parseResult.score.tickCount(); ++index) {
        if (parseResult.score.ticks()[index]) {
            hasRemainingNote = true;
            break;
        }
    }
    if (!hasRemainingNote) {
        if (playbackRunning_) {
            stopPlayback();
        }
        transportTick_ = tickIndex;
        editor_.setPlayheadTick(tickIndex, false);
        setStatus(L"当前位置之后没有可播放的音符");
        return;
    }

    stopPlayback();
    auto keySender = std::make_unique<playback::VirtualHidKeySender>();
    if (!keySender->isOpen()) {
        setStatus(L"虚拟 HID 驱动未安装或未启动 · 请先安装 driver/YuanqinVhid");
        return;
    }
    const HWND gameWindow = playback::GenshinWindowTarget::find();
    if (!gameWindow || !playback::GenshinWindowTarget::activate(gameWindow, window_)) {
        setStatus(L"未找到或无法激活原神窗口 · 请先启动原神并保持窗口化或无边框模式");
        return;
    }
    transportTick_ = tickIndex;
    editor_.setPlaybackActive(true);
    editor_.setPlayheadTick(tickIndex, true);
    EnableWindow(bpmEdit_, FALSE);
    playbackRunning_ = true;
    const std::uint64_t generation = ++playbackGeneration_;
    playback::PlaybackOptions options;
    options.bpm = bpm;
    options.startTick = tickIndex;
    core::Score score = std::move(parseResult.score);

    playbackThread_ = std::jthread(
        [this, generation, keySender = std::move(keySender), score = std::move(score), options](
            std::stop_token stopToken) mutable {
            playback::MusicPlayer player;
            const auto result = player.play(
                score, *keySender, options,
                [&stopToken] { return stopToken.stop_requested(); },
                [this, generation](std::size_t currentTick) {
                    PostMessageW(window_, kPlaybackProgressMessage,
                                 static_cast<WPARAM>(generation),
                                 static_cast<LPARAM>(currentTick));
                });
            PostMessageW(window_, kPlaybackCompleteMessage, static_cast<WPARAM>(generation),
                         static_cast<LPARAM>(result.status));
        });
    setStatus(L"正在通过虚拟 HID 键盘播放 · 乐谱编辑已锁定 · 点击其他拍位可立即跳播");
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::stopPlayback(bool unlockEditor) {
    ++playbackGeneration_;
    if (playbackThread_.joinable()) {
        playbackThread_.request_stop();
        playbackThread_.join();
    }
    playbackRunning_ = false;
    if (unlockEditor) {
        editor_.setPlaybackActive(false);
        if (bpmEdit_) {
            EnableWindow(bpmEdit_, TRUE);
        }
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void MainWindow::togglePlayback() {
    if (playbackRunning_) {
        stopPlayback();
        setStatus(L"已暂停 · 可编辑乐谱或拖动进度位置");
    } else {
        const std::size_t startTick = transportTick_ >= transportTotalTicks_ ? 0 : transportTick_;
        startPlayback(startTick);
    }
}

void MainWindow::playFromBeginning() {
    startPlayback(0);
}

void MainWindow::seekTo(std::size_t tickIndex, bool keepPlaying) {
    tickIndex = std::min(tickIndex, transportTotalTicks_ > 0 ? transportTotalTicks_ - 1 : 0);
    if (keepPlaying) {
        startPlayback(tickIndex);
    } else {
        if (playbackRunning_) {
            stopPlayback();
        }
        transportTick_ = tickIndex;
        editor_.setPlayheadTick(tickIndex, false);
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void MainWindow::handlePlaybackProgress(std::uint64_t generation, std::size_t tickIndex) {
    if (generation != playbackGeneration_ || !playbackRunning_) {
        return;
    }
    transportTick_ = std::min(tickIndex, transportTotalTicks_);
    if (!draggingProgress_) {
        editor_.setPlayheadTick(transportTick_, true);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::handlePlaybackComplete(std::uint64_t generation, int status) {
    if (generation != playbackGeneration_) {
        return;
    }
    if (playbackThread_.joinable()) {
        playbackThread_.join();
    }
    playbackRunning_ = false;
    editor_.setPlaybackActive(false);
    EnableWindow(bpmEdit_, TRUE);
    if (status == static_cast<int>(playback::PlaybackStatus::Completed)) {
        transportTick_ = transportTotalTicks_;
        if (transportTotalTicks_ > 0) {
            editor_.setPlayheadTick(transportTotalTicks_ - 1, false);
        }
        setStatus(L"播放完成");
    } else if (status == static_cast<int>(playback::PlaybackStatus::SendFailed)) {
        setStatus(L"播放中断：虚拟 HID 驱动通信失败 · 请检查设备管理器中的 Yuanqin Virtual HID Keyboard");
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::invoke(ToolbarAction action) {
    switch (action) {
        case ToolbarAction::NewFile:
            if (playbackRunning_) stopPlayback();
            newDocument();
            break;
        case ToolbarAction::OpenFile:
            if (playbackRunning_) stopPlayback();
            openDocument();
            break;
        case ToolbarAction::Save:
            if (!documents_.empty()) saveDocument(activeDocument_);
            break;
        case ToolbarAction::SaveAs:
            if (!documents_.empty()) saveDocumentAs(activeDocument_);
            break;
    }
}

void MainWindow::updateWindowTitle() {
    if (documents_.empty()) {
        SetWindowTextW(window_, L"幽歌琴谱");
        return;
    }
    const auto& document = documents_[activeDocument_];
    const std::wstring title = document.displayName + (document.modified ? L" *" : L"") +
                               L" — 幽歌琴谱";
    SetWindowTextW(window_, title.c_str());
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::setStatus(std::wstring message) {
    statusMessage_ = std::move(message);
    InvalidateRect(window_, nullptr, FALSE);
}

std::optional<std::filesystem::path> MainWindow::chooseOpenPath() const {
    std::array<wchar_t, 32768> fileName{};
    const std::wstring initialDirectory = scoreDirectory().wstring();
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"琴谱文本 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetOpenFileNameW(&dialog) ? std::optional<std::filesystem::path>(fileName.data())
                                     : std::nullopt;
}

std::optional<std::filesystem::path> MainWindow::chooseSavePath(
    const DocumentTab& document) const {
    std::array<wchar_t, 32768> fileName{};
    const std::wstring initialDirectory = scoreDirectory().wstring();
    const std::wstring suggested = document.path ? document.path->wstring()
                                                  : document.displayName + L".txt";
    std::copy_n(suggested.c_str(), std::min(suggested.size(), fileName.size() - 1), fileName.data());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"琴谱文本 (*.txt)\0*.txt\0所有文件 (*.*)\0*.*\0\0";
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrDefExt = L"txt";
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetSaveFileNameW(&dialog) ? std::optional<std::filesystem::path>(fileName.data())
                                     : std::nullopt;
}

}  // namespace yuanqin::app
