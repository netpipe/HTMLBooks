/*
Single-file Qt 5.12 C++ mini HTML viewer / local website browser
Save as: main.cpp

.pro file example (save as viewer.pro):

QT       += core gui widgets webenginewidgets
QT       += texttospeech      # optional; remove if not available

CONFIG += c++11

SOURCES += main.cpp

# On some platforms you may need to link additional libraries.

Build:
qmake viewer.pro
make

Usage:
Place index.html and its subpages in the same folder as the executable (or open a folder using File->Open Directory).
Run the app; Home loads index.html by default.

Notes:
- Uses QWebEngineView (Qt WebEngine). Make sure Qt was built with WebEngine support.
- Text-to-speech uses QTextToSpeech (Qt TextToSpeech module). If your Qt build doesn't include it, that section will still compile if the module is present; otherwise remove the QTextToSpeech parts or install the module.
- Search across subpages reads *.html, *.htm files under the loaded site directory and performs a case-insensitive text search within files (simple string search). It then lists results and allows opening.
- Printing uses QWebEnginePage::printToPdf and opens the generated PDF.
*/

#include <QApplication>
#include <QMainWindow>
#include <QToolBar>
#include <QAction>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QListWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QLabel>
#include <QKeySequence>
#include <QDesktopServices>
#include <QUrl>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QDebug>
#include <QComboBox>
#include <QtWebEngine/QtWebEngine>   // add this include at the top


#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWebEngineWidgets/QWebEnginePage>



#ifdef QT_TEXTTOSPEECH_LIB
#include <QTextToSpeech>
#endif

class MiniBrowser : public QMainWindow {
    Q_OBJECT
public:
    MiniBrowser(QWidget *parent = nullptr) : QMainWindow(parent) {
        setWindowTitle("HTML Reader");
        resize(1000, 700);

        webview = new QWebEngineView(this);

        // Central layout: left pane search/results, right is webview
        splitter = new QSplitter(this);

        QWidget *left = new QWidget(this);
        QVBoxLayout *lv = new QVBoxLayout(left);
        lv->setContentsMargins(4,4,4,4);

        searchEdit = new QLineEdit(this);
        searchEdit->setPlaceholderText("Search (press Enter)");
        connect(searchEdit, &QLineEdit::returnPressed, this, &MiniBrowser::onSearch);

        // scope toggle
        scopeLabel = new QLabel("Scope:", this);
        scopeCombo = new QComboBox(this);
        scopeCombo->addItem("Current page");
        scopeCombo->addItem("All subpages (folder)");

        QHBoxLayout *scopel = new QHBoxLayout();
        scopel->addWidget(scopeLabel);
        scopel->addWidget(scopeCombo);
        scopel->addStretch();

        resultsList = new QListWidget(this);
        resultsList->setSelectionMode(QAbstractItemView::SingleSelection);
        connect(resultsList, &QListWidget::itemActivated, this, &MiniBrowser::onResultActivated);

        lv->addWidget(searchEdit);
        lv->addLayout(scopel);
        lv->addWidget(resultsList);

        left->setLayout(lv);

        splitter->addWidget(left);
        splitter->addWidget(webview);
        splitter->setStretchFactor(1, 3);

        setCentralWidget(splitter);

        createToolbar();
        statusBar()->showMessage("Ready");

#ifdef QT_TEXTTOSPEECH_LIB
        tts = new QTextToSpeech(this);
#else
        tts = nullptr;
#endif

        // Default: home file path empty until user opens directory or file
        siteDir = QDir::currentPath()+"/book/";
        indexPath = QDir(siteDir).filePath("index.html");
        if (QFile::exists(indexPath)) {
            loadLocal(indexPath);
        }
              indexPath = QDir(siteDir).filePath("index.htm");
        if (QFile::exists(indexPath)) {
            loadLocal(indexPath);
        }
        // connect selection changed to update status
        connect(webview, &QWebEngineView::urlChanged, this, &MiniBrowser::onUrlChanged);
    }

private slots:
    void createToolbar() {
        QToolBar *tb = addToolBar("Navigation");

        QAction *backAct = tb->addAction("Back");
        backAct->setShortcut(QKeySequence::Back);
        connect(backAct, &QAction::triggered, webview, &QWebEngineView::back);

        QAction *forwardAct = tb->addAction("Forward");
        forwardAct->setShortcut(QKeySequence::Forward);
        connect(forwardAct, &QAction::triggered, webview, &QWebEngineView::forward);

        QAction *homeAct = tb->addAction("Home");
        homeAct->setShortcut(QKeySequence(Qt::CTRL + Qt::Key_H));
        connect(homeAct, &QAction::triggered, this, &MiniBrowser::onHome);

        tb->addSeparator();

        pathEdit = new QLineEdit(this);
        pathEdit->setPlaceholderText("Open file or folder path (press Enter)");
        pathEdit->setMinimumWidth(300);
        tb->addWidget(pathEdit);
        connect(pathEdit, &QLineEdit::returnPressed, this, &MiniBrowser::onOpenPath);

        QAction *openFileAct = tb->addAction("Open File...");
        connect(openFileAct, &QAction::triggered, this, &MiniBrowser::onOpenFileDialog);

        QAction *openDirAct = tb->addAction("Open Directory...");
        connect(openDirAct, &QAction::triggered, this, &MiniBrowser::onOpenDirDialog);

        tb->addSeparator();

        QAction *printAct = tb->addAction("Print");
        printAct->setShortcut(QKeySequence::Print);
        connect(printAct, &QAction::triggered, this, &MiniBrowser::onPrint);

        QAction *readSelectionAct = tb->addAction("Read Selection");
        connect(readSelectionAct, &QAction::triggered, this, &MiniBrowser::onReadSelection);

        QAction *readPageAct = tb->addAction("Read Page");
        connect(readPageAct, &QAction::triggered, this, &MiniBrowser::onReadPage);

        QAction *stopAct = tb->addAction("Stop");
        connect(stopAct, &QAction::triggered, webview, &QWebEngineView::stop);

        QAction *reloadAct = tb->addAction("Reload");
        reloadAct->setShortcut(QKeySequence::Refresh);
        connect(reloadAct, &QAction::triggered, webview, &QWebEngineView::reload);
    }

    void onHome() {
        // Try index.html first
        QString idx = QDir(siteDir).filePath("index.html");
        if (!QFile::exists(idx)) {
            // Try index.htm fallback
            idx = QDir(siteDir).filePath("index.htm");
        }

        if (QFile::exists(idx)) {
            loadLocal(idx);
            return;
        }

        // If no index file, fall back to first HTML/HTM file in folder
        QDir dir(siteDir);
        QStringList filters;
        filters << "*.html" << "*.htm";
        QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);

        if (!files.isEmpty()) {
            idx = dir.filePath(files.first());
            loadLocal(idx);
            return;
        }

        QMessageBox::information(this, "Home not found",
                                 QString("No index.html, index.htm, or other HTML files found in %1")
                                 .arg(siteDir));
    }


    void onOpenPath() {
        QString p = pathEdit->text().trimmed();
        if (p.isEmpty()) return;
        QFileInfo fi(p);
        if (fi.isDir()) {
            setSiteDir(fi.absoluteFilePath());
        } else if (fi.isFile()) {
            setSiteDir(fi.absolutePath());
            loadLocal(fi.absoluteFilePath());
        } else {
            QMessageBox::warning(this, "Invalid path", "The path is not a file or directory.");
        }
    }

    void onOpenFileDialog() {
        QString f = QFileDialog::getOpenFileName(this, "Open HTML file", siteDir, "HTML Files (*.html *.htm);;All Files (*)");
        if (!f.isEmpty()) {
            QFileInfo fi(f);
            setSiteDir(fi.absolutePath());
            loadLocal(f);
            pathEdit->setText(f);
        }
    }

    void onOpenDirDialog() {
        QString d = QFileDialog::getExistingDirectory(this, "Open Directory", siteDir);
        if (!d.isEmpty()) {
            setSiteDir(d);
            pathEdit->setText(d);
            // try loading index.html
            QString idx = QDir(d).filePath("index.html");
            if (!QFile::exists(idx)) {
                idx = QDir(d).filePath("index.htm");   // check .htm fallback
            }
            if (QFile::exists(idx)) {
                loadLocal(idx);
            } else {
                QMessageBox::information(this, "No index file",
                                         QString("No index.html or index.htm found in %1").arg(d));
            }

        }
    }

    void setSiteDir(const QString &d) {
        siteDir = d;
        indexPath = QDir(siteDir).filePath("index.html");
        statusBar()->showMessage(QString("Site directory: %1").arg(siteDir));
    }

    void loadLocal(const QString &filePath) {
        QUrl url = QUrl::fromLocalFile(QFileInfo(filePath).absoluteFilePath());
        webview->load(url);
        statusBar()->showMessage(QString("Loaded: %1").arg(filePath));
    }

    void onSearch() {
        QString term = searchEdit->text().trimmed();
        if (term.isEmpty()) return;
        resultsList->clear();
        if (scopeCombo->currentText().startsWith("Current")) {
            // search within current page via findText
            webview->page()->findText(QString(), QWebEnginePage::FindFlag::FindBackward); // clear previous
            webview->page()->findText(term, QWebEnginePage::FindFlag::FindCaseSensitively, [this,term](bool found){
                if(found) {
                    statusBar()->showMessage(QString("Found on page: %1").arg(term));
                } else {
                    statusBar()->showMessage(QString("No matches for '%1' on current page").arg(term));
                }
            });
        } else {
            // search all HTML files in siteDir
            QDir dir(siteDir);
            QStringList filters; filters << "*.html" << "*.htm";
            QFileInfoList files = dir.entryInfoList(filters, QDir::Files | QDir::NoSymLinks, QDir::Name);
            // also include subdirectories recursively
            files = recursiveFindHtml(siteDir);
            bool any=false;
            for (const QFileInfo &fi: files) {
                QFile f(fi.absoluteFilePath());
                if (!f.open(QIODevice::ReadOnly|QIODevice::Text)) continue;
                QTextStream in(&f);
                QString contents = in.readAll();
                f.close();
                if (contents.contains(term, Qt::CaseInsensitive)) {
                    QListWidgetItem *it = new QListWidgetItem(QString("%1 — %2").arg(fi.fileName(), fi.absoluteFilePath()));
                    it->setData(Qt::UserRole, fi.absoluteFilePath());
                    resultsList->addItem(it);
                    any=true;
                }
            }
            if (!any) statusBar()->showMessage(QString("No matches for '%1' in site directory").arg(term));
            else statusBar()->showMessage("Search complete");
        }
    }

    QFileInfoList recursiveFindHtml(const QString &dirPath) {
        QFileInfoList results;
        QDir dir(dirPath);
        for (const QFileInfo &fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            results += recursiveFindHtml(fi.absoluteFilePath());
        }
        QStringList filters; filters << "*.html" << "*.htm";
        results += dir.entryInfoList(filters, QDir::Files | QDir::NoSymLinks, QDir::Name);
        return results;
    }

    void onResultActivated(QListWidgetItem *item) {
        QString file = item->data(Qt::UserRole).toString();
        if (!file.isEmpty()) loadLocal(file);
    }

    void onPrint() {
        // print to PDF then open
        QString tmp = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QString pdfPath = QDir(tmp).filePath("mini_browser_print.pdf");
//        webview->page()->printToPdf(pdfPath, [this,pdfPath](bool ok){
//            if (ok) {
//                statusBar()->showMessage(QString("Saved PDF: %1").arg(pdfPath));
//                QDesktopServices::openUrl(QUrl::fromLocalFile(pdfPath));
//            } else {
//                QMessageBox::warning(this, "Print failed", "Failed to create PDF for printing.");
//            }
      //  });
    }

    void onReadSelection() {
#ifdef QT_TEXTTOSPEECH_LIB
        if (!tts) { QMessageBox::information(this, "TTS not available", "Text-to-speech not available in this build."); return; }
        webview->page()->runJavaScript("window.getSelection().toString();", [this](const QVariant &v){
            QString text = v.toString();
            if (text.isEmpty()) {
                QMessageBox::information(this, "No selection", "Please select text in the page to read.");
                return;
            }
            tts->say(text);
        });
#else
        QMessageBox::information(this, "TTS not available", "Text-to-speech support was not compiled in. Rebuild with Qt TextToSpeech module.");
#endif
    }

    void onReadPage() {
#ifdef QT_TEXTTOSPEECH_LIB
        if (!tts) { QMessageBox::information(this, "TTS not available", "Text-to-speech not available in this build."); return; }
        // grab innerText of body
        webview->page()->runJavaScript("(function(){return document.body ? document.body.innerText : document.documentElement.innerText; })();", [this](const QVariant &v){
            QString text = v.toString();
            if (text.isEmpty()) { QMessageBox::information(this, "Nothing to read", "Page contains no readable text."); return; }
            tts->say(text);
        });
#else
        QMessageBox::information(this, "TTS not available", "Text-to-speech support was not compiled in. Rebuild with Qt TextToSpeech module.");
#endif
    }

    void onUrlChanged(const QUrl &url) {
        statusBar()->showMessage(QString("URL: %1").arg(url.toString()));
    }

private:
    QSplitter *splitter;
    QWebEngineView *webview;
    QLineEdit *searchEdit;
    QListWidget *resultsList;
    QComboBox *scopeCombo;
    QLabel *scopeLabel;
    QLineEdit *pathEdit;
    QString siteDir;
    QString indexPath;

#ifdef QT_TEXTTOSPEECH_LIB
    QTextToSpeech *tts;
#else
    QObject *tts; // placeholder
#endif
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // Required for Qt WebEngine
   // QtWebEngine::initialize();

    MiniBrowser w;
    w.show();
    return app.exec();
}

#include "main.moc"
