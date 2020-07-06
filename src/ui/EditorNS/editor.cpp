#include "include/EditorNS/editor.h"

#include "include/notepadqq.h"
#include "include/nqqsettings.h"

#include <ote/textedit.h>
#include "ote/Highlighter/repository.h"
#include "ote/plugins/bracketmatcherplugin.h"
#include "ote/plugins/colorlabelsplugin.h"
#include "ote/plugins/latexplugin.h"

#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QMessageBox>
#include <QRegExp>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace EditorNS
{
    Editor::Editor(QWidget* parent)
        : QWidget(parent)
        , m_textEditor(new ote::TextEdit(parent))
    {
        const auto& repo = ote::TextEdit::getRepository();

        QString themeName = NqqSettings::getInstance().Appearance.getColorScheme();
        auto theme = repo.theme(themeName);

        fullConstructor(theme);
    }

    void Editor::fullConstructor(const ote::Theme& theme) // FIXME: Should use new Theme
    {
        m_layout = new QVBoxLayout(this);
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_layout->setSpacing(0);
        m_layout->addWidget(m_textEditor, 1);
        setLayout(m_layout);

        setTheme(theme);
        setLanguage(nullptr);

        connect(m_textEditor, &ote::TextEdit::textChanged, this, &Editor::contentChanged);
        connect(m_textEditor, &ote::TextEdit::cursorPositionChanged, this, &Editor::cursorActivity);
        connect(m_textEditor, &ote::TextEdit::gotFocus, this, &Editor::gotFocus);

        connect(m_textEditor, &ote::TextEdit::modificationChanged, [this](bool b) { emit cleanChanged(!b); });

        connect(m_textEditor, &ote::TextEdit::mouseWheelUsed, this, &Editor::mouseWheel);
        connect(m_textEditor, &ote::TextEdit::urlsDropped, this, &Editor::urlsDropped);

        new ote::ColorLabelsPlugin(m_textEditor);
        new ote::LatexPlugin(m_textEditor);
        new ote::BracketMatcherPlugin(m_textEditor);
    }

    QSharedPointer<Editor> Editor::getNewEditor(QWidget *parent)
    {
        return QSharedPointer<Editor>(getNewEditorUnmanagedPtr(parent), &Editor::deleteLater);
    }

    Editor *Editor::getNewEditorUnmanagedPtr(QWidget *parent)
    {
        return new Editor(parent);
    }

    void Editor::setFocus()
    {
        m_textEditor->setFocus();
    }

    void Editor::clearFocus()
    {
        m_textEditor->clearFocus();
    }

    /**
     * Automatically converts local relative file names to absolute ones.
     */
    void Editor::setFilePath(const QUrl &filename)
    {
        QUrl old = m_filePath;
        QUrl newUrl = filename;

        if (newUrl.isLocalFile())
            newUrl = QUrl::fromLocalFile(QFileInfo(filename.toLocalFile()).absoluteFilePath());

        m_filePath = newUrl;
        emit fileNameChanged(old, newUrl);
    }

    /**
     * Always returns an absolute url.
     */
    QUrl Editor::filePath() const
    {
        return m_filePath;
    }

    QString Editor::tabName() const
    {
        return m_tabName;
    }

    void Editor::setTabName(const QString& name)
    {
        m_tabName = name;
    }

    bool Editor::isClean()
    {
        return !m_textEditor->isModified();
    }

    void Editor::markClean()
    {
        m_textEditor->setModified(false);
    }

    void Editor::markDirty()
    {
        m_textEditor->setModified(true);
    }

    int Editor::getHistoryGeneration()
    {
        return m_textEditor->document()->revision();
    }

    void Editor::setLanguage(ote::Definition def)
    {
        // We use setLanguage() also when changing indentation modes, so this needs to be up here.
        if (!m_customIndentationMode)
            setIndentationMode(def);

        if (m_textEditor->getDefinition() == def)
            return;

        m_textEditor->setDefinition(def);
        emit currentLanguageChanged(def.name());
    }

    void Editor::setLanguage(const QString& language)
    {
        setLanguage(m_textEditor->getRepository().definitionForName(language));
    }

    void Editor::setLanguageFromFileName(const QString& fileName)
    {
        setLanguage(m_textEditor->getRepository().definitionForFileName(fileName));
    }

    void Editor::setLanguageFromFileName()
    {
        setLanguageFromFileName(filePath().toString());
    }

    void EditorNS::Editor::detectAndSetLanguage()
    {
        // First try to find a definition for the file's extension
        // FIXME: Second try to find a definition for the file's name
        // Third try to find a definition for the content
        auto def = m_textEditor->getRepository().definitionForFileName(m_filePath.toLocalFile());
        if (def.isValid()) {
            setLanguage(def);
            return;
        }

        def = m_textEditor->getRepository().definitionForContent(value());
        if (def.isValid()) {
            setLanguage(def);
            return;
        }

        setLanguage(ote::Definition());
    }

    void Editor::setIndentationMode(const ote::Definition& def)
    {
        const auto& s = NqqSettings::getInstance().Languages;
        const bool useDefaults = s.getUseDefaultSettings(def.name());
        const auto& langId = useDefaults ? "default" : def.name();

        return setIndentationMode(!s.getIndentWithSpaces(langId), s.getTabSize(langId));
    }

    void Editor::setIndentationMode(const bool useTabs, const int size)
    {
        m_textEditor->setTabWidth(size);
        m_textEditor->setTabToSpaces(!useTabs);
    }

    Editor::IndentationMode Editor::indentationMode()
    {
        auto tabWidth = m_textEditor->getConfig().tabWidth;
        auto usingSpaces = m_textEditor->getConfig().convertTabToSpaces;
        return Editor::IndentationMode{!usingSpaces, tabWidth};
    }

    void Editor::setCustomIndentationMode(const bool useTabs, const int size)
    {
        m_customIndentationMode = true;
        setIndentationMode(useTabs, size);
    }

    void Editor::setCustomIndentationMode(const bool useTabs)
    {
        m_customIndentationMode = true;
        setIndentationMode(useTabs, 0);
    }

    void Editor::clearCustomIndentationMode()
    {
        m_customIndentationMode = false;
        // setIndentationMode(getLanguage()); // FIXME
    }

    bool Editor::isUsingCustomIndentationMode() const
    {
        return m_customIndentationMode;
    }

    void EditorNS::Editor::setZoomLevel(int level)
    {
        m_textEditor->setZoomTo(level);
    }

    void EditorNS::Editor::zoomIn()
    {
        m_textEditor->zoomIn();
    }

    void EditorNS::Editor::zoomOut()
    {
        m_textEditor->zoomOut();
    }

    int EditorNS::Editor::getZoomLevel() const
    {
        return m_textEditor->getConfig().zoomLevel;
    }

    void Editor::setSmartIndent(bool enabled)
    {
        m_textEditor->setSmartIndent(enabled);
    }

    void Editor::setValue(const QString& value, bool undo)
    {        
        m_textEditor->setPlainText(value, undo);
    }

    QString Editor::value()
    {
        return m_textEditor->toPlainText();
    }

    bool Editor::fileOnDiskChanged() const
    {
        return m_fileOnDiskChanged;
    }

    void Editor::setFileOnDiskChanged(bool fileOnDiskChanged)
    {
        m_fileOnDiskChanged = fileOnDiskChanged;
    }

    void Editor::setSelectionsText(const QStringList &texts, SelectMode mode)
    {
        // FIXME doesn't care about other select modes
        m_textEditor->setTextInSelections(texts, mode == SelectMode::Selected);
    }

    void Editor::setSelectionsText(const QStringList &texts)
    {
        setSelectionsText(texts, SelectMode::After);
    }

    ote::Definition Editor::getLanguage() const
    {
        return m_textEditor->getDefinition();
    }

    void Editor::insertBanner(QWidget *banner)
    {
        m_layout->insertWidget(0, banner);
    }

    void Editor::removeBanner(QWidget *banner)
    {
        if (banner != m_textEditor && m_layout->indexOf(banner) >= 0) {
            m_layout->removeWidget(banner);
            emit bannerRemoved(banner);
        }
    }

    void Editor::removeBanner(QString objectName)
    {
        for (auto&& banner : findChildren<QWidget *>(objectName)) {
            removeBanner(banner);
        }
    }

    void Editor::setLineWrap(const bool wrap)
    {
        m_textEditor->setWordWrap(wrap);
    }

    void Editor::setEOLVisible(const bool showeol)
    {
        m_textEditor->setEndOfLineMarkersVisible(showeol);
    }

    void Editor::setWhitespaceVisible(const bool showspace)
    {
        m_textEditor->setWhitespaceVisible(showspace);
    }

    /*void Editor::setMathEnabled(const bool enabled)
    {
        // FIXME - Math functionality unfixable at the moment
        // asyncSendMessageWithResultP("C_CMD_ENABLE_MATH", enabled);
    }*/

    QPair<int, int> Editor::cursorPosition()
    {
        auto p = m_textEditor->getLineColumnForCursorPos(m_textEditor->getCursorPosition());
        return {p.first, p.second};
    }

    void Editor::setCursorPosition(const int line, const int column)
    {
        m_textEditor->setCursorPosition(line, column);
    }

    void Editor::setCursorPosition(const QPair<int, int> &position)
    {
        setCursorPosition(position.first, position.second);
    }

    void Editor::setCursorPosition(const Cursor &cursor)
    {
        setCursorPosition(cursor.line, cursor.column);
    }

    void Editor::setSelection(int fromLine, int fromCol, int toLine, int toCol)
    {
        ote::TextEdit::Selection s{m_textEditor->getCursorPosForLineColumn(fromLine, fromCol),
            m_textEditor->getCursorPosForLineColumn(toLine, toCol)};
        m_textEditor->setSelection(s);
    }

    QPair<int, int> Editor::scrollPosition()
    {
        auto p = m_textEditor->getScrollPosition();
        return {p.x(), p.y()};
    }

    void Editor::setScrollPosition(const int left, const int top)
    {
        m_textEditor->setScrollPosition(QPoint{left, top});
    }

    void Editor::setScrollPosition(const QPair<int, int> &position)
    {
        setScrollPosition(position.first, position.second);
    }

    QString Editor::endOfLineSequence() const
    {
        return m_endOfLineSequence;
    }

    void Editor::setEndOfLineSequence(const QString &newLineSequence)
    {
        m_endOfLineSequence = newLineSequence;
    }

    void Editor::setFont(const QFont& font)
    {
        m_textEditor->setFont(font);
    }

    QTextCodec *Editor::codec() const
    {
        return m_codec;
    }

    void Editor::setCodec(QTextCodec *codec)
    {
        m_codec = codec;
    }

    bool Editor::bom() const
    {
        return m_bom;
    }

    void Editor::setBom(bool bom)
    {
        m_bom = bom;
    }

    void Editor::setTheme(const ote::Theme& theme)
    {
        m_textEditor->setTheme(theme);
    }

    void Editor::setTheme(const QString& themeName)
    {
        const auto& theme = ote::TextEdit::getRepository().theme(themeName);
        m_textEditor->setTheme(theme);
    }

    QList<Editor::Selection> Editor::selections()
    {
        // TODO: Do we want to do this conversion?
        const auto& sels = m_textEditor->getSelections();
        QList<Editor::Selection> selList;

        for (const auto& sel : sels) {
            const auto& start = m_textEditor->getLineColumnForCursorPos(sel.start);
            const auto& end = m_textEditor->getLineColumnForCursorPos(sel.end);
            selList << Selection{{start.first, start.second}, {end.first, end.second}};
        }

        return selList;
    }

    QStringList Editor::selectedTexts()
    {
        return m_textEditor->getSelectedTexts();
    }

    void Editor::setOverwrite(bool overwrite)
    {
        m_textEditor->setOverwriteMode(overwrite);
    }

    std::pair<Editor::IndentationMode, bool> Editor::detectDocumentIndentation()
    {
        // This is basically a copy from App.js DETECT_INDENTATION function.
        QRegularExpression reg("([ ]{2,}|[\t]+)[^ \t]+?");
        QStringList lines = m_textEditor->toPlainText().split('\n', QString::SkipEmptyParts);
        int maxLines = 50;

        for (const auto& line : lines) {
            const auto it = reg.match(line);

            if (maxLines-- <= 0)
                break;

            if (it.hasMatch()) {
                const auto match = it.captured(1);

                if (match[0] == '\t')
                    return std::make_pair(Editor::IndentationMode{true, 0}, true);
                else {
                    int length = match.length();
                    if (length == 2 || length == 4 || length == 8)
                        return std::make_pair(Editor::IndentationMode{false, length}, true);
                    else
                        return std::make_pair(Editor::IndentationMode{false, 0}, false);
                }
            }
        }

        return std::make_pair(Editor::IndentationMode{0, 0}, false);
    }

    void Editor::print(std::shared_ptr<QPrinter> printer)
    {
        const auto& theme = m_textEditor->getTheme();

        m_textEditor->setTheme(ote::TextEdit::getRepository().theme("Printing"));
        m_textEditor->print(printer.get());
        m_textEditor->setTheme(theme);
    }

    QString Editor::getCurrentWord()
    {
        return m_textEditor->getCurrentWord();
    }

    int Editor::characterCount() const
    {
        return m_textEditor->getCharCount();
    }

    int Editor::lineCount()
    {
        return m_textEditor->getLineCount();
    }
}
