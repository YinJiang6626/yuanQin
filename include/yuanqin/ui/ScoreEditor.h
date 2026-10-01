#pragma once

#include "yuanqin/ui/ScoreDocument.h"

#include <Windows.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace yuanqin::ui {

class ScoreEditor {
public:
    ScoreEditor() = default;
    ~ScoreEditor();

    ScoreEditor(const ScoreEditor&) = delete;
    ScoreEditor& operator=(const ScoreEditor&) = delete;

    [[nodiscard]] static bool registerWindowClass(HINSTANCE instance);
    [[nodiscard]] bool create(HWND parent, HINSTANCE instance, int controlId);
    [[nodiscard]] HWND handle() const noexcept;

    void setDocument(ScoreDocument* document);
    void setSelectionRange(std::size_t anchorTick, std::size_t caretTick);
    void setChangedCallback(std::function<void()> callback);
    void setBeforeChangeCallback(std::function<void()> callback);
    void setSelectionChangedCallback(std::function<void(std::size_t)> callback);
    void setInsertMode(bool enabled);
    void setPlaybackActive(bool active);
    void setPlayheadTick(std::size_t tickIndex, bool centerIfFollowing = true);
    [[nodiscard]] std::size_t selectedTick() const noexcept;
    [[nodiscard]] std::size_t selectionAnchorTick() const noexcept;
    [[nodiscard]] std::size_t selectionCaretTick() const noexcept;
    [[nodiscard]] std::size_t selectionStartTick() const noexcept;
    [[nodiscard]] std::size_t selectionEndTick() const noexcept;
    [[nodiscard]] std::size_t displayedTickCount() const;

private:
    struct CellLayout {
        RECT bounds{};
        std::size_t measureIndex{};
        std::size_t beatIndex{};
    };

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam,
                                             LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void paint();
    void createFonts();
    void destroyFonts();
    void updateScrollBar();
    void setScrollOffset(int value);
    void scrollBy(int delta);
    void ensureSelectionVisible();
    void centerSelection();
    void markUserScroll();
    void notifyBeforeChange();
    void notifyChanged();
    void notifySelectionChanged();
    void moveSelection(long long beatDelta, bool extendSelection = false);
    void setCaretTick(std::size_t tickIndex, bool extendSelection);
    void collapseSelectionForMove(bool towardEnd);
    void replaceSelection(const std::vector<std::string>& values, bool insertBefore);
    void typeNote(char note);
    void beginGroup(char opening);
    void clearSelection();
    void deleteSelectionBackward();
    void copySelectionToClipboard() const;
    void cutSelectionToClipboard();
    void pasteClipboard();
    [[nodiscard]] std::vector<std::string> clipboardBeats() const;
    [[nodiscard]] bool hasSelection() const noexcept;
    [[nodiscard]] std::size_t displayedRowCount() const;
    [[nodiscard]] std::vector<CellLayout> calculateLayout(int clientWidth) const;
    [[nodiscard]] bool selectAt(POINT point);

    HWND window_{nullptr};
    ScoreDocument* document_{nullptr};
    std::function<void()> changedCallback_;
    std::function<void()> beforeChangeCallback_;
    std::function<void(std::size_t)> selectionChangedCallback_;
    HFONT noteFont_{nullptr};
    HFONT smallFont_{nullptr};
    std::size_t selectedMeasure_{0};
    std::size_t selectedBeat_{0};
    std::size_t selectionAnchorTick_{0};
    bool mouseSelecting_{false};
    bool insertMode_{false};
    int scrollOffset_{0};
    bool playbackActive_{false};
    bool followPlayback_{true};
};

}  // namespace yuanqin::ui
