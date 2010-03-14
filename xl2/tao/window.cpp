// ****************************************************************************
//  window.cpp                                                     Tao project
// ****************************************************************************
// 
//   File Description:
// 
//     The main Tao output window
// 
// 
// 
// 
// 
// 
// 
// 
// ****************************************************************************
// This document is released under the GNU General Public License.
// See http://www.gnu.org/copyleft/gpl.html and Matthew 25:22 for details
//  (C) 1992-2010 Christophe de Dinechin <christophe@taodyne.com>
//  (C) 2010 Taodyne SAS
// ****************************************************************************

#include <QtGui>
#include "window.h"
#include "widget.h"

#include <iostream>
#include <sstream>
#include <string>

#include <menuinfo.h>
#include <bfs.h>
#include <QList>
#include <QRegExp>

TAO_BEGIN

Window::Window(XL::Main *xlr, XL::SourceFile *sf)
// ----------------------------------------------------------------------------
//    Create a Tao window with default parameters
// ----------------------------------------------------------------------------
    : xlRuntime(xlr), xlProgram(sf),
      textEdit(NULL), taoWidget(NULL),
      isUntitled(sf == NULL)
{
    // Create the widgets
    QDockWidget *dock = new QDockWidget(tr("Source"));
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    textEdit = new QTextEdit(dock);
    dock->setWidget(textEdit);
    addDockWidget(Qt::RightDockWidgetArea, dock);

    taoWidget = new Widget(this, sf);
    setCentralWidget(taoWidget);

    // Create menus, actions, stuff
    createActions();
    createMenus();
    createToolBars();
    createStatusBar();
    connect(textEdit->document(), SIGNAL(contentsChanged()),
            this, SLOT(documentWasModified()));

    // Set the window attributes
    setAttribute(Qt::WA_DeleteOnClose);
    readSettings();
    setUnifiedTitleAndToolBarOnMac(true);
    if (sf)
        loadFile(QString::fromStdString(sf->name));
    else
        setCurrentFile("");
}


void Window::closeEvent(QCloseEvent *event)
{
    if (maybeSave()) {
        writeSettings();
        event->accept();
    } else {
        event->ignore();
    }
}

void Window::newFile()
{
    Window *other = new Window(xlRuntime, NULL);
    other->move(x() + 40, y() + 40);
    other->show();
}

void Window::open()
{
    QString fileName = QFileDialog::getOpenFileName
        (this,
         tr("Open Tao Document"),
         tr(""),
         tr("Tao documents (*.ddd);;XL programs (*.xl);;"
            "Headers (*.dds *.xs);;All files (*.*)"));
    if (!fileName.isEmpty()) {
        Window *existing = findWindow(fileName);
        if (existing) {
            existing->show();
            existing->raise();
            existing->activateWindow();
            return;
        }

        if (isUntitled && textEdit->document()->isEmpty()
                && !isWindowModified()) {
            loadFile(fileName);
        } else {
            text fn = fileName.toStdString();
            xlRuntime->LoadFile(fn);
            XL::SourceFile &sf = xlRuntime->files[fn];
            Window *other = new Window(xlRuntime, &sf);
            if (other->isUntitled) {
                delete other;
                return;
            }
            other->move(x() + 40, y() + 40);
            other->show();
        }
    }
}

bool Window::save()
{
    if (isUntitled) {
        return saveAs();
    } else {
        return saveFile(curFile);
    }
}

bool Window::saveAs()
{
    QString fileName = QFileDialog::getSaveFileName(this, tr("Save As"),
                                                    curFile);
    if (fileName.isEmpty())
        return false;

    return saveFile(fileName);
}

void Window::about()
{
    kstring txt =
        "<b>Tao</b>, an interactive collaboration tool.<br/>"
        "Brought to you by Taodyne SAS:<br/>"
        "Anne Lempereur<br/>"
        "Catherine Burvelle<br/>"
        "Jérôme Forissier<br/>"
        "Lionel Schaffhauser<br/>"
        "Christophe de Dinechin.";
   QMessageBox::about (this, tr("About Tao"), tr(txt));
}

void Window::documentWasModified()
{
    setWindowModified(true);
}

void Window::createActions()
{
    newAct = new QAction(QIcon(":/images/new.png"), tr("&New"), this);
    newAct->setShortcuts(QKeySequence::New);
    newAct->setStatusTip(tr("Create a new file"));
    connect(newAct, SIGNAL(triggered()), this, SLOT(newFile()));

    openAct = new QAction(QIcon(":/images/open.png"), tr("&Open..."), this);
    openAct->setShortcuts(QKeySequence::Open);
    openAct->setStatusTip(tr("Open an existing file"));
    connect(openAct, SIGNAL(triggered()), this, SLOT(open()));

    saveAct = new QAction(QIcon(":/images/save.png"), tr("&Save"), this);
    saveAct->setShortcuts(QKeySequence::Save);
    saveAct->setStatusTip(tr("Save the document to disk"));
    connect(saveAct, SIGNAL(triggered()), this, SLOT(save()));

    saveAsAct = new QAction(tr("Save &As..."), this);
    saveAsAct->setShortcuts(QKeySequence::SaveAs);
    saveAsAct->setStatusTip(tr("Save the document under a new name"));
    connect(saveAsAct, SIGNAL(triggered()), this, SLOT(saveAs()));

    closeAct = new QAction(tr("&Close"), this);
    closeAct->setShortcut(tr("Ctrl+W"));
    closeAct->setStatusTip(tr("Close this window"));
    connect(closeAct, SIGNAL(triggered()), this, SLOT(close()));

    exitAct = new QAction(tr("E&xit"), this);
    exitAct->setShortcuts(QKeySequence::Quit);
    exitAct->setStatusTip(tr("Exit the application"));
    connect(exitAct, SIGNAL(triggered()), qApp, SLOT(closeAllWindows()));

    cutAct = new QAction(QIcon(":/images/cut.png"), tr("Cu&t"), this);
    cutAct->setShortcuts(QKeySequence::Cut);
    cutAct->setStatusTip(tr("Cut the current selection's contents to the "
                            "clipboard"));
    connect(cutAct, SIGNAL(triggered()), textEdit, SLOT(cut()));

    copyAct = new QAction(QIcon(":/images/copy.png"), tr("&Copy"), this);
    copyAct->setShortcuts(QKeySequence::Copy);
    copyAct->setStatusTip(tr("Copy the current selection's contents to the "
                             "clipboard"));
    connect(copyAct, SIGNAL(triggered()), textEdit, SLOT(copy()));

    pasteAct = new QAction(QIcon(":/images/paste.png"), tr("&Paste"), this);
    pasteAct->setShortcuts(QKeySequence::Paste);
    pasteAct->setStatusTip(tr("Paste the clipboard's contents into the current "
                              "selection"));
    connect(pasteAct, SIGNAL(triggered()), textEdit, SLOT(paste()));

    aboutAct = new QAction(tr("&About"), this);
    aboutAct->setStatusTip(tr("Show the application's About box"));
    connect(aboutAct, SIGNAL(triggered()), this, SLOT(about()));

    aboutQtAct = new QAction(tr("About &Qt"), this);
    aboutQtAct->setStatusTip(tr("Show the Qt library's About box"));
    connect(aboutQtAct, SIGNAL(triggered()), qApp, SLOT(aboutQt()));


    cutAct->setEnabled(false);
    copyAct->setEnabled(false);
    connect(textEdit, SIGNAL(copyAvailable(bool)),
            cutAct, SLOT(setEnabled(bool)));
    connect(textEdit, SIGNAL(copyAvailable(bool)),
            copyAct, SLOT(setEnabled(bool)));
}

//! [implicit tr context]
void Window::createMenus()
{
    fileMenu = menuBar()->addMenu(tr("&File"));
//! [implicit tr context]
    fileMenu->addAction(newAct);
    fileMenu->addAction(openAct);
    fileMenu->addAction(saveAct);
    fileMenu->addAction(saveAsAct);
    fileMenu->addSeparator();
    fileMenu->addAction(closeAct);
    fileMenu->addAction(exitAct);

    editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(cutAct);
    editMenu->addAction(copyAct);
    editMenu->addAction(pasteAct);

    menuBar()->addSeparator();

    helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(aboutAct);
    helpMenu->addAction(aboutQtAct);
}

void Window::createToolBars()
{
//! [0]
    fileToolBar = addToolBar(tr("File"));
    fileToolBar->addAction(newAct);
    fileToolBar->addAction(openAct);
//! [0]
    fileToolBar->addAction(saveAct);

    editToolBar = addToolBar(tr("Edit"));
    editToolBar->addAction(cutAct);
    editToolBar->addAction(copyAct);
    editToolBar->addAction(pasteAct);
}

void Window::createStatusBar()
{
    statusBar()->showMessage(tr("Ready"));
}

void Window::readSettings()
{
    QSettings settings;
    QPoint pos = settings.value("pos", QPoint(200, 200)).toPoint();
    QSize size = settings.value("size", QSize(400, 400)).toSize();
    move(pos);
    resize(size);
}

void Window::writeSettings()
{
    QSettings settings;
    settings.setValue("pos", pos());
    settings.setValue("size", size());
}

bool Window::maybeSave()
{
    if (textEdit->document()->isModified()) {
	QMessageBox::StandardButton ret;
        ret = QMessageBox::warning(this, tr("SDI"),
                     tr("The document has been modified.\n"
                        "Do you want to save your changes?"),
                     QMessageBox::Save | QMessageBox::Discard
		     | QMessageBox::Cancel);
        if (ret == QMessageBox::Save)
            return save();
        else if (ret == QMessageBox::Cancel)
            return false;
    }
    return true;
}

void Window::loadFile(const QString &fileName)
{

    QFile file(fileName);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("Cannot read file"),
                             tr("Cannot read file %1:\n%2.")
                             .arg(fileName)
                             .arg(file.errorString()));
        return;
    }

    QTextStream in(&file);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    textEdit->setPlainText(in.readAll());
    QApplication::restoreOverrideCursor();

    setCurrentFile(fileName);
    statusBar()->showMessage(tr("File loaded"), 2000);

    // Clean menus
    resetTaoMenus();

    text fn = fileName.toStdString();
    xlRuntime->LoadFile(fn);
    xlProgram = &xlRuntime->files[fn];
    taoWidget->xlProgram = xlProgram;
    taoWidget->updateGL();
}

bool Window::saveFile(const QString &fileName)
{
    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("SDI"),
                             tr("Cannot write file %1:\n%2.")
                             .arg(fileName)
                             .arg(file.errorString()));
        return false;
    }

    do
    {
        QTextStream out(&file);
        QApplication::setOverrideCursor(Qt::WaitCursor);
        out << textEdit->toPlainText();
        QApplication::restoreOverrideCursor();
    } while (0);                // Flush

    setCurrentFile(fileName);
    statusBar()->showMessage(tr("File saved"), 2000);

    // Clean menus
    resetTaoMenus();

    text fn = fileName.toStdString();
    xlRuntime->LoadFile(fn);
    xlProgram = &xlRuntime->files[fn];
    taoWidget->xlProgram = xlProgram;
    taoWidget->updateGL();

    return true;
}
void Window::resetTaoMenus()
{
    // Clean menus
    if (taoWidget->xlProgram)
    {
        QRegExp reg("^_TOP_MENU_.*", Qt::CaseSensitive);
        QList<QMenu *> menu_list = menuBar()->findChildren<QMenu *>(reg);
        QList<QMenu *>::iterator it;
        for(it = menu_list.begin(); it!=menu_list.end(); ++it)
        {
            QMenu *menu = *it;
            std::cout << menu->objectName().toStdString()
                    << " removed from menu bar \n";
            menuBar()->removeAction(menu->menuAction());
            delete menu;
        }
        taoWidget->currentMenu = NULL;
        taoWidget->currentMenuBar = this->menuBar();
    }

}

void Window::setCurrentFile(const QString &fileName)
{
    static int sequenceNumber = 1;

    isUntitled = fileName.isEmpty();
    if (isUntitled) {
        curFile = tr("document%1.ddd").arg(sequenceNumber++);
    } else {
        curFile = QFileInfo(fileName).canonicalFilePath();
    }

    textEdit->document()->setModified(false);
    setWindowModified(false);
    setWindowFilePath(curFile);
}

QString Window::strippedName(const QString &fullFileName)
{
    return QFileInfo(fullFileName).fileName();
}

Window *Window::findWindow(const QString &fileName)
{
    QString canonicalFilePath = QFileInfo(fileName).canonicalFilePath();

    foreach (QWidget *widget, qApp->topLevelWidgets()) {
        Window *mainWin = qobject_cast<Window *>(widget);
        if (mainWin && mainWin->curFile == canonicalFilePath)
            return mainWin;
    }
    return 0;
}

void Window::updateProgram(XL::Tree *tree)
{
    if ( ! tree) return ;
//    std::cerr << "Updating program with " << tree << "\n";
    std::ostringstream oss;
    oss << tree << "\n";
    textEdit->setPlainText(QString::fromStdString(oss.str()));
    // save();
}

TAO_END

