#include "yuanqin/ui/ScoreEditor.h"

#include "yuanqin/core/ScoreParser.h"

#include <imm.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <utility>

namespace yuanqin::ui {
namespace {

constexpr wchar_t kEditorClassName[] = L"YuanQinScoreEditor";
constexpr int kPageMargin = 26;
constexpr int kRowTop = 22;
constexpr int kRowHeight = 88;
constexpr int kRowGap = 14;
constexpr int kRowNumberWidth = 46;
constexpr std::size_t kDefaultRows = 8;
constexpr UINT_PTR kReturnToPlayheadTimer = 1;

constexpr COLORREF kCanvas = RGB(238, 247, 250);
constexpr COLORREF kCard = RGB(252, 254, 255);
constexpr COLORREF kCardBorder = RGB(190, 220, 228);
constexpr COLORREF kMeasureLine = RGB(102, 180, 200);
constexpr COLORREF kBeatLine = RGB(196, 203, 229);
constexpr COLORREF kInk = RGB(25, 76, 96);
constexpr COLORREF kMutedInk = RGB(103, 137, 151);
constexpr COLORREF kSelection = RGB(226, 239, 250);
constexpr COLORREF kSelectionBorder = RGB(120, 102, 180);
constexpr COLORREF kEmptyHint = RGB(188, 208, 217);

std::wstring widenAscii(const std::string& value) {
    return std::wstring(value.begin(), value.end());
}

int textUnits(const std::string& value) {
    return std::max(1, static_cast<int>(value.size()));
}

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

}  // namespace

ScoreEditor::~ScoreEditor() {
    destroyFonts();
}

bool ScoreEditor::registerWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32513));
    windowClass.lpszClassName = kEditorClassName;
    return RegisterClassExW(&windowClass) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool ScoreEditor::create(HWND parent, HINSTANCE instance, int controlId) {
    window_ = CreateWindowExW(0, kEditorClassName, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                 WS_VSCROLL | WS_CLIPCHILDREN,
                              0, 0, 0, 0, parent,
                              reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)), instance,
                              this);
    return window_ != nullptr;
}

HWND ScoreEditor::handle() const noexcept {
    return window_;
}

void ScoreEditor::setDocument(ScoreDocument* document) {
    document_ = document;
    selectedMeasure_ = 0;
    selectedBeat_ = 0;
    scrollOffset_ = 0;
    playbackActive_ = false;
    followPlayback_ = true;
    if (window_) {
        KillTimer(window_, kReturnToPlayheadTimer);
    }
    updateScrollBar();
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void ScoreEditor::setChangedCallback(std::function<void()> callback) {
    changedCallback_ = std::move(callback);
}

void ScoreEditor::setSelectionChangedCallback(std::function<void(std::size_t)> callback) {
    selectionChangedCallback_ = std::move(callback);
}

void ScoreEditor::setPlaybackActive(bool active) {
    playbackActive_ = active;
    followPlayback_ = true;
    if (!active && window_) {
        KillTimer(window_, kReturnToPlayheadTimer);
    }
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void ScoreEditor::setPlayheadTick(std::size_t tickIndex, bool centerIfFollowing) {
    selectedMeasure_ = tickIndex / kBeatsPerMeasure;
    selectedBeat_ = tickIndex % kBeatsPerMeasure;
    updateScrollBar();
    if (playbackActive_ && centerIfFollowing) {
        if (followPlayback_) {
            centerSelection();
        }
    } else {
        ensureSelectionVisible();
    }
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

std::size_t ScoreEditor::selectedTick() const noexcept {
    return selectedMeasure_ * kBeatsPerMeasure + selectedBeat_;
}

std::size_t ScoreEditor::displayedTickCount() const {
    return displayedRowCount() * kMeasuresPerRow * kBeatsPerMeasure;
}

LRESULT CALLBACK ScoreEditor::windowProcedure(HWND window, UINT message, WPARAM wParam,
                                               LPARAM lParam) {
    auto* editor = reinterpret_cast<ScoreEditor*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<CREATESTRUCTW*>(lParam);
        editor = static_cast<ScoreEditor*>(creation->lpCreateParams);
        editor->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(editor));
    }
    return editor ? editor->handleMessage(message, wParam, lParam)
                  : DefWindowProcW(window, message, wParam, lParam);
}

LRESULT ScoreEditor::handleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE:
            // Score cells accept physical musical keys, not natural-language text.
            // Disabling the IME for this child window prevents letter notes from
            // being captured by a Chinese input-method candidate popup.
            ImmAssociateContext(window_, nullptr);
            createFonts();
            return 0;

        case WM_DESTROY:
            destroyFonts();
            window_ = nullptr;
            return 0;

        case WM_SIZE:
            updateScrollBar();
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            paint();
            return 0;

        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTTAB;

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            InvalidateRect(window_, nullptr, FALSE);
            return 0;

        case WM_LBUTTONDOWN: {
            if (GetForegroundWindow() == GetAncestor(window_, GA_ROOT)) {
                SetFocus(window_);
            }
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            if (selectAt(point)) {
                followPlayback_ = true;
                KillTimer(window_, kReturnToPlayheadTimer);
                InvalidateRect(window_, nullptr, FALSE);
                notifySelectionChanged();
            }
            return 0;
        }

        case WM_MOUSEWHEEL:
            markUserScroll();
            scrollBy(-GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * 58);
            return 0;

        case WM_VSCROLL: {
            markUserScroll();
            SCROLLINFO info{};
            info.cbSize = sizeof(info);
            info.fMask = SIF_ALL;
            GetScrollInfo(window_, SB_VERT, &info);
            int next = scrollOffset_;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: next -= 44; break;
                case SB_LINEDOWN: next += 44; break;
                case SB_PAGEUP: next -= static_cast<int>(info.nPage); break;
                case SB_PAGEDOWN: next += static_cast<int>(info.nPage); break;
                case SB_THUMBTRACK: next = info.nTrackPos; break;
                default: return 0;
            }
            setScrollOffset(next);
            return 0;
        }

        case WM_TIMER:
            if (wParam == kReturnToPlayheadTimer) {
                KillTimer(window_, kReturnToPlayheadTimer);
                if (playbackActive_) {
                    followPlayback_ = true;
                    centerSelection();
                    InvalidateRect(window_, nullptr, FALSE);
                }
                return 0;
            }
            break;

        case WM_KEYDOWN:
            if (!document_) {
                return 0;
            }
            if (playbackActive_) {
                return 0;
            }
            if (!(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
                if (wParam >= 'A' && wParam <= 'Z' &&
                    core::ScoreParser::isPlayableNote(static_cast<char>(wParam))) {
                    const auto& before = document_->beat(selectedMeasure_, selectedBeat_);
                    const bool isGroup = before.size() >= 2 &&
                                         (before.front() == '(' || before.front() == '[');
                    document_->typeNote(selectedMeasure_, selectedBeat_, static_cast<char>(wParam));
                    notifyChanged();
                    if (!isGroup) {
                        moveSelection(1);
                    }
                    return 0;
                }
                if (wParam == VK_OEM_4 || (wParam == '9' && (GetKeyState(VK_SHIFT) & 0x8000))) {
                    document_->beginGroup(selectedMeasure_, selectedBeat_,
                                          wParam == VK_OEM_4 ? '[' : '(');
                    notifyChanged();
                    return 0;
                }
            }
            switch (wParam) {
                case VK_LEFT: moveSelection(-1); return 0;
                case VK_RIGHT: moveSelection(1); return 0;
                case VK_UP: moveSelection(-static_cast<long long>(kBeatsPerMeasure * kMeasuresPerRow)); return 0;
                case VK_DOWN: moveSelection(kBeatsPerMeasure * kMeasuresPerRow); return 0;
                case VK_TAB: moveSelection((GetKeyState(VK_SHIFT) & 0x8000) ? -1 : 1); return 0;
                case VK_HOME:
                    selectedBeat_ = 0;
                    InvalidateRect(window_, nullptr, FALSE);
                    notifySelectionChanged();
                    return 0;
                case VK_END:
                    selectedBeat_ = kBeatsPerMeasure - 1;
                    InvalidateRect(window_, nullptr, FALSE);
                    notifySelectionChanged();
                    return 0;
                case VK_BACK:
                    document_->backspace(selectedMeasure_, selectedBeat_);
                    notifyChanged();
                    return 0;
                case VK_DELETE:
                    document_->clearBeat(selectedMeasure_, selectedBeat_);
                    notifyChanged();
                    return 0;
                case VK_RETURN:
                    moveSelection(1);
                    return 0;
                default:
                    break;
            }
            break;

        case WM_CHAR: {
            // Musical input is handled from WM_KEYDOWN so it remains independent
            // of the active Windows input method (including Chinese IMEs).
            return 0;
        }
    }

    return DefWindowProcW(window_, message, wParam, lParam);
}

void ScoreEditor::createFonts() {
    if (noteFont_) {
        return;
    }
    noteFont_ = CreateFontW(-22, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            VARIABLE_PITCH, L"Cascadia Mono");
    smallFont_ = CreateFontW(-13, 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             VARIABLE_PITCH, L"Microsoft YaHei UI");
}

void ScoreEditor::destroyFonts() {
    if (noteFont_) {
        DeleteObject(noteFont_);
        noteFont_ = nullptr;
    }
    if (smallFont_) {
        DeleteObject(smallFont_);
        smallFont_ = nullptr;
    }
}

std::size_t ScoreEditor::displayedRowCount() const {
    const std::size_t documentRows = document_
        ? (document_->measureCount() + kMeasuresPerRow - 1) / kMeasuresPerRow
        : 0;
    const std::size_t selectionRows = selectedMeasure_ / kMeasuresPerRow + 1;
    return std::max({kDefaultRows, documentRows + 1, selectionRows + 1});
}

std::vector<ScoreEditor::CellLayout> ScoreEditor::calculateLayout(int clientWidth) const {
    std::vector<CellLayout> cells;
    const int cardRight = std::max(kPageMargin + 300, clientWidth - kPageMargin - 16);
    const int contentLeft = kPageMargin + kRowNumberWidth;
    const int contentRight = cardRight - 12;
    const int availableWidth = std::max(240, contentRight - contentLeft);

    for (std::size_t row = 0; row < displayedRowCount(); ++row) {
        const int y = kRowTop + static_cast<int>(row) * (kRowHeight + kRowGap) - scrollOffset_;
        std::array<double, kMeasuresPerRow> measureWeights{};
        double measureWeightTotal = 0.0;
        for (std::size_t column = 0; column < kMeasuresPerRow; ++column) {
            const std::size_t measureIndex = row * kMeasuresPerRow + column;
            int units = 0;
            for (std::size_t beatIndex = 0; beatIndex < kBeatsPerMeasure; ++beatIndex) {
                units += textUnits(document_ ? document_->beat(measureIndex, beatIndex) : std::string{});
            }
            measureWeights[column] = static_cast<double>(std::max(4, units));
            measureWeightTotal += measureWeights[column];
        }

        const int measureBase = static_cast<int>(availableWidth * 0.17);
        const int flexibleWidth = std::max(0, availableWidth - measureBase * 4);
        int measureX = contentLeft;
        for (std::size_t column = 0; column < kMeasuresPerRow; ++column) {
            const int measureRight = column + 1 == kMeasuresPerRow
                ? contentRight
                : measureX + measureBase + static_cast<int>(std::round(
                    flexibleWidth * measureWeights[column] / measureWeightTotal));
            const int measureWidth = std::max(40, measureRight - measureX);

            std::array<double, kBeatsPerMeasure> beatWeights{};
            double beatWeightTotal = 0.0;
            const std::size_t measureIndex = row * kMeasuresPerRow + column;
            for (std::size_t beatIndex = 0; beatIndex < kBeatsPerMeasure; ++beatIndex) {
                beatWeights[beatIndex] = textUnits(
                    document_ ? document_->beat(measureIndex, beatIndex) : std::string{});
                beatWeightTotal += beatWeights[beatIndex];
            }

            const int beatBase = std::max(8, static_cast<int>(measureWidth * 0.09));
            const int beatFlexible = std::max(0, measureWidth - beatBase * 4);
            int beatX = measureX;
            for (std::size_t beatIndex = 0; beatIndex < kBeatsPerMeasure; ++beatIndex) {
                const int beatRight = beatIndex + 1 == kBeatsPerMeasure
                    ? measureRight
                    : beatX + beatBase + static_cast<int>(std::round(
                        beatFlexible * beatWeights[beatIndex] / beatWeightTotal));
                cells.push_back({{beatX, y + 24, beatRight, y + kRowHeight - 10},
                                 measureIndex, beatIndex});
                beatX = beatRight;
            }
            measureX = measureRight;
        }
    }
    return cells;
}

void ScoreEditor::paint() {
    PAINTSTRUCT paintStruct{};
    const HDC windowContext = BeginPaint(window_, &paintStruct);
    RECT client{};
    GetClientRect(window_, &client);

    const HDC context = CreateCompatibleDC(windowContext);
    const HBITMAP bitmap = CreateCompatibleBitmap(windowContext, std::max(1L, client.right),
                                                   std::max(1L, client.bottom));
    const auto oldBitmap = SelectObject(context, bitmap);
    const HBRUSH canvasBrush = CreateSolidBrush(kCanvas);
    FillRect(context, &client, canvasBrush);
    DeleteObject(canvasBrush);
    SetBkMode(context, TRANSPARENT);

    const auto cells = calculateLayout(client.right);
    const std::size_t rows = displayedRowCount();
    for (std::size_t row = 0; row < rows; ++row) {
        const int y = kRowTop + static_cast<int>(row) * (kRowHeight + kRowGap) - scrollOffset_;
        if (y + kRowHeight < 0 || y > client.bottom) {
            continue;
        }

        RECT shadow{kPageMargin + 2, y + 3, client.right - kPageMargin - 14, y + kRowHeight + 3};
        fillRoundedRect(context, shadow, 14, RGB(220, 235, 240));
        RECT card{kPageMargin, y, client.right - kPageMargin - 16, y + kRowHeight};
        fillRoundedRect(context, card, 14, kCard);
        const HPEN cardPen = CreatePen(PS_SOLID, 1, kCardBorder);
        const auto oldPen = SelectObject(context, cardPen);
        const auto oldBrush = SelectObject(context, GetStockObject(NULL_BRUSH));
        RoundRect(context, card.left, card.top, card.right, card.bottom, 14, 14);
        SelectObject(context, oldBrush);
        SelectObject(context, oldPen);
        DeleteObject(cardPen);

        SelectObject(context, smallFont_);
        SetTextColor(context, kMutedInk);
        RECT numberRect{kPageMargin + 8, y + 27, kPageMargin + kRowNumberWidth - 4,
                        y + kRowHeight - 10};
        const std::wstring number = std::to_wstring(row + 1);
        DrawTextW(context, number.c_str(), -1, &numberRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        const auto firstCell = row * kMeasuresPerRow * kBeatsPerMeasure;
        for (std::size_t column = 0; column < kMeasuresPerRow; ++column) {
            const auto measureFirst = firstCell + column * kBeatsPerMeasure;
            if (measureFirst >= cells.size()) {
                break;
            }
            RECT labelRect{cells[measureFirst].bounds.left, y + 5,
                           cells[measureFirst + kBeatsPerMeasure - 1].bounds.right, y + 24};
            const std::wstring label = L"M" + std::to_wstring(row * kMeasuresPerRow + column + 1);
            SetTextColor(context, RGB(130, 167, 180));
            DrawTextW(context, label.c_str(), -1, &labelRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        if (cell.bounds.bottom < 0 || cell.bounds.top > client.bottom) {
            continue;
        }

        const bool selected = document_ && cell.measureIndex == selectedMeasure_ &&
                              cell.beatIndex == selectedBeat_;
        if (selected) {
            RECT highlight = cell.bounds;
            InflateRect(&highlight, -3, -5);
            fillRoundedRect(context, highlight, 9,
                            playbackActive_ ? RGB(214, 246, 245) : kSelection);
            const HPEN selectionPen = CreatePen(PS_SOLID, 2,
                                                 playbackActive_ ? RGB(39, 170, 176)
                                                                 : kSelectionBorder);
            const auto oldPen = SelectObject(context, selectionPen);
            const auto oldBrush = SelectObject(context, GetStockObject(NULL_BRUSH));
            RoundRect(context, highlight.left, highlight.top, highlight.right, highlight.bottom, 9, 9);
            SelectObject(context, oldBrush);
            SelectObject(context, oldPen);
            DeleteObject(selectionPen);
        }

        if (cell.beatIndex > 0) {
            const HPEN dotted = CreatePen(PS_DOT, 1, kBeatLine);
            const auto oldPen = SelectObject(context, dotted);
            MoveToEx(context, cell.bounds.left, cell.bounds.top + 9, nullptr);
            LineTo(context, cell.bounds.left, cell.bounds.bottom - 7);
            SelectObject(context, oldPen);
            DeleteObject(dotted);
        } else if (cell.measureIndex % kMeasuresPerRow > 0) {
            const HPEN divider = CreatePen(PS_SOLID, 1, kMeasureLine);
            const auto oldPen = SelectObject(context, divider);
            MoveToEx(context, cell.bounds.left, cell.bounds.top - 14, nullptr);
            LineTo(context, cell.bounds.left, cell.bounds.bottom - 4);
            SelectObject(context, oldPen);
            DeleteObject(divider);
        }

        RECT textRect = cell.bounds;
        InflateRect(&textRect, -4, -4);
        const std::string value = document_ ? document_->beat(cell.measureIndex, cell.beatIndex)
                                            : std::string{};
        SelectObject(context, noteFont_);
        if (value.empty()) {
            SetTextColor(context, kEmptyHint);
            const wchar_t* placeholder = selected ? L"·" : L"";
            DrawTextW(context, placeholder, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(context, kInk);
            const std::wstring wideValue = widenAscii(value);
            DrawTextW(context, wideValue.c_str(), -1, &textRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    }

    BitBlt(windowContext, 0, 0, client.right, client.bottom, context, 0, 0, SRCCOPY);
    SelectObject(context, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(context);
    EndPaint(window_, &paintStruct);
}

void ScoreEditor::updateScrollBar() {
    if (!window_) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const int totalHeight = kRowTop * 2 + static_cast<int>(displayedRowCount()) *
                            (kRowHeight + kRowGap) - kRowGap;
    SCROLLINFO info{};
    info.cbSize = sizeof(info);
    info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    info.nMin = 0;
    info.nMax = std::max(0, totalHeight - 1);
    info.nPage = static_cast<UINT>(std::max(0L, client.bottom));
    info.nPos = scrollOffset_;
    SetScrollInfo(window_, SB_VERT, &info, TRUE);
    setScrollOffset(scrollOffset_);
}

void ScoreEditor::setScrollOffset(int value) {
    if (!window_) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const int totalHeight = kRowTop * 2 + static_cast<int>(displayedRowCount()) *
                            (kRowHeight + kRowGap) - kRowGap;
    const int maximum = std::max(0, totalHeight - static_cast<int>(client.bottom));
    const int next = std::clamp(value, 0, maximum);
    if (next != scrollOffset_) {
        scrollOffset_ = next;
        SetScrollPos(window_, SB_VERT, scrollOffset_, TRUE);
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void ScoreEditor::scrollBy(int delta) {
    setScrollOffset(scrollOffset_ + delta);
}

void ScoreEditor::ensureSelectionVisible() {
    if (!window_) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const int row = static_cast<int>(selectedMeasure_ / kMeasuresPerRow);
    const int logicalTop = kRowTop + row * (kRowHeight + kRowGap);
    const int logicalBottom = logicalTop + kRowHeight;
    if (logicalTop < scrollOffset_) {
        setScrollOffset(logicalTop - 6);
    } else if (logicalBottom > scrollOffset_ + client.bottom) {
        setScrollOffset(logicalBottom - client.bottom + 6);
    }
}

void ScoreEditor::centerSelection() {
    if (!window_) {
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    const int row = static_cast<int>(selectedMeasure_ / kMeasuresPerRow);
    const int rowCenter = kRowTop + row * (kRowHeight + kRowGap) + kRowHeight / 2;
    setScrollOffset(rowCenter - static_cast<int>(client.bottom) / 2);
}

void ScoreEditor::markUserScroll() {
    if (!playbackActive_ || !window_) {
        return;
    }
    followPlayback_ = false;
    KillTimer(window_, kReturnToPlayheadTimer);
    SetTimer(window_, kReturnToPlayheadTimer, 3000, nullptr);
}

void ScoreEditor::notifyChanged() {
    updateScrollBar();
    InvalidateRect(window_, nullptr, FALSE);
    if (changedCallback_) {
        changedCallback_();
    }
}

void ScoreEditor::notifySelectionChanged() {
    if (selectionChangedCallback_) {
        selectionChangedCallback_(selectedTick());
    }
}

void ScoreEditor::moveSelection(long long beatDelta) {
    const long long current = static_cast<long long>(selectedMeasure_ * kBeatsPerMeasure + selectedBeat_);
    const long long next = std::max(0LL, current + beatDelta);
    selectedMeasure_ = static_cast<std::size_t>(next) / kBeatsPerMeasure;
    selectedBeat_ = static_cast<std::size_t>(next) % kBeatsPerMeasure;
    updateScrollBar();
    ensureSelectionVisible();
    InvalidateRect(window_, nullptr, FALSE);
    notifySelectionChanged();
}

bool ScoreEditor::selectAt(POINT point) {
    RECT client{};
    GetClientRect(window_, &client);
    for (const auto& cell : calculateLayout(client.right)) {
        if (PtInRect(&cell.bounds, point)) {
            selectedMeasure_ = cell.measureIndex;
            selectedBeat_ = cell.beatIndex;
            return true;
        }
    }
    return false;
}

}  // namespace yuanqin::ui
