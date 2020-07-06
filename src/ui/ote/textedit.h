#ifndef TEXTEDITOR_H
#define TEXTEDITOR_H

#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTimer>

#include <vector>

#include "Highlighter/theme.h"
#include "Highlighter/definition.h"
#include <QSettings>

namespace ote {

class TextEditGutter;
class SyntaxHighlighter;
class Repository;
class EditorLabel;

/**
 * Config
 * Contains all sorts of configuration settings for TextEdit and can be passed to its constructor
 * for easy initialization.
 *
 * If you add a new config option that needs extra work to function, make sure to add this in
 * TextEdit's constructor.
 */
struct Config {
    // Text options
    bool showEndOfLineMarkers   = false;
    bool showLinebreaks         = false;
    bool useSmartIndent         = false;
    bool convertTabToSpaces     = false;
    bool wordWrap               = false;

    int tabWidth                = 4;

    int zoomLevel               = 0;

    bool enableLineHighlight    = true;
    bool enableTextDragging     = false;
    int cursorFlashTime         = -1;     // 0==off, -1==default

    // Sidebar
    bool showBookmarkStrip  = true;
    bool showNumberStrip    = true;
    bool showFoldingStrip   = true;
    bool showEditStrip      = true;

    QFont font              = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    // Writes this config to the given QSettings object.
    void writeToSettings(QSettings& settings) const;
    // Creates a new Config from the data inside the given QSettings object.
    static Config readFromSettings(const QSettings& settings);
};

class TextEdit : public QPlainTextEdit
{
    Q_OBJECT

public:
    TextEdit(QWidget* parent, Config cfg = Config());

    /**
     * Repository
     * The repository contains all loaded themes and syntax definitions. It needs to be initialized
     * beofre constructing and using TextEdit objects. You should call initRepository before doing
     * anything else with OTE.
     */
    static Repository& getRepository() {
        return *s_repository;
    }
    static void initRepository(const QString& path);

    const Config& getConfig() const { return m_config; }

    void setTheme(const Theme& theme);
    Theme getTheme() const;

    void setDefinition(const Definition& d);
    Definition getDefinition() const;

    SyntaxHighlighter* getHighlighter() { return m_highlighter; }
    const SyntaxHighlighter* getHighlighter() const { return m_highlighter; }

    /**
     * Text Options
     */
    // Creates a small symbol at the end of each line.
    void setEndOfLineMarkersVisible(bool enable);
    // Makes tabs and spaces visible.
    void setWhitespaceVisible(bool show);
    // Creates a small symbol at places where a text line is broken up because of word wrap.
    void setShowLinebreaks(bool show);
    // Will intelligently insert indents when pressing Return/Backspace
    void setSmartIndent(bool enable);
    // If set, pressing the tab key will insert the necessary amount of spaces instead.
    void setTabToSpaces(bool enable);

    // Sets the time interval of cursor flashes.
    void setCursorFlashTime(int msecs);

    // Automatically break lines when they reach the right border of the text field
    void setWordWrap(bool enable);

    // Sets the size of a tabstop in number of spaces. Default is 4.
    void setTabWidth(int width);
    
    /**
     * Font Settings
     * Use getConfig().font instead of font() to get the current font.
     * font() will return the "real" font including text zoom, etc.
     */
    void setFont(QFont font);

    /**
     * Cursor Operations
     * Even when multiple text cursors are active in the document, there is
     * always one main cursor. The following functions always only act upon
     * that main cursor unless specified otherwise.
     */

    using CursorPos = int; // Cursor position is relative to the start of the document

    // Sets the current main cursor to position. Line and Column start at 0.
    void setCursorPosition(const CursorPos& pos);
    void setCursorPosition(int line, int column);
    CursorPos getCursorPosition() const;

    // Returns a pair <line,column> for the CursorPos p. Both line and column start at 0.
    std::pair<int,int> getLineColumnForCursorPos(const CursorPos& p) const;
    CursorPos getCursorPosForLineColumn(int line, int column) const;

    // Returns the word under the cursor. Empty string if there is none.
    QString getCurrentWord() const;

    /**
     * Selection Operations
     * Like cursors, there can be multiple selections active in a document.
     * Unless specified otherwise, these functions operate on the selection
     * of the main cursor.
     */
    struct Selection {
        Selection() = default;
        Selection(int start, int end) : start(start), end(end) {}

        CursorPos start = -1;
        CursorPos end = -1;

        bool isValid() const { return start >= 0 && end >= start; }
        bool length() const { return end-start; }
        bool hasSelection() const { return length() > 0; }
        bool isEmpty() const { return length() == 0; }
    };

    // Returns current selection. Also for all selections.
    Selection getSelection() const;
    std::vector<Selection> getSelections() const;

    // Returns the currently selected text. Also for all selections.
    QString getSelectedText() const;
    QStringList getSelectedTexts() const;

    // Set current selection. This will remove all other cursors/selections.
    void setSelection(const Selection& sel);
    // Selects all those selections. Will remove all other cursors/selections.
    // The main cursor will be set to the last selection.
    void setSelections(const std::vector<Selection>& selections);
    // Replaces the selected text. This acts the same as a paste action.
    void setTextInSelection(const QString& text, bool keepSelection=true);
    // Replaces all selections. As a special case, if the number of selections equals
    // the number of items in the StringList, only one item will be pasted into each
    // selection. Otherwise, all list items will be pasted into every selection.
    void setTextInSelections(const QStringList& texts, bool keepSelection=true);

    /**
     * Scrolling
     */
    // Sets/Gets scroll position. The x/y of the QPoint are the values of the scrollbars.
    QPoint getScrollPosition() const;
    void setScrollPosition(const QPoint& p);

    // Finding and replacing
    // TODO: Add overloads for QRegularExpression

    struct FindSettings {
        Selection range;
        CursorPos start = -1;
        bool backwards = false;
        bool wholeWordsOnly = false;
        bool wrapAround = true;
        bool caseSensitive = false;
    };

    Selection find(const QString& term, const FindSettings& settings);
    std::vector<Selection> findAll(const QString& term, const FindSettings& settings);

    Selection findRegex(const QRegularExpression& expr, const FindSettings& settings);
    std::vector<Selection> findAllRegex(const QRegularExpression& expr, const FindSettings& settings);





    using FindFlags = QTextDocument::FindFlags;
    using FindFlag = QTextDocument::FindFlag;

    bool find(const QString& term, FindFlags flags = FindFlags());
    bool find(const QString& term, int startPos, int endPos=-1, FindFlags flags = FindFlags(), bool wrapAround=true);
    std::vector<Selection> findAll(const QString& term, int startPos=0, int endPos=-1, FindFlags flags = FindFlags());

    //bool find(const QRegExp &exp, QTextDocument::FindFlags options = QTextDocument::FindFlags()) = delete;

    // Returns whether the currently selected text is the one selected by the most recent call of find(...)
    bool isSearchTermSelected() const { return m_findTermSelected; }

    /**
     * Text Zoom
     * There is no percentage zoom. Instead, each zoom level corresponds to a +1/-1 change in
     * the font's point size. Setting the zoom level to 0 removes all zoom.
     */
    void resetZoom();
    void setZoomTo(int value);
    void zoomIn();
    void zoomOut();

    /**
     * Document History
     */
    // Gets/sets whether the contents have been modified. This also handles redo/undo properly.
    bool isModified() const;
    void setModified(bool modified);
    // Removes all remembered undo/redo actions
    void clearHistory();
    // Returns an integer that changes everytime a change is made to the document.
    // This value of volatile. Don't save it and use it in future program runs.
    int getModificationRevision() const;

    /**
     * Line Operations
     * These functions only operate on selected text lines
     */
    // Moves the text line(s) under the cursor up/down by one.
    void moveSelectedBlocksUp();
    void moveSelectedBlocksDown();
    // Pastes a copy of the selected line(s) below the current.
    void duplicateSelectedBlocks();
    // Deletes the selected line (s).
    void deleteSelectedBlocks();

    /**
     * Document Operations
     * These functions operate on the entire document
     */
    // Sets the entire document's text. Use this when loading/reloading the file's contents.
    // Try not to use it for other stuff. TextEdit assumes that the loaded file is in pristine condition
    // after setPlainText().
    void setPlainText(const QString &text, bool undo = false);
    // Finds leading whitespace comprised of tabs or spaces and converts them entirely into tabs/spaces.
    // When converting to tabs, spaces sometimes need to be used to fill gaps.
    void convertLeadingWhitespaceToTabs();
    void convertLeadingWhitespaceToSpaces();
    // Removes leading and/or trailing whitespace (both spaces and tabs) from all lines.
    void trimWhitespace(bool leading, bool trailing);
    // Returns the number of text lines (ignoring word wrapped lines)
    int getLineCount() const;
    // Returns the number of individual characters in the document.
    int getCharCount() const;

    /**
     * Folding Operations
     * Folding is the act of hiding larger text passages behind a single text line.
     * The Syntax Definition handles folding regions, but generally multi-line comments
     * and function bodies can be folded.
     */
    // Returns whether the specific block is the start of a folding region.
    bool isFoldingRegion(const QTextBlock& block) const;
    // Returns whether a specific region is currently folded.
    bool isRegionFolded(const QTextBlock& block) const;
    // folds/unfolds a specific region if possible.
    void toggleRegionFold(QTextBlock startBlock);

    bool isBookmarked(const QTextBlock& block) const;
    bool isBookmarked(CursorPos pos) const;
    void setBookmark(const QTextBlock block, bool bookmarked);
    void setBookmark(CursorPos pos, bool bookmarked);
    void toggleBookmark(const QTextBlock& block);

    /**
     * EditorLabels
     * These are image labels that can be anchored to a text position, similar to
     * a QTextCursor. These labels stay until the text it's anchored to is removed
     * or the label is explicitely deleted. If you want to keep a reference to an
     * EditorLabel, only do so using WeakEditorLabelPtr! A regular pointer can end
     * up pointing to a deleted EditorLabel if the TextEdit has to delete it.
     */
    typedef std::shared_ptr<EditorLabel> EditorLabelPtr;
    typedef std::weak_ptr<EditorLabel> WeakEditorLabelPtr;
    typedef std::vector<EditorLabelPtr>::iterator EditorLabelIterator;
    typedef std::pair<EditorLabelIterator, EditorLabelIterator> EditorLabelRange;

    // Returns all editor labels between (begin,end) or the given block.
    EditorLabelRange getEditorLabelsInRange(int begin, int end);
    EditorLabelRange getEditorLabelsInBlock(const QTextBlock& block);

    // Removes all labels that are marked for deletion. This is move efficient than
    // removing them one by one.
    void deleteMarkedEditorLabelsInRange(const EditorLabelRange& range);

    // Returns the label at a given position, or a nullptr if there is none.
    WeakEditorLabelPtr getEditorLabelAtPos(int pos);
    // Removes a single EditorLabel, if it still exists.
    void removeEditorLabel(WeakEditorLabelPtr label);
    // Adds a new editor label.
    WeakEditorLabelPtr addEditorLabel(EditorLabelPtr label);

signals:
    // Emitted after a text line's content changed. This happens *after* highlighting has happened.
    void blockHighlighted(const QTextBlock& block);
    // Emitted when the mouse wheel is used while on this TextEdit
    void mouseWheelUsed(QWheelEvent *ev);
    // Emitted when this object gains focus
    void gotFocus();
    // Emitted when a drop event occurs
    void urlsDropped(const QList<QUrl>& urls);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent* event) override;
    void dropEvent(QDropEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *) override;
    void focusInEvent(QFocusEvent* event) override;
    void paintEvent(QPaintEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void leaveEvent(QEvent* evt) override;
    
private:
    friend class TextEditGutter;
    friend class PluginBase;

    /**
     * Multi-cursor Operations
     */
    // Handles cursor and selection changes for more than one cursor at once.
    bool mcsMoveOperation(QKeyEvent *evt);
    // Inserts text into all cursors. This is used for single-character input as well. Look
    // below for a paste operation.
    void mcsInsertText(const QString& text);
    // Create a new (non-main) cursor. Only use this function. don't just add to m_cursors!
    bool mcsAddCursor(const QTextCursor& c);
    // Removes cursors at the same position. Should be called after any operation that can
    // move cursors.
    void mcsEnsureUniqueCursors();
    // Recreates the highlight formats for all cursors. Call this after any operation that
    // changes cursor selections.
    void mcsUpdateSelectionHighlights();
    // Removes all cursors, leaving only the main cursor. Most of the same we want an immediate
    // viewport update to reflect these changes.
    void mcsClearAllCursors(bool updateViewport=true);
    // Paste operation. If the number of cursors equal the number of lines in text/list
    // only one line is pasted into each cursor.
    void mcsPaste(const QStringList& list);
    void mcsPaste(const QString& text);

    // Slots for handling signals
    void onCursorPositionChanged();
    void onSelectionChanged();
    // Is called right before the contents actually change.
    void onContentsChange(int position, int removed, int added);
    // Handles cursor blinking
    void onCursorRepaint();
    // The normal keyPressEvent when we don't have multiple cursors active.
    void singleCursorKeyPressEvent(QKeyEvent* e);
    
    /**
      * Other stuff
      */

    // Returns the QTextCursor of the selection under the position p
    // Invalid cursor if it's not under a selection.
    QTextCursor getSelectionUnderPoint(QPoint p) const;

    // Returns the text block at a specific y-coordinate. I think this is in
    // viewport coordinate space. Don't ask me. I don't remember.
    QTextBlock blockAtPosition(int y) const;

    void updateSidebarGeometry();
    void updateSidebarArea(const QRect& rect, int dy);

    // Used when all EditorLabels need to be redrawn, e.g. when font size or
    // editor geometry changes.
    void redrawAllEditorLabels();
    // Adds a highlight format to highlight the entire line.
    void highlightCurrentLine();
    // Makes sure a text block is not folded. This is useful for search functions to make sure
    // your search result is visible.
    void ensureSelectionUnfolded(const Selection& sel);
    // Returns the text block that marks the end of a folding region. Invalid if none found.
    QTextBlock findEndOfFoldingRegion(const QTextBlock& startBlock) const;
    // Returns the text block that marks the beginning of the folding region. Invalid if none found.
    QTextBlock findBeginOfFoldingRegion(const QTextBlock& startBlock) const;
    // Returns whether the text block is folded.
    bool isBlockFolded(const QTextBlock& block) const;
    // Returns [column,row] under 'point'.
    QPoint getGridPointAt(const QPoint& point);
    // Takes a text block and returns a pair of positions.
    // The substring formed by [begin,end] is the part of the block's text that is between
    // the visual columns [beginColumn,endColumn].
    std::pair<int, int> getVisualSelection(const QTextBlock& block, const int beginColumn, const int endColumn);
#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    // Qt 5.9 and lower don't offer setTabStopDistance yet. This is taken straight from the Qt
    // source code, with only minimal changes to make it work in 5.4+.
    void setTabStopDistance(qreal distance);
#endif

    /**
     * Extra Selections
     * Text highlighting works via ExtraSelections that can modify text fore/background.
     * These selections are sorted based on priority. LineHighlights have lowest priority,
     * plugin-added highlights have highest.
     * If you want to add your own highlights outside of TextEdit, subclass PluginBase.
     */
    enum ESType {
        ESLineHighlight = 0,
        ESSameItems = 1,
        ESCursorSelection = 2,
        ESPluginStart = 3
    };
    using ExtraSelectionList = QList<QTextEdit::ExtraSelection>;
    using ExtraSelectionMap = QMap<int /*ESType*/, ExtraSelectionList>;

    const ExtraSelectionList* getExtraSelections(int /*ESType*/ type) const;
    void setExtraSelections(int /*ESType*/ type, ExtraSelectionList list);


    /**
     * Painting Stuff
     * The following functions are used inside paintEvent.
     */

    struct BlockData {
        QTextBlock block;
        QRect translatedRect;
    };
    using BlockList = std::vector<BlockData>;

    // Returns all blocks visible in viewport/rect, includes their translated positions.
    BlockList getBlocksInViewport() const;
    BlockList getBlocksInRect(QRect rect) const;

    void paintLineBreaks(QPainter& painter, const BlockList& blockList) const;
    void paintSearchBlock(QPainter& painter, const QRect& eventRect, const QTextBlock& block);
    void paintLineSuffixes(QPainter& painter, const BlockList& blockList) const;
    void compositeExtraSelections();

    /**
     * Member variables
     */

    // Contains all configuration settings
    Config m_config;

    // Whether any extraSelection lists were changed. If so, they get composited during next paint
    bool m_extraSelectionsModified = false;
    // List of all active cursors, including main cursor.
    std::vector<QTextCursor> m_cursors;
    // Timer that controls cursor blinking
    QTimer m_cursorTimer;
    // True if flashing cursors should be drawn at the moment.
    bool m_drawCursorsOn = true;
    // Contains the QTextDocument revision that was current when this document was last set to unmodified.
    int m_lastSavedRevision = 0;
    // Contains the QTextDocument revision that was current when this document was last loaded/reloaded.
    int m_initialRevision = 0;

    enum class McsTriggerState {
        NoTrigger,  // Trigger not pressed
        Click,      // Trigger pressed
        Drag        // Mouse drag while trigger pressed
    } m_mcsTriggerState = McsTriggerState::NoTrigger;

    struct {
        QPoint pos, anchor;
        int left() const { return std::min(pos.x(), anchor.x()); }
        int right() const { return std::max(pos.x(), anchor.x()); }
        int top() const { return std::min(pos.y(), anchor.y()); }
        int bottom() const { return std::max(pos.y(), anchor.y()); }
        int width() const { return right() - left(); }
        int height() const { return bottom() - top(); }
    } mcsBlock;

    enum class DragState {
        NoDrag,     // No drag
        Begin,      // Left button pressed but no movement yet
        Ongoing     // Left button pressed and movement
    } m_dragState = DragState::NoDrag;
    QPoint m_dragOrigin;
    QTextCursor m_dragCursor;


    // Variables for search/replace.
    bool m_findActive = false;
    bool m_findTermSelected = false;
    QString m_findTerm;
    QPair<int,int> m_findRange;
    QTextDocument::FindFlags m_findFlags;

    int m_fontSize = 0; // Will be set by setFont(...) in constructor call

    ExtraSelectionMap m_extraSelections;
    TextEditGutter* m_sideBar;
    SyntaxHighlighter* m_highlighter;
    std::vector<EditorLabelPtr> m_editorLabels;
    static Repository* s_repository;
};

} // namespace ote

#endif // TEXTEDITOR_H
