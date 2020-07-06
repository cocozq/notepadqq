#ifndef EDITOR_H
#define EDITOR_H

#include <QObject>
#include <QWidget>
#include <QQueue>
#include <QTextCodec>
#include <QVBoxLayout>
#include <QVariant>
#include <QWheelEvent>
#include <QUrl>
#include <QPrinter>


namespace ote {
class TextEdit;
}

#include "ote/Highlighter/definition.h"
#include "ote/Highlighter/theme.h"


class EditorTabWidget;

namespace EditorNS
{
    class Editor : public QWidget
    {
        Q_OBJECT
    public:

        explicit Editor(QWidget *parent = 0);

        static QSharedPointer<Editor> getNewEditor(QWidget *parent = 0);
        static Editor *getNewEditorUnmanagedPtr(QWidget *parent);

        struct Cursor {
            int line;
            int column;

            bool operator == (const Cursor &x) const {
                return line == x.line && column == x.column;
            }

            bool operator < (const Cursor &x) const {
                return std::tie(line, column) < std::tie(x.line, x.column);
            }

            bool operator <= (const Cursor &x) const {
                return *this == x || *this < x;
            }

            bool operator > (const Cursor &x) const {
                return !(*this <= x);
            }

            bool operator >= (const Cursor &x) const {
                return !(*this < x);
            }
        };

        struct Selection {
            Cursor from;
            Cursor to;
        };

        struct IndentationMode {
            bool useTabs;
            int size;
        };

        /**
             * @brief Give focus to the editor, so that the user can start
             *        typing. Note that calling won't automatically switch to
             *        the tab where the editor is. Use EditorTabWidget::setCurrentIndex()
             *        and TopEditorContainer::setFocus() for that.
             */
        Q_INVOKABLE void setFocus();

        /**
             * @brief Remove the focus from the editor.
             */
        Q_INVOKABLE void clearFocus();

        /**
             * @brief Set the file name associated with this editor
             * @param filename full path of the file
             */
        Q_INVOKABLE void setFilePath(const QUrl &filename);

        /**
             * @brief Get the file name associated with this editor
             * @return
             */
        Q_INVOKABLE QUrl filePath() const;

        Q_INVOKABLE bool fileOnDiskChanged() const;
        Q_INVOKABLE void setFileOnDiskChanged(bool fileOnDiskChanged);

        enum class SelectMode {
            Before,
            After,
            Selected
        };

        void insertBanner(QWidget *banner);
        void removeBanner(QWidget *banner);
        void removeBanner(QString objectName);

        // Lower-level message wrappers:
        Q_INVOKABLE bool isClean();
        Q_INVOKABLE void markClean();
        Q_INVOKABLE void markDirty();

        /**
         * @brief Returns an integer that denotes the editor's history state. Making changes to
         *        the contents increments the integer while reverting changes decrements it again.
         */
        Q_INVOKABLE int getHistoryGeneration();

        /**
         * @brief Set the language to use for the editor.
         *        It automatically adjusts tab settings from
         *        the default configuration for the specified language.
         * @param language Language id
         */
        Q_INVOKABLE void setLanguage(ote::Definition def);
        Q_INVOKABLE void setLanguage(const QString &language);
        Q_INVOKABLE void setLanguageFromFileName(const QString& filePath);
        Q_INVOKABLE void setLanguageFromFileName();
        void detectAndSetLanguage();
        Q_INVOKABLE void setValue(const QString &value, bool undo = false);
        Q_INVOKABLE QString value();

        /**
         * @brief Set custom indentation settings which may be different
         *        from the default tab settings associated with the current
         *        language.
         *        If this method is called, further calls to setLanguage()
         *        will NOT modify these tab settings. Use
         *        clearCustomIndentationMode() to reset to default settings.
         * @param useTabs
         * @param size Size of an indentation. If 0, keeps the current one.
         */
        void setCustomIndentationMode(const bool useTabs, const int size);
        void setCustomIndentationMode(const bool useTabs);
        void clearCustomIndentationMode();
        bool isUsingCustomIndentationMode() const;

        int getZoomLevel() const;
        void zoomOut();
        void zoomIn();
        void setZoomLevel(int level);

        Q_INVOKABLE void setSmartIndent(bool enabled);
        Q_INVOKABLE void setSelectionsText(const QStringList &texts, SelectMode mode);
        Q_INVOKABLE void setSelectionsText(const QStringList &texts);
        ote::Definition getLanguage() const;
        Q_INVOKABLE void setLineWrap(const bool wrap);
        Q_INVOKABLE void setEOLVisible(const bool showeol);
        Q_INVOKABLE void setWhitespaceVisible(const bool showspace);
        //Q_INVOKABLE void setMathEnabled(const bool enabled);

        void setFont(const QFont& font);

        /**
         * @brief Get the current cursor position
         * @return a <line, column> pair.
         */
        QPair<int, int> cursorPosition();
        void setCursorPosition(const int line, const int column);
        void setCursorPosition(const QPair<int, int> &position);
        void setCursorPosition(const Cursor &cursor);

        /**
         * @brief Get the current scroll position
         * @return a <left, top> pair.
         */
        QPair<int, int> scrollPosition();
        void setScrollPosition(const int left, const int top);
        void setScrollPosition(const QPair<int, int> &position);
        QString endOfLineSequence() const;
        void setEndOfLineSequence(const QString &endOfLineSequence);

        QTextCodec *codec() const;

        /**
         * @brief Set the codec for this Editor.
         *        This method does not change the in-memory or on-screen
         *        representation of the document (which is always Unicode).
         *        It serves solely as a way to keep track of the encoding
         *        that needs to be used when the document gets saved.
         * @param codec
         */
        void setCodec(QTextCodec *codec);

        bool bom() const;
        void setBom(bool bom);

        void setTheme(const ote::Theme& theme);
        void setTheme(const QString& themeName);

        ote::TextEdit* textEditor() { return m_textEditor; }

        QList<Selection> selections();

        /**
         * @brief Returns the currently selected texts.
         * @return
         */
        Q_INVOKABLE QStringList selectedTexts();

        void setOverwrite(bool overwrite);

        /**
         * @brief Detect the indentation mode used within the current document.
         * @return a pair whose first element is the document indentation, that is
         *         significative only if the second element ("found") is true.
         */
        std::pair<IndentationMode, bool> detectDocumentIndentation();
        Editor::IndentationMode indentationMode();

        QString getCurrentWord();

        void setSelection(int fromLine, int fromCol, int toLine, int toCol);

        int lineCount();

        int characterCount() const;

    private:
        friend class ::EditorTabWidget;

        // These functions should only be used by EditorTabWidget to manage the tab's title. This works around
        // KDE's habit to automatically modify QTabWidget's tab titles to insert shortcut sequences (like &1).
        QString tabName() const;
        void setTabName(const QString& name);

        QVBoxLayout *m_layout;

        ote::TextEdit* m_textEditor;

        QUrl m_filePath = QUrl();
        QString m_tabName;
        bool m_fileOnDiskChanged = false;
        bool m_loaded = false;
        QString m_endOfLineSequence = "\n";
        QTextCodec *m_codec = QTextCodec::codecForName("UTF-8");
        bool m_bom = false;
        bool m_customIndentationMode = false;

        void fullConstructor(const ote::Theme& theme);

        void setIndentationMode(const bool useTabs, const int size);
        void setIndentationMode(const ote::Definition& def);

    signals:
        void gotFocus();
        void mouseWheel(QWheelEvent *ev);
        void urlsDropped(QList<QUrl> urls);
        void bannerRemoved(QWidget *banner);

        // Pre-interpreted messages:
        void contentChanged();
        void cursorActivity();
        void cleanChanged(bool isClean);
        void fileNameChanged(const QUrl &oldFileName, const QUrl &newFileName);

        /**
             * @brief The editor finished loading. There should be
             *        no need to use this signal outside this class.
             */
        void editorReady();

        void currentLanguageChanged(QString name);

    public slots:
        void print(std::shared_ptr<QPrinter> printer);
    };

}

#endif // EDITOR_H
