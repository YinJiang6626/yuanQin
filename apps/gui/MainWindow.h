#pragma once

#include "yuanqin/ui/ScoreDocument.h"
#include "yuanqin/ui/ScoreEditor.h"

#include <Windows.h>

#include <filesystem>
#include <optional>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace yuanqin::app {

class MainWindow {
public:
    ~MainWindow();

    [[nodiscard]] bool create(HINSTANCE instance, int showCommand);
    [[nodiscard]] bool processShortcut(const MSG& message);
    [[nodiscard]] HWND handle() const noexcept;

private:
    enum class ToolbarAction { NewFile, OpenFile, Save, SaveAs };

    struct DocumentTab {
        ui::ScoreDocument document;
        std::optional<std::filesystem::path> path;
        std::wstring displayName;
        bool modified{false};
    };

    struct ToolbarItem {
        RECT bounds{};
        ToolbarAction action{};
        const wchar_t* label{};
    };

    struct TabItem {
        RECT bounds{};
        RECT closeBounds{};
        std::size_t index{};
    };

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam,
                                             LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createFonts();
    void destroyFonts();
    void layoutChildren();
    void paint();
    void newDocument();
    void openDocument();
    bool saveDocument(std::size_t index);
    bool saveDocumentAs(std::size_t index);
    bool writeDocument(std::size_t index, const std::filesystem::path& path);
    bool confirmClose(std::size_t index);
    void closeDocument(std::size_t index);
    void setActiveDocument(std::size_t index);
    void markActiveDocumentChanged();
    void onEditorSelectionChanged(std::size_t tickIndex);
    void invoke(ToolbarAction action);
    void startPlayback(std::size_t tickIndex);
    void stopPlayback(bool unlockEditor = true);
    void togglePlayback();
    void playFromBeginning();
    void seekTo(std::size_t tickIndex, bool keepPlaying);
    void handlePlaybackProgress(std::uint64_t generation, std::size_t tickIndex);
    void handlePlaybackComplete(std::uint64_t generation, int status);
    void updateWindowTitle();
    void setStatus(std::wstring message);
    [[nodiscard]] double playbackBpm() const;
    [[nodiscard]] std::size_t activeScoreTickCount() const;
    [[nodiscard]] std::size_t progressTickFromX(int x) const;
    void setWindowOpacityFromX(int x);
    void applyWindowOpacity();
    [[nodiscard]] RECT playPauseBounds() const;
    [[nodiscard]] RECT playFromBeginningBounds() const;
    [[nodiscard]] RECT progressBounds() const;
    [[nodiscard]] RECT opacityBounds() const;
    [[nodiscard]] RECT headerDragBounds() const;
    [[nodiscard]] bool shouldHandleWithoutActivation(POINT clientPoint) const;
    [[nodiscard]] std::vector<ToolbarItem> toolbarItems() const;
    [[nodiscard]] std::vector<TabItem> tabItems() const;
    [[nodiscard]] std::optional<std::filesystem::path> chooseOpenPath() const;
    [[nodiscard]] std::optional<std::filesystem::path> chooseSavePath(
        const DocumentTab& document) const;

    HWND window_{nullptr};
    HINSTANCE instance_{nullptr};
    HWND bpmEdit_{nullptr};
    HBRUSH bpmEditBrush_{nullptr};
    ui::ScoreEditor editor_;
    std::vector<DocumentTab> documents_;
    std::size_t activeDocument_{0};
    int untitledCounter_{1};
    HFONT titleFont_{nullptr};
    HFONT subtitleFont_{nullptr};
    HFONT interfaceFont_{nullptr};
    HFONT smallFont_{nullptr};
    std::wstring statusMessage_{L"单击任意拍位开始编辑 · ( ) 为和弦 · [ ] 为琵琶音"};
    std::jthread playbackThread_;
    bool playbackRunning_{false};
    std::uint64_t playbackGeneration_{0};
    std::size_t transportTick_{0};
    std::size_t transportTotalTicks_{1};
    bool draggingProgress_{false};
    bool dragWasPlaying_{false};
    std::size_t draggedTick_{0};
    bool draggingOpacity_{false};
    int windowOpacityPercent_{92};
    bool draggingWindow_{false};
    POINT windowDragStartCursor_{};
    RECT windowDragStartBounds_{};
};

}  // namespace yuanqin::app
