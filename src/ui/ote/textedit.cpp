#include "textedit.h"

#include "Highlighter/repository.h"
#include "Highlighter/syntaxhighlighter.h"
#include "editorlabel.h"
#include "texteditgutter.h"

#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QFontDatabase>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QRegularExpression>
#include <QScrollBar>
#include <QTextBlock>

#include <algorithm>
#include <cmath>

namespace ote {

#define WRITE_KEY(name, var) if(defaultConfig.var != var) settings.setValue("OTE/" name, QVariant::fromValue(var))
#define READ_KEY(type, name, var) cfg.var = settings.value("OTE/" name, cfg.var).value<type>();

void Config::writeToSettings(QSettings& settings) const
{
    Config defaultConfig;

    WRITE_KEY("showEndOfLineMarkers", showEndOfLineMarkers);
    WRITE_KEY("showLinebreaks", showLinebreaks);
    WRITE_KEY("useSmartIndent", useSmartIndent);
    WRITE_KEY("convertTabToSpaces", convertTabToSpaces);
    WRITE_KEY("wordWrap", wordWrap);
    WRITE_KEY("tabWidth", tabWidth);
    WRITE_KEY("zoomLevel", zoomLevel);
    WRITE_KEY("enableLineHighlight", enableLineHighlight);
    WRITE_KEY("cursorFlashTime", cursorFlashTime);
    WRITE_KEY("showBookmarkStrip", showBookmarkStrip);
    WRITE_KEY("showNumberStrip", showNumberStrip);
    WRITE_KEY("showFoldingStrip", showFoldingStrip);
    WRITE_KEY("showEditStrip", showEditStrip);
    WRITE_KEY("font", font);
}

Config Config::readFromSettings(const QSettings& settings)
{
    Config cfg;
    READ_KEY(bool, "showEndOfLineMarkers", showEndOfLineMarkers);
    READ_KEY(bool, "showLinebreaks", showLinebreaks);
    READ_KEY(bool, "useSmartIndent", useSmartIndent);
    READ_KEY(bool, "convertTabToSpaces", convertTabToSpaces);
    READ_KEY(bool, "wordWrap", wordWrap);
    READ_KEY(int, "tabWidth", tabWidth);
    READ_KEY(int, "zoomLevel", zoomLevel);
    READ_KEY(bool, "enableLineHighlight", enableLineHighlight);
    READ_KEY(int, "cursorFlashTime", cursorFlashTime);
    READ_KEY(bool, "showBookmarkStrip", showBookmarkStrip);
    READ_KEY(bool, "showNumberStrip", showNumberStrip);
    READ_KEY(bool, "showFoldingStrip", showFoldingStrip);
    READ_KEY(bool, "showEditStrip", showEditStrip);
    READ_KEY(QFont, "font", font);

    return cfg;
}


Repository* TextEdit::s_repository = nullptr;

TextEdit::TextEdit(QWidget* parent, Config cfg)
    : QPlainTextEdit(parent)
    , m_sideBar(new TextEditGutter(this,cfg))
    , m_highlighter(new SyntaxHighlighter(this))
{
    connect(&m_cursorTimer, &QTimer::timeout, this, &TextEdit::onCursorRepaint);
    connect(this, &QPlainTextEdit::blockCountChanged, this, &TextEdit::updateSidebarGeometry);
    connect(this, &QPlainTextEdit::updateRequest, this, &TextEdit::updateSidebarArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &TextEdit::onCursorPositionChanged); // slot
    connect(this, &QPlainTextEdit::selectionChanged, this, &TextEdit::onSelectionChanged);
    connect(document(), &QTextDocument::contentsChange, this, &TextEdit::onContentsChange);
    connect(m_highlighter, &ote::SyntaxHighlighter::blockHighlighted, this, &TextEdit::blockHighlighted);

    m_highlighter->setDocument(document()); // Important to set this *after* the blockChanged connect

    // Some config options need some extra work. We'll set them manually.
    setWordWrap(cfg.wordWrap);
    setZoomTo(cfg.zoomLevel);
    setCursorFlashTime(cfg.cursorFlashTime);
    setFont(cfg.font);
    m_config = cfg;


    setCenterOnScroll(false);

    updateSidebarGeometry();
    onCursorPositionChanged();
}

void TextEdit::setTheme(const Theme& theme)
{
    if (theme == getTheme())
        return;

    auto pal = qApp->palette();
    if (theme.isValid()) {
        pal.setColor(QPalette::Base, theme.editorColor(Theme::BackgroundColor));
        pal.setColor(QPalette::Text, theme.textColor(Theme::Normal));
        pal.setColor(QPalette::Highlight, theme.editorColor(Theme::CurrentLine));
    }
    setPalette(pal);
    viewport()->setPalette(pal);

    m_highlighter->setTheme(theme);
    m_highlighter->startRehighlighting();
    m_sideBar->setTheme(theme);

    onCursorPositionChanged();
    onSelectionChanged();

    redrawAllEditorLabels();
}

Theme TextEdit::getTheme() const
{
    return m_highlighter->theme();
}

void TextEdit::highlightCurrentLine()
{
    if (!m_config.enableLineHighlight)
        return;

    QTextEdit::ExtraSelection selection;
    selection.format.setBackground(QBrush(getTheme().editorColor(Theme::CurrentLine)));
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    selection.cursor = textCursor();
    selection.cursor.clearSelection();

    setExtraSelections(ESLineHighlight, ExtraSelectionList() << selection);
}

void TextEdit::initRepository(const QString& path)
{
    delete s_repository;
    s_repository = new Repository(path);
}

void TextEdit::setDefinition(const Definition& d)
{
    m_highlighter->setDefinition(d);
}

Definition TextEdit::getDefinition() const
{
    return m_highlighter->definition();
}

void TextEdit::setEndOfLineMarkersVisible(bool enable)
{
    if (enable == m_config.showEndOfLineMarkers)
        return;

    m_config.showEndOfLineMarkers = enable;
    viewport()->repaint();
}

void TextEdit::setWhitespaceVisible(bool show)
{
    auto opts = document()->defaultTextOption();
    auto flags = opts.flags();

    if (show)
        opts.setFlags(flags | QTextOption::ShowTabsAndSpaces);
    else
        opts.setFlags(flags & (~QTextOption::ShowTabsAndSpaces));

    document()->setDefaultTextOption(opts);
}

void TextEdit::setShowLinebreaks(bool show)
{
    if (show == m_config.showLinebreaks)
        return;

    m_config.showLinebreaks = show;
    update();
}

void TextEdit::setSmartIndent(bool enable)
{
    m_config.useSmartIndent = enable;
}

void TextEdit::setTabToSpaces(bool enable)
{
    m_config.convertTabToSpaces = enable;
}

void TextEdit::setWordWrap(bool enable)
{
    if (enable == m_config.wordWrap)
        return;

    m_config.wordWrap = enable;
    enable ? setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere) : setWordWrapMode(QTextOption::NoWrap);
}

void TextEdit::setTabWidth(int width)
{
    if (width < 0 || width == m_config.tabWidth)
        return;

    m_config.tabWidth = width;
    setFont(m_config.font);
}

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
void TextEdit::setTabStopDistance(qreal distance)
{
    QTextOption opt = document()->defaultTextOption();
    if (opt.tabStop() == distance || distance < 0)
        return;
    opt.setTabStop(distance);
    document()->setDefaultTextOption(opt);
}
#endif

void TextEdit::setFont(QFont font)
{
    if (!QFontInfo(font).fixedPitch()) {
        qDebug() << "Selected font is not monospace: " << font.family() << font.style();
    }

    m_config.font = font;

    m_fontSize = font.pointSize();
    font.setPointSize(font.pointSize() + m_config.zoomLevel);

    // Calculating letter width using QFrontMetrics isn't 100% accurate. Small inaccuracies
    // can accumulate over time. Instead, we can calculate a good letter spacing value and
    // make the font use it.
    // https://stackoverflow.com/a/42071875/1038629
    QFontMetricsF fm(font);
#if QT_VERSION < QT_VERSION_CHECK(5, 11, 0)
    auto stopWidth = m_config.tabWidth * fm.width(' ');
#else
    auto stopWidth = m_config.tabWidth * fm.horizontalAdvance(' ');
#endif
    auto letterSpacing = (ceil(stopWidth) - stopWidth) / m_config.tabWidth;

    font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    QPlainTextEdit::setFont(font);
    setTabStopDistance(ceil(stopWidth));

    redrawAllEditorLabels();
}

void TextEdit::setCursorFlashTime(int msecs)
{
    if (msecs==-1)
        msecs = QApplication::cursorFlashTime();
    if (msecs == 0) {
        m_cursorTimer.stop();
    } else
        m_cursorTimer.start(msecs / 2);

    QApplication::setCursorFlashTime(msecs);
}

QString TextEdit::getCurrentWord() const
{
    auto c = textCursor();
    c.select(QTextCursor::WordUnderCursor);
    return c.selectedText();
}

int TextEdit::getLineCount() const
{
    return blockCount();
}

int TextEdit::getCharCount() const
{
    return document()->characterCount();
}

std::pair<int, int> TextEdit::getLineColumnForCursorPos(const TextEdit::CursorPos& p) const
{
    QTextCursor c(document());
    c.setPosition(p);
    return {c.blockNumber(), c.positionInBlock()};
}

ote::TextEdit::CursorPos ote::TextEdit::getCursorPosForLineColumn(int line, int column) const
{
    const auto& block = document()->findBlockByNumber(line);
    return block.position() + std::min(column, block.length());
}

void TextEdit::setCursorPosition(int line, int column)
{
    const auto& block = document()->findBlockByNumber(line);
    const auto col = std::max(0, std::min(column, block.length() - 1));
    setCursorPosition(block.position() + col);
}

void TextEdit::setCursorPosition(const TextEdit::CursorPos& pos)
{
    auto c = textCursor();
    c.setPosition(pos);

    if (isRegionFolded(c.block()))
        toggleRegionFold(c.block());

    setTextCursor(c);
}

TextEdit::CursorPos TextEdit::getCursorPosition() const
{
    return textCursor().position();
}

QStringList TextEdit::getSelectedTexts() const
{
    QStringList texts;
    for (const auto& c : m_cursors)
        if (c.hasSelection())
            texts << c.selectedText();

    return texts;
}

QString TextEdit::getSelectedText() const
{
    return textCursor().selectedText();
}

TextEdit::Selection TextEdit::getSelection() const
{
    auto c = textCursor();
    return {c.selectionStart(), c.selectionEnd()};
}

std::vector<TextEdit::Selection> TextEdit::getSelections() const
{
    std::vector<Selection> sels;
    for (const auto& c : m_cursors)
        if (c.hasSelection()) {
            sels.emplace_back(c.selectionStart(), c.selectionEnd());
        }

    return sels;
}

void TextEdit::setSelection(const TextEdit::Selection& sel)
{
    bool mcsEnabled = m_cursors.size() > 1;

    if (mcsEnabled) {
        mcsClearAllCursors();
    }

    ensureSelectionUnfolded(sel);

    auto cur = QTextCursor(document());
    cur.setPosition(sel.start);
    cur.setPosition(sel.end, QTextCursor::KeepAnchor);
    setTextCursor(cur);
}

void TextEdit::setSelections(const std::vector<TextEdit::Selection>& selections)
{
    if (selections.empty())
        return;

    m_cursors.clear();
    for (const auto& sel : selections) {
        ensureSelectionUnfolded(sel);

        QTextCursor c(document());
        c.setPosition(sel.start);
        c.setPosition(sel.end, QTextCursor::KeepAnchor);
        mcsAddCursor(c);
    }

    mcsUpdateSelectionHighlights();
    setTextCursor(m_cursors.back());
}

void TextEdit::setTextInSelection(const QString& text, bool keepSelection)
{
    auto c = textCursor();

    if (keepSelection)
        c.setKeepPositionOnInsert(true);

    c.insertText(text);
}

void TextEdit::setTextInSelections(const QStringList& texts, bool keepSelection)
{
    // TODO: unused variable
    Q_UNUSED(keepSelection);
    mcsPaste(texts);
}

QPoint TextEdit::getScrollPosition() const
{
    return {horizontalScrollBar()->sliderPosition(), verticalScrollBar()->sliderPosition()};
}

void TextEdit::setScrollPosition(const QPoint& p)
{
    horizontalScrollBar()->setSliderPosition(p.x());
    verticalScrollBar()->setSliderPosition(p.y());
}

bool TextEdit::find(const QString& term, TextEdit::FindFlags flags)
{
    return find(term, 0, -1, flags);
}

bool TextEdit::find(const QString& term, int regionStart, int regionEnd, TextEdit::FindFlags flags, bool wrapAround)
{
    auto curr = textCursor();
    if (regionEnd < 0)
        regionEnd = document()->characterCount() - 1;

    if (curr.position() < regionStart)
        curr.setPosition(regionStart);
    else if (curr.position() > regionEnd)
        curr.setPosition(regionEnd);

    const bool fwd = !flags.testFlag(QTextDocument::FindBackward);
    int pos = curr.position();

    if (flags & QTextDocument::FindBackward)
        pos = curr.selectionStart() - 1;
    else
        pos = curr.selectionEnd();

    auto c = document()->find(QRegularExpression(term), pos, flags);

    // If c moved past the search range or failed to find a match we may want to loop around and retry.
    if (wrapAround) {
        if ((fwd && c.isNull()) || c.selectionEnd() > regionEnd)
            c = document()->find(term, regionStart, flags);
        else if ((!fwd && c.isNull()) || c.selectionStart() < regionStart)
            c = document()->find(term, regionEnd, flags);
    }

    if (!c.isNull() && c.selectionEnd() <= regionEnd && c.selectionStart() >= regionStart) {
        setSelection({c.selectionStart(), c.selectionEnd()});
        m_findTermSelected = true;
        return true;
    }

    return false;
}

std::vector<TextEdit::Selection> TextEdit::findAll(
    const QString& term, int startPos, int endPos, TextEdit::FindFlags flags)
{
    std::vector<Selection> selections;

    endPos = endPos == -1 ? document()->characterCount() - 1 : endPos;

    setCursorPosition(startPos);
    while (find(term, startPos, -1, flags, false)) {
        startPos = getCursorPosition();
        selections.push_back(getSelection());
    }
    return selections;
}

void TextEdit::resetZoom()
{
    setZoomTo(0);
}

void TextEdit::setZoomTo(int value)
{
    // Clamp maximum font size to [4,40]. That should be sensible.
    const int MIN_FONT_SIZE = 4;
    const int MAX_FONT_SIZE = 40;

    if (m_fontSize + value < MIN_FONT_SIZE)
        value = MIN_FONT_SIZE - m_fontSize;
    else if (m_fontSize + value > MAX_FONT_SIZE)
        value = MAX_FONT_SIZE - m_fontSize;

    m_config.zoomLevel = value;

    QFont f = font();
    f.setPointSize(m_fontSize + m_config.zoomLevel);
    QPlainTextEdit::setFont(f);

    updateSidebarGeometry();
    redrawAllEditorLabels();
}

void TextEdit::zoomIn()
{
    setZoomTo(m_config.zoomLevel + 1);
}

void TextEdit::zoomOut()
{
    setZoomTo(m_config.zoomLevel - 1);
}

void TextEdit::clearHistory()
{
    document()->clearUndoRedoStacks();
}

int TextEdit::getModificationRevision() const
{
    return document()->revision();
}

bool TextEdit::isModified() const
{
    return document()->isModified();
}

void TextEdit::setModified(bool modified)
{
    // If modification is set to false we can assume the document was saved.
    // In this case, store the current revision so we know what lines are changed in the future.
    if (!modified)
        m_lastSavedRevision = document()->revision();
    document()->setModified(modified);
}

// The logic for block up- and down movement is inspired by Tsu Jan's code used in FeatherPad.
// https://github.com/tsujan/FeatherPad/blob/master/featherpad/textedit.cpp#L737
void TextEdit::moveSelectedBlocksUp()
{
    if (isReadOnly()) return;

    QTextCursor cursor = textCursor();
    const int anch = cursor.anchor();
    const int pos = cursor.position();

    QTextCursor tmp = cursor;
    tmp.setPosition(anch);
    const int anchorInBlock = tmp.positionInBlock();
    tmp.setPosition(pos);
    const int posInBlock = tmp.positionInBlock();

    // Select all currently selected blocks
    cursor.setPosition(std::max(anch, pos));
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.setPosition(std::min(anch, pos), QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);

    const QString txt = cursor.selectedText();

    // Try to include the previous line in the selection. If it fails, there is none.
    if (!cursor.movePosition(QTextCursor::PreviousBlock, QTextCursor::KeepAnchor))
        return;

    if (isBlockFolded(cursor.block()))
        toggleRegionFold(findBeginOfFoldingRegion(cursor.block()));

    // If it succeeded, make sure only the '\n' at the end is selected, remove text, move to
    // beginning of line, insert text, insert newline.
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.beginEditBlock();
    cursor.removeSelectedText();
    cursor.movePosition(QTextCursor::StartOfBlock);
    const int firstLine = cursor.position();
    cursor.insertText(txt);
    cursor.insertBlock();
    cursor.endEditBlock();
    cursor.movePosition (QTextCursor::PreviousBlock);
    // Restore old selection
    if (anch >= pos) {
        cursor.setPosition(cursor.block().position() + anchorInBlock);
        cursor.setPosition(firstLine + posInBlock, QTextCursor::KeepAnchor);
    } else {
        const int lastLine = cursor.position();
        cursor.setPosition(firstLine + anchorInBlock);
        cursor.setPosition(lastLine + posInBlock, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
    ensureCursorVisible();
}

void TextEdit::moveSelectedBlocksDown()
{
    if (isReadOnly()) return;

    QTextCursor cursor = textCursor();
    const int anch = cursor.anchor();
    const int pos = cursor.position();

    QTextCursor tmp = cursor;
    tmp.setPosition (anch);
    const int anchorInBlock = tmp.positionInBlock();
    tmp.setPosition (pos);
    const int posInBlock = tmp.positionInBlock();

    cursor.setPosition(std::min(anch, pos));
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.setPosition(std::max(anch, pos), QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    const QString txt = cursor.selectedText();
    if (!cursor.movePosition (QTextCursor::NextBlock, QTextCursor::KeepAnchor))
        return;

    if (isFoldingRegion(cursor.block()))
        toggleRegionFold(cursor.block());

    cursor.beginEditBlock();
    cursor.deleteChar();
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertText(QString (QChar::ParagraphSeparator));
    const int firstLine = cursor.position();
    cursor.insertText(txt);
    cursor.endEditBlock();
    if (anch >= pos) {
        cursor.setPosition(cursor.block().position() + anchorInBlock);
        cursor.setPosition(firstLine + posInBlock, QTextCursor::KeepAnchor);
    } else {
        cursor.movePosition(QTextCursor::StartOfBlock);
        const int lastLine = cursor.position();
        cursor.setPosition(firstLine + anchorInBlock);
        cursor.setPosition(lastLine + posInBlock, QTextCursor::KeepAnchor);
    }

    setTextCursor(cursor);
    ensureCursorVisible();
}

void TextEdit::duplicateSelectedBlocks()
{
    if (isReadOnly()) return;

    auto c = textCursor();
    auto blockCursor = c;

    blockCursor.setPosition(c.selectionStart());
    blockCursor.movePosition(QTextCursor::StartOfBlock);
    blockCursor.setPosition(c.selectionEnd(), QTextCursor::KeepAnchor);
    auto success = blockCursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);

    // The previous call fails when we're at the end of the document. In that case we insert a new
    // Block and remove it later.
    if (!success) {
        auto v = blockCursor;
        v.movePosition(QTextCursor::EndOfBlock, QTextCursor::MoveAnchor);
        v.insertBlock();
        blockCursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    }

    c.beginEditBlock();
    auto text = blockCursor.selectedText();

    c = textCursor();
    c.setPosition(c.selectionStart());
    c.movePosition(QTextCursor::StartOfBlock);
    c.insertText(text);

    if (!success)
        c.deletePreviousChar();

    c.endEditBlock();
}

void TextEdit::deleteSelectedBlocks()
{
    if (isReadOnly()) return;

    auto c = textCursor();
    auto ce = c;

    ce.beginEditBlock();
    ce.setPosition(c.selectionStart());
    ce.movePosition(QTextCursor::StartOfBlock);
    ce.setPosition(c.selectionEnd(), QTextCursor::KeepAnchor);
    auto success = ce.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
    if (!success)
        ce.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);

    ce.removeSelectedText();
    if (!success)
        ce.deletePreviousChar();
    ce.endEditBlock();
}

void TextEdit::setPlainText(const QString &text, bool undo)
{
    // setPlainText triggers a lot of QTextDocument::contentsChange, each of which will trigger the
    // SyntaxHighlighter. That means the entire document will be highlighted immediately, causing quite some
    // delays initially.
    if (undo) {
        QString oldValue = this->toPlainText();
        auto cc = textCursor();
        cc.beginEditBlock();
        cc.setPosition(0);
        cc.setPosition(oldValue.length(), QTextCursor::KeepAnchor);
        cc.removeSelectedText();
        cc.insertText(text);
        cc.endEditBlock();
    } else {
        m_highlighter->setEnabled(false);
        QPlainTextEdit::setPlainText(text);
        m_highlighter->setEnabled(true);

        m_initialRevision = document()->revision();
        m_lastSavedRevision = m_initialRevision;
    }

}

// pair.first = number of ws characters found, pair.second = number of spaces needed
QPair<int, int> getLeadingWSLength(const QStringRef& ref, int tabWidth)
{
    auto ws = 0;

    for (int i = 0; i < ref.size(); ++i) {
        switch (ref.at(i).toLatin1()) {
        case ' ':
            ws += 1;
            break;
        case '\t': {
            const auto distToNextCol = tabWidth - (ws % tabWidth);
            ws += distToNextCol;
            break;
        }
        default:
            return {i, ws};
        }
    }

    return {ref.size(), ws};
}

void TextEdit::convertLeadingWhitespaceToTabs()
{
    if (isReadOnly()) return;

    // TODO: Might be more efficient using a block-based approach?
    QString plaintext = toPlainText();
    auto lines = plaintext.splitRef('\n');
    QString final;
    final.reserve(plaintext.length());

    for (auto& line : lines) {
        auto pair = getLeadingWSLength(line, m_config.tabWidth);
        auto idx = pair.first;
        auto ws = pair.second;

        final += QString(ws / m_config.tabWidth, '\t') + QString(ws % m_config.tabWidth, ' ') ;
        final += line.mid(idx);
        final += '\n';
    }

    if (!final.isEmpty())
        final.chop(1); // Remove last '\n' since it creates an extra line at the end

    auto c = textCursor();
    auto p = getCursorPosition();
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(final);
    setCursorPosition(p);
    c.endEditBlock();
}

void TextEdit::convertLeadingWhitespaceToSpaces()
{
    if (isReadOnly()) return;

    // TODO: Might be more efficient using a block-based approach?
    QString plaintext = toPlainText();
    auto lines = plaintext.splitRef('\n');
    QString final;
    final.reserve(plaintext.length());

    for (auto& line : lines) {
        auto pair = getLeadingWSLength(line, m_config.tabWidth);
        auto idx = pair.first;
        auto ws = pair.second;

        final += QString(ws, ' ');
        final += line.mid(idx);
        final += '\n';
    }

    if (!final.isEmpty())
        final.chop(1); // Remove last '\n' since it creates an extra line at the end

    auto c = textCursor();
    auto p = getCursorPosition();
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(final);
    setCursorPosition(p);
    c.endEditBlock();
}

int findFirstNonWS(const QStringRef& ref)
{
    for (int i = 0; i < ref.size(); ++i)
        if (!ref.at(i).isSpace())
            return i;

    return ref.size();
}

int findLastNonWS(const QStringRef& ref)
{
    for (int i = ref.size() - 1; i >= 0; i--)
        if (!ref.at(i).isSpace())
            return i;

    return 0;
}

void TextEdit::trimWhitespace(bool leading, bool trailing)
{
    if (isReadOnly()) return;

    if (!leading && !trailing)
        return;

    const QString& original = toPlainText();
    const auto lines = original.splitRef('\n');
    QString final;
    final.reserve(original.length());

    for (auto& line : lines) {
        const int start = !leading ? 0 : findFirstNonWS(line);
        const int end = !trailing ? -1 : findLastNonWS(line) - start + 1;

        final += line.mid(start, end);
        final += '\n';
    }

    if (!final.isEmpty())
        final.chop(1); // Remove last '\n' since it creates an extra line at the end

    auto c = textCursor();
    auto p = getCursorPosition();
    c.beginEditBlock();
    c.select(QTextCursor::Document);
    c.insertText(final);
    setCursorPosition(p);
    c.endEditBlock();
}

void TextEdit::updateSidebarGeometry()
{
    const auto firstBlock = firstVisibleBlock();
    const int lineHeight =
        int(firstBlock.isValid() ? blockBoundingRect(firstBlock).height() / firstBlock.lineCount() : 17); // Decent default

    m_sideBar->updateSizeHint(lineHeight);
    auto gutterWidth = m_sideBar->sizeHint().width();

    setViewportMargins(gutterWidth, 0, 0, 0);
    const auto r = contentsRect();
    m_sideBar->setGeometry(QRect(r.left(), r.top(), gutterWidth, r.height()));

    auto g = horizontalScrollBar()->geometry();
    g.setLeft(gutterWidth);
    horizontalScrollBar()->setGeometry(g);
}

void TextEdit::updateSidebarArea(const QRect& rect, int dy)
{
    if (dy)
        m_sideBar->scroll(0, dy);
    else
        m_sideBar->update(0, rect.y(), m_sideBar->width(), rect.height());
}

void TextEdit::onCursorPositionChanged()
{
    // Highlight current line
    highlightCurrentLine();

    auto c = textCursor();

    const auto numCursors = m_cursors.size();
    if (numCursors == 0)
        m_cursors.push_back(c);
    else if (numCursors == 1)
        m_cursors[0] = c;

    m_drawCursorsOn = false;

    m_cursorTimer.start();
    onCursorRepaint();

    m_findTermSelected = false;
}

void TextEdit::onSelectionChanged()
{
    const auto& cursor = textCursor();
    const auto& text = cursor.selectedText();
    const bool mcsEnabled = m_cursors.size() > 1;

    if (mcsEnabled || text.length() < 2 || text.trimmed().isEmpty()) {
        setExtraSelections(ESSameItems, {});
        return;
    }

    const auto& fullText = toPlainText();
    int j = 0;

    ExtraSelectionList list;
    QTextEdit::ExtraSelection selection;
    selection.format.setForeground(QBrush(getTheme().textColor(Theme::Keyword)));
    selection.format.setBackground(
        QBrush(getTheme().editorColor(Theme::SearchHighlight)));

    while ((j = fullText.indexOf(text, j)) != -1) {
        selection.cursor = cursor;
        selection.cursor.setPosition(j);
        selection.cursor.setPosition(j + text.length(), QTextCursor::KeepAnchor);

        list.append(selection);
        ++j;
    }

    setExtraSelections(ESSameItems, list);
}

void TextEdit::onContentsChange(int position, int removed, int added)
{
    auto& lbls = m_editorLabels;

    // When block contents change we want to:
    // * Remove all labels in the erased text range
    // * Modify the position of all labels past the beginning of the modified text range
    // * Update the display rects of all labels in the modified text range and a few blocks above that.

    // Move back a few blocks to find a good starting position from where we need to update all display rects.
    auto b = document()->findBlock(position);
    for (int i=EditorLabel::MAX_LINE_COUNT; i>0; --i)
        b = b.previous();
    if (!b.isValid())
        b = document()->firstBlock();
    auto startPos = b.position();

    // Find the first label past startPos.
    const auto lowerBound =
        std::lower_bound(lbls.begin(), lbls.end(), startPos, [](const EditorLabelPtr& ptr, int position) {
            return ptr->m_absPos < position;
        });

    const auto it = std::remove_if(lowerBound, lbls.end(), [position, added, removed](EditorLabelPtr& ptr) {
        // Mark label for deletion if needed
        if (ptr->m_absPos >= position && ptr->m_absPos <= position + removed) {
            return true;
        }

        // Queue an update to display rect
        ptr->m_changed = true;

        // Move label position if needed
        if (ptr->m_absPos >= position)
            ptr->m_absPos += added - removed;

        return false;
    });

    if (it != lbls.end()) {
        lbls.erase(it, lbls.end());
        viewport()->update();
    }
}

void TextEdit::ensureSelectionUnfolded(const TextEdit::Selection& sel)
{
    auto block = document()->findBlock(sel.start);
    const auto& endBlock = document()->findBlock(sel.end);

    do {
        if (isRegionFolded(block))
            toggleRegionFold(block);

        if (block == endBlock)
            break;

        block = block.next();
    } while (true);
}

const TextEdit::ExtraSelectionList* TextEdit::getExtraSelections(int type) const
{
    const auto it = m_extraSelections.find(type);
    return (it == m_extraSelections.end()) ? nullptr : &*it;
}

void TextEdit::setExtraSelections(int type, TextEdit::ExtraSelectionList list)
{
    m_extraSelectionsModified = true;
    m_extraSelections[type] = std::move(list);
}

void TextEdit::mousePressEvent(QMouseEvent* evt)
{
    if (evt->button() != Qt::LeftButton)
        return QPlainTextEdit::mousePressEvent(evt);

    const auto shiftAlt = Qt::ShiftModifier | Qt::AltModifier;
    const bool shiftAltPressed = (evt->modifiers() & shiftAlt) == shiftAlt;

    if (shiftAltPressed) {
        m_mcsTriggerState = McsTriggerState::Click;
        return;
    }

    // TODO: This needs to change if we want to have mcs text dragging
    if (m_cursors.size() > 1)  {
        mcsClearAllCursors();
        return QPlainTextEdit::mousePressEvent(evt);
    }

    if (m_config.enableTextDragging) {
        // FIXME: Only take action when cursor is truly on top of selection,
        // or we break triple selection
        const auto c = getSelectionUnderPoint(evt->pos());

        if (!c.isNull()) {
            m_dragState = DragState::Begin;
            m_dragOrigin = evt->pos();
            m_dragCursor = c;
            return;
        }
    }

    return QPlainTextEdit::mousePressEvent(evt);
}

QPoint TextEdit::getGridPointAt(const QPoint& point)
{
    const QTextCursor& cursor = cursorForPosition(point);
    const QString& text = cursor.block().text();
    const int position = cursor.positionInBlock();

    int column = 0;
    for (int i = 0; i < position; ++i) {
        if (text.at(i) == QLatin1Char('\t'))
            column = column - (column % m_config.tabWidth) + m_config.tabWidth;
        else
            ++column;
    }

    if (cursor.positionInBlock() == cursor.block().length()-1)
        column += (point.x() - cursorRect(cursor).center().x()) / QFontMetricsF(font()).width(QLatin1Char(' '));

    return {column, cursor.blockNumber()};
}

std::pair<int,int> TextEdit::getVisualSelection(const QTextBlock& block, const int beginColumn, const int endColumn) {
    if (beginColumn > endColumn)
        return {-1,-1};

    const QString& text = block.text();
    const int length = std::min(endColumn, text.length());
    int column = 0;
    int begin = -1, end = length;
    int m_tabWidth = 4;

    int i = 0;
    while (i < length && column < beginColumn) {
        if (text.at(i) == QLatin1Char('\t'))
            column = column - (column % m_tabWidth) + m_tabWidth;
        else
            ++column;
        ++i;
    }

    if (column < beginColumn)
        return {-1,-1};
    begin = i;

    while (i < length && column < endColumn) {
        if (text.at(i) == QLatin1Char('\t'))
            column = column - (column % m_tabWidth) + m_tabWidth;
        else
            ++column;
        ++i;
    }

    if (column < endColumn)
        end = text.length();
    else
        end = i;

    return std::make_pair(begin, end);
}

void TextEdit::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragState == DragState::Begin) {
        if ((event->pos()-m_dragOrigin).manhattanLength() > 5) {
            m_dragState = DragState::Ongoing;
            QGuiApplication::setOverrideCursor(Qt::DragMoveCursor);
        }
    } else if (m_dragState == DragState::Ongoing) {
        setTextCursor(cursorForPosition(event->pos()));
    } else if (m_mcsTriggerState == McsTriggerState::Click) {
        m_mcsTriggerState = McsTriggerState::Drag;
        mcsBlock.anchor = getGridPointAt(event->pos());
    } else if (m_mcsTriggerState == McsTriggerState::Drag) {
        mcsBlock.pos = getGridPointAt(event->pos());
        viewport()->update(); // TODO: Could be optimized by not repainting everything
    } else
        QPlainTextEdit::mouseMoveEvent(event);
}

void TextEdit::mouseReleaseEvent(QMouseEvent *event)
{
    const bool isLeftClick = event->button() == Qt::LeftButton;

    if (!isLeftClick)
        return QPlainTextEdit::mouseReleaseEvent(event);

    if (m_dragState == DragState::Ongoing) {
        m_dragState = DragState::NoDrag;
        QGuiApplication::restoreOverrideCursor();

        auto c = cursorForPosition(event->pos());
        const auto cpos = c.position();

        if (!(m_dragCursor.selectionStart() <= cpos &&
              cpos <= m_dragCursor.selectionEnd())) {
            c.beginEditBlock();
            c.insertText(m_dragCursor.selectedText());
            m_dragCursor.removeSelectedText();
            c.endEditBlock();
        } else { // Unselect any selection made, like a normal click
            setTextCursor(c);
        }
        return;
    } else if (m_dragState == DragState::Begin) {
        m_dragState = DragState::NoDrag;
        setTextCursor(cursorForPosition(event->pos()));
        return;
    } else if (m_mcsTriggerState == McsTriggerState::Click) {
        m_mcsTriggerState = McsTriggerState::NoTrigger;
        mcsAddCursor(cursorForPosition(event->pos()));
        return;
    } else if (m_mcsTriggerState == McsTriggerState::Drag) {
        m_mcsTriggerState = McsTriggerState::NoTrigger;

        mcsClearAllCursors(false);

        QTextBlock b = document()->findBlockByLineNumber(mcsBlock.top());
        while (b.isValid() && b.blockNumber() <= mcsBlock.bottom()) {
            auto p = getVisualSelection(b, mcsBlock.left(), mcsBlock.right());
            if (p.first != -1) {
                QTextCursor c(b);
                c.setPosition(b.position() + p.first);
                c.setPosition(b.position() + p.second, QTextCursor::KeepAnchor);
                mcsAddCursor(c);
            }
            b = b.next();
        };

        if (!m_cursors.empty()) {
            auto c = m_cursors[0];
            setTextCursor(c);
            if (m_cursors.size()==1)
                mcsClearAllCursors(false);
            else {
                mcsEnsureUniqueCursors();
                mcsUpdateSelectionHighlights();
            }
        }

        viewport()->update();
        return;
    } else
        return QPlainTextEdit::mouseReleaseEvent(event);

    Q_ASSERT_X(false, "mouseReleaseEvent", "Do we want to end up here?");
}

static inline bool isPrintableText(const QString& text)
{
    return !text.isEmpty() && (text[0].isPrint() || text[0] == '\t' || text[0] == '\r');
}

bool TextEdit::mcsMoveOperation(QKeyEvent* evt)
{
    QTextCursor::MoveOperation op = QTextCursor::NoMove;
    QTextCursor::MoveMode mode = QTextCursor::MoveAnchor;

    if (evt == QKeySequence::MoveToNextChar) { // Handle up,down,left,right
        op = QTextCursor::Right;
    } else if (evt == QKeySequence::MoveToPreviousChar) {
        op = QTextCursor::Left;
    } else if (evt == QKeySequence::MoveToNextLine) {
        op = QTextCursor::Down;
    } else if (evt == QKeySequence::MoveToPreviousLine) {
        op = QTextCursor::Up;
    } else if (evt == QKeySequence::MoveToEndOfLine) { // Handle begin/end of line
        op = QTextCursor::EndOfLine;
    } else if (evt == QKeySequence::MoveToStartOfLine) {
        op = QTextCursor::StartOfLine;
    } else if (evt == QKeySequence::MoveToNextWord) { // Handle next/prev word
        op = QTextCursor::NextWord;
    } else if (evt == QKeySequence::MoveToPreviousWord) {
        op = QTextCursor::PreviousWord;
    } else if (evt == QKeySequence::MoveToEndOfDocument) { // Handle begin/end document
        op = QTextCursor::End;
    } else if (evt == QKeySequence::MoveToStartOfDocument) {
        op = QTextCursor::Start;
    } else if (evt == QKeySequence::SelectNextChar) { // Now the same with SHIFT pressed
        op = QTextCursor::Right;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectPreviousChar) {
        op = QTextCursor::Left;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectNextLine) {
        op = QTextCursor::Down;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectPreviousLine) {
        op = QTextCursor::Up;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectEndOfLine) { // Handle begin/end of line
        op = QTextCursor::EndOfLine;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectStartOfLine) {
        op = QTextCursor::StartOfLine;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectNextWord) { // Handle next/prev word
        op = QTextCursor::NextWord;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectPreviousWord) {
        op = QTextCursor::PreviousWord;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectEndOfDocument) { // Handle begin/end document
        op = QTextCursor::End;
        mode = QTextCursor::KeepAnchor;
    } else if (evt == QKeySequence::SelectStartOfDocument) {
        op = QTextCursor::Start;
        mode = QTextCursor::KeepAnchor;
    } else
        return false;

    const auto currentCursor = textCursor();
    for (auto& c : m_cursors) {
        const bool setcc = (c == currentCursor);
        c.movePosition(op, mode);
        if (setcc)
            setTextCursor(c);
    }

    mcsEnsureUniqueCursors();
    return true;
}

void TextEdit::mcsInsertText(const QString& text)
{
    auto cc = textCursor();
    cc.beginEditBlock();
    for (auto& c : m_cursors) {
        if (c.hasSelection()) {
            cc.setPosition(c.selectionStart());
            cc.setPosition(c.selectionEnd(), QTextCursor::KeepAnchor);
            cc.removeSelectedText();
        } else {
            cc.setPosition(c.position());
            if (overwriteMode() && !cc.atBlockEnd())
                cc.deleteChar();
        }
        cc.insertText(text);
    }
    cc.endEditBlock();
}

bool TextEdit::mcsAddCursor(const QTextCursor& c)
{
    const auto it = std::lower_bound(
        m_cursors.begin(), m_cursors.end(), c.position(), [](const QTextCursor& c, int p) { return c.position() < p; });

    if (it != m_cursors.end() && it->position() == c.position())
        return false;

    m_cursors.insert(it, c);
    return true;
}

void TextEdit::mcsEnsureUniqueCursors()
{
    // TODO: don't allow overlapping selections.
    auto pos = std::unique(m_cursors.begin(), m_cursors.end(), [](const QTextCursor& a, const QTextCursor& b) {
        return a.position() == b.position();
    });
    m_cursors.erase(pos, m_cursors.end());
}

void TextEdit::mcsUpdateSelectionHighlights()
{
    ExtraSelectionList sels;
    QTextEdit::ExtraSelection es;
    es.format.setBackground(QBrush(getTheme().editorColor(Theme::TextSelection)));

    for (const auto& c : m_cursors) {
        if (!c.hasSelection())
            continue;
        es.cursor = c;
        sels << es;
    }
    setExtraSelections(ESCursorSelection, sels);
}

void TextEdit::mcsClearAllCursors(bool updateViewport)
{
    m_cursors.clear();
    setExtraSelections(ESCursorSelection, {});
    if (updateViewport)
        viewport()->update();
}

void TextEdit::mcsPaste(const QStringList& list)
{
    const size_t numLines = size_t(list.size());

    QTextCursor bc(document());
    bc.beginEditBlock();
    if (numLines == m_cursors.size()) {
        int i = 0;
        for (auto& c : m_cursors) {
            c.insertText(list[i++]);
        }
    } else {
        mcsInsertText(list.join('\n'));
    }
    bc.endEditBlock();
}

void TextEdit::mcsPaste(const QString& text)
{
    const size_t numLines = size_t(text.count('\n')) + 1;

    if (numLines == m_cursors.size()) {
        const auto arr = text.split('\n');

        int i = 0;
        for (auto& c : m_cursors) {
            c.insertText(arr[i++]);
        }
    } else {
        mcsInsertText(text);
    }
}

void TextEdit::onCursorRepaint()
{
    if (m_config.cursorFlashTime==0)
        m_drawCursorsOn = true;
    else
        m_drawCursorsOn = !m_drawCursorsOn;

    // If we only have a single cursor it'll be updated by QPlainTextEdit's default mechanism.
    // For more than one cursor we'll just update the whole viewport.
    // Possible optimization: find the first/last visible cursor and update only the area between them.
    if (m_cursors.size() > 1)
        viewport()->update();
}

void TextEdit::keyPressEvent(QKeyEvent* event)
{
    const auto shiftAlt = Qt::ShiftModifier | Qt::AltModifier;
    const bool shiftAltPressed = (event->modifiers() & shiftAlt) == shiftAlt;
    const bool ctrlPressed = (event->modifiers() & Qt::ControlModifier) == Qt::ControlModifier;

    if (shiftAltPressed) {
        QTextCursor::MoveOperation op = QTextCursor::NoMove;
        switch (event->key()) {
        case Qt::Key_Up:
            op = QTextCursor::Up;
            break;
        case Qt::Key_Down:
            op = QTextCursor::Down;
            break;
        }

        if (op != QTextCursor::NoMove) {
            auto c = textCursor();
            c.movePosition(op);
            mcsAddCursor(c);
            setTextCursor(c);
        }
        else // Some languages use Shift+Alt as a normal modifier.
            return singleCursorKeyPressEvent(event);

        return;
    }

    if (ctrlPressed) {
        int step = 0;
        switch(event->key()) {
        case Qt::Key_Up: step = -1; break;
        case Qt::Key_Down: step = +1; break;
        }
        if (step != 0) {
            verticalScrollBar()->setValue( verticalScrollBar()->value() + step );
            return;
        }
    }

    if (m_cursors.size() <= 1)
        return singleCursorKeyPressEvent(event);

    // Cancel mcs drag and mcs with Escape key
    if (event->key() == Qt::Key_Escape) {
        if (m_mcsTriggerState == McsTriggerState::Drag) {
            m_mcsTriggerState = McsTriggerState::NoTrigger;
            viewport()->update();
        } else {
            mcsClearAllCursors();
        }
        return;
    }

    if (mcsMoveOperation(event)) {
        mcsUpdateSelectionHighlights();
        return;
    }

    // All following actions except Copy are mutating. Skip if read-only
    if (isReadOnly() && !(event == QKeySequence::Copy))
        return singleCursorKeyPressEvent(event);

    if (event == QKeySequence::Undo)
        return undo();
    if (event == QKeySequence::Redo)
        return redo();

    const QString eventText = event->text();
    if (isPrintableText(eventText)) {
        mcsInsertText(eventText);
        return;
    }

    if (event == QKeySequence::Copy || event == QKeySequence::Cut) {
        const bool cut = event == QKeySequence::Cut;
        QString text;
        QTextCursor blockCursor(document());

        if (cut)
            blockCursor.beginEditBlock();

        for (auto& c : m_cursors) {
            text += c.selectedText() + '\n';
            if (cut)
                c.removeSelectedText();
        }
        if (text.isEmpty())
            return;

        text.chop(1);
        QGuiApplication::clipboard()->setText(text);

        if (cut) {
            blockCursor.endEditBlock();
            mcsEnsureUniqueCursors();
        }
        return;
    }
    if (event == QKeySequence::Paste) {
        mcsPaste(QGuiApplication::clipboard()->text());
        return;
    }

    if (event == QKeySequence::Delete) {
        QTextCursor blockCursor(document());
        blockCursor.beginEditBlock();
        for (auto& c : m_cursors) {
            if (c.hasSelection()) {
                c.removeSelectedText();
            } else {
                c.deleteChar();
            }
        }
        blockCursor.endEditBlock();

        mcsEnsureUniqueCursors();
        return;
    }

    if (event->key() == Qt::Key_Backspace) {
        QTextCursor blockCursor(document());
        blockCursor.beginEditBlock();
        for (auto& c : m_cursors) {
            if (c.hasSelection()) {
                c.removeSelectedText();
            } else {
                c.deletePreviousChar();
            }
        }
        blockCursor.endEditBlock();

        mcsEnsureUniqueCursors();
        return;
    }
}

void TextEdit::singleCursorKeyPressEvent(QKeyEvent* e)
{
    // All following actions modify the text in some way. Just skip them if read-only.
    if (isReadOnly()) {
        return QPlainTextEdit::keyPressEvent(e);
    }

    if (e->key() == Qt::Key_Tab && m_config.convertTabToSpaces) {
        auto cursor = textCursor();
        auto numSpaces = m_config.tabWidth - cursor.positionInBlock() % m_config.tabWidth;
        if (numSpaces == 0)
            numSpaces = m_config.tabWidth;
        cursor.insertText(QString(numSpaces, ' '));
        return;
    }

    if (e->key() == Qt::Key_Return && m_config.useSmartIndent) {
        auto cursor = textCursor();
        cursor.beginEditBlock();
        QPlainTextEdit::keyPressEvent(e);
        auto txt = textCursor().block().previous().text();

        int txtPos = 0;
        while (txt[txtPos] == ' ' || txt[txtPos] == '\t')
            ++txtPos;

        textCursor().insertText(txt.mid(0, txtPos));
        cursor.endEditBlock();
        return;
    }

    if (e->key() == Qt::Key_Backspace && m_config.convertTabToSpaces) {
        auto txt = textCursor().block().text();

        if (txt.isEmpty())
            goto processEventNormally;

        if (txt.endsWith(QString(m_config.tabWidth, ' ')) && (txt.length() % m_config.tabWidth) == 0) {
            auto c = textCursor();

            c.movePosition(QTextCursor::Left, QTextCursor::KeepAnchor, m_config.tabWidth);
            c.removeSelectedText();
            return;
        }
    }

    if (e->key() == Qt::Key_Insert)
        setOverwriteMode(!overwriteMode());

processEventNormally:
    QPlainTextEdit::keyPressEvent(e);
}

QTextCursor TextEdit::getSelectionUnderPoint(QPoint p) const
{
    const auto cpos = cursorForPosition(p).position();

    for (const auto& c : m_cursors) {
        if (c.hasSelection() && cpos >= c.selectionStart() && cpos <= c.selectionEnd())
            return c;
    }
    return QTextCursor();
}

void TextEdit::wheelEvent(QWheelEvent* event)
{
    emit mouseWheelUsed(event);
    QPlainTextEdit::wheelEvent(event);
}

void TextEdit::dropEvent(QDropEvent* event)
{
    if (event->mimeData() && event->mimeData()->hasUrls())
        emit urlsDropped(event->mimeData()->urls());
}

void TextEdit::inputMethodEvent(QInputMethodEvent* e)
{
    // inputMethodEvent is used for languages like Chinese that composite their characters.
    // This function is almost the same as TextEditorWidget::inputMethodEvent from the qt-creator source.
    if (e->commitString().isEmpty() && e->preeditString().isEmpty() && e->attributes().isEmpty()) {
        e->accept();
        return;
    }

    if (m_cursors.size()>1) {
        if (!e->commitString().isEmpty())
            mcsInsertText(e->commitString());
        return;
    }
    QPlainTextEdit::inputMethodEvent(e);
}

void TextEdit::focusInEvent(QFocusEvent* event)
{
    emit gotFocus();
    QPlainTextEdit::focusInEvent(event);
}

void TextEdit::contextMenuEvent(QContextMenuEvent* event)
{
    QPlainTextEdit::contextMenuEvent(event);
    /*auto menu = createStandardContextMenu(event->pos());
    menu->exec(event->globalPos());
    delete menu;*/
}

void TextEdit::paintLineSuffixes(QPainter& painter, const BlockList& blockList) const
{
    // Paints EOL symbols and the '((...))' labels for folded regions.
    const QFontMetrics& metrics = fontMetrics();
    const QChar visualArrow = ushort(0x21A4);
    const qreal arrowWidth = metrics.width(visualArrow);
    const QChar cont[]{0x2E28, 0x22EF, 0x22EF, 0x2E29, 0}; // ((...))
    const QString contStr(cont);
    const qreal constWidth = metrics.width(contStr);
    const qreal spaceWidth = metrics.width(' ') * 2;

    const QColor textColor(getTheme().textColor(Theme::Normal));
    const QBrush regionBrush(getTheme().textColor(Theme::RegionMarker));

    painter.save();
    painter.setPen(textColor);

    for (const auto& blockData : blockList) {
        const auto block = blockData.block;
        const auto geom = blockData.translatedRect;

        if (!block.isVisible())
            continue;

        const bool folded = isRegionFolded(block);

        if (!m_config.showEndOfLineMarkers && !folded)
            continue;

        const QTextLayout* layout = block.layout();
        const int lineCount = layout->lineCount();
        const QTextLine line = layout->lineAt(lineCount - 1);
        const QRectF lineRect = line.naturalTextRect().translated(contentOffset().x(), geom.top());

        if (m_config.showEndOfLineMarkers)
            painter.drawText(QPointF(lineRect.right() + 2, lineRect.top() + line.ascent()), visualArrow);

        if (folded) {
            qreal offset = spaceWidth + (m_config.showEndOfLineMarkers ? arrowWidth : 0);
            painter.save();
            painter.setPen(getTheme().textColor(Theme::RegionMarker));

            QRectF rect;
            rect.setTopLeft(QPointF(lineRect.right() + offset, lineRect.top()));
            rect.setHeight(lineRect.height() - 1); // normal height() is a little taller than the line highlight
            rect.setWidth(constWidth);

            // painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath path;
            path.addRoundedRect(rect, 3.0, 3.0);
            painter.fillPath(path, regionBrush);
            painter.drawPath(path);

            painter.setPen(textColor);
            painter.drawText(QPointF(lineRect.right() + offset, lineRect.top() + line.ascent()), contStr);
            painter.restore();
        }
    }

    painter.restore();
}

void TextEdit::paintLineBreaks(QPainter& painter, const BlockList& blockList) const
{
    if (!m_config.showLinebreaks)
        return;

    const QChar visualArrow = ushort(0x21B5);
    const auto arrowWidth = fontMetrics().boundingRect(visualArrow).width();

    painter.save();
    painter.setPen(getTheme().textColor(Theme::Normal));

    for (const auto& blockData : blockList) {
        const auto block = blockData.block;
        const auto geom = blockData.translatedRect;

        const QTextLayout* layout = block.layout();
        const int lineCount = layout->lineCount();
        const int arrowX = geom.width() - contentsMargins().right() - arrowWidth;

        if (lineCount <= 1)
            continue;

        for (int i = 0; i < lineCount - 1; ++i) {
            const QTextLine line = layout->lineAt(i);
            const QRectF lineRect = line.naturalTextRect().translated(contentOffset().x(), geom.top());

            painter.drawText(QPointF(arrowX, lineRect.top() + line.ascent()), visualArrow);
        }
    }

    painter.restore();
}

void TextEdit::paintSearchBlock(QPainter& painter, const QRect& /*eventRect*/, const QTextBlock& block)
{
    return;
    QTextLayout* layout = block.layout();
    QString text = block.text();

    QTextLine line = layout->lineForTextPosition(0);

    QRectF rr = line.naturalTextRect();
    QRectF r = blockBoundingGeometry(block).translated(contentOffset());

    rr.moveTop(rr.top() + r.top());

    painter.fillRect(rr, QColor("#5000FF00"));

    QColor lineCol = QColor("red");
    QPen pen = painter.pen();
    painter.setPen(lineCol);
    // if (block == d->m_findScopeStart.block())
    painter.drawLine(rr.topLeft(), rr.topRight());
    // if (block == d->m_findScopeEnd.block())
    painter.drawLine(rr.bottomLeft(), rr.bottomRight());
    painter.drawLine(rr.topLeft(), rr.bottomLeft());
    painter.drawLine(rr.topRight(), rr.bottomRight());
    painter.setPen(pen);

    /*qreal spacew = QFontMetricsF(font()).width(QLatin1Char(' '));

    int offset = 0;
    int relativePos  =  ts.positionAtColumn(text,
                                            d->m_findScopeVerticalBlockSelectionFirstColumn,
                                            &offset);

    qreal x = line.cursorToX(relativePos) + offset * spacew;

    int eoffset = 0;
    int erelativePos  =  ts.positionAtColumn(text,
                                             d->m_findScopeVerticalBlockSelectionLastColumn,
                                             &eoffset);
    QTextLine eline = layout->lineForTextPosition(erelativePos);
    qreal ex = eline.cursorToX(erelativePos) + eoffset * spacew;


    rr.moveTop(rr.top() + r.top());
    rr.setLeft(r.left() + x);
    if (line.lineNumber() == eline.lineNumber())
        rr.setRight(r.left() + ex);


    QColor lineCol = QColor("red");
    QPen pen = painter.pen();
    painter.setPen(lineCol);
    if (blockFS == d->m_findScopeStart.block())
        painter.drawLine(rr.topLeft(), rr.topRight());
    if (blockFS == d->m_findScopeEnd.block())
        painter.drawLine(rr.bottomLeft(), rr.bottomRight());
    painter.drawLine(rr.topLeft(), rr.bottomLeft());
    painter.drawLine(rr.topRight(), rr.bottomRight());
    painter.setPen(pen);
*/

    /*  auto er = eventRect;

    if (!m_findActive) {
        QTextBlock blockFS = block;
        QPointF offsetFS = contentOffset();
        while (blockFS.isValid()) {

            QRectF r = blockBoundingRect(blockFS).translated(offsetFS);

            if (r.bottom() >= er.top() && r.top() <= er.bottom()) {

                if (blockFS.position() >= d->m_findScopeStart.block().position()
                        && blockFS.position() <= d->m_findScopeEnd.block().position()) {
                    QTextLayout *layout = blockFS.layout();
                    QString text = blockFS.text();
                    const TabSettings &ts = d->m_document->tabSettings();
                    qreal spacew = QFontMetricsF(font()).width(QLatin1Char(' '));

                    int offset = 0;
                    int relativePos  =  ts.positionAtColumn(text,
                                                            d->m_findScopeVerticalBlockSelectionFirstColumn,
                                                            &offset);
                    QTextLine line = layout->lineForTextPosition(relativePos);
                    qreal x = line.cursorToX(relativePos) + offset * spacew;

                    int eoffset = 0;
                    int erelativePos  =  ts.positionAtColumn(text,
                                                             d->m_findScopeVerticalBlockSelectionLastColumn,
                                                             &eoffset);
                    QTextLine eline = layout->lineForTextPosition(erelativePos);
                    qreal ex = eline.cursorToX(erelativePos) + eoffset * spacew;

                    QRectF rr = line.naturalTextRect();
                    rr.moveTop(rr.top() + r.top());
                    rr.setLeft(r.left() + x);
                    if (line.lineNumber() == eline.lineNumber())
                        rr.setRight(r.left() + ex);
                    painter.fillRect(rr, QColor("yellow"));

                    QColor lineCol = QColor("red");
                    QPen pen = painter.pen();
                    painter.setPen(lineCol);
                    if (blockFS == d->m_findScopeStart.block())
                        painter.drawLine(rr.topLeft(), rr.topRight());
                    if (blockFS == d->m_findScopeEnd.block())
                        painter.drawLine(rr.bottomLeft(), rr.bottomRight());
                    painter.drawLine(rr.topLeft(), rr.bottomLeft());
                    painter.drawLine(rr.topRight(), rr.bottomRight());
                    painter.setPen(pen);
                }
            }
            offsetFS.ry() += r.height();

            if (offsetFS.y() > viewport()->rect().height())
                break;

            blockFS = blockFS.next();
            if (!blockFS.isVisible()) {
                // invisible blocks do have zero line count
                blockFS = document()->findBlockByLineNumber(blockFS.firstLineNumber()); //findBlockByNumber?
            }

        }
    }*/
}

void TextEdit::compositeExtraSelections()
{
    if (!m_extraSelectionsModified)
        return;

    m_extraSelectionsModified = false;
    ExtraSelectionList fullList;

    for (const auto& list : m_extraSelections)
        fullList << list;

    QPlainTextEdit::setExtraSelections(fullList);
}

void TextEdit::redrawAllEditorLabels()
{
    for (auto& lbl : m_editorLabels)
        lbl->markForRedraw();
}

static void fillBackground(QPainter* p, const QRectF& rect, QBrush brush, const QRectF& gradientRect = QRectF())
{
    p->save();
    if (brush.style() >= Qt::LinearGradientPattern && brush.style() <= Qt::ConicalGradientPattern) {
        if (!gradientRect.isNull()) {
            QTransform m = QTransform::fromTranslate(gradientRect.left(), gradientRect.top());
            m.scale(gradientRect.width(), gradientRect.height());
            brush.setTransform(m);
            const_cast<QGradient*>(brush.gradient())->setCoordinateMode(QGradient::LogicalMode);
        }
    } else {
        p->setBrushOrigin(rect.topLeft());
    }
    p->fillRect(rect, brush);
    p->restore();
}

void TextEdit::paintEvent(QPaintEvent* e)
{
    compositeExtraSelections();

    QPainter painter(viewport());
    Q_ASSERT(qobject_cast<QPlainTextDocumentLayout*>(document()->documentLayout()));
    QPointF offset(contentOffset());
    QRect er = e->rect();
    const QRect viewportRect = viewport()->rect();
    QTextBlock block = firstVisibleBlock();
    const qreal maximumWidth = document()->documentLayout()->documentSize().width();

    // Set a brush origin so that the WaveUnderline knows where the wave started
    painter.setBrushOrigin(offset);

    // keep right margin clean from full-width selection
    const int maxX = int(offset.x() + qMax(qreal(viewportRect.width()), maximumWidth) - document()->documentMargin());
    er.setRight(qMin(er.right(), maxX));
    painter.setClipRect(er);

    QAbstractTextDocumentLayout::PaintContext context = getPaintContext();

    const bool drawCursor =
        m_drawCursorsOn && hasFocus() && ((!isReadOnly() || (textInteractionFlags() & Qt::TextSelectableByKeyboard)));
    const bool drawCursorAsBlock = drawCursor && overwriteMode();

    // In some cases the main textCursor isn't properly added to the cursor vector. We pretty much don't ever want this
    // empty
    if (m_cursors.empty())
        m_cursors.push_back(textCursor());

    // TODO: single selction handled differently for now. Since it's added by Qt itself we've got to catch it and change
    // it Single-cursor doesn't ever call the code for creating text highlights. That should have to happen in
    // onCursorChange() I guess.
    if (m_cursors.size() > 1 && textCursor().hasSelection())
        context.selections.pop_back();
    else if (!context.selections.isEmpty() && textCursor().hasSelection()) {
        auto& s = context.selections.last();

        s.format.clearForeground();
        s.format.setBackground(QBrush(getTheme().editorColor(Theme::TextSelection)));
    }

    while (block.isValid()) {
        QRectF r = blockBoundingRect(block).translated(offset);

        if (r.bottom() >= er.top())
            break;

        offset.ry() += r.height();
        if (offset.y() > viewportRect.height())
            break;

        block = block.next();
    }

    std::vector<QTextCursor const*> cursorsInBlock;
    auto cursorIt = m_cursors.cbegin();

    QTextBlock beginBlock = block;
    QTextBlock endBlock;

    QRectF mcsBlockRect;
    if (m_mcsTriggerState == McsTriggerState::Drag) {
        const qreal spacew = QFontMetricsF(font()).width(QLatin1Char(' '));
        const qreal margin = document()->documentMargin();
        mcsBlockRect.setLeft(mcsBlock.left() * spacew + margin);
        mcsBlockRect.setWidth(mcsBlock.width() * spacew);
        mcsBlockRect.setHeight(9999);
    }

    while (block.isValid()) {
        QRectF r = blockBoundingRect(block).translated(offset);
        QTextLayout* layout = block.layout();
        const int blockNum = block.blockNumber();

        if (!block.isVisible()) {
            offset.ry() += r.height();
            block = block.next();
            continue;
        }

        if (r.top() > er.bottom())
            break;

        endBlock = block;
        QTextBlockFormat blockFormat = block.blockFormat();

        QBrush bg = blockFormat.background();
        /*QColor cc = bg.color(); // Random coloration
        cc.setBlue( rand() % 200 );
        cc.setGreen( rand() % 200 );
        bg = QBrush(cc);*/
        if (bg != Qt::NoBrush) {
            QRectF contentsRect = r;
            contentsRect.setWidth(qMax(r.width(), maximumWidth));
            fillBackground(&painter, contentsRect, bg);
        }

        QVector<QTextLayout::FormatRange> selections;
        const int blockStart = block.position();
        const int blockLength = block.length();
        const int blockEnd = blockStart + blockLength;

        // Collect selections for this block
        for (int i = 0; i < context.selections.size(); ++i) {
            const QAbstractTextDocumentLayout::Selection& range = context.selections.at(i);
            const int selStart = range.cursor.selectionStart() - blockStart;
            const int selEnd = range.cursor.selectionEnd() - blockStart;
            if (selStart < blockLength && selEnd > 0 && selEnd > selStart) {
                QTextLayout::FormatRange o;
                o.start = selStart;
                o.length = selEnd - selStart;
                o.format = range.format;
                selections.append(o);
            } else if (!range.cursor.hasSelection() && range.format.hasProperty(QTextFormat::FullWidthSelection) &&
                       block.contains(range.cursor.position())) {
                // for full width selections we don't require an actual selection, just
                // a position to specify the line. that's more convenience in usage.
                QTextLayout::FormatRange o;
                QTextLine l = layout->lineForTextPosition(range.cursor.position() - blockStart);

                o.start = l.textStart();
                o.length = l.textLength();
                if (o.start + o.length == blockLength - 1)
                    ++o.length; // include newline
                o.format = range.format;
                selections.append(o);
            }
        }

        // Collect cursors for this block and add selections for block-cursors (when overwriteMode is enabled)
        if (drawCursor) {
            cursorsInBlock.clear();
            while (cursorIt != m_cursors.cend()) {
                const auto& c = *cursorIt;
                auto cpos = c.position();
                if (cpos >= blockEnd)
                    break;
                if (cpos >= blockStart)
                    cursorsInBlock.push_back(&c);
                ++cursorIt;
            }

            if (drawCursorAsBlock) {
                for (auto& c : cursorsInBlock) {
                    if (c->position() == blockEnd - 1)
                        continue;

                    QTextLayout::FormatRange o;
                    o.start = c->position() - blockStart;
                    o.length = 1;
                    o.format.setForeground(palette().base());
                    o.format.setBackground(palette().text());
                    selections.append(o);
                    c = nullptr;
                }
            }
        }

        layout->draw(&painter, offset, selections, er);

        if (m_mcsTriggerState == McsTriggerState::Drag) {
            if (blockNum == mcsBlock.top()) {
                const QTextLine line = layout->lineForTextPosition(0);
                QRectF rr = line.naturalTextRect();
                mcsBlockRect.moveTop(rr.top() + r.top());
            }
            if (blockNum == mcsBlock.bottom()) {
                const QTextLine line = layout->lineForTextPosition(0);
                QRectF rr = line.naturalTextRect();
                mcsBlockRect.setBottom(r.bottom() + rr.top());
            }
        }

        // Draw non-block cursors now
        if (drawCursor) {
            for (const auto c : cursorsInBlock) {
                if (c == nullptr)
                    continue;

                auto cpos = c->position();
                if (cpos < -1)
                    cpos = layout->preeditAreaPosition() - (cpos + 2);
                else
                    cpos -= block.position();
                layout->drawCursor(&painter, offset, cpos, cursorWidth());
            }
        }

        offset.ry() += r.height();
        if (offset.y() > viewportRect.height())
            break;
        block = block.next();
    }

    if (backgroundVisible() && !block.isValid() && offset.y() <= er.bottom() &&
        (centerOnScroll() || verticalScrollBar()->maximum() == verticalScrollBar()->minimum())) {
        painter.fillRect(QRect(QPoint(int(er.left()), int(offset.y())), er.bottomRight()), palette().background());
    }

    auto bl = getBlocksInRect(e->rect());
    paintLineBreaks(painter, bl);
    paintLineSuffixes(painter, bl);

    // Paint EditorLabels
    int numLines = EditorLabel::MAX_LINE_COUNT;
    while(numLines > 0) {
        auto prevBlock = beginBlock.previous();
        if (!prevBlock.isValid())
            break;
        beginBlock = prevBlock;
        if (!prevBlock.isVisible())
            continue;
        numLines -= prevBlock.lineCount();
    }

    EditorLabelIterator upper, lower;
    std::tie(lower, upper) = getEditorLabelsInRange(beginBlock.position(), endBlock.position() + endBlock.length());
    bool wantRepaint = false;

    for (; lower != upper; ++lower) {
        const auto ptr = *lower;
        const QTextBlock b = document()->findBlock(ptr->m_absPos);

        if (!b.isVisible())
            continue;

        if (ptr->m_changed && ptr->updateDisplayRect()) {
            ptr->updatePixmap();
            wantRepaint = true;
        }

        const auto op = blockBoundingGeometry(b).translated(contentOffset()).topLeft();
        ptr->draw(painter, op);
    }

    if (m_mcsTriggerState == McsTriggerState::Drag) {
        QPen pen;
        QBrush b;
        b.setStyle(Qt::SolidPattern);
        b.setColor(getTheme().editorColor(Theme::TextSelection));
        pen.setWidth(1);
        pen.setColor(getTheme().editorColor(Theme::IconBorder));
        painter.setPen(pen);
        painter.setBrush(b);
        painter.setOpacity(0.5);
        painter.drawRect(mcsBlockRect);
    }


    if (wantRepaint)
        viewport()->update();
}

TextEdit::BlockList TextEdit::getBlocksInViewport() const
{
    return getBlocksInRect(viewport()->rect());
}

TextEdit::BlockList TextEdit::getBlocksInRect(QRect rect) const
{
    BlockList bl;
    auto block = firstVisibleBlock();
    auto contentOff = contentOffset();

    while (block.isValid()) {
        const auto geom = blockBoundingGeometry(block).translated(contentOff).toRect();

        if (geom.bottom() >= rect.top()) {
            bl.push_back(BlockData{block, geom});

            if (geom.top() > rect.bottom())
                break;
        }

        block = block.next();
    }
    return bl;
}

QTextBlock TextEdit::blockAtPosition(int y) const
{
    auto block = firstVisibleBlock();
    if (!block.isValid())
        return QTextBlock();

    const auto geom = blockBoundingGeometry(block).translated(contentOffset()).toRect();
    int top = geom.top();
    int bottom = top + geom.height();

    do {
        if (top <= y && y <= bottom)
            return block;
        block = block.next();
        top = bottom;
        bottom = int(top + blockBoundingRect(block).height());
    } while (block.isValid());
    return QTextBlock();
}

void TextEdit::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    updateSidebarGeometry();

    if (wordWrapMode() != QTextOption::NoWrap)
        redrawAllEditorLabels();
}

void TextEdit::leaveEvent(QEvent* evt)
{
    if(m_dragState == DragState::Ongoing) {
        QGuiApplication::restoreOverrideCursor();
        m_dragState = DragState::NoDrag;
    }
    QPlainTextEdit::leaveEvent(evt);
}

QTextBlock TextEdit::findEndOfFoldingRegion(const QTextBlock& startBlock) const
{
    return m_highlighter->findFoldingRegionEnd(startBlock);
}

QTextBlock TextEdit::findBeginOfFoldingRegion(const QTextBlock& startBlock) const
{
    if (m_highlighter->startsFoldingRegion(startBlock))
        return startBlock;

    auto block = startBlock.previous();
    while(block.isValid() && ! m_highlighter->startsFoldingRegion(block))
        block = block.previous();

    return block;
}

bool TextEdit::isBlockFolded(const QTextBlock& block) const
{
    return !block.isVisible();
}

bool TextEdit::isFoldingRegion(const QTextBlock& block) const
{
    return m_highlighter->startsFoldingRegion(block);
}

bool TextEdit::isRegionFolded(const QTextBlock& block) const
{
    if (!block.isValid())
        return false;

    const auto nextBlock = block.next();
    if (!nextBlock.isValid())
        return false;
    return !nextBlock.isVisible();
}

void TextEdit::toggleRegionFold(QTextBlock startBlock)
{
    // we also want to fold the last line of the region, therefore the ".next()"

    while(!startBlock.isVisible())
        startBlock = startBlock.previous();

    const auto endBlock = findEndOfFoldingRegion(startBlock).next();

    if (isRegionFolded(startBlock)) {
        // unfold
        auto block = startBlock.next();
        while (block.isValid() && !block.isVisible()) {
            block.setVisible(true);
            block.setLineCount(block.layout()->lineCount());
            block = block.next();
        }

    } else {
        // fold
        auto block = startBlock.next();
        while (block.isValid() && block != endBlock) {
            block.setVisible(false);
            block.setLineCount(0);
            block = block.next();
        }
    }

    // redraw document
    document()->markContentsDirty(startBlock.position(), endBlock.position() - startBlock.position() + 1);

    // update scrollbars
    emit document()->documentLayout()->documentSizeChanged(document()->documentLayout()->documentSize());
}

bool TextEdit::isBookmarked(const QTextBlock& block) const
{
    return m_highlighter->isBookmarked(block);
}

bool TextEdit::isBookmarked(TextEdit::CursorPos pos) const
{
    QTextCursor c(document());
    c.setPosition(pos);
    return m_highlighter->isBookmarked(c.block());
}

void TextEdit::setBookmark(const QTextBlock block, bool bookmarked)
{
    m_highlighter->setBookmark(block, bookmarked);
}

void TextEdit::setBookmark(TextEdit::CursorPos pos, bool bookmarked)
{
    QTextCursor c(document());
    c.setPosition(pos);
    m_highlighter->setBookmark(c.block(), bookmarked);
}

void TextEdit::toggleBookmark(const QTextBlock& block)
{
    m_highlighter->toggleBookmark(block);
}

void TextEdit::deleteMarkedEditorLabelsInRange(const TextEdit::EditorLabelRange &range)
{
    auto it = std::remove_if(range.first, range.second, [](const EditorLabelPtr& ptr) {
        return ptr->m_markedForDeletion;
    });
    if (it == range.second)
        return;

    m_editorLabels.erase(it, range.second);
    viewport()->update();
}

TextEdit::EditorLabelRange TextEdit::getEditorLabelsInRange(int begin, int end)
{
    const auto lower =
        std::upper_bound(m_editorLabels.begin(), m_editorLabels.end(), begin, [](int val, const EditorLabelPtr& ptr) {
            return val < ptr->m_absPos;
        });

    if (lower == m_editorLabels.end())
        return {lower, lower};

    const auto upper =
        std::find_if(lower, m_editorLabels.end(), [end](const EditorLabelPtr& ptr) { return ptr->m_absPos > end; });
    return {lower, upper};
}

TextEdit::EditorLabelRange TextEdit::getEditorLabelsInBlock(const QTextBlock& block)
{
    const auto begin = block.position();
    const auto end = begin + block.length();
    return getEditorLabelsInRange(begin, end);
}

WeakEditorLabelPtr TextEdit::getEditorLabelAtPos(int pos)
{
    auto it = std::find_if(m_editorLabels.begin(), m_editorLabels.end(), [pos](const EditorLabelPtr& ptr) {
        return pos == ptr->m_absPos;
    });

    if (it != m_editorLabels.end())
        return *it;
    else
        return WeakEditorLabelPtr();
}

void TextEdit::removeEditorLabel(WeakEditorLabelPtr label)
{
    if (label.expired())
        return;

    auto it = std::find(m_editorLabels.begin(), m_editorLabels.end(), label.lock());

    if (it != m_editorLabels.end()) {
        m_editorLabels.erase(it);
        viewport()->update();
    }
}

WeakEditorLabelPtr TextEdit::addEditorLabel(EditorLabelPtr label)
{
    const int pos = label->m_absPos;
    auto it =
        std::lower_bound(m_editorLabels.begin(), m_editorLabels.end(), pos, [](const EditorLabelPtr& ptr, int pos) {
            return ptr->m_absPos < pos;
        });
    return *m_editorLabels.insert(it, label);
}


} // namespace ote
