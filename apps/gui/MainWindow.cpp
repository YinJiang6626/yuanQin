#include "MainWindow.h"

#include "yuanqin/core/ScoreParser.h"
#include "yuanqin/playback/GenshinWindowTarget.h"
#include "yuanqin/playback/MusicPlayer.h"
#include "yuanqin/playback/VirtualHidKeySender.h"
#include "yuanqin/playback/WindowsApiKeySender.h"

#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

namespace yuanqin::app {
namespace {

constexpr wchar_t kMainWindowClassName[] = L"YuanQinMainWindow";
constexpr int kHeaderHeight = 160;
constexpr int kFooterHeight = 62;
constexpr int kEditorControlId = 1001;
constexpr int kBpmControlId = 1002;
constexpr int kOutputBackendControlId = 1003;
constexpr int kBpmCorrectionControlId = 1004;
constexpr int kArpeggioIntervalControlId = 1005;
constexpr int kDefaultBpmControlId = 1006;
constexpr int kDefaultBpmCorrectionControlId = 1007;
constexpr int kDefaultArpeggioIntervalControlId = 1008;
constexpr int kTempoPanelCollapsedHeight = 28;
constexpr int kTempoPanelMinimumHeight = 92;
constexpr int kSidebarCollapsedWidth = 62;
constexpr int kSidebarExpandedWidth = 132;
constexpr int kSettingsHeaderHeight = 108;
constexpr int kSettingsContentHeight = 800;
constexpr std::array<int, 5> kBpmCorrections{1, 2, 4, 8, 16};
constexpr UINT kPlaybackProgressMessage = WM_APP + 11;
constexpr UINT kPlaybackCompleteMessage = WM_APP + 12;
constexpr UINT_PTR kTabAnimationTimer = 1;

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

void drawGearIcon(HDC context, const RECT& bounds, COLORREF color) {
    const int centerX = (bounds.left + bounds.right) / 2;
    const int centerY = (bounds.top + bounds.bottom) / 2;
    const int innerRadius = 9;
    const int outerRadius = 15;
    const HPEN pen = CreatePen(PS_SOLID, 2, color);
    const auto oldPen = SelectObject(context, pen);
    const auto oldBrush = SelectObject(context, GetStockObject(NULL_BRUSH));
    Ellipse(context, centerX - innerRadius, centerY - innerRadius,
            centerX + innerRadius, centerY + innerRadius);
    Ellipse(context, centerX - 3, centerY - 3, centerX + 4, centerY + 4);
    for (int index = 0; index < 8; ++index) {
        const double angle = static_cast<double>(index) * 3.14159265358979323846 / 4.0;
        MoveToEx(context,
                 centerX + static_cast<int>(std::lround(std::cos(angle) * innerRadius)),
                 centerY + static_cast<int>(std::lround(std::sin(angle) * innerRadius)), nullptr);
        LineTo(context,
               centerX + static_cast<int>(std::lround(std::cos(angle) * outerRadius)),
               centerY + static_cast<int>(std::lround(std::sin(angle) * outerRadius)));
    }
    SelectObject(context, oldBrush);
    SelectObject(context, oldPen);
    DeleteObject(pen);
}

void drawWorkspaceIcon(HDC context, const RECT& bounds, COLORREF color) {
    const int centerX = (bounds.left + bounds.right) / 2;
    const int centerY = (bounds.top + bounds.bottom) / 2;
    const RECT sheet{centerX - 12, centerY - 15, centerX + 13, centerY + 16};
    const HPEN pen = CreatePen(PS_SOLID, 2, color);
    const auto oldPen = SelectObject(context, pen);
    const auto oldBrush = SelectObject(context, GetStockObject(NULL_BRUSH));
    RoundRect(context, sheet.left, sheet.top, sheet.right, sheet.bottom, 4, 4);
    for (int offset = -7; offset <= 7; offset += 7) {
        MoveToEx(context, centerX - 7, centerY + offset, nullptr);
        LineTo(context, centerX + 8, centerY + offset);
    }
    SelectObject(context, oldBrush);
    SelectObject(context, oldPen);
    DeleteObject(pen);
}

void drawSidebarChevron(HDC context, const RECT& bounds, bool expanded, COLORREF color) {
    const int centerX = (bounds.left + bounds.right) / 2;
    const int centerY = (bounds.top + bounds.bottom) / 2;
    const int direction = expanded ? -1 : 1;
    const HPEN pen = CreatePen(PS_SOLID, 2, color);
    const auto oldPen = SelectObject(context, pen);
    MoveToEx(context, centerX - direction * 4, centerY - 7, nullptr);
    LineTo(context, centerX + direction * 3, centerY);
    LineTo(context, centerX - direction * 4, centerY + 7);
    SelectObject(context, oldPen);
    DeleteObject(pen);
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
    loadSystemSettings();
    outputBackend_ = systemSettings_.outputBackend;

    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(101));
    if (!windowClass.hIcon) {
        windowClass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    }
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
    if (message.message != WM_KEYDOWN) {
        return false;
    }

    // Tab belongs to the score editor's input mode rather than focus traversal.
    // Keep it disabled while playback locks the editor.
    if (message.wParam == VK_TAB && GetFocus() == editor_.handle() &&
        !playbackRunning_ && !(GetKeyState(VK_CONTROL) & 0x8000) &&
        !(GetKeyState(VK_MENU) & 0x8000)) {
        toggleInsertMode();
        return true;
    }

    if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
        return false;
    }

    if (GetFocus() == editor_.handle()) {
        if (message.wParam == 'Z') {
            undoActiveDocument();
            return true;
        }
        if (message.wParam == 'Y') {
            redoActiveDocument();
            return true;
        }
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
            bpmCorrectionCombo_ = CreateWindowExW(
                0, L"COMBOBOX", L"",
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                0, 0, 0, 0, window_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kBpmCorrectionControlId)),
                instance_, nullptr);
            if (!bpmCorrectionCombo_) {
                return -1;
            }
            SendMessageW(bpmCorrectionCombo_, WM_SETFONT,
                         reinterpret_cast<WPARAM>(interfaceFont_), TRUE);
            for (const wchar_t* multiplier : {L"×1", L"×2", L"×4", L"×8", L"×16"}) {
                SendMessageW(bpmCorrectionCombo_, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(multiplier));
            }
            SendMessageW(bpmCorrectionCombo_, CB_SETCURSEL, 0, 0);
            arpeggioIntervalEdit_ = CreateWindowExW(
                0, L"EDIT", L"107", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                0, 0, 0, 0, window_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kArpeggioIntervalControlId)),
                instance_, nullptr);
            if (!arpeggioIntervalEdit_) {
                return -1;
            }
            SendMessageW(arpeggioIntervalEdit_, WM_SETFONT,
                         reinterpret_cast<WPARAM>(interfaceFont_), TRUE);
            outputBackendCombo_ = CreateWindowExW(
                0, L"COMBOBOX", L"",
                WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                0, 0, 0, 0, window_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kOutputBackendControlId)),
                instance_, nullptr);
            if (!outputBackendCombo_) {
                return -1;
            }
            SendMessageW(outputBackendCombo_, WM_SETFONT,
                         reinterpret_cast<WPARAM>(interfaceFont_), TRUE);
            SendMessageW(outputBackendCombo_, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(L"Windows API"));
            SendMessageW(outputBackendCombo_, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(L"虚拟 HID 驱动"));
            defaultBpmEdit_ = CreateWindowExW(
                0, L"EDIT", L"80", WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                0, 0, 0, 0, window_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultBpmControlId)),
                instance_, nullptr);
            defaultBpmCorrectionCombo_ = CreateWindowExW(
                0, L"COMBOBOX", L"", WS_CHILD | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                0, 0, 0, 0, window_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultBpmCorrectionControlId)),
                instance_, nullptr);
            defaultArpeggioIntervalEdit_ = CreateWindowExW(
                0, L"EDIT", L"107", WS_CHILD | WS_TABSTOP | ES_NUMBER | ES_CENTER,
                0, 0, 0, 0, window_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDefaultArpeggioIntervalControlId)),
                instance_, nullptr);
            if (!defaultBpmEdit_ || !defaultBpmCorrectionCombo_ ||
                !defaultArpeggioIntervalEdit_) {
                return -1;
            }
            for (HWND control : {defaultBpmEdit_, defaultBpmCorrectionCombo_,
                                 defaultArpeggioIntervalEdit_}) {
                SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(interfaceFont_), TRUE);
            }
            for (const wchar_t* multiplier : {L"×1", L"×2", L"×4", L"×8", L"×16"}) {
                SendMessageW(defaultBpmCorrectionCombo_, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(multiplier));
            }
            syncSystemSettingsControls();
            bpmEditBrush_ = CreateSolidBrush(RGB(232, 247, 249));
            editor_.setBeforeChangeCallback([this] { recordActiveDocumentHistory(); });
            editor_.setChangedCallback([this] { markActiveDocumentChanged(); });
            editor_.setSelectionChangedCallback(
                [this](std::size_t tickIndex) { onEditorSelectionChanged(tickIndex); });
            newDocument();
            updatePageVisibility();
            layoutChildren();
            return 0;

        case WM_CTLCOLOREDIT:
            if (reinterpret_cast<HWND>(lParam) == bpmEdit_ ||
                reinterpret_cast<HWND>(lParam) == arpeggioIntervalEdit_ ||
                reinterpret_cast<HWND>(lParam) == defaultBpmEdit_ ||
                reinterpret_cast<HWND>(lParam) == defaultArpeggioIntervalEdit_) {
                const HDC context = reinterpret_cast<HDC>(wParam);
                SetTextColor(context, kDeepTeal);
                SetBkColor(context, RGB(232, 247, 249));
                return reinterpret_cast<LRESULT>(bpmEditBrush_);
            }
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == kBpmControlId && HIWORD(wParam) == EN_CHANGE) {
                if (!updatingBpmEdit_ && !updatingTempoControls_ &&
                    activeDocument_ < documents_.size()) {
                    wchar_t value[32]{};
                    GetWindowTextW(bpmEdit_, value, static_cast<int>(std::size(value)));
                    documents_[activeDocument_].bpmText = value;
                }
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            if (LOWORD(wParam) == kArpeggioIntervalControlId && HIWORD(wParam) == EN_CHANGE) {
                updateArpeggioIntervalFromControl();
                return 0;
            }
            if (LOWORD(wParam) == kDefaultBpmControlId && HIWORD(wParam) == EN_CHANGE) {
                updateDefaultBpmFromControl();
                return 0;
            }
            if (LOWORD(wParam) == kDefaultArpeggioIntervalControlId &&
                HIWORD(wParam) == EN_CHANGE) {
                updateDefaultArpeggioIntervalFromControl();
                return 0;
            }
            if (LOWORD(wParam) == kBpmCorrectionControlId &&
                HIWORD(wParam) == CBN_SELCHANGE) {
                updateBpmCorrectionFromControl();
                return 0;
            }
            if (LOWORD(wParam) == kDefaultBpmCorrectionControlId &&
                HIWORD(wParam) == CBN_SELCHANGE) {
                updateDefaultBpmCorrectionFromControl();
                return 0;
            }
            if (LOWORD(wParam) == kOutputBackendControlId &&
                HIWORD(wParam) == CBN_SELCHANGE) {
                updateOutputBackendFromControl();
                return 0;
            }
            break;

        case WM_MOUSEWHEEL:
            if (activePage_ == Page::SystemSettings) {
                const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                scrollSettingsBy(-steps * 54);
                return 0;
            }
            if (activePage_ == Page::Workspace || activePage_ == Page::Composition) {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(window_, &point);
                point.x -= sidebarWidth();
                const RECT viewport = tempoPanelViewportBounds();
                if (tempoPanelExpanded_ && PtInRect(&viewport, point)) {
                    const int steps = GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA;
                    scrollTempoPanelBy(-steps * 34);
                    return 0;
                }
            }
            break;

        case WM_GETMINMAXINFO: {
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize = {760, 620};
            return 0;
        }

        case WM_SIZE:
            layoutChildren();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_TIMER:
            if (wParam == kTabAnimationTimer) {
                tickTabAnimation();
                return 0;
            }
            break;

        case WM_MOUSEACTIVATE: {
            const HWND gameWindow = playback::GenshinWindowTarget::find();
            const bool preserveGameFocus = playbackRunning_ ||
                (gameWindow && GetForegroundWindow() == gameWindow);
            if (preserveGameFocus) {
                if (LOWORD(lParam) == HTCAPTION) {
                    return MA_NOACTIVATE;
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
            }
            break;
        }

        case WM_SETCURSOR: {
            if (LOWORD(lParam) != HTCLIENT || activePage_ == Page::SystemSettings) {
                break;
            }
            POINT cursor{};
            if (GetCursorPos(&cursor)) {
                ScreenToClient(window_, &cursor);
                cursor.x -= sidebarWidth();
                const RECT resizeBounds = tempoPanelResizeBounds();
                if (tempoPanelExpanded_ && PtInRect(&resizeBounds, cursor)) {
                    SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
                    return TRUE;
                }
            }
            break;
        }

        case WM_PAINT:
            paint();
            return 0;

        case WM_LBUTTONDOWN: {
            const POINT windowPoint{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const RECT settingsButton = sidebarSettingsBounds();
            const RECT workspaceButton = sidebarWorkspaceBounds();
            const RECT compositionButton = sidebarCompositionBounds();
            const RECT sidebarToggle = sidebarToggleBounds();
            if (PtInRect(&settingsButton, windowPoint)) {
                setActivePage(Page::SystemSettings);
                return 0;
            }
            if (PtInRect(&workspaceButton, windowPoint)) {
                setActivePage(Page::Workspace);
                return 0;
            }
            if (PtInRect(&compositionButton, windowPoint)) {
                setActivePage(Page::Composition);
                return 0;
            }
            if (PtInRect(&sidebarToggle, windowPoint)) {
                toggleSidebar();
                return 0;
            }

            if (activePage_ == Page::SystemSettings) {
                const RECT scrollBar = settingsScrollbarBounds();
                if (PtInRect(&scrollBar, windowPoint) && maximumSettingsScroll() > 0) {
                    draggingSettingsScroll_ = true;
                    setSettingsScrollFromY(windowPoint.y);
                    SetCapture(window_);
                }
                return 0;
            }

            POINT point = windowPoint;
            point.x -= sidebarWidth();
            const RECT tempoToggle = tempoPanelToggleBounds();
            if (PtInRect(&tempoToggle, point)) {
                tempoPanelExpanded_ = !tempoPanelExpanded_;
                tempoPanelScrollOffset_ = 0;
                layoutChildren();
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            const RECT tempoResize = tempoPanelResizeBounds();
            if (tempoPanelExpanded_ && PtInRect(&tempoResize, point)) {
                draggingTempoPanelResize_ = true;
                tempoPanelResizeStartY_ = point.y;
                tempoPanelResizeStartHeight_ = tempoPanelHeight_;
                SetCursor(LoadCursorW(nullptr, IDC_SIZENS));
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
            RECT opacity = opacityBounds();
            InflateRect(&opacity, 0, 12);
            if (PtInRect(&opacity, point)) {
                draggingOpacity_ = true;
                setWindowOpacityFromX(point.x);
                SetCapture(window_);
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
            const RECT editModeToggle = editModeToggleBounds();
            if (PtInRect(&editModeToggle, point)) {
                toggleInsertMode();
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
                    draggingTab_ = true;
                    draggedTabIndex_ = item.index;
                    tabDragTargetIndex_ = item.index;
                    tabDragGrabOffsetX_ = point.x - item.bounds.left;
                    tabDragTargetOffset_ = 0.0F;
                    tabVisualOffsets_.assign(documents_.size(), 0.0F);
                    startTabAnimation();
                    SetCapture(window_);
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
            if (draggingSettingsScroll_) {
                setSettingsScrollFromY(GET_Y_LPARAM(lParam));
                return 0;
            }
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
            if (draggingTempoPanelResize_) {
                RECT client{};
                GetClientRect(window_, &client);
                const int delta = tempoPanelResizeStartY_ - GET_Y_LPARAM(lParam);
                const int maximum = std::max(kTempoPanelMinimumHeight,
                    static_cast<int>(client.bottom) - kHeaderHeight - kFooterHeight - 48);
                tempoPanelHeight_ = std::clamp(tempoPanelResizeStartHeight_ + delta,
                                               kTempoPanelMinimumHeight, maximum);
                layoutChildren();
                // Moving the child editor exposes its old bounds.  Force the
                // parent and every child to repaint in this mouse turn so those
                // exposed areas cannot leave a visual trail behind the panel.
                RedrawWindow(window_, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                return 0;
            }
            if (draggingOpacity_) {
                setWindowOpacityFromX(GET_X_LPARAM(lParam) - sidebarWidth());
                return 0;
            }
            if (draggingProgress_) {
                draggedTick_ = progressTickFromX(GET_X_LPARAM(lParam) - sidebarWidth());
                editor_.setPlayheadTick(draggedTick_, false);
                InvalidateRect(window_, nullptr, FALSE);
                return 0;
            }
            if (draggingTab_) {
                POINT point{GET_X_LPARAM(lParam) - sidebarWidth(), GET_Y_LPARAM(lParam)};
                updateTabDragTarget(point);
                return 0;
            }
            break;

        case WM_LBUTTONUP:
            if (draggingSettingsScroll_) {
                draggingSettingsScroll_ = false;
                setSettingsScrollFromY(GET_Y_LPARAM(lParam));
                ReleaseCapture();
                return 0;
            }
            if (draggingWindow_) {
                draggingWindow_ = false;
                ReleaseCapture();
                return 0;
            }
            if (draggingTempoPanelResize_) {
                draggingTempoPanelResize_ = false;
                ReleaseCapture();
                return 0;
            }
            if (draggingOpacity_) {
                setWindowOpacityFromX(GET_X_LPARAM(lParam) - sidebarWidth());
                draggingOpacity_ = false;
                ReleaseCapture();
                return 0;
            }
            if (draggingProgress_) {
                draggedTick_ = progressTickFromX(GET_X_LPARAM(lParam) - sidebarWidth());
                draggingProgress_ = false;
                ReleaseCapture();
                seekTo(draggedTick_, dragWasPlaying_);
                return 0;
            }
            if (draggingTab_) {
                draggingTab_ = false;
                ReleaseCapture();
                reorderDocument(draggedTabIndex_, tabDragTargetIndex_);
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            draggingWindow_ = false;
            draggingOpacity_ = false;
            draggingSettingsScroll_ = false;
            draggingTempoPanelResize_ = false;
            draggingTab_ = false;
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
            saveSystemSettings();
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
    if (!window_ || !editor_.handle() || !bpmEdit_ || !bpmCorrectionCombo_ ||
        !arpeggioIntervalEdit_ || !outputBackendCombo_ || !defaultBpmEdit_ ||
        !defaultBpmCorrectionCombo_ || !defaultArpeggioIntervalEdit_) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    settingsScrollOffset_ = std::clamp(settingsScrollOffset_, 0, maximumSettingsScroll());
    tempoPanelScrollOffset_ = std::clamp(tempoPanelScrollOffset_, 0, maximumTempoPanelScroll());
    const int sidebar = sidebarWidth();
    const int contentWidth = std::max(0, static_cast<int>(client.right) - sidebar);
    const int tempoPanelHeight = tempoPanelCurrentHeight();
    MoveWindow(editor_.handle(), sidebar, kHeaderHeight, contentWidth,
               std::max(0, static_cast<int>(client.bottom) - kHeaderHeight -
                               kFooterHeight - tempoPanelHeight), TRUE);

    const TempoPanelLayout tempoLayout = tempoPanelLayout();
    MoveWindow(bpmEdit_, sidebar + tempoLayout.bpm.controlBounds.left,
               tempoLayout.bpm.controlBounds.top,
               tempoLayout.bpm.controlBounds.right - tempoLayout.bpm.controlBounds.left, 27, TRUE);
    MoveWindow(bpmCorrectionCombo_, sidebar + tempoLayout.correction.controlBounds.left,
               tempoLayout.correction.controlBounds.top,
               tempoLayout.correction.controlBounds.right -
                   tempoLayout.correction.controlBounds.left,
               220, TRUE);
    MoveWindow(arpeggioIntervalEdit_, sidebar + tempoLayout.arpeggio.controlBounds.left,
               tempoLayout.arpeggio.controlBounds.top,
               tempoLayout.arpeggio.controlBounds.right -
                   tempoLayout.arpeggio.controlBounds.left,
               27, TRUE);

    const int settingsCardTop = kSettingsHeaderHeight + 28 - settingsScrollOffset_;
    const int defaultValuesTop = settingsCardTop + 230;
    MoveWindow(outputBackendCombo_, sidebar + 64, settingsCardTop + 96,
               std::min(330, std::max(190, contentWidth - 128)), 220, TRUE);
    const int defaultControlX = std::min(sidebar + 230, static_cast<int>(client.right) - 112);
    MoveWindow(defaultBpmEdit_, defaultControlX, defaultValuesTop + 84, 92, 27, TRUE);
    MoveWindow(defaultBpmCorrectionCombo_, defaultControlX, defaultValuesTop + 124, 92, 220, TRUE);
    MoveWindow(defaultArpeggioIntervalEdit_, defaultControlX, defaultValuesTop + 164, 92, 27, TRUE);
    updatePageVisibility();
}

RECT MainWindow::playPauseBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    return {18, client.bottom - kFooterHeight + 10, 58, client.bottom - 10};
}

RECT MainWindow::playFromBeginningBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    return {66, client.bottom - kFooterHeight + 10, 106, client.bottom - 10};
}

RECT MainWindow::progressBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    return {130, client.bottom - 37, std::max(150L, client.right - 92), client.bottom - 29};
}

RECT MainWindow::opacityBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    // Keep opacity beside the fixed input-mode control instead of consuming
    // space in the transport footer.
    return {std::max(180L, client.right - 360), 83,
            std::max(181L, client.right - 238), 89};
}

RECT MainWindow::headerDragBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    return {18, 76, std::max(19L, client.right - 18), 104};
}

RECT MainWindow::editModeToggleBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    // The input-mode control keeps a stable editor-header location. It is not
    // tied to the Save As button because the composition page has extra tools.
    return {std::max(0L, client.right - 124), 73, std::max(0L, client.right - 32), 99};
}

int MainWindow::tempoPanelCurrentHeight() const noexcept {
    return tempoPanelExpanded_ ? tempoPanelHeight_ : kTempoPanelCollapsedHeight;
}

RECT MainWindow::tempoPanelBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    const LONG bottom = client.bottom - kFooterHeight;
    return {0, bottom - tempoPanelCurrentHeight(), client.right, bottom};
}

RECT MainWindow::tempoPanelToggleBounds() const {
    const RECT panel = tempoPanelBounds();
    const LONG center = (panel.left + panel.right) / 2;
    return {center - 20, panel.top + 2, center + 20,
            std::min(panel.bottom - 2, panel.top + 26)};
}

RECT MainWindow::tempoPanelResizeBounds() const {
    const RECT panel = tempoPanelBounds();
    return {panel.left, panel.top - 4, panel.right, panel.top + 5};
}

RECT MainWindow::tempoPanelViewportBounds() const {
    const RECT panel = tempoPanelBounds();
    return {panel.left + 22, panel.top + 30, panel.right - 22, panel.bottom - 9};
}

MainWindow::TempoPanelLayout MainWindow::tempoPanelLayout() const {
    const RECT viewport = tempoPanelViewportBounds();
    TempoPanelLayout layout{};
    const int contentLeft = viewport.left;
    const int contentRight = std::max(contentLeft + 1,
                                      static_cast<int>(viewport.right - 18));
    constexpr int kControlHeight = 27;
    constexpr int kLabelGap = 8;
    constexpr int kGroupGap = 24;
    constexpr int kLineGap = 12;
    int x = contentLeft;
    int y = 0;
    const auto placeSetting = [&](int labelWidth, int controlWidth) {
        const int groupWidth = labelWidth + kLabelGap + controlWidth;
        if (x > contentLeft && x + groupWidth > contentRight) {
            x = contentLeft;
            y += kControlHeight + kLineGap;
        }
        TempoSettingLayout setting;
        setting.labelBounds = {x, viewport.top + y - tempoPanelScrollOffset_,
                               x + labelWidth, viewport.top + y - tempoPanelScrollOffset_ +
                                                   kControlHeight};
        setting.controlBounds = {setting.labelBounds.right + kLabelGap, setting.labelBounds.top,
                                 setting.labelBounds.right + kLabelGap + controlWidth,
                                 setting.labelBounds.bottom};
        x = setting.controlBounds.right + kGroupGap;
        return setting;
    };

    layout.bpm = placeSetting(52, 82);
    layout.correction = placeSetting(52, 92);
    layout.arpeggio = placeSetting(156, 82);
    layout.contentHeight = y + kControlHeight;
    return layout;
}

int MainWindow::maximumTempoPanelScroll() const {
    if (!tempoPanelExpanded_) {
        return 0;
    }
    const RECT viewport = tempoPanelViewportBounds();
    const int viewportHeight = std::max(0, static_cast<int>(viewport.bottom - viewport.top));
    return std::max(0, tempoPanelLayout().contentHeight - viewportHeight);
}

int MainWindow::sidebarWidth() const noexcept {
    return sidebarExpanded_ ? kSidebarExpandedWidth : kSidebarCollapsedWidth;
}

RECT MainWindow::sidebarSettingsBounds() const {
    return {9, 16, sidebarWidth() - 9, 66};
}

RECT MainWindow::sidebarWorkspaceBounds() const {
    return {9, 76, sidebarWidth() - 9, 126};
}

RECT MainWindow::sidebarCompositionBounds() const {
    return {9, 136, sidebarWidth() - 9, 186};
}

RECT MainWindow::sidebarToggleBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    return {9, std::max(196L, client.bottom - 58), sidebarWidth() - 9,
            std::max(238L, client.bottom - 14)};
}

RECT MainWindow::settingsViewportBounds() const {
    RECT client{};
    GetClientRect(window_, &client);
    return {sidebarWidth(), kSettingsHeaderHeight, client.right, client.bottom};
}

int MainWindow::maximumSettingsScroll() const {
    const RECT viewport = settingsViewportBounds();
    return std::max(0, kSettingsContentHeight -
                           std::max(0, static_cast<int>(viewport.bottom - viewport.top)));
}

RECT MainWindow::settingsScrollbarBounds() const {
    const RECT viewport = settingsViewportBounds();
    return {std::max(viewport.left, viewport.right - 16), viewport.top + 14,
            std::max(viewport.left, viewport.right - 8), viewport.bottom - 14};
}

RECT MainWindow::settingsScrollThumbBounds() const {
    const RECT track = settingsScrollbarBounds();
    const int trackHeight = std::max(1L, track.bottom - track.top);
    const RECT viewport = settingsViewportBounds();
    const int viewportHeight = std::max(1L, viewport.bottom - viewport.top);
    const int thumbHeight = std::clamp(
        static_cast<int>(std::lround(static_cast<double>(trackHeight) * viewportHeight /
                                     std::max(viewportHeight, kSettingsContentHeight))),
        42, trackHeight);
    const int maximum = maximumSettingsScroll();
    const int travel = std::max(0, trackHeight - thumbHeight);
    const int top = track.top + (maximum == 0 ? 0 :
        static_cast<int>(std::lround(static_cast<double>(travel) * settingsScrollOffset_ /
                                     maximum)));
    return {track.left, top, track.right, top + thumbHeight};
}

void MainWindow::scrollSettingsBy(int delta) {
    settingsScrollOffset_ = std::clamp(settingsScrollOffset_ + delta,
                                       0, maximumSettingsScroll());
    layoutChildren();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::setSettingsScrollFromY(int y) {
    const RECT track = settingsScrollbarBounds();
    const RECT thumb = settingsScrollThumbBounds();
    const int travel = std::max(1L, (track.bottom - track.top) - (thumb.bottom - thumb.top));
    const int rawPosition = y - static_cast<int>(track.top) -
                            static_cast<int>((thumb.bottom - thumb.top) / 2);
    const int position = std::clamp(rawPosition, 0, travel);
    settingsScrollOffset_ = maximumSettingsScroll() == 0 ? 0 :
        static_cast<int>(std::lround(static_cast<double>(position) *
                                     maximumSettingsScroll() / travel));
    layoutChildren();
    InvalidateRect(window_, nullptr, FALSE);
}

bool MainWindow::shouldHandleWithoutActivation(POINT clientPoint) const {
    const RECT settingsButton = sidebarSettingsBounds();
    const RECT workspaceButton = sidebarWorkspaceBounds();
    const RECT compositionButton = sidebarCompositionBounds();
    const RECT toggleButton = sidebarToggleBounds();
    if (PtInRect(&settingsButton, clientPoint) || PtInRect(&workspaceButton, clientPoint) ||
        PtInRect(&compositionButton, clientPoint) ||
        PtInRect(&toggleButton, clientPoint)) {
        return true;
    }
    if (activePage_ == Page::SystemSettings) {
        return true;
    }

    POINT workspacePoint = clientPoint;
    workspacePoint.x -= sidebarWidth();
    RECT playPause = playPauseBounds();
    RECT fromBeginning = playFromBeginningBounds();
    RECT progress = progressBounds();
    RECT opacity = opacityBounds();
    RECT tempoPanel = tempoPanelBounds();
    RECT tempoResize = tempoPanelResizeBounds();
    const RECT headerDrag = headerDragBounds();
    RECT editModeToggle = editModeToggleBounds();
    InflateRect(&progress, 0, 12);
    InflateRect(&opacity, 0, 12);
    if (PtInRect(&headerDrag, workspacePoint) || PtInRect(&playPause, workspacePoint) ||
        PtInRect(&fromBeginning, workspacePoint) ||
        PtInRect(&progress, workspacePoint) || PtInRect(&opacity, workspacePoint) ||
        PtInRect(&tempoPanel, workspacePoint) ||
        PtInRect(&tempoResize, workspacePoint) ||
        PtInRect(&editModeToggle, workspacePoint)) {
        return true;
    }
    for (const auto& item : tabItems()) {
        if (PtInRect(&item.bounds, workspacePoint)) {
            return true;
        }
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
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    struct Definition { ToolbarAction action; const wchar_t* label; int width; };
    std::vector<Definition> definitions{{ToolbarAction::NewFile, L"＋ 新建", 84},
                                        {ToolbarAction::OpenFile, L"打开", 78},
                                        {ToolbarAction::Save, L"保存", 78},
                                        {ToolbarAction::SaveAs, L"另存为", 92}};
    if (activePage_ == Page::Composition) {
        definitions.push_back({ToolbarAction::ExportText, L"导出 .txt", 94});
    }
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
    client.right = std::max<LONG>(0, client.right - sidebarWidth());
    const int available = std::max(200, static_cast<int>(client.right) - 64);
    const DocumentKind kind = activePage_ == Page::Composition ? DocumentKind::Composition
                                                                 : DocumentKind::Standard;
    const auto count = static_cast<int>(std::count_if(documents_.begin(), documents_.end(),
        [kind](const DocumentTab& tab) { return tab.kind == kind; }));
    if (count == 0) {
        return result;
    }
    const int width = std::clamp(available / count, 118, 218);
    int x = 30;
    for (std::size_t index = 0; index < documents_.size(); ++index) {
        if (documents_[index].kind != kind) {
            continue;
        }
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

int MainWindow::tabVisualOffset(std::size_t documentIndex) const {
    if (documentIndex >= tabVisualOffsets_.size()) {
        return 0;
    }
    return static_cast<int>(std::lround(tabVisualOffsets_[documentIndex]));
}

void MainWindow::updateTabDragTarget(POINT point) {
    const auto items = tabItems();
    for (const auto& item : items) {
        if (PtInRect(&item.bounds, point)) {
            tabDragTargetIndex_ = item.index;
            break;
        }
    }
    const auto dragged = std::find_if(items.begin(), items.end(), [this](const TabItem& item) {
        return item.index == draggedTabIndex_;
    });
    if (dragged != items.end()) {
        tabDragTargetOffset_ = static_cast<float>(point.x - tabDragGrabOffsetX_ -
                                                  dragged->bounds.left);
    }
    startTabAnimation();
}

void MainWindow::startTabAnimation() {
    tabAnimating_ = true;
    if (window_) {
        SetTimer(window_, kTabAnimationTimer, 16, nullptr);
    }
}

void MainWindow::tickTabAnimation() {
    if (tabVisualOffsets_.size() != documents_.size()) {
        tabVisualOffsets_.assign(documents_.size(), 0.0F);
    }
    const auto items = tabItems();
    const auto dragged = std::find_if(items.begin(), items.end(), [this](const TabItem& item) {
        return item.index == draggedTabIndex_;
    });
    const auto target = std::find_if(items.begin(), items.end(), [this](const TabItem& item) {
        return item.index == tabDragTargetIndex_;
    });
    const std::size_t draggedPosition = dragged == items.end()
        ? items.size() : static_cast<std::size_t>(std::distance(items.begin(), dragged));
    const std::size_t targetPosition = target == items.end()
        ? draggedPosition : static_cast<std::size_t>(std::distance(items.begin(), target));
    const float tabStep = dragged == items.end() ? 0.0F
        : static_cast<float>((dragged->bounds.right - dragged->bounds.left) + 5);

    bool moving = false;
    for (std::size_t position = 0; position < items.size(); ++position) {
        const auto& item = items[position];
        float desired = 0.0F;
        if (draggingTab_ && item.index == draggedTabIndex_) {
            desired = tabDragTargetOffset_;
        } else if (draggingTab_ && draggedPosition < targetPosition &&
                   position > draggedPosition && position <= targetPosition) {
            desired = -tabStep;
        } else if (draggingTab_ && targetPosition < draggedPosition &&
                   position >= targetPosition && position < draggedPosition) {
            desired = tabStep;
        }
        float& current = tabVisualOffsets_[item.index];
        const float delta = desired - current;
        if (std::abs(delta) > 0.25F) {
            current += delta * 0.32F;
            moving = true;
        } else {
            current = desired;
        }
    }
    if (!draggingTab_ && !moving) {
        tabAnimating_ = false;
        KillTimer(window_, kTabAnimationTimer);
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::paint() {
    PAINTSTRUCT paintStruct{};
    const HDC windowContext = BeginPaint(window_, &paintStruct);
    RECT windowClient{};
    GetClientRect(window_, &windowClient);
    const HDC context = CreateCompatibleDC(windowContext);
    const HBITMAP bitmap = CreateCompatibleBitmap(windowContext, std::max(1L, windowClient.right),
                                                   std::max(1L, windowClient.bottom));
    const auto oldBitmap = SelectObject(context, bitmap);

    const HBRUSH background = CreateSolidBrush(kSilver);
    FillRect(context, &windowClient, background);
    DeleteObject(background);

    if (activePage_ == Page::SystemSettings) {
        paintSettingsPage(context, windowClient);
        paintSidebar(context, windowClient);
        BitBlt(windowContext, 0, 0, windowClient.right, windowClient.bottom,
               context, 0, 0, SRCCOPY);
        SelectObject(context, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(context);
        EndPaint(window_, &paintStruct);
        return;
    }

    RECT client{0, 0, std::max<LONG>(0, windowClient.right - sidebarWidth()),
                windowClient.bottom};
    POINT previousOrigin{};
    SetViewportOrgEx(context, sidebarWidth(), 0, &previousOrigin);

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

    RECT editModeToggle = editModeToggleBounds();
    SelectObject(context, interfaceFont_);
    constexpr wchar_t kInputModeLabel[] = L"输入模式：";
    SIZE inputModeLabelSize{};
    GetTextExtentPoint32W(context, kInputModeLabel, 5, &inputModeLabelSize);
    RECT editModeLabel{std::max(0L, editModeToggle.left - inputModeLabelSize.cx - 14),
                       editModeToggle.top, editModeToggle.left - 8, editModeToggle.bottom};
    SetTextColor(context, RGB(214, 242, 245));
    DrawTextW(context, kInputModeLabel, -1, &editModeLabel,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    fillRoundedRect(context, editModeToggle, 9,
                    insertMode_ ? RGB(231, 224, 249) : RGB(220, 244, 245));
    SelectObject(context, smallFont_);
    SetTextColor(context, insertMode_ ? RGB(83, 71, 139) : kDeepTeal);
    DrawTextW(context, insertMode_ ? L"插入" : L"替换", -1, &editModeToggle,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    const RECT opacity = opacityBounds();
    SelectObject(context, smallFont_);
    SetTextColor(context, RGB(214, 242, 245));
    RECT opacityLabel{std::max(0L, opacity.left - 62), opacity.top - 9,
                      opacity.left - 8, opacity.bottom + 9};
    DrawTextW(context, L"透明度", -1, &opacityLabel,
              DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    fillRoundedRect(context, opacity, 6, RGB(62, 125, 144));
    const double opacityRatio = std::clamp(
        static_cast<double>(windowOpacityPercent_ - 25) / 75.0, 0.0, 1.0);
    RECT opacityFill = opacity;
    opacityFill.right = opacity.left + static_cast<LONG>(std::lround(
        static_cast<double>(opacity.right - opacity.left) * opacityRatio));
    if (opacityFill.right > opacityFill.left) {
        fillRoundedRect(context, opacityFill, 6, RGB(133, 220, 223));
    }
    const int opacityHandleX = opacity.left + static_cast<int>(std::lround(
        static_cast<double>(opacity.right - opacity.left) * opacityRatio));
    const HBRUSH opacityHandleBrush = CreateSolidBrush(RGB(231, 224, 249));
    const auto oldOpacityHandleBrush = SelectObject(context, opacityHandleBrush);
    const auto oldOpacityHandlePen = SelectObject(context, GetStockObject(NULL_PEN));
    Ellipse(context, opacityHandleX - 6, opacity.top - 5,
            opacityHandleX + 6, opacity.bottom + 5);
    SelectObject(context, oldOpacityHandlePen);
    SelectObject(context, oldOpacityHandleBrush);
    DeleteObject(opacityHandleBrush);

    for (const auto& item : tabItems()) {
        const bool active = item.index == activeDocument_;
        TabItem tab = item;
        const int visualOffset = tabVisualOffset(item.index);
        OffsetRect(&tab.bounds, visualOffset, 0);
        OffsetRect(&tab.closeBounds, visualOffset, 0);
        fillRoundedRect(context, tab.bounds, 12, active ? RGB(249, 253, 255) : RGB(95, 175, 194));
        if (active) {
            RECT accent{tab.bounds.left + 12, tab.bounds.bottom - 4, tab.bounds.right - 12,
                        tab.bounds.bottom - 1};
            fillRoundedRect(context, accent, 3, kLavender);
        }
        if (draggingTab_ && item.index == tabDragTargetIndex_ &&
            item.index != draggedTabIndex_) {
            const HPEN dropPen = CreatePen(PS_SOLID, 2, RGB(227, 218, 255));
            const auto oldDropPen = SelectObject(context, dropPen);
            const auto oldDropBrush = SelectObject(context, GetStockObject(NULL_BRUSH));
            RoundRect(context, tab.bounds.left + 1, tab.bounds.top + 1,
                      tab.bounds.right - 1, tab.bounds.bottom - 1, 11, 11);
            SelectObject(context, oldDropBrush);
            SelectObject(context, oldDropPen);
            DeleteObject(dropPen);
        }
        SelectObject(context, interfaceFont_);
        SetTextColor(context, active ? kDeepTeal : RGB(232, 250, 251));
        RECT label{tab.bounds.left + 14, tab.bounds.top, tab.closeBounds.left - 4, tab.bounds.bottom};
        std::wstring title = documents_[item.index].displayName;
        if (documents_[item.index].modified) {
            title += L"  •";
        }
        DrawTextW(context, title.c_str(), -1, &label,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SetTextColor(context, active ? RGB(110, 104, 158) : RGB(222, 246, 248));
        RECT close = tab.closeBounds;
        DrawTextW(context, L"×", -1, &close, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    SelectObject(context, smallFont_);
    SetTextColor(context, RGB(181, 229, 235));
    RECT dragHint = headerDragBounds();
    DrawTextW(context, L"─  拖动窗口  ─", -1, &dragHint,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    paintTempoPanel(context, client);

    RECT footer{0, client.bottom - kFooterHeight, client.right, client.bottom};
    const HBRUSH footerBrush = CreateSolidBrush(RGB(23, 83, 101));
    FillRect(context, &footer, footerBrush);
    DeleteObject(footerBrush);

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
    RECT timeRect{progress.right + 8, footer.top + 8, client.right - 8, footer.bottom - 8};
    SetTextColor(context, RGB(213, 239, 243));
    DrawTextW(context, timeText.c_str(), -1, &timeRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    SetViewportOrgEx(context, previousOrigin.x, previousOrigin.y, nullptr);
    paintSidebar(context, windowClient);
    BitBlt(windowContext, 0, 0, windowClient.right, windowClient.bottom,
           context, 0, 0, SRCCOPY);
    SelectObject(context, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(context);
    EndPaint(window_, &paintStruct);
}

void MainWindow::paintTempoPanel(HDC context, const RECT& client) {
    RECT panel = tempoPanelBounds();
    panel.right = std::min(panel.right, client.right);
    const HBRUSH panelBrush = CreateSolidBrush(RGB(31, 104, 123));
    FillRect(context, &panel, panelBrush);
    DeleteObject(panelBrush);

    const HPEN topEdge = CreatePen(PS_SOLID, 1, RGB(103, 193, 205));
    const auto oldEdge = SelectObject(context, topEdge);
    MoveToEx(context, panel.left, panel.top, nullptr);
    LineTo(context, panel.right, panel.top);
    SelectObject(context, oldEdge);
    DeleteObject(topEdge);

    const RECT toggle = tempoPanelToggleBounds();
    fillRoundedRect(context, toggle, 10, RGB(57, 137, 155));
    SelectObject(context, smallFont_);
    SetTextColor(context, RGB(211, 240, 243));
    RECT status{20, panel.top + 2, std::max(21L, toggle.left - 12), panel.top + 26};
    DrawTextW(context, statusMessage_.c_str(), -1, &status,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    const int centerX = (toggle.left + toggle.right) / 2;
    const int centerY = (toggle.top + toggle.bottom) / 2;
    const std::array<POINT, 3> chevron = tempoPanelExpanded_
        ? std::array<POINT, 3>{{{centerX - 6, centerY - 3}, {centerX + 6, centerY - 3},
                                {centerX, centerY + 4}}}
        : std::array<POINT, 3>{{{centerX - 6, centerY + 3}, {centerX + 6, centerY + 3},
                                {centerX, centerY - 4}}};
    const HBRUSH chevronBrush = CreateSolidBrush(RGB(224, 245, 247));
    const auto oldChevronBrush = SelectObject(context, chevronBrush);
    const auto oldChevronPen = SelectObject(context, GetStockObject(NULL_PEN));
    Polygon(context, chevron.data(), static_cast<int>(chevron.size()));
    SelectObject(context, oldChevronPen);
    SelectObject(context, oldChevronBrush);
    DeleteObject(chevronBrush);

    if (!tempoPanelExpanded_) {
        return;
    }

    const RECT viewport = tempoPanelViewportBounds();
    const TempoPanelLayout tempoLayout = tempoPanelLayout();
    const int saved = SaveDC(context);
    IntersectClipRect(context, viewport.left, viewport.top, viewport.right, viewport.bottom);
    SelectObject(context, interfaceFont_);
    SetTextColor(context, RGB(229, 247, 248));
    RECT bpmLabel = tempoLayout.bpm.labelBounds;
    DrawTextW(context, L"BPM", -1, &bpmLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT correctionLabel = tempoLayout.correction.labelBounds;
    DrawTextW(context, L"补正", -1, &correctionLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RECT pipaLabel = tempoLayout.arpeggio.labelBounds;
    DrawTextW(context, L"琵琶音间隔（ms）", -1, &pipaLabel,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    RestoreDC(context, saved);

    const int maximum = maximumTempoPanelScroll();
    if (maximum > 0) {
        RECT track{panel.right - 13, viewport.top, panel.right - 7, viewport.bottom};
        fillRoundedRect(context, track, 4, RGB(79, 152, 166));
        const int viewportHeight = std::max(1L, viewport.bottom - viewport.top);
        const int thumbHeight = std::max(18, viewportHeight * viewportHeight /
                                                   std::max(viewportHeight,
                                                            tempoLayout.contentHeight));
        const int travel = std::max(0, viewportHeight - thumbHeight);
        RECT thumb = track;
        thumb.top += maximum == 0 ? 0 : travel * tempoPanelScrollOffset_ / maximum;
        thumb.bottom = thumb.top + thumbHeight;
        fillRoundedRect(context, thumb, 4, RGB(184, 231, 235));
    }
}

void MainWindow::paintSidebar(HDC context, const RECT& client) {
    const RECT sidebar{0, 0, sidebarWidth(), client.bottom};
    const HBRUSH background = CreateSolidBrush(RGB(14, 57, 76));
    FillRect(context, &sidebar, background);
    DeleteObject(background);

    const RECT edge{sidebar.right - 2, 0, sidebar.right, client.bottom};
    const HBRUSH edgeBrush = CreateSolidBrush(RGB(87, 189, 202));
    FillRect(context, &edge, edgeBrush);
    DeleteObject(edgeBrush);

    const RECT settings = sidebarSettingsBounds();
    const RECT workspace = sidebarWorkspaceBounds();
    const RECT composition = sidebarCompositionBounds();
    const RECT toggle = sidebarToggleBounds();
    if (activePage_ == Page::SystemSettings) {
        fillRoundedRect(context, settings, 15, RGB(61, 139, 160));
    }
    if (activePage_ == Page::Workspace) {
        fillRoundedRect(context, workspace, 15, RGB(61, 139, 160));
    }
    if (activePage_ == Page::Composition) {
        fillRoundedRect(context, composition, 15, RGB(61, 139, 160));
    }

    RECT settingsIcon = settings;
    RECT workspaceIcon = workspace;
    RECT compositionIcon = composition;
    if (sidebarExpanded_) {
        settingsIcon.right = settingsIcon.left + 50;
        workspaceIcon.right = workspaceIcon.left + 50;
        compositionIcon.right = compositionIcon.left + 50;
    }
    drawGearIcon(context, settingsIcon,
                 activePage_ == Page::SystemSettings ? RGB(238, 235, 251)
                                                     : RGB(171, 220, 226));
    drawWorkspaceIcon(context, workspaceIcon,
                      activePage_ == Page::Workspace ? RGB(238, 235, 251)
                                                     : RGB(171, 220, 226));
    const COLORREF compositionColor = activePage_ == Page::Composition
        ? RGB(238, 235, 251) : RGB(171, 220, 226);
    const HPEN compositionPen = CreatePen(PS_SOLID, 2, compositionColor);
    const auto oldPen = SelectObject(context, compositionPen);
    const int iconLeft = compositionIcon.left + (compositionIcon.right - compositionIcon.left - 22) / 2;
    MoveToEx(context, iconLeft, compositionIcon.top + 17, nullptr);
    LineTo(context, iconLeft + 22, compositionIcon.top + 17);
    MoveToEx(context, iconLeft, compositionIcon.top + 31, nullptr);
    LineTo(context, iconLeft + 22, compositionIcon.top + 31);
    MoveToEx(context, iconLeft + 4, compositionIcon.top + 10, nullptr);
    LineTo(context, iconLeft + 4, compositionIcon.top + 38);
    SelectObject(context, oldPen);
    DeleteObject(compositionPen);

    fillRoundedRect(context, toggle, 12, RGB(25, 82, 101));
    drawSidebarChevron(context, toggle, sidebarExpanded_, RGB(177, 224, 230));

    if (sidebarExpanded_) {
        SelectObject(context, smallFont_);
        SetTextColor(context, RGB(232, 250, 251));
        RECT settingsText{settings.left + 52, settings.top, settings.right - 8, settings.bottom};
        RECT workspaceText{workspace.left + 52, workspace.top, workspace.right - 8, workspace.bottom};
        RECT compositionText{composition.left + 52, composition.top, composition.right - 8, composition.bottom};
        DrawTextW(context, L"设置", -1, &settingsText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(context, L"编辑", -1, &workspaceText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        DrawTextW(context, L"创作", -1, &compositionText, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
}

void MainWindow::paintSettingsPage(HDC context, const RECT& client) {
    const int left = sidebarWidth();
    TRIVERTEX vertices[2]{{left, 0, 0x1000, 0x5A00, 0x7300, 0},
                          {client.right, kSettingsHeaderHeight, 0x5200, 0xC900, 0xDA00, 0}};
    GRADIENT_RECT gradient{0, 1};
    GradientFill(context, vertices, 2, &gradient, 1, GRADIENT_FILL_RECT_H);
    SetBkMode(context, TRANSPARENT);

    SelectObject(context, titleFont_);
    SetTextColor(context, kWhite);
    RECT title{left + 38, 20, client.right - 36, 58};
    DrawTextW(context, L"系统设置", -1, &title, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(context, subtitleFont_);
    SetTextColor(context, RGB(205, 244, 246));
    RECT subtitle{left + 40, 59, client.right - 36, 83};
    DrawTextW(context, L"SYSTEM SETTINGS  ·  输出与应用配置", -1, &subtitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const RECT viewport = settingsViewportBounds();
    const int saved = SaveDC(context);
    IntersectClipRect(context, viewport.left, viewport.top, viewport.right, viewport.bottom);

    const int cardLeft = left + 36;
    const int cardRight = std::max(cardLeft + 320, static_cast<int>(client.right) - 38);
    const int systemIoTop = kSettingsHeaderHeight + 28 - settingsScrollOffset_;
    const RECT systemIoCard{cardLeft, systemIoTop, cardRight, systemIoTop + 206};
    fillRoundedRect(context, systemIoCard, 18, RGB(248, 252, 254));
    const RECT systemIoAccent{systemIoCard.left, systemIoCard.top, systemIoCard.left + 6,
                              systemIoCard.bottom};
    fillRoundedRect(context, systemIoAccent, 6, kLavender);

    SelectObject(context, interfaceFont_);
    SetTextColor(context, kDeepTeal);
    RECT systemIoTitle{systemIoCard.left + 28, systemIoCard.top + 20,
                       systemIoCard.right - 24, systemIoCard.top + 48};
    DrawTextW(context, L"系统IO", -1, &systemIoTitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(context, smallFont_);
    SetTextColor(context, kMuted);
    RECT systemIoHelp{systemIoCard.left + 28, systemIoCard.top + 48,
                      systemIoCard.right - 24, systemIoCard.top + 76};
    DrawTextW(context, L"选择播放时使用的按键输出后端。切换后对所有乐谱生效。", -1,
              &systemIoHelp, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(context, interfaceFont_);
    SetTextColor(context, kDeepTeal);
    RECT backendLabel{systemIoCard.left + 28, systemIoCard.top + 91,
                      systemIoCard.left + 122, systemIoCard.top + 121};
    DrawTextW(context, L"输出方式", -1, &backendLabel,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const wchar_t* backendDescription = outputBackend_ == OutputBackend::WindowsApi
        ? L"Windows API：沿用旧版本的扫描码 SendInput 输出，无需安装驱动。"
        : L"虚拟 HID 驱动：通过 YuanqinVhid 设备提交标准键盘报告，需要安装并启动驱动。";
    SelectObject(context, smallFont_);
    SetTextColor(context, RGB(73, 116, 132));
    RECT backendHelp{systemIoCard.left + 28, systemIoCard.top + 140,
                     systemIoCard.right - 24, systemIoCard.bottom - 18};
    DrawTextW(context, backendDescription, -1, &backendHelp,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

    const int defaultValuesTop = systemIoCard.bottom + 24;
    const RECT defaultValuesCard{cardLeft, defaultValuesTop, cardRight, defaultValuesTop + 230};
    fillRoundedRect(context, defaultValuesCard, 18, RGB(236, 247, 249));
    const RECT defaultValuesAccent{defaultValuesCard.left, defaultValuesCard.top,
                                   defaultValuesCard.left + 6, defaultValuesCard.bottom};
    fillRoundedRect(context, defaultValuesAccent, 6, RGB(111, 181, 196));
    SelectObject(context, interfaceFont_);
    SetTextColor(context, RGB(54, 103, 121));
    RECT defaultValuesTitle{defaultValuesCard.left + 28, defaultValuesCard.top + 20,
                            defaultValuesCard.right - 24, defaultValuesCard.top + 48};
    DrawTextW(context, L"默认数值", -1, &defaultValuesTitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(context, smallFont_);
    SetTextColor(context, kMuted);
    RECT defaultValuesHelp{defaultValuesCard.left + 28, defaultValuesCard.top + 48,
                           defaultValuesCard.right - 24, defaultValuesCard.top + 74};
    DrawTextW(context, L"仅在打开文件的瞬间应用，不会影响已打开乐谱。", -1,
              &defaultValuesHelp, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(context, interfaceFont_);
    SetTextColor(context, kDeepTeal);
    const int defaultLabelLeft = defaultValuesCard.left + 28;
    const int defaultLabelRight = defaultValuesCard.left + 178;
    RECT defaultBpmLabel{defaultLabelLeft, defaultValuesCard.top + 82,
                          defaultLabelRight, defaultValuesCard.top + 112};
    RECT defaultCorrectionLabel{defaultLabelLeft, defaultValuesCard.top + 122,
                                 defaultLabelRight, defaultValuesCard.top + 152};
    RECT defaultArpeggioLabel{defaultLabelLeft, defaultValuesCard.top + 162,
                               defaultLabelRight, defaultValuesCard.top + 192};
    DrawTextW(context, L"BPM", -1, &defaultBpmLabel, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(context, L"补正", -1, &defaultCorrectionLabel,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    DrawTextW(context, L"琵琶音间隔（ms）", -1, &defaultArpeggioLabel,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const int shortcutTop = defaultValuesCard.bottom + 24;
    const RECT shortcutCard{cardLeft, shortcutTop, cardRight, shortcutTop + 132};
    fillRoundedRect(context, shortcutCard, 18, RGB(248, 252, 254));
    const RECT shortcutAccent{shortcutCard.left, shortcutCard.top, shortcutCard.left + 6,
                              shortcutCard.bottom};
    fillRoundedRect(context, shortcutAccent, 6, RGB(151, 140, 205));
    SelectObject(context, interfaceFont_);
    SetTextColor(context, kDeepTeal);
    RECT shortcutTitle{shortcutCard.left + 28, shortcutCard.top + 22,
                       shortcutCard.right - 24, shortcutCard.top + 52};
    DrawTextW(context, L"快捷键设置", -1, &shortcutTitle,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(context, smallFont_);
    SetTextColor(context, kMuted);
    RECT shortcutHelp{shortcutCard.left + 28, shortcutCard.top + 58,
                      shortcutCard.right - 28, shortcutCard.bottom - 20};
    DrawTextW(context, L"快捷键自定义将在后续版本提供。", -1, &shortcutHelp,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RestoreDC(context, saved);

    if (maximumSettingsScroll() > 0) {
        const RECT track = settingsScrollbarBounds();
        const RECT thumb = settingsScrollThumbBounds();
        fillRoundedRect(context, track, 8, RGB(205, 224, 230));
        fillRoundedRect(context, thumb, 8, RGB(91, 170, 188));
    }
}

void MainWindow::setActivePage(Page page) {
    if (activePage_ == page) {
        return;
    }
    if (playbackRunning_) {
        stopPlayback();
    }
    activePage_ = page;
    if (page != Page::SystemSettings) {
        const DocumentKind wanted = page == Page::Composition ? DocumentKind::Composition
                                                              : DocumentKind::Standard;
        const auto found = std::find_if(documents_.begin(), documents_.end(),
            [wanted](const DocumentTab& tab) { return tab.kind == wanted; });
        if (found == documents_.end()) {
            newDocument();
        } else {
            setActiveDocument(static_cast<std::size_t>(std::distance(documents_.begin(), found)));
        }
    }
    updatePageVisibility();
    layoutChildren();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::toggleSidebar() {
    sidebarExpanded_ = !sidebarExpanded_;
    layoutChildren();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::updatePageVisibility() {
    if (!editor_.handle() || !bpmEdit_ || !bpmCorrectionCombo_ || !arpeggioIntervalEdit_ ||
        !outputBackendCombo_ || !defaultBpmEdit_ || !defaultBpmCorrectionCombo_ ||
        !defaultArpeggioIntervalEdit_) {
        return;
    }
    const bool workspaceVisible = activePage_ == Page::Workspace || activePage_ == Page::Composition;
    const RECT tempoViewport = tempoPanelViewportBounds();
    const TempoPanelLayout tempoLayout = tempoPanelLayout();
    const auto controlVisible = [&tempoViewport](const RECT& bounds) {
        return bounds.top >= tempoViewport.top && bounds.bottom <= tempoViewport.bottom;
    };
    ShowWindow(editor_.handle(), workspaceVisible ? SW_SHOW : SW_HIDE);
    ShowWindow(bpmEdit_, workspaceVisible && tempoPanelExpanded_ &&
                            controlVisible(tempoLayout.bpm.controlBounds)
                            ? SW_SHOW : SW_HIDE);
    ShowWindow(bpmCorrectionCombo_, workspaceVisible && tempoPanelExpanded_ &&
                                         controlVisible(tempoLayout.correction.controlBounds)
                                     ? SW_SHOW : SW_HIDE);
    ShowWindow(arpeggioIntervalEdit_, workspaceVisible && tempoPanelExpanded_ &&
                                            controlVisible(tempoLayout.arpeggio.controlBounds)
                                        ? SW_SHOW : SW_HIDE);
    const RECT settingsViewport = settingsViewportBounds();
    const int systemIoTop = kSettingsHeaderHeight + 28 - settingsScrollOffset_;
    const int defaultValuesTop = systemIoTop + 230;
    const auto settingsControlVisible = [&settingsViewport](int top) {
        return top >= settingsViewport.top && top + 27 <= settingsViewport.bottom;
    };
    const bool settingsVisible = !workspaceVisible;
    ShowWindow(outputBackendCombo_, settingsVisible && settingsControlVisible(systemIoTop + 96)
                                       ? SW_SHOW : SW_HIDE);
    ShowWindow(defaultBpmEdit_, settingsVisible && settingsControlVisible(defaultValuesTop + 84)
                                     ? SW_SHOW : SW_HIDE);
    ShowWindow(defaultBpmCorrectionCombo_,
               settingsVisible && settingsControlVisible(defaultValuesTop + 124)
                   ? SW_SHOW : SW_HIDE);
    ShowWindow(defaultArpeggioIntervalEdit_,
               settingsVisible && settingsControlVisible(defaultValuesTop + 164)
                   ? SW_SHOW : SW_HIDE);
}

void MainWindow::syncBpmEditor() {
    syncTempoControls();
}

void MainWindow::syncTempoControls() {
    if (!bpmEdit_ || !bpmCorrectionCombo_ || !arpeggioIntervalEdit_ ||
        activeDocument_ >= documents_.size()) {
        return;
    }
    updatingBpmEdit_ = true;
    updatingTempoControls_ = true;
    SetWindowTextW(bpmEdit_, documents_[activeDocument_].bpmText.c_str());
    const auto correction = documents_[activeDocument_].bpmCorrection;
    const auto match = std::find(kBpmCorrections.begin(), kBpmCorrections.end(), correction);
    const LRESULT selection = match == kBpmCorrections.end()
        ? 0
        : static_cast<LRESULT>(std::distance(kBpmCorrections.begin(), match));
    SendMessageW(bpmCorrectionCombo_, CB_SETCURSEL, selection, 0);
    SetWindowTextW(arpeggioIntervalEdit_,
                   std::to_wstring(documents_[activeDocument_].arpeggioIntervalMs).c_str());
    updatingTempoControls_ = false;
    updatingBpmEdit_ = false;
}

std::filesystem::path MainWindow::systemSettingsPath() {
    std::array<wchar_t, 32768> appData{};
    const DWORD length = GetEnvironmentVariableW(L"APPDATA", appData.data(),
                                                  static_cast<DWORD>(appData.size()));
    if (length > 0 && length < appData.size()) {
        return std::filesystem::path(appData.data()) / L"YuanQin" / L"settings.ini";
    }
    return std::filesystem::current_path() / L"yuanqin_settings.ini";
}

void MainWindow::loadSystemSettings() {
    const auto path = systemSettingsPath();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return;
    }

    std::string line;
    while (std::getline(input, line)) {
        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        if (key == "output_backend") {
            systemSettings_.outputBackend = value == "virtual_hid"
                ? OutputBackend::VirtualHid : OutputBackend::WindowsApi;
            continue;
        }
        try {
            if (key == "default_bpm" && !value.empty()) {
                std::size_t consumed = 0;
                const double bpm = std::stod(value, &consumed);
                if (consumed == value.size() && bpm > 0.0) {
                    systemSettings_.defaultBpmText = std::wstring(value.begin(), value.end());
                }
            } else if (key == "default_correction") {
                int correction = std::stoi(value);
                // Older builds offered ×32 by mistake. Preserve the user's intent as ×16.
                if (correction == 32) {
                    correction = 16;
                }
                if (std::find(kBpmCorrections.begin(), kBpmCorrections.end(), correction) !=
                    kBpmCorrections.end()) {
                    systemSettings_.defaultBpmCorrection = correction;
                }
            } else if (key == "default_arpeggio_interval_ms") {
                const int interval = std::stoi(value);
                if (interval > 0 && interval <= 10000) {
                    systemSettings_.defaultArpeggioIntervalMs = interval;
                }
            }
        } catch (const std::exception&) {
            // Keep the built-in default when a hand-edited configuration value is invalid.
        }
    }
}

void MainWindow::saveSystemSettings() const {
    const auto path = systemSettingsPath();
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return;
    }
    output << "output_backend=" << (systemSettings_.outputBackend == OutputBackend::VirtualHid
                                         ? "virtual_hid" : "windows_api") << '\n';
    output << "default_bpm=" << std::string(systemSettings_.defaultBpmText.begin(),
                                               systemSettings_.defaultBpmText.end()) << '\n';
    output << "default_correction=" << systemSettings_.defaultBpmCorrection << '\n';
    output << "default_arpeggio_interval_ms=" << systemSettings_.defaultArpeggioIntervalMs << '\n';
}

void MainWindow::syncSystemSettingsControls() {
    if (!outputBackendCombo_ || !defaultBpmEdit_ || !defaultBpmCorrectionCombo_ ||
        !defaultArpeggioIntervalEdit_) {
        return;
    }
    updatingSystemSettingsControls_ = true;
    SendMessageW(outputBackendCombo_, CB_SETCURSEL,
                 systemSettings_.outputBackend == OutputBackend::WindowsApi ? 0 : 1, 0);
    SetWindowTextW(defaultBpmEdit_, systemSettings_.defaultBpmText.c_str());
    const auto correction = systemSettings_.defaultBpmCorrection;
    const auto match = std::find(kBpmCorrections.begin(), kBpmCorrections.end(), correction);
    SendMessageW(defaultBpmCorrectionCombo_, CB_SETCURSEL,
                 match == kBpmCorrections.end() ? 0 :
                     static_cast<LRESULT>(std::distance(kBpmCorrections.begin(), match)), 0);
    SetWindowTextW(defaultArpeggioIntervalEdit_,
                   std::to_wstring(systemSettings_.defaultArpeggioIntervalMs).c_str());
    updatingSystemSettingsControls_ = false;
}

void MainWindow::updateOutputBackendFromControl() {
    if (updatingSystemSettingsControls_ || !outputBackendCombo_) {
        return;
    }
    if (playbackRunning_) {
        stopPlayback();
    }
    const LRESULT selection = SendMessageW(outputBackendCombo_, CB_GETCURSEL, 0, 0);
    outputBackend_ = selection == 0 ? OutputBackend::WindowsApi : OutputBackend::VirtualHid;
    systemSettings_.outputBackend = outputBackend_;
    saveSystemSettings();
    setStatus(outputBackend_ == OutputBackend::WindowsApi
                  ? L"输出方式已切换为 Windows API"
                  : L"输出方式已切换为虚拟 HID 驱动");
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::updateDefaultBpmFromControl() {
    if (updatingSystemSettingsControls_ || !defaultBpmEdit_) {
        return;
    }
    wchar_t value[32]{};
    GetWindowTextW(defaultBpmEdit_, value, static_cast<int>(std::size(value)));
    wchar_t* end = nullptr;
    const double bpm = std::wcstod(value, &end);
    if (end != value && end && *end == L'\0' && bpm > 0.0) {
        systemSettings_.defaultBpmText = value;
        saveSystemSettings();
    }
}

void MainWindow::updateDefaultBpmCorrectionFromControl() {
    if (updatingSystemSettingsControls_ || !defaultBpmCorrectionCombo_) {
        return;
    }
    const LRESULT selection = SendMessageW(defaultBpmCorrectionCombo_, CB_GETCURSEL, 0, 0);
    const std::size_t index = selection >= 0
        ? std::min<std::size_t>(static_cast<std::size_t>(selection), kBpmCorrections.size() - 1)
        : 0;
    systemSettings_.defaultBpmCorrection = kBpmCorrections[index];
    saveSystemSettings();
}

void MainWindow::updateDefaultArpeggioIntervalFromControl() {
    if (updatingSystemSettingsControls_ || !defaultArpeggioIntervalEdit_) {
        return;
    }
    wchar_t value[32]{};
    GetWindowTextW(defaultArpeggioIntervalEdit_, value, static_cast<int>(std::size(value)));
    wchar_t* end = nullptr;
    const long interval = std::wcstol(value, &end, 10);
    if (end != value && end && *end == L'\0' && interval > 0 && interval <= 10000) {
        systemSettings_.defaultArpeggioIntervalMs = static_cast<int>(interval);
        saveSystemSettings();
    }
}

void MainWindow::updateBpmCorrectionFromControl() {
    if (updatingTempoControls_ || !bpmCorrectionCombo_ || activeDocument_ >= documents_.size()) {
        return;
    }
    const LRESULT selection = SendMessageW(bpmCorrectionCombo_, CB_GETCURSEL, 0, 0);
    const std::size_t index = selection >= 0
        ? std::min<std::size_t>(static_cast<std::size_t>(selection), kBpmCorrections.size() - 1)
        : 0;
    documents_[activeDocument_].bpmCorrection = kBpmCorrections[index];
    setStatus(L"BPM 补正已设为 ×" + std::to_wstring(kBpmCorrections[index]));
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::updateArpeggioIntervalFromControl() {
    if (updatingTempoControls_ || !arpeggioIntervalEdit_ ||
        activeDocument_ >= documents_.size()) {
        return;
    }
    wchar_t value[32]{};
    GetWindowTextW(arpeggioIntervalEdit_, value, static_cast<int>(std::size(value)));
    wchar_t* end = nullptr;
    const long parsed = std::wcstol(value, &end, 10);
    documents_[activeDocument_].arpeggioIntervalMs =
        end != value && end && *end == L'\0' && parsed > 0 && parsed <= 10000
            ? static_cast<int>(parsed)
            : 0;
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::scrollTempoPanelBy(int delta) {
    const int next = std::clamp(tempoPanelScrollOffset_ + delta, 0, maximumTempoPanelScroll());
    if (next == tempoPanelScrollOffset_) {
        return;
    }
    tempoPanelScrollOffset_ = next;
    layoutChildren();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::recordActiveDocumentHistory() {
    if (activeDocument_ >= documents_.size()) {
        return;
    }
    auto& tab = documents_[activeDocument_];
    tab.undoHistory.push_back({tab.document, tab.composition, editor_.activeHand(),
                              editor_.selectionAnchorTick(), editor_.selectionCaretTick()});
    if (tab.undoHistory.size() > 200) {
        tab.undoHistory.erase(tab.undoHistory.begin());
    }
    tab.redoHistory.clear();
}

void MainWindow::undoActiveDocument() {
    if (activeDocument_ >= documents_.size()) {
        return;
    }
    auto& tab = documents_[activeDocument_];
    if (tab.undoHistory.empty()) {
        setStatus(L"没有可撤销的编辑操作");
        return;
    }
    tab.redoHistory.push_back({tab.document, tab.composition, editor_.activeHand(),
                              editor_.selectionAnchorTick(), editor_.selectionCaretTick()});
    EditorSnapshot snapshot = std::move(tab.undoHistory.back());
    tab.undoHistory.pop_back();
    tab.document = std::move(snapshot.document);
    tab.composition = std::move(snapshot.composition);
    tab.activeHand = snapshot.activeHand;
    tab.selectionAnchor = snapshot.selectionAnchor;
    tab.selectionCaret = snapshot.selectionCaret;
    tab.modified = serializedDocument(tab) != tab.savedText;
    if (tab.kind == DocumentKind::Composition) {
        editor_.setCompositionDocuments(&tab.composition.right(), &tab.composition.left());
        editor_.setActiveHand(snapshot.activeHand);
    } else {
        editor_.setDocument(&tab.document);
    }
    editor_.setSelectionRange(tab.selectionAnchor, tab.selectionCaret);
    transportTick_ = tab.selectionCaret;
    transportTotalTicks_ = activeScoreTickCount();
    updateWindowTitle();
    setStatus(L"已撤销");
}

void MainWindow::redoActiveDocument() {
    if (activeDocument_ >= documents_.size()) {
        return;
    }
    auto& tab = documents_[activeDocument_];
    if (tab.redoHistory.empty()) {
        setStatus(L"没有可恢复的编辑操作");
        return;
    }
    tab.undoHistory.push_back({tab.document, tab.composition, editor_.activeHand(),
                              editor_.selectionAnchorTick(), editor_.selectionCaretTick()});
    EditorSnapshot snapshot = std::move(tab.redoHistory.back());
    tab.redoHistory.pop_back();
    tab.document = std::move(snapshot.document);
    tab.composition = std::move(snapshot.composition);
    tab.activeHand = snapshot.activeHand;
    tab.selectionAnchor = snapshot.selectionAnchor;
    tab.selectionCaret = snapshot.selectionCaret;
    tab.modified = serializedDocument(tab) != tab.savedText;
    if (tab.kind == DocumentKind::Composition) {
        editor_.setCompositionDocuments(&tab.composition.right(), &tab.composition.left());
        editor_.setActiveHand(snapshot.activeHand);
    } else {
        editor_.setDocument(&tab.document);
    }
    editor_.setSelectionRange(tab.selectionAnchor, tab.selectionCaret);
    transportTick_ = tab.selectionCaret;
    transportTotalTicks_ = activeScoreTickCount();
    updateWindowTitle();
    setStatus(L"已恢复");
}

void MainWindow::toggleInsertMode() {
    insertMode_ = !insertMode_;
    editor_.setInsertMode(insertMode_);
    setStatus(insertMode_ ? L"已切换为插入模式：输入会将后续拍位向后移动"
                          : L"已切换为替换模式：输入会替换当前拍位");
}

void MainWindow::newDocument() {
    DocumentTab tab;
    tab.kind = activePage_ == Page::Composition ? DocumentKind::Composition : DocumentKind::Standard;
    // New tabs, including the startup editor/composition tabs, begin with the
    // values selected in System Settings.  Later edits remain per-document.
    tab.bpmText = systemSettings_.defaultBpmText;
    tab.bpmCorrection = systemSettings_.defaultBpmCorrection;
    tab.arpeggioIntervalMs = systemSettings_.defaultArpeggioIntervalMs;
    tab.displayName = (tab.kind == DocumentKind::Composition ? L"未命名创作 " : L"未命名 ") +
                      std::to_wstring(untitledCounter_++);
    tab.savedText = serializedDocument(tab);
    documents_.push_back(std::move(tab));
    setActiveDocument(documents_.size() - 1);
    setStatus(tab.kind == DocumentKind::Composition
                  ? L"已新建双手乐谱；R 为右手、L 为左手，选择任意拍位即可输入"
                  : L"已新建空白乐谱；选择任意拍位即可输入");
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
    tab.kind = activePage_ == Page::Composition ? DocumentKind::Composition : DocumentKind::Standard;
    // System defaults are applied once, at the moment an existing file is opened.
    // They never overwrite values the user subsequently changes on this tab.
    tab.bpmText = systemSettings_.defaultBpmText;
    tab.bpmCorrection = systemSettings_.defaultBpmCorrection;
    tab.arpeggioIntervalMs = systemSettings_.defaultArpeggioIntervalMs;
    if (tab.kind == DocumentKind::Composition) {
        tab.composition = ui::CompositionDocument::fromText(text);
    } else {
        tab.document = ui::ScoreDocument::fromText(text);
    }
    tab.path = normalized;
    tab.displayName = fileNameFor(normalized);
    tab.savedText = serializedDocument(tab);
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

bool MainWindow::exportCompositionAsText(std::size_t index) {
    if (index >= documents_.size() || documents_[index].kind != DocumentKind::Composition) {
        return false;
    }
    const auto path = chooseExportPath(documents_[index]);
    if (!path) {
        return false;
    }
    std::ofstream output(*path, std::ios::binary | std::ios::trunc);
    if (!output) {
        MessageBoxW(window_, L"无法写入目标文件，请检查目录权限。", L"导出失败",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    const std::string content = documents_[index].composition.mergedScore().toText();
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        MessageBoxW(window_, L"导出琴谱时发生错误。", L"导出失败", MB_OK | MB_ICONERROR);
        return false;
    }
    setStatus(L"已导出合并后的 .txt 乐谱到 " + path->wstring());
    return true;
}

bool MainWindow::writeDocument(std::size_t index, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        MessageBoxW(window_, L"无法写入目标文件，请检查目录权限。", L"保存失败",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    const std::string content = serializedDocument(documents_[index]);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output) {
        MessageBoxW(window_, L"写入乐谱时发生错误。", L"保存失败", MB_OK | MB_ICONERROR);
        return false;
    }

    documents_[index].path = std::filesystem::absolute(path).lexically_normal();
    documents_[index].displayName = fileNameFor(path);
    documents_[index].savedText = content;
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

void MainWindow::reorderDocument(std::size_t from, std::size_t target) {
    if (from >= documents_.size() || target >= documents_.size() || from == target ||
        documents_[from].kind != documents_[target].kind) {
        return;
    }

    if (tabVisualOffsets_.size() != documents_.size()) {
        tabVisualOffsets_.assign(documents_.size(), 0.0F);
    }
    std::vector<float> visualLefts(documents_.size(), 0.0F);
    for (const auto& item : tabItems()) {
        visualLefts[item.index] = static_cast<float>(item.bounds.left) +
                                  tabVisualOffsets_[item.index];
    }

    DocumentTab moved = std::move(documents_[from]);
    const float movedOffset = tabVisualOffsets_[from];
    const float movedLeft = visualLefts[from];
    documents_.erase(documents_.begin() + static_cast<std::ptrdiff_t>(from));
    tabVisualOffsets_.erase(tabVisualOffsets_.begin() + static_cast<std::ptrdiff_t>(from));
    visualLefts.erase(visualLefts.begin() + static_cast<std::ptrdiff_t>(from));
    const std::size_t insertion = target;
    documents_.insert(documents_.begin() + static_cast<std::ptrdiff_t>(insertion),
                      std::move(moved));
    tabVisualOffsets_.insert(tabVisualOffsets_.begin() + static_cast<std::ptrdiff_t>(insertion),
                             movedOffset);
    visualLefts.insert(visualLefts.begin() + static_cast<std::ptrdiff_t>(insertion), movedLeft);

    if (activeDocument_ == from) {
        activeDocument_ = insertion;
    } else {
        if (activeDocument_ > from) {
            --activeDocument_;
        }
        if (activeDocument_ >= insertion) {
            ++activeDocument_;
        }
    }
    for (const auto& item : tabItems()) {
        tabVisualOffsets_[item.index] = visualLefts[item.index] -
            static_cast<float>(item.bounds.left);
    }
    setActiveDocument(activeDocument_);
    startTabAnimation();
    setStatus(L"已调整乐谱标签顺序");
}

void MainWindow::setActiveDocument(std::size_t index) {
    if (index >= documents_.size()) {
        return;
    }
    if (playbackRunning_) {
        stopPlayback();
    }
    activeDocument_ = index;
    auto& tab = documents_[activeDocument_];
    if (tab.kind == DocumentKind::Composition) {
        editor_.setCompositionDocuments(&tab.composition.right(), &tab.composition.left());
        editor_.setActiveHand(tab.activeHand);
    } else {
        editor_.setDocument(&tab.document);
    }
    editor_.setSelectionRange(documents_[activeDocument_].selectionAnchor,
                              documents_[activeDocument_].selectionCaret);
    editor_.setInsertMode(insertMode_);
    syncBpmEditor();
    transportTick_ = documents_[activeDocument_].selectionCaret;
    transportTotalTicks_ = activeScoreTickCount();
    updateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::markActiveDocumentChanged() {
    if (activeDocument_ >= documents_.size()) {
        return;
    }
    documents_[activeDocument_].modified = serializedDocument(documents_[activeDocument_]) !=
        documents_[activeDocument_].savedText;
    transportTotalTicks_ = std::max<std::size_t>(1, activeScoreTickCount());
    updateWindowTitle();
    setStatus(L"修改仅保存在内存中 · Ctrl+S 保存 · Ctrl+Shift+S 另存为");
}

void MainWindow::onEditorSelectionChanged(std::size_t tickIndex) {
    if (activeDocument_ < documents_.size()) {
        documents_[activeDocument_].activeHand = editor_.activeHand();
        documents_[activeDocument_].selectionAnchor = editor_.selectionAnchorTick();
        documents_[activeDocument_].selectionCaret = editor_.selectionCaretTick();
    }
    transportTick_ = tickIndex;
    transportTotalTicks_ = std::max({std::size_t{1}, activeScoreTickCount(), tickIndex + 1});
    if (playbackRunning_) {
        seekTo(tickIndex, true);
    } else {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

double MainWindow::playbackBpm() const {
    if (activeDocument_ >= documents_.size()) {
        return 80.0;
    }
    const auto& document = documents_[activeDocument_];
    const std::wstring& text = document.bpmText;
    wchar_t* end = nullptr;
    const double value = std::wcstod(text.c_str(), &end);
    return end != text.c_str() && end && *end == L'\0' && value > 0.0
        ? value * static_cast<double>(document.bpmCorrection)
        : 0.0;
}

int MainWindow::arpeggioIntervalMs() const {
    return activeDocument_ < documents_.size()
        ? documents_[activeDocument_].arpeggioIntervalMs
        : 0;
}

std::size_t MainWindow::activeScoreTickCount() const {
    if (activeDocument_ >= documents_.size()) {
        return 1;
    }
    const auto& tab = documents_[activeDocument_];
    const std::size_t scoreTicks = (tab.kind == DocumentKind::Composition
        ? std::max(tab.composition.right().measureCount(), tab.composition.left().measureCount())
        : tab.document.measureCount()) * ui::kBeatsPerMeasure;
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
    const int pipaInterval = arpeggioIntervalMs();
    if (pipaInterval <= 0) {
        setStatus(L"请输入 1 到 10000 之间的琵琶音间隔（毫秒）");
        if (!tempoPanelExpanded_) {
            tempoPanelExpanded_ = true;
            layoutChildren();
        }
        SetFocus(arpeggioIntervalEdit_);
        return;
    }

    const auto& tab = documents_[activeDocument_];
    const std::string playbackText = tab.kind == DocumentKind::Composition
        ? tab.composition.mergedScore().toText()
        : tab.document.toText();
    auto parseResult = core::ScoreParser::parseText(playbackText);
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

    const bool wasPlaying = playbackRunning_;
    if (!wasPlaying) {
        capturePlaybackSelection();
    }
    stopPlayback(true, false);
    const HWND gameWindow = playback::GenshinWindowTarget::find();
    if (!gameWindow || !playback::GenshinWindowTarget::activate(gameWindow, window_)) {
        restorePlaybackSelection();
        setStatus(L"未找到或无法激活原神窗口 · 请先启动原神并保持窗口化或无边框模式");
        return;
    }

    std::unique_ptr<playback::IKeySender> keySender;
    if (outputBackend_ == OutputBackend::WindowsApi) {
        keySender = std::make_unique<playback::WindowsApiKeySender>(gameWindow);
    } else {
        auto virtualHidSender = std::make_unique<playback::VirtualHidKeySender>();
        if (!virtualHidSender->isOpen()) {
            restorePlaybackSelection();
            setStatus(L"虚拟 HID 驱动未安装或未启动 · 可在系统设置中改用 Windows API");
            return;
        }
        keySender = std::move(virtualHidSender);
    }
    transportTick_ = tickIndex;
    editor_.setPlaybackActive(true);
    editor_.setPlayheadTick(tickIndex, true);
    EnableWindow(bpmEdit_, FALSE);
    EnableWindow(bpmCorrectionCombo_, FALSE);
    EnableWindow(arpeggioIntervalEdit_, FALSE);
    playbackRunning_ = true;
    const std::uint64_t generation = ++playbackGeneration_;
    playback::PlaybackOptions options;
    options.bpm = bpm;
    options.startTick = tickIndex;
    options.arpeggioStepInterval = std::chrono::milliseconds(pipaInterval);
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
    setStatus(outputBackend_ == OutputBackend::WindowsApi
                  ? L"正在通过 Windows API 播放 · 乐谱编辑已锁定 · 点击其他拍位可立即跳播"
                  : L"正在通过虚拟 HID 键盘播放 · 乐谱编辑已锁定 · 点击其他拍位可立即跳播");
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::stopPlayback(bool unlockEditor, bool restoreCompositionSelection) {
    ++playbackGeneration_;
    if (playbackThread_.joinable()) {
        playbackThread_.request_stop();
        playbackThread_.join();
    }
    playbackRunning_ = false;
    if (unlockEditor) {
        editor_.setPlaybackActive(false);
        if (restoreCompositionSelection) {
            restorePlaybackSelection();
        }
        if (bpmEdit_) {
            EnableWindow(bpmEdit_, TRUE);
        }
        if (bpmCorrectionCombo_) {
            EnableWindow(bpmCorrectionCombo_, TRUE);
        }
        if (arpeggioIntervalEdit_) {
            EnableWindow(arpeggioIntervalEdit_, TRUE);
        }
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void MainWindow::capturePlaybackSelection() {
    playbackSelectionRestorePending_ = false;
    if (activeDocument_ >= documents_.size() ||
        documents_[activeDocument_].kind != DocumentKind::Composition) {
        return;
    }

    playbackSelectionRestorePending_ = true;
    playbackSelectionDocument_ = activeDocument_;
    playbackSelectionAnchor_ = editor_.selectionAnchorTick();
    playbackSelectionCaret_ = editor_.selectionCaretTick();
    playbackSelectionHand_ = editor_.activeHand();
}

void MainWindow::restorePlaybackSelection() {
    if (!playbackSelectionRestorePending_) {
        return;
    }
    playbackSelectionRestorePending_ = false;
    if (playbackSelectionDocument_ != activeDocument_ ||
        activeDocument_ >= documents_.size() ||
        documents_[activeDocument_].kind != DocumentKind::Composition) {
        return;
    }

    const bool playbackFinished = transportTotalTicks_ > 0 &&
        transportTick_ >= transportTotalTicks_;
    const std::size_t selectionTick = transportTotalTicks_ == 0
        ? 0
        : std::min(transportTick_, transportTotalTicks_ - 1);
    auto& tab = documents_[activeDocument_];
    // The paired composition highlight is only used while playing.  On pause,
    // return to the hand where playback began, but keep its selection at the
    // exact beat where playback stopped instead of jumping back to the start.
    tab.activeHand = playbackSelectionHand_;
    tab.selectionAnchor = selectionTick;
    tab.selectionCaret = selectionTick;
    editor_.setActiveHand(playbackSelectionHand_);
    editor_.setSelectionRange(selectionTick, selectionTick);
    // Retain the end sentinel after a completed composition so the ordinary
    // play button follows the same restart-from-zero rule as editor mode.
    transportTick_ = playbackFinished ? transportTotalTicks_ : selectionTick;
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
    EnableWindow(bpmCorrectionCombo_, TRUE);
    EnableWindow(arpeggioIntervalEdit_, TRUE);
    if (status == static_cast<int>(playback::PlaybackStatus::Completed)) {
        transportTick_ = transportTotalTicks_;
        if (transportTotalTicks_ > 0) {
            editor_.setPlayheadTick(transportTotalTicks_ - 1, false);
        }
        setStatus(L"播放完成");
    } else if (status == static_cast<int>(playback::PlaybackStatus::SendFailed)) {
        setStatus(outputBackend_ == OutputBackend::WindowsApi
                      ? L"播放中断：Windows API 按键发送失败"
                      : L"播放中断：虚拟 HID 驱动通信失败 · 请检查设备管理器中的 Yuanqin Virtual HID Keyboard");
    }
    restorePlaybackSelection();
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
        case ToolbarAction::ExportText:
            if (!documents_.empty()) exportCompositionAsText(activeDocument_);
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
    const bool composition = activePage_ == Page::Composition;
    dialog.lpstrFilter = composition ? L"双手琴谱 (*.dtxt)\0*.dtxt\0\0"
                                      : L"琴谱文本 (*.txt)\0*.txt\0\0";
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
    const bool composition = document.kind == DocumentKind::Composition;
    const std::wstring suggested = document.path ? document.path->wstring()
                                                  : document.displayName + (composition ? L".dtxt" : L".txt");
    std::copy_n(suggested.c_str(), std::min(suggested.size(), fileName.size() - 1), fileName.data());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = composition ? L"双手琴谱 (*.dtxt)\0*.dtxt\0\0"
                                      : L"琴谱文本 (*.txt)\0*.txt\0\0";
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrDefExt = composition ? L"dtxt" : L"txt";
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetSaveFileNameW(&dialog) ? std::optional<std::filesystem::path>(fileName.data())
                                     : std::nullopt;
}

std::optional<std::filesystem::path> MainWindow::chooseExportPath(
    const DocumentTab& document) const {
    std::array<wchar_t, 32768> fileName{};
    const std::wstring initialDirectory = scoreDirectory().wstring();
    const std::wstring suggested = document.path
        ? document.path->stem().wstring() + L".txt"
        : document.displayName + L".txt";
    std::copy_n(suggested.c_str(), std::min(suggested.size(), fileName.size() - 1), fileName.data());
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = window_;
    dialog.lpstrFilter = L"琴谱文本 (*.txt)\0*.txt\0\0";
    dialog.lpstrFile = fileName.data();
    dialog.nMaxFile = static_cast<DWORD>(fileName.size());
    dialog.lpstrDefExt = L"txt";
    dialog.lpstrInitialDir = initialDirectory.c_str();
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;
    return GetSaveFileNameW(&dialog) ? std::optional<std::filesystem::path>(fileName.data())
                                     : std::nullopt;
}

std::string MainWindow::serializedDocument(const DocumentTab& document) const {
    return document.kind == DocumentKind::Composition ? document.composition.toText()
                                                       : document.document.toText();
}

}  // namespace yuanqin::app
