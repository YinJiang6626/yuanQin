#pragma once

#include "yuanqin/ui/ScoreDocument.h"

#include <Windows.h>

#include <cstddef>
#include <functional>
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
    void setChangedCallback(std::function<void()> callback);
    void setSelectionChangedCallback(std::function<void(std::size_t)> callback);
    void setPlaybackActive(bool active);
    void setPlayheadTick(std::size_t tickIndex, bool centerIfFollowing = true);
    [[nodiscard]] std::size_t selectedTick() const noexcept;
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
    void notifyChanged();
    void notifySelectionChanged();
    void moveSelection(long long beatDelta);
    [[nodiscard]] std::size_t displayedRowCount() const;
    [[nodiscard]] std::vector<CellLayout> calculateLayout(int clientWidth) const;
    [[nodiscard]] bool selectAt(POINT point);

    HWND window_{nullptr};
    ScoreDocument* document_{nullptr};
    std::function<void()> changedCallback_;
    std::function<void(std::size_t)> selectionChangedCallback_;
    HFONT noteFont_{nullptr};
    HFONT smallFont_{nullptr};
    std::size_t selectedMeasure_{0};
    std::size_t selectedBeat_{0};
    int scrollOffset_{0};
    bool playbackActive_{false};
    bool followPlayback_{true};
};

}  // namespace yuanqin::ui
