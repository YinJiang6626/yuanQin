#pragma once

#include "yuanqin/ui/ScoreDocument.h"
#include "yuanqin/ui/CompositionDocument.h"
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
    enum class ToolbarAction { NewFile, OpenFile, Save, SaveAs, ExportText };
    enum class Page { SystemSettings, Workspace, Composition };
    enum class DocumentKind { Standard, Composition };
    enum class OutputBackend { WindowsApi, VirtualHid };

    struct EditorSnapshot {
        ui::ScoreDocument document;
        ui::CompositionDocument composition;
        ui::ScoreEditor::Hand activeHand{ui::ScoreEditor::Hand::Right};
        std::size_t selectionAnchor{};
        std::size_t selectionCaret{};
    };

    struct DocumentTab {
        DocumentKind kind{DocumentKind::Standard};
        ui::ScoreDocument document;
        ui::CompositionDocument composition;
        std::optional<std::filesystem::path> path;
        std::wstring displayName;
        std::string savedText;
        std::wstring bpmText{L"80"};
        int bpmCorrection{1};
        // 4 minimum units at BPM 80 last 750 ms; 750 / 7 rounds to 107 ms.
        int arpeggioIntervalMs{107};
        ui::ScoreEditor::Hand activeHand{ui::ScoreEditor::Hand::Right};
        std::size_t selectionAnchor{};
        std::size_t selectionCaret{};
        std::vector<EditorSnapshot> undoHistory;
        std::vector<EditorSnapshot> redoHistory;
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

    struct TempoSettingLayout {
        RECT labelBounds{};
        RECT controlBounds{};
    };

    struct TempoPanelLayout {
        TempoSettingLayout bpm;
        TempoSettingLayout correction;
        TempoSettingLayout arpeggio;
        int contentHeight{};
    };

    struct SystemSettings {
        OutputBackend outputBackend{OutputBackend::WindowsApi};
        std::wstring defaultBpmText{L"80"};
        int defaultBpmCorrection{1};
        int defaultArpeggioIntervalMs{107};
    };

    static LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam,
                                             LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createFonts();
    void destroyFonts();
    void layoutChildren();
    void paint();
    void paintSidebar(HDC context, const RECT& client);
    void paintSettingsPage(HDC context, const RECT& client);
    void setActivePage(Page page);
    void toggleSidebar();
    void updatePageVisibility();
    void syncBpmEditor();
    void syncTempoControls();
    void updateOutputBackendFromControl();
    void updateDefaultBpmFromControl();
    void updateDefaultBpmCorrectionFromControl();
    void updateDefaultArpeggioIntervalFromControl();
    void updateBpmCorrectionFromControl();
    void updateArpeggioIntervalFromControl();
    void recordActiveDocumentHistory();
    void undoActiveDocument();
    void redoActiveDocument();
    void toggleInsertMode();
    void scrollSettingsBy(int delta);
    void setSettingsScrollFromY(int y);
    void scrollTempoPanelBy(int delta);
    void loadSystemSettings();
    void saveSystemSettings() const;
    void syncSystemSettingsControls();
    void newDocument();
    void openDocument();
    bool saveDocument(std::size_t index);
    bool saveDocumentAs(std::size_t index);
    bool exportCompositionAsText(std::size_t index);
    bool writeDocument(std::size_t index, const std::filesystem::path& path);
    bool confirmClose(std::size_t index);
    void closeDocument(std::size_t index);
    void reorderDocument(std::size_t from, std::size_t target);
    void updateTabDragTarget(POINT point);
    void startTabAnimation();
    void tickTabAnimation();
    [[nodiscard]] int tabVisualOffset(std::size_t documentIndex) const;
    void setActiveDocument(std::size_t index);
    void markActiveDocumentChanged();
    void onEditorSelectionChanged(std::size_t tickIndex);
    void invoke(ToolbarAction action);
    void startPlayback(std::size_t tickIndex);
    void stopPlayback(bool unlockEditor = true, bool restoreCompositionSelection = true);
    void capturePlaybackSelection();
    void restorePlaybackSelection();
    void togglePlayback();
    void playFromBeginning();
    void seekTo(std::size_t tickIndex, bool keepPlaying);
    void handlePlaybackProgress(std::uint64_t generation, std::size_t tickIndex);
    void handlePlaybackComplete(std::uint64_t generation, int status);
    void updateWindowTitle();
    void setStatus(std::wstring message);
    [[nodiscard]] double playbackBpm() const;
    [[nodiscard]] int arpeggioIntervalMs() const;
    [[nodiscard]] std::size_t activeScoreTickCount() const;
    [[nodiscard]] std::size_t progressTickFromX(int x) const;
    void setWindowOpacityFromX(int x);
    void applyWindowOpacity();
    [[nodiscard]] RECT playPauseBounds() const;
    [[nodiscard]] RECT playFromBeginningBounds() const;
    [[nodiscard]] RECT progressBounds() const;
    [[nodiscard]] RECT opacityBounds() const;
    [[nodiscard]] RECT headerDragBounds() const;
    [[nodiscard]] RECT editModeToggleBounds() const;
    [[nodiscard]] RECT tempoPanelBounds() const;
    [[nodiscard]] RECT tempoPanelToggleBounds() const;
    [[nodiscard]] RECT tempoPanelResizeBounds() const;
    [[nodiscard]] RECT tempoPanelViewportBounds() const;
    [[nodiscard]] TempoPanelLayout tempoPanelLayout() const;
    [[nodiscard]] int tempoPanelCurrentHeight() const noexcept;
    [[nodiscard]] int maximumTempoPanelScroll() const;
    void paintTempoPanel(HDC context, const RECT& client);
    [[nodiscard]] RECT sidebarSettingsBounds() const;
    [[nodiscard]] RECT sidebarWorkspaceBounds() const;
    [[nodiscard]] RECT sidebarCompositionBounds() const;
    [[nodiscard]] RECT sidebarToggleBounds() const;
    [[nodiscard]] RECT settingsViewportBounds() const;
    [[nodiscard]] RECT settingsScrollbarBounds() const;
    [[nodiscard]] RECT settingsScrollThumbBounds() const;
    [[nodiscard]] int sidebarWidth() const noexcept;
    [[nodiscard]] int maximumSettingsScroll() const;
    [[nodiscard]] bool shouldHandleWithoutActivation(POINT clientPoint) const;
    [[nodiscard]] std::vector<ToolbarItem> toolbarItems() const;
    [[nodiscard]] std::vector<TabItem> tabItems() const;
    [[nodiscard]] std::optional<std::filesystem::path> chooseOpenPath() const;
    [[nodiscard]] std::optional<std::filesystem::path> chooseSavePath(
        const DocumentTab& document) const;
    [[nodiscard]] std::optional<std::filesystem::path> chooseExportPath(
        const DocumentTab& document) const;
    [[nodiscard]] std::string serializedDocument(const DocumentTab& document) const;
    [[nodiscard]] static std::filesystem::path systemSettingsPath();

    HWND window_{nullptr};
    HINSTANCE instance_{nullptr};
    HWND bpmEdit_{nullptr};
    HWND bpmCorrectionCombo_{nullptr};
    HWND arpeggioIntervalEdit_{nullptr};
    HWND outputBackendCombo_{nullptr};
    HWND defaultBpmEdit_{nullptr};
    HWND defaultBpmCorrectionCombo_{nullptr};
    HWND defaultArpeggioIntervalEdit_{nullptr};
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
    bool playbackSelectionRestorePending_{false};
    std::size_t playbackSelectionDocument_{0};
    std::size_t playbackSelectionAnchor_{0};
    std::size_t playbackSelectionCaret_{0};
    ui::ScoreEditor::Hand playbackSelectionHand_{ui::ScoreEditor::Hand::Right};
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
    bool draggingTab_{false};
    std::size_t draggedTabIndex_{0};
    std::size_t tabDragTargetIndex_{0};
    int tabDragGrabOffsetX_{0};
    float tabDragTargetOffset_{0.0F};
    std::vector<float> tabVisualOffsets_;
    bool tabAnimating_{false};
    Page activePage_{Page::Workspace};
    OutputBackend outputBackend_{OutputBackend::WindowsApi};
    SystemSettings systemSettings_{};
    bool insertMode_{false};
    bool sidebarExpanded_{false};
    int settingsScrollOffset_{0};
    bool draggingSettingsScroll_{false};
    bool tempoPanelExpanded_{true};
    int tempoPanelHeight_{120};
    int tempoPanelScrollOffset_{0};
    bool draggingTempoPanelResize_{false};
    int tempoPanelResizeStartY_{0};
    int tempoPanelResizeStartHeight_{0};
    bool updatingBpmEdit_{false};
    bool updatingTempoControls_{false};
    bool updatingSystemSettingsControls_{false};
};

}  // namespace yuanqin::app
