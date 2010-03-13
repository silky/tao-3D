// ****************************************************************************
//  widget-surface.cpp                                              Tao project
// ****************************************************************************
//
//   File Description:
//
//     Render a web page into a texture
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

#include "widget-surface.h"
#include "gl_keepers.h"
#include <QtWebKit>


TAO_BEGIN


// ============================================================================
//
//    WidgetSurface - Surface for any kind of QWidget
//
// ============================================================================

WidgetSurface::WidgetSurface(Widget *parent, QWidget *widget,
                             uint width, uint height)
// ----------------------------------------------------------------------------
//   Create a renderer with the right size
// ----------------------------------------------------------------------------
    : widget(widget), textureId(0), dirty(true)
{
    widget->show();
    glGenTextures(1, &textureId);
}


WidgetSurface::~WidgetSurface()
// ----------------------------------------------------------------------------
//   When deleting the info, delete all renderers we have
// ----------------------------------------------------------------------------
{
    delete widget;
    glDeleteTextures(1, &textureId);
}


void WidgetSurface::resize(uint w, uint h)
// ----------------------------------------------------------------------------
//   Resize the FBO and if necessary request a repaint
// ----------------------------------------------------------------------------
{
    // Resizing the widget should trigger a repaint
    if (w != widget->width() || h != widget->height())
    {
        widget->resize(w, h);
        dirty = true;
    }
}


void WidgetSurface::bind ()
// ----------------------------------------------------------------------------
//    Activate a given widget
// ----------------------------------------------------------------------------
{
    if (dirty)
    {
        QImage image(widget->width(), widget->height(),
                     QImage::Format_ARGB32);
        widget->render(&image);

        // Generate the GL texture
        image = QGLWidget::convertToGLFormat(image);
        glBindTexture(GL_TEXTURE_2D, textureId);
        glTexImage2D(GL_TEXTURE_2D, 0, 3,
                     image.width(), image.height(), 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, image.bits());

        dirty = false;
    }

    // Bind the texture that we got
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glEnable(GL_TEXTURE_2D);
#ifdef GL_MULTISAMPLE   // Not supported on Windows
    glEnable(GL_MULTISAMPLE);
#endif
}


void WidgetSurface::repaint()
// ----------------------------------------------------------------------------
//   The page requests a repaint, repaint in the frame buffer object
// ----------------------------------------------------------------------------
{
    dirty = true;
}



// ============================================================================
//
//   WebViewSurface - Specialization for QWebView
//
// ============================================================================

WebViewSurface::WebViewSurface(Widget *parent, uint width, uint height)
// ----------------------------------------------------------------------------
//    Build the QWebView
// ----------------------------------------------------------------------------
    : WidgetSurface(parent, new QWebView(parent), width, height),
      urlTree(NULL), url(""), progress(0)
{
    QWebView *webView = (QWebView *) widget;
    connect(webView->page(),    SIGNAL(repaintRequested(const QRect &)),
            this,               SLOT(repaint()));
    connect(webView,            SIGNAL(loadFinished(bool)),
            this,               SLOT(finishedLoading(bool)));
    connect(webView,            SIGNAL(loadProgress(int)),
            this,               SLOT(loadProgress(int)));
}


void WebViewSurface::bind(XL::Text *urlTree, XL::Integer *progressTree)
// ----------------------------------------------------------------------------
//    Update depending on URL changes, then bind texture
// ----------------------------------------------------------------------------
{
    if (progress != progressTree)
        progress = progressTree;

    if (urlTree != this->urlTree || urlTree->value != url)
    {
        QWebView *webView = (QWebView *) widget;
        url = urlTree->value;
        this->urlTree = urlTree;
        webView->load(QUrl(QString::fromStdString(url)));
        if (progress)
            progress->value = 0;
    }

    WidgetSurface::bind();
}


void WebViewSurface::finishedLoading (bool loadedOK)
// ----------------------------------------------------------------------------
//   When we loaded the surface, update the URL
// ----------------------------------------------------------------------------
{
    if (urlTree)
    {
        QWebView *webView = (QWebView *) widget;
        urlTree->value = webView->url().toString().toStdString();
    }
    if (progress)
        progress->value = 100;
}


void WebViewSurface::loadProgress(int progressPercent)
// ----------------------------------------------------------------------------
//   If we have a progress status, update it
// ----------------------------------------------------------------------------
{
    if (progress)
        progress->value = progressPercent;
}



// ============================================================================
//
//   LineEditSurface - Specialization for QLineView
//
// ============================================================================

LineEditSurface::LineEditSurface(Widget *parent,
                                 uint width, uint height,
                                 bool immed)
// ----------------------------------------------------------------------------
//    Build the QLineEdit
// ----------------------------------------------------------------------------
    : WidgetSurface(parent, new QLineEdit(parent), width, height),
      contents(NULL), immediate(immed)
{
    QLineEdit *lineEdit = (QLineEdit *) widget;
    lineEdit->setFrame(true);
    lineEdit->setReadOnly(false);
    lineEdit->selectAll();
    connect(lineEdit, SIGNAL(textChanged(const QString &)),
            this,     SLOT(textChanged(const QString &)));
    connect(lineEdit, SIGNAL(editingFinished()),
            this,     SLOT(repaint()));
    connect(lineEdit, SIGNAL(selectionChanged()),
            this,     SLOT(repaint()));
    connect(lineEdit, SIGNAL(cursorPositionChanged(int, int)),
            this,     SLOT(repaint()));
    connect(lineEdit, SIGNAL(returnPressed()),
            this,     SLOT(inputValidated()));
}


void LineEditSurface::bind(XL::Text *contents)
// ----------------------------------------------------------------------------
//    Update text based on text changes
// ----------------------------------------------------------------------------
{
    if (contents != this->contents)
    {
        QLineEdit *lineEdit = (QLineEdit *) widget;
        this->contents = NULL;
        lineEdit->setText(QString::fromStdString(contents->value));
        this->contents = contents;
    }

    WidgetSurface::bind();
}


void LineEditSurface::textChanged(const QString &text)
// ----------------------------------------------------------------------------
//    If the text changed, update the associated XL::Text
// ----------------------------------------------------------------------------
{
    if (contents && immediate)
        contents->value = text.toStdString();
    repaint();
}


void LineEditSurface::inputValidated()
// ----------------------------------------------------------------------------
//    If the text changed, update the associated XL::Text
// ----------------------------------------------------------------------------
{
    if (contents)
    {
        QLineEdit *lineEdit = (QLineEdit *) widget;
        contents->value = lineEdit->text().toStdString();
    }
    repaint();
}

TAO_END
