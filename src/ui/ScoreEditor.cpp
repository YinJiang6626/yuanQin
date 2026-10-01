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
constexpr int kCompositionRowHeight = 142;
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
    leftDocument_ = nullptr;
    activeHand_ = Hand::Right;
    selectedMeasure_ = 0;
    selectedBeat_ = 0;
    selectionAnchorTick_ = 0;
    groupCaretOffset_ = 0;
    groupEditing_ = false;
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

void ScoreEditor::setCompositionDocuments(ScoreDocument* right, ScoreDocument* left) {
    document_ = right;
    leftDocument_ = left;
    activeHand_ = Hand::Right;
    selectedMeasure_ = 0;
    selectedBeat_ = 0;
    selectionAnchorTick_ = 0;
    groupCaretOffset_ = 0;
    groupEditing_ = false;
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

void ScoreEditor::setActiveHand(Hand hand) {
    if (isComposition()) {
        activeHand_ = hand;
        if (window_) {
            InvalidateRect(window_, nullptr, FALSE);
        }
    }
}

void ScoreEditor::setSelectionRange(std::size_t anchorTick, std::size_t caretTick) {
    groupEditing_ = false;
    selectionAnchorTick_ = anchorTick;
    selectedMeasure_ = caretTick / kBeatsPerMeasure;
    selectedBeat_ = caretTick % kBeatsPerMeasure;
    resetGroupCaret();
    updateScrollBar();
    ensureSelectionVisible();
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void ScoreEditor::setChangedCallback(std::function<void()> callback) {
    changedCallback_ = std::move(callback);
}

void ScoreEditor::setBeforeChangeCallback(std::function<void()> callback) {
    beforeChangeCallback_ = std::move(callback);
}

void ScoreEditor::setSelectionChangedCallback(std::function<void(std::size_t)> callback) {
    selectionChangedCallback_ = std::move(callback);
}

void ScoreEditor::setInsertMode(bool enabled) {
    insertMode_ = enabled;
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
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
    selectionAnchorTick_ = tickIndex;
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

std::size_t ScoreEditor::selectionAnchorTick() const noexcept {
    return selectionAnchorTick_;
}

std::size_t ScoreEditor::selectionCaretTick() const noexcept {
    return selectedTick();
}

std::size_t ScoreEditor::selectionStartTick() const noexcept {
    return std::min(selectionAnchorTick_, selectedTick());
}

std::size_t ScoreEditor::selectionEndTick() const noexcept {
    return std::max(selectionAnchorTick_, selectedTick());
}

std::size_t ScoreEditor::displayedTickCount() const {
    return displayedRowCount() * kMeasuresPerRow * kBeatsPerMeasure;
}

ScoreEditor::Hand ScoreEditor::activeHand() const noexcept {
    return activeHand_;
}

ScoreDocument* ScoreEditor::activeDocument() noexcept {
    return documentFor(activeHand_);
}

const ScoreDocument* ScoreEditor::activeDocument() const noexcept {
    return documentFor(activeHand_);
}

ScoreDocument* ScoreEditor::documentFor(Hand hand) noexcept {
    return hand == Hand::Left && leftDocument_ ? leftDocument_ : document_;
}

const ScoreDocument* ScoreEditor::documentFor(Hand hand) const noexcept {
    return hand == Hand::Left && leftDocument_ ? leftDocument_ : document_;
}

bool ScoreEditor::isComposition() const noexcept {
    return leftDocument_ != nullptr;
}

int ScoreEditor::rowHeight() const noexcept {
    return isComposition() ? kCompositionRowHeight : kRowHeight;
}

int ScoreEditor::rowStride() const noexcept {
    return rowHeight() + kRowGap;
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
            if (selectAt(point, true)) {
                followPlayback_ = true;
                KillTimer(window_, kReturnToPlayheadTimer);
                selectionAnchorTick_ = selectedTick();
                if (!playbackActive_) {
                    mouseSelecting_ = true;
                    SetCapture(window_);
                }
                InvalidateRect(window_, nullptr, FALSE);
                notifySelectionChanged();
            }
            return 0;
        }

        case WM_MOUSEMOVE:
            if (mouseSelecting_) {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (selectAt(point)) {
                    ensureSelectionVisible();
                    InvalidateRect(window_, nullptr, FALSE);
                    notifySelectionChanged();
                }
                return 0;
            }
            break;

        case WM_LBUTTONUP:
            if (mouseSelecting_) {
                mouseSelecting_ = false;
                ReleaseCapture();
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (selectAt(point)) {
                    ensureSelectionVisible();
                    InvalidateRect(window_, nullptr, FALSE);
                    notifySelectionChanged();
                }
                return 0;
            }
            break;

        case WM_CAPTURECHANGED:
            mouseSelecting_ = false;
            return 0;

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

        case WM_KEYDOWN: {
            if (!activeDocument()) {
                return 0;
            }
            if (playbackActive_) {
                return 0;
            }
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                switch (wParam) {
                    case 'C': copySelectionToClipboard(); return 0;
                    case 'X': cutSelectionToClipboard(); return 0;
                    case 'V': pasteClipboard(); return 0;
                    default: break;
                }
            }
            if (!(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
                if (wParam >= 'A' && wParam <= 'Z' &&
                    core::ScoreParser::isPlayableNote(static_cast<char>(wParam))) {
                    typeNote(static_cast<char>(wParam));
                    return 0;
                }
                if (wParam == VK_OEM_4 || (wParam == '9' && (GetKeyState(VK_SHIFT) & 0x8000))) {
                    beginGroup(wParam == VK_OEM_4 ? '[' : '(');
                    return 0;
                }
                if (wParam == '0' && (GetKeyState(VK_SHIFT) & 0x8000)) {
                    beginGroup('(');
                    return 0;
                }
                if (wParam == VK_SPACE) {
                    replaceSelection({std::string{}}, insertMode_);
                    return 0;
                }
            }
            const bool extendSelection = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            switch (wParam) {
                case VK_LEFT:
                    if (!extendSelection && moveGroupCaret(-1)) return 0;
                    moveSelection(-1, extendSelection);
                    return 0;
                case VK_RIGHT:
                    if (!extendSelection && moveGroupCaret(1)) return 0;
                    moveSelection(1, extendSelection);
                    return 0;
                case VK_UP: moveSelection(-static_cast<long long>(kBeatsPerMeasure * kMeasuresPerRow), extendSelection); return 0;
                case VK_DOWN: moveSelection(kBeatsPerMeasure * kMeasuresPerRow, extendSelection); return 0;
                case VK_TAB: moveSelection(extendSelection ? -1 : 1, extendSelection); return 0;
                case VK_HOME:
                    setCaretTick((selectedTick() / kBeatsPerMeasure) * kBeatsPerMeasure,
                                 extendSelection);
                    return 0;
                case VK_END:
                    setCaretTick((selectedTick() / kBeatsPerMeasure) * kBeatsPerMeasure +
                                     kBeatsPerMeasure - 1,
                                 extendSelection);
                    return 0;
                case VK_BACK:
                    if (deleteGroupBackward()) return 0;
                    deleteSelectionBackward();
                    return 0;
                case VK_DELETE:
                    clearSelection();
                    return 0;
                case VK_RETURN:
                    if (!hasSelection()) {
                        const auto* document = activeDocument();
                        const auto& value = document
                            ? document->beat(selectedMeasure_, selectedBeat_)
                            : std::string{};
                        if (value.size() >= 2 &&
                            (value.front() == '(' || value.front() == '[')) {
                            groupEditing_ = !groupEditing_;
                            if (groupEditing_) {
                                resetGroupCaret();
                            }
                            InvalidateRect(window_, nullptr, FALSE);
                            return 0;
                        }
                    }
                    moveSelection(1, false);
                    return 0;
                default:
                    break;
            }
            break;
        }

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
    const auto rowsFor = [](const ScoreDocument* source) {
        return source ? (source->measureCount() + kMeasuresPerRow - 1) / kMeasuresPerRow : 0;
    };
    const std::size_t documentRows = std::max(rowsFor(document_), rowsFor(leftDocument_));
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
        std::array<double, kMeasuresPerRow> measureWeights{};
        std::array<std::array<double, kBeatsPerMeasure>, kMeasuresPerRow> beatWeights{};
        double measureWeightTotal = 0.0;
        for (std::size_t column = 0; column < kMeasuresPerRow; ++column) {
            const std::size_t measureIndex = row * kMeasuresPerRow + column;
            int measureUnits = 0;
            for (std::size_t beatIndex = 0; beatIndex < kBeatsPerMeasure; ++beatIndex) {
                int units = textUnits(document_ ? document_->beat(measureIndex, beatIndex)
                                                : std::string{});
                if (isComposition()) {
                    units = std::max(units, textUnits(leftDocument_
                        ? leftDocument_->beat(measureIndex, beatIndex) : std::string{}));
                }
                beatWeights[column][beatIndex] = static_cast<double>(units);
                measureUnits += units;
            }
            measureWeights[column] = static_cast<double>(std::max(4, measureUnits));
            measureWeightTotal += measureWeights[column];
        }

        const auto appendHand = [&](int cellTop, int cellBottom, Hand hand) {
        const int measureBase = static_cast<int>(availableWidth * 0.17);
        const int flexibleWidth = std::max(0, availableWidth - measureBase * 4);
        int measureX = contentLeft;
        for (std::size_t column = 0; column < kMeasuresPerRow; ++column) {
            const std::size_t measureIndex = row * kMeasuresPerRow + column;
            const int measureRight = column + 1 == kMeasuresPerRow
                ? contentRight
                : measureX + measureBase + static_cast<int>(std::round(
                    flexibleWidth * measureWeights[column] / measureWeightTotal));
            const int measureWidth = std::max(40, measureRight - measureX);

            double beatWeightTotal = 0.0;
            for (std::size_t beatIndex = 0; beatIndex < kBeatsPerMeasure; ++beatIndex) {
                beatWeightTotal += beatWeights[column][beatIndex];
            }

            const int beatBase = std::max(8, static_cast<int>(measureWidth * 0.09));
            const int beatFlexible = std::max(0, measureWidth - beatBase * 4);
            int beatX = measureX;
            for (std::size_t beatIndex = 0; beatIndex < kBeatsPerMeasure; ++beatIndex) {
                const int beatRight = beatIndex + 1 == kBeatsPerMeasure
                    ? measureRight
                    : beatX + beatBase + static_cast<int>(std::round(
                        beatFlexible * beatWeights[column][beatIndex] / beatWeightTotal));
                cells.push_back({{beatX, cellTop, beatRight, cellBottom},
                                 measureIndex, beatIndex, hand});
                beatX = beatRight;
            }
            measureX = measureRight;
        }
        };

        const int y = kRowTop + static_cast<int>(row) * rowStride() - scrollOffset_;
        if (isComposition()) {
            const int contentTop = y + 24;
            const int contentBottom = y + rowHeight() - 10;
            constexpr int handGap = 8;
            const int handHeight = (contentBottom - contentTop - handGap) / 2;
            appendHand(contentTop, contentTop + handHeight, Hand::Right);
            appendHand(contentTop + handHeight + handGap, contentBottom, Hand::Left);
        } else {
            appendHand(y + 24, y + rowHeight() - 10, Hand::Right);
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
        const int rowHeightValue = rowHeight();
        const int y = kRowTop + static_cast<int>(row) * rowStride() - scrollOffset_;
        if (y + rowHeightValue < 0 || y > client.bottom) {
            continue;
        }

        RECT shadow{kPageMargin + 2, y + 3, client.right - kPageMargin - 14, y + rowHeightValue + 3};
        fillRoundedRect(context, shadow, 14, RGB(220, 235, 240));
        RECT card{kPageMargin, y, client.right - kPageMargin - 16, y + rowHeightValue};
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
        const std::wstring number = std::to_wstring(row + 1);
        if (!isComposition()) {
            RECT numberRect{kPageMargin + 8, y + 27, kPageMargin + kRowNumberWidth - 4,
                            y + rowHeightValue - 10};
            DrawTextW(context, number.c_str(), -1, &numberRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        const auto cellsPerHandRow = kMeasuresPerRow * kBeatsPerMeasure;
        const auto firstCell = row * cellsPerHandRow * (isComposition() ? 2 : 1);
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
        if (isComposition()) {
            const auto leftFirst = firstCell + cellsPerHandRow;
            if (leftFirst >= cells.size()) {
                continue;
            }
            RECT numberRect{kPageMargin + 6, cells[firstCell].bounds.top,
                            kPageMargin + 22, cells[leftFirst].bounds.bottom};
            DrawTextW(context, number.c_str(), -1, &numberRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            RECT rightLabel{kPageMargin + 24, cells[firstCell].bounds.top,
                            kPageMargin + kRowNumberWidth - 4, cells[firstCell].bounds.bottom};
            RECT leftLabel{kPageMargin + 24, cells[leftFirst].bounds.top,
                           kPageMargin + kRowNumberWidth - 4, cells[leftFirst].bounds.bottom};
            SetTextColor(context, RGB(91, 83, 146));
            DrawTextW(context, L"R", -1, &rightLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SetTextColor(context, RGB(45, 137, 145));
            DrawTextW(context, L"L", -1, &leftLabel, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }

    for (std::size_t index = 0; index < cells.size(); ++index) {
        const auto& cell = cells[index];
        if (cell.bounds.bottom < 0 || cell.bounds.top > client.bottom) {
            continue;
        }

        const std::size_t cellTick = cell.measureIndex * kBeatsPerMeasure + cell.beatIndex;
        const bool playbackCell = playbackActive_ && isComposition() &&
                                  cellTick == selectedTick();
        const bool selected = playbackCell || (activeDocument() && cell.hand == activeHand_ &&
                              cellTick >= selectionStartTick() &&
                              cellTick <= selectionEndTick());
        const bool caret = playbackCell || (activeDocument() && cell.hand == activeHand_ &&
                           cell.measureIndex == selectedMeasure_ &&
                           cell.beatIndex == selectedBeat_);
        if (selected) {
            RECT highlight = cell.bounds;
            InflateRect(&highlight, -3, -5);
            fillRoundedRect(context, highlight, 9,
                            playbackActive_ ? RGB(214, 246, 245) : kSelection);
            const HPEN selectionPen = CreatePen(PS_SOLID, caret ? 2 : 1,
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
        const auto* source = documentFor(cell.hand);
        const std::string value = source ? source->beat(cell.measureIndex, cell.beatIndex)
                                            : std::string{};
        SelectObject(context, noteFont_);
        if (value.empty()) {
            SetTextColor(context, kEmptyHint);
            const wchar_t* placeholder = caret ? L"·" : L"";
            DrawTextW(context, placeholder, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            SetTextColor(context, kInk);
            const std::wstring wideValue = widenAscii(value);
            DrawTextW(context, wideValue.c_str(), -1, &textRect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            const bool hasGroupCursor = caret && groupEditing_ && !playbackActive_ && value.size() >= 2 &&
                (value.front() == '(' || value.front() == '[') && groupCaretOffset_ > 0 &&
                groupCaretOffset_ < value.size();
            if (hasGroupCursor) {
                SIZE total{};
                SIZE prefix{};
                GetTextExtentPoint32W(context, wideValue.c_str(),
                                      static_cast<int>(wideValue.size()), &total);
                GetTextExtentPoint32W(context, wideValue.c_str(),
                                      static_cast<int>(groupCaretOffset_), &prefix);
                const int textStart = (textRect.left + textRect.right - total.cx) / 2;
                const int caretX = std::clamp(textStart + prefix.cx, textRect.left + 2,
                                              textRect.right - 2);
                const HPEN caretPen = CreatePen(PS_SOLID, 2, RGB(84, 59, 156));
                const auto oldPen = SelectObject(context, caretPen);
                MoveToEx(context, caretX, textRect.top + 5, nullptr);
                LineTo(context, caretX, textRect.bottom - 5);
                SelectObject(context, oldPen);
                DeleteObject(caretPen);
            }
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
                            rowStride() - kRowGap;
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
                            rowStride() - kRowGap;
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
    const int logicalTop = kRowTop + row * rowStride();
    const int logicalBottom = logicalTop + rowHeight();
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
    const int rowCenter = kRowTop + row * rowStride() + rowHeight() / 2;
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

void ScoreEditor::notifyBeforeChange() {
    if (beforeChangeCallback_) {
        beforeChangeCallback_();
    }
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

bool ScoreEditor::hasSelection() const noexcept {
    return selectionAnchorTick_ != selectedTick();
}

void ScoreEditor::setCaretTick(std::size_t tickIndex, bool extendSelection) {
    selectedMeasure_ = tickIndex / kBeatsPerMeasure;
    selectedBeat_ = tickIndex % kBeatsPerMeasure;
    if (!extendSelection) {
        selectionAnchorTick_ = tickIndex;
    }
    groupEditing_ = false;
    resetGroupCaret();
    updateScrollBar();
    ensureSelectionVisible();
    InvalidateRect(window_, nullptr, FALSE);
    notifySelectionChanged();
}

void ScoreEditor::collapseSelectionForMove(bool towardEnd) {
    if (!hasSelection()) {
        return;
    }
    setCaretTick(towardEnd ? selectionEndTick() : selectionStartTick(), false);
}

void ScoreEditor::moveSelection(long long beatDelta, bool extendSelection) {
    if (!extendSelection && hasSelection()) {
        collapseSelectionForMove(beatDelta > 0);
        return;
    }
    const long long current = static_cast<long long>(selectedTick());
    const long long next = std::max(0LL, current + beatDelta);
    setCaretTick(static_cast<std::size_t>(next), extendSelection);
}

void ScoreEditor::resetGroupCaret() {
    const auto* document = activeDocument();
    if (!document) {
        groupCaretOffset_ = 0;
        return;
    }
    const auto& value = document->beat(selectedMeasure_, selectedBeat_);
    groupCaretOffset_ = value.size() >= 2 && (value.front() == '(' || value.front() == '[')
        ? value.size() - 1
        : 0;
}

bool ScoreEditor::moveGroupCaret(int delta) {
    if (hasSelection() || !groupEditing_) {
        return false;
    }
    const auto* document = activeDocument();
    if (!document) {
        return false;
    }
    const auto& value = document->beat(selectedMeasure_, selectedBeat_);
    if (value.size() < 2 || (value.front() != '(' && value.front() != '[')) {
        return false;
    }
    if (groupCaretOffset_ == 0 || groupCaretOffset_ >= value.size()) {
        groupCaretOffset_ = value.size() - 1;
    }
    if ((delta < 0 && groupCaretOffset_ <= 1) ||
        (delta > 0 && groupCaretOffset_ >= value.size() - 1)) {
        groupEditing_ = false;
        InvalidateRect(window_, nullptr, FALSE);
        return false;
    }
    groupCaretOffset_ = static_cast<std::size_t>(std::clamp(
        static_cast<long long>(groupCaretOffset_) + delta, 1LL,
        static_cast<long long>(value.size() - 1)));
    if (window_) {
        InvalidateRect(window_, nullptr, FALSE);
    }
    return true;
}

bool ScoreEditor::deleteGroupBackward() {
    if (!groupEditing_ || hasSelection()) {
        return false;
    }
    auto* document = activeDocument();
    if (!document) {
        return false;
    }
    const auto& value = document->beat(selectedMeasure_, selectedBeat_);
    if (value.size() < 2 || (value.front() != '(' && value.front() != '[')) {
        groupEditing_ = false;
        return false;
    }
    const bool willChange = value == "()" || value == "[]" || groupCaretOffset_ > 1;
    if (willChange) {
        notifyBeforeChange();
    }
    const bool changed = document->eraseGroupCharacterBefore(
        selectedMeasure_, selectedBeat_, groupCaretOffset_);
    if (changed) {
        if (document->beat(selectedMeasure_, selectedBeat_).empty()) {
            groupEditing_ = false;
        }
        notifyChanged();
    }
    InvalidateRect(window_, nullptr, FALSE);
    return true;
}

void ScoreEditor::replaceSelection(const std::vector<std::string>& values, bool insertBefore) {
    auto* document = activeDocument();
    if (!document || values.empty()) {
        return;
    }
    const std::size_t start = selectionStartTick();
    const std::size_t count = selectionEndTick() - start + 1;
    notifyBeforeChange();
    if (hasSelection()) {
        document->eraseBeats(start, count);
        document->insertBeats(start, values);
    } else if (insertBefore) {
        document->insertBeats(start, values);
    } else {
        for (std::size_t index = 0; index < values.size(); ++index) {
            document->setBeatAt(start + index, values[index]);
        }
    }
    setCaretTick(start + values.size(), false);
    notifyChanged();
}

void ScoreEditor::typeNote(char note) {
    auto* document = activeDocument();
    if (!document) {
        return;
    }
    const auto& before = document->beat(selectedMeasure_, selectedBeat_);
    const bool isGroup = before.size() >= 2 && (before.front() == '(' || before.front() == '[');
    if (!hasSelection() && isGroup && groupEditing_) {
        notifyBeforeChange();
        if (document->insertGroupNote(selectedMeasure_, selectedBeat_, groupCaretOffset_, note)) {
            notifyChanged();
            return;
        }
    }
    if (!hasSelection() && isGroup && !groupEditing_) {
        moveSelection(1, false);
        typeNote(note);
        return;
    }
    if (hasSelection() || insertMode_) {
        replaceSelection({std::string(1, note)}, insertMode_);
        return;
    }
    notifyBeforeChange();
    document->typeNote(selectedMeasure_, selectedBeat_, note);
    notifyChanged();
    if (!isGroup) {
        moveSelection(1, false);
    }
}

void ScoreEditor::beginGroup(char opening) {
    auto* document = activeDocument();
    if (!document) {
        return;
    }
    const auto& value = document->beat(selectedMeasure_, selectedBeat_);
    if (opening == '(' && !hasSelection() && value.size() >= 2 &&
        value.front() == '[' && value.back() == ']' && groupEditing_) {
        notifyBeforeChange();
        if (document->insertPipaChord(selectedMeasure_, selectedBeat_, groupCaretOffset_)) {
            notifyChanged();
            return;
        }
    }
    if (!hasSelection() && value.size() >= 2 &&
        (value.front() == '(' || value.front() == '[') && !groupEditing_) {
        moveSelection(1, false);
        beginGroup(opening);
        return;
    }
    if (hasSelection()) {
        replaceSelection({opening == '(' ? "()" : "[]"}, insertMode_);
        return;
    }
    notifyBeforeChange();
    document->beginGroup(selectedMeasure_, selectedBeat_, opening);
    groupCaretOffset_ = 1;
    groupEditing_ = true;
    notifyChanged();
}

void ScoreEditor::clearSelection() {
    auto* document = activeDocument();
    if (!document) {
        return;
    }
    const std::size_t start = selectionStartTick();
    const std::size_t count = selectionEndTick() - start + 1;
    notifyBeforeChange();
    document->clearBeats(start, count);
    setCaretTick(start, false);
    notifyChanged();
}

void ScoreEditor::deleteSelectionBackward() {
    auto* document = activeDocument();
    if (!document) {
        return;
    }
    std::size_t start = selectionStartTick();
    std::size_t count = selectionEndTick() - start + 1;
    if (!hasSelection()) {
        if (start == 0) {
            return;
        }
        --start;
        count = 1;
    }
    notifyBeforeChange();
    document->eraseBeats(start, count);
    setCaretTick(start, false);
    notifyChanged();
}

void ScoreEditor::copySelectionToClipboard() const {
    const auto* document = activeDocument();
    if (!document || !OpenClipboard(window_)) {
        return;
    }
    EmptyClipboard();
    const auto values = document->beatsInRange(selectionStartTick(),
                                                 selectionEndTick() - selectionStartTick() + 1);
    std::wstring text;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            text.push_back(L'\t');
        }
        text.append(values[index].begin(), values[index].end());
    }
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory) {
        if (auto* target = static_cast<wchar_t*>(GlobalLock(memory))) {
            std::copy(text.c_str(), text.c_str() + text.size() + 1, target);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                GlobalFree(memory);
            }
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

void ScoreEditor::cutSelectionToClipboard() {
    auto* document = activeDocument();
    if (!document) {
        return;
    }
    copySelectionToClipboard();
    const std::size_t start = selectionStartTick();
    const std::size_t count = selectionEndTick() - start + 1;
    notifyBeforeChange();
    document->eraseBeats(start, count);
    setCaretTick(start, false);
    notifyChanged();
}

std::vector<std::string> ScoreEditor::clipboardBeats() const {
    std::vector<std::string> result;
    if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(window_)) {
        return result;
    }
    const HGLOBAL handle = GetClipboardData(CF_UNICODETEXT);
    const auto* source = handle ? static_cast<const wchar_t*>(GlobalLock(handle)) : nullptr;
    if (!source) {
        CloseClipboard();
        return result;
    }
    std::string text;
    for (const wchar_t* current = source; *current; ++current) {
        if (*current <= 0x7f) {
            text.push_back(static_cast<char>(*current));
        }
    }
    GlobalUnlock(handle);
    CloseClipboard();

    const bool hasSeparators = text.find_first_of("\t\r\n") != std::string::npos;
    if (hasSeparators) {
        std::string current;
        for (std::size_t index = 0; index < text.size(); ++index) {
            const char value = text[index];
            if (value == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
                continue;
            }
            if (value == '\t' || value == '\n' || value == '\r') {
                result.push_back(std::move(current));
                current.clear();
            } else {
                current.push_back(value);
            }
        }
        result.push_back(std::move(current));
        return result;
    }

    for (std::size_t index = 0; index < text.size();) {
        const char value = text[index++];
        if (value == '(' || value == '[') {
            const char closing = value == '(' ? ')' : ']';
            std::string group(1, value);
            while (index < text.size() && text[index] != closing) {
                if (value == '[' && (text[index] == '(' || text[index] == ')')) {
                    group.push_back(text[index]);
                } else if (core::ScoreParser::isPlayableNote(text[index])) {
                    group.push_back(text[index]);
                }
                ++index;
            }
            if (index < text.size() && text[index] == closing) {
                ++index;
            }
            group.push_back(closing);
            result.push_back(std::move(group));
        } else if (value == ' ') {
            result.emplace_back();
        } else if (core::ScoreParser::isPlayableNote(value)) {
            result.emplace_back(1, value);
        }
    }
    return result;
}

void ScoreEditor::pasteClipboard() {
    if (!activeDocument()) {
        return;
    }
    const auto values = clipboardBeats();
    if (values.empty()) {
        return;
    }
    replaceSelection(values, insertMode_);
}

bool ScoreEditor::selectAt(POINT point, bool toggleGroupEditing) {
    RECT client{};
    GetClientRect(window_, &client);
    for (const auto& cell : calculateLayout(client.right)) {
        if (PtInRect(&cell.bounds, point)) {
            const bool sameCell = activeHand_ == cell.hand &&
                selectedMeasure_ == cell.measureIndex && selectedBeat_ == cell.beatIndex &&
                !hasSelection();
            activeHand_ = cell.hand;
            selectedMeasure_ = cell.measureIndex;
            selectedBeat_ = cell.beatIndex;
            const auto* document = activeDocument();
            const std::string value = document ? document->beat(selectedMeasure_, selectedBeat_)
                                               : std::string{};
            const bool isGroup = value.size() >= 2 &&
                (value.front() == '(' || value.front() == '[');
            if (sameCell && toggleGroupEditing && isGroup) {
                groupEditing_ = !groupEditing_;
            } else if (!sameCell) {
                groupEditing_ = isGroup;
                resetGroupCaret();
            }
            return true;
        }
    }
    return false;
}

}  // namespace yuanqin::ui
