/**
 * @mainpage Tao Presentations Documentation
 *
 * Welcome to the online documentation for Tao Presentations.
 *
 * @section secWhatYouWillFind What is in this manual
 *
 * In this manual, you will find reference information on:
 *   - the format and structure of Tao documents,
 *   - the constructs (@a builtins) that you can use to write documents,
 *   - the XLR language, which is the basis of Tao documents.
 *
 * The reference pages also contain some examples.
 *
 * @section secMoreInfo More information
 *
 * You may find more information and the latest news about Tao Presentations on
 * our web site: http://www.taodyne.com.
 *
 * @section secGettingStarted Getting started
 *
 * The easiest way to learn Tao Presentations is probably to start with
 * sample documents and change them to your liking. This documentation is
 * there to help you understand specific aspects of the document description
 * language as you progress.
 *
 * A number of examples are available as @a templates. To create a new Tao
 * document from a template, click <tt>File > New from Template Chooser...</tt>
 * and follow the wizard. When done, click <tt>View > Document Source</tt> to
 * bring up a simple source code editor.
 * You can edit Tao Presentation documents with any text editor that supports
 * the UTF-8 encoding. Please make sure you do select this format when saving
 * your file, or non-ASCII characters may not show up correctly in Tao.
 *
 * @note Currently, only the main <tt>.ddd</tt> document file can be edited
 * with the bundled editor. Composite documents (see below) should be modified
 * with an external text editor.
 *
 * @section secPlatformNotes Platform Notes
 *
 * @subsection secStdoutStderr Standard Output, Standard Error
 *
 * Errors are normally shown in the application's graphical user interface,
 * either through dialog boxes or in the error display pane. However, some
 * diagnostic message are sent only to the standard output or standard error
 * channels:
 *  - Debug traces,
 *  - Messages sent by the write and writeln builtins.
 *
 * The destination of the standard output and standard error are different
 * if you are using Tao Presentations on MacOSX or on Windows.
 *
 * @subsubsection secWindows Windows
 *
 * The standard output and standard error channels of Tao Presentations are
 * normally redirected to the file @c tao.log in the @c Tao folder under the
 * current users's document folder. This file is overwritten each time the
 * application is started.
 *
 * Errors are normally shown in the application's graphical user interface,
 * either with dialog boxes or in the error display pane. However some
 * diagnostic message are sent only to the @c tao.log %file:
 *  - Debug traces,
 *  - Messages sent by the write and writeln builtins.
 *
 * However, if the application is started from a command prompt the standard
 * output and standard error will remain connected to the command prompt
 * window, and will not be sent to @c tao.log.
 *
 * @subsubsection secMacOSX MacOSX
 *
 * The standard output and standard error channels of Tao Presentations are
 * normally sent to log files and can be seen with the MacOSX @c console
 * application.
 *
 * However, if the application is started from a terminal, standard output
 * and standard error will remain connected to the terminal window, and
 * will not appear in the console.
 *
 * @section secTaoDocuments Tao Presentation documents
 *
 * A Tao document is a text file with the <tt>.ddd</tt> extension. The
 * character encoding is <a href="http://en.wikipedia.org/wiki/UTF-8">UTF-8</a>
 * which enables the use of international characters anywhere in the document.
 *
 * In addition to the main <tt>.ddd</tt> file, a Tao document may be composed
 * of other files:
 *  - Secondary files imported into the main file, in order to make the
 *    document more modular and easier to work with. @n
 *    These files may be <tt>.ddd</tt> files themselves, as long as they are
 *    valid Tao documents on their own. For instance, you may want to subdivide
 *    a long or complex presentation into several shorter ones, and then create
 *    a main <tt>.ddd</tt> file that will simply import all the others. @n
 *    When an imported file cannot be opened as a Tao document on its own,
 *    it is usual to name it with a <tt>.xl</tt> extension to denote that the
 *    file is XL(R) language source.
 *  - Pictures. Typically (but not necessarily) stored under the @c images
 *    subdirectory. Several file formats are supported (PNG, JPEG, and so on;
 *    refer to the @ref image primitive for details).
 *  - Fonts. TrueType or OpenType fonts may be stored in the @c fonts
 *    subdirectory.
 *
 * @subsection subsecDocStructure Document structure
 *
 * Tao documents are written in the Tao language, which enables to describe the
 * structure of the document (by creating items such as: pages, slides, text
 * boxes, rectangles and so on), and at the same time define its dynamic properties
 * such as how the document changes with time or external events (key press
 * or mouse position, for instance).
 *
 * @subsection secXLRef The XLR Language
 *
 * The XLR programming language is the basis of the Tao document syntax.
 * Detailed information is available in <a href="XLRef.pdf">the XLR language
 * reference guide</a>. However, a deep knowledge of XL is not needed to write
 * Tao documents.
 *
 * @subsection secBuiltins Tao Builtins and Primitives
 *
 * A @a builtin is any language construct that is not user-defined. For
 * example, @ref page is a builtin defined by the Tao program.
 *
 * Two categories of builtins can be distinguished:
 *  - @ref TaoBuiltins "Tao builtins" are implemented by the Tao Presentations
 *    program itself, and are imediately available in a document,
 *  - @ref Modules "Module builtins" are defined in @a modules, and require
 *    an @ref import statement before they can be used.
 *
 * When a builtin is implemented in native code (C or C++), it is called a
 * @a primitive.
 *
 * @subsection secExecModel Execution and Drawing Model
 *
 * In order to display a document, Tao @a executes the document (or parts of
 * it). The steps are the following:
 *
 * <ol>
 * <li> @b Compile and @b execute the document once. This step creates a
 *      hierarchy of objects of two kinds: @a Drawings and @a Layouts.
 *      <ul>
 *        <li> A Drawing is anything that may be drawn. For example,
 *             executing the @ref rectangle builtin creates a Drawing object
 *             of type Rectangle.
 *        <li> A Layout is a special kind of drawing that may contain other
 *             Drawings. The following builtins create Layouts: @ref page,
 *             @ref shape, @ref locally, @ref group, @ref anchor, @ref table,
 *             @ref table_cell.
 *      </ul>
 *      In addition to creating objects, the execution of some primitives
 *      makes the containing layout sensitive to some external events. @n
 *      For example, @ref seconds will request re-execution of the
 *      containing layout one second later, while @ref mouse_x will cause
 *      its re-execution upon reception of a <em>mouse move</em> event.
 * <li> @b Draw the Layout and Drawing hierarchy created by the document
 *      execution. @n
 *      For exemple, drawing a Rectangle object consists in displaying this
 *      rectangle on the screen with all the attributes that were selected
 *      during the the program execution (such as: @ref color or
 *      @ref line_color).
 * <li> @b Wait for an external event to occur.
 * <li> @b Dispatch the event from the top to the bottom of the Layout
 *      hierarchy. When a Layout is encountered that has subscribed to the
 *      event, @b re-execute the part of the document that is under that
 *      layout.
 * <li> @b Draw the complete Layout and Drawing hierarchy.
 * </ol>
 *
 * The following example shows how events trigger partial execution.
 *
 * @code
writeln "1: minutes = " & text minutes
locally
    writeln "2: seconds = " & text seconds
locally
    writeln "3: mouse_x = " & text mouse_x
 * @endcode
 *
 * The above code first writes three lines of text, one for each
 * message. This corresponds to the first, complete execution of the
 * document. @n
 * Then, line number 2 shows up every second. Indeed, the @ref seconds
 * builtin has associated a one-second timer to the Layout object created by
 * the first @ref locally block, its inner code is therefore re-executed
 * periodically.
 * Similarly, line number 3 is printed whenever the mouse is moved. Note
 * that moving the mouse does not cause line number 2 to be executed again,
 * and the second changing does not cause line number 3 to be run either. @n
 * When the minute changes, however, all three lines are displayed again.
 * This is because the @ref minutes builtin has attached a one-minute timer
 * to the main Layout object. Since the two @ref locally blocks are nested
 * inside the main Layout of the document, they get re-executed too.
 *
 * @bug [#871] Partial execution not triggered correctly
 */

/**
 * @addtogroup TaoBuiltins Tao Presentations Builtins
 *
 * Core functions for Tao Presentations.
 *
 * Tao documents are written using the XL language. Tao builtins are XL forms
 * that you must use to describe your document. They are further divided into
 * categories as shown above.
 *
 * @todo Document movie / movie_texture?
 * @todo Document Git-related primitives (branch/commit chooser commands and
 *       checkout)?
 * @todo Document "error" primitives?
 * @todo Document "tree management" primitives? (insert, delete_selection,
 *       set_attribute)
 * @todo Document the z-order management primitives? (bring_to_front, etc.)
 * @todo Document the group management primitives? (group, group_selection,
 *       ungroup_selection)
 */
