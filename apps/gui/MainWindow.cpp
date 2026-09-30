#include "MainWindow.h"

#include <commdlg.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

namespace yuanqin::app {
namespace {

constexpr wchar_t kMainWindowClassName[] = L"YuanQinMainWindow";
constexpr int kHeaderHeight = 160;
constexpr int kFooterHeight = 32;
constexpr int kEditorControlId = 1001;

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

}  // namespace

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
    window_ = CreateWindowExW(0, kMainWindowClassName, L"幽歌琴谱", WS_OVERLAPPEDWINDOW |
                              WS_CLIPCHILDREN, x, y, width, height, nullptr, nullptr, instance, this);
    if (!window_) {
        return false;
    }

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
            editor_.setChangedCallback([this] { markActiveDocumentChanged(); });
            newDocument();
            layoutChildren();
            return 0;

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

        case WM_PAINT:
            paint();
            return 0;

        case WM_LBUTTONDOWN: {
            const POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
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
            return 0;
        }

        case WM_CLOSE:
            for (std::size_t index = documents_.size(); index > 0; --index) {
                if (!confirmClose(index - 1)) {
                    return 0;
                }
            }
            DestroyWindow(window_);
            return 0;

        case WM_DESTROY:
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
    if (!window_ || !editor_.handle()) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    MoveWindow(editor_.handle(), 0, kHeaderHeight, client.right,
               std::max(0, static_cast<int>(client.bottom) - kHeaderHeight - kFooterHeight), TRUE);
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

    RECT footer{0, client.bottom - kFooterHeight, client.right, client.bottom};
    const HBRUSH footerBrush = CreateSolidBrush(RGB(23, 83, 101));
    FillRect(context, &footer, footerBrush);
    DeleteObject(footerBrush);
    SelectObject(context, smallFont_);
    SetTextColor(context, RGB(215, 241, 244));
    RECT status{26, footer.top, client.right - 26, footer.bottom};
    DrawTextW(context, statusMessage_.c_str(), -1, &status,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

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
    activeDocument_ = index;
    editor_.setDocument(&documents_[activeDocument_].document);
    updateWindowTitle();
    InvalidateRect(window_, nullptr, FALSE);
}

void MainWindow::markActiveDocumentChanged() {
    if (activeDocument_ >= documents_.size()) {
        return;
    }
    documents_[activeDocument_].modified = true;
    updateWindowTitle();
    setStatus(L"修改仅保存在内存中 · Ctrl+S 保存 · Ctrl+Shift+S 另存为");
}

void MainWindow::invoke(ToolbarAction action) {
    switch (action) {
        case ToolbarAction::NewFile: newDocument(); break;
        case ToolbarAction::OpenFile: openDocument(); break;
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
