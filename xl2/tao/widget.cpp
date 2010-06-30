// ****************************************************************************
//  widget.cpp							   Tao project
// ****************************************************************************
//
//   File Description:
//
//     The main widget used to display some Tao stuff
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
//  (C) 2010 Lionel Schaffhauser <lionel@taodyne.com>
//  (C) 2010 Catherine Burvelle <cathy@taodyne.com>
//  (C) 2010 Jérôme Forissier <jerome@taodyne.com>
//  (C) 2010 Taodyne SAS
// ****************************************************************************

#include "widget.h"
#include "tao.h"
#include "main.h"
#include "runtime.h"
#include "opcodes.h"
#include "gl_keepers.h"
#include "frame.h"
#include "texture.h"
#include "svg.h"
#include "widget_surface.h"
#include "window.h"
#include "apply_changes.h"
#include "activity.h"
#include "selection.h"
#include "drag.h"
#include "manipulator.h"
#include "menuinfo.h"
#include "repository.h"
#include "application.h"
#include "tao_utf8.h"
#include "layout.h"
#include "page_layout.h"
#include "space_layout.h"
#include "shapes.h"
#include "text_drawing.h"
#include "shapes3d.h"
#include "path3d.h"
#include "table.h"
#include "attributes.h"
#include "transforms.h"
#include "undo.h"
#include "serializer.h"
#include "binpack.h"
#include "normalize.h"
#include "error_message_dialog.h"
#include "group_layout.h"
#include "font.h"
#include "objloader.h"
#include "tree_cloning.h"
#include "gl2ps.h"

#include <QApplication>
#include <QToolButton>
#include <QtGui/QImage>
#include <cmath>
#include <QFont>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <QVariant>
#include <QtWebKit>
#include <sys/time.h>
#include <sys/stat.h>

#define TAO_CLIPBOARD_MIME_TYPE "application/tao-clipboard"
namespace TaoFormulas { void EnterFormulas(XL::Symbols *syms); }

TAO_BEGIN

// ============================================================================
//
//   Widget life management
//
// ============================================================================

double Widget::zNear = 2000.0;
double Widget::zFar = 40000.0;


Widget::Widget(Window *parent, XL::SourceFile *sf)
// ----------------------------------------------------------------------------
//    Create the GL widget
// ----------------------------------------------------------------------------
    : QGLWidget(QGLFormat(QGL::SampleBuffers|QGL::AlphaChannel), parent),
      xlProgram(sf),
      symbolTableForFormulas(new XL::Symbols(NULL)),
      symbolTableRoot(new XL::Name("formula_symbol_table")),
      inError(false), mustUpdateDialogs(false),
      space(NULL), layout(NULL), path(NULL), table(NULL),
      pageName(""),
      pageId(0), pageFound(0), pageShown(1), pageTotal(1),
      pageTree(NULL),
      drawAllPages(false),
      currentShape(NULL),
      currentGridLayout(NULL),
      currentGroup(NULL), fontFileMgr(NULL), activities(NULL),
      id(0), charId(0), capacity(1), manipulator(0),
      wasSelected(false), selectionChanged(false),
      w_event(NULL), focusWidget(NULL), focusId(0), keyboardModifiers(0),
      currentMenu(NULL), currentMenuBar(NULL),currentToolBar(NULL),
      orderedMenuElements(QVector<MenuInfo*>(10, NULL)), order(0),
      colorAction(NULL), fontAction(NULL),
      lastMouseX(0), lastMouseY(0), lastMouseButtons(0),
      timer(this), idleTimer(this),
      pageStartTime(1e6), pageRefresh(1e6), frozenTime(1e6), startTime(1e6),
      tmin(~0ULL), tmax(0), tsum(0), tcount(0),
      nextSave(now()), nextCommit(nextSave), nextSync(nextSave),
      nextPull(nextSave), animated(true),
      currentFileDialog(NULL),
      zoom(1.0),
      eyeX(0.0), eyeY(0.0), eyeZ(Widget::zNear),
      centerX(0.0), centerY(0.0), centerZ(0.0),
      autoSaveEnabled(true)
{
    // Make sure we don't fill background with crap
    setAutoFillBackground(false);

    // Make this the current context for OpenGL
    makeCurrent();

    // Create the main page we draw on
    space = new SpaceLayout(this);
    layout = space;

    // Prepare the timers
    connect(&timer, SIGNAL(timeout()), this, SLOT(updateGL()));
    connect(&idleTimer, SIGNAL(timeout()), this, SLOT(dawdle()));
    idleTimer.start(100);

    // Receive notifications for focus
    connect(qApp, SIGNAL(focusChanged (QWidget *, QWidget *)),
            this,  SLOT(appFocusChanged(QWidget *, QWidget *)));
    setFocusPolicy(Qt::StrongFocus);

    // Prepare the menubar
    currentMenuBar = parent->menuBar();
    connect(parent->menuBar(),  SIGNAL(triggered(QAction*)),
            this,               SLOT(userMenu(QAction*)));

    toDialogLabel["LookIn"]   = (QFileDialog::DialogLabel)QFileDialog::LookIn;
    toDialogLabel["FileName"] = (QFileDialog::DialogLabel)QFileDialog::FileName;
    toDialogLabel["FileType"] = (QFileDialog::DialogLabel)QFileDialog::FileType;
    toDialogLabel["Accept"]   = (QFileDialog::DialogLabel)QFileDialog::Accept;
    toDialogLabel["Reject"]   = (QFileDialog::DialogLabel)QFileDialog::Reject;

    // Connect the symbol table for formulas
    symbolTableRoot->SetSymbols(symbolTableForFormulas);
    TaoFormulas::EnterFormulas(symbolTableForFormulas);

    // Select format for source file view
    srcRenderer = new XL::Renderer(srcRendererOutput);
    QFileInfo stylesheet("xl:srcview.stylesheet");
    srcRenderer->SelectStyleSheet(+stylesheet.canonicalFilePath());

    // Make sure we get mouse events even when no click is made
    setMouseTracking(true);
    new Identify("Focus Rectangle", this);
}


Widget::~Widget()
// ----------------------------------------------------------------------------
//   Destroy the widget
// ----------------------------------------------------------------------------
{
    delete space;
    delete path;
    delete srcRenderer;
}

// ============================================================================
//
//   Slots
//
// ============================================================================

void Widget::dawdle()
// ----------------------------------------------------------------------------
//   Operations to do when idle (in the background)
// ----------------------------------------------------------------------------
{
    if (!xlProgram)
        return;

    // Check if this is the first time we go idle or if time wrapped up
    if (pageStartTime > CurrentTime())
        pageRefresh = pageStartTime = startTime = frozenTime = CurrentTime();

    // Run all activities, which will get them a chance to update refresh
    for (Activity *a = activities; a; a = a->Idle()) ;

    // We will only auto-save and commit if we have a valid repository
    Repository *repo           = repository();
    XL::Main   *xlr            = XL::MAIN;

    // Check if we need to refresh something
    double currentTime = CurrentTime();
    double idleInterval = 0.001 * idleTimer.interval();
    double timerInterval = 0.001 * timer.interval();
    double remaining = pageRefresh - currentTime;
    if (!timer.isActive() ||
        remaining <= timerInterval || remaining <= idleInterval)
    {
        if (remaining <= 0)
            remaining = 0.001;
        timer.stop();
        timer.setSingleShot(true);
        timer.start(1000 * remaining);
    }

    if (xlProgram->changed && xlProgram->readOnly)
    {
        updateProgramSource();
        if (!repo)
            xlProgram->changed = false;
    }

    // Check if it's time to save
    ulonglong tick = now();
    longlong saveDelay = longlong(nextSave - tick);
    if (repo && saveDelay < 0 && repo->idle() && autoSaveEnabled)
    {
        doSave(tick);
    }

    // Check if it's time to commit
    longlong commitDelay = longlong (nextCommit - tick);
    if (repo && commitDelay < 0 && repo->state == Repository::RS_NotClean &&
        autoSaveEnabled)
    {
        doCommit(tick);
    }

    // Check if it's time to merge from the remote repository
    // REVISIT: sync: what if several widgets share the same repository?
    longlong pullDelay = longlong (nextPull - tick);
    if (repo && pullDelay < 0 && repo->state == Repository::RS_Clean)
    {
        doPull(tick);
    }

    // Check if it's time to reload
    longlong syncDelay = longlong(nextSync - tick);
    if (syncDelay < 0)
    {
        refreshProgram();
        syncDelay = tick + xlr->options.sync_interval * 1000;
    }
}


void Widget::draw()
// ----------------------------------------------------------------------------
//    Redraw the widget
// ----------------------------------------------------------------------------
{
    TaoSave saveCurrent(current, this);

    // Timing
    ulonglong before = now();
    w_event = NULL;

    // Setup the initial drawing environment
    double w = width(), h = height();
    setup(w, h);
    pageW = (21.0 / 2.54) * logicalDpiX(); // REVISIT
    pageH = (29.7 / 2.54) * logicalDpiY();
    flowName = "";
    flows.clear();
    pageId = 0;
    pageFound = 0;
    pageTree = NULL;
    lastPageName = "";

    // Clear the background
    glClearColor (1.0, 1.0, 1.0, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    Layout::polygonOffset = 0;

    // Make sure we compile the selection the first time
    static bool first = true;
    if (first)
    {
        XL::Symbols *s = xlProgram->symbols;
        double x = 0;
        (XL::XLCall("draw_selection"), x,x,x,x).build(s);
        (XL::XLCall("draw_selection"), x,x,x,x,x,x).build(s);
        (XL::XLCall("draw_widget_selection"), x,x,x,x).build(s);
        (XL::XLCall("draw_widget_selection"), x,x,x,x,x,x).build(s);
        (XL::XLCall("draw_3D_selection"), x,x,x,x,x,x).build(s);
        (XL::XLCall("draw_handle"), x, x, x).build(s);
        (XL::XLCall("draw_control_point_handle"), x, x, x).build(s);
        first = false;
    }

    // If there is a program, we need to run it
    pageRefresh = CurrentTime() + 86400;        // 24 hours
    runProgram();

    // Check if we want to refresh something
    ulonglong after = now();
    double remaining = pageRefresh - CurrentTime();
    if (remaining < 0.001)
        remaining = 0.001;
    timer.setSingleShot(true);
    timer.start(1000 * remaining);

    // Timing
    elapsed(before, after);

    // Render all activities, e.g. the selection rectangle
    SpaceLayout selectionSpace(this);
    XL::LocalSave<Layout *> saveLayout(layout, &selectionSpace);
    glDisable(GL_DEPTH_TEST);
    for (Activity *a = activities; a; a = a->Display()) ;
    selectionSpace.Draw(NULL);
    glEnable(GL_DEPTH_TEST);

    // Update page count for next run
    pageTotal = pageId ? pageId : 1;
    if (pageFound)
        pageShown = pageFound;
    else
        pageName = "";

    // If we must update dialogs, do it now
    if (mustUpdateDialogs)
    {
        mustUpdateDialogs = false;
        if (colorDialog)
            updateColorDialog();
        if (fontDialog)
            updateFontDialog();
    }

    if (selectionChanged)
    {
        Window *window = (Window *) parentWidget();
        // TODO: honoring isReadOnly involves more than just this
        if (!window->isReadOnly)
            updateProgramSource();
        selectionChanged = false;
    }
}


void Widget::runProgram()
// ----------------------------------------------------------------------------
//   Run the current XL program
// ----------------------------------------------------------------------------
{
    XL::GarbageCollector::Collect();

    // Don't run anything if we detected errors running previously
    if (inError)
        return;

    // Reset the selection id for the various elements being drawn
    focusWidget = NULL;

    // Run the XL program associated with this widget
    QTextOption alignCenter(Qt::AlignCenter);
    IFTRACE(memory)
        std::cerr << "Run, Drawing::count = " << space->count << ", ";
    space->Clear();
    IFTRACE(memory)
        std::cerr << "cleared, count = " << space->count << ", ";
    XL::LocalSave<Layout *> saveLayout(layout, space);
    id = charId = 0;

    // Evaluate the program
    XL::MAIN->EvalContextFiles(((Window*)parent())->contextFileNames);

    if (Tree *prog = xlProgram->tree)
        xl_evaluate(prog);

    // Clean the end of the old menu list.
    for  ( ; order < orderedMenuElements.count(); order++)
    {
        delete orderedMenuElements[order];
        orderedMenuElements[order] = NULL;
    }

    // Reset the order value.
    order          = 0;
    currentMenu    = NULL;
    currentToolBar = NULL;
    currentMenuBar = ((Window*)parent())->menuBar();

    // Remember how many elements are drawn on the page, plus arbitrary buffer
    if (id + charId + 100 > capacity)
        capacity = id + charId + 100;
    else if (id + charId + 50 < capacity / 2)
        capacity = capacity / 2;

    // After we are done, draw the space with all the drawings in it
    id = charId = 0;
    space->Draw(NULL);
    IFTRACE(memory)
        std::cerr << "Draw, count = " << space->count << "\n";
    id = charId = 0;
    selectionTrees.clear();
    space->offset.Set(0,0,0);
    space->DrawSelection(NULL);

    // Clipboard management
    checkCopyAvailable();
}


void Widget::identifySelection()
// ----------------------------------------------------------------------------
//   Draw the elements in global space for selection purpose
// ----------------------------------------------------------------------------
{
    id = charId = 0;
    space->offset.Set(0,0,0);
    space->Identify(NULL);
}


void Widget::updateSelection()
// ----------------------------------------------------------------------------
//   Redraw selection in order to perform text editing operations
// ----------------------------------------------------------------------------
{
    id = charId = 0;
    selectionTrees.clear();
    space->offset.Set(0,0,0);
    space->DrawSelection(NULL);
}


static void printWidget(QWidget *w)
// ----------------------------------------------------------------------------
//   Print a widget for debugging purpose
// ----------------------------------------------------------------------------
{
    printf("%p", w);
    if (w)
        printf(" (%s)", w->metaObject()->className());
}



void Widget::appFocusChanged(QWidget *prev, QWidget *next)
// ----------------------------------------------------------------------------
//   Notifications when focus changes
// ----------------------------------------------------------------------------
{
    IFTRACE(focus)
    {
        printf("Focus "); printWidget(prev); printf ("->"); printWidget(next);
        const QObjectList &children = this->children();
        QObjectList::const_iterator it;
        printf("\nChildren:");
        for (it = children.begin(); it != children.end(); it++)
        {
            printf(" ");
            printWidget((QWidget *) *it);
        }
        printf("\n");
    }
}



void Widget::checkCopyAvailable()
// ----------------------------------------------------------------------------
//   Emit a signal when clipboard can copy or cut something (or cannot anymore)
// ----------------------------------------------------------------------------
{
    bool isSelected = selected();
    if (wasSelected != isSelected)
    {
        emit copyAvailable(isSelected);
        wasSelected = isSelected;
    }
}


bool Widget::canPaste()
// ----------------------------------------------------------------------------
//   Is current clibpoard data in a suitable format to be pasted?
// ----------------------------------------------------------------------------
{
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();
    return (mimeData->hasFormat(TAO_CLIPBOARD_MIME_TYPE));
}


void Widget::cut()
// ----------------------------------------------------------------------------
//   Cut current selection into clipboard
// ----------------------------------------------------------------------------
{
    copy();
    IFTRACE(clipboard)
        std::cerr << "Clipboard: deleting selection\n";
    deleteSelection();
}


void Widget::copy()
// ----------------------------------------------------------------------------
//   Copy current selection into clipboard
// ----------------------------------------------------------------------------
{
    if (!hasSelection())
        return;

    // Build a single tree from all the selected sub-trees
    XL::Tree *tree = copySelection();

    IFTRACE(clipboard)
    {
        std::cerr << "Clipboard: copying:\n";
        XL::Renderer render(std::cerr);
        render.SelectStyleSheet("debug.stylesheet");
        render.Render(tree);
    }

    // Serialize the tree
    std::string ser;
    std::ostringstream ostr;
    XL::Serializer serializer(ostr);
    tree->Do(serializer);
    ser += ostr.str();

    // Encapsulate serialized tree as MIME data
    QByteArray binData;
    binData.append(ser.data(), ser.length());
    QMimeData *mimeData = new QMimeData;
    mimeData->setData(TAO_CLIPBOARD_MIME_TYPE, binData);

    // Transfer into clipboard
    QClipboard *clipboard = QApplication::clipboard();
    clipboard->setMimeData(mimeData);
}


void Widget::paste()
// ----------------------------------------------------------------------------
//   Paste the clipboard content at the current selection
// ----------------------------------------------------------------------------
{
    // Does clipboard contain Tao stuff?)
    if (!canPaste())
        return;

    // Read clipboard content
    const QMimeData *mimeData = QApplication::clipboard()->mimeData();

    // Extract serialized tree
    QByteArray binData = mimeData->data(TAO_CLIPBOARD_MIME_TYPE);
    std::string ser(binData.data(), binData.length());

    // De-serialize
    std::istringstream istr(ser);
    XL::Deserializer deserializer(istr);
    XL::Tree *tree = deserializer.ReadTree();
    if (!deserializer.IsValid())
        return;

    IFTRACE(clipboard)
    {
        std::cerr << "Clipboard: pasting:\n";
        XL::Renderer render(std::cerr);
        render.SelectStyleSheet("debug.stylesheet");
        render.Render(tree);
    }

    // Insert tree at end of current page
    // TODO: paste with an offset to avoid exactly overlapping objects
    insert(NULL, tree);

}


Name_p Widget::bringToFront(Tree_p /*self*/)
// ----------------------------------------------------------------------------
//   Bring the selected shape to front
// ----------------------------------------------------------------------------
{
    Tree * select = removeSelection();
    if ( ! select )
        return XL::xl_false;

    insert(NULL, select, "Selection brought to front");
    return XL::xl_true;
}


Name_p Widget::sendToBack(Tree_p /*self*/)
// ----------------------------------------------------------------------------
//   Send the selected shape to back
// ----------------------------------------------------------------------------
{
    Tree * select = removeSelection();
    if ( ! select )
        return XL::xl_false;
    // Make sure the new objects appear selected next time they're drawn
    selectStatements(select);

    // Start at the top of the program to find where we will insert
    Tree_p *top = &xlProgram->tree;
    Tree *page = pageTree;

    // If we have a current page, insert only in that context
    if (page)
    {
        // Restrict insertion to that page
        top = &pageTree;

        // The page instructions often runs a 'do' block
        if (Prefix *prefix = page->AsPrefix())
            if (Name *left = prefix->left->AsName())
                if (left->value == "do")
                    top = &prefix->right;

        // If the page code is a block, look inside
        if (XL::Block *block = (*top)->AsBlock())
            top = &block->child;
    }

    Symbols *symbols = (*top)->Symbols();
    *top = new XL::Infix("\n", select, *top);
    (*top)->SetSymbols(symbols);

    // Reload the program and mark the changes
    reloadProgram();
    markChanged("Selection sent to back");

    return XL::xl_true;
}

Name_p Widget::bringForward(Tree_p /*self*/)
// ----------------------------------------------------------------------------
//   Swap the selected shape and the one in front of it
// ----------------------------------------------------------------------------
{
    if (!hasSelection())
        return XL::xl_false;

    std::set<Tree_p >::iterator sel = selectionTrees.begin();
    XL::FindParentAction getParent(*sel);
    Tree * parent = xlProgram->tree->Do(getParent);
    // Check if we are not the only one
    if (!parent)
        return XL::xl_false;
    Infix * current = parent->AsInfix();
    if ( !current )
        return XL::xl_false;

    Tree * tmp =  NULL;
    Infix * next = current->right->AsInfix();
    if ( !next )
    {
        // We are at the bottom of the tree
        //Check if we are already the latest
        if (current->right == *sel)
            return XL::xl_false;

        // just swap left and right of parent.
        tmp = current->left;
        current->left = current->right;
        current->right = tmp;
    }
    else
    {
        tmp = current->left;
        current->left = next->left;
        next->left = tmp;
    }
    selectStatements(tmp);
    // Reload the program and mark the changes
    reloadProgram();
    markChanged("Selection brought forward");
    return XL::xl_true;
}


Name_p Widget::sendBackward(Tree_p /*self*/)
// ----------------------------------------------------------------------------
//   Swap the selected shape and the one just behind it
// ----------------------------------------------------------------------------
{
    if (!hasSelection())
        return XL::xl_false;

    std::set<Tree_p >::iterator sel = selectionTrees.begin();
    XL::FindParentAction getParent(*sel);
    Tree * parent = xlProgram->tree->Do(getParent);
    // Check if we are not the only one
    if (!parent)
        return XL::xl_false;
    Infix * current = parent->AsInfix();
    if ( !current )
        return XL::xl_false;

     Tree * tmp = NULL;
    // check if we are at the bottom of the tree
    if (current->right == *sel)
    {
        tmp = current->right;
        current->right = current->left;
        current->left = tmp;
    }
    else
    {
        XL::FindParentAction getGrandParent(parent);
        Tree * grandParent = xlProgram->tree->Do(getGrandParent);
        // No grand parent means the shape is already to back
        if (!grandParent)
            return XL::xl_false;
        Infix * previous = grandParent->AsInfix();
        if ( !previous )
            return XL::xl_false;

        tmp = current->left;
        current->left = previous->left;
        previous->left = tmp;
    }
    selectStatements(tmp);
    // Reload the program and mark the changes
    reloadProgram();
    markChanged("Selection sent backward");
    return XL::xl_true;
}


bool Widget::selectionsEqual(selection_map &s1, selection_map &s2)
// ----------------------------------------------------------------------------
//   Compare selections
// ----------------------------------------------------------------------------
//   We can't use operator== because we only compare keys, not values
{
    Widget::selection_map::iterator i;
    for (i = s1.begin(); i != s1.end(); i++)
        if ((*i).second)
            if (!s2.count((*i).first) || !s2[(*i).first])
                return false;
    for (i = s2.begin(); i != s2.end(); i++)
        if ((*i).second)
            if (!s1.count((*i).first) || !s1[(*i).first])
                return false;
    return true;
}


QStringList Widget::fontFiles()
// ----------------------------------------------------------------------------
//   Return the paths of all font files used in the document
// ----------------------------------------------------------------------------
{
    struct FFM {
        FFM(FontFileManager *&m): m(m) { m = new FontFileManager(); }
       ~FFM()                          { delete m; m = NULL; }
       FontFileManager *&m;
    } ffm(fontFileMgr);

    drawAllPages = true;
    draw();
    drawAllPages = false;
    draw();
    if (!fontFileMgr->errors.empty())
    {
        // Some font files are not in a suitable format, so we won't try to
        // embed them (Qt can only load TrueType, TrueType Collection and
        // OpenType files).
        Window *window = (Window *) parentWidget();
        foreach (QString m, fontFileMgr->errors)
            window->addError(m);
    }
    return fontFileMgr->fontFiles;
}


void Widget::enableAnimations(bool enable)
// ----------------------------------------------------------------------------
//   Enable or disable animations on the page
// ----------------------------------------------------------------------------
{
    animated = enable;
    frozenTime = CurrentTime();
}


void Widget::showHandCursor(bool enabled)
// ----------------------------------------------------------------------------
//   Switch panning mode on/off
// ----------------------------------------------------------------------------
{
    if (enabled)
        setCursor(Qt::OpenHandCursor);
    else
        setCursor(Qt::ArrowCursor);
}


void Widget::resetView()
// ----------------------------------------------------------------------------
//   Restore default view parameters (zoom, position etc.)
// ----------------------------------------------------------------------------
{
    zoom = 1.0;
    eyeX = 0.0;
    eyeY = 0.0;
    eyeZ = Widget::zNear;
    centerX = 0.0;
    centerY = 0.0;
    centerZ = 0.0;
    setup(width(), height());
    updateGL();
}


void Widget::saveAndCommit()
// ----------------------------------------------------------------------------
//   Save files and commit to repository if needed
// ----------------------------------------------------------------------------
{
    ulonglong tick = now();
    if (doSave(tick))
        doCommit(tick);
}


void Widget::userMenu(QAction *p_action)
// ----------------------------------------------------------------------------
//   User menu slot activation
// ----------------------------------------------------------------------------
{
    if (!p_action)
        return;

    IFTRACE(menus)
        std::cout << "Action " << p_action->objectName().toStdString()
                  << " (" << p_action->text().toStdString() << ") activated\n";

    QVariant var =  p_action->data();
    if (!var.isValid())
        return;

    TaoSave saveCurrent(current, this);
    XL::Tree *t = var.value<XL::Tree_p>();
    xl_evaluate(t);        // Typically will insert something...
}


bool Widget::refresh(double delay)
// ----------------------------------------------------------------------------
//   Refresh the screen after the given time interval
// ----------------------------------------------------------------------------
{
    double end = CurrentTime() + delay;
    if (pageRefresh > end)
    {
        pageRefresh = end;
        return true;
    }
    return false;
}


void Widget::commitSuccess(QString id, QString msg)
// ----------------------------------------------------------------------------
//   Document was succesfully committed to repository (see doCommit())
// ----------------------------------------------------------------------------
{
    Window *window = (Window *) parentWidget();
    window->undoStack->push(new UndoCommand(repository(), id, msg));
}



// ============================================================================
//
//   OpenGL setup
//
// ============================================================================

void Widget::initializeGL()
// ----------------------------------------------------------------------------
//    Called once per rendering to setup the GL environment
// ----------------------------------------------------------------------------
{
}


void Widget::resizeGL(int width, int height)
// ----------------------------------------------------------------------------
//   Called when the size changes
// ----------------------------------------------------------------------------
{
    space->space = Box3(-width/2, -height/2, 0, width, height, 0);
    tmax = tsum = tcount = 0;
    tmin = ~tmax;
}


void Widget::paintGL()
// ----------------------------------------------------------------------------
//    Repaint the contents of the window
// ----------------------------------------------------------------------------
{
    draw();
    showGlErrors();
}


void Widget::setup(double w, double h, Box *picking)
// ----------------------------------------------------------------------------
//   Setup an initial environment for drawing
// ----------------------------------------------------------------------------
{
    // Setup viewport
    glViewport(0, 0, w, h);

    // Setup the projection matrix
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    // Restrict the picking area if any is given as input
    if (picking)
    {
        GLint viewport[4] = { 0, 0, w, h };
        Box b = *picking;
        b.Normalize();
        Vector size = b.upper - b.lower;
        Point center = b.lower + size / 2;
        gluPickMatrix(center.x, center.y, size.x+1, size.y+1, viewport);
    }

    // Setup the frustum for the projection
    double zNear = Widget::zNear, zFar = Widget::zFar;
    double upX = 0.0, upY = 1.0, upZ = 0.0;
    glFrustum ((-w/2)*zoom, (w/2)*zoom, (-h/2)*zoom, (h/2)*zoom, zNear, zFar);
    gluLookAt(eyeX, eyeY, eyeZ, centerX, centerY, centerZ, upX, upY, upZ);
    glTranslatef(0.0, 0.0, -zNear);
    glScalef(2.0, 2.0, 2.0);

    // Setup the model view matrix so that 1.0 unit = 1px
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Reset default GL parameters
    setupGL();
}


void Widget::setupGL()
// ----------------------------------------------------------------------------
//   Setup default GL parameters
// ----------------------------------------------------------------------------
{
    // Setup other
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LINE_SMOOTH);
    glEnable(GL_POINT_SMOOTH);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glEnable(GL_POLYGON_OFFSET_POINT);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    glLineWidth(1);
    glLineStipple(1, -1);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_TEXTURE_RECTANGLE_ARB);
    glDisable(GL_CULL_FACE);
}


uint Widget::showGlErrors()
// ----------------------------------------------------------------------------
//   Display all OpenGL errors in the error window
// ----------------------------------------------------------------------------
{
    static GLenum last = GL_NO_ERROR;
    static unsigned int count = 0;
    GLenum err = glGetError();
    uint errorsFound = 0;
    while (err != GL_NO_ERROR)
    {
        std::ostringstream cerr;
        errorsFound++;
        if (!count)
        {
            cerr << "GL Error: " << (char *) gluErrorString(err)
                 << " [error code: " << err << "]";
            last = err;
        }
        if (err != last || count == 100)
        {
            cerr << "GL Error: error " << last << " repeated "
                 << count << " times";
            count = 0;
        }
        else
        {
            count++;
        }
        text msg = cerr.str();
        if (msg.length())
        {
            Window *window = (Window *) parentWidget();
            window->addError(+msg);
        }
        err = glGetError();
    }

    return errorsFound;
}



// ============================================================================
//
//   Widget basic events (painting, mause, ...)
//
// ============================================================================

bool Widget::forwardEvent(QEvent *event)
// ----------------------------------------------------------------------------
//   Forward event to the focus proxy if there is any
// ----------------------------------------------------------------------------
{
    if (QObject *focus = focusWidget)
        return focus->event(event);

    return false;
}


bool Widget::forwardEvent(QMouseEvent *event)
// ----------------------------------------------------------------------------
//   Forward event to the focus proxy if there is any, adjusting coordinates
// ----------------------------------------------------------------------------
{
    if (QObject *focus = focusWidget)
    {
        int x = event->x();
        int y = event->y();
        int w = focusWidget->width();
        int h = focusWidget->height();

        Point3 u = unproject(x, y, 0);
        int nx = u.x + w/2;
        int ny = h/2 - u.y;
        QMouseEvent local(event->type(), QPoint(nx, ny ),
                          event->button(), event->buttons(),
                          event->modifiers());
        IFTRACE(widgets)
        {
            std::cerr << "forwardEvent::Event type "<< event->type()
                    << " Event->x="<<nx <<" Event->y="<< ny
                    << " focusWidget name " << +(focus->objectName())
                    << std::endl;
        }

        return focus->event(&local);
    }
    return false;
}


static text keyName(QKeyEvent *event)
// ----------------------------------------------------------------------------
//   Return the properly formatted key name for a key event
// ----------------------------------------------------------------------------
{
    // Try to find if there is a callback in the code for this key
    text name = +event->text();
    text ctrl = "";             // Name for Control, Meta and Alt

    uint key = (uint) event->key();
    switch(key)
    {
    case Qt::Key_Space:                 name = "Space"; break;
    case Qt::Key_A:                     ctrl = "A"; break;
    case Qt::Key_B:                     ctrl = "B"; break;
    case Qt::Key_C:                     ctrl = "C"; break;
    case Qt::Key_D:                     ctrl = "D"; break;
    case Qt::Key_E:                     ctrl = "E"; break;
    case Qt::Key_F:                     ctrl = "F"; break;
    case Qt::Key_G:                     ctrl = "G"; break;
    case Qt::Key_H:                     ctrl = "H"; break;
    case Qt::Key_I:                     ctrl = "I"; break;
    case Qt::Key_J:                     ctrl = "J"; break;
    case Qt::Key_K:                     ctrl = "K"; break;
    case Qt::Key_L:                     ctrl = "L"; break;
    case Qt::Key_M:                     ctrl = "M"; break;
    case Qt::Key_N:                     ctrl = "N"; break;
    case Qt::Key_O:                     ctrl = "O"; break;
    case Qt::Key_P:                     ctrl = "P"; break;
    case Qt::Key_Q:                     ctrl = "Q"; break;
    case Qt::Key_R:                     ctrl = "R"; break;
    case Qt::Key_S:                     ctrl = "S"; break;
    case Qt::Key_T:                     ctrl = "T"; break;
    case Qt::Key_U:                     ctrl = "U"; break;
    case Qt::Key_V:                     ctrl = "V"; break;
    case Qt::Key_W:                     ctrl = "W"; break;
    case Qt::Key_X:                     ctrl = "X"; break;
    case Qt::Key_Y:                     ctrl = "Y"; break;
    case Qt::Key_Z:                     ctrl = "Z"; break;
    case Qt::Key_0:                     ctrl = "0"; break;
    case Qt::Key_1:                     ctrl = "1"; break;
    case Qt::Key_2:                     ctrl = "2"; break;
    case Qt::Key_3:                     ctrl = "3"; break;
    case Qt::Key_4:                     ctrl = "4"; break;
    case Qt::Key_5:                     ctrl = "5"; break;
    case Qt::Key_6:                     ctrl = "6"; break;
    case Qt::Key_7:                     ctrl = "7"; break;
    case Qt::Key_8:                     ctrl = "8"; break;
    case Qt::Key_9:                     ctrl = "9"; break;
    case Qt::Key_Exclam:                ctrl = "!"; break;
    case Qt::Key_QuoteDbl:              ctrl = "\""; break;
    case Qt::Key_NumberSign:            ctrl = "#"; break;
    case Qt::Key_Dollar:                ctrl = "$"; break;
    case Qt::Key_Percent:               ctrl = "%"; break;
    case Qt::Key_Ampersand:             ctrl = "&"; break;
    case Qt::Key_Apostrophe:            ctrl = "'"; break;
    case Qt::Key_ParenLeft:             ctrl = "("; break;
    case Qt::Key_ParenRight:            ctrl = ")"; break;
    case Qt::Key_Asterisk:              ctrl = "*"; break;
    case Qt::Key_Plus:                  ctrl = "+"; break;
    case Qt::Key_Comma:                 ctrl = ","; break;
    case Qt::Key_Minus:                 ctrl = "-"; break;
    case Qt::Key_Period:                ctrl = "."; break;
    case Qt::Key_Slash:                 ctrl = "/"; break;
    case Qt::Key_Colon:                 ctrl = ":"; break;
    case Qt::Key_Semicolon:             ctrl = ";"; break;
    case Qt::Key_Less:                  ctrl = "<"; break;
    case Qt::Key_Equal:                 ctrl = "="; break;
    case Qt::Key_Greater:               ctrl = ">"; break;
    case Qt::Key_Question:              ctrl = "?"; break;
    case Qt::Key_At:                    ctrl = "@"; break;
    case Qt::Key_BracketLeft:           ctrl = "["; break;
    case Qt::Key_Backslash:             ctrl = "\\"; break;
    case Qt::Key_BracketRight:          ctrl = "]"; break;
    case Qt::Key_AsciiCircum:           ctrl = "^"; break;
    case Qt::Key_Underscore:            ctrl = "_"; break;
    case Qt::Key_QuoteLeft:             ctrl = "`"; break;
    case Qt::Key_BraceLeft:             ctrl = "{"; break;
    case Qt::Key_Bar:                   ctrl = "|"; break;
    case Qt::Key_BraceRight:            ctrl = "}"; break;
    case Qt::Key_AsciiTilde:            ctrl = "~"; break;
    case Qt::Key_Escape:                name = "Escape"; break;
    case Qt::Key_Tab:                   name = "Tab"; break;
    case Qt::Key_Backtab:               name = "Backtab"; break;
    case Qt::Key_Backspace:             name = "Backspace"; break;
    case Qt::Key_Return:                name = "Return"; break;
    case Qt::Key_Enter:                 name = "Enter"; break;
    case Qt::Key_Insert:                name = "Insert"; break;
    case Qt::Key_Delete:                name = "Delete"; break;
    case Qt::Key_Pause:                 name = "Pause"; break;
    case Qt::Key_Print:                 name = "Print"; break;
    case Qt::Key_SysReq:                name = "SysReq"; break;
    case Qt::Key_Clear:                 name = "Clear"; break;
    case Qt::Key_Home:                  name = "Home"; break;
    case Qt::Key_End:                   name = "End"; break;
    case Qt::Key_Left:                  name = "Left"; break;
    case Qt::Key_Up:                    name = "Up"; break;
    case Qt::Key_Right:                 name = "Right"; break;
    case Qt::Key_Down:                  name = "Down"; break;
    case Qt::Key_PageUp:                name = "PageUp"; break;
    case Qt::Key_PageDown:              name = "PageDown"; break;
    case Qt::Key_Shift:                 name = ""; break;
    case Qt::Key_Control:               name = ""; break;
    case Qt::Key_Meta:                  name = ""; break;
    case Qt::Key_Alt:                   name = ""; break;
    case Qt::Key_AltGr:                 name = "AltGr"; break;
    case Qt::Key_CapsLock:              name = "CapsLock"; break;
    case Qt::Key_NumLock:               name = "NumLock"; break;
    case Qt::Key_ScrollLock:            name = "ScrollLock"; break;
    case Qt::Key_F1:                    name = "F1"; break;
    case Qt::Key_F2:                    name = "F2"; break;
    case Qt::Key_F3:                    name = "F3"; break;
    case Qt::Key_F4:                    name = "F4"; break;
    case Qt::Key_F5:                    name = "F5"; break;
    case Qt::Key_F6:                    name = "F6"; break;
    case Qt::Key_F7:                    name = "F7"; break;
    case Qt::Key_F8:                    name = "F8"; break;
    case Qt::Key_F9:                    name = "F9"; break;
    case Qt::Key_F10:                   name = "F10"; break;
    case Qt::Key_F11:                   name = "F11"; break;
    case Qt::Key_F12:                   name = "F12"; break;
    case Qt::Key_F13:                   name = "F13"; break;
    case Qt::Key_F14:                   name = "F14"; break;
    case Qt::Key_F15:                   name = "F15"; break;
    case Qt::Key_F16:                   name = "F16"; break;
    case Qt::Key_F17:                   name = "F17"; break;
    case Qt::Key_F18:                   name = "F18"; break;
    case Qt::Key_F19:                   name = "F19"; break;
    case Qt::Key_F20:                   name = "F20"; break;
    case Qt::Key_F21:                   name = "F21"; break;
    case Qt::Key_F22:                   name = "F22"; break;
    case Qt::Key_F23:                   name = "F23"; break;
    case Qt::Key_F24:                   name = "F24"; break;
    case Qt::Key_F25:                   name = "F25"; break;
    case Qt::Key_F26:                   name = "F26"; break;
    case Qt::Key_F27:                   name = "F27"; break;
    case Qt::Key_F28:                   name = "F28"; break;
    case Qt::Key_F29:                   name = "F29"; break;
    case Qt::Key_F30:                   name = "F30"; break;
    case Qt::Key_F31:                   name = "F31"; break;
    case Qt::Key_F32:                   name = "F32"; break;
    case Qt::Key_F33:                   name = "F33"; break;
    case Qt::Key_F34:                   name = "F34"; break;
    case Qt::Key_F35:                   name = "F35"; break;
    case Qt::Key_Menu:                  name = "Menu"; break;
    case Qt::Key_Help:                  name = "Help"; break;
    case Qt::Key_Back:                  name = "Back"; break;
    case Qt::Key_Forward:               name = "Forward"; break;
    case Qt::Key_Stop:                  name = "Stop"; break;
    case Qt::Key_Refresh:               name = "Refresh"; break;
    case Qt::Key_VolumeDown:            name = "VolumeDown"; break;
    case Qt::Key_VolumeMute:            name = "VolumeMute"; break;
    case Qt::Key_VolumeUp:              name = "VolumeUp"; break;
    case Qt::Key_BassBoost:             name = "BassBoost"; break;
    case Qt::Key_BassUp:                name = "BassUp"; break;
    case Qt::Key_BassDown:              name = "BassDown"; break;
    case Qt::Key_TrebleUp:              name = "TrebleUp"; break;
    case Qt::Key_TrebleDown:            name = "TrebleDown"; break;
    case Qt::Key_MediaPlay:             name = "MediaPlay"; break;
    case Qt::Key_MediaStop:             name = "MediaStop"; break;
    case Qt::Key_MediaPrevious:         name = "MediaPrevious"; break;
    case Qt::Key_MediaNext:             name = "MediaNext"; break;
    case Qt::Key_MediaRecord:           name = "MediaRecord"; break;
    case Qt::Key_HomePage:              name = "HomePage"; break;
    case Qt::Key_Favorites:             name = "Favorites"; break;
    case Qt::Key_Search:                name = "Search"; break;
    case Qt::Key_Standby:               name = "Standby"; break;
    case Qt::Key_OpenUrl:               name = "OpenUrl"; break;
    case Qt::Key_LaunchMail:            name = "LaunchMail"; break;
    case Qt::Key_LaunchMedia:           name = "LaunchMedia"; break;
    case Qt::Key_Launch0:               name = "Launch0"; break;
    case Qt::Key_Launch1:               name = "Launch1"; break;
    case Qt::Key_Launch2:               name = "Launch2"; break;
    case Qt::Key_Launch3:               name = "Launch3"; break;
    case Qt::Key_Launch4:               name = "Launch4"; break;
    case Qt::Key_Launch5:               name = "Launch5"; break;
    case Qt::Key_Launch6:               name = "Launch6"; break;
    case Qt::Key_Launch7:               name = "Launch7"; break;
    case Qt::Key_Launch8:               name = "Launch8"; break;
    case Qt::Key_Launch9:               name = "Launch9"; break;
    case Qt::Key_LaunchA:               name = "LaunchA"; break;
    case Qt::Key_LaunchB:               name = "LaunchB"; break;
    case Qt::Key_LaunchC:               name = "LaunchC"; break;
    case Qt::Key_LaunchD:               name = "LaunchD"; break;
    case Qt::Key_LaunchE:               name = "LaunchE"; break;
    case Qt::Key_LaunchF:               name = "LaunchF"; break;
    case Qt::Key_MonBrightnessUp:       name = "MonBrightnessUp"; break;
    case Qt::Key_MonBrightnessDown:     name = "MonBrightnessDown"; break;
    case Qt::Key_KeyboardLightOnOff:    name = "KeyboardLightOnOff"; break;
    case Qt::Key_KeyboardBrightnessUp:  name = "KeyboardBrightnessUp"; break;
    case Qt::Key_KeyboardBrightnessDown:name = "KeyboardBrightnessDown"; break;
    case Qt::Key_PowerOff:              name = "PowerOff"; break;
    case Qt::Key_WakeUp:                name = "WakeUp"; break;
    case Qt::Key_Eject:                 name = "Eject"; break;
    case Qt::Key_ScreenSaver:           name = "ScreenSaver"; break;
    case Qt::Key_WWW:                   name = "WWW"; break;
    case Qt::Key_Memo:                  name = "Memo"; break;
    case Qt::Key_LightBulb:             name = "LightBulb"; break;
    case Qt::Key_Shop:                  name = "Shop"; break;
    case Qt::Key_History:               name = "History"; break;
    case Qt::Key_AddFavorite:           name = "AddFavorite"; break;
    case Qt::Key_HotLinks:              name = "HotLinks"; break;
    case Qt::Key_BrightnessAdjust:      name = "BrightnessAdjust"; break;
    case Qt::Key_Finance:               name = "Finance"; break;
    case Qt::Key_Community:             name = "Community"; break;
    case Qt::Key_AudioRewind:           name = "AudioRewind"; break;
    case Qt::Key_BackForward:           name = "BackForward"; break;
    case Qt::Key_ApplicationLeft:       name = "ApplicationLeft"; break;
    case Qt::Key_ApplicationRight:      name = "ApplicationRight"; break;
    case Qt::Key_Book:                  name = "Book"; break;
    case Qt::Key_CD:                    name = "CD"; break;
    case Qt::Key_Calculator:            name = "Calculator"; break;
    case Qt::Key_ToDoList:              name = "ToDoList"; break;
    case Qt::Key_ClearGrab:             name = "ClearGrab"; break;
    case Qt::Key_Close:                 name = "Close"; break;
    case Qt::Key_Copy:                  name = "Copy"; break;
    case Qt::Key_Cut:                   name = "Cut"; break;
    case Qt::Key_Display:               name = "Display"; break;
    case Qt::Key_DOS:                   name = "DOS"; break;
    case Qt::Key_Documents:             name = "Documents"; break;
    case Qt::Key_Excel:                 name = "Excel"; break;
    case Qt::Key_Explorer:              name = "Explorer"; break;
    case Qt::Key_Game:                  name = "Game"; break;
    case Qt::Key_Go:                    name = "Go"; break;
    case Qt::Key_iTouch:                name = "iTouch"; break;
    case Qt::Key_LogOff:                name = "LogOff"; break;
    case Qt::Key_Market:                name = "Market"; break;
    case Qt::Key_Meeting:               name = "Meeting"; break;
    case Qt::Key_MenuKB:                name = "MenuKB"; break;
    case Qt::Key_MenuPB:                name = "MenuPB"; break;
    case Qt::Key_MySites:               name = "MySites"; break;
    case Qt::Key_News:                  name = "News"; break;
    case Qt::Key_OfficeHome:            name = "OfficeHome"; break;
    case Qt::Key_Option:                name = "Option"; break;
    case Qt::Key_Paste:                 name = "Paste"; break;
    case Qt::Key_Phone:                 name = "Phone"; break;
    case Qt::Key_Calendar:              name = "Calendar"; break;
    case Qt::Key_Reply:                 name = "Reply"; break;
    case Qt::Key_Reload:                name = "Reload"; break;
    case Qt::Key_RotateWindows:         name = "RotateWindows"; break;
    case Qt::Key_RotationPB:            name = "RotationPB"; break;
    case Qt::Key_RotationKB:            name = "RotationKB"; break;
    case Qt::Key_Save:                  name = "Save"; break;
    case Qt::Key_Send:                  name = "Send"; break;
    case Qt::Key_Spell:                 name = "Spell"; break;
    case Qt::Key_SplitScreen:           name = "SplitScreen"; break;
    case Qt::Key_Support:               name = "Support"; break;
    case Qt::Key_TaskPane:              name = "TaskPane"; break;
    case Qt::Key_Terminal:              name = "Terminal"; break;
    case Qt::Key_Tools:                 name = "Tools"; break;
    case Qt::Key_Travel:                name = "Travel"; break;
    case Qt::Key_Video:                 name = "Video"; break;
    case Qt::Key_Word:                  name = "Word"; break;
    case Qt::Key_Xfer:                  name = "Xfer"; break;
    case Qt::Key_ZoomIn:                name = "ZoomIn"; break;
    case Qt::Key_ZoomOut:               name = "ZoomOut"; break;
    case Qt::Key_Away:                  name = "Away"; break;
    case Qt::Key_Messenger:             name = "Messenger"; break;
    case Qt::Key_WebCam:                name = "WebCam"; break;
    case Qt::Key_MailForward:           name = "MailForward"; break;
    case Qt::Key_Pictures:              name = "Pictures"; break;
    case Qt::Key_Music:                 name = "Music"; break;
    case Qt::Key_Battery:               name = "Battery"; break;
    case Qt::Key_Bluetooth:             name = "Bluetooth"; break;
    case Qt::Key_WLAN:                  name = "WLAN"; break;
    case Qt::Key_UWB:                   name = "UWB"; break;
    case Qt::Key_AudioForward:          name = "AudioForward"; break;
    case Qt::Key_AudioRepeat:           name = "AudioRepeat"; break;
    case Qt::Key_AudioRandomPlay:       name = "AudioRandomPlay"; break;
    case Qt::Key_Subtitle:              name = "Subtitle"; break;
    case Qt::Key_AudioCycleTrack:       name = "AudioCycleTrack"; break;
    case Qt::Key_Time:                  name = "Time"; break;
    case Qt::Key_Hibernate:             name = "Hibernate"; break;
    case Qt::Key_View:                  name = "View"; break;
    case Qt::Key_TopMenu:               name = "TopMenu"; break;
    case Qt::Key_PowerDown:             name = "PowerDown"; break;
    case Qt::Key_Suspend:               name = "Suspend"; break;
    case Qt::Key_ContrastAdjust:        name = "ContrastAdjust"; break;
    case Qt::Key_MediaLast:             name = "MediaLast"; break;
    case Qt::Key_Call:                  name = "Call"; break;
    case Qt::Key_Context1:              name = "Context1"; break;
    case Qt::Key_Context2:              name = "Context2"; break;
    case Qt::Key_Context3:              name = "Context3"; break;
    case Qt::Key_Context4:              name = "Context4"; break;
    case Qt::Key_Flip:                  name = "Flip"; break;
    case Qt::Key_Hangup:                name = "Hangup"; break;
    case Qt::Key_No:                    name = "No"; break;
    case Qt::Key_Select:                name = "Select"; break;
    case Qt::Key_Yes:                   name = "Yes"; break;
    case Qt::Key_Execute:               name = "Execute"; break;
    case Qt::Key_Printer:               name = "Printer"; break;
    case Qt::Key_Play:                  name = "Play"; break;
    case Qt::Key_Sleep:                 name = "Sleep"; break;
    case Qt::Key_Zoom:                  name = "Zoom"; break;
    case Qt::Key_Cancel:                name = "Cancel"; break;
    }

    // Add modifiers to the name if we have them
    static Qt::KeyboardModifiers modifiers = 0;
    if (event->type() == QEvent::KeyPress)
        modifiers = event->modifiers();
    if (modifiers)
    {
        if (ctrl == "")
        {
            if (name.length() != 1 && (modifiers & Qt::ShiftModifier))
                name = "Shift-" + name;
            ctrl = name;
        }
        else
        {
            int shift = modifiers & Qt::ShiftModifier;
            if (shift && shift != modifiers)
                name = ctrl = "Shift-" + ctrl;
        }
        if (modifiers & Qt::ControlModifier)
            name = ctrl = "Control-" + ctrl;
        if (modifiers & Qt::AltModifier)
            name = ctrl = "Alt-" + ctrl;
        if (modifiers & Qt::MetaModifier)
            name = ctrl = "Meta-" + ctrl;
    }

    return name;
}


void Widget::keyPressEvent(QKeyEvent *event)
// ----------------------------------------------------------------------------
//   A key is pressed
// ----------------------------------------------------------------------------
{
    TaoSave saveCurrent(current, this);
    EventSave save(this->w_event, event);
    keyboardModifiers = event->modifiers();

    // Forward it down the regular event chain
    if (forwardEvent(event))
        return;

    // Now call "key" in the current context
    text name = keyName(event);
    XL::Symbols *syms = xlProgram->symbols;
    (XL::XLCall ("key"), name) (syms);
}


void Widget::keyReleaseEvent(QKeyEvent *event)
// ----------------------------------------------------------------------------
//   A key is released
// ----------------------------------------------------------------------------
{
    TaoSave saveCurrent(current, this);
    EventSave save(this->w_event, event);
    keyboardModifiers = event->modifiers();

    // Forward it down the regular event chain
    if (forwardEvent(event))
        return;

    // Now call "key" in the current context with the ~ prefix
    text name = "~" + keyName(event);
    XL::Symbols *syms = xlProgram->symbols;
    (XL::XLCall ("key"), name) (syms);
}


void Widget::mousePressEvent(QMouseEvent *event)
// ----------------------------------------------------------------------------
//   Mouse button click
// ----------------------------------------------------------------------------
{
    if (cursor().shape() == Qt::OpenHandCursor)
        return startPanning(event);

    TaoSave saveCurrent(current, this);
    EventSave save(this->w_event, event);
    keyboardModifiers = event->modifiers();

    QMenu * contextMenu = NULL;
    uint    button      = (uint) event->button();
    int     x           = event->x();
    int     y           = event->y();

    // Save location
    lastMouseX = x;
    lastMouseY = y;
    lastMouseButtons = button;

    // Create a selection if left click and nothing going on right now
    if (button == Qt::LeftButton)
        new Selection(this);

    // Send the click to all activities
    for (Activity *a = activities; a; a = a->Click(button, 1, x, y)) ;

    // Check if some widget is selected and wants that event
    if (forwardEvent(event))
        return;

    // Otherwise create our local contextual menu
    if (button ==  Qt::RightButton)
    {
        switch (event->modifiers())
        {
        default :
        case Qt::NoModifier :
            contextMenu = parent()->findChild<QMenu*>(CONTEXT_MENU);
            break;
        case Qt::ShiftModifier :
            contextMenu = parent()->findChild<QMenu*>(SHIFT_CONTEXT_MENU);
            break;
        case Qt::ControlModifier :
            contextMenu = parent()->findChild<QMenu*>(CONTROL_CONTEXT_MENU);
            break;
        case Qt::AltModifier :
            contextMenu = parent()->findChild<QMenu*>(ALT_CONTEXT_MENU);
            break;
        case Qt::MetaModifier :
            contextMenu = parent()->findChild<QMenu*>(META_CONTEXT_MENU);
            break;
        }

        if (contextMenu)
            contextMenu->exec(event->globalPos());
    }
}


void Widget::mouseReleaseEvent(QMouseEvent *event)
// ----------------------------------------------------------------------------
//   Mouse button is released
// ----------------------------------------------------------------------------
{
    if (cursor().shape() == Qt::ClosedHandCursor)
        return endPanning(event);

    TaoSave saveCurrent(current, this);
    EventSave save(this->w_event, event);
    keyboardModifiers = event->modifiers();

    uint button = (uint) event->button();
    int x = event->x();
    int y = event->y();

    // Save location
    lastMouseX = x;
    lastMouseY = y;
    lastMouseButtons = button;

    // Check if there is an activity that deals with it
    for (Activity *a = activities; a; a = a->Click(button, 0, x, y)) ;

    // Pass the event down the event chain
    forwardEvent(event);
}


void Widget::mouseMoveEvent(QMouseEvent *event)
// ----------------------------------------------------------------------------
//    Mouse move
// ----------------------------------------------------------------------------
{
    if (cursor().shape() == Qt::ClosedHandCursor)
        return doPanning(event);

    TaoSave saveCurrent(current, this);
    EventSave save(this->w_event, event);
    keyboardModifiers = event->modifiers();
    int buttons = event->buttons();
    bool active = buttons != Qt::NoButton;
    int x = event->x();
    int y = event->y();

    // Save location
    lastMouseX = x;
    lastMouseY = y;
    lastMouseButtons = buttons;

    // Check if there is an activity that deals with it
    for (Activity *a = activities; a; a = a->MouseMove(x, y, active)) ;

    // Pass the event down the event chain
    forwardEvent(event);
}


void Widget::mouseDoubleClickEvent(QMouseEvent *event)
// ----------------------------------------------------------------------------
//   Mouse double click
// ----------------------------------------------------------------------------
{
    TaoSave saveCurrent(current, this);
    EventSave save(this->w_event, event);
    keyboardModifiers = event->modifiers();

    // Create a selection if left click and nothing going on right now
    uint    button      = (uint) event->button();
    int     x           = event->x();
    int     y           = event->y();
    if (button == Qt::LeftButton && ( !activities || !activities->next))
        new Selection(this);

    // Save location
    lastMouseX = x;
    lastMouseY = y;
    lastMouseButtons = button;

    // Send the click to all activities
    for (Activity *a = activities; a; a = a->Click(button, 2, x, y)) ;

    forwardEvent(event);
}


void Widget::wheelEvent(QWheelEvent *event)
// ----------------------------------------------------------------------------
//   Mouse wheel: zoom in/out
// ----------------------------------------------------------------------------
{
    TaoSave saveCurrent(current, this);
    EventSave save(this->w_event, event);
    keyboardModifiers = event->modifiers();
    int     x           = event->x();
    int     y           = event->y();

    // Save location
    lastMouseX = x;
    lastMouseY = y;

    if (forwardEvent(event))
        return;

    // Propagate the wheel event
    XL::Symbols *symbols = xlProgram->symbols;
    int d = event->delta();
    Qt::Orientation orientation = event->orientation();
    longlong dx = orientation == Qt::Horizontal ? d : 0;
    longlong dy = orientation == Qt::Vertical   ? d : 0;
    (XL::XLCall("wheel_event"), dx, dy)(symbols);
}


void Widget::timerEvent(QTimerEvent *event)
// ----------------------------------------------------------------------------
//    Timer expired
// ----------------------------------------------------------------------------
{
    TaoSave saveCurrent(current, this);
    EventSave save(this->w_event, event);
    forwardEvent(event);
}


void Widget::startPanning(QMouseEvent *event)
// ----------------------------------------------------------------------------
//    Enter view panning mode
// ----------------------------------------------------------------------------
{
    setCursor(Qt::ClosedHandCursor);
    panX = event->x();
    panY = event->y();
}


void Widget::doPanning(QMouseEvent *event)
// ----------------------------------------------------------------------------
//    Move view to follow mouse (panning mode)
// ----------------------------------------------------------------------------
{
    int x, y, dx, dy;

    x = event->x();
    y = event->y();
    dx = x - panX;
    dy = y - panY;

    eyeX -= 2*dx*zoom;
    eyeY += 2*dy*zoom;
    centerX -= 2*dx*zoom;
    centerY += 2*dy*zoom;

    panX = x;
    panY = y;

    setup(width(), height());
    updateGL();
}


void Widget::endPanning(QMouseEvent *)
// ----------------------------------------------------------------------------
//    Leave view panning mode
// ----------------------------------------------------------------------------
{
    setCursor(Qt::OpenHandCursor);
}



// ============================================================================
//
//    XL program management
//
// ============================================================================

void Widget::updateProgram(XL::SourceFile *source)
// ----------------------------------------------------------------------------
//   Change the XL program, clean up stuff along the way
// ----------------------------------------------------------------------------
{
    xlProgram = source;
    if (Tree *prog = xlProgram->tree)
    {
        Renormalize renorm(this);
        xlProgram->tree = prog->Do(renorm);
        xlProgram->tree->SetSymbols(prog->Symbols());
    }
    refreshProgram();
    inError = false;
}


void Widget::applyAction(XL::Action &action)
// ----------------------------------------------------------------------------
//   Applies an action on the tree and all the dependents
// ----------------------------------------------------------------------------
{
    Tree *prog = xlProgram->tree;
    if (!prog)
        return;

    // Lookup imported files
    import_set iset;
    ImportedFilesChanged(prog, iset, false);

    import_set::iterator it;
    for (it = iset.begin(); it != iset.end(); it++)
    {
        XL::SourceFile &sf = **it;
        if (sf.tree)
            sf.tree->Do(action);
    }
}


void Widget::reloadProgram(XL::Tree *newProg)
// ----------------------------------------------------------------------------
//   Set the program to reload
// ----------------------------------------------------------------------------
{
    Tree *prog = xlProgram->tree;

    if (!newProg)
    {
        // We want to force a clone so that we recompile everything
        if (prog)
        {
            Renormalize renorm(this);
            newProg = prog->Do(renorm);
            newProg->SetSymbols(prog->Symbols());
            xlProgram->tree = newProg;
        }
    }

    // Check if we can simply change some parameters in the tree
    else
    {
        ApplyChanges changes(newProg);
        if (!prog->Do(changes))
        {
            // Need a big hammer, i.e. reload the complete program
            newProg->SetSymbols(prog->Symbols());
            xlProgram->tree = newProg;
        }
    }
    inError = false;

    // Now update the window
    updateProgramSource();
}


void Widget::updateProgramSource()
// ----------------------------------------------------------------------------
//   Update the contents of the program source window
// ----------------------------------------------------------------------------
{
    Window *window = (Window *) parentWidget();
    if (window->dock->isHidden())
        return;
    if (Tree *prog = xlProgram->tree)
    {
        text txt = "";
        srcRendererOutput.str(txt);

        // Tell renderer how to highlight selected items
        std::set<Tree_p >::iterator i;
        srcRenderer->highlights.clear();
        for (i = selectionTrees.begin(); i != selectionTrees.end(); i++)
            srcRenderer->highlights[*i] = "selected";
        srcRenderer->Render(prog);

        text html = srcRendererOutput.str();
        window->setHtml(+html);
        IFTRACE(srcview)
        {
            QFile data("srcview.html");
            if (data.open(QFile::WriteOnly | QFile::Truncate))
            {
                QTextStream out(&data);
                out << +html;
            }
        }
    }
    else
    {
        window->setHtml("");
    }
}


void Widget::refreshProgram()
// ----------------------------------------------------------------------------
//   Check if any of the source files we depend on changed
// ----------------------------------------------------------------------------
{
    Repository *repo = repository();
    Tree *prog = xlProgram->tree;
    if (!prog || !repo || xlProgram->readOnly)
        return;

    // Loop on imported files
    import_set iset;
    if (ImportedFilesChanged(prog, iset, false))
    {
        import_set::iterator it;
        bool needBigHammer = false;
        bool loadError     = false;
        for (it = iset.begin(); it != iset.end(); it++)
        {
            XL::SourceFile &sf = **it;
            text fname = sf.name;
            struct stat st;
            stat (fname.c_str(), &st);

            if (st.st_mtime > sf.modified)
            {
                IFTRACE(filesync)
                    std::cerr << "File " << fname << " changed\n";

                Tree *replacement = NULL;
                if (repo)
                {
                    replacement = repo->read(fname);
                }
                else
                {
                    XL::Syntax syntax(XL::MAIN->syntax);
                    XL::Positions &positions = XL::MAIN->positions;
                    XL::Errors &errors = XL::MAIN->errors;
                    XL::Parser parser(fname.c_str(), syntax, positions, errors);
                    replacement = parser.Parse();
                }

                if (!replacement)
                {
                    // Uh oh, file went away?
                    needBigHammer = true;
                }
                else
                {
                    // Make sure we normalize the replacement
                    Renormalize renorm(this);
                    replacement = replacement->Do(renorm);

                    // Check if we can simply change some parameters in file
                    ApplyChanges changes(replacement);
                    if (!sf.tree->Do(changes))
                        needBigHammer = true;

                    // Record new modification time
                    sf.modified = st.st_mtime;

                    IFTRACE(filesync)
                    {
                        if (needBigHammer)
                            std::cerr << "Need to reload everything.\n";
                        else
                            std::cerr << "Surgical replacement worked\n";
                    }
                } // Replacement checked

                if (fname == xlProgram->name)
                {
                    // Update source file view
                    Window *window = (Window *) parentWidget();
                    loadError = !window->loadFileIntoSourceFileView(+fname);
                    if (loadError)
                    {
                        IFTRACE(filesync)
                            std::cerr << "Main program could not be read\n";

                        // Source file is cleared, delete tree
                        sf.tree = new XL::Text("Program could not be read",
                                               "//", "\n");
                    }
                }

                // If a file was modified, we need to refresh the screen
                refresh();

            } // If file modified
        } // For all files

        // If we were not successful with simple changes, reload everything...
        if (needBigHammer)
        {
            if (!loadError)
            {
                for (it = iset.begin(); it != iset.end(); it++)
                {
                    XL::SourceFile &sf = **it;
                    text fname = sf.name;
                    XL::MAIN->LoadFile(fname);
                    inError = false;
                }
            }
        }
    }
}


void Widget::markChanged(text reason)
// ----------------------------------------------------------------------------
//    Record that the program changed
// ----------------------------------------------------------------------------
{
    Repository *repo = repository();
    if (repo)
        repo->markChanged(reason);

    if (Tree *prog = xlProgram->tree)
    {
        import_set done;
        ImportedFilesChanged(prog, done, true);

        import_set::iterator f;
        for (f = done.begin(); f != done.end(); f++)
        {
            XL::SourceFile &sf = **f;
            if (&sf != xlProgram && sf.changed)
                writeIfChanged(sf);
        }
    }

    // Now update the window
    updateProgramSource();

    // Cause the screen to redraw
    refresh(0);
}


void Widget::selectStatements(Tree *tree)
// ----------------------------------------------------------------------------
//   Put all statements in the given selection in the next selection
// ----------------------------------------------------------------------------
{
    // Deselect previous selection
    selection.clear();
    selectionTrees.clear();

    // Fill the selection for next time
    selectNextTime.clear();
    Tree *t = tree;
    while (Infix *i = t->AsInfix())
    {
        if (i->name != "\n" && i->name != ";")
            break;
        selectNextTime.insert(i->left);
        t = i->right;
    }
    selectNextTime.insert(t);
    selectionChanged = true;
}


bool Widget::writeIfChanged(XL::SourceFile &sf)
// ----------------------------------------------------------------------------
//   Write file to repository if marked 'changed' and reset change attributes
// ----------------------------------------------------------------------------
{
    text fname = sf.name;
    if (sf.changed)
    {
        Repository *repo = repository();

        if (!repo)
            return false;

        if (repo->write(fname, sf.tree))
        {
            // Mark the tree as no longer changed
            sf.changed = false;

            // Record that we need to commit it sometime soon
            repo->change(fname);
            IFTRACE(filesync)
                std::cerr << "Changed " << fname << "\n";

            // Record time when file was changed
            struct stat st;
            stat (fname.c_str(), &st);
            sf.modified = st.st_mtime;

            return true;
        }

        IFTRACE(filesync)
            std::cerr << "Could not write " << fname << " to repository\n";
    }
    return false;
}


bool Widget::doPull(ulonglong tick)
// ----------------------------------------------------------------------------
//   Pull from remote repository and reset next pull time
// ----------------------------------------------------------------------------
{
    Repository *repo = repository();
    bool ok = repo->pull();
    nextPull = tick + repo->pullInterval * 1000;
    return ok;
}


bool Widget::enableAutoSave(bool enabled)
// ----------------------------------------------------------------------------
//   Enable or disable automatic (periodic) save
// ----------------------------------------------------------------------------
{
    bool old = autoSaveEnabled;
    autoSaveEnabled = enabled;
    return old;
}

bool Widget::doSave(ulonglong tick)
// ----------------------------------------------------------------------------
//   Save source files that have changed and reset next save time
// ----------------------------------------------------------------------------
{
    bool changed = false;
    XL::Main *xlr = XL::MAIN;
    XL::source_files::iterator it;
    for (it = xlr->files.begin(); it != xlr->files.end(); it++)
    {
        XL::SourceFile &sf = (*it).second;
        if (writeIfChanged(sf))
            changed = true;
    }

    // Record when we will save file again
    nextSave = tick + xlr->options.save_interval * 1000;
    return changed;
}


bool Widget::doCommit(ulonglong tick)
// ----------------------------------------------------------------------------
//   Commit files previously written to repository and reset next commit time
// ----------------------------------------------------------------------------
{
    Repository * repo = repository();
    if (repo->state == Repository::RS_Clean)
        return false;

    IFTRACE(filesync)
            std::cerr << "Commit\n";
    bool done;
    done = repo->commit();
    if (done)
    {
        XL::Main *xlr = XL::MAIN;
        nextCommit = tick + xlr->options.commit_interval * 1000;

        Window *window = (Window *) parentWidget();
        window->markChanged(false);

        return true;
    }
    return false;
}


Repository * Widget::repository()
// ----------------------------------------------------------------------------
//   Return the repository associated with the current document (may be NULL)
// ----------------------------------------------------------------------------
{
    Window * win = (Window *)parentWidget();
    return win->repository();
}


XL::Tree *Widget::get(Tree *shape, text name, text topNameList)
// ----------------------------------------------------------------------------
//   Find an attribute in the current shape, group or returns NULL
// ----------------------------------------------------------------------------
{
    // Can't get attributes without a current shape
    if (!shape)
        return NULL;

    // The current shape has to be a 'shape' prefix
    XL::Prefix *shapePrefix = shape->AsPrefix();
    if (!shapePrefix)
        return NULL;
    Name *shapeName = shapePrefix->left->AsName();
    if (!shapeName || topNameList.find(shapeName->value) == std::string::npos)
        return NULL;

    // Take the right child. If it's a block, extract the block
    Tree *child = shapePrefix->right;
    if (XL::Block *block = child->AsBlock())
        child = block->child;

    // Now loop on all statements, looking for the given name
    while (child)
    {
        Tree *what = child;

        // Check if we have \n or ; infix
        XL::Infix *infix = child->AsInfix();
        if (infix && (infix->name == "\n" || infix->name == ";"))
        {
            what = infix->left;
            child = infix->right;
        }
        else
        {
            child = NULL;
        }

        // Analyze what we got here: is it in the form 'funcname args' ?
        if (XL::Prefix *prefix = what->AsPrefix())
            if (Name *prefixName = prefix->left->AsName())
                if (prefixName->value == name)
                    return prefix;

        // Is it a name
        if (Name *singleName = what->AsName())
            if (singleName->value == name)
                return singleName;
    }

    return NULL;
}


bool Widget::set(Tree *shape, text name, Tree *value, text topNameList)
// ----------------------------------------------------------------------------
//   Set an attribute in the current shape or group, return true if successful
// ----------------------------------------------------------------------------
{
    // Can't get attributes without a current shape
    if (!shape)
        return false;

    // The current shape has to be a 'shape' prefix
    XL::Prefix *shapePrefix = shape->AsPrefix();
    if (!shapePrefix)
        return false;
    Name *shapeName = shapePrefix->left->AsName();
    if (!shapeName || topNameList.find(shapeName->value) == std::string::npos)
        return false;

    // Take the right child. If it's a block, extract the block
    Tree_p *addr = &shapePrefix->right;
    Tree *child = *addr;
    if (XL::Block *block = child->AsBlock())
    {
        addr = &block->child;
        child = *addr;
    }
    Tree_p *topAddr = addr;

    // Now loop on all statements, looking for the given name
    while (child)
    {
        Tree *what = child;

        // Check if we have \n or ; infix
        XL::Infix *infix = child->AsInfix();
        if (infix && (infix->name == "\n" || infix->name == ";"))
        {
            addr = &infix->left;
            what = *addr;
            child = infix->right;
        }
        else
        {
            child = NULL;
        }

        // Analyze what we got here: is it in the form 'funcname args' ?
        if (value->AsPrefix())
        {
            if (XL::Prefix *prefix = what->AsPrefix())
            {
                if (Name *prefixName = prefix->left->AsName())
                {
                    if (prefixName->value == name)
                    {
                        ApplyChanges changes(value);
                        if (!(*addr)->Do(changes))
                        {
                            // Need big hammer here, reload everything
                            *addr = value;
                            reloadProgram();
                        }
                        return true;
                    }
                }
            }
        }
        else if (Name *newName = value->AsName())
        {
            if (Name *stmtName = what->AsName())
            {
                if (stmtName->value == name)
                {
                    // If the name is different, need to update
                    if (newName->value != name)
                    {
                        *addr = value;
                        reloadProgram();
                    }
                    return true;
                }
            }
        }

    } // Loop on all items

    // We didn't find the name: set the top level item
    *topAddr = new XL::Infix("\n", value, *topAddr);
    reloadProgram();
    return true;
}


bool Widget::get(Tree *shape, text name, XL::TreeList &args, text topName)
// ----------------------------------------------------------------------------
//   Get the arguments, decomposing args in a comma-separated list
// ----------------------------------------------------------------------------
{
    // Check if we can get the tree
    Tree *attrib = get(shape, name, topName);
    if (!attrib)
        return false;

    // Check if we expect a single name or a prefix
    args.clear();
    if (attrib->AsName())
        return true;

    // Check that we have a prefix
    XL::Prefix *prefix = attrib->AsPrefix();
    if (!prefix)
        return false;           // ??? This shouldn't happen

    // Get attribute arguments and decompose them into 'args'
    Tree *argsTree = prefix->right;
    while (XL::Infix *infix = argsTree->AsInfix())
    {
        if (infix->name != ",")
            break;
        args.push_back(infix->right);
        argsTree = infix->left;
    }
    args.push_back(argsTree);
    std::reverse(args.begin(), args.end());

    // Success
    return true;
}


bool Widget::set(Tree *shape, text name, XL::TreeList &args, text topName)
// ----------------------------------------------------------------------------
//   Set the arguments, building the comma-separated list
// ----------------------------------------------------------------------------
{
    Tree *call = new XL::Name(name);
    if (uint arity = args.size())
    {
        Tree *argsTree = args[0];
        for (uint a = 1; a < arity; a++)
            argsTree = new XL::Infix(",", argsTree, args[a]);
        call = new XL::Prefix(call, argsTree);
    }

    return set(shape, name, call, topName);
}


bool Widget::get(Tree *shape, text name, attribute_args &args, text topName)
// ----------------------------------------------------------------------------
//   Get the arguments, decomposing args in a comma-separated list
// ----------------------------------------------------------------------------
{
    // Get the trees
    XL::TreeList treeArgs;
    if (!get(shape, name, treeArgs, topName))
        return false;

    // Convert from integer or tree values
    XL::TreeList::iterator i;
    for (i = treeArgs.begin(); i != treeArgs.end(); i++)
    {
        Tree *arg = *i;
        if (!arg->IsConstant())
            arg = xl_evaluate(arg);
        if (XL::Real *asReal = arg->AsReal())
            args.push_back(asReal->value);
        else if (XL::Integer *asInteger = arg->AsInteger())
            args.push_back(asInteger->value);
        else return false;
    }

    return true;
}


bool Widget::set(Tree *shape, text name, attribute_args &args, text topName)
// ----------------------------------------------------------------------------
//   Set the arguments, building the comma-separated list
// ----------------------------------------------------------------------------
{
    Tree *call = new XL::Name(name);
    if (uint arity = args.size())
    {
        Tree *argsTree = new XL::Real(args[0]);
        for (uint a = 1; a < arity; a++)
            argsTree = new XL::Infix(",", argsTree, new XL::Real(args[a]));
        call = new XL::Prefix(call, argsTree);
    }

    return set(shape, name, call, topName);
}



// ============================================================================
//
//    Performance timing
//
// ============================================================================

ulonglong Widget::now()
// ----------------------------------------------------------------------------
//    Return the current time in microseconds
// ----------------------------------------------------------------------------
{
    // Timing
    struct timeval tv;
    gettimeofday(&tv, NULL);
    ulonglong t = tv.tv_sec * 1000000ULL + tv.tv_usec;
    return t;
}


ulonglong Widget::elapsed(ulonglong since, ulonglong until,
                          bool stats, bool show)
// ----------------------------------------------------------------------------
//    Record how much time passed since last measurement
// ----------------------------------------------------------------------------
{
    ulonglong t = until - since;
    if (t == 0)
        t = 1; // Because Windows lies

    if (stats)
    {
        if (tmin > t) tmin = t;
        if (tmax < t) tmax = t;
        tsum += t;
        tcount++;
    }

    if (show && (tcount & 15) == 0)
    {
        char buffer[80];
        snprintf(buffer, sizeof(buffer),
                 "Duration=" CONFIG_UHUGE_FORMAT "-" CONFIG_UHUGE_FORMAT
                 " (~%f) %5.2f-%5.2f FPS (~%5.2f)",
                 tmin, tmax, double(tsum )/ tcount,
                 (100000000ULL / tmax)*0.01,
                 (100000000ULL / tmin)*0.01,
                 (100000000ULL * tcount / tsum) * 0.01);
        Window *window = (Window *) parentWidget();
        window->statusBar()->showMessage(QString(buffer));
    }

    return t;
}



// ============================================================================
//
//    Selection management
//
// ============================================================================

uint Widget::selected(uint i)
// ----------------------------------------------------------------------------
//   Test if the current shape is selected
// ----------------------------------------------------------------------------
{
    return i && selection.count(i) > 0 ? selection[i] : 0;
}


uint Widget::selected(Layout *layout)
// ----------------------------------------------------------------------------
//   Test if the current shape is selected
// ----------------------------------------------------------------------------
{
    return selected(layout->id);
}


bool Widget::focused(Layout *layout)
// ----------------------------------------------------------------------------
//   Test if the current shape is selected
// ----------------------------------------------------------------------------
{
    return layout->id == focusId;
}


void Widget::select(uint id, uint count)
// ----------------------------------------------------------------------------
//    Change the current shape selection state if we are in selectable state
// ----------------------------------------------------------------------------
{
    if (id)
    {
        uint s;
        switch (count)
        {
        case 0:        // Deselect
            selection.erase(id);
            break;
        case 1:        // Add "one single click" to selection state
            selection[id] += 1;
            break;
        case 2:        // Add "one double click" to selection state
            selection[id] += (1 << 16);
            break;
        case (uint)-1: // Remove "one single click" from selection state
            s = selected(id);
            if (singleClicks(s))
                selection[id] -= 1;
            break;
        default:       // Error
            break;
        }
    }
}


void Widget::reselect(Tree *from, Tree *to)
// ----------------------------------------------------------------------------
//   If 'from' is in any selection map, add 'to' to this selection
// ----------------------------------------------------------------------------
{
    // Check if we are possibly changing the selection
    if ( selectionTrees.count(from) )
        selectionTrees.insert(to);

    // Check if we are possibly changing the next selection
    if (selectNextTime.count(from))
        selectNextTime.insert(to);

    // Check if we are possibly changing the page tree reference
    if (pageTree == from)
        pageTree = to;
}


void Widget::deleteFocus(QWidget *widget)
// ----------------------------------------------------------------------------
//   Make sure we don't keep a focus on a widget that was deleted
// ----------------------------------------------------------------------------
{
    if (focusWidget == widget)
        focusWidget = NULL;
}


bool Widget::requestFocus(QWidget *widget, coord x, coord y)
// ----------------------------------------------------------------------------
//   Some other widget request the focus
// ----------------------------------------------------------------------------
{
    IFTRACE(widgets)
            std::cerr << "Widget::requestFocus name " << +(widget->objectName()) << std::endl;

    if (!focusWidget)
    {
        GLMatrixKeeper saveGL;
        Vector3 v = layout->Offset() + Vector3(x, y, 0);
        focusWidget = widget;
        glTranslatef(v.x, v.y, v.z);
        recordProjection();
        QFocusEvent focusIn(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
        QObject *fin = focusWidget;
        fin->event(&focusIn);
    }
    return focusWidget == widget;
}


void Widget::recordProjection()
// ----------------------------------------------------------------------------
//   Record the transformation matrix for the current projection
// ----------------------------------------------------------------------------
{
    glGetDoublev(GL_PROJECTION_MATRIX, focusProjection);
    glGetDoublev(GL_MODELVIEW_MATRIX, focusModel);
    glGetIntegerv(GL_VIEWPORT, focusViewport);
}


Point3 Widget::unproject (coord x, coord y, coord z)
// ----------------------------------------------------------------------------
//   Convert mouse clicks into 3D planar coordinates for the focus object
// ----------------------------------------------------------------------------
{
    // Adjust between mouse and OpenGL coordinate systems
    y = height() - y;

    // Get 3D coordinates for the near plane based on window coordinates
    GLdouble x3dn, y3dn, z3dn;
    gluUnProject(x, y, 0.0,
                 focusModel, focusProjection, focusViewport,
                 &x3dn, &y3dn, &z3dn);

    // Same with far-plane 3D coordinates
    GLdouble x3df, y3df, z3df;
    gluUnProject(x, y, 1.0,
                 focusModel, focusProjection, focusViewport,
                 &x3df, &y3df, &z3df);

    GLfloat zDistance = z3dn - z3df;
    if (zDistance == 0.0)
        zDistance = 1.0;
    GLfloat ratio = (z3dn - z) / zDistance;
    GLfloat x3d = x3dn + ratio * (x3df - x3dn);
    GLfloat y3d = y3dn + ratio * (y3df - y3dn);

    return Point3(x3d, y3d, z);
}


Drag *Widget::drag()
// ----------------------------------------------------------------------------
//   Return the drag activity that we can use to unproject
// ----------------------------------------------------------------------------
{
    Drag *result = active<Drag>();
    if (result)
        recordProjection();
    return result;
}


TextSelect *Widget::textSelection()
// ----------------------------------------------------------------------------
//   Return text selection if appropriate, possibly creating it from a Drag
// ----------------------------------------------------------------------------
{
    TextSelect *result = active<TextSelect>();
    if (result)
        recordProjection();
    return result;
}


static inline void resetLayout(Layout *where)
// ----------------------------------------------------------------------------
//   Put layout back into a state appropriate for drawing a selection
// ----------------------------------------------------------------------------
{
    if (where)
    {
        where->lineWidth = 1;
        where->lineColor = Color(1,0,0,1);
        where->fillColor = Color(0,1,0,0.8);
        where->fillTexture = 0;
    }
}


void Widget::drawSelection(Layout *where,
                           const Box3 &bnds, text selName, uint id)
// ----------------------------------------------------------------------------
//    Draw a 2D or 3D selection with the given coordinates
// ----------------------------------------------------------------------------
{
    // Symbols where we will find the selection code
    XL::Symbols *symbols = xlProgram->symbols;

    Box3 bounds(bnds);
    bounds.Normalize();

    coord w = bounds.Width();
    coord h = bounds.Height();
    coord d = bounds.Depth();
    Point3 c  = bounds.Center();

    SpaceLayout selectionSpace(this);

    XL::LocalSave<Layout *> saveLayout(layout, &selectionSpace);
    GLAttribKeeper          saveGL;
    resetLayout(where);
    selectionSpace.id = id;
    selectionSpace.isSelection = true;
    saveSelectionState(where);
    glDisable(GL_DEPTH_TEST);
    if (bounds.Depth() > 0)
        (XL::XLCall("draw_" + selName), c.x, c.y, c.z, w, h, d) (symbols);
    else
        (XL::XLCall("draw_" + selName), c.x, c.y, w, h) (symbols);
    selectionSpace.Draw(where);
    glEnable(GL_DEPTH_TEST);
}


void Widget::drawHandle(Layout *where,
                        const Point3 &p, text handleName, uint id)
// ----------------------------------------------------------------------------
//    Draw the handle of a 2D or 3D selection
// ----------------------------------------------------------------------------
{
    // Symbols where we will find the selection code
    XL::Symbols *symbols = xlProgram->symbols;

    SpaceLayout selectionSpace(this);

    XL::LocalSave<Layout *> saveLayout(layout, &selectionSpace);
    GLAttribKeeper          saveGL;
    resetLayout(where);
    glDisable(GL_DEPTH_TEST);
    selectionSpace.id = id;
    selectionSpace.isSelection = true;
    (XL::XLCall("draw_" + handleName), p.x, p.y, p.z) (symbols);

    selectionSpace.Draw(where);
    glEnable(GL_DEPTH_TEST);
}


void Widget::drawTree(Layout *where, Tree *code)
// ----------------------------------------------------------------------------
//    Draw some tree, e.g. cell fill and border
// ----------------------------------------------------------------------------
{
    XL::Symbols *symbols = code->Symbols(); assert(symbols);
    SpaceLayout selectionSpace(this);

    XL::LocalSave<Layout *> saveLayout(layout, &selectionSpace);
    GLAttribKeeper          saveGL;
    glDisable(GL_DEPTH_TEST);
    xl_evaluate(code);

    selectionSpace.Draw(where);
    glEnable(GL_DEPTH_TEST);
}


void Widget::saveSelectionState(Layout *where)
// ----------------------------------------------------------------------------
//   Save the color and font for the selection
// ----------------------------------------------------------------------------
{
    if (where)
    {
        selectionColor["line_color"] = where->lineColor;
        selectionColor["color"] = where->fillColor;
        selectionFont = where->font;
    }
}


Tree * Widget::shapeAction(text n, GLuint id)
// ----------------------------------------------------------------------------
//   Return the shape action for the given name and GL id
// ----------------------------------------------------------------------------
{
    action_map::iterator foundName = actionMap.find(n);
    if (foundName != actionMap.end())
    {
        GLid_map::iterator foundAction = (*foundName).second.find(id);
        if (foundAction != (*foundName).second.end())
        {
            return (*foundAction).second;
        }
    }
    return NULL;
}



// ============================================================================
//
//   XLR runtime entry points
//
// ============================================================================

#pragma GCC diagnostic ignored "-Wunused-parameter"

Widget *Widget::current = NULL;
typedef XL::Tree Tree;

XL::Text_p Widget::page(Tree_p self, text name, Tree_p body)
// ----------------------------------------------------------------------------
//   Start a new page, returns the previously named page
// ----------------------------------------------------------------------------
{
    // We start with first page if we had no page set
    if (pageName == "")
        pageName = name;

    // Increment pageId
    pageId++;

    // If the page is set, then we display it
    if (pageName == name || drawAllPages)
    {
        // Initialize back-link
        pageFound = pageId;
        pageLinks.clear();
        if (pageId > 1)
            pageLinks["PageUp"] = lastPageName;
        pageTree = body;
        xl_evaluate(body);
    }
    else if (pageName == lastPageName)
    {
        // We are executing the page following the current one:
        // Check if PageDown is set, otherwise set current page as default
        if (pageLinks.count("PageDown") == 0)
            pageLinks["PageDown"] = name;
    }

    lastPageName = name;
    return new Text(pageName);
}


XL::Text_p Widget::pageLink(Tree_p self, text key, text name)
// ----------------------------------------------------------------------------
//   Indicate the chaining of pages, returns previous information
// ----------------------------------------------------------------------------
{
    text old = pageLinks[key];
    pageLinks[key] = name;
    return new Text(old);
}


XL::Text_p Widget::gotoPage(Tree_p self, text page)
// ----------------------------------------------------------------------------
//   Directly go to the given page
// ----------------------------------------------------------------------------
{
    text old = pageName;
    pageName = page;
    return new Text(old);
}


XL::Text_p Widget::pageLabel(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the label of the current page
// ----------------------------------------------------------------------------
{
    return new Text(pageName);
}


XL::Integer_p Widget::pageNumber(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the number of the current page
// ----------------------------------------------------------------------------
{
    return new Integer(pageShown);
}


XL::Integer_p Widget::pageCount(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the number of pages in the current document
// ----------------------------------------------------------------------------
{
    return new Integer(pageTotal ? pageTotal : 1);
}


XL::Real_p Widget::pageWidth(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the width of the page
// ----------------------------------------------------------------------------
{
    return new Real(pageW);
}


XL::Real_p Widget::pageHeight(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the height of the page
// ----------------------------------------------------------------------------
{
    return new Real(pageH);
}


XL::Real_p Widget::frameWidth(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the width of the current layout frame
// ----------------------------------------------------------------------------
{
    return new Real(layout->Bounds(layout).Width());
}


XL::Real_p Widget::frameHeight(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the height of the current layout frame
// ----------------------------------------------------------------------------
{
    return new Real(layout->Bounds(layout).Height());
}


XL::Real_p Widget::frameDepth(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the depth of the current layout frame
// ----------------------------------------------------------------------------
{
    return new Real(layout->Bounds(layout).Depth());
}


XL::Real_p Widget::windowWidth(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the width of the window in which we display
// ----------------------------------------------------------------------------
{
    return new Real(width());
}


XL::Real_p Widget::windowHeight(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the height of window in which we display
// ----------------------------------------------------------------------------
{
    return new Real(height());
}


XL::Real_p Widget::time(Tree_p self)
// ----------------------------------------------------------------------------
//   Return a fractional time, including milliseconds
// ----------------------------------------------------------------------------
{
    refresh(0.04);
    if (animated)
        frozenTime = CurrentTime();
    return new XL::Real(frozenTime);
}


XL::Real_p Widget::pageTime(Tree_p self)
// ----------------------------------------------------------------------------
//   Return a fractional time, including milliseconds
// ----------------------------------------------------------------------------
{
    refresh(0.04);
    if (animated)
        frozenTime = CurrentTime();
    return new XL::Real(frozenTime - pageStartTime);
}


XL::Real_p Widget::after(Tree_p self, double delay, Tree_p code)
// ----------------------------------------------------------------------------
//   Execute the given code only after the specified amount of time
// ----------------------------------------------------------------------------
{
    if (animated)
        frozenTime = CurrentTime();

    double now = frozenTime;
    double elapsed = now - startTime;

    if (elapsed < delay)
    {
        if (pageRefresh > startTime + delay)
            pageRefresh = startTime + delay;
    }
    else
    {
        XL::LocalSave<double> saveTime(startTime, startTime + delay);
        xl_evaluate(code);
    }

    return new XL::Real(elapsed);
}


XL::Real_p Widget::every(Tree_p self,
                         double interval, double duty,
                         Tree_p code)
// ----------------------------------------------------------------------------
//   Execute the given code only after the specified amount of time
// ----------------------------------------------------------------------------
{
    if (animated)
        frozenTime = CurrentTime();

    double now = frozenTime;
    double elapsed = now - startTime;
    double active = fmod(elapsed, interval);
    double start = now - active;
    double delay = duty * interval;

    if (active > delay)
    {
        if (pageRefresh > start + interval)
            pageRefresh = start + interval;
    }
    else
    {
        XL::LocalSave<double> saveTime(startTime, start);
        xl_evaluate(code);
        if (pageRefresh > start + delay)
            pageRefresh = start + delay;
    }
    return new XL::Real(elapsed);
}


Real_p Widget::mouseX(Tree_p self)
// ----------------------------------------------------------------------------
//    Return the position of the mouse
// ----------------------------------------------------------------------------
{
    layout->Add(new RecordMouseCoordinates(self));
    if (MouseCoordinatesInfo *info = self->GetInfo<MouseCoordinatesInfo>())
        return new Real(info->coordinates.x);
    return new Real(0.0);
}


Real_p Widget::mouseY(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the position of the mouse
// ----------------------------------------------------------------------------
{
    layout->Add(new RecordMouseCoordinates(self));
    if (MouseCoordinatesInfo *info = self->GetInfo<MouseCoordinatesInfo>())
        return new Real(info->coordinates.y);
    return new Real(0.0);
}


Integer_p Widget::mouseButtons(Tree_p self)
// ----------------------------------------------------------------------------
//    Return the buttons of the last mouse event
// ----------------------------------------------------------------------------
{
    return new Integer(lastMouseButtons);
}


Tree_p Widget::shapeAction(Tree_p self, text name, Tree_p action)
// ----------------------------------------------------------------------------
//   Set the action associated with a click or other on the object
// ----------------------------------------------------------------------------
{
    actionMap[name][layout->id] = action;
    if (!action->Symbols())
        action->SetSymbols(self->Symbols());
    return XL::xl_true;
}


Tree_p Widget::locally(Tree_p self, Tree_p child)
// ----------------------------------------------------------------------------
//   Evaluate the child tree while preserving the current state
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> save(layout, layout->AddChild(layout->id));
    Tree_p result = xl_evaluate(child);
    return result;
}


Tree_p Widget::shape(Tree_p self, Tree_p child)
// ----------------------------------------------------------------------------
//   Evaluate the child and mark the current shape
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(newId()));
    XL::LocalSave<Tree_p>   saveShape (currentShape, self);
    if (selectNextTime.count(self))
    {
        selection[id]++;
        selectNextTime.erase(self);
    }
    Tree_p result = xl_evaluate(child);
    return result;
}


Tree_p Widget::activeWidget(Tree_p self, Tree_p child)
// ----------------------------------------------------------------------------
//   Create a context for active widgets, e.g. buttons
// ----------------------------------------------------------------------------
//   We set currentShape to NULL, which means that we won't create manipulator
//   so the widget is active (it can be selected) but won't budge
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(newId()));
    XL::LocalSave<Tree_p>   saveShape (currentShape, NULL);
    if (selectNextTime.count(self))
    {
        selection[id]++;
        selectNextTime.erase(self);
    }
    Tree_p result = xl_evaluate(child);
    return result;
}


Tree_p Widget::anchor(Tree_p self, Tree_p child)
// ----------------------------------------------------------------------------
//   Anchor a set of shapes to the current position
// ----------------------------------------------------------------------------
{
    AnchorLayout *anchor = new AnchorLayout(this);
    anchor->id = layout->id;
    layout->Add(anchor);
    XL::LocalSave<Layout *> saveLayout(layout, anchor);
    if (selectNextTime.count(self))
    {
        selection[id]++;
        selectNextTime.erase(self);
    }
    Tree_p result = xl_evaluate(child);
    return result;
}


Tree_p Widget::resetTransform(Tree_p self)
// ----------------------------------------------------------------------------
//   Reset transform to original projection state
// ----------------------------------------------------------------------------
{
    layout->Add(new ResetTransform());
    return XL::xl_false;
}


static inline XL::Real *r(double x)
// ----------------------------------------------------------------------------
//   Utility shortcut to create a constant real value
// ----------------------------------------------------------------------------
{
    return new XL::Real(x);
}


Tree_p Widget::rotatex(Tree_p self, Real_p rx)
// ----------------------------------------------------------------------------
//   Rotate around X
// ----------------------------------------------------------------------------
{
    return rotate(self, rx, r(1), r(0), r(0));
}


Tree_p Widget::rotatey(Tree_p self, Real_p ry)
// ----------------------------------------------------------------------------
//   Rotate around Y
// ----------------------------------------------------------------------------
{
    return rotate(self, ry, r(0), r(1), r(0));
}


Tree_p Widget::rotatez(Tree_p self, Real_p rz)
// ----------------------------------------------------------------------------
//   Rotate around Z
// ----------------------------------------------------------------------------
{
    return rotate(self, rz, r(0), r(0), r(1));
}


Tree_p Widget::rotate(Tree_p self, Real_p ra, Real_p rx, Real_p ry, Real_p rz)
// ----------------------------------------------------------------------------
//    Rotation along an arbitrary axis
// ----------------------------------------------------------------------------
{
    layout->Add(new Rotation(ra, rx, ry, rz));
    layout->hasMatrix = true;
    return XL::xl_true;
}


Tree_p Widget::translatex(Tree_p self, Real_p x)
// ----------------------------------------------------------------------------
//   Translate along X
// ----------------------------------------------------------------------------
{
    return translate(self, x, r(0), r(0));
}


Tree_p Widget::translatey(Tree_p self, Real_p y)
// ----------------------------------------------------------------------------
//   Translate along Y
// ----------------------------------------------------------------------------
{
    return translate(self, r(0), y, r(0));
}


Tree_p Widget::translatez(Tree_p self, Real_p z)
// ----------------------------------------------------------------------------
//   Translate along Z
// ----------------------------------------------------------------------------
{
    return translate(self, r(0), r(0), z);
}


Tree_p Widget::translate(Tree_p self, Real_p tx, Real_p ty, Real_p tz)
// ----------------------------------------------------------------------------
//     Translation along three axes
// ----------------------------------------------------------------------------
{
    layout->Add(new Translation(tx, ty, tz));
    layout->hasMatrix = true;
    return XL::xl_true;
}


Tree_p Widget::rescalex(Tree_p self, Real_p x)
// ----------------------------------------------------------------------------
//   Rescale along X
// ----------------------------------------------------------------------------
{
    return rescale(self, x, r(1), r(1));
}


Tree_p Widget::rescaley(Tree_p self, Real_p y)
// ----------------------------------------------------------------------------
//   Rescale along Y
// ----------------------------------------------------------------------------
{
    return rescale(self, r(1), y, r(1));
}


Tree_p Widget::rescalez(Tree_p self, Real_p z)
// ----------------------------------------------------------------------------
//   Rescale along Z
// ----------------------------------------------------------------------------
{
    return rescale(self, r(1), r(1), z);
}


Tree_p Widget::rescale(Tree_p self, Real_p sx, Real_p sy, Real_p sz)
// ----------------------------------------------------------------------------
//     Scaling along three axes
// ----------------------------------------------------------------------------
{
    layout->Add(new Scale(sx, sy, sz));
    layout->hasMatrix = true;
    return XL::xl_true;
}


XL::Name_p Widget::depthTest(XL::Tree_p self, bool enable)
// ----------------------------------------------------------------------------
//   Change the delta we use for the depth
// ----------------------------------------------------------------------------
{
    layout->Add(new DepthTest(enable));
    return XL::xl_true;
}


Tree_p Widget::refresh(Tree_p self, double delay)
// ----------------------------------------------------------------------------
//    Refresh after the given number of seconds
// ----------------------------------------------------------------------------
{
    return refresh (delay) ? XL::xl_true : XL::xl_false;
}


XL::Name_p Widget::showSource(XL::Tree_p self, bool show)
// ----------------------------------------------------------------------------
//   Switch to full screen
// ----------------------------------------------------------------------------
{
    Window *window = (Window *) parentWidget();
    bool old = window->showSourceView(show);
    return old ? XL::xl_true : XL::xl_false;
}


XL::Name_p Widget::fullScreen(XL::Tree_p self, bool fs)
// ----------------------------------------------------------------------------
//   Switch to full screen
// ----------------------------------------------------------------------------
{
    bool oldFs = isFullScreen();
    Window *window = (Window *) parentWidget();
    window->switchToFullScreen(fs);
    return oldFs ? XL::xl_true : XL::xl_false;
}


XL::Name_p Widget::toggleFullScreen(XL::Tree_p self)
// ----------------------------------------------------------------------------
//   Switch to full screen
// ----------------------------------------------------------------------------
{
    return fullScreen(self, !isFullScreen());
}


XL::Name_p Widget::toggleHandCursor(XL::Tree_p self)
// ----------------------------------------------------------------------------
//   Switch between hand and arrow cursor
// ----------------------------------------------------------------------------
{
    bool isArrow = (cursor().shape() == Qt::ArrowCursor);
    showHandCursor(isArrow);
    return (!isArrow) ? XL::xl_true : XL::xl_false;
}


XL::Name_p Widget::resetView(XL::Tree_p self)
// ----------------------------------------------------------------------------
//   Restore default view parameters (zoom, position etc.)
// ----------------------------------------------------------------------------
{
    resetView();
    return XL::xl_true;
}


XL::Name_p Widget::panView(Tree_p self, coord dx, coord dy)
// ----------------------------------------------------------------------------
//   Pan the current view by the current amount
// ----------------------------------------------------------------------------
{
    eyeX += dx;
    eyeY += dy;
    centerX += dx;
    centerY += dy;

    setup(width(), height());
    updateGL();
    return XL::xl_true;
}


Real_p Widget::currentZoom(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the current zoom level
// ----------------------------------------------------------------------------
{
    return new Real(zoom);
}


Name_p Widget::setZoom(Tree_p self, scale z)
// ----------------------------------------------------------------------------
//   Decrease zoom level
// ----------------------------------------------------------------------------
{
    if (z > 0)
    {
        zoom = z;
        setup(width(), height());
        updateGL();
        return XL::xl_true;
    }
    return XL::xl_false;
}


Infix_p Widget::currentEyePosition(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the current eye position
// ----------------------------------------------------------------------------
{
    return new Infix(",", new Real(eyeX), new Real(eyeY));
}


Name_p Widget::setEyePosition(Tree_p self, coord x, coord y)
// ----------------------------------------------------------------------------
//   Set the eye position and update view
// ----------------------------------------------------------------------------
{
    eyeX = x;
    eyeY = y;
    setup(width(), height());
    updateGL();
    return XL::xl_true;
}


Infix_p Widget::currentCenterPosition(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the current center position
// ----------------------------------------------------------------------------
{
    return new Infix(",", new Real(centerX), new Real(centerY));
}


Name_p Widget::setCenterPosition(Tree_p self, coord x, coord y)
// ----------------------------------------------------------------------------
//   Set the center position and update view
// ----------------------------------------------------------------------------
{
    centerX = x;
    centerY = y;
    setup(width(), height());
    updateGL();
    return XL::xl_true;
}


Integer_p Widget::lastModifiers(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the current modifiers
// ----------------------------------------------------------------------------
{
    return new Integer(keyboardModifiers);
}


XL::Name_p Widget::enableAnimations(XL::Tree_p self, bool fs)
// ----------------------------------------------------------------------------
//   Enable or disable animations
// ----------------------------------------------------------------------------
{
    bool oldFs = hasAnimations();
    Window *window = (Window *) parentWidget();
    if (oldFs != fs)
        window->toggleAnimations();
    return oldFs ? XL::xl_true : XL::xl_false;
}


XL::Integer_p  Widget::polygonOffset(Tree_p self,
                                    double f0, double f1,
                                    double u0, double u1)
// ----------------------------------------------------------------------------
//   Set the polygon offset factors
// ----------------------------------------------------------------------------
{
    Layout::factorBase = f0;
    Layout::factorIncrement = f1;
    Layout::unitBase = u0;
    Layout::unitIncrement = u1;
    return new Integer(Layout::polygonOffset);
}


Name_p Widget::printPage(Tree_p self, text filename)
// ----------------------------------------------------------------------------
//    Print a page either to a file or by picking file
// ----------------------------------------------------------------------------
{
    if (filename == "")
    {
        QPrintDialog printDialog(this);
        if (printDialog.exec() != QDialog::Accepted)
            return XL::xl_false;
        filename = xlProgram->name + ".pdf";
        printDialog.printer()->setDocName(+filename);
    }


    FILE *fp = fopen(filename.c_str(), "wb");
    GLint buffsize = 0, state = GL2PS_OVERFLOW;
    GLint viewport[4];
    uint kind = GL2PS_PDF;

    if (filename.rfind(".pdf") != filename.npos)
        kind = GL2PS_PDF;
    else if (filename.rfind(".svg") != filename.npos)
        kind = GL2PS_SVG;
    else if (filename.rfind(".pgf") != filename.npos)
        kind = GL2PS_PGF;
    else if (filename.rfind(".tex") != filename.npos)
        kind = GL2PS_TEX;
    else if (filename.rfind(".eps") != filename.npos)
        kind = GL2PS_EPS;
    else if (filename.rfind(".ps") != filename.npos)
        kind = GL2PS_PS;

    glGetIntegerv(GL_VIEWPORT, viewport);

    // Disable locale if any, to avoid emitting 1,3 instead of 1.3 in files
    char *oldlocale = setlocale(LC_NUMERIC, "C");

    while(state == GL2PS_OVERFLOW)
    {
        buffsize += 1024*1024;
        gl2psBeginPage ( "Tao Output", "Tao", viewport,
                         kind, GL2PS_BSP_SORT,
                         GL2PS_DRAW_BACKGROUND |
                         GL2PS_SIMPLE_LINE_OFFSET |
                         GL2PS_OCCLUSION_CULL |
                         GL2PS_BEST_ROOT,
                         GL_RGBA, 0, NULL, 0, 0, 0, buffsize,
                         fp, filename.c_str());
        gl2psLineWidth(1);
        gl2psPointSize(1);
        gl2psBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        gl2psEnable(GL2PS_BLEND);
        space->printing = true;
        space->Draw(NULL);
        space->printing = false;
        state = gl2psEndPage();
    }

    setlocale(LC_NUMERIC, oldlocale);
    fclose(fp);

    return XL::xl_true;
}


Tree_p Widget::lineColor(Tree_p self, double r, double g, double b, double a)
// ----------------------------------------------------------------------------
//    Set the RGBA color for lines
// ----------------------------------------------------------------------------
{
    layout->Add(new LineColor(r, g, b, a));
    return XL::xl_true;
}


Tree_p Widget::lineWidth(Tree_p self, double lw)
// ----------------------------------------------------------------------------
//    Select the line width for OpenGL
// ----------------------------------------------------------------------------
{
    layout->Add(new LineWidth(lw));
    layout->hasAttributes = true;
    return XL::xl_true;
}


Tree_p Widget::lineStipple(Tree_p self, uint16 pattern, uint16 scale)
// ----------------------------------------------------------------------------
//    Select the line stipple pattern for OpenGL
// ----------------------------------------------------------------------------
{
    layout->Add(new LineStipple(pattern, scale));
    layout->hasAttributes = true;
    return XL::xl_true;
}


Tree_p Widget::fillColor(Tree_p self, double r, double g, double b, double a)
// ----------------------------------------------------------------------------
//    Set the RGBA color for fill
// ----------------------------------------------------------------------------
{
    layout->Add(new FillColor(r, g, b, a));
    return XL::xl_true;
}


Tree_p Widget::fillTexture(Tree_p self, text img)
// ----------------------------------------------------------------------------
//     Build a GL texture out of an image file
// ----------------------------------------------------------------------------
{
    GLuint texId = 0;

    if (img != "")
    {
        ImageTextureInfo *rinfo = self->GetInfo<ImageTextureInfo>();
        if (!rinfo)
        {
            rinfo = new ImageTextureInfo();
            self->SetInfo<ImageTextureInfo>(rinfo);
        }
        texId = rinfo->bind(img);
    }

    layout->Add(new FillTexture(texId));
    layout->hasAttributes = true;
    return XL::xl_true;
}


Tree_p Widget::fillTextureFromSVG(Tree_p self, text img)
// ----------------------------------------------------------------------------
//    Draw an image in SVG format
// ----------------------------------------------------------------------------
//    The image may be animated, in which case we will get repaintNeeded()
//    signals that we send to our 'draw()' so that we redraw as needed.
{
    GLuint texId = 0;
    if (img != "")
    {
        SvgRendererInfo *rinfo = self->GetInfo<SvgRendererInfo>();
        if (!rinfo)
        {
            rinfo = new SvgRendererInfo(this);
            self->SetInfo<SvgRendererInfo>(rinfo);
        }
        texId = rinfo->bind(img);
    }
    layout->Add(new FillTexture(texId));
    layout->hasAttributes = true;
    return XL::xl_true;
}


Tree *InsertImageWidthAndHeightAction::DoInfix(Infix *what)
// ----------------------------------------------------------------------------
// Action modifying the Infix before the "path" component.
// ----------------------------------------------------------------------------
{
    if ( done || what->name != "," || ! what->right->AsText())
        return what;

    Real *width = new Real(ww);
    Real *height = new Real(hh);
    Infix *inf2 = new XL::Infix (",", what->left, width);
    Infix *inf1 = new XL::Infix (",", inf2, height);
    what->left = inf1;

    done = true;
    return what;
}


Tree_p Widget::image(Tree_p self, Real_p x, Real_p y, text filename)
//----------------------------------------------------------------------------
//  Make an image : rewrite the source with image x,y,w,h,path
//----------------------------------------------------------------------------
//  If w or h is 0 then the image width or height is used and assigned to it.
{
    GLuint texId = 0;
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));

    ImageTextureInfo *rinfo = self->GetInfo<ImageTextureInfo>();
    if (!rinfo)
    {
        rinfo = new ImageTextureInfo();
        self->SetInfo<ImageTextureInfo>(rinfo);
    }
    texId = rinfo->bind(filename);

    layout->Add(new FillTexture(texId));
    layout->hasAttributes = true;

    Rectangle shape(Box(x-rinfo->width/2, y-rinfo->height/2,
                        rinfo->width, rinfo->height));
    layout->Add(new Rectangle(shape));

    // Replace image x,y,"toto" with x,y,w,h,"toto"
    InsertImageWidthAndHeightAction insertAct(rinfo->width, rinfo->height);
    self->Do(insertAct);

    // The structure of the program has changed, we need to recompile
    reloadProgram();
    markChanged("Image size added");

    return XL::xl_true;
}


Tree_p Widget::image(Tree_p self, Real_p x, Real_p y, Real_p w, Real_p h,
                     text filename)
//----------------------------------------------------------------------------
//  Make an image
//----------------------------------------------------------------------------
//  If w or h is 0 then the image width or height is used and assigned to it.
{
    GLuint texId = 0;
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));

    ImageTextureInfo *rinfo = self->GetInfo<ImageTextureInfo>();
    if (!rinfo)
    {
        rinfo = new ImageTextureInfo();
        self->SetInfo<ImageTextureInfo>(rinfo);
    }
    texId = rinfo->bind(filename);

    layout->Add(new FillTexture(texId));
    layout->hasAttributes = true;

    Rectangle shape(Box(x-w/2, y-h/2, w, h));
    layout->Add(new Rectangle(shape));

    if (currentShape)
        layout->Add(new ControlRectangle(currentShape, x, y, w, h));

    return XL::xl_true;
}


Tree_p Widget::textureWrap(Tree_p self, bool s, bool t)
// ----------------------------------------------------------------------------
//   Record if we want to wrap textures or clamp them
// ----------------------------------------------------------------------------
{
    layout->Add(new TextureWrap(s, t));
    return XL::xl_true;
}


Tree_p Widget::textureTransform(Tree_p self, Tree_p code)
// ----------------------------------------------------------------------------
//   Apply a texture transformation
// ----------------------------------------------------------------------------
{
    layout->hasTextureMatrix = true;
    layout->Add(new TextureTransform(true));
    Tree_p result = xl_evaluate(code);
    layout->Add(new TextureTransform(false));
    return result;
}



// ============================================================================
//
//    Path management
//
// ============================================================================

Tree_p Widget::newPath(Tree_p self, Tree_p child)
// ----------------------------------------------------------------------------
//   Evaluate the child tree within a polygon
// ----------------------------------------------------------------------------
{
    if (path)
        return Ooops("Path '$1' while evaluating a path", self);

    TesselatedPath *localPath = new TesselatedPath(GLU_TESS_WINDING_ODD);
    XL::LocalSave<GraphicPath *> save(path, localPath);
    layout->Add(localPath);
    if (currentShape)
        layout->Add(new GraphicPathManipulator(currentShape, localPath, self));
    Tree_p result = xl_evaluate(child);
    return result;
}


Tree_p Widget::moveTo(Tree_p self, Real_p x, Real_p y, Real_p z)
// ----------------------------------------------------------------------------
//    Add a 'moveTo' to the current path
// ----------------------------------------------------------------------------
{
    if (path)
    {
        path->moveTo(Point3(x,y,z));
        path->AddControl(self, x, y, z);
    }
    else
    {
        layout->Add(new MoveTo(x, y, z));
    }
    return XL::xl_true;
}


Tree_p Widget::lineTo(Tree_p self, Real_p x, Real_p y, Real_p z)
// ----------------------------------------------------------------------------
//    Add a 'lineTo' to the current path
// ----------------------------------------------------------------------------
{
    if (!path)
        return Ooops("No path for '$1'", self);
    path->lineTo(Point3(x,y,z));
    path->AddControl(self, x, y, z);
    return XL::xl_true;
}


Tree_p Widget::curveTo(Tree_p self,
                       Real_p cx, Real_p cy, Real_p cz,
                       Real_p x, Real_p y, Real_p z)
// ----------------------------------------------------------------------------
//    Add a quadric curveTo to the current path
// ----------------------------------------------------------------------------
{
    if (!path)
        return Ooops("No path for '$1'", self);
    path->curveTo(Point3(cx, cy, cz), Point3(x,y,z));
    path->AddControl(self, x, y, z);
    path->AddControl(self, cx, cy, cz);
    return XL::xl_true;
}


Tree_p Widget::curveTo(Tree_p self,
                       Real_p c1x, Real_p c1y, Real_p c1z,
                       Real_p c2x, Real_p c2y, Real_p c2z,
                       Real_p x, Real_p y, Real_p z)
// ----------------------------------------------------------------------------
//    Add a cubic curveTo to the current path
// ----------------------------------------------------------------------------
{
    if (!path)
        return Ooops("No path for '$1'", self);
    path->curveTo(Point3(c1x, c1y, c1z), Point3(c2x, c2y, c2z), Point3(x,y,z));
    path->AddControl(self, x, y, z);
    path->AddControl(self, c1x, c1y, c1z);
    path->AddControl(self, c2x, c2y, c2z);
    return XL::xl_true;
}


Tree_p Widget::moveToRel(Tree_p self, Real_p x, Real_p y, Real_p z)
// ----------------------------------------------------------------------------
//    Add a relative moveTo
// ----------------------------------------------------------------------------
{
    if (path)
    {
        path->moveTo(Vector3(x,y,z));
        path->AddControl(self, x, y, z);
    }
    else
    {
        layout->Add(new MoveToRel(x, y, z));
    }
    return XL::xl_true;
}


Tree_p Widget::lineToRel(Tree_p self, Real_p x, Real_p y, Real_p z)
// ----------------------------------------------------------------------------
//    Add a 'lineTo' to the current path
// ----------------------------------------------------------------------------
{
    if (!path)
        return Ooops("No path for '$1'", self);
    path->lineTo(Vector3(x,y,z));
    path->AddControl(self, x, y, z);
    return XL::xl_true;
}


Tree_p Widget::pathTextureCoord(Tree_p self, Real_p x, Real_p y, Real_p r)
// ----------------------------------------------------------------------------
//    Add a texture coordinate to the path
// ----------------------------------------------------------------------------
{
    return XL::Ooops ("Path texture coordinate '$1' not supported yet", self);
}


Tree_p Widget::pathColor(Tree_p self, Real_p r, Real_p g, Real_p b, Real_p a)
// ----------------------------------------------------------------------------
//   Add a color element to the path
// ----------------------------------------------------------------------------
{
    return XL::Ooops ("Path color coordinate '$1' not supported yet", self);
}


Tree_p Widget::closePath(Tree_p self)
// ----------------------------------------------------------------------------
//    Close the path back to its origin
// ----------------------------------------------------------------------------
{
    if (!path)
        return Ooops("No path for '$1'", self);
    path->close();
    return XL::xl_true;
}


static GraphicPath::EndpointStyle endpointStyle(symbol_r n)
// ----------------------------------------------------------------------------
//   Translates XL name into endpoint style enum
// ----------------------------------------------------------------------------
{
    text name = n.value;
    std::transform(name.begin(), name.end(), name.begin(), ::toupper);

    if (name == "NONE")
    {
        return GraphicPath::NONE;
    }
    else if (name == "ARROWHEAD")
    {
        return GraphicPath::ARROWHEAD;
    }
    else if (name == "TRIANGLE")
    {
        return GraphicPath::TRIANGLE;
    }
    else if (name == "POINTER")
    {
        return GraphicPath::POINTER;
    }
    else if (name == "DIAMOND")
    {
        return GraphicPath::DIAMOND;
    }
    else if (name == "CIRCLE")
    {
        return GraphicPath::CIRCLE;
    }
    else if (name == "SQUARE")
    {
        return GraphicPath::SQUARE;
    }
    else if (name == "BAR")
    {
        return GraphicPath::BAR;
    }
    else if (name == "CUP")
    {
        return GraphicPath::CUP;
    }
    else if (name == "FLETCHING")
    {
        return GraphicPath::FLETCHING;
    }
    else if (name == "HOLLOW_CIRCLE")
    {
        return GraphicPath::HOLLOW_CIRCLE;
    }
    else if (name == "HOLLOW_SQUARE")
    {
        return GraphicPath::HOLLOW_SQUARE;
    }
    else
    {
        // Others...
        return GraphicPath::NONE;
    }
}

Tree_p Widget::endpointsStyle(Tree_p self, symbol_r s, symbol_r e)
// ----------------------------------------------------------------------------
//   Specify the style of the path endpoints
// ----------------------------------------------------------------------------
{
    if (!path)
        return Ooops("No path for '$1'", self);

    path->startStyle = endpointStyle(s);
    path->endStyle   = endpointStyle(e);

    return XL::xl_true;
}


// ============================================================================
//
//    2D primitives that can be in a path or standalone
//
// ============================================================================

Tree_p Widget::fixedSizePoint(Tree_p self, coord x,coord y,coord z, coord s)
// ----------------------------------------------------------------------------
//   Draw a point with the given size
// ----------------------------------------------------------------------------
{
    layout->Add(new FixedSizePoint(Point3(x, y, z), s));
    return XL::xl_true;
}


Tree_p Widget::rectangle(Tree_p self, Real_p x, Real_p y, Real_p w, Real_p h)
// ----------------------------------------------------------------------------
//    Draw a rectangle
// ----------------------------------------------------------------------------
{
    Rectangle shape(Box(x-w/2, y-h/2, w, h));
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new Rectangle(shape));

    if (currentShape)
        layout->Add(new ControlRectangle(currentShape, x, y, w, h));

    return XL::xl_true;
}


Tree_p Widget::isoscelesTriangle(Tree_p self,
                                 Real_p x, Real_p y, Real_p w, Real_p h)
// ----------------------------------------------------------------------------
//    Draw an isosceles triangle
// ----------------------------------------------------------------------------
{
    IsoscelesTriangle shape(Box(x-w/2, y-h/2, w, h));
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new IsoscelesTriangle(shape));

    if (currentShape)
        layout->Add(new ControlRectangle(currentShape, x, y, w, h));

    return XL::xl_true;
}


Tree_p Widget::rightTriangle(Tree_p self,
                             Real_p x, Real_p y, Real_p w, Real_p h)
// ----------------------------------------------------------------------------
//    Draw a right triangle
// ----------------------------------------------------------------------------
{
    RightTriangle shape(Box(x-w/2, y-h/2, w, h));
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new RightTriangle(shape));

    if (currentShape)
        layout->Add(new ControlRectangle(currentShape, x, y, w, h));

    return XL::xl_true;
}


Tree_p Widget::ellipse(Tree_p self, Real_p cx, Real_p cy, Real_p w, Real_p h)
// ----------------------------------------------------------------------------
//   Circle centered around (cx,cy), size w * h
// ----------------------------------------------------------------------------
{
    Ellipse shape(Box(cx-w/2, cy-h/2, w, h));
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new Ellipse(shape));

    if (currentShape)
        layout->Add(new ControlRectangle(currentShape, cx, cy, w, h));

    return XL::xl_true;
}


Tree_p Widget::ellipseArc(Tree_p self,
                          Real_p cx, Real_p cy, Real_p w, Real_p h,
                          Real_p start, Real_p sweep)
// ----------------------------------------------------------------------------
//   Circular sector centered around (cx,cy)
// ----------------------------------------------------------------------------
{
    EllipseArc shape(Box(cx-w/2, cy-h/2, w, h), start, sweep);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new EllipseArc(shape));

    if (currentShape)
        layout->Add(new ControlRectangle(currentShape, cx, cy, w, h));

    return XL::xl_true;
}


Tree_p Widget::roundedRectangle(Tree_p self,
                                Real_p cx, Real_p cy,
                                Real_p w, Real_p h, Real_p r)
// ----------------------------------------------------------------------------
//   Rounded rectangle with radius r for the rounded corners
// ----------------------------------------------------------------------------
{
    RoundedRectangle shape(Box(cx-w/2, cy-h/2, w, h), r);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new RoundedRectangle(shape));

    if (currentShape)
        layout->Add(new ControlRoundedRectangle(currentShape, cx,cy,w,h, r));


    return XL::xl_true;
}


Tree_p Widget::ellipticalRectangle(Tree_p self,
                                   Real_p cx, Real_p cy,
                                   Real_p w, Real_p h, Real_p r)
// ----------------------------------------------------------------------------
//   Elliptical rectangle with ratio r for the elliptic sides
// ----------------------------------------------------------------------------
{
    EllipticalRectangle shape(Box(cx-w/2, cy-h/2, w, h), r);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new EllipticalRectangle(shape));

    if (currentShape)
        layout->Add(new ControlRoundedRectangle(currentShape,
                                                cx, cy, w, h, r));

    return XL::xl_true;
}


Tree_p Widget::arrow(Tree_p self,
                     Real_p cx, Real_p cy, Real_p w, Real_p h,
                     Real_p ax, Real_p ary)
// ----------------------------------------------------------------------------
//   Arrow
// ----------------------------------------------------------------------------
{
    Arrow shape(Box(cx-w/2, cy-h/2, w, h), ax, ary);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new Arrow(shape));

    if (currentShape)
        layout->Add(new ControlArrow(currentShape, cx, cy, w, h, ax, ary));

    return XL::xl_true;
}


Tree_p Widget::doubleArrow(Tree_p self,
                           Real_p cx, Real_p cy, Real_p w, Real_p h,
                           Real_p ax, Real_p ary)
// ----------------------------------------------------------------------------
//   Double arrow
// ----------------------------------------------------------------------------
{
    DoubleArrow shape(Box(cx-w/2, cy-h/2, w, h), ax, ary);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new DoubleArrow(shape));

    if (currentShape)
        layout->Add(new ControlArrow(currentShape, cx,cy,w,h, ax,ary, true));

    return XL::xl_true;
}


Tree_p Widget::starPolygon(Tree_p self,
                           Real_p cx, Real_p cy, Real_p w, Real_p h,
                           Integer_p p, Integer_p q)
// ----------------------------------------------------------------------------
//     GL regular p-side star polygon {p/q} centered around (cx,cy)
// ----------------------------------------------------------------------------
{
    StarPolygon shape(Box(cx-w/2, cy-h/2, w, h), p, q);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new StarPolygon(shape));

    if (currentShape)
        layout->Add(new ControlPolygon(currentShape, cx, cy, w, h, p));

    return XL::xl_true;
}


Tree_p Widget::star(Tree_p self,
                    Real_p cx, Real_p cy, Real_p w, Real_p h,
                    Integer_p p, Real_p r)
// ----------------------------------------------------------------------------
//     GL regular p-side star centered around (cx,cy), inner radius ratio r
// ----------------------------------------------------------------------------
{
    Star shape(Box(cx-w/2, cy-h/2, w, h), p, r);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new Star(shape));

    if (currentShape)
        layout->Add(new ControlStar(currentShape, cx, cy, w, h, p, r));

    return XL::xl_true;
}


Tree_p Widget::speechBalloon(Tree_p self,
                             Real_p cx, Real_p cy, Real_p w, Real_p h,
                             Real_p r, Real_p ax, Real_p ay)
// ----------------------------------------------------------------------------
//   Speech balloon with radius r for rounded corners, and point a for the tail
// ----------------------------------------------------------------------------
{
    SpeechBalloon shape(Box(cx-w/2, cy-h/2, w, h), r, ax, ay);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new SpeechBalloon(shape));

    if (currentShape)
        layout->Add(new ControlBalloon(currentShape, cx, cy, w, h, r, ax, ay));

    return XL::xl_true;
}


Tree_p Widget::callout(Tree_p self,
                       Real_p cx, Real_p cy, Real_p w, Real_p h,
                       Real_p r, Real_p ax, Real_p ay, Real_p d)
// ----------------------------------------------------------------------------
//   Callout with radius r for corners, and point a, width b for the tail
// ----------------------------------------------------------------------------
{
    Callout shape(Box(cx-w/2, cy-h/2, w, h), r, ax, ay, d);
    if (path)
        shape.Draw(*path);
    else
        layout->Add(new Callout(shape));

    if (currentShape)
        layout->Add(new ControlCallout(currentShape,
                                       cx, cy, w, h,
                                       r, ax, ay, d));

    return XL::xl_true;
}


XL::Tree_p Widget::debugBinPacker(Tree_p self, uint w, uint h, Tree_p t)
// ----------------------------------------------------------------------------
//   Debug the bin packer
// ----------------------------------------------------------------------------
{
    BinPacker binpack(w, h);
    GraphicPath *path = new GraphicPath;

    struct BinPackerTest : XL::Action
    {
        BinPackerTest(GraphicPath *path, BinPacker &bp)
            : path(path), bp(bp), w(0) {}

        virtual Tree *Do (Tree *what) { return what; }
        void Allocate(uint w, uint h)
        {
            BinPacker::Rect rect;
            while (!bp.Allocate(w, h, rect))
            {
                uint ww = bp.Width();
                uint hh = bp.Height();
                do { ww <<= 1; } while (ww < w);
                do { hh <<= 1; } while (hh < h);
                bp.Resize(ww, hh);
            }

            path->moveTo(Point3(rect.x1, rect.y1, 0));
            path->lineTo(Point3(rect.x1, rect.y2, 0));
            path->lineTo(Point3(rect.x2, rect.y2, 0));
            path->lineTo(Point3(rect.x2, rect.y1, 0));
            path->close();
        }
        Tree *DoInteger (Integer *what)
        {
            if (!w)
            {
                w = what->value;
            }
            else
            {
                Allocate(w, what->value);
                w = 0;
            }
            return what;
        }
        Tree *DoText(Text *what)
        {
            QFont font(+what->value, w ? w : -1);
            QFontMetrics fm(font);
            for (uint i = 32; i < 256; i++)
            {
                QChar qc(i);
                QRect bounds(fm.boundingRect(qc));
                Allocate(bounds.width(), bounds.height());
            }
            return what;
        }
        GraphicPath *path;
        BinPacker   &bp;
        uint         w;
    } binPackerTest(path, binpack);

    t->Do(binPackerTest);
    layout->Add(path);

    return XL::xl_false;
}


double debugX = 0, debugY = 0, debugW = 0, debugH = 0;
XL::Tree_p Widget::debugParameters(Tree_p self,
                                   double x, double y,
                                   double w, double h)
// ----------------------------------------------------------------------------
//   Set debug parameters for fine-tuning stuff
// ----------------------------------------------------------------------------
{
    debugX = x;
    debugY = y;
    debugW = w;
    debugH = h;
    return XL::xl_false;
}



// ============================================================================
//
//    3D primitives
//
// ============================================================================

Tree_p Widget::sphere(Tree_p self,
                      Real_p x, Real_p y, Real_p z,
                      Real_p w, Real_p h, Real_p d,
                      Integer_p slices, Integer_p stacks)
// ----------------------------------------------------------------------------
//     GL sphere
// ----------------------------------------------------------------------------
{
    layout->Add(new Sphere(Box3(x-w/2, y-h/2, z-d/2, w,h,d), slices, stacks));
    if (currentShape)
        layout->Add (new ControlBox(currentShape, x, y, z, w, h, d));
    return XL::xl_true;
}


Tree_p Widget::cube(Tree_p self,
                    Real_p x, Real_p y, Real_p z,
                    Real_p w, Real_p h, Real_p d)
// ----------------------------------------------------------------------------
//    A simple cubic box
// ----------------------------------------------------------------------------
{
    layout->Add(new Cube(Box3(x-w/2, y-h/2, z-d/2, w,h,d)));
    if (currentShape)
        layout->Add(new ControlBox(currentShape, x, y, z, w, h, d));
    return XL::xl_true;
}


Tree_p Widget::cone(Tree_p self,
                    Real_p x, Real_p y, Real_p z,
                    Real_p w, Real_p h, Real_p d)
// ----------------------------------------------------------------------------
//    A simple cone
// ----------------------------------------------------------------------------
{
    layout->Add(new Cone(Box3(x-w/2, y-h/2, z-d/2, w,h,d)));
    if (currentShape)
        layout->Add(new ControlBox(currentShape, x, y, z, w, h, d));
    return XL::xl_true;
}


Tree_p Widget::object(Tree_p self,
                      Real_p x, Real_p y, Real_p z,
                      Real_p w, Real_p h, Real_p d,
                      Text_p name)
// ----------------------------------------------------------------------------
//   Load a 3D object
// ----------------------------------------------------------------------------
{
    // Try to load the 3D object in memory and graphic card
    Object3D *obj = Object3D::Object(name);
    if (!obj)
        return XL::xl_false;

    // Update object dimensions if we didn't specify them
    if (w->value <= 0 || h->value <= 0 || d->value <= 0)
    {
        Box3 &bounds = obj->bounds;
        if (w->value <= 0)
            w->value = bounds.Width();
        if (h->value <= 0)
            h->value = bounds.Height();
        if (d->value <= 0)
            d->value = bounds.Depth();
        markChanged ("Update object dimensions");
    }

    // Add the object
    layout->Add(new Object3DDrawing(obj, x, y, z, w, h, d));
    if (currentShape)
        layout->Add(new ControlBox(currentShape, x, y, z, w, h, d));

    return XL::xl_true;
}



// ============================================================================
//
//    Text and font
//
// ============================================================================

Tree_p  Widget::textBox(Tree_p self,
                        Real_p x, Real_p y, Real_p w, Real_p h, Tree_p prog)
// ----------------------------------------------------------------------------
//   Create a new page layout and render text in it
// ----------------------------------------------------------------------------
{
    PageLayout *tbox = new PageLayout(this);
    tbox->space = Box3(x - w/2, y-h/2, 0, w, h, 0);
    layout->Add(tbox);
    flows[flowName] = tbox;

    if (currentShape)
    {
        tbox->id = layout->id;
        layout->Add(new ControlRectangle(currentShape, x, y, w, h));
    }

    XL::LocalSave<Layout *> save(layout, tbox);
    return xl_evaluate(prog);
}


Tree_p Widget::textOverflow(Tree_p self,
                            Real_p x, Real_p y, Real_p w, Real_p h)
// ----------------------------------------------------------------------------
//   Overflow text box for the rest of the current text flow
// ----------------------------------------------------------------------------
{
    // Add page layout overflow rectangle
    PageLayoutOverflow *overflow =
        new PageLayoutOverflow(Box(x - w/2, y-h/2, w, h), this, flowName);
    layout->Add(overflow);
    if (currentShape)
        layout->Add(new ControlRectangle(currentShape, x, y, w, h));

    return XL::xl_true;
}


XL::Text_p Widget::textFlow(Tree_p self, text name)
// ----------------------------------------------------------------------------
//    Set the name of the current text flow
// ----------------------------------------------------------------------------
{
    text oldName = flowName;
    flowName = name;
    return new XL::Text(oldName);
}


Tree_p Widget::textSpan(Tree_p self, Text_p contents)
// ----------------------------------------------------------------------------
//   Insert a block of text with the current definition of font, color, ...
// ----------------------------------------------------------------------------
{
    if (path)
        TextSpan(contents).Draw(*path, layout);
    else
        layout->Add(new TextSpan(contents));
    return XL::xl_true;
}


Tree_p Widget::textFormula(Tree_p self, Tree_p value)
// ----------------------------------------------------------------------------
//   Insert a block of text corresponding to the given formula
// ----------------------------------------------------------------------------
{
    XL::Prefix_p prefix = self->AsPrefix();
    assert(prefix);

    if (path)
        TextFormula(prefix, this).Draw(*path, layout);
    else
        layout->Add(new TextFormula(prefix, this));
    return value;
}


Tree_p Widget::font(Tree_p self, Tree_p description)
// ----------------------------------------------------------------------------
//   Select a font family
// ----------------------------------------------------------------------------
{
    FontParsingAction parseFont(self->Symbols(), layout->font);
    description->Do(parseFont);
    layout->font = parseFont.font;
    layout->Add(new FontChange(layout->font));
    if (fontFileMgr)
        fontFileMgr->AddFontFiles(layout->font);
    return XL::xl_true;
}


Tree_p Widget::fontSize(Tree_p self, double size)
// ----------------------------------------------------------------------------
//   Select a font size
// ----------------------------------------------------------------------------
{
    layout->font.setPointSizeF(size);
    layout->Add(new FontChange(layout->font));
    return XL::xl_true;
}


Tree_p Widget::fontScaling(Tree_p self, double scaling, double minAASize)
// ----------------------------------------------------------------------------
//   Change the font scaling factor
// ----------------------------------------------------------------------------
{
    if (glyphCache.fontScaling != scaling ||
        glyphCache.minFontSizeForAntialiasing != minAASize)
    {
        glyphCache.Clear();
        glyphCache.fontScaling = scaling;
        glyphCache.minFontSizeForAntialiasing = minAASize;
    }
    return XL::xl_true;
}


Tree_p Widget::fontPlain(Tree_p self)
// ----------------------------------------------------------------------------
//   Select whether this is italic or not
// ----------------------------------------------------------------------------
{
    QFont &font = layout->font;
    font.setStyle(QFont::StyleNormal);
    font.setWeight(QFont::Normal);
    font.setStretch(QFont::Unstretched);
    font.setUnderline(false);
    font.setStrikeOut(false);
    font.setOverline(false);
    layout->Add(new FontChange(font));
    return XL::xl_true;
}


static inline scale clamp(scale value, scale min, scale max)
// ----------------------------------------------------------------------------
//   Clamp the input value between the min and max given
// ----------------------------------------------------------------------------
{
    if (value < min)    value = min;
    if (value > max)    value = max;
    return value;
}


Tree_p Widget::fontItalic(Tree_p self, scale amount)
// ----------------------------------------------------------------------------
//   Select whether this is italic or not
// ----------------------------------------------------------------------------
//   Qt italic values range from 0 (Normal) to 2 (Oblique)
{
    amount = clamp(amount, 0, 2);
    layout->font.setStyle(QFont::Style(amount));
    layout->Add(new FontChange(layout->font));
    return XL::xl_true;
}


Tree_p Widget::fontBold(Tree_p self, scale amount)
// ----------------------------------------------------------------------------
//   Select whether the font is bold or not
// ----------------------------------------------------------------------------
//   Qt weight values range from 0 to 99 with 50 = regular
{
    amount = clamp(amount, 0, 99);
    layout->font.setWeight(QFont::Weight(amount));
    layout->Add(new FontChange(layout->font));
    return XL::xl_true;
}


Tree_p Widget::fontUnderline(Tree_p self, scale amount)
// ----------------------------------------------------------------------------
//    Select whether we underline a font
// ----------------------------------------------------------------------------
//    Qt doesn't support setting the size of the underline, it's on or off
{
    layout->font.setUnderline(bool(amount));
    layout->Add(new FontChange(layout->font));
    return XL::xl_true;
}


Tree_p Widget::fontOverline(Tree_p self, scale amount)
// ----------------------------------------------------------------------------
//    Select whether we draw an overline
// ----------------------------------------------------------------------------
//    Qt doesn't support setting the size of the overline, it's on or off
{
    layout->font.setOverline(bool(amount));
    layout->Add(new FontChange(layout->font));
    return XL::xl_true;
}


Tree_p Widget::fontStrikeout(Tree_p self, scale amount)
// ----------------------------------------------------------------------------
//    Select whether we strikeout a font
// ----------------------------------------------------------------------------
//    Qt doesn't support setting the size of the strikeout, it's on or off
{
    layout->font.setStrikeOut(bool(amount));
    layout->Add(new FontChange(layout->font));
    return XL::xl_true;
}


Tree_p Widget::fontStretch(Tree_p self, scale amount)
// ----------------------------------------------------------------------------
//    Set font stretching factor
// ----------------------------------------------------------------------------
//    Qt font stretch ranges from 0 to 4000, with 100 = 100%.
{
    amount = clamp(amount, 0, 40);
    layout->font.setStretch(int(amount * 100));
    layout->Add(new FontChange(layout->font));
    return XL::xl_true;
}


static inline JustificationChange::Axis jaxis(uint a)
// ----------------------------------------------------------------------------
//   Return the right justification axis
// ----------------------------------------------------------------------------
{
    switch(a)
    {
    default:
    case 0: return JustificationChange::AlongX;
    case 1: return JustificationChange::AlongY;
    case 2: return JustificationChange::AlongZ;
    }
}


Tree_p Widget::justify(Tree_p self, scale amount, uint axis)
// ----------------------------------------------------------------------------
//   Change justification along the given axis
// ----------------------------------------------------------------------------
{
    layout->Add(new JustificationChange(amount, jaxis(axis)));
    return XL::xl_true;
}


Tree_p Widget::center(Tree_p self, scale amount, uint axis)
// ----------------------------------------------------------------------------
//   Change centering along the given axis
// ----------------------------------------------------------------------------
{
    layout->Add(new CenteringChange(amount, jaxis(axis)));
    return XL::xl_true;
}


Tree_p Widget::spread(Tree_p self, scale amount, uint axis)
// ----------------------------------------------------------------------------
//   Change the spread along the given axis
// ----------------------------------------------------------------------------
{
    layout->Add(new SpreadChange(amount, jaxis(axis)));
    return XL::xl_true;
}


Tree_p Widget::spacing(Tree_p self, scale amount, uint axis)
// ----------------------------------------------------------------------------
//   Change the spacing along the given axis
// ----------------------------------------------------------------------------
{
    layout->Add(new SpacingChange(amount, jaxis(axis)));
    return XL::xl_true;
}


Tree_p Widget::minimumSpace(Tree_p self, coord before, coord after, uint axis)
// ----------------------------------------------------------------------------
//   Define the paragraph or word space
// ----------------------------------------------------------------------------
{
    layout->Add(new MinimumSpacingChange(before, after, jaxis(axis)));
    return XL::xl_true;
}


Tree_p Widget::horizontalMargins(Tree_p self, coord left, coord right)
// ----------------------------------------------------------------------------
//   Set the horizontal margin for text
// ----------------------------------------------------------------------------
{
    layout->Add(new HorizontalMarginChange(left, right));
    return XL::xl_true;
}


Tree_p Widget::verticalMargins(Tree_p self, coord top, coord bottom)
// ----------------------------------------------------------------------------
//   Set the vertical margin for text
// ----------------------------------------------------------------------------
{
    layout->Add(new VerticalMarginChange(top, bottom));
    return XL::xl_true;
}


Tree_p Widget::drawingBreak(Tree_p self, Drawing::BreakOrder order)
// ----------------------------------------------------------------------------
//   Change the spacing along the given axis
// ----------------------------------------------------------------------------
{
    layout->Add(new DrawingBreak(order));
    return XL::xl_true;
}


XL::Name_p Widget::textEditKey(Tree_p self, text key)
// ----------------------------------------------------------------------------
//   Send a key to the activities
// ----------------------------------------------------------------------------
{
    // Check if we are changing pages here...
    if (pageLinks.count(key))
    {
        pageName = pageLinks[key];
        selection.clear();
        selectionTrees.clear();
        delete textSelection();
        delete drag();
        pageStartTime = startTime = frozenTime = CurrentTime();
        draw();
        refresh(0);
        return XL::xl_true;
    }

    for (Activity *a = activities; a; a = a->Key(key)) ;
    return XL::xl_true;
}


XL::Text_p Widget::loremIpsum(Tree_p self, Integer_p nwords)
// ----------------------------------------------------------------------------
//    Generate arbitrary length dummy text based on the well-known sequence
// ----------------------------------------------------------------------------
{
    if (!nwords)
        return new XL::Text("");

    static struct LoremWords {
        LoremWords()
        {
            using namespace std;
            string lorem =
            "Lorem ipsum dolor sit amet, consectetur adipisicing elit, sed do "
            "eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut "
            "enim ad minim veniam, quis nostrud exercitation ullamco laboris "
            "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor "
            "in reprehenderit in voluptate velit esse cillum dolore eu fugiat "
            "nulla pariatur. Excepteur sint occaecat cupidatat non proident, "
            "sunt in culpa qui officia deserunt mollit anim id est laborum.";
            istringstream iss(lorem);
            std::copy(istream_iterator<string>(iss),
                      istream_iterator<string>(),
                      back_inserter<vector<string> >(words));
        }
        std::vector<std::string> words;
    } lorem;

    std::string ret = lorem.words[0];
    int size = lorem.words.size();
    for (int i = 1; i < nwords; i++)
        ret += " " + lorem.words[i % size];

    std::string::reverse_iterator i = ret.rbegin();
    if (!ispunct(*i))
        ret += ".";
    else if (*i != '.')
        *i = '.';

    return new XL::Text(ret);
}


Text_p Widget::loadText(Tree_p self, text file)
// ----------------------------------------------------------------------------
//    Load a text file from disk
// ----------------------------------------------------------------------------
{
    std::ostringstream output;
    text qualified = "doc:" + file;
    QFileInfo fileInfo(+qualified);
    if (fileInfo.exists())
    {
        text path = +fileInfo.canonicalFilePath();
        std::ifstream input(path.c_str());
        while (input.good())
        {
            char c = input.get();
            if (input.good())
                output << c;
        }
    }
    text contents = output.str();
    return new XL::Text(contents);
}



// ============================================================================
//
//   Tables
//
// ============================================================================

Tree_p Widget::newTable(Tree_p self,
                        Integer_p rows, Integer_p columns,
                        Tree_p body)
// ----------------------------------------------------------------------------
//   Case of a new table without a position
// ----------------------------------------------------------------------------
{
    return newTable(self, r(0), r(0), rows, columns, body);
}


Tree_p Widget::newTable(Tree_p self, Real_p x, Real_p y,
                        Integer_p r, Integer_p c, Tree_p body)
// ----------------------------------------------------------------------------
//   Create a new table
// ----------------------------------------------------------------------------
{
    Table *tbl = new Table(this, x, y, r, c);
    XL::LocalSave<Table *> saveTable(table, tbl);
    layout->Add(tbl);

    if (currentShape)
        layout->Add(new TableManipulator(currentShape, x, y, tbl));

    // Patch the symbol table with short versions of table_xyz functions
    if (Prefix *prefix = self->AsPrefix())
    {
        NameToNameReplacement replacer;
        replacer["cell"]    = "table_cell";
        replacer["fill"]    = "table_fill";
        replacer["margins"] = "table_cell_margins";
        replacer["fill"]    = "table_cell_fill";
        replacer["border"]  = "table_cell_border";
        replacer["x"]       = "table_cell_x";
        replacer["y"]       = "table_cell_y";
        replacer["w"]       = "table_cell_w";
        replacer["h"]       = "table_cell_h";
        replacer["row"]     = "table_cell_row";
        replacer["column"]  = "table_cell_column";
        replacer["rows"]    = "table_rows";
        replacer["columns"] = "table_columns";
        if (!prefix->right->Symbols())
            prefix->right->SetSymbols(self->Symbols());
        Tree *tablified = replacer.Replace(prefix->right);
        if (replacer.replaced)
        {
            prefix->right = tablified;
            reloadProgram();
            return XL::xl_false;
        }
    }

    return xl_evaluate(body);
}


Tree_p Widget::tableCell(Tree_p self, Real_p w, Real_p h, Tree_p body)
// ----------------------------------------------------------------------------
//   Define a sized cell in the table
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table cell '$1' outside of any table", self);
    if (!body->Symbols())
        body->SetSymbols(self->Symbols());

    // Define a new text layout
    PageLayout *tbox = new PageLayout(this);
    tbox->space = Box3(0, 0, 0, w, h, 0);
    table->Add(tbox);
    flows[flowName] = tbox;

    XL::LocalSave<Layout *> save(layout, tbox);
    return xl_evaluate(body);
}


Tree_p Widget::tableCell(Tree_p self, Tree_p body)
// ----------------------------------------------------------------------------
//   Define a free-size cell in the table
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table cell '$1' outside of any table", self);
    if (!body->Symbols())
        body->SetSymbols(self->Symbols());

    // Define a new text layout
    Layout *tbox = new Layout(this);
    table->Add(tbox);

    XL::LocalSave<Layout *> save(layout, tbox);
    return xl_evaluate(body);
}


Tree_p Widget::tableMargins(Tree_p self,
                            Real_p x, Real_p y, Real_p w, Real_p h)
// ----------------------------------------------------------------------------
//   Set the margin rectangle for the table
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table margins '$1' outside of any table", self);
    table->margins = Box(x-w/2, y-h/2, w, h);
    return XL::xl_true;
}


Tree_p Widget::tableMargins(Tree_p self, Real_p w, Real_p h)
// ----------------------------------------------------------------------------
//   Set the margin rectangle for the table
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table margins '$1' outside of any table", self);
    table->margins = Box(-w/2, -h/2, w, h);
    return XL::xl_true;
}


Tree_p Widget::tableFill(Tree_p self, Tree_p code)
// ----------------------------------------------------------------------------
//   Define the fill code for cells
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table fill '$1' outside of any table", self);
    if (!code->Symbols())
        code->SetSymbols(self->Symbols());
    table->fill = code;
    return XL::xl_true;
}


Tree_p Widget::tableBorder(Tree_p self, Tree_p code)
// ----------------------------------------------------------------------------
//   Define the border code for cells
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table border '$1' outside of any table", self);
    if (!code->Symbols())
        code->SetSymbols(self->Symbols());
    table->border = code;
    return XL::xl_true;
}


Real_p Widget::tableCellX(Tree_p self)
// ----------------------------------------------------------------------------
//   Get the horizontal center of the current table cell
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table cell position '$1' without a table", self)
            ->AsReal();
    return new Real(table->cellBox.Center().x, self->Position());
}


Real_p Widget::tableCellY(Tree_p self)
// ----------------------------------------------------------------------------
//   Get the vertical center of the current table cell
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table cell position '$1' without a table", self)
            ->AsReal();
    return new Real(table->cellBox.Center().y, self->Position());
}


Real_p Widget::tableCellW(Tree_p self)
// ----------------------------------------------------------------------------
//   Get the horizontal size of the current table cell
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table cell size '$1' without a table", self)
            ->AsReal();
    return new Real(table->cellBox.Width(), self->Position());
}


Real_p Widget::tableCellH(Tree_p self)
// ----------------------------------------------------------------------------
//   Get the vertical size of the current table cell
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table cell size '$1' without a table", self)
            ->AsReal();
    return new Real(table->cellBox.Height(), self->Position());
}


Integer_p Widget::tableRow(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the current row
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table cell attribute '$1' without a table", self)
            ->AsInteger();
    return new Integer(table->row, self->Position());
}


Integer_p Widget::tableColumn(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the current column
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table cell attribute '$1' without a table", self)
            ->AsInteger();
    return new Integer(table->column, self->Position());
}


Integer_p Widget::tableRows(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the number of rows in the current table
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table attribute '$1' without a table", self)
            ->AsInteger();
    return new Integer(table->rows, self->Position());
}


Integer_p Widget::tableColumns(Tree_p self)
// ----------------------------------------------------------------------------
//   Return the number of columns in the current table
// ----------------------------------------------------------------------------
{
    if (!table)
        return Ooops("Table attribute '$1' without a table", self)
            ->AsInteger();
    return new Integer(table->columns, self->Position());
}



// ============================================================================
//
//   Frames and widgets
//
// ============================================================================

Tree_p Widget::status(Tree_p self, text caption)
// ----------------------------------------------------------------------------
//   Set the status line of the window
// ----------------------------------------------------------------------------
{
    Window *window = (Window *) parentWidget();
    window->statusBar()->showMessage(+caption);
    return XL::xl_true;
}


Tree_p Widget::framePaint(Tree_p self,
                          Real_p x, Real_p y, Real_p w, Real_p h,
                          Tree_p prog)
// ----------------------------------------------------------------------------
//   Draw a frame with the current text flow
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild());
    Tree_p result = frameTexture(self, w, h, prog);

    // Draw a rectangle with the resulting texture
    layout->Add(new Rectangle(Box(x-w/2, y-h/2, w, h)));
    if (currentShape)
        layout->Add(new FrameManipulator(currentShape, x, y, w, h));
    return result;
}


Tree_p Widget::frameTexture(Tree_p self, double w, double h, Tree_p prog)
// ----------------------------------------------------------------------------
//   Make a texture out of the current text layout
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    FrameInfo *frame = self->GetInfo<FrameInfo>();
    Tree_p result = XL::xl_false;
    if (!frame)
    {
        frame = new FrameInfo(w,h);
        self->SetInfo<FrameInfo> (frame);
    }

    do
    {
        GLAllStateKeeper saveGL;
        XL::LocalSave<Layout *> saveLayout(layout, layout->NewChild());

        // Clear the background and setup initial state
        frame->resize(w,h);
        setup(w, h);
        result = xl_evaluate(prog);

        // Draw the layout in the frame context
        frame->begin();
        layout->Draw(NULL);
        frame->end();

        // Delete the layout (it's not a child of the outer layout)
        delete layout;
        layout = NULL;
    } while (0); // State keeper and layout

    // Bind the resulting texture
    GLuint tex = frame->bind();
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return result;
}


Tree_p Widget::urlPaint(Tree_p self,
                        Real_p x, Real_p y, Real_p w, Real_p h,
                        Text_p url, Integer_p progress)
// ----------------------------------------------------------------------------
//   Draw a URL in the curent frame
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));
    urlTexture(self, w, h, url, progress);
    WebViewSurface *surface = self->GetInfo<WebViewSurface>();
    layout->Add(new ClickThroughRectangle(Box(x-w/2, y-h/2, w, h), surface));
    if (currentShape)
        layout->Add(new WidgetManipulator(currentShape, x, y, w, h, surface));
    return XL::xl_true;
}


Tree_p Widget::urlTexture(Tree_p self, double w, double h,
                          Text_p url, Integer_p progress)
// ----------------------------------------------------------------------------
//   Make a texture out of a given URL
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    WebViewSurface *surface = self->GetInfo<WebViewSurface>();
    if (!surface)
    {
        surface = new WebViewSurface(self, this);
        self->SetInfo<WebViewSurface> (surface);
    }

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind(url, progress);
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}


Tree_p Widget::lineEdit(Tree_p self,
                        Real_p x, Real_p y, Real_p w, Real_p h,
                        Text_p txt)
// ----------------------------------------------------------------------------
//   Draw a line editor in the curent frame
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));
    lineEditTexture(self, w, h, txt);
    LineEditSurface *surface = txt->GetInfo<LineEditSurface>();
    layout->Add(new ClickThroughRectangle(Box(x-w/2, y-h/2, w, h), surface));
    if (currentShape)
        layout->Add(new WidgetManipulator(currentShape, x, y, w, h, surface));
    return XL::xl_true;
}


Tree_p Widget::lineEditTexture(Tree_p self, double w, double h, Text_p txt)
// ----------------------------------------------------------------------------
//   Make a texture out of a given line editor
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    LineEditSurface *surface = txt->GetInfo<LineEditSurface>();
    if (!surface)
    {
        surface = new LineEditSurface(txt, this);
        txt->SetInfo<LineEditSurface> (surface);
    }

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind(txt);
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}

Tree_p Widget::radioButton(Tree_p self,
                           Real_p x,Real_p y, Real_p w,Real_p h,
                           Text_p name, text_p lbl, Text_p  sel, Tree_p act)
// ----------------------------------------------------------------------------
//   Draw a radio button in the curent frame
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));
    radioButtonTexture(self, w, h, name, lbl, sel, act);
    return abstractButton(self, name, x, y, w, h);
}


Tree_p Widget::radioButtonTexture(Tree_p self, double w, double h, Text_p name,
                                  Text_p lbl, Text_p  sel, Tree_p act)
// ----------------------------------------------------------------------------
//   Make a texture out of a given radio button
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    AbstractButtonSurface *surface = name->GetInfo<AbstractButtonSurface>();
    if (!surface)
    {
        surface = new RadioButtonSurface(name, this, +name->value);
        name->SetInfo<AbstractButtonSurface> (surface);
    }

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind(lbl, act, sel);
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}


Tree_p Widget::checkBoxButton(Tree_p self,
                              Real_p x,Real_p y, Real_p w, Real_p h,
                              Text_p name, text_p lbl, Text_p  sel, Tree_p act)
// ----------------------------------------------------------------------------
//   Draw a check button in the curent frame
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));
    checkBoxButtonTexture(self, w, h, name, lbl, sel, act);
    return abstractButton(self, name, x, y, w, h);
}


Tree_p Widget::checkBoxButtonTexture(Tree_p self,
                                     double w, double h, Text_p name,
                                     Text_p lbl, Text_p  sel, Tree_p act)
// ----------------------------------------------------------------------------
//   Make a texture out of a given checkbox button
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    AbstractButtonSurface *surface = name->GetInfo<AbstractButtonSurface>();
    if (!surface)
    {
        surface = new CheckBoxSurface(name, this, +name->value);
        name->SetInfo<AbstractButtonSurface> (surface);
    }

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind(lbl, act, sel);
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}


Tree_p Widget::pushButton(Tree_p self,
                          Real_p x, Real_p y, Real_p w, Real_p h,
                          Text_p name, Text_p lbl, Tree_p  act)
// ----------------------------------------------------------------------------
//   Draw a push button in the curent frame
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));
    pushButtonTexture(self, w, h, name, lbl, act);
    return abstractButton(self, name, x, y, w, h);
}


Tree_p Widget::pushButtonTexture(Tree_p self,
                                 double w, double h, Text_p name,
                                 Text_p lbl, Tree_p act)
// ----------------------------------------------------------------------------
//   Make a texture out of a given push button
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    AbstractButtonSurface *surface = name->GetInfo<AbstractButtonSurface>();
    if (!surface)
    {
        surface = new PushButtonSurface(name, this, +name->value);
        name->SetInfo<AbstractButtonSurface> (surface);
    }

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind(lbl, act, NULL);
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}


Tree_p Widget::abstractButton(Tree_p self, Text_p name,
                              Real_p x, Real_p y, Real_p w, Real_p h)
// ----------------------------------------------------------------------------
//   Draw any button in the curent frame
// ----------------------------------------------------------------------------
{
    AbstractButtonSurface *surface = name->GetInfo<AbstractButtonSurface>();

    if (currentGroup &&
        !currentGroup->buttons().contains((QAbstractButton*)surface->widget))
    {
        currentGroup->addButton((QAbstractButton*)surface->widget);
    }

    if (currentGridLayout)
    {
        currentGridLayout->addWidget(surface->widget, y, x);
        return XL::xl_true;
    }

    layout->Add(new ClickThroughRectangle(Box(x-w/2, y-h/2, w, h), surface));
    if (currentShape)
        layout->Add(new WidgetManipulator(currentShape, x, y, w, h, surface));

    return XL::xl_true;
}


QColorDialog *Widget::colorDialog = NULL;
Tree_p Widget::colorChooser(Tree_p self, text treeName, Tree_p action)
// ----------------------------------------------------------------------------
//   Draw a color chooser
// ----------------------------------------------------------------------------
{
    if (colorDialog)
    {
        delete colorDialog;
        colorDialog = NULL;
    }

    colorAction = action;
    colorName = treeName;

    // Setup the color dialog
    colorDialog = new QColorDialog(this);
    colorDialog->setOption(QColorDialog::ShowAlphaChannel, true);
    colorDialog->setOption(QColorDialog::DontUseNativeDialog, false);
    colorDialog->setModal(false);
    updateColorDialog();

    // Connect the dialog and sh'ow it
#ifdef Q_WS_MAC
    // To make the color dialog look Mac-like, we don't show OK and Cancel
    colorDialog->setOption(QColorDialog::NoButtons, true);
#else
    // On other platforms, it's expected fro OK and Cancel to show up
    connect(colorDialog, SIGNAL(colorSelected (const QColor&)),
            this, SLOT(colorChosen(const QColor &)));
    connect(colorDialog, SIGNAL(rejected()), this, SLOT(colorRejected()));
#endif
    connect(colorDialog, SIGNAL(currentColorChanged (const QColor&)),
            this, SLOT(colorChanged(const QColor &)));
    colorDialog->show();

    return XL::xl_true;
}


void Widget::colorRejected()
// ----------------------------------------------------------------------------
//   Slot called by the color widget's "cancel" button.
// ----------------------------------------------------------------------------
{
    colorChanged(originalColor);
}


void Widget::colorChosen(const QColor & col)
// ----------------------------------------------------------------------------
//   Slot called by the color widget a color is chosen and dialog is closed
// ----------------------------------------------------------------------------
{
    colorChanged(col);
}


void Widget::colorChanged(const QColor & col)
// ----------------------------------------------------------------------------
//   Slot called by the color widget when a color is selected
// ----------------------------------------------------------------------------
{
    if (!colorAction)
        return;

    assert(!current);
    TaoSave saveCurrent(current, this);
    IFTRACE (widgets)
    {
        std::cerr << "Color "<< col.name().toStdString()
                  << "was chosen for reference "<< colorAction << "\n";
    }

    // We override names 'red', 'green', 'blue' and 'alpha' in the input tree
    struct ColorTreeClone : XL::TreeClone
    {
        ColorTreeClone(const QColor &c): color(c){}
        XL::Tree *DoName(XL::Name *what)
        {
            if (what->value == "red")
                return new XL::Real(color.redF(), what->Position());
            if (what->value == "green")
                return new XL::Real(color.greenF(), what->Position());
            if (what->value == "blue")
                return new XL::Real(color.blueF(), what->Position());
            if (what->value == "alpha")
                return new XL::Real(color.alphaF(), what->Position());

            return new XL::Name(what->value, what->Position());
        }
        QColor color;
    } replacer(col);

    // The tree to be evaluated needs its own symbol table before evaluation
    XL::Tree *toBeEvaluated = colorAction;
    XL::Symbols *syms = toBeEvaluated->Symbols(); assert(syms);
    syms = new XL::Symbols(syms);
    toBeEvaluated = toBeEvaluated->Do(replacer);
    toBeEvaluated->SetSymbols(syms);

    // Evaluate the input tree
    xl_evaluate(toBeEvaluated);
}


void Widget::updateColorDialog()
// ----------------------------------------------------------------------------
//   Pick colors from the selection
// ----------------------------------------------------------------------------
{
    if (!colorDialog)
        return;

    TaoSave saveCurrent(current, this);

    // Make sure we don't update the trees, only get their colors
    XL::LocalSave<Tree_p > action(colorAction, NULL);
    Color c = selectionColor[colorName];
    originalColor.setRgbF(c.red, c.green, c.blue, c.alpha);

    // Get the default color from the first selected shape
    for (std::set<Tree_p >::iterator i = selectionTrees.begin();
         i != selectionTrees.end();
         i++)
    {
        attribute_args color;
        if (get(*i, colorName, color) && color.size() == 4)
        {
            originalColor.setRgbF(color[0], color[1], color[2], color[3]);
            break;
        }
    }
    colorDialog->setCurrentColor(originalColor);
}


QFontDialog *Widget::fontDialog = NULL;
Tree_p Widget::fontChooser(Tree_p self, Tree_p action)
// ----------------------------------------------------------------------------
//   Draw a font chooser
// ----------------------------------------------------------------------------
{
    if (fontDialog)
    {
        delete fontDialog;
        fontDialog = NULL;
    }

    fontDialog = new QFontDialog(this);
    connect(fontDialog, SIGNAL(fontSelected (const QFont&)),
            this, SLOT(fontChosen(const QFont &)));
    connect(fontDialog, SIGNAL(currentFontChanged (const QFont&)),
            this, SLOT(fontChanged(const QFont &)));

    fontDialog->setOption(QFontDialog::NoButtons, true);
    fontDialog->setOption(QFontDialog::DontUseNativeDialog, false);
    fontDialog->setModal(false);
    updateFontDialog();

    fontDialog->show();
    fontAction = action;
    if (!fontAction->Symbols())
        fontAction->SetSymbols(self->Symbols());

    return XL::xl_true;
}


void Widget::fontChosen(const QFont& ft)
// ----------------------------------------------------------------------------
//    A font was selected. Evaluate the action.
// ----------------------------------------------------------------------------
{
    fontChanged(ft);
}


void Widget::fontChanged(const QFont& ft)
// ----------------------------------------------------------------------------
//    A font was selected. Evaluate the action.
// ----------------------------------------------------------------------------
{
    if (!fontAction)
        return;

    IFTRACE2 (widgets,fonts)
    {
        std::cerr << "Font "<< ft.toString().toStdString()
                  << " was chosen for reference "<< fontAction << "\n";
    }

    struct FontTreeClone : XL::TreeClone
    {
        FontTreeClone(const QFont &f) : font(f){}
        XL::Tree *DoName(XL::Name *what)
        {
            if (what->value == "font_family")
                return new XL::Text(font.family().toStdString(),
                                    "\"" ,"\"",what->Position());
            if (what->value == "font_size")
                return new XL::Integer(font.pointSize(), what->Position());
            if (what->value == "font_weight")
                return new XL::Integer(font.weight(), what->Position());
            if (what->value == "font_slant")
                return new XL::Integer((int) font.style() * 100,
                                       what->Position());
            if (what->value == "font_stretch")
                return new XL::Integer(font.stretch(), what->Position());
            if (what->value == "font_is_italic")
                return font.italic() ? XL::xl_true : XL::xl_false;
            if (what->value == "font_is_bold")
                return font.bold() ? XL::xl_true : XL::xl_false;

            return new XL::Name(what->value, what->Position());
        }
        QFont font;
    } replacer(ft);

    // The tree to be evaluated needs its own symbol table before evaluation
    XL::Tree *toBeEvaluated = fontAction;
    XL::Symbols *syms = toBeEvaluated->Symbols(); assert(syms);
    syms = new XL::Symbols(syms);
    toBeEvaluated = toBeEvaluated->Do(replacer);
    toBeEvaluated->SetSymbols(syms);

    // Evaluate the input tree
    TaoSave saveCurrent(current, this);
    xl_evaluate(toBeEvaluated);
}


void Widget::updateFontDialog()
// ----------------------------------------------------------------------------
//   Pick font information from the selection
// ----------------------------------------------------------------------------
{
    if (!fontDialog)
        return;
    fontDialog->setCurrentFont(selectionFont);
}


Tree_p Widget::colorChooser(Tree_p self,
                            Real_p x, Real_p y, Real_p w, Real_p h,
                            Tree_p action)
// ----------------------------------------------------------------------------
//   Draw a color chooser
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));

    colorChooserTexture(self, w, h, action);

    ColorChooserSurface *surface = self->GetInfo<ColorChooserSurface>();
    layout->Add(new ClickThroughRectangle(Box(x-w/2, y-h/2, w, h), surface));
    if (currentShape)
        layout->Add(new WidgetManipulator(currentShape, x, y, w, h, surface));
    return XL::xl_true;
}


Tree_p Widget::colorChooserTexture(Tree_p self,
                                   double w, double h, Tree_p action)
// ----------------------------------------------------------------------------
//   Make a texture out of a given color chooser
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    ColorChooserSurface *surface = self->GetInfo<ColorChooserSurface>();
    if (!surface)
    {
        surface = new ColorChooserSurface(self, this, action);
        self->SetInfo<ColorChooserSurface> (surface);
    }

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind();
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}


Tree_p Widget::fontChooser(Tree_p self,
                           Real_p x, Real_p y, Real_p w, Real_p h,
                           Tree_p action)
// ----------------------------------------------------------------------------
//   Draw a color chooser
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));

    fontChooserTexture(self, w, h, action);

    FontChooserSurface *surface = self->GetInfo<FontChooserSurface>();
    layout->Add(new ClickThroughRectangle(Box(x-w/2, y-h/2, w, h), surface));
    if (currentShape)
        layout->Add(new WidgetManipulator(currentShape, x, y, w, h, surface));
    return XL::xl_true;
}


Tree_p Widget::fontChooserTexture(Tree_p self, double w, double h,
                                  Tree_p action)
// ----------------------------------------------------------------------------
//   Make a texture out of a given color chooser
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    FontChooserSurface *surface = self->GetInfo<FontChooserSurface>();
    if (!surface)
    {
        surface = new FontChooserSurface(self, this, action);
        self->SetInfo<FontChooserSurface> (surface);
    }

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind();
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}


QFileDialog *Widget::fileDialog = NULL;
Tree_p Widget::fileChooser(Tree_p self, Tree_p properties)
// ----------------------------------------------------------------------------
//   Draw a file chooser
// ----------------------------------------------------------------------------
{
    if (fileDialog)
    {
        delete fileDialog;
        fileDialog = NULL;
    }

    // Setup the color dialog
    fileDialog = new QFileDialog(this);
    currentFileDialog = fileDialog;
    fileDialog->setModal(false);

    updateFileDialog(properties, self);

    // Connect the dialog and show it
    connect(fileDialog, SIGNAL(fileSelected (const QString&)),
            this, SLOT(fileChosen(const QString &)));
    fileDialog->show();

    return XL::xl_true;
}


void Widget::updateFileDialog(Tree *properties, Tree *context)
// ----------------------------------------------------------------------------
//   Execute code for a file dialog
// ----------------------------------------------------------------------------
//   The action for a file dialog contains names that can be shortcuts
//   to the actual PREFIX name in graphics.tbl. For instance, action in
//   a file dialog is a shortcut for file_chooser_action.
//   This function performs the replacement
{
    NameToNameReplacement map;

    map["action"]    = "file_chooser_action";
    map["directory"] = "file_chooser_directory";
    map["label"]     = "file_chooser_label";
    map["filter"]    = "file_chooser_filter";

    if (!properties->Symbols())
        properties->SetSymbols(context->Symbols());
    XL::Tree *toBeEvaluated = map.Replace(properties);
    xl_evaluate(toBeEvaluated);

}


Tree_p Widget::setFileDialogAction(Tree_p self, Tree_p action)
// ----------------------------------------------------------------------------
//   Set the action that will be execute when OK is pressed.
// ----------------------------------------------------------------------------
{
    IFTRACE (widgets)
    {
        std::cerr << "setFileDialogAction "  << std::endl;
    }

    if (currentFileDialog)
    {
        XL::Tree_p root(action);
        currentFileDialog->setProperty("TAO_ACTION", QVariant::fromValue(root));
        return XL::xl_true;
    }
    return XL::xl_false;
}


Tree_p Widget::setFileDialogDirectory(Tree_p self, text dirname)
// ----------------------------------------------------------------------------
//   Set the directory to open first
// ----------------------------------------------------------------------------
{
    IFTRACE (widgets)
    {
        std::cerr << "setFileDialogDirectory " << dirname << std::endl;
    }

    if (currentFileDialog)
    {
        currentFileDialog->setDirectory(+dirname);
        return XL::xl_true;
    }
    return XL::xl_false;
}


Tree_p Widget::setFileDialogFilter(Tree_p self, text filters)
// ----------------------------------------------------------------------------
//   Set the file filters (file pattern, e.g. *.img)
// ----------------------------------------------------------------------------
{
    IFTRACE (widgets)
    {
        std::cerr << "setFileDialogFilter " << filters << std::endl;
    }

    if (currentFileDialog)
    {
        currentFileDialog->setNameFilter(+filters);
        return XL::xl_true;
    }
    return XL::xl_false;
}


Tree_p Widget::setFileDialogLabel(Tree_p self, text label, text value)
// ----------------------------------------------------------------------------
//   Set labels on a file dialog
// ----------------------------------------------------------------------------
// 5 labels may be set : LookIn, FileName, FileType, Accept, Reject
{
    IFTRACE (widgets)
    {
        std::cerr << "setFileDialogLabel " << label
                  << " to " << value << std::endl;
    }

    if (currentFileDialog)
    {
        currentFileDialog->setLabelText(toDialogLabel[label], +value);
        return XL::xl_true;
    }
    return XL::xl_false;
}


void Widget::fileChosen(const QString & filename)
// ----------------------------------------------------------------------------
//   Slot called by the filechooser widget when a file is selected
// ----------------------------------------------------------------------------
{
    if(!currentFileDialog)
        return;

    XL::Tree_p fileAction =
        currentFileDialog->property("TAO_ACTION").value<XL::Tree_p>();
    if (!fileAction)
        return;

    IFTRACE (widgets)
    {
        std::cerr << "File "<< filename.toStdString()
                  << "was chosen for reference "<< fileAction << "\n";
    }

    // We override names 'filename', 'filepath', 'filepathname', 'relfilepath'
    QFileInfo file(filename);
    QString relFilePath = QDir(((Window*)parent())->currentProjectFolderPath()).
                          relativeFilePath(file.canonicalFilePath());
    if (relFilePath.contains(".."))
    {
        QDir::home().
                relativeFilePath(file.canonicalFilePath());
        if (relFilePath.contains(".."))
        {
            relFilePath = file.canonicalFilePath();
        } else {
            relFilePath.prepend("~/");
        }
    }
    NameToTextReplacement map;

    map["file_name"] = +file.fileName();
    map["file_directory"] = +file.canonicalPath();
    map["file_path"] = +file.canonicalFilePath();
    map["rel_file_path"] = +relFilePath;

    XL::Tree *toBeEvaluated = map.Replace(fileAction);

    // Evaluate the input tree
    TaoSave saveCurrent(current, this);
    xl_evaluate(toBeEvaluated);
}


Tree_p Widget::fileChooser(Tree_p self, Real_p x, Real_p y, Real_p w, Real_p h,
                           Tree_p properties)
// ----------------------------------------------------------------------------
//   Draw a file chooser in the GL widget
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));

    fileChooserTexture(self, w, h, properties);

    FileChooserSurface *surface = self->GetInfo<FileChooserSurface>();
    layout->Add(new ClickThroughRectangle(Box(x-w/2, y-h/2, w, h), surface));
    if (currentShape)
        layout->Add(new WidgetManipulator(currentShape, x, y, w, h, surface));
    return XL::xl_true;
}


Tree_p Widget::fileChooserTexture(Tree_p self, double w, double h,
                                  Tree_p properties)
// ----------------------------------------------------------------------------
//   Make a texture out of a given file chooser
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    FileChooserSurface *surface = self->GetInfo<FileChooserSurface>();
    if (!surface)
    {
        surface = new FileChooserSurface(self, this);
        self->SetInfo<FileChooserSurface> (surface);
    }
    currentFileDialog = (QFileDialog *)surface->widget;

    updateFileDialog(properties, self);

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind();
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}


Tree_p Widget::buttonGroup(Tree_p self, bool exclusive, Tree_p buttons)
// ----------------------------------------------------------------------------
//   Create a button group for radio buttons
// ----------------------------------------------------------------------------
{
    GroupInfo *grpInfo = buttons->GetInfo<GroupInfo>();
    if (!grpInfo)
    {
        grpInfo = new GroupInfo(buttons, this);
        grpInfo->setExclusive(exclusive);
        buttons->SetInfo<GroupInfo>(grpInfo);
    }
    currentGroup = grpInfo;

    NameToNameReplacement map;
    map["action"] = "button_group_action";
    XL::Tree *toBeEvaluated = map.Replace(buttons);

    // Evaluate the input tree
    xl_evaluate(toBeEvaluated);
    currentGroup = NULL;

    return XL::xl_true;
}


Tree_p Widget::setButtonGroupAction(Tree_p self, Tree_p action)
// ----------------------------------------------------------------------------
//   Set the action to be executed by the current buttonGroup if any.
// ----------------------------------------------------------------------------
{
    if (currentGroup && currentGroup->action)
    {
        currentGroup->action = action;
    }

    return XL::xl_true;
}


Tree_p Widget::groupBox(Tree_p self,
                        Real_p x, Real_p y, Real_p w, Real_p h,
                        Text_p lbl, Tree_p buttons)
// ----------------------------------------------------------------------------
//   Draw a group box in the curent frame
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));

    groupBoxTexture(self, w, h, lbl);

    GroupBoxSurface *surface = self->GetInfo<GroupBoxSurface>();
    layout->Add(new ClickThroughRectangle(Box(x-w/2, y-h/2, w, h), surface));
    if (currentShape)
        layout->Add(new WidgetManipulator(currentShape, x, y, w, h, surface));

    xl_evaluate(buttons);

    surface->dirty = true;
    ((WidgetSurface*)surface)->bind();
    currentGridLayout = NULL;

    return XL::xl_true;
}


Tree_p Widget::groupBoxTexture(Tree_p self, double w, double h, Text_p lbl)
// ----------------------------------------------------------------------------
//   Make a texture out of a given push button
// ----------------------------------------------------------------------------
{
    if (w < 16) w = 16;
    if (h < 16) h = 16;


    // Get or build the current frame if we don't have one
    GroupBoxSurface *surface = self->GetInfo<GroupBoxSurface>();
    if (!surface)
    {
        surface = new GroupBoxSurface(self, this);
        self->SetInfo<GroupBoxSurface> (surface);
    }

    currentGridLayout = surface->grid();


    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind(lbl);
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}


Tree_p Widget::videoPlayer(Tree_p self,
                           Real_p x, Real_p y, Real_p w, Real_p h, Text_p url)
// ----------------------------------------------------------------------------
//   Make a video player
// ----------------------------------------------------------------------------
{
    XL::LocalSave<Layout *> saveLayout(layout, layout->AddChild(layout->id));
    videoPlayerTexture(self, w, h, url);
    VideoPlayerSurface *surface = self->GetInfo<VideoPlayerSurface>();
    layout->Add(new ClickThroughRectangle(Box(x-w/2, y-h/2, w, h), surface));
    if (currentShape)
        layout->Add(new WidgetManipulator(currentShape, x, y, w, h, surface));

    return XL::xl_true;

}


Tree_p Widget::videoPlayerTexture(Tree_p self, Real_p wt, Real_p ht, Text_p url)
// ----------------------------------------------------------------------------
//   Make a video player texture
// ----------------------------------------------------------------------------
{
    double w = wt, h = ht;
    if (w < 16) w = 16;
    if (h < 16) h = 16;

    // Get or build the current frame if we don't have one
    VideoPlayerSurface *surface = self->GetInfo<VideoPlayerSurface>();
    if (!surface)
    {
        surface = new VideoPlayerSurface(self, this);
        self->SetInfo<VideoPlayerSurface> (surface);
    }

    // Resize to requested size, and bind texture
    surface->resize(w,h);
    GLuint tex = surface->bind(url);
    layout->Add(new FillTexture(tex));
    layout->hasAttributes = true;

    return XL::xl_true;
}



// ============================================================================
//
//    Error management
//
// ============================================================================

Tree_p Widget::runtimeError(Tree_p self, text msg, Tree_p arg)
// ----------------------------------------------------------------------------
//   Display an error message from the input
// ----------------------------------------------------------------------------
{
    if (current)
        current->inError = true;             // Stop refreshing
    return formulaRuntimeError(self, msg, arg);
}


Tree_p Widget::formulaRuntimeError(Tree_p self, text msg, Tree_p arg)
// ----------------------------------------------------------------------------
//   Display a runtime error while executing a formula
// ----------------------------------------------------------------------------
{
    XL::Error err(msg, arg, NULL, NULL);

    if (current)
    {
        Window *window = (Window *) current->parentWidget();
        window->addError(+err.Message());
    }
    else
    {
        err.Display();
    }

    Tree_p result = new XL::Name("#ERROR");
    result->code = XL::xl_identity;
    return result;
}



// ============================================================================
//
//   Menu management
//
// ============================================================================
// * Menu name philosophy :
// * The full name is used to register menus and menu items against
//   the menubar.  Those names are not displayed and must be unique.
// * Menu created by the XL programmer must be differentiated from the
//   originals ones because they have to be recreated or modified at
//   each loop of XL.  When top menus are deleted they recursively
//   delete their children (sub menus and menu items), so we have to
//   take care of sub menu at deletion time.
//
//
// * Menu and menu items lifecycle : Menus are created when the xl
//   program is executed the first time.  Menus display text can be
//   modified at each execution. At each loop, for each element (menu,
//   menu_item, toolbar,...) there name is looked for as a main window children,
//   if found, the order is checked against the registered value in
//   orderedMenuElements. If the order is OK, the label, etc are updated; if not
//   or not found at all a new element is created and registered.
// ============================================================================

Tree_p Widget::menuItem(Tree_p self, text name, text lbl, text iconFileName,
                        bool isCheckable, Text_p isChecked, Tree_p t)
// ----------------------------------------------------------------------------
//   Create a menu item
// ----------------------------------------------------------------------------
{
    if (!currentMenu && !currentToolBar)
        return XL::xl_false;

    QString fullName = +name;

    if (QAction* act = parent()->findChild<QAction*>(fullName))
    {
        // MenuItem found, update label, icon, checkmark if the order is OK.
        if (order < orderedMenuElements.size() &&
            orderedMenuElements[order] != NULL &&
            orderedMenuElements[order]->fullname == fullName)
        {
            act->setText(+lbl);
            if (iconFileName != "")
                act->setIcon(QIcon(+iconFileName));
            else
                act->setIcon(QIcon());
            act->setChecked(strcasecmp(isChecked->value.c_str(), "true") == 0);

            order++;
            return XL::xl_true;
        }

        // The name exist but it is not in the good order so clean it.
        delete act;
    }

    // Store the tree in the QAction.
    QVariant var = QVariant::fromValue(XL::Tree_p(t));

    IFTRACE(menus)
    {
        std::cerr << "menuItem CREATION with name "
                  << fullName.toStdString() << " and order " << order << "\n";
        std::cerr.flush();
    }

    QAction * p_action;
    QWidget * par;
    if (currentMenu)
        par =  currentMenu;
    else
        par = currentToolBar;

    p_action = new QAction(+lbl, par);
    p_action->setData(var);

    // Set the item sensible to the selection
    if (fullName.startsWith("menu:select:"))
    {
        p_action->setEnabled(hasSelection());
        connect(this, SIGNAL(copyAvailable(bool)),
                p_action, SLOT(setEnabled(bool)));
    }

    if (iconFileName != "")
        p_action->setIcon(QIcon(+iconFileName));
    else
        p_action->setIcon(QIcon());

    p_action->setCheckable(isCheckable);
    p_action->setChecked(strcasecmp(isChecked->value.c_str(), "true") == 0);
    p_action->setObjectName(fullName);

    if (order >= orderedMenuElements.size())
        orderedMenuElements.resize(order+10);

    if (orderedMenuElements[order])
    {
        QAction*before = orderedMenuElements[order]->p_action;
        if (currentMenu)
            currentMenu->insertAction(before, p_action);
        else
            currentToolBar->insertAction(before, p_action);

        delete orderedMenuElements[order];
    }
    else
    {
        if (currentMenu)
            currentMenu->addAction(p_action);
        else
            currentToolBar->addAction(p_action);
    }

    orderedMenuElements[order] = new MenuInfo(fullName,
                                              p_action);
    order++;

    return XL::xl_true;
}


Tree_p Widget::menu(Tree_p self, text name, text lbl,
                    text iconFileName, bool isSubMenu)
// ----------------------------------------------------------------------------
// Add the menu to the current menu bar or create the contextual menu
// ----------------------------------------------------------------------------
{
    bool isContextMenu = false;

    // Build the full name of the menu
    // Uses the current menu name, the given string and the isSubmenu.
    QString fullname = +name;
    if (fullname.startsWith(CONTEXT_MENU))
    {
        isContextMenu = true;
    }

    // If the menu is registered, no need to recreate it if the order is exact.
    // This is used at reload time.
    if (QMenu *tmp = parent()->findChild<QMenu*>(fullname))
    {
        if (lbl == "" && iconFileName == "")
        {
            // Just set the current menu to the requested one
            currentMenu = tmp;
            return XL::xl_true;
        }

        if (order < orderedMenuElements.size())
        {
            if (MenuInfo *menuInfo = orderedMenuElements[order])
            {
                if (menuInfo->fullname == fullname)
                {
                    // Set the currentMenu and update the label and icon.
                    currentMenu = tmp;
                    if (lbl != menuInfo->title)
                    {
                        currentMenu->setTitle(+lbl);
                        menuInfo->title = lbl;
                    }
                    if (iconFileName != menuInfo->icon)
                    {
                        if (iconFileName != "")
                            currentMenu->setIcon(QIcon(+iconFileName));
                        else
                            currentMenu->setIcon(QIcon());
                        menuInfo->icon = iconFileName;
                    }
                    order++;
                    return XL::xl_true;
                }
            }
        }
        // The name exist but it is not in the good order so clean it
        delete tmp;
    }

    QWidget *par = NULL;
    // The menu is not yet registered. Create it and set the currentMenu.
    if (isContextMenu)
    {
        currentMenu = new QMenu((Window*)parent());
        connect(currentMenu, SIGNAL(triggered(QAction*)),
                this,        SLOT(userMenu(QAction*)));
    }
    else
    {
        if (isSubMenu && currentMenu)
            par = currentMenu;
        else if (currentMenuBar)
            par = currentMenuBar;
        else if (currentToolBar)
            par = currentToolBar;

        currentMenu = new QMenu(+lbl, par);
    }

    currentMenu->setObjectName(fullname);

    if (iconFileName != "")
        currentMenu->setIcon(QIcon(+iconFileName));

    if (order >= orderedMenuElements.size())
        orderedMenuElements.resize(order+10);

    if (par)
    {
        QAction *before = NULL;
        if (orderedMenuElements[order])
        {
            before = orderedMenuElements[order]->p_action;
        }
        else
        {
            if (par == currentMenuBar)
                before = ((Window*)parent())->shareMenu->menuAction();

//            par->addAction(currentMenu->menuAction());
        }
        par->insertAction(before, currentMenu->menuAction());

        QToolButton* button = NULL;
        if (par == currentToolBar &&
            (button = dynamic_cast<QToolButton*>
             (currentToolBar-> widgetForAction(currentMenu->menuAction()))))
            button->setPopupMode(QToolButton::InstantPopup);
    }

    if (orderedMenuElements[order])
        delete orderedMenuElements[order];

    orderedMenuElements[order] = new MenuInfo(fullname,
                                              currentMenu->menuAction());
    orderedMenuElements[order]->title = lbl;
    orderedMenuElements[order]->icon = iconFileName;

    IFTRACE(menus)
    {
        std::cerr << "menu CREATION with name "
                  << fullname.toStdString() << " and order " << order << "\n";
        std::cerr.flush();
    }

    order++;

    return XL::xl_true;
}


Tree_p  Widget::menuBar(Tree_p self)
// ----------------------------------------------------------------------------
// Set the currentManueBar to the default menuBar.
// ----------------------------------------------------------------------------
{
    currentMenuBar = ((Window *)parent())->menuBar();
    currentToolBar = NULL;
    currentMenu = NULL;
    return XL::xl_true;
}


Tree_p  Widget::toolBar(Tree_p self, text name, text title, bool isFloatable,
                        text location)
// ----------------------------------------------------------------------------
// Add the toolBar to the current widget
// ----------------------------------------------------------------------------
// The location is the prefered location for the toolbar.
// The supported values are [n|N]*, [e|E]*, [s|S]*, West or N, E, S, W, O
{
    QString fullname = +name;
    Window *win = (Window *)parent();
    if (QToolBar *tmp = win->findChild<QToolBar*>(fullname))
    {
        if (order < orderedMenuElements.size() &&
            orderedMenuElements[order] != NULL &&
            orderedMenuElements[order]->fullname == fullname)
        {
            // Set the currentMenu and update the label and icon.
            currentToolBar = tmp;
            order++;
            currentMenuBar = NULL;
            currentMenu = NULL;
            return XL::xl_true;
        }

        // The name exist but it is not in the good order so remove it.
        delete tmp;
    }

    currentToolBar = win->addToolBar(+title);
    currentToolBar->setObjectName(fullname);
    currentToolBar->setFloatable(isFloatable);

    switch (location[0]) {
    case 'n':
    case 'N':
        win->addToolBarBreak(Qt::TopToolBarArea);
        win->addToolBar(Qt::TopToolBarArea, currentToolBar);
        break;
    case 'e':
    case 'E':
        win->addToolBarBreak(Qt::RightToolBarArea);
        win->addToolBar(Qt::RightToolBarArea, currentToolBar);
        break;
    case 's':
    case 'S':
        win->addToolBarBreak(Qt::BottomToolBarArea);
        win->addToolBar(Qt::BottomToolBarArea, currentToolBar);
        break;
    case 'w':
    case 'W':
    case 'o':
    case 'O':
        win->addToolBarBreak(Qt::LeftToolBarArea);
        win->addToolBar(Qt::LeftToolBarArea, currentToolBar);
        break;
    }

    if (QMenu* view = win->findChild<QMenu*>(VIEW_MENU_NAME))
        view->addAction(currentToolBar->toggleViewAction());

    connect(currentToolBar, SIGNAL(actionTriggered(QAction*)),
            this, SLOT(userMenu(QAction*)));

    IFTRACE(menus)
    {
        std::cerr << "toolbar CREATION with name "
                  << fullname.toStdString() << " and order " << order << "\n";
        std::cerr.flush();
    }

    if (order >= orderedMenuElements.size())
        orderedMenuElements.resize(order+10);

    if (orderedMenuElements[order])
        delete orderedMenuElements[order];

    orderedMenuElements[order] = new MenuInfo(fullname, currentToolBar);

    order++;
    currentMenuBar = NULL;
    currentMenu = NULL;

    return XL::xl_true;
}


Tree_p  Widget::separator(Tree_p self)
// ----------------------------------------------------------------------------
//   Add the separator to the current widget
// ----------------------------------------------------------------------------
{

    QString fullname = QString("SEPARATOR_%1").arg(order);

    if (QAction *tmp = parent()->findChild<QAction*>(fullname))
    {
        if (order < orderedMenuElements.size() &&
            orderedMenuElements[order] != NULL &&
            orderedMenuElements[order]->fullname == fullname)
        {
//            IFTRACE(menus)
//            {
//                std::cerr << "separator found with name "
//                          << fullname.toStdString() << " and order "
//                          << order << "\n";
//                std::cerr.flush();
//            }
            order++;
            return XL::xl_true;
        }

        delete tmp;
    }

    QWidget *par = NULL;
    if (currentMenu)
        par = currentMenu;
    else if (currentMenuBar)
        par = currentMenuBar;
    else if (currentToolBar)
        par = currentToolBar;

    QAction *act = new QAction(par);
    act->setSeparator(true);
    act->setObjectName(fullname);

    IFTRACE(menus)
    {
        std::cerr << "separator CREATION with name "
                  << fullname.toStdString() << " and order " << order << "\n";
        std::cerr.flush();
    }
    if (order >= orderedMenuElements.size())
        orderedMenuElements.resize(order+10);

    if (orderedMenuElements[order])
    {
        if (par)
        {
            QAction *before = orderedMenuElements[order]->p_action;
            par->insertAction(before, act);
        }
        delete orderedMenuElements[order];
    }
    else
    {
        if (par)
            par->addAction(act);
    }
    orderedMenuElements[order] = new MenuInfo(fullname, act);
    order++;
    return XL::xl_true;
}



// ============================================================================
//
//    Tree selection management
//
// ============================================================================

XL::Name_p Widget::insert(Tree_p self, Tree_p toInsert, text msg)
// ----------------------------------------------------------------------------
//    Insert at the end of page or program
// ----------------------------------------------------------------------------
{
    // For 'insert { statement; }', we don't want the { } block
    if (XL::Block *block = toInsert->AsBlock())
        toInsert = block->child;

    // Make sure the new objects appear selected next time they're drawn
    selectStatements(toInsert);

    // Start at the top of the program to find where we will insert
    Tree_p *top = &xlProgram->tree;
    Infix *parent  = NULL;

    // If we have a current page, insert only in that context
    if (Tree *page = pageTree)
    {
        // Restrict insertion to that page
        top = &pageTree;

        // The page instructions often runs a 'do' block
        if (Prefix *prefix = page->AsPrefix())
            if (Name *left = prefix->left->AsName())
                if (left->value == "do")
                    top = &prefix->right;

        // If the page code is a block, look inside
        if (XL::Block *block = (*top)->AsBlock())
            top = &block->child;
    }

    // Descend on the right of the statements
    Tree *program = *top;
    if (!program)
    {
        *top = toInsert;
        toInsert->SetSymbols(xlProgram->symbols);
    }
    else
    {
        if (Infix *statements = program->AsInfix())
        {
            statements = statements->LastStatement();
            parent = statements;
            program = statements->right;
        }

        // Append at end of the statements
        Tree_p *what = parent ? &parent->right : top;
        Symbols *symbols = (*what)->Symbols();
        *what = new XL::Infix("\n", *what, toInsert);
        (*what)->SetSymbols(symbols);
    }

    // Reload the program and mark the changes
    reloadProgram();
    markChanged(msg);

    return XL::xl_true;
}


XL::Tree_p Widget::copySelection()
// ----------------------------------------------------------------------------
//    Copy the selection from the tree
// ----------------------------------------------------------------------------
{
    if (!hasSelection() || !xlProgram || !xlProgram->tree)
        return NULL;

    CopySelection copy(this);

    return xlProgram->tree->Do(copy);
}

XL::Tree_p Widget::removeSelection()
// ----------------------------------------------------------------------------
//    Remove the selection from the tree and return a copy of it
// ----------------------------------------------------------------------------
{
    XL::Tree *tree = copySelection();
    if ( !tree)
        return NULL;
    deleteSelection();
    return tree;
}

XL::Name_p Widget::deleteSelection(Tree_p self, text key)
// ----------------------------------------------------------------------------
//    Delete the selection (with text support)
// ----------------------------------------------------------------------------
{
    if (textSelection())
        return textEditKey(self, key);

    deleteSelection();

    return XL::xl_true;
}


void Widget::deleteSelection()
// ----------------------------------------------------------------------------
//    Delete the selection (when selection is not text)
// ----------------------------------------------------------------------------
{
    XL::Tree *what = xlProgram->tree;
    if (what)
    {
        DeleteSelectionAction del(this);
        what = what->Do(del);

        if (!what)
            xlProgram->tree = what;
        reloadProgram(what);
        markChanged("Deleted selection");
    }
    selection.clear();
    selectionTrees.clear();
}


XL::Name_p Widget::setAttribute(Tree_p self,
                                text name, Tree_p attribute,
                                text shape)
// ----------------------------------------------------------------------------
//    Insert the tree in all shapes in the selection
// ----------------------------------------------------------------------------
{
    if (Tree *program = xlProgram->tree)
    {
        if (XL::Block_p block = attribute->AsBlock())
            attribute = block->child;

        SetAttributeAction setAttrib(name, attribute, this, shape);
        program->Do(setAttrib);

        // We don't need to reloadProgram() because Widget::set does it
        markChanged("Updated " + name + " attribute");

        return XL::xl_true;
    }
    return XL::xl_false;
}



// ============================================================================
//
//   Group management
//
// ============================================================================

Tree_p Widget::group(Tree_p self, Tree_p shapes)
// ----------------------------------------------------------------------------
//   Group objects together, make them selectable as a whole
// ----------------------------------------------------------------------------
{
    GroupLayout *group = new GroupLayout(this, self);
    group->id = newId();
    layout->Add(group);
    XL::LocalSave<Layout *> saveLayout(layout, group);
    XL::LocalSave<Tree_p>   saveShape (currentShape, self);
    if (selectNextTime.count(self))
    {
        selection[id]++;
        selectNextTime.erase(self);
    }

    Tree_p result = xl_evaluate(shapes);
    return result;
}


Tree_p Widget::updateParentWithGroupInPlaceOfChild(Tree *parent, Tree *child)
// ----------------------------------------------------------------------------
//   Replace 'child' with a group created from the selection
// ----------------------------------------------------------------------------
{
    Name * groupName = new Name("group");
    Tree * group = new Prefix(groupName, new Block(copySelection(), "I+", "I-"));

    Infix * inf = parent->AsInfix();
    if ( inf )
    {
        if (inf->left == child)
            inf->left = group;
        else
            inf->right = group;

        return group;
    }

    Prefix * pref = parent->AsPrefix();
    if ( pref )
    {
        if (pref->left == child)
            pref->left = group;
        else
            pref->right = group;

        return group;
    }

    Postfix * pos = parent->AsPostfix();
    if ( pos )
    {
        if (pos->left == child)
            pos->left = group;
        else
            pos->right = group;

        return group;
    }

    Block * block = parent->AsBlock();
    if (block)
    {
        block->child = group;
        return group;
    }

    return NULL;

}


Name_p Widget::groupSelection(Tree_p /*self*/)
// ----------------------------------------------------------------------------
//    Create the group from the selected objects
// ----------------------------------------------------------------------------
{
    if (!hasSelection())
        return XL::xl_false;

    // Find the first non-selected ancestor of the first element
    //      in the selection set.
    std::set<Tree_p >::iterator sel = selectionTrees.begin();
    Tree * child = *sel;
    Tree * parent = NULL;
    do {
        XL::FindParentAction getParent(child);
        parent = xlProgram->tree->Do(getParent);
    } while (parent && selectionTrees.count(parent) && (child = parent));

    // Check if we are not the only one
    if (!parent)
        return XL::xl_false;

    // Do the work
    Tree * theGroup = updateParentWithGroupInPlaceOfChild(parent, child);
    if (! theGroup )
        return XL::xl_false;

    deleteSelection();

    selectStatements(theGroup);
    // Reload the program and mark the changes
    reloadProgram();
    markChanged("Selection grouped");

    return XL::xl_true;
}


bool Widget::updateParentWithChildrenInPlaceOfGroup(Tree *parent, Prefix *group)
// ----------------------------------------------------------------------------
//    Helper function: Plug the group's chlid tree under the parent.
// ----------------------------------------------------------------------------
{
    Infix * inf = parent->AsInfix();
    Block * block = group->right->AsBlock();
    if ( !block)
        return false;

    if ( inf )
    {
        if (inf->left == group)
        {
            if (Infix * inf_child = block->child->AsInfix())
            {

                Tree *p_right = inf->right;
                Infix *last = inf_child->LastStatement();
                last->right = new Infix("\n",last->right, p_right);
                inf->left = inf_child->left;
                inf->right = inf_child->right;
            }
            else
                inf->left = block->child;

        }
        else
            inf->right = block->child;

        return true;
    }

    Prefix * pref = parent->AsPrefix();
    if ( pref )
    {
        if (pref->left == group)
            pref->left = block->child;
        else
            pref->right = block->child;

        return true;
    }

    Postfix * pos = parent->AsPostfix();
    if ( pos )
    {
        if (pos->left == group)
            pos->left = block->child;
        else
            pos->right = block->child;

        return true;
    }

    Block * blockPar = parent->AsBlock();
    if (blockPar)
    {
        blockPar->child = block->child;
        return true;
    }

    return false;

}

Name_p Widget::ungroupSelection(Tree_p /*self*/)
// ----------------------------------------------------------------------------
//    Remove the group instruction from the source code
// ----------------------------------------------------------------------------
{
    if (!hasSelection())
        return XL::xl_false;

    std::set<Tree_p >::iterator sel = selectionTrees.begin();

    Prefix * groupTree = (*sel)->AsPrefix();
    if (!groupTree)
        return XL::xl_false;

    Name * name = groupTree->left->AsName();
    if (! name || name->value != "group")
        return XL::xl_false;

    XL::FindParentAction getParent(*sel);
    Tree * parent = xlProgram->tree->Do(getParent);
    // Check if we are not the only one
    if (!parent)
        return XL::xl_false;

    bool res = updateParentWithChildrenInPlaceOfGroup(parent, groupTree);
    if (! res )
        return XL::xl_false;

    selectStatements(groupTree->right);
    // Reload the program and mark the changes
    reloadProgram();
    markChanged("Selection ungrouped");

    return XL::xl_true;

}

// ============================================================================
//
//   Unit conversions
//
// ============================================================================

XL::Real_p Widget::fromCm(Tree_p self, double cm)
// ----------------------------------------------------------------------------
//   Convert from cm to pixels
// ----------------------------------------------------------------------------
{
    XL_RREAL(cm * logicalDpiX() * (1.0 / 2.54));
}


XL::Real_p Widget::fromMm(Tree_p self, double mm)
// ----------------------------------------------------------------------------
//   Convert from mm to pixels
// ----------------------------------------------------------------------------
{
    XL_RREAL(mm * logicalDpiX() * (0.1 / 2.54));
}


XL::Real_p Widget::fromIn(Tree_p self, double in)
// ----------------------------------------------------------------------------
//   Convert from inch to pixels
// ----------------------------------------------------------------------------
{
    XL_RREAL(in * logicalDpiX());
}


XL::Real_p Widget::fromPt(Tree_p self, double pt)
// ----------------------------------------------------------------------------
//   Convert from pt to pixels
// ----------------------------------------------------------------------------
{
    XL_RREAL(pt * logicalDpiX() * (1.0 / 72.0));
}


XL::Real_p Widget::fromPx(Tree_p self, double px)
// ----------------------------------------------------------------------------
//   Convert from pixel (currently 1-1 mapping, could be based on scaling?)
// ----------------------------------------------------------------------------
{
    XL_RREAL(px);
}



// ============================================================================
//
//    Misc...
//
// ============================================================================

Tree_p Widget::constant(Tree_p self, Tree_p tree)
// ----------------------------------------------------------------------------
//   Return a clone of the tree to make sure it is not modified
// ----------------------------------------------------------------------------
{
    MarkAsConstant(tree);
    return tree;
}



// ============================================================================
//
//   Tree substitution / replacement helpers
//
// ============================================================================

XL::Tree *NameToNameReplacement::DoName(XL::Name *what)
// ----------------------------------------------------------------------------
//   Replace a name with another name
// ----------------------------------------------------------------------------
{
    std::map<text, text>::iterator found = map.find(what->value);
    if (found != map.end())
    {
        replaced = true;
        return new XL::Name((*found).second, what->Position());
    }
    return new XL::Name(what->value, what->Position());
}


XL::Tree *  NameToNameReplacement::Replace(XL::Tree *original)
// ----------------------------------------------------------------------------
//   Perform name replacement and give the result its own symbol table
// ----------------------------------------------------------------------------
{
    XL::Tree *copy = original;
    XL::Symbols *syms = original->Symbols(); assert(syms);
    syms = new XL::Symbols(syms);
    copy = original->Do(*this);
    copy->SetSymbols(syms);
    return copy;
}


XL::Tree *NameToTextReplacement::DoName(XL::Name *what)
// ----------------------------------------------------------------------------
//   Replace a name with a text
// ----------------------------------------------------------------------------
{
    std::map<text, text>::iterator found = map.find(what->value);
    if (found != map.end())
    {
        replaced = true;
        return new XL::Text((*found).second, "\"", "\"", what->Position());
    }
    return new XL::Name(what->value, what->Position());
}

TAO_END


// ============================================================================
//
//   Helper functions
//
// ============================================================================

namespace XL
{
void tao_widget_refresh(double delay)
// ----------------------------------------------------------------------------
//    Refresh the current widget
// ----------------------------------------------------------------------------
{
    TAO(refresh(delay));
}
}
